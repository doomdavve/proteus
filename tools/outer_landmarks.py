"""Karlatornet, Lilla Bommen and Hisingsbron for the outer map (A3.5g).

Used by tools/prepare_outer_map.py. Hand-built blockouts at their mapped positions, on the
baked terrain: footprints and the bridge's line come from the pinned OSM corridor, heights from
its tags and the plan's facts; twist, banding, colours, tower and pier sizes are authored
assumptions (ASSUMPTIONS, recorded in the manifest). The OSM footprints of the two towers are
left out of the ordinary building extrusion (SKIP). Coordinates are crane-origin metres,
x = east, z = north, y = up.
"""
import math

import numpy as np
from shapely import transform
from shapely.geometry import LineString, Point, Polygon
from shapely.geometry.polygon import orient
from shapely.ops import unary_union

from outer_dressing import Builder

KARLATORNET = "r/17090257"
LILLA_BOMMEN = "w/175648796"
SKIP = {KARLATORNET, LILLA_BOMMEN}
# Hisingsbron's two carriageways on the bridge (layer 1, bridge=yes): their ends average to the
# deck's axis, north end first.
HISINGSBRON_WAYS = (480082401, 1363879305, 1363879304, 480082404)

KARLA_HEIGHT = 246.0
KARLA_FLOORS = 73
KARLA_TWIST_FROM = 70.0        # metres; the tower turns above this
KARLA_TWIST = math.radians(24)  # at the top
KARLA_BODY = (214, 209, 198)
KARLA_GLASS = (70, 86, 102)
KARLA_HOTEL = (122, 88, 66)
KARLA_HOTEL_FLOORS = 15

BOMMEN_HEIGHT = 86.0
BOMMEN_FLOORS = 22
BOMMEN_RED = (178, 44, 40)
BOMMEN_WHITE = (232, 228, 220)
BOMMEN_DECK = (58, 70, 82)

DECK_WIDTH = 40.0
DECK_CLEARANCE = 12.0   # underside over the channel
DECK_DEPTH = 1.6
DECK_TOP = (96, 100, 108)
DECK_SIDE = (206, 210, 214)
DECK_UNDER = (120, 124, 130)
LIFT_SPAN = 90.0        # the lift span's length along the axis, centred on the channel
LIFT_TOWER = 44.0       # tower height above the water
TOWER = (232, 234, 236)
TOWER_SIZE = (4.0, 6.0)  # along the axis, across it
PIER = (150, 152, 150)
PIER_SIZE = (3.0, 7.0)
PIER_SPACING = 50.0
LAMP = (70, 74, 80)

ASSUMPTIONS = (
    "Footprints, the bridge line and the towers' heights are OSM (Karlatornet 246 m, Lilla "
    "Bommen 86 m) or Swedish Wikipedia (Hisingsbron: 12 m clearance, 13 m deck, 28 m lifted). "
    "Authored by eye: Karlatornet's pale facade with dark glass bands, a 24 degree turn of its "
    "upper floors above 70 m, and a brown 15-floor hotel wing on its north-east side; Lilla "
    "Bommen's red and white floor bands, dark observation deck and red cap, with no low-rise wing; "
    "Hisingsbron's 40 m deck, 90 m lift span on the channel, four 44 m white lift towers, "
    "piers every 50 m (carved out of the water) and lamp posts. Colours are authored.")


def _shape(osm, frame, multipolygon, key):
    kind, number = key.split("/")
    number = int(number)
    if kind == "w":
        way = next(w for w in osm["ways"] if w["id"] == number)
        return Polygon(frame.xy(way["geometry"]))
    ways = {w["id"]: [tuple(p) for p in w["geometry"]] for w in osm["ways"]}
    relation = next(r for r in osm["relations"] if r["id"] == number)
    shape, problems = multipolygon(relation["members"], ways)
    assert not problems, (key, problems)
    return transform(shape, lambda c: frame.array(c))


