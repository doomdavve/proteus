# Architecture review: growing into a boating game

Reviewed 2026-10-19 against `f92ed70` on macOS. This is a source-based assessment and
proposed sequence, not an implemented migration or a new gameplay commitment.
Gameplay direction remains in [BOATING_PLAN.md](BOATING_PLAN.md).

Status: source review and clock probe complete; verification results recorded below.
The [foundations implementation plan](BOATING_FOUNDATIONS_PLAN.md) owns the selected next
sequence and its task status. P1/P2a/P3a, C1–C3 and the basin W1/W3, S1/S2, S3 and S4 slices
are now implemented; see that plan for the next task.
No runtime changes in this review.

Current-context note, 2026-10-23: this is a dated baseline. Arcade wakes now use four
emitters and 128 objects, flight/landing/capsize and mutual height-aware pair impacts exist,
and handling compatibility was revision 5 at this review. C1 subsequently advanced
compatibility to revision 6. The 64-entry shader discussion and ABI sizes below
describe the reviewed older version. The [multiplayer source assessment](MULTIPLAYER_PLAN.md#source-assessment-and-corrections-to-older-assumptions)
records current symbols, remaining boundaries and an authoritative native-first design.
Its prerequisites use [foundations slices](BOATING_FOUNDATIONS_PLAN.md#multiplayer-prerequisite-slices--2026-10-23),
not a competing refactor sequence. Multiplayer has since reached a diagnostic protocol/transport
milestone (M3, 2026-10-23); this review predates it.

## Recommendation

Keep C17, raylib, fixed-step physics, and the compact visual style. The next useful investment
is explicit ownership and a few enforceable module boundaries. The current code can support a
small explorer/time-trial game; larger maps and interacting racers expose assumptions that are
currently reasonable for a single player in one loaded location.

Preserve these strengths:

- The 120 Hz step, deterministic driving tools, and physical invariants in the validators.
- Owned scene snapshots shared by color, shadow, and reflection passes.
- The shared analytic water definition and production-shader parity checks.
- Bounded particles, wakes, and lights; static GPU geometry and the nearby water patch.
- Portable, validated pose recordings, atomic file replacement, and handling revision checks.
- Separate visual boat recipes, platform packaging, and relocation smoke checks.

CPU rendering remains valuable as a reference and diagnostic path. It need not gain every
future GPU cosmetic feature. Define parity around coordinates, physical surfaces, poses, and
gameplay visibility, with a few intentional visual baselines.

## Findings and priorities

### 1. Establish a long-session clock before expanding exploration

[`Boat.time`](../src/boat.h) is a float incremented by `BOAT_STEP` in
[`boat_step`](../src/boat.c). It drives water, traffic, and presentation. A standalone C probe
using the exact increment produced these results on this Mac:

| Fixed ticks | Intended seconds | Accumulated float seconds |
| --- | ---: | ---: |
| 28,800 | 240 | 239.963470 |
| 72,000 | 600 | 600.197449 |
| 432,000 | 3,600 | 3,591.572266 |

This is a clock-arithmetic reproduction, not an hour of gameplay playback. The course timer
already uses a separate double and does not have this same float-accumulation defect.

Use a session-owned integer tick counter and derive seconds with double arithmetic. Keep
bounded/local phases for float shader calculations, including wake birth times. Simply casting
an ever-growing double to float in shaders does not solve eventual phase precision loss.
Treat the clock correction as a potentially observable simulation change: preserve reference
traces, check water/traffic interactions, and assess replay/handling revision compatibility.

### 2. Make world ownership explicit before adding location transitions

[`waterfront_scene.c`](../src/waterfront_scene.c) selects the river through a process-global
boolean. [`boat_map.c`](../src/boat_map.c) lazily caches shoreline edges globally, while
[`boat_water.c`](../src/boat_water.c) caches surfaces by a basin/mapped boolean. The API explicitly
requires map selection before cache/resource initialization. This works for the current
launch-once location choice; it is not a safe runtime world-switching API.

Introduce a small `World` owner with an explicit location ID, immutable map data, collision
index, water settings/history, and actor state. Give map-dependent CPU/GPU resources an explicit
load/unload lifetime. Prove load A, unload A, load B, return to A without stale caches or leaks.
Keep immutable reusable geometry shared where useful.

The `Boat` currently owns environment settings and wake history. With multiple boats, a shared
world water field must receive emissions from every relevant vessel. Update phases explicitly:
read the current field, step vessels in a stable order, then publish new wake emissions. Define
whether newly emitted wakes affect the next tick so vessel iteration order cannot change results.

### 3. Separate simulation, session rules, and presentation incrementally

[`boat.c`](../src/boat.c) contains forces, contacts, particles, camera, scene construction, and
CPU drawing. `Boat` contains course progress and presentation state alongside physics.
[`boat_course.c`](../src/boat_course.c) mixes timing with HUD rendering; physics validators
therefore pull in scenery and drawing implementations. [`tools/wake_velocity.c`](../tools/wake_velocity.c)
is now the actual game application, with input, pause, replay transport, audio, saving, and
rendering coordinated in one loop.

Extract along these existing responsibilities, without changing handling in the same patch:

| Owner | Responsibility |
| --- | --- |
| App/session | Input commands, pause, fixed-step accumulator, mode, race progression, saves |
| World | Location data, collision queries, shared water, traffic and interactive actors |
| Boat simulation | Boat state/configuration, forces and contacts for one fixed tick |
| Presentation | Camera, cosmetic particles, HUD, audio parameters, render snapshot |
| Render backends | CPU/GPU resources and consumption of the same snapshot |

The essential dependency rule: simulation does not call drawing, keyboard, audio-device, or
filesystem APIs. Passing small raylib math types is acceptable; replacing every vector type
would add work without enforcing that rule. Start with concrete structs/functions, not a
generic plugin engine or ECS.

Add a `BoatInput` command type so keyboard, controller, scripted driving, and future AI use the
same step entry point. Keep previous/current physical poses for render interpolation when
frame pacing needs it; visual interpolation must not feed back into collision or race timing.
The current 0.1-second delta clamp bounds catch-up work but drops time during long stalls.
Count dropped time and choose an explicit race-validity policy before competitive leaderboards.

### 4. Spatial queries and render visibility are the main growth risks

[`boat_map_water_contact`](../src/boat_map.c) searches water triangles and scans every shoreline
edge. Normal clear-water translation and rotation checks together can issue twelve queries per
120 Hz tick, before traffic correction and presentation queries. This is a visible scaling
problem in the algorithm; it is not a measured claim that collision dominates today's frames.

Build a simple static grid or BVH over collision edges/triangles and query the swept hull's
neighborhood. Preserve the current exhaustive query as a test oracle. Validate corners, narrow
gaps, crop boundaries, bridge supports, and candidate-cell boundaries. Add swept hull contacts
when speed/thin obstacles require them; six point samples plus rollback can miss obstacles
between samples and do not provide general continuous collision detection.

The GPU renderer groups the city into material meshes and draws them in main, reflection, and
shadow paths. It has raster backface culling, but no world-chunk visibility selection in those
paths. For a larger map, retain batching *within spatial chunks*, attach bounds, and select
visible chunks separately for each pass. Main-camera visibility alone is insufficient for
off-screen shadow casters and reflected objects. Instance repeated boats, gulls, or props only
when draw-submission measurements justify it; keep caps and distance-based detail first.

### 5. Measure the actual boating workload before optimizing

The boating loop's scene timer starts after physics and audio updates; the overlay exposes
frame, scene, and present time, not separate simulation cost. GPU route reports correctly label
CPU submission time. These are useful tools, but neither establishes actual GPU execution cost.

Proposed initial target: sustained 60 fps (16.67 ms per frame) on an explicitly named minimum
machine, with headroom. Keep the 120 Hz simulation until measurements and feel justify changing
it. CPU and GPU work overlap, so report their budgets separately rather than adding submission
time to GPU execution time.

Record Release-build median/p95/p99 frame times, tick cost and ticks/frame, dropped simulation
time, draw calls/triangles by pass, water-patch rebuild/upload spikes, and memory high-water marks.
Use GPU timing where supported; otherwise label that measurement unavailable. Use Instruments
to attribute unresolved CPU stalls. Avoid strict millisecond CI gates on variable shared runners;
retain dedicated-machine baselines and deterministic workload/count checks.

Benchmark dock departure, dense harbor, river/bridge, shoreline impacts, a filled wake history,
course/ghost recording, and finish/save. Include AA on/off, steady state, and cold/rebuild spikes.
The existing route audit is a good starting fixture, but it does not cover all these workloads.

Two specific costs to investigate:

- [`boat_gpu.c`](../src/boat_gpu.c) renders AA at 1280x800 before downsampling to 320x200:
  sixteen times the main-target pixel count. Both water shaders loop over live wake entries
  (capacity 64), with early exits. Bound nearby wake work and expose quality settings before
  multiplying history by the number of vessels. Preserve exact physics queries separately from
  visual distance/detail choices, and retain CPU/GPU field checks.
- Water patches rebuild/upload after quantized center changes; replay recording can grow via
  `realloc`, and a finish synchronously copies/writes a recording from the physics update path.
  Measure spikes, reserve known recording capacity where useful, and queue save work after the
  tick. Add a background writer only if that remaining stall warrants its complexity.

On this ABI `BoatWater` is 2,148 bytes and many query APIs pass it by value; `Boat` is 19,248 bytes
and the scene snapshot 61,940 bytes. These are bounded and reasonable at today's scale. Prefer
`const` pointers for shared read-only water queries and keep an intentional owned frame copy.
Compiler optimization may remove copies; these sizes are not proof of a current bottleneck.

### 6. Make content explicit before multiplying boats and courses

Water collision/topology currently relies on leading triangles having exactly zero height.
GPU ground/material classification also uses exact colors and height thresholds. An art/import
change can therefore alter semantics without changing a gameplay definition.

Have the importer emit explicit surface/material IDs, collision contours, bounds, stable object
IDs, and navigable-water metadata. Rendering and collision should be generated from common
authored data; collision should not discover its meaning from paint colors or triangle order.
Centralize simulation/import/render coordinate conversions: the reflected X convention currently
appears in physics, mesh code, and horizontal GPU presentation flips. Keep cardinal-direction,
camera, gate, and contact tests during any migration.

Introduce `BoatSpec`, `CourseDefinition`, and `WorldDefinition` as typed C data first. Move
frequently edited values to validated external data when that improves the actual iteration
loop. Keep visual boat recipes distinct from handling/collision specifications. Course gates
currently assume northbound Z-plane crossings; arbitrary racing courses need oriented gates,
ordered progression, lap/reset rules, and stable course IDs.

Keep pose recordings for ghosts: they avoid depending on cross-platform bit-identical physics.
They are not resumable world saves or authoritative proofs of a race. Before adding boats,
courses, or environment variants, key bests by course/content revision, boat/handling revision,
and environment/rules. A save game additionally needs location, progression, and relevant world
state. Decide multiplayer's priority before designing interacting racers or online validation;
fixed-step floating-point physics alone does not establish deterministic networking.

### 7. Turn existing validation into a cheaper development loop

[`CMakeLists.txt`](../CMakeLists.txt) compiles shared source lists separately into many executables.
Use a few static/object libraries aligned with the boundaries above, preserving target-specific
asset paths and definitions. This reduces repeated compilation and makes dependencies reviewable.
Make asset-copy work incremental; the current custom target copies the asset tree each build.

Keep physical invariant tests, deterministic scenarios, replay-parser validation, and GPU parity.
Add focused checks for world reload, long-session time, frame-rate-independent command playback,
and indexed/exhaustive collision equivalence as those changes land. Test failure paths and run
ASan/UBSan in a separate development/CI configuration. Split fast simulation checks from slower
visual integration checks. A screenshot command exiting successfully is a smoke test, not by
itself a visual regression assertion.

macOS CI runs the non-GPU suite; Windows CI currently runs handling and two CPU screenshots.
Expand Windows to the applicable CPU suite, especially replay persistence and environment tests.
Keep a separate graphics-capable verification lane and manual GPU/audio/input checks. Runtime
asset relocation is already tested and should be preserved.

## Suggested sequence and stopping points

1. **Next small changes:** performance baseline/counters; integer world clock with soak checks;
   shared CMake targets; broader CPU CI and sanitizer configuration. Separate behavior-changing
   clock work from structural extractions. Leave accepted handling coefficients unchanged.
2. **Before another substantial world/gameplay feature:** explicit world lifecycle; simulation /
   presentation split; immutable boat/course definitions; spatial collision index when map/query
   measurements warrant it. A second boat or course should exercise these boundaries naturally.
3. **When expanding geography or fleet:** spatial render chunks, per-pass visibility, actor detail
   budgets, shared bounded wakes, resource lifetime/streaming policies, and versioned persistence.
   Load whole small maps until memory or loading-time evidence calls for streaming. Introduce
   local origins if geographic scale causes observable precision issues.

Completion criteria should be concrete: existing handling scenarios still pass, an unrelated
render edit does not rebuild physics, a second course does not add branches inside boat forces,
switching maps releases the old map, and collision cost follows nearby geometry rather than
the entire region. Profile and play after each meaningful change.

Other product decisions affect architecture: exploration persistence versus short race sessions;
AI opponents versus ghosts; controller remapping/deadzones and camera comfort; readable speed,
collision, and gate feedback at low resolution; recovery after grounding; audio volume/settings;
and the hardware/OS support matrix. Preserve map/music/third-party source and attribution
metadata as content grows. These are design inputs, not reasons to start a general engine rewrite.

## Verification and limits

- 2026-10-19, macOS: Debug configure and build completed at the reviewed revision. CMake emitted
  raylib's existing macOS OpenGL deprecation notice.
- Full Debug CTest run passed: 69/69, including 18 GPU checks, in 147.08 seconds with the
  existing `PROTEUS_GPU_TESTS=ON` configuration. Documentation local file links and diff
  whitespace checks passed.
- Standalone optimized C clock/size probe ran using the repository headers. Local scratch source
  and executable are under ignored `build/architecture-review/`; values are recorded above.
- No runtime implementation changed. No new manual driving/audio review, Release workload
  benchmark, GPU execution timing, Instruments capture, Windows execution, or user acceptance
  is claimed. Scaling findings are source-based risks until measured under representative load.
- Concurrent edits to the handling validator and related physics/Windows documentation appeared
  during this review and were left untouched. The suite result describes the binaries built for
  this review; it is not verification of subsequent edits made by another task.
