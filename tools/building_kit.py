#!/usr/bin/env python3
"""Shared building descriptions and emission for the scenery bakes (docs/BUILDINGS.md).

Both bakes (prepare_waterfront.py on flat ground, outer_dressing.py on terrain) describe a
mapped building as a BuildingDescription and emit it through one Emitter, which writes the
triangles in the order the pack records name:

    walls of every section, then the roofs.

Walls come as the bake's pairs (a, c, b), (a, d, c) with a and b on the bottom edge, outward
for the exterior ring counter-clockwise and the courtyard rings clockwise (shapely orient
sign=1); roofs face up. Windows are not triangles: each wall that takes windows gets a facade
record (map_pack.py documents it) with its wall pair, bay and floor counts and a window style,
and the runtime generator (src/scenery/building_windows.c) expands them into quads near the
camera and the facade shader draws them beyond. A window's identity and look depend on the
building's stable id, its facade number (the wall's place in emission order, counting walls
without windows) and its bay and floor only: see the seed in building_windows.h.

Emitted records are the dicts tools/map_pack.py writes: id, first, end, detail_first, runs and
facades. A building without facade treatment (houses, stone) is all ROLE_DETAIL: drawn flat,
no windows. A wall buried in another section of the same building (a facade wall whose outside
is covered by a section as tall as itself) gets none either; party walls between separate
buildings are not known to the kit.

Migration. Only buildings whose base id is in MIGRATED get records and baked windows; every other
building is emitted exactly as before the kit (per section: walls, then roof; no records), so the
renderers keep classifying it by colour and geometry. The set grows with the plan (B2 pilot,
B3 rollout); an empty set reproduces the pre-kit packs byte for byte (apart from the version).
"""
import math
import re
from dataclasses import dataclass, field

from shapely.geometry import LineString
from shapely.geometry.polygon import orient
from shapely.ops import unary_union

import map_pack

ID_PATTERN = re.compile(r"^(way|relation)/[0-9]+(#[0-9]+)?$")
MIN_WINDOW_WALL = 5.5  # walls shorter than this get no windows
# Window style 0, the legacy grid: first bay 2.5 m in, a bay every 4 m; first floor 1.5 m up, a
# floor every 3.2 m; ground floor windows 2.2 x 1.9 m, upper floors 1.5 x 1.5 m; the glass stands
# 3.5 cm off its wall. (u0, pitch_u, y0, pitch_v, w0, h0, w1, h1, offset), see map_pack.py.
STYLES = [(2.5, 4.0, 1.5, 3.2, 2.2, 1.9, 1.5, 1.5, .035)]
WINDOW_END_MARGIN = .3  # clear wall between the last window and the corner, metres
WINDOW_TOP_MARGIN = .5  # and between the top window and the wall's top
OVERRIDE_KEYS = {"wall", "roof", "windows"}
# Source ids (without a "#piece" suffix) emitted with records and baked windows. B1 pilot, to
# exercise the pipeline: the Eriksberg quay row and its neighbours (river crop), and a few
# blocks and halls upstream (outer dressing, terrain adapter).
MIGRATED = frozenset(f"way/{n}" for n in (
    164963584, 164963867, 164963869, 164963897, 164964585, 303925538, 303925539, 282574762,
    115318537, 503087601, 503087593, 88426613, 1204511650, 1204511655, 1204511659, 1204511647))


def migrated(ident):
    return ident.partition("#")[0] in MIGRATED


@dataclass
class Section:
    """A prism of the building: SHAPE (X/Z polygon, holes allowed) from BOTTOM to TOP (metres).

    PART is the source building:part id when the section comes from one (provenance only)."""
    shape: object
    bottom: float
    top: float
    part: str = None


@dataclass
class BuildingDescription:
    id: str  # Stable source id: "way/ID" or "relation/ID", "#N" for pieces of a clipped footprint.
    sections: list
    wall: tuple
    roof: tuple
    facade: bool = True  # False: all detail, drawn flat (houses, stone).
    override: str = None  # Key into the overrides table given to the Emitter.
    migrate: bool = None  # None: by MIGRATED.
    extra: dict = field(default_factory=dict)  # Provenance; never read by the emitter.


def polygons(shape):
    if shape.is_empty:
        return []
    if shape.geom_type == "Polygon":
        return [shape]
    return [p for g in getattr(shape, "geoms", []) for p in polygons(g)]


def sort_key(ident):
    kind, number = ident.split("/")
    number, _, piece = number.partition("#")
    return (kind, int(number), int(piece or -1))


