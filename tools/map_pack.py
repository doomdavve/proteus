#!/usr/bin/env python3
"""Scenery packs: the baked map meshes the game loads at run time (src/scenery/boat_scenery.c).

A bake writes two files into assets/geometry/<name>/:

- scenery.bin, the whole mesh for the renderers, minimap and kit: a 32-byte little-endian
  header of eight u32 (magic "PSCN", version, vertex count, triangle count, leading water
  triangles, bridge first triangle, payload bytes, compressed bytes) and one raw DEFLATE stream.
  The payload holds the vertices as float32 x, y, z, then the triangles as three u32 vertex
  indices and an RGBA colour, the layout of geometry3d.h.
  Version 4 appends, after the DEFLATE stream, a u32 raw size, a u32 crc32 of the second stream
  (the loader checks it before inflating, as the inflater does not bound corrupt input) and a
  second raw DEFLATE stream holding the building-record block (see BUILDING_BLOCK below) with its arrays byte-shuffled
  (shuffle_block); version 3 stored that block
  uncompressed (still read, no longer written); version 1 packs have none and the loader still
  accepts them. (Version 2,
  which baked window triangles, was never shipped and is no longer read.) The
  header's water count and the leading water triangles are the same in both versions.
- water.h, the compiled collision map: the leading water triangles alone, with their vertices
  renumbered in their original order. Physics, the server and the multiplayer content digest
  use it, so they need no asset files. The loader rejects a pack whose water differs from it.

Coordinates are rounded to 0.1 mm and then to float32, as the C compiler read the old headers.

    python -I tools/map_pack.py --self-test
    python -I tools/map_pack.py --from-header PREFIX HEADER OUTPUT   # one-off header conversion
"""
import argparse
import math
import re
import struct
import tempfile
import zlib
from fractions import Fraction
from pathlib import Path

MAGIC = 0x4E435350  # "PSCN"
VERSION_1 = 1
VERSION_3 = 3  # the record block stored raw (read, no longer written)
VERSION = 4  # the record block deflated: u32 raw size, u32 crc32 of the stream, one raw DEFLATE stream
HEADER = struct.Struct("<8I")
TRIANGLE = struct.Struct("<3I4B")

# Version 3 building-record block, little-endian, directly after the DEFLATE stream and ending
# the file exactly: a 20-byte head of five u32 (magic "PBLD", building count, run count, facade
# count, style count), then the buildings, runs, facades and styles.
#   building (56 bytes): u32 id hash (crc32 of the stable id, e.g. "way/164963584"), first
#     triangle, end triangle (exclusive), detail_first (the first triangle of the building's
#     attached detail: balconies, canopies; walls and roof come before it), first run, run count,
#     first facade, facade count; f32 min x, y, z and max x, y, z of its triangles.
#   run (12 bytes): u32 first triangle, end triangle (exclusive), then u8 role, u8 facade index
#     (255 none), u16 heading (outward facade direction, 0..65535 for the full turn from +X
#     toward +Z; 0 when not a facade).
#   facade (12 bytes): one wall that takes windows, as compact grid data; no window triangles are
#     stored (src/scenery/building_windows.c expands them). u32 first triangle of the wall's pair
#     (a, c, b) and (a, d, c), which gives the wall's origin a, end b, base y and top y, so a
#     rebake or draping cannot disagree with the mesh; u16 facade number in its building (the
#     window seed input; walls without windows leave gaps), u16 bays, u16 floors, u8 style
#     (index into the styles), u8 lighting profile (PROFILES; 0 residential).
#   style (36 bytes): f32 first bay offset along the wall, bay pitch, first floor height above the
#     base, floor pitch, ground floor window width and height, upper floor window width and
#     height, and the offset of the window plane from the wall. Window F of bay B spans
#     [u0 + B pitch, + width] along the wall and [y0 + F pitch, + height] above its base.
# Buildings are sorted by first triangle, do not overlap, and lie after the leading water. Runs
# of a building are ordered, disjoint and inside it; a triangle outside every run has no role
# (the loader draws it as detail). Facades of a building are ordered by number, inside it and on
# wall-role triangles. Roles: ROLE_*.
BUILDING_MAGIC = 0x444C4250  # "PBLD"
BUILDING_HEAD = struct.Struct("<5I")
BUILDING = struct.Struct("<8I6f")
RUN = struct.Struct("<3I")
FACADE = struct.Struct("<I3HBB")
# Window lighting schedules (scenery/building_windows.h BUILDING_PROFILE_*), the facade's last byte.
PROFILES = ("residential", "workplace", "industrial", "quiet")
STYLE = struct.Struct("<9f")
# Wall roles take facade records (windows). ROLE_WALL is the fallback building's wall: the
# renderers pick its material from its tint (the retained palette). PLASTER, BRICK and PANEL name
# the material of an authored composition and draw the run's own colour. BALCONY, CANOPY and TRIM
# are attached architecture drawn flat in their own colour.
(ROLE_NONE, ROLE_WALL, ROLE_ROOF, ROLE_DETAIL, ROLE_BALCONY, ROLE_CANOPY, ROLE_TRIM,
 ROLE_WALL_PLASTER, ROLE_WALL_BRICK, ROLE_WALL_PANEL) = range(10)
