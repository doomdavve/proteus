#!/usr/bin/env python3
"""M5 real-time soak driver over real ENet processes; for manual runs such as --seconds 1800.

Not a CTest test: it runs on the wall clock. The same lifecycle runs on a virtual clock in
validate_boat_net_enet (boat_net_enet.enet_lifecycle). Uses only loopback and private UDP
ports. Three clients stay while the fourth leaves, rejoins, crashes and is replaced after
timeout. All drive, reset and send inactive input. The room drops silent peers after
--peer-timeout seconds (default 5, as in a normal room).
Every child has a deadline and is reaped, including on assertion failure or interruption.
Logs go to temporary files so a child's output cannot block a pipe or exhaust memory.
This does not measure memory plateaus or simulate network impairment.
"""

import argparse
import subprocess
import tempfile
import time
from pathlib import Path

from boat_net_process import fields, free_port

CHURN_SECONDS = 3  # Each leaving or replacement bot: long enough to reset and go inactive.
CRASH_HOLD = 1  # Seconds the crashing bot drives before it is killed.


def cycle_seconds(peer_timeout):
    """One leave/reject/crash/timeout/replace cycle, with a second for joins."""
    return 2 * CHURN_SECONDS + CRASH_HOLD + peer_timeout + 1 + 1


class Child:
    def __init__(self, command, directory, name):
        self.path = Path(directory) / f"{name}.log"
        self.log = self.path.open("w")
        self.process = subprocess.Popen(command, stdout=self.log, stderr=subprocess.STDOUT)
        self.deadline = time.monotonic() + 12
        self.name = name

    def output(self):
        return self.path.read_text()

    def wait_for(self, marker):
        while time.monotonic() < self.deadline:
            text = self.output()
            if marker in text:
                return text
            if self.process.poll() is not None:
                break
            time.sleep(.02)
        raise AssertionError(f"{self.name}: missing {marker!r}:\n{self.output()}")

    def finish(self):
        self.process.wait(timeout=max(.01, self.deadline - time.monotonic()))
        text = self.output()
        print(f"[{self.name}]\n{text.strip()}", flush=True)
        self.log.close()
        assert self.process.returncode == 0, f"{self.name}: exit {self.process.returncode}"
        return text

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=5)
        self.log.close()


def check_bot(child, seconds):
    out = child.finish()
    summary = fields(out, "boat_bot summary:")
    lifecycle = fields(out, "lifecycle:")
    transport = fields(out, "transport:")
    assert int(summary["snapshots"]) >= seconds * 20, child.name
    assert int(summary["bootstraps"]) >= 1 and int(summary["resyncs"]) <= 3, child.name
    assert float(summary["max_mismatch_ms"]) < 1000, child.name
    assert fields(out, "control:")["decode_errors"] == "0", child.name
    assert 0 < int(lifecycle["max_wakes"]) <= 128, child.name
    assert int(lifecycle["max_players"]) == 4, child.name
    assert int(lifecycle["resets_completed"]) > 0, child.name
    assert int(lifecycle["inactive_periods"]) > 0, child.name
    assert int(transport["max_message"]) <= 1100, child.name
    assert 0 < int(transport["max_datagram"]) <= 1200, child.name
    assert 0 < int(transport["peer_mtu"]) <= 1200, child.name
    assert fields(out, "births:")["dropped"] == "0", child.name


def run(args, directory):
    children = []

    def spawn(command, name):
        child = Child(command, directory, name)
        children.append(child)
        return child

    port = free_port()
    endpoint = f"127.0.0.1:{port}"
    server_seconds = args.seconds + 4
    try:
        server = spawn([args.server, "--port", str(port), "--no-lan", "--waves",
                        "--location", args.location, "--mode", args.mode,
                        "--peer-timeout", str(args.peer_timeout),
                        "--seconds", str(server_seconds)], "server")
        server.wait_for("listening on")
        server.deadline = time.monotonic() + server_seconds + 10

        def bot(name, seconds, inactive_every=3):
            child = spawn([args.bot, "--connect", endpoint, "--name", name,
                           "--seconds", str(seconds), "--predict", "--reset-every", "2",
                           "--inactive-every", str(inactive_every)], name)
            child.wait_for("boat_bot joined:")
            assert f"location={args.location}" in child.output(), child.name
            child.deadline = time.monotonic() + seconds + 10
            return child

        persistent = [bot(f"stay-{i}", args.seconds) for i in range(3)]
        churn_until = time.monotonic() + args.seconds - cycle_seconds(args.peer_timeout)
        cycles = 0
        while True:
            fourth = bot(f"leave-{cycles}", CHURN_SECONDS, 2)
            full = spawn([args.bot, "--connect", endpoint, "--expect-reject"],
                         f"full-{cycles}")
            assert "room is full" in full.finish()
            check_bot(fourth, CHURN_SECONDS)
            crashed = bot(f"crash-{cycles}", 10)
            # Join-in-progress while the other three continue driving and resetting.
            time.sleep(CRASH_HOLD)
            crashed.process.kill()
            crashed.process.wait(timeout=5)
            crashed.log.close()
            # The room's peer timeout; ENet may report the disconnect first.
            time.sleep(args.peer_timeout + 1)
            replacement = bot(f"rejoin-{cycles}", CHURN_SECONDS, 2)
            check_bot(replacement, CHURN_SECONDS)
            cycles += 1
            if time.monotonic() >= churn_until:
                break

        for child in persistent:
            check_bot(child, args.seconds)
        out = server.finish()
        room = fields(out, "boat_server room:")
        assert int(room["ticks"]) == server_seconds * 120
        assert int(room["accepted"]) == 3 + 3 * cycles
        assert room["accepted"] == room["left"]
        assert int(room["timeouts"]) == cycles
        assert int(fields(out, "inputs:")["neutral"]) > 0
        assert int(fields(out, "rejected:")["full"]) == cycles
        faults = fields(out, "faults:")
        assert all(faults[k] == "0" for k in
                   ("malformed", "forbidden", "misbehaving", "output", "emission"))
        transport = fields(out, "transport:")
        assert int(transport["max_message"]) <= 1100
        assert 0 < int(transport["max_datagram"]) <= 1200
        print(f"boat_net_session=ok cycles={cycles} seconds={args.seconds}")
    except BaseException:
        for child in children:
            print(f"[{child.name} failure context]\n{child.output()}", flush=True)
        raise
    finally:
        for child in children:
            child.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--server", required=True)
    parser.add_argument("--bot", required=True)
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--peer-timeout", type=int, default=5, choices=range(1, 61),
                        metavar="1-60")
    parser.add_argument("--location", choices=("basin", "river"), default="basin")
    parser.add_argument("--mode", choices=("free", "deathmatch"), default="free")
    args = parser.parse_args()
    shortest = cycle_seconds(args.peer_timeout) + 1
    if not shortest <= args.seconds <= 7200:
        parser.error(f"--seconds must be between {shortest} and 7200")
    with tempfile.TemporaryDirectory(prefix="boat-net-session-") as directory:
        run(args, directory)


if __name__ == "__main__":
    main()
