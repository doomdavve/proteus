# Marina authoring expansion

Status, 2026-11-01: **planned; implementation has not started.** This plan records the
user's decisions after reviewing the gaps in [PR #131](https://github.com/doomdavve/proteus/pull/131).
It owns the next marina work; [the plugin plan](PLUGIN_PLAN.md) retains the broader roadmap
and [the API](PLUGIN_API.md) describes only implemented capabilities.

Completed task: scope and five-step execution plan written. Next task: **M1 structures**.
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
| M1 | Configurable marina structures and tested larger capacities | Merged #131 | Planned; next |
| M2 | Boat dimensions, colors, mast height and painted hull names | M1 | Planned |
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
