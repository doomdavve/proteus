#!/usr/bin/env python3
"""Check that the browser author's wire magic matches the C plugin wire version."""
import argparse
import re
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
    args = parser.parse_args()
    header = (args.root / 'src/boat_plugin_wire.h').read_text()
    match = re.search(r'^#define\s+BOAT_PLUGIN_WIRE_VERSION\s+\(?(0x[0-9a-fA-F]+)u?\)?',
                      header, re.MULTILINE)
    if not match:
        raise SystemExit('boat_plugin_wire.h has no BOAT_PLUGIN_WIRE_VERSION')
    expected = int(match.group(1), 16)
    script = (args.root / 'web/plugin_author.js').read_text()
    found = re.search(r'const\s+WIRE_MAGIC\s*=\s*(0x[0-9a-fA-F]+)\s*;', script)
    if not found:
        raise SystemExit('web/plugin_author.js has no WIRE_MAGIC constant')
    actual = int(found.group(1), 16)
    if actual != expected:
        raise SystemExit(
            f'web/plugin_author.js WIRE_MAGIC is 0x{actual:08x}; '
            f'BOAT_PLUGIN_WIRE_VERSION is 0x{expected:08x}')
    print(f'web_plugin_wire=ok version=0x{expected:08x}')


if __name__ == '__main__':
    main()
