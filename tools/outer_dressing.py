"""Buildings, landmarks and trees for the outer slice beyond the river crop (A3.5a).

Used by tools/prepare_outer_map.py. Everything here is a blockout on the baked terrain:
footprints, crane and mark positions come from the pinned OSM corridor; heights without tags,
roof shapes, crane and mark proportions, colours and the tree scatter are authored assumptions,
recorded in the manifest. Coordinates are crane-origin metres, x = east, z = north, y = up.
"""
import math
import zlib

import numpy as np
from shapely import contains_xy, make_valid, prepare, transform
from shapely.geometry import LineString, Point, Polygon
from shapely.geometry.polygon import orient
from shapely.ops import unary_union

HOUSE_TYPES = {"detached", "house", "semidetached_house", "terrace", "villa", "farm",
               "residential", "bungalow", "cabin"}
SMALL_TYPES = {"garage", "garages", "shed", "carport", "allotment_house", "hut", "boathouse",
               "roof", "kiosk", "toilets"}
PORT_TYPES = {"industrial", "warehouse", "storage_tank", "service", "transportation", "silo"}
HOUSE_WALL = 5.5       # eaves of a house without tags
SMALL_WALL = 2.8
SINK = 0.6             # walls start this far below the lowest footprint corner
MIN_AREA = 6.0
HOUSE_WALLS = [(222, 218, 205), (156, 58, 48), (214, 186, 120), (176, 182, 186),
               (200, 205, 196)]
HOUSE_ROOFS = [(74, 72, 76), (120, 62, 52), (88, 84, 82)]
CITY_WALLS = [(166, 173, 195), (185, 183, 191), (154, 166, 188), (190, 177, 163)]
STONE = (150, 141, 126)
CRANE = (198, 80, 54)
CRANE_DARK = (120, 58, 46)
STEEL = (140, 146, 152)
CONTAINER_CRANE_HEIGHT = 118.0  # the mapped height; used for the untagged ones too
MAST_HEIGHT = 30.0
FORT_RADIUS = 150.0            # buildings this close to Nya Älvsborg are stone
MARK_RADIUS = 1.5              # water carved around marks that stand in it
TREE_SPACING = 11.0
TREE_SHADES = [(56, 84, 58), (66, 95, 62), (50, 74, 56), (74, 98, 66)]
ASSUMPTIONS = (
    "Footprints, crane, mast and mark positions and tagged heights are OSM. Untagged heights: "
    "houses 5.5 m eaves with a gable roof on near-rectangular footprints, sheds and garages "
    "2.8 m, tanks 14 m, other buildings 8-14 m by area. Walls sink 0.6 m below the lowest "
    "corner. Container cranes are a raised-boom blockout at the mapped 118 m (also used where "
    "untagged), boom toward the nearest water; lighting masts 30 m; marks are 3.5-4.5 m piles "
    "in their mapped colour on a 1.5 m carved rock where they stand in water. Buildings within "
    "150 m of Nya Älvsborg are stone. Trees are an authored scatter (11 m lattice, 80 m value "
    "noise) on ground at least 3 m high and under 24 degrees, 6 m from shore and clear of "
    "buildings, roads and the port; not mapped vegetation. Colours are authored.")


def _hash(*values):
    return zlib.crc32(",".join(str(v) for v in values).encode()) / 0xFFFFFFFF


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


class Builder:
    """Collects triangles whose visible side is (p1 - p0) x (p2 - p0), like the bake."""

    def __init__(self):
        self.triangles = []

    def face(self, points, color, toward):
        """Add a triangle, flipped if needed so its visible side faces direction TOWARD."""
        n = _cross(_sub(points[1], points[0]), _sub(points[2], points[0]))
        if sum(a * b for a, b in zip(n, toward)) < 0:
            points = [points[0], points[2], points[1]]
        self.triangles.append(([tuple(map(float, p)) for p in points], color))

    def quad(self, a, b, c, d, color, toward):
        self.face([a, b, c], color, toward)
        self.face([a, c, d], color, toward)

    def prism(self, ring, bottom, top, color, roof=None):
        """Vertical walls around RING (x, z points) and an optional flat top."""
        poly = orient(Polygon(ring), sign=1)
        coords = list(poly.exterior.coords)
        for (x, z), (xx, zz) in zip(coords, coords[1:]):
            out = (zz - z, 0, -(xx - x))  # right of a counter-clockwise edge
            if out == (0, 0, 0):
                continue
            self.quad((x, bottom, z), (xx, bottom, zz), (xx, top, zz), (x, top, z), color, out)
        if roof is not None:
            for t in _triangulate(poly):
                self.face([(x, top, z) for x, z in t], roof, (0, 1, 0))

    def beam(self, p, q, width, color):
        """A square-section bar from P to Q (x, y, z)."""
        d = np.subtract(q, p)
        length = float(np.linalg.norm(d))
        if length < 1e-6:
            return
        d = d / length
        up = np.array([0.0, 1.0, 0.0]) if abs(d[1]) < .9 else np.array([1.0, 0.0, 0.0])
        u = np.cross(d, up)
        u /= np.linalg.norm(u)
        v = np.cross(d, u)
        h = width / 2
        corners = [u * h + v * h, -u * h + v * h, -u * h - v * h, u * h - v * h]
        a = [tuple(np.add(p, c)) for c in corners]
        b = [tuple(np.add(q, c)) for c in corners]
        for i in range(4):
            j = (i + 1) % 4
            out = tuple(corners[i] + corners[j])
            self.quad(a[i], a[j], b[j], b[i], color, out)
        self.quad(*a, color, tuple(-d))
        self.quad(*b, color, tuple(d))


