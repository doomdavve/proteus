#!/usr/bin/env python3
"""Authored building compositions for tools/building_kit.py (docs/BUILDINGS.md).

A composition is a dict of explicit parameters keyed by stable source id in
tools/building_overrides.json. This module validates them and holds the reusable primitives the
kit's emitter combines: massing (setbacks and recessed upper volumes), roof edges (parapet and
cornice), balcony stacks, a ground-floor band and an entrance with its canopy. Everything is
plain geometry on shapely footprints; the emitter owns triangle numbering and records.

Units are metres, colours RGB triples. Heights are measured from the building's lowest base.
"""
import math

from shapely.geometry import LineString, Point, Polygon, box
from shapely.geometry.polygon import orient
from shapely.ops import unary_union

FAMILIES = ("balcony", "stepped", "brick", "block", "warehouse", "house", "shed")
MATERIALS = ("plaster", "brick", "panel")
ROOF_SHAPES = ("flat", "gable", "sawtooth")
SIDES = {"west": (-1, 0), "east": (1, 0), "south": (0, -1), "north": (0, 1)}  # pack x, z
STYLE_NAMES = ("legacy", "balcony", "ribbon", "punched", "shop", "lobby", "cottage", "hall")
WALL_STYLES = ("balcony", "ribbon", "punched", "cottage", "hall")  # the choices for `style`
GROUND_STYLES = ("shop", "lobby")
# What a family means when an entry names no more than its family: wall material, window style.
FAMILY_DEFAULTS = {"balcony": ("plaster", "balcony"), "stepped": ("panel", "ribbon"),
                   "brick": ("brick", "punched"), "block": ("plaster", "punched"),
                   "warehouse": ("panel", "hall"), "house": ("plaster", "cottage"),
                   "shed": ("panel", "cottage")}
KEYS = {"map", "family", "wall", "roof", "trim", "material", "style", "ground", "setbacks",
        "parapet", "cornice", "balconies", "entrance", "note", "auto", "legacy", "roof_shape",
        "windows", "doors", "lighting"}
LIGHTING = ("residential", "workplace", "industrial", "quiet")  # window schedules by hour
DOOR_KEYS = {"count", "width", "height", "color"}
GROUND_KEYS = {"band", "wall", "material", "style", "windows"}
# An apartment unit is UNIT bays wide; every EVERY-th unit (from OFFSET) has one balcony stack,
# SPAN bays wide and centred in its unit, on each floor FROM_FLOOR..TO_FLOOR.
BALCONY_KEYS = {"sides", "from_floor", "to_floor", "unit", "span", "every", "offset", "depth",
                "slab", "front", "max_stacks"}
ENTRANCE_KEYS = {"side", "at", "width", "canopy", "canopy_color", "door", "style", "fascia"}


def polygons(shape):
    if shape.is_empty:
        return []
    if shape.geom_type == "Polygon":
        return [shape]
    return [p for g in getattr(shape, "geoms", []) for p in polygons(g)]


def _color(value, what):
    if not (isinstance(value, (list, tuple)) and len(value) == 3
            and all(isinstance(c, int) and not isinstance(c, bool) and 0 <= c <= 255
                    for c in value)):
        raise ValueError(f"{what}: bad colour {value!r}")
    return tuple(value)


def _number(value, what, low, high):
    if isinstance(value, bool) or not isinstance(value, (int, float)) \
            or not math.isfinite(value) or not low <= value <= high:
        raise ValueError(f"{what}: {value!r} is not a number in {low}..{high}")
    return float(value)


def _integer(value, what, low, high):
    if isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high:
        raise ValueError(f"{what}: {value!r} is not an integer in {low}..{high}")
    return value


def _unknown(table, allowed, what):
    if not isinstance(table, dict):
        raise ValueError(f"{what}: expected an object")
    extra = set(table) - allowed
    if extra:
        raise ValueError(f"{what}: unknown keys {sorted(extra)}")


