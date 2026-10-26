#!/usr/bin/env python3
"""Bake the first outer-river slice (river crop plus the Långedrag run) from the A0 sources.

Requires the pins in docs/adventure/requirements.txt. Run from anywhere:

    python -I tools/prepare_outer_map.py --self-test     # fixtures only
    python -I tools/prepare_outer_map.py                 # writes assets/geometry/outer/

Reads only the checked-in A0.2 sources and never fetches. Normal C builds use the checked-in
output. The map holds water, flat land and quay walls only: no buildings, bridge, roads or
planting (A3 adds them per region). Water is the A0.3 assembly (land polygons plus OSM inland
water, bridge legs removed); land is the rest of the region at the river's assumed 3.5 m ground.
Where the region cuts open water, the water ends at an edge that collision treats as shore: a
playable boundary, not a coastline. The manifest reports its length.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from shapely.geometry import LineString, Point, Polygon, box  # noqa: E402
from shapely.geometry.polygon import orient  # noqa: E402
from shapely.ops import unary_union  # noqa: E402

from prepare_waterfront import GROUND, polygons, triangulate  # noqa: E402

spec = importlib.util.spec_from_file_location("survey", ROOT / "docs/adventure/survey.py")
survey = importlib.util.module_from_spec(spec)
spec.loader.exec_module(survey)

# Straight shoreline tolerance for baking. The A0.3 coarse-LOD figure is 8 m; the river bake
# keeps mapped vertices, so stay well below hull scale here.
SIMPLIFY = 0.5
QUAY_BOTTOM = -2.0
ROUTE_CLEARANCE = survey.ROUTE_CLEARANCE


def region_shape():
    """The river crop joined to the first slice, in crane-origin east/north metres."""
    crop = box(*survey.RIVER_CROP)
    return unary_union([crop, Polygon(survey.FIRST_SLICE)])


def clean(shape):
    """Drop zero-width spikes where a shore meets the region edge along the same line.

    The difference of the region and its water can leave a polygon whose ring runs out along
    the region edge and back; constrained triangulation rejects it. A 1 mm opening removes
    only such spikes, so it is applied just to the polygons that fail.
    """
    kept = []
    for poly in polygons(shape):
        try:
            triangulate(poly)
        except AssertionError:
            poly = poly.buffer(-.001, join_style=2).buffer(.001, join_style=2)
            kept += polygons(poly)
        else:
            kept.append(poly)
    return unary_union(kept)


def bake(water, region):
    """Return (vertices, triangles, stats) for water at y=0 then land and walls."""
    vertices, triangles, lookup = [], [], {}

    def triangle(points, color):
        ids = []
        for p in points:
            p = tuple(round(v, 4) for v in p)
            if p not in lookup:
                lookup[p] = len(vertices)
                vertices.append(p)
            ids.append(lookup[p])
        triangles.append((ids, color))

    def surface(shape, y, color):
        for poly in polygons(shape):
            for t in triangulate(poly):
                # Clockwise in X/Z means upward-facing in X/Y/Z.
                pts = list(orient(t, sign=-1).exterior.coords)[:3]
                triangle([(x, y, z) for x, z in pts], color)

    def walls(shape, bottom, top, color):
        for poly in polygons(shape):
            poly = orient(poly, sign=1)
            for ring in [poly.exterior, *poly.interiors]:
                for (x, z), (xx, zz) in zip(ring.coords, list(ring.coords)[1:]):
                    a, b, c, d = (x, bottom, z), (xx, bottom, zz), (xx, top, zz), (x, top, z)
                    triangle([a, c, b], color)
                    triangle([a, d, c], color)

    land = clean(region.difference(water))
    surface(water, 0, (45, 79, 104))
    water_triangles = len(triangles)
    surface(land, GROUND, (135, 139, 144))
    walls(land, 0, GROUND, (94, 102, 114))
    walls(land, QUAY_BOTTOM, 0, (94, 102, 114))
    return vertices, triangles, {"water_triangles": water_triangles, "land": land}


def assemble_region(frame, osm, land_source):
    geo = survey.assemble(osm, land_source, frame)
    region = region_shape()
    navigable = next(p for p in survey.polygons_of(geo["water"]) if p.contains(Point(survey.DOCK)))
    # Keep only water connected to the dock. Disconnected ponds inside the region are
    # not part of the playable water and would add unreachable collision.
    water = navigable.simplify(SIMPLIFY, preserve_topology=True).intersection(region)
    water = unary_union([p for p in survey.polygons_of(water)
                         if p.contains(Point(survey.DOCK)) or p.area > 2500])
    return geo, region, water


def check(geo, region, water):
    """Assertions the bake must satisfy against the A0.3 route and shelter."""
    connected = next(p for p in survey.polygons_of(water) if p.contains(Point(survey.DOCK)))
    corridor = water.buffer(-ROUTE_CLEARANCE)
    route = LineString(survey.SLICE_ROUTE)
    assert corridor.covers(route), "slice route leaves the baked water envelope"
    assert connected.contains(Point(survey.DETOUR[-1])), "baked water does not reach the shelter"
    assert region.buffer(1e-6).covers(water)
    return route


def write(output, vertices, triangles, stats, region, water, route):
    output.mkdir(parents=True, exist_ok=True)
    lines = ["// Generated by tools/prepare_outer_map.py. Do not edit.",
             "// Map data (c) OpenStreetMap contributors, ODbL 1.0; see manifest.json.",
             "static const Vector3 outer_vertices[] = {"]
    lines += ["    { " + ", ".join(f"{v:.4f}f" for v in p) + " }," for p in vertices]
    lines += ["};", "static const Triangle3D outer_triangles[] = {"]
    lines += [f"    {{ {{ {i[0]}, {i[1]}, {i[2]} }}, {{ {c[0]}, {c[1]}, {c[2]}, 255 }} }},"
              for i, c in triangles]
    lines += ["};", f"enum {{ outer_bridge_first_triangle = {len(triangles)} }};", ""]
    content = "\n".join(lines)
    (output / "geometry.h").write_text(content)
    land = stats["land"]
    boundary = water.boundary.intersection(region.boundary.buffer(0.01))
    manifest = {
        "generator": "tools/prepare_outer_map.py",
        "sources": ["docs/adventure/sources/osm-corridor.json.gz",
                    "docs/adventure/sources/land-polygons.json.gz"],
        "origin_epsg4326": list(survey.ORIGIN),
        "crs": "EPSG:3007 offsets from the crane origin; x = east, z = north",
        "license": "ODbL-1.0", "license_url": "https://opendatacommons.org/licenses/odbl/1-0/",
        "attribution": "Map data © OpenStreetMap contributors",
        "ground": "Assumed flat ground at 3.5 m above water; terrain samples not used",
        "quay_bottom": QUAY_BOTTOM,
        "region": {"river_crop": list(survey.RIVER_CROP), "first_slice": survey.FIRST_SLICE,
                   "area_km2": round(region.area / 1e6, 3)},
        "simplify_m": SIMPLIFY,
        "water_area_km2": round(water.area / 1e6, 3),
        "land_area_km2": round(land.area / 1e6, 3),
        "playable_boundary_m": round(boundary.length, 1),
        "route_length_m": round(route.length, 1),
        "route_min_shore_clearance_m": round(route.distance(water.boundary), 1),
        "vertices": len(vertices), "triangles": len(triangles),
        "water_triangles": stats["water_triangles"],
        "contents": "water, flat land and quay walls only; no buildings, bridge or roads",
        "geometry_sha256": hashlib.sha256(content.encode()).hexdigest(),
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Baked {len(vertices)} vertices, {len(triangles)} triangles "
          f"({stats['water_triangles']} water), {boundary.length:.0f} m playable boundary")


def self_test():
    # Water stays below a raised shelf; land is the region less water; an island hole survives.
    region = box(0, 0, 100, 100)
    water = box(10, 10, 90, 90).difference(box(40, 40, 60, 60))
    vertices, triangles, stats = bake(water, region)
    assert stats["water_triangles"] == len(triangulate(water))
    assert all(vertices[v][1] == 0 for ids, _ in triangles[:stats["water_triangles"]]
               for v in ids)
    assert abs(stats["land"].area - (10000 - water.area)) < 1e-6
    # Region edge through open water is a playable boundary, not shoreline.
    cut = region.intersection(box(-50, 20, 150, 80))
    assert abs(cut.boundary.intersection(region.boundary.buffer(.01)).length - 120) < .1


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    self_test()
    if args.self_test:
        print("outer map fixtures OK")
        return
    sources = json.loads(survey.MANIFEST.read_text())
    osm, _ = survey.read_source("osm-corridor.json.gz", sources)
    land_source, _ = survey.read_source("land-polygons.json.gz", sources)
    frame = survey.Frame()
    geo, region, water = assemble_region(frame, osm, land_source)
    route = check(geo, region, water)
    vertices, triangles, stats = bake(water, region)
    write(args.output or ROOT / "assets/geometry/outer", vertices, triangles, stats, region,
          water, route)


if __name__ == "__main__":
    main()
