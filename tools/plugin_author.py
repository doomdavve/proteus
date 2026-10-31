#!/usr/bin/env python3
"""P4 native draft authoring and cancellable isolated validation (protocol 1)."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import stat
import struct
import subprocess
import sys
import tempfile
import time

# Packaged copies live in signed app bundles; importing must not write bytecode beside them.
sys.dont_write_bytecode = True
from plugin_schema import schema  # noqa: E402

MANIFEST_MAX = 4096
SOURCE_MAX = 65536
ROOT = Path(__file__).resolve().parent.parent
FRAME_MAX = 4 << 20
LOG_MAX = 16384


OPERATIONS = ["capabilities", "schema", "anchors", "validate", "preview", "capture", "export",
              "workspace-read", "workspace-write", "live-inspect", "live-stage", "live-apply",
              "live-undo", "live-discard", "live-console"]
LIVE_ACTIONS = ("inspect", "stage", "apply", "undo", "discard", "console")
RESPONSE_MAX = 32768


def executable(name):
    return name + ".exe" if os.name == "nt" else name


def load_layout():
    """Source-checkout paths, or a release package's plugin_tools.json beside this file."""
    here = Path(__file__).resolve().parent
    path = here / "plugin_tools.json"
    if not path.exists():
        return {"kind": "source", "base": ROOT,
                "worker": ROOT / "build" / executable("boat_plugin_worker"),
                "game": ROOT / "build" / executable("wake_velocity"),
                "anchors": ROOT / "tools" / "plugin_anchors.json",
                "anchors_name": "tools/plugin_anchors.json",
                "api_document": "docs/PLUGIN_API.md",
                "examples": ["plugins/langedrag", "plugins/harbor-life", "plugins/river-sprint",
                             "plugins/shelter-tour", "plugins/basin-free", "plugins/river-free",
                             "plugins/time-trial"]}
    with open(path, encoding="utf-8") as stream:
        package = json.load(stream)
    if package.get("layout") != 1:
        raise SystemExit(f"{path}: unsupported plugin tools layout")
    # Packaged paths are relative to this directory; capabilities reports them resolved.
    resolved = {key: (here / package[key]).resolve()
                for key in ("worker", "game", "anchors", "api_document")}
    return {"kind": "package", "base": here, "package": package["package"],
            "version": package["version"], "worker": resolved["worker"],
            "game": resolved["game"], "anchors": resolved["anchors"],
            "anchors_name": str(resolved["anchors"]),
            "api_document": str(resolved["api_document"]),
            "examples": [str((here / name).resolve()) for name in package["examples"]]}


LAYOUT = load_layout()