def _rect(shape):
    """Centre, half lengths and the long axis angle of SHAPE's minimum rotated rectangle."""
    pts = list(orient(shape.minimum_rotated_rectangle, sign=1).exterior.coords)[:4]
    a, b, c = pts[0], pts[1], pts[2]
    ab, bc = math.dist(a, b), math.dist(b, c)
    centre = (sum(p[0] for p in pts) / 4, sum(p[1] for p in pts) / 4)
    if ab >= bc:
        return centre, ab / 2, bc / 2, math.atan2(b[1] - a[1], b[0] - a[0])
    return centre, bc / 2, ab / 2, math.atan2(c[1] - b[1], c[0] - b[0])


def _box_ring(centre, half_u, half_v, angle):
    cx, cz = centre
    c, s = math.cos(angle), math.sin(angle)
    return [(cx + u * c - v * s, cz + u * s + v * c)
            for u, v in ((-half_u, -half_v), (half_u, -half_v), (half_u, half_v),
                         (-half_u, half_v), (-half_u, -half_v))]


def _ground(model, ring):
    return float(np.min(model(np.asarray(ring, dtype=float))))


def karlatornet(builder, shape, model):
    """The tower as floor groups turning above KARLA_TWIST_FROM, and its hotel wing."""
    centre, half_u, half_v, angle = _rect(shape)
    base = _ground(model, _box_ring(centre, half_u, half_v, angle)) - .6
    storey = KARLA_HEIGHT / KARLA_FLOORS
    group = 3  # floors per slab: a pale spandrel band and a dark glass band
    y = base
    floor = 0
    while floor < KARLA_FLOORS:
        n = min(group, KARLA_FLOORS - floor)
        top = y + n * storey
        mid = (y + top) / 2 - base
        turn = KARLA_TWIST * max(0.0, (mid - KARLA_TWIST_FROM) / (KARLA_HEIGHT - KARLA_TWIST_FROM))
        # The crown narrows a little over the last 30 m.
        shrink = 1 - .12 * max(0.0, (mid - (KARLA_HEIGHT - 30)) / 30)
        ring = _box_ring(centre, half_u * shrink, half_v * shrink, angle + turn)
        split = y + n * storey * .35
        builder.prism(ring, y, split, KARLA_BODY)
        builder.prism(ring, split, top, KARLA_GLASS,
                      roof=KARLA_BODY if floor + n >= KARLA_FLOORS else None)
        y = top
        floor += n
    # A thin crown mast with the aircraft warning light's housing.
    builder.beam((centre[0], y, centre[1]), (centre[0], y + 8, centre[1]), .6, KARLA_BODY)
    # Hotel wing: on the long side away from the river (north-east), 15 floors.
    nx, nz = -math.sin(angle), math.cos(angle)
    if nz < 0:
        nx, nz = -nx, -nz
    hotel_centre = (centre[0] + nx * (half_v + 9), centre[1] + nz * (half_v + 9))
    ring = _box_ring(hotel_centre, half_u * .9, 9, angle)
    hotel_base = _ground(model, ring) - .6
    builder.prism(ring, hotel_base, base + .6 + KARLA_HOTEL_FLOORS * 3.3, KARLA_HOTEL,
                  roof=tuple(c - 30 for c in KARLA_HOTEL))
    return y + 8


