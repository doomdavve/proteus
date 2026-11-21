#!/usr/bin/env python3
"""Automatic building families for the scenery bakes (docs/BUILDINGS.md, B3).

`assign(description)` chooses one composition (the params of tools/building_compose.py) for a
mapped building from facts the source supports, in this order:

  1. supported mapped facts: the building tag, levels, height, roof shape, material and colour,
     shop and amenity tags;
  2. conservative inference from footprint scale, height and the district the adapter names
     (`context`: urban, industrial, coastal);
  3. a plain fallback block, recorded with the reason.

An authored entry in tools/building_overrides.json replaces or tunes the result (building_kit.py).
Variation is stable per building: choices that the facts do not decide come from the crc32 of the
stable source id, never from iteration order. Missing data never implies a shop, a public
entrance or balconies: a shop band needs a shop or amenity tag, and entrances are authored only.

    python tools/building_families.py     # fixtures
"""
import math
import zlib

import building_compose

HOUSE_TYPES = {"detached", "house", "semidetached_house", "terrace", "villa", "farm",
               "residential", "bungalow", "cabin", "dormitory_house"}
SMALL_TYPES = {"garage", "garages", "shed", "carport", "allotment_house", "hut", "boathouse", "roof",
               "kiosk", "toilets", "greenhouse", "cabin", "barn", "stable", "bunker"}
GABLED_SMALL = {"boathouse", "allotment_house", "hut", "cabin", "barn", "stable"}
TANK_TYPES = {"storage_tank", "silo", "tank"}
INDUSTRIAL_TYPES = {"industrial", "warehouse", "factory", "manufacture", "hangar", "storage",
                    "transportation", "service", "depot", "logistics"}
APARTMENT_TYPES = {"apartments", "residential", "dormitory", "hotel"}  # residential: when large
MODERN_TYPES = {"office", "commercial", "retail", "hospital", "university", "college", "school",
                "kindergarten", "civic", "public", "government", "sports_hall", "supermarket"}
SHOP_AMENITIES = {"cafe", "restaurant", "pub", "bar", "fast_food", "bank", "pharmacy", "ice_cream"}
GABLED_ROOFS = {"gabled", "hipped", "half-hipped", "gambrel", "saltbox", "pyramidal", "gable"}
FLAT_ROOFS = {"flat", "skillion", "round", "dome", "many", "shed"}

PLASTER = [(230, 214, 170), (226, 190, 140), (206, 120, 98), (232, 228, 216), (180, 196, 178),
           (210, 200, 176), (196, 168, 132)]
BRICK = [(172, 94, 70), (164, 90, 68), (150, 84, 64), (186, 140, 92), (128, 82, 70)]
PANEL = [(160, 170, 186), (184, 186, 192), (150, 160, 172), (196, 190, 180), (140, 150, 160)]
INDUSTRIAL = [(150, 156, 160), (170, 160, 140), (130, 140, 150), (166, 124, 96), (124, 130, 134)]
HOUSE_WALLS = [(222, 218, 205), (156, 58, 48), (214, 186, 120), (176, 182, 186), (200, 205, 196),
               (236, 232, 222), (120, 140, 160)]
HOUSE_ROOFS = [(74, 72, 76), (120, 62, 52), (88, 84, 82), (96, 66, 58)]
FLAT_ROOFS_RGB = [(98, 98, 104), (112, 116, 124), (88, 90, 96), (104, 100, 96)]
INDUSTRIAL_ROOFS = [(110, 114, 118), (90, 96, 104), (126, 98, 84), (100, 104, 96)]
TRIMS = [(236, 232, 222), (244, 240, 232), (92, 84, 80), (222, 214, 196)]
COLOURS = {"white": (236, 232, 222), "grey": (170, 172, 176), "gray": (170, 172, 176),
           "yellow": (226, 196, 120), "red": (170, 70, 58), "brown": (140, 100, 78),
           "beige": (222, 204, 170), "green": (150, 172, 146), "blue": (140, 160, 190),
           "orange": (214, 150, 96), "black": (70, 70, 76), "cream": (236, 226, 196),
           "pink": (220, 170, 160)}


