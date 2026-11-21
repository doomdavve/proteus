# Buildings

Specification of the building data contract as implemented. It grows with the
[buildings plan](BUILDINGS_PLAN.md); sections for source schema, families and facade
construction are added when they exist. Nothing here describes unimplemented behaviour.

## Scenery pack v3 building records

`assets/geometry/<map>/scenery.bin` is written by `tools/map_pack.py` and read by
`src/scenery/boat_scenery.c`. Version 1 is the 32-byte header plus one DEFLATE stream of
vertices and triangles. Version 3 has the same header and stream, then an uncompressed record
block that ends the file exactly (the layout is documented in `tools/map_pack.py`). Version 2,
which baked a quad of triangles per window, was never shipped; the loader rejects it.

- Per building: crc32 of its stable source id (for example `way/164963584`), triangle range
  `[first, end)`, `detail_first` (attached detail starts there; walls and roof come before it,
  and a migrated building has none yet), its run and facade table slices and the bounds of its
  triangles.
- Per run: a triangle range with a role, a facade index and the facade's outward heading, so
  triangles can be grouped by orientation. Roles (`BoatSceneryRole`): `wall` (the fallback building's
  wall; the renderers pick its material from its tint), `wall_plaster`, `wall_brick`, `wall_panel`
  (walls of an authored composition: the role is the material, the colour is the run's own), `roof`,
  `balcony`, `canopy`, `trim` (attached architecture, drawn flat in its own colour) and `detail`
  (anything else, flat). `boat_scenery_role_is_wall` names the four wall roles; only they take
  facade records.
- Per facade (a wall rectangle that takes windows): the first triangle of its wall pair, its number in the
  building, bay and floor counts and a window style. Origin, length, base and top y and heading
  come from the pair's vertices, so draping and rebakes cannot disagree with the mesh. No window
  triangles are stored: the runtime generator expands the grid (see
  [Window generation](#window-generation)).
- Per style: the grid (first bay offset, bay pitch, first floor height, floor pitch, ground and
  upper window sizes, offset from the wall), shared by the facades that name it.
- The leading water, `water.h`, the water atlas and the collision/digest data are not part of the
  records and do not change when records are added: all building triangles follow the water.

The loader accepts version 1 (no records: renderers keep their legacy colour and geometry
classification) and rejects a version 3 pack whose block is truncated, has trailing bytes,
overlaps buildings or runs, places a building inside the water or outside the mesh, repeats an id
hash, has an unknown role, a facade that is not on a wall pair of its building or whose grid does
not fit its wall, or a style that is not finite and positive. A triangle of a building outside
every run is `detail`.
Identity is the id, not the position: the writer sorts records by first triangle and the hash does
not depend on input order or on other buildings.

C access: `boat_scenery_buildings`, `boat_scenery_runs`, `boat_scenery_roles`,
`boat_scenery_facades` and `boat_scenery_window_styles` in `src/scenery/boat_scenery.h`.

## Window generation

`src/scenery/building_windows.[ch]` (in `proteus_boat_scenery`) is the one place that turns a
facade record into windows; it draws nothing and allocates nothing. `building_wall` reads the
wall's origin, end, outward normal, length and top from its triangle pair;
`building_windows` and `building_window` give each window's quad (bottom left, top left, top
right, bottom right, seen from outside), glass colour and whether it is lit at night, in the
style's grid. Ground-floor windows have their own size. `BUILDING_WINDOW_NEAR_RANGE` (430 m) is
the horizontal distance within which the renderers use the quads.

Seeds depend on identity only: the facade seed is `crc32("<id>/<facade number>")` (computed by
continuing the pack's id hash over `/<n>`, so the pack needs no id strings) and a window's seed is
`fmix32(facade_seed + bay * 0x9E3779B1 + floor * 0x85EBCA77)`. Glass is `seed % 7` (0 to 2 mid
blue, 3 pale, else dark); occupied (lit at night) is `((seed ^ (seed >> 15)) * 2654435761) >> 29 <
3`, three in eight. The bay and floor are mixed by an integer function rather than hashed into
the crc string so that the facade shader can repeat the formula exactly. Moving, reordering or
adding other buildings never changes a window; changing a building's id or the number of a wall
(an extra wall before it in the footprint ring) does.

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
  5 upstream; B2 adds two river neighbours, 164963868 and 164964582) are emitted with records. Every
  other building is emitted exactly as before the kit and has no record. With an empty set the packs
  equal the pre-kit packs.
- A migrated building emits the exposed rectangles of its walls (below), then its roofs, then its
  attached detail. Roles: wall runs (one per change of role), one roof run, one run per detail role
  (`trim`, `balcony`, `canopy`, `detail`); `detail_first` is where the attached detail starts. A
  building with `facade=False` (stone) is all `detail`.
- Exposed wall intervals. Each wall edge of each section is cut into the rectangles (u0..u1 along
  the wall, y0..y1) that no solid buries: other sections of the same building (as far as they
  reach, 2 cm tolerance) and neighbouring buildings of the same bake (as far as they reach, 30 cm
  tolerance: a shared or party wall). Only exposed rectangles become wall triangles (the bake's pair
  `a, c, b` and `a, d, c` per rectangle) and only they take facade records, so an opening never
  stands on a buried wall nor crosses a part, ground band or roof boundary. Pieces under 0.9 m are
  dropped. A section's roof is its footprint minus the volumes standing on it. A building emitted on
  its own (the outer adapter) has no neighbours; only its own sections bury walls.
- Windows are facade records, not triangles. Window style 0 is the legacy grid (first bay 2.5 m
  in, a bay every 4 m; floors from 1.5 m every 3.2 m; ground floor windows 2.2 x 1.9 m, upper 1.5
  x 1.5 m; glass 3.5 cm off the wall). A rectangle of the fallback gets a record when it is at least
  5.5 m high and `grid()` fits at least one window clear of the corners (0.3 m) and the top (0.5 m).
  The facade number counts every rectangle in emission order, windowless ones included. A composed
  building uses one of the five further styles by family and band (below); its rectangles take
  records from 3 m high.
- Draping into the outer map (`prepare_outer_map.py`): `Overlay` keeps each river triangle's pack
  index and building record. A recorded building is one solid for draping (lowest terrain under
  its base footprint), so raised parts and detached features follow it; other solids are grouped
  by shared corners as before. Records follow their triangles into the outer pack.

```sh
# needs docs/adventure/requirements.txt (shapely, pyproj, numpy)
python tools/building_kit.py                           # kit and composition fixtures
python -I tools/prepare_waterfront.py                  # river pack; do this first
python -I tools/prepare_outer_map.py                   # outer pack (reads the river pack)
```

## Compositions (B2)

`tools/building_overrides.json` holds authored compositions keyed by stable source id; the bake reads
the entries whose `map` is its own (default `river`). `building_kit.load_compositions` validates them
(`tools/building_compose.validate_params`): unknown keys, non-finite or out-of-range numbers, bad
colours, ascending setback levels, an entrance without a ground band and ids that are not
`way/N` or `relation/N` are refused, and the emitter refuses an id that names no building of the bake,
a building that is not in `MIGRATED`, a stone building or one split into pieces. A building without
an entry is the plain fallback and is built as in B1. All lengths are metres, heights above the
building's lowest base.

```json
"way/164963867": {
  "family": "balcony", "wall": [238, 226, 196], "roof": [112, 116, 124], "trim": [244, 240, 232],
  "ground": {"band": 4.0, "wall": [182, 150, 112], "style": "lobby"},
  "setbacks": [{"above": 21.5, "inset": 2.2}], "parapet": 0.8, "cornice": 0.35,
  "balconies": {"sides": ["west", "north", "south"], "span": 1, "every": 1, "depth": 1.7,
                "slab": [222, 218, 210], "front": [190, 92, 62]},
  "entrance": {"side": "west", "at": 0.5, "width": 3.0, "canopy": 2.0, "canopy_color": [190, 92, 62]}
}
```

- `family` (`balcony`, `stepped`, `brick`) chooses the default material (plaster, panel, brick) and
  window style (`balcony` 1.7 x 1.9 m on a 4 m bay, `ribbon` 2.6 x 1.5 m piers 1.4 m, `punched` 1.2 x 1.8
  m on 3 m); `material` and `style` override them. `ground` splits a band off the bottom of every
  wall (own colour, material and a `shop` or `lobby` glazing style of 3 m windows). Colours are the
  wall (`wall`), roof (`roof`) and trim (`trim`: cornice, parapet, piers, fascia).
- `setbacks` recede the part above a level: `inset` all round or `cut` away from one side
  (`west`, `east`, `south`, `north` of the pack axes, `depth` metres, cumulative). The section splits at the
  level; footprints only shrink (checked by a fixture), so the ground footprint never grows. The
  terrace left by a setback is its lower section's roof; its parapet is drawn on the exposed roof
  edge only.
- `parapet` (height of a 0.32 m wall on the roof edge, role `trim`) and `cornice` (a 0.55 m band
  projecting from the wall below the roof, cut back where it would touch a neighbour, water or a
  vehicle street).
- `balconies`: on exposed rectangles facing a listed `side`, a balcony `span` bays wide on every
  `every`-th bay (from bay `offset`) on floors `from_floor` to `to_floor` (negative counts from
  the top) of the window grid. A balcony is a slab, a solid front panel and two side panels (role
  `balcony`, `slab` and `front` colours), 40 triangles. Each is rejected, and counted in the bake
  manifest, when its footprint (grown 10 cm) touches the building's own footprint, a neighbour that
  reaches that height, water (grown 0.5 m) or a vehicle street (6 m wide ways; footways may pass under).
- `entrance`: on the ground piece best facing `side` and long enough, an interval `width` wide at
  fraction `at` of the wall has no window. It gets piers and a lintel (`trim`), a door leaf
  (`detail`), a step, and a canopy (role `canopy`) `canopy` metres deep that shrinks in 0.4 m steps
  until it clears the same obstacles, or is dropped; `fascia` adds a signboard band over the whole
  ground floor of that wall.
- The bake records per composed building the balconies placed and rejected and the canopy depth in
  `manifest.json` (`compositions`).

Clearances come from geometry the bake knows: the river water and vehicle streets (obstacles) and
the other buildings of the bake (their footprints as far as they rise). Authored attached
architecture does not read the street furniture, which is generated at run time; the generic
frontage and forecourt passes (`waterfront_corner.c`) skip walls with an authored wall role, and
their `space()` test already treats any horizontal surface above the quay (balcony slabs, canopies,
roof edges) as occupied, so benches and planters are not placed under them. The minimap
ignores `balcony`, `canopy` and `trim` triangles, which would otherwise fatten a footprint.

B2 compositions on the Eriksberg quay row (ids and colours in the file): brick shopfront block
164963584 with its brick wing 164963868 and yellow-brick 164963897; balcony blocks 164963867 (cream,
terracotta fronts) and 164964582 (grey-blue, ochre fronts), both with a recessed top storey; pale
stepped block 164963869 (two terraces receding from the quay end, glazed lobby) and 164964585.

## Rendering consumers

Both renderers read roles through `boat_scenery_roles` and never classify a migrated building's
triangles by colour or geometry (`waterfront_facade_role` in `src/render/waterfront_corner.[ch]`),
and take its windows from the generator (`building_windows.h`) only:

- Materials: the GPU facade builder takes the wall material from the role for authored walls
  (`wall_plaster`, `wall_brick`, `wall_panel`) and their own colour from the triangles; a `wall`
  role keeps the retained palette chosen from the tint. `roof` goes to the roof mesh; `balcony`,
  `canopy`, `trim` and `detail` go to the flat mesh in their colours, so shadow and reflection
  passes (which draw the same static meshes) include them. Tile, cell and record bounds come from the
  vertices of the triangles in them, so projecting detail is inside them (checked by `boat_scenery`);
  the 0.1 m growth only matters for window quads 3.5 cm off a wall.
- Generic windows (the `waterfront_window_wall` grid, seeded by triangle index) are skipped for
  every triangle with a role, in the CPU windows pass and in the GPU `build_windows`, so a migrated
  wall never gets two window sets. Unmigrated buildings and packs without records keep the legacy
  path unchanged, including its index-seeded windows. `waterfront_window_glass` and
  `waterfront_window_occupied` call the generator's functions, so there is one palette.
- CPU (`facade_windows` in `src/render/waterfront.c`): every frame, each building within
  `BUILDING_WINDOW_NEAR_RANGE` (430 m, horizontal) and in view expands its facades that face the
  camera into glow quads tinted by the window light. Beyond the range the CPU view draws no
  windows, as for generic ones.
- GPU (`src/render/renderer_gpu.c`, `assets/shaders/waterfront_city.*`): wall triangles with role
  `wall` join the facade mesh (the `a, c, b / a, d, c` pairing is the kit's emission order), `roof`
  the roof mesh, `detail` the flat mesh. Each frame `window_select` expands the 512 m detail cells
  within the near range plus a 96 m prefetch of the followed boat (the camera without a player)
  into window meshes drawn by the windows pass, and releases cells beyond twice the range. Beyond
  430 m the facade shader draws the same grid: each wall vertex carries its facade record number
  (the y of its normal, free on a horizontal wall) and `facadeData`, a three-texel RGBA32F row per
  record, gives the shader the seed halves, counts and style. The shader repeats the generator's
  seed, glass and occupancy formulas, filters the window rectangle by the pixel footprint and
  settles on the palette mean and the lit share (3/8) once a pixel spans about a bay, so the far
  facade neither shimmers nor pops against the quads. The mask and the quads overlap between 430
  and 526 m; they are the same windows, so nothing doubles visibly. More than 32,767 facade
  records cannot be named by the vertices: all cells then expand and stay (not exercised).
- `tools/ambient_bake.c` classifies facades by role for migrated buildings.
- Known side effect: the river kit's frontage pass (planters, benches, trims) keys on triangle
  indices and the window seeds of unmigrated buildings use them too. With the kit emitting walls
  and roofs only, the river kit is again 7944 boxes with the pre-kit digest; B2 moves the
  frontages to facade runs.

### Rebaking and reviewing the Eriksberg group

```sh
# needs docs/adventure/requirements.txt (shapely, pyproj, numpy)
python -I tools/prepare_waterfront.py && python -I tools/prepare_outer_map.py
build/ambient_bake --map river --out assets/geometry/river/ambient.bin
build/ambient_bake --map outer --out assets/geometry/outer/ambient.bin
# Look at the quay row in a window (GPU), from the basin:
./build/wake_velocity --location outer --start -5,80,200 --demo-idle
```

## Generated outputs and packaging

`scenery.bin`, `manifest.json` (building record counts) and `water.h` come from the two bakes;
`ambient.bin` is geometry-keyed and must be rebaked after every pack change
(`ambient_bake --map river|outer --out assets/geometry/<map>/ambient.bin`; CTest `ambient_<map>`
fails when stale). `water.h` and the water atlas are unchanged by a rebake (CTest
`building_packs`). The macOS package and the Windows workflow ship the outer map's
`water_atlas.bin` and `ambient.bin` next to its pack (they were missing from them before B1); the
web build preloads the river pack and serves the outer pack with both sidecars lazily, by
globbing `assets/geometry/*/*.bin`, so a version 2 pack needs no list change.
