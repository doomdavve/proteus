#!/usr/bin/env python3
"""Multiplayer M3 loopback smoke test: one boat_server room and boat_bot client processes over
real ENet/UDP on 127.0.0.1.

Two bots join and drive for a few seconds; a third presents the wrong room token and must be
refused; a second server on the same port must fail with a clear message; a LAN search
(boat_bot --discover, on a private discovery port) must find the room. Checks exit codes,
summaries, message/datagram/MTU bounds and that every process stops within a bounded time.

With --deathmatch (protocol v2): a deathmatch room with a 4 s round and two hunting bots that
stay through the intermission. Each bot's received scoreboard for round 1 must equal the
server log's, and each must have received every round event and capsize the server logged.

With --race (protocol v4): a river race room found by LAN search as mode=race, and two racing
bots through the grid, the countdown and GO. Each bot must have been held on the grid, hold the
server's GO tick and race state, and have received every race event the server logged.
"""

import argparse
import re
import socket
import subprocess
import sys
import time


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def fields(text, prefix):
    for line in text.splitlines():
        if line.startswith(prefix):
            return dict(re.findall(r"(\w+)=([^\s]+)", line))
    raise AssertionError(f"missing '{prefix}' in output:\n{text}")


def server_round(out, number):
    """Round NUMBER's final rows from the server log, and the capsizes logged before it ended."""
    capsizes = 0
    for line in out.splitlines():
        if " capsize " in line:
            capsizes += 1
        if f" round {number} end:" in line:
            rows = re.findall(r" (\d):\S+=(-?\d+) \(#\d+, (\d+) kills, (\d+) deaths, (\d+) self\)",
                line)
            return {slot: "/".join(values) for slot, *values in rows}, capsizes
    raise AssertionError(f"server never ended round {number}:\n{out}")


def bot_round(out, number):
    for line in out.splitlines():
        if line.startswith(f"boat_bot round {number} end:"):
            return dict(re.findall(r" (\d)=(-?\d+/\d+/\d+/\d+)", line))
    raise AssertionError(f"bot never saw round {number} end:\n{out}")