ROLE_COUNT = 10
WALL_ROLES = (ROLE_WALL, ROLE_WALL_PLASTER, ROLE_WALL_BRICK, ROLE_WALL_PANEL)
NO_FACADE = 255


def f32(text):
    """The float32 nearest the decimal TEXT, as a C compiler reads "TEXTf"."""
    value = float(text)
    rounded = struct.unpack("<f", struct.pack("<f", value))[0]
    if rounded != value:
        # Python rounds the decimal to double first. That only goes wrong when the double is a
        # tie between two float32: settle it from the exact decimal.
        exact = Fraction(text)
        other = struct.unpack("<f", struct.pack("<f", float(2 * Fraction(value) - Fraction(rounded))))[0]
        if abs(Fraction(other) - exact) < abs(Fraction(rounded) - exact):
            rounded = other
    return rounded


def c_float(value):
    """A C literal that reads back as exactly float32 VALUE."""
    text = f"{value:.9g}"
    return text + ("f" if "." in text or "e" in text else ".0f")


def leading_water(vertices, triangles):
    count = 0
    for ids, _ in triangles:
        if any(vertices[i][1] != 0 for i in ids):
            break
        count += 1
    return count


def stable_id_hash(stable_id):
    return zlib.crc32(stable_id.encode("utf-8")) & 0xFFFFFFFF


def shift_record(record, index):
    """RECORD with every triangle number passed through INDEX(triangle, is_end): is_end is True
    for an exclusive range end, which maps from the last triangle it names. Used to re-base
    records when triangles are renumbered or moved."""
    detail = record["detail_first"]
    return {**record, "first": index(record["first"], False), "end": index(record["end"], True),
            "detail_first": index(record["end"], True) if detail == record["end"]
            else index(detail, False),
            "runs": [(index(a, False), index(b, True), *rest) for a, b, *rest in record["runs"]],
            "facades": [{**f, "wall": index(f["wall"], False)}
                        for f in record.get("facades", [])]}