def capabilities():
    layout = {"kind": LAYOUT["kind"]}
    if LAYOUT["kind"] == "package":
        layout.update(package=LAYOUT["package"], version=LAYOUT["version"])
    return {"protocol": 1, "operations": OPERATIONS, "layout": layout,
            "kinds": {"voyage": "outer", "free": "basin, river or outer", "course": "basin",
                      "race": "river", "match": "any or a compiled location"},
            "capabilities": ["voyage.define", "scene.define", "free.define", "course.define",
                             "race.define", "match.define", "objective.hud",
                             "objective.timers"],
            "scene": {"grant": "scene.define", "envelope_version": 1,
                      "supported_subsystems": ["marina.berths", "marina.modules",
                                               "traffic.vessels", "gulls.flocks"],
                      "installed": True, "planned_subsystems": [],
                      "limits": {"berths": 7, "modules": 4, "vessels": 8, "flocks": 4,
                                 "birds_per_flock": 8, "birds": 32, "cues_per_flock": 16},
                      "depth": "unverified",
                      "recipes": ["skiff", "runabout", "sailboat", "cruiser", "speedboat",
                                  "sailboat_7m", "workboat"],
                      "module_recipes": ["pontoon", "pontoon_small", "gangway", "quay"],
                      "craft_recipes": ["runabout", "cruiser", "sailboat", "ferry", "alvsnabben",
                                        "frihamnen", "schooner", "workboat"],
                      "route_recipes": ["river_runabout", "river_cruiser", "river_sailboat",
                                        "river_ferry", "alvsnabben", "frihamnen", "schooner",
                                        "marina_pass"],
                      "route_stops": {"alvsnabben": ["stenpiren", "lindholmspiren",
                                                     "eriksberg"],
                                      "frihamnen": ["stenpiren", "frihamnen"]},
                      "bridge_routes": ["schooner"],
                      "flock_recipes": ["gull"],
                      "cue_kinds": ["feeding", "alarm"],
                      "traffic_wakes": "visual only"},
            "limits": {"manifest_bytes": MANIFEST_MAX, "source_bytes": SOURCE_MAX,
                       "lua_bytes": 2097152, "entry_instructions": 1000000,
                       "callback_instructions": 100000, "migration_instructions": 500000},
            "api_document": LAYOUT["api_document"], "anchors": LAYOUT["anchors_name"],
            "examples": LAYOUT["examples"],
            "isolation": "disposable process; restricted Lua; no OS security sandbox",
            "preview": {"max_ticks": 72000, "fixtures": ["voyage-calm-steer-v1", "race-four-grid-v1"]},
            "browser": {"capabilities": ["voyage.define", "free.define", "course.define",
                                         "scene.define"],
                        "locations": ["basin", "river", "outer"],
                        "bundle_format": "wake-velocity-plugin", "bundle_version": 1,
                        "result_bytes": 16384},
            "capture": {"kinds": ["voyage"], "renderer": "cpu", "max_seconds": 60,
                        "width": 320, "height": 200, "format": "png"},
            "live": {"game_option": "--plugin-bridge DIR", "kinds": ["voyage"],
                     "transport": "private local directory mailbox; per-launch token",
                     "actions": list(LIVE_ACTIONS), "pins": {
                         "stage": ["base_revision", "base_epoch", "expect_bundle"],
                         "apply": ["held_revision", "base_epoch"],
                         "undo": ["base_revision", "base_epoch"], "discard": ["base_epoch"]}},
            "unsupported": ["race_capture", "gpu_capture", "race_live", "browser_live",
                            "network_bridge", "browser_race"]}


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


def export_bundle(directory):
    directory = Path(directory)
    return {"format": "wake-velocity-plugin", "version": 1,
            "manifest": read_regular(directory / "plugin.json", MANIFEST_MAX).decode("utf-8"),
            "source": read_regular(directory / "main.lua", SOURCE_MAX).decode("utf-8")}


def workspace_read(directory):
    """Read the two selected draft files using the validation snapshot encoding."""
    return workspace_result(snapshot(directory))


def workspace_result(payload):
    manifest_size, _ = struct.unpack(">II", payload[:8])
    return {"protocol": 1, "operation": "workspace-read", "status": "ok",
            "bundle_id": hashlib.sha256(payload).hexdigest(), "draft": True,
            "files": {"plugin.json": payload[8:8 + manifest_size].decode("utf-8"),
                      "main.lua": payload[8 + manifest_size:].decode("utf-8")}}


