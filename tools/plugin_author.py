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
BUNDLE_MAX = 200000
MODULES_MAX = 8
MODULE_NAME_MAX = 32
ENTRY = "main.lua"
# The source image of src/plugins/boat_plugin_image.h; the browser packs the same bytes
# (web/plugin_author.js) and tests/support/fixtures/plugin_image_golden.hex pins all three.
IMAGE_MAGIC = b"\0PMI\x01"
MODULE_NAME = re.compile(r"[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*){0,2}\Z")
NAME_RULE = ("must be 1-3 dot-separated names of a-z, 0-9 and '_', each starting with a letter, "
             f"in at most {MODULE_NAME_MAX} bytes")
V1_MESSAGE = "version 1 bundles are not supported; re-export"


OPERATIONS = ["capabilities", "schema", "anchors", "validate", "preview", "map", "capture", "export",
              "import", "workspace-read", "workspace-write", "live-inspect", "live-stage", "live-apply",
              "live-undo", "live-discard", "live-console", "sync-shared", "init", "check", "status",
              "stage", "apply", "undo", "discard", "console", "play"]
WORKSPACE_OPERATIONS = ("init", "check", "map", "status", "stage", "apply", "undo", "discard", "console",
                        "play")
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
                             "plugins/shelter-tour", "plugins/basin-free",
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


SHARED = "_shared"


def shared_modules(plugins):
    """name -> bytes of each module file in PLUGINS/_shared (module names as in manifests).
    The directory is a source of copies, never a package: it has no plugin.json."""
    base = Path(plugins) / SHARED
    if not base.is_dir():
        raise ValueError(f"{base}: no shared module directory (run from a source checkout)")
    found = {}
    for path in sorted(base.rglob("*.lua")):
        name = ".".join(path.relative_to(base).with_suffix("").parts)
        if module_valid(name) and name != "main" and not path.is_symlink():
            found[name] = read_regular(path, SOURCE_MAX)
    return found


def sync_shared(plugins, check=False):
    """Make every package's copy of a shared module equal to PLUGINS/_shared's. A copy is a
    module file whose name exists in _shared: declared in plugin.json, or merely present under
    the package's modules directory. Returns {"checked", "drifted", "changed"} (package-relative
    paths, sorted); with CHECK nothing is written and "changed" is empty."""
    plugins = Path(plugins)
    shared = shared_modules(plugins)
    checked, drifted = 0, []
    for package in sorted(p for p in plugins.iterdir()
                          if p.is_dir() and not p.name.startswith(("_", "."))
                          and (p / "plugin.json").is_file()):
        names = set(declared_modules((package / "plugin.json").read_bytes()))
        names |= {name for name in shared if (package / module_file(name)).exists()}
        for name in sorted(names & set(shared)):
            checked += 1
            target = package / module_file(name)
            if target.is_symlink():
                raise ValueError(f"{package.name}/{module_file(name)}: symbolic links are unsupported")
            if not target.exists() or target.read_bytes() != shared[name]:
                drifted.append(f"{package.name}/{module_file(name)}")
                if not check:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(shared[name])
    return {"checked": checked, "drifted": drifted, "changed": [] if check else drifted}


