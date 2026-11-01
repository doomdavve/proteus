#!/usr/bin/env python3
"""Reject production use of the compiled content selection removed in PD5.

Fixtures keep the shipped voyage, course, race course and rules for validators and review tools.
They are not production content: no production target may link or include them, and the removed
compiled symbols may not appear in src/ or a production target's sources. With --binary, the
symbols are also checked in the built executable using --nm.
"""
import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

REMOVED = (
    'boat_voyage_outer',
    'boat_course_gates',
    'boat_course_default',
    'boat_course_compiled_digest',
    'BOAT_COURSE_START',
    'boat_race_river_course',
    'boat_race_default_rules',
    'boat_match_default_rules',
    'boat_combat_default_rules',
)
PRODUCTION_TARGETS = (
    'wake_velocity',
    'boat_server',
    'boat_bot',
    'boat_plugin_worker',
    'boat_plugin_catalog_gen',
    'plugin_stage',
)
PRODUCTION_SOURCES = (
    'tools/wake_velocity.c',
    'tools/wake_velocity_plugin.c',
    'tools/wake_velocity_net.c',
    'tools/wake_velocity_host.c',
    'tools/boat_server.c',
    'tools/boat_bot.c',
    'tools/boat_plugin_worker.c',
    'tools/boat_plugin_catalog_gen.c',
    'tools/boat_plugin_stage_worker.c',
)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-dir', type=Path, required=True)
parser.add_argument('--binary', type=Path, action='append', default=[])
parser.add_argument('--nm', default='nm')
args = parser.parse_args()
root = args.source_dir.resolve()
failures = []

def words(path):
    try:
        return path.read_text(errors='replace')
    except OSError as error:
        failures.append(f'{path}: {error}')
        return ''

# Removed symbols are absent from every core/session source and every production target source.
sources = list((root / 'src').glob('*.[ch]'))
sources += [root / name for name in PRODUCTION_SOURCES]
for path in sources:
    text = words(path)
    for symbol in REMOVED:
        if re.search(rf'(?<![A-Za-z0-9_]){re.escape(symbol)}(?![A-Za-z0-9_])', text):
            failures.append(f'{path.relative_to(root)}: uses removed compiled symbol {symbol}')

# No production source includes or names the fixture library.
for path in [root / name for name in PRODUCTION_SOURCES]:
    text = words(path)
    if 'boat_fixtures' in text:
        failures.append(f'{path.relative_to(root)}: production source uses boat fixtures')

# No production target links the fixture library, in either native or web CMake files.
for cmake in [root / 'CMakeLists.txt', *sorted((root / 'cmake').glob('*.cmake'))]:
    text = words(cmake)
    for target in PRODUCTION_TARGETS:
        for match in re.finditer(
                rf'target_link_libraries\s*\(\s*{re.escape(target)}\b(.*?)\)', text, re.S):
            if 'proteus_boat_fixtures' in match.group(1):
                failures.append(f'{cmake.relative_to(root)}: {target} links proteus_boat_fixtures')

# The built executables carry neither removed symbols nor fixture content.
nm = shutil.which(args.nm)
if args.binary and nm:
    for binary in args.binary:
        if not binary.exists():
            failures.append(f'{binary}: missing executable')
            continue
        result = subprocess.run([nm, '-g', str(binary)], capture_output=True, text=True)
        if result.returncode != 0:
            failures.append(f'{binary}: nm failed: {result.stderr.strip()}')
            continue
        # Mach-O prefixes every C symbol with an underscore.
        symbols = set(re.findall(r'\b_?([A-Za-z_][A-Za-z0-9_]*)\b', result.stdout))
        for symbol in (*REMOVED, 'boat_fixture_voyage_outer', 'boat_fixture_course_gates',
                'boat_fixture_course_default',
                'boat_fixture_river_course', 'boat_fixture_race_default_rules',
                'boat_fixture_match_default_rules', 'boat_fixture_combat_default_rules'):
            if symbol in symbols:
                failures.append(f'{binary}: contains {symbol}')

if args.binary and not nm:
    print(f'compiled_selection: {args.nm} not found; executables not checked')

if failures:
    for failure in failures:
        print(f'compiled_selection: {failure}', file=sys.stderr)
    raise SystemExit(1)
print('compiled_selection=ok')
