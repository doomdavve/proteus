"""River and stress benchmark report contract for boat_water_review --benchmark.

--options checks argument and output-path rejection without a graphics context. The GPU run
checks row counts, warmup exclusion, recomputed percentiles, finite values, route completion and
final-state parity with the existing --check controller. The stress scenario is display-free
through --stress-state and must report the same fleet state and saturated wake counts in its
benchmark run. Timing thresholds are deliberately not asserted.
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
STRESS_WARMUP = 120
STRESS_FRAMES = 1020
STATS = ('simulation_ms', 'render_submit_ms', 'present_ms', 'frame_ms', 'frame_interval_ms',
         'gpu_ms', 'app_submissions', 'scene_triangles', 'map_submissions', 'map_triangles')
PASSES = ('shadow', 'reflection', 'main')
GROUPS = ('map', 'water', 'object')


def run(*args):
    return subprocess.run([exe, *map(str, args)], capture_output=True, text=True)


def final_state(stdout):
    lines = [line for line in stdout.splitlines() if line.startswith('final_state ')]
    assert len(lines) == 1, stdout
    return lines[0][len('final_state '):]


def stress_state(stdout):
    lines = [line for line in stdout.splitlines() if line.startswith('stress_state ')]
    assert len(lines) == 1, stdout
    return lines[0][len('stress_state '):]


def percentiles(report, rows, name, start):
    values = sorted(float(row[name]) for row in rows[start:] if row[name])
    stats = report[name]
    if not values:
        assert stats is None, name
        return
    n = len(values)
    assert stats['samples'] == n, name
    for key, index in (('p50', (n - 1) * 50 // 100), ('p95', (n - 1) * 95 // 100),
                       ('p99', (n - 1) * 99 // 100), ('max', n - 1)):
        assert stats[key] == values[index], (name, key)
    assert abs(stats['mean'] - sum(values) / n) <= 1e-4 * max(1, values[-1]), name


with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    rejected = [
        ['--aa', 'off'],
        ['--benchmark', root / 'x'],
        ['--benchmark', root / 'x', '--aa', '2x2'],
        ['--benchmark', root / 'x', '--aa', 'off', '--check'],
        ['--benchmark', root / 'x', '--aa', 'off', '--check-gpu'],
        ['--benchmark', root / 'x', '--aa', 'off', '--check-gpu-lifecycle'],
        ['--benchmark', root / 'x', '--aa', 'off', '--output', root / 'out'],
        ['--benchmark', root / 'x', '--aa', 'off', '--sequence'],
        ['--benchmark', root / 'x', '--aa', 'off', '--renderer', 'cpu'],
        ['--benchmark', root / 'x', '--aa', 'off', '--scenario', 'none'],
        ['--benchmark', '', '--aa', 'off'],
        ['--benchmark', f'{root}/', '--aa', 'off'],
        ['--benchmark', 'x' * 4001, '--aa', 'off'],
        ['--aa', 'off', '--benchmark'],
        ['--visible', '--pacing', 'vsync'],
        ['--benchmark', root / 'x', '--aa', 'off', '--pacing', 'vsync'],
        ['--benchmark', root / 'x', '--aa', 'off', '--visible', '--pacing', 'fast'],
        ['--stress-state', '--waves'],
        ['--stress-state', '--benchmark', root / 'x'],
    ]
    for args in rejected:
        result = run(*args)
        assert result.returncode != 0, args
    # Unusable prefixes fail before a window opens and leave no partial report.
    assert run('--benchmark', root / 'missing' / 'r', '--aa', 'off').returncode != 0
    (root / 'blocked.json').mkdir()
    assert run('--benchmark', root / 'blocked', '--aa', 'off').returncode != 0
    assert not (root / 'blocked.csv').exists()
    # The stress fixture runs without a window or graphics context.
    state_result = run('--stress-state')
    assert state_result.returncode == 0, state_result.stderr
    expected_stress = stress_state(state_result.stdout)
    assert 'hash=' in expected_stress and 'wake_live=128' in expected_stress
    assert 'traffic_live=128' in expected_stress
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

    assert report['schema_revision'] == 4 and report['mode'] == 'benchmark'
    assert report['scenario'] == 'river-review' and report['route'] == 'river-review'
    assert report['tile_size'] == 1024
    assert report['renderer'] == 'gpu' and report['aa'] == 'off'
    assert report['preset'] == 'waves' and report['window'] == 'hidden'
    assert report['pacing'] == 'unpaced' and report['vsync_hint'] is False
    assert report['internal_resolution'] == [320, 200]
    assert report['build'] and report['platform'] and report['gl_renderer']
    assert report['tick_hz'] == 120 and report['ticks_per_frame'] == 2
    assert report['run_completed'] is True and report['route_completed'] is True
    assert report['destination'] == 5
    assert report['final_state'] == expected_state == final_state(result.stdout)
    assert report['driver_draw_calls'] is None and report['submission_counter']
    assert report['wake_objects'] is None

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
        assert int(row['wake_live']) == int(row['wake_peak_live']) == 0
        assert int(row['wake_emitters']) == 0 and int(row['traffic_wake_live']) == 0
        # Pass and group splits partition the mesh submissions; the remainder is one batched
        # sky quad (two triangles) for each of the reflection and main passes.
        for kind, total, sky in (('submissions', 'app_submissions', 2),
                                 ('triangles', 'scene_triangles', 4)):
            by_pass = sum(int(row[f'{p}_{kind}']) for p in PASSES)
            assert by_pass == sum(int(row[f'{g}_{kind}']) for g in GROUPS), (i, kind)
            assert int(row[total]) == by_pass + sky, (i, kind)
        for p in PASSES:
            assert int(row[f'{p}_submissions']) > 0 and int(row[f'{p}_triangles']) > 0, (i, p)
        assert int(row['map_triangles']) > 0 and int(row['water_triangles']) > 0, i
        for p in PASSES:
            assert int(row[f'{p}_tiles_culled']) >= 0, (i, p)
    # The 96 m shadow box never needs the whole map.
    assert all(int(row['shadow_tiles_culled']) > 0 for row in rows)
    assert previous_end == report['total_ticks'] == int(expected_state.split()[0][5:])
    assert report['warmup_end_tick'] == int(rows[WARMUP - 1]['end_tick'])

    for name in STATS:
        percentiles(report, rows, name, WARMUP)
    assert report['simulation_ms']['samples'] == len(rows[WARMUP:])
    gpu_rows = sum(1 for row in rows[WARMUP:] if row['gpu_ms'])
    assert report['gpu_samples'] == gpu_rows
    if report['gpu_timer'] is None:
        assert report['gpu_ms'] is None and gpu_rows == 0
    else:
        assert report['gpu_samples'] + report['gpu_samples_skipped'] == len(rows[WARMUP:])
    deadline = report['deadline_ms']
    assert abs(deadline - 1000 / 60) < 1e-6
    assert report['frame_deadline_misses'] == sum(
        float(row['frame_ms']) > deadline for row in rows[WARMUP:])
    assert report['interval_deadline_misses'] == sum(
        float(row['frame_interval_ms']) > deadline for row in rows[WARMUP:])
    assert report['wave_patch_rebuilds_measured'] == sum(
        int(row['wave_patch_rebuilds']) for row in rows[WARMUP:])
    assert report['wave_patch_rebuilds'] == sum(int(row['wave_patch_rebuilds']) for row in rows)
    assert report['wave_patch_rebuilds'] > 0 and report['wave_vertex_capacity'] > 0

    # Stress: four fleet emitters and a saturated traffic pool. Display-free state and the
    # benchmark's final state must match exactly.
    stress = root / 'stress-off'
    result = run('--benchmark', stress, '--aa', 'off', '--waves', '--scenario', 'stress')
    assert result.returncode == 0, result.stderr
    stress_report = json.loads(stress.with_suffix('.json').read_text())
    with stress.with_suffix('.csv').open() as stream:
        stress_rows = list(csv.DictReader(stream))
    assert stress_report['schema_revision'] == 4
    assert stress_report['scenario'] == 'stress-basin' and stress_report['route'] == 'stress-basin'
    assert stress_report['run_completed'] is True and stress_report['route_completed'] is False
    assert stress_report['final_state'] == expected_stress == final_state(result.stdout)
    assert stress_report['frames'] == STRESS_FRAMES == len(stress_rows)
    assert stress_report['warmup_frames'] == STRESS_WARMUP
    assert stress_report['measured_frames'] == STRESS_FRAMES - STRESS_WARMUP
    wake = stress_report['wake_objects']
    assert wake['emitters'] == 4 and wake['boat_pool_capacity'] == 128
    assert wake['live'] == wake['peak_live'] == 128
    assert wake['traffic_live'] == wake['traffic_peak_live'] == 128
    # Wetness gaps can drop one emitter's slot briefly; sustained occupancy must stay near the
    # 128-piece cap and every emitter must remain active.
    assert min(int(row['wake_live']) for row in stress_rows[STRESS_WARMUP:]) >= 112
    assert max(int(row['wake_peak_live']) for row in stress_rows) == 128
    for i, row in enumerate(stress_rows):
        assert int(row['wake_peak_live']) <= 128, i
        assert int(row['wake_emitters']) == 4 and int(row['traffic_wake_live']) == 128, i
        for key in ('simulation_ms', 'render_submit_ms', 'present_ms', 'frame_ms'):
            assert math.isfinite(float(row[key])) and float(row[key]) >= 0, (i, key)
        for kind, total, sky in (('submissions', 'app_submissions', 2),
                                 ('triangles', 'scene_triangles', 4)):
            by_pass = sum(int(row[f'{p}_{kind}']) for p in PASSES)
            assert by_pass == sum(int(row[f'{g}_{kind}']) for g in GROUPS), (i, kind)
            assert int(row[total]) == by_pass + sky, (i, kind)
    for name in STATS:
        percentiles(stress_report, stress_rows, name, STRESS_WARMUP)
    assert stress_report['gpu_samples'] == sum(
        1 for row in stress_rows[STRESS_WARMUP:] if row['gpu_ms'])
print('river benchmark rows, warmup, percentiles, route completion and final-state parity pass; '
      'stress fixture state and saturated wake counts match')
