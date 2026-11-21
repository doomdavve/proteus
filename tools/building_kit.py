#!/usr/bin/env python3
"""Shared building descriptions and emission for the scenery bakes (docs/BUILDINGS.md).

Both bakes (prepare_waterfront.py on flat ground, outer_dressing.py on terrain) describe a
mapped building as a BuildingDescription and emit it through one Emitter, which writes the
triangles in the order the pack records name:

    wall rectangles of every section, then the roofs, then attached detail.

Walls are the exposed rectangles of each section's wall edges (see Emitter.wall_rects): the part
of a wall buried in another section of the building or in a neighbouring building is not emitted.
A rectangle comes as the bake's pair (a, c, b), (a, d, c) with a and b on the bottom edge, outward
for the exterior ring counter-clockwise and the courtyard rings clockwise (shapely orient sign=1);
roofs face up. Windows are not triangles: each rectangle that takes windows gets a facade
record (map_pack.py documents it) with its wall pair, bay and floor counts and a window style,
and the runtime generator (src/scenery/building_windows.c) expands them into quads near the
camera and the facade shader draws them beyond. A window's identity and look depend on the
building's stable id, its facade number (the rectangle's place in emission order, counting
rectangles without windows) and its bay and floor only: see the seed in building_windows.h.

Emitted records are the dicts tools/map_pack.py writes: id, first, end, detail_first, runs and
facades. A building without facade treatment (houses, stone) is all ROLE_DETAIL: drawn flat,
no windows.

Migration. Only buildings whose base id is in MIGRATED get records; every other building is
emitted exactly as before the kit (per section: walls, then roof; no records), so the
renderers keep classifying it by colour and geometry. The set grows with the plan (B2 pilot,
B3 rollout); an empty set reproduces the pre-kit packs byte for byte (apart from the version).

Composition. A migrated building with an entry in tools/building_overrides.json (keyed by its
stable id) is built by tools/building_compose.py's primitives: setbacks, ground band, roof edges,
balconies and an entrance. Without an entry it is the plain fallback: its mapped sections, one wall
role and the roof, plus windows on its exposed walls.
"""
import json
import math
import re
from dataclasses import dataclass, field
from pathlib import Path

from shapely import STRtree
from shapely.geometry import LineString, Point
from shapely.geometry.polygon import orient
from shapely.ops import unary_union

import building_compose
import map_pack

ROOT = Path(__file__).resolve().parents[1]
OVERRIDE_FILE = ROOT / "tools/building_overrides.json"
ID_PATTERN = re.compile(r"^(way|relation)/[0-9]+(#[0-9]+)?$")
MIN_WINDOW_WALL = 5.5  # fallback walls shorter than this get no windows
MIN_PIECE = .9  # exposed wall pieces narrower or lower than this are dropped
# Window styles: (u0, pitch_u, y0, pitch_v, w0, h0, w1, h1, offset), see map_pack.py. First bay u0
# in from the wall start, a bay every pitch_u; first floor y0 above the wall's base, a floor every
# pitch_v; ground floor windows w0 x h0, upper w1 x h1; the glass stands offset off the wall.
STYLE_LEGACY, STYLE_BALCONY, STYLE_RIBBON, STYLE_PUNCHED, STYLE_SHOP, STYLE_LOBBY = range(6)
STYLES = [
    (2.5, 4.0, 1.5, 3.2, 2.2, 1.9, 1.5, 1.5, .035),  # the legacy grid (fallback buildings)
    (1.5, 4.0, 1.0, 3.2, 1.7, 1.9, 1.7, 1.9, .035),  # balcony block: doors and windows on a 4 m bay
    (1.0, 4.0, 1.0, 3.2, 2.6, 1.5, 2.6, 1.5, .035),  # ribbon: wide glazing, narrow piers
    (1.4, 3.0, 1.0, 3.2, 1.2, 1.8, 1.2, 1.8, .035),  # punched: narrow, tall, close together
    (1.4, 4.4, .35, 3.2, 3.2, 2.9, 3.2, 2.9, .035),  # shopfront: broad glazing in a ground band
    (1.2, 3.4, .35, 3.2, 2.6, 3.0, 2.6, 3.0, .035),  # lobby/ground glazing
]
WINDOW_END_MARGIN = .3  # clear wall between the last window and the corner, metres
WINDOW_TOP_MARGIN = .5  # and between the top window and the wall's top
OVERRIDE_KEYS = {"wall", "roof", "windows"}
# Source ids (without a "#piece" suffix) emitted with records. B1 pilot, to exercise the pipeline:
# the Eriksberg quay row and its neighbours (river crop), and a few blocks and halls upstream
# (outer dressing, terrain adapter). B2 adds the compositions' neighbours.
MIGRATED = frozenset(f"way/{n}" for n in (
    164963584, 164963867, 164963869, 164963897, 164964585, 303925538, 303925539, 282574762,
    115318537, 503087601, 503087593, 88426613, 1204511650, 1204511655, 1204511659, 1204511647,
    164963868, 164964582))
