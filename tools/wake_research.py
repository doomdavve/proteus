#!/usr/bin/env python3
"""Run alternating R0 baseline/diagnostic/wake-off measurements; retain raw evidence."""
import argparse
import csv
import hashlib
import json
import math
import platform
from pathlib import Path
import subprocess

TIMINGS = ('source_ms', 'query_ms', 'diagnostic_ms', 'pack_ms', 'submit_ms',
           'wait_ms', 'total_ms', 'gpu_ms')


def percentile(values, fraction):
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * fraction) - 1)]


def summarize(prefix):
    meta = json.loads(prefix.with_suffix('.json').read_text())
    with prefix.with_suffix('.csv').open() as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != meta['measured_frames'] or len(rows) != 3600:
        raise ValueError(f'{prefix}: expected 3600 measured frames')
    result = {'metadata': meta, 'timings': {}, 'counters': {}}
    for key in TIMINGS:
        supported = not (meta.get('aggregate') and key != 'total_ms')
        supported &= not (key in ('submit_ms', 'wait_ms', 'gpu_ms') and not meta['graphics'])
        supported &= key != 'gpu_ms' or meta['gpu_timer']
        if not supported:
            result['timings'][key] = None
            continue
        values = [float(row[key]) for row in rows]
        if not all(math.isfinite(v) and v >= 0 for v in values):
            raise ValueError(f'{prefix}: invalid {key}')
        result['timings'][key] = {f'p{p}': percentile(values, p / 100) for p in (50, 95, 99)}
    for key in ('live', 'peak', 'traffic_live', 'slots_examined', 'active_examined',
                'expensive_derived', 'packed_bytes', 'stamp_pieces', 'stamp_outside',
                'stamp_texels', 'stamp_bytes', 'history_bytes', 'record_upload_bytes',
                'max_record_length'):
        values = [float(row[key]) for row in rows if row[key]]
        if key.startswith(('stamp_', 'history_', 'record_upload_')) and not meta['graphics']:
            values = []
        result['counters'][key] = {'min': min(values), 'max': max(values)} if values else None
    if meta['diagnostics'] and not meta['off'] and meta['path'] == 'straight':
        if not all(int(row['expensive_derived']) > 0 for row in rows):
            raise ValueError(f'{prefix}: straight workload failed to sample the wake')
    result['resolution_limited'] = [key for key, stats in result['timings'].items() if stats and stats['p50'] <= 5 * meta['timer_min_ms']]
    result['births'] = sum(int(row['births']) for row in rows)
    # At 120 Hz and a 60-tick emission cadence, continuously wet paths emit 120 per hull.
    if meta['path'] in ('straight', 'circle', 'wide', 'eight', 'reverse') and not meta['off']:
        if result['births'] != 120 * meta['emitters'] or meta['saturated_frames'] != 3600:
            raise ValueError(f'{prefix}: unsaturated continuous fixture')
    result['artifacts'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in (prefix.with_suffix('.csv'), prefix.with_suffix('.json'))}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--gpu', action='store_true')
    parser.add_argument('--high', action='store_true')
    parser.add_argument('--location', choices=('basin', 'outer'), default='basin')
    parser.add_argument('--traffic', action='store_true')
    parser.add_argument('--paths', nargs='+', default=['straight', 'wide', 'circle', 'eight', 'reverse'])
    parser.add_argument('--emitters', type=int, choices=(1, 4), nargs='+', default=[1, 4])
    parser.add_argument('--layouts', choices=('dense', 'dispersed'), nargs='+', default=['dense', 'dispersed'])
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'platform': platform.platform(), 'revision': subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], text=True).strip(), 'binary_sha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        'source_sha256': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
            map(Path, ['tools/wake_research.c', 'tools/wake_research.py', 'src/core/boat_wake_objects.c', 'assets/shaders/waterfront_water.vs', 'assets/shaders/waterfront_water.fs'])},
        'runs': [], 'comparisons': []}
    binary = str(args.binary.resolve())
    for path in args.paths:
        for emitters in args.emitters:
            for layout in args.layouts:
                comparisons = []
                for repeat in range(3):
                    runs = {}
                    # Alternate instrumentation order; off is a separate interaction control.
                    order = ('aggregate', 'baseline', 'diagnostic', 'off') if repeat % 2 == 0 else ('diagnostic', 'baseline', 'aggregate', 'off')
                    for mode in order:
                        prefix = args.output / f'{path}-{emitters}-{layout}-{repeat}-{mode}'
                        command = [binary, '--output', str(prefix), '--path', path,
                                   '--emitters', str(emitters), '--location', args.location]
                        if layout == 'dispersed': command.append('--dispersed')
                        if mode == 'diagnostic': command.append('--diagnostics')
                        if mode == 'aggregate': command.append('--aggregate')
                        if mode == 'off': command.append('--off')
                        for enabled, flag in ((args.gpu, '--gpu'), (args.high, '--high'), (args.traffic, '--traffic')):
                            if enabled: command.append(flag)
                        print(' '.join(command), flush=True)
                        subprocess.run(command, check=True)
                        run = summarize(prefix)
                        run['command'] = command
                        run['prefix'] = str(prefix)
                        report['runs'].append(run)
                        runs[mode] = run
                    if runs['baseline']['metadata']['hash'] != runs['diagnostic']['metadata']['hash']:
                        raise ValueError('diagnostic instrumentation changed records or probes')
                    if runs['aggregate']['metadata']['hash'] != runs['baseline']['metadata']['hash']:
                        raise ValueError('phase timers changed records or probes')
                    entry = {'path': path, 'emitters': emitters, 'layout': layout, 'repeat': repeat,
                             'identical_field_hash': True, 'delta': {}}
                    for mode in ('diagnostic', 'off'):
                        entry['delta'][mode] = {}
                        for metric in TIMINGS:
                            a = runs['baseline']['timings'][metric]
                            b = runs[mode]['timings'][metric]
                            entry['delta'][mode][metric] = None if a is None or b is None else {
                                p: {'ms': b[p] - a[p], 'percent': 100 * (b[p] / a[p] - 1) if a[p] > 5 * runs['baseline']['metadata']['timer_min_ms'] else None}
                                for p in a}
                    a = runs['aggregate']['timings']['total_ms']
                    b = runs['baseline']['timings']['total_ms']
                    entry['timer_overhead'] = {p: {'ms': b[p] - a[p], 'percent': 100 * (b[p] / a[p] - 1) if a[p] > 5 * runs['baseline']['metadata']['timer_min_ms'] else None} for p in a}
                    comparisons.append(entry)
                report['comparisons'].extend(comparisons)
                # Preserve completed groups if a later GPU/platform run fails.
                (args.output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f"summary: {args.output / 'summary.json'}", flush=True)


if __name__ == '__main__':
    main()