def lilla_bommen(builder, shape, model):
    """The tower in red and white floor bands, its observation deck and the red cap."""
    ring = list(orient(shape.simplify(.5), sign=1).exterior.coords)
    base = _ground(model, ring) - .6
    body = BOMMEN_HEIGHT - 9
    storey = body / BOMMEN_FLOORS
    for floor in range(BOMMEN_FLOORS):
        y = base + .6 + floor * storey
        builder.prism(ring, y - (.6 if floor == 0 else 0), y + storey * .4, BOMMEN_RED)
        builder.prism(ring, y + storey * .4, y + storey, BOMMEN_WHITE)
    top = base + .6 + body
    inset = shape.simplify(.5).buffer(-2.5, join_style=2)
    deck = list(orient(inset, sign=1).exterior.coords)
    builder.prism(ring, top, top + .8, BOMMEN_WHITE, roof=BOMMEN_WHITE)
    builder.prism(deck, top + .8, top + 4.5, BOMMEN_DECK)
    cap = list(orient(inset.buffer(-2, join_style=2), sign=1).exterior.coords)
    builder.prism(deck, top + 4.5, top + 6, BOMMEN_RED, roof=BOMMEN_RED)
    builder.prism(cap, top + 6, base + .6 + BOMMEN_HEIGHT, BOMMEN_RED, roof=BOMMEN_WHITE)
    return base + .6 + BOMMEN_HEIGHT


def hisingsbron_axis(osm, frame):
    """(north end, south end) of the deck axis, from the carriageways' end points."""
    ends = []
    for number in HISINGSBRON_WAYS:
        way = next(w for w in osm["ways"] if w["id"] == number)
        ends.append(frame.xy([way["geometry"][0], way["geometry"][-1]]))
    points = [p for pair in ends for p in pair]
    # The northernmost and southernmost carriageway ends, averaged across the two directions.
    points.sort(key=lambda p: p[1])
    south = np.mean(points[:2], axis=0)
    north = np.mean(points[-2:], axis=0)
    return tuple(north), tuple(south)


def hisingsbron_layout(axis, water):
    """Stations along the axis (metres from the north end) for the lift span and the piers.

    Returns {"length", "span": (s0, s1), "towers": [s...], "piers": [s...], "wet": (s0, s1)}.
    The lift span is centred on the stretch of the axis over water.
    """
    line = LineString(axis)
    wet = line.intersection(water)
    stations = [line.project(Point(p)) for g in getattr(wet, "geoms", [wet])
                for p in g.coords] if not wet.is_empty else []
    assert stations, "Hisingsbron's axis does not cross the water"
    w0, w1 = min(stations), max(stations)
    middle = (w0 + w1) / 2
    span = (middle - LIFT_SPAN / 2, middle + LIFT_SPAN / 2)
    piers = []
    s = span[0] - PIER_SPACING
    while s > 20:
        piers.append(s)
        s -= PIER_SPACING
    s = span[1] + PIER_SPACING
    while s < line.length - 20:
        piers.append(s)
        s += PIER_SPACING
    return {"length": line.length, "span": span, "towers": [span[0], span[1]],
            "piers": sorted(piers), "wet": (w0, w1)}


def _frame(axis):
    (x0, z0), (x1, z1) = axis
    length = math.dist(axis[0], axis[1])
    ux, uz = (x1 - x0) / length, (z1 - z0) / length
    return (x0, z0), (ux, uz), (-uz, ux)


def _at(axis, s, v):
    (x0, z0), (ux, uz), (vx, vz) = _frame(axis)
    return x0 + ux * s + vx * v, z0 + uz * s + vz * v


def hisingsbron_obstacles(axis, layout):
    """Footprints of the towers and piers (shapely polygons), to carve out of the water."""
    (_, _), (ux, uz), _ = _frame(axis)
    angle = math.atan2(uz, ux)
    out = []
    for s in layout["towers"]:
        for v in (-DECK_WIDTH / 2 - TOWER_SIZE[1] / 2, DECK_WIDTH / 2 + TOWER_SIZE[1] / 2):
            out.append(Polygon(_box_ring(_at(axis, s, v), TOWER_SIZE[0] / 2 + .5,
                                         TOWER_SIZE[1] / 2 + .5, angle)))
    for s in layout["piers"]:
        for v in (-DECK_WIDTH / 4, DECK_WIDTH / 4):
            out.append(Polygon(_box_ring(_at(axis, s, v), PIER_SIZE[0] / 2, PIER_SIZE[1] / 2,
                                         angle)))
    return out


