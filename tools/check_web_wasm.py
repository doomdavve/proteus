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


def leb(data, at):
    value = shift = 0
    while True:
        byte = data[at]
        at += 1
        value |= (byte & 0x7f) << shift
        if not byte & 0x80:
            return value, at
        shift += 7


def wasm_symbols(path):
    """Imported and exported names, which survive stripping."""
    data = path.read_bytes()
    if data[:4] != b'\0asm':
        raise SystemExit(f'{path} is not a Wasm module')
    at, names = 8, []
    while at < len(data):
        section = data[at]
        size, at = leb(data, at + 1)
        end = at + size
        if section in (2, 7):  # Import and export sections.
            count, cursor = leb(data, at)
            for _ in range(count):
                length, cursor = leb(data, cursor)
                name = data[cursor:cursor + length].decode('utf-8', 'replace')
                names.append(name)
                cursor += length
                if section == 2:
                    length, cursor = leb(data, cursor)
                    cursor += length  # Field name.
                    kind = data[cursor]
                    cursor += 1
                    if kind == 0:
                        _, cursor = leb(data, cursor)
                    elif kind == 1:
                        cursor += 1
                        flags, cursor = leb(data, cursor)
                        _, cursor = leb(data, cursor)
                        if flags & 1:
                            _, cursor = leb(data, cursor)
                    elif kind == 2:
                        flags, cursor = leb(data, cursor)
                        _, cursor = leb(data, cursor)
                        if flags & 1:
                            _, cursor = leb(data, cursor)
                    elif kind == 3:
                        cursor += 2
                else:
                    cursor += 1  # Kind.
                    _, cursor = leb(data, cursor)
        at = end
    return names


def assert_no_lua(game, worker=None):
    for needle in LUA_STRINGS:
        if contains(game, needle):
            raise SystemExit(f'{game} contains Lua marker {needle!r}')
    lua_symbols = [name for name in wasm_symbols(game) if 'lua' in name.lower()]
    if lua_symbols:
        raise SystemExit(f'{game} imports or exports Lua symbols: {lua_symbols}')
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
