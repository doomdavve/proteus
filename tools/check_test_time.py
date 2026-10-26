#!/usr/bin/env python3
"""Reject tests that wait for wall-clock time to pass.

Time-dependent code takes the time as an input, and tests drive it with a virtual clock (see
docs/TESTING.md). Sleeping, fixed-duration runs and decisions on elapsed time make tests slow
and flaky, and usually mean state that should have been extracted. Wall-clock limits are
allowed only as failure deadlines: subprocess timeouts, or an option or call on a line tagged
with a "deadline" comment.

Scanned: the validators and test headers (tools/validate_*.c, tools/boat_test_*.h), every
script CMakeLists.txt registers as a test, and the add_test commands themselves. Measuring a
cost with clock() for a printed report is allowed; asserting on it is not (review rule).
"""

import argparse
import re
import sys
from pathlib import Path

C_WAITS = re.compile(r"\b(sleep|usleep|nanosleep|thrd_sleep|boat_net_sleep|boat_net_enet_wait|"
                     r"boat_net_lan_wait|WaitTime|SetTargetFPS)\s*\(")
PY_CLOCKS = re.compile(r"\btime\.(sleep|monotonic|time|perf_counter)\s*\(|\"--seconds\"")
JS_CLOCKS = re.compile(r"\b(setTimeout|setInterval|Date\.now|performance\.now)\s*\(")
TEST_ARGS = re.compile(r"(?<!\w)--(seconds|frames)\b")

# Known exceptions, each a follow-up: test name -> reason.
ALLOWED_TESTS = {
    "wake_velocity_host_gpu": "windowed frames follow the display; the game loop takes its "
                              "frame time from the wall clock",
}


def scripts(cmake):
    return sorted(set(re.findall(r"\$\{CMAKE_CURRENT_SOURCE_DIR\}/(tools/[\w.]+\.(?:py|cjs|mjs))",
                                 cmake)))


def add_tests(cmake):
    """(name, command text) for each add_test(NAME ...) call."""
    for match in re.finditer(r"add_test\(NAME\s+(\S+)(.*?)\)\s*$", cmake, re.S | re.M):
        yield match.group(1), match.group(2)


def scan_lines(path, text, pattern, comment):
    problems = []
    for number, line in enumerate(text.splitlines(), 1):
        code = line.split(comment, 1)[0]
        if pattern.search(code) and "deadline" not in line:
            problems.append(f"{path}:{number}: waits on wall time: {line.strip()}")
    return problems


def check(root):
    cmake = (root / "CMakeLists.txt").read_text()
    problems = []
    sources = sorted(root.glob("tools/validate_*.c")) + sorted(root.glob("tools/boat_test_*.h"))
    for path in sources:
        problems += scan_lines(path.relative_to(root), path.read_text(), C_WAITS, "//")
    for name in scripts(cmake):
        path = root / name
        if path.name == Path(__file__).name:
            continue  # Its self-test holds the very patterns it rejects.
        if path.suffix == ".py":
            problems += scan_lines(name, path.read_text(), PY_CLOCKS, "#")
        else:
            problems += scan_lines(name, path.read_text(), JS_CLOCKS, "//")
    for name, command in add_tests(cmake):
        # *_rejected tests check that an invalid command line exits before running.
        if TEST_ARGS.search(command) and name not in ALLOWED_TESTS and not name.endswith(
                "_rejected"):
            problems.append(f"CMakeLists.txt: test {name} runs for wall-clock time: "
                            f"{' '.join(command.split())}")
    return problems


def self_test():
    bad = [("x.c", "    usleep(1000);", C_WAITS, "//"),
           ("x.py", "    time.sleep(1)", PY_CLOCKS, "#"),
           ("x.py", '    run([bot, "--seconds", "4"])', PY_CLOCKS, "#"),
           ("x.cjs", "  setTimeout(done, 10);", JS_CLOCKS, "//")]
    good = [("x.c", "    clock_t start = clock(); // cost report", C_WAITS, "//"),
            ("x.py", '    run([bot, "--seconds", "20"])  # deadline', PY_CLOCKS, "#"),
            ("x.c", "    // usleep(1000) in a comment", C_WAITS, "//")]
    for path, line, pattern, comment in bad:
        assert scan_lines(path, line, pattern, comment), line
    for path, line, pattern, comment in good:
        assert not scan_lines(path, line, pattern, comment), line
    assert dict(add_tests("add_test(NAME t COMMAND bot --seconds 4)\n")) == {
        "t": " COMMAND bot --seconds 4"}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    args = parser.parse_args()
    self_test()
    problems = check(args.root)
    for problem in problems:
        print(problem)
    print("test_time=" + ("fail" if problems else "ok"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