def _rgb(color, what):
    if len(color) != 3 or not all(isinstance(c, int) and 0 <= c <= 255 for c in color):
        raise ValueError(f"{what}: bad colour {color!r}")
    return tuple(color)


def validate(descriptions, overrides=None):
    """Raise ValueError unless DESCRIPTIONS are emittable. Checks ids (format, uniqueness),
    sections (finite heights, bottom < top, non-empty valid polygons), colours, and that every
    override reference names an entry of OVERRIDES whose keys are known."""
    overrides = overrides or {}
    seen = set()
    for key, value in overrides.items():
        if not isinstance(value, dict) or set(value) - OVERRIDE_KEYS:
            raise ValueError(f"override {key!r}: unknown keys {sorted(set(value) - OVERRIDE_KEYS)}")
    for d in descriptions:
        if not ID_PATTERN.match(d.id):
            raise ValueError(f"bad building id {d.id!r}")
        if d.id in seen:
            raise ValueError(f"duplicate building id {d.id}")
        seen.add(d.id)
        if not d.sections:
            raise ValueError(f"{d.id}: no sections")
        _rgb(d.wall, d.id)
        _rgb(d.roof, d.id)
        if d.override is not None and d.override not in overrides:
            raise ValueError(f"{d.id}: unknown override {d.override!r}")
        if not (migrated(d.id) if d.migrate is None else d.migrate):
            continue  # Legacy buildings are emitted as they always were.
        for s in d.sections:
            if not (math.isfinite(s.bottom) and math.isfinite(s.top) and s.bottom < s.top):
                raise ValueError(f"{d.id}: bad section heights {s.bottom}..{s.top}")
            parts = polygons(s.shape)
            if not parts or not s.shape.is_valid:
                raise ValueError(f"{d.id}: empty or invalid footprint")
            for p in parts:
                if p.area < 1e-6 or not all(math.isfinite(c) for xy in p.exterior.coords
                                            for c in xy):
                    raise ValueError(f"{d.id}: degenerate or non-finite footprint")


class Emitter:
    """Emit descriptions through TRIANGLE(points, colour) and COUNT() (the bake's sink)."""

    def __init__(self, triangle, count, triangulate, overrides=None):
        self.triangle, self.count, self.triangulate = triangle, count, triangulate
        self.overrides = overrides or {}

    def emit_all(self, descriptions):
        """Emit every description in stable-id order; returns the records of the migrated ones."""
        validate(descriptions, self.overrides)
        records = [self.emit(d) for d in sorted(descriptions, key=lambda d: sort_key(d.id))]
        return [r for r in records if r]

    def emit_legacy(self, d):
        """The pre-kit emission: per section its walls, then its roof; no record."""
        for s in d.sections:
            for poly in polygons(s.shape):
                poly = orient(poly, sign=1)
                for ring in [poly.exterior, *poly.interiors]:
                    pts = list(ring.coords)
                    for (x, z), (xx, zz) in zip(pts, pts[1:]):
                        if x == xx and z == zz:
                            continue
                        a, b = (x, s.bottom, z), (xx, s.bottom, zz)
                        c, e = (xx, s.top, zz), (x, s.top, z)
                        self.triangle([a, c, b], d.wall)
                        self.triangle([a, e, c], d.wall)
            for poly in polygons(s.shape):
                for t in self.triangulate(poly):
                    self.triangle([(x, s.top, z) for x, z in map_pack_clockwise(t)], d.roof)
        return None

    def emit(self, d):
        if not (migrated(d.id) if d.migrate is None else d.migrate):
            return self.emit_legacy(d)
        over = self.overrides.get(d.override, {})
        wall, roof = over.get("wall", d.wall), over.get("roof", d.roof)
        windows = d.facade and over.get("windows", True)
        first = self.count()
        edges = []  # (a, b, bottom, top, section) of each wall, in emission order
        for section, s in enumerate(d.sections):
            for poly in polygons(s.shape):
                poly = orient(poly, sign=1)
                for ring in [poly.exterior, *poly.interiors]:
                    pts = list(ring.coords)
                    for (x, z), (xx, zz) in zip(pts, pts[1:]):
                        if math.hypot(xx - x, zz - z) < 1e-6:
                            continue
                        a, b = (x, s.bottom, z), (xx, s.bottom, zz)
                        c, e = (xx, s.top, zz), (x, s.top, z)
                        self.triangle([a, c, b], wall)
                        self.triangle([a, e, c], wall)
                        edges.append((a, b, s.bottom, s.top, section))
        runs = []
        walls_end = self.count()
        if walls_end > first:
            runs.append((first, walls_end, map_pack.ROLE_WALL if d.facade else map_pack.ROLE_DETAIL,
                         map_pack.NO_FACADE, 0))
        for s in d.sections:
            for poly in polygons(s.shape):
                for t in self.triangulate(poly):
                    pts = [(x, s.top, z) for x, z in map_pack_clockwise(t)]
                    self.triangle(pts, roof)
        roof_end = self.count()
        if roof_end > walls_end:
            runs.append((walls_end, roof_end,
                         map_pack.ROLE_ROOF if d.facade else map_pack.ROLE_DETAIL,
                         map_pack.NO_FACADE, 0))
        facades = []
        if windows:
            for index, (a, b, bottom, top, section) in enumerate(edges):
                bays, floors = grid(math.hypot(b[0] - a[0], b[2] - a[2]), top - bottom)
                if bays and floors and not buried(d, section, a, b):
                    # Walls are the first triangles of the building, a pair each, in edge order.
                    facades.append({"wall": first + 2 * index, "index": index, "bays": bays,
                                    "floors": floors, "style": 0})
        return {"id": d.id, "first": first, "end": self.count(), "detail_first": roof_end,
                "runs": runs, "facades": facades}


