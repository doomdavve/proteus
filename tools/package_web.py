#!/usr/bin/env python3
"""Package the Emscripten output with content-addressed URLs; no extra dependencies."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import shutil
import subprocess


def build_version(root):
    try:
        return subprocess.check_output(
            ["git", "-C", str(root), "describe", "--tags", "--match", "v[0-9]*",
             "--always", "--dirty"],
            text=True, stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--raylib-license", type=Path, required=True)
    parser.add_argument("--lua-license", type=Path)
    parser.add_argument("--version", help="Override the Git build version (e.g. source archives)")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    site = args.build_dir / "site"
    site.mkdir(parents=True, exist_ok=True)
    files = {}
    for suffix in ("js", "wasm", "data"):
        original = f"wake_velocity.{suffix}"
        content = (args.build_dir / original).read_bytes()
        digest = hashlib.sha256(content).hexdigest()[:16]
        name = f"wake_velocity-{digest}.{suffix}"
        (site / name).write_bytes(content)
        files[original] = name
    # Plugin compiler/evaluator is lazy and runs only in isolated Web Workers.
    for original in ("plugin_stage.js", "plugin_stage.wasm"):
        content = (args.build_dir / original).read_bytes()
        stem, suffix = original.rsplit(".", 1)
        name = f"{stem}-{hashlib.sha256(content).hexdigest()[:16]}.{suffix}"
        (site / name).write_bytes(content)
        files[original] = name
    worker = (root / "web/plugin_worker.js").read_bytes()
    worker_name = f"plugin_worker-{hashlib.sha256(worker).hexdigest()[:16]}.js"
    (site / worker_name).write_bytes(worker)
    files["plugin_worker.js"] = worker_name
    example = json.dumps({"format": "wake-velocity-plugin", "version": 1,
        "manifest": (root / "plugins/langedrag/plugin.json").read_text(),
        "source": (root / "plugins/langedrag/main.lua").read_text()}, sort_keys=True).encode()
    example_name = f"plugin_example-{hashlib.sha256(example).hexdigest()[:16]}.json"
    (site / example_name).write_bytes(example)
    files["plugin_example.json"] = example_name
    # Packs the page fetches on demand, by their asset path (web/scenery_loader.js).
    lazy = args.build_dir / "web-lazy"
    for path in sorted(lazy.glob("geometry/*/scenery.bin")):
        content = path.read_bytes()
        digest = hashlib.sha256(content).hexdigest()[:16]
        name = f"scenery-{path.parent.name}-{digest}.bin"
        (site / name).write_bytes(content)
        files[path.relative_to(lazy).as_posix()] = name
    template = (root / "web/index.html").read_text()
    version = args.version if args.version is not None else build_version(root)
    page = template.replace("@WEB_FILES@", json.dumps(files))
    page = page.replace("@WEB_VERSION@", html.escape(version))
    (site / "index.html").write_text(page)
    shutil.copyfile(root / "web/river.png", site / "river.png")
    shutil.copyfile(root / "web/servers.json", site / "servers.json")
    for name in ("manifest.webmanifest", "icon.svg", "apple-touch-icon.png", "icon-192.png", "icon-512.png"):
        shutil.copyfile(root / "web" / name, site / name)
    (site / ".nojekyll").touch()
    shutil.copyfile(args.raylib_license, site / "raylib-LICENSE.txt")
    shutil.copyfile(args.lua_license or args.build_dir / "lua-LICENSE.txt", site / "lua-LICENSE.txt")
    # Remove outputs from earlier builds only, never unrelated files.
    for pattern in ("wake_velocity-*.js", "wake_velocity-*.wasm", "wake_velocity-*.data",
                    "scenery-*.bin", "plugin_stage-*.js", "plugin_stage-*.wasm",
                    "plugin_worker-*.js", "plugin_example-*.json"):
        for path in site.glob(pattern):
            if path.name not in files.values():
                path.unlink()
    initial = sum((site / name).stat().st_size for key, name in files.items()
                  if key.startswith("wake_velocity."))
    later = sum((site / name).stat().st_size for key, name in files.items()
                if not key.startswith("wake_velocity."))
    print(f"Web build: {site} / {version} ({initial:,} bytes before HTTP compression, "
          f"{later:,} more fetched on demand)")


if __name__ == "__main__":
    main()
