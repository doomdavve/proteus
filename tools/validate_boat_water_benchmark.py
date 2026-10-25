"""River benchmark report contract for boat_water_review --benchmark.

--options checks argument and output-path rejection without a graphics context. The default
run needs a GPU context: it checks row counts, warmup exclusion, recomputed percentiles,
finite values, route completion and final-state parity with the existing --check controller.
Timing thresholds are deliberately not asserted.
"""
import csv
import json
import math
import pathlib
import subprocess
import sys
import tempfile

exe = sys.argv[1]
options_only = '--options' in sys.argv[2:]
WARMUP = 120
STATS = ('simulation_ms', 'render_submit_ms', 'present_ms', 'frame_ms', 'frame_interval_ms',
         'gpu_ms')


def run(*args):
    return subprocess.run([exe, *map(str, args)], capture_output=True, text=True)


def final_state(stdout):
    lines = [line for line in stdout.splitlines() if line.startswith('final_state ')]
    assert len(lines) == 1, stdout
    return lines[0][len('final_state '):]


with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    rejected = [
        ['--aa', 'off'],
        ['--benchmark', root / 'x'],
        ['--benchmark', root / 'x', '--aa', '2x2'],
        ['--benchmark', root / 'x', '--aa', 'off', '--check'],
        ['--benchmark', root / 'x', '--aa', 'off', '--check-gpu'],
        ['--benchmark', root / 'x', '--aa', 'off', '--output', root / 'out'],
        ['--benchmark', root / 'x', '--aa', 'off', '--sequence'],
        ['--benchmark', root / 'x', '--aa', 'off', '--renderer', 'cpu'],
        ['--benchmark', '', '--aa', 'off'],
        ['--benchmark', f'{root}/', '--aa', 'off'],
        ['--benchmark', 'x' * 4001, '--aa', 'off'],
        ['--aa', 'off', '--benchmark'],
    ]
    for args in rejected:
        result = run(*args)
        assert result.returncode != 0, args
    # Unusable prefixes fail before a window opens and leave no partial report.
    assert run('--benchmark', root / 'missing' / 'r', '--aa', 'off').returncode != 0
    (root / 'blocked.json').mkdir()
    assert run('--benchmark', root / 'blocked', '--aa', 'off').returncode != 0
    assert not (root / 'blocked.csv').exists()
    if options_only:
        print('river benchmark option and output-path rejection pass')
        sys.exit(0)

    check = run('--check', '--waves')
    assert check.returncode == 0, check.stderr
    expected_state = final_state(check.stdout)
    prefix = root / 'waves-off'
    result = run('--benchmark', prefix, '--aa', 'off', '--waves')
    assert result.returncode == 0, result.stderr
    report = json.loads(prefix.with_suffix('.json').read_text())
    with prefix.with_suffix('.csv').open() as stream:
        rows = list(csv.DictReader(stream))

    assert report['schema_revision'] == 1 and report['mode'] == 'benchmark'
    assert report['renderer'] == 'gpu' and report['aa'] == 'off'
    assert report['preset'] == 'waves' and report['window'] == 'hidden'
    assert report['pacing'] == 'unpaced' and report['vsync_hint'] is False
    assert report['internal_resolution'] == [320, 200]
    assert report['build'] and report['platform'] and report['gl_renderer']
    assert report['tick_hz'] == 120 and report['ticks_per_frame'] == 2
    assert report['route_completed'] is True and report['destination'] == 5
    assert report['final_state'] == expected_state == final_state(result.stdout)
    for key in ('app_submissions', 'driver_draw_calls', 'scene_triangles'):
        assert report[key] is None

    frames = report['frames']
    assert len(rows) == frames and report['warmup_frames'] == WARMUP
    assert report['measured_frames'] == frames - WARMUP > 0
    previous_end = 0
    for i, row in enumerate(rows):
        assert int(row['frame']) == i and int(row['warmup']) == (i < WARMUP)
        first, end, ticks = int(row['first_tick']), int(row['end_tick']), int(row['ticks'])
        assert first == previous_end and end - first == ticks
        # Frame 0 draws the initial pose; the last frame may end on an odd arrival tick.
        assert ticks == (0 if i == 0 else 2) or (i == frames - 1 and ticks == 1)
        previous_end = end
        for key in ('x', 'z', 'heading', 'simulation_ms', 'render_submit_ms', 'present_ms',
                    'frame_ms'):
            assert math.isfinite(float(row[key])), (i, key)
        for key in ('simulation_ms', 'render_submit_ms', 'present_ms', 'frame_ms'):
            assert float(row[key]) >= 0, (i, key)
        parts = sum(float(row[key]) for key in ('simulation_ms', 'render_submit_ms',
                                                 'present_ms'))
        assert abs(parts - float(row['frame_ms'])) < 1e-3 + 1e-5 * parts, i
        assert (row['frame_interval_ms'] == '') == (i == 0)
        for key in ('frame_interval_ms', 'gpu_ms'):
            if row[key]:
                value = float(row[key])
                assert math.isfinite(value) and value >= 0, (i, key)
        assert int(row['wave_patch_rebuilds']) >= 0 and int(row['wake_uploads']) >= 0
    assert previous_end == report['total_ticks'] == int(expected_state.split()[0][5:])
    assert report['warmup_end_tick'] == int(rows[WARMUP - 1]['end_tick'])

    measured = rows[WARMUP:]
    for name in STATS:
        values = sorted(float(row[name]) for row in measured if row[name])
        stats = report[name]
        if not values:
            assert stats is None, name
            continue
        n = len(values)
        assert stats['samples'] == n, name
        for key, index in (('p50', (n - 1) * 50 // 100), ('p95', (n - 1) * 95 // 100),
                           ('p99', (n - 1) * 99 // 100), ('max', n - 1)):
            assert stats[key] == values[index], (name, key)
        assert abs(stats['mean'] - sum(values) / n) <= 1e-4 * max(1, values[-1]), name
    assert report['simulation_ms']['samples'] == len(measured)
    gpu_rows = sum(1 for row in measured if row['gpu_ms'])
    assert report['gpu_samples'] == gpu_rows
    if report['gpu_timer'] is None:
        assert report['gpu_ms'] is None and gpu_rows == 0
    else:
        assert report['gpu_samples'] + report['gpu_samples_skipped'] == len(measured)
    deadline = report['deadline_ms']
    assert abs(deadline - 1000 / 60) < 1e-6
    assert report['frame_deadline_misses'] == sum(
        float(row['frame_ms']) > deadline for row in measured)
    assert report['interval_deadline_misses'] == sum(
        float(row['frame_interval_ms']) > deadline for row in measured)
    assert report['wave_patch_rebuilds_measured'] == sum(
        int(row['wave_patch_rebuilds']) for row in measured)
    assert report['wave_patch_rebuilds'] == sum(int(row['wave_patch_rebuilds']) for row in rows)
    assert report['wave_patch_rebuilds'] > 0 and report['wave_vertex_capacity'] > 0
print('river benchmark rows, warmup, percentiles, route completion and final-state parity pass')