def _triangulate(poly):
    from shapely import constrained_delaunay_triangles

    return [list(t.exterior.coords)[:3] for t in constrained_delaunay_triangles(poly).geoms]


def _parts(shape):
    if shape.is_empty:
        return []
    if shape.geom_type == "Polygon":
        return [shape]
    return [p for g in getattr(shape, "geoms", []) for p in _parts(g)]


def _height(model, points):
    return np.asarray(model(np.asarray(points, dtype=float).reshape(-1, 2)), dtype=float)


def building_shape(tags, area):
    """(wall height, roof) for a footprint: 'gable', 'flat'; heights in metres above ground."""
    kind = tags.get("building", "yes")
    if "height" in tags:
        try:
            value = float(tags["height"].replace(",", "."))
        except ValueError:
            value = 0
        if 0 < value <= 200:
            return value, "flat"
    if "building:levels" in tags:
        try:
            floors = float(tags["building:levels"])
        except ValueError:
            floors = 0
        if 0 < floors <= 60:
            return floors * 3 + 1, "gable" if kind in HOUSE_TYPES and floors <= 3 else "flat"
    if kind in SMALL_TYPES or area < 40:
        return SMALL_WALL, "flat"
    if kind in HOUSE_TYPES or (kind == "yes" and area < 220):
        return HOUSE_WALL, "gable"
    if kind == "storage_tank":
        return 14.0, "flat"
    if kind == "apartments":
        return 13.0, "flat"
    if kind in PORT_TYPES or kind in {"commercial", "retail", "office", "school"}:
        return 9.0 if area < 2000 else 14.0, "flat"
    return 8.0 if area < 1000 else 12.0, "flat"


def gable(builder, rect, base, eave, wall, roof):
    """Four walls of a rectangle and a ridge along its long side."""
    pts = list(orient(rect, sign=1).exterior.coords)[:4]
    sides = [math.dist(pts[i], pts[(i + 1) % 4]) for i in range(4)]
    k = 0 if sides[0] >= sides[1] else 1  # long sides are k and k + 2
    short = sides[(k + 1) % 4]
    rise = min(4.0, max(1.5, short * .35))
    builder.prism(pts + [pts[0]], base, eave, wall)
    # Ridge runs between the midpoints of the two short sides.
    a, b, c, d = (pts[(k + i) % 4] for i in range(4))  # a-b long, b-c short, c-d long, d-a short
    m1 = ((b[0] + c[0]) / 2, eave + rise, (b[1] + c[1]) / 2)
    m2 = ((d[0] + a[0]) / 2, eave + rise, (d[1] + a[1]) / 2)
    center = (sum(p[0] for p in pts) / 4, sum(p[1] for p in pts) / 4)
    for p, q, m in ((b, c, m1), (d, a, m2)):  # gable ends
        out = ((p[0] + q[0]) / 2 - center[0], 0, (p[1] + q[1]) / 2 - center[1])
        builder.face([(p[0], eave, p[1]), (q[0], eave, q[1]), m], wall, out)
    for p, q, r, s in ((a, b, m1, m2), (c, d, m2, m1)):  # slopes
        out = ((p[0] + q[0]) / 2 - center[0], 1, (p[1] + q[1]) / 2 - center[1])
        builder.quad((p[0], eave, p[1]), (q[0], eave, q[1]), r, s, roof, out)


