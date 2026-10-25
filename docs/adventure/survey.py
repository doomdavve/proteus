"""Survey the Gothenburg–Vinga coverage, route, first slice and tile costs offline.

Requires the pins in docs/adventure/requirements.txt. Run from the repository root:

    python -I docs/adventure/survey.py --self-test   # synthetic topology fixtures only
    python -I docs/adventure/survey.py               # writes the survey artifacts here

Reads only the checked-in A0.2 sources and the compiled river manifest; never fetches.
Outputs are planning evidence, not runtime geometry or a navigation chart.
"""
import argparse
import gzip
import hashlib
import json
import math
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[1]
MANIFEST = BASE / "sources.json"
RIVER_MANIFEST = ROOT / "assets/geometry/river/manifest.json"
ORIGIN = (11.9089495, 57.6973921)
# Survey frame: metres east/north of the crane in EPSG:3007. Simulation X points west.
RIVER_CROP = (-1500.0, -1600.0, 950.0, 800.0)
DOCK = (12.0, 60.0)
ENVELOPE_MARGIN = 250.0  # route/slice keep-out from the source box, metres
ROUTE_CLEARANCE = 8.0    # conservative survey envelope, as in docs/river/survey.py
TILE_SIZES = (512, 1024)
SPEEDS = (10, 15, 20)

URBAN_BUILDINGS = 25     # building outlines whose centroid lies in a tile
# Explicit cost model. Arrays match Vector3 (12 bytes) and Triangle3D (16 bytes) in the
# compiled river; these are estimates, never measured bake sizes.
VERTEX_BYTES, TRIANGLE_BYTES = 12, 16
TILE_RECORD_BYTES = 64   # id, bounds, class, counts and offsets per tile
LANDMARK_RECORD_BYTES = 24
COARSE_TOLERANCE = 8.0   # coarse-LOD shoreline simplification, metres

# Hand-placed waypoints in the survey frame, checked below against the assembled water.
# They were found by a coarse offline shortest-path pass with generous clearance, then
# rounded. They follow open mapped water only; no chart, depth or overhead data were used.
RIVER = [DOCK, (24, -70), (15, -180), (-250, -500), (-650, -850), (-1100, -960)]
BRANCH = (-3300, -1550)    # on the main line, south of the Arendal quays
ROUTE = RIVER + [BRANCH, (-7440, -2660), (-8340, -3220), (-11480, -4820), (-17500, -7240),
                 (-17760, -7225), (-17830, -7262)]
# Kept west of the awash rocks off the Långedrag shore, then into the old harbour basin.
DETOUR = [BRANCH, (-3850, -2400), (-3850, -3150), (-3700, -3310), (-3620, -3380)]
SHORT_START = [(-3820, -3920), (-3880, -3960), (-8340, -3220)]
SLICE_ROUTE = RIVER + DETOUR
# The first slice joins the river crop along its west/south edges (200 m overlap) and adds
# Älvsborgsfjorden, Nya Älvsborg, the Långedrag shore and the open water beyond as view.
FIRST_SLICE = [(-1300, -300), (-3000, -300), (-4800, -700), (-5300, -1600), (-5300, -3000),
               (-4300, -3900), (-3300, -3900), (-2600, -3000), (-1300, -1800)]
# (label, OSM element, role); positions come from the pinned source, not from this table.
LANDMARKS = [
    ("Eriksberg crane origin", None, "origin"),
    ("Älvsborgsbron", ("w", 387711139), "bridge"),
    ("Nya Varvet", ("n", 2439395466), "shelter"),
    ("Nya Älvsborg", ("n", 1770252341), "landmark"),
    ("Långedrag old harbour", ("n", 4946285197), "shelter"),
    ("Saltholmen", ("n", 313760549), "start"),
    ("Rivö fyr", ("n", 1149696893), "landmark"),
    ("Vinga fyr", ("w", 426743093), "landmark"),
    ("Vinga gästhamn", ("n", 1484095980), "shelter"),
    ("Hönö Klåva", ("n", 1673918294), "shelter"),
]
HAZARD_TAGS = {"seamark:type": {"rock", "obstruction", "wreck"},
               "natural": {"rock", "reef", "shoal", "stone"}}


def read_source(name, manifest):
    raw = (BASE / "sources" / name).read_bytes()
    if name.endswith(".gz"):
        raw = gzip.decompress(raw)
    expected = manifest["derived"]["outputs"][name]["content_sha256"]
    if hashlib.sha256(raw).hexdigest() != expected:
        sys.exit(f"{name}: content differs from sources.json; rerun the A0.2 preparation")
    return json.loads(raw), expected


class Frame:
    """EPSG:4326 lon/lat to metres east/north of the crane origin."""

    def __init__(self):
        from pyproj import Transformer

        self.forward = Transformer.from_crs(4326, 3007, always_xy=True)
        self.inverse = Transformer.from_crs(3007, 4326, always_xy=True)
        self.ox, self.oy = self.forward.transform(*ORIGIN)

    def xy(self, coords):
        xs, ys = self.forward.transform([c[0] for c in coords], [c[1] for c in coords])
        return [(x - self.ox, y - self.oy) for x, y in zip(xs, ys)]

    def array(self, coords):
        import numpy

        x, y = self.forward.transform(coords[:, 0], coords[:, 1])
        return numpy.column_stack([numpy.asarray(x) - self.ox, numpy.asarray(y) - self.oy])

    def lonlat(self, point):
        return self.inverse.transform(point[0] + self.ox, point[1] + self.oy)


# --- Topology assembly --------------------------------------------------------------------

