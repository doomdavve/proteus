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

## Shared building kit (`tools/building_kit.py`)

Both bakes describe a building as a `BuildingDescription` (stable id, sections with bottom and
top, wall and roof colour, optional override key) and emit it through one `Emitter`:
`prepare_waterfront.py` (flat 3.5 m ground) and `outer_dressing.py` (terrain; gabled houses still
use their own gable builder). `validate` rejects bad ids, duplicate ids, non-finite or inverted
heights, empty, invalid or degenerate footprints, bad colours and override references that name no
entry. Descriptions are emitted in stable-id order, so input order does not matter.

- Ids: `way/N` or `relation/N`; a footprint clipped to the dressing area gets `#K` for its K-th
  piece. The river crop's buildings carry the bare id.
- Migration: only ids in `building_kit.MIGRATED` (B1: 16 pilot buildings, 11 in the river crop and
  5 upstream) are emitted with records. Every other building is emitted exactly as before the kit
  and has no record. With an empty set the packs equal the pre-kit packs.
- A migrated building emits all its walls (the bake's pairs `a, c, b` and `a, d, c`, outward for
  exterior rings and into courtyards), then the roofs, then baked window quads. Roles: one wall
  run, one roof run, one window run per facade with its outward heading; `detail_first` is the
  first window. A building with `facade=False` (stone) is all `detail`.
- Windows: the legacy grid (2.5 m in, every 4 m; floors from 1.5 m every 3.2 m; none on walls under
  5.5 m) as two triangles per window, glass colour from `crc32("<id>/<facade>/<bay>/<floor>")`;
  the alpha byte is occupancy (255 lit at night). `glass` and `occupied` mirror the C functions.
- Draping into the outer map (`prepare_outer_map.py`): `Overlay` keeps each river triangle's pack
  index and building record. A recorded building is one solid for draping (lowest terrain under
  its base footprint), so raised parts and detached features follow it; other solids are grouped
  by shared corners as before. Records follow their triangles into the outer pack.

```sh
# needs docs/adventure/requirements.txt (shapely, pyproj, numpy)
python tools/building_kit.py                           # kit fixtures
python -I tools/prepare_waterfront.py                  # river pack; do this first
python -I tools/prepare_outer_map.py                   # outer pack (reads the river pack)
```

## Rendering consumers

Both renderers read roles through `boat_scenery_roles` and never classify a migrated building's
triangles by colour or geometry (`waterfront_facade_role`, `waterfront_baked_window` in
`src/render/waterfront_corner.[ch]`):

- Generic windows (the `waterfront_window_wall` grid, seeded by triangle index) are skipped for
  every triangle with a role, in the CPU windows pass and in the GPU `build_windows`, so migrated
  walls never get two window sets. Unmigrated buildings and packs without records keep the legacy
  path unchanged, including its index-seeded windows.
- CPU: `waterfront_draw_boating` leaves window triangles out of the land draw and draws each baked
  quad as a glow quad tinted by the window light (daytime glass; occupied windows lit at night),
  with the generic window's camera-facing and 430 m limits.
- GPU: wall triangles with role `wall` join the facade mesh (the `a, c, b / a, d, c` pairing is the
  kit's emission order), `roof` the roof mesh, `detail` the flat mesh; window triangles are not in
  the flat mesh but in the detail-cell window meshes, near and far. Baked windows are not merged
  for the far level yet (a B4 lever); legacy windows still merge 2x2.
- `tools/ambient_bake.c` classifies facades by role for migrated buildings.
- Known side effect: the river kit's frontage pass (planters, benches, trims) keys on triangle
  indices and the window seeds of unmigrated buildings use them too, so adding baked windows
  shifts them. The B1 rebake changed the river kit from 7944 to 7980 boxes (clearances
  unchanged); B2 moves the frontages to facade runs.

## Generated outputs and packaging

`scenery.bin`, `manifest.json` (building record counts) and `water.h` come from the two bakes;
`ambient.bin` is geometry-keyed and must be rebaked after every pack change
(`ambient_bake --map river|outer --out assets/geometry/<map>/ambient.bin`; CTest `ambient_<map>`
fails when stale). `water.h` and the water atlas are unchanged by a rebake (CTest
`building_packs`). The macOS package and the Windows workflow ship the outer map's
`water_atlas.bin` and `ambient.bin` next to its pack (they were missing from them before B1); the
web build preloads the river pack and serves the outer pack with both sidecars lazily, by
globbing `assets/geometry/*/*.bin`, so a version 2 pack needs no list change.