def building_block(vertices, triangles, water, buildings, styles=()):
    """The version 3 record block for BUILDINGS: dicts with "id" (stable id string), "first",
    "end", "detail_first", "runs" [(first, end, role, facade, heading)] and optionally "facades"
    [{"wall", "index", "bays", "floors", "style"}]. STYLES are the window grids facades refer to:
    tuples of the nine STYLE floats. Raises ValueError on any violation of the contract above.
    Order of the input does not matter; records are sorted by first triangle."""
    records, runs_out, facades_out, seen = [], [], [], set()
    previous_end = water
    for style in styles:
        if len(style) != 9 or not all(math.isfinite(v) for v in style) \
                or not all(v > 0 for v in style[1:4:2] + style[4:8]) or style[0] < 0 \
                or style[2] < 0 or style[8] < 0:
            raise ValueError(f"bad window style {style!r}")
    for b in sorted(buildings, key=lambda b: b["first"]):
        ident = b["hash"] if "hash" in b else stable_id_hash(b["id"])
        first, end, detail = b["first"], b["end"], b["detail_first"]
        if ident in seen:
            raise ValueError(f"duplicate building id {b.get('id', ident)}")
        seen.add(ident)
        if not (previous_end <= first < end <= len(triangles)):
            raise ValueError(f"{b.get('id', ident)}: triangle range {first}..{end} overlaps, is empty, "
                             f"precedes the water or leaves the mesh")
        if not first <= detail <= end:
            raise ValueError(f"{b.get('id', ident)}: detail_first {detail} outside its range")
        previous_end = end
        low = [min(vertices[i][k] for ids, _ in triangles[first:end] for i in ids) for k in range(3)]
        high = [max(vertices[i][k] for ids, _ in triangles[first:end] for i in ids) for k in range(3)]
        if not all(math.isfinite(v) for v in low + high):
            raise ValueError(f"{b.get('id', ident)}: non-finite bounds")
        run_first, cursor = len(runs_out), first
        for r_first, r_end, role, facade, heading in b["runs"]:
            if not (cursor <= r_first < r_end <= end):
                raise ValueError(f"{b.get('id', ident)}: run {r_first}..{r_end} overlaps or leaves it")
            if not 1 <= role < ROLE_COUNT or not (0 <= facade <= NO_FACADE) \
                    or not 0 <= heading <= 0xFFFF:
                raise ValueError(f"{b.get('id', ident)}: bad run role/facade/heading")
            cursor = r_end
            runs_out.append(RUN.pack(r_first, r_end, role | facade << 8 | heading << 16))
        facade_first, last = len(facades_out), -1
        for f in b.get("facades", []):
            wall = f["wall"]
            if not (first <= wall and wall + 2 <= end) or not last < f["index"] <= 0xFFFF \
                    or not (1 <= f["bays"] <= 0xFFFF and 1 <= f["floors"] <= 0xFFFF) \
                    or not 0 <= f["style"] < len(styles) \
                    or not 0 <= f.get("profile", 0) < len(PROFILES):
                raise ValueError(f"{b.get('id', ident)}: bad facade {f!r}")
            last = f["index"]
            facades_out.append(FACADE.pack(wall, f["index"], f["bays"], f["floors"], f["style"],
                                         f.get("profile", 0)))
        records.append(BUILDING.pack(ident, first, end, detail, run_first,
                                     len(runs_out) - run_first, facade_first,
                                     len(facades_out) - facade_first, *low, *high))
    return (BUILDING_HEAD.pack(BUILDING_MAGIC, len(records), len(runs_out), len(facades_out),
                               len(styles))
            + b"".join(records) + b"".join(runs_out) + b"".join(facades_out)
            + b"".join(STYLE.pack(*style) for style in styles))


def _sections(blob):
    """(offset, record size, record count) of the three record arrays of a block."""
    magic, count, run_count, facade_count, _ = BUILDING_HEAD.unpack_from(blob)
    assert magic == BUILDING_MAGIC
    at = BUILDING_HEAD.size
    out = []
    for size, n in ((BUILDING.size, count), (RUN.size, run_count), (FACADE.size, facade_count)):
        out.append((at, size, n))
        at += size * n
    return out


def shuffle_block(blob):
    """BLOB with each record array stored byte-plane first (all first bytes, then all second
    bytes...): deflate packs the ascending triangle numbers and the repeated small fields of the
    records far better that way (half the size). The head and the styles stay as they are."""
    out = bytearray(blob)
    for at, size, n in _sections(blob):
        for column in range(size):
            out[at + column * n:at + (column + 1) * n] = blob[at + column:at + size * n:size]
    return bytes(out)


def unshuffle_block(blob):
    out = bytearray(blob)
    for at, size, n in _sections(blob):
        for column in range(size):
            out[at + column:at + size * n:size] = blob[at + column * n:at + (column + 1) * n]
    return bytes(out)