def validate_params(ident, params):
    """The checked, normalised copy of the composition PARAMS of building IDENT; ValueError names
    the key at fault. Colours and numbers are bounded so a typo cannot make an absurd building."""
    what = f"{ident}"
    _unknown(params, KEYS, what)
    if params.get("legacy"):
        # An explicit unmigrated fallback: the building keeps the pre-kit walls and roof.
        if set(params) - {"legacy", "map", "note"} or params["legacy"] is not True:
            raise ValueError(f"{what}: a legacy entry takes only legacy, map and note")
        return {"legacy": True, "map": params.get("map", "river")}
    auto = params.get("auto", False)
    if auto not in (True, False):
        raise ValueError(f"{what}.auto: expected true or false")
    # An `auto` entry tunes the automatic assignment (tools/building_families.py): every key is
    # optional and the entry's keys replace the assigned ones. Without `auto` the entry is a full
    # authored composition and names its family.
    family = params.get("family")
    if family not in FAMILIES and not (auto and family is None):
        raise ValueError(f"{what}: family must be one of {FAMILIES}, not {family!r}")
    out = {"map": params.get("map", "river")}
    if auto:
        out["auto"] = True
    if "family" in params:
        out["family"] = params["family"]
    if "lighting" in params:
        if params["lighting"] not in LIGHTING:
            raise ValueError(f"{what}.lighting: not one of {LIGHTING}")
        out["lighting"] = params["lighting"]
    for key in ("wall", "roof", "trim"):
        if key in params:
            out[key] = _color(params[key], f"{what}.{key}")
    if "material" in params:
        if params["material"] not in MATERIALS:
            raise ValueError(f"{what}.material: not one of {MATERIALS}")
        out["material"] = params["material"]
    if "style" in params:
        if params["style"] not in WALL_STYLES:
            raise ValueError(f"{what}.style: not one of {WALL_STYLES}")
        out["style"] = params["style"]
    if "roof_shape" in params:
        if params["roof_shape"] not in ROOF_SHAPES:
            raise ValueError(f"{what}.roof_shape: not one of {ROOF_SHAPES}")
        out["roof_shape"] = params["roof_shape"]
    if "windows" in params:
        if not isinstance(params["windows"], bool):
            raise ValueError(f"{what}.windows: expected true or false")
        out["windows"] = params["windows"]
    if "doors" in params:
        d = params["doors"]
        _unknown(d, DOOR_KEYS, f"{what}.doors")
        out["doors"] = {"count": _integer(d.get("count", 1), f"{what}.doors.count", 1, 6),
                        "width": _number(d.get("width", 4.0), f"{what}.doors.width", 2.5, 8),
                        "height": _number(d.get("height", 3.8), f"{what}.doors.height", 2.2, 6)}
        if "color" in d:
            out["doors"]["color"] = _color(d["color"], f"{what}.doors.color")
    for key, low, high in (("parapet", 0, 2.0), ("cornice", 0, 1.5)):
        if key in params:
            out[key] = _number(params[key], f"{what}.{key}", low, high)
    if "ground" in params:
        g = params["ground"]
        _unknown(g, GROUND_KEYS, f"{what}.ground")
        out["ground"] = {"band": _number(g.get("band", 4.0), f"{what}.ground.band", 2.5, 6.0)}
        if "wall" in g:
            out["ground"]["wall"] = _color(g["wall"], f"{what}.ground.wall")
        if "material" in g:
            if g["material"] not in MATERIALS:
                raise ValueError(f"{what}.ground.material: not one of {MATERIALS}")
            out["ground"]["material"] = g["material"]
        if "style" in g:
            if g["style"] not in GROUND_STYLES:
                raise ValueError(f"{what}.ground.style: not one of {GROUND_STYLES}")
            out["ground"]["style"] = g["style"]
        out["ground"]["windows"] = bool(g.get("windows", True))
    out["setbacks"] = []
    last = 0
    for n, s in enumerate(params.get("setbacks", [])):
        _unknown(s, {"above", "inset", "cut", "depth"}, f"{what}.setbacks[{n}]")
        above = _number(s.get("above"), f"{what}.setbacks[{n}].above", 3, 150)
        if above <= last:
            raise ValueError(f"{what}.setbacks[{n}]: levels must ascend")
        last = above
        if ("inset" in s) == ("cut" in s):
            raise ValueError(f"{what}.setbacks[{n}]: give exactly one of inset and cut")
        if "inset" in s:
            out["setbacks"].append({"above": above,
                                    "inset": _number(s["inset"], f"{what}.setbacks[{n}].inset", .5, 8)})
        else:
            if s["cut"] not in SIDES:
                raise ValueError(f"{what}.setbacks[{n}].cut: not one of {sorted(SIDES)}")
            out["setbacks"].append({"above": above, "cut": s["cut"], "depth": _number(
                s.get("depth"), f"{what}.setbacks[{n}].depth", 1, 40)})
    if "balconies" in params:
        b = params["balconies"]
        _unknown(b, BALCONY_KEYS, f"{what}.balconies")
        sides = b.get("sides", sorted(SIDES))
        if not (isinstance(sides, list) and sides and all(s in SIDES for s in sides)):
            raise ValueError(f"{what}.balconies.sides: a non-empty list of {sorted(SIDES)}")
        out["balconies"] = {
            "sides": list(sides),
            "from_floor": _integer(b.get("from_floor", 1), f"{what}.balconies.from_floor", 0, 60),
            "to_floor": _integer(b.get("to_floor", -2), f"{what}.balconies.to_floor", -60, 60),
            "unit": _integer(b.get("unit", 3), f"{what}.balconies.unit", 2, 4),
            "span": _integer(b.get("span", 1), f"{what}.balconies.span", 1, 2),
            "every": _integer(b.get("every", 1), f"{what}.balconies.every", 1, 6),
            "offset": _integer(b.get("offset", 0), f"{what}.balconies.offset", 0, 5),
            "max_stacks": _integer(b.get("max_stacks", 99), f"{what}.balconies.max_stacks", 1, 99),
            "depth": _number(b.get("depth", 1.6), f"{what}.balconies.depth", .8, 2.6)}
        if out["balconies"]["span"] > out["balconies"]["unit"]:
            raise ValueError(f"{what}.balconies.span: wider than the unit")
        for key in ("slab", "front"):
            if key in b:
                out["balconies"][key] = _color(b[key], f"{what}.balconies.{key}")
    if "entrance" in params:
        e = params["entrance"]
        _unknown(e, ENTRANCE_KEYS, f"{what}.entrance")
        if e.get("side") not in SIDES:
            raise ValueError(f"{what}.entrance.side: not one of {sorted(SIDES)}")
        out["entrance"] = {
            "side": e["side"], "at": _number(e.get("at", .5), f"{what}.entrance.at", .1, .9),
            "width": _number(e.get("width", 3.2), f"{what}.entrance.width", 1.6, 8),
            "canopy": _number(e.get("canopy", 2.0), f"{what}.entrance.canopy", 0, 3.5)}
        for key in ("canopy_color", "door", "fascia"):
            if key in e:
                out["entrance"][key] = _color(e[key], f"{what}.entrance.{key}")
        if "style" in e:
            if e["style"] not in GROUND_STYLES:
                raise ValueError(f"{what}.entrance.style: not one of {GROUND_STYLES}")
            out["entrance"]["style"] = e["style"]
        if "ground" not in out and not auto:
            raise ValueError(f"{what}: an entrance needs a ground band")
    return out


