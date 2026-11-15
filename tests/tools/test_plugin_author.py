#!/usr/bin/env python3
"""P4 authoring protocol and worker lifecycle checks; virtual deadlines, no sleeps."""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))  # The tool under test.
import plugin_author as author

ROOT = Path(__file__).resolve().parents[2]
WORKER = None
GAME = None
BRIDGE_SERVER = None


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
            self.assertEqual(anchor["location"], "outer")
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


def bridge_reply(directory, request_id, session, **fields):
    lines = ["wake-velocity-bridge 1", f"id={request_id}", f"session={session}"]
    lines += [f"{key}={value}" for key, value in fields.items()]
    # Bytes, not text: Windows text mode would write CRLF, which the protocol does not use.
    (Path(directory) / f"response-{request_id}").write_bytes(("\n".join(lines) + "\n").encode())


class BridgeLifecycle(unittest.TestCase):
    """The client against a fake game writing responses; virtual deadlines."""

    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.directory = Path(self.scratch.name)
        (self.directory / "session").write_bytes(
            b"wake-velocity-bridge 1\nsession=00aa\ntoken=" + b"7" * 32 + b"\npid=1\n")

    def tearDown(self):
        self.scratch.cleanup()

    def test_request_and_response(self):
        payload = author.snapshot(ROOT / "plugins/langedrag")
        bundle = author.hashlib.sha256(payload).hexdigest()
        job = author.BridgeJob(self.directory, "stage", 100, 2,
                               {"base_revision": 3, "base_epoch": 2}, payload, bundle)
        request = (self.directory / "request").read_bytes()
        header, _, body = request.partition(b"\n\n")
        self.assertEqual(body, payload)
        self.assertEqual(header.decode().split("\n"), [
            "wake-velocity-bridge 1", f"id={job.id}", "token=" + "7" * 32, "operation=stage",
            "base_revision=3", "base_epoch=2", f"payload={len(payload)}"])
        self.assertEqual(sorted(p.name for p in self.directory.iterdir()), ["request", "session"])
        with self.assertRaisesRegex(ValueError, "busy"):
            author.BridgeJob(self.directory, "inspect", 100)
        self.assertIsNone(job.poll(101))
        bridge_reply(self.directory, job.id, "00aa", status="ok", outcome="held", revision=3,
                     held_revision=4, held_change="restart_voyage", boat_x="1.5",
                     inspect="revision=3 schema=1")
        result = job.poll(101.5)
        self.assertEqual(result["status"], "ok")
        self.assertEqual((result["revision"], result["held_revision"]), (3, 4))
        self.assertEqual(result["boat_x"], 1.5)
        self.assertEqual(result["inspection"], ["revision=3 schema=1"])
        self.assertEqual(result["bundle_id"], bundle)
        self.assertEqual(result["pins"], {"base_revision": 3, "base_epoch": 2})
        self.assertFalse((self.directory / f"response-{job.id}").exists())

    def test_deadline_rejection_and_foreign_response(self):
        job = author.BridgeJob(self.directory, "inspect", 100, 2)
        self.assertEqual(job.poll(102)["status"], "deadline")
        (self.directory / "request").unlink()
        job = author.BridgeJob(self.directory, "undo", 0, 2, {"base_revision": 1, "base_epoch": 0})
        bridge_reply(self.directory, job.id, "00aa", status="ok", outcome="rejected")
        self.assertEqual(job.poll(1)["status"], "rejected")
        (self.directory / "request").unlink()
        job = author.BridgeJob(self.directory, "inspect", 0, 2)
        bridge_reply(self.directory, job.id, "other", status="ok")
        self.assertEqual(job.poll(1)["status"], "bridge_failed")

    def test_pins(self):
        payload = author.snapshot(ROOT / "plugins/langedrag")
        with self.assertRaisesRegex(ValueError, "expect-bundle"):
            author.BridgeJob(self.directory, "stage", 0, 2, {}, payload)
        with self.assertRaisesRegex(ValueError, "changed"):
            author.BridgeJob(self.directory, "stage", 0, 2, {}, payload, "0" * 64)
        with self.assertRaises(ValueError):
            author.BridgeJob(self.directory, "apply", 0, 2, {"held_revision": -1})
        self.assertFalse((self.directory / "request").exists())
        run = subprocess.run([sys.executable, str(ROOT / "tools/plugin_author.py"), "live-undo",
                              "--bridge", str(self.directory), "--base-epoch", "0"],
                             capture_output=True, timeout=30)
        self.assertEqual(json.loads(run.stdout)["status"], "input_error")
        self.assertIn("--base-revision", json.loads(run.stdout)["message"])


