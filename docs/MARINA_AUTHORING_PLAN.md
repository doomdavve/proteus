# Marina authoring expansion

Status, 2026-11-01: **M1 implemented; M2 appearance is next.** This plan records the
user's decisions after reviewing the gaps in [PR #131](https://github.com/doomdavve/proteus/pull/131).
It owns the next marina work; [the plugin plan](PLUGIN_PLAN.md) retains the broader roadmap
and [the API](PLUGIN_API.md) describes only implemented capabilities.

Completed task: **M1 structures and capacity**. Next task: **M2 boat appearance**.
Verification evidence and limitations are recorded below; agreeing to features is not visual
or gameplay acceptance of an implementation.

## Agreed result

A plugin can assemble a larger marina from fixed quay decks, floating platforms, walkways
and independently placed finger piers; customize boats including painted hull names; and
schedule residents and visitors to use its slips. Boats follow authored routes and dock
without appearing twice, changing identity or leaving invisible contacts behind. Native code
owns meshes, motion, clearance, collision and rendering. Lua supplies bounded data.

No backward compatibility is required for replaced authoring schemas or pontoon recipes.
Update every bundled plugin and its fixtures in the step that changes the contract. Do not
retain obsolete recipes or add compatibility shims. Schema/wire changes still need explicit
versioning and clean rejection of incompatible content, including room client/server pairs.
Existing ferry services and bridge coordination must continue working.

Deferred: crane/landmark edits, buildings/windows/street dressing, lighting and weather,
people/trams/additional wildlife, cross-package dependencies, custom meshes/textures/shaders,
and bathymetry. Depth remains explicitly unverified. Reactive traffic avoidance, passengers,
owners and refueling gameplay are outside this work. A visiting boat can dwell at an authored
fuel-stop berth without simulating fuel or customers. Traffic wakes remain visual.

## Delivery and dependencies

| Step | Result | Depends on | Status |
| --- | --- | --- | --- |
| M1 | Configurable marina structures and tested larger capacities | Merged #131 | Implemented; verification below |
| M2 | Boat dimensions, colors, mast height and painted hull names | M1 | Planned; next |
| M3 | Authored routes, stops and docking maneuvers | M2 | Planned |
| M4 | Separate berths/vessels and resident/visitor schedules | M3 | Planned |
| M5 | Package-local Lua modules and integrated marina example | M4 | Planned |

Use one separate worktree and PR per step. If a step exceeds a reviewable change, split its
native owner and plugin integration into explicitly dependent PRs with the same completion
gate; do not mark the step complete while its feature remains disconnected. Branch children
from parents, target their parent PR branches, merge in order, and propagate updated parents
before requesting review. Do not implement in the user's main checkout.

Each step updates its status here with completed work, next task, actual dated verification
and remaining limits. Update the documentation index only when the area summary changes.
M5 is last so module loading does not block the geometry/motion work; temporary duplication
must be kept synchronized until then.

## M1: structures and capacity

Read `src/boat_scene_definition.[ch]` (including validation), `src/waterfront_harbor.[ch]`,
`src/boat_plugin_lua.c`, `src/boat_plugin_wire.[ch]`,
`src/waterfront_scene.[ch]` and `docs/HARBOR_BOATS.md`. Confirm current filenames and ownership
before editing; these entry points do not prescribe a broad refactor.

Replace the fixed pontoon recipes with configurable walkway and finger-pier pieces, fixed
quay decks and floating platforms. Keep necessary shore-to-floating access via a gangway.
Specify position, heading, length, width and deck height with finite supported ranges and
explicit units. Define attachment points, legal joins and anchored/floating behavior.
Floating pieces use shared wave poses; gangways keep their shore endpoint fixed. Fixed decks
must state their shore/support placement rules. No terrain editing or arbitrary mesh import.

All pieces need matching rendered geometry, water/shore placement checks, player contacts,
camera exclusion and entrance clearance. Distinguish a legal connected join from an unsafe
overlap. Establish explicit larger berth/module capacities by measuring a representative
second row with many finger piers and a maximum-budget fixture. Audit every fixed array,
rope/mesh count, stack allocation, wire limit and renderer buffer affected by the increase.
Record chosen limits and frame/memory/staging costs before treating them as supported.

