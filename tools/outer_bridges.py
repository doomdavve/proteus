"""The canal bridges of the outer map (GH2, docs/GOTHENBURG_HARBOUR_PLAN.md).

Used by tools/prepare_outer_map.py. Each `man_made=bridge` outline in the canals becomes a deck
on its minimum rotated rectangle, and each `bridge=*` way that crosses canal water outside those
outlines becomes a deck along the way. They are scenery: the water stays continuous underneath and
boats do not touch them (GH8 owns low-bridge contact).

OSM has no heights and Höjdmodell removes bridges, so every height here is an estimate: the deck
top is the street level a few metres beyond the water at both ends (clamped to TOP_RANGE), the
underside is a fixed depth below it, and arch bridges (`bridge:structure=arch`) curve down
towards the quays. Stora Bommens Bro, the tour's lowest bridge (Swedish Wikipedia), gets a
lower underside by hand (CLEARANCE). Colours and railings are authored. Coordinates are
crane-origin metres, x = east, z = north, y = up.
"""
import math

import numpy as np
from shapely.geometry import LineString, Point, Polygon, box
from shapely.geometry.polygon import orient
from shapely.ops import unary_union

from outer_dressing import Builder

# The city canals: Vallgraven, Stora Hamnkanalen and Rosenlundskanalen, up to the map's east edge.
CANALS = box(2450.0, 250.0, 4200.0, 1180.0)
TOP_RANGE = (2.4, 3.6)   # deck top above the water, metres
BEAM_DEPTH = .55         # deck top to a beam bridge's flat underside
ARCH_DEPTH = .5          # deck top to an arch's crown
ARCH_SPRING = 1.3        # an arch's underside where it meets the quay, above the water
CLEARANCE = {"Stora Bommens Bro": 1.75}  # underside over the water, by hand (the duck bridge)
REACH = 4.0              # how far past the water's edge the street level is sampled
RAIL_HEIGHT = 1.0
POST_SPACING = 2.5
# Ways without an outline: OSM width when tagged, else by kind.
WAY_WIDTH = {"footway": 3.0, "path": 3.0, "cycleway": 3.0, "pedestrian": 6.0}
DEFAULT_WIDTH = 9.0
TOP = (118, 116, 112)       # asphalt and setts
FASCIA_BEAM = (96, 102, 108)
FASCIA_ARCH = (150, 140, 126)  # granite
UNDER = (66, 68, 72)
RAIL = (38, 46, 42)

ASSUMPTIONS = (
    "Canal bridges (GH2): outlines and ways are OSM; heights are estimates: deck top at the "
    "street level 4 m past the water at both ends (Höjdmodell, clamped to 2.4-3.6 m), beam "
    "underside .55 m below it, arches from a .5 m crown down to 1.3 m at the quays, and Stora "
    "Bommens Bro (Swedish Wikipedia: the lowest) by hand at 1.75 m. Colours, railings and "
    "default widths are authored.")


def _axes(rect):
    """Centre, the two unit axes and their half lengths of a rotated rectangle polygon."""
    pts = list(orient(rect, sign=1).exterior.coords)[:4]
    a, b, d = np.array(pts[0]), np.array(pts[1]), np.array(pts[3])
    centre = np.mean(np.array(pts), axis=0)
    u, v = b - a, d - a
    return centre, u / np.linalg.norm(u), v / np.linalg.norm(v), \
        np.linalg.norm(u) / 2, np.linalg.norm(v) / 2


def _wet_extent(water, centre, u, limit, v=None, offsets=(0.0,)):
    """The water under the deck along U (through CENTRE, within LIMIT): on each line offset along
    V, the run of water stations nearest the centre; the extent of those runs, or (None, None)."""
    lo, hi = None, None
    stations = np.arange(-limit, limit + .01, .5)
    for off in offsets:
        base = centre + (v * off if v is not None else 0)
        wet = [water.contains(Point(*(base + u * s))) for s in stations]
        runs, start = [], None
        for i, w in enumerate(wet + [False]):
            if w and start is None:
                start = i
            elif not w and start is not None:
                runs.append((stations[start], stations[i - 1]))
                start = None
        if not runs:
            continue
        a, b = min(runs, key=lambda r: 0 if r[0] <= 0 <= r[1] else min(abs(r[0]), abs(r[1])))
        lo = a if lo is None else min(lo, a)
        hi = b if hi is None else max(hi, b)
    return lo, hi