def workspace_write(directory, filename, content, expected_bundle):
    """Replace one draft file, with an optimistic pin; never evaluate or install Lua."""
    if filename not in ("plugin.json", "main.lua"):
        raise ValueError("workspace file must be plugin.json or main.lua")
    if not expected_bundle:
        raise ValueError("workspace-write requires --expect-bundle")
    limit = MANIFEST_MAX if filename == "plugin.json" else SOURCE_MAX
    if len(content) > limit:
        raise ValueError(f"{filename}: exceeds {limit} bytes")
    content.decode("utf-8")
    directory = Path(directory).resolve(strict=True)
    before = snapshot(directory)
    manifest_size, _ = struct.unpack(">II", before[:8])
    before[8:8 + manifest_size].decode("utf-8")
    before[8 + manifest_size:].decode("utf-8")
    if hashlib.sha256(before).hexdigest() != expected_bundle:
        raise ValueError("bundle changed since the expected snapshot")
    target = directory / filename
    mode = stat.S_IMODE(target.stat().st_mode)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=directory, prefix=".plugin-draft-",
                                         delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(content)
        os.chmod(temporary, mode)
        # Detect edits during preparation as well as stale requests at entry.
        if snapshot(directory) != before:
            raise ValueError("bundle changed during draft preparation")
        os.replace(temporary, target)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    manifest = content if filename == "plugin.json" else before[8:8 + manifest_size]
    source = content if filename == "main.lua" else before[8 + manifest_size:]
    result = workspace_result(struct.pack(">II", len(manifest), len(source)) + manifest + source)
    result.update(operation="workspace-write", previous_bundle_id=expected_bundle,
                  changed_file=filename)
    return result


class ValidationJob:
    """One immutable request. Caller supplies monotonic NOW; poll never runs Lua or waits.

    close/cancel always kill and reap the worker. No result installs a live revision.
    Base revision is correlation metadata, not permission to apply a result later.
    """

    def __init__(self, worker, payload, base_revision, now, timeout=5.0,
                 expected_bundle=None, operation="validate", ticks=360):
        if (operation not in ("validate", "preview") or type(ticks) is not int
                or not 0 <= ticks <= 72000):
            raise ValueError("preview ticks must be an integer in [0, 72000]")
        self.operation = operation
        self.ticks = ticks
        if not math.isfinite(timeout) or timeout <= 0 or timeout > 60:
            raise ValueError("timeout must be finite and in (0, 60]")
        if type(base_revision) is not int or not 0 <= base_revision <= 0xffffffff:
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
            command = [str(Path(worker).resolve())]
            if operation == "preview":
                command += ["--preview", str(ticks)]
            self.process = subprocess.Popen(command, stdin=self.input,
                                            stdout=self.output, stderr=subprocess.DEVNULL,
                                            close_fds=True)
        except BaseException:
            self.input.close()
            self.output.close()
            raise

    def finish(self, status, evidence=None):
        self.result = {"protocol": 1, "operation": self.operation, "bundle_id": self.bundle,
                       "base_revision": self.base_revision, "status": status}
        if evidence is not None:
            self.result["evidence"] = evidence
            if self.operation == "preview" and status == "ok":
                canonical = json.dumps(evidence["scenario"], sort_keys=True, separators=(",", ":"))
                self.result["artifact_id"] = hashlib.sha256(
                    (self.bundle + ":" + str(self.base_revision) + ":" + canonical).encode()).hexdigest()
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
            if self.operation == "preview" and code == 0:
                scenario = evidence.get("scenario")
                if not isinstance(scenario, dict) or not isinstance(scenario.get("preview"), dict):
                    raise ValueError("invalid preview evidence")
                scenario = scenario["preview"]
                if scenario.get("ticks") != self.ticks or scenario.get("fixture") not in (
                        "voyage-calm-steer-v1", "race-four-grid-v1"):
                    raise ValueError("invalid preview evidence")
            if (evidence["status"] == "ok") != (code == 0):
                raise ValueError("worker status disagrees with exit code")
        except (ValueError, UnicodeError, TypeError):
            return self.finish("worker_failed")
        return self.finish("ok" if code == 0 else "rejected", evidence)

    def close(self):
        self.cancel()


def anchors():
    with open(LAYOUT["anchors"], encoding="utf-8") as stream:
        return json.load(stream)


def find_anchor(name):
    for anchor in anchors()["anchors"]:
        if anchor["id"] == name:
            return anchor
    raise ValueError(f"unknown anchor {name}; see the anchors operation")