Acceptance:

- A migrated `harbor-life` and `harbor-room` contain both fixed and floating structures,
  independently arranged fingers, a second row and a usable player entrance/exit.
- Boundary dimensions, capacity+1, duplicate IDs, invalid attachments, shore violations,
  unsafe overlap and entrance obstruction reject atomically. Legitimate joins validate.
- Wave-time captures and motion inspection show continuous floating joins and a fixed shore
  landing. Contacts and camera clearance follow the drawn structures.
- Removed recipe spellings are rejected; catalog, schemas, discovery, validators, source
  bundles and native/browser/room data paths agree on the replacement contract.

## M2: boat appearance

Read `src/boat_model.[ch]`, `src/waterfront_harbor.[ch]`,
`src/boat_scene_definition.[ch]`, validation in `src/boat_scene_definition.[ch]` and both renderers.

Keep registered hull recipes as starting shapes; add bounded length, beam, mast-height and
hull/deck/trim color overrides plus a name painted on the hull. Define which recipes have a
mast and reject meaningless overrides. Mesh ownership and caching must support distinct boats
using the same recipe without leaking resources on repeated Apply/Undo.

Dimensions drive generated geometry, mooring attachments, motion/contact envelopes and route
clearance together. Hull lettering uses native rendering and bundled typography, with a
specified text length, supported characters, placement and long-name policy. It follows the
hull pose and occlusion in CPU/GPU views rather than becoming a floating UI label. Preserve
nearest-neighbor integer-scaled presentation. No custom font or texture import is needed.

Acceptance:

- Two boats sharing a recipe retain visibly different dimensions, colors and painted names
  across native/browser serialization, reload and Undo.
- Minimum/maximum dimensions, unavailable masts, invalid colors and unsupported/overlong names
  have explicit validation behavior. Maximum dimensions still pass placement checks.
- Ropes, camera clearance and player contact match each customized hull. Inspect lettering
  from both sides, near/far and under wave motion in CPU/GPU captures and a windowed demo.

## M3: authored routes and docking motion

Read `src/waterfront_traffic.[ch]`, `src/waterfront_upstream.[ch]`,
validation in `src/boat_scene_definition.[ch]`, `src/boat_net_*.[ch]` and `docs/HARBOR_TRAFFIC.md`.

Add bounded plugin-authored waypoint routes with native smooth turns, speed limits, stops,
slow docking approaches and departure maneuvers. Define open versus looping routes, heading
at stops, turn/acceleration limits and exact-time endpoint behavior. M3 supports explicit
terminal poses; M4 binds these to named berths. Preserve existing retained ferry/bridge
services. No reactive steering AI or physical traffic wakes.

Validate swept customized hull corridors against shore, structures, entrances where relevant,
moored hulls and scheduled traffic. Specify conservative bounds between samples so a thin
obstacle cannot be skipped by a coarse timestep. Prove clearance over the supported combined
schedule; bound periods/validation work and reject schedules that exceed the proof budget.
Use explicit simulation ticks, never wall-clock timers or frame-integrated trajectories.

Acceptance:

- One craft follows a new route through turns, pauses, approaches a slip and departs; visible,
  contact, camera and wake identities use the same native pose.
- Reject zero-length/invalid segments, impossible turns, excessive speed, broken loop joins,
  swept collisions and conflicting timetables. Legal separated passages validate.
- Direct seeks, reverse seeks, different frame steps and loop boundaries produce identical
  poses. Route edits Apply/Undo atomically under documented restart rules.

## M4: occupancy, residents and visitors

Separate vessel identity/appearance from berth geometry and capacity. A berth can be empty;
a vessel has one identity through approach, docking, dwell, departure, transit and return.
Express resident and visiting service schedules as bounded definitions referencing vessel,
berth and route IDs. Define the before-arrival/after-departure state of visitors, dwell
interval boundaries and repeat behavior explicitly.