def join_rings(chains):
    """Join member ways into closed rings; return (rings, open chains). Never closes gaps."""
    pending = [list(map(tuple, c)) for c in chains if len(c) >= 2]
    rings = [c for c in pending if c[0] == c[-1]]
    pending = [c for c in pending if c[0] != c[-1]]
    # Join on shared end nodes only. Unmatched ends are reported, not bridged.
    ends = {}
    for i, c in enumerate(pending):
        ends.setdefault(c[0], []).append(i)
        ends.setdefault(c[-1], []).append(i)
    used = set()
    opened = []
    for start in range(len(pending)):
        if start in used:
            continue
        used.add(start)
        chain = pending[start]
        grown = True
        while chain[0] != chain[-1] and grown:
            grown = False
            for i in ends.get(chain[-1], []):
                if i not in used:
                    other = pending[i]
                    chain = chain + (other[1:] if other[0] == chain[-1] else other[-2::-1])
                    used.add(i)
                    grown = True
                    break
        if chain[0] == chain[-1] and len(chain) >= 4:
            rings.append(chain)
        else:
            opened.append(chain)
    return rings, opened


def multipolygon(members, ways):
    """Assemble an OSM multipolygon from member ways; return (geometry, problems)."""
    from shapely import make_valid
    from shapely.geometry import Polygon
    from shapely.ops import unary_union

    problems = []
    rings = {"outer": [], "inner": []}
    for m in members:
        if m["type"] != "w":
            continue
        way = ways.get(m["ref"])
        if way is None:
            problems.append(f"missing way {m['ref']}")
            continue
        rings["inner" if m["role"] == "inner" else "outer"].append(way)
    outer, outer_open = join_rings(rings["outer"])
    inner, inner_open = join_rings(rings["inner"])
    problems += [f"open {role} chain of {len(c)} nodes" for role, chains
                 in (("outer", outer_open), ("inner", inner_open)) for c in chains]
    shells = unary_union([make_valid(Polygon(r)) for r in outer])
    holes = unary_union([make_valid(Polygon(r)) for r in inner])
    return make_valid(shells.difference(holes)), problems


def polygons_of(geometry):
    if geometry.is_empty:
        return []
    if geometry.geom_type == "Polygon":
        return [geometry]
    return [g for g in getattr(geometry, "geoms", []) for g in polygons_of(g)]


def is_water(tags):
    return (tags.get("natural") == "water" or tags.get("waterway") in {"riverbank", "dock"}
            or tags.get("landuse") == "basin")


def assemble(osm, land, frame):
    """Sea, inland water, bridge supports and land, assembled before any clipping.

    Topology is built in lon/lat, where the source box is exact, then projected vertex by
    vertex. Box edges are classified as source boundaries before projection.
    """
    from shapely import make_valid, transform
    from shapely.geometry import Polygon, box
    from shapely.ops import unary_union

    envelope = box(*osm["envelope_wsen"])
    ways = {w["id"]: [tuple(p) for p in w["geometry"]] for w in osm["ways"]}
    land_area = make_valid(unary_union([Polygon(rings[0], rings[1:])
                                        for rings in land["polygons"]]))
    sea = make_valid(envelope.difference(land_area))

    inland, unresolved = [], []
    for r in osm["relations"]:
        if r["tags"].get("type") != "multipolygon" or not is_water(r["tags"]):
            continue
        shape, problems = multipolygon(r["members"], ways)
        if problems:
            unresolved.append({"relation": r["id"], "name": r["tags"].get("name"),
                               "problems": sorted(set(problems))})
        if not shape.is_empty and shape.intersects(envelope):
            inland.append(shape)
    for w in osm["ways"]:
        g = ways[w["id"]]
        if is_water(w["tags"]) and len(g) >= 4 and g[0] == g[-1]:
            shape = make_valid(Polygon(g))
            if shape.intersects(envelope):
                inland.append(shape)
    supports = [make_valid(Polygon(ways[w["id"]])) for w in osm["ways"]
                if "bridge:support" in w["tags"] and len(w["geometry"]) >= 4
                and w["geometry"][0] == w["geometry"][-1]]
    inland_water = make_valid(unary_union(inland)).intersection(envelope)
    water = make_valid(unary_union([sea, inland_water]))
    legs = make_valid(unary_union(supports)).intersection(water)
    water = make_valid(water.difference(legs))
    edge = envelope.boundary.buffer(1e-9)
    shoreline = water.boundary.difference(edge)
    source_edge = water.boundary.intersection(edge)

    def project(geometry):
        return transform(geometry, lambda c: frame.array(c))

    return {
        "envelope": project(envelope), "water": project(water),
        "land": project(make_valid(envelope.difference(water))),
        "sea": project(sea), "inland_water": project(inland_water),
        "supports": project(legs), "shoreline": project(shoreline),
        "source_edge": project(source_edge), "unresolved": unresolved,
        "support_count": len(polygons_of(legs)),
    }


# --- Tiles and cost estimates ------------------------------------------------------------

def tile_id(east, north, size):
    """Grid anchored at the crane origin; floor keeps negative coordinates contiguous."""
    return math.floor(east / size), math.floor(north / size)


def tile_box(tile, size):
    from shapely.geometry import box

    return box(tile[0] * size, tile[1] * size, (tile[0] + 1) * size, (tile[1] + 1) * size)


def tiles_covering(region, size):
    from shapely import prepare

    prepare(region)
    west, south, east, north = region.bounds
    (x0, y0), (x1, y1) = tile_id(west, south, size), tile_id(east, north, size)
    return [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)
            if region.intersects(tile_box((x, y), size))]


def edge_count(lines):
    return sum(len(g.coords) - 1 for g in getattr(lines, "geoms", [lines]) if not g.is_empty)