def png_size(frame):
    if (len(frame) < 24 or frame[:8] != b"\x89PNG\r\n\x1a\n" or frame[12:16] != b"IHDR"):
        raise ValueError("capture is not a PNG")
    return struct.unpack(">II", frame[16:24])


class CaptureJob:
    """One pinned CPU frame from the game's screenshot path, with ValidationJob's lifecycle.

    The game reads a private copy of the snapshot, so later draft saves cannot change the frame.
    The child is the trusted native game (synchronous Lua, as in --plugin), only time-bounded.
    """

    def __init__(self, game, payload, base_revision, now, anchor=None, seconds=1.0,
                 camera=None, timeout=20.0, expected_bundle=None):
        if not math.isfinite(timeout) or timeout <= 0 or timeout > 60:
            raise ValueError("timeout must be finite and in (0, 60]")
        if not math.isfinite(seconds) or not 0 <= seconds <= 60:
            raise ValueError("capture time must be in [0, 60] seconds")
        if type(base_revision) is not int or not 0 <= base_revision <= 0xffffffff:
            raise ValueError("base revision must be an unsigned 32-bit integer")
        if len(payload) > 8 + MANIFEST_MAX + SOURCE_MAX:
            raise ValueError("snapshot exceeds input limit")
        camera = camera or {}
        for key, low, high in (("distance", 8, 48), ("height", 4, 36)):
            if key in camera and not (math.isfinite(camera[key]) and low <= camera[key] <= high):
                raise ValueError(f"camera {key} must be in [{low}, {high}]")
        self.bundle = hashlib.sha256(payload).hexdigest()
        if expected_bundle is not None and expected_bundle != self.bundle:
            raise ValueError("bundle changed since the expected snapshot")
        manifest_size, _ = struct.unpack(">II", payload[:8])
        manifest = json.loads(payload[8:8 + manifest_size])
        # The native parser stays authoritative; this only routes the request.
        if not isinstance(manifest, dict) or manifest.get("kind") != "voyage":
            raise ValueError("capture supports voyage plugins only; free, course and race capture are unsupported")
        if anchor is not None and anchor["location"] != manifest.get("location"):
            raise ValueError(f"anchor {anchor['id']} is not in {manifest.get('location')}")
        self.base_revision = base_revision
        self.deadline = now + timeout
        self.result = None
        self.frame = None
        self.scenario = {"location": manifest.get("location"), "seconds": seconds,
                         "ticks": round(seconds * 120), "renderer": "cpu",
                         "anchor": anchor["id"] if anchor else None, "camera": camera,
                         "scene_grant": "scene.define" in manifest.get("capabilities", [])}
        self.directory = tempfile.TemporaryDirectory(prefix="plugin-capture-")
        self.log = tempfile.TemporaryFile()
        try:
            package = Path(self.directory.name) / "package"
            package.mkdir()
            (package / "plugin.json").write_bytes(payload[8:8 + manifest_size])
            (package / "main.lua").write_bytes(payload[8 + manifest_size:])
            self.package = str(package)
            self.output = Path(self.directory.name) / "frame.png"
            command = [str(Path(game).resolve()), "--renderer", "cpu", "--plugin", self.package,
                       "--screenshot", str(self.output), "--time", repr(float(seconds))]
            if anchor:
                command += ["--start", f"{anchor['x']!r},{anchor['z']!r},"
                            f"{anchor['heading_degrees']!r}"]
            for key in ("distance", "height"):
                if key in camera:
                    command += [f"--camera-{key}", repr(float(camera[key]))]
            # The game finds assets beside itself; the source root also suits a checkout.
            self.process = subprocess.Popen(command, cwd=LAYOUT["base"], stdin=subprocess.DEVNULL,
                                            stdout=self.log, stderr=subprocess.STDOUT,
                                            close_fds=True)
        except BaseException:
            self.log.close()
            self.directory.cleanup()
            raise

    def finish(self, status, message=None, evidence=None):
        self.result = {"protocol": 1, "operation": "capture", "bundle_id": self.bundle,
                       "base_revision": self.base_revision, "status": status,
                       "scenario": self.scenario}
        if message:
            self.result["message"] = message
        if evidence is not None:
            self.result["evidence"] = evidence
            canonical = json.dumps({"scenario": self.scenario, "evidence": evidence},
                                   sort_keys=True, separators=(",", ":"))
            pins = self.bundle + ":" + str(self.base_revision) + ":"
            self.result["artifact_id"] = hashlib.sha256((pins + canonical).encode()).hexdigest()
        self.log.close()
        self.directory.cleanup()
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
        self.log.seek(0, os.SEEK_END)
        self.log.seek(max(0, self.log.tell() - LOG_MAX))
        lines = [line.replace(self.package, "plugin") for line in
                 self.log.read().decode("utf-8", "replace").splitlines() if line.strip()]
        plugin = [line for line in lines if line.startswith("Plugin ")]
        if code != 0:
            # A diagnostic from the native host means the candidate itself was rejected.
            rejected = [line for line in plugin if " error in " in line]
            return self.finish("rejected" if rejected else "capture_failed",
                               (rejected or lines or [f"game exited with {code}"])[-1])
        match = re.search(r": (\S+) R(\d+) ACTIVE ([0-9a-f]{16})", plugin[-1] if plugin else "")
        try:
            if not match:
                raise ValueError("game did not report an active plugin revision")
            with open(self.output, "rb") as stream:
                frame = stream.read(FRAME_MAX + 1)
            if len(frame) > FRAME_MAX:
                raise ValueError("capture exceeds frame limit")
            width, height = png_size(frame)
        except (OSError, ValueError) as error:
            return self.finish("capture_failed", str(error))
        self.frame = frame
        return self.finish("ok", evidence={
            "plugin_id": match[1], "revision": int(match[2]), "gameplay_digest": match[3],
            "frame": {"format": "png", "width": width, "height": height,
                      "bytes": len(frame), "sha256": hashlib.sha256(frame).hexdigest()}})

    def close(self):
        self.cancel()