def apply_setbacks(sections, params, make_section):
    """The sections after the setbacks of PARAMS: each section that crosses a setback level splits
    there, and the part above recedes (INSET all round, or CUT away from one side). Footprints only
    shrink, so the ground footprint never grows; MAKE_SECTION(shape, bottom, top, part) builds
    the new sections. Heights are unchanged."""
    if not params["setbacks"]:
        return list(sections)
    ground = min(s.bottom for s in sections)
    result = []
    for s in sections:
        pieces = [s]
        for step in params["setbacks"]:
            level = ground + step["above"]
            nxt = []
            for p in pieces:
                if not (p.bottom < level - 1e-6 and p.top > level + 1e-6):
                    nxt.append(p)
                    continue
                if "inset" in step:
                    shape = p.shape.buffer(-step["inset"], join_style=2)
                else:
                    minx, minz, maxx, maxz = p.shape.bounds
                    dx, dz = SIDES[step["cut"]]
                    depth = step["depth"]
                    cut = box(minx, minz, minx + depth, maxz) if dx < 0 else \
                        box(maxx - depth, minz, maxx, maxz) if dx > 0 else \
                        box(minx, minz, maxx, minz + depth) if dz < 0 else \
                        box(minx, maxz - depth, maxx, maxz)
                    shape = p.shape.difference(cut)
                shape = unary_union([q for q in polygons(shape) if q.area > 4])
                nxt.append(make_section(p.shape, p.bottom, level, p.part))
                if not shape.is_empty:
                    nxt.append(make_section(shape, level, p.top, p.part))
            pieces = nxt
        result.extend(pieces)
    return result