def tile_shoreline(shoreline, tile):
    """Mapped shoreline inside one tile. Tile edges never contribute, unlike water.boundary."""
    return shoreline.intersection(tile)


def building_cost(vertices):
    """Extruded footprint: walls 2n triangles, fan roof n-2, 2n unshared vertices."""
    n = max(vertices, 3)
    return 2 * n * VERTEX_BYTES + (3 * n - 2) * TRIANGLE_BYTES


def shoreline_cost(edges):
    """Quay wall (2 triangles) plus water and land fill (about 1 triangle each) per edge."""
    return edges * (2 * VERTEX_BYTES + 4 * TRIANGLE_BYTES)


BOX_COST = 8 * VERTEX_BYTES + 10 * TRIANGLE_BYTES


def tile_record(tile, size, geo, buildings, landmarks):
    cell = tile_box(tile, size)
    water = geo["water"].intersection(cell)
    land = geo["land"].intersection(cell)
    covered = geo["envelope"].intersection(cell).area
    shore = tile_shoreline(geo["shoreline"], cell)
    coarse = shore.simplify(COARSE_TOLERANCE, preserve_topology=False)
    inside = [b for b in buildings["tree"].query(cell)
              if cell.contains(buildings["centroids"][b])]
    edges, coarse_edges = edge_count(shore), edge_count(coarse)
    marks = sum(1 for p in landmarks if cell.contains(p))
    if len(inside) >= URBAN_BUILDINGS:
        kind = "urban"
    elif shore.length > 0:
        kind = "shore"
    elif water.area > 0 and land.area < 1:
        kind = "sea"
    elif covered < 1:
        kind = "unknown"
    else:
        kind = "land"
    detail = (sum(building_cost(buildings["vertices"][b]) for b in inside)
              + shoreline_cost(edges) + 2 * TRIANGLE_BYTES + 4 * VERTEX_BYTES)
    return {
        "tile": list(tile), "class": kind,
        "water_m2": round(water.area), "land_m2": round(land.area),
        "unsurveyed_m2": round(cell.area - covered),
        "shoreline_m": round(shore.length, 1), "shoreline_edges": edges,
        "buildings": len(inside), "landmarks": marks,
        "metadata_bytes_est": TILE_RECORD_BYTES + LANDMARK_RECORD_BYTES * marks,
        "detail_bytes_est": detail,
        "coarse_bytes_est": (BOX_COST * len(inside) + shoreline_cost(coarse_edges)
                             + 2 * TRIANGLE_BYTES + 4 * VERTEX_BYTES),
    }


