#!/usr/bin/env python3
"""Event-driven --record/CLI smoke over ENet. All timeouts are failure deadlines."""
import argparse
from pathlib import Path
import socket
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', required=True)
    parser.add_argument('--bot', required=True)
    parser.add_argument('--validator', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='proteus-session-record-') as directory:
        path = Path(directory) / 'hunt.p5r'
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        server = subprocess.Popen([
            args.server, '--bind', '127.0.0.1', '--port', str(port), '--no-lan',
            '--location', 'outer', '--package', 'proteus.outer-hunt', '--record', str(path),
            '--exit-with-stdin'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True)
        try:
            # Reading readiness blocks only until the outer CTest failure deadline.
            assert 'listening on' in server.stdout.readline()
            for name in ('one', 'two'):
                bot = subprocess.run([
                    args.bot, '--connect', f'127.0.0.1:{port}', '--name', name,
                    '--snapshots', '5'], capture_output=True, text=True, timeout=20)
                assert bot.returncode == 0, bot.stdout + bot.stderr
                assert 'boat_bot objective:' in bot.stdout, bot.stdout
            server.stdin.close()
            server.stdin = None
            out, err = server.communicate(timeout=20)
            assert server.returncode == 0, out + err
        finally:
            if server.poll() is None:
                server.kill()
                server.communicate()
        original = path.read_bytes()
        for flags in ([], ['--diagnostic'], ['--seek', '12']):
            result = subprocess.run([args.validator, str(path), *flags],
                                    capture_output=True, text=True, timeout=20)
            assert result.returncode == 0, result.stdout + result.stderr
            assert 'boat_session_record=ok' in result.stdout
        for data in (original[:-1], original + b'x', original[:32],
                     original[:40] + bytes([original[40] ^ 1]) + original[41:]):
            bad = Path(directory) / 'bad.p5r'
            bad.write_bytes(data)
            result = subprocess.run([args.validator, str(bad)],
                                    capture_output=True, text=True, timeout=20)
            assert result.returncode != 0, result.stdout
        # Existing recordings are never overwritten, and a failed create closes cleanly.
        result = subprocess.run([args.server, '--port', str(port), '--no-lan',
                                 '--record', str(path)], capture_output=True, text=True, timeout=20)
        assert result.returncode != 0 and 'cannot create recording' in result.stderr
        assert path.read_bytes() == original
    print('boat_session_record_process=ok')


if __name__ == '__main__':
    main()