LIVE_INTEGERS = {"revision", "epoch", "undo_depth", "staging", "held_revision", "held_undo",
                 "held_base", "alert", "diagnostic_line", "tick", "shelter", "exposed", "arrived"}
LIVE_REAL = {"boat_x", "boat_z", "heading"}


def parse_bridge(data):
    """KEY=VALUE lines after the protocol line; repeated inspect lines form a list."""
    lines = data.decode("utf-8", "replace").split("\n")
    if lines[0] != "wake-velocity-bridge 1":
        raise ValueError("not a protocol 1 bridge file")
    fields = {}
    for line in lines[1:]:
        if not line:
            continue
        key, separator, value = line.partition("=")
        if not separator:
            raise ValueError("malformed bridge line")
        if key == "inspect":
            fields.setdefault("inspection", []).append(value)
        elif key == "output":
            fields.setdefault("output", []).append(value)
        elif key in LIVE_INTEGERS:
            fields[key] = int(value)
        elif key in LIVE_REAL:
            fields[key] = float(value)
        else:
            fields[key] = value
    return fields


class BridgeJob:
    """One request to a running game's --plugin-bridge mailbox, with ValidationJob's lifecycle.

    The game answers between ticks on its own poll; a stage answer waits for isolated staging.
    Cancelling stops waiting only: a request the game has read still takes effect.
    """

    def __init__(self, bridge, action, now, timeout=10.0, pins=None, payload=b"",
                 expected_bundle=None):
        if action not in LIVE_ACTIONS:
            raise ValueError(f"unknown live action {action}")
        if not math.isfinite(timeout) or timeout <= 0 or timeout > 60:
            raise ValueError("timeout must be finite and in (0, 60]")
        pins = pins or {}
        for key, value in pins.items():
            if type(value) is not int or not 0 <= value <= 0xffffffffffffffff:
                raise ValueError(f"{key} must be an unsigned integer")
        self.action = action
        self.bundle = None
        if action == "stage":
            if len(payload) > 8 + MANIFEST_MAX + SOURCE_MAX:
                raise ValueError("snapshot exceeds input limit")
            self.bundle = hashlib.sha256(payload).hexdigest()
            if not expected_bundle:
                raise ValueError("live-stage requires --expect-bundle")
            if expected_bundle != self.bundle:
                raise ValueError("bundle changed since the expected snapshot")
        self.bridge = Path(bridge)
        session = parse_bridge(read_regular(self.bridge / "session", 4096))
        self.session = session.get("session")
        self.id = os.urandom(16).hex()
        self.pins = pins
        self.deadline = now + timeout
        self.result = None
        self.response = self.bridge / f"response-{self.id}"
        header = ["wake-velocity-bridge 1", f"id={self.id}", f"token={session.get('token')}",
                  f"operation={action}"] + [f"{key}={value}" for key, value in pins.items()]
        if payload:
            header.append(f"payload={len(payload)}")
        temporary = self.bridge / f".request-{self.id}"
        with open(temporary, "xb") as stream:
            stream.write(("\n".join(header) + "\n\n").encode() + payload)
        try:
            # A hard link publishes the complete request without replacing a waiting one.
            os.link(temporary, self.bridge / "request")
        except FileExistsError:
            raise ValueError("bridge busy: an earlier request has not been read") from None
        finally:
            temporary.unlink()

    def finish(self, status, fields=None):
        self.result = {"protocol": 1, "operation": "live-" + self.action, "status": status,
                       "request_id": self.id, "session": self.session}
        if self.bundle:
            self.result["bundle_id"] = self.bundle
        if self.pins:
            self.result["pins"] = self.pins
        if fields:
            for key in ("protocol", "operation", "status", "id"):
                fields.pop(key, None)
            self.result.update(fields)
        return self.result

    def cancel(self, status="cancelled"):
        if self.result is None:
            self.finish(status)
        return self.result

    def poll(self, now):
        if self.result is not None:
            return self.result
        try:
            data = read_regular(self.response, RESPONSE_MAX)
        except FileNotFoundError:
            if now >= self.deadline:
                return self.cancel("deadline")
            return None
        fields = parse_bridge(data)
        self.response.unlink(missing_ok=True)
        if fields.get("id") != self.id or fields.get("session") != self.session:
            return self.finish("bridge_failed", {"message": "response for another request"})
        status = fields.get("status")
        # Like validate, a candidate the game refused is rejected, not an accepted request.
        if status == "ok" and fields.get("outcome") in (
                "rejected", "incompatible", "migration_failed"):
            status = "rejected"
        return self.finish(status, fields)

    def close(self):
        self.cancel()