def deathmatch(args):
    port = free_port()
    endpoint = f"127.0.0.1:{port}"
    bot_seconds = 8  # The round starts within a second of joining and lasts 4 s.
    server_seconds = bot_seconds + 3  # Bots join at once and leave first.
    server = subprocess.Popen(
        [args.server, "--port", str(port), "--mode", "deathmatch", "--round-seconds", "4",
         "--location", args.location,
         "--seconds", str(server_seconds)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    problems = []
    try:
        line = server.stdout.readline()
        if "deathmatch" not in line:
            raise AssertionError(f"server did not start a deathmatch room: {line!r}")
        print(line.strip())
        bots = [subprocess.Popen([args.bot, "--connect", endpoint, "--name", name, "--hunt",
                    "--seconds", str(bot_seconds)],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                for name in ("hunt-a", "hunt-b")]
        outputs = []
        for bot in bots:
            out, err = bot.communicate(timeout=bot_seconds + 20)
            print(out.strip())
            if bot.returncode != 0:
                problems.append(f"bot exit {bot.returncode}: {err}")
            outputs.append(out)
        out, err = server.communicate(timeout=server_seconds + 15)
        print(out.strip())
        if server.returncode != 0:
            problems.append(f"server exit {server.returncode}: {err}")
        if not problems:
            rows, capsizes = server_round(out, 1)
            for text in outputs:
                match = fields(text, "match:")
                summary = fields(text, "boat_bot summary:")
                checks = {
                    "scoreboard": bot_round(text, 1) == rows,
                    # Round start, every capsize, round end; the bot leaves in the intermission.
                    "events": int(match["events"]) == capsizes + 2,
                    "no resyncs": int(summary["resyncs"]) == 0,
                    "no decode errors": int(fields(text, "control:")["decode_errors"]) == 0,
                }
                problems += [f"bot check failed: {k}" for k, ok in checks.items() if not ok]
            faults = fields(out, "faults:")
            if not all(faults[k] == "0" for k in ("malformed", "forbidden", "output", "emission")):
                problems.append(f"server faults: {faults}")
            print(f"round 1: {rows} after {capsizes} capsizes")
    finally:
        if server.poll() is None:
            server.kill()
    for problem in problems:
        print(problem)
    print("boat_net_deathmatch=" + ("fail" if problems else "ok"))
    return 1 if problems else 0


def race(args):
    port = free_port()
    lan_port = free_port()
    endpoint = f"127.0.0.1:{port}"
    bot_seconds = 8  # Grid within a second of the second join, GO 4 s later.
    server_seconds = bot_seconds + 4  # The LAN search delays the bots by 1 s.
    server = subprocess.Popen(
        [args.server, "--port", str(port), "--lan-port", str(lan_port), "--mode", "race",
         "--seconds", str(server_seconds)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    problems = []
    try:
        line = server.stdout.readline()
        if "race" not in line or "river" not in line:
            raise AssertionError(f"server did not start a river race room: {line!r}")
        print(line.strip())
        search = subprocess.run([args.bot, "--discover", "--lan-port", str(lan_port), "--seconds",
            "1"], capture_output=True, text=True, timeout=10)
        rooms = [l for l in search.stdout.splitlines() if l.startswith(f"lan room {endpoint} ")]
        if len(rooms) != 1 or "mode=race" not in rooms[0] or "compatible=yes" not in rooms[0]:
            problems.append(f"LAN search: {search.stdout}{search.stderr}")
        else:
            print(rooms[0])
        bots = [subprocess.Popen([args.bot, "--connect", endpoint, "--name", name, "--predict",
                    "--seconds", str(bot_seconds)],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                for name in ("race-a", "race-b")]
        outputs = []
        for bot in bots:
            out, err = bot.communicate(timeout=bot_seconds + 20)
            print(out.strip())
            if bot.returncode != 0:
                problems.append(f"bot exit {bot.returncode}: {err}")
            outputs.append(out)
        out, err = server.communicate(timeout=server_seconds + 15)
        print(out.strip())
        if server.returncode != 0:
            problems.append(f"server exit {server.returncode}: {err}")
        if not problems:
            grid = re.search(r"race 1 grid go=(\d+)", out)
            # Events the bots stayed for: everything logged before the first leaver's DNF.
            logged = out.split(" dnf ")[0]
            events = len(re.findall(r"^tick \d+ (?:race|gate|finish|dnf|recover|park) ", logged,
                re.MULTILINE))
            if not grid or "race 1 racing" not in out:
                problems.append("server never released race 1")
            for text in outputs:
                state = fields(text, "race:")
                summary = fields(text, "boat_bot summary:")
                checks = {
                    "racing": state["round"] == "1" and state["phase"] == "3",
                    "same GO": grid is not None and state["go"] == grid.group(1),
                    # Observed 120 Hz simulation ticks, independent of bot polling cadence.
                    "held on the grid": int(state["held_ticks"]) >= 150,
                    "events": int(state["events"]) >= events,
                    "no resyncs": int(summary["resyncs"]) == 0,
                    "no decode errors": int(fields(text, "control:")["decode_errors"]) == 0,
                    "boat moved": float(fields(text, "control:")["travelled_m"]) > 20,
                }
                problems += [f"bot check failed: {k}" for k, ok in checks.items() if not ok]
            faults = fields(out, "faults:")
            if not all(faults[k] == "0" for k in ("malformed", "forbidden", "output", "emission")):
                problems.append(f"server faults: {faults}")
    finally:
        if server.poll() is None:
            server.kill()
    for problem in problems:
        print(problem)
    print("boat_net_race=" + ("fail" if problems else "ok"))
    return 1 if problems else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--server", required=True)
    parser.add_argument("--bot", required=True)
    parser.add_argument("--seconds", type=int, default=4)
    parser.add_argument("--deathmatch", action="store_true")
    parser.add_argument("--race", action="store_true")
    parser.add_argument("--location", choices=("basin", "river"), default="basin")
    args = parser.parse_args()
    if args.deathmatch:
        return deathmatch(args)
    if args.race:
        return race(args)
    port = free_port()
    lan_port = free_port()
    endpoint = f"127.0.0.1:{port}"
    server_seconds = args.seconds + 5  # The LAN search delays the bots by 1 s.
    started = time.monotonic()
    server = subprocess.Popen(
        [args.server, "--port", str(port), "--lan-port", str(lan_port), "--token", "smoke",
         "--location", args.location,
         "--seconds", str(server_seconds)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    problems = []
    try:
        line = server.stdout.readline()
        if "listening on" not in line:
            raise AssertionError(f"server did not start: {line!r}")
        print(line.strip())
        clash = subprocess.run([args.server, "--port", str(port), "--ticks", "1"],
            capture_output=True, text=True, timeout=10)
        if clash.returncode == 0 or "cannot listen" not in clash.stderr:
            problems.append(f"second server on the same port: {clash.returncode} {clash.stderr}")
        else:
            print("port clash:", clash.stderr.strip())
        search = subprocess.run([args.bot, "--discover", "--lan-port", str(lan_port), "--seconds",
            "1"], capture_output=True, text=True, timeout=10)
        found = f"lan room {endpoint} "
        rooms = [l for l in search.stdout.splitlines() if l.startswith(found)]
        if search.returncode != 0 or len(rooms) != 1 or not all(
                f in rooms[0] for f in ("mode=free", "players=0/4", "token=yes", "compatible=yes")):
            problems.append(f"LAN search: {search.returncode} {search.stdout}{search.stderr}")
        else:
            print(rooms[0])
        bots = [subprocess.Popen([args.bot, "--connect", endpoint, "--token", "smoke",
                    "--name", name, "--seconds", str(args.seconds)],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                for name in ("smoke-a", "smoke-b")]
        intruder = subprocess.run([args.bot, "--connect", endpoint, "--token", "wrong",
            "--expect-reject"], capture_output=True, text=True, timeout=20)
        if intruder.returncode != 0 or "wrong room token" not in intruder.stdout:
            problems.append(f"wrong token: {intruder.returncode} {intruder.stdout}"
                f"{intruder.stderr}")
        else:
            print(intruder.stdout.strip())
        for bot in bots:
            out, err = bot.communicate(timeout=args.seconds + 20)
            print(out.strip())
            if bot.returncode != 0:
                problems.append(f"bot exit {bot.returncode}: {err}")
                continue
            summary = fields(out, "boat_bot summary:")
            control = fields(out, "control:")
            transport = fields(out, "transport:")
            births = fields(out, "births:")
            expected_snapshots = 30 * args.seconds * 0.8
            # Share of ticks the bot sent a command for. A descheduled bot (busy CI runners stall
            # processes for 50-200 ms) cannot send commands for ticks the server has already run,
            # so judge use of commands against what was sent, not against every tick.
            coverage = min(1.0, int(control["commands"]) / (120 * float(summary["seconds"])))
            print(f"command coverage: {coverage:.3f}")
            checks = {
                "snapshots": int(summary["snapshots"]) >= expected_snapshots,
                "no resyncs": int(summary["resyncs"]) == 0,
                "one bootstrap": int(summary["bootstraps"]) == 1,
                "short mismatches": float(summary["max_mismatch_ms"]) < 250,
                "births applied": int(births["applied"]) > 0 and int(births["dropped"]) == 0,
                "commands sent": coverage > 0.5,
                "commands used": float(control["received_ratio"]) > 0.85 * coverage,
                "boat moved": float(control["travelled_m"]) > 10,
                "no decode errors": int(control["decode_errors"]) == 0,
                "message bound": int(transport["max_message"]) <= 1100,
                "datagram bound": 0 < int(transport["max_datagram"]) <= 1200,
                "mtu": 0 < int(transport["peer_mtu"]) <= 1200,
            }
            problems += [f"bot check failed: {k}" for k, ok in checks.items() if not ok]
        out, err = server.communicate(timeout=server_seconds + 15)
        elapsed = time.monotonic() - started
        print(out.strip())
        if server.returncode != 0:
            problems.append(f"server exit {server.returncode}: {err}")
        else:
            room = fields(out, "boat_server room:")
            rejected = fields(out, "rejected:")
            faults = fields(out, "faults:")
            transport = fields(out, "transport:")
            checks = {
                "two accepted": room["accepted"] == "2" and room["left"] == "2",
                "token rejected": rejected["token"] == "1",
                "no faults": all(faults[k] == "0" for k in
                    ("malformed", "forbidden", "misbehaving", "output", "emission")),
                "ticks": int(room["ticks"]) == server_seconds * 120,
                "message bound": int(transport["max_message"]) <= 1100,
                "datagram bound": 0 < int(transport["max_datagram"]) <= 1200,
                "bounded exit": elapsed < server_seconds + 10,
            }
            problems += [f"server check failed: {k}" for k, ok in checks.items() if not ok]
    finally:
        if server.poll() is None:
            server.kill()
    for problem in problems:
        print(problem)
    print("boat_net_loopback=" + ("fail" if problems else "ok"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