def tile_table(region, size, geo, buildings, landmarks):
    records = [tile_record(t, size, geo, buildings, landmarks)
               for t in tiles_covering(region, size)]
    classes = {}
    for r in records:
        classes[r["class"]] = classes.get(r["class"], 0) + 1
    total = {k: sum(r[k] for r in records) for k in (
        "water_m2", "land_m2", "shoreline_edges", "buildings", "metadata_bytes_est",
        "detail_bytes_est", "coarse_bytes_est")}
    detail = sorted(r["detail_bytes_est"] for r in records)
    return {
        "tile_size_m": size, "tiles": len(records), "classes": dict(sorted(classes.items())),
        "totals": total,
        "detail_bytes_est_median": detail[len(detail) // 2] if detail else 0,
        "detail_bytes_est_max": detail[-1] if detail else 0,
        "records": records,
    }


def river_calibration(geo, buildings):
    """Apply the cost model to the river crop and compare with the actual compiled arrays."""
    from shapely.geometry import box

    manifest = json.loads(RIVER_MANIFEST.read_text())
    actual = manifest["vertices"] * VERTEX_BYTES + manifest["triangles"] * TRIANGLE_BYTES
    crop = box(*RIVER_CROP)
    inside = [b for b in buildings["tree"].query(crop)
              if crop.contains(buildings["centroids"][b])]
    estimate = (sum(building_cost(buildings["vertices"][b]) for b in inside)
                + shoreline_cost(edge_count(geo["shoreline"].intersection(crop))))
    return {
        "compiled_vertices": manifest["vertices"], "compiled_triangles": manifest["triangles"],
        "compiled_array_bytes": actual, "model_estimate_bytes": estimate,
        "model_over_compiled": round(estimate / actual, 2),
        "note": ("Compiled arrays come from the 2026-10-17 river snapshot and include the "
                 "authored bridge, quays, landscape and building parts; the estimate uses "
                 "the 2026-10-24 source and the generic model above."),
    }


# --- Route measurement -------------------------------------------------------------------

def measure(name, points, geo, hazards, bridges):
    from shapely.geometry import LineString

    line = LineString(points)
    corridor = geo["water"].buffer(-ROUTE_CLEARANCE)
    if not corridor.covers(line):
        outside = line.difference(corridor)
        raise SystemExit(f"{name} leaves the {ROUTE_CLEARANCE:g} m water envelope "
                         f"for {outside.length:.1f} m near {outside.representative_point()}")
    margin = line.distance(geo["envelope"].exterior)
    if margin < ENVELOPE_MARGIN:
        raise SystemExit(f"{name} is {margin:.0f} m from the source boundary")
    near = sorted(h for h, p in hazards.items() if line.distance(p) <= 50)
    overhead = {}
    for ref, (bridge, shape) in sorted(bridges.items()):
        if line.intersects(shape):
            overhead.setdefault(bridge, []).append(ref)
    return {
        "east_north": [list(p) for p in points],
        "length_m": round(line.length, 1),
        "min_shore_clearance_m": round(line.distance(geo["shoreline"]), 1),
        "min_source_boundary_distance_m": round(margin, 1),
        "minutes_at": {f"{v}_mps": round(line.length / v / 60, 1) for v in SPEEDS},
        "mapped_hazards_within_50m": len(near),
        "mapped_hazard_ids_within_50m": near[:40],
        "overhead_crossings": overhead,
    }


# --- Source features ---------------------------------------------------------------------

def closed(geometry):
    return len(geometry) >= 4 and geometry[0] == geometry[-1]


def features(osm, frame):
    """Buildings, landmarks, overhead structures and mapped point hazards, projected."""
    from shapely import STRtree
    from shapely.geometry import LineString, Point, Polygon

    def project(geometry):
        return frame.xy(geometry)

    centroids, vertices = [], []
    for w in osm["ways"]:
        if ("building" in w["tags"] or "building:part" in w["tags"]) and closed(w["geometry"]):
            shape = Polygon(project(w["geometry"]))
            centroids.append(shape.centroid)
            vertices.append(len(w["geometry"]) - 1)
    buildings = {"centroids": centroids, "vertices": vertices, "tree": STRtree(centroids)}

    elements = {("n", n["id"]): n for n in osm["points"]}
    elements.update({("w", w["id"]): w for w in osm["ways"]})
    marks = []
    for label, ref, role in LANDMARKS:
        if ref is None:
            point = (0.0, 0.0)
        else:
            e = elements[ref]
            if ref[0] == "n":
                point = project([e["coord"]])[0]
            else:
                point = tuple(Polygon(project(e["geometry"])).centroid.coords[0])
        marks.append({"label": label, "osm": None if ref is None else f"{ref[0]}/{ref[1]}",
                      "role": role, "east_north": [round(point[0], 1), round(point[1], 1)],
                      "name": None if ref is None else elements[ref]["tags"].get("name")})

    def hazard(tags):
        return any(tags.get(k) in v for k, v in HAZARD_TAGS.items())

    hazards = {f"n/{n['id']}": Point(project([n["coord"]])[0])
               for n in osm["points"] if hazard(n["tags"])}
    hazards.update({f"w/{w['id']}": LineString(project(w["geometry"]))
                    for w in osm["ways"] if hazard(w["tags"])})
    bridges = {}
    for w in osm["ways"]:
        t = w["tags"]
        if t.get("man_made") == "bridge" or t.get("bridge", "no") != "no":
            g = project(w["geometry"])
            name = t.get("bridge:name") or t.get("name") or "unnamed"
            if t.get("highway") == "trunk" and name == "Västerleden":
                name = "Älvsborgsbron"  # the E 6.20 carriageways on the bridge deck
            shape = Polygon(g) if closed(w["geometry"]) else LineString(g)
            bridges[f"w/{w['id']}"] = (name, shape)
    return buildings, marks, hazards, bridges


# --- Map ---------------------------------------------------------------------------------

COLORS = {"water": "#b8dce8", "land": "#e7e1d3", "shore": "#3f6f80", "source": "#c0392b",
          "crop": "#c97a22", "slice": "#24775a", "route": "#b34835", "detour": "#7a4fa3",
          "start": "#5b6f2a", "unknown": "#cfcfcf", "grid": "#7d8a91"}
TILE_COLORS = {"sea": "#d7ecf3", "shore": "#f1d9a6", "urban": "#d79a8b", "land": "#e0dccd",
               "unknown": "#cfcfcf"}


def draw(geo, marks, routes, slice_shape, tables, info):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Patch, Rectangle
    from shapely.geometry import Point, box
    from shapely.plotting import plot_line, plot_polygon

    matplotlib.rcParams["svg.hashsalt"] = "proteus-adventure-survey"
    matplotlib.rcParams["path.simplify"] = False
    fig, (top, bottom) = plt.subplots(2, 1, figsize=(12, 15.5),
                                      gridspec_kw={"height_ratios": [1, 1.12]})
    fig.patch.set_facecolor("#f5f2eb")
    west, south, east, north = geo["envelope"].bounds
    # Drawing-only simplification keeps the SVG reviewable; measurements use full detail.
    for ax, bounds, tolerance in [
            (top, (west - 600, south - 600, east + 600, north + 600), 10.0),
            (bottom, slice_shape.buffer(400).bounds, 2.0)]:
        view = box(*bounds)
        ax.set_facecolor(COLORS["unknown"])
        ax.add_patch(Rectangle((bounds[0], bounds[1]), bounds[2] - bounds[0],
                               bounds[3] - bounds[1], facecolor="none", hatch="///",
                               edgecolor="#b0b0b0", linewidth=0))
        plot_polygon(geo["envelope"], ax=ax, add_points=False, facecolor=COLORS["land"],
                     edgecolor="none")
        water = geo["water"].intersection(view).simplify(tolerance)
        plot_polygon(water, ax=ax, add_points=False, facecolor=COLORS["water"],
                     edgecolor="none")
        shore = geo["shoreline"].intersection(view).simplify(tolerance)
        plot_line(shore, ax=ax, add_points=False, color=COLORS["shore"], linewidth=.35)
        plot_line(geo["source_edge"], ax=ax, add_points=False, color=COLORS["source"],
                  linewidth=1.2, linestyle=(0, (4, 2)))
        x0, y0, x1, y1 = RIVER_CROP
        ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, fill=False, linewidth=1.6,
                               edgecolor=COLORS["crop"], linestyle="--"))
        plot_polygon(slice_shape, ax=ax, add_points=False, facecolor="none",
                     edgecolor=COLORS["slice"], linewidth=1.8)
        ax.set(xlim=(bounds[0], bounds[2]), ylim=(bounds[1], bounds[3]), aspect="equal")
        ax.tick_params(labelsize=7)

    # Overview: routes, landmarks and the 512 m grid lines.
    size = TILE_SIZES[0]
    gx0, gx1 = math.floor(west / size), math.ceil(east / size)
    gy0, gy1 = math.floor(south / size), math.ceil(north / size)
    for i in range(gx0, gx1 + 1):
        top.axvline(i * size, color=COLORS["grid"], linewidth=.15, alpha=.5)
    for j in range(gy0, gy1 + 1):
        top.axhline(j * size, color=COLORS["grid"], linewidth=.15, alpha=.5)
    styles = {"main": (COLORS["route"], "-", 2.0), "detour": (COLORS["detour"], "-", 1.6),
              "short_start": (COLORS["start"], (0, (5, 2)), 1.6)}
    for name, (color, style, width) in styles.items():
        xs, ys = zip(*routes[name]["east_north"])
        for ax in (top, bottom):
            ax.plot(xs, ys, color=color, linestyle=style, linewidth=width, zorder=4)
    markers = {"origin": "*", "bridge": "s", "shelter": "o", "landmark": "^", "start": "D"}
    for m in marks:
        x, y = m["east_north"]
        for ax in (top, bottom):
            ax.scatter([x], [y], marker=markers[m["role"]], s=34, color="#253238", zorder=5)
        offset = (4, -11) if m["label"] == "Vinga gästhamn" else (4, 5)
        top.annotate(m["label"], (x, y), xytext=offset, textcoords="offset points",
                     fontsize=7.5, color="#253238")
    top.annotate("River-mouth approach\n(Älvsborgsfjorden)", (-2700, -1450),
                 xytext=(-1500, -4300), fontsize=8, color=COLORS["slice"],
                 arrowprops={"arrowstyle": "->", "color": COLORS["slice"]})
    top.annotate("Northern approach\n(Torshamnen, port)", (-4700, -500),
                 xytext=(-9000, 2100), fontsize=8, color=COLORS["slice"],
                 arrowprops={"arrowstyle": "->", "color": COLORS["slice"]})
    top.set_title("Gothenburg → Vinga: source coverage and candidate route\n"
                  "Metres east/north of the Eriksberg crane (EPSG:3007)", loc="left",
                  fontsize=11)

    # First slice: 512 m tile classes and labels.
    for r in tables["first_slice"][size]["records"]:
        x, y = r["tile"]
        bottom.add_patch(Rectangle((x * size, y * size), size, size, linewidth=.6,
                                   facecolor="none", edgecolor=COLORS["grid"], zorder=3))
        bottom.text(x * size + 14, y * size + size - 20, f"{x},{y}", fontsize=6,
                    color="#253238", va="top", zorder=6,
                    bbox={"boxstyle": "square,pad=.15", "linewidth": 0,
                          "facecolor": TILE_COLORS[r["class"]]})
    for m in marks:
        x, y = m["east_north"]
        if slice_shape.buffer(400).contains(Point(x, y)):
            bottom.annotate(m["label"], (x, y), xytext=(5, 5), textcoords="offset points",
                            fontsize=8, color="#253238", zorder=7)
    bottom.set_title(f"First slice: {info['slice_area_km2']} km², 512 m tiles "
                     "(id = floor(east/512), floor(north/512))", loc="left", fontsize=10)

    handles = [
        Patch(facecolor=COLORS["water"], label="Mapped water (sea + OSM inland water)"),
        Patch(facecolor=COLORS["land"], label="Land (processed OSM land polygons)"),
        Patch(facecolor=COLORS["unknown"], hatch="///", edgecolor="#b0b0b0",
              label="Outside source envelope (unknown)"),
        plt.Line2D([], [], color=COLORS["source"], linestyle="--",
                   label="Source boundary (not shoreline)"),
        plt.Line2D([], [], color=COLORS["crop"], linestyle="--", label="Current river crop"),
        plt.Line2D([], [], color=COLORS["slice"], label="Candidate first slice"),
        plt.Line2D([], [], color=COLORS["route"], label="Candidate route to Vinga"),
        plt.Line2D([], [], color=COLORS["detour"], label="Shelter detour (Långedrag)"),
        plt.Line2D([], [], color=COLORS["start"], linestyle="--",
                   label="Optional shorter start (Saltholmen)"),
    ] + [plt.Line2D([], [], marker=markers[role], color="#253238", linestyle="none",
                    label=label) for role, label in [
            ("shelter", "Candidate sheltered stop"), ("landmark", "Landmark"),
            ("bridge", "Bridge"), ("start", "Shorter-start location")]
    ] + [Patch(facecolor=c, label=f"Tile label: {k}") for k, c in TILE_COLORS.items()
         if k != "unknown"]
    fig.legend(handles=handles, loc="lower center", ncol=4, fontsize=7.5, frameon=False,
               bbox_to_anchor=(.5, .028))
    fig.text(.06, .008,
             "Map data © OpenStreetMap contributors, ODbL 1.0; land polygons processed by "
             "osmdata.openstreetmap.de. Planning only — not for navigation.\n"
             f"Terrain: {info['terrain']}. Depth and overhead clearance are not surveyed; "
             f"route checks use a {ROUTE_CLEARANCE:g} m 2D envelope.",
             fontsize=7.5, color="#48555b")
    fig.subplots_adjust(left=.06, right=.98, top=.96, bottom=.1, hspace=.12)
    fig.savefig(BASE / "coverage.png", dpi=110, metadata={"Software": None})
    fig.savefig(BASE / "coverage.svg", metadata={"Date": None, "Creator": None})
    plt.close(fig)


