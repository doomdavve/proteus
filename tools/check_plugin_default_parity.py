#!/usr/bin/env python3
"""PD1 parity gate: bundled default sessions capture byte-identically to --no-plugins."""

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

CASES = (
    ("basin", ["--location", "basin"], "3"),
    ("river", ["--location", "river"], "3"),
    ("outer", ["--location", "outer"], "3"),
    ("course", ["--course"], "1"),
)


def capture(game, root, output, mode, seconds, compiled):
    command = [str(game), "--renderer", "cpu", "--time", seconds,
               "--screenshot", str(output)]
    command += mode
    if compiled:
        command.append("--no-plugins")
    result = subprocess.run(command, cwd=root, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    if result.returncode != 0:
        print(result.stdout, file=sys.stderr)
        raise SystemExit(f"parity capture failed: {' '.join(command)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    args = parser.parse_args()
    game = args.game.resolve()
    with tempfile.TemporaryDirectory(prefix="plugin-default-parity-") as scratch:
        scratch = Path(scratch)
        for name, mode, seconds in CASES:
            default = scratch / f"{name}-default.png"
            compiled = scratch / f"{name}-compiled.png"
            capture(game, args.root, default, mode, seconds, False)
            capture(game, args.root, compiled, mode, seconds, True)
            if default.read_bytes() != compiled.read_bytes():
                raise SystemExit(f"plugin_default_parity: {name} differs from --no-plugins")
            print(f"parity {name}=ok")
    print("plugin_default_parity=ok")


if __name__ == "__main__":
    main()
