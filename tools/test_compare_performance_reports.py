"""Performance report analyzer contracts, using checked-in synthetic fixtures only.

These fixtures contain no personal or hardware capture data. The test covers compatible and
incompatible comparisons, ZIP round-trips, schema/bounds rejection and HTML output.
"""
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent
ANALYZER = ROOT / 'compare_performance_reports.py'
FIXTURES = ROOT / 'fixtures' / 'performance'


def run(*args):
    return subprocess.run([sys.executable, ANALYZER, *map(str, args)], capture_output=True,
                          text=True)


with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    result = run(FIXTURES / 'candidate', '--baseline', FIXTURES / 'baseline',
                 '--html', root / 'compare.html')
    assert result.returncode == 0, result.stderr
    assert 'Comparable' in result.stdout
    assert 'frame_ms' in result.stdout and 'p95 delta' in result.stdout
    assert 'Frame p95 moved -4.20 ms' in result.stdout
    html = (root / 'compare.html').read_text()
    assert '<svg' in html and '<polyline' in html

    mismatch = run(FIXTURES / 'candidate', '--baseline', FIXTURES / 'incompatible')
    assert mismatch.returncode == 0
    assert 'Not comparable' in mismatch.stdout
    assert 'No numerical regression verdict' in mismatch.stdout

    single = run(FIXTURES / 'candidate')
    assert single.returncode == 0 and 'single-report description' in single.stdout

    archive = root / 'report.zip'
    with zipfile.ZipFile(archive, 'w') as bundle:
        for file in (FIXTURES / 'candidate').iterdir():
            bundle.write(file, file.name)
    zipped = run(archive)
    assert zipped.returncode == 0, zipped.stderr
    assert 'single-report description' in zipped.stdout

    def broken(name, mutate):
        target = root / name
        shutil.copytree(FIXTURES / 'candidate', target)
        mutate(target)
        return run(target)

    def revision(target):
        manifest = json.loads((target / 'manifest.json').read_text())
        manifest['revision'] = 99
        (target / 'manifest.json').write_text(json.dumps(manifest))

    assert broken('revision', revision).returncode != 0

    def wrong_count(target):
        manifest = json.loads((target / 'manifest.json').read_text())
        manifest['sampling']['samples'] = 1
        (target / 'manifest.json').write_text(json.dumps(manifest))

    assert broken('count', wrong_count).returncode != 0

    def missing(target):
        (target / 'summary.json').unlink()

    assert broken('missing', missing).returncode != 0

    def non_finite(target):
        lines = (target / 'samples.csv').read_text().splitlines()
        fields = lines[1].split(',')
        fields[5] = 'nan'
        lines[1] = ','.join(fields)
        (target / 'samples.csv').write_text('\n'.join(lines) + '\n')

    assert broken('nan', non_finite).returncode != 0

    def huge(target):
        manifest = json.loads((target / 'manifest.json').read_text())
        manifest['sampling']['samples'] = 20001
        (target / 'manifest.json').write_text(json.dumps(manifest))

    assert broken('huge', huge).returncode != 0

    extra = root / 'extra.zip'
    with zipfile.ZipFile(extra, 'w') as bundle:
        for file in (FIXTURES / 'candidate').iterdir():
            bundle.write(file, file.name)
        bundle.writestr('payload.exe', b'MZ')
    rejected = run(extra)
    assert rejected.returncode != 0 and 'unexpected archive member' in rejected.stderr

print('performance_reports=ok compatible, incompatible, ZIP, schema and bounds checks')