# --- Self-checks -------------------------------------------------------------------------

def self_test():
    from shapely.geometry import LineString, Point, Polygon, box

    # Negative IDs: floor, not truncation toward zero.
    assert tile_id(-0.001, 0, 512) == (-1, 0) and tile_id(-512, -512.001, 512) == (-1, -2)
    assert tile_id(511.999, 512, 512) == (0, 1) and tile_id(-1024, 1023.9, 1024) == (-1, 0)

    # Two adjacent coastal tiles with an island across their seam. Each tile's water
    # boundary includes the seam; the counted shoreline must not.
    sea = box(0, -512, 1024, 0)
    island = box(400, -400, 624, -200)
    water = sea.difference(island)
    shoreline = island.exterior
    left, right = tile_box((0, -1), 512), tile_box((1, -1), 512)
    assert tiles_covering(island, 512) == [(0, -1), (1, -1)]
    parts = [tile_shoreline(shoreline, t).length for t in (left, right)]
    assert abs(sum(parts) - shoreline.length) < 1e-6 and parts[0] == parts[1] == 424
    seam = LineString([(512, -512), (512, 0)])
    assert water.intersection(left).boundary.intersection(seam).length > 0
    for tile in (left, right):
        assert tile_shoreline(shoreline, tile).intersection(seam).length == 0, "seam as shore"

    # Multipolygon with an island hole, and an incomplete coastline that stays open.
    ways = {1: [(0, 0), (100, 0), (100, 100)], 2: [(100, 100), (0, 100), (0, 0)],
            3: [(40, 40), (60, 40), (60, 60), (40, 60), (40, 40)], 4: [(200, 0), (300, 0)]}
    lake, problems = multipolygon([{"type": "w", "ref": 1, "role": "outer"},
                                   {"type": "w", "ref": 2, "role": "outer"},
                                   {"type": "w", "ref": 3, "role": "inner"}], ways)
    assert not problems and lake.area == 9600 and len(lake.interiors) == 1
    broken, problems = multipolygon([{"type": "w", "ref": 1, "role": "outer"},
                                     {"type": "w", "ref": 9, "role": "outer"}], ways)
    assert broken.is_empty and problems == ["missing way 9", "open outer chain of 3 nodes"]
    rings, opened = join_rings([ways[1], ways[4]])
    assert not rings and len(opened) == 2, "an incomplete coastline was closed"

    # Bridge opening: legs are solid, the deck is not land, the gap remains navigable.
    channel = box(0, 0, 300, 100)
    legs = [box(60, 40, 80, 60), box(220, 40, 240, 60)]
    deck = box(0, 45, 300, 55)
    open_water = channel.difference(legs[0]).difference(legs[1])
    assert open_water.buffer(-ROUTE_CLEARANCE).covers(LineString([(150, 20), (150, 80)]))
    assert not open_water.buffer(-ROUTE_CLEARANCE).covers(LineString([(70, 20), (70, 80)]))
    assert open_water.contains(Point(150, 50)) and deck.contains(Point(150, 50))

    # Assembly in lon/lat: box edges are source boundaries, real coast is shoreline, and an
    # island in an inland lake survives as a hole.
    frame = Frame()
    env = (11.80, 57.65, 11.82, 57.66)
    land = {"polygons": [[[[11.80, 57.65], [11.82, 57.65], [11.82, 57.655], [11.80, 57.655],
                           [11.80, 57.65]]]]}
    lake = [[11.805, 57.651], [11.815, 57.651], [11.815, 57.654], [11.805, 57.654],
            [11.805, 57.651]]
    isle = [[11.809, 57.652], [11.811, 57.652], [11.811, 57.653], [11.809, 57.653],
            [11.809, 57.652]]
    leg = [[11.8100, 57.6570], [11.8102, 57.6570], [11.8102, 57.6571], [11.8100, 57.6571],
           [11.8100, 57.6570]]
    osm = {"envelope_wsen": list(env), "points": [],
           "ways": [{"id": 1, "tags": {}, "geometry": lake},
                    {"id": 2, "tags": {}, "geometry": isle},
                    {"id": 3, "tags": {"bridge:support": "pier"}, "geometry": leg}],
           "relations": [{"id": 9, "tags": {"type": "multipolygon", "natural": "water"},
                          "members": [{"type": "w", "ref": 1, "role": "outer"},
                                      {"type": "w", "ref": 2, "role": "inner"}]}]}
    geo = assemble(osm, land, frame)
    assert len(geo["water"].geoms) == 2 and geo["support_count"] == 1
    lake_shape = min(geo["water"].geoms, key=lambda g: g.area)
    assert len(lake_shape.interiors) == 1, "lake island lost"
    width = frame.xy([(11.80, 57.655), (11.82, 57.655)])
    coast = math.dist(*width)
    expected = coast + lake_shape.exterior.length + lake_shape.interiors[0].length
    expected += geo["supports"].length
    assert abs(geo["shoreline"].length - expected) < 0.05, "source box counted as shoreline"
    top = math.dist(*frame.xy([(11.80, 57.66), (11.82, 57.66)]))
    side = math.dist(*frame.xy([(11.80, 57.655), (11.80, 57.66)]))
    assert abs(geo["source_edge"].length - top - 2 * side) < 0.05, "source edge misclassified"