def pack_bytes(vertices, triangles, bridge_first, buildings=None, styles=()):
    """VERTICES: float32-exact (x, y, z); TRIANGLES: [((a, b, c), (r, g, b[, alpha]))]. BUILDINGS None
    writes a version 1 pack; a list (possibly empty) writes version 3 with its records."""
    payload = bytearray()
    for p in vertices:
        payload += struct.pack("<3f", *p)
    for ids, color in triangles:
        payload += TRIANGLE.pack(*ids, *color[:3], color[3] if len(color) > 3 else 255)
    deflate = zlib.compressobj(9, zlib.DEFLATED, -15)
    data = deflate.compress(bytes(payload)) + deflate.flush()
    water = leading_water(vertices, triangles)
    head = HEADER.pack(MAGIC, VERSION_1 if buildings is None else VERSION, len(vertices),
                       len(triangles), water, bridge_first, len(payload), len(data))
    block = b""
    if buildings is not None:
        raw = shuffle_block(building_block(vertices, triangles, water, buildings, styles))
        packer = zlib.compressobj(9, zlib.DEFLATED, -15)
        stream = packer.compress(raw) + packer.flush()
        block = struct.pack("<2I", len(raw), zlib.crc32(stream)) + stream
    return head + data + block


def read_buildings(blob):
    """The version 3 block BLOB as ([{"hash", "first", "end", "detail_first", "min", "max",
    "runs": [(first, end, role, facade, heading)], "facades": [{"wall", "index", "bays",
    "floors", "style"}]}], styles)."""
    magic, count, run_count, facade_count, style_count = BUILDING_HEAD.unpack_from(blob)
    assert magic == BUILDING_MAGIC
    runs_at = BUILDING_HEAD.size + count * BUILDING.size
    facades_at = runs_at + run_count * RUN.size
    styles_at = facades_at + facade_count * FACADE.size
    assert len(blob) == styles_at + style_count * STYLE.size
    out = []
    for i in range(count):
        ident, first, end, detail, run_first, n, facade_first, facade_n, *box = BUILDING.unpack_from(
            blob, BUILDING_HEAD.size + i * BUILDING.size)
        runs = []
        for j in range(run_first, run_first + n):
            a, b, packed = RUN.unpack_from(blob, runs_at + j * RUN.size)
            runs.append((a, b, packed & 255, packed >> 8 & 255, packed >> 16))
        facades = []
        for j in range(facade_first, facade_first + facade_n):
            wall, index, bays, floors, style, profile = FACADE.unpack_from(blob, facades_at + j * FACADE.size)
            facades.append({"wall": wall, "index": index, "bays": bays, "floors": floors,
                            "style": style, "profile": profile})
        out.append({"hash": ident, "first": first, "end": end, "detail_first": detail,
                    "min": box[:3], "max": box[3:], "runs": runs, "facades": facades})
    return out, [STYLE.unpack_from(blob, styles_at + j * STYLE.size) for j in range(style_count)]


def read_pack(path, with_buildings=False):
    """(vertices, triangles, water_count, bridge_first) from a scenery.bin, plus the building
    records and styles (None for version 1; read_buildings' pair) when WITH_BUILDINGS."""
    blob = Path(path).read_bytes()
    magic, version, nv, nt, water, bridge, size, packed = HEADER.unpack_from(blob)
    assert magic == MAGIC and version in (VERSION_1, VERSION_3, VERSION), \
        f"{path}: unknown pack version"
    payload = zlib.decompress(blob[HEADER.size:HEADER.size + packed], -15)
    assert len(payload) == size == nv * 12 + nt * TRIANGLE.size
    vertices = [struct.unpack_from("<3f", payload, 12 * i) for i in range(nv)]
    triangles = []
    for i in range(nt):
        *ids, r, g, b, a = TRIANGLE.unpack_from(payload, 12 * nv + TRIANGLE.size * i)
        triangles.append((tuple(ids), (r, g, b) if a == 255 else (r, g, b, a)))
    if with_buildings:
        tail = blob[HEADER.size + packed:]
        if version == VERSION:
            raw_size, crc = struct.unpack_from("<2I", tail)
            assert zlib.crc32(tail[8:]) == crc
            tail = unshuffle_block(zlib.decompress(tail[8:], -15))
            assert len(tail) == raw_size
        return vertices, triangles, water, bridge, (read_buildings(tail) if version != VERSION_1
                                                    else None)
    return vertices, triangles, water, bridge


