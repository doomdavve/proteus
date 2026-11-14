#!/usr/bin/env python3
"""H5 lockdown soak (Linux): boat_server --lockdown kill serves real ENet/UDP bots and a LAN
search after dropping filesystem access and syscalls, then exits cleanly (a stray syscall
would kill it with SIGSYS). Also checks the refusal path: a required Landlock ABI above what
any kernel offers must stop the server before it serves. Event-driven; deadlines only.
"""

import argparse
import socket
import subprocess
import sys

DEADLINE = 30


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--bot", required=True)
    parser.add_argument("--mode", default="kill")
    args = parser.parse_args()
    problems = []
    refused = subprocess.run([args.server, "--port", str(free_port()), "--no-lan", "--lockdown",
        "errno", "--lockdown-min-abi", "99", "--ticks", "1"], capture_output=True, text=True,
        timeout=DEADLINE)
    if refused.returncode == 0 or "refused, not serving" not in refused.stderr:
        problems.append(f"refusal: {refused.returncode} {refused.stdout}{refused.stderr}")
    port, lan_port = free_port(), free_port()
    endpoint = f"127.0.0.1:{port}"
    server = subprocess.Popen([args.server, "--port", str(port), "--lan-port", str(lan_port),
        "--lockdown", args.mode, "--until-empty"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        line = ""
        for _ in range(10):
            line = server.stdout.readline()
            if line.startswith("lockdown:") or not line:
                break
        if not line.startswith("lockdown:"):
            raise AssertionError(f"no lockdown line: {line!r} {server.poll()}")
        print(line.strip())
        search = subprocess.run([args.bot, "--discover", "--first", "--lan-port", str(lan_port),
            "--seconds", str(DEADLINE)],  # deadline: --first exits at the first answer
            capture_output=True, text=True, timeout=DEADLINE + 5)
        if search.returncode != 0 or f"lan room {endpoint} " not in search.stdout:
            problems.append(f"LAN search: {search.returncode} {search.stdout}{search.stderr}")
        bots = [subprocess.Popen([args.bot, "--connect", endpoint, "--name", name,
                    "--snapshots", "20"], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    text=True) for name in ("lock-a", "lock-b")]
        for bot in bots:
            out, err = bot.communicate(timeout=DEADLINE)
            if bot.returncode != 0 or "boat_bot summary:" not in out:
                problems.append(f"bot exit {bot.returncode}: {out}{err}")
        out, err = server.communicate(timeout=DEADLINE)
        if server.returncode != 0 or "every player has left" not in out:
            problems.append(f"server exit {server.returncode}: {err}")
    finally:
        if server.poll() is None:
            server.kill()
    for problem in problems:
        print(problem)
    print("boat_lockdown_process=" + ("fail" if problems else "ok"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
