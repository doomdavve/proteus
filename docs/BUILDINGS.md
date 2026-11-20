# Buildings

Specification of the building data contract as implemented. It grows with the
[buildings plan](BUILDINGS_PLAN.md); sections for source schema, families and facade
construction are added when they exist. Nothing here describes unimplemented behaviour.

## Scenery pack v2 building records

`assets/geometry/<map>/scenery.bin` is written by `tools/map_pack.py` and read by
`src/scenery/boat_scenery.c`. Version 1 is the 32-byte header plus one DEFLATE stream of
vertices and triangles. Version 2 has the same header and stream, then an uncompressed record
block that ends the file exactly (the layout is documented in `tools/map_pack.py`):

- Per building: crc32 of its stable source id (for example `way/164963584`), triangle range
  `[first, end)`, `detail_first` (attached detail such as windows starts there; walls and roof
  come before it), its run table slice and the bounds of its triangles.
- Per run: a triangle range with a role (`wall`, `roof`, `window`, `detail`), a facade index and
  the facade's outward heading, so triangles can be grouped by orientation.
- The leading water, `water.h`, the water atlas and the collision/digest data are not part of the
  records and do not change when records are added: all building triangles follow the water.

The loader accepts version 1 (no records: renderers keep their legacy colour and geometry
classification) and rejects a version 2 pack whose block is truncated, has trailing bytes,
overlaps buildings or runs, places a building inside the water or outside the mesh, repeats an id
hash, or has an unknown role. A triangle of a building outside every run is `detail`.
Identity is the id, not the position: the writer sorts records by first triangle and the hash does
not depend on input order or on other buildings.

C access: `boat_scenery_buildings`, `boat_scenery_runs`, `boat_scenery_roles` in
`src/scenery/boat_scenery.h`.

## Commands

```sh
python -I tools/map_pack.py --self-test        # writer, round trip, invalid records, v1
build/validate_boat_scenery                    # loader cases (CTest: boat_scenery)
```