def deck_profile(layout, ground_at, s):
    """Top of the deck at station S: level over the channel, ramping to the ground at the ends."""
    level = DECK_CLEARANCE + DECK_DEPTH
    w0, w1 = layout["wet"]
    length = layout["length"]
    if s < w0 - 30:
        t = s / max(1.0, w0 - 30)
        return ground_at(0.0) + .4 + (level - ground_at(0.0) - .4) * t
    if s > w1 + 30:
        t = (length - s) / max(1.0, length - (w1 + 30))
        return ground_at(length) + .4 + (level - ground_at(length) - .4) * t
    return level


def hisingsbron(builder, axis, layout, model, lift_span=True):
    """The deck (with the lift span when LIFT_SPAN), towers, piers and lamp posts."""
    (x0, z0), (ux, uz), (vx, vz) = _frame(axis)
    angle = math.atan2(uz, ux)
    length = layout["length"]

    def ground_at(s):
        return float(model(np.array([_at(axis, s, 0)]))[0])

    stations = sorted({*np.arange(0, length, 10.0).tolist(), length, *layout["span"]})
    half = DECK_WIDTH / 2
    for a, b in zip(stations, stations[1:]):
        if not lift_span and layout["span"][0] <= (a + b) / 2 <= layout["span"][1]:
            continue
        ta, tb = deck_profile(layout, ground_at, a), deck_profile(layout, ground_at, b)
        pa = [_at(axis, a, -half), _at(axis, a, half)]
        pb = [_at(axis, b, -half), _at(axis, b, half)]
        top = [(pa[0][0], ta, pa[0][1]), (pb[0][0], tb, pb[0][1]),
               (pb[1][0], tb, pb[1][1]), (pa[1][0], ta, pa[1][1])]
        bottom = [(x, y - DECK_DEPTH, z) for x, y, z in top]
        builder.quad(*top, DECK_TOP, (0, 1, 0))
        builder.quad(*bottom, DECK_UNDER, (0, -1, 0))
        builder.quad(bottom[0], bottom[1], top[1], top[0], DECK_SIDE, (-vx, 0, -vz))
        builder.quad(bottom[3], bottom[2], top[2], top[3], DECK_SIDE, (vx, 0, vz))
    for s in (0.0, length):
        t = deck_profile(layout, ground_at, s)
        p = [_at(axis, s, -half), _at(axis, s, half)]
        out = (-ux, 0, -uz) if s == 0 else (ux, 0, uz)
        builder.quad((p[0][0], t - DECK_DEPTH, p[0][1]), (p[1][0], t - DECK_DEPTH, p[1][1]),
                     (p[1][0], t, p[1][1]), (p[0][0], t, p[0][1]), DECK_SIDE, out)
    for s in layout["towers"]:
        for v in (-half - TOWER_SIZE[1] / 2, half + TOWER_SIZE[1] / 2):
            ring = _box_ring(_at(axis, s, v), TOWER_SIZE[0] / 2, TOWER_SIZE[1] / 2, angle)
            builder.prism(ring, -2.0, LIFT_TOWER, TOWER, roof=TOWER)
            # The machinery house on top, a step wider across the deck.
            ring = _box_ring(_at(axis, s, v), TOWER_SIZE[0] / 2 + .6, TOWER_SIZE[1] / 2 + .6,
                             angle)
            builder.prism(ring, LIFT_TOWER - 4, LIFT_TOWER, TOWER, roof=(200, 204, 208))
    for s in layout["piers"]:
        t = deck_profile(layout, ground_at, s) - DECK_DEPTH
        for v in (-DECK_WIDTH / 4, DECK_WIDTH / 4):
            x, z = _at(axis, s, v)
            ring = _box_ring((x, z), PIER_SIZE[0] / 2, PIER_SIZE[1] / 2, angle)
            builder.prism(ring, min(-2.0, ground_at(s) - 1), t, PIER)
    for s in np.arange(15.0, length - 10, 30.0):
        t = deck_profile(layout, ground_at, float(s))
        if layout["span"][0] - 4 < s < layout["span"][1] + 4:
            continue
        for v in (-half + .6, half - .6):
            x, z = _at(axis, float(s), v)
            builder.beam((x, t, z), (x, t + 8, z), .35, LAMP)
            builder.beam((x, t + 7.8, z), (x - vx * .9 * np.sign(v), t + 8.1,
                                           z - vz * .9 * np.sign(v)), .35, LAMP)


