#!/usr/bin/env python3
"""Hosting from the game: `boat_study --serve` is boat_server's room, and --exit-with-stdin closes
it when the hosting game's pipe ends (as when boat_study --host leaves or is killed).

Starts the room through boat_study with private ports, lets a boat_bot drive in it, closes the
room's standard input and checks that the room says goodbye and exits within two seconds.
"""

import argparse
import socket
import subprocess
import sys
import time


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--study", required=True)
    parser.add_argument("--bot", required=True)
    args = parser.parse_args()
    port, lan_port = free_port(), free_port()
    room = subprocess.Popen(
        [args.study, "--serve", "--port", str(port), "--lan-port", str(lan_port),
         "--exit-with-stdin"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        first = room.stdout.readline()
        assert first.startswith("boat_server listening on"), first
        bot = subprocess.run([args.bot, "--connect", f"127.0.0.1:{port}", "--seconds", "1"],
                             capture_output=True, text=True, timeout=20)
        assert bot.returncode == 0, bot.stdout + bot.stderr
        assert "control: commands=" in bot.stdout, bot.stdout
        closed = time.monotonic()
        room.stdin.close()
        room.wait(timeout=2)
        waited = time.monotonic() - closed
        out = room.stdout.read()
        assert room.returncode == 0, out
        assert "standard input closed; closing the room" in out, out
        assert "accepted=1 left=1" in out, out
    finally:
        if room.poll() is None:
            room.kill()
            room.wait()
    print(f"boat_host=ok closed_in={waited:.2f}s")


if __name__ == "__main__":
    sys.exit(main())