def _pick(ident, salt, table):
    """A stable choice from TABLE for the building IDENT: the crc32 of its id and SALT."""
    return table[zlib.crc32(f"{ident}/{salt}".encode()) % len(table)]


def _colour(text):
    """RGB for a building:colour / roof:colour tag (#rrggbb or a common name), else None."""
    if not isinstance(text, str):
        return None
    text = text.strip().lower()
    if len(text) == 7 and text[0] == "#":
        try:
            rgb = tuple(int(text[i:i + 2], 16) for i in (1, 3, 5))
        except ValueError:
            return None
        return rgb
    return COLOURS.get(text)


def _tone(rgb, lo=96, hi=238):
    """RGB pulled into a range that reads as a wall in the game's lighting."""
    top = max(rgb)
    scale = 1.0
    if top > hi:
        scale = hi / top
    elif top < lo:
        scale = lo / max(top, 1)
    return tuple(max(0, min(255, round(c * scale))) for c in rgb)


FACT_TAGS = ("building", "building:levels", "height", "roof:shape", "building:material",
             "building:colour", "roof:colour", "shop", "amenity", "name", "man_made")


def facts(tags, area, height, basis, parts=1, holes=0):
    """The source facts of a building as the families read them (and the report prints): the tag
    subset above, the footprint AREA in m2, the HEIGHT in metres and its BASIS ("height",
    "levels"/"floors * 3 + 1", "fallback"...), the number of PARTS and courtyard HOLES."""
    levels = None
    try:
        levels = float(tags["building:levels"]) if "building:levels" in tags else None
    except ValueError:
        levels = None
    return {"tags": {k: tags[k] for k in FACT_TAGS if k in tags}, "area": round(area, 1),
            "height": round(height, 2), "basis": basis, "levels": levels, "parts": parts,
            "holes": holes}


def facts_summary(d):
    f = d.facts
    return (f"{f['tags'].get('building', '?')}, {f['area']:.0f} m2, {f['height']:.1f} m "
            f"({f['basis']}), {d.context or 'urban'}")


def assign(d, force_family=None):
    """{"comp", "family", "basis", "why"} for the BuildingDescription D (facts must be set). COMP
    is validated against tools/building_compose.py. FORCE_FAMILY (an `auto` entry's family)
    overrides the choice and builds that family's recipe from the same facts."""
    f, ident = d.facts, d.id
    tags = f["tags"]
    kind = tags.get("building", "yes")
    area, height, levels = f["area"], f["height"], f.get("levels")
    ctx = d.context or "urban"
    tagged_height = f["basis"] in ("height", "levels", "floors * 3 + 1", "tag")
    family, basis, why = _choose(kind, tags, area, height, levels, ctx, tagged_height, ident, f)
    if force_family:
        family, basis, why = force_family, "override", f"family forced to {force_family}"
    comp = _recipe(family, d, kind, tags, area, height, ctx, ident, f)
    comp["map"] = "river"
    return {"comp": building_compose.validate_params(ident, comp), "family": family, "basis": basis,
            "why": why}


