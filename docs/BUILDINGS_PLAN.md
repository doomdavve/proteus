# Buildings and waterfront architecture overhaul

Status, 2026-11-20: B0 (inventory, baseline, decisions) done; no building implementation or visual
acceptance yet. Results and proposed decisions are in [B0 results](#b0-results--2026-11-20).
Goal: inviting, stylized buildings with coherent silhouettes, facade depth and street edges.
Scope: Eriksberg pilot, reusable families across the outer city, and a finished generation spec.
Open: confirm the B0 decisions in B1, re-measure budgets on AC power, pilot look.
Done when: the implementation is verified and reviewed, rollout is complete, and BUILDINGS.md
specifies the shipped source-to-rendering system with reproducible authoring commands.
Next: B1 shared building foundation (confirms the proposed decisions).
Delivery: one local implementation branch, small focused commits per step; no PR until authorized.

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

### B2 — Eriksberg architectural pilot

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

All are proposals for B1 to confirm; the user has not decided any of them.

| Topic | Proposal | Notes |
| --- | --- | --- |
| Source schema | One offline `buildings.json` per map (or manifest section), keyed by normalised OSM id (`way/ID`; outer's `w/`, `r/` mapped to it) with parent id, part id, base or raised part, footprint rings with holes, base and top y, height with provenance (tag, levels, fallback, terrain lift), a small tag subset (building, levels, roof shape, material), family, palette and optional override id. Source facts are stored apart from assumed fields. | Today the river manifest keeps only osm, height, basis, courtyards, parts; the outer map keeps nothing per building |
| Draping | Carry parent ids through `river_overlay` instead of rebuilding solids from shared XZ corners | Needed for detached balconies and canopies |
| Shared emission | New `tools/building_kit.py`, called by `prepare_waterfront.py` (flat-ground adapter) and `outer_dressing.py` (terrain adapter); retire the two private extrusion and roof paths. Architecture is baked into triangles offline. | The C renderers stay consumers; no second grammar |
| Window layout in C | Either bake window quads, or reduce the grid loops copied in `waterfront.c` and `renderer_gpu.c` to one shared iterator in `waterfront_corner.[ch]`. Prefer baked quads for migrated buildings. | Open for B1: baked quads add triangles against the budget |
| Metadata transport | Recommended: an optional sidecar `assets/geometry/<map>/buildings.bin`, keyed by the same mesh FNV as `ambient.bin`, with per-building first/last triangle, stable id hash, roles and facade-run table; plus `detail_first`. Alternative: pack v2 with a trailing record block (loader accepts v1 and v2). | Sidecar keeps v1 and the loader's version check and tolerates absence, but a stale or missing sidecar fails silently (the `ambient.bin` hazard) and needs a lifecycle, web and package entry. Pack v2 makes metadata exact and atomic with the mesh, at the cost of a version bump in writer, reader, tests and web. Decide in B1 after the first fixture. |
| Identity and seeds | Seed = crc32 of the stable id plus facade, bay, floor (as `outer_dressing.py:232` does for its own seed); replace `waterfront_window_seed(triangle, ...)`, the frontage trims `(i/2)%4` and the `id % 4` tint with a family palette keyed by id hash | `validate_waterfront_lighting.c:337` pins the current seed signature and must change with it |
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