def write_frame(path, frame):
    """Atomically place a verified frame; a failure leaves any previous file."""
    path = Path(path)
    with tempfile.NamedTemporaryFile(dir=path.parent or ".", prefix=".capture-",
                                     delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(frame)
    try:
        os.replace(temporary, path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def live_job(args):
    action = args.operation[len("live-"):]
    if not args.bridge:
        raise ValueError(f"{args.operation} requires --bridge DIR")
    needed = {"stage": ("base_revision", "base_epoch"), "apply": ("held_revision", "base_epoch"),
              "undo": ("base_revision", "base_epoch"), "discard": ("base_epoch",),
              "console": ("base_epoch",), "inspect": ()}[action]
    pins = {}
    for key in needed:
        value = getattr(args, key)
        if value is None:
            raise ValueError(f"{args.operation} requires --{key.replace('_', '-')}")
        pins[key] = value
    payload = b""
    if action == "stage":
        if not args.directory:
            raise ValueError("live-stage requires a plugin directory")
        payload = snapshot(args.directory)
    elif action == "console":
        if args.line is None:
            raise ValueError("live-console requires --line TEXT")
        payload = args.line.encode("utf-8")
    return BridgeJob(args.bridge, action, time.monotonic(), args.timeout or 10, pins, payload,
                     args.expect_bundle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=OPERATIONS)
    parser.add_argument("directory", nargs="?")
    parser.add_argument("--worker", type=Path, default=LAYOUT["worker"])
    parser.add_argument("--base-revision", type=int)
    parser.add_argument("--base-epoch", type=int)
    parser.add_argument("--held-revision", type=int)
    parser.add_argument("--bridge", type=Path, help="the game's --plugin-bridge directory")
    parser.add_argument("--expect-bundle")
    parser.add_argument("--line", help="one console line for live-console")
    parser.add_argument("--file", choices=["plugin.json", "main.lua"])
    parser.add_argument("--ticks", type=int, default=360)
    parser.add_argument("--timeout", type=float)
    parser.add_argument("--game", type=Path, default=LAYOUT["game"])
    parser.add_argument("--anchor")
    parser.add_argument("--time", type=float, default=1.0)
    parser.add_argument("--camera-distance", type=float)
    parser.add_argument("--camera-height", type=float)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.operation == "capabilities":
        print(json.dumps(capabilities(), sort_keys=True))
        return 0
    if args.operation == "schema":
        print(json.dumps(schema(), sort_keys=True))
        return 0
    if args.operation == "anchors":
        print(json.dumps(anchors(), sort_keys=True))
        return 0
    offline_base = args.base_revision or 0  # Correlation only; live pins must be explicit.
    job = None
    try:
        if args.operation in ("workspace-read", "workspace-write"):
            if not args.directory:
                raise ValueError(f"{args.operation} requires a plugin directory")
            if args.operation == "workspace-read":
                result = workspace_read(args.directory)
            else:
                if not args.file or not args.expect_bundle:
                    raise ValueError("workspace-write requires --file and --expect-bundle")
                limit = MANIFEST_MAX if args.file == "plugin.json" else SOURCE_MAX
                result = workspace_write(args.directory, args.file,
                                         sys.stdin.buffer.read(limit + 1), args.expect_bundle)
            print(json.dumps(result, sort_keys=True))
            return 0
        if args.operation == "export":
            if not args.directory:
                raise ValueError("export requires a plugin directory")
            encoded = (json.dumps(export_bundle(args.directory), ensure_ascii=False, indent=2)
                       + "\n").encode("utf-8")
            if len(encoded) > 200000:
                raise ValueError("bundle exceeds the 200000-byte import limit")
            sys.stdout.buffer.write(encoded)
            return 0
        if args.operation.startswith("live-"):
            job = live_job(args)
        elif not args.directory:
            raise ValueError(f"{args.operation} requires a plugin directory")
        elif args.operation == "capture":
            if not args.output:
                raise ValueError("capture requires --output FILE.png")
            camera = {key: value for key, value in (("distance", args.camera_distance),
                      ("height", args.camera_height)) if value is not None}
            job = CaptureJob(args.game, snapshot(args.directory), offline_base,
                             time.monotonic(), find_anchor(args.anchor) if args.anchor else None,
                             args.time, camera, args.timeout or 20, args.expect_bundle)
        else:
            job = ValidationJob(args.worker, snapshot(args.directory), offline_base,
                                time.monotonic(), args.timeout or 5, args.expect_bundle,
                                args.operation, args.ticks)
        while (result := job.poll(time.monotonic())) is None:
            time.sleep(.01)
        if args.operation == "capture" and result["status"] == "ok":
            write_frame(args.output, job.frame)
            result["frame_path"] = str(args.output)
    except KeyboardInterrupt:
        result = job.cancel() if job else {"status": "cancelled"}
    except (OSError, ValueError, UnicodeError) as error:
        result = {"protocol": 1, "status": "input_error", "message": str(error)}
    finally:
        if job:
            job.close()
    print(json.dumps(result, sort_keys=True))
    return 0 if result["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
