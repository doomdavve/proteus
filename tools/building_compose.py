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

FAMILIES = ("balcony", "stepped", "brick")
MATERIALS = ("plaster", "brick", "panel")
SIDES = {"west": (-1, 0), "east": (1, 0), "south": (0, -1), "north": (0, 1)}  # pack x, z
STYLE_NAMES = ("legacy", "balcony", "ribbon", "punched", "shop", "lobby")
KEYS = {"map", "family", "wall", "roof", "trim", "material", "style", "ground", "setbacks",
        "parapet", "cornice", "balconies", "entrance", "note"}
GROUND_KEYS = {"band", "wall", "material", "style", "windows"}
BALCONY_KEYS = {"sides", "from_floor", "to_floor", "span", "every", "offset", "depth", "slab",
                "front"}
ENTRANCE_KEYS = {"side", "at", "width", "canopy", "canopy_color", "door", "style"}


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
    if params.get("family") not in FAMILIES:
        raise ValueError(f"{what}: family must be one of {FAMILIES}, not {params.get('family')!r}")
    out = {"family": params["family"], "map": params.get("map", "river")}
    for key in ("wall", "roof", "trim"):
        if key in params:
            out[key] = _color(params[key], f"{what}.{key}")
    if "material" in params:
        if params["material"] not in MATERIALS:
            raise ValueError(f"{what}.material: not one of {MATERIALS}")
        out["material"] = params["material"]
    if "style" in params:
        if params["style"] not in STYLE_NAMES[1:5]:
            raise ValueError(f"{what}.style: not one of {STYLE_NAMES[1:5]}")
        out["style"] = params["style"]
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
            if g["style"] not in STYLE_NAMES[4:]:
                raise ValueError(f"{what}.ground.style: not one of {STYLE_NAMES[4:]}")
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
            "from_floor": _integer(b.get("from_floor", 0), f"{what}.balconies.from_floor", 0, 60),
            "to_floor": _integer(b.get("to_floor", -2), f"{what}.balconies.to_floor", -60, 60),
            "span": _integer(b.get("span", 1), f"{what}.balconies.span", 1, 4),
            "every": _integer(b.get("every", 1), f"{what}.balconies.every", 1, 6),
            "offset": _integer(b.get("offset", 0), f"{what}.balconies.offset", 0, 5),
            "depth": _number(b.get("depth", 1.6), f"{what}.balconies.depth", .8, 2.6)}
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
        for key in ("canopy_color", "door"):
            if key in e:
                out["entrance"][key] = _color(e[key], f"{what}.entrance.{key}")
        if "style" in e:
            if e["style"] not in STYLE_NAMES[4:]:
                raise ValueError(f"{what}.entrance.style: not one of {STYLE_NAMES[4:]}")
            out["entrance"]["style"] = e["style"]
        if "ground" not in out:
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

    def add(self, role, points, color):
        self.by_role.setdefault(role, []).append((points, color))

    def prism(self, shape, y0, y1, color, role, top=True, bottom=True, hide=()):
        """The faces of SHAPE (X/Z polygons) from Y0 to Y1: sides facing away from the material, a
        top and a bottom. HIDE is wall segments ((x, z), (x, z)); sides lying along one are
        skipped, as they would be buried in the wall."""
        for poly in polygons(shape):
            poly = orient(poly, sign=1)
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
            for t in self.triangulate(poly):
                pts = self.up(t)
                if top:
                    self.add(role, [(x, y1, z) for x, z in pts], color)
                if bottom:
                    self.add(role, [(x, y0, z) for x, z in reversed(pts)], color)


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