def water_header(prefix, generator, vertices, triangles, extra=""):
    """The compiled collision description: the leading water triangles, renumbered in order."""
    water = triangles[:leading_water(vertices, triangles)]
    used = sorted({i for ids, _ in water for i in ids})
    index = {old: new for new, old in enumerate(used)}
    lines = [f"// Generated by {generator}. Do not edit.",
             "// Map data (c) OpenStreetMap contributors, ODbL 1.0; see manifest.json.",
             "// The leading water triangles of scenery.bin, which the loader checks against these.",
             f"static const Vector3 {prefix}_water_vertices[] = {{"]
    lines += ["    { " + ", ".join(c_float(v) for v in vertices[i]) + " }," for i in used]
    lines += ["};", f"static const Triangle3D {prefix}_water_triangles[] = {{"]
    lines += [f"    {{ {{ {index[a]}, {index[b]}, {index[c]} }}, {{ {r}, {g}, {bl}, 255 }} }},"
              for (a, b, c), (r, g, bl) in water]
    lines += ["};", ""]
    return "\n".join(lines) + extra


def write(output, prefix, generator, vertices, triangles, bridge_first, extra="", buildings=None,
          styles=()):
    """Write scenery.bin and water.h; returns the pack bytes (for the manifest hash). BUILDINGS
    and STYLES as for pack_bytes."""
    output.mkdir(parents=True, exist_ok=True)
    vertices = [tuple(f32(f"{v:.4f}") for v in p) for p in vertices]
    blob = pack_bytes(vertices, triangles, bridge_first, buildings, styles)
    (output / "scenery.bin").write_bytes(blob)
    (output / "water.h").write_text(
        water_header(prefix, generator, vertices, triangles, extra))
    return blob


def parse_header(prefix, text):
    """(vertex strings, triangles, bridge_first, trailing text) from a pre-A3.5i geometry.h."""
    head, rest = text.split(f"{prefix}_triangles[]", 1)
    vertices = [m for m in re.findall(
        r"\{ (-?[\d.]+)f, (-?[\d.]+)f, (-?[\d.]+)f \},?\n", head)]
    body, tail = rest.split("};", 1)
    triangles = [((int(m[0]), int(m[1]), int(m[2])), (int(m[3]), int(m[4]), int(m[5])))
                 for m in re.findall(
                     r"\{ \{ (\d+), (\d+), (\d+) \}, \{ (\d+), (\d+), (\d+), 255 \} \},", body)]
    bridge = int(re.search(rf"{prefix}_bridge_first_triangle = (\d+)", tail).group(1))
    # Whatever follows the bridge enum (the outer map's kit ground) stays compiled.
    extra = re.sub(rf"enum \{{ {prefix}_bridge_first_triangle = \d+ \}};\n?", "", tail)
    return vertices, triangles, bridge, extra.lstrip("\n")


def from_header(prefix, header, output, generator):
    vertices, triangles, bridge, extra = parse_header(prefix, Path(header).read_text())
    vertices = [tuple(f32(v) for v in p) for p in vertices]
    blob = pack_bytes(vertices, triangles, bridge)
    output.mkdir(parents=True, exist_ok=True)
    (output / "scenery.bin").write_bytes(blob)
    (output / "water.h").write_text(water_header(prefix, generator, vertices, triangles, extra))
    print(f"{prefix}: {len(vertices)} vertices, {len(triangles)} triangles, "
          f"{leading_water(vertices, triangles)} water; pack {len(blob)} bytes")


