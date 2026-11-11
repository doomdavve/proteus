# Buildings and waterfront architecture overhaul

Status, 2026-11-11: planned; no building implementation or visual acceptance yet.
Goal: inviting, stylized buildings with coherent silhouettes, facade depth and street edges.
Scope: Eriksberg pilot, reusable families across the outer city, and a finished generation spec.
Open: baseline inventory, presentation-data contract, pilot look and measured detail budgets.
Done when: the implementation is verified and reviewed, rollout is complete, and BUILDINGS.md
specifies the shipped source-to-rendering system with reproducible authoring commands.
Next: B0 baseline and architecture decisions, then B1 shared building foundation.
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

### B0 — Inventory, baseline and decisions

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
No invented source facts, performance gains or visual acceptance. Next: B1.

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
