#!/usr/bin/env python3
"""Hosting from the game: `wake_velocity --serve` is boat_server's room, and --exit-with-stdin closes
it when the hosting game's pipe ends (as when wake_velocity --host leaves or is killed).

Starts the room through wake_velocity with private ports, lets a boat_bot join and leave after a
few snapshots, closes the room's standard input and checks that the room says goodbye and exits.
The 2 s limit on that exit is a failure deadline; nothing waits for time to pass.
"""

import argparse
import socket
import subprocess
import sys


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
        bot = subprocess.run([args.bot, "--connect", f"127.0.0.1:{port}", "--snapshots", "3"],
                             capture_output=True, text=True, timeout=20)
        assert bot.returncode == 0, bot.stdout + bot.stderr
        assert "control: commands=" in bot.stdout, bot.stdout
        room.stdin.close()
        room.wait(timeout=2)
        out = room.stdout.read()
        assert room.returncode == 0, out
        assert "standard input closed; closing the room" in out, out
        assert "accepted=1 left=1" in out, out
    finally:
        if room.poll() is None:
            room.kill()
            room.wait()
    print("boat_host=ok")


if __name__ == "__main__":
    sys.exit(main())
