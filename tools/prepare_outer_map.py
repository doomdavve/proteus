#!/usr/bin/env python3
"""Bake the first outer-river slice (river crop plus the Långedrag run) from the A0 sources.

Requires the pins in docs/adventure/requirements.txt. Run from anywhere:

    python -I tools/prepare_outer_map.py --self-test     # fixtures only
    python -I tools/prepare_outer_map.py                 # writes assets/geometry/outer/

Reads only the checked-in A0.2 sources and never fetches. Normal C builds use the checked-in
output. Outside the river crop the map holds water, Höjdmodell terrain and quay walls, and since
A3.5a the corridor's buildings, the container cranes and port masts, channel marks and a
scattered tree cover (tools/outer_dressing.py); no roads. Inside the crop (A3.3) the river
bake's buildings, roads, gardens, trees and bridge are imported from
assets/geometry/river/scenery.bin. The terrain covers the crop too: roads and garden beds are
painted onto it and the other solids are moved onto it (drape_objects); the bridge is kept as
it is. Water is the A0.3 assembly (land polygons plus OSM inland water, bridge legs removed).
Where the region cuts open water, the water ends at an edge that collision treats as shore: a
playable boundary, not a coastline. The manifest reports its length. The bake is written as a
scenery pack plus the compiled water and kit ground (tools/map_pack.py).
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

import map_pack  # noqa: E402
import outer_dressing  # noqa: E402
import outer_landmarks  # noqa: E402
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
RIVER_GEOMETRY = ROOT / "assets/geometry/river/scenery.bin"
# Colours of the river bake's own land top and quay walls, which the outer terrain replaces.
RIVER_GROUND_COLORS = {(135, 139, 144), (94, 102, 114)}
# River ground paint: colour and its height above the river's GROUND (roads, garden beds).
PAINT = {(83, 89, 99): .03, (72, 105, 79): .08}
PAINT_GRID = .01  # metres
TINY_EDGE = .001  # metres; open land edges this short are tolerated (check_watertight)
# The river kit (x0, z0, x1, z1: the river crop): the runtime sets its crane, bridge deck, cafe,
# street furniture, frontages, quay fittings, cars, lamps and harbour on the land surface
# sampled here every GROUND_STEP metres.
GROUND_AREA = survey.RIVER_CROP
GROUND_STEP = 4.0
# A3.5f: Göta älv upstream of the river crop, past Lindholmen, Stenpiren, Lilla Bommen and
# Hisingsbron (x0, z0, x1, z1). The Höjdmodell window (prepare_terrain_dem.DEM_WINDOW) and the
# OSM envelope (prepare_adventure_sources.ENVELOPE) cover it with a margin.
UPSTREAM = (950.0, -300.0, 4200.0, 2300.0)
# Dock to the river's exit past Hisingsbron, through the middle of the channel (east/north m).
UPSTREAM_ROUTE = survey.RIVER[:3] + [(400, 40), (900, 380), (1500, 560), (2000, 750),
                                     (2500, 1000), (2800, 1250), (3200, 1650), (3450, 1950),
                                     (3700, 2100), (3950, 2230)]


def river_overlay():
    """The river bake's triangles above its ground: buildings, roads, gardens, trees, bridge.

    Returns (plain, bridge), each a list of (three (x, y, z) points, colour). The river's water,
    flat land top and quay walls are dropped; the outer map has its own. The bridge keeps all of
    its triangles, which start at the pack's bridge first triangle. Coordinates are the pack's
    float32 values (before A3.5i, the header's 0.1 mm decimals).
    """
    vertices, triangles, _, bridge_first = map_pack.read_pack(RIVER_GEOMETRY)
    plain, bridge = [], []
    for index, (ids, color) in enumerate(triangles):
        points = [vertices[i] for i in ids]
        if index >= bridge_first:
            bridge.append((points, color))
        elif any(y != 0 for _, y, _ in points) and color not in RIVER_GROUND_COLORS:
            plain.append((points, color))
    return plain, bridge


def split_overlay(plain):
    """Separate the river's ground paint from its solid objects.

    Roads and garden beds are flat triangles just above the river's GROUND. They become paint:
    the terrain surface takes their colour where they lie, so they follow the ground with no
    extra layer. Everything else (buildings, planting islands, trees) stays solid and is moved
    onto the terrain by drape_objects. Returns (objects, paint), paint being
    [(geometry in X/Z, colour)].
    """
    from shapely.geometry import Polygon

    pieces = {color: [] for color in PAINT}
    objects = []
    for points, color in plain:
        if color in PAINT and all(abs(y - GROUND - PAINT[color]) < 1e-3 for _, y, _ in points):
            pieces[color].append(Polygon([(x, z) for x, _, z in points]))
        else:
            objects.append((points, color))
    return objects, [(p, color) for color, p in pieces.items()]


def paint_layers(land, paint):
    """Partition LAND by PAINT (split_overlay's) into [(geometry, colour or None)].

    The split is made once for the whole map, so the terrain cells cut every layer at the same
    points and the layers share their vertices. A PAINT_GRID snap closes the hairline gaps
    between the river's decal triangles.
    """
    from shapely import set_precision

    layers, taken = [], None
    for shapes, color in paint:
        if not shapes:
            continue
        # Snapping attaches the grid to the geometry and to everything made from it, and the
        # constrained triangulation then places triangles outside its polygon; return to
        # floating precision.
        cover = set_precision(set_precision(unary_union(shapes), PAINT_GRID), 0)
        cover = unary_union(polygons(cover.intersection(land)))
        if taken is not None:
            cover = unary_union(polygons(cover.difference(taken)))
        taken = cover if taken is None else taken.union(cover)
        layers.append((cover, color))
    rest = land if taken is None else unary_union(polygons(land.difference(taken)))
    return [(rest, None)] + layers


def drape_objects(objects, model):
    """Move each solid of the river overlay by the terrain under it, less the river's GROUND.

    Solids are the groups of triangles that share a corner in X/Z. One standing on the
    ground goes to the lowest terrain under its footprint (corners and a 5 m lattice inside),
    so no edge floats and the uphill side is sunk instead. A raised building part moves with
    the solid whose footprint holds its centre; a floating one without such a solid uses its
    own footprint. Returns (moved triangles, per-solid lifts in metres).
    """
    import numpy as np
    from shapely import STRtree, contains_xy, get_coordinates
    from shapely.geometry import MultiPoint

    parent = {}

    def find(k):
        while parent[k] != k:
            parent[k] = parent[parent[k]]
            k = parent[k]
        return k

    def key(x, z):
        return (round(x, 3), round(z, 3))

    for points, _ in objects:
        ks = [key(x, z) for x, _, z in points]
        for k in ks:
            parent.setdefault(k, k)
        for k in ks[1:]:
            a, b = find(ks[0]), find(k)
            if a != b:
                parent[b] = a
    groups = {}
    for index, (points, _) in enumerate(objects):
        groups.setdefault(find(key(points[0][0], points[0][2])), []).append(index)

    def footprint(indices):
        pts = [p for i in indices for p in objects[i][0]]
        base = min(y for _, y, _ in pts)
        return base, MultiPoint([(x, z) for x, y, z in pts if y < base + .01]).convex_hull

    def lowest(shape):
        coords = [tuple(c) for c in get_coordinates(shape)]
        x0, z0, x1, z1 = shape.bounds
        lattice = np.array([(x, z) for x in np.arange(math.ceil(x0 / 5) * 5, x1, 5.0)
                            for z in np.arange(math.ceil(z0 / 5) * 5, z1, 5.0)])
        if len(lattice) and shape.area > 0:
            coords += [tuple(p) for p in lattice[contains_xy(shape, *lattice.T)]]
        return float(model(np.array(coords)).min())

    lift, standing, raised = {}, [], []
    for root, indices in sorted(groups.items()):
        base, shape = footprint(indices)
        if base < GROUND + 1:
            lift[root] = lowest(shape) - GROUND
            standing.append((root, shape))
        else:
            raised.append((root, shape))
    tree = STRtree([shape for _, shape in standing])
    for root, shape in raised:
        holders = sorted(tree.query(shape.centroid, predicate="within"))
        lift[root] = lift[standing[holders[0]][0]] if holders else lowest(shape) - GROUND
    # The original order is kept: the renderers pair consecutive facade triangles.
    moved = [([(x, y + lift[find(key(points[0][0], points[0][2]))], z) for x, y, z in points],
              color) for points, color in objects]
    return moved, lift


def region_shape():
    """The river crop joined to the first slice and the river upstream (crane east/north m)."""
    crop = box(*survey.RIVER_CROP)
    return unary_union([crop, Polygon(survey.FIRST_SLICE), box(*UPSTREAM)])


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
    Edges shorter than TINY_EDGE are allowed: where a paint boundary passes within a millimetre
    of a cell corner, the sliver between them is dropped as a zero-area triangle. Returns their
    count.
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
    bad, tiny = [], 0
    for edge, count in edges.items():
        a, b = tuple(edge)
        if count == 1 and not boundary.contains(Point((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)):
            if math.dist(a, b) < TINY_EDGE:
                tiny += 1
            else:
                bad.append((a, b))
        assert count <= 2, f"edge used {count} times at {tuple(edge)}"
    assert not bad, f"{len(bad)} open interior edges, first {bad[:3]}"
    return tiny


def ground_grid(cells, height, model, area=GROUND_AREA, step=GROUND_STEP):
    """The baked land surface on a lattice over AREA, in centimetres, rows south to north.

    Points on land take the height of the land triangle under them; points in water take the
    terrain model's. Returns {"origin": (x0, z0), "step", "columns", "rows", "cm"}.
    """
    import numpy as np
    from shapely import STRtree, points
    from shapely.geometry import Polygon

    x0, z0, x1, z1 = area
    columns, rows = int((x1 - x0) / step) + 1, int((z1 - z0) / step) + 1
    xs, zs = np.meshgrid(x0 + step * np.arange(columns), z0 + step * np.arange(rows))
    xy = np.c_[xs.ravel(), zs.ravel()]
    values = model(xy)
    tree = STRtree([Polygon(tri) for tri in cells])
    point_index, cell_index = tree.query(points(xy), predicate="intersects")
    for p, c in zip(point_index, cell_index):
        (ax, az), (bx, bz), (cx, cz) = cells[c]
        ha, hb, hc = (height[tuple(q)] for q in cells[c])
        det = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
        u = ((bz - cz) * (xy[p, 0] - cx) + (cx - bx) * (xy[p, 1] - cz)) / det
        v = ((cz - az) * (xy[p, 0] - cx) + (ax - cx) * (xy[p, 1] - cz)) / det
        values[p] = u * ha + v * hb + (1 - u - v) * hc
    return {"origin": (x0, z0), "step": step, "columns": columns, "rows": rows,
            "cm": [int(round(h * 100)) for h in values]}


def bake(water, region, model=None, overlay=None, dressing=()):
    """Return (vertices, triangles, stats) for water at y=0 then land and walls.

    Without MODEL the land is flat at GROUND; with one it is the 20 m/5 m terrain surface.
    OVERLAY is (plain, bridge, paint): the plain triangles (river_overlay's solids, moved by
    drape_objects) follow the land and the bridge ones follow, from the returned bridge_first
    index; PAINT (split_overlay's) colours the land top. DRESSING triangles, already on
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

    plain, bridge, paint = overlay or ([], [], [])
    cells, shore_edges, tags = outer_terrain.cell_pieces(
        land, region, triangulate_cell, paint_layers(land, paint))
    painted = list(zip(cells, tags))
    points = {tuple(p) for tri in cells for p in tri}
    points |= {tuple(p) for edge in shore_edges for p in edge}
    ordered = sorted(points)
    height = dict(zip(ordered, model(np.array(ordered)).tolist()))
    for tri, paint_color in painted:
        hs = [height[tuple(p)] for p in tri]
        (x0, z0), (x1, z1), (x2, z2) = tri
        # Upward normal of the clockwise-in-X/Z triangle, from its height gradient.
        ux, uz, uh = x1 - x0, z1 - z0, hs[1] - hs[0]
        vx, vz, vh = x2 - x0, z2 - z0, hs[2] - hs[0]
        ny = abs(ux * vz - uz * vx)
        nx, nz = uh * vz - uz * vh, ux * vh - uh * vx
        slope = math.degrees(math.atan2(math.hypot(nx, nz), ny)) if ny else 90.0
        triangle([(p[0], h, p[1]) for p, h in zip(tri, hs)],
                 paint_color or terrain_color(sum(hs) / 3, slope))
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
    tiny_edges = check_watertight(cells, land)
    assert sum(a for a, _ in DROPPED) < 1.0, f"untriangulated land {DROPPED[:5]}"
    stats.update(overlay_triangles=len(plain), bridge_first=bridge_first,
                 bridge_triangles=len(bridge), dressing_first=dressing_first,
                 dropped_pieces=len(DROPPED), dropped_area_m2=sum(a for a, _ in DROPPED),
                 land_triangles=len(cells), shore_edges=len(shore_edges),
                 painted_triangles=sum(1 for _, c in painted if c), tiny_open_edges=tiny_edges,
                 height_min=min(height.values()), height_max=max(height.values()),
                 ground=ground_grid(cells, height, model))
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
    assert corridor.covers(LineString(UPSTREAM_ROUTE)), "upstream route leaves the water envelope"
    assert connected.contains(Point(survey.DETOUR[-1])), "baked water does not reach the shelter"
    assert region.buffer(1e-6).covers(water)
    return route


def write(output, vertices, triangles, stats, region, water, route, terrain=None):
    ground = stats["ground"]
    lines = ["// The land surface over the river kit's streets and harbour, in centimetres, rows",
             "// from south to north (tools/prepare_outer_map.py ground_grid).",
             f"enum {{ outer_ground_columns = {ground['columns']}, "
             f"outer_ground_rows = {ground['rows']} }};",
             f"static const float outer_ground_x0 = {ground['origin'][0]:.1f}f, "
             f"outer_ground_z0 = {ground['origin'][1]:.1f}f, "
             f"outer_ground_step = {ground['step']:.1f}f;",
             "static const short outer_ground_cm[] = {"]
    cm = ground["cm"]
    lines += ["    " + ", ".join(str(v) for v in cm[i:i + 16]) + ","
              for i in range(0, len(cm), 16)]
    lines += ["};", ""]
    pack = map_pack.write(output, "outer", "tools/prepare_outer_map.py", vertices, triangles,
                          stats["bridge_first"], "\n".join(lines))
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
                   if terrain is None else "Höjdmodell 2022 heights everywhere, the river "
                   "crop included"),
        "terrain": terrain,
        "quay_bottom": QUAY_BOTTOM,
        "region": {"river_crop": list(survey.RIVER_CROP), "first_slice": survey.FIRST_SLICE,
                   "upstream": list(UPSTREAM),
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
                    "buildings (A3.5a; since A3.5f before the authored range, with windows), container "
                    "cranes, port masts, channel marks and trees (A3.5a)",
        "dressing": stats.get("dressing") and {
            "first_triangle": stats["dressing_first"], **stats["dressing"]},
        "river_overlay": {"source": "assets/geometry/river/scenery.bin",
                          "triangles": stats.get("overlay_triangles"),
                          "bridge_triangles": stats.get("bridge_triangles"),
                          "river_ground_m": GROUND,
                          "placement": "roads and garden beds paint the terrain; each other "
                                       "solid moves by the lowest terrain under it less "
                                       "river_ground_m; the bridge is unchanged",
                          **(stats.get("draped") or {})},
        "land_triangles": stats.get("land_triangles"),
        "kit_ground": stats.get("ground") and {
            "area": list(GROUND_AREA), "step_m": GROUND_STEP,
            "columns": stats["ground"]["columns"], "rows": stats["ground"]["rows"],
            "use": "waterfront_outer_ground: the river kit (crane, bridge deck, cafe, "
                   "street furniture, frontages, quay fittings, cars, lamps and harbour) stands "
                   "on the baked land surface"},
        "scenery_sha256": hashlib.sha256(pack).hexdigest(),
        "water_sha256": hashlib.sha256((output / "water.h").read_bytes()).hexdigest(),
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Baked {len(vertices)} vertices, {len(triangles)} triangles "
          f"({stats['water_triangles']} water), {boundary.length:.0f} m playable boundary")


def self_test():
    outer_dressing.self_test()
    outer_landmarks.self_test()
    outer_terrain.self_test()
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
    # The kit's ground lattice follows the land surface (and the model over water).
    ground = stats["ground"]
    for j in range(0, ground["rows"], 7):
        for i in range(0, ground["columns"], 7):
            x, z = ground["origin"][0] + i * GROUND_STEP, ground["origin"][1] + j * GROUND_STEP
            cm = ground["cm"][j * ground["columns"] + i]
            assert abs(cm - (x * .1 + z * .05 + 2) * 100) <= 1, (x, z, cm)
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
    header, heights = outer_terrain.load_grid()
    model = outer_terrain.GridModel(header, heights)
    # A3.5g: Karlatornet, Lilla Bommen and Hisingsbron; the bridge's piers and lift towers
    # stand in the river, so they leave the water before the route checks.
    landmarks, obstacles, landmark_stats = outer_landmarks.landmarks(
        osm, frame, survey.multipolygon, water, model)
    water = water.difference(obstacles)
    route = check(geo, region, water)
    terrain = {
        "source": header["source"], "licence": header["licence"], "survey": header["survey"],
        "source_file": "docs/adventure/sources/terrain-hojdmodell.bin.gz",
        "source_sha256": hashlib.sha256(outer_terrain.SOURCE.read_bytes()).hexdigest(),
        "datum": "RH 2000 (product description). Heights are used as metres above the water "
                 "at y = 0; the scanned water surface sits near -0.26 m.",
        "grid_cell_m": header["cell_m"],
        "cell_inland_m": outer_terrain.COARSE, "cell_shore_m": outer_terrain.FINE,
        "shore_band_m": outer_terrain.BAND, "min_land_m": outer_terrain.MIN_LAND,
        "interpolation": "bilinear between the centres of the grid's cell means",
        "check_vs_baskarta_spot_heights": outer_terrain.spot_check(
            model, outer_terrain.load_spots(), geo_land(region, water)),
    }
    plain, bridge = river_overlay()
    objects, paint = split_overlay(plain)
    objects, lifts = drape_objects(objects, model)
    fort = next(Point(m["east_north"]) for m in survey.features(osm, frame)[1]
                if m["label"] == "Nya Älvsborg")
    buildings, dressing, dressed = outer_dressing.dress(osm, frame, region, crop, water,
                                             geo_land(region, water), model, fort,
                                             survey.multipolygon, outer_landmarks.SKIP)
    dressed.update(marks_in_water=carved, assumptions=outer_dressing.ASSUMPTIONS,
                   landmarks=landmark_stats)
    dressing = dressing + landmarks
    vertices, triangles, stats = bake(water, region, model, (objects + buildings, bridge, paint),
                                      dressing)
    stats["dressing"] = dressed
    values = sorted(lifts.values())
    stats["draped"] = {"solids": len(values), "paint_source_triangles": sum(len(p) for p, _ in paint),
                       "painted_land_triangles": stats["painted_triangles"],
                       "lift_min_m": round(values[0], 2), "lift_max_m": round(values[-1], 2),
                       "lift_median_m": round(values[len(values) // 2], 2)}
    terrain.update(tiny_open_edges=stats["tiny_open_edges"], dropped_pieces=stats["dropped_pieces"],
                   dropped_area_m2=round(stats["dropped_area_m2"], 4),
                   height_min_m=round(stats["height_min"], 2),
                   height_max_m=round(stats["height_max"], 2))
    write(args.output or ROOT / "assets/geometry/outer", vertices, triangles, stats, region,
          water, route, terrain)


if __name__ == "__main__":
    main()
