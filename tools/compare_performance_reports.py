#!/usr/bin/env python3
"""Compare Wake Velocity performance report bundles on the Mac.

Usage:
  compare_performance_reports.py REPORT [--baseline REPORT] [--output FILE.md]
      [--html FILE.html]

REPORT and --baseline accept an extracted bundle directory or a .zip produced by the in-game
Export performance action and the Windows Collect performance launcher. Only the documented
files are read, with bounded sizes. Two reports are called comparable only when the capture kind,
map, preset, settings and machine identity agree; different machines are shown side by side and
are never described as a code regression. Output uses stdlib only.
"""
import argparse
import csv
import io
import json
import math
import pathlib
import sys
import zipfile

MAX_MEMBER = 32 * 1024 * 1024
MAX_TOTAL = 96 * 1024 * 1024
MAX_SAMPLES = 20000
EXPECTED = {'manifest.json', 'summary.json', 'samples.csv', 'summary.html', 'build.txt'}
REQUIRED_SAMPLE_FIELDS = ('sample', 'time', 'frame_ms', 'interval_ms', 'simulation_ms',
                          'render_submit_ms', 'present_ms', 'gpu_ms', 'has_gpu')
MEASURES = ('frame_ms', 'interval_ms', 'simulation_ms', 'render_submit_ms', 'present_ms',
            'gpu_ms')


class ReportError(Exception):
    pass


def read_bundle(path):
    """Return a mapping of the five documented files, from a directory or bounded ZIP."""
    path = pathlib.Path(path)
    if path.is_dir():
        files = {}
        for name in EXPECTED:
            file = path / name
            if not file.is_file():
                raise ReportError(f'missing {name} in {path}')
            if file.stat().st_size > MAX_MEMBER:
                raise ReportError(f'{name} is larger than the report bound')
            files[name] = file.read_text()
        return files
    if not path.is_file():
        raise ReportError(f'no report at {path}')
    if path.stat().st_size > MAX_TOTAL:
        raise ReportError('archive is larger than the report bound')
    files = {}
    total = 0
    try:
        with zipfile.ZipFile(path) as archive:
            for member in archive.infolist():
                if member.filename not in EXPECTED:
                    raise ReportError(f'unexpected archive member {member.filename}')
                if member.file_size > MAX_MEMBER:
                    raise ReportError(f'{member.filename} is larger than the report bound')
                total += member.file_size
                if total > MAX_TOTAL:
                    raise ReportError('archive contents exceed the report bound')
                files[member.filename] = archive.read(member).decode('utf-8')
    except zipfile.BadZipFile as error:
        raise ReportError(f'not a ZIP report: {error}') from error
    missing = EXPECTED - files.keys()
    if missing:
        raise ReportError(f'archive is missing {", ".join(sorted(missing))}')
    return files


def parse_report(path):
    files = read_bundle(path)
    try:
        manifest = json.loads(files['manifest.json'])
        summary = json.loads(files['summary.json'])
    except ValueError as error:
        raise ReportError(f'invalid JSON: {error}') from error
    if manifest.get('format') != 'wake-velocity-performance' or manifest.get('revision') != 1:
        raise ReportError('unsupported report format or revision')
    sampling = manifest.get('sampling') or {}
    samples = int(sampling.get('samples', -1))
    if samples < 0 or samples > MAX_SAMPLES:
        raise ReportError('sample count outside the report bound')
    reader = csv.DictReader(io.StringIO(files['samples.csv']))
    if not reader.fieldnames or any(field not in reader.fieldnames
                                    for field in REQUIRED_SAMPLE_FIELDS):
        raise ReportError('samples.csv is missing required fields')
    rows = []
    for row in reader:
        rows.append(row)
        if len(rows) > MAX_SAMPLES:
            raise ReportError('samples.csv exceeds the sample bound')
    if len(rows) != samples:
        raise ReportError(f'manifest says {samples} samples, csv has {len(rows)}')
    for row in rows:
        for field in ('time', 'frame_ms', 'interval_ms', 'simulation_ms', 'render_submit_ms',
                      'present_ms'):
            value = float(row[field])
            if not math.isfinite(value) or value < 0:
                raise ReportError(f'non-finite or negative {field}')
        if row.get('gpu_ms'):
            value = float(row['gpu_ms'])
            if not math.isfinite(value) or value < 0:
                raise ReportError('non-finite GPU sample')
    return manifest, summary, rows


def identity(manifest):
    session = manifest.get('session') or {}
    system = manifest.get('system') or {}
    tool = manifest.get('tool') or {}
    return {
        'kind': manifest.get('capture_kind'),
        'map': session.get('map'),
        'preset': session.get('preset'),
        'vsync': session.get('vsync'),
        'aa': session.get('aa'),
        'machine': (tool.get('platform'), system.get('gl_renderer')),
    }


def measure(summary, name):
    stats = summary.get(name) or {}
    return stats.get('p50'), stats.get('p95'), stats.get('p99'), stats.get('samples')


def reasons_incomparable(left, right):
    a, b = identity(left), identity(right)
    reasons = []
    for key in ('kind', 'map', 'preset', 'vsync', 'aa'):
        if a[key] != b[key]:
            reasons.append(f'{key} differs ({a[key]} vs {b[key]})')
    return reasons


def fmt(value):
    return '-' if value is None else f'{value:.2f}'


