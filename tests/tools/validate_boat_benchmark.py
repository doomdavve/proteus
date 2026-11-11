"""Bounded report, error handling, and same-build deterministic trace contracts."""
import csv
import json
import math
import pathlib
import subprocess
import sys
import tempfile

exe = sys.argv[1]
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    def run(*args):
        return subprocess.run([exe, *map(str, args)], capture_output=True, text=True)
    reports = []
    for name in ('a', 'b'):
        prefix = root / name
        result = run('--scenario', 'wake-loop', '--mode', 'simulation', '--renderer',
                     'none', '--warmup', 10, '--frames', 800, '--output', prefix)
        assert result.returncode == 0, result.stderr
        report = json.loads(prefix.with_suffix('.json').read_text())
        reports.append(report)
        assert report['schema_revision'] == report['scenario_revision'] == 1
        assert isinstance(report['physics_revision'], int) and report['physics_revision'] > 0
        assert report['mode'] == 'simulation' and report['renderer'] == 'none'
        assert report['presentation'] == 'on'
        assert report['frames'] == 800 and report['warmup_frames'] == 10
        assert report['warmup_ticks'] == 20 and report['measured_ticks'] == 1600
        assert report['total_ticks'] == 1620 and report['ticks_per_frame'] == 2
        assert report['render_cpu_ms'] is None and report['gpu_ms'] is None
        assert report['build'] and report['platform'] and len(report['state_hash']) == 16
        assert 0 < report['births'] and 0 < report['peak_objects'] <= 32
        assert report['landings'] >= 0
        with prefix.with_suffix('.csv').open() as stream:
            rows = list(csv.DictReader(stream))
        assert len(rows) == 800
        with pathlib.Path(str(prefix) + '.state.csv').open() as stream:
            states = list(csv.DictReader(stream))
        assert len(states) == 810
        assert [int(row['end_tick']) for row in states] == list(range(2, 1621, 2))
        assert states[-1]['state_hash'] == report['state_hash']
        assert int(states[-1]['births']) == report['births']
        assert int(states[-1]['landings']) == report['landings']
        assert all(math.isfinite(float(row[key])) for row in states
                   for key in ('time', 'x', 'z', 'vx', 'vz', 'height', 'vertical_velocity'))
        if name == 'a':
            first_states = states
        else:
            assert states == first_states
        times = []
        for i, row in enumerate(rows):
            assert int(row['frame']) == i and int(row['end_tick']) == 20 + 2 * (i + 1)
            value = float(row['simulation_ms'])
            assert math.isfinite(value) and value >= 0
            times.append(value)
        times.sort()
        for key, index in [('p50', 799 * 50 // 100), ('p95', 799 * 95 // 100),
                           ('p99', 799 * 99 // 100), ('max', 799)]:
            assert abs(report['simulation_ms'][key] - times[index]) < 1e-8
    assert reports[0]['state_hash'] == reports[1]['state_hash']
    result = run('--warmup', 0, '--frames', 810, '--output', root / 'continuous')
    assert result.returncode == 0, result.stderr
    continuous = json.loads((root / 'continuous.json').read_text())
    assert continuous['state_hash'] == reports[0]['state_hash']
    # Cosmetics never feed physics: a simulation-only run has the same physical trace.
    result = run('--warmup', 10, '--frames', 800, '--presentation', 'off', '--output',
                 root / 'bare')
    assert result.returncode == 0, result.stderr
    bare = json.loads((root / 'bare.json').read_text())
    assert bare['presentation'] == 'off' and bare['state_hash'] == reports[0]['state_hash']
    with open(root / 'a.state.csv') as a, open(root / 'bare.state.csv') as b:
        assert a.read() == b.read()
    for args in [[], ['--frames', '0'], ['--warmup', '-1'], ['--frames', '100001'],
                 ['--frames', '99999999999999999999999'], ['--frames', '2x'],
                 ['--mode', 'render'], ['--renderer', 'cpu'], ['--scenario', 'other'],
                 ['--presentation', 'maybe'], ['--unknown', '1'], ['--frames']]:
        result = run('--output', root / 'invalid', *args) if args else run()
        assert result.returncode != 0 and 'boat_benchmark:' in result.stderr, args
    assert run('--frames', 1, '--output', root / 'missing' / 'out').returncode != 0
    (root / 'blocked-trace.state.csv').mkdir()
    assert run('--frames', 1, '--output', root / 'blocked-trace').returncode != 0
    (root / 'blocked.json').mkdir()
    assert run('--frames', 1, '--output', root / 'blocked').returncode != 0
    result = run('--warmup', 0, '--frames', 1, '--output', root / 'one')
    assert result.returncode == 0, result.stderr
print('benchmark schema, percentiles, bounds/errors and repeated state hashes pass')