class Detail:
    """Attached architecture collected per role, in the order it is emitted."""

    def __init__(self, triangulate, up):
        self.triangulate, self.up = triangulate, up
        self.by_role = {}
        self.skipped = 0

    def add(self, role, points, color):
        self.by_role.setdefault(role, []).append((points, color))

    def prism(self, shape, y0, y1, color, role, top=True, bottom=True, hide=()):
        """The faces of SHAPE (X/Z polygons) from Y0 to Y1: sides facing away from the material, a
        top and a bottom. HIDE is wall segments ((x, z), (x, z)); sides lying along one are
        skipped, as they would be buried in the wall."""
        for poly in polygons(shape):
            poly = orient(poly, sign=1)
            tops = self._triangles(poly) if top or bottom else []
            if tops is None:
                self.skipped += 1  # a sliver the triangulator refuses: leave it out, never fail
                continue
            for ring in [poly.exterior, *poly.interiors]:
                pts = list(ring.coords)
                for (x, z), (xx, zz) in zip(pts, pts[1:]):
                    if math.hypot(xx - x, zz - z) < 1e-6:
                        continue
                    if any(_along((x, z), (xx, zz), h) for h in hide):
                        continue
                    a, b, c, e = (x, y0, z), (xx, y0, zz), (xx, y1, zz), (x, y1, z)
                    self.add(role, [a, c, b], color)
                    self.add(role, [a, e, c], color)
            for t in tops:
                pts = self.up(t)
                if top:
                    self.add(role, [(x, y1, z) for x, z in pts], color)
                if bottom:
                    self.add(role, [(x, y0, z) for x, z in reversed(pts)], color)

    def _triangles(self, poly):
        """The triangles of POLY, or None when it cannot be triangulated (a sliver)."""
        for candidate in (poly, poly.buffer(0).simplify(.02)):
            try:
                parts = polygons(candidate)
                if len(parts) == 1 and parts[0].area > 1e-4:
                    return self.triangulate(parts[0])
            except Exception:  # shapely and the bake's own checks raise AssertionError
                continue
        return None


def _along(p, q, wall):
    line = LineString(wall)
    return line.distance(Point(p)) < .08 and line.distance(Point(q)) < .08


def roof_edges(detail, roof, shape, above, top, params, trim, role, blocked):
    """A parapet along the exposed edges of ROOF (a shapely polygon at height TOP) and a cornice
    band projecting from the wall below it. ABOVE is the footprints of volumes rising from this roof;
    their walls take neither. BLOCKED is geometry the cornice must not project into (neighbours,
    water, roads): the band is cut back there."""
    parapet = params.get("parapet", 0.0)
    cornice = params.get("cornice", 0.0)
    if cornice > 0:
        ring = shape.buffer(cornice, join_style=2).difference(shape)
        if not above.is_empty:
            ring = ring.difference(above.buffer(cornice + .3, join_style=2))
        if not blocked.is_empty:
            ring = ring.difference(blocked)
        ring = unary_union([p for p in polygons(ring) if p.area > .3])
        if not ring.is_empty:
            detail.prism(ring, top - .55, top, trim, role, top=True, bottom=True)
    if parapet > 0:
        thickness = .32
        inner = roof.buffer(-thickness, join_style=2)
        ring = roof.difference(inner)
        if not above.is_empty:
            ring = ring.difference(above.buffer(.1, join_style=2))
        ring = unary_union([p for p in polygons(ring) if p.area > .2])
        if not ring.is_empty:
            detail.prism(ring, top, top + parapet, trim, role, top=True, bottom=False)


def balcony_boxes(detail, wall, u_range, slab_top, depth, slab, front, role):
    """One balcony on a wall: a slab, a solid front panel and two side panels. WALL is the
    (origin, unit direction along the wall, outward normal) triple in X/Z; U_RANGE the span along
    the wall; the slab top is SLAB_TOP (y). Returns its X/Z footprint."""
    (ox, oz), (tx, tz), (nx, nz) = wall
    u0, u1 = u_range

    def rect(ua, ub, va, vb):
        return Polygon([(ox + tx * u + nx * v, oz + tz * u + nz * v)
                        for u, v in ((ua, va), (ub, va), (ub, vb), (ua, vb))])

    # The back of each part sits .05 inside the wall; the faces there are skipped.
    line = ((ox - tx * 5000, oz - tz * 5000), (ox + tx * 5000, oz + tz * 5000))
    hide = [line]
    detail.prism(rect(u0, u1, -.05, depth), slab_top - .22, slab_top, slab, role, hide=hide)
    rail = 1.05
    detail.prism(rect(u0, u1, depth - .12, depth), slab_top, slab_top + rail, front, role,
                 bottom=False)
    for ua, ub in ((u0, u0 + .1), (u1 - .1, u1)):
        detail.prism(rect(ua, ub, -.05, depth - .12), slab_top, slab_top + rail, front, role,
                     bottom=False, hide=hide)
    return rect(u0, u1, 0, depth)


