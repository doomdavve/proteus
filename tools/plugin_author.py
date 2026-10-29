#!/usr/bin/env python3
"""P4 native authoring discovery and cancellable isolated validation (protocol 1)."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import stat
import struct
import subprocess
import tempfile
import time

MANIFEST_MAX = 4096
SOURCE_MAX = 65536


def capabilities():
    return {"protocol": 1, "operations": ["capabilities", "validate"],
            "kinds": {"voyage": "outer", "race": "river"},
            "capabilities": ["voyage.define", "race.define", "objective.hud", "objective.timers"],
            "limits": {"manifest_bytes": MANIFEST_MAX, "source_bytes": SOURCE_MAX,
                       "lua_bytes": 2097152, "entry_instructions": 1000000,
                       "callback_instructions": 100000, "migration_instructions": 500000},
            "api_document": "docs/PLUGIN_API.md", "examples": ["plugins/langedrag",
                "plugins/river-sprint", "plugins/shelter-tour"],
            "isolation": "disposable process; restricted Lua; no OS security sandbox",
            "unsupported": ["preview", "capture", "apply", "undo", "browser", "tool_bridge"]}


def read_regular(path, limit):
    # Nonblocking open rejects FIFOs without waiting for a writer; fstat checks the opened file.
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    if path.is_symlink():
        raise ValueError(f"{path.name}: symbolic links are unsupported")
    with os.fdopen(os.open(path, flags), "rb") as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise ValueError(f"{path.name}: expected a regular file")
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"{path.name}: exceeds {limit} bytes")
    return data


def snapshot(directory):
    """API 1 has exactly plugin.json and main.lua; the native parser checks the manifest."""
    directory = Path(directory)
    manifest = read_regular(directory / "plugin.json", MANIFEST_MAX)
    source = read_regular(directory / "main.lua", SOURCE_MAX)
    return struct.pack(">II", len(manifest), len(source)) + manifest + source


class ValidationJob:
    """One immutable request. Caller supplies monotonic NOW; poll never runs Lua or waits.

    close/cancel always kill and reap the worker. No result installs a live revision.
    Base revision is correlation metadata, not permission to apply a result later.
    """

    def __init__(self, worker, payload, base_revision, now, timeout=5.0, expected_bundle=None):
        if not math.isfinite(timeout) or timeout <= 0 or timeout > 60:
            raise ValueError("timeout must be finite and in (0, 60]")
        if not isinstance(base_revision, int) or not 0 <= base_revision <= 0xffffffff:
            raise ValueError("base revision must be an unsigned 32-bit integer")
        if len(payload) > 8 + MANIFEST_MAX + SOURCE_MAX:
            raise ValueError("snapshot exceeds input limit")
        self.bundle = hashlib.sha256(payload).hexdigest()
        if expected_bundle is not None and expected_bundle != self.bundle:
            raise ValueError("bundle changed since the expected snapshot")
        self.base_revision = base_revision
        self.deadline = now + timeout
        self.result = None
        self.input = tempfile.TemporaryFile()
        self.output = tempfile.TemporaryFile()
        try:
            self.input.write(payload)
            self.input.seek(0)
            self.process = subprocess.Popen([str(Path(worker).resolve())], stdin=self.input,
                                            stdout=self.output, stderr=subprocess.DEVNULL,
                                            close_fds=True)
        except BaseException:
            self.input.close()
            self.output.close()
            raise

    def finish(self, status, evidence=None):
        self.result = {"protocol": 1, "operation": "validate", "bundle_id": self.bundle,
                       "base_revision": self.base_revision, "status": status}
        if evidence is not None:
            self.result["evidence"] = evidence
        self.input.close()
        self.output.close()
        return self.result

    def cancel(self, status="cancelled"):
        if self.result is None:
            self.process.kill()
            self.process.wait()
            self.finish(status)
        return self.result

    def poll(self, now):
        if self.result is not None:
            return self.result
        if now >= self.deadline:
            return self.cancel("deadline")
        code = self.process.poll()
        if code is None:
            return None
        self.output.seek(0)
        data = self.output.read(8193)
        try:
            if len(data) > 8192 or code not in (0, 1):
                raise ValueError("invalid worker exit/output")
            evidence = json.loads(data)
            if not isinstance(evidence, dict) or not isinstance(evidence.get("status"), str):
                raise ValueError("invalid worker response")
            if (evidence["status"] == "ok") != (code == 0):
                raise ValueError("worker status disagrees with exit code")
        except (ValueError, UnicodeError):
            return self.finish("worker_failed")
        return self.finish("ok" if code == 0 else "rejected", evidence)

    def close(self):
        self.cancel()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=["capabilities", "validate"])
    parser.add_argument("directory", nargs="?")
    parser.add_argument("--worker", type=Path, default=Path(__file__).resolve().parent.parent
                        / "build" / ("boat_plugin_worker.exe" if os.name == "nt" else "boat_plugin_worker"))
    parser.add_argument("--base-revision", type=int, default=0)
    parser.add_argument("--expect-bundle")
    parser.add_argument("--timeout", type=float, default=5)
    args = parser.parse_args()
    if args.operation == "capabilities":
        print(json.dumps(capabilities(), sort_keys=True))
        return 0
    job = None
    try:
        if not args.directory:
            raise ValueError("validate requires a plugin directory")
        job = ValidationJob(args.worker, snapshot(args.directory), args.base_revision,
                            time.monotonic(), args.timeout, args.expect_bundle)
        while (result := job.poll(time.monotonic())) is None:
            time.sleep(.01)
    except KeyboardInterrupt:
        result = job.cancel() if job else {"status": "cancelled"}
    except (OSError, ValueError) as error:
        result = {"protocol": 1, "status": "input_error", "message": str(error)}
    finally:
        if job:
            job.close()
    print(json.dumps(result, sort_keys=True))
    return 0 if result["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
