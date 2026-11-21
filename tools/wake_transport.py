#!/usr/bin/env python3
"""Run the private R2b p=1 field benchmark or matched CPU diagnostic movies."""
import argparse
from pathlib import Path
import platform
import subprocess
from wake_geometry import sha, save, banner, summary

MODES = ('baseline', 'spread')

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
    report = {'shared_driver_sha256': sha(Path(__file__).with_name('wake_geometry.py')), 'platform': platform.platform(), 'binary_sha256': sha(args.binary),
              'sources': {str(p): sha(p) for p in (args.source_root / name for name in
                          ('wake_geometry.c', 'wake_geometry.h', 'wake_capture.c', 'wake_capture.h', 'wake_transport.c', 'wake_transport.h', 'wake_transport_review.c'))},
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
                                r = summary(target, 0, emitters)
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