# --- Survey ------------------------------------------------------------------------------

def write_json(name, data):
    (BASE / name).write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n")


def rounded(value):
    if isinstance(value, float):
        return round(value, 1)
    if isinstance(value, dict):
        return {k: rounded(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [rounded(v) for v in value]
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--self-test", action="store_true", help="run fixtures only")
    args = parser.parse_args()
    self_test()
    if args.self_test:
        print("survey fixtures OK")
        return

    from shapely.geometry import LineString, Point, Polygon, box

    sources = json.loads(MANIFEST.read_text())
    osm, osm_hash = read_source("osm-corridor.json.gz", sources)
    land, land_hash = read_source("land-polygons.json.gz", sources)
    frame = Frame()
    geo = assemble(osm, land, frame)
    buildings, marks, hazards, bridges = features(osm, frame)

    components = polygons_of(geo["water"])
    navigable = next(p for p in components if p.contains(Point(DOCK)))
    vinga = next(m for m in marks if m["label"] == "Vinga gästhamn")["east_north"]
    assert navigable.distance(Point(vinga)) < 50, "Vinga is not reachable in mapped water"

    routes = {
        "main": measure("main route", ROUTE, geo, hazards, bridges),
        "detour": measure("shelter detour", DETOUR, geo, hazards, bridges),
        "first_slice": measure("first-slice route", SLICE_ROUTE, geo, hazards, bridges),
        "short_start": measure("shorter start", SHORT_START, geo, hazards, bridges),
    }
    # The shorter start joins the main route at its waypoint west of Rivö.
    joined = (LineString(SHORT_START).length
              + LineString(ROUTE[ROUTE.index(SHORT_START[-1]):]).length)
    routes["short_start"]["to_vinga_length_m"] = round(joined, 1)
    routes["short_start"]["to_vinga_minutes_at"] = {f"{v}_mps": round(joined / v / 60, 1)
                                                    for v in SPEEDS}
    bridge = next(m for m in marks if m["role"] == "bridge")
    for name in ("main", "first_slice"):
        assert "w/387711139" in routes[name]["overhead_crossings"].get("Älvsborgsbron", []), \
            "route misses the bridge"
    other_overhead = sorted({b for r in routes.values() for b in r["overhead_crossings"]}
                            - {"Älvsborgsbron"})

    slice_shape = Polygon(FIRST_SLICE)
    crop = box(*RIVER_CROP)
    assert slice_shape.is_valid and slice_shape.covers(LineString(DETOUR))
    assert slice_shape.distance(geo["envelope"].exterior) >= ENVELOPE_MARGIN
    joint = slice_shape.intersection(crop)
    slice_water = navigable.intersection(slice_shape.union(crop))
    connected = next(p for p in polygons_of(slice_water) if p.contains(Point(DOCK)))
    assert connected.contains(Point(DETOUR[-1])), "slice water does not reach the shelter"
    far = max(math.dist(bridge["east_north"], p) for p in FIRST_SLICE)

    regions = {
        "river_crop": crop,
        "first_slice": slice_shape,
        "resident_crop_and_slice": crop.union(slice_shape),
        "route_corridor_1km": LineString(ROUTE).buffer(1000).intersection(geo["envelope"]),
        "source_envelope": geo["envelope"],
    }
    landmark_points = [Point(m["east_north"]) for m in marks]
    tables = {name: {size: tile_table(region, size, geo, buildings, landmark_points)
                     for size in TILE_SIZES} for name, region in regions.items()}

    terrain = sources["acquisition"]["terrain_lantmateriet"]
    terrain_text = ("not acquired — " + terrain["access_dependency"].rstrip(".")
                    + "; authored-relief fallback" if terrain["status"] != "acquired"
                    else "acquired")
    info = {"slice_area_km2": round(slice_shape.area / 1e6, 2), "terrain": terrain_text}
    draw(geo, marks, routes, slice_shape, tables, info)

    def compare(name):
        out = {}
        for size, t in tables[name].items():
            out[f"{size}m"] = {k: v for k, v in t.items() if k != "records"}
        return out

    small, large = tables["first_slice"][512], tables["first_slice"][1024]
    resident = tables["resident_crop_and_slice"]
    corridor = tables["route_corridor_1km"]
    calibration = river_calibration(geo, buildings)
    factor = calibration["compiled_array_bytes"] / calibration["model_estimate_bytes"]

    def sea_share(size):
        return round(100 * corridor[size]["classes"].get("sea", 0) / corridor[size]["tiles"])

    decision = {
        "initial_experiment_tile_m": 512,
        "reason": (
            f"Record overhead is small at either size: the river crop plus first slice is "
            f"{resident[512]['tiles']} tiles at 512 m "
            f"({resident[512]['totals']['metadata_bytes_est']} B estimated metadata) versus {resident[1024]['tiles']} at 1 km. 512 m bounds the "
            f"largest first-slice tile at about {round(small['detail_bytes_est_max'] / 1024)} "
            f"KiB of estimated detail instead of {round(large['detail_bytes_est_max'] / 1024)} "
            f"KiB, and {sea_share(512)}% of the route corridor's 512 m tiles are geometry-free "
            f"open sea versus {sea_share(1024)}% at 1 km. Compare 1 km once A0.4/A2 measure "
            f"real submission and draw costs."),
        "runtime_format": "Not chosen; this is a survey parameter, not a bake format.",
    }
    resident_estimate = {
        f"{size}m": {
            "model_detail_bytes": resident[size]["totals"]["detail_bytes_est"],
            "scaled_to_river_bake_bytes": round(resident[size]["totals"]["detail_bytes_est"]
                                                * factor),
            "model_coarse_bytes": resident[size]["totals"]["coarse_bytes_est"],
            "metadata_bytes": resident[size]["totals"]["metadata_bytes_est"],
        } for size in TILE_SIZES}

    shoreline_slice = geo["shoreline"].intersection(slice_shape)
    summary = {
        "schema": 1,
        "generator": "docs/adventure/survey.py",
        "inputs": {
            "osm-corridor.json.gz": {"content_sha256": osm_hash},
            "land-polygons.json.gz": {"content_sha256": land_hash},
            "sources_manifest": "docs/adventure/sources.json",
            "river_manifest": {"path": "assets/geometry/river/manifest.json",
                               "sha256": hashlib.sha256(RIVER_MANIFEST.read_bytes()).hexdigest()},
            "osm_snapshot_utc": sources["acquisition"]["osm_sweden"]["snapshot_utc"],
            "land_snapshot_utc": sources["acquisition"]["land_polygons"]["snapshot_utc"],
        },
        "frame": {
            "crs": "EPSG:3007", "origin_epsg4326": list(ORIGIN),
            "origin_epsg3007": [round(frame.ox, 3), round(frame.oy, 3)],
            "axes": "metres east, north of the origin; simulation X points west (X = -east)",
            "tile_ids": "floor(east / size), floor(north / size), anchored at the origin",
        },
        "topology": {
            "method": ("Sea = source box minus processed land polygons; OSM natural=water/"
                       "riverbank multipolygons and closed ways are added (the land polygons "
                       "treat the river and lakes as land); bridge:support areas are solid. "
                       "Assembled in lon/lat before any clipping, then projected."),
            "water_components": len(components),
            "navigable_component_km2": round(navigable.area / 1e6, 2),
            "water_km2": round(geo["water"].area / 1e6, 2),
            "land_km2": round(geo["land"].area / 1e6, 2),
            "shoreline_km": round(geo["shoreline"].length / 1000, 1),
            "source_boundary_km": round(geo["source_edge"].length / 1000, 1),
            "bridge_supports_in_water": geo["support_count"],
            "unresolved_water_relations": geo["unresolved"],
            "incomplete_source_relations": sources["derived"]["osm"]["incomplete_relations"],
        },
        "routes": {k: {kk: vv for kk, vv in v.items() if kk not in ("east_north",)}
                   for k, v in routes.items()},
        "first_slice": {
            "area_km2": info["slice_area_km2"],
            "water_km2": round(slice_shape.intersection(geo["water"]).area / 1e6, 2),
            "shoreline_km": round(shoreline_slice.length / 1000, 1),
            "overlap_with_river_crop_km2": round(joint.area / 1e6, 2),
            "farthest_point_from_bridge_m": round(far),
            "shelter": "Långedrag old harbour",
            "route_length_m": routes["first_slice"]["length_m"],
            "experience_target": "5–10 minutes including exploration (adventure plan); "
                                 "not inferred from distance",
        },
        "landmarks": marks,
        "tiles": {name: compare(name) for name in regions},
        "river_calibration": calibration,
        "resident_crop_and_slice_estimate": resident_estimate,
        "decision": decision,
        "gaps": {
            "terrain": terrain_text,
            "depth": "No bathymetry; EMODnet not acquired. Mapped rocks/reefs are counted "
                     "near routes but are not a depth survey.",
            "overhead": "Älvsborgsbron OSM maxheight:marine=45 m is an unverified tag. "
                        + (f"Other mapped crossings: {', '.join(other_overhead)}."
                           if other_overhead else "No other mapped bridge crosses the routes."),
            "outside_envelope": "No source data beyond the A0.2 box; it is drawn hatched.",
            "route": "2D clearance from mapped shoreline only; not swept-hull verification "
                     "and not navigation certification.",
        },
        "cost_model": {
            "vertex_bytes": VERTEX_BYTES, "triangle_bytes": TRIANGLE_BYTES,
            "building": "2n vertices and 3n-2 triangles for an n-vertex footprint",
            "shoreline_edge": "2 vertices and 4 triangles (quay wall plus fills)",
            "coarse": f"buildings as 8-vertex/10-triangle boxes; shoreline simplified "
                      f"{COARSE_TOLERANCE:g} m",
            "metadata": f"{TILE_RECORD_BYTES} B per tile plus {LANDMARK_RECORD_BYTES} B per "
                        "landmark",
            "urban_tile": f"at least {URBAN_BUILDINGS} building outline centroids",
            "status": "Estimates only; actual bake sizes depend on the A2 format.",
        },
        "attribution": "© OpenStreetMap contributors, ODbL 1.0; land polygons processed by "
                       "osmdata.openstreetmap.de",
    }
    write_json("summary.json", rounded(summary))
    write_json("route.json", rounded({
        "schema": 1, "generator": "docs/adventure/survey.py",
        "crs": "EPSG:3007 offsets from the crane origin", "units": "metres",
        "coordinate_order": "east,north", "origin_epsg4326": list(ORIGIN),
        "clearance_envelope_m": ROUTE_CLEARANCE,
        "status": "Survey candidate; not a runtime route, chart or swept-hull check.",
        "routes": routes,
    }))
    lonlat = [[round(c, 7) for c in frame.lonlat(p)] for p in FIRST_SLICE]
    write_json("first-slice.json", rounded({
        "schema": 1, "generator": "docs/adventure/survey.py",
        "crs": "EPSG:3007 offsets from the crane origin", "units": "metres",
        "coordinate_order": "east,north", "origin_epsg4326": list(ORIGIN),
        "polygon_east_north": [list(p) for p in FIRST_SLICE],
        "polygon_lonlat_epsg4326": lonlat,
        "tiles_512m": [r["tile"] for r in small["records"]],
        "tiles_1024m": [r["tile"] for r in large["records"]],
        "tile_records_512m": small["records"],
        "tile_records_1024m": large["records"],
        "justification": (
            "Joins the river crop's west and south edges, adds Älvsborgsfjorden past Nya "
            "Älvsborg to the Långedrag old harbour shelter, and keeps the open water toward "
            "Rivöfjorden as the exposed view. One connected water body; no source edge."),
    }))
    print(json.dumps({k: summary[k] for k in ("routes", "first_slice", "decision")},
                     indent=1, ensure_ascii=False))


if __name__ == "__main__":
    main()
