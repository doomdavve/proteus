"""CPU contract for boat_water_review --lighting-qualify.

Checks bounds and rejected combinations without opening a window, then runs one small
deterministic stress qualification. The measured trajectory, hour cadence, warmup exclusion,
CSV rows and JSON shape are checked. Timing values are checked for finiteness only; no frame-time
threshold is asserted.
"""
import csv
import json
import math
import pathlib
import subprocess
import sys
import tempfile

exe = sys.argv[1]
SAMPLES = 25
WARMUP = 8
ROWS = SAMPLES + WARMUP


def run(*args):
    return subprocess.run([exe, *map(str, args)], capture_output=True, text=True)


with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    rejected = [
        ['--lighting-qualify', root / 'x', '--renderer', 'cpu', '--lighting-samples', '24'],
        ['--lighting-qualify', root / 'x', '--renderer', 'cpu', '--lighting-samples', '3602'],
        ['--lighting-qualify', root / 'x', '--renderer', 'cpu', '--lighting-samples', 'nan'],
        ['--lighting-qualify', root / 'x', '--renderer', 'cpu', '--resolution', 'high'],
        ['--lighting-qualify', root / 'x', '--renderer', 'cpu', '--hdr', 'on'],
        ['--lighting-qualify', root / 'x', '--renderer', 'cpu', '--daylight', 'sunset'],
        ['--lighting-qualify', root / 'x', '--renderer', 'cpu', '--aa', 'off'],
        ['--lighting-qualify', root / 'x', '--renderer', 'cpu', '--check'],
        ['--lighting-qualify', root / 'x', '--renderer', 'cpu', '--check-presentation'],
        ['--lighting-qualify', root / 'x', '--renderer', 'cpu', '--check-water-history'],
        ['--lighting-qualify', root / 'x', '--renderer', 'cpu', '--geometry'],
        ['--lighting-qualify', root / 'x', '--renderer', 'cpu', '--scenario', 'none'],
        ['--lighting-qualify', '', '--renderer', 'cpu'],
        ['--lighting-qualify', f'{root}/', '--renderer', 'cpu'],
        ['--lighting-samples', '25'],
        ['--lighting-qualify', root / 'x', '--renderer', 'cpu', '--lighting-hour', 'nope'],
    ]
    for args in rejected:
        result = run(*args)
        assert result.returncode != 0, args
    # An unusable prefix fails before a window or report is created.
    assert run('--lighting-qualify', root / 'missing' / 'r', '--renderer', 'cpu',
               '--scenario', 'stress', '--lighting-samples', SAMPLES).returncode != 0
    assert not (root / 'missing' / 'r.json').exists()

    prefix = root / 'qualify'
    result = run('--lighting-qualify', prefix, '--renderer', 'cpu', '--scenario', 'stress',
                 '--lighting-samples', SAMPLES, '--waves', '--lighting-hour', '18.5',
                 '--resolution', 'low', '--hdr', 'off')
    assert result.returncode == 0, result.stderr
    report = json.loads(prefix.with_suffix('.json').read_text())
    with prefix.with_suffix('.csv').open() as stream:
        rows = list(csv.DictReader(stream))

    assert report['schema_revision'] == 1 and report['mode'] == 'lighting_qualification'
    assert report['tool'] == 'boat_water_review'
    assert report['renderer'] == 'cpu' and report['scenario'] == 'stress-basin'
    assert report['waves'] is True and report['resolution'] == [320, 200]
    assert report['hdr'] is False and report['gpu_timer'] is None
    assert report['gl_renderer'] is None and report['gl_version'] is None
    assert report['sample_count'] == SAMPLES and report['warmup_samples'] == WARMUP
    assert report['measured_samples'] == SAMPLES and report['frame_rows'] == ROWS
    assert abs(report['sample_stride_seconds'] - 900 / (SAMPLES - 1)) < 1e-12
    assert report['virtual_cycle_seconds'] == 900
    assert report['initialization_ms'] >= 0
    assert report['start_hour'] == 18.5 and report['fixed_hour'] == 18.5
    assert abs(report['cycle_final_hour'] - 18.5) < 1e-5
    assert report['cycle_hours_complete'] is True and report['cycle_wraps'] is True
    assert report['final_state_match'] is True
    assert report['deadline_rule'] == 'value > deadline_ms'
    assert abs(report['deadline_ms'] - 1000 / 60) < 1e-6
    assert report['runs']['fixed']['final_state'] == report['runs']['cycle']['final_state']
    assert report['runs']['fixed']['hour_bins_visited'] == 1
    assert report['runs']['cycle']['hour_bins_visited'] == 24
    assert report['runs']['fixed']['gpu_samples'] == 0
    assert report['runs']['cycle']['gpu_samples'] == 0
    assert report['runs']['fixed']['gpu_ms'] is None
    assert report['runs']['cycle']['gpu_ms'] is None
    assert report['runs']['fixed']['gpu_samples_skipped'] == SAMPLES
    assert report['runs']['cycle']['gpu_samples_skipped'] == SAMPLES
    assert report['runs']['fixed']['first_draw_ms'] >= 0
    assert report['runs']['cycle']['first_draw_ms'] >= 0
    assert 'counter_semantics' in report
    for name in ('simulation_ms', 'submission_ms', 'present_ms', 'frame_ms',
                 'app_submissions', 'scene_triangles', 'sky_draw_samples'):
        for run_name in ('fixed', 'cycle'):
            stats = report['runs'][run_name][name]
            assert stats['samples'] == SAMPLES, (run_name, name)
    assert report['runs']['fixed']['present_ms']['max'] == 0
    assert report['runs']['cycle']['present_ms']['max'] == 0
    # CPU sky work is reported honestly: reference and visible samples are separate.
    for run_name in ('fixed', 'cycle'):
        sky = report['runs'][run_name]['sky']
        assert sky['draw_calls'] == SAMPLES
        assert sky['draw_samples'] > 0

    assert len(rows) == ROWS * 2
    fixed_rows = [row for row in rows if row['run'] == 'fixed']
    cycle_rows = [row for row in rows if row['run'] == 'cycle']
    assert len(fixed_rows) == len(cycle_rows) == ROWS
    assert sum(row['warmup'] == '1' for row in fixed_rows) == WARMUP
    assert sum(row['warmup'] == '1' for row in cycle_rows) == WARMUP
    measured = [row for row in cycle_rows if row['warmup'] == '0']
    assert len(measured) == SAMPLES
    assert {int(float(row['hour']) % 24) for row in measured} == set(range(24))
    assert abs(float(measured[0]['virtual_elapsed_s'])) < 1e-9
    assert abs(float(measured[-1]['virtual_elapsed_s']) - 900) < 1e-6
    assert abs(float(measured[-1]['hour']) - 18.5) < 1e-5
    for row in fixed_rows:
        assert float(row['hour']) == 18.5
    for row in rows:
        for key in ('simulation_ms', 'submission_ms', 'present_ms', 'frame_ms'):
            value = float(row[key])
            assert math.isfinite(value) and value >= 0, key
        parts = sum(float(row[key]) for key in ('simulation_ms', 'submission_ms', 'present_ms'))
        assert abs(parts - float(row['frame_ms'])) < 1e-3 + 1e-5 * parts
        assert row['gpu_ms'] == ''
        # CPU rows carry renderer3d mesh submissions/triangles, not GPU DrawMesh zeroes.
        assert int(row['app_submissions']) > 0 and int(row['scene_triangles']) > 0
    # Both runs advance the same independent physics; the final pose equality above is the
    # state contract, and the CSV carries one row per renderer per physical sample.
    assert {row['sample'] for row in fixed_rows} == {str(i) for i in range(ROWS)}
    assert result.stdout.count('final_state ') == 1
print('lighting qualification bounds, fixed/cycle cadence, warmup separation, state parity and '
      'CPU report shape pass')