def markdown(primary_manifest, primary_summary, primary_rows, baseline=None):
    lines = ['# Performance comparison', '']
    tool = primary_manifest.get('tool') or {}
    system = primary_manifest.get('system') or {}
    session = primary_manifest.get('session') or {}
    lines.append(f'- Candidate: `{tool.get("commit")}` {tool.get("build_configuration")} on '
                 f'`{system.get("gl_renderer")}` ({tool.get("platform")})')
    lines.append(f'- Capture: {primary_manifest.get("capture_kind")}; map '
                 f'`{session.get("map")}`; preset `{session.get("preset")}`; '
                 f'vsync {session.get("vsync")}; AA {session.get("aa")}')
    lines.append(f'- Samples: {len(primary_rows)} over '
                 f'{(primary_summary.get("duration_s") or 0):.2f} s; '
                 f'{primary_summary.get("gpu_samples", 0)} GPU / '
                 f'{primary_summary.get("gpu_missing", 0)} missing')
    lines.append('')
    if baseline is None:
        lines += ['No baseline supplied; this is a single-report description.', '']
        pairs = [('Candidate', primary_summary)]
    else:
        base_manifest, base_summary, _ = baseline
        reasons = reasons_incomparable(primary_manifest, base_manifest)
        same_machine = identity(primary_manifest)['machine'] == identity(base_manifest)['machine']
        if reasons:
            lines.append('**Not comparable:** ' + '; '.join(reasons) +
                         '. No numerical regression verdict is made.')
        elif not same_machine:
            lines.append('**Different machines:** values are shown side by side; their absolute '
                         'difference is not a code regression.')
        else:
            lines.append('**Comparable:** same machine, capture kind and settings.')
        lines.append('')
        pairs = [('Baseline', base_summary), ('Candidate', primary_summary)]
        lines += ['| Measure | Baseline p50/p95/p99 | Candidate p50/p95/p99 | p95 delta |',
                  '| --- | --- | --- | --- |']
        for name in MEASURES:
            b = measure(base_summary, name)
            c = measure(primary_summary, name)
            delta = '-' if b[1] is None or c[1] is None else f'{c[1] - b[1]:+.2f} ms'
            lines.append(f'| {name} | {fmt(b[0])}/{fmt(b[1])}/{fmt(b[2])} | '
                         f'{fmt(c[0])}/{fmt(c[1])}/{fmt(c[2])} | {delta} |')
        lines.append('')
        if not reasons and same_machine:
            b = measure(base_summary, 'frame_ms')[1]
            c = measure(primary_summary, 'frame_ms')[1]
            if b and c:
                lines.append(f'Frame p95 moved {c - b:+.2f} ms ({100 * (c - b) / b:+.1f}%).')
                lines.append('')
    lines += ['| Measure | p50 | p95 | p99 | max |',
              '| --- | --- | --- | --- | --- |']
    for name in MEASURES:
        stats = primary_summary.get(name) or {}
        lines.append(f'| {name} | {fmt(stats.get("p50"))} | {fmt(stats.get("p95"))} | '
                     f'{fmt(stats.get("p99"))} | {fmt(stats.get("max"))} |')
    lines += ['', 'Missing GPU samples stay missing; they are never counted as zero. '
                  'Live driving is hitch evidence, not a controlled benchmark.', '']
    return '\n'.join(lines)


def downsample(rows, field, width=1000):
    values = [float(row[field]) for row in rows if row.get(field)]
    if len(values) <= width:
        return values
    step = len(values) / width
    return [values[min(len(values) - 1, int(i * step))] for i in range(width)]


def html_document(primary_manifest, primary_rows, baseline=None):
    series = [('candidate', '#ffd246', primary_rows)]
    if baseline:
        series.append(('baseline', '#69a', baseline[2]))
    maximum = 1.0
    for _, _, rows in series:
        values = downsample(rows, 'frame_ms')
        if values:
            maximum = max(maximum, max(values))
    width, height = 1000, 280
    parts = [f'<svg viewBox="0 0 {width} {height}" width="100%" height="{height}" '
             'style="background:#0b1218">']
    for index, (label, color, rows) in enumerate(series):
        values = downsample(rows, 'frame_ms')
        if not values:
            continue
        points = []
        for i, value in enumerate(values):
            x = width * i / max(1, len(values) - 1)
            y = height - 10 - (height - 20) * value / maximum
            points.append(f'{x:.1f},{y:.1f}')
        parts.append(f'<polyline fill="none" stroke="{color}" stroke-width="1" '
                     f'points="{" ".join(points)}"/>')
        parts.append(f'<text x="8" y="{18 + index * 14}" fill="{color}">{label}</text>')
    parts.append('</svg>')
    tool = primary_manifest.get('tool') or {}
    system = primary_manifest.get('system') or {}
    return ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
            '<title>Wake Velocity comparison</title><style>'
            'body{font:14px system-ui,sans-serif;background:#101820;color:#e8eef4;margin:24px}'
            '</style></head><body><h1>Performance comparison</h1>'
            f'<p>{tool.get("commit")} &middot; {tool.get("build_configuration")} &middot; '
            f'{system.get("gl_renderer")}</p>'
            + ''.join(parts) +
            '<p>Frame wall over the retained window. Read summary.json for all measures.</p>'
            '</body></html>\n')


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report')
    parser.add_argument('--baseline')
    parser.add_argument('--output')
    parser.add_argument('--html')
    args = parser.parse_args(argv[1:])
    try:
        primary = parse_report(args.report)
        baseline = parse_report(args.baseline) if args.baseline else None
        text = markdown(*primary, baseline)
        if args.output:
            pathlib.Path(args.output).write_text(text)
        else:
            print(text)
        if args.html:
            pathlib.Path(args.html).write_text(html_document(primary[0], primary[2], baseline))
    except (ReportError, OSError) as error:
        print(f'compare_performance_reports: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