CONTEXT_TOLERANCE = .3  # a neighbour's wall this close to ours counts as a shared wall
ROLE_FOR_MATERIAL = {"plaster": map_pack.ROLE_WALL_PLASTER, "brick": map_pack.ROLE_WALL_BRICK,
                     "panel": map_pack.ROLE_WALL_PANEL}


def migrated(ident):
    return ident.partition("#")[0] in MIGRATED


def base_id(ident):
    return ident.partition("#")[0]


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


polygons = building_compose.polygons
STYLE_NAMES = building_compose.STYLE_NAMES


def sort_key(ident):
    kind, number = ident.split("/")
    number, _, piece = number.partition("#")
    return (kind, int(number), int(piece or -1))


def _rgb(color, what):
    if len(color) != 3 or not all(isinstance(c, int) and 0 <= c <= 255 for c in color):
        raise ValueError(f"{what}: bad colour {color!r}")
    return tuple(color)


def load_compositions(path=OVERRIDE_FILE, map_name="river"):
    """The validated compositions of MAP_NAME from the override file PATH: {id: params}."""
    table = json.loads(Path(path).read_text())
    return validate_compositions(table, map_name)


def validate_compositions(table, map_name):
    if not isinstance(table, dict):
        raise ValueError("overrides: expected an object keyed by source id")
    out = {}
    for ident, params in table.items():
        if ident.startswith("_"):
            continue  # comments
        if not re.match(r"^(way|relation)/[0-9]+$", ident):
            raise ValueError(f"overrides: {ident!r} is not a source id (way/N or relation/N)")
        if not isinstance(params, dict):
            raise ValueError(f"{ident}: expected an object")
        checked = building_compose.validate_params(ident, params)
        if checked["map"] == map_name:
            out[ident] = checked
    return out


def validate(descriptions, overrides=None, compositions=None):
    """Raise ValueError unless DESCRIPTIONS are emittable. Checks ids (format, uniqueness),
    sections (finite heights, bottom < top, non-empty valid polygons), colours, that every
    override reference names an entry of OVERRIDES whose keys are known, and that every composition
    names a building of DESCRIPTIONS that is migrated."""
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
    bases = {base_id(d.id): d for d in descriptions}
    for ident in compositions or {}:
        d = bases.get(ident)
        if d is None:
            raise ValueError(f"composition {ident}: no such building in this bake")
        if not (migrated(d.id) if d.migrate is None else d.migrate):
            raise ValueError(f"composition {ident}: the building is not migrated")
        if not d.facade:
            raise ValueError(f"composition {ident}: the building takes no facade treatment")
        if any(base_id(o.id) == ident and o is not d for o in descriptions):
            raise ValueError(f"composition {ident}: the building was split into pieces")


