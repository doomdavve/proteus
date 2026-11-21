#!/usr/bin/env python3
"""Render matched R0 diagnostic-neutrality reels (requires desktop GL and ffmpeg)."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--scenes', nargs='+', choices=('circle', 'straight', 'eight', 'stop', 'dry', 'reverse', 'teleport', 'service', 'outer', 'high'),
                   default=['straight', 'circle', 'eight', 'stop', 'dry', 'reverse', 'teleport', 'service', 'outer', 'high'])
    p.add_argument('--views', nargs='+', choices=('chase', 'overhead'), default=['chase', 'overhead'])
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {'binary_sha256': sha(args.binary), 'reels': []}
    for scene in args.scenes:
        for view in args.views:
            path = 'circle' if scene in ('outer', 'high', 'service') else scene
            flags = ['--path', path, '--emitters', '4', '--gpu']
            if scene in ('outer', 'high', 'service'): flags += ['--location', 'outer']
            if scene == 'high': flags += ['--high']
            if scene == 'service': flags += ['--scene', 'plugins/marina-berth-demo', '--off']
            if view == 'overhead': flags += ['--overhead']
            # Reproduce stopped-source propagation and discontinuities inside the clip.
            start = 23 if scene in ('stop', 'teleport') else 7 if scene in ('dry', 'service') else 20
            prefixes, folders, commands = [], [], []
            for mode in ('baseline', 'diagnostic'):
                prefix = args.output / f'{scene}-{view}-{mode}'
                folder = args.output / f'{scene}-{view}-{mode}-frames'
                folder.mkdir(exist_ok=True)
                if any(folder.glob('*.png')):
                    raise ValueError(f'{folder}: use a fresh output directory; stale frames are not accepted')
                command = [str(args.binary.resolve()), '--output', str(prefix), '--clip', str(folder),
                           '--clip-start', str(start), *flags]
                if mode == 'diagnostic': command.append('--diagnostics')
                print(' '.join(command), flush=True)
                subprocess.run(command, check=True)
                folders.append(folder)
                prefixes.append(prefix)
                commands.append(command)
            expected = {f'{prefix}{i:04d}.png' for prefix in ('', 'field-') for i in range(100)}
            if any({p.name for p in folder.glob('*.png')} != expected for folder in folders):
                raise ValueError('expected exactly 100 scene frames and 100 field frames per side')
            hashes = []
            same = True
            for a in sorted(folders[0].glob('*.png')):
                b = folders[1] / a.name
                same &= sha(a) == sha(b)
                hashes.append({'frame': a.name, 'baseline': sha(a), 'diagnostic': sha(b)})
            if not hashes or not same:
                raise ValueError(f'{scene}/{view}: paired image pixels changed')
            outputs = []
            for field in ('', 'field-'):
                output = args.output / f'{scene}-{view}-{field}paired.mp4'
                inputs = []
                for folder in folders:
                    inputs += ['-framerate', '10', '-i', str(folder / f'{field}%04d.png')]
                banner = Path(str(prefixes[0]) + ('-field-banner.png' if field else '-banner.png'))
                inputs += ['-framerate', '10', '-i', str(banner)]
                filters = ("[0:v]scale=640:400:flags=neighbor[a];"
                           "[1:v]scale=640:400:flags=neighbor[b];"
                           "[a][b]hstack[body];[2:v][body]vstack[v]")
                encode = ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', *inputs,
                          '-filter_complex', filters, '-map', '[v]', '-c:v', 'libx264', '-crf', '20',
                          '-pix_fmt', 'yuv420p', '-frames:v', '100', str(output)]
                subprocess.run(encode, check=True)
                probe = subprocess.check_output(['ffprobe', '-v', 'error', '-count_frames',
                    '-select_streams', 'v:0', '-show_entries', 'stream=nb_read_frames',
                    '-of', 'json', str(output)], text=True)
                if json.loads(probe)['streams'][0]['nb_read_frames'] != '100':
                    raise ValueError(f'{output}: encoded video does not have 100 frames')
                outputs.append({'path': str(output), 'sha256': sha(output), 'command': encode})
            manifest['reels'].append({'scene': scene, 'view': view, 'commands': commands,
                                      'all_pngs_identical': same, 'images': hashes, 'outputs': outputs,
                                      'metadata': [json.loads(p.with_suffix('.json').read_text()) for p in prefixes]})
            (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
