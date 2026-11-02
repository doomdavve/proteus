#!/usr/bin/env python3
"""Summarize boat_water_review --benchmark reports for the graphics plan.

Usage: summarize_graphics_benchmarks.py DIR [DIR ...]

Reads every *.json report (schema revision 4), groups repeated runs by scenario,
preset and AA, and prints a Markdown table of median p50/p95/p99 values plus the
submission and triangle medians. Nothing is asserted: this is a reporting helper
for measured hardware runs, not a CTest gate.
"""
import json
import pathlib
import statistics
import sys

FIELDS = (
    ('gpu_ms', 'GPU ms'),
    ('render_submit_ms', 'CPU submit ms'),
    ('frame_ms', 'frame ms'),
    ('simulation_ms', 'sim ms'),
)


def median(rows, name, percentile):
    values = [row[name][percentile] for row in rows if row.get(name)]
    return statistics.median(values) if values else None


def spread(rows, name, percentile):
    values = [row[name][percentile] for row in rows if row.get(name)]
    return (min(values), max(values)) if values else (None, None)


def load(paths):
    reports = []
    for path in paths:
        for file in sorted(path.glob('*.json')):
            try:
                report = json.loads(file.read_text())
            except (OSError, ValueError) as error:
                print(f'skip {file}: {error}', file=sys.stderr)
                continue
            if report.get('tool') != 'boat_water_review' or report.get('mode') != 'benchmark':
                continue
            report['_file'] = str(file)
            reports.append(report)
    return reports


def fmt(value):
    return '-' if value is None else f'{value:.2f}'


def main(argv):
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    reports = load([pathlib.Path(arg) for arg in argv[1:]])
    if not reports:
        print('no benchmark reports found', file=sys.stderr)
        return 1
    machines = {(r.get('build'), r.get('platform'), r.get('gl_renderer')) for r in reports}
    print('Machines:')
    for build, platform, renderer in sorted(machines):
        print(f'- build `{build}` / `{platform}` / `{renderer}`')
    print()
    groups = {}
    for report in reports:
        key = (report.get('scenario'), report.get('preset'), report.get('aa'),
               report.get('pacing', 'unpaced'))
        groups.setdefault(key, []).append(report)
    header = ['scenario', 'preset', 'aa', 'pacing', 'runs']
    for _, label in FIELDS:
        header += [f'{label} p50', f'{label} p95', f'{label} p99']
    header += ['subs p50', 'tris p50']
    print('| ' + ' | '.join(header) + ' |')
    print('|' + '|'.join('---' for _ in header) + '|')
    for key in sorted(groups, key=lambda item: tuple(str(part) for part in item)):
        rows = groups[key]
        cells = [str(part if part is not None else '-') for part in key] + [str(len(rows))]
        for name, _ in FIELDS:
            for percentile in ('p50', 'p95', 'p99'):
                cells.append(fmt(median(rows, name, percentile)))
        for name in ('app_submissions', 'scene_triangles'):
            value = median(rows, name, 'p50')
            cells.append('-' if value is None else f'{value:.0f}')
        print('| ' + ' | '.join(cells) + ' |')
    # Repeated-run variation: report the widest p95 spread per group.
    print()
    print('Repeated-run p95 spread (min..max):')
    for key in sorted(groups, key=lambda item: tuple(str(part) for part in item)):
        rows = groups[key]
        parts = []
        for name, label in FIELDS:
            low, high = spread(rows, name, 'p95')
            if low is not None:
                parts.append(f'{label} {low:.2f}..{high:.2f}')
        print(f'- {"/".join(str(part) for part in key)}: ' + ', '.join(parts))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