class Emitter:
    """Emit descriptions through TRIANGLE(points, colour) and COUNT() (the bake's sink).

    COMPOSITIONS ({base id: params}, see load_compositions) author individual buildings.
    OBSTACLES is geometry (water, streets) that attached architecture must not project into.
    Neighbouring buildings come from the descriptions given to emit_all (or CONTEXT, a list of
    (id, shape, bottom, top) given to emit for one building at a time)."""

    def __init__(self, triangle, count, triangulate, overrides=None, compositions=None,
                 obstacles=None, context=None):
        self.triangle, self.count, self.triangulate = triangle, count, triangulate
        self.overrides = overrides or {}
        self.compositions = compositions or {}
        self.obstacles = obstacles if obstacles is not None else None
        self.context = list(context or [])
        self._tree = None
        self.report = {}  # id -> counts and rejected features, for the manifest

    def emit_all(self, descriptions):
        """Emit every description in stable-id order; returns the records of the migrated ones."""
        validate(descriptions, self.overrides, self.compositions)
        self.context = [(d.id, s.shape, s.bottom, s.top) for d in descriptions for s in d.sections]
        self._tree = None
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

    # -- exposure ---------------------------------------------------------------------------

    def _neighbours(self, ident, line):
        """(shape, bottom, top, tolerance) of the other buildings near LINE."""
        if not self.context:
            return []
        if self._tree is None:
            self._tree = STRtree([c[1] for c in self.context])
        found = self._tree.query(line.buffer(CONTEXT_TOLERANCE + .1))
        return [(self.context[i][1], self.context[i][2], self.context[i][3], CONTEXT_TOLERANCE)
                for i in sorted(found) if base_id(self.context[i][0]) != base_id(ident)]

    def wall_rects(self, d, sections):
        """The exposed rectangles of the walls of SECTIONS, in emission order: a list of dicts
        with "section", "a", "b" (the wall edge's end points in X/Z), "u0", "u1" (metres along it),
        "y0", "y1" and "normal". A wall piece buried in another section of the building (as far as
        that section reaches) or in a neighbouring building takes no wall and no window."""
        rects = []
        for si, s in enumerate(sections):
            covers = [(o.shape, o.bottom, o.top, .02) for oi, o in enumerate(sections) if oi != si]
            for poly in polygons(s.shape):
                poly = orient(poly, sign=1)
                for ring in [poly.exterior, *poly.interiors]:
                    pts = list(ring.coords)
                    for (x, z), (xx, zz) in zip(pts, pts[1:]):
                        length = math.hypot(xx - x, zz - z)
                        if length < 1e-6:
                            continue
                        nx, nz = (zz - z) / length, -(xx - x) / length
                        probe = LineString([(x + nx * .05, z + nz * .05),
                                            (xx + nx * .05, zz + nz * .05)])
                        near = [c for c in covers if c[0].distance(probe) < c[3] + .1]
                        near += [c for c in self._neighbours(d.id, probe)
                                 if c[2] > s.bottom and c[1] < s.top]
                        for u0, u1, y0, y1 in exposed(probe, length, s.bottom, s.top, near):
                            rects.append({"section": si, "a": (x, z), "b": (xx, zz), "u0": u0,
                                          "u1": u1, "y0": y0, "y1": y1, "normal": (nx, nz)})
        return rects

    # -- emission ---------------------------------------------------------------------------

    def emit(self, d):
        if not (migrated(d.id) if d.migrate is None else d.migrate):
            return self.emit_legacy(d)
        over = self.overrides.get(d.override, {})
        wall, roof = over.get("wall", d.wall), over.get("roof", d.roof)
        comp = self.compositions.get(base_id(d.id)) if d.facade else None
        windows = d.facade and over.get("windows", True)
        sections = d.sections
        if comp:
            sections = building_compose.apply_setbacks(d.sections, comp, Section)
            wall = comp.get("wall", wall)
            roof = comp.get("roof", roof)
        rects = self.wall_rects(d, sections)
        trim = (comp or {}).get("trim", wall)
        entrance = []
        if comp:
            rects, entrance = self.compose_rects(d, rects, sections, comp, wall)
        else:
            for r in rects:
                r.update(role=map_pack.ROLE_WALL if d.facade else map_pack.ROLE_DETAIL,
                         color=wall, style=STYLE_LEGACY, min_wall=MIN_WINDOW_WALL,
                         windows=windows)
        first = self.count()
        facades, runs = [], []
        for index, r in enumerate(rects):
            a, b = r["a"], r["b"]
            length = math.hypot(b[0] - a[0], b[1] - a[1])
            tx, tz = (b[0] - a[0]) / length, (b[1] - a[1]) / length
            p0 = (a[0] + tx * r["u0"], a[1] + tz * r["u0"])
            p1 = (a[0] + tx * r["u1"], a[1] + tz * r["u1"])
            pa, pb = (p0[0], r["y0"], p0[1]), (p1[0], r["y0"], p1[1])
            pc, pe = (p1[0], r["y1"], p1[1]), (p0[0], r["y1"], p0[1])
            wall_first = self.count()
            self.triangle([pa, pc, pb], r["color"])
            self.triangle([pa, pe, pc], r["color"])
            if runs and runs[-1][2] == r["role"] and runs[-1][1] == wall_first:
                runs[-1] = (runs[-1][0], wall_first + 2, runs[-1][2], map_pack.NO_FACADE, 0)
            else:
                runs.append((wall_first, wall_first + 2, r["role"], map_pack.NO_FACADE, 0))
            if r["windows"]:
                bays, floors = grid(r["u1"] - r["u0"], r["y1"] - r["y0"], STYLES[r["style"]],
                                    r["min_wall"])
                if bays and floors:
                    facades.append({"wall": wall_first, "index": index, "bays": bays,
                                    "floors": floors, "style": r["style"]})
                    r["grid"] = (bays, floors)
        walls_end = self.count()
        # Roofs: each section's top, less what other sections of the building stand on it.
        roof_role = map_pack.ROLE_ROOF if d.facade else map_pack.ROLE_DETAIL
        roof_shapes = []
        for s in sections:
            above = [o.shape for o in sections
                     if o.bottom <= s.top + 1e-6 and o.top > s.top + 1e-6 and o.shape is not s.shape]
            shape = s.shape.difference(unary_union(above)) if above else s.shape
            roof_shapes.append((s, shape, unary_union(above) if above else Point(0, 0).buffer(0)))
            for poly in polygons(shape):
                if poly.area < .05:
                    continue
                for t in self.triangulate(poly):
                    self.triangle([(x, s.top, z) for x, z in map_pack_clockwise(t)], roof)
        roof_end = self.count()
        if roof_end > walls_end:
            runs.append((walls_end, roof_end, roof_role, map_pack.NO_FACADE, 0))
        if comp:
            self.emit_detail(d, comp, sections, rects, roof_shapes, trim, entrance, runs)
        return {"id": d.id, "first": first, "end": self.count(), "detail_first": roof_end,
                "runs": [r for r in runs if r[1] > r[0]], "facades": facades}

    # -- composition ------------------------------------------------------------------------

    def compose_rects(self, d, rects, sections, comp, wall):
        """Assign role, colour, window style and window rule to each wall rectangle of a composed
        building: the ground band is split from the walls above it and the entrance interval is
        cut out of the best-facing ground piece. Returns (pieces, entrance or None)."""
        family = comp["family"]
        material = comp.get("material", {"balcony": "plaster", "stepped": "panel",
                                         "brick": "brick"}[family])
        style = STYLE_NAMES.index(comp.get("style", {"balcony": "balcony", "stepped": "ribbon",
                                                     "brick": "punched"}[family]))
        ground_y = min(s.bottom for s in sections)
        g = comp.get("ground")
        pieces = []
        for r in rects:
            r = dict(r)
            upper = dict(r, role=ROLE_FOR_MATERIAL[material], color=wall, style=style,
                         min_wall=3.2, windows=True)
            if g is None or r["y0"] > ground_y + 1e-6:
                pieces.append(upper)
                continue
            band = ground_y + g["band"]
            low = dict(r, role=ROLE_FOR_MATERIAL[g.get("material", material)],
                       color=g.get("wall", wall), min_wall=3.0, ground=True,
                       style=STYLE_NAMES.index(g.get("style", "shop")),
                       windows=g["windows"])
            if r["y1"] <= band + 1.0:
                pieces.append(low)
            else:
                low["y1"] = band
                upper["y0"] = band
                pieces.extend([low, upper])
        entrance = None
        e = comp.get("entrance")
        if e:
            dx, dz = building_compose.SIDES[e["side"]]
            best = None
            for i, p in enumerate(pieces):
                if not p.get("ground"):
                    continue
                align = p["normal"][0] * dx + p["normal"][1] * dz
                span = p["u1"] - p["u0"]
                if align < .5 or span < e["width"] + 2.4:
                    continue
                key = (round(align, 3), span)
                if best is None or key > best[0]:
                    best = (key, i)
            if best is not None:
                p = pieces[best[1]]
                centre = p["u0"] + e["at"] * (p["u1"] - p["u0"])
                lo, hi = centre - e["width"] / 2, centre + e["width"] / 2
                door = dict(p, u0=lo, u1=hi, windows=False)
                left, right = dict(p, u1=lo), dict(p, u0=hi)
                if "style" in e:
                    left["style"] = right["style"] = STYLE_NAMES.index(e["style"])
                pieces[best[1]:best[1] + 1] = [x for x in (left, door, right)
                                               if x["u1"] - x["u0"] >= MIN_PIECE]
                entrance = {"rect": door, "centre": centre, "params": e}
            else:
                self.report.setdefault(base_id(d.id), {})["entrance"] = "no wall fits"
        pieces = [p for p in pieces if p["u1"] - p["u0"] >= MIN_PIECE and p["y1"] - p["y0"] >= .5]
        return pieces, entrance

    def emit_detail(self, d, comp, sections, rects, roof_shapes, trim, entrance, runs):
        detail = building_compose.Detail(self.triangulate, map_pack_clockwise)
        own = unary_union([s.shape for s in sections])
        report = self.report.setdefault(base_id(d.id), {"balconies": 0, "rejected": 0})
        probe = own.buffer(3)
        near = [(sh, lo, hi) for sh, lo, hi, _ in self._neighbours(d.id, probe)]
        blocked = unary_union(([self.obstacles] if self.obstacles is not None else [])
                              + [sh.buffer(.25) for sh, _, _ in near])
        for s, roof_shape, above in roof_shapes:
            if roof_shape.is_empty:
                continue
            building_compose.roof_edges(detail, roof_shape, s.shape, above, s.top, comp, trim,
                                        map_pack.ROLE_TRIM, blocked)
        b = comp.get("balconies")
        ground_y = min(s.bottom for s in sections)
        if b:
            slab = b.get("slab", trim)
            front = b.get("front", slab)
            for r in rects:
                if "grid" not in r or r.get("ground"):
                    continue
                dx, dz = r["normal"]
                if not any(dx * building_compose.SIDES[s][0] + dz * building_compose.SIDES[s][1]
                           > .7 for s in b["sides"]):
                    continue
                bays, floors = r["grid"]
                style = STYLES[r["style"]]
                u0s, pitch_u, y0s, pitch_v, w0, h0, w1, h1, _ = style
                last = floors + b["to_floor"] if b["to_floor"] < 0 else b["to_floor"]
                a, bb = r["a"], r["b"]
                length = math.hypot(bb[0] - a[0], bb[1] - a[1])
                tx, tz = (bb[0] - a[0]) / length, (bb[1] - a[1]) / length
                wall = (a, (tx, tz), (dx, dz))
                for bay in range(b["offset"], bays - b["span"] + 1, b["every"]):
                    ua = r["u0"] + u0s + bay * pitch_u - .5
                    ub = r["u0"] + u0s + (bay + b["span"] - 1) * pitch_u + w1 + .5
                    ua, ub = max(ua, r["u0"] + .05), min(ub, r["u1"] - .05)
                    for f in range(b["from_floor"], min(floors - 1, last) + 1):
                        top = r["y0"] + y0s - .9 + f * pitch_v
                        foot = building_compose.Polygon(
                            [(a[0] + tx * u + dx * v, a[1] + tz * u + dz * v)
                             for u, v in ((ua, -.02), (ub, -.02), (ub, b["depth"] + .1),
                                          (ua, b["depth"] + .1))])
                        clash = foot.intersects(own.buffer(-.04)) or foot.intersects(blocked) \
                            or any(hi > top - .3 and lo < top + 1.2 and foot.intersects(sh.buffer(.3))
                                   for sh, lo, hi in near)
                        if clash:
                            report["rejected"] += 1
                            continue
                        report["balconies"] += 1
                        building_compose.balcony_boxes(detail, wall, (ua, ub), top, b["depth"],
                                                       slab, front, map_pack.ROLE_BALCONY)
        if entrance:
            e, door = entrance["params"], entrance["rect"]
            a, bb = door["a"], door["b"]
            dx, dz = door["normal"]
            length = math.hypot(bb[0] - a[0], bb[1] - a[1])
            tx, tz = (bb[0] - a[0]) / length, (bb[1] - a[1]) / length
            wall = (a, (tx, tz), (dx, dz))
            lo, hi = door["u0"], door["u1"]
            y = door["y0"]

            def rect(ua, ub, va, vb):
                return building_compose.Polygon(
                    [(a[0] + tx * u + dx * v, a[1] + tz * u + dz * v)
                     for u, v in ((ua, va), (ub, va), (ub, vb), (ua, vb))])

            line = ((a[0] - tx * 5000, a[1] - tz * 5000), (a[0] + tx * 5000, a[1] + tz * 5000))
            canopy = e["canopy"]
            while canopy >= 1.0:
                foot = rect(lo - .8, hi + .8, 0, canopy)
                if not foot.intersects(blocked):
                    break
                canopy -= .4
            else:
                canopy = 0
            report["entrance_canopy"] = round(canopy, 2)
            if "fascia" in e:
                # A signboard band along the whole ground floor of the entrance wall.
                for g in rects:
                    if g.get("ground") and g["a"] == a and g["b"] == bb and g["windows"] is not None:
                        detail.prism(rect(g["u0"], g["u1"], -.05, .2), g["y1"] - .95, g["y1"] - .12,
                                     e["fascia"], map_pack.ROLE_TRIM, hide=[line])
            centre = entrance["centre"]
            band = door["y1"] - y
            pier = trim
            detail.prism(rect(lo, lo + .4, -.05, .3), y, y + band - .45, pier,
                         map_pack.ROLE_TRIM, hide=[line])
            detail.prism(rect(hi - .4, hi, -.05, .3), y, y + band - .45, pier,
                         map_pack.ROLE_TRIM, hide=[line])
            detail.prism(rect(lo, hi, -.05, .3), y + band - .45, y + band - .05, pier,
                         map_pack.ROLE_TRIM, hide=[line])
            detail.prism(rect(centre - .95, centre + .95, -.05, .12), y, y + 2.55,
                         e.get("door", (58, 46, 40)), map_pack.ROLE_DETAIL, bottom=False,
                         hide=[line])
            detail.prism(rect(lo - .3, hi + .3, 0, max(canopy, 1.2) - .1 if canopy else 1.0),
                         y, y + .14, (150, 146, 138), map_pack.ROLE_DETAIL, bottom=False)
            if canopy:
                detail.prism(rect(lo - .8, hi + .8, -.05, canopy), y + band - .5, y + band - .2,
                             e.get("canopy_color", trim), map_pack.ROLE_CANOPY, hide=[line])
                detail.prism(rect(lo - .8, hi + .8, canopy - .12, canopy), y + band - .75,
                             y + band - .2, e.get("canopy_color", trim), map_pack.ROLE_CANOPY,
                             bottom=False)
        for role in (map_pack.ROLE_TRIM, map_pack.ROLE_BALCONY, map_pack.ROLE_CANOPY,
                     map_pack.ROLE_DETAIL):
            triangles = detail.by_role.get(role)
            if not triangles:
                continue
            start = self.count()
            for points, color in triangles:
                self.triangle(points, color)
            runs.append((start, self.count(), role, map_pack.NO_FACADE, 0))