def landmarks(osm, frame, multipolygon, water, model):
    """(triangles, obstacles, stats). OBSTACLES are footprints to carve out of the water."""
    builder = Builder()
    karla_top = karlatornet(builder, _shape(osm, frame, multipolygon, KARLATORNET), model)
    karla = len(builder.triangles)
    bommen_top = lilla_bommen(builder, _shape(osm, frame, multipolygon, LILLA_BOMMEN), model)
    bommen = len(builder.triangles) - karla
    axis = hisingsbron_axis(osm, frame)
    layout = hisingsbron_layout(axis, water)
    hisingsbron(builder, axis, layout, model)
    obstacles = hisingsbron_obstacles(axis, layout)
    stats = {
        "karlatornet": {"triangles": karla, "top_m": round(karla_top, 1)},
        "lilla_bommen": {"triangles": bommen, "top_m": round(bommen_top, 1)},
        "hisingsbron": {"triangles": len(builder.triangles) - karla - bommen,
                        "axis_north": [round(v, 2) for v in axis[0]],
                        "axis_south": [round(v, 2) for v in axis[1]],
                        "length_m": round(layout["length"], 1),
                        "lift_span_m": [round(v, 1) for v in layout["span"]],
                        "over_water_m": [round(v, 1) for v in layout["wet"]],
                        "piers": len(layout["piers"]), "clearance_m": DECK_CLEARANCE},
        "assumptions": ASSUMPTIONS,
    }
    return builder.triangles, unary_union(obstacles), stats


def self_test():
    # A level model and a straight channel: the span is centred on the water, piers stay clear
    # of it, and the deck is level over the channel and ramps down at both ends.
    class Flat:
        def __call__(self, xy):
            return np.full(len(np.asarray(xy).reshape(-1, 2)), 4.0)

    axis = ((0.0, 400.0), (0.0, 0.0))
    water = Polygon([(-500, 150), (500, 150), (500, 300), (-500, 300)])
    layout = hisingsbron_layout(axis, water)
    assert abs(sum(layout["span"]) / 2 - 175) < 1e-6, layout
    assert all(not (layout["span"][0] - 1 < s < layout["span"][1] + 1) for s in layout["piers"])

    def ground_at(s):
        return 4.0

    assert deck_profile(layout, ground_at, 175) == DECK_CLEARANCE + DECK_DEPTH
    assert abs(deck_profile(layout, ground_at, 0) - 4.4) < 1e-9
    assert abs(deck_profile(layout, ground_at, 400) - 4.4) < 1e-9
    b = Builder()
    hisingsbron(b, axis, layout, Flat(), lift_span=False)
    spans = [p for points, _ in b.triangles for p in points
             if layout["span"][0] + 1 < 400 - p[2] < layout["span"][1] - 1 and abs(p[0]) < 15]
    assert not spans, "deck triangles inside the open lift span"
    holes = hisingsbron_obstacles(axis, layout)
    assert len(holes) == 4 + 2 * len(layout["piers"])
    # The towers turn above KARLA_TWIST_FROM and keep their footprint below it.
    b = Builder()
    top = karlatornet(b, Polygon([(0, 0), (30, 0), (30, 30), (0, 30)]), Flat())
    assert abs(top - (4 - .6 + KARLA_HEIGHT + 8)) < 1e-6, top
    low = [p for points, _ in b.triangles for p in points if p[1] < 4 + 60]
    assert all(-1e-6 <= p[0] <= 30 + 1e-6 or p[2] > 30 for p in low)