class Bridge(unittest.TestCase):
    """The CLI against the game's bridge over a still session (validate_wake_bridge --serve)."""

    def setUp(self):
        if not BRIDGE_SERVER:
            self.skipTest("needs --bridge-server")
        self.scratch = tempfile.TemporaryDirectory()
        self.mailbox = Path(tempfile.mkdtemp(dir=self.scratch.name))  # Private, mode 0700.
        self.package = Path(self.scratch.name) / "package"
        self.package.mkdir()
        for name in ("plugin.json", "main.lua"):
            (self.package / name).write_bytes((ROOT / "plugins/langedrag" / name).read_bytes())
        self.server = subprocess.Popen([BRIDGE_SERVER, "--serve", str(self.mailbox)], cwd=ROOT,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        while not (self.mailbox / "session").exists():  # Startup is external; CTest bounds it.
            self.assertIsNone(self.server.poll())

    def tearDown(self):
        (self.mailbox / "stop").write_text("")
        self.server.wait(timeout=30)
        self.scratch.cleanup()

    def live(self, action, *options, directory=None):
        command = [sys.executable, str(ROOT / "tools/plugin_author.py"), "live-" + action]
        command += [str(directory)] if directory else []
        command += ["--bridge", str(self.mailbox), *options]
        run = subprocess.run(command, capture_output=True, timeout=30)
        return json.loads(run.stdout), run.returncode

    def edit(self, old, new):
        pin = author.workspace_read(self.package)["bundle_id"]
        source = (self.package / "main.lua").read_bytes().replace(old, new)
        return author.workspace_write(self.package, "main.lua", source, pin)["bundle_id"]

    def test_inspect_stage_apply_undo(self):
        result, code = self.live("inspect")
        self.assertEqual((code, result["status"], result["revision"]), (0, "ok", 1))
        self.assertEqual(result["plugin"], "proteus.langedrag")
        self.assertIn("state=", result["inspection"])
        result, code = self.live("console", "--base-epoch", "0", "--line", "/capabilities")
        self.assertEqual((code, result["status"]), (0, "ok"))
        self.assertIn("voyage.define", result["output"])
        pin = self.edit(b'name = "BRIDGE"', b'name = "ALVSBORG"')
        stage = ("--base-revision", "1", "--base-epoch", "0", "--expect-bundle", pin)
        result, code = self.live("stage", *stage, directory=self.package)
        self.assertEqual((code, result["outcome"], result["revision"]), (0, "applied", 2))
        self.assertEqual(result["bundle_id"], pin)
        result, code = self.live("stage", *stage, directory=self.package)
        self.assertEqual((code, result["status"], result["revision"]), (1, "stale", 2))
        # PC: a route edit installs live and keeps the boat; no Apply is needed.
        pin = self.edit(b"radius = 150", b"radius = 120")
        result, _ = self.live("stage", "--base-revision", "2", "--base-epoch", "1",
                              "--expect-bundle", pin, directory=self.package)
        self.assertEqual((result["outcome"], result["revision"]), ("applied", 3))
        # A water change is still held, and Apply must name its revision.
        pin = self.edit(b"height = 1.6", b"height = 1.2")
        result, _ = self.live("stage", "--base-revision", "3", "--base-epoch", "2",
                              "--expect-bundle", pin, directory=self.package)
        self.assertEqual((result["outcome"], result["held_change"]), ("held", "restart_session"))
        result, _ = self.live("apply", "--held-revision", "4", "--base-epoch", "2")
        self.assertEqual((result["outcome"], result["revision"]), ("applied", 4))
        # Undo holds the previous water revision; Discard drops it.
        result, _ = self.live("undo", "--base-revision", "4", "--base-epoch", "3")
        self.assertEqual((result["outcome"], result["held_revision"], result["held_undo"]),
                         ("held", 3, 1))
        result, _ = self.live("discard", "--base-epoch", "3")
        self.assertEqual((result["outcome"], result["held_revision"]), ("discarded", 0))
        pin = self.edit(b"max_speed = 6", b"max_speed = 'six'")
        result, code = self.live("stage", "--base-revision", "4", "--base-epoch", "3",
                                 "--expect-bundle", pin, directory=self.package)
        self.assertEqual((code, result["status"], result["revision"]), (1, "rejected", 4))
        self.assertEqual(result["diagnostic_field"], "voyage.arrival.max_speed")


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


FIXTURE = ROOT / "tests/support/fixtures/plugins/modules-route"
GOLDEN = ROOT / "tests/support/fixtures/plugin_image_golden.hex"


class Modules(unittest.TestCase):
    """M5 W4: packages with modules through snapshot, drafts, version 2 bundles and the worker."""

    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        self.directory = Path(self.scratch.name) / "package"
        shutil.copytree(FIXTURE, self.directory)

    def tool(self, *args, content=None):
        return subprocess.run([sys.executable, str(ROOT / "tools/plugin_author.py"), *args],
                              input=content, capture_output=True, timeout=30)

    def test_golden_image(self):
        # The same three files and bytes the C packer and web/plugin_author.js are held to.
        files = [("main.lua", b"return {}\n"), ("marina", "return 'G\u00f6teborg'\n".encode()),
                 ("marina.berths", b"return {}\n")]
        image = author.pack_image(files)
        self.assertEqual(image.hex(), GOLDEN.read_text().strip())
        self.assertEqual(author.unpack_image(image), files)
        for bad in (image[:-1], image + b"\0", b"x" + image[1:], image[:5] + b"\x04" + image[6:],
                    image[:5] + b"\x00" + image[6:]):
            with self.assertRaises(ValueError):
                author.unpack_image(bad)
        with self.assertRaises(ValueError):
            author.pack_image([("main.lua", b""), ("main.lua", b"")])

    def test_snapshot_carries_modules(self):
        payload = author.snapshot(self.directory)
        manifest, entry, modules = author.split_snapshot(payload)
        self.assertEqual(list(modules), ["route", "route.arrival"])
        self.assertEqual(modules["route.arrival"],
                         (self.directory / "modules/route/arrival.lua").read_bytes())
        size = struct.unpack(">I", payload[4:8])[0]
        self.assertTrue(author.image_is(payload[len(payload) - size:]))
        # A module-only edit is a different snapshot, so a different pin.
        (self.directory / "modules/route.lua").write_bytes(b"return 1\n")
        self.assertNotEqual(author.snapshot(self.directory), payload)
        # A package without modules is unchanged: the raw entry bytes.
        plain = Path(self.scratch.name) / "plain"
        shutil.copytree(ROOT / "plugins/langedrag", plain)
        raw = author.snapshot(plain)
        self.assertEqual(raw[len(raw) - (plain / "main.lua").stat().st_size:],
                         (plain / "main.lua").read_bytes())

    def test_worker_validates_modules(self):
        job = author.ValidationJob(WORKER, author.snapshot(self.directory), 0, 0)
        try:
            job.process.wait(timeout=10)  # Failure deadline, not simulated time.
            self.assertEqual(job.poll(1)["status"], "ok")
        finally:
            job.close()
        (self.directory / "modules/route.lua").write_bytes(b"local x = = 1\n")
        job = author.ValidationJob(WORKER, author.snapshot(self.directory), 0, 0)
        try:
            job.process.wait(timeout=10)
            self.assertNotEqual(job.poll(1)["status"], "ok")
            self.assertIn("modules/route.lua", json.dumps(job.result))
        finally:
            job.close()

    def test_read_refusals(self):
        (self.directory / "modules/route.lua").unlink()
        with self.assertRaisesRegex(ValueError, "modules/route.lua: missing"):
            author.snapshot(self.directory)
        (self.directory / "modules/route.lua").write_bytes(b"x" * 70000)
        with self.assertRaisesRegex(ValueError, "modules/route.lua is larger than"):
            author.snapshot(self.directory)
        (self.directory / "modules/route.lua").write_bytes(b"return 1\n")
        if os.name != "nt":
            (self.directory / "modules/route").rename(self.directory / "modules/real")
            (self.directory / "modules/route").symlink_to(self.directory / "modules/real")
            with self.assertRaisesRegex(ValueError, "symbolic links"):
                author.snapshot(self.directory)

    def test_shared_budget_and_limits(self):
        manifest = (self.directory / "plugin.json").read_bytes().decode()
        entry = b"-- entry\n"
        edge = 65536 - 6 - (1 + 8 + 4 + len(entry)) - (1 + 5 + 4) - (1 + 13 + 4)
        ok = {"route": b"x" * edge, "route.arrival": b""}
        self.assertEqual(len(author.package_source(manifest, entry, ok)), 65536)
        ok["route"] += b"x"
        with self.assertRaisesRegex(ValueError, "modules/route/arrival.lua is larger than the 0 bytes"):
            author.package_source(manifest, entry, ok)
        with self.assertRaisesRegex(ValueError, "route.arrival.*missing"):
            author.package_source(manifest, entry, {"route": b""})
        with self.assertRaisesRegex(ValueError, "does not declare"):
            author.package_source(manifest, entry, {"route": b"", "route.arrival": b"", "x": b""})
        for names, message in ((["a"] * 2, "repeats modules"), (["Bad"], "dot-separated"),
                               (["main"], "must not be"), (["a.b.c.d"], "dot-separated"),
                               ([f"m{i}" for i in range(9)], "at most 8"),
                               (["a" * 33], "dot-separated"), ("x", "array")):
            with self.assertRaisesRegex(ValueError, message):
                author.declared_modules(json.dumps({"modules": names}))
        self.assertEqual(len(author.declared_modules(
            json.dumps({"modules": [f"m{i}" for i in range(8)]}))), 8)

    def test_bundle_round_trip(self):
        bundle = author.export_bundle(self.directory)
        self.assertEqual(sorted(bundle), ["format", "manifest", "modules", "source", "version"])
        self.assertEqual(bundle["version"], 2)
        self.assertEqual(list(bundle["modules"]), ["route", "route.arrival"])
        text = json.dumps(bundle)
        target = Path(self.scratch.name) / "imported"
        author.import_bundle(text, target)
        for path in ("plugin.json", "main.lua", "modules/route.lua", "modules/route/arrival.lua"):
            self.assertEqual((target / path).read_bytes(), (self.directory / path).read_bytes())
        self.assertEqual(author.snapshot(target), author.snapshot(self.directory))
        with self.assertRaisesRegex(ValueError, "already exists"):
            author.import_bundle(text, target)
        # The command line: export, then import.
        exported = self.tool("export", str(self.directory))
        self.assertEqual(exported.returncode, 0, exported.stderr)
        other = Path(self.scratch.name) / "cli"
        run = self.tool("import", str(other), content=exported.stdout)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(author.snapshot(other), author.snapshot(self.directory))
        # A package without modules still exports with an empty modules object.
        plain = author.export_bundle(ROOT / "plugins/langedrag")
        self.assertEqual(plain["modules"], {})
        self.assertEqual(author.capabilities()["browser"]["bundle_version"], 2)

    def test_bundle_refusals(self):
        bundle = author.export_bundle(self.directory)
        def refuse(data, message):
            with self.assertRaisesRegex(ValueError, message):
                author.bundle_package(json.dumps(data))
        refuse({**bundle, "version": 1}, "version 1 bundles are not supported; re-export")
        refuse({k: v for k, v in bundle.items() if k != "modules"} | {"version": 1},
               "version 1 bundles are not supported")
        refuse({**bundle, "version": 3}, "Expected a Wake Velocity version 2")
        refuse({k: v for k, v in bundle.items() if k != "modules"}, "Expected a Wake Velocity")
        refuse({**bundle, "extra": 1}, "Expected a Wake Velocity")
        refuse({**bundle, "modules": {"route": "x"}}, "route.arrival.*missing")
        refuse({**bundle, "modules": {**bundle["modules"], "other": ""}}, "does not declare")
        refuse({**bundle, "modules": []}, "Expected source text")
        refuse({**bundle, "modules": {**bundle["modules"], "route": 1}}, "Expected source text")
        refuse({**bundle, "manifest": bundle["manifest"].replace('"modules"', '"modules": [], "x"')},
               "Expected|does not declare")
        refuse({**bundle, "modules": {**bundle["modules"], "route": "x" * 70000}}, "larger than")
        with self.assertRaisesRegex(ValueError, "200 KB"):
            author.bundle_package(" " * 200001)
        # A bundle that lists its modules in another order imports in the manifest's order.
        reordered = {**bundle, "modules": dict(reversed(list(bundle["modules"].items())))}
        _, _, modules = author.bundle_package(json.dumps(reordered))
        self.assertEqual(list(modules), ["route", "route.arrival"])

    def test_workspace_modules(self):
        before = author.workspace_read(self.directory)
        self.assertIn("modules/route/arrival.lua", before["files"])
        edit = (before["files"]["modules/route/arrival.lua"] + "-- edited\n").encode()
        after = author.workspace_write(self.directory, "modules/route/arrival.lua", edit,
                                       before["bundle_id"])
        self.assertEqual((self.directory / "modules/route/arrival.lua").read_bytes(), edit)
        self.assertEqual(after["changed_file"], "modules/route/arrival.lua")
        self.assertNotEqual(after["bundle_id"], before["bundle_id"])
        self.assertEqual(list(self.directory.rglob(".plugin-draft-*")), [])
        for filename in ("modules/undeclared.lua", "modules/../main.lua", "modules/route.lua/x"):
            with self.assertRaises(ValueError):
                author.workspace_write(self.directory, filename, b"x", after["bundle_id"])
        # A manifest that declares a module with no file is refused before anything changes.
        manifest = json.loads(after["files"]["plugin.json"])
        manifest["modules"].append("absent")
        with self.assertRaisesRegex(ValueError, "modules/absent.lua: missing"):
            author.workspace_write(self.directory, "plugin.json",
                                   json.dumps(manifest).encode(), after["bundle_id"])
        # An existing file is declared by editing plugin.json; oversize totals are refused.
        (self.directory / "modules/extra.lua").write_bytes(b"return {}\n")
        manifest["modules"] = ["route", "route.arrival", "extra"]
        declared = author.workspace_write(self.directory, "plugin.json",
                                          json.dumps(manifest).encode(), after["bundle_id"])
        self.assertIn("modules/extra.lua", declared["files"])
        for size, message in ((70000, "exceeds"), (65400, "larger than")):
            with self.assertRaisesRegex(ValueError, message):
                author.workspace_write(self.directory, "modules/extra.lua", b"x" * size,
                                       declared["bundle_id"])
        run = self.tool("workspace-write", str(self.directory), "--file", "modules/extra.lua",
                        "--expect-bundle", declared["bundle_id"], content=b"return 2\n")
        self.assertEqual(run.returncode, 0, run.stderr)

    def test_capture_writes_modules(self):
        captured = {}
        real = author.write_package

        def spy(directory, *rest):
            real(directory, *rest)
            captured["files"] = sorted(str(p.relative_to(directory))
                                       for p in Path(directory).rglob("*.lua"))

        with patch.object(author, "write_package", spy), \
                patch.object(author.subprocess, "Popen", side_effect=OSError("no game")):
            with self.assertRaises(OSError):
                author.CaptureJob("game", author.snapshot(self.directory), 0, 0)
        self.assertEqual(captured["files"], ["main.lua", "modules/route.lua",
                                             "modules/route/arrival.lua"])


class Packaged(unittest.TestCase):
    """A release layout from package_plugin_tools, driven from outside the checkout."""

    def test_relocated_tools(self):
        import package_plugin_tools

        self.assertEqual(author.capabilities()["layout"], {"kind": "source"})
        # Layout paths are relative, so a real GAME needs the layout on its drive (Windows).
        near = Path(GAME).resolve().parent if GAME else None
        with tempfile.TemporaryDirectory(prefix="plugin tools ", dir=near) as scratch:
            package = Path(scratch) / "package"
            authoring = package / "authoring"
            # GAME stays in the build tree so capture finds its assets; it is optional here.
            game = GAME or package / author.executable("wake_velocity")
            package_plugin_tools.install(authoring, WORKER,
                                         package / author.executable("boat_plugin_worker"),
                                         game, package / "assets/plugins", "test", "dev")

            def tool(*args):
                run = subprocess.run([sys.executable, str(authoring / "plugin_author.py"), *args],
                                     cwd=scratch, capture_output=True, timeout=30)
                return run.returncode, json.loads(run.stdout)

            code, data = tool("capabilities")
            self.assertEqual(code, 0)
            self.assertEqual(data["layout"], {"kind": "package", "package": "test",
                                              "version": "dev"})
            for path in data["examples"] + [data["anchors"], data["api_document"]]:
                self.assertTrue(Path(path).exists(), path)
            self.assertEqual(tool("anchors")[1], author.anchors())
            # The shared marina travels as each example's own module copy; _shared is no package.
            installed = package / "assets/plugins"
            for name in ("harbor-life", "harbor-room"):
                self.assertEqual((installed / name / "modules/eriksberg_marina.lua").read_bytes(),
                                 (ROOT / "plugins/_shared/eriksberg_marina.lua").read_bytes())
            self.assertFalse((installed / "_shared").exists())
            self.assertEqual(sorted(p.name for p in installed.iterdir() if p.name.startswith("_")), [])
            self.assertFalse(any(Path(example).name.startswith("_") for example in data["examples"]))
            for example in data["examples"]:
                # A loaded Linux runner can exceed the product's 5 s default during the
                # harbour-life traffic sweep; this check is about the relocated package.
                code, result = tool("validate", example, "--timeout", "20")
                self.assertEqual((code, result["status"]), (0, "ok"), result)
            code, result = tool("preview", data["examples"][2], "--ticks", "12")
            self.assertEqual((code, result["status"]), (0, "ok"), result)
            if GAME:
                frame = Path(scratch) / "frame.png"
                code, result = tool("capture", data["examples"][0], "--time", "0",
                                    "--output", str(frame))
                self.assertEqual((code, result["status"]), (0, "ok"), result)
                self.assertEqual(author.png_size(frame.read_bytes()), (320, 200))
            # Signed app bundles must not gain bytecode caches.
            self.assertFalse((authoring / "__pycache__").exists())
            layout = authoring / "plugin_tools.json"
            layout.write_text(layout.read_text().replace('"layout": 1', '"layout": 2'))
            run = subprocess.run([sys.executable, str(authoring / "plugin_author.py"),
                                  "capabilities"], capture_output=True, text=True, timeout=30)
            self.assertNotEqual(run.returncode, 0)
            self.assertIn("unsupported plugin tools layout", run.stderr)


class SharedModules(unittest.TestCase):
    """M5 W5: sync-shared, and harbor-life and harbor-room as self-contained module packages."""

    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        self.plugins = Path(self.scratch.name) / "plugins"
        (self.plugins / "_shared").mkdir(parents=True)
        (self.plugins / "_shared/marina.lua").write_bytes(b"return { a = 1 }\n")
        for name, modules in (("one", '["marina"]'), ("two", '["marina", "local"]'), ("bare", "[]")):
            package = self.plugins / name
            (package / "modules").mkdir(parents=True)
            (package / "plugin.json").write_text('{"modules": %s}' % modules)
            (package / "main.lua").write_bytes(b"return {}\n")
        (self.plugins / "two/modules/local.lua").write_bytes(b"return 1\n")
        (self.plugins / "two/modules/marina.lua").write_bytes(b"return { a = 2 }\n")

    def tool(self, *args):
        run = subprocess.run([sys.executable, str(ROOT / "tools/plugin_author.py"), *args],
                             capture_output=True, timeout=30)
        return run.returncode, json.loads(run.stdout)

    def test_sync_shared(self):
        self.assertIn("sync-shared", author.capabilities()["operations"])
        # --check reports every drifted or missing copy, names them and writes nothing.
        code, result = self.tool("sync-shared", str(self.plugins), "--check")
        self.assertEqual(code, 1)
        self.assertEqual((result["status"], result["checked"], result["changed"]), ("drift", 2, []))
        self.assertEqual(result["drifted"], ["one/modules/marina.lua", "two/modules/marina.lua"])
        self.assertFalse((self.plugins / "one/modules/marina.lua").exists())
        self.assertEqual((self.plugins / "two/modules/marina.lua").read_bytes(), b"return { a = 2 }\n")
        # Without --check the copies are created or rewritten; unshared modules stay.
        code, result = self.tool("sync-shared", str(self.plugins))
        self.assertEqual((code, result["status"], result["changed"]), (0, "ok", result["drifted"]))
        for name in ("one", "two"):
            self.assertEqual((self.plugins / name / "modules/marina.lua").read_bytes(),
                             b"return { a = 1 }\n")
        self.assertEqual((self.plugins / "two/modules/local.lua").read_bytes(), b"return 1\n")
        self.assertFalse((self.plugins / "bare/modules/marina.lua").exists())
        code, result = self.tool("sync-shared", str(self.plugins), "--check")
        self.assertEqual((code, result["status"], result["drifted"]), (0, "ok", []))
        # _shared is never a package: a stray manifest there does not make it a sync target.
        (self.plugins / "_shared/plugin.json").write_text('{"modules": ["marina"]}')
        self.assertEqual(self.tool("sync-shared", str(self.plugins), "--check")[1]["checked"], 2)
        # An undeclared file with a shared name is a copy too.
        (self.plugins / "bare/modules/marina.lua").write_bytes(b"x")
        self.assertEqual(self.tool("sync-shared", str(self.plugins), "--check")[1]["drifted"],
                         ["bare/modules/marina.lua"])
        code, result = self.tool("sync-shared", str(self.plugins / "missing"))
        self.assertEqual((code, result["status"]), (1, "input_error"))

    def test_shipped_copies_are_in_sync(self):
        code, result = self.tool("sync-shared", "--check")
        self.assertEqual((code, result["status"], result["drifted"]), (0, "ok", []), result)
        self.assertGreaterEqual(result["checked"], 2)

    def test_marina_packages_bundle_and_round_trip(self):
        shared = (ROOT / "plugins/_shared/eriksberg_marina.lua").read_bytes()
        for name in ("harbor-life", "harbor-room"):
            source = ROOT / "plugins" / name
            self.assertEqual(json.loads((source / "plugin.json").read_text())["modules"],
                             ["eriksberg_marina"])
            self.assertEqual((source / "modules/eriksberg_marina.lua").read_bytes(), shared)
            bundle = author.export_bundle(source)
            self.assertEqual(bundle["version"], 2)
            self.assertEqual(bundle["modules"], {"eriksberg_marina": shared.decode("utf-8")})
            target = Path(self.scratch.name) / name
            author.import_bundle(json.dumps(bundle), target)
            self.assertEqual(author.snapshot(target), author.snapshot(source))
            self.assertEqual((target / "modules/eriksberg_marina.lua").read_bytes(), shared)
            # The entry is small; the scene is not in it.
            self.assertNotIn(b"berths", (target / "main.lua").read_bytes())
            code, result = self.worker_check(target)
            self.assertEqual((code, result["status"]), (0, "ok"), result)
            _, original = self.worker_check(source)
            self.assertEqual(result["gameplay_digest"], original["gameplay_digest"])

    def test_integrated_marina_example(self):
        """M5 W6: four modules (one a shared copy, two nested), exported, imported and staged."""
        source = ROOT / "plugins/marina-integrated-demo"
        manifest = json.loads((source / "plugin.json").read_text())
        self.assertEqual(manifest["modules"],
                         ["eriksberg_marina", "marina", "guests.routes", "guests.services"])
        for name in manifest["modules"]:
            self.assertTrue((source / author.module_file(name)).is_file(), name)
        self.assertEqual((source / "modules/eriksberg_marina.lua").read_bytes(),
                         (ROOT / "plugins/_shared/eriksberg_marina.lua").read_bytes())
        bundle = author.export_bundle(source)
        self.assertEqual(bundle["version"], 2)
        self.assertEqual(sorted(bundle["modules"]), sorted(manifest["modules"]))
        target = Path(self.scratch.name) / "integrated"
        author.import_bundle(json.dumps(bundle), target)
        self.assertEqual(author.snapshot(target), author.snapshot(source))
        self.assertTrue((target / "modules/guests/services.lua").is_file())
        code, result = self.worker_check(target)
        self.assertEqual((code, result["status"]), (0, "ok"), result)
        self.assertEqual(result["scene"]["berths"], 14)
        self.assertEqual(result["scene"]["modules"], 15)
        self.assertEqual(result["scene"]["vessels"], 6)
        _, original = self.worker_check(source)
        self.assertEqual(result["scene"]["digest"], original["scene"]["digest"])
        # A module-only edit changes the package's identity and its scene; a broken module is
        # reported with its file.
        edited = Path(self.scratch.name) / "edited"
        author.import_bundle(json.dumps(bundle), edited)
        services = edited / "modules/guests/services.lua"
        services.write_text(services.read_text().replace("start_tick = 240", "start_tick = 120"))
        self.assertNotEqual(author.snapshot(edited), author.snapshot(source))
        code, result = self.worker_check(edited)
        self.assertEqual((code, result["status"]), (0, "ok"), result)
        self.assertNotEqual(result["scene"]["digest"], original["scene"]["digest"])
        services.write_text(services.read_text().replace("start_tick = 120", "start_tick = 360"))
        code, result = self.worker_check(edited)
        self.assertEqual((code, result["status"]), (1, "schema"), result)
        self.assertIn("crosses scene.traffic.vessels", result["message"])
        routes = edited / "modules/guests/routes.lua"
        routes.write_text(routes.read_text().replace("local z = 231", "local z = = 231"))
        code, result = self.worker_check(edited)
        self.assertEqual((code, result["status"], result["file"]),
                         (1, "syntax", "modules/guests/routes.lua"), result)

    def worker_check(self, directory):
        run = subprocess.run([WORKER], input=author.snapshot(directory), capture_output=True,
                             timeout=20)
        self.assertIn(run.returncode, (0, 1), run.stderr)
        return run.returncode, json.loads(run.stdout)


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
        for name in ("langedrag", "harbor-life", "river-sprint", "shelter-tour", "marina-route-demo", "marina-berth-demo",
                     "marina-integrated-demo"):
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

    def test_scene_evidence(self):
        payload = author.snapshot(ROOT / "plugins/harbor-life")
        code, result = self.run_worker(payload)
        self.assertEqual(code, 0, result)
        self.assertEqual(result["scene"], {"present": True, "version": 3,
            "digest": result["scene"]["digest"], "content": True,
            "subsystems": ["marina.berths", "marina.modules", "traffic.vessels", "gulls.flocks"], "berths": 12,
            "modules": 13, "vessels": 4, "flocks": 1})
        self.assertNotEqual(result["scene"]["digest"], "0000000000000000")
        job = author.ValidationJob(WORKER, payload, 3, 0, operation="preview", ticks=12)
        try:
            job.process.wait(timeout=10)
            self.assertEqual(job.poll(1)["status"], "ok", job.result)
            self.assertEqual(job.result["evidence"]["scene"]["berths"], 12)
            self.assertEqual(job.result["evidence"]["scene"]["modules"], 13)
            self.assertEqual(job.result["evidence"]["scene"]["vessels"], 4)
            self.assertEqual(job.result["evidence"]["scene"]["flocks"], 1)
        finally:
            job.close()
        default = self.run_worker(author.snapshot(ROOT / "plugins/langedrag"))[1]
        self.assertFalse(default["scene"]["present"])
        self.assertEqual(default["scene"]["berths"], 0)

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
        for name in ("langedrag", "harbor-life", "river-sprint", "shelter-tour"):
            manifest = json.loads((ROOT / "plugins" / name / "plugin.json").read_text(encoding="utf-8"))
            # "modules" is optional; harbor-life declares one (M5 W5).
            self.assertEqual(set(manifest) - {"modules"},
                             set(data["schemas"]["manifest"]["required"]))
        self.assertEqual(data["schemas"]["race"]["properties"]["gates"]["maxItems"], 8)
        marina = data["schemas"]["scene"]["properties"]["marina"]["properties"]
        self.assertEqual(marina["berths"]["maxItems"], 32)
        self.assertEqual(marina["modules"]["maxItems"], 64)
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
                # The scene envelope's bounds are exercised through the voyage package; a
                # race/free scene is nested under the kind table and would need a package with
                # one. The decoder's field path is scene-relative either way.
                if ".scene." in path:
                    continue
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
        for name in ("langedrag", "harbor-life", "shelter-tour", "river-sprint"):
            with self.subTest(package=name):
                payload = author.snapshot(ROOT / "plugins" / name)
                evidence = []
                for _ in range(2):
                    job = author.ValidationJob(WORKER, payload, 3, 0, operation="preview", ticks=360)
                    try:
                        # Failure deadline: Debug CI shares CPUs with other numerical validators.
                        # The preview still advances exactly 360 virtual ticks.
                        job.process.wait(timeout=30)  # Failure deadline, no simulation clock.
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
    parser.add_argument("--bridge-server")
    args, remaining = parser.parse_known_args()
    BRIDGE_SERVER = str(Path(args.bridge_server).resolve()) if args.bridge_server else None
    WORKER = str(Path(args.worker).resolve())
    GAME = str(Path(args.game).resolve()) if args.game else None
    unittest.main(argv=[__file__] + remaining)
