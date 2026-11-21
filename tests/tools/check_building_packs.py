#!/usr/bin/env python3
"""The checked-in scenery packs keep their water: a rebake that adds or changes buildings must
leave water.h (and so collision, the water atlas and the multiplayer content digest) byte for
byte as pinned here, and the pack's leading water must be what water.h compiles. Update a pin
only for a deliberate change to a map's water.

    PYTHONPATH=tools python3 tests/tools/check_building_packs.py
"""
import hashlib
import json
import re
from pathlib import Path

import map_pack

ROOT = Path(__file__).resolve().parents[2]
# water_sha256 of each map's water.h as of the buildings track's start (B1), 2026-11-20.
PINS = {"river": "f0dc521fd2e657cbe0b615a9e14a6cebf96d039553767575bc979ad82b68cf4c",
        "outer": "a721eb87ca72f679c9a6bfe9e5628e1e3a146de9f018fbb2daa505b9299a114c"}


def main():
    for name, pin in PINS.items():
        directory = ROOT / "assets/geometry" / name
        water = (directory / "water.h").read_bytes()
        manifest = json.loads((directory / "manifest.json").read_text())
        assert hashlib.sha256(water).hexdigest() == pin, f"{name}: water.h changed"
        assert manifest["water_sha256"] == pin, f"{name}: manifest water hash is not the pin"
        pack = (directory / "scenery.bin").read_bytes()
        assert manifest["scenery_sha256"] == hashlib.sha256(pack).hexdigest(), \
            f"{name}: manifest does not describe scenery.bin"
        vertices, triangles, leading, bridge, block = map_pack.read_pack(
            directory / "scenery.bin", True)
        records, styles = block if block else (None, None)
        # The pack's leading water is exactly water.h's, triangle for triangle.
        compiled = re.findall(r"\{ \{ (\d+), (\d+), (\d+) \}, \{ (\d+), (\d+), (\d+), 255 \} \},",
                              water.decode())
        assert len(compiled) == leading, f"{name}: {len(compiled)} compiled water, {leading} packed"
        assert all(ids and all(vertices[i][1] == 0 for i in ids) for ids, _ in triangles[:leading])
        assert any(vertices[i][1] != 0 for i in triangles[leading][0])
        assert [c[:3] for _, c in triangles[:leading]] == [tuple(map(int, c[3:])) for c in compiled]
        # Building detail lies after the water and the records are in range.
        assert records is not None and records, f"{name}: no building records"
        assert all(leading <= r["first"] and r["end"] <= len(triangles) for r in records)
        # No baked window triangles: facade records (wall pairs of their building) hold windows.
        # Attached detail (balconies, canopies, trim) starts at detail_first, after walls and roof.
        assert len(styles) == 6 and all(r["first"] <= r["detail_first"] <= r["end"] for r in records)
        facades = [f for r in records for f in r["facades"]]
        assert facades, f"{name}: no facade records"
        for r in records:
            for f in r["facades"]:
                t, u = triangles[f["wall"]][0], triangles[f["wall"] + 1][0]
                a, c, b = (vertices[i] for i in t)
                assert r["first"] <= f["wall"] and f["wall"] + 2 <= r["end"]
                assert u[0] == t[0] and u[2] == t[1] and a[1] == b[1] and c[1] > a[1]
    check_compositions()
    print("building_packs=ok")


def check_compositions():
    """Every authored composition of the override file is in its bake's manifest and pack: the
    building has a record whose runs use an authored wall role and attached detail."""
    table = json.loads((ROOT / "tools/building_overrides.json").read_text())
    entries = {k: v for k, v in table.items() if not k.startswith("_")}
    assert entries, "no compositions"
    for name in ("river",):
        directory = ROOT / "assets/geometry" / name
        manifest = json.loads((directory / "manifest.json").read_text())
        _, _, _, _, block = map_pack.read_pack(directory / "scenery.bin", True)
        records = {r["hash"]: r for r in block[0]}
        for ident, params in entries.items():
            if params.get("map", "river") != name:
                continue
            assert ident in manifest["compositions"], f"{ident}: not baked"
            assert manifest["compositions"][ident]["family"] == params["family"]
            record = records[map_pack.stable_id_hash(ident)]
            roles = {run[2] for run in record["runs"]}
            assert roles & {map_pack.ROLE_WALL_PLASTER, map_pack.ROLE_WALL_BRICK,
                            map_pack.ROLE_WALL_PANEL}, f"{ident}: no authored wall role"
            assert roles & {map_pack.ROLE_TRIM}, f"{ident}: no roof edge"
            assert record["detail_first"] < record["end"], f"{ident}: no attached detail"
            if "balconies" in params:
                assert map_pack.ROLE_BALCONY in roles, f"{ident}: no balconies"
            if "entrance" in params:
                assert map_pack.ROLE_CANOPY in roles or manifest["compositions"][ident].get(
                    "entrance_canopy") == 0, f"{ident}: no canopy"


if __name__ == "__main__":
    main()
