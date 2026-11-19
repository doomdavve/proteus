# Marina authoring expansion

Status, 2026-11-15: **done.** M1 structures, M2 appearance, M3 authored routes and M4 vessels with
one-shot services are merged (M4: #209); M5 (package-local modules, the shared Eriksberg marina module
and the integrated example `marina-integrated-demo`) is merged (#211). The contracts live in the [Plugin API](PLUGIN_API.md): M4 in
[Marina vessel services](PLUGIN_API.md#marina-vessel-services-m4), M5 in
[Package modules](PLUGIN_API.md#package-modules-m5). What remains is listed as explicit open items in
[Closing leftovers](#closing-leftovers--2026-11-15). No next task is scheduled. Automated verification and human acceptance are separate; no human review of
M5 motion has happened. The [plugin plan](PLUGIN_PLAN.md) owns the broader roadmap.

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
| M3 | Authored routes, stops and docking maneuvers | M2 | Complete; C1–C4 closed, evidence and limits below |
| M4 | Separate berths/vessels and resident/visitor schedules | M3 | Complete, merged #209; contract in the Plugin API; browser/Wasm, room and motion review owed |
| M5 | Package-local Lua modules and integrated marina example | M4 | Done (W1-W7), merged #211; contract in the [Plugin API](PLUGIN_API.md#package-modules-m5); [execution package](#m5-execution-package--2026-11-15) |

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

Implemented and qualified, 2026-11-14: one authored open or looping route has up to four tangent line
and arc pieces, integer tick durations, optional dwell and bounded speed/acceleration. Zero-dwell
joins preserve speed. A static site survey checks water, explicit and compiled contacts and the
permanent ferry. A moving survey checks the route duration and one full period of each other
periodic craft after arrival, with conservative bounds and capped work. Disjoint loop corridors
prove safety directly; overlapping POINTS corridors can use an exact-tick common schedule,
subject to the existing work cap and explicit unsupported-repeat rejection. A second authored
path is rejected. The staging wire is PSTB;
room definition v5 and protocol v15 carry paths.
`plugins/marina-route-demo` approaches a platform, pauses and departs while retaining the upstream
ferry and bridge service. [Plugin API](PLUGIN_API.md) describes the implemented subset.

### Initial implementation evidence — historical

This baseline predates C1–C3; the closure result below supersedes its open verification gaps.

Focused macOS/Apple Silicon Debug tests passed for path motion and joins, Lua decoding, location
survey, staging and room wire, protocol codec and golden bytes. The example validated via
`plugin_author.py`; CPU screenshots at 0, 8, 12 and 18 seconds were rendered, but only the initial
frame clearly showed the visitor. A 300-frame windowed demo was interrupted after macOS reported
display-service connection errors; no motion review is claimed. The full CTest run passed 560
tests; a v15 LAN golden byte and the test-time lint failed, then passed with `boat_plugin_author`
after the fixture and test registration were corrected. Thirteen tests were disabled. No human
motion review, GPU or browser review is claimed. A focused native Apply/Undo test changes a dwell,
requires a session restart and restores the previous route. Shared-frame, contact, camera and wake
checks exercise the same moving path. Legacy preset traffic retains
its existing 4,160-second sampled check. Route bounds may conservatively reject safe passages; overlapping loops require supported
exact-tick repeats within the work cap. Multiple authored paths are not supported. Earlier
incremental full runs passed all enabled tests after the documented
socket or boundary reruns; final integrated verification is recorded below when performed.

### Implementation and acceptance contract — completed

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

### M3 closure (C4) — 2026-11-14

**M3 is complete for the bounded one-route contract.** C1–C3 satisfy the implementation and
agent qualification gate; this does not imply human acceptance or close M4/M5.

| Gate | Completed evidence |
| --- | --- |
| C1 clearance and legal separated passages | [Settled contract and regressions](#c1-decision--2026-11-14): simultaneous bounded schedules accept separated crossings and reject conflicts, unsupported repeats and exhausted work; retained services stay unchanged. |
| C2a deterministic composition | [Composed-path result](#c2a-execution-contract--2026-11-14): turns, dwell, terminal hold, loop wrap, independent owners and seek/cadence equality. |
| C2b visible approach/pause/departure | [CPU/GPU captures and windowed recording](evidence/marina-c2b/2026-11-14/README.md): full visitor/platform visibility across the complete sequence. |
| C3 final integration | [Native/browser/Wasm qualification](#c3-integrated-qualification--2026-11-14): native full CTest, Wasm validation/packaging, real browser staging and timetable Apply/Undo. |
| C4 contract and handoff | API, traffic/protocol specs, schema/discovery claims, package guides, plugin roadmap and index reconciled; closing verification recorded below. |

The current contract lives in the [Plugin API](PLUGIN_API.md#location-life-scene-contract-l11l43),
[traffic guide](HARBOR_TRAFFIC.md#plugin-authored-paths) and
[protocol](MULTIPLAYER_PROTOCOL.md#room-definition).
The [route package guide](../plugins/marina-route-demo/README.md) owns launch/capture commands.
The schema/discovery already expose `route = "open"`, line/arc pieces and the one-route cap;
C4 changes documentation only, with no schema or wire-version change.

Remaining limits are explicit: conservative enclosing hulls can reject safe passages;
overlapped loop corridors require supported exact-tick repeats within the 200,000-evaluation
budget; legacy preset checks retain their sampled timetable. Depth, multiple authored paths,
reactive avoidance and physical traffic wakes are outside M3. M4 owns named berths,
reservations, snug docking/contact exceptions and a single vessel owner across moored/traffic
handoffs; M5 owns local source modules and the combined example. Human acceptance, Windows/Linux,
physical Safari/mobile and a new mixed WebRTC route-demo play session remain unverified.
C2b's platform stand-off is the qualified approach example, not an occupancy handoff.

C3 is recorded in local commit `edc6a08`; C4 is a separate focused local commit on the same
`feat/marina-m3-c3` branch. No push or PR is authorized by this closure. The broader marina
plan remains open for M4/M5. Next: prepare M4 using the open-loop board and the ownership
contract below; unrelated tracks retain their owners.

Closing verification, 2026-11-14, Apple Silicon/macOS 26.6.2: `cmake --build build -j 8`
passed; `ctest --preset full` with localhost socket access passed **568/568 tests in 44.19 s**,
GPU CTests OFF. `plugin_author.py schema` and `capabilities` succeeded; schema inspection
confirmed the open-route line/arc alternatives, four-piece limit and discovery constraints.
`plugin_author.py validate plugins/marina-route-demo` returned `status: ok` with seven traffic
actors and the platform. All 480 local links/anchors across the eight changed guides resolved;
`git diff --check` passed. This documentation-only step needs no new audiovisual review.
C3's browser/Wasm results and C2b's native motion evidence remain dated evidence; they were
not rerun for C4. No new platform or human acceptance is claimed.

### M3 bounded closure audit — 2026-11-14

Historical pre-C1/C2/C3 findings: the closure result above supersedes the open assessments
and work queue below. Dated execution/result sections retain their original evidence.

Audited local `feat/marina-m3-routes` at `1462e60` plus its existing uncommitted
implementation changes. This is an evidence audit, not acceptance or a scope reduction.
The original acceptance criteria above remain authoritative. No implementation was changed
by this audit. M4 occupancy/handoffs and M5 modules remain deferred.

| Acceptance area | Evidence in current source | Closure assessment |
| --- | --- | --- |
| Turns, pause, approach and departure | `plugins/marina-route-demo/main.lua`: line/arc/line/arc, dwell after piece 3; `check_open_path` in `tests/core/validate_traffic.c` checks joins, dwell, speed and acceleration | Numerically exercised; visible docking sequence still unqualified. Example uses a platform and no named berth; named berth binding belongs to M4. |
| Shared visible/contact/camera/wake pose | `check_open_path` compares scene motion with route motion, queries contact and camera, and requires wakes | Covered by automated assertions; visual continuity still needs review. |
| Invalid segments, limits and loop joins | Leg/arc validators, `boat_plugin.open_path_decode`, `harbor_traffic` | Substantial coverage; passing tests alone do not prove every clearance bound. |
| Swept static and moving clearance | `check_open_path_site`, `check_open_path_traffic`, `check_loop_path_traffic` in `src/plugins/boat_plugin.c`; location fixtures reject shore, structures, contacts, traffic and excessive work | Conservative enclosing-circle checks exist. Loop checks compare spatial corridors independently of time. Legacy preset pairs retain the sampled 4,160-second check. Original combined-schedule contract is not fully demonstrated. |
| Legal separated passages | Safe spatially separated open and loop fixtures in plugin location tests | No identified positive fixture proving a time-separated crossing. Such loop crossings can reject by design. A second authored path rejects. This is a contract gap, not just an unrun test. |
| Direct/reverse seeks and frame steps | Leg and arc validators exercise reordered seeks/frame steps; `check_open_path` checks composed joins and loop boundary | Primitive coverage is strong; add a composed-path seek/step regression spanning dwell and loop wrap before claiming the complete acceptance clause. |
| Atomic Apply/Undo | `boat_plugin_session.marina_route_apply_undo` edits dwell, requires restart and restores route | Native assertion coverage exists; browser behavior remains unqualified. |
| Serialization and retained services | Plugin staging and room path roundtrips; demo retains ferry and schooner services | Native coverage exists; fresh Wasm/browser evidence is still owed. |

Closure work, in order:

- **C1 — implement the settled clearance contract below.** Scope is settled; the
  implementation and targeted regressions are recorded below; integrated qualification remains
  open. Keep the one-route/four-piece cap.
  Replace timing-independent rejection where a bounded schedule proof can establish safety.
- **C2a — composed-path numerical evidence.** Add finite and looping composed fixtures with
  turns and dwell. Check exact fields across direct/reverse/reordered seeks, independent owners
  and different tick cadences; assert geometry and stopped/moving states at boundary ticks.
- **C2b — visual docking evidence.** Inspect an entire demo sequence with a camera that keeps
  the visitor visible. Capture before/at/after the dwell and both turns; inspect CPU/GPU views
  and windowed motion. Record any display-service blocker without claiming motion acceptance.
- **C3 — qualify the final integrated tree once.** Build and run full CTest after final fixes;
  run the affected browser/Wasm staging/room contracts and record platform limits. Repeat only
  failed checks while diagnosing, then perform one final full run after implementation settles.
- **C4 — close documentation and local commits.** Reconcile API/protocol/schema/package guides,
  record verification and unresolved limits here, and update the index's milestone summary.
  Mark M3 done only after C1–C3 are satisfied or explicitly deferred by the user. Keep this
  branch local pending explicit PR approval.

#### C2a execution contract — 2026-11-14

Continue from merged #201 (`06dde10`) in local `feat/marina-m3-c2`. This is the numerical
part of C2; C2b remains a separate focused step. It defers M4 occupancy, M5 modules and the
unrelated Open loops board items in the documentation index. No PR or push is authorized.

Done when finite mixed line/arc/line motion with an interior dwell and a looping two-arc
path with interior/final dwells agree through the native path evaluator and production
traffic adapter at identical ticks. Compare motion fields rather than padding across
1/7/31-tick cadences, forward/reverse/alternating seeks and an interleaved translated owner.
Anchor repeatability with expected positions and moving/stopped states around dwell, finite
start/end and three loop-wrap boundaries, including negative loop time.

The new regression exposed a one-tick adapter lag after wrap: seconds for tick 1441 in a
1440-tick loop were reduced to the period before conversion, losing enough precision to
select tick 0 rather than tick 1. Recover the absolute tick before modulo by the validated
path duration. Preserve finite-path clamping and protect huge finite times from conversion
overflow. No route schema, capacity, wire layout or retained-service evaluator change.

Next: C2b must establish a reproducible camera/launch command keeping the visitor visible
through the full 0–19 s sequence. Capture both turns and ticks before/at/after the 12–14 s
dwell on CPU and GPU, then inspect full windowed motion. C3 owns final integrated
native/browser/Wasm qualification; C4 owns milestone closure. C2a success alone closes neither
C2 nor M3, and does not imply human acceptance.

C2a result, 2026-11-14, Apple Silicon/macOS: implemented the finite mixed-path and looping
turn/dwell regressions, exact native-to-adapter float-cast comparisons, product-form and
division-form clock inputs, cadence/reordered seeks, translated-owner interleaving and
expected boundary states above. Fractional-loop times hold the preceding tick; huge positive
and negative finite times remain bounded. The regression failed on the pre-fix loop adapter
and passes with absolute-tick wrapping. Independent read-only review found no concrete blocker.

Debug configure/build passed, reusing pinned local raylib/ENet sources and vendored Lua.
The initial clean build emitted one upstream `stb_vorbis.c` warning; the final incremental
build emitted no warnings. Final `ctest --preset full` with local socket access passed
**568/568 tests in 46.09 s**, GPU tests OFF. The earlier sandboxed baseline quick run passed
362/370; its eight socket-dependent failures are covered by the successful final full run.
`harbor_traffic` also passed separately. Added/rewritten local links (six) and `git diff
--check` passed; existing documentation links to the renamed index heading were searched
and none found. The test commands above were exercised in this worktree.

A bounded windowed startup/load smoke completed with exit 0 using
`./build/wake_velocity --plugin plugins/marina-route-demo --start 0,150,0 --demo --frames 120`.
It reached 136 simulation ticks; it does not cover a wrap or the full docking sequence.
No new CPU/GPU capture inspection, full motion review, browser/Wasm build, Windows/Linux
qualification or human acceptance is claimed. Those remain C2b/C3 work. Exact recovery of
arbitrary ticks beyond double-time precision is not established; huge-time cases check
bounded output, not exact phase. The branch remains local pending explicit PR approval.

#### C2b execution contract — 2026-11-14

Continue locally on `feat/marina-m3-c2`, following C2a commit `f05f74e`. Done when the entire
visitor hull and platform stay visible from approach through both turns, the 12–14 s dwell
and the 19 s terminal pose in CPU/GPU captures and agent-run windowed playback. Inspect ticks
before/at/after each transition as well as the turn/dwell interiors. Human acceptance remains
separate. C3 platform/browser/Wasm qualification, C4 closure, M4 occupancy and M5 modules are
deferred to their owning steps; no push or PR is authorized.

The existing screenshot helm drove the viewing boat away. Add `--demo-idle` for neutral helm
in solo free/voyage screenshots and demos, using the existing local-authority script hook
and the direct-path fallback. Keep the world clock running. Initialize its camera heading to
the placed hull at tick zero. Reject unsupported work/course/replay/room combinations.

An elevated side view exposed bridge occlusion over the old route. Translate the visitor's
four pieces 60 m along Z into open water, preserve their timing and shape, and put the platform
at `(12,203)`. The camera is `--start -5,185,90 --camera-distance 16 --camera-height 30
--fixed-fov`. Keep swept-envelope checks unchanged: the paused hull stays about four metres
from the platform edge. A closer candidate at Z=202 rejected clearance and was discarded.
This demonstrates approach/pause/departure, not contact, mooring or an M4 berth handoff.

The [package guide](../plugins/marina-route-demo/README.md) owns launch and boundary-capture
commands. `--no-vsync --frames 1800` bounds a 60 Hz windowed review to about 30 s so the full
19 s sequence fits regardless of the monitor's refresh rate. Capture modes retain 120 Hz
simulation time and low-resolution integer-scaled presentation.

C2b result, 2026-11-14, Apple Silicon/macOS 26.6.2, native Debug: implemented the neutral
capture/demo helm, camera initialization and relocated example above. The final camera also
keeps the approach above the live HUD; the first trial camera passed stills but hid the
approaching hull behind instruments, so it was replaced. Nineteen CPU and nineteen GPU
captures bracket both turns, dwell start/end and terminal hold at ±1 tick, plus interiors.
All were inspected; the full visitor and platform are visible, geometry/headings agree
between renderers, and no jump is visible at the sampled boundaries. GPU reflections/shadows
retain their existing differences from CPU. Independent read-only code and capture review
found no concrete blocker.

The 1,800-frame GPU windowed run completed with exit 0, **3,611 simulation ticks** and zero
restarts. A 24-second game-window-only recording includes the full approach, pause, departure
and terminal hold; all 96 extracted samples at 4 fps were inspected. The HUD stays clear of
the visitor throughout. Tick-zero stills cover the startup pose before the recording starts.
The [capture sheets, recording, manifest and commands](evidence/marina-c2b/2026-11-14/README.md)
are retained for review. This is agent-run playback and sampled motion inspection, not human
visual/gameplay acceptance or audio review.

`cmake --build build -j 8` passed without warnings. Final `ctest --preset full` with local
socket access passed **568/568 tests in 45.42 s**, GPU CTests OFF. The GPU captures
and agent-run windowed review above used a desktop graphics context. `plugin_author.py validate` accepted
the final relocated scene; the closer discarded platform candidate rejected explicitly.
Six CLI rejection checks covered idle mode without demo/capture and with work, course,
replay, host and client modes. A separate `wake_study --no-player-wake` CPU capture exercised
the direct fallback at 13 s: the spectator remained still and the visitor held its dwell pose;
its water appearance differs from the normal arcade-pool path, so byte parity is not claimed.

All 20 added/rewritten local links and nine retained artifact checksums passed;
`git diff --check` passed. The documented capture/playback option combinations were exercised.

C2b is complete as agent evidence for the documented platform approach/pause/departure.
C3 still owns fresh browser/Wasm staging/room qualification and platform limits; C4 owns
milestone closure. No new browser build, interactive browser, Windows/Linux, depth, snug
mooring, named berth ownership or human acceptance is claimed. The branch remains local.

#### C3 integrated qualification — 2026-11-14

Qualified merged #203 (`bb4d589`) in the separate local `feat/marina-m3-c3` worktree,
Apple Silicon/macOS 26.6.2. No production behavior changed. The real-browser authoring
check now imports `marina-route-demo`, launches its checked route into the game, stages
240 → 360 dwell ticks without changing the active owner, applies the held revision and
restores the original revision with Undo/Apply. It waits for asynchronous outer scenery
loading; an initial immediate assertion raced on Firefox/WebKit and was corrected.

Fresh native Debug configure/build passed. One warning came from raylib's bundled
`stb_vorbis.c` pointer comparison; no project-source warning was emitted. With local socket
access, `ctest --preset full` passed **568/568 tests in 73.73 s**; GPU CTests were OFF.
This includes composed-path regressions, staging roundtrips, native Apply/Undo, the browser
staging backend model, room-definition finite/loop path roundtrips and protocol golden bytes.

Emscripten **6.0.12**, MinSizeRel: `web` and `validate_boat_plugin` targets built and the
content-hashed site packaged. The first sandboxed link could not lock the SDK cache; rebuilding
with cache access passed. Packaging confirmed Lua parser/VM isolation from the game Wasm,
with the isolated evaluator as positive control. Node/Wasm `free_scene_location`,
`open_path_decode` and `scene_traffic` cases passed; Node authoring contracts and the PSTB
browser/native wire-version check also passed.

The complete authoring suite passed on Chromium 156.0.8078.4 (SwiftShader), Firefox 157.0
and WebKit 27.2, each with 20 fresh worker validations and three restored sessions. Chromium
also passed freeze/resume and deadline-across-freeze checks. An additional installed Chrome
157.0.8103.52 run passed the same suite. No page errors were recorded.
Browser results and exact commands are retained in the
[C3 evidence](evidence/marina-c3/2026-11-14/README.md). Route activation, held dwell edit and
Undo restored owner tuples `[1,0,0]` → `[2,1,0]` → `[1,2,0]`.
These checks qualify staging and owner lifecycle, not frame-by-frame browser route motion.
Native CPU/GPU/motion evidence remains the C2b result above. Browser room contract coverage
here is serialization and native authority/transport tests, not a new mixed WebRTC route-demo
play session. Windows/Linux, physical Safari/mobile devices, audio listening and human
acceptance remain unverified. WebKit automation is not physical Safari acceptance.

C3 is complete; C4 owns reconciling the remaining historical/live status and closing M3.
M4 occupancy/named berths and M5 modules remain deferred. Changes stay local pending explicit
PR approval.

#### C1 decision — 2026-11-14

Retain the original acceptance requirement that legal separated passages validate. Do not
close M3 merely by documenting timing-independent rejection as a limitation. Keep **one
authored path per scene, at most four pieces**, with the existing actor capacity for retained
services. The acceptance example explicitly requires one craft; multiple newly authored paths
are not necessary to complete M3. The audit's suggestion that broader authored-route capacity
was itself a blocker was too strong. Additional route capacity is a future extension, not a
new M3 requirement. M4 still owns named berths, reservations and vessel handoffs.

Required implementation boundary:

- Preserve the current 120 Hz native path evaluation and three-minute authored duration cap.
- For finite paths, prove clearance throughout travel/dwell and at the stationary terminal
  pose for a full cycle of each periodic obstacle. Before-start behavior remains the native
  clamped start pose; no visitor spawn/despawn semantics are introduced.
- For loops, keep the conservative spatial-corridor proof as a cheap sufficient check for
  disjoint traffic. When corridors overlap, use simultaneous poses over a bounded common
  schedule rather than rejecting solely because the corridors intersect.
- Only claim an exact common schedule for periods whose repeat semantics are represented
  exactly on integer simulation ticks. Do not round a legacy floating period into a claimed
  proof. If an overlapping legacy schedule cannot be proved with the supported method,
  reject with an explicit unsupported-schedule diagnostic. Retained ferry/bridge behavior
  must remain unchanged.
- Bound common periods with the existing overflow-safe `boat_schedule_common_period` helper;
  cap validation at the existing 200,000 traffic-work budget per authored actor. Account for
  every fallback evaluation in that budget. Reject overflow or exhausted work explicitly.
- Use conservative hull envelopes and between-sample motion bounds, including endpoint and
  wrap behavior. Spatial separation remains sufficient regardless of period compatibility.
  No reactive avoidance, timetable search, new route primitives or broad refactor is needed.

C1 completion evidence must include a positive periodic crossing whose spatial corridors
intersect but whose simultaneous swept envelopes remain separated, the same geometry with a
conflicting timetable rejected, a finite-path crossing pair, wrap/endpoint cases, and explicit
work-budget/incompatible-period rejection. Include a retained-service regression. A green
existing test suite alone cannot close C1. If the legacy evaluator cannot supply an exact
repeat contract, report that specific obstacle before changing its behavior or reducing scope.

C1 implementation evidence, 2026-11-14, macOS/Apple Silicon Debug: the location validator now
keeps spatial separation as sufficient proof and falls back on overlapping corridors to a
simultaneous native-pose survey over an overflow-safe, bounded pairwise integer-tick LCM.
Only POINTS supplies the supported legacy exact time-wrap contract; ellipse/circuit overlaps
explicitly reject as unsupported. The authored period must exactly match its validated tick
count. All attempted spatial and simultaneous actor evaluations consume the existing 200,000
traffic-work cap. Finite-path surveys retain full travel/dwell plus a complete obstacle period
at the terminal pose. Margins include tick-held motion, geometric wrap mismatch and bounded
float intermediates; retained-service evaluation is unchanged. A focused core helper reuses
native built leg lengths to reject stopped POINTS actors whose acceleration/deceleration ramps
overlap. This gates every speed-based proof, including spatial and finite surveys, because the
legacy evaluator can jump internally in that otherwise structurally valid case. The upstream
retained ferries pass the helper unchanged.

Targeted regressions cover an intersecting circle/rounded-square pair accepted at a separated
phase and rejected at a conflicting phase, equivalent phases across wrap, an inexact authored
period, a fractional-tick obstacle period, coprime tick periods exceeding the common-work budget,
an overlapping legacy ellipse, finite crossing/terminal-occupancy cases, and a structurally valid
stopped route with overlapping ramps rejected before surveying. Existing safe-loop
and retained-service conflict checks remain active. `cmake --build build --target
validate_boat_plugin -j 8` passed; `ctest --test-dir build -R
'^boat_plugin\.(free_scene_location|open_path_decode|scene_traffic)$' --output-on-failure`
passed 3/3 tests. Independent read-only review caught the overlapping-ramp discontinuity;
the helper and rejection regression above resolve it, and final review found no remaining
concrete blocker within the documented float/geometry assumptions.

Final C1 verification, 2026-11-14, Apple Silicon/macOS: native Debug build passed and
`ctest --preset full`, run with local socket access, passed **563/563 enabled tests** in one
invocation; 13 tests were disabled and GPU tests were OFF. Emscripten 6.0.12 MinSizeRel
`web` and `validate_boat_plugin` targets built. The optional Wasm validator initially failed
because its fixture target was missing before the native CMake early return; defining that
validator-only dependency in `cmake/Web.cmake` restored it. Node/Wasm cases
`free_scene_location`, `open_path_decode` and `scene_traffic` all passed on the final tree.
`git diff --check` passed. Changed documentation adds no external/local link targets; cited
commands and named regression targets were exercised. C1 changes validation only, so no new
visual capture was needed for this slice. Interactive browser, GPU/motion review and
Windows/Linux remain unverified; C2–C4 remain open. Existing uncommitted M3 changes were
preserved; no push or PR was made. No additional capacity expansion or M4/M5 work is included.

Integration verification, 2026-11-14: merged `origin/main` at `f8c3fbf` into the M3
branch for PR #201. The sole conflict was the `marina_pass` initializer; retained main's
bounded `set_id` helper and the M3 route data. Native Debug build passed without warnings;
full CTest with local socket access passed **568/568 tests** in 54.10 s. GPU tests remain OFF;
no new Wasm or audiovisual run was performed for this merge. `git diff --check` passed.

Effort assessment (inference from source/history, not session usage telemetry): 15 commits
built timetable, leg, arc, clearance, traffic, plugin, staging and room layers before the
integrated demo. The continuous-clearance requirement and repeated cross-layer integration
explain substantial work. Stale status text and an unresolved subset-versus-contract decision
make repeated continuation expensive. No precise token or elapsed-work attribution is available.

Fresh audit verification, 2026-11-14, Apple Silicon/macOS, existing Debug configuration:
`cmake --build build -j 8` passed. `ctest --preset full` passed 549/563 enabled tests;
14 socket-dependent tests failed under sandbox restrictions (including an explicit
`PermissionError` on loopback bind). All 14 passed with local socket access using
`ctest --preset full --rerun-failed --output-on-failure`. Thirteen tests were disabled.
This is a full run plus a failed-test rerun, not a single clean full invocation.
`git diff --check` passed; the audit adds no links or command examples requiring new targets.
Earlier dated results above are historical evidence. GPU tests are disabled in this
configuration. No new visual, browser or Windows/Linux review was performed.

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

### M4 decisions — 2026-11-14

- Route cap: lift M3's one authored route to a small N (initially 4 independent `open` paths),
  one path per vessel. A resident round trip is one path that starts and ends at its berth; a
  visitor's stay is the path's dwell piece at the berth. Moving-survey work stays under the
  existing cap, so N is bounded by the proof budget, not by a constant alone.
- Outside its service window a resident is moored at its berth and a visitor is absent (no hull,
  contact, camera exclusion or wake). A visitor parked elsewhere before arrival is deferred: it
  needs a second resting place and ownership rule.
- Berth binding: the berth is the single source of the terminal pose. Authors name the berth ID;
  the path's berth endpoint is derived or checked against it, never authored independently.
- Services are one-shot (no repeat) in M4; repeat is deferred explicitly.
- Ownership is one pure function of (vessel, tick): moored, travelling or absent.
- Native owner budget raised 1 MiB → 1.25 MiB (user-approved 2026-11-15): vessel/service data adds
  about 1.2 KB to each of ten revision-slot scene copies, which exceeded the old 500 B headroom.

O1a/O1b status, 2026-11-15 (macOS Debug, local): `src/core/boat_service.[ch]` owns exact-tick
ownership, berth-pose binding and reservations; `BoatSceneDefinition` gains empty berths, up to
4 vessels and 8 one-shot services, validated for missing IDs, static-hull berths, hull fit, pose
binding, one service per route, resident exclusivity and berth/vessel window overlap.
Tests `boat_service` and `boat_scene_service` pass; the full CTest run passed 561/561. The
Lua decoder, wire and route cap above one path are not yet connected.

O2 status, 2026-11-15 (macOS Debug, local): `src/core/waterfront_service.[ch]` is the single hull
evaluation. The harbor frame gives each static (non-empty) berth hull a compact slot, then each
vessel a slot that keeps its index while absent (`visible` false), with ropes only while moored.
Traffic frames hide a serviced route actor (no mesh, lights or wake); contacts and camera
exclusion use the same service hull, naming the route actor. Moored and travelling hulls both use
`boat_water_pose`, so the handoff has no wave pop by construction (test: ≤5 cm across it). Test
`waterfront_service` checks exact ticks around every handoff; the full run passed 571/571.

O3 status, 2026-11-15 (macOS Debug, local): Lua `empty` berths, `marina.vessels` and
`marina.services` decode with field paths; the staging wire is PSTC and the room definition v6
inside protocol v16 (golden bytes and digest updated), and old content is refused by version.
The survey lifts M3's one-path cap: authored paths are checked pairwise over their shared window,
a service vessel uses its own hull, may touch only its berth's pier during docking pieces, and
empty berths are open water. Cases `marina_service_decode` and `marina_service_survey`; the full
run passed 573/573. The JSON schema tool lists the new fields. Not yet done: the example package,
CPU/GPU frames, browser/Wasm and room end-to-end runs, and a loop-path service.
Limits: a serviced vessel has no visual wake yet.

O4 status, 2026-11-15 (macOS Debug, local): `plugins/marina-berth-demo` has two empty berths, a
resident round trip (5–25 s) and a visitor stay (2–21 s, moored 14–16 s). CPU and GPU stills
show authored hull colours and no doubled or missing hull at the handoffs; the model cache builds
per-vessel models. A service timetable edit now classifies as a session restart (it was live).
Case `marina_berth_apply_undo`; the full run passed 574/574. Evidence:
[2026-11-15](evidence/marina-m4/2026-11-15/README.md). Not done: browser/Wasm (no Emscripten
here), room end to end, independent sessions, and a watched motion review (the windowed run
exited cleanly but was not observed).

O5 closing, 2026-11-15: the contract moved into [Plugin API](PLUGIN_API.md#marina-vessel-services-m4);
superseded 'one path' and 'no berth binding' statements, the 1 MiB owner references and the route
demo/traffic pointers were corrected. Leftovers, all explicit: a parked visitor (needs a second
resting place), visual wakes for serviced vessels, repeating services, loop-path services, M5
modules, and the owed checks listed above (none of them run).

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

### M5 execution package — 2026-11-15

Historical: the plan as written before W1, from a read of the code then; "What exists" describes
that tree, not the current one (see the W1-W6 results below and the
[Plugin API](PLUGIN_API.md#package-modules-m5)).

**What exists.** A package is two files: `plugin.json` (4 KiB) and one entry (`main.lua`, 64 KiB
`BOAT_PLUGIN_SOURCE_MAX`). `boat_plugin_io.c` reads them (regular file, `O_NOFOLLOW`), and every later
stage carries one `(source, source_size)` pair: the eval request (`PEQ1`, 128 KiB), the stage worker,
`BoatPluginRevision` (ten retained revisions inside the 1.25 MiB native owner budget), the build-time
catalog generator, room package/objective/record sources, the browser worker and the bundle JSON
(`wake-velocity-plugin` version 1, `{manifest, source}`). The sandbox removes `require`; each execution
builds a fresh Lua VM, so isolation between instances and revisions already holds. `harbor-room`'s
scene is a hand copy of `harbor-life`'s ("copied until M5").
`BoatPluginDiagnostic.file` is 16 bytes.

**Decisions to confirm (recommendation first).**

- D1, what a module is: a file named by the manifest. `"modules": ["marina", "marina.berths"]`,
  at most 8 entries, names are dot-separated `[a-z][a-z0-9_]*` segments (at most 3, 32 bytes) and resolve to
  `modules/<a>/<b>.lua`. No directory walking, so native, catalog and browser see the same bounded
  list, and an undeclared `require` fails by name. Alternative: discover `modules/` by directory.
- D2, size: the entry and all modules share the existing 64 KiB. They are packed into one
  length-prefixed *source image* held in the existing `source` buffers, so the eval request, revision,
  wire, record and owner budget keep their sizes and the carriers change little. Alternative: raise the
  total, which needs an owner-budget re-measure like M4's 1 MiB to 1.25 MiB.
- D3, bundle format: `wake-velocity-plugin` version 2 adds `modules` (name to text); version 1 is
  rejected with a clear message, as the plan's no-compatibility rule allows. Alternative: also accept
  version 1 as a bundle with no modules.
- D4, shared marina source: `plugins/_shared/eriksberg_marina.lua` is the source; each of
  `harbor-life` and `harbor-room` carries a checked-in copy under `modules/`, kept equal by a CTest
  drift check and `tools/plugin_author.py sync-shared`. Alternative: copy at CMake/package time, which
  leaves the checkout packages incomplete.
- D5, deferrals: M5 does not add a package-to-package import, a `package` table, `load`/`dofile`,
  module hot-reload outside the existing Apply/Undo, or any module access to files. The M4 leftovers
  (parked visitor, serviced-vessel wakes, repeat, loop-path services) stay deferred.

**Steps (one commit each; one branch and PR).**

- W1, bundle and IO. Manifest `modules`, source-image pack/unpack, `boat_plugin_read` reads declared
  modules under the same regular-file/no-symlink rule, bounded by count and total bytes. Widen the
  diagnostic's file field for `modules/<name>.lua`. Tests: traversal and absolute names, symlink,
  missing and undeclared files, oversize, duplicate names, a module name that collides with the entry.
- W2, `require` in the VM. A native `require(name)` over the image, with a per-VM cache, a loading set
  for cycle detection, the sandbox environment for each module, chunk names `modules/<name>.lua` and
  returned value checked as the module result. Budgets stay per VM, so instructions and heap are
  cumulative across modules. Tests: nested and cached imports, a table shared inside one evaluation but
  never across two, cycles, missing, budget exhaustion inside a module, errors reporting the module and
  line.
- W3, carriers. Confirm each one carries the image: eval worker, stage job, host revision and
  restart classification (a module-only edit changes identity and is classified like an entry edit),
  catalog generator, room package/objective/record, native `--plugin`. Tests: module-only edit with
  Apply, Undo and a failed stage leaving the active revision, native worker and catalog round trips,
  record replay with modules.
- W4, authoring tools. `plugin_author.py` export/import/check/draft with `modules`, `plugin_schema.py`,
  the browser bundle (`plugin_author.js`, `/plugins/browser/`), its import/export, and Web Worker
  evaluation. The Wasm build is not available locally; native `web_*` Node tests cover the JS, and
  the Wasm run stays owed.
- W5, shared marina. Move the Eriksberg scene into the shared module, update both packages and the drift
  check, README and anchors; `harbor-life` and `harbor-room` must decode to the same scene as before.
- W6, integrated example. The larger marina with customized hulls and painted names, a resident round
  trip and a visitor's complete stay, as one package using modules. Measure frame, memory and staging
  against #131 at the same camera, time and options; CPU/GPU stills and a windowed run separately.
- W7, closing. Move the module contract into `docs/PLUGIN_API.md`, fix superseded statements
  (the sandbox's "no `require`", the one-source bundle format, "copied until M5"), update
  `docs/README.md` and this plan, and list or drop leftovers.

**Deferred and open.** From the Open loops board, nothing here depends on or closes another track.
The M4 leftovers above remain open; so do the browser/Wasm and room runs, which need Emscripten and a
room and which W3 and W4 touch.

### M5 W1 result

Implemented 2026-11-15: manifest `modules` (at most 8; dot-separated `[a-z][a-z0-9_]*` names, at most
3 segments and 32 bytes, unique, never `main`), `src/plugins/boat_plugin_image.[ch]` (a versioned
little-endian source image: magic `\0PMI` + version 1, a file count, then per file a one-byte name length,
the name, a u32 length and the bytes, entry first, whole image at most 64 KiB), module reads in
`boat_plugin_read` (each directory component opened without following links on POSIX; the Windows branch
checks reparse points per component and is not compiled here), and `BoatPluginDiagnostic.file` widened
from 16 to 64 bytes (wire forms are length-prefixed, so layouts and versions are unchanged). A package
without modules keeps the raw entry bytes as its source; an image is produced only when modules are
declared, and the Lua evaluators refuse an image by name until W2. `tools/plugin_schema.py` gained the
optional `modules` property.

Verified on macOS Debug: new cases `boat_plugin.manifest_modules`, `boat_plugin.plugin_image`,
`boat_plugin.image_refused` and `boat_plugin.package_modules` (valid flat and nested modules, missing,
directory, symlinked file, symlinked and non-directory `modules` or intermediate directory, oversize
file, oversize total, exact budget, undeclared files ignored, entry mimicking the magic) pass. Not
verified: the Windows read branch, Wasm/browser (bundle format is W4), and any carrier that stores an
image (W3).

### M5 W2 result

Implemented 2026-11-15: the Lua evaluators (voyage, free, course, race, match, query and the
transition/objective runner, so callbacks too) accept raw entry text unchanged or an image. An
image is unpacked and validated (malformed is `BOAT_PLUGIN_SYNTAX`, "malformed package source
image"); `require` is installed only when it carries modules, as a native function closing over
the caller's image bytes with a registry-held cache and a loading stack in the per-VM state.
Semantics: valid declared name only, chunk name `modules/<path>.lua`, same globals as the entry, a
nil/no result caches `true`, cycles fail as `module cycle: a -> b -> a`, nesting at most 8. Failures
in a module name its file and line (`failure()` takes `modules/...:LINE:` from the message; a
module syntax error stays `BOAT_PLUGIN_SYNTAX`). The instruction-budget error is now raised without
`luaL_error`'s caller prefix, which had named the calling function's line for budget exhaustion
inside a called function. Limits: an `error(msg, 0)` in a module has no position and reports the
entry's file.

Verified on macOS Debug: `boat_plugin.require_modules` (nested, cached identity, shared and unused
modules, nil/non-table results, no `package`, two evaluations isolated, cycles, missing, 13
invalid names, syntax and runtime errors in modules with file and line), `require_budgets`
(top-level and later-called spin, budget summed across modules, heap), `require_scene` (a scene
from a module equals the inline scene), the malformed-image rejection `image_refused`, and
`boat_plugin_state.module_callbacks` (callbacks reload modules; errors and budget name the module).
Not verified: carriers storing an image (W3), the browser/Wasm VM, and any shipped plugin using
modules (W5).

### M5 W3 result

Implemented 2026-11-15 in one commit. A package's source image travels in the existing
`(source, source_size)` buffers through every carrier; no wire layout or version changed.

**New check.** `boat_plugin_check_source(manifest, source, size, d)` (data only, in
`boat_plugin.c`) runs wherever a manifest and a source meet: `boat_plugin_bundle_manifest` (every
`boat_plugin_load_*`, so the host, room packages and `--plugin`), `boat_plugin_load_race`, the stage
and query workers, the catalog generator and the catalog wire decoder. Modules are declared exactly
when the source is an image, the image unpacks completely, its entry name is the manifest's `entry`,
and its module names equal the manifest list in the same order; otherwise `plugin.json modules` (or
`entry`) is refused with a message, or `malformed package source image` (syntax class).

**Classification rule.** Identity of a revision includes its source image, compared as bytes, exactly
where source is compared today. Classification is by decoded data (`boat_plugin_classify` and its
free/course/race/match forms); for a voyage objective package the retained source bytes are also
compared, so a module-only edit that changes a callback is migrate or restart like an entry edit.
A module-only edit that changes neither decoded data nor an objective package's bytes is
`BOAT_PLUGIN_UNCHANGED`: nothing is installed and no revision is made, exactly as for an entry edit
(non-objective revisions do not retain source). The save-polling key (`boat_plugin_read_key`) folds
the whole image, so a module-only edit is seen as a change. Unchanged pre-existing gap, not made
worse: free-kind objective classification does not compare source, for entry or module edits alike.

**Per carrier.** Changed: `boat_plugin_stage.c` workers, `boat_plugin_wire.c` decode, `boat_plugin_load.c`
(check), catalog generator (check) and CMake (module files are build dependencies of bundled
packages), `wake_velocity_plugin.c` (`/save` wrote the image as `main.lua`; now writes entry and
module files; the regenerated manifest of `wake_plugin_promote`/`wake_plugin_save` dropped `modules`,
now kept), `wake_command.c` (console edits and queries wrap the entry as text; they refuse a package
with modules), `boat_plugin_bench.c` (refuses modules), `boat_room_package.c` (a module failure's
error carries `modules/x.lua:LINE`). Needed nothing: `boat_plugin_io.c` (W1), eval request/worker and
`boat_plugin_state.c` (length-framed, never text-checked), stage wire framing, host revisions and
`boat_plugin_revision.h`, stage backends (posix, windows, web), `boat_plugin_worker.c`,
`wake_velocity_bridge.c`, `boat_race_plugin.c`, `boat_plugin_review.c`, `boat_room_objective.c`,
`boat_room_record_objective.c`, `boat_session_record.c` (length plus byte hash; no format or version
change, `MULTIPLAYER_PROTOCOL.md` and `SESSION_RECORDING.md` untouched), `wake_velocity_objective.c`,
`wake_velocity_plugin_web.c` (byte pairs). `cargo_sandbox`'s self-test wraps entry text but runs only
shipped single-file packages.

**Verification (macOS Debug, native).** New: `boat_plugin_modules` (read, host revision, real stage
worker, module-only edit with RESTART_VOYAGE, Apply and Undo of images, a decoded-data edit, UNCHANGED
for comment-only module and entry edits, three module failures leaving the active revision with the
module file named, manifest/image mismatch rejections, query, save and regenerated-manifest round
trip, console refusal), eval case `image_source`, `boat_plugin_catalog_modules` (+ `_syntax`,
`_runtime`: the generator fails naming `modules/route.lua:2`), `boat_room_package.module_directory`,
`boat_session_record.hunt_modules`, and an image case in `boat_plugin_stage_web` (the web backend
compiled natively over its page model). Full `ctest --test-dir build -j 8` on the sandboxed
run: 591/591 passed (the W2 `require_*` cases now declare their modules in the manifest).

**Not verified / W4 must do.** The bundle JSON (`wake-velocity-plugin` version 2 with `modules`),
`plugin_author.py`/`plugin_schema.py`, `web/plugin_author.js` and its worker (import must build the
image with the same entry name and module order; export must unpack it), the browser export
(`console_save` passes the image as `source` today), the `wake_web_plugin_*` import path. Not run:
Windows read/save branches, Wasm, a nested-module `/save` (single-level only is tested), the
`F5` live reload of a module edit in a windowed game.

### M5 W4 result

Implemented 2026-11-15. A package with modules now travels end to end through the authoring tools
and the browser; the native side (W3) is unchanged except for one new golden check.

**Bundle `wake-velocity-plugin` version 2** is `{format, version: 2, manifest, source, modules}`;
`source` is the entry text and `modules` an object of name to text (`{}` without modules; the key is
required, so the key set is exact). Version 1 is rejected everywhere (browser decode, saved draft,
`plugin_author.py import`) with "version 1 bundles are not supported; re-export" (D3). Names must equal
the manifest's `modules` list; the package uses the manifest's order whatever the object's order.
Limits mirror `boat_plugin_read` (entry, modules and framing share 64 KiB, 8 modules, name grammar,
file named in each message); the native worker stays the authority.

**Python.** `plugin_author.py`: `pack_image`/`unpack_image`, `snapshot` (the manifest plus the entry or
image), `workspace-read/-write` (module files as `modules/<path>.lua`; a manifest naming a missing
module file is refused before writing), `export` (version 2), new `import DIRECTORY < bundle`, capture
writes modules into its temporary package, `capabilities` reports `bundle_version: 2`.
`plugin_schema.py` already had `modules` (W1); `package_plugin_tools.py` copies whole example
directories, so module directories ship unchanged; `package_web.py` writes the example as version 2.

**Browser.** `web/plugin_author.js` packs the W1 image byte-for-byte, builds it in the manifest's order
for the worker, unpacks the image `console_save` passes to `Module.boatPluginExport`, writes
`/plugins/browser/modules/...` (clearing stale modules first) and restores saved drafts, refusing a
stale version 1 save with the message instead of throwing. Editor: a **Module** selector with one text
area, an add box and **Add module**/**Remove module**, which rewrite the manifest's `modules` array;
a hand edit that disagrees is reported by name at stage/export. No C change was needed: the Wasm side
passes (manifest, source) bytes untouched.

**Agreement across implementations** is one golden vector,
`tests/support/fixtures/plugin_image_golden.hex`: the C packer (`boat_plugin_modules`), `plugin_author.py`
(`test_plugin_author.py`, class `Modules`) and `plugin_author.js` (`web_plugin_author`) each pack the same three
files (entry, `marina` with a non-ASCII character, `marina.berths`) and compare to it.

**Verification (macOS Debug, native + Node).** New Python `Modules` cases (golden, snapshot, real worker
with a module syntax error naming the file, read refusals, budget edge, name rules, bundle round trip
through the CLI, bundle refusals, workspace module writes, capture package), Node cases (golden,
frame/unpack, version 1 and malformed bundles, limits and messages, writePackage over a stub file
system, restore, and the editor wiring over a stub DOM). Not verified: any Chrome/Wasm run (no Emscripten
here; `check_web_plugin_author.py` gained a module import/stage/export step and a version 1 refusal
that were only compiled, not run), the real DOM and CSS of the module controls, Windows. Limits kept:
the in-game PC console refused a package with modules (`HAS MODULES`; lifted in W5, see its result);
`workspace-write` cannot create a module file, only edit existing declared ones.

### M5 W5 result

Implemented 2026-11-15. The Eriksberg marina scene lives once, in `plugins/_shared/eriksberg_marina.lua`
(a module that returns the scene table, with harbor-life's comments). `harbor-life` and `harbor-room`
each carry a byte-identical copy at `modules/eriksberg_marina.lua`, declare it in `plugin.json`
`modules`, and `require("eriksberg_marina")` for `scene` (D4). Before the move the two inline scene
tables were compared and were textually equal (no difference found); the shared file is harbor-life's
copy. The convention is in [Shared source and copies](PLUGIN_API.md#package-modules-m5).

**Equivalence evidence.** The scene, voyage and room digests were recorded from the single-file packages
(commit f402c87, before any edit) and are asserted as constants by the new `marina_shared`
(`tests/plugins/validate_marina_shared.c`): scene `0692ebb6e02eca36` (12 berths, 13 modules, 4 vessels,
1 flock), harbor-life voyage and catalog entry digest `e8509272172cb7c4`, harbor-room catalog entry digest
`0692ebb6e02eca36` (a free entry's digest is its scene's), harbor-room `boat_room_definition_digest`
`914a7a169b542df6`. The same test loads harbor-life from its directory (`boat_plugin_read` plus
`boat_plugin_load_voyage`) and the build-time catalog, and harbor-room through `boat_room_package_load` by
directory and by catalog id, and requires all four scene digests equal each other and the constants. Every
existing assertion remains; none was loosened (the tests that edit the packages' text are listed below).

**Test changes the move forced.** Validators that string-edit a shipped package's entry or pair its manifest
with other source read harbor-life/harbor-room as modules now, so they call the new
`tests/support/boat_test_flat_package.h` (`boat_test_flatten_package`), which inlines each `require` as an
immediately called function and drops `modules` from the manifest; the evaluated data is the same. Used in
`validate_boat_plugin`, `_session`, `_stage`, `validate_scene_site`, `validate_boat_room_package` and
`boat_test_module_package.h`. Fragments that spanned lines moved two spaces left with the module's
indentation. `test_plugin_author.test_schema` allows the optional `modules` key. Not flattened (they run the
real module image): `validate_harbor_life`, `scene_traffic`'s first load, the open-leg wire cases,
`marina_shared`, the catalog and the room package paths.

**Deviation: the PC console accepts packages with modules.** `wake_command` edits and queries of the
now-module-carrying harbor-life would otherwise have been refused (`HAS MODULES`, the W3 limit), and
`wake_command` edits harbor-life. The console now wraps the entry as before and re-packs it with the
active image's modules (`stage_form`); modules are never edited from the console. `boat_plugin_modules`
asserts an edit stages and a query sees it, replacing the refusal assertion; `wake_command` cleans the
saved `modules/` directory it now writes.

**Drift check and `sync-shared`.** `python3 tools/plugin_author.py sync-shared [PLUGINS-DIR] [--check]`
copies every `_shared/<name>.lua` into each package that declares that module, or already has a file of that
name under `modules/`; it creates `modules/` as needed and reports `checked`, `drifted` and `changed`;
`--check` writes nothing and exits 1 on drift. CTest `plugin_shared` (`tests/lint/check_plugin_shared.py`,
quick) runs the same function over the checkout and fails naming the file and the fix command;
`plugin_shared_detects` is its self-test (drifted, missing and undeclared copies, an unshared module left
alone, a `plugin.json` in `_shared`, and `_shared` listed in CMake or `EXAMPLES`). The Python
`SharedModules` cases cover the command line against a temp tree and the shipped tree.

**`_shared` is not a package.** Every enumeration is an explicit list (`BOAT_PLUGIN_PACKAGES` and the catalog
list in `src/plugins/CMakeLists.txt`, `EXAMPLES` in `package_plugin_tools.py`, the authoring layout's
examples, `package_web.py`); none globs `plugins/`. `plugin_shared` fails if any of them names a
`plugins/_` path, and `sync-shared` itself skips names starting with `_` or `.`. The release package's
`assets/plugins` gets whole example directories, so `modules/` ships and `_shared` does not
(`test_relocated_tools` checks both, and validates harbor-life and harbor-room from the relocated layout).
Catalog regeneration: `src/plugins/CMakeLists.txt` lists each package's `modules/*.lua` (W3), so editing a
package copy regenerates the catalog and touching `_shared` alone does not (checked by touching each).

**Bundles.** `export` of harbor-life and harbor-room is a version 2 bundle whose `modules` holds the shared
text; import then snapshot equals the source package and the real worker validates the imported copy with
the same gameplay digest (`SharedModules.test_marina_packages_bundle_and_round_trip`).

**Verification (macOS Debug).** Full suite `ctest --test-dir build -j 8`: 594 of 594 passed. Not verified:
Wasm/browser runs of the module-carrying examples (file lists and Node-free Python round trips only), the
Windows/Linux builds, any windowed, GPU or visual check (the scene digests are equal, so no visual change
is expected, but no capture was taken), and human acceptance. Console edits of a module package were
exercised headlessly only.

### M5 W6 result

Implemented 2026-11-15: `plugins/marina-integrated-demo` (an example, not a catalog or release entry,
like the M3/M4 demos), four modules plus an entry. `eriksberg_marina` is the W5 shared copy (the drift
check covers it: three packages now); `marina` requires it, deep-copies it (the module result is
cached for the whole evaluation) and appends two empty guest berths and two floating platforms;
`guests.routes` holds the two authored paths and `guests.services` the two vessels and services (it
requires `guests.routes`); `main.lua` assembles the voyage and appends the paths to the marina's
traffic. The requires nest and one module name is dotted (`modules/guests/*.lua`).

**Design decisions.** The resident and visitor use two new side-to berths on a guest dock at
`z = 231`, north of the passing craft's loop (`z` 184-198), not slips of the shared marina. A service
path is forward-only and starts and ends at the berth pose (the resident) or must dock there and leave
from it (the visitor); the marina's slips have a walkway at the stern, so no tangent-continuous round
trip ends in one. This is reasoning from the geometry, not a rejected trial. The shared module's data
is not edited, and harbor-life and harbor-room are unchanged. The timetable is the M4 timetable
(resident away 5-25 s, visitor 2-21 s moored 14-16 s) with the geometry moved 36 m north; limits used:
14 of 32 berths, 15 of 64 modules, 6 of 8 traffic vessels, 2 of 4 vessels, 2 of 8 services. Valid visitor
`start_tick` on a 120-tick grid: 0, 120, 240 and 1440 onwards; 360-1320 cross the racetrack.

**Tests (macOS Debug).** `boat_plugin_session.marina_integrated_apply_undo` (counts, painted names, owner
at 16 ticks including half-open boundaries and a far tick, a module-only `start_tick` edit refused
at 360, staged at 120 as a session restart, Apply, Undo, and four failed module edits, each naming
`modules/marina.lua`, `modules/guests/routes.lua` or `modules/guests/services.lua`, leaving the active
revision), a real stage-worker block in `boat_plugin_modules`, and
`boat_plugin_author` `SharedModules.test_integrated_marina_example` (bundle export/import, worker,
digest change on a module-only edit). `tests/support/boat_test_module_edit.h` edits one module of an
image in memory. Limit found: a layout or timetable the decoder rejects reports the scene field with
file `main.lua`, because it sees the assembled scene; only Lua syntax and runtime errors name the
module file.

**Evidence and costs** ([2026-11-15](evidence/marina-m5/2026-11-15/README.md)): CPU and GPU stills at
the key moments, plus a measurement against `harbor-life` at the #131 camera/time/options with a
manual tool, `measure_marina_package` (not in CTest). The #131 absolute numbers (9.2/9.5 ms) are not
reproduced by this tree on this host (`validate_harbor_life` prints 13.7/14.3 ms), so the baseline is
harbor-life today. Three runs each: CPU p50 about 0.2 ms (1.5%) above harbor-life, within run spread
of the compiled reference; GPU 4.0-4.4 ms in both; staging 786 ms to 832 ms in process (+5.8%) and 1,613
to 1,705 ms through the worker; Lua heap 39 to 61 KB; native owner unchanged. Debug only.

**Not verified.** Browser/Wasm, a room end to end, Windows/Linux, a Release build, a watched motion
review of the windowed run (it ran 3,607 ticks, 12 times, exit 0, unwatched), human acceptance.
**Finding for the owner of M2 lettering:** in-game GPU stills at high resolution show the painted names
legible but mirrored from both sides, byte-identically for harbor-life, while `harbor_review` reads
correctly; not caused by W6 and not isolated.

## Closing leftovers — 2026-11-15

The closing step (W7) moved the module contract into
[Package modules](PLUGIN_API.md#package-modules-m5) and the M4 contract was already in the Plugin API.
Nothing below is built. Each item is open unless marked dropped; none is scheduled.

Added 2026-11-19: the agent map query (`./wv map`, [Map query](PLUGIN_API.md#map-query)) gives an authoring
agent exact water/land probes and an ASCII grid with the draft overlaid. Still owed for agent-written
marinas: curated anchors for the other Eriksberg basins (the east side and Lilla Bommen) once their
coordinates are read off the grid, and human review of what an agent produces.

Open, product and behaviour:

- M4 leftovers: a parked visitor needs a second resting place; repeating services; services on loop
  paths; and the weakly tested dock exemption in the pairwise survey (`dock_exemption` and `dock_exempt`
  in `src/plugins/boat_plugin.c`, which let a serviced vessel's route clear its own berth's module while it
  docks).
- Visual wakes for serviced vessels: undecided and deferred. The user never answered the deferral
  recommendation.
- Diagnostics for a module-assembled scene: a decoder rejection names the scene field under `main.lua`,
  not the module that built it.
- `error(msg, 0)` and out-of-memory errors in a module report the entry file.
- The console cannot create or edit modules, and `workspace-write` cannot create a module file.
- The guest-dock placement of `marina-integrated-demo` (z = 231, north of the passing craft): the user
  accepted it.

Open, verification not performed:

- A browser/Wasm run of module bundles (no Emscripten was available).
- A room end to end with a module package.
- An independent-session comparison.
- Windows and Linux builds of the new IO and `modules` paths.
- Release-build measurements (the W6 numbers are Debug only).
- Human motion review of `marina-berth-demo` and `marina-integrated-demo`; the windowed runs were
  never watched.
- The measured baseline in the #131 note is not reproduced by this tree on this host (13.7/14.3 ms
  here against 9.2/9.5 ms there), so W6 compared against `harbor-life` today.

Unconfirmed observation, owner of M2 lettering: painted hull names read mirrored in in-game GPU stills
at high resolution (`gpu-names-01s.png`-style views), while `harbor_review --renderer gpu` reads them
correctly. The cause is not isolated; the user chose to leave it open.

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
rather than supporting arbitrary over-water construction. The two packages duplicated
scene data until M5 W5 (now one shared module). CPU/GPU captures and scripted window playback are separate from human
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

The [plugin plan](PLUGIN_PLAN.md) owns the broader roadmap.