def grid(length, height, style=STYLES[0]):
    """(bays, floors) of the window grid STYLE on a wall LENGTH metres long and HEIGHT high:
    every window fits inside the wall, clear of the corners and the top. Walls under
    MIN_WINDOW_WALL high get none."""
    u0, pitch_u, y0, pitch_v, w0, h0, w1, h1, _ = style
    if height < MIN_WINDOW_WALL:
        return 0, 0
    bays = 0
    while u0 + bays * pitch_u + max(w0, w1) <= length - WINDOW_END_MARGIN:
        bays += 1
    floors = 0
    while y0 + floors * pitch_v + (h0 if floors == 0 else h1) <= height - WINDOW_TOP_MARGIN:
        floors += 1
    return bays, floors


def buried(d, section, a, b):
    """True if the outside of wall A..B of SECTION of D lies inside another section of the same
    building that spans the wall's full height: such a wall is interior and takes no windows."""
    s = d.sections[section]
    covering = [o.shape for i, o in enumerate(d.sections)
                if i != section and o.bottom <= s.bottom + 1e-6 and o.top >= s.top - 1e-6]
    if not covering:
        return False
    length = math.hypot(b[0] - a[0], b[2] - a[2])
    nx, nz = (b[2] - a[2]) / length, -(b[0] - a[0]) / length  # outward, as the renderers derive it
    line = LineString([(a[0] + nx * .05, a[2] + nz * .05), (b[0] + nx * .05, b[2] + nz * .05)])
    return unary_union(covering).buffer(.02).contains(line)


def map_pack_clockwise(triangle):
    """The triangle's X/Z points clockwise (upward-facing), as the bake's surface()."""
    (x0, z0), (x1, z1), (x2, z2) = triangle
    cross = (x1 - x0) * (z2 - z0) - (x2 - x0) * (z1 - z0)
    return [(x0, z0), (x1, z1), (x2, z2)] if cross < 0 else [(x0, z0), (x2, z2), (x1, z1)]


