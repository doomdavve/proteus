# Marina authoring expansion

Status, 2026-11-04: **M1 implemented and merged in
[PR #133](https://github.com/doomdavve/proteus/pull/133); M2 boat appearance is implemented and
merged in [PR #142](https://github.com/doomdavve/proteus/pull/142).** This plan records the
user's decisions after reviewing the gaps in [PR #131](https://github.com/doomdavve/proteus/pull/131).
It owns the next marina work; [the plugin plan](PLUGIN_PLAN.md) retains the broader roadmap
and [the API](PLUGIN_API.md) describes only implemented capabilities.

Completed implementation: **M1 structures and capacity** and **M2 boat appearance**. M3 is
underway: isolated common-period and straight-leg motion helpers are implemented but not connected
to routes. Next task: define the bounded combined-timetable clearance proof and integrate motion
with authored routes. Verification
evidence and limitations are recorded
below; agreeing to features is not visual or gameplay acceptance of an implementation.

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
| M2 | Boat dimensions, colors, mast height and painted hull names | M1 | Implemented; verification below |
| M3 | Authored routes, stops and docking maneuvers | M2 | In progress; unintegrated timetable and straight-leg primitives |
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

Read `src/core/boat_scene_definition.[ch]` (including validation), `src/core/waterfront_harbor.[ch]`,
`src/plugins/boat_plugin_lua.c`, `src/plugins/boat_plugin_wire.[ch]`,
`src/render/waterfront_scene.[ch]` and `docs/HARBOR_BOATS.md`. Confirm current filenames and ownership
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

Read `src/core/boat_model.[ch]`, `src/core/waterfront_harbor.[ch]`,
`src/core/boat_scene_definition.[ch]`, validation in `src/core/boat_scene_definition.[ch]` and both renderers.

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

### M2 execution package — 2026-11-03

The user selected M2 as the next preparation task. Baseline: merged `main` at `6e78e5c`,
confirmed against GitHub on 2026-11-03; no plugin implementation PR was open. P0–P4.9 and the
plugins-by-default cutover are implemented. P5 authority callbacks/prediction/recordings remain
the next main-plugin milestone, independent of this selected marina queue. M1's dated results
below establish implementation and automated checks; human marina acceptance remains pending.

This package prepares one M2 implementation worktree/PR. Start `feat/marina-m2-appearance`
from current `main` after this preparation merges. If starting before then, branch from
`docs/marina-m2-preparation` and target it, following the stacking rules above. M3 routes, M4 vessel
identity/occupancy and M5 modules remain outside M2; customize moored scene berths now without
introducing their future service model. The player's craft, travelling craft and docked ro-pax
ferry keep their existing recipes.

#### Grounded integration points

| Owner at the baseline | Current behavior and M2 work |
| --- | --- |
| [`boat_model.[ch]`](../src/core/boat_model.h) | `BoatModelSpec` already has length, beam, mast height and hull/deck/trim colors. `BoatModel` is caller-owned, capped at 1,024 triangles and must not be copied after initialization because its mesh points into its own arrays. Resolve overrides before generation; add bounded hull lettering without copying initialized mesh views. |
| [`boat_scene_definition.[ch]`](../src/core/boat_scene_definition.h), [`boat_plugin_lua.c`](../src/plugins/boat_plugin_lua.c) | `BoatSceneBerth` stores recipe/placement/mooring only. Add pointer-free appearance data, strict Lua decoding, structural validation and explicit-field digests. Keep IDs separate from painted names. |
| [`boat_plugin.c`](../src/plugins/boat_plugin.c), [`waterfront_harbor.[ch]`](../src/core/waterfront_harbor.h) | Placement, traffic clearance, water pose, ropes, contacts and camera bounds obtain a spec by recipe. Route every authored-berth consumer through one resolved spec, including discovery's reported hull dimensions. Preserve the existing excursion margins. |
| [`boat_view.[ch]`](../src/render/boat_view.h), [`waterfront.c`](../src/render/waterfront.c), [`renderer_gpu.[ch]`](../src/render/renderer_gpu.h) | CPU harbor drawing indexes static recipe meshes; GPU scene bind uploads seven recipe meshes and drawing indexes `frame.recipes`. Add view-owned bounded per-boat resources, revision refresh and destruction. A transform alone cannot supply different colors or names to two boats sharing a recipe. |
| [`boat_plugin_wire.c`](../src/plugins/boat_plugin_wire.c), [`boat_room_definition.c`](../src/core/boat_room_definition.c), [`plugin_author.js`](../web/plugin_author.js) | Native/browser staging and the room codec encode scene fields explicitly. Carry appearance through both codecs and the browser decoder; update magic/version checks, catalog generation, source export and content identity together. Clients continue consuming data without Lua. |
| [`plugin_schema.py`](../tools/plugin_schema.py), [`PLUGIN_API.md`](PLUGIN_API.md), [`HARBOR_BOATS.md`](HARBOR_BOATS.md) | Schemas, discovery and guides currently promise fixed recipes. Update live claims with the implemented contract; retain dated baseline findings as history. |

#### Proposed contract for implementation

These are concrete engineering defaults for the implementation, **not a currently supported
API**. Prove the ranges and geometry budget in the first commit; record any necessary change
here and in the final API together rather than silently advertising an unverified range.

- Add an optional berth `appearance` record with optional `length`, `beam`, `mast_height`,
  `hull`, `deck`, `trim` and `name`. Missing values inherit the registered recipe. Missing
  appearance produces the existing hull without lettering. Reject unknown keys and wrong
  types without coercion. Keep defaults canonical in decoded data so implicit and explicit
  equivalent appearances have the same semantic identity.
- Length and beam are metres, each initially restricted to 0.75–1.25 times that berth recipe's
  dimensions, within the native generator's 3–40 m and 1–10 m limits. Scale cabin length and
  longitudinal cabin offset with length; retain recipe freeboard, draft and cabin height.
  Validate the complete resolved spec and attachment geometry, not just the two scalars.
  Limits describe supported shapes, not permission to overlap the existing slips: site checks
  still reject an oversized hull at an unsuitable berth. Use a separate clear-water fixture
  to demonstrate valid maximum-size placement.
- Only `sailboat` and `sailboat_7m` accept a mast override. Propose a finite height from
  `freeboard + 2` through 12 m, measured above the calm waterline as in `BoatModelSpec`.
  Reject zero and mast fields on other recipes; mast removal is outside this slice.
- Colors are dense RGB triples of integers 0–255, stored as opaque native colors. Reject
  alpha, fractions, missing/extra channels and out-of-range values. Do not add custom materials.
- A present name is 1–16 ASCII characters from `A–Z`, `0–9`, space, period, hyphen and apostrophe,
  with at least one letter/digit. An absent name disables lettering. Reject unsupported bytes,
  embedded NULs and overlong names; do not truncate or silently transliterate. Accented and
  lowercase lettering are an explicit M2 limitation. Use bundled pixel glyphs and native
  geometry with a deterministic contrasting color derived from the hull color.
- Paint the name on both hull sides, readable from each side, inside a defined hull-side
  region. Fit the full accepted name uniformly into that region without wrapping. Glyph panels
  must follow the generated hull surface, pose, depth, clipping, lighting and GPU reflection/
  shadow policy; a screen-space label or a flat panel hovering outside a tapered hull fails
  this gate. At distance the name may become too small to read; no floating replacement label.

The existing ferry `glyph`/`lettering` helper in `waterfront_harbor.c` covers only the letters
in “VINGA LINE” and assumes a constant ferry beam. Reuse the geometry approach where useful,
but it is not a complete font or a placement solution for small tapered hulls. Count worst-case
glyph runs on both sides plus the most expensive recipe. Prove the final mesh cap, including
failure behavior, before selecting a larger fixed capacity or separate lettering storage.

#### Implementation sequence and completion gates

The implementation status below tracks these rows. Use focused commits in one PR; do not mark M2 complete after
only the native generator or codec works.

| Commit | Work and evidence required before moving on |
| --- | --- |
| 1. Definition and resolved geometry | Implement canonical appearance, one resolver, validation/digest and native lettering. Demonstrate two instances of one recipe, min/max dimensions/masts, name/color rejection and worst-case triangle counts. Run the build and quick lane. |
| 2. Revision and data paths | Connect Lua, staging wire, room codec/bootstrap, browser decode, schema/discovery and catalog generation. Migrate every bundled scene and fixture that uses the changed schema, including `harbor-life`, `harbor-room` and `harbor-race`. Test appearance-only digest differences, round trips, malformed/truncated data, old-version rejection, failed staging and Apply/Undo. |
| 3. Runtime owners and rendering | Bind the resolved spec to every placement/traffic sweep, pose/rope/contact/camera consumer and both draw paths. CPU/GPU view owners hold bounded meshes keyed by resolved appearance. Rebuild/upload on accepted appearance changes, never per frame; remove stale slots and destroy all resources with their view. Exercise independent views and repeated reload/Undo/removal. |
| 4. Example and qualification | Give at least two boats sharing a recipe different dimensions, palettes and names in the mirrored marina packages. Keep the entrance, retained ferries and bridge schedule valid. Finish current guides/schema assertions, full native checks, browser build/contracts, room integration, captures, windowed demo and measured resource costs. Record pending human acceptance separately. |

At this baseline the compatibility versions are scene v2, staging PST8, room definition v2
and multiplayer v10. Plan coordinated bumps to scene v3, PST9, room v3 and multiplayer v11
if those numbers are still free when implementation begins. Update the owning
[protocol document](MULTIPLAYER_PROTOCOL.md) and all version-dependent fixtures/checks in the
same PR; do not add a compatibility decoder. Confirm that maximum names/colors/dimensions
with 32 berths, 64 structures, longest IDs and a race course fit the current 16,384-byte room
definition and staging response/page limits. Audit retained host slots, game/worker stacks,
CPU mesh bytes and GPU vertex/index bytes. Report actual sizes rather than carrying M1's
measurements forward as M2 evidence.

Color/name changes must not reset progress, displace the player or reuse another boat's mesh.
Dimension changes use the existing safe live-layout policy: accept only when the player is
clear, otherwise hold the candidate. Undo follows the same rule. Detect changed appearance
by explicit fields/content identity, not a scene pointer that the host can reuse. Keep any
candidate mesh construction/upload failure from partially replacing the active resources;
the old revision must remain usable. Test interleaved independent worlds/views, destroy and
recreate one owner, shrink/grow/empty berth lists and repeated Apply/Undo so stale hulls,
double frees, leaked uploads and pointer aliases cannot hide behind one successful frame.

#### Verification to run for M2

Extend the existing validators: `validate_harbor`, `validate_scene_site`,
`validate_boat_plugin`, `validate_boat_plugin_stage`, `validate_boat_plugin_session`,
`validate_boat_plugin_catalog` and `validate_boat_room_package`. Assert declared fields and
mesh contents rather than struct padding. Include the production console/native worker and
browser import/reload/Undo paths, not just a direct resolver call. Exercise existing GPU view
binding/lifecycle tests with distinct customized appearances. Follow
[the testing guide](TESTING.md) for test names/labels and run Debug configure/build plus
`ctest --preset full`; `quick` is the commit loop. Keep clocks virtual.

Build/package the Emscripten target and run Node browser-authoring contracts. Run the full
Chromium/Firefox/WebKit authoring workflow once on the final implementation revision because
both scene wire and resource lifetime change; keep it manual-only as agreed in
[the browser CI guide](WEB_BUILD.md#browser-authoring-ci-lanes-2026-10-30). Verify matching
native/browser room clients receive distinct appearance data and reject old protocol pairs.
Record unavailable platform/device evidence explicitly; do not claim a release or deployment.

Capture CPU/GPU overview frames at `outer.marina` and `outer.eriksberg`, close views from
both sides of the two customized hulls, a far view and multiple wave times including a reverse
seek. Inspect text orientation, hull occlusion, ropes and conservative contact/camera bounds.
Run a windowed wave demo and an actual Apply/Undo/name/dimension edit sequence. Record scripted
playback separately from human visual/gameplay acceptance. Compare unchanged, customized and
maximum-count scenes at the same camera/time/options: CPU/GPU p50/p95, staging/Apply cost,
triangle/upload counts and retained bytes. These are measurements outside CTest, not timing
assertions. Add exact reproducible capture commands to the package guide when the fixture exists.

Preparation verification, 2026-11-03, Apple Silicon/macOS: Debug configure reused the pinned
raylib/Lua/ENet source trees; build passed with one upstream `stb_vorbis.c` warning and no
project warnings. Full `ctest --preset full` passed **354/354 enabled tests in 32.83 s**, with
local socket access, 13 retired wake-field tests disabled and `PROTEUS_GPU_TESTS=OFF`. All
16 local file/anchor links in this plan resolve; `git diff --check` passed. No new audiovisual
review was needed for this documentation-only preparation; Windows/Linux, GPU and interactive
browser/device checks were not rerun. No M2 code, appearance data, serialization, lettering,
performance or visual acceptance is implemented by this preparation. The proposed API and
budgets above remain subject to the implementation's validation and geometry proof.

### M2 implementation status — 2026-11-03

Implemented on `feat/marina-m2-appearance`, based on merged preparation `807bfb3`.
Completed: row 1's native appearance definition, shared resolver, canonical defaults, explicit
digest fields and hull-surface lettering; row 2's Lua/schema/discovery, PST9 worker/catalog,
scene/room v3 and multiplayer v11 data paths; row 3's runtime consumers/view-owned meshes; and
row 4's mirrored CPU example and integrated native/browser qualification.

The complete bundled 3×5 alphabet/digit/punctuation set, repeated through the 16-character
limit on every registered berth recipe at both dimension/mast boundaries, produces at most
894 triangles. The existing 1,024-triangle cap therefore stays unchanged. A `BoatModel` remains
53,280 bytes; `BoatSceneDefinition` grows to 15,440 bytes. Lettering follows the forward hull
side plane, reads correctly from each side and uses native opaque geometry. The CPU renderer's
existing mirrored camera basis is compensated in glyph layout alone; the hull shape, contact
spec and scene orientation are preserved, and both renderers share the same hull surface.

Row 1 verification on Apple Silicon/macOS: Debug build and `validate_harbor` pass, including
geometry bounds, winding, palette/mast/name rejection, canonical digests and independent model
storage. The initial sandboxed quick run failed socket-only ENet checks; the socket-enabled
rerun is the authoritative quick-lane result. Full-suite, GPU/browser and human acceptance
remain pending for the integrated implementation.

Row 2 verification: Debug build, `scene_appearance`, room maximum-size/round-trip and Node
browser transport checks pass. The maximum room definition is 10,152 / 16,384 bytes (10 pages);
the maximum scene with a full 64 KiB source occupies 80,124 staging bytes. This exposed the
browser worker/transport's former 16 KiB reply limit; stage replies now use the Wasm host's
163,840-byte read cap, with at/above-boundary Node checks. Query replies retain their 16 KiB
worker limit. The browser decodes scene bodies in the shared C/Wasm codec; JavaScript checks
the transport magic/framing rather than implementing another scene decoder. Protocol golden
fixtures pin the new v11 header and unchanged payload. The updated quick lane passes 249
enabled tests with local sockets. Interactive browser/resource lifetime checks remain pending.

Row 3: the shared resolved spec now supplies berth/site/traffic/spawn checks, water pose,
ropes, contacts, camera bounds and CPU/GPU draw resources. Customized meshes belong to each
view; unchanged frames borrow their current mesh, while default boats borrow immutable recipe
meshes. Replacement first builds the complete CPU set, then uploads the complete GPU set and
commits together. A failed build/upload retains the old resource set and reports draw failure;
it does not roll back a previously accepted gameplay revision. Undo can still restore that
revision. GPU bounds cached by VAO are invalidated after replacement because IDs can be reused.
Shrinking/removing berths releases obsolete models/uploads, and view destruction releases all.

Row 3 verification on macOS: Debug build, all 250 enabled quick tests and `gpu_view_binding`
pass with socket/desktop access. The new `harbor_view` drives 96 change/remove/restore cycles,
independent owners, failure after partial candidate construction, no-rebuild repeats and custom
contact/camera bounds. The GPU binding check drives 32 cycles, 32 customized slots, independent
views, no-reupload repeats and a forced second-upload failure after the first succeeds; the
old views remain intact.

Row 4 (mirrored example + integrated qualification): `harbor-life` and `harbor-room` now carry
the second-row `MIRA`/`SALT` runabouts and the 8.5 m `sailboat_aft` mast; both packages share the
same source, and `harbor-race` migrates. The CPU view sets `mirror_lettering` before its first
prepare so its existing mirrored camera basis renders outward-readable glyphs from either side;
the GPU view uses ordinary geometry. Color/name edits classify cosmetic (progress kept, no
reset); dimension/mast edits stay live (boat kept) and hold while the hull overlaps; Undo
reapplies under the same policy. A NUL byte in a Lua field name rejects rather than truncating.

Row 4 verification, 2026-11-03, Apple Silicon/macOS, Debug with `PROTEUS_GPU_TESTS=ON`: the full
`ctest --preset full` passed **388/388 enabled tests** (117.09 s), including 33 GPU checks and
261 quick-lane tests, with 15 retired wake-field experiments disabled. `validate_harbor_life`
measures recipe-default versus populated frames, and `harbor_appearance_edits`/
`boat_plugin_stage`/`wake_command` exercise the session, isolated worker and PC console
Apply/Undo paths; `validate_boat_room_package` carries customized hulls through room definition,
contact and prediction. The optional browser authoring checker passed on **Chromium, Firefox and
WebKit**, including the M2 name/dimension Apply/Undo/export sequence and a full 64 KiB source
reply; Chromium also exercised tab freeze/resume and the deadline across a freeze. CPU and GPU
`names-port`/`names-starboard`/`names-far` lettering captures were generated and inspected;
`git diff --check` and the changed documents' local links resolve. Rerun on 2026-11-04 after
formatting: `ctest --preset full -j8` passed 387/388. The GPU `instancing_parity` check missed
its `ok` line under parallel load, then passed when run alone. The GPU `names-port` and
`names-far` captures were regenerated and inspected: names read up close and blur out at 32 m.
Human visual/gameplay acceptance, Windows/Linux builds and fresh interactive browser/device runs remain separate.

## M3: authored routes and docking motion

First groundwork, 2026-11-14: `boat_schedule_common_period` computes the exact least common
multiple of positive tick periods, rejecting overflow and caller-budget overruns without changing
the result. A separate straight-leg primitive evaluates explicit ticks using smoothstep, with
analytic speed and acceleration bounds and exact terminal poses. These are not yet used by traffic
validation or rendering. M3 remains incomplete: the existing
floating-point traffic schedules and fixed 4,160-second sampled horizon have not been converted
to a proven bounded combined timetable. The next implementation must define tick conversion,
the validation budget and conservative swept envelopes before connecting the helper. Open routes,
docking, authoring, wire integration, Apply/Undo and visual qualification remain outstanding.
An unintegrated pairwise straight-leg clearance helper samples synchronized tick positions and
inflates enclosing circles by a half-tick maximum relative travel bound. It may conservatively
reject safe passages; the numerical rounding margins and its extension to turns, scenery and
periodic schedules remain to be justified before it can serve as a scene proof. On 2026-11-14,
macOS/Apple Silicon Debug, the build and focused leg, clearance, schedule and core-boundary tests
passed. No motion or user acceptance is claimed.
The related S1a gap is fixed: free-package marina and traffic scenes now use the voyage location
checks, except for voyage-only shelters. Their existing fixed-horizon traffic check still requires
the stronger M3 proof. On 2026-11-14 macOS/Apple Silicon Debug, the full build and full CTest
preset passed all 558 enabled tests with local sockets allowed; 13 retired wake-field tests were
disabled. No visual or human motion check was performed for this validation-only change.
On macOS/Apple Silicon Debug, the full build passed. The full CTest preset passed 542 tests
inside the sandbox; the 14 socket-dependent failures passed on an unsandboxed rerun. Thirteen
retired wake-field tests were disabled. No motion or user acceptance is claimed.
After adding the straight-leg primitive on the same platform, the full build passed and the full
CTest preset passed 556 tests; `boat_local.course_saved_ghost` failed in the parallel run and
passed on an isolated rerun. Thirteen retired wake-field tests remained disabled.

Read `src/core/waterfront_traffic.[ch]`, `src/core/waterfront_upstream.[ch]`,
validation in `src/core/boat_scene_definition.[ch]`, `src/net/boat_net_*.[ch]` and `docs/HARBOR_TRAFFIC.md`.

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

Read `src/plugins/boat_plugin_io.[ch]`, `src/plugins/boat_plugin_lua.[ch]`,
`src/plugins/boat_plugin_eval.[ch]`, `src/plugins/boat_plugin_stage.[ch]`, catalog generation,
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