def capabilities():
    layout = {"kind": LAYOUT["kind"]}
    if LAYOUT["kind"] == "package":
        layout.update(package=LAYOUT["package"], version=LAYOUT["version"])
    return {"protocol": 1, "operations": OPERATIONS, "layout": layout,
            "kinds": {"voyage": "outer", "free": "basin or outer", "course": "basin",
                      "race": "outer", "match": "any or a compiled location"},
            "capabilities": ["voyage.define", "scene.define", "free.define", "course.define",
                             "race.define", "match.define", "objective.hud",
                             "objective.timers"],
            "scene": {"grant": "scene.define", "envelope_version": 3,
                      "supported_subsystems": ["marina.berths", "marina.modules",
                                               "traffic.vessels", "gulls.flocks"],
                      "installed": True, "planned_subsystems": [],
                      "limits": {"berths": 32, "modules": 64, "vessels": 8, "flocks": 4,
                                 "birds_per_flock": 8, "birds": 32, "cues_per_flock": 16},
                      "depth": "unverified",
                      "berth_appearance": {"fields": ["length", "beam", "mast_height", "hull", "deck", "trim", "name"],
                          "dimension_ratio": [0.75, 1.25], "name_characters": "A-Z 0-9 .-'",
                          "name_max": 16, "mast_recipes": ["sailboat", "sailboat_7m"]},
                      "recipes": ["skiff", "runabout", "sailboat", "cruiser", "speedboat",
                                  "sailboat_7m", "workboat"],
                      "module_recipes": ["walkway", "platform", "gangway", "quay_deck", "finger_pier"],
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
                       "modules": MODULES_MAX, "module_name_bytes": MODULE_NAME_MAX,
                       "lua_bytes": 2097152, "entry_instructions": 1000000,
                       "callback_instructions": 100000, "migration_instructions": 500000},
            "api_document": LAYOUT["api_document"], "anchors": LAYOUT["anchors_name"],
            "examples": LAYOUT["examples"],
            "isolation": "disposable process; restricted Lua; no OS security sandbox",
            "preview": {"max_ticks": 72000, "fixtures": ["voyage-calm-steer-v1", "race-four-grid-v1"]},
            "browser": {"capabilities": ["voyage.define", "free.define", "course.define",
                                         "scene.define"],
                        "locations": ["basin", "outer"],
                        "bundle_format": "wake-velocity-plugin", "bundle_version": 2,
                        "result_bytes": 16384},
            "workspace": {"kinds": ["voyage", "free"],
                          "check": {"voyage": ["validate", "preview"], "free": ["validate"]}},
            "capture": {"kinds": ["voyage"], "renderer": "cpu", "max_seconds": 60,
                        "width": 320, "height": 200, "format": "png"},
            "live": {"game_option": "--plugin-bridge DIR", "kinds": ["voyage", "free"],
                     "free_requires": "scene.work (a working harbour)",
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


def module_valid(name):
    return isinstance(name, str) and len(name) <= MODULE_NAME_MAX and bool(MODULE_NAME.match(name))


def module_file(name):
    """modules/a/b.lua for the module a.b."""
    return "modules/" + name.replace(".", "/") + ".lua"


def image_is(source):
    return source.startswith(IMAGE_MAGIC)


def pack_image(files):
    """FILES is [(name, bytes)], the entry first; the bytes boat_plugin_image_pack writes."""
    if not 1 <= len(files) <= MODULES_MAX + 1:
        raise ValueError("package image needs 1-9 files")
    out = bytearray(IMAGE_MAGIC + bytes([len(files)]))
    names = set()
    for name, data in files:
        raw = name.encode("utf-8")
        if not 1 <= len(raw) <= MODULE_NAME_MAX or name in names or len(data) > SOURCE_MAX:
            raise ValueError(f"package image file {name} cannot be packed")
        names.add(name)
        out += bytes([len(raw)]) + raw + struct.pack("<I", len(data)) + data
        if len(out) > SOURCE_MAX:
            raise ValueError("package image exceeds the 64 KiB source budget")
    return bytes(out)


def unpack_image(image):
    """The inverse of pack_image; anything pack could not have written is refused."""
    bad = ValueError("malformed package source image")
    if not image_is(image) or len(image) > SOURCE_MAX or len(image) < len(IMAGE_MAGIC) + 1:
        raise bad
    count = image[len(IMAGE_MAGIC)]
    if not 1 <= count <= MODULES_MAX + 1:
        raise bad
    at, files, seen = len(IMAGE_MAGIC) + 1, [], set()
    for index in range(count):
        if at >= len(image):
            raise bad
        n = image[at]
        at += 1
        if not 1 <= n <= MODULE_NAME_MAX or len(image) - at < n + 4:
            raise bad
        try:
            name = image[at:at + n].decode("utf-8")
        except UnicodeError:
            raise bad from None
        at += n
        size = struct.unpack("<I", image[at:at + 4])[0]
        at += 4
        if size > len(image) - at or (index and not module_valid(name)) or name in seen:
            raise bad
        seen.add(name)
        files.append((name, image[at:at + size]))
        at += size
    if at != len(image):
        raise bad
    return files


def declared_modules(manifest):
    """The module names plugin.json declares, in order, with the native parser's refusals. The
    native parser stays the authority: a manifest that is not JSON declares none here."""
    try:
        data = json.loads(manifest)
    except ValueError:
        return []
    if not isinstance(data, dict) or "modules" not in data:
        return []
    names = data["modules"]
    if not isinstance(names, list) or len(names) > MODULES_MAX:
        raise ValueError(f"plugin.json modules must be an array of at most {MODULES_MAX} "
                         "module names")
    for i, name in enumerate(names):
        if not module_valid(name):
            raise ValueError(f"plugin.json modules[{i + 1}] {NAME_RULE}")
        if name == "main":
            raise ValueError(f"plugin.json modules[{i + 1}] must not be \"main\", the "
                             "entry's own name")
        if names.index(name) != i:
            raise ValueError(f"plugin.json modules[{i + 1}] repeats modules[{names.index(name) + 1}]")
    return names


def budget_message(label, room):
    return (f"{label} is larger than the {room} bytes left of the {SOURCE_MAX}-byte source "
            "budget (entry, modules and framing share it)")


def package_source(manifest, entry, modules):
    """The source the native side holds for MANIFEST (bytes): the entry bytes, or an image of the
    entry and MODULES (a dict of name to bytes) in the manifest's order. Entry, modules and
    framing share SOURCE_MAX, as boat_plugin_read counts it."""
    names = declared_modules(manifest)
    for name in modules:
        if name not in names:
            raise ValueError(f'bundle modules holds "{name}", which plugin.json does not declare')
    for name in names:
        if name not in modules:
            raise ValueError(f'plugin.json declares module "{name}" but {module_file(name)} '
                             "is missing")
    if not names:
        if len(entry) > SOURCE_MAX:
            raise ValueError(f"{ENTRY}: exceeds {SOURCE_MAX} bytes")
        return entry
    used = len(IMAGE_MAGIC) + 1
    for name, data in [(ENTRY, entry)] + [(n, modules[n]) for n in names]:
        framing = 1 + len(name) + 4
        if used + framing > SOURCE_MAX or len(data) > SOURCE_MAX - used - framing:
            raise ValueError(budget_message(
                ENTRY if name == ENTRY else module_file(name), max(0, SOURCE_MAX - used - framing)))
        used += framing + len(data)
    return pack_image([(ENTRY, entry)] + [(n, modules[n]) for n in names])


def split_source(manifest, source):
    """The inverse for a snapshot: (entry bytes, {name: bytes}) from the manifest and source."""
    names = declared_modules(manifest)
    if not image_is(source):
        if names:
            raise ValueError(f"plugin.json declares {len(names)} module"
                             f"{'' if len(names) == 1 else 's'} but the package source is not "
                             "a module image")
        return source, {}
    files = unpack_image(source)
    if not names:
        raise ValueError("plugin.json declares no modules but the package source is a module "
                         "image")
    if files[0][0] != ENTRY:
        raise ValueError("plugin.json entry is not the entry file the module image carries")
    if [name for name, _ in files[1:]] != names:
        raise ValueError("plugin.json modules do not match the modules in the package source "
                         "image (names and order must agree)")
    return files[0][1], dict(files[1:])


def read_modules(directory, names, used):
    """Declared module files under DIRECTORY/modules, regular files only and no links on the
    way, within what is left of the shared source budget (USED bytes already taken)."""
    modules = {}
    for name in names:
        path = Path(directory)
        for part in module_file(name).split("/"):
            path = path / part
            if path.is_symlink():
                raise ValueError(f"{module_file(name)}: symbolic links are unsupported")
        room = max(0, SOURCE_MAX - used - (1 + len(name) + 4))
        try:
            modules[name] = read_regular(path, room)
        except ValueError as error:
            if "exceeds" in str(error):
                raise ValueError(budget_message(module_file(name), room)) from None
            raise
        except FileNotFoundError:
            raise ValueError(f"{module_file(name)}: missing") from None
        except NotADirectoryError:
            raise ValueError(f"{module_file(name)}: expected a regular file") from None
        used += 1 + len(name) + 4 + len(modules[name])
    return modules


def read_package(directory):
    """(manifest, entry, modules) bytes of the package in DIRECTORY."""
    directory = Path(directory)
    manifest = read_regular(directory / "plugin.json", MANIFEST_MAX)
    names = declared_modules(manifest)
    entry = read_regular(directory / ENTRY, SOURCE_MAX)
    used = len(IMAGE_MAGIC) + 1 + 1 + len(ENTRY) + 4 + len(entry)
    return manifest, entry, read_modules(directory, names, used)


def snapshot(directory):
    """The manifest and the source (the entry, or an image when modules are declared), framed
    for the worker; the native parser checks the manifest."""
    manifest, entry, modules = read_package(directory)
    source = package_source(manifest, entry, modules)
    return struct.pack(">II", len(manifest), len(source)) + manifest + source


def split_snapshot(payload):
    """(manifest, entry, modules) of a snapshot payload."""
    manifest_size, _ = struct.unpack(">II", payload[:8])
    manifest, source = payload[8:8 + manifest_size], payload[8 + manifest_size:]
    entry, modules = split_source(manifest, source)
    return manifest, entry, modules


def write_package(directory, manifest, entry, modules):
    """Write plugin.json, the entry and each module under DIRECTORY (created if missing)."""
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "plugin.json").write_bytes(manifest)
    (directory / ENTRY).write_bytes(entry)
    for name, data in modules.items():
        path = directory / module_file(name)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def export_bundle(directory):
    """The version 2 bundle of a package directory: {format, version, manifest, source,
    modules}; modules is {} without any."""
    manifest, entry, modules = read_package(directory)
    package_source(manifest, entry, modules)  # The shared limits, as the native read applies.
    return {"format": "wake-velocity-plugin", "version": 2, "manifest": manifest.decode("utf-8"),
            "source": entry.decode("utf-8"),
            "modules": {name: data.decode("utf-8") for name, data in modules.items()}}


def bundle_package(text):
    """(manifest, entry, modules) bytes of a bundle's JSON text, refusing what the browser and
    the native read refuse. Version 1 bundles are not supported."""
    if len(text.encode("utf-8")) > BUNDLE_MAX:
        raise ValueError("Bundle exceeds the 200 KB import limit")
    data = json.loads(text)
    if isinstance(data, dict) and data.get("format") == "wake-velocity-plugin" \
            and data.get("version") == 1:
        raise ValueError(V1_MESSAGE)
    if (not isinstance(data, dict) or data.get("format") != "wake-velocity-plugin"
            or data.get("version") != 2
            or sorted(data) != ["format", "manifest", "modules", "source", "version"]):
        raise ValueError("Expected a Wake Velocity version 2 plugin bundle")
    modules = data["modules"]
    if (not isinstance(data["manifest"], str) or not isinstance(data["source"], str)
            or not isinstance(modules, dict)
            or not all(isinstance(value, str) for value in modules.values())):
        raise ValueError("Expected source text")
    manifest, entry = data["manifest"].encode("utf-8"), data["source"].encode("utf-8")
    if len(manifest) > MANIFEST_MAX:
        raise ValueError("Package exceeds the 4 KiB manifest or 64 KiB source limit")
    modules = {name: value.encode("utf-8") for name, value in modules.items()}
    package_source(manifest.decode("utf-8"), entry, modules)
    names = declared_modules(manifest.decode("utf-8"))
    return manifest, entry, {name: modules[name] for name in names}


def import_bundle(text, directory):
    """Write a bundle's package into DIRECTORY, which must not exist yet."""
    manifest, entry, modules = bundle_package(text)
    if os.path.lexists(directory):
        raise ValueError(f"{directory} already exists")
    write_package(directory, manifest, entry, modules)


def workspace_read(directory):
    """Read the draft files using the validation snapshot encoding."""
    return workspace_result(snapshot(directory))


def workspace_result(payload):
    manifest, entry, modules = split_snapshot(payload)
    files = {"plugin.json": manifest.decode("utf-8"), ENTRY: entry.decode("utf-8")}
    for name, data in modules.items():
        files[module_file(name)] = data.decode("utf-8")
    return {"protocol": 1, "operation": "workspace-read", "status": "ok",
            "bundle_id": hashlib.sha256(payload).hexdigest(), "draft": True, "files": files}


def draft_target(directory, filename, manifest):
    """The file a workspace-write may replace: plugin.json, main.lua or a declared module."""
    if filename in ("plugin.json", ENTRY):
        return directory / filename, SOURCE_MAX if filename == ENTRY else MANIFEST_MAX
    for name in declared_modules(manifest.decode("utf-8")):
        if filename == module_file(name):
            return directory / filename, SOURCE_MAX
    raise ValueError("workspace file must be plugin.json, main.lua or a module declared in "
                     "plugin.json")


def workspace_write(directory, filename, content, expected_bundle):
    """Replace one draft file, with an optimistic pin; never evaluate or install Lua. Adding or
    removing a module is a plugin.json edit over a module file that already exists."""
    if not expected_bundle:
        raise ValueError("workspace-write requires --expect-bundle")
    directory = Path(directory).resolve(strict=True)
    before = snapshot(directory)
    manifest, entry, modules = split_snapshot(before)
    target, limit = draft_target(directory, filename, manifest)
    if len(content) > limit:
        raise ValueError(f"{filename}: exceeds {limit} bytes")
    content.decode("utf-8")
    if hashlib.sha256(before).hexdigest() != expected_bundle:
        raise ValueError("bundle changed since the expected snapshot")
    if filename == "plugin.json":
        # A new manifest must still pair with the module files that exist, within the budget.
        manifest = content
        modules = read_modules(directory, declared_modules(content.decode("utf-8")),
                               len(IMAGE_MAGIC) + 2 + len(ENTRY) + 4 + len(entry))
    elif filename == ENTRY:
        entry = content
    else:
        modules[next(n for n in modules if module_file(n) == filename)] = content
    source = package_source(manifest.decode("utf-8"), entry, modules)
    mode = stat.S_IMODE(target.stat().st_mode)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent, prefix=".plugin-draft-",
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
    result = workspace_result(struct.pack(">II", len(manifest), len(source)) + manifest + source)
    result.update(operation="workspace-write", previous_bundle_id=expected_bundle,
                  changed_file=filename)
    return result


MAP_PROBES = 24
MAP_CELLS = 4096


def map_args_checked(request):
    """Worker arguments for a map request: {"probe": [(x, z)...]} or {"box": (x0, z0, x1, z1),
    "step": metres}. The worker re-checks every limit; this keeps bad input out of argv."""
    if not isinstance(request, dict):
        raise ValueError("map needs --probe X,Z (repeatable) or --box X0,Z0,X1,Z1 [--step M]")
    if request.get("hulls"):
        return ["hulls"]
    if request.get("probe"):
        points = request["probe"]
        if len(points) > MAP_PROBES:
            raise ValueError(f"map takes at most {MAP_PROBES} probes")
        return ["probe"] + [repr(float(v)) for point in points for v in point]
    box = request.get("box")
    if not box or len(box) != 4:
        raise ValueError("map needs --probe X,Z (repeatable) or --box X0,Z0,X1,Z1 [--step M]")
    step = float(request.get("step", 1.0))
    if not 0.25 <= step <= 8:
        raise ValueError(f"map step {step:g} m is outside 0.25..8 m")
    x0, x1 = sorted((float(box[0]), float(box[2])))
    z0, z1 = sorted((float(box[1]), float(box[3])))
    cols, rows = math.ceil((x1 - x0) / step) + 1, math.ceil((z1 - z0) / step) + 1
    if cols * rows > MAP_CELLS:
        fit = next((s for s in (0.25, 0.5, 1, 2, 4, 8) if
                    (math.ceil((x1 - x0) / s) + 1) * (math.ceil((z1 - z0) / s) + 1) <= MAP_CELLS),
                   None)
        advice = (f"use --step {fit:g}" if fit else "split the box into smaller boxes")
        raise ValueError(f"map box is {cols} x {rows} = {cols * rows} cells at {step:g} m; the "
                         f"limit is {MAP_CELLS}. {advice}")
    return ["box"] + [repr(float(v)) for v in box] + [repr(step)]


class ValidationJob:
    """One immutable request. Caller supplies monotonic NOW; poll never runs Lua or waits.

    close/cancel always kill and reap the worker. No result installs a live revision.
    Base revision is correlation metadata, not permission to apply a result later.
    """

    def __init__(self, worker, payload, base_revision, now, timeout=5.0,
                 expected_bundle=None, operation="validate", ticks=360, map_args=None):
        if operation == "map":
            ticks = 0
            self.map_args = map_args_checked(map_args)
        if (operation not in ("validate", "preview", "map") or type(ticks) is not int
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
            elif operation == "map":
                command += ["--map"] + self.map_args
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
        data = self.output.read(16385)
        try:
            if len(data) > (16384 if self.operation == "map" else 8192) or code not in (0, 1):
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


def map_request(args):
    try:
        if args.hulls:
            return {"hulls": True}
        if args.probe:
            return {"probe": [tuple(float(v) for v in text.split(",", 1)) for text in args.probe]}
        if args.box:
            return {"box": tuple(float(v) for v in args.box.split(",")), "step": args.step}
    except ValueError:
        raise ValueError("map coordinates are numbers: --probe X,Z or --box X0,Z0,X1,Z1") from None
    return None


def anchors():
    with open(LAYOUT["anchors"], encoding="utf-8") as stream:
        return json.load(stream)


def find_anchor(name):
    for anchor in anchors()["anchors"]:
        if anchor["id"] == name:
            return anchor
    raise ValueError(f"unknown anchor {name}; see the anchors operation")


def capture_anchor(args):
    """The named anchor, or the ad-hoc pose --pose X,Z,DEGREES (outer; heading 0 faces -z)."""
    if getattr(args, "anchor", None) and getattr(args, "pose", None):
        raise ValueError("use --anchor or --pose, not both")
    if getattr(args, "pose", None):
        try:
            x, z, heading = (float(v) for v in args.pose.split(","))
        except ValueError:
            raise ValueError("--pose is X,Z,DEGREES") from None
        if not all(math.isfinite(v) for v in (x, z, heading)) or abs(x) > 20000 or abs(z) > 20000:
            raise ValueError("--pose values must be finite and within 20000 m")
        return {"id": f"pose:{x:g},{z:g},{heading:g}", "location": "outer", "x": x, "z": z,
                "heading_degrees": heading}
    return find_anchor(args.anchor) if args.anchor else None


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
            write_package(package, *split_snapshot(payload))
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
    parser.add_argument("--file", help="plugin.json, main.lua or modules/NAME.lua for workspace-write")
    parser.add_argument("--ticks", type=int, default=360)
    parser.add_argument("--timeout", type=float)
    parser.add_argument("--check", action="store_true", help="sync-shared: report drift only")
    parser.add_argument("--game", type=Path, default=LAYOUT["game"])
    parser.add_argument("--anchor")
    parser.add_argument("--pose", metavar="X,Z,DEGREES",
                        help="capture/check: an ad-hoc boat pose on outer water instead of --anchor")
    parser.add_argument("--probe", action="append", metavar="X,Z",
                        help="map: a point to query (repeatable, at most 24)")
    parser.add_argument("--box", metavar="X0,Z0,X1,Z1", help="map: a grid over this box")
    parser.add_argument("--hulls", action="store_true",
                        help="map: the hull recipes and the berth envelope rule")
    parser.add_argument("--step", type=float, default=1.0, help="map: grid cell metres, 0.25..8")
    parser.add_argument("--time", type=float, default=1.0)
    parser.add_argument("--camera-distance", type=float)
    parser.add_argument("--camera-height", type=float)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--from", dest="source", metavar="PLUGIN",
                        help="init (required): the shipped package name or plugin directory to copy")
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
    if args.operation == "sync-shared":
        try:
            result = sync_shared(args.directory or ROOT / "plugins", args.check)
        except (OSError, ValueError) as error:
            print(json.dumps({"protocol": 1, "operation": "sync-shared", "status": "input_error",
                              "message": str(error)}, sort_keys=True))
            return 1
        status = "drift" if args.check and result["drifted"] else "ok"
        print(json.dumps({"protocol": 1, "operation": "sync-shared", "status": status,
                          **result}, sort_keys=True))
        return 0 if status == "ok" else 1
    if args.operation in WORKSPACE_OPERATIONS:
        import plugin_workspace  # Agent workspaces take their pins from the game itself.
        result = plugin_workspace.run(sys.modules[__name__], args)
        print(json.dumps(result, sort_keys=True))
        return 0 if result.get("status") == "ok" else 1
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
            if len(encoded) > BUNDLE_MAX:
                raise ValueError("bundle exceeds the 200000-byte import limit")
            sys.stdout.buffer.write(encoded)
            return 0
        if args.operation == "import":
            if not args.directory:
                raise ValueError("import requires a new plugin directory")
            import_bundle(sys.stdin.buffer.read(BUNDLE_MAX + 1).decode("utf-8"), args.directory)
            print(json.dumps({"protocol": 1, "operation": "import", "status": "ok",
                              "directory": str(args.directory)}, sort_keys=True))
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
                             time.monotonic(), capture_anchor(args),
                             args.time, camera, args.timeout or 20, args.expect_bundle)
        else:
            job = ValidationJob(args.worker, snapshot(args.directory), offline_base,
                                time.monotonic(), args.timeout or 5, args.expect_bundle,
                                args.operation, args.ticks,
                                map_request(args) if args.operation == "map" else None)
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