Use native deterministic evaluation of ownership at a tick. A vessel must never be rendered
or contacted once as moored and again as traffic, or disappear at a handoff. Docked ropes and
floating poses transition consistently. Berth fit uses the customized hull and its excursion.
Reserve slips over arrival/dwell/departure intervals, including maneuver clearance; reject
conflicting reservations rather than inventing slip-selection or avoidance AI. Integrate
these schedules into M3's swept checks, allowing the occupying vessel's legitimate berth
approach without exempting unrelated boats from collision.

Acceptance:

- The example contains an empty berth, a resident that departs and returns, and a visitor
  that arrives, stays and leaves. All events fit a documented short capture/demo schedule.
- Reject missing IDs, double booking, impossible berth fit, discontinuous route endpoints,
  overlapping maneuvers and a vessel assigned to two simultaneous services.
- Test exact ticks before/at/after every handoff and cycle boundary, direct/reverse seeks,
  reload, restart Apply/Undo and independent sessions. One visible hull/contact owner exists
  whenever the vessel is present. Native/server/browser views agree.

## M5: local modules and integrated example

Read `src/boat_plugin_io.[ch]`, `src/boat_plugin_lua.[ch]`,
`src/boat_plugin_eval.[ch]`, `src/boat_plugin_stage.[ch]`, catalog generation,
`tools/plugin_author.py`, and the browser source-bundle/import/export path.

Add a restricted package-local module loader over immutable bundled Lua source bytes.
Specify module names, package-root resolution, return/cache behavior and diagnostics. Native
IO acquires files before evaluation; the Lua evaluator gets no filesystem or network access.
Reject traversal, absolute paths, symlink escapes, missing modules and cycles with clear
errors. Bound file count, total bytes, memory and cumulative execution work across modules.
Preserve isolation between package instances, evaluations and revisions. Longer filenames
and multi-file diagnostics need review of existing fixed text buffers.

Carry the same complete source bundle through native workers, live staging, draft tools,
browser import/export and build-time catalogs. Changing a module must affect content identity
and stage the entire package atomically; failure preserves the active revision. Clarify that
local modules cannot import another installed package. Sharing a scene between independent
packages must not introduce an undeclared cross-package dependency: choose a common repository
source copied into each self-contained bundle during packaging, with a drift check. Evaluate
modules separately so mutable Lua tables cannot leak between package instances.

Acceptance:

- `harbor-life` and `harbor-room` derive their marina from one repository source and run as
  self-contained packages in native/catalog/browser paths. Edits and export preserve modules.
- Test nested imports, cached imports, isolated instances, cycles, traversal/symlinks,
  missing files, budget exhaustion and a module-only edit followed by Apply/Undo.
- Run the combined larger marina with customized hulls, readable painted names, a resident
  round trip and a visitor's complete stay. Record frame/memory/staging costs against #131
  at the same camera/time/options; inspect CPU/GPU frames and windowed motion separately.

## Verification and handoff

Each implementation PR updates the current API, package/workspace guides, schemas/discovery
and affected fixtures together. Search README/docs/package guides for superseded claims;
retain dated baseline evidence as history. Check changed local links and command examples.
Keep `proteus_boat_core` and `proteus_boat_net` free of drawing, Lua, IO and platform APIs.
Room revisions use existing supported session boundaries; this plan does not add live
mid-race room revision support or imply a production deployment.

For each step run Debug configure/build and `ctest --preset full`; use `quick` during
iteration. Reuse pinned dependency sources if available. Run appropriate native worker,
wire/catalog, browser contract/Wasm and room integration checks when their paths change.
Record platform, renderer/test options, actual counts and unrun platform coverage. Capture
and inspect deterministic CPU/GPU views at `outer.marina` and `outer.eriksberg`, plus close
views needed for joins/lettering/docking; add a windowed manual demo for motion and loading.
Use virtual time for tests. Human visual/gameplay acceptance remains separate from numerical
validation and automated window runs. No new audiovisual review is needed for this plan PR.