def self_test():
    assert f32("0.1") == struct.unpack("<f", struct.pack("<f", 0.1))[0]
    assert f32("-3452.7192") == struct.unpack("<f", struct.pack("<f", -3452.7192))[0]
    # A decimal just above a float32 tie whose double rounds onto the tie.
    tie = Fraction(1) + Fraction(1, 2 ** 24)
    text = f"{float(tie):.30f}1"
    assert f32(text) == 1 + 2 ** -23, f32(text)
    for v in (0.0, 5.0, f32("1e-7"), -3452.71923828125, 1234567.0, f32("0.1")):
        assert f32(c_float(v)[:-1]) == v and re.fullmatch(r"-?\d+(\.\d*)?(e-?\d+)?f",
                                                          c_float(v).replace("e+", "e")), v
    vertices = [(0, 0, 0), (1, 0, 0), (0, 0, 1), (5, 2, 5), (6, 2, 5), (5, 2, 6), (1, 0, 1)]
    triangles = [((0, 2, 1), (10, 20, 30)), ((1, 2, 6), (10, 20, 30)),
                 ((3, 5, 4), (40, 50, 60))]
    with tempfile.TemporaryDirectory() as d:
        blob = write(Path(d), "t", "self-test", vertices, triangles, 3)
        v, t, water, bridge = read_pack(Path(d) / "scenery.bin")
        assert (water, bridge) == (2, 3) and t == triangles and v == [tuple(map(float, p))
                                                                         for p in vertices]
        assert blob == (Path(d) / "scenery.bin").read_bytes()
        header = (Path(d) / "water.h").read_text()
        assert "{ { 0, 2, 1 }" in header and "{ { 1, 2, 3 }" in header, header
        assert header.count("f, ") == 2 * 4, header  # Four water vertices, renumbered.


STYLE_FIXTURE = (2.5, 4.0, 1.5, 3.2, 2.2, 1.9, 1.5, 1.5, .035)


def building_fixture():
    """Two buildings after two water triangles; returns (vertices, triangles, buildings). Each
    has one wall pair (a, c, b), (a, d, c), a roof-less detail triangle and one facade record."""
    vertices = [(0, 0, 0), (1, 0, 0), (0, 0, 1), (1, 0, 1)]
    triangles = [((0, 2, 1), (1, 2, 3)), ((1, 2, 3), (1, 2, 3))]
    buildings = []
    for n, (x, name) in enumerate([(10, "way/2"), (20, "way/1")]):
        base = len(vertices)
        vertices += [(x, 3.5, 0), (x + 8, 3.5, 0), (x + 8, 9.5, 0), (x, 9.5, 0)]
        first = len(triangles)
        triangles += [((base, base + 2, base + 1), (166, 173, 195)),
                      ((base, base + 3, base + 2), (166, 173, 195)),
                      ((base, base + 2, base + 3), (50, 60, 70))]
        buildings.append({"id": name, "first": first, "end": first + 3, "detail_first": first + 2,
                          "runs": [(first, first + 2, ROLE_WALL, NO_FACADE, 0),
                                   (first + 2, first + 3, ROLE_DETAIL, NO_FACADE, 100 * n)],
                          "facades": [{"wall": first, "index": 3 * n, "bays": 2, "floors": 1,
                                       "style": 0}]})
    return vertices, triangles, buildings