def layout(centre, u, v, half_u, half_v, water, model, name, arch):
    """A deck's span along U and width along V, with its heights, or None if it spans no water."""
    lo, hi = _wet_extent(water, centre, u, half_u + 30, v, (-half_v * .8, 0.0, half_v * .8))
    if lo is None:
        return None
    # A crossing spans less water than runs on under it; a walkway along a quay spans nothing.
    across, along = _wet_extent(water, centre + u * (lo + hi) / 2, v, 2 * (hi - lo) + 10)
    if across is None or along - across <= hi - lo:
        return None
    # The deck lands at least 1.5 m on each quay.
    s0, s1 = min(-half_u, lo - 1.5), max(half_u, hi + 1.5)
    ends = [centre + u * (lo - REACH), centre + u * (hi + REACH)]
    street = float(np.min(model(np.array(ends))))
    top = min(max(street, TOP_RANGE[0]), TOP_RANGE[1])
    under = top - (ARCH_DEPTH if arch else BEAM_DEPTH)
    if name in CLEARANCE:
        under = CLEARANCE[name]
        top = max(top, under + BEAM_DEPTH)
    return {"centre": centre, "u": u, "v": v, "span": (s0, s1), "wet": (lo, hi),
            "half_width": half_v, "top": top, "under": under, "arch": arch}


def underside(deck, s):
    """Height of the underside at station S: flat for a beam, a parabola for an arch."""
    if not deck["arch"]:
        return deck["under"]
    lo, hi = deck["wet"]
    mid, half = (lo + hi) / 2, max((hi - lo) / 2, 1.0)
    t = min(abs(s - mid) / half, 1.0)
    spring = min(ARCH_SPRING, deck["under"])
    return deck["under"] - (deck["under"] - spring) * t * t


def build(builder, deck):
    """Top, fascias, underside, ends and railings of one deck."""
    c, u, v = deck["centre"], deck["u"], deck["v"]
    s0, s1 = deck["span"]
    hw = deck["half_width"]
    top = deck["top"]

    def at(s, w, y):
        p = c + u * s + v * w
        return (float(p[0]), float(y), float(p[1]))

    stations = sorted({s0, s1, *np.arange(math.ceil(s0), s1, 1.0 if deck["arch"] else 4.0)})
    fascia = FASCIA_ARCH if deck["arch"] else FASCIA_BEAM
    uu, vv = (float(u[0]), 0.0, float(u[1])), (float(v[0]), 0.0, float(v[1]))
    for a, b in zip(stations, stations[1:]):
        ya, yb = underside(deck, a), underside(deck, b)
        builder.quad(at(a, -hw, top), at(b, -hw, top), at(b, hw, top), at(a, hw, top), TOP,
                     (0, 1, 0))
        builder.quad(at(a, -hw, ya), at(b, -hw, yb), at(b, hw, yb), at(a, hw, ya), UNDER,
                     (0, -1, 0))
        for side in (-1, 1):
            out = tuple(side * x for x in vv)
            builder.quad(at(a, side * hw, ya), at(b, side * hw, yb), at(b, side * hw, top),
                         at(a, side * hw, top), fascia, out)
    for s, sign in ((s0, -1), (s1, 1)):
        y = underside(deck, s)
        builder.quad(at(s, -hw, y), at(s, hw, y), at(s, hw, top), at(s, -hw, top), fascia,
                     tuple(sign * x for x in uu))
    # Railings along both edges over the water and a little beyond.
    lo, hi = deck["wet"]
    r0, r1 = max(s0, lo - 1.0), min(s1, hi + 1.0)
    for side in (-1, 1):
        w = side * (hw - .08)
        builder.beam(at(r0, w, top + RAIL_HEIGHT), at(r1, w, top + RAIL_HEIGHT), .08, RAIL)
        for s in np.arange(r0, r1 + .01, POST_SPACING):
            builder.beam(at(float(s), w, top), at(float(s), w, top + RAIL_HEIGHT), .07, RAIL)


def _outline_decks(osm, frame, water, model):
    decks = []
    for way in osm["ways"]:
        tags = way["tags"]
        if tags.get("man_made") != "bridge" or len(way["geometry"]) < 4:
            continue
        shape = Polygon(frame.xy(way["geometry"]))
        if not CANALS.contains(shape.centroid):
            continue
        centre, a, b, half_a, half_b = _axes(shape.minimum_rotated_rectangle)
        # The span axis is the one whose ends land: past its ends there is no water.
        def landed(axis, half):
            return sum(not water.contains(Point(*(centre + axis * s * (half + 6))))
                       for s in (-1, 1))
        if landed(a, half_a) >= landed(b, half_b):
            u, v, half_u, half_v = a, b, half_a, half_b
        else:
            u, v, half_u, half_v = b, a, half_b, half_a
        # Gangways out to the pontoons at Lilla Bommen end in the river: they are not crossings.
        if landed(u, half_u) < 2:
            continue
        name = tags.get("name", "?")
        deck = layout(centre, u, v, half_u, half_v, water, model, name,
                      tags.get("bridge:structure") == "arch")
        if deck:
            deck.update(name=name, osm=f"w/{way['id']}", shape=shape)
            decks.append(deck)
    return decks


