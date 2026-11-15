#!/usr/bin/env python3
"""Retain isolated R2a CPU timing comparisons or signed-height/foam movies."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import platform
import struct
import subprocess
import zlib

MODES = ('baseline', 'short', 'open', 'angle45', 'short_matched')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def banner(path, candidate):
    # Small bitmap labels avoid graphics contexts or platform font dependencies.
    glyphs = {
        'A': '01110/10001/10001/11111/10001/10001/10001',
        'B': '11110/10001/10001/11110/10001/10001/11110',
        'D': '11110/10001/10001/10001/10001/10001/11110',
        'E': '11111/10000/10000/11110/10000/10000/11111',
        'G': '01111/10000/10000/10111/10001/10001/01111',
        'H': '10001/10001/10001/11111/10001/10001/10001',
        'I': '11111/00100/00100/00100/00100/00100/11111',
        'L': '10000/10000/10000/10000/10000/10000/11111',
        'M': '10001/11011/10101/10101/10001/10001/10001',
        'N': '10001/11001/10101/10011/10001/10001/10001',
        'O': '01110/10001/10001/10001/10001/10001/01110',
        'P': '11110/10001/10001/11110/10000/10000/10000',
        'R': '11110/10001/10001/11110/10100/10010/10001',
        'S': '01111/10000/10000/01110/00001/00001/11110',
        'T': '11111/00100/00100/00100/00100/00100/00100',
        'C': '01111/10000/10000/10000/10000/10000/01111',
        '4': '10010/10010/10010/11111/00010/00010/00010',
        '5': '11111/10000/10000/11110/00001/00001/11110',
        '_': '00000/00000/00000/00000/00000/00000/11111',
    }
    rows = [bytearray(1536 * 3) for _ in range(32)]
    for origin, text in ((12, 'BASELINE'), (780, candidate.upper())):
        for i, char in enumerate(text):
            for y, line in enumerate(glyphs[char].split('/')):
                for x, bit in enumerate(line):
                    if bit == '1':
                        for dy in range(3):
                            for dx in range(3):
                                k = (origin + 18 * i + x * 3 + dx) * 3
                                rows[5 + y * 3 + dy][k:k+3] = b'\xff' * 3
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 1536, 32, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(b''.join(b'\0' + row for row in rows))) + chunk(b'IEND', b''))


def summary(path, mode, emitters):
    with path.open() as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == 3600 and all(None not in r and None not in r.values() for r in rows)
    assert all(int(r['live']) == emitters * (64 if mode in (1, 4) else 32) for r in rows)
    assert sum(int(r['births']) for r in rows) == emitters * (240 if mode in (1, 4) else 120)
    metrics = {}
    for key in ('source_ms', 'query_ms', 'total_ms'):
        values = sorted(float(r[key]) for r in rows)
        assert all(math.isfinite(x) and x >= 0 for x in values)
        metrics[key] = {f'p{p}': values[math.ceil(len(values) * p / 100) - 1] for p in (50, 95, 99)}
    assert math.isfinite(float(rows[-1]['checksum']))
    return {'timings': metrics, 'live': int(rows[-1]['live']), 'slots': int(rows[-1]['slots']),
            'record_bytes': int(rows[-1]['record_bytes']), 'workspace_bytes': int(rows[-1]['workspace_bytes']),
            'sha256': sha(path), 'path': str(path.resolve())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, default=Path('tools'),
                        help='sources corresponding to the executable (archived sources for a retained binary)')
    parser.add_argument('--bench', action='store_true')
    parser.add_argument('--media', action='store_true')
    parser.add_argument('--paths', nargs='+', choices=('straight', 'circle', 'wide', 'eight', 'reverse', 'crossing'))
    args = parser.parse_args()
    assert args.bench != args.media, 'choose one of --bench and --media'
    args.output.mkdir(parents=True, exist_ok=True)
    manifest_path = args.output / 'manifest.json'
    if manifest_path.exists():
        raise ValueError('use a fresh directory to avoid mixing runs')
    report = {'platform': platform.platform(), 'binary_sha256': sha(args.binary),
              'sources': {str(p): sha(p) for p in (args.source_root / name for name in
                          ('wake_geometry.c', 'wake_geometry.h', 'wake_geometry_review.c'))},
              'driver_sha256': sha(Path(__file__)), 'runs': []}
    binary = str(args.binary.resolve())
    if args.bench:
        for path in args.paths or ('straight', 'circle', 'reverse'):
            for emitters in (1, 4):
                for layout in ('dense', 'dispersed'):
                    for repeat in range(3):
                        for candidate in range(1, len(MODES)):
                            for mode in ((0, candidate) if repeat % 2 == 0 else (candidate, 0)):
                                target = args.output / f'{path}-{emitters}-{layout}-{repeat}-{candidate}-{mode}.csv'
                                command = [binary, '--bench', path, str(mode), str(emitters), layout, '--output', str(target)]
                                print(' '.join(command), flush=True)
                                subprocess.run(command, check=True)
                                r = summary(target, mode, emitters)
                                r.update(path_kind=path, mode=MODES[mode], candidate=MODES[candidate], emitters=emitters,
                                         layout=layout, repeat=repeat, command=command)
                                report['runs'].append(r)
                        save(manifest_path, report)
    else:
        for path in args.paths or ('straight', 'circle', 'wide', 'eight', 'reverse', 'crossing'):
            for view in ('wide', 'detail'):
                folders = []
                for mode, name in enumerate(MODES):
                    folder = args.output / f'{path}-{view}-{name}-frames'
                    folder.mkdir()
                    command = [binary, '--clip', path, str(mode), '--output', str(folder)]
                    if view == 'detail':
                        command.append('--detail')
                    print(' '.join(command), flush=True)
                    subprocess.run(command, check=True)
                    frames = sorted(folder.glob('*.png'))
                    assert [p.name for p in frames] == [f'{i:04d}.png' for i in range(100)]
                    report['runs'].append({'path_kind': path, 'view': view, 'mode': name, 'command': command,
                                           'frames': {str(p.resolve()): sha(p) for p in frames}})
                    folders.append(folder)
                for mode, name in enumerate(MODES[1:], 1):
                    header = args.output / f'{name}-banner.png'
                    banner(header, name)
                    movie = args.output / f'{path}-{view}-{name}-paired.mp4'
                    command = ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-framerate', '10',
                               '-i', str(folders[0] / '%04d.png'), '-framerate', '10', '-i', str(folders[mode] / '%04d.png'),
                               '-framerate', '10', '-i', str(header), '-filter_complex',
                               '[0:v]scale=768:384:flags=neighbor[a];[1:v]scale=768:384:flags=neighbor[b];'
                               '[a][b]hstack[body];[2:v][body]vstack[v]', '-map', '[v]', '-c:v', 'libx264',
                               '-crf', '18', '-pix_fmt', 'yuv420p', '-frames:v', '100', str(movie)]
                    subprocess.run(command, check=True)
                    probe = subprocess.check_output(['ffprobe', '-v', 'error', '-count_frames', '-select_streams', 'v:0',
                        '-show_entries', 'stream=nb_read_frames', '-of', 'csv=p=0', str(movie)], text=True).strip()
                    assert probe == '100'
                    report.setdefault('movies', []).append({'path': str(movie.resolve()), 'sha256': sha(movie), 'command': command})
                save(manifest_path, report)
    print(f'manifest: {manifest_path}', flush=True)


if __name__ == '__main__':
    main()
