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
    args, remaining = parser.parse_known_args()
    WORKER = str(Path(args.worker).resolve())
    unittest.main(argv=[__file__] + remaining)
