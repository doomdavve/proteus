#!/usr/bin/env python3
"""P4 authoring protocol and worker lifecycle checks; virtual deadlines, no sleeps."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import plugin_author as author

ROOT = Path(__file__).resolve().parent.parent
WORKER = None
GAME = None


class FakeProcess:
    def __init__(self, args, stdin, stdout, **kwargs):
        self.code = None
        self.killed = False
        self.reaped = False
        self.output = stdout

    def poll(self):
        return self.code

    def kill(self):
        self.killed = True
        self.code = -9

    def wait(self):
        self.reaped = True
        return self.code


class Lifecycle(unittest.TestCase):
    def job(self, **kwargs):
        with patch.object(author.subprocess, "Popen", FakeProcess):
            return author.ValidationJob(WORKER, b"snapshot", 7, 100, **kwargs)

    def test_deadline(self):
        job = self.job(timeout=2)
        self.assertIsNone(job.poll(101))
        result = job.poll(102)
        self.assertEqual(result["status"], "deadline")
        self.assertEqual(result["base_revision"], 7)
        self.assertTrue(job.process.killed and job.process.reaped)
        self.assertTrue(job.input.closed and job.output.closed)
        self.assertIs(result, job.cancel())

    def test_cancel(self):
        job = self.job()
        self.assertEqual(job.cancel()["status"], "cancelled")
        self.assertTrue(job.process.reaped)
        self.assertIs(job.poll(101), job.result)

    def test_bad_worker(self):
        for code, output in [(0, b"garbage"), (-9, b""), (0, b"x" * 8193),
                             (0, b'{"status":"syntax"}'), (0, b'[]')]:
            job = self.job()
            job.process.code = code
            job.output.write(output)
            job.output.flush()
            self.assertEqual(job.poll(101)["status"], "worker_failed")
            self.assertTrue(job.output.closed)

    def test_pin_and_limits(self):
        for kwargs in [{"timeout": 0}, {"timeout": float("nan")},
                       {"timeout": 61}, {"expected_bundle": "stale"}]:
            with self.assertRaises(ValueError):
                self.job(**kwargs)


class CaptureLifecycle(unittest.TestCase):
    def job(self, package="langedrag", **kwargs):
        payload = author.snapshot(ROOT / "plugins" / package)
        with patch.object(author.subprocess, "Popen", FakeProcess):
            return author.CaptureJob("game", payload, 7, 100, **kwargs)

    def test_deadline_and_cleanup(self):
        job = self.job(timeout=2)
        directory = Path(job.directory.name)
        self.assertTrue((directory / "package" / "main.lua").is_file())
        self.assertIsNone(job.poll(101))
        self.assertEqual(job.poll(102)["status"], "deadline")
        self.assertTrue(job.process.killed and job.process.reaped)
        self.assertFalse(directory.exists())
        self.assertTrue(job.log.closed)
        self.assertIs(job.cancel(), job.result)

    def test_bad_game_output(self):
        for code, log, status in [(0, b"", "capture_failed"),
                                  (1, b"Plugin x: syntax error in main.lua:1 : bad", "rejected"),
                                  (1, b"River spawn is not navigable.", "capture_failed"),
                                  (0, b"Plugin x: p R1 ACTIVE 0123456789abcdef", "capture_failed")]:
            job = self.job()
            job.process.code = code
            job.log.write(log)
            if code == 0 and log:
                job.output.write_bytes(b"not a png")
            result = job.poll(101)
            self.assertEqual(result["status"], status, log)
            self.assertNotIn("artifact_id", result)
            self.assertIsNone(job.frame)

    def test_request_limits(self):
        bridge = author.find_anchor("outer.bridge")
        for package, kwargs in [("langedrag", {"timeout": 0}), ("langedrag", {"seconds": 61}),
                                ("langedrag", {"seconds": float("nan")}),
                                ("langedrag", {"camera": {"distance": 7}}),
                                ("langedrag", {"camera": {"height": 37}}),
                                ("langedrag", {"expected_bundle": "stale"}),
                                ("langedrag", {"anchor": author.find_anchor("river.sprint-start")}),
                                ("river-sprint", {"anchor": bridge})]:
            with self.assertRaises(ValueError, msg=(package, kwargs)):
                self.job(package, **kwargs)
        with self.assertRaises(ValueError):
            author.find_anchor("nowhere")

    def test_anchors(self):
        data = author.anchors()
        ids = [anchor["id"] for anchor in data["anchors"]]
        self.assertEqual(len(ids), len(set(ids)))
        for anchor in data["anchors"]:
            self.assertTrue(anchor["id"].startswith(anchor["location"] + "."))
            self.assertIn(anchor["location"], ("outer", "river"))
        self.assertIn("capture", author.capabilities()["operations"])
        self.assertNotIn("capture", author.capabilities()["unsupported"])


class Capture(unittest.TestCase):
    """Real CPU captures; the wait is a failure deadline, not simulated time."""

    def setUp(self):
        if not GAME:
            self.skipTest("needs --game")

    def run_jobs(self, jobs):
        try:
            for job in jobs:
                job.process.wait(timeout=60)
            return [job.poll(0) for job in jobs]
        finally:
            for job in jobs:
                job.close()

    def test_anchors_are_navigable(self):
        payload = author.snapshot(ROOT / "plugins/langedrag")
        outer = [a for a in author.anchors()["anchors"] if a["location"] == "outer"]
        results = self.run_jobs([author.CaptureJob(GAME, payload, 0, 0, anchor, 0, timeout=60)
                                 for anchor in outer])
        for anchor, result in zip(outer, results):
            self.assertEqual(result["status"], "ok", (anchor["id"], result))

    def test_pinned_repeatable_evidence(self):
        payload = author.snapshot(ROOT / "plugins/shelter-tour")
        bridge = author.find_anchor("outer.bridge")
        camera = {"distance": 20, "height": 10}
        jobs = [author.CaptureJob(GAME, payload, 3, 0, bridge, 1, camera, timeout=60)
                for _ in range(2)]
        first, second = self.run_jobs(jobs)
        self.assertEqual(first, second)
        self.assertEqual(first["status"], "ok")
        self.assertEqual(first["evidence"]["plugin_id"], "proteus.shelter-tour")
        self.assertEqual(first["evidence"]["frame"]["width"], 320)
        self.assertEqual(first["scenario"]["ticks"], 120)
        self.assertEqual(jobs[0].frame, jobs[1].frame)
        self.assertEqual(len(first["artifact_id"]), 64)

    def test_rejected_candidate_and_cli(self):
        manifest = (ROOT / "plugins/langedrag/plugin.json").read_bytes()
        source = b"this is invalid lua"
        payload = struct.pack(">II", len(manifest), len(source)) + manifest + source
        result = self.run_jobs([author.CaptureJob(GAME, payload, 0, 0, timeout=60)])[0]
        self.assertEqual(result["status"], "rejected")
        self.assertIn("syntax", result["message"])
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch) / "frame.png"
            output.write_bytes(b"previous")
            pin = author.workspace_read(ROOT / "plugins/langedrag")["bundle_id"]
            run = subprocess.run([sys.executable, str(ROOT / "tools/plugin_author.py"), "capture",
                                  str(ROOT / "plugins/langedrag"), "--game", GAME, "--time", "0",
                                  "--anchor", "outer.dock", "--expect-bundle", pin,
                                  "--output", str(output)], capture_output=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout)
            result = json.loads(run.stdout)
            self.assertEqual(result["bundle_id"], pin)
            frame = output.read_bytes()
            self.assertEqual(author.png_size(frame), (320, 200))
            self.assertEqual(result["evidence"]["frame"]["bytes"], len(frame))
            self.assertEqual([p.name for p in Path(scratch).iterdir()], ["frame.png"])


class Workspace(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        self.directory = Path(self.scratch.name)
        for name in ("plugin.json", "main.lua"):
            (self.directory / name).write_bytes((ROOT / "plugins/langedrag" / name).read_bytes())

    def test_read_write_and_pins(self):
        before = author.workspace_read(self.directory)
        self.assertEqual(before["bundle_id"], author.hashlib.sha256(
            author.snapshot(self.directory)).hexdigest())
        source = (before["files"]["main.lua"] + "\n-- Göteborg\n").encode()
        after = author.workspace_write(self.directory, "main.lua", source, before["bundle_id"])
        self.assertTrue(after["draft"])
        self.assertEqual(after["previous_bundle_id"], before["bundle_id"])
        self.assertEqual(after["files"]["plugin.json"], before["files"]["plugin.json"])
        self.assertEqual((self.directory / "main.lua").read_bytes(), source)
        with self.assertRaises(ValueError):
            author.workspace_write(self.directory, "main.lua", b"stale", before["bundle_id"])
        self.assertEqual(author.workspace_read(self.directory)["bundle_id"], after["bundle_id"])
        self.assertEqual(list(self.directory.glob(".plugin-draft-*")), [])

    def test_rejection_preserves_files(self):
        before = author.workspace_read(self.directory)
        pin = before["bundle_id"]
        for filename, content, expected in [("../escape", b"x", pin),
                ("main.lua", b"x", None), ("main.lua", b"\xff", pin),
                ("main.lua", b"x" * 65537, pin), ("plugin.json", b"x" * 4097, pin)]:
            with self.assertRaises((ValueError, UnicodeError)):
                author.workspace_write(self.directory, filename, content, expected)
            self.assertEqual(author.workspace_read(self.directory), before)
        with patch.object(author.os, "replace", side_effect=OSError("denied")):
            with self.assertRaises(OSError):
                author.workspace_write(self.directory, "main.lua", b"draft", pin)
        self.assertEqual(author.workspace_read(self.directory), before)
        self.assertEqual(list(self.directory.glob(".plugin-draft-*")), [])

    def test_edit_during_preparation(self):
        before = author.workspace_read(self.directory)
        chmod = os.chmod

        def concurrent_edit(path, mode):
            chmod(path, mode)
            (self.directory / "plugin.json").write_bytes(b"external edit")

        with patch.object(author.os, "chmod", side_effect=concurrent_edit):
            with self.assertRaises(ValueError):
                author.workspace_write(self.directory, "main.lua", b"replacement",
                                       before["bundle_id"])
        self.assertEqual((self.directory / "main.lua").read_bytes(),
                         before["files"]["main.lua"].encode("utf-8"))
        self.assertEqual((self.directory / "plugin.json").read_bytes(), b"external edit")
        self.assertEqual(list(self.directory.glob(".plugin-draft-*")), [])

    def test_links_and_special_files(self):
        if os.name == "nt":
            self.skipTest("POSIX links/FIFO fixture")
        pin = author.workspace_read(self.directory)["bundle_id"]
        target = self.directory / "main.lua"
        target.unlink()
        target.symlink_to(self.directory / "plugin.json")
        with self.assertRaises(ValueError):
            author.workspace_write(self.directory, "main.lua", b"draft", pin)
        target.unlink()
        os.mkfifo(target)
        with self.assertRaises(ValueError):
            author.workspace_read(self.directory)

    def test_cli_draft_does_not_evaluate(self):
        def run(*args, content=None):
            return subprocess.run([sys.executable, str(ROOT / "tools/plugin_author.py"),
                *args], input=content, capture_output=True, timeout=10)
        before = json.loads(run("workspace-read", str(self.directory)).stdout)
        args = ("workspace-write", str(self.directory), "--file", "main.lua",
                "--expect-bundle", before["bundle_id"])
        result = run(*args, content=b"not valid Lua")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["files"]["main.lua"], "not valid Lua")
        stale = run(*args, content=b"stale")
        self.assertEqual(stale.returncode, 1)
        self.assertEqual(json.loads(stale.stdout)["status"], "input_error")


class Worker(unittest.TestCase):
    def run_worker(self, payload):
        run = subprocess.run([WORKER], input=payload, capture_output=True, timeout=10)
        self.assertIn(run.returncode, (0, 1), run.stderr)
        return run.returncode, json.loads(run.stdout)

    def test_stuck_process_deadline(self):
        real_popen = subprocess.Popen

        def stuck_worker(args, **kwargs):
            return real_popen([sys.executable, "-c", "while True: pass"], **kwargs)

        with patch.object(author.subprocess, "Popen", stuck_worker):
            job = author.ValidationJob(WORKER, b"snapshot", 1, 0, timeout=1)
        self.assertEqual(job.poll(1)["status"], "deadline")
        self.assertIsNotNone(job.process.returncode)
        self.assertTrue(job.input.closed and job.output.closed)

    def test_init_probe(self):
        manifest = (ROOT / "plugins/shelter-tour/plugin.json").read_bytes()
        source = (ROOT / "plugins/shelter-tour/main.lua").read_bytes()
        source = source.replace(b"local s = {bridge=false", b"error('init probe'); local s = {bridge=false")
        payload = struct.pack(">II", len(manifest), len(source)) + manifest + source
        self.assertEqual(self.run_worker(payload)[1]["status"], "runtime")

    def test_examples_and_job(self):
        for name in ("langedrag", "river-sprint", "shelter-tour"):
            payload = author.snapshot(ROOT / "plugins" / name)
            code, result = self.run_worker(payload)
            self.assertEqual(code, 0, result)
            self.assertEqual(result["status"], "ok")
            self.assertNotEqual(result["gameplay_digest"], "0000000000000000")
            self.assertEqual(result["cost"]["leaked_bytes"], 0)
            job = author.ValidationJob(WORKER, payload, 3, 0)
            try:
                job.process.wait(timeout=10)  # Failure deadline, not simulated time.
                self.assertEqual(job.poll(1)["status"], "ok")
                self.assertEqual(job.result["base_revision"], 3)
            finally:
                job.close()

    def test_framing(self):
        for payload in (b"", struct.pack(">II", 4097, 0), struct.pack(">II", 0, 65537),
                        struct.pack(">II", 1, 1) + b"x", struct.pack(">II", 0, 0) + b"x"):
            self.assertEqual(self.run_worker(payload)[1]["status"], "io")

    def test_lua_failures(self):
        manifest = (ROOT / "plugins/langedrag/plugin.json").read_bytes()
        for source, status in [(b"this is invalid lua", "syntax"),
                               (b"while true do end", "budget"),
                               (b"return {}", "schema"),
                               (b"return io.open('secret')", "runtime")]:
            payload = struct.pack(">II", len(manifest), len(source)) + manifest + source
            self.assertEqual(self.run_worker(payload)[1]["status"], status)

    def test_schema(self):
        data = author.schema()
        self.assertEqual(data["api"], 1)
        for name in ("langedrag", "river-sprint", "shelter-tour"):
            manifest = json.loads((ROOT / "plugins" / name / "plugin.json").read_text(encoding="utf-8"))
            self.assertEqual(set(manifest), set(data["schemas"]["manifest"]["required"]))
        self.assertEqual(data["schemas"]["race"]["properties"]["gates"]["maxItems"], 8)
        self.assertIn("schema", author.capabilities()["operations"])
        self.assertIn("preview", author.capabilities()["operations"])

    def test_schema_native_bounds(self):
        # Every advertised numeric descriptor bound is exercised against the native decoder.
        def leaves(node, path):
            if node.get("type") in ("number", "integer"):
                yield path, node
            for key, child in node.get("properties", {}).items():
                yield from leaves(child, path + "." + key)
            if "items" in node:
                yield from leaves(node["items"], path + "[1]")

        for kind, package in (("voyage", "langedrag"), ("race", "river-sprint")):
            manifest = (ROOT / "plugins" / package / "plugin.json").read_bytes()
            original = (ROOT / "plugins" / package / "main.lua").read_text(encoding="utf-8")
            for path, node in leaves(author.schema()["schemas"][kind], kind):
                for bound, direction in (("minimum", -1), ("maximum", 1)):
                    value = node[bound] + direction * max(1, abs(node[bound]) * .1)
                    source = ("local p = (function()\n" + original + "\nend)(); p."
                              + path + " = " + repr(value) + "; return p").encode()
                    code, result = self.run_worker(struct.pack(">II", len(manifest), len(source))
                                                   + manifest + source)
                    self.assertEqual(code, 1, (path, bound, result))
                    self.assertEqual(result["status"], "schema", (path, bound, result))
                    self.assertEqual(result["field"], path, (bound, result))

    def test_preview(self):
        for name in ("langedrag", "shelter-tour", "river-sprint"):
            payload = author.snapshot(ROOT / "plugins" / name)
            evidence = []
            for _ in range(2):
                job = author.ValidationJob(WORKER, payload, 3, 0, operation="preview", ticks=360)
                try:
                    job.process.wait(timeout=10)  # Failure deadline, no simulation clock.
                    self.assertEqual(job.poll(1)["status"], "ok", job.result)
                    self.assertEqual(job.result["operation"], "preview")
                    self.assertEqual(len(job.result["artifact_id"]), 64)
                    evidence.append(job.result)
                finally:
                    job.close()
            self.assertEqual(evidence[0], evidence[1])
        for ticks in (-1, 72001, 1.5):
            with self.assertRaises(ValueError):
                author.ValidationJob(WORKER, b"", 0, 0, operation="preview", ticks=ticks)

    def test_preview_callback_failure(self):
        manifest = (ROOT / "plugins/shelter-tour/plugin.json").read_bytes()
        source = (ROOT / "plugins/shelter-tour/main.lua").read_bytes()
        source = source.replace(b"update = function(c, s)", b"update = function(c, s) error('preview update')")
        self.assertIn(b"preview update", source)
        run = subprocess.run([WORKER, "--preview", "120"],
            input=struct.pack(">II", len(manifest), len(source)) + manifest + source,
            capture_output=True, timeout=10)
        self.assertEqual(run.returncode, 1)
        self.assertEqual(json.loads(run.stdout)["status"], "runtime")

    def test_snapshot(self):
        with tempfile.TemporaryDirectory() as scratch:
            directory = Path(scratch)
            for name in ("plugin.json", "main.lua"):
                (directory / name).write_bytes(b"a")
            before = author.snapshot(directory)
            (directory / "main.lua").write_bytes(b"b")
            after = author.snapshot(directory)
            self.assertNotEqual(before, after)
            job = author.ValidationJob(WORKER, before, 0, 0)
            pin = job.bundle
            job.close()
            with self.assertRaises(ValueError):
                author.ValidationJob(WORKER, after, 0, 0, expected_bundle=pin)
            (directory / "main.lua").write_bytes(b"a" * 65537)
            with self.assertRaises(ValueError):
                author.snapshot(directory)
            if os.name != "nt":
                (directory / "main.lua").unlink()
                (directory / "main.lua").symlink_to(directory / "plugin.json")
                with self.assertRaises(ValueError):
                    author.snapshot(directory)
                (directory / "main.lua").unlink()
                os.mkfifo(directory / "main.lua")
                with self.assertRaises(ValueError):
                    author.snapshot(directory)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", required=True)
    parser.add_argument("--game")
    args, remaining = parser.parse_known_args()
    WORKER = str(Path(args.worker).resolve())
    GAME = str(Path(args.game).resolve()) if args.game else None
    unittest.main(argv=[__file__] + remaining)
