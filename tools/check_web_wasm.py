#!/usr/bin/env python3
"""Prove the browser game's Wasm carries no Lua parser or VM.

The game links proteus_plugins for its runtime package host, so the linker discards Lua rather
than the build omitting it. Test the linked output, not the link line. The worker is the positive
control: it must contain the same strings, otherwise the check could pass by looking at nothing.
"""
import argparse
from pathlib import Path

# Messages and version text that only the Lua parser/VM contributes.
LUA_STRINGS = (
    b'Lua 5.4',
    b'unexpected symbol',
    b'attempt to index',
    b'stack traceback',
    b'chunk has too many syntax levels',
    b"bad argument #",
)


def contains(path, needle):
    return needle in path.read_bytes()


def assert_no_lua(game, worker=None):
    for needle in LUA_STRINGS:
        if contains(game, needle):
            raise SystemExit(f'{game} contains Lua marker {needle!r}')
    if worker:
        present = [needle for needle in LUA_STRINGS if contains(worker, needle)]
        if len(present) < 2:
            raise SystemExit(
                f'{worker} has too few Lua markers ({present}); the check cannot see them')
        print(f'web_wasm_lua=absent game={game.name} '
              f'controls={len(present)}/{len(LUA_STRINGS)}')
    else:
        print(f'web_wasm_lua=absent game={game.name}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--worker', type=Path)
    args = parser.parse_args()
    assert_no_lua(args.game, args.worker)


if __name__ == '__main__':
    main()