def self_test():
    from shapely import constrained_delaunay_triangles
    from shapely.geometry import Polygon, box

    def tri(poly):
        return [list(t.exterior.coords)[:3] for t in constrained_delaunay_triangles(poly).geoms]

    def build(descriptions, overrides=None):
        out = []
        emitter = Emitter(lambda p, c: out.append((p, c)), lambda: len(out), tri, overrides)
        return out, emitter.emit_all(descriptions)

    def Desc(*args, **kw):
        return BuildingDescription(*args, migrate=kw.pop("migrate", True), **kw)

    court = Polygon([(0, 0), (40, 0), (40, 30), (0, 30)], [[(15, 10), (25, 10), (25, 20), (15, 20)]])
    a = Desc("way/7", [Section(court, 3.5, 19.5), Section(box(45, 0, 55, 8), 3.5, 9)],
                            (166, 173, 195), (136, 143, 165))
    b = Desc("relation/2", [Section(box(60, 0, 70, 10), 3.5, 6)], (1, 2, 3),
                            (4, 5, 6), facade=False)
    c = Desc("way/3#1", [Section(box(80, 0, 90, 10), 3.5, 10)], (5, 5, 5), (6, 6, 6))
    out, records = build([a, b, c])
    out2, records2 = build([c, a, b])  # input order is irrelevant
    assert out == out2 and records == records2
    assert [r["id"] for r in records] == ["way/3#1", "way/7", "relation/2"] or \
        [r["id"] for r in records] == sorted([r["id"] for r in records], key=sort_key)
    # Adding an unrelated building leaves the other buildings' records (and so their windows)
    # unchanged: identity is the id and the wall's place in the building, never a global index.
    extra = Desc("way/1", [Section(box(100, 0, 110, 10), 3.5, 12)], (5, 5, 5), (6, 6, 6))
    out3, records3 = build([a, b, c, extra])
    by = {r["id"]: r for r in records}
    by3 = {r["id"]: r for r in records3}
    rel = lambda r: [{**f, "wall": f["wall"] - r["first"]} for f in r["facades"]]
    assert rel(by["way/7"]) == rel(by3["way/7"]) and by["way/7"]["facades"]
    assert [r["id"] for r in records3] != [r["id"] for r in records]  # way/1 sorts in
    for r in records:
        pos = r["first"]
        for first, end, role, facade, heading in r["runs"]:
            assert first >= pos and end > first and end <= r["end"]
            pos = end
        assert r["detail_first"] == r["end"]  # no baked detail: windows are runtime data
    # No window triangles: a building is its walls then its roof, as before the kit.
    assert len(out) == sum(r["end"] - r["first"] for r in records)
    # Roles: one wall run, one roof run; houses are detail only.
    runs = by["way/7"]["runs"]
    assert [x[2] for x in runs] == [map_pack.ROLE_WALL, map_pack.ROLE_ROOF]
    assert [x[2] for x in by["relation/2"]["runs"]] == [map_pack.ROLE_DETAIL] * 2
    assert by["relation/2"]["facades"] == []
    # Facade records name wall pairs (a, c, b), (a, d, c) of the building and fit their wall.
    r = by["way/7"]
    seen = []
    for f in r["facades"]:
        t = out[f["wall"]][0]
        nxt = out[f["wall"] + 1][0]
        assert r["first"] <= f["wall"] < runs[0][1] and (f["wall"] - r["first"]) % 2 == 0
        assert f["index"] == (f["wall"] - r["first"]) // 2 and f["index"] not in seen
        seen.append(f["index"])
        assert t[0][1] == t[2][1] and t[1][1] > t[0][1] and nxt[0] == t[0] and nxt[2] == t[1]
        length = math.hypot(t[2][0] - t[0][0], t[2][2] - t[0][2])
        assert (f["bays"], f["floors"]) == grid(length, t[1][1] - t[0][1])
        assert f["bays"] >= 1 and f["floors"] >= 1 and f["style"] == 0
    # Four outer walls, four courtyard walls facing the yard and the wing's four.
    assert len(r["facades"]) == 4 + 4 + 4 and by["way/3#1"]["facades"]
    # Wall triangles are the bake's pairs facing away from the material (courtyard walls into
    # the yard); short walls take no windows.
    from shapely.geometry import Point
    material = court.union(box(45, 0, 55, 8))

    def outward(points):
        n = _normal(*points[:3])
        length = math.hypot(n[0], n[2])
        mid = [sum(p[i] for p in points[:3]) / 3 for i in (0, 2)]
        out_point = Point(mid[0] + n[0] / length * .2, mid[1] + n[2] / length * .2)
        in_point = Point(mid[0] - n[0] / length * .2, mid[1] - n[2] / length * .2)
        return not material.contains(out_point) and material.buffer(.3).contains(in_point)

    for i in range(r["first"], runs[0][1], 2):
        p0, p1, p2 = out[i][0]
        assert p0[1] == p2[1] and p1[1] > p0[1] and out[i + 1][0][0] == p0 and outward(out[i][0])
    for pts, _ in out[by["way/7"]["first"]:by["way/7"]["end"]]:
        assert all(math.isfinite(v) for p in pts for v in p)
    # The grid: windows fit inside the wall, clear of corners and top; short and low walls none.
    for length, height in [(5, 6), (9, 6), (40, 16), (4.9, 20), (20, 5.4), (20, 5.5)]:
        bays, floors = grid(length, height)
        u0, pu, y0, pv, w0, h0, w1, h1, _ = STYLES[0]
        if bays:
            assert u0 + (bays - 1) * pu + max(w0, w1) <= length - WINDOW_END_MARGIN + 1e-9
            assert u0 + bays * pu + max(w0, w1) > length - WINDOW_END_MARGIN
        if floors:
            assert y0 + (floors - 1) * pv + (h0 if floors == 1 else h1) \
                <= height - WINDOW_TOP_MARGIN + 1e-9
    assert grid(4.9, 20) == (0, 0) or grid(4.9, 20)[0] == 0
    assert grid(20, 5.4) == (0, 0) and grid(20, 5.5)[0] > 0 and grid(5, 6)[0] == 1
    # A wall buried in a taller or equal neighbouring section of the same building has none:
    # two blocks sharing the wall x = 10 (the lower block's wall is inside the tall one).
    tall = Section(box(0, 0, 10, 10), 3.5, 20)
    low = Section(box(10, 0, 20, 10), 3.5, 12)
    twin = Desc("way/20", [tall, low], (1, 1, 1), (2, 2, 2))
    out6, rec6 = build([twin])
    shared = [out6[f["wall"]][0] for f in rec6[0]["facades"]
              if out6[f["wall"]][0][0][0] == 10 == out6[f["wall"]][0][2][0]]
    assert len(rec6[0]["facades"]) == 7  # 8 walls: the low block's wall at x = 10 is buried
    assert len(shared) == 1 and shared[0][1][1] == 20  # only the tall block's wall keeps windows
    # A legacy building has no record and is walls then roof per section; migration is by id.
    old = BuildingDescription("way/50", [Section(box(0, 0, 10, 10), 3.5, 12)], (1, 1, 1), (2, 2, 2))
    assert not migrated("way/50") and migrated("way/164963584#2")
    out5, rec5 = build([old])
    assert rec5 == [] and len(out5) == 8 + 2 and out5[-1][1] == (2, 2, 2) and out5[0][1] == (1, 1, 1)
    # Overrides: a known reference applies, an unknown one is refused.
    d = Desc("way/9", [Section(box(0, 0, 10, 10), 3.5, 12)], (1, 1, 1), (2, 2, 2),
                            override="no-windows")
    out4, rec4 = build([d], {"no-windows": {"windows": False}})
    assert rec4[0]["detail_first"] == rec4[0]["end"] and rec4[0]["facades"] == []
    for bad in [Desc("way/9", [Section(box(0, 0, 10, 10), 3.5, 12)], (1, 1, 1),
                                    (2, 2, 2), override="missing"),
                Desc("bogus", [Section(box(0, 0, 10, 10), 3.5, 12)], (1, 1, 1), (2, 2, 2)),
                Desc("way/9", [Section(box(0, 0, 10, 10), 12, 3.5)], (1, 1, 1), (2, 2, 2)),
                Desc("way/9", [Section(box(0, 0, 10, 10), 3.5, float("nan"))],
                                    (1, 1, 1), (2, 2, 2)),
                Desc("way/9", [Section(Polygon(), 3.5, 5)], (1, 1, 1), (2, 2, 2)),
                Desc("way/9", [Section(Polygon([(0, 0), (1, 1), (1, 0), (0, 1)]), 3.5, 5)],
                                    (1, 1, 1), (2, 2, 2)),
                Desc("way/9", [Section(box(0, 0, 10, 10), 3.5, 5)], (1, 1, 300), (2, 2, 2)),
                Desc("way/9", [], (1, 1, 1), (2, 2, 2))]:
        try:
            validate([bad], {"no-windows": {"windows": False}})
        except ValueError:
            continue
        raise AssertionError(f"accepted {bad}")
    for dup in ([a, a], ):
        try:
            validate(dup)
        except ValueError:
            continue
        raise AssertionError("duplicate ids accepted")
    try:
        validate([a], {"x": {"colour": 1}})
    except ValueError:
        pass
    else:
        raise AssertionError("unknown override key accepted")
    # The same building and the same geometry always give the same pack bytes.
    import tempfile
    from pathlib import Path
    with tempfile.TemporaryDirectory() as d:
        packs = []
        for order in ([a, b, c], [c, b, a]):
            lookup, verts, tris = {}, [], []

            def sink(points, color):
                ids = []
                for p in points:
                    p = tuple(round(v, 4) for v in p)
                    ids.append(lookup.setdefault(p, len(lookup)))
                    if len(lookup) > len(verts):
                        verts.append(p)
                tris.append((ids, color))

            water = [[(0, 0, 0), (0, 0, 1), (1, 0, 0)]]
            sink(water[0], (1, 2, 3))
            records = Emitter(sink, lambda: len(tris), tri).emit_all(order)
            packs.append(map_pack.pack_bytes(verts, tris, len(tris), records, STYLES))
        assert packs[0] == packs[1]


def _normal(a, b, c):
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    return (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])


if __name__ == "__main__":
    self_test()
    print("building kit fixtures OK")
