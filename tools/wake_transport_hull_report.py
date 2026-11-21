#!/usr/bin/env python3
"""Report private paired hull screens without treating changed outcomes as acceptance."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--field", type=Path, default=Path("docs/evidence/wakes/r2b/field/concentration.csv"))
    args = parser.parse_args()
    with args.input.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == 504
    assert all(None not in row and None not in row.values() for row in rows)
    keys = ("fixture", "turn", "after_stop_seconds", "target", "initial_speed", "initial_angle")
    pairs = {}
    for row in rows:
        key = tuple(row[k] for k in keys)
        assert row["mode"] not in pairs.setdefault(key, {})
        pairs[key][row["mode"]] = row
    # Floors decide only when to show a relative ratio, not a new acceptance tolerance.
    floors = dict(heave=.05, roll=.01, pitch=.01, launch=.1, landing_delta_v_proxy=.1)
    report = dict(pairs=len(pairs), straight_controls=0, metric_flags=[],
                  near_zero_baseline_changes=[], outcome_changes=[], unsettled=[],
                  at_time_extrema=[], ratio_floors=floors)
    for key, modes in pairs.items():
        a, b = modes["baseline"], modes["spread"]
        identity = dict(zip(keys, key))
        assert a["ticks"] == b["ticks"]
        if a["fixture"] == "straight":
            assert {k: v for k, v in a.items() if k != "mode"} == {k: v for k, v in b.items() if k != "mode"}
            report["straight_controls"] += 1
        for metric, floor in floors.items():
            before, after = float(a[metric]), float(b[metric])
            assert math.isfinite(before) and math.isfinite(after)
            delta = after - before
            item = dict(case=identity, metric=metric, baseline=before, spread=after, delta=delta)
            if before > floor:
                item["percent"] = 100 * delta / before
                if abs(item["percent"]) > 10:
                    report["metric_flags"].append(item)
            elif delta:
                report["near_zero_baseline_changes"].append(item)
        changes = {k: [a[k], b[k]] for k in ("landings", "capsized", "final_state") if a[k] != b[k]}
        if changes:
            report["outcome_changes"].append(dict(case=identity, changes=changes))
        if a["settled"] != "1" or b["settled"] != "1":
            report["unsettled"].append(identity)
        if a["fixture"].endswith("-at-time"):
            for row in (a, b):
                assert int(row["target_support_queries"]) > 0
                assert float(row["first_target_time"]) == 29
            report["at_time_extrema"].append(dict(case=identity,
                target=[float(a["target_x"]), float(a["target_z"])],
                baseline_height=float(a["target_initial_height"]), spread_height=float(b["target_initial_height"])))
    assert report["straight_controls"] == 27
    with args.field.open(newline="") as stream:
        field = {r["mode"]: r for r in csv.DictReader(stream)
                 if r["path"] == "eight" and r["after_stop_seconds"] == "4"}
    trough = next(r for r in report["at_time_extrema"] if r["case"]["fixture"] == "eight-trough-at-time")
    assert trough["baseline_height"] == float(field["baseline"]["min_height"])
    assert trough["spread_height"] == float(field["spread"]["min_height"])
    report["flagged_one_metre_trough_matched"] = True
    report["input_sha256"] = hashlib.sha256(args.input.read_bytes()).hexdigest()
    report["geometry_selected"] = False
    args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
