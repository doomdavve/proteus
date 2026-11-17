#!/usr/bin/env python3
"""Summarize retained R2b field screens and contemporary paired CPU runs."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics


def rows(path):
    with path.open(newline="") as stream:
        result = list(csv.DictReader(stream))
    assert result and all(None not in r and None not in r.values() for r in result), path
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--screen", type=Path, required=True)
    parser.add_argument("--cpu", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((args.cpu / "manifest.json").read_text())
    groups = {}
    for run in manifest["runs"]:
        key = (run["path_kind"], run["emitters"], run["layout"], run["mode"])
        groups.setdefault(key, []).append(run)
        raw = Path(run["path"])
        assert hashlib.sha256(raw.read_bytes()).hexdigest() == run["sha256"]
        assert len(rows(raw)) == 3600
    assert len(manifest["runs"]) == 72
    summary = []
    for path in ("straight", "circle", "reverse"):
        for emitters in (1, 4):
            for layout in ("dense", "dispersed"):
                for component in ("source_ms", "query_ms", "total_ms"):
                    item = dict(path=path, emitters=emitters, layout=layout, component=component)
                    for mode in ("baseline", "spread"):
                        runs = groups[path, emitters, layout, mode]
                        assert len(runs) == 3
                        for p in ("p50", "p95", "p99"):
                            item[f"{mode}_{p}"] = statistics.median(r["timings"][component][p] for r in runs)
                        p95 = [r["timings"][component]["p95"] for r in runs]
                        item[f"{mode}_p95_min"] = min(p95)
                        item[f"{mode}_p95_max"] = max(p95)
                        p99 = [r["timings"][component]["p99"] for r in runs]
                        item[f"{mode}_p99_min"] = min(p99)
                        item[f"{mode}_p99_max"] = max(p99)
                    for p in ("p50", "p95", "p99"):
                        delta = item[f"spread_{p}"] - item[f"baseline_{p}"]
                        item[f"delta_{p}_ms"] = delta
                        base = item[f"baseline_{p}"]
                        item[f"delta_{p}_percent"] = 100 * delta / base if base > .005 else ""
                    summary.append(item)
    with (args.output / "timings.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, list(summary[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(summary)
    tables = {name: rows(args.screen / f"{name}.csv") for name in
              ("straight", "arrivals", "derivatives", "turns", "crossings", "concentration")}
    arrival = {mode: [r for r in tables["arrivals"] if r["mode"] == mode]
               for mode in ("baseline", "spread")}
    assert len(arrival["baseline"]) == len(arrival["spread"]) == 1921
    arrival_equal = all({k: v for k, v in a.items() if k != "mode"} ==
                        {k: v for k, v in b.items() if k != "mode"}
                        for a, b in zip(arrival["baseline"], arrival["spread"]))
    concentrations = {tuple(r[k] for k in ("path", "after_stop_seconds", "grid_metres", "mode")): r
                      for r in tables["concentration"]}
    increases = []
    for key, candidate in concentrations.items():
        if key[-1] != "spread":
            continue
        baseline = concentrations[(*key[:-1], "baseline")]
        for component in ("max_height", "min_height", "max_gradient", "max_vertical_speed"):
            a, b = abs(float(baseline[component])), abs(float(candidate[component]))
            assert math.isfinite(a) and math.isfinite(b)
            if b > a:
                increases.append(dict(path=key[0], after_stop_seconds=key[1], grid_metres=key[2],
                                      component=component, baseline=a, spread=b, delta=b-a,
                                      percent=100*(b-a)/a if a > .005 else None))
    report = dict(runs=72, measured_frames_per_run=3600, warmup_simulated_seconds=20,
                  measured_simulated_seconds=60, arrival_traces_identical=arrival_equal,
                  concentration_increases=increases, timing_summary=summary,
                  per_run_screen_exceedances=[dict(path=r["path_kind"], emitters=r["emitters"],
                      layout=r["layout"], mode=r["mode"], repeat=r["repeat"],
                      p95=r["timings"]["total_ms"]["p95"], p99=r["timings"]["total_ms"]["p99"],
                      raw=r["path"], sha256=r["sha256"]) for r in manifest["runs"]
                      if r["timings"]["total_ms"]["p95"] > .10
                      or r["timings"]["total_ms"]["p99"] > .15],
                  cpu_manifest_sha256=hashlib.sha256((args.cpu / "manifest.json").read_bytes()).hexdigest(),
                  screen_sha256={name: hashlib.sha256((args.screen / f"{name}.csv").read_bytes()).hexdigest()
                                 for name in tables})
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