def _facing(points, toward):
    """POINTS (a triangle, 3D) wound so its visible side faces TOWARD."""
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = points
    ux, uy, uz, vx, vy, vz = bx - ax, by - ay, bz - az, cx - ax, cy - ay, cz - az
    n = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
    if sum(a * b for a, b in zip(n, toward)) < 0:
        return [points[0], points[2], points[1]]
    return list(points)


def rectangle_of(shape, minimum_ratio):
    """The minimum rotated rectangle of SHAPE (a polygon without holes) as four (x, z) corners
    with the long side first, or None unless the shape fills at least MINIMUM_RATIO of it."""
    if shape.geom_type != "Polygon" or shape.interiors or shape.area < 1:
        return None
    rect = shape.minimum_rotated_rectangle
    if rect.geom_type != "Polygon" or rect.area <= 0 or shape.area / rect.area < minimum_ratio:
        return None
    pts = list(orient(rect, sign=1).exterior.coords)[:4]
    if math.dist(pts[0], pts[1]) < math.dist(pts[1], pts[2]):
        pts = pts[1:] + pts[:1]
    return pts


def pitched_roof(corners, eave, kind, rise, roof, wall, glass):
    """Triangles ((three (x, y, z)), colour) of a pitched roof over the rectangle CORNERS (four
    (x, z), long side a-b first, counter-clockwise) at height EAVE. `gable`: a ridge along the long
    side, slopes in ROOF and gable ends in WALL. `sawtooth`: teeth about 8 m wide along the long
    side, each a slope in ROOF rising to a vertical glazed face in GLASS, with end triangles in
    WALL. Every triangle faces away from the roof's volume."""
    a, b, c, d = corners
    out = []
    cx, cz = (a[0] + c[0]) / 2, (a[1] + c[1]) / 2

    def add(points, color, toward):
        out.append((_facing(points, toward), color))

    def quad(p, q, r, s, color, toward):
        add([p, q, r], color, toward)
        add([p, r, s], color, toward)

    if kind == "gable":
        # The ridge runs between the midpoints of the short sides b-c and d-a.
        m1 = ((b[0] + c[0]) / 2, eave + rise, (b[1] + c[1]) / 2)
        m2 = ((d[0] + a[0]) / 2, eave + rise, (d[1] + a[1]) / 2)
        for p, q, m in ((b, c, m1), (d, a, m2)):
            toward = ((p[0] + q[0]) / 2 - cx, 0, (p[1] + q[1]) / 2 - cz)
            add([(p[0], eave, p[1]), (q[0], eave, q[1]), m], wall, toward)
        for p, q, r, s in ((a, b, m1, m2), (c, d, m2, m1)):
            toward = ((p[0] + q[0]) / 2 - cx, 1, (p[1] + q[1]) / 2 - cz)
            quad((p[0], eave, p[1]), (q[0], eave, q[1]), r, s, roof, toward)
        return out
    # sawtooth: u along a-b, v along b-c.
    length, depth = math.dist(a, b), math.dist(b, c)
    ux, uz = (b[0] - a[0]) / length, (b[1] - a[1]) / length
    vx, vz = (c[0] - b[0]) / depth, (c[1] - b[1]) / depth
    teeth = max(2, round(length / 8))
    pitch = length / teeth

    def at(u, v, y):
        return (a[0] + ux * u + vx * v, y, a[1] + uz * u + vz * v)

    for i in range(teeth):
        u0, u1 = i * pitch, (i + 1) * pitch
        low0, low1 = at(u0, 0, eave), at(u1, 0, eave)
        high1 = at(u1, 0, eave + rise)
        far0, far1 = at(u0, depth, eave), at(u1, depth, eave)
        farhigh1 = at(u1, depth, eave + rise)
        quad(low0, far0, farhigh1, high1, roof, (-ux * rise * .5 + 0, 1, -uz * rise * .5))
        quad(at(u1, 0, eave), high1, farhigh1, far1, glass, (ux, 0, uz))
        add([low0, low1, high1], wall, (-vx, 0, -vz))
        add([far0, far1, farhigh1], wall, (vx, 0, vz))
    return out