def footprints(osm, frame, multipolygon):
    """Projected (osm id, tags, shape) for building ways and multipolygon relations.

    MULTIPOLYGON is the survey's assembler, which works on lon/lat ways.
    """
    out = []
    ways = {w["id"]: [tuple(p) for p in w["geometry"]] for w in osm["ways"]}
    for w in osm["ways"]:
        g = w["geometry"]
        if "building" in w["tags"] and len(g) >= 4 and g[0] == g[-1]:
            out.append((f"w/{w['id']}", w["tags"], make_valid(Polygon(frame.xy(g)))))
    for r in osm["relations"]:
        if "building" not in r["tags"] or r["tags"].get("type") != "multipolygon":
            continue
        shape, problems = multipolygon(r["members"], ways)
        if problems or shape.is_empty:
            continue
        out.append((f"r/{r['id']}", r["tags"],
                    make_valid(transform(shape, lambda c: frame.array(c)))))
    return out


def add_buildings(builder, osm, frame, area, model, fort, multipolygon):
    """Extrude the corridor's buildings inside AREA on the terrain. Returns manifest stats."""
    prepare(area)
    stats = {"buildings": 0, "gabled": 0, "skipped_small": 0, "stone": 0}
    solids = []
    port = []
    for key, tags, shape in sorted(footprints(osm, frame, multipolygon), key=lambda e: e[0]):
        if not area.intersects(shape):
            continue
        shape = shape.intersection(area).simplify(.25, preserve_topology=True)
        for part in _parts(shape):
            if part.area < MIN_AREA:
                stats["skipped_small"] += 1
                continue
            part = Polygon(part.exterior)  # courtyards are rare out here; fill them
            wall, roof = building_shape(tags, part.area)
            ground = _height(model, list(part.exterior.coords))
            base = float(ground.min()) - SINK
            eave = float(ground.max()) + wall
            stone = part.centroid.distance(fort) < FORT_RADIUS
            seed = int(_hash(key) * 1e6)
            if stone:
                color, top = STONE, tuple(c - 22 for c in STONE)
                stats["stone"] += 1
            elif roof == "gable":
                color, top = HOUSE_WALLS[seed % 5], HOUSE_ROOFS[seed // 5 % 3]
            else:
                color = CITY_WALLS[seed % 4]
                top = tuple(max(0, c - 30) for c in color)
            rect = None
            if roof == "gable" and not stone:
                with np.errstate(invalid="ignore"):  # slivers have no defined envelope
                    rect = part.minimum_rotated_rectangle
            if rect is not None and rect.area > 0 and part.area / rect.area > .8:
                gable(builder, rect, base, eave, color, top)
                stats["gabled"] += 1
            else:
                builder.prism(list(part.exterior.coords), base, eave, color, roof=top)
            stats["buildings"] += 1
            solids.append(part)
            if tags.get("building") in PORT_TYPES or part.area > 3000:
                port.append(part)
    return stats, unary_union(solids), unary_union(port)


def container_crane(builder, x, z, ground, toward, height):
    """Ship-to-shore crane blockout with its boom raised, boom side toward the water."""
    ux, uz = toward
    vx, vz = -uz, ux

    def at(u, v, y):
        return (x + ux * u + vx * v, y, z + uz * u + vz * v)

    deck = ground + 48
    for u in (-14, 14):
        for v in (-9, 9):
            builder.beam(at(u, v, ground - .5), at(u, v, deck), 2.4, CRANE)
    for v in (-9, 9):
        builder.beam(at(-14, v, deck), at(14, v, deck), 2.2, CRANE)
        builder.beam(at(-14, v, ground + 14), at(14, v, ground + 14), 1.4, CRANE)
    for u in (-14, 14):
        builder.beam(at(u, -9, deck), at(u, 9, deck), 2.2, CRANE)
    for v in (-4, 4):
        builder.beam(at(-34, v, deck + 2), at(20, v, deck + 2), 2.0, CRANE)
        builder.beam(at(20, v, deck + 2), at(34, v, ground + height), 1.8, CRANE)
        builder.beam(at(-6, v, deck + 2), at(0, v, ground + .62 * height), 1.6, CRANE)
        builder.beam(at(6, v, deck + 2), at(0, v, ground + .62 * height), 1.6, CRANE)
        builder.beam(at(0, v, ground + .62 * height), at(34, v, ground + height), .7, STEEL)
        builder.beam(at(0, v, ground + .62 * height), at(-34, v, deck + 2), .7, STEEL)
    builder.beam(at(-30, 0, deck + 6), at(-20, 0, deck + 6), 9.0, CRANE_DARK)


def mast(builder, x, z, ground, height=MAST_HEIGHT):
    builder.beam((x, ground - .5, z), (x, ground + height, z), .8, STEEL)
    builder.beam((x, ground + height - 1, z), (x, ground + height + .6, z), 2.6, STEEL)


def mark(builder, x, z, ground, colour, height):
    """A channel mark: a pile in its colour with a darker top and a small light."""
    body = {"red": (186, 52, 46), "green": (52, 128, 70), "white": (226, 226, 220),
            "black": (40, 40, 44)}.get(colour, (226, 226, 220))
    builder.beam((x, ground - 2, z), (x, ground + height * .8, z), 1.1, body)
    builder.beam((x, ground + height * .8, z), (x, ground + height, z), .7, (40, 40, 44))


def tree(builder, x, z, ground, size, shade):
    """A five-sided spruce-like cone over a short inverted cone."""
    radius = 1.5 + size * 1.4
    skirt = ground + 1.4
    apex = (x, ground + 5 + size * 5.5, z)
    foot = (x, ground + .3, z)
    turn = _hash(x, z) * math.tau
    ring = [(x + radius * math.cos(turn + i * math.tau / 5), skirt,
             z + radius * math.sin(turn + i * math.tau / 5)) for i in range(5)]
    dark = tuple(c - 12 for c in shade)
    for i in range(5):
        a, b = ring[i], ring[(i + 1) % 5]
        out = ((a[0] + b[0]) / 2 - x, .5, (a[2] + b[2]) / 2 - z)
        builder.face([a, b, apex], shade, out)
        builder.face([a, b, foot], dark, (out[0], -1, out[2]))


def add_trees(builder, land, keep_out, model):
    """Clustered scatter on gentle, dry ground away from buildings, roads, port and shore."""
    allowed = land.buffer(-6).difference(keep_out)
    prepare(allowed)
    x0, z0, x1, z1 = allowed.bounds
    xs = np.arange(math.floor(x0 / TREE_SPACING), math.ceil(x1 / TREE_SPACING) + 1)
    zs = np.arange(math.floor(z0 / TREE_SPACING), math.ceil(z1 / TREE_SPACING) + 1)
    cand = []
    for i in xs:
        for j in zs:
            # Value noise on an 80 m lattice makes copses and clearings.
            gx, gz = i * TREE_SPACING / 80, j * TREE_SPACING / 80
            fx, fz = gx - math.floor(gx), gz - math.floor(gz)
            fx, fz = fx * fx * (3 - 2 * fx), fz * fz * (3 - 2 * fz)
            corner = [_hash("n", math.floor(gx) + a, math.floor(gz) + b)
                      for a, b in ((0, 0), (1, 0), (0, 1), (1, 1))]
            noise = (corner[0] * (1 - fx) + corner[1] * fx) * (1 - fz) + \
                (corner[2] * (1 - fx) + corner[3] * fx) * fz
            if noise < .45 or _hash("p", i, j) > (noise - .45) * 2.2:
                continue
            cand.append((i * TREE_SPACING + (_hash("x", i, j) - .5) * TREE_SPACING * .8,
                         j * TREE_SPACING + (_hash("z", i, j) - .5) * TREE_SPACING * .8))
    if not cand:
        return 0
    cand = np.array(cand)
    cand = cand[contains_xy(allowed, cand[:, 0], cand[:, 1])]
    h = _height(model, cand)
    dx = _height(model, cand + [3, 0]) - _height(model, cand - [3, 0])
    dz = _height(model, cand + [0, 3]) - _height(model, cand - [0, 3])
    slope = np.degrees(np.arctan(np.hypot(dx, dz) / 6))
    kept = (h >= 3) & (slope < 24)
    for (x, z), ground in zip(cand[kept], h[kept]):
        seed = _hash("t", round(x, 2), round(z, 2))
        tree(builder, float(x), float(z), float(ground) - .2, seed,
             TREE_SHADES[int(seed * 997) % len(TREE_SHADES)])
    return int(kept.sum())


def nearest_water_direction(water, x, z):
    from shapely.ops import nearest_points

    p = nearest_points(water, Point(x, z))[0]
    dx, dz = p.x - x, p.y - z
    length = math.hypot(dx, dz) or 1.0
    return dx / length, dz / length


def marks(osm, frame, region, crop):
    """Channel marks and minor lights in the outer slice: (id, x, z, colour, height)."""
    out = []
    for n in osm["points"]:
        t = n["tags"]
        kind = t.get("seamark:type", "")
        if not (kind.startswith("beacon") or kind == "light_minor"):
            continue
        x, z = frame.xy([n["coord"]])[0]
        if not region.contains(Point(x, z)) or crop.contains(Point(x, z)):
            continue
        colour = (t.get(f"seamark:{kind}:colour") or t.get("seamark:light:colour") or "white")
        out.append((n["id"], x, z, colour.split(";")[0], 4.5 if kind == "light_minor" else 3.5))
    return sorted(out)


def carve_marks(water, mark_list):
    """Water less a small island under each mark that stands in it, so boats meet the pile."""
    islands = [Point(x, z).buffer(MARK_RADIUS, quad_segs=2)
               for _, x, z, _, _ in mark_list if water.contains(Point(x, z))]
    return water.difference(unary_union(islands)) if islands else water, len(islands)


def dress(osm, frame, region, crop, water, land, model, fort, multipolygon):
    """(buildings, other) triangles ((three (x, y, z)), colour) and manifest stats.

    Since A3.5f the buildings are returned apart: the bake puts them before the authored
    range, like the river's own buildings, so the renderers give their walls facades and
    windows. Cranes, masts, marks and trees stay authored, in flat colours.
    """
    houses = Builder()
    builder = Builder()
    outside = land.difference(crop)
    stats, solids, port = add_buildings(houses, osm, frame, outside, model, fort,
                                         multipolygon)
    port_zone = [port.buffer(40)]
    cranes = masts = 0
    for n in sorted(osm["points"], key=lambda n: n["id"]):
        t = n["tags"]
        x, z = frame.xy([n["coord"]])[0]
        if not outside.contains(Point(x, z)):
            continue
        ground = float(_height(model, [(x, z)])[0])
        if t.get("man_made") == "crane" and "container" in t.get("note:de", "").lower() + \
                t.get("seamark:crane:category", ""):
            height = float(t.get("height", CONTAINER_CRANE_HEIGHT))
            container_crane(builder, x, z, ground, nearest_water_direction(water, x, z), height)
            port_zone.append(Point(x, z).buffer(60))
            cranes += 1
        elif t.get("man_made") == "tower" and t.get("tower:type") == "lighting":
            mast(builder, x, z, ground)
            port_zone.append(Point(x, z).buffer(60))
            masts += 1
    placed = 0
    for _, x, z, colour, height in marks(osm, frame, region, crop):
        inside = land.contains(Point(x, z))
        ground = float(_height(model, [(x, z)])[0]) if inside else 0.0
        mark(builder, x, z, ground, colour, height)
        placed += 1
    roads = []
    for w in osm["ways"]:
        if "highway" in w["tags"] and len(w["geometry"]) >= 2:
            roads.append(LineString(frame.xy(w["geometry"])).buffer(4, cap_style=2))
    keep_out = unary_union([solids.buffer(3), unary_union(roads), *port_zone])
    trees = add_trees(builder, outside, keep_out, model)
    stats.update(container_cranes=cranes, lighting_masts=masts, marks=placed, trees=trees,
                 building_triangles=len(houses.triangles), triangles=len(builder.triangles))
    return houses.triangles, builder.triangles, stats


def self_test():
    assert building_shape({"building": "detached"}, 120) == (HOUSE_WALL, "gable")
    assert building_shape({"building": "garage"}, 30) == (SMALL_WALL, "flat")
    assert building_shape({"building": "yes", "height": "21,5"}, 900) == (21.5, "flat")
    assert building_shape({"building": "apartments", "building:levels": "4"}, 600) == (13, "flat")
    # Every face of a gabled house and a beam looks away from its centre.
    b = Builder()
    gable(b, Polygon([(0, 0), (12, 0), (12, 8), (0, 8)]), 0, 5, (1, 1, 1), (2, 2, 2))
    b.beam((0, 0, 0), (0, 10, 0), 2, (1, 1, 1))
    assert len(b.triangles) == 14 + 12
    for points, _ in b.triangles[:14]:
        n = _cross(_sub(points[1], points[0]), _sub(points[2], points[0]))
        c = [sum(p[i] for p in points) / 3 for i in range(3)]
        assert sum(a * b for a, b in zip(n, _sub(c, (6, 2.5, 4)))) > 0, points
    # The roof is the only part above the eaves, and the ridge sits on the long axis.
    top = max(p[1] for points, _ in b.triangles[:14] for p in points)
    assert abs(top - (5 + 8 * .35)) < 1e-9
