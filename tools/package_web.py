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
    template = (root / "web/index.html").read_text()
    version = args.version if args.version is not None else build_version(root)
    page = template.replace("@WEB_FILES@", json.dumps(files))
    page = page.replace("@WEB_VERSION@", html.escape(version))
    (site / "index.html").write_text(page)
    shutil.copyfile(root / "web/river.png", site / "river.png")
    shutil.copyfile(root / "web/servers.json", site / "servers.json")
    (site / ".nojekyll").touch()
    shutil.copyfile(args.raylib_license, site / "raylib-LICENSE.txt")
    # Remove outputs from earlier builds only, never unrelated files.
    for pattern in ("wake_velocity-*.js", "wake_velocity-*.wasm", "wake_velocity-*.data"):
        for path in site.glob(pattern):
            if path.name not in files.values():
                path.unlink()
    total = sum((site / name).stat().st_size for name in files.values())
    print(f"Web build: {site} / {version} ({total:,} bytes before HTTP compression)")


if __name__ == "__main__":
    main()