Planning verification, 2026-11-01, Apple Silicon/macOS: Debug configure reused the pinned
raylib/Lua/ENet source trees; build passed (one upstream `stb_vorbis.c` warning). Full
`ctest --preset full` passed **349/349 enabled tests**, with 13 retired wake-field tests
disabled and `PROTEUS_GPU_TESTS=OFF`. The first sandboxed run failed socket-dependent tests;
the complete rerun with local socket access passed. All 342 local links/anchors in the four
changed documents resolve; `git diff --check` passed. No audiovisual run was needed for this
documentation-only change. Windows/Linux/browser builds and GPU tests were not rerun.
Known limitations: proposed fields, exact budgets and module syntax are not yet an API.
The implementation must choose and document these within the agreed scope. Performance,
CPU/GPU appearance, docking quality and cross-platform behavior remain unverified.


## M1 implementation — 2026-11-01

Implemented scene v2 with dimensioned `walkway`, `finger_pier`, `platform`, `quay_deck` and
`gangway` pieces. Length .5–60 m, width .5–20 m and floating deck top .25–2 m are finite metres;
fixed quay tops are world Y=3.8–8 m. A gangway declares its 2–20 m horizontal span and landing
height; position/heading derive from its land anchor and named shore-side midpoint. The
[API](PLUGIN_API.md#scene-schema-v2) owns the full contract.

Floating pieces anchor their X/Z and share their root's wave heave, staying level for exact
joins. An optional earlier floating parent defines an orthogonal boundary join with ≥.25 m
shared edge and equal deck height; cycles, detachment and penetration reject. Gangways keep
the shore end fixed on actual compiled terrain. Fixed quay footprints require land throughout,
with a .3 m cap above sampled terrain. Shoreline-segment/rectangle tests catch thin shore
intrusions. Hulls, structures, the declared entrance width and spawn/recovery areas retain
clearance. Contacts and camera exclusions use authored footprints; conservative vertical
camera bounds include wave excursion. Berth ropes use the nearest floating footprint.

Both bundled marinas now have twelve hulls in two rows and thirteen structures: two walkways,
eight independent fingers, a platform, a fixed quay deck and a shore gangway. `harbor-race`
also migrates. Existing ferry dwell, schooner/bridge coordination and gulls remain. Source
bundles, discovery, schemas, native workers/catalog and the browser stage wire use the same
contract. Removed `pontoon`, `pontoon_small` and `quay` module spellings reject. Staging PST8,
room definition v2 and multiplayer v10 reject old content/pairs; no hosting deployment is
part of this change.

Capacity audit: 32 berths, 64 modules, 520 line transforms; the seven compiled hulls and seven
recipe mesh slots stay separate from authored capacity. Unit-box instances add no per-piece
mesh owner. `BoatSceneDefinition` grows 7,944 → 14,032 bytes; `WaterfrontHarborFrame` grows
2,740 → 22,624 bytes. Ten retained host slots grow 743,272 → 804,152 bytes (+60,880). The stage
job's existing ≤80 KiB assertion still passes and its source+32 KiB response buffer is unchanged.
The production game owner and catalog/staging allocations remain static/heap-owned. Validator
revision hosts now use static storage so their extra 60 KiB does not consume the native stack;
individual scene copies are 14 KiB and shared harbor frames 22 KiB. The existing Wasm game/worker
1 MiB stacks and Node catalog 8 MiB stack remain unchanged.
The maximum room fixture uses 23-character berth/module IDs and 63 gangway references; its
8,840-byte definition fits the 16,384-byte cap in nine pages. A placed 32/64 fjord fixture
validates alongside the shipped traffic; a separate runtime rope stress uses 514/520 segments.
The CPU stress renders all berth/module slots plus eight craft and 32 birds. These are fixed
bounds, not dynamic growth or a new allocation/cache system.

Cost experiment, macOS/Apple Silicon, Debug, 320×200 CPU, `outer.marina` camera, 180 virtual
frames per sample, same harness in parent `1e92637` and this worktree:

| Scene | Parent CPU p50 / p95 | M1 CPU p50 / p95 |
| --- | --- | --- |
| Compiled control | 9.429 / 9.799 ms | 9.460 / 9.739 ms |
| Bundled marina (7/3 → 12/13 berths/modules) | 9.843 / 10.129 ms | 9.762 / 10.139 ms |
| Full slot render stress (7/4 → 32/64, eight craft, 32 birds) | 11.468 / 11.799 ms | 12.316 / 12.771 ms |

One initial native host-stage sample was 797.400 ms parent / 811.988 ms M1 (4,383 / 6,820
source bytes). Most staging cost remains the existing 4,160 s traffic timetable proof. These
are local observations, not cross-platform targets or timing assertions. The larger stress
adds about 1 ms at p95; the normal package stays within observed run variation. M1's populated
scene reports 53 hull/module/bird mesh instances, 37 lights and 48 wakes; the stress has 200
such mesh instances before lines/scenery. Authored modules share the existing unit box (12
triangles each, 768 at capacity), without per-instance CPU/GPU mesh uploads.

Verification, 2026-11-01, Apple Silicon/macOS, Debug with `PROTEUS_GPU_TESTS=ON`: configure and
build passed without project warnings; **375/375 enabled CTests passed in 80.75 s**, including
28 GPU checks; 15 retired wake-field experiments remain disabled. The site
validator proves legal joins, dimensions, capacity+1, ID/attachment failures, shoreline and
terrain violations, unsafe overlaps, entrance obstruction, atomic native host rejection,
direct/reverse wave seeks, fixed landing, contacts/camera and the placed maximum fixture.
Room bootstrap, native workers, Apply/Undo, catalog, browser wire and ferry/bridge tests are
included in the full suite. The Emscripten MinSizeRel `web` target built and packaged with Lua
absent from the game Wasm. The optional Node/Wasm validator passes `scene_contract` and
`scene_berths`; its missing test-fixture linkage was restored in `cmake/Web.cmake`. Schema
metadata tests, browser wire checks and compiled-selection guards passed after the final
metadata/build-file edits. All 366 local links/anchors in seven changed documents resolve.
CPU/GPU `outer.marina` and `outer.eriksberg` wave captures at 7 s,
CPU second-row and both-row approach views, and a 300-frame GPU `--waves --demo` run completed;
frames were inspected. Numerical wave-time checks cover 0/7/reverse-to-0 directly. The earlier
sandboxed network/GPU attempt was rerun with desktop and local socket access. Changed local
links and `git diff --check` pass.

Remaining limits: no Windows/Linux build or fresh
interactive browser/device qualification; no human visual/gameplay acceptance, depth proof,
terrain editing, arbitrary meshes or M2–M5 behavior. Floating assemblies intentionally heave
without pitch/roll. Camera exclusion is conservative, and fixed decks require land underneath
rather than supporting arbitrary over-water construction. The two packages still duplicate
scene data until M5. CPU/GPU captures and scripted window playback are separate from human
motion/audio review.

### macOS Release CI correction — 2026-11-01

PR #133's initial macOS Release job failed only `scene_site` at the raw `memcmp` of two
`WaterfrontHarborFrame` results. C aggregate assignments do not guarantee the contents of
padding bytes, including the gap between a module's one-byte kind and its pose. The seek test
must compare state rather than the frame's object representation. The initial Windows, Linux
and browser CI jobs passed at `590e127`; this does not establish macOS Release success.

The corrected test starts the two output buffers with different byte patterns and compares
all counts/flags, compiled poses, and every active boat/recipe, module kind/pose and line pose
field by field. Float components still require exact equality; no epsilon or determinism
requirement was relaxed. Unused capacity and padding are excluded. Production geometry,
rendering, physics and serialization are unchanged. Next task remains M2 appearance.

The original failure did not reproduce with the local AppleClang 21 Release build, including
the different buffer patterns; the C representation issue still makes the previous assertion
invalid. Release was configured with the CI's arm64, deployment-target 13.0, static-library
and GPU-off options. Its full suite passed 349/349 enabled tests in 52.98 s (13 retired field
experiments disabled). The Debug build and full preset passed 375/375 enabled tests in
76.93 s, including 28 GPU tests (15 retired experiments disabled). Both builds had no project
warnings; `git diff --check` passed. This test/documentation-only correction needs no new
audiovisual review; human acceptance remains pending.
