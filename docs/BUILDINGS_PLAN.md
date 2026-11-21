# Buildings and waterfront architecture overhaul

Status, 2026-11-21: B0 done. B1 and B2 implemented; the user accepted the B2 look (balcony stacks,
projecting-surround reveal, 40 m relief range; no cut-wall holes). B3 implemented locally: every
mapped building (river crop and outer dressing) is built from facade records through automatic
families plus overrides, pack v4, `tools/building_report.py`; awaiting the user's review of the B3
stills and a playtest (no PR, no user acceptance). Checks, numbers and limits:
[B3 results](#b3-results--2026-11-21).
Goal: inviting, stylized buildings with coherent silhouettes, facade depth and street edges.
Scope: Eriksberg pilot, reusable families across the outer city, and a finished generation spec.
Open: B3 look in motion (user), expansion hitch and distance/motion/platform qualification (B4).
Done when: the implementation is verified and reviewed, rollout is complete, and BUILDINGS.md
specifies the shipped source-to-rendering system with reproducible authoring commands.
Next: the user's review of B3, then B4 qualification. Delivery: one local branch.

## Intent and boundaries

The user selected building generation/modelling as the next overhaul after comparing a real
Eriksberg waterfront photograph with the game. Photorealism is explicitly outside the goal.
At the default integer-scaled resolution, neighbouring buildings should have distinct,
coherent personalities, readable entrances and convincing relationships to the quay.
Reuse code and architectural ideas throughout the city without assigning arbitrary detail
to every wall or turning every district into the same waterfront apartment development.

This track owns permanent building architecture: massing, roofs, facade composition,
windows, balconies, entrances, shopfronts, attached awnings, canopies and small terraces.
It includes plinths, stoops, short forecourt/retaining walls and railings, and the shared
construction primitives needed for low waterfront pavilions and warehouses. Placement of
standalone related structures is limited to the pilot's immediate building edges; this is not a new city-wide street-furniture or marina-layout system.

Defer the crane, bridges, landmark redesign, ferries, pontoons, water shading and animation,
people/cyclists and traffic. Existing landmark geometry and authored places must survive the
rollout. This does not resume the paused harbour gameplay track. The open loops in
[the index](README.md#open-loops), especially graphics/platform review, living-city actors,
marina routes, plugin P5 and architectural restructuring, retain their owners. Coordinate
shared-file changes with the architecture track; this plan does not reorganize the application.

## Starting point and ownership

Code inspection, not a new visual or performance baseline:

- `tools/prepare_waterfront.py` extrudes mapped footprints, including mapped building parts,
  and records source identity and height provenance in its manifest.
- `tools/prepare_outer_map.py` combines the river crop, terrain, dressing and upstream city.
  Its river overlay/draping reconstructs objects from shared XZ corners rather than parent
  identity; detached architectural features need an explicit parent/base to avoid bad grounding.
  `tools/outer_dressing.py` supplies a separate broader-building importer and simple roof
  choices; audit courtyard filling and rectangular gable approximations during normalization.
- `tools/map_pack.py` writes version-1 scenery packs: vertices and coloured triangles, without
  explicit building or facade identity. `src/scenery/boat_scenery.[ch]` loads/checks those packs.
- `src/render/waterfront.c` and `src/render/renderer_gpu.c` independently derive windows from wall triangles.
  Surface classification relies on palette/geometry conventions; occupancy seeds use triangle
  indices. New massing could accidentally change window identities or receive duplicate windows.
- `src/render/waterfront_corner.c` also supplies existing shops/entrances and masks competing generic
  windows at the cafe; migrate frontage decisions rather than stacking a second system.
- Geometry-keyed `ambient.bin` assets need rebaking after geometry changes; verify dependent
  atlas validity, browser loading and packaging rather than assuming a scenery-only change.
- `assets/shaders/waterfront_city.*` and the GPU renderer own material shading; shared building
  geometry should not require matching CPU and GPU pixel output.

[Living city](LIVING_CITY_PLAN.md) remains the owner of actors and historical dressing work.
[3D.md](3D.md) owns general renderer behaviour; [the performance guide](BOATING_PERFORMANCE.md)
owns benchmark procedures. The new `docs/BUILDINGS.md`, created during implementation and
completed in B5, will own the present-tense building construction contract. Do not copy this
proposal into a spec and describe it as implemented.

## Proposed construction model

Resolve source features into explicit building descriptions before emitting detail. Use one
shared construction policy for the river kit, outer dressing and upstream buildings, with
adapters for their source/terrain differences. Prefer offline baking for static architecture;
retain only the presentation metadata actually needed by runtime consumers. B0/B1 must settle
where shared feature generation lives so Python and the two C renderers do not each implement
an independent architectural grammar.

Keep descriptions bounded and immutable at runtime, with render data owned by the bound view;
no general scene graph, Lua API, network messages or building gameplay state is needed.

A description needs stable source/part identity, mapped footprint and holes, terrain/base and
height provenance, family, palette, oriented facade runs, floor/bay rhythm and explicit feature
choices. These are design requirements, not a prescribed struct or pack layout. Retain source
facts separately from artistic assumptions. Authored overrides reference stable source IDs,
with precedence and validation, rather than triangle offsets or incidental iteration order.

Selection order: explicit override, supported mapped facts, conservative family inference,
then a simple fallback. Derive feature/occupancy seeds from stable building/facade/bay/floor
identities, never triangle indices or transient list positions. Variation is stable per building,
with a small number of family-level choices. Windows, balconies, roof and ground floor follow one coherent composition. Missing
data is not evidence for a shop, public entrance or elaborate setback on every facade.

Initial family candidates (final selection follows B0 inventory):

| Family | Readable character |
| --- | --- |
| Waterfront apartments | Balcony stacks, pale walls, selective stepped upper floors |
| Brick urban blocks | Grouped openings, strong roof edge, substantial ground floor |
| Modern towers | Vertical facade sections, recesses and deliberate roof termination |
| Harbour warehouses | Broad volumes, large doors, pitched or sawtooth roof where appropriate |
| Small coastal buildings / pavilions | Compact proportions, pitched or simple flat roofs, restrained trim |

Preserve mapped footprint envelopes, courtyards, meaningful height/part relationships and
terrain placement. Any artistic change to massing must be explicit and bounded; balconies,
awnings and terraces need clearance rules for roads, water, adjacent buildings and existing
scene dressing. Recess upper volumes rather than expanding the ground footprint by default.
Do not place doors blindly on courtyard/party walls or treat every footprint edge as frontage.

Building detail is presentation data. Do not derive navigable water, physics contacts or room
compatibility from decorative triangles. Establish what existing collision and content digests
actually consume before changing the pack. A presentation-only rebake should preserve compiled
water and simulation behaviour; any unavoidable compatibility change must be named and handled,
not hidden behind an assumption that scenery never affects compatibility.

## Execution steps

Each step gets a focused local commit, documentation status and actual verification evidence.
Keep one branch and eventual PR for this milestone. B2 is the artistic review point before
broad rollout; complete its concrete implementation/captures before seeking user feedback.

### B0 — Inventory, baseline and decisions (done 2026-11-20)

- Trace all building sources, mapped parts, source manifests, draping, runtime consumers,
  city dressing and CPU/GPU window/material classification. Include minimap and browser loading.
- Select exact source IDs for a group of neighbouring Eriksberg buildings and comparison
  districts: an upstream urban block, industrial reach and smaller coastal settlement.
- Capture fixed boat-level near/mid/far views, daylight, dusk and night, plus a short approach route.
  Save reproducible camera/time/options and inspect default resolution first; high resolution
  is a diagnostic. Keep baseline artifacts under `build/buildings/`.
- Measure Release geometry counts, submissions, memory/pack size, binding time/upload bytes
  and frame-time samples using the performance guide. Set numeric incremental budgets from these results before B2.
- Record decisions for source schema, shared emission location, metadata transport, stable
  identity/seeds, collision separation, family selection and fallback handling.

Acceptance: a bounded pilot list, reproducible baseline and concrete contract decisions exist.
No invented source facts, performance gains or visual acceptance. Met as recorded in
[B0 results](#b0-results--2026-11-20), with the limits named there. Next: B1.

### B1 — Shared building descriptions and emission

- Add the smallest explicit presentation contract that supports the pilot and all source
  adapters. Validate stable IDs, dimensions, finite values, holes, parts, facade orientation,
  material roles and override references. Define deterministic ordering and seed policy.
- Carry parent/part identity and placement through river-to-outer draping, including detached
  balconies/canopies and roof features. Never ground each disconnected feature independently.
- Implement the chosen shared feature emission path. CPU and GPU consume the same building
  placement/composition; preserve GPU-specific shading and distant-detail optimizations.
- Replace RGB/tall-triangle recognition for migrated buildings with explicit semantic roles.
  Prevent old auto-windows from being added to new walls, balconies, canopies or roof faces.
- If pack metadata changes, update writer/reader, version/failure behaviour, lifecycle and web
  loading in the same step. Do not assume a format upgrade is mandatory before choosing it.
- Start BUILDINGS.md with only the implemented data/ownership contract and actual commands.

Acceptance: simple fallback buildings exercise the full pipeline in native CPU/GPU and browser;
no duplicate windows, changed water or simulation results. Identity survives reordered input
and unrelated feature additions. Existing source adapters and landmarks still work. Next: B2.

### B2 — Eriksberg architectural pilot (implemented 2026-11-21, awaiting the user's review)

- Implement a small neighbouring group using at least three clearly distinct compositions:
  balcony-heavy apartment block, pale stepped block and a contrasting brick/solid block,
  subject to mapped geometry. Include one purposeful entrance/shopfront treatment.
- Add the reusable massing, roof, balcony, facade and attached ground-floor primitives needed
  for these buildings. Facade records must track exposed wall intervals so openings do not
  appear on buried/shared walls or cross part/roof boundaries. Prefer broad depth and shadow
  shapes to thin railings or texture noise.
- Integrate or replace overlapping existing entrances, shopfronts and cafe dressing deliberately.
  Check inward/outward normals, terrain joins, corners, party walls and projection clearances.
- Compare matched baseline views and approach motion. Check daylight, dusk, reflections,
  near/far shadows and low-resolution legibility; measure against B0 budgets.

Acceptance: a player can distinguish individual buildings and read their relation to the quay
at ordinary boat distance. Supply captures and a runnable local review command; record the
user's visual review separately from automated checks. Refine this group before B3 rollout.

### B3 — City-wide reuse and district character

- Complete the selected family set and integrate all mapped building source paths through it.
  Assign families conservatively from source facts, scale/context and stable authored choices.
- Preserve explicit landmark overrides and building parts. Handle courtyards, short/angled
  walls, irregular polygons, holes, terrain slopes and sparse tags with safe fallbacks.
- Apply a broad baseline of character; avoid one rich pilot amid unchanged generic neighbours.
  Keep industrial, dense urban and coastal districts visibly different.
- Validate known source IDs, family distribution and fallback reasons in an author-readable
  report. Provide one documented way to inspect or override a building without editing C.

Acceptance: matched comparison districts show appropriate variety and complete coverage, with
no disappeared buildings, doubled parts or obvious intersections. Next: B4 qualification.

### B4 — Distance, motion and platform qualification

- Keep silhouette and large balcony/facade rhythms at distance; simplify fine features before
  they shimmer. Reuse existing tiling/batching rather than one draw per building detail.
- Regenerate geometry-keyed ambient-occlusion assets and check other dependent assets for
  staleness; verify native/browser packaging includes all required outputs.
- Verify geometry bounds include projecting/rooftop features in primary, shadow and reflection
  passes. Check detail transitions in an approach and lateral traversal, including dusk windows.
- Measure the same Release samples as B0 and record incremental cost, scene/pack memory and
  browser loading implications. If budgets fail, simplify/merge/cull and repeat affected checks.
- Exercise native GPU, CPU captures and the browser on this machine. Name wider-platform and
  human-review gaps explicitly; use Instruments if measurements warrant deeper investigation.

### Runtime cost levers (updated 2026-11-20)

Variant 2 took levers 1 and 3 in B1: windows are generated, not stored, and the GPU draws quads
only for cells near the followed boat and a shader mask beyond 430 m (`docs/BUILDINGS.md`). Still
available for B4, none measured: (2) group windows by facade orientation per cell so back-facing
groups are skipped whole; (4) instancing for repeated windows and balconies; (5) occlusion by the
nearest quay row at boat level, only if measured to pay; (6) drop windows, or use the mask, in
shadow and reflection passes (the far shadow map has 1.5 m texels; reflections already draw the
mask beyond the range). Cell expansion runs in the frame that needs it; spreading it over frames
is a B4 candidate if a hitch is measured.

Acceptance: measured budgets hold, required automated checks pass and review artifacts expose
motion/pop/shadow/reflection behaviour. Automated demo exit is not human acceptance. Next: B5.

### B5 — Close with the implemented specification

- Finish BUILDINGS.md as the source-to-rendering spec: source/provenance and coordinates;
  identities and parts; terrain/height policy; recipe selection and override precedence;
  parameters/units and validation; massing/roof/facade/attached-structure construction;
  shared geometry and material roles; windows/occupancy; pack/load/lifetime ownership;
  CPU/GPU/browser behaviour; distance/culling; collision/digest boundaries and fallback rules.
- Include reproducible offline rebuild and inspection commands, generated output ownership,
  a small real override example, testing entry points and how to add a family safely.
- Update affected authoring/rendering guides; search README.md, docs and relevant asset guides
  for superseded building-generation claims, particularly live summaries in 3D.md,
  LIVING_CITY_PLAN.md and FREE_RIDING_ADVENTURE_FIRST_STEPS.md. Check local links and command examples.
- Convert leftovers into explicit open items or drop them deliberately. Record implementation,
  automated verification, manual playback and user acceptance separately; mark this plan done
  only when the required implementation, review and spec are complete.

Acceptance: another contributor can trace and modify a real building from retained source to
screen using the spec, without reverse-engineering triangle conventions. Update the index's
area summary and link the spec. No PR/push without the user's explicit approval.

## Verification requirements

Run the Debug configure/build and `ctest --preset full` before each implementation handoff;
renderer changes enable `PROTEUS_GPU_TESTS=ON`. Use focused meaningful fixtures for stable
identity, invalid contracts, geometry bounds/orientation, part/hole handling, overrides and
fallbacks, pack round trips/failure cases where changed, and unchanged water/simulation.
Do not test each decorative vertex or encode screenshots as universal cross-platform truth.

Inspect representative deterministic CPU/GPU captures when geometry changes, and run a
windowed demo for changed asset loading or runtime behaviour. Browser verification covers the
changed source-to-load path, not just successful compilation. Performance claims require
same-machine repeated Release measurements with options and commits recorded. Tests use
explicit simulated time. No timing sleeps in validators.

## Planning evidence — 2026-11-11

The user requested Astra assistance and an eventual implementation plus a generation spec.
Astra reviewed the architecture and staged scope; this plan also follows local source inspection.
This change is planning only: no new building implementation, audiovisual review, performance
measurement or acceptance is claimed. Verification results are recorded below after execution.

Planning verification, 2026-11-11, Apple Silicon / macOS 26.6.2: Debug configure/build
passed using the main checkout's pinned dependency sources with disconnected fetching; GPU
CTest checks were off. The first configure lacked a populated Lua source path; supplying the
existing source resolved it. Bundled raylib emitted its existing stb_vorbis pointer-comparison
warning. The sandboxed full suite hit local-socket restrictions; a complete rerun with socket
access passed all 539 enabled tests in 45.21 seconds (13 retired experiments disabled). Logs:
`/private/tmp/proteus-building-{configure,build,ctest-desktop}.log`. Local link targets in the
new plan and updated index exist, the Open loops anchor resolves, and `git diff --check`
passes. No application or generated asset changes; no new audiovisual review is required for
this planning-only change. Implementation checks in B0–B5 remain unperformed.

## B0 results — 2026-11-20

B0 changed no application code or assets. Facts below come from code reading and the runs
named; items marked Proposal are not decided and are for B1 to confirm. Nothing here is user
approval or visual acceptance.

### Pilot source IDs (proposal)

Coordinates are metres east/north of the crane origin in the pack frame (simulation x is
the negative of pack x); IDs are OSM way IDs unless marked `r/`. Heights and level counts come
from the OSM corpus and the river manifest; the manifest lacks levels, type and roof shape.

| Group | IDs (pack x, z) | Why |
| --- | --- | --- |
| Eriksberg quay row (river kit) | 164963584 (6 lev, 19 m; 64,43), 164963867 (8, 25 m; 68,77), 164963869 (12, 37 m; 67,111), 164963897 (8; 151), 164964585 (6; 103,187) | Contiguous waterfront apartments with 6/8/12-level contrast, the B2 balcony/stepped/solid trio |
| Eriksberg contrast | 303925538 (height tag 17; -37,34); 13-level trio 303925539 / 282574762 / 115318537 (-99..-85, 59..-42) | Explicit height and tower massing |
| Eriksberg low neighbours | 503087601 (h5; -75,152), 503087593 (h3; -57,83) | Sheds next to the cafe/kit origin (-71.8, 105.9); frontage migration check |
| Excluded | 15956173 Eriksbergs bockkran and parts 15956174-6 | Landmark; deferred |
| Urban block, Masthugget/Majorna (outer dressing) | `w/88426613` Saluhall Briggen (2454,106); `w/1204511650`, `1204511655`, `1204511659`, `1204511647` (2272-2330, 59-85); `w/1204721415`, `16`, `17`, `22` (8 levels; 2268-2304, 194-241); `w/1204721425` (2882 m2, 8 levels) | Dense, mixed height tags, large footprints: brick-block test |
| Industrial reach, Lindholmen/Gotaverken (outer dressing) | `w/31511593` (14571 m2), `w/31511573` (9672), `w/27121500` (6919, h5), `w/191842847` (3100, h5); alternates: Skarvik tanks `w/88285707`, `88285757`, `88285662`, `88285770` (-755, -41..-197); sheds `w/99291093` (-2053,-479), `w/99293244` (-2385,-349) | Broad volumes and sheds for the warehouse family |
| Coastal settlement, Langedrag basin (outer dressing) | `w/55949698` Sjoradningshuset (-3573,-3106), `w/55949705` Nimbus boatyard (-3494,-3208), `w/55949706` GKSS clubhouse, `w/74395834` commercial (-3538,-3480), houses `w/74395845`, `74395850`, `74395899` | Gable/house family, boathouses, no tagged heights |

The outer manifest keeps no per-building IDs (only aggregate dressing counts), so the outer
IDs were verified against the OSM corpus, not against the baked pack. Whether the baked pack
contains each of them was not checked.

### Baseline: commands, cameras, artifacts

Artifacts live in `build/buildings/` (untracked; regenerated by
`build/buildings/capture_baseline.sh`, which is itself untracked and must be rebuilt from this
section if `build/` is lost). Exact commands are in `build/buildings/commands.log`; images in
`img/`, measurements in `perf/`, pack statistics in `pack_stats.txt`. Every capture is
`wake_velocity --renderer cpu --location outer --start X,Z,H --demo-idle [--daylight dusk |
--lighting-hour 0] --screenshot FILE --time 1` at the default 320x200 (Debug build);
simulation x is minus pack x, heading 0 faces -Z, 90 faces +X, 270 faces -X.

| View | Start X,Z,H | Looks at |
| --- | --- | --- |
| eriksberg-near / mid / far | -200,60,160 / -300,-100,160 / -50,-420,175 | Quay row at ~70 m, ~300 m, ~620 m (day, dusk, night) |
| urban-masthugget | -2300,500,0 | The Masthugget/Majorna bank; nearest navigable water, ~250 m north of the candidate block (z 59-241) |
| industrial-lindholmen | -2250,980,170 | Lindholmen shore; the Gotaverken reach (z 1200-1350) is further inland and not reachable by boat (-2250,1000 and -2200,1000 are not navigable) |
| coastal-langedrag | 3650,-3250,270 | Langedrag basin quay and villas, ~100 m off the candidate buildings |

Approach route: non-idle scripted drive from -50,-420,175 sampled at `--time` 0, 4, 8, 12 (it
turns and is not a fixed approach to the quay). GPU captures: `eriksberg-near-gpu.png` and
`-gpu-high.png` (diagnostic). An earlier first set of urban/industrial/coastal cameras did not
sit at the candidate districts (the industrial one showed mostly quay wall and cranes); those
were replaced on 2026-11-20 and the old images moved to `build/buildings/old/`.

Observations from the inspected stills (a visual impression, not a review): near/mid day
blocks look like uniform grey or pale boxes with regular window grids, flat parapets and
unreadable entrances; dusk and night read through lit windows only; the GPU high-resolution
view adds door canopies and brick/pale variety but no balconies or roof detail. The
Masthugget view shows large grey blocks with window grids and flat roofs; the Lindholmen
view shows pale mid-rise blocks, not industrial buildings, so the Gotaverken sheds have no
boat-level baseline view yet; Langedrag shows low coloured villas with pitched roofs, the best
existing variety.

### Measurements

Machine: MacBook Pro M1 Max, macOS 26.6.2, AppleClang 21, commit 41f3420 (clean tree), on
battery power. Release (`-O3`) for measurements, Debug for CPU captures. Noise: frame and GPU
times varied 10-25% between runs on battery, so treat times as indicative; counts below are
deterministic.

| Measure | Value |
| --- | --- |
| Pack sizes | outer `scenery.bin` 5,370,005 B (619,497 triangles, 361,664 vertices, 3,731 water, 534,517 land+buildings; 14.25 MB payload); river 852,701 B (78,612 triangles) |
| `ambient.bin` | outer 772,854 B; river 79,603 B |
| Outer GPU geometry | 284 meshes, 1,367,735 triangles; vertex 72.46 MB + index 8.21 MB = 80.7 MB; far-LOD 250,348 triangles |
| Classes (triangles) | city 316,413; facades 59,052; roofs 28,370; quay 212,905; corner 89,542; windows 652,766 (+202,578 far); river kit 347,390 |
| `boat_water_review --benchmark --aa off --scenario outer`, 3 runs, 12,003 frames, 320x200 | frame p50/p95 1.98/5.14, 1.86/4.89, 1.55/4.24 ms; render_submit p50 1.15-1.26, p95 1.5-1.7 ms; gpu p50 1.13-1.56, p95 2.1-2.4 ms; submissions 144/236 and scene triangles 744,616/1,440,442 (p50/p95, identical across runs) |

Fixed-pose runs (600 frames, visible window, `--no-vsync`, 3 runs; p50):

| View | render ms | gpu ms | submissions | scene triangles |
| --- | --- | --- | --- | --- |
| eriksberg-near | 1.38-1.40 | 3.30-3.34 | 176 | 1.32 M |
| eriksberg-mid | 1.46-1.47 | 3.38-3.40 | 204 | 1.39 M |
| eriksberg-far | 1.64-1.76 | 3.90-3.94 | 256 | 1.62 M |
| urban-masthugget | 3.47-3.95 | 4.05-4.24 | 130 | 1.43 M |
| industrial-lindholmen | 3.90-4.02 | 3.03-3.09 | 124 | 1.00 M |
| coastal-langedrag | 4.19-4.26 | 4.50-4.65 | 225 | 1.22 M |

Frame 0 of each run costs about 280-295 ms of render submit (first draw and upload of the
outer scene); this is a proxy, not an isolated bind time. No dedicated bind-time or
upload-byte counter exists; upload bytes are the geometry sizes above. One Debug CPU capture
takes about 0.45 s wall. The render ms of the three new views is higher than the Eriksberg
views at fewer submissions, which was not investigated.

Proposed budgets (proposals, to be re-measured on AC power with the same commands before B2;
the counts are stable, the times are not):

| Budget | Proposal |
| --- | --- |
| `scenery.bin` | <= +20% (<= 6.4 MB); `ambient.bin` within +20% |
| Static outer geometry | <= +15% triangles (<= 1.57 M) and <= +10% GPU bytes (<= 89 MB) by B3; B2 pilot <= +60k triangles |
| Eriksberg near/mid views | scene triangles <= +10%, submissions <= +8 (batch into existing classes) |
| Time | render p50 <= +0.15 ms, gpu p50 <= +0.4 ms in near/mid and benchmark; p95 <= +0.3/+0.6 ms; 3 runs on AC |
| First-frame upload | <= +15% (about 320 ms) |

### Claims checked against code

- Window winding (checked by derivation, 2026-11-20). `Builder.prism` in
  `tools/outer_dressing.py` emits each wall as `quad(a, b, c, d)` with `a`,`b` on the bottom
  edge, and `face()` flips a triangle whose normal opposes the outward direction. For an edge
  (dx, dz) the normal of (a, b, c) is (-dz h, 0, dx h) and the outward direction is (dz, 0,
  -dx), so the first triangle is always flipped to (a, c, b) and the second to (a, d, c),
  whatever the ring orientation. This matches the river generator and the predicate in
  `waterfront_window_wall` (base edge vertex 0 and 2, vertex 1 high). Not run against a baked
  pack triangle by triangle; the outer captures show windows on dressing walls. Gabled houses
  use `HOUSE_WALLS` colours and get no windows by design.
- Leading water. `boat_scenery_load_bytes` rejects a pack whose `boat_map_leading_water`
  differs from the header or whose water differs triangle for triangle from the compiled map
  (`src/scenery/boat_scenery.c:104-111`, `water_matches` at :60). Collision and the content
  digest read only the compiled `water.h`. So a rebake that leaves the leading water
  triangles and their order unchanged, with all new geometry after them, cannot change them
  and the loader would refuse otherwise. Both tools already record `water_sha256` in the
  manifest. Verified by reading code; no test that rebakes and compares exists yet.

### Decisions

Proposals for B1 to confirm, except where marked. The user decided on 2026-11-20: scenery pack v2
(version bump accepted), baked window quads, and no baseline for the unreachable Götaverken reach.

| Topic | Proposal | Notes |
| --- | --- | --- |
| Source schema | One offline `buildings.json` per map (or manifest section), keyed by normalised OSM id (`way/ID`; outer's `w/`, `r/` mapped to it) with parent id, part id, base or raised part, footprint rings with holes, base and top y, height with provenance (tag, levels, fallback, terrain lift), a small tag subset (building, levels, roof shape, material), family, palette and optional override id. Source facts are stored apart from assumed fields. | Today the river manifest keeps only osm, height, basis, courtyards, parts; the outer map keeps nothing per building |
| Draping | Carry parent ids through `river_overlay` instead of rebuilding solids from shared XZ corners | Needed for detached balconies and canopies |
| Shared emission | New `tools/building_kit.py`, called by `prepare_waterfront.py` (flat-ground adapter) and `outer_dressing.py` (terrain adapter); retire the two private extrusion and roof paths. Architecture is baked into triangles offline. | The C renderers stay consumers; no second grammar |
| Window layout in C | **Decided 2026-11-20 (variant 2, replacing the baked-quads decision of the same day): the pack stores facade records (wall pair, bay and floor counts, window style), not window triangles.** One C generator, `src/scenery/building_windows.[ch]`, expands them into quads when a tile or building is within `BUILDING_WINDOW_NEAR_RANGE` of the camera; beyond it the facade shader draws the same grid as a mask. The grid loops in `waterfront.c` and `renderer_gpu.c` are retired for migrated buildings (the legacy path stays for unmigrated ones). | Baking every building would add about 170k triangles (0.85 to 3.4 MB) to the river pack alone; records add about 0.18 MB at full rollout. |
| Metadata transport | **Decided: pack v2** with a trailing record block (loader accepts v1 and v2). Not chosen: an optional sidecar `assets/geometry/<map>/buildings.bin`, keyed by the same mesh FNV as `ambient.bin`, with per-building first/last triangle, stable id hash, roles and facade-run table; plus `detail_first`. | Sidecar keeps v1 and the loader's version check and tolerates absence, but a stale or missing sidecar fails silently (the `ambient.bin` hazard) and needs a lifecycle, web and package entry. Pack v2 makes metadata exact and atomic with the mesh, at the cost of a version bump in writer, reader, tests and web.  |
| Identity and seeds | Facade seed = crc32 of the stable id and facade number (`crc32("<id>/<facade>")`, computed by continuing the pack's id hash), window seed = murmur3 `fmix32(facade_seed + bay * 0x9E3779B1 + floor * 0x85EBCA77)`; the glass and lit rules are the legacy ones. Replaces `waterfront_window_seed(triangle, ...)` for migrated buildings; the frontage trims `(i/2)%4` and the `id % 4` tint still need a family palette keyed by id hash (B2). | Proposal that deviates from a literal `crc32(id, facade, bay, floor)`: bay and floor are mixed by an integer function so that the facade shader reproduces the seed exactly (a shader cannot format and hash a decimal string). Identity is still the id, the facade number, the bay and the floor. `validate_waterfront_lighting.c:337` pins the legacy seed signature, which stays for unmigrated buildings. |
| Collision separation | New detail sits after the leading water; `water.h` and the atlas stay byte-identical; add a test comparing `water_sha256` before and after a rebake; classify detail by explicit role, not exact RGB | No content-digest change expected; verify by test, not assumption |
| Family selection | Order: explicit override by source id, supported mapped facts (tags, parts), conservative inference from footprint, height and context, then a simple fallback. Missing data never implies a shop or public entrance. Report the family and fallback reason per building. | Family list stays provisional until B3 |
| Fallback in C | When the sidecar is absent or stale, keep the legacy tint and triangle-index path unchanged and versioned; never mix both on one building (suppress generic windows where baked ones exist rather than masking geometrically as the cafe does) | Keeps old packs, test maps and `cargo_sandbox` rendering |
| Frontages and cafe | Migrate `build_frontages` and `build_cafe` to per-facade-run descriptions; delete index-based trims | They are tied to river-crop massing (a.y == 3.5, c.y >= 12, length >= 9, hard-coded cafe origin) |

### Hazards found

- `tools/macos/package.py` copies only `manifest.json` and `scenery.bin`, so the macOS bundle
  omits `ambient.bin` and `water_atlas.bin` (ambient occlusion and the water atlas are already
  absent there). The Windows workflow was not checked. Pre-existing; fix or confirm in B4 at
  the latest, and B1 if the sidecar ships.
- A stale `ambient.bin` is silently ignored by the loader (optional sidecar; the web loader only
  warns), so scenery just loses occlusion. Only the `ambient_<map>` CTest checks (and
  `tools/ambient_bake --check`) catch it; any vertex, index, colour or order change forces a
  rebake of both river (80 KB) and outer (773 KB).
- The minimap draws any non-green, near-horizontal triangle at least 4 m above local ground, so
  terraces, balcony slabs and roof volumes can fatten building footprints.
- Cull and tile bounds are grown by only 0.1 m, so projections beyond that can be culled;
  roof overhangs are not in the window/detail wall cells; thin balcony slabs may alias in the
  1.5 m far shadow texels (from code reading; not exercised).
- New wall triangles of building tint at least 5.5 m high with a flat base automatically get
  windows (setbacks, party walls); detail in another colour escapes window, facade, roof and
  ambient classification. Roof detection is colour arithmetic (tint minus 30).
- Windows use the triangle index as seed in both renderers, so any added or reordered triangle
  reshuffles lit patterns, entrances and trims building-wide; the facade pairing in
  `renderer_gpu.c` assumes pairs in a,c,b / a,d,c order.
- The river pack is also the kit layout source in the outer map, so river and outer packs must
  stay consistent. Web preloads the river pack and lazily fetches the outer pack (5.37 MB),
  so pack growth is a download cost.

### Verification and limits

Performed: code tracing; the commands above on one machine (Apple M1 Max, macOS 26.6.2,
battery power); inspection of the stills by the implementing agent; the derivation above.
Not performed: browser or any other platform; AC-power or idle-machine repeats; pixel
comparison of captures (GPU and CPU outputs are not expected to match); user visual review;
a rebake or water-hash test; checking that every outer source ID exists in the baked pack;
a Gotaverken boat-level view (not reachable); an isolated bind-time or upload-byte counter.
The first run of the per-view performance reports failed when launched from a backgrounded
script (cause not isolated); relaunching with stdin from `/dev/null` and per-run logs worked.
Starting poses that were not navigable (the game exits non-zero): -600,-60,140, -800,100,110,
-500,-100,150, -400,-350,150, 3000,-700,180, -2350,420,0, -2350,350,0, -2250,1000,180,
-2300,1100,180, -2200,1000,180, 3620,-3150,270. No documentation outside this plan changed,
so no other live summary was affected; the index row stays accurate.

## B1 results — 2026-11-20 (superseded in part by the rework below)

Implemented (commits on feat/buildings, see `git log`): scenery pack v2 with trailing building
records (loader still accepts v1); `tools/building_kit.py` with `prepare_waterfront.py` and
`outer_dressing.py` adapters; parent identity carried through river-to-outer draping
(`Overlay` in `prepare_outer_map.py`); explicit-role consumers in the CPU and GPU renderers and
`ambient_bake`; rebaked river and outer packs and `ambient.bin`; packaging fix. Contract and
commands: [BUILDINGS.md](BUILDINGS.md).

Scope choices to review. Only 16 pilot buildings are migrated (`building_kit.MIGRATED`; 11 river
crop, 5 upstream). Migrating all buildings bakes about 170k window triangles into the river pack
alone (0.85 to 3.4 MB, preloaded by the web build), far over the proposed +20% pack budget, so
the rollout (B3) must decide that cost first. With the pilot set: river `scenery.bin` 852,701 to
891,823 bytes, outer 5,370,005 to 5,417,388 bytes (+0.9%); +2,860 and +3,728 baked window
triangles. Unmigrated buildings are triangle for triangle as before (checked: removing the baked
windows from the river pack gives the previous pack's triangle list exactly; the outer pack
differs in 16 non-window triangles, the pilot building that now drapes as one solid).

Checks run (Apple M1 Max, macOS 26.6.2, Debug, `PROTEUS_GPU_TESTS=ON`, shared busy machine):
`python -I tools/map_pack.py --self-test`; `tools/building_kit.py` and
`tools/prepare_outer_map.py --self-test` under the pinned requirements (venv); both bakes
re-run offline (the unchanged sources reproduced the pre-B1 packs byte for byte first); water.h of
both maps byte-identical (CTest `building_packs`); `ambient_river`/`ambient_outer` rebaked and
passing; `boat_scenery` extended (v2 round trip, 15 invalid-record cases, v1 compatibility,
migrated-building role and window checks on both shipped packs); `city_dressing` digest updated
(river kit 7944 to 7980 boxes, clearance digest unchanged, caused by index-keyed frontage
planters). `ctest --preset full` (PROTEUS_GPU_TESTS=ON): 634 of 634 passed. CPU captures at the Eriksberg
near/mid views and Masthugget (day, dusk, night) in `build/buildings/new/` against the B0
baseline in `build/buildings/img/`: differences are confined to lit-window patterns of buildings
after the first pilot building (index-seeded legacy windows), baked windows visible and lit at
night; GPU captures (default resolution) of near day and night inspected.

Not verified: browser/web (no emscripten here; by reading, the web build globs the pack files
and runs the same loader, but a v2 pack in the wasm build and the 2 packs' download times are
untested); native windowed demo run beyond scripted captures; Windows and Linux; macOS package
and Windows workflow edits (sidecar copies) not run; Release performance and the B0 budgets
(not re-measured); far-window merging for baked windows (drawn one to one in both levels, a
B4 lever); user visual review.

## B1 rework results — 2026-11-20

The user decided "variant 2" on 2026-11-20: the pack stores compact facade-run data, not window
triangles. Commits on feat/buildings: facade records and pack v3 (kit, writer, loader, tests,
rebaked packs and ambient); the window generator `src/scenery/building_windows.[ch]` and tests;
CPU and GPU wiring (CPU expansion within 430 m; GPU cells near the anchor plus a facade-shader
mask beyond). Contract: [BUILDINGS.md](BUILDINGS.md).

Choices to review (proposals): (1) the facade record names the wall pair and keeps counts and a
style; origin, length, base, top and heading come from the pair's vertices rather than being
stored again, so draping cannot disagree (the user listed those fields; they are available, not
duplicated); (2) seeds are `crc32("<id>/<facade>")` then `fmix32` with bay and floor, not a crc of
all four, so the shader can reproduce them exactly; (3) near range 430 m (the CPU's old limit; a
1.5 m window is about one pixel there at 320x200), 96 m prefetch, release beyond 860 m; (4)
windows now fit inside the wall (0.3 m from corners, 0.5 m under the top), so pilot buildings have
1,239 (river) and 1,623 (outer) windows instead of about 1,430 and 1,860 baked; (5) walls buried
in another section of the same building get none; (6) mask: filtered by the pixel footprint,
settling on the palette mean and a 3/8 lit share once a pixel spans a bay.

Sizes (bytes). B0: river 852,701, outer 5,370,005. Baked pilot (B1 before the rework): 891,823 and
5,417,388. Now: river 854,285 (+0.2% on B0), outer 5,372,445 (+0.05%). Experiment, not committed
(every building migrated): river 1,033,047 (+21%; 1,498 buildings, 5,038 facades, 78,826
windows; baking them would have been about 3.4 MB), outer 5,987,482 (+11.5%; 4,914 buildings,
18,587 facades, 307,373 windows). The record block is uncompressed and regular; B3 should
compress it or shrink the building and run records if the +20% pack budget binds. 18,587 facades fit the
vertex format's 32,767.

Checks run (Apple M1 Max, macOS 26.6.2): `map_pack.py --self-test`; `building_kit.py` fixtures
and `prepare_outer_map.py --self-test` (venv with the pinned shapely/pyproj/numpy); both bakes
re-run (the unmodified bakes reproduced the committed packs first); `water.h` byte-identical
(CTest `building_packs`); river kit back to 7,944 boxes and its pre-kit digest; Debug
`ctest --preset full` with `PROTEUS_GPU_TESTS=ON`: 635 of 635 passed, including new
`building_windows`, extended `boat_scenery` (v3 and 28 invalid-record cases) and
`detail_transitions` (window cells expand, release and re-expand). GPU stills at 1280x800 (near,
mid, far; day, dusk, night) with the mask only and the quads only (range forced to -1000 and
1e6) agree: same windows, glass and lit state, mean difference under 0.8/765 per pixel,
differences at window edges. CPU stills inspected. Release, AC power, `boat_water_review`-style
fixed poses, 3 runs each, interleaved with main (41f3420) built the same way: scene triangles
-2,316 to +914 (pilot only); submissions +0 to +6; render p50 and gpu p50 within the 10-30%
run-to-run noise (for example eriksberg-near gpu p50 3.54-3.66 ms on main, 3.59-3.97 new;
render 2.7-4.3 ms either way). No budget claim is made beyond: no regression distinguishable from
noise on the pilot. Not measured: all buildings migrated (the case that matters), cell
expansion time (it runs inside the frame that crosses a cell), bind time.

Not verified: browser/web (WebGL2 `texelFetch`, integer shader maths, a v3 pack in wasm), Windows
and Linux, macOS package, the more-than-32,767-facades fallback, human review of the mask look and
of motion across the 430 m boundary (stills only), the shadow pass (windows are not drawn in it).

## B2 results — 2026-11-21

Commits on feat/buildings (`git log 22fccfc..`): roles for materials and attached detail in the
pack, loader and GPU facade builder; composition primitives, exposed wall rectangles and validated
overrides; the first baked group; tests, rebaked packs and ambient; shop fascia, entrances, minimap
and tones. Contract and schema: [BUILDINGS.md](BUILDINGS.md#compositions-b2). Authored data:
`tools/building_overrides.json`.

What exists. Seven buildings of the Eriksberg quay row have explicit compositions: brick shopfront
block 164963584 (teal canopy and signboard on its quay end) with brick wing 164963868 and
yellow-brick 164963897; balcony blocks 164963867 (cream, terracotta fronts, 70 balconies placed, 59
rejected by clearance, door canopy) and 164964582 (grey-blue, ochre fronts, 45 placed, 8 rejected);
pale stepped block 164963869 (12 levels, two terraces receding from the quay end, glazed lobby) and
164964585. Every other building, including B1's pilots, is the B1 fallback, now with windows only
on exposed wall intervals (the fallback pilots change only where a neighbour or another section
buries part of a wall; 2 triangles more in the river pack before any composition). The cafe and its frontage are in
the west row (-72, 106) and are untouched; the generic entrance and forecourt passes skip composed
walls, and benches and planters keep out from under balconies and canopies through their existing
"horizontal surface above the quay" test.

Review command (GPU, from the basin at the quay row; a window opens, `Esc` quits):

```sh
./build/wake_velocity --location outer --start -5,80,200   # drive it yourself
./build/wake_velocity --location outer --start -5,80,200 --demo --demo-idle   # boat held still
```

Other poses: `--start -40,-80,200` (150 m, the whole row), `--start -25,30,215` (brick block and
shopfront), add `--daylight dusk` or `--lighting-hour 0`, `--renderer cpu` for the CPU view.
Captures (untracked, regenerated by `build/buildings/b2/capture.sh BINARY OUTDIR`): baseline B0
Release in `build/buildings/b2/base/`, B2 in `build/buildings/b2/new/`, side by side (B0 left, B2
right, x3) in `build/buildings/b2/cmp/`: the three B0 cameras and three row cameras, day, dusk,
night, CPU and GPU (36 pairs). The rebuilding recipe for the script is in its header.

Numbers (Apple M1 Max, macOS 26.6.2, AC power, Release; B0 = a `git archive` of 41f3420, B2 =
aae58c0; `build/buildings/b2/perf.sh`, 3 interleaved runs each, `perf_summary.py`). Triangles (deterministic):
river pack 78,612 to 85,277 (+6,665), outer pack 619,497 to 626,162 (+6,665; the pilot adds under
the 60k allowance); static outer GPU triangles 1,367,735 to 1,366,478 (windows are generated, not
stored), GPU vertex bytes 72.46 to 72.54 MB; `scenery.bin` river 852,701 to 904,456 B (+6.1%),
outer 5,370,005 to 5,421,381 B (+1.0%); `ambient.bin` river 79,603 to 90,981 B (+14%), outer
772,854 to 785,846 B (+1.7%). Fixed-pose views (600 frames, p50, three runs): scene triangles
+12,736 to +14,595 (about +1%, budget +10%), submissions +2 to +6 (budget +8). render p50 is
within noise (B0 3.66 to 4.17 ms vs B2 3.20 to 4.22 ms at eriksberg-near; runs vary 10 to 25%). gpu p50
(ms, B0 vs B2): eriksberg-near 3.68 to 3.83 vs 3.79 to 4.01; mid 3.80 to 3.93 vs 4.11 to 4.22; far 4.57 to
4.71 vs 4.72 to 5.07; row-near 4.27 to 4.56 vs 4.61 to 4.83; row-mid 5.25 to 5.39 vs 5.41 to 5.48;
row-quay 4.81 to 4.90 vs 4.90 to 5.27. So gpu p50 is consistently 0.1 to 0.5 ms higher with B2, at
or past the +0.4 ms budget at eriksberg-far and row-quay; I did not isolate the cause (14k extra
triangles cannot explain it; more facade-mask records, new pixel-heavy glazing styles and larger
flat-mesh tiles are candidates) and treat it as an open B4 item. The 12,003-frame route benchmark
(`--aa off`): frame p50 1.57/2.04/1.94 ms (B0) vs 1.62/2.07/1.65 (B2), render_submit p50 1.12 to 1.18
vs 1.18 to 1.20, gpu p50 1.19 to 1.50 vs 1.24 to 1.50, submissions identical, p95 scene triangles
1,440,442 vs 1,447,952. Not measured: first-frame upload, bind time, cell expansion time.

Checks run (2026-11-21): `python tools/building_kit.py` (kit and composition fixtures: exposure
intervals, party walls, setbacks never grow the footprint, orientation of walls, roofs and slabs,
bounds, determinism and independence, 11 invalid overrides and bad references), `map_pack.py
--self-test`, `prepare_outer_map.py --self-test`; both bakes and `ambient_bake` re-run offline;
`water.h` and the water atlas byte-identical (CTest `building_packs`, which also checks every
authored composition is in the manifest and pack); Debug `ctest --preset full` with
`PROTEUS_GPU_TESTS=ON`: 635 of 635 passed after repinning `city_dressing` (river kit 7,944 to 7,635
boxes, both digests: composed walls get no generic frontage) and loosening the window-count pins
of `building_windows` to lower bounds; new loader cases for the new roles; `boat_scenery` checks
every record's bounds hold all its triangles and that balcony and canopy vertices stay out of the
water. CPU and GPU captures at 320x200 for the six views x three lights (inspected), plus GPU
1280x800 diagnostics of the row.

Not verified: browser/web, Windows, Linux, macOS package; native windowed play and approach
motion past the group (stills only; the 430 m window mask boundary and cell expansion with the new
wall counts are unwatched); the shadow pass in motion; the minimap look (it builds and its tests
pass; not looked at); user visual review.

Honest visual assessment (implementer's, not acceptance). At 70 to 300 m from the basin the five
quay-row buildings read as different buildings: brick against cream balcony stacks against the
pale tower, with legible quay-level shopfront bands and entrance canopies, and the dusk/night
lit-window pattern is no worse than B0. It is a clear step from uniform grey boxes. What is still
weak: the stepped terraces are barely readable from boat level (the receding upper volumes hide
behind the front face; they show from the side and from far above); at the B0 "near" and "far"
cameras the row is at the frame edge or a few pixels tall, so the improvement there is
colour and roofline only; the CPU renderer's flat light turns shaded brick and yellow brick nearly
black or muddy olive at dusk; the pale tower's glazing at 300 m is high-contrast and slightly
noisy; balcony bays are missing where a street polygon clears them (59 of 129 on 164963867), so a
facade can look irregular; trims are thin lines at distance; only the quay-row group has any
authored detail, so its neighbours (including 303912306/7 and the west row) look like the old
boxes next to it. Colours were chosen to separate buildings, not from photographs.

Open questions for the user: (1) is the brick-and-ochre palette against the pale tower the
right level of contrast, or should the group be quieter; (2) should balconies hang over the
street side when clear (the rule now rejects any overlap with a 6 m street polygon, which also
rejects some that only touch it); (3) the stepped block needs either a deeper cut or setbacks on
the long sides to read at boat level; (4) accept the +0.1 to +0.5 ms gpu p50 for B2, or hold until
B4 finds the cause; (5) the west row and the remaining quay buildings (303912306/7 and others)
are left as fallbacks until B3.


### B2 review round 2 — 2026-11-21

User feedback: balconies on every bay are unrealistic; windows look like stickers. Done locally
(commits "Place balconies as whole stacks...", "Give windows a reveal...", "Rebake packs..."):

- Balconies are per apartment unit (`unit` 2 to 4 bays, one balcony `span` 1 or 2 bays per
  `every`-th unit, aligned stacks from floor 1 to below the top floor; ground and top floor plain).
  A stack is rejected whole when any level clashes. 164963867: unit 3, span 1; 164964582: unit 4,
  span 2. Survivors (bake manifest): 3 stacks of 4 balconies each on both buildings (12), 5 and 3
  stacks rejected by street and neighbour clearance. That is few; the clearance rule is unchanged
  as asked (decision below).
- Windows: shared generator faces (`building_window_faces`: glass plus lintel/jamb/sill bands,
  the CPU and GPU quads, and the far shader mask repeats them) and relief within 80 m
  (`building_window_reveal`: 16 cm returns, rim over lintel and under sill, mullion on wide
  ground windows). The wall is not cut, so the glass is "sunken" by a surround that projects.
- Measured (Release, AC, row-quay view, 400 frames, 2 runs, short pass only; B0 gpu p50 4.56 ms):
  first version (9 faces, 120 m) +0.8 ms (5.2 to 5.5) and +147 submissions with per-facade meshes;
  merged mesh set fixed the submissions (306 vs 302); 7 faces at 80 m gave gpu p50 4.51 to 4.59 ms
  (no measurable change), scene triangles 1.803M (B0) to 1.827M (+1.3%). With the deep range 0
  (bands only) gpu p50 4.52 to 4.58. So the earlier +0.1 to +0.5 ms gpu p50 of B2 is not
  reproduced by this version (the earlier 70 and 45 balconies per building are gone); I did not
  bisect which of the old roles or the old balconies caused it. Limits: 2 views, 2 runs, no
  route benchmark, no expansion-hitch measurement (the relief set rebuilds when the near facades
  change; unwatched).
- Skewed windows in CPU stills: the generator is exact (a CTest checks the corners form a
  rectangle); I attribute it to perspective and pixel stepping of oblique quads in the 320x200
  CPU view, not verified in the rasteriser. CPU dusk brick near-black look: not changed.
- Checks: `python tools/building_kit.py`, both packs and ambient rebaked, Debug `ctest --preset
  full` 635 of 635 (GPU tests on), city_dressing box digest repinned. Captures in
  `build/buildings/b2/review/` (CPU and GPU, GPU high-res, block and quay views; day, and dusk for
  the block). Not verified: motion, the 80 m relief boundary while moving, user review.
- For the user: are fewer balconies (clearance) acceptable or should balconies over a street
  polygon at floor 2 and above be allowed; is the projecting-surround reveal convincing, or is a
  cut wall (a real hole) wanted for B3.

Relief range 2026-11-21: the user viewed the rework ("Looks good!", hi-res, outer) and asked for the
relief range to be cut from 80 m to about 40 m: `BUILDING_WINDOW_DEEP_RANGE` is 40 m, release
hysteresis 20 m. The shaded lintel/jamb/sill bands are unchanged out to 430 m. Quick stills at 30,
50 and 100 m only; no perf pass (fewer relief faces than the measured 80 m state).
User review 2026-11-21 (windowed, hi-res, outer): the user noticed the switch from relief to bands
when looking for it at the 40 m range and approved it as a good trade-off. This accepts the
40 m relief range and the lintel/jamb/sill bands only; the balcony-stack density, the
projecting-surround reveal and the rest of B2 remain unreviewed.
