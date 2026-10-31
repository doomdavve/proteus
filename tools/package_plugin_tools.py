#!/usr/bin/env python3
"""Install the plugin authoring tools, worker and examples into a native release package."""

import argparse
import json
import os
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ("plugin_author.py", "plugin_schema.py", "plugin_anchors.json")
EXAMPLES = ("langedrag", "river-sprint", "shelter-tour", "harbor-life", "basin-free",
            "river-free", "time-trial")


def install(authoring, worker, worker_path, game_path, plugins, package, version):
    """Copy into AUTHORING and PLUGINS; WORKER goes to WORKER_PATH. GAME_PATH must be where
    the package's wake_velocity will live. Every path the layout records is relative."""
    authoring, plugins = Path(authoring), Path(plugins)
    authoring.mkdir(parents=True)
    for name in TOOLS:
        shutil.copy2(ROOT / "tools" / name, authoring / name)
    shutil.copy2(ROOT / "docs/PLUGIN_API.md", authoring / "PLUGIN_API.md")
    shutil.copy2(ROOT / "tools/plugin_tools_README.txt", authoring / "README.txt")
    Path(worker_path).parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(worker, worker_path)
    plugins.mkdir(parents=True, exist_ok=True)
    for name in EXAMPLES:
        shutil.copytree(ROOT / "plugins" / name, plugins / name)

    def relative(path):
        return Path(os.path.relpath(Path(path).resolve(), authoring.resolve()))

    layout = {"layout": 1, "package": package, "version": version,
              "worker": relative(worker_path).as_posix(), "game": relative(game_path).as_posix(),
              "anchors": "plugin_anchors.json", "api_document": "PLUGIN_API.md",
              "examples": [relative(plugins / name).as_posix() for name in EXAMPLES]}
    (authoring / "plugin_tools.json").write_text(json.dumps(layout, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--authoring", type=Path, required=True)
    parser.add_argument("--worker", type=Path, required=True, help="the built boat_plugin_worker")
    parser.add_argument("--worker-path", type=Path, required=True)
    parser.add_argument("--game-path", type=Path, required=True)
    parser.add_argument("--plugins", type=Path, required=True)
    parser.add_argument("--package", required=True, help="for example windows-x64")
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    install(args.authoring, args.worker, args.worker_path, args.game_path, args.plugins,
            args.package, args.version)


if __name__ == "__main__":
    main()