def building_test():
    vertices, triangles, buildings = building_fixture()
    styles = [STYLE_FIXTURE]
    with tempfile.TemporaryDirectory() as d:
        path = Path(d) / "scenery.bin"
        blob = pack_bytes(vertices, triangles, 8, buildings, styles)
        path.write_bytes(blob)
        *_, (records, read_styles) = read_pack(path, True)
        assert [(r["first"], r["end"]) for r in records] == [(2, 5), (5, 8)]
        assert records[0]["hash"] == stable_id_hash("way/2") and read_styles == [STYLE.unpack(STYLE.pack(*STYLE_FIXTURE))]
        assert records[0]["min"][1] == 3.5 and records[0]["max"][1] == 9.5
        assert records[1]["runs"][1] == (7, 8, ROLE_DETAIL, NO_FACADE, 100)
        assert records[1]["facades"] == [{"wall": 5, "index": 3, "bays": 2, "floors": 1, "style": 0, "profile": 0}]
        # Identity is the id, not the input order or the list position.
        assert pack_bytes(vertices, triangles, 8, buildings[::-1], styles) == blob
        # The deflated block holds byte-shuffled arrays; shuffling is its own inverse step.
        raw = building_block(vertices, triangles, 2, buildings, styles)
        assert shuffle_block(raw) != raw and unshuffle_block(shuffle_block(raw)) == raw
        # A version 1 pack carries no block and reads as before.
        v1 = pack_bytes(vertices, triangles, 8)
        path.write_bytes(v1)
        assert read_pack(path, True)[4] is None and len(v1) < len(blob)
        # An unrelated building added elsewhere leaves every other record byte-identical.
        extra = [dict(b) for b in buildings]
        extra.append({"id": "way/9", "first": 8, "end": 9, "detail_first": 8, "runs": []})
        triangles2 = triangles + [((4, 6, 5), (1, 1, 1))]
        records2, _ = read_buildings(building_block(vertices, triangles2, 2, extra, styles))
        assert [r["hash"] for r in records2[:2]] == [r["hash"] for r in records]
        assert records2[:2] == records
        # Re-basing keeps the facade references with their triangles.
        moved = shift_record(buildings[0], lambda i, end: i + 10)
        assert moved["facades"][0]["wall"] == buildings[0]["facades"][0]["wall"] + 10
        assert moved["first"] == buildings[0]["first"] + 10 and moved["end"] == buildings[0]["end"] + 10
    wall = {"wall": 2, "index": 0, "bays": 1, "floors": 1, "style": 0}
    bad = [lambda b: b[0].update(first=1),  # inside the water
           lambda b: b[0].update(end=9),  # leaves the mesh
           lambda b: b[1].update(first=4),  # overlaps
           lambda b: b[0].update(detail_first=9),
           lambda b: b[0].update(id="way/1"),  # duplicate id
           lambda b: b[0]["runs"].append((2, 3, ROLE_WALL, 0, 0)),  # overlapping run
           lambda b: b[0]["runs"].__setitem__(0, (2, 4, 10, 0, 0)),  # bad role
           lambda b: b[0]["runs"].__setitem__(0, (2, 4, ROLE_WALL, 0, 70000)),
           lambda b: b[0]["facades"].__setitem__(0, {**wall, "wall": 4}),  # pair leaves the building
           lambda b: b[0]["facades"].__setitem__(0, {**wall, "bays": 0}),
           lambda b: b[0]["facades"].__setitem__(0, {**wall, "floors": 70000}),
           lambda b: b[0]["facades"].__setitem__(0, {**wall, "style": 1}),  # no such style
           lambda b: b[0]["facades"].__setitem__(0, {**wall, "profile": len(PROFILES)}),
           lambda b: b[0]["facades"].append({**wall, "index": 0})]  # numbers must increase
    for mutate in bad:
        v, t, b = building_fixture()
        mutate(b)
        try:
            building_block(v, t, 2, b, styles)
        except ValueError:
            continue
        raise AssertionError("invalid building records were accepted")
    v, t, b = building_fixture()
    for style in [(1,) * 8, (2.5, 0, 1.5, 3.2, 2.2, 1.9, 1.5, 1.5, .035),
                  (2.5, 4, 1.5, 3.2, 2.2, 1.9, 1.5, float("nan"), .035),
                  (2.5, 4, 1.5, 3.2, -2.2, 1.9, 1.5, 1.5, .035)]:
        try:
            building_block(v, t, 2, b, [style])
        except ValueError:
            continue
        raise AssertionError(f"bad style {style} accepted")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--from-header", nargs=3, metavar=("PREFIX", "HEADER", "OUTPUT"))
    parser.add_argument("--generator", default="tools/map_pack.py")
    args = parser.parse_args()
    self_test()
    building_test()
    if args.from_header:
        prefix, header, output = args.from_header
        from_header(prefix, header, Path(output), args.generator)
    else:
        print("map pack fixtures OK")


if __name__ == "__main__":
    main()