def _way_decks(osm, frame, water, model, covered):
    decks = []
    for way in osm["ways"]:
        tags = way["tags"]
        if tags.get("bridge") in (None, "no") or "highway" not in tags and "railway" not in tags:
            continue
        line = LineString(frame.xy(way["geometry"]))
        if not CANALS.contains(line.centroid):
            continue
        wet = line.intersection(water)
        if wet.is_empty or wet.length < 3 or covered.contains(wet.centroid):
            continue
        # Only crossings land at both ends: walkways out onto piers span nothing.
        if any(water.contains(Point(p)) for p in (line.coords[0], line.coords[-1])):
            continue
        (x0, z0), (x1, z1) = line.coords[0], line.coords[-1]
        u = np.array([x1 - x0, z1 - z0])
        u /= np.linalg.norm(u)
        v = np.array([-u[1], u[0]])
        centre = np.array(wet.centroid.coords[0])
        try:
            width = float(str(tags.get("width", "")).split()[0])
        except (ValueError, IndexError):
            width = WAY_WIDTH.get(tags.get("highway"), DEFAULT_WIDTH)
        deck = layout(centre, u, v, line.length / 2, width / 2, water, model,
                      tags.get("name", "?"), False)
        # A sidewalk stub at a bridge end crosses only a corner of the water it is laid over.
        if deck and wet.length < .6 * (deck["wet"][1] - deck["wet"][0]):
            deck = None
        if deck:
            deck.update(name=tags.get("name", "?"), osm=f"w/{way['id']}",
                        shape=Polygon(line.buffer(width / 2, cap_style=2)))
            decks.append(deck)
            covered = covered.union(deck["shape"].buffer(1.0))
    return decks


def bridges(osm, frame, water, model):
    """(triangles, stats) for every canal bridge."""
    decks = _outline_decks(osm, frame, water, model)
    covered = unary_union([d["shape"].buffer(1.0) for d in decks])
    decks += _way_decks(osm, frame, water, model, covered)
    builder = Builder()
    table = []
    for deck in decks:
        before = len(builder.triangles)
        build(builder, deck)
        lo, hi = deck["wet"]
        table.append({"name": deck["name"], "osm": deck["osm"],
                      "structure": "arch" if deck["arch"] else "beam",
                      "over_water_m": round(float(hi - lo), 1),
                      "width_m": round(float(2 * deck["half_width"]), 1),
                      "top_m": round(float(deck["top"]), 2),
                      "clearance_m": round(float(deck["under"]), 2),
                      "triangles": len(builder.triangles) - before})
    stats = {"count": len(decks), "triangles": len(builder.triangles), "bridges": table,
             "assumptions": ASSUMPTIONS}
    return builder.triangles, stats, c_table(decks)


def c_table(decks):
    """The decks as WaterfrontBridge initialisers for the compiled water header: renderer X/Z
    (east, north) centre and span axis, the span and its water stations, the half width, the
    underside over the middle of the water and at the quays (equal for a beam), and the top."""
    lines = ["// GH2: the canal bridge decks (tools/outer_bridges.py) for the camera and the tour.",
             "static const WaterfrontBridge outer_bridges[] = {"]
    for d in decks:
        c, u = d["centre"], d["u"]
        spring = min(ARCH_SPRING, d["under"]) if d["arch"] else d["under"]
        values = [c[0], c[1], u[0], u[1], *d["span"], *d["wet"], d["half_width"], d["under"],
                  spring, d["top"]]
        lines.append("    { " + ", ".join(f"{float(x):.3f}f" for x in values)
                     + f" }}, // {d['name']} ({d['osm']})")
    return lines + ["};", ""]


def self_test():
    # A straight 20 m canal along X with quays at 3 m: a beam deck across it spans the water,
    # lands on both quays, sits at the street level and keeps a flat underside; an arch springs
    # lower at the quays than at its crown.
    class Quay:
        def __call__(self, xy):
            return np.full(len(np.asarray(xy).reshape(-1, 2)), 3.0)

    water = box(-100, -10, 100, 10)
    centre, u, v = np.array([0.0, 0.0]), np.array([0.0, 1.0]), np.array([1.0, 0.0])
    deck = layout(centre, u, v, 9.0, 4.0, water, Quay(), "test", False)
    assert deck["wet"][0] <= -9.5 and deck["wet"][1] >= 9.5, deck["wet"]
    assert deck["span"][0] < -10 and deck["span"][1] > 10, deck["span"]
    assert abs(deck["top"] - 3.0) < 1e-9 and abs(deck["under"] - (3.0 - BEAM_DEPTH)) < 1e-9
    arch = layout(centre, u, v, 9.0, 4.0, water, Quay(), "test", True)
    assert underside(arch, 0.0) > underside(arch, arch["wet"][1]) >= ARCH_SPRING - 1e-9
    low = layout(centre, u, v, 9.0, 4.0, water, Quay(), "Stora Bommens Bro", False)
    assert low["under"] == CLEARANCE["Stora Bommens Bro"]
    b = Builder()
    build(b, deck)
    ys = [p[1] for points, _ in b.triangles for p in points]
    assert min(ys) >= deck["under"] - 1e-6 and max(ys) <= deck["top"] + RAIL_HEIGHT + .1
    # Nothing to span: a deck wholly on land is dropped.
    assert layout(np.array([0.0, 50.0]), u, v, 5.0, 3.0, water, Quay(), "dry", False) is None


if __name__ == "__main__":
    self_test()
    print("outer bridges OK")
