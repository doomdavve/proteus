"""Capture three sequential P3a runs after stopping other build/test workloads.

Usage: python3 tools/boat_benchmark_baseline.py BENCHMARK OUTPUT_DIRECTORY
Timing artifacts are local evidence, never pass/fail performance thresholds.
"""
import csv
import json
import pathlib
import subprocess
import sys

exe = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(sys.argv[2])
root.mkdir(parents=True, exist_ok=True)
reference = None
for attempt in range(1, 4):
    prefix = root / f'run-{attempt}'
    subprocess.run([str(exe), '--scenario', 'wake-loop', '--mode', 'simulation',
                    '--renderer', 'none', '--warmup', '120', '--frames', '7200',
                    '--output', str(prefix)], check=True)
    report = json.loads(prefix.with_suffix('.json').read_text())
    with pathlib.Path(str(prefix) + '.state.csv').open() as stream:
        trace = list(csv.DictReader(stream))
    # The finite one-boat fixture must emit and return to a landing, not fill the fleet pool.
    assert report['scenario_revision'] == 1 and report['hash_revision'] == 2
    assert report['warmup_ticks'] == 240 and report['measured_ticks'] == 14400
    assert report['total_ticks'] == 14640 and len(trace) == 7320
    assert report['physics_revision'] == 7
    assert report['births'] == report['peak_objects'] == 19
    assert report['landings'] == 1
    assert int(trace[-1]['live']) == 0
    assert max(int(row['live']) for row in trace) == 19
    assert {int(row['motion']) for row in trace}.issuperset({0, 1, 2})
    assert trace[-1]['state_hash'] == report['state_hash']
    if reference is None:
        reference = trace
    else:
        assert trace == reference, 'same-build physical traces differ'
    print(json.dumps({'run': attempt, 'build': report['build'],
                      'state_hash': report['state_hash'],
                      'simulation_ms': report['simulation_ms']}, sort_keys=True))
print('Three identical finite-return traces; emission, flight, landing and expiry milestones pass')