def _choose(kind, tags, area, height, levels, ctx, tagged_height, ident, f):
    if kind in TANK_TYPES:
        return "shed", "tags", f"building={kind}: a tank, no windows"
    if kind == "parking" and area >= 100:
        return "block", "tags", "building=parking: a multi-storey deck with ribbon openings"
    if kind in SMALL_TYPES or area < 40:
        return "shed", "tags" if kind in SMALL_TYPES else "inferred", \
            f"building={kind}" if kind in SMALL_TYPES else f"footprint {area:.0f} m2 under 40"
    if kind == "service" and area < 150:
        return "shed", "tags", "building=service under 150 m2: a utility building"
    if kind in HOUSE_TYPES and not (kind == "residential" and (area >= 250 or height > 9)):
        return "house", "tags", f"building={kind}"
    if kind == "yes" and area < 220 and height <= 9 and "building:levels" not in tags \
            and "height" not in tags:
        return "house", "inferred", f"untagged {area:.0f} m2 footprint under 9 m: a house"
    if kind in INDUSTRIAL_TYPES:
        return "warehouse", "tags", f"building={kind}"
    if ctx == "industrial" and kind in ("yes", "commercial", "retail", "office") and height <= 16 \
            and area >= 400:
        return "warehouse", "inferred", f"{area:.0f} m2, {height:.0f} m high in the port"
    if height >= 45 and area >= 300:
        return "stepped", "inferred", f"tower: {height:.0f} m high"
    if kind in APARTMENT_TYPES or kind == "residential" or (levels or 0) >= 4:
        if kind in APARTMENT_TYPES and height >= 12 and area >= 300 and ctx != "industrial" \
                and zlib.crc32(f"{ident}/bal".encode()) % 4 == 0:
            return "balcony", "tags", f"{kind}, {height:.0f} m: apartments with a balcony stack"
        brick = tags.get("building:material") in ("brick", "stone") \
            or zlib.crc32(f"{ident}/br".encode()) % 5 < 2
        if brick:
            return "brick", "tags" if tags.get("building:material") == "brick" else "inferred", \
                "brick block"
        return "block", "tags" if kind in APARTMENT_TYPES else "inferred", f"{kind} block"
    if kind in MODERN_TYPES:
        return "block", "tags", f"building={kind}"
    if not tagged_height and kind == "yes" and not (tags.get("shop") or tags.get("amenity")):
        return "block", "fallback", "no usable tags: plain block"
    brick = tags.get("building:material") in ("brick", "stone") \
        or zlib.crc32(f"{ident}/br".encode()) % 4 == 0
    return ("brick" if brick else "block"), "inferred", "no family tag: block from height and scale"


def _recipe(family, d, kind, tags, area, height, ctx, ident, f):
    comp = {"family": family}
    wall = _colour(tags.get("building:colour"))
    roof = _colour(tags.get("roof:colour"))
    if family in ("house", "shed"):
        walls, roofs = HOUSE_WALLS, HOUSE_ROOFS
    elif family == "warehouse" or ctx == "industrial":
        walls, roofs = INDUSTRIAL, INDUSTRIAL_ROOFS
    elif family == "brick":
        walls, roofs = BRICK, FLAT_ROOFS_RGB
    elif family == "stepped":
        walls, roofs = PANEL, FLAT_ROOFS_RGB
    elif family == "block" and (kind in MODERN_TYPES or kind == "parking"
                                or zlib.crc32(f"{ident}/pn".encode()) % 3 == 0):
        walls, roofs = PANEL, FLAT_ROOFS_RGB
        comp["material"] = "panel"
        comp["style"] = "ribbon"
    else:
        walls, roofs = PLASTER, FLAT_ROOFS_RGB
    comp["wall"] = list(_tone(wall) if wall else _pick(ident, "wall", walls))
    comp["roof"] = list(_tone(roof, 60, 200) if roof else _pick(ident, "roof", roofs))
    comp["trim"] = list(_pick(ident, "trim", TRIMS))
    if family == "balcony":
        comp["balconies"] = {"sides": ["west", "north", "south", "east"], "unit": 3, "span": 1,
                             "every": 3, "depth": 1.7, "max_stacks": 1,
                             "slab": list(_pick(ident, "slab", TRIMS)),
                             "front": list(_pick(ident, "front", [(190, 92, 62), (84, 112, 126),
                                                                  (176, 150, 96), (110, 122, 108)]))}
    elif family == "stepped":
        if height >= 45:
            comp["setbacks"] = [{"above": round(height * .82, 1), "inset": min(3.0, math.sqrt(area) / 8)}]
    elif family == "warehouse":
        comp["doors"] = {"count": 3 if area > 1200 else 2 if area > 500 else 1, "width": 4.2,
                         "height": 3.9}
        comp["roof_shape"] = "gable"
        if area >= 1500 and ctx == "industrial" and zlib.crc32(f"{ident}/st".encode()) % 3 == 0:
            comp["roof_shape"] = "sawtooth"
    elif family == "house":
        comp["roof_shape"] = "gable"
        shape = tags.get("roof:shape")
        if shape in FLAT_ROOFS:
            comp["roof_shape"] = "flat"
        if height < 4:
            comp["windows"] = False
    elif family == "shed":
        comp["windows"] = False
        if kind in GABLED_SMALL:
            comp["roof_shape"] = "gable"
    if tags.get("shop") and family in ("brick", "block", "balcony") and height >= 6 \
            or tags.get("amenity") in SHOP_AMENITIES and family in ("brick", "block") and height >= 6:
        comp["ground"] = {"band": 4.0, "style": "shop", "wall": list(_pick(ident, "gw", PLASTER))}
    return comp


