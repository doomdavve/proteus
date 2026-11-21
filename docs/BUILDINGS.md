# Buildings

Specification of how buildings go from mapped source to screen, as implemented: the pack
records, window generation and lighting, the shared kit, authored compositions, automatic
families, the report and override mechanism, the renderers' consumption and the generated
outputs. It is the contract; the history, measurements and decisions are in the
[buildings plan](BUILDINGS_PLAN.md) (done 2026-11-21). Nothing here describes unimplemented
behaviour. To trace one building from source to screen, start at the
[source and coordinates](#source-coordinates-and-heights) section, then follow
[the report](#the-report-and-one-way-to-override-a-building).

## Source, coordinates and heights

Footprints, heights and tags come from the OpenStreetMap crop that `tools/prepare_waterfront.py`
reads (the river crop, X -1500..950, Z -1600..800 m; see `3D.md`) and from the outer map's
dressing source read by `tools/outer_dressing.py` (everything else on `outer`). Pack coordinates are
metres in the pack's axes; the simulation's x is minus the pack's x (heading 0 faces -Z). A
building is a stable source id (`way/N`, `relation/N`, `#K` for the K-th piece of a footprint
clipped to the bake area), one or more sections (footprint polygon, bottom and top) and tags.
Heights: the `height` tag, else `building:levels` times 3 m plus 1 m, else a footprint-scale
estimate the report marks as `fallback`; parts partition their parent by height (highest overlapping
part wins). The river crop stands on a flat 3.5 m quay; the outer map drapes each building on the
lowest terrain under its footprint (`prepare_outer_map.py`), so raised parts follow it. Provenance
of each choice is in `buildings.json` (see the report below).

Boundaries: the collision data, the water, `water.h` and the digests ignore buildings, so a rebake
of building geometry never changes simulation (CTest `building_packs`). Fallbacks, in order:
override entry, supported facts, inference, plain block; a geometry element the triangulator
refuses is left out, never a failed bake.

## Scenery pack v4 building records

`assets/geometry/<map>/scenery.bin` is written by `tools/map_pack.py` and read by
`src/scenery/boat_scenery.c`. Version 1 is the 32-byte header plus one DEFLATE stream of
vertices and triangles. Version 4 has the same header and stream, then the record block: a u32 raw
size, a u32 crc32 of the stream, and a second raw DEFLATE stream that ends the file exactly. The
block's three record arrays (buildings, runs, facades) are stored byte-plane first (all first
bytes, then all second bytes), which deflates to half the size of the plain arrays: the river
block is 86 kB deflated plain and 46 kB shuffled. The loader checks the crc before inflating
(the inflater does not bound corrupt input, so a truncated or damaged stream is refused first) and
restores the arrays in place. The layout is documented in `tools/map_pack.py`. Version 3 (the
same block stored raw) is still read and no longer written; version 2, which baked a quad of
triangles per window, was never shipped and is rejected.

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
  building, bay and floor counts, a window style and a lighting profile (the record's last byte,
  below 4; see [Window lighting](#window-lighting)). Origin, length, base and top y and heading
  come from the pair's vertices, so draping and rebakes cannot disagree with the mesh. No window
  triangles are stored: the runtime generator expands the grid (see
  [Window generation](#window-generation)).
- Per style: the grid (first bay offset, bay pitch, first floor height, floor pitch, ground and
  upper window sizes, offset from the wall), shared by the facades that name it.
- The leading water, `water.h`, the water atlas and the collision/digest data are not part of the
  records and do not change when records are added: all building triangles follow the water.

The loader accepts version 1 (no records: renderers keep their legacy colour and geometry
classification) and rejects a version 3 or 4 pack whose block is truncated, has trailing bytes,
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

### Window lighting

A window is lit only if it is occupied (three in eight, as above); `building_window_lit(profile,
seed, minute)` then says whether its schedule has the light on at a minute of the day, 0 to 1439.
The facade record's profile picks the schedule, and `tools/building_kit.py` assigns it per
facade: shop, lobby and ribbon glazing is `workplace`, a warehouse or any other non-house building
of an industrial district is `industrial`, a shed is `quiet`, everything else `residential`; an
authored `lighting` key (`residential`, `workplace`, `industrial`, `quiet`) in
`tools/building_overrides.json` replaces the choice. The facade record's profile byte is the only
lighting data in the pack, so the pack version is unchanged (0, residential, is what older packs
hold).

| Profile | Evening | Overnight |
| --- | --- | --- |
| residential | every occupied window on at 16:00-20:00, off at 22:00-01:00 | 3 in 16 until 05:00; 2 in 16 light 05:00-07:00 to 08:00 |
| workplace | on through a working evening, off at 18:00-22:00 | 1 in 16 all night |
| industrial | 1 in 4 until a 21:00-23:00 shift end | 1 in 16 all night, 1 in 16 early |
| quiet | 1 in 4 until 19:00-21:00 | none |

The offsets are drawn from the window seed (`fmix32(seed + 0x7F4A7C15)`: the low 4 bits pick the
window's role, the next two bytes place its switch-on and switch-off inside the ranges), so a
window keeps its habits and neighbours differ. The function is integer only and
`window_lit` in `assets/shaders/waterfront_city.fs` repeats it exactly. The clock is
`WaterfrontLighting.window_clock/window_minute`, set by `waterfront_lighting_hour` (the fixed
`--lighting-hour` and the Working Harbour clock); a fixed daylight phase has no clock and lights
every occupied window as before. Window light itself (how bright the windows are at the hour) is
unchanged and independent. The GPU window quads carry the seed in their texture coordinates and
the profile in the two low bits of the vertex alpha (its lit share is the upper six), so the shader
decides each frame and nothing is rebuilt when the hour changes; the facade mask reads the
profile from the facade record's texel and settles distant facades on
`building_window_lit_share(profile, minute)`, the lit fraction of all windows at that minute,
uploaded as `windowMean`. A merged far window of the untagged generic windows stands for its
first window's seed. Unmigrated generic windows follow `residential`. No building family yields
`quiet` facades by default (houses and sheds take no windows); authored entries can.

Windows have a reveal in three tiers, all from this module so the CPU and GPU draw the same faces.
`building_window_faces` splits the pane into four coplanar faces: the glass, a dark band under the
lintel (14% of the height, glass x 0.4), a mid band on the left jamb (8% of the width, x 0.7) and
a light sill (8% of the height, glass mixed half with stone). Each face's alpha is its share of the
night window light (occupied glass 255, lintel 77, jamb 153, sill 0). This is the impression the
whole near range (430 m) draws, and `window_mask` in `assets/shaders/waterfront_city.fs` repeats
the same bands beyond, settling toward a slightly darker mean once a window is about a pixel.
Within `BUILDING_WINDOW_DEEP_RANGE` (40 m, cut from 80 m at the user's request, 2026-11-21: the camera must be close to see the relief) `building_window_reveal` adds real relief: sill,
lintel and jamb returns 16 cm deep, a 7 cm rim over the lintel and under the sill on the outer
plane and, on ground-floor windows 1.8 m or wider, a mullion (7 faces at most, none behind the
wall plane; a fuller frame of 9 faces at 120 m cost about +0.8 ms gpu p50 at the quay row, this
one none measurable). The wall is a
flat quad pair that the window does not cut, so the glass is sunken by a surround that projects
from the wall rather than by a hole in it; callers grow wall and cell bounds by
`BUILDING_WINDOW_MARGIN` (0.3 m). The GPU keeps one merged relief mesh set of the facades whose wall is
within the deep range of the followed boat (a facade leaves it 20 m beyond), rebuilt when that set
changes, so the pass adds a few submissions however many facades are near.

Seeds depend on identity only: the facade seed is `crc32("<id>/<facade number>")` (computed by
continuing the pack's id hash over `/<n>`, so the pack needs no id strings) and a window's seed is
`fmix32(facade_seed + bay * 0x9E3779B1 + floor * 0x85EBCA77)`. Glass is `seed % 7` (0 to 2 mid
blue, 3 pale, else dark); occupied (lit at night) is `((seed ^ (seed >> 15)) * 2654435761) >> 29 <
3`, three in eight. The bay and floor are mixed by an integer function rather than hashed into
the crc string so that the facade shader can repeat the formula exactly. Moving, reordering or
adding other buildings never changes a window; changing a building's id or the number of a wall
(an extra wall before it in the footprint ring) does.

## Commands and tests

```sh
python -I tools/map_pack.py --self-test        # writer, round trip, invalid records, v1
build/validate_boat_scenery                    # loader cases (CTest: boat_scenery)
build/validate_building_windows                # generator, schedules (CTest: building_windows)
```

Other entry points (all in `ctest --preset full`; the Python ones need the tool dependencies):
`building_kit` and `building_families` (fixtures of the emitters and the family rules),
`building_packs` (shipped packs: the 32,767 facade limit, water and digest unchanged), `building_report` (every override names a baked building), `ambient_river` and
`ambient_outer` (ambient occlusion matches the pack), `waterfront_lighting` (the window clock stays
out of the blend). Tests drive a virtual minute, never the wall clock.

### Adding a family or a window style safely

1. Add the recipe to `tools/building_families.py` (a choice rule from facts, then a build) and its
   fixture in the same file; a family only changes colour, material, window style, roof shape,
   doors and cheap setbacks, never invents a shop or an entrance.
2. A new window style is a row in `STYLES` of `tools/building_kit.py` and a name in
   `building_compose.STYLE_NAMES`; the loader validates the grid, and `validate_building_windows` pins the generator.
3. Rebake both packs, then `ambient_bake` for both maps, then `python3 tools/building_report.py
   --check`; the pack-size and facade-count budgets are checked by `building_packs`.
4. Look at it (CPU capture and GPU, day, dusk and night) before trusting the numbers.

## Shared building kit (`tools/building_kit.py`)

Both bakes describe a building as a `BuildingDescription` (stable id, sections with bottom and
top, wall and roof colour, optional override key) and emit it through one `Emitter`:
`prepare_waterfront.py` (flat 3.5 m ground) and `outer_dressing.py` (terrain; gabled houses still
use their own gable builder). `validate` rejects bad ids, duplicate ids, non-finite or inverted
heights, empty, invalid or degenerate footprints, bad colours and override references that name no
entry. Descriptions are emitted in stable-id order, so input order does not matter.

- Ids: `way/N` or `relation/N`; a footprint clipped to the dressing area gets `#K` for its K-th
  piece. The river crop's buildings carry the bare id.
- Migration (B3): every mapped building is emitted with records: the river crop's buildings and
  parts (`prepare_waterfront.py`) and the outer dressing's (`outer_dressing.py`, gabled houses
  included). The only buildings on the pre-kit path (walls then roof, no record, classified by the
  renderers from colour and geometry) are the explicit `"legacy": true` entries of
  `tools/building_overrides.json` and descriptions built with `migrate=False`; the report names them
  (none ship). Buildings near the fort are stone: all `detail`, flat, no windows.
- A building emits the exposed rectangles of its walls (below), then its roofs, then its
  attached detail. Roles: wall runs (one per change of role), one roof run, one run per detail role
  (`trim`, `balcony`, `canopy`, `detail`); `detail_first` is where the attached detail starts. A
  building with `facade=False` (stone) is all `detail`.
- Exposed wall intervals. Each wall edge of each section is cut into the rectangles (u0..u1 along
  the wall, y0..y1) that no solid buries: other sections of the same building (as far as they
  reach, 2 cm tolerance) and neighbouring buildings of the same bake (as far as they reach, 30 cm
  tolerance: a shared or party wall). Only exposed rectangles become wall triangles (the bake's pair
  `a, c, b` and `a, d, c` per rectangle) and only they take facade records, so an opening never
  stands on a buried wall nor crosses a part, ground band or roof boundary. Pieces under 0.9 m are
  dropped. A section's roof is its footprint minus the volumes standing on it. Both adapters give the
  emitter the whole bake's footprints as context, so terraces and party walls bury each other's
  walls in the outer map too.
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
  "balconies": {"sides": ["west", "north", "south"], "unit": 3, "span": 1, "every": 1, "depth": 1.7,
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
- `balconies`: apartments. On exposed rectangles facing a listed `side`, the window grid is cut into
  units `unit` bays wide (2 to 4); every `every`-th unit (from unit `offset`) has one balcony,
  `span` bays wide (1 or 2, at most the unit) and centred in its unit, in front of its glazing; the
  other windows stay plain. A unit's balconies form one aligned stack on floors `from_floor`
  (default 1: the ground floor has none) to `to_floor` (default -2, negative counts from the top, so
  the top floor has none either). A balcony is a slab, a solid front panel and two side panels (role
  `balcony`, `slab` and `front` colours), 40 triangles. A stack is kept whole or dropped whole, never
  ragged: it is rejected when any level's footprint (grown 10 cm) touches the building's own
  footprint, a neighbour that reaches that height, water (grown 0.5 m) or a vehicle street (6 m
  wide ways; footways may pass under). Fewer, larger balconies also cost fewer triangles.
- `entrance`: on the ground piece best facing `side` and long enough, an interval `width` wide at
  fraction `at` of the wall has no window. It gets piers and a lintel (`trim`), a door leaf
  (`detail`), a step, and a canopy (role `canopy`) `canopy` metres deep that shrinks in 0.4 m steps
  until it clears the same obstacles, or is dropped; `fascia` adds a signboard band over the whole
  ground floor of that wall.
- The bake records per composed building the balconies and stacks placed and the stacks rejected and the canopy depth in
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

## Families, districts and the building report (B3)

`tools/building_families.py` chooses one composition for every building that has no authored
entry, from facts the source supports, in this order: (1) mapped facts: the `building` tag, levels,
height, `roof:shape`, `building:material`, `building:colour`, `roof:colour`, `shop` and `amenity`;
(2) conservative inference from footprint scale, height and the district the adapter names; (3) a
plain block, with the reason recorded. Choices the facts do not decide come from the crc32 of the
stable source id, so a building keeps its look when anything else changes. Missing data never
implies a shop, a public entrance or a balcony: a ground-floor shop band needs a `shop` or food
`amenity` tag, entrances are authored only, and balcony stacks need `building=apartments` (or
`residential` when large).

| Family | Chosen for | Built as |
| --- | --- | --- |
| `house` | detached, house, terrace, villa, farm and similar tags; small untagged footprints under 9 m | plaster walls in the house palette, `cottage` windows, gable roof on a near-rectangular footprint |
| `shed` | garage, shed, carport, boathouse, kiosk, tank and silo tags; footprints under 40 m2 | panel walls, no windows, flat roof (gable for boathouse/cabin types) |
| `warehouse` | industrial, warehouse, service (150 m2 and up) tags; untagged 400 m2+ buildings in the port | broad panel walls, a high `hall` window row, large doors (up to 3) on the longest ground wall, low gable or sawtooth roof (sawtooth: 1,500 m2+ in the port, one in three) |
| `brick` | `building:material=brick`, else about 3 in 10 apartment blocks | brick walls, `punched` windows |
| `block` | other apartments, residential, commercial, office, civic, parking | plaster (most) or panel walls with `punched` or `ribbon` windows; a shop band where tagged |
| `balcony` | one in four apartment blocks over 12 m and 300 m2 (not in the port) | plaster walls, `balcony` windows and one balcony stack |
| `stepped` | 45 m and taller with 300 m2 or more | panel walls, `ribbon` windows, a recessed top storey |

Districts come from the adapters: the river crop is `urban`; in the outer map the port (footprints
near industrial buildings or over 3,000 m2) is `industrial` (grey and rust panels, warehouses), a
footprint with eight or more neighbours within 50 m is `urban` (pastel plaster, brick, the B2
families), the rest within 150 m of water is `coastal` and otherwise `suburban` (house palette,
gabled roofs). The district decides palette and the industrial rule; it never invents a use.

Nothing in an automatic composition adds triangle-heavy relief (parapets, cornices, rails): the
pack budget cannot afford it for 6,000 buildings, and the B2 hero compositions keep theirs. What
differs between buildings is colour, material, window style, roof shape, doors, setbacks of towers
and (a few) balcony stacks, all cheap. `roof_shape` `gable` and `sawtooth` roof a single section
over a near-rectangular footprint (at least 0.9 of its minimum rotated rectangle, 0.8 for houses,
which a gabled house footprint is replaced by); courtyards, several sections and irregular outlines
keep the flat roof and the report says so (`roof`).

Safe fallbacks: a footprint the triangulator refuses for an edge or roof is left out of that
element (never a failed bake); an assignment the validator rejects gives the plain building (the
B1 fallback: one wall role, legacy window grid) with the reason in the report; walls shorter than
0.9 m are kept whole and never split; fully buried buildings keep their roof; courtyards of 30 m2
and more stay holes in the outer map (smaller ones are filled). More than 32,767 facade records
cannot be named by the GPU vertex format: the bake test (`building_packs`) refuses it, and the
shipped packs hold 6,287 (river) and about 26,000 (outer, which includes the river kit).

### The report and one way to override a building

Each bake writes `assets/geometry/<map>/buildings.json` (generated; the river report covers the
river crop's buildings, the outer one the buildings the outer map dresses): per stable source id
the family, how it was chosen (`override`, `override+auto`, `tags`, `inferred`, `fallback`, `plain`,
`legacy`), the district, the reason, the roof, the source facts (tag, footprint, height and where
the height came from), its facade, window and triangle counts and detail counts, and a summary
(family and basis counts, fallback reasons, legacy ids). `tools/building_report.py` reads it
with the standard library only:

```sh
python3 tools/building_report.py                          # distribution, fallbacks, legacy
python3 tools/building_report.py way/164963867            # one building (also 164963867, w/123)
python3 tools/building_report.py --family warehouse --map outer --limit 20
python3 tools/building_report.py --fallbacks              # buildings built without enough facts
python3 tools/building_report.py --check                  # every override names a baked building
```

To change a building, add an entry keyed by its source id to `tools/building_overrides.json` (the
report prints a starting point), rebake and look again; no C changes:

```json
"way/1204511650": {"auto": true, "map": "outer", "family": "block", "wall": [236, 208, 130]},
"way/164963867": {"family": "balcony", "...": "a full authored composition, as in B2"},
"way/55949698":  {"legacy": true, "map": "outer", "note": "why this stays on the pre-kit path"}
```

An `auto` entry starts from the automatic result and replaces the keys it names (a new `family`
rebuilds that family's recipe from the same facts); an entry without `auto` is a full authored
composition and needs its `family`; a `legacy` entry keeps the pre-kit walls and roof with no
records. Precedence: override entry, supported facts, inference, plain fallback. `map` names the
bake that reads the entry (`river` by default, `outer` for the outer dressing); ids are `way/N` or
`relation/N`, with `#K` pieces of a clipped footprint sharing their base id's entry. The validator
refuses unknown keys, out-of-range numbers, bad colours and ids that name no building of the bake.
Added keys: `lighting` (the window schedule, see Window lighting), `roof_shape` (`flat`, `gable`, `sawtooth`), `windows` (false: none), `doors` (`count`,
`width`, `height`, `color`: large doors on the longest ground wall) and `balconies.max_stacks`;
families `block`, `warehouse`, `house` and `shed`; window styles `cottage` (1.0 x 1.1 m on a 3 m bay)
and `hall` (2.2 x 1.3 m, a row from 4.6 m up, above the doors).

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
  the 0.1 m growth only matters for window quads 3.5 cm off a wall, and the CPU view grows a building's
  box by `BUILDING_WINDOW_MARGIN` (0.3 m) for the window relief.
- Generic windows (the `waterfront_window_wall` grid, seeded by triangle index) are skipped for
  every triangle with a role, in the CPU windows pass and in the GPU `build_windows`, so a migrated
  wall never gets two window sets. Unmigrated buildings and packs without records keep the legacy
  path unchanged, including its index-seeded windows. `waterfront_window_glass` and
  `waterfront_window_occupied` call the generator's functions, so there is one palette.
- CPU (`facade_windows` in `src/render/waterfront.c`): every frame, each building within
  `BUILDING_WINDOW_NEAR_RANGE` (430 m, horizontal) and in view expands its facades that face the
  camera into glow quads tinted by the window light (the reveal bands for all, the relief within
  `BUILDING_WINDOW_DEEP_RANGE` of the camera). Beyond the range the CPU view draws no
  windows, as for generic ones.
- GPU (`src/render/renderer_gpu.c`, `assets/shaders/waterfront_city.*`): wall triangles with role
  `wall` join the facade mesh (the `a, c, b / a, d, c` pairing is the kit's emission order), `roof`
  the roof mesh, `detail` the flat mesh. Each frame `window_select` expands the 512 m detail cells
  within the near range plus a 96 m prefetch of the followed boat (the camera without a player)
  into window meshes drawn by the windows pass (and, per facade, the relief meshes within the deep
  range, see Window generation), and releases cells beyond twice the range. Beyond
  430 m the facade shader draws the same grid: each wall vertex carries its facade record number
  (the y of its normal, free on a horizontal wall) and `facadeData`, a three-texel RGBA32F row per
  record, gives the shader the seed halves, counts and style. The shader repeats the generator's
  seed, glass and occupancy formulas, filters the window rectangle by the pixel footprint and
  settles on the palette mean and the profile's lit share at the hour (3/8 with no clock) once a pixel spans about a bay, so the far
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
./build/wake_velocity --location outer --start -5,80,200   # drive it yourself
./build/wake_velocity --location outer --start -5,80,200 --demo --demo-idle   # boat held still
```

## Generated outputs and packaging

`scenery.bin`, `manifest.json` (building record counts) and `water.h` come from the two bakes;
`ambient.bin` is geometry-keyed and must be rebaked after every pack change
(`ambient_bake --map river|outer --out assets/geometry/<map>/ambient.bin`; CTest `ambient_<map>`
fails when stale). `water.h` and the water atlas are unchanged by a rebake (CTest
`building_packs`). The macOS package and the Windows workflow ship the outer map's
`water_atlas.bin` and `ambient.bin` next to its pack (they were missing from them before B1); the
web build preloads the river pack and serves the outer pack with both sidecars lazily, by
globbing `assets/geometry/*/*.bin`, so a new pack version needs no list change.
