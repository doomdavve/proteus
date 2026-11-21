#!/usr/bin/env python3
"""Inspect how a mapped building was built, and how to override it, without editing C.

Reads assets/geometry/<map>/buildings.json (written by the bakes, one entry per source id) and
tools/building_overrides.json. Needs only the Python standard library.

    python tools/building_report.py                    # family distribution, fallbacks, legacy
    python tools/building_report.py way/164963867      # one building (also: 164963867, w/123)
    python tools/building_report.py --family warehouse --map outer --limit 20
    python tools/building_report.py --fallbacks        # buildings built without enough facts
    python tools/building_report.py --check            # every override names a baked building

To change a building add an entry keyed by its source id to tools/building_overrides.json (the
report prints a starting point), rebake (docs/BUILDINGS.md#rebaking) and look again:
  {"way/N": {"auto": true, "family": "brick", "wall": [172, 94, 70]}}   tune the automatic result
  {"way/N": {"family": "balcony", ...}}                                 a full authored composition
  {"way/N": {"legacy": true, "note": "why"}}                            keep the pre-kit walls
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAPS = ("river", "outer")
OVERRIDES = ROOT / "tools/building_overrides.json"


def load(name):
    path = ROOT / "assets/geometry" / name / "buildings.json"
    if not path.exists():
        return None
    return json.loads(path.read_text())


def entry(columns, row):
    return dict(zip(columns, row))


def normalise(text):
    """Source ids as the bake writes them: way/N, relation/N (w/N and r/N are the survey's)."""
    text = text.strip()
    m = re.fullmatch(r"(way|relation|w|r)?/?(\d+)(#\d+)?", text)
    if not m:
        raise SystemExit(f"not a source id: {text!r} (way/N, relation/N, w/N, r/N or N)")
    kind = {"w": "way", "r": "relation"}.get(m.group(1), m.group(1))
    return kind, m.group(2), m.group(3)


def find(reports, text):
    kind, number, piece = normalise(text)
    hits = []
    for name, report in reports.items():
        for ident, row in report["buildings"].items():
            base, _, p = ident.partition("#")
            k, n = base.split("/")
            if n == number and (kind is None or k == kind) and (piece is None or "#" + p == piece):
                hits.append((name, ident, entry(report["columns"], row)))
    return hits


def summary(reports):
    for name, report in reports.items():
        s = report["summary"]
        print(f"{name}: {s['buildings']} buildings, {s['facades']} facades, {s['windows']} windows,"
              f" {s['triangles']} triangles")
        for key in ("families", "basis", "contexts", "roofs"):
            print(f"  {key}: " + ", ".join(f"{k} {v}" for k, v in s[key].items()))
        for why, count in s["fallback_reasons"].items():
            print(f"  fallback {count:5d}  {why}")
        print(f"  legacy (unmigrated): {', '.join(s['legacy']) or 'none'}")


def show(name, ident, e, overrides):
    print(f"{ident}   [{name}]")
    print(f"  family   {e['family']}   basis: {e['basis']}   district: {e['context']}")
    print(f"  because  {e['why']}")
    print(f"  facts    building={e['building']}  footprint {e['area_m2']} m2  height {e['height_m']} m"
          f" ({e['height_basis']})")
    print(f"  built    roof {e['roof'] or 'flat'}, {e['facades']} facades, {e['windows']} windows,"
          f" {e['triangles']} triangles" + (f", detail {e['detail']}" if e["detail"] else ""))
    base = ident.partition("#")[0]
    if base in overrides:
        print(f"  override {json.dumps(overrides[base])}")
    else:
        family = e["family"] if e["family"] not in ("plain", "stone") else "block"
        tail = ', "map": "outer"' if name == "outer" else ""
        print("  override none. To change it, add to tools/building_overrides.json:")
        print(f'    "{base}": {{"auto": true, "family": "{family}"{tail}}}')
    print("  then rebake (docs/BUILDINGS.md) and re-run this command.")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("id", nargs="?")
    ap.add_argument("--map", choices=MAPS)
    ap.add_argument("--family")
    ap.add_argument("--fallbacks", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--limit", type=int, default=40)
    args = ap.parse_args()
    reports = {n: r for n in MAPS if (not args.map or n == args.map) and (r := load(n))}
    if not reports:
        raise SystemExit("no assets/geometry/<map>/buildings.json: run the bakes (docs/BUILDINGS.md)")
    overrides = {k: v for k, v in json.loads(OVERRIDES.read_text()).items() if not k.startswith("_")}
    if args.check:
        known = {i.partition("#")[0] for r in reports.values() for i in r["buildings"]}
        missing = sorted(k for k in overrides if k not in known)
        print("overrides naming no baked building:", ", ".join(missing) or "none")
        return 1 if missing else 0
    if args.id:
        hits = find(reports, args.id)
        if not hits:
            print(f"{args.id}: not a building of the maps' reports (a landmark, a part of another"
                  f" building, outside the maps, or under the minimum footprint)")
            return 1
        for hit in hits:
            show(*hit, overrides)
        return 0
    if args.family or args.fallbacks:
        shown = 0
        for name, report in reports.items():
            for ident, row in report["buildings"].items():
                e = entry(report["columns"], row)
                if (args.family and e["family"] != args.family) or (
                        args.fallbacks and e["basis"] not in ("fallback", "plain", "legacy")):
                    continue
                if shown < args.limit:
                    print(f"{ident:24} {name:5} {e['family']:10} {e['basis']:9} {e['why']}")
                shown += 1
        print(f"{shown} buildings" + (f" (first {args.limit} shown)" if shown > args.limit else ""))
        return 0
    summary(reports)
    return 0


if __name__ == "__main__":
    sys.exit(main())