def self_test():
    from shapely.geometry import box

    import building_kit

    def desc(ident, kind, area=400, height=12, ctx="urban", basis="fallback", **tags):
        side = math.sqrt(area)
        section = building_kit.Section(box(0, 0, side, side), 3.5, 3.5 + height)
        return building_kit.BuildingDescription(
            ident, [section], (1, 2, 3), (4, 5, 6),
            facts={"tags": {"building": kind, **tags}, "area": area, "height": height,
                   "basis": basis, "levels": None},
            context=ctx)

    cases = [("way/1", desc("way/1", "garage", 30, 2.8), "shed"),
             ("way/2", desc("way/2", "detached", 120, 5.5), "house"),
             ("way/3", desc("way/3", "warehouse", 2000, 9), "warehouse"),
             ("way/4", desc("way/4", "apartments", 900, 20, levels=None), None),
             ("way/5", desc("way/5", "yes", 100, 5.5), "house"),
             ("way/6", desc("way/6", "storage_tank", 300, 14), "shed"),
             ("way/7", desc("way/7", "yes", 500, 80, basis="height"), "stepped"),
             ("way/8", desc("way/8", "yes", 500, 12, ctx="industrial"), "warehouse"),
             ("way/9", desc("way/9", "yes", 500, 12), "block")]
    for ident, d, want in cases:
        got = assign(d)
        assert want is None or got["family"] == want, (ident, got["family"], want)
        assert got["comp"]["family"] == got["family"] and got["basis"] in (
            "tags", "inferred", "fallback") and got["why"]
        assert assign(d) == got  # stable
    # Missing data never implies a shop: no ground band without a tag; a shop tag asks for one.
    assert "ground" not in assign(desc("way/10", "yes", 500, 12))["comp"]
    assert "ground" not in assign(desc("way/10", "apartments", 500, 12))["comp"]
    assert assign(desc("way/11", "commercial", 500, 12, shop="clothes"))["comp"].get("ground")
    # Sparse tags fall back to a plain block and say so.
    sparse = assign(desc("way/12", "yes", 500, 16))
    assert sparse["basis"] == "fallback" and sparse["family"] == "block"
    # Colour tags are honoured and kept readable; unknown names are ignored.
    assert assign(desc("way/13", "yes", 500, 12, **{"building:colour": "#ff0000"}))["comp"]["wall"][0] <= 238
    assert assign(desc("way/13", "yes", 500, 12, **{"building:colour": "mauve"}))["comp"]["wall"]
    # A forced family keeps the facts but changes the recipe.
    assert assign(desc("way/14", "yes", 500, 12), "warehouse")["comp"]["family"] == "warehouse"
    print("building families OK")


if __name__ == "__main__":
    self_test()
