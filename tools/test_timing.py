#!/usr/bin/env python3
"""Rank CTest durations from a JUnit report and flag quick-lane tests over their budget.

    ctest --test-dir build -j 10 --output-junit test-speed/run.xml
    python3 tools/test_timing.py build/test-speed/run.xml --top 20

Tests labelled `quick` must finish within --budget seconds each; --enforce turns an overrun
into a non-zero exit. This is a report, not a CTest test, so machine load cannot fail the suite.
"""

import argparse
import sys
import xml.etree.ElementTree as ET


def load(path):
    tests = []
    for case in ET.parse(path).getroot().iter("testcase"):
        if case.get("status") == "disabled":
            continue
        labels = ""
        for prop in case.iter("property"):
            if prop.get("name") == "cmake_labels":
                labels = prop.get("value") or ""
        failed = case.get("status") == "fail" or case.find("failure") is not None
        tests.append((float(case.get("time", 0)), case.get("name"), labels, failed))
    return tests


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("report", help="JUnit XML written by ctest --output-junit")
    parser.add_argument("--top", type=int, default=25, help="rows to print (0: all)")
    parser.add_argument("--budget", type=float, default=.1, help="quick-lane seconds per test")
    parser.add_argument("--enforce", action="store_true", help="exit 1 on a quick overrun")
    args = parser.parse_args()
    tests = sorted(load(args.report), reverse=True)
    if not tests:
        sys.exit(f"{args.report}: no tests ran")
    total = sum(t for t, *_ in tests)
    over = [(t, name) for t, name, labels, _ in tests
            if "quick" in labels.split(";") and t > args.budget]
    rows = tests if args.top == 0 else tests[:args.top]
    width = max(len(name) for _, name, *_ in rows)
    for seconds, name, labels, failed in rows:
        mark = " FAILED" if failed else ""
        print(f"{seconds:8.3f} s {seconds / total:6.1%}  {name:<{width}}  {labels}{mark}")
    buckets = [(.02, "<=20 ms"), (.05, "<=50 ms"), (1, "<=1 s"), (float("inf"), ">1 s")]
    counts, low = [], -1
    for limit, label in buckets:
        counts.append(f"{label}: {sum(1 for t, *_ in tests if low < t <= limit)}")
        low = limit
    print(f"{len(tests)} tests, {total:.1f} s summed; " + ", ".join(counts))
    failed = sum(1 for *_, f in tests if f)
    if failed:
        print(f"{failed} failed")
    for seconds, name in over:
        print(f"over quick budget ({args.budget * 1000:.0f} ms): {name} {seconds * 1000:.0f} ms")
    if args.enforce and over:
        sys.exit(1)


if __name__ == "__main__":
    main()
