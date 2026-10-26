#!/usr/bin/env python3
"""Multiplayer process smoke test: one boat_server room and boat_bot processes over real
ENet/UDP on 127.0.0.1, ended by events rather than by elapsed time.

A second server on the same port must fail with a clear message. A LAN search
(boat_bot --discover --first, on a private discovery port) must find the room once. Two bots
join with the room token, leave after a few snapshots, and a third with the wrong token must
be refused. The room (--until-empty) closes once both have left. Checks exit codes, summaries
and the message/datagram/MTU bounds.

Session rules over the real transport (deathmatch, race, churn and timeouts, the river map)
run in-process on a virtual clock in validate_boat_net_enet; this test only proves that the
programs connect, discover and exit. Timeouts below are failure deadlines, never waits.
"""

import argparse
import re
import socket
import subprocess
import sys

DEADLINE = 20  # Seconds; reached only when something hangs.
SNAPSHOTS = 3  # Snapshots each bot receives after joining before it leaves.


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def fields(text, prefix):
    for line in text.splitlines():
        if line.startswith(prefix):
            return dict(re.findall(r"(\w+)=([^\s]+)", line))
    raise AssertionError(f"missing '{prefix}' in output:\n{text}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--server", required=True)
    parser.add_argument("--bot", required=True)
    args = parser.parse_args()
    port, lan_port = free_port(), free_port()
    endpoint = f"127.0.0.1:{port}"
    server = subprocess.Popen(
        [args.server, "--port", str(port), "--lan-port", str(lan_port), "--token", "smoke",
         "--until-empty"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    problems = []
    try:
        line = server.stdout.readline()
        if "listening on" not in line:
            raise AssertionError(f"server did not start: {line!r}")
        print(line.strip())
        clash = subprocess.run([args.server, "--port", str(port), "--ticks", "1"],
            capture_output=True, text=True, timeout=DEADLINE)
        if clash.returncode == 0 or "cannot listen" not in clash.stderr:
            problems.append(f"second server on the same port: {clash.returncode} {clash.stderr}")
        search = subprocess.run([args.bot, "--discover", "--first", "--lan-port", str(lan_port),
            "--seconds", str(DEADLINE)],  # deadline: --first exits at the first answer
            capture_output=True, text=True, timeout=DEADLINE + 5)
        rooms = [l for l in search.stdout.splitlines() if l.startswith(f"lan room {endpoint} ")]
        if search.returncode != 0 or len(rooms) != 1 or not all(
                f in rooms[0] for f in ("mode=free", "players=0/4", "token=yes", "compatible=yes")):
            problems.append(f"LAN search: {search.returncode} {search.stdout}{search.stderr}")
        else:
            print(rooms[0])
        bots = [subprocess.Popen([args.bot, "--connect", endpoint, "--token", "smoke",
                    "--name", name, "--snapshots", str(SNAPSHOTS)],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                for name in ("smoke-a", "smoke-b")]
        intruder = subprocess.run([args.bot, "--connect", endpoint, "--token", "wrong",
            "--expect-reject"], capture_output=True, text=True, timeout=DEADLINE)
        if intruder.returncode != 0 or "wrong room token" not in intruder.stdout:
            problems.append(f"wrong token: {intruder.returncode} {intruder.stdout}"
                f"{intruder.stderr}")
        for bot in bots:
            out, err = bot.communicate(timeout=DEADLINE)
            print(out.strip())
            if bot.returncode != 0:
                problems.append(f"bot exit {bot.returncode}: {err}")
                continue
            summary = fields(out, "boat_bot summary:")
            control = fields(out, "control:")
            transport = fields(out, "transport:")
            checks = {
                "snapshots": int(summary["snapshots"]) >= SNAPSHOTS,
                "no resyncs": int(summary["resyncs"]) == 0,
                "one bootstrap": int(summary["bootstraps"]) == 1,
                "no decode errors": int(control["decode_errors"]) == 0,
                "message bound": int(transport["max_message"]) <= 1100,
                "datagram bound": 0 < int(transport["max_datagram"]) <= 1200,
                "mtu": 0 < int(transport["peer_mtu"]) <= 1200,
            }
            problems += [f"bot check failed: {k}" for k, ok in checks.items() if not ok]
        out, err = server.communicate(timeout=DEADLINE)
        print(out.strip())
        if server.returncode != 0 or "every player has left" not in out:
            problems.append(f"server exit {server.returncode}: {err}")
        else:
            room = fields(out, "boat_server room:")
            rejected = fields(out, "rejected:")
            faults = fields(out, "faults:")
            checks = {
                "two accepted": room["accepted"] == "2" and room["left"] == "2",
                "token rejected": rejected["token"] == "1",
                "no faults": all(faults[k] == "0" for k in
                    ("malformed", "forbidden", "misbehaving", "output", "emission")),
            }
            problems += [f"server check failed: {k}" for k, ok in checks.items() if not ok]
    finally:
        if server.poll() is None:
            server.kill()
    for problem in problems:
        print(problem)
    print("boat_net_process=" + ("fail" if problems else "ok"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
