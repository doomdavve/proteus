#!/usr/bin/env python3
"""Package a native wake_velocity build as a versioned Wake Velocity macOS app ZIP."""

import argparse
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import package_plugin_tools  # noqa: E402


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--output-dir", type=Path, default=Path("build/macos-package"))
    parser.add_argument("--raylib-source", type=Path, help="Override FetchContent source location")
    parser.add_argument("--version", default="dev", help="Release tag or development build identity")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]*", args.version):
        parser.error("version must be a filename-safe tag or build identity")
    release_version = re.match(r"v?(\d+\.\d+\.\d+)(?:[-+].*)?$", args.version)
    bundle_version = release_version.group(1) if release_version else "0.0.0"
    root = Path(__file__).resolve().parents[2]
    build = args.build_dir.resolve()
    output = args.output_dir.resolve()
    raylib = args.raylib_source or build / "_deps/raylib-src"
    app = output / "Wake Velocity.app"
    if app.exists():
        raise SystemExit(f"Refusing to overwrite existing app: {app}")
    executable = build / "wake_velocity"
    # The plugin authoring tools' isolated validator ships beside the game.
    worker = build / "boat_plugin_worker"
    architectures = run("lipo", "-archs", str(executable))
    if architectures != "arm64" or run("lipo", "-archs", str(worker)) != architectures:
        raise SystemExit(f"Expected Apple Silicon build, got: {architectures}")
    minimum_os = re.search(r"\bminos (\S+)", run("otool", "-l", str(executable)))
    if not minimum_os:
        raise SystemExit("Could not determine executable's minimum macOS version")
    # The game is also its own room authority (wake_velocity --serve), so it ships alone.
    for binary in (executable, worker):
        for line in run("otool", "-L", str(binary)).splitlines()[1:]:
            library = line.strip().split(" (", 1)[0]
            if not library.startswith(("/System/Library/", "/usr/lib/")):
                raise SystemExit(f"Unbundled dependency of {binary.name}: {library}")
    commit = run("git", "-C", str(root), "rev-parse", "HEAD")
    dirty = bool(run("git", "-C", str(root), "status", "--porcelain"))
    contents = app / "Contents"
    resources = contents / "Resources"
    (contents / "MacOS").mkdir(parents=True)
    resources.mkdir()
    shutil.copy2(executable, contents / "MacOS" / "wake_velocity")
    assets = resources / "assets"
    assets.mkdir()
    for directory in ("shaders", "textures", "title", "fonts"):
        shutil.copytree(build / "assets" / directory, assets / directory)
    (assets / "music").mkdir()
    for name in ("a-little-boat.xm", "a-little-boat-cozy.xm", "acid-wake.xm"):
        shutil.copy2(build / "assets/music" / name, assets / "music")
    # The mapped locations' scenery packs, which the game loads at start (A3.5i).
    for name in ("river", "outer"):
        (assets / "geometry" / name).mkdir(parents=True)
        for file in ("manifest.json", "scenery.bin"):
            shutil.copy2(root / "assets/geometry" / name / file, assets / "geometry" / name)
    # Reference Lua plugins (--plugin) and their authoring tools; authors copy an example
    # somewhere writable to edit it. Nested code must live in Contents/MacOS to be signed.
    package_plugin_tools.install(resources / "authoring", worker,
                                 contents / "MacOS" / "boat_plugin_worker",
                                 contents / "MacOS" / "wake_velocity", assets / "plugins",
                                 "macos-arm64", args.version)
    shutil.copy2(raylib / "LICENSE", resources / "raylib-LICENSE.txt")
    shutil.copy2(raylib / "src/external/glfw/LICENSE.md", resources / "glfw-LICENSE.md")
    shutil.copy2(root / "third_party/jsmn/LICENSE", resources / "jsmn-LICENSE.txt")
    # wake_velocity links ENet for --connect (multiplayer M3).
    shutil.copy2(build / "_deps/enet-src/LICENSE", resources / "enet-LICENSE.txt")
    # wake_velocity links Lua 5.4 for --plugin; CMake extracts its notice from lua.h.
    shutil.copy2(build / "lua-LICENSE.txt", resources / "lua-LICENSE.txt")
    (resources / "build.txt").write_text(
        f"Version: {args.version}\nCommit: {commit}\n"
        f"Uncommitted changes: {dirty}\nArchitecture: {architectures}\n"
        "Signing: ad-hoc; no verified developer identity or Apple notarization\n"
    )
    with (contents / "Info.plist").open("wb") as stream:
        plistlib.dump({
            "CFBundleExecutable": "wake_velocity",
            "CFBundleIdentifier": "org.proteus.boating",
            "CFBundleName": "Wake Velocity",
            "CFBundleDisplayName": "Wake Velocity",
            "CFBundlePackageType": "APPL",
            "CFBundleShortVersionString": bundle_version,
            "CFBundleVersion": bundle_version,
            "NSHighResolutionCapable": True,
            "LSMinimumSystemVersion": minimum_os.group(1),
        }, stream)
    run("codesign", "--force", "--sign", "-", str(contents / "MacOS" / "boat_plugin_worker"))
    run("codesign", "--force", "--sign", "-", str(app))
    run("codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app))
    archive = output / f"wake_velocity-{args.version}-macos-arm64.zip"
    run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(app), str(archive))
    digest = run("shasum", "-a", "256", str(archive)).split()[0]
    archive.with_suffix(".zip.sha256").write_text(f"{digest}  {archive.name}\n")
    print(archive)


if __name__ == "__main__":
    main()
