#!/usr/bin/env python3
"""Fail a routine CI build if its curated lane grows or loses essential checks."""
import argparse
import json
import subprocess
import sys

REQUIRED = {"test_time", "platform_bridges", "plugin_stage_backend", "plugin_data_boundary",
            "boat_core_boundary", "boat_net_boundary", "boat_screenshot", "boat_outer_screenshot"}


def check(tests):
    names = {test["name"] for test in tests}
    if not 50 <= len(tests) <= 150:
        raise ValueError(f"CI lane has {len(tests)} checks; expected 50..150. Review its budget.")
    if missing := REQUIRED - names:
        raise ValueError(f"CI lane is missing critical checks: {sorted(missing)}")
    for test in tests:
        props = {prop["name"]: prop["value"] for prop in test.get("properties", [])}
        labels = props.get("LABELS", [])
        if props.get("DISABLED") or "gpu" in labels or ("slow" in labels and test["name"] not in REQUIRED):
            raise ValueError(f"CI lane includes a disabled, GPU or exhaustive check: {test['name']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--ctest", default="ctest")
    args = parser.parse_args()
    listing = subprocess.run([args.ctest, "--test-dir", args.build_dir, "-C", args.config,
                              "-L", "^ci$", "--show-only=json-v1"],
                             check=True, capture_output=True, text=True,
                             timeout=10)  # Failure deadline, no simulation clock.
    tests = json.loads(listing.stdout)["tests"]
    check(tests)
    print(f"CI lane: {len(tests)} enabled checks; maximum 150; no exhaustive/GPU cases")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, subprocess.SubprocessError) as error:
        sys.exit(str(error))
