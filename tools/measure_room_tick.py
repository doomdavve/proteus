#!/usr/bin/env python3
"""Measure boat_server's service cost per tick in real time (a manual tool, never a CTest).

Runs a local room with four boat_bot clients per case and reads the server's own
`service_ms_per_tick` summary line (busy wall time per 120 Hz tick). It compares the shelter hunt
(free room on outer with the Lua objective) against a plain free room and a deathmatch room.
Usage: python3 tools/measure_room_tick.py --build-dir build [--seconds 20] [--repeats 3]
"""
import argparse, re, socket, subprocess, sys, tempfile, time
from pathlib import Path

CASES = {
    'free': ['--mode', 'free', '--location', 'outer'],
    'hunt': ['--mode', 'free', '--location', 'outer', '--package', 'proteus.outer-hunt'],
    'deathmatch': ['--mode', 'deathmatch', '--location', 'outer'],
}

def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]

def run(build, name, seconds):
    port = free_port()
    with tempfile.TemporaryFile('w+') as out:
        server = subprocess.Popen([str(build / 'boat_server'), '--bind', '127.0.0.1', '--port', str(port),
                                   '--no-lan', '--seconds', str(seconds + 4), *CASES[name]], stdout=out, stderr=subprocess.STDOUT)
        time.sleep(1.0)
        bots = [subprocess.Popen([str(build / 'boat_bot'), '--connect', f'127.0.0.1:{port}', '--name', f'b{i}',
                                  '--seconds', str(seconds), *(['--hunt'] if name == 'deathmatch' else [])],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) for i in range(4)]
        for b in bots:
            b.wait()
        server.wait(timeout=30)
        out.seek(0)
        text = out.read()
    m = re.search(r'service_ms_per_tick: ([0-9.]+)', text)
    players = re.search(r'accepted=(\d+)', text)
    if not m or not players or int(players.group(1)) != 4:
        sys.exit(f'{name}: bad run\n{text}')
    return float(m.group(1))

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--build-dir', type=Path, required=True)
    p.add_argument('--seconds', type=int, default=20)
    p.add_argument('--repeats', type=int, default=3)
    a = p.parse_args()
    build = a.build_dir.resolve()
    for name in CASES:
        values = [run(build, name, a.seconds) for _ in range(a.repeats)]
        print(f'{name}: service_ms_per_tick ' + ' '.join(f'{v:.4f}' for v in values) +
              f' (min {min(values):.4f})', flush=True)
