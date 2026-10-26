#!/usr/bin/env python3
"""Bake the first outer-river slice (river crop plus the Långedrag run) from the A0 sources.

Requires the pins in docs/adventure/requirements.txt. Run from anywhere:

    python -I tools/prepare_outer_map.py --self-test     # fixtures only
    python -I tools/prepare_outer_map.py                 # writes assets/geometry/outer/

Reads only the checked-in A0.2 sources and never fetches. Normal C builds use the checked-in
output. Outside the river crop the map holds water, Baskarta terrain and quay walls, and since
A3.5a the corridor's buildings, the container cranes and port masts, channel marks and a
scattered tree cover (tools/outer_dressing.py); no roads. Inside the crop (A3.3) the ground is
the river's flat 3.5 m and the river bake's buildings, roads, gardens, trees and bridge are
imported unchanged from
assets/geometry/river/geometry.h, so the crop looks and behaves as the river location does; the
terrain blends in over BLEND_M outside it. Water is the A0.3 assembly (land polygons plus OSM
inland water, bridge legs removed).
Where the region cuts open water, the water ends at an edge that collision treats as shore: a
playable boundary, not a coastline. The manifest reports its length.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from shapely.geometry import LineString, Point, Polygon, box  # noqa: E402
from shapely.geometry.polygon import orient  # noqa: E402
from shapely.ops import unary_union  # noqa: E402

import outer_dressing  # noqa: E402
import outer_terrain  # noqa: E402
from prepare_waterfront import GROUND, polygons, triangulate  # noqa: E402

spec = importlib.util.spec_from_file_location("survey", ROOT / "docs/adventure/survey.py")
survey = importlib.util.module_from_spec(spec)
spec.loader.exec_module(survey)

# Straight shoreline tolerance for baking. The A0.3 coarse-LOD figure is 8 m; the river bake
# keeps mapped vertices, so stay well below hull scale here.
SIMPLIFY = 0.5
QUAY_BOTTOM = -2.0
ROUTE_CLEARANCE = survey.ROUTE_CLEARANCE
BLEND_M = 200.0  # Distance outside the river crop over which terrain rises from flat ground.
RIVER_GEOMETRY = ROOT / "assets/geometry/river/geometry.h"
# Colours of the river bake's own land top and quay walls, which the outer terrain replaces.
RIVER_GROUND_COLORS = {(135, 139, 144), (94, 102, 114)}


def river_overlay():
    """The river bake's triangles above its ground: buildings, roads, gardens, trees, bridge.

    Returns (plain, bridge), each a list of (three (x, y, z) points, colour). The river's water,
    flat land top and quay walls are dropped; the outer map has its own. The bridge keeps all of
    its triangles, which start at the header's river_bridge_first_triangle.
    """
    text = RIVER_GEOMETRY.read_text()
    vertices = [tuple(map(float, m)) for m in re.findall(
        r"\{ (-?[\d.]+)f, (-?[\d.]+)f, (-?[\d.]+)f \},?\n", text.split("river_triangles")[0])]
    triangles = [(tuple(map(int, m[:3])), tuple(map(int, m[3:]))) for m in re.findall(
        r"\{ \{ (\d+), (\d+), (\d+) \}, \{ (\d+), (\d+), (\d+), 255 \} \},", text)]
    bridge_first = int(re.search(r"river_bridge_first_triangle = (\d+)", text).group(1))
    plain, bridge = [], []
    for index, (ids, color) in enumerate(triangles):
        points = [vertices[i] for i in ids]
        if index >= bridge_first:
            bridge.append((points, color))
        elif any(y != 0 for _, y, _ in points) and color not in RIVER_GROUND_COLORS:
            plain.append((points, color))
    return plain, bridge


def flatten_in_crop(model):
    """Terrain model that is flat at GROUND in the river crop and blends to MODEL beyond it."""
    import numpy as np

    x0, z0, x1, z1 = survey.RIVER_CROP

    def blended(xy):
        xy = np.asarray(xy, dtype=float)
        dx = np.maximum(np.maximum(x0 - xy[:, 0], xy[:, 0] - x1), 0)
        dz = np.maximum(np.maximum(z0 - xy[:, 1], xy[:, 1] - z1), 0)
        t = np.clip(np.hypot(dx, dz) / BLEND_M, 0, 1)
        return GROUND + (model(xy) - GROUND) * (t * t * (3 - 2 * t))

    return blended


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


DROPPED = []  # (area, centroid) of land pieces no repair could triangulate


def triangulate_cell(part):
    """Clockwise-in-X/Z triangles of one land piece, tolerating zero-width spikes.

    Constrained triangulation rejects some clipped slivers. Retry on a 1 mm opening, then on a
    1 mm simplification; a piece that still fails is dropped and reported.
    """
    from shapely.errors import GEOSException

    attempts = (lambda p: p,
                lambda p: p.buffer(-.001, join_style=2).buffer(.001, join_style=2),
                lambda p: p.simplify(.001, preserve_topology=True))
    for attempt in attempts:
        try:
            triangles = [t for p in polygons(attempt(part)) for t in triangulate(p)]
        except (AssertionError, GEOSException):
            continue
        # Clipped slivers can yield zero-area triangles with a repeated vertex; drop them.
        return [pts for pts in (list(orient(t, sign=-1).exterior.coords)[:3] for t in triangles)
                if len({(round(x, 4), round(z, 4)) for x, z in pts}) == 3 and
                abs((pts[1][0] - pts[0][0]) * (pts[2][1] - pts[0][1])
                    - (pts[2][0] - pts[0][0]) * (pts[1][1] - pts[0][1])) > 1e-6]
    DROPPED.append((part.area, tuple(part.centroid.coords[0])))
    return []


def terrain_color(height, slope_deg):
    """Quay grey near the water, greener with height, bare rock on steep ground."""
    def mix(a, b, t):
        t = max(0.0, min(1.0, t))
        return tuple(round(x + (y - x) * t) for x, y in zip(a, b))

    ground = mix((135, 139, 144), (92, 116, 84), (height - 3) / 12)
    return mix(ground, (122, 119, 112), (slope_deg - 25) / 15)


def check_watertight(cells, land):
    """Every land edge is shared by two triangles unless it lies on the land boundary.

    A single-use edge elsewhere is a T-junction or hole, where the 20 m and 5 m cells disagree.
    """
    from collections import Counter

    from shapely import prepare
    from shapely.geometry import Point

    def key(p):
        return (round(p[0], 4), round(p[1], 4))

    edges = Counter()
    for tri in cells:
        for p, q in ((0, 1), (1, 2), (2, 0)):
            edges[frozenset((key(tri[p]), key(tri[q])))] += 1
    boundary = land.boundary.buffer(.02, quad_segs=2)
    prepare(boundary)
    bad = []
    for edge, count in edges.items():
        a, b = tuple(edge)
        if count == 1 and not boundary.contains(Point((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)):
            bad.append((a, b))
        assert count <= 2, f"edge used {count} times at {tuple(edge)}"
    assert not bad, f"{len(bad)} open interior edges, first {bad[:3]}"


def bake(water, region, model=None, overlay=None, dressing=()):
    """Return (vertices, triangles, stats) for water at y=0 then land and walls.

    Without MODEL the land is flat at GROUND; with one it is the 20 m/5 m terrain surface.
    OVERLAY is river_overlay()'s (plain, bridge): the plain triangles follow the land and the
    bridge ones follow, from the returned bridge_first index. DRESSING triangles, already on
    the terrain, come last: like the bridge they are authored geometry in flat colours, so they
    share its range, which the renderers keep out of facade texturing and windows.
    """
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
    stats = {"water_triangles": water_triangles, "land": land}
    wall_color = (94, 102, 114)
    if model is None:
        surface(land, GROUND, (135, 139, 144))
        walls(land, 0, GROUND, wall_color)
        walls(land, QUAY_BOTTOM, 0, wall_color)
        stats["bridge_first"] = len(triangles)
        return vertices, triangles, stats

    import numpy as np

    cells, shore_edges = outer_terrain.cell_pieces(land, region, triangulate_cell)
    points = {tuple(p) for tri in cells for p in tri}
    points |= {tuple(p) for edge in shore_edges for p in edge}
    ordered = sorted(points)
    height = dict(zip(ordered, model(np.array(ordered)).tolist()))
    for tri in cells:
        hs = [height[tuple(p)] for p in tri]
        (x0, z0), (x1, z1), (x2, z2) = tri
        # Upward normal of the clockwise-in-X/Z triangle, from its height gradient.
        ux, uz, uh = x1 - x0, z1 - z0, hs[1] - hs[0]
        vx, vz, vh = x2 - x0, z2 - z0, hs[2] - hs[0]
        ny = abs(ux * vz - uz * vx)
        nx, nz = uh * vz - uz * vh, ux * vh - uh * vx
        slope = math.degrees(math.atan2(math.hypot(nx, nz), ny)) if ny else 90.0
        triangle([(p[0], h, p[1]) for p, h in zip(tri, hs)],
                 terrain_color(sum(hs) / 3, slope))
    for p, q in shore_edges:
        (x, z), (xx, zz) = p, q
        h, hh = height[tuple(p)], height[tuple(q)]
        for bottom, hi, hj in ((0.0, h, hh), (QUAY_BOTTOM, 0.0, 0.0)):
            a, b = (x, bottom, z), (xx, bottom, zz)
            c, d = (xx, hj, zz), (x, hi, z)
            triangle([a, c, b], wall_color)
            triangle([a, d, c], wall_color)
    surface_area = sum(abs((x1 - x0) * (z2 - z0) - (x2 - x0) * (z1 - z0)) / 2
                       for (x0, z0), (x1, z1), (x2, z2) in cells)
    plain, bridge = overlay or ([], [])
    for points, color in plain:
        triangle(points, color)
    bridge_first = len(triangles)
    for points, color in bridge:
        triangle(points, color)
    dressing_first = len(triangles)
    for points, color in dressing:
        triangle(points, color)
    assert abs(surface_area - land.area) < 1.0 + sum(a for a, _ in DROPPED), \
        f"land surface {surface_area:.1f} m2 differs from land {land.area:.1f} m2"
    check_watertight(cells, land)
    assert sum(a for a, _ in DROPPED) < 1.0, f"untriangulated land {DROPPED[:5]}"
    stats.update(overlay_triangles=len(plain), bridge_first=bridge_first,
                 bridge_triangles=len(bridge), dressing_first=dressing_first,
                 dropped_pieces=len(DROPPED), dropped_area_m2=sum(a for a, _ in DROPPED),
                 land_triangles=len(cells), shore_edges=len(shore_edges),
                 height_min=min(height.values()), height_max=max(height.values()))
    return vertices, triangles, stats


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


def write(output, vertices, triangles, stats, region, water, route, terrain=None):
    output.mkdir(parents=True, exist_ok=True)
    lines = ["// Generated by tools/prepare_outer_map.py. Do not edit.",
             "// Map data (c) OpenStreetMap contributors, ODbL 1.0; see manifest.json.",
             "static const Vector3 outer_vertices[] = {"]
    lines += ["    { " + ", ".join(f"{v:.4f}f" for v in p) + " }," for p in vertices]
    lines += ["};", "static const Triangle3D outer_triangles[] = {"]
    lines += [f"    {{ {{ {i[0]}, {i[1]}, {i[2]} }}, {{ {c[0]}, {c[1]}, {c[2]}, 255 }} }},"
              for i, c in triangles]
    lines += ["};", f"enum {{ outer_bridge_first_triangle = {stats['bridge_first']} }};", ""]
    content = "\n".join(lines)
    (output / "geometry.h").write_text(content)
    land = stats["land"]
    terrain = terrain if isinstance(terrain, dict) else None
    boundary = water.boundary.intersection(region.boundary.buffer(0.01))
    manifest = {
        "generator": "tools/prepare_outer_map.py",
        "sources": ["docs/adventure/sources/osm-corridor.json.gz",
                    "docs/adventure/sources/land-polygons.json.gz"],
        "origin_epsg4326": list(survey.ORIGIN),
        "crs": "EPSG:3007 offsets from the crane origin; x = east, z = north",
        "license": "ODbL-1.0", "license_url": "https://opendatacommons.org/licenses/odbl/1-0/",
        "attribution": "Map data © OpenStreetMap contributors",
        "ground": ("Assumed flat ground at 3.5 m above water; terrain samples not used"
                   if terrain is None else "Baskarta contours and spot heights outside the "
                   "river crop; flat 3.5 m inside it, blending over terrain_blend_m"),
        "terrain": terrain,
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
        "contents": "water, land surface and quay walls; inside the river crop also the river "
                    "bake's buildings, roads, gardens, trees and bridge (A3.3); outside it "
                    "buildings, container cranes, port masts, channel marks and trees (A3.5a)",
        "dressing": stats.get("dressing") and {
            "first_triangle": stats["dressing_first"], **stats["dressing"]},
        "river_overlay": {"source": "assets/geometry/river/geometry.h",
                          "triangles": stats.get("overlay_triangles"),
                          "bridge_triangles": stats.get("bridge_triangles"),
                          "ground_m": GROUND, "terrain_blend_m": BLEND_M},
        "land_triangles": stats.get("land_triangles"),
        "geometry_sha256": hashlib.sha256(content.encode()).hexdigest(),
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Baked {len(vertices)} vertices, {len(triangles)} triangles "
          f"({stats['water_triangles']} water), {boundary.length:.0f} m playable boundary")


def self_test():
    outer_dressing.self_test()
    # Water stays below a raised shelf; land is the region less water; an island hole survives.
    region = box(0, 0, 100, 100)
    water = box(10, 10, 90, 90).difference(box(40, 40, 60, 60))
    vertices, triangles, stats = bake(water, region)
    assert stats["water_triangles"] == len(triangulate(water))
    assert all(vertices[v][1] == 0 for ids, _ in triangles[:stats["water_triangles"]]
               for v in ids)
    assert abs(stats["land"].area - (10000 - water.area)) < 1e-6
    # A tilted plane: the 20 m/5 m surface covers the land exactly, with no height cracks.
    class Plane:
        def __call__(self, xy):
            xy = __import__("numpy").asarray(xy, dtype=float)
            return xy[:, 0] * .1 + xy[:, 1] * .05 + 2

    big = box(0, 0, 300, 300)
    lake = box(-10, -10, 310, 100).union(box(150, 100, 190, 140))
    vertices, triangles, stats = bake(lake, big, Plane())
    first = stats["water_triangles"]
    tops = triangles[first:first + stats["land_triangles"]]
    area = 0.0
    for ids, _ in tops:
        (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = (vertices[i] for i in ids)
        cross = (x1 - x0) * (z2 - z0) - (x2 - x0) * (z1 - z0)
        assert cross < 0, "downward-facing land"
        area += -cross / 2
        for x, y, z in ((x0, y0, z0), (x1, y1, z1), (x2, y2, z2)):
            assert abs(y - (x * .1 + z * .05 + 2)) < 1e-3, "height off the plane"
    assert abs(area - stats["land"].area) < 1e-3, (area, stats["land"].area)
    assert stats["land_triangles"] > 2 * len(triangulate(stats["land"])), "no refinement"
    # Region edge through open water is a playable boundary, not shoreline.
    cut = region.intersection(box(-50, 20, 150, 80))
    assert abs(cut.boundary.intersection(region.boundary.buffer(.01)).length - 120) < .1


def geo_land(region, water):
    return clean(region.difference(water))


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
    crop = box(*survey.RIVER_CROP)
    water, carved = outer_dressing.carve_marks(
        water, outer_dressing.marks(osm, frame, region, crop))
    route = check(geo, region, water)
    import numpy as np

    data, contours, spots = outer_terrain.load_samples()
    model = outer_terrain.HeightModel(np.vstack([contours, spots]))
    terrain = {
        "source": data["source"], "licence": data["licence"],
        "source_file": "docs/adventure/sources/terrain-baskarta.json.gz",
        "source_sha256": hashlib.sha256(outer_terrain.SOURCE.read_bytes()).hexdigest(),
        "datum": "Not confirmed from the product description; assumed RH 2000 (RH 70 differs "
                 "by well under half a metre in Gothenburg). Shore heights sit near 0 m.",
        "cell_inland_m": outer_terrain.COARSE, "cell_shore_m": outer_terrain.FINE,
        "shore_band_m": outer_terrain.BAND, "min_land_m": outer_terrain.MIN_LAND,
        "interpolation": "linear over a Delaunay triangulation of contour vertices and spot "
                         "heights; nearest sample outside its hull",
        "holdout_vs_spot_heights": outer_terrain.holdout_error(
            contours, spots, geo_land(region, water)),
    }
    overlay = river_overlay()
    blended = flatten_in_crop(model)
    fort = next(Point(m["east_north"]) for m in survey.features(osm, frame)[1]
                if m["label"] == "Nya Älvsborg")
    dressing, dressed = outer_dressing.dress(osm, frame, region, crop, water,
                                             geo_land(region, water), blended, fort,
                                             survey.multipolygon)
    dressed.update(marks_in_water=carved, assumptions=outer_dressing.ASSUMPTIONS)
    vertices, triangles, stats = bake(water, region, blended, overlay, dressing)
    stats["dressing"] = dressed
    terrain.update(dropped_pieces=stats["dropped_pieces"],
                   dropped_area_m2=round(stats["dropped_area_m2"], 4),
                   height_min_m=round(stats["height_min"], 2),
                   height_max_m=round(stats["height_max"], 2))
    write(args.output or ROOT / "assets/geometry/outer", vertices, triangles, stats, region,
          water, route, terrain)


if __name__ == "__main__":
    main()