def exposed(probe, length, y0, y1, covers):
    """The rectangles (u0, u1, y0, y1) of a wall of LENGTH and heights Y0..Y1 not covered by any of
    COVERS ((shape, bottom, top, tolerance) of solids). PROBE is the wall's line moved .05 m
    outward: where it runs inside a solid (grown by its tolerance) the wall is buried there."""
    spans = []
    for shape, lo, hi, tol in covers:
        if hi <= y0 + 1e-6 or lo >= y1 - 1e-6:
            continue
        inside = probe.intersection(shape.buffer(tol))
        for g in getattr(inside, "geoms", [inside]):
            if g.geom_type != "LineString" or g.length < .01:
                continue
            ua, ub = sorted(probe.project(Point(c)) for c in (g.coords[0], g.coords[-1]))
            spans.append((max(ua, 0), min(ub, length), max(lo, y0), min(hi, y1)))
    ys = sorted({y0, y1, *[v for s in spans for v in s[2:]]})
    slabs = []
    for lo, hi in zip(ys, ys[1:]):
        if hi - lo < 1e-6:
            continue
        mid = (lo + hi) / 2
        covered = sorted((s[0], s[1]) for s in spans if s[2] <= mid <= s[3])
        cursor, free = 0.0, []
        for ua, ub in covered:
            if ua > cursor + 1e-6:
                free.append((cursor, ua))
            cursor = max(cursor, ub)
        if cursor < length - 1e-6:
            free.append((cursor, length))
        slabs.append((lo, hi, free))
    out = []  # merge slabs with the same free intervals, bottom to top
    for lo, hi, free in slabs:
        for ua, ub in free:
            for item in out:
                if abs(item[0] - ua) < 1e-6 and abs(item[1] - ub) < 1e-6 and abs(item[3] - lo) < 1e-6:
                    item[3] = hi
                    break
            else:
                out.append([ua, ub, lo, hi])
    return [tuple(r) for r in sorted(out, key=lambda r: (r[2], r[0]))
            if r[1] - r[0] >= MIN_PIECE or r[1] - r[0] >= length - 1e-6 and r[1] - r[0] > .01]


