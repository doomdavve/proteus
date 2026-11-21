#!/usr/bin/env python3
"""Summarize the private R2b capture-only benchmark, preserving paired field neutrality."""

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    groups = {}
    with args.raw.open(newline="") as handle:
        for row in csv.DictReader(handle):
            key = (int(row["emitters"]), int(row["repetition"]), row["mode"])
            groups.setdefault(key, []).append(row)
    assert len(groups) == 12
    for count in (1, 4):
        for repeat in range(3):
            before = groups[count, repeat, "baseline"]
            after = groups[count, repeat, "capture"]
            assert len(before) == len(after) == 3600
            for index, (a, b) in enumerate(zip(before, after)):
                assert int(a["frame"]) == int(b["frame"]) == index
                assert int(a["live"]) == int(b["live"]) == count * 32
                assert a["births"] == b["births"] and a["height"] == b["height"]
                for row in (a, b):
                    assert math.isfinite(float(row["height"]))
                    assert all(math.isfinite(float(row[f])) and float(row[f]) >= 0
                               for f in ("tracking_ms", "query_ms", "enclosing_ms"))

    def percentile(rows, field, p):
        values = sorted(float(row[field]) for row in rows)
        return values[math.ceil(p * (len(values) - 1))]

    summaries = []
    for count in (1, 4):
        for field in ("tracking_ms", "query_ms", "enclosing_ms"):
            item = {"emitters": count, "component": field}
            for mode in ("baseline", "capture"):
                item[mode] = {
                    label: statistics.median(percentile(groups[count, repeat, mode], field, p)
                                             for repeat in range(3))
                    for label, p in (("p50", .5), ("p95", .95), ("p99", .99))
                }
                values = [percentile(groups[count, repeat, mode], field, .95)
                          for repeat in range(3)]
                item[mode]["p95_range"] = [min(values), max(values)]
            item["delta_ms"] = {p: item["capture"][p] - item["baseline"][p]
                                for p in ("p50", "p95", "p99")}
            item["delta_percent"] = {
                p: 100 * item["delta_ms"][p] / item["baseline"][p]
                if item["baseline"][p] > .005 else None
                for p in ("p50", "p95", "p99")
            }
            summaries.append(item)
    args.output.write_text(json.dumps({
        "raw_sha256": hashlib.sha256(args.raw.read_bytes()).hexdigest(),
        "runs": 12, "measured_frames_per_run": 3600, "warmup_simulated_seconds": 20,
        "measured_simulated_seconds": 60, "neutral_frame_pairs": 21600,
        "summary": summaries,
    }, indent=2) + "\n")


if __name__ == "__main__":
    main()
