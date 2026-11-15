"""Behavioral harness contract, never timing thresholds."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='proteus-wake-r0-') as temporary:
    root = Path(temporary)
    def run(name, *flags):
        prefix = root / name
        subprocess.run([binary, '--output', str(prefix), *flags], check=True)
        with prefix.with_suffix('.csv').open() as stream:
            rows = list(csv.DictReader(stream))
        return json.loads(prefix.with_suffix('.json').read_text()), rows
    baseline, rows = run('baseline')
    diagnostic, measured = run('diagnostic', '--diagnostics')
    assert baseline['hash'] == diagnostic['hash']
    assert baseline['saturated_frames'] == 3600
    assert len(rows) == 3600 and int(rows[-1]['tick']) == 9600
    assert sum(int(row['births']) for row in rows) == 480
    assert all(int(row['live']) == 128 for row in rows)
    assert all(0 <= int(row['expensive_derived']) <= int(row['active_examined'])
               <= int(row['slots_examined']) for row in measured)
    off, empty = run('off', '--off')
    assert all(int(row['live']) == 0 and float(row['height']) == 0 for row in empty)
    raw, raw_rows = run('teleport', '--path', 'teleport', '--emitters', '1')
    restart, restart_rows = run('restart', '--path', 'teleport', '--emitters', '1', '--restart')
    assert max(float(r['max_record_length']) for r in raw_rows) > 1000
    assert max(float(r['max_record_length']) for r in restart_rows) < 8
    slow, slow_rows = run('slow', '--path', 'slow')
    assert all(int(row['live']) == 0 for row in slow_rows)
    straight, trace = run('straight', '--path', 'straight', '--diagnostics')
    assert all(int(r['expensive_derived']) > 0 and int(r['queries']) == 30 for r in trace)
    if len(sys.argv) > 2:
        turn_csv = root / 'turns.csv'
        subprocess.run([str(Path(sys.argv[2]).resolve()), str(turn_csv)], check=True)
        with turn_csv.open() as stream:
            turns = list(csv.DictReader(stream))
        assert len(turns) == 20 and all(None not in r and None not in r.values() for r in turns)
        assert any(r['eligible_samples'] == '0' for r in turns)
        assert all(r['min_crest_ratio'] == r['max_crest_ratio'] == '' for r in turns if r['eligible_samples'] == '0')
    invalid = subprocess.run([binary, '--output', str(root / 'bad'), '--emitters', '4junk'])
    assert invalid.returncode != 0
print('wake_research_contract=ok')