def grid(length, height, style=STYLES[0], min_wall=MIN_WINDOW_WALL):
    """(bays, floors) of the window grid STYLE on a wall LENGTH metres long and HEIGHT high:
    every window fits inside the wall, clear of the corners and the top. Walls under MIN_WALL
    high get none."""
    u0, pitch_u, y0, pitch_v, w0, h0, w1, h1, _ = style
    if height < min_wall:
        return 0, 0
    bays = 0
    while u0 + bays * pitch_u + max(w0, w1) <= length - WINDOW_END_MARGIN:
        bays += 1
    floors = 0
    while y0 + floors * pitch_v + (h0 if floors == 0 else h1) <= height - WINDOW_TOP_MARGIN:
        floors += 1
    return bays, floors


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


def compose_test():
    """Fixtures for exposure, setbacks, compositions and override validation."""
    from shapely import constrained_delaunay_triangles
    from shapely.geometry import Polygon, box

    def tri(poly):
        return [list(t.exterior.coords)[:3] for t in constrained_delaunay_triangles(poly).geoms]

    def build(descriptions, compositions=None, obstacles=None):
        out = []
        emitter = Emitter(lambda p, c: out.append((p, c)), lambda: len(out), tri,
                          compositions=compositions, obstacles=obstacles)
        records = emitter.emit_all(descriptions)
        return out, records, emitter

    def Desc(ident, sections, wall=(1, 2, 3), roof=(4, 5, 6), **kw):
        return BuildingDescription(ident, sections, wall, roof, migrate=True, **kw)

    # exposed(): the intervals of a wall that no solid buries (a cover grows by its tolerance).
    def same(got, want):
        return len(got) == len(want) and all(
            all(abs(x - y) < .05 for x, y in zip(g, w)) for g, w in zip(got, want))

    probe = LineString([(0, .05), (30, .05)])
    assert exposed(probe, 30, 0, 20, []) == [(0, 30, 0, 20)]
    assert same(exposed(probe, 30, 0, 20, [(box(10, 0, 20, 5), 0, 20, .02)]),
                [(0, 10, 0, 20), (20, 30, 0, 20)])
    # A lower neighbour buries the wall only as far as it reaches; windows start above it.
    assert same(exposed(probe, 30, 0, 20, [(box(-5, -1, 35, 5), 0, 8, .02)]), [(0, 30, 8, 20)])
    assert exposed(probe, 30, 0, 20, [(box(-5, -1, 35, 5), 0, 20, .02)]) == []
    # An L-shaped cover leaves a wide lower piece and a taller piece above it.
    pieces = exposed(probe, 30, 0, 20, [(box(0, 0, 10, 5), 0, 10, .02),
                                        (box(20, 0, 30, 5), 0, 20, .02)])
    assert same(sorted(pieces), [(0, 20, 10, 20), (10, 20, 0, 10)]) or same(
        sorted(pieces), [(0, 10, 10, 20), (10, 20, 0, 20)]), pieces
    # Party walls: two buildings sharing a wall have no windows on it; a 5 m gap restores them.
    near = Desc("way/1", [Section(box(0, 0, 20, 10), 3.5, 20)])
    flush = Desc("way/2", [Section(box(20, 0, 40, 10), 3.5, 20)])
    far = Desc("way/3", [Section(box(25, 0, 45, 10), 3.5, 20)])

    def east_windows(a, b):
        out, records, _ = build([a, b])
        mine = next(r for r in records if r["id"] == a.id)
        return [f for f in mine["facades"]
                if out[f["wall"]][0][0][0] == 20 == out[f["wall"]][0][2][0]]

    assert east_windows(near, flush) == []
    assert east_windows(near, far) != []
    out, records, _ = build([near, flush])
    assert all(r["detail_first"] == r["end"] for r in records)  # fallback: no attached detail
    # Setbacks recede above their level, never grow the footprint, and keep the mapped heights.
    ground = box(0, 0, 40, 16)
    base = Section(ground, 3.5, 40.5)
    comp = validate_compositions({"way/9": {
        "family": "stepped", "setbacks": [{"above": 17, "cut": "west", "depth": 8},
                                          {"above": 27, "inset": 2.0}]}}, "river")["way/9"]
    parts = building_compose.apply_setbacks([base], comp, Section)
    assert [(s.bottom, s.top) for s in parts] == [(3.5, 20.5), (20.5, 30.5), (30.5, 40.5)]
    assert parts[1].shape.bounds[0] == 8 and parts[1].shape.bounds[2] == 40
    assert parts[2].shape.bounds[0] == 10 and parts[2].shape.bounds[1] == 2
    assert all(ground.contains(s.shape.buffer(-1e-6)) for s in parts)
    assert parts[1].shape.area < parts[0].shape.area and parts[2].shape.area < parts[1].shape.area
    # A composed building: balconies, a ground band, an entrance, roof edges.
    block = Desc("way/164963867", [Section(box(0, 0, 44, 16), 3.5, 28.5)], (238, 226, 196),
                 (112, 116, 124))
    comp = {"way/164963867": {
        "family": "balcony", "wall": [238, 226, 196], "roof": [112, 116, 124],
        "ground": {"band": 4.0, "style": "lobby"}, "setbacks": [{"above": 21.5, "inset": 2.2}],
        "parapet": .8, "cornice": .35,
        "balconies": {"sides": ["north", "south", "west"], "span": 1, "every": 1, "depth": 1.7},
        "entrance": {"side": "west", "at": .5, "width": 3.2, "canopy": 2.0}}}
    comp = validate_compositions(comp, "river")
    out, records, emitter = build([block], comp, box(-30, -40, -12, 60))
    r = records[0]
    first, end, detail = r["first"], r["end"], r["detail_first"]
    assert first <= r["facades"][0]["wall"] < detail <= end
    roles = {}
    for a, b, role, facade, heading in r["runs"]:
        assert first <= a < b <= end
        for i in range(a, b):
            roles[i] = role
    assert [role for a, b, role, f, h in r["runs"]][-4:].count(map_pack.ROLE_BALCONY) == 1
    assert {map_pack.ROLE_WALL_PLASTER, map_pack.ROLE_ROOF, map_pack.ROLE_TRIM,
            map_pack.ROLE_BALCONY, map_pack.ROLE_CANOPY, map_pack.ROLE_DETAIL} <= set(roles.values())
    roof_run = next(x for x in r["runs"] if x[2] == map_pack.ROLE_ROOF)
    assert roof_run[1] == detail and all(roles[i] == map_pack.ROLE_WALL_PLASTER
                                         for i in range(first, roof_run[0]))
    # Facades: wall pairs of wall roles; openings stay inside their rectangle and its section.
    levels = {3.5, 3.5 + 4.0, 3.5 + 21.5, 28.5}
    for f in r["facades"]:
        t = out[f["wall"]][0]
        assert map_pack.ROLE_WALL <= roles[f["wall"]] in map_pack.WALL_ROLES
        y0, y1 = t[0][1], t[1][1]
        length = math.hypot(t[2][0] - t[0][0], t[2][2] - t[0][2])
        style = STYLES[f["style"]]
        u0, pu, v0, pv, w0, h0, w1, h1, _ = style
        assert u0 + (f["bays"] - 1) * pu + max(w0, w1) <= length - WINDOW_END_MARGIN + 1e-9
        assert v0 + (f["floors"] - 1) * pv + (h0 if f["floors"] == 1 else h1) \
            <= y1 - y0 - WINDOW_TOP_MARGIN + 1e-9
        assert y0 in levels and y1 in levels  # never across a band, setback or roof level
    # The upper volume recedes: no wall above the setback stands on the ground footprint's edge.
    upper = [t for t in (out[i][0] for i in range(first, detail) if roles[i] in map_pack.WALL_ROLES)
             if t[0][1] == 3.5 + 21.5 and t[0][1] == t[2][1]]
    assert upper and all(min(p[0] for p in t) >= 2.2 - 1e-6 and min(p[2] for p in t) >= 2.2 - 1e-6
                         for t in upper)
    # Balconies keep clear of the obstacle (the water box x -30..-12) and of the building itself.
    balconies = [out[i][0] for i in range(first, end) if roles[i] == map_pack.ROLE_BALCONY]
    assert balconies and emitter.report["way/164963867"]["balconies"] > 20
    for pts in balconies:
        assert all(not (-30 < p[0] < -12 and -40 < p[2] < 60) for p in pts)
        assert all(math.isfinite(v) for p in pts for v in p)
    assert emitter.report["way/164963867"]["entrance_canopy"] > 0
    # Determinism and independence: the same input gives the same triangles, in any order,
    # and an unrelated building leaves this one's records alone.
    other = Desc("way/5", [Section(box(100, 0, 110, 10), 3.5, 12)])
    out2, records2, _ = build([other, block], comp, box(-30, -40, -12, 60))
    mine = next(x for x in records2 if x["id"] == block.id)
    assert out2[mine["first"]:mine["end"]] == out[first:end]
    # Overrides are validated: unknown keys, bad ranges and references that match no building.
    bad = [{"family": "castle"}, {"family": "brick", "wall": [1, 2]},
           {"family": "brick", "extra": 1}, {"family": "brick", "parapet": 9},
           {"family": "brick", "setbacks": [{"above": 10}]},
           {"family": "brick", "setbacks": [{"above": 10, "inset": 1, "cut": "west"}]},
           {"family": "brick", "setbacks": [{"above": 10, "inset": 1}, {"above": 5, "inset": 1}]},
           {"family": "brick", "balconies": {"depth": 5}},
           {"family": "brick", "balconies": {"sides": ["up"]}},
           {"family": "brick", "entrance": {"side": "west"}},  # needs a ground band
           {"family": "brick", "ground": {"band": 1}}]
    for params in bad:
        try:
            validate_compositions({"way/1": params}, "river")
        except ValueError:
            continue
        raise AssertionError(f"accepted {params}")
    for key in ("way/x", "building/1", "1"):
        try:
            validate_compositions({key: {"family": "brick"}}, "river")
        except ValueError:
            continue
        raise AssertionError(f"accepted id {key}")
    assert validate_compositions({"way/1": {"family": "brick", "map": "outer"}}, "river") == {}
    ok = Desc("way/5", [Section(box(0, 0, 10, 10), 3.5, 9)])
    validate([ok], compositions={"way/5": {"family": "brick"}})
    for ident, subject in (("way/404", ok),
                           ("way/5", BuildingDescription("way/5", ok.sections, (1, 2, 3), (4, 5, 6),
                                                         migrate=False))):
        try:
            validate([subject], compositions={ident: {"family": "brick"}})
        except ValueError:
            continue
        raise AssertionError("composition reference not validated")
    # The shipped file validates and every entry names a pilot building.
    shipped = load_compositions()
    assert shipped and all(migrated(i) for i in shipped)


def _normal(a, b, c):
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    return (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])


if __name__ == "__main__":
    self_test()
    compose_test()
    print("building kit fixtures OK")
