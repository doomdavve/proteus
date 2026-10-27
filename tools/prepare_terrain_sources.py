#!/usr/bin/env python3
"""Clip Göteborg's Baskarta contours and spot heights to the first outer-river slice.

Requires pyshp, pyproj and shapely (see docs/adventure/requirements.txt). Run from anywhere:

    python -I tools/prepare_terrain_sources.py --self-test
    python -I tools/prepare_terrain_sources.py SHP_DIR      # SHP_DIR holds the unzipped
                                                             # baskarta_shp_v2.zip layers

Writes docs/adventure/sources/terrain-baskarta.json.gz: `hojdkurvor` (0.5 m contours) and
`hojdpunkt` (spot heights) inside the slice window plus a margin, as east/north metres from
the crane origin and heights in metres. The full 1.6 GB city download is never committed.
Source: Göteborgs stad, Stadsbyggnadsförvaltningen, Baskarta v2 (EPSG:3007), released as open
data under CC0 1.0. Reads only the named directory and never fetches. Since A3.5b the outer
bake takes its heights from tools/prepare_terrain_dem.py and reads only the spot heights here,
as a check.
"""
import argparse
import gzip
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs/adventure/sources/terrain-baskarta.json.gz"
ORIGIN = (11.9089495, 57.6973921)  # The crane origin used by every adventure tool.
# East/north metres from the origin: the baked region (river crop plus first slice) and margin.
WINDOW = (-5600.0, -4200.0, 1250.0, 1100.0)
CONTOUR_TOLERANCE = 0.4  # metres; contours are re-simplified in the plane, z is constant.
LAYERS = ("hojdkurvor", "hojdpunkt")


def origin_3007():
    from pyproj import Transformer

    return Transformer.from_crs(4326, 3007, always_xy=True).transform(*ORIGIN)


def clip_contours(records, ox, oy, window=WINDOW):
    """records: (points[(x, y)], z list) in EPSG:3007. Returns [(z, [x0, y0, x1, y1, ...])]."""
    from shapely.geometry import LineString

    west, south, east, north = window
    out, skipped = [], 0
    for points, zs in records:
        if not points or len(points) < 2:
            continue
        pts = [(x - ox, y - oy) for x, y in points]
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        if max(xs) < west or min(xs) > east or max(ys) < south or min(ys) > north:
            continue
        if max(zs) - min(zs) > 1e-3:
            skipped += 1
            continue
        line = LineString(pts).simplify(CONTOUR_TOLERANCE, preserve_topology=False)
        flat = [round(c, 1) for p in line.coords for c in p]
        out.append((round(sum(zs) / len(zs), 2), flat))
    return out, skipped


def clip_points(records, ox, oy, window=WINDOW):
    west, south, east, north = window
    out = []
    for (x, y), z in records:
        x, y = x - ox, y - oy
        if west <= x <= east and south <= y <= north:
            out.append([round(x, 1), round(y, 1), round(z, 2)])
    return out


def self_test():
    # Contours inside the window are kept, constant z required, and a straight run collapses.
    inside = ([(10, 10), (20, 10.1), (30, 10)], [5.0, 5.0, 5.0])
    outside = ([(1e5, 1e5), (1e5 + 1, 1e5)], [5.0, 5.0])
    sloped = ([(0, 0), (5, 5)], [1.0, 3.0])
    lines, skipped = clip_contours([inside, outside, sloped], 0.0, 0.0)
    assert len(lines) == 1 and skipped == 1 and lines[0][0] == 5.0 and len(lines[0][1]) == 4
    assert clip_points([((3, 4), 2.5), ((1e5, 0), 1.0)], 1.0, 1.0) == [[2.0, 3.0, 2.5]]


def read_layers(directory):
    import shapefile

    result = {}
    for name in LAYERS:
        prj = (directory / f"{name}.prj").read_text()
        assert 'AUTHORITY["EPSG",3007]' in prj, f"{name}: not EPSG:3007"
        reader = shapefile.Reader(str(directory / name))
        result[name] = reader
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("shp_dir", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    self_test()
    if args.self_test or not args.shp_dir:
        print("terrain source fixtures OK")
        return
    ox, oy = origin_3007()
    readers = read_layers(args.shp_dir)
    west, south, east, north = WINDOW
    box = (ox + west, oy + south, ox + east, oy + north)
    contours = []
    for shape in readers["hojdkurvor"].iterShapes(bbox=box):
        points = shape.points
        for i, start in enumerate(shape.parts):
            end = shape.parts[i + 1] if i + 1 < len(shape.parts) else len(points)
            contours.append((points[start:end], list(shape.z[start:end])))
    lines, skipped = clip_contours(contours, ox, oy)
    spots = []
    for shape in readers["hojdpunkt"].iterShapes(bbox=box):
        spots.append((shape.points[0], shape.z[0]))
    points = clip_points(spots, ox, oy)
    lines.sort(key=lambda item: (item[0], item[1]))
    points.sort()
    data = {
        "schema": 1,
        "generator": "tools/prepare_terrain_sources.py",
        "source": "Göteborgs stad, Baskarta v2 (hojdkurvor, hojdpunkt)",
        "licence": "CC0 1.0",
        "crs": "EPSG:3007 offsets from the crane origin; heights in metres",
        "origin_epsg4326": list(ORIGIN),
        "window_wsen": list(WINDOW),
        "contour_tolerance_m": CONTOUR_TOLERANCE,
        "skipped_non_constant_contours": skipped,
        "source_files": {name: {"shp_sha256": hashlib.sha256(
            (args.shp_dir / f"{name}.shp").read_bytes()).hexdigest()} for name in LAYERS},
        "contours": [[z, flat] for z, flat in lines],
        "points": points,
    }
    raw = json.dumps(data, separators=(",", ":"), ensure_ascii=False).encode()
    OUTPUT.write_bytes(gzip.compress(raw, 9, mtime=0))
    print(f"{len(lines)} contours ({skipped} skipped), {len(points)} spot heights, "
          f"{len(raw) / 1e6:.1f} MB json, {OUTPUT.stat().st_size / 1e6:.1f} MB gz")


if __name__ == "__main__":
    sys.exit(main())
