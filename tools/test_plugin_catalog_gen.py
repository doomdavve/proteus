#!/usr/bin/env python3
"""PD1: the build-time catalog generator rejects invalid packages with their diagnostic."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

MANIFEST = {
    "id": "test.free",
    "name": "Test free",
    "api": 1,
    "kind": "free",
    "entry": "main.lua",
    "location": "basin",
    "capabilities": ["free.define"],
    "state_version": 1,
}


def package(root, name, manifest, source):
    directory = root / name
    directory.mkdir()
    (directory / "plugin.json").write_text(json.dumps(manifest))
    (directory / "main.lua").write_text(source)
    return directory


def run(generator, output, packages):
    return subprocess.run([str(generator), "--output", str(output)] + [str(p) for p in packages],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generator", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="plugin-catalog-gen-") as scratch:
        root = Path(scratch)
        output = root / "catalog.c"
        good = package(root, "good", MANIFEST, "return { free = { version = 1 } }\n")
        result = run(args.generator, output, [good])
        if result.returncode != 0 or not output.exists():
            print(result.stdout, file=sys.stderr)
            raise SystemExit("valid package did not generate a catalog")
        objective = dict(MANIFEST)
        objective.update(id="test.objective", kind="voyage", location="outer",
                         capabilities=["voyage.define", "objective.hud"])
        bad_objective = package(root, "objective", objective,
                                "return { init=function() end, update=function() end,"
                                " voyage={shelters={}} }\n")
        result = run(args.generator, output, [bad_objective])
        if result.returncode == 0 or "objective" not in result.stdout:
            raise SystemExit("objective package was not rejected")
        bad_schema = package(root, "schema", MANIFEST, "return { free = { version = 2 } }\n")
        result = run(args.generator, output, [bad_schema])
        if result.returncode == 0 or "free.version" not in result.stdout:
            raise SystemExit("invalid free schema was not rejected")
        duplicate = package(root, "duplicate", MANIFEST, "return { free = { version = 1 } }\n")
        result = run(args.generator, output, [good, duplicate])
        if result.returncode == 0 or "duplicate" not in result.stdout:
            raise SystemExit("duplicate package id was not rejected")
    print("plugin_catalog_gen=ok")


if __name__ == "__main__":
    main()
