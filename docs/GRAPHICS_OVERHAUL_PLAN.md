# Graphics overhaul plan

Status, 2026-11-01: **implementation unstarted; package preparation in progress**. The
[execution process](#package-execution-process) defines one implementation PR per package and
three batched human playtests. Prepared briefs include a short kickoff prompt for the next agent.
**Next implementation: G0**, after its planning instructions are approved. Low resolution remains
the default; the [look test](#water-look-test--2026-10-31) rejected water squares and favoured plain
high resolution as an option. Historical measurements and user decisions below remain scoped to
2026-10-31; no overhaul implementation or result acceptance is claimed.

## Intended result

Move the GPU renderer towards massive geometric and lighting complexity while keeping the game's
retro-rooted, modern-effects, cute and cozy feeling. In the user's words, low resolution "isn't
mandatory" for that feeling, the result must not look generic, and the water feeling alive and
dynamic is probably the key. So the water gets first call on the budget, and scale is a means to
that rather than the goal.

Non-goals: photorealism, physically based materials, and matching what a large engine does for its
own sake. Mesh shaders, virtual geometry, ray tracing and temporal anti-aliasing are out of scope:
they are costly here and push towards the generic look this plan exists to avoid.

## Identity rules

Check each visual stage against these. Change a rule by editing it here, with the reason.

1. **Judge it moving.** A look only counts if it holds up in play. World-anchored water squares
   looked fine in stills and "horrible in-game", so they are out. High resolution without them is
   welcome, and 320×200 stays available as a preset.
2. **Water comes first.** It gets the largest share of new frame budget and is the first thing
   reviewed in any capture.
3. **Toy-like shapes stay.** Keep flat facets and procedural finishes rather than photographic
   textures. The blocky foam thrown up by a fast boat is a signature the user loves: keep it, with
   lifetimes that make sense. Those cubes are objects that live and die, unlike the rejected water
   squares, which were a pattern painted over the surface.
4. **Colour is authored.** Keep palettes per time of day and restrained bloom. No filmic grey
   tonemapping, no temporal blur.
5. **Light is for coziness.** Lighting complexity should show up as warm windows and lamp streaks
   on the water, not as realism.

## Baseline — 2026-10-31

Measured at `d3467ff`, before #102 added the reflection-pass clip. M1 Max, macOS, GL `4.1 Metal -
90.5`, **Debug** build, hidden and unpaced, on the river route with waves, one run each:

```sh
./build/boat_water_review --benchmark PREFIX --aa off --waves
./build/boat_water_review --benchmark PREFIX --aa 4x4 --waves
```

| Per frame | 320×200 (`--aa off`) | 1280×800 internal (`--aa 4x4`) |
| --- | --- | --- |
| GPU time p50 / p95 | 2.54 / 2.94 ms | 3.45 / 5.41 ms |
| CPU submission p50 / p95 | 3.62 / 3.98 ms | 3.97 / 4.44 ms |

From the 4×4 run:

| Measure | p50 | p95 |
| --- | --- | --- |
| Scene triangles | 208 k | 750 k |
| Application submissions | 1,011 | 1,040 |
| Object submissions / triangles | 947 / 26 k | 966 / 27 k |
| Map submissions / triangles | 58 / 144 k | 98 / 706 k |
| Shadow / reflection / main submissions | 229 / 256 / 524 | 239 / 269 / 536 |

These are one-off development numbers, not a qualified baseline; G0 replaces them. No weak GPU or
browser was measured. Earlier controlled Release figures are in
[Boating performance](BOATING_PERFORMANCE.md#river-frame-benchmark--2026-10-25).

Findings from reading the code:

- **Draw calls are the ceiling.** Objects make about 28 triangles per draw, at about 4 µs per draw
  all-in, which would fill a 60 Hz frame at roughly 4,000 draws. Scene geometry is not the limit
  on this machine.
- **Objects are not culled** and are redrawn in each pass (`draw_life` in `renderer_gpu.c`).
- **Uniform lookups by name.** `draw_colored_mesh` makes two `GetShaderLocation` calls per draw,
  each a `glGetUniformLocation`; `set_water_uniforms` makes dozens per frame. The cost has not been
  profiled.
- **Vertex data is heavy.** `builder_triangle` writes three unique 36-byte vertices per triangle in
  separate buffers: about 93 MB for the 861 k-triangle river, with the same arrays kept in RAM.
- **No level of detail.** Long views down the river cost about 3.6× the median triangle count.
- **Wakes cost every water pixel.** `objectWake` in `waterfront_water.fs` loops over up to 256
  pieces with two texture fetches each. The benchmark does not exercise four racing boats.
- **Lights are a capped per-pixel loop** of 32, chosen on the CPU (`select_local_lights`).
- **Water ignores what is under it.** No scene depth reaches the water shader, so there is no shore
  fade or depth tint. Ambient displacement stops at the 80 m patch, and foam has no memory.
- **Pixels are comparatively cheap here.** 16× the pixels adds about 0.9 ms at p50 and 2.5 ms at
  p95. A linear estimate puts 1440p near 12 ms p95: fine at 60 Hz, not at 120 Hz. Unverified.

### Platform limits

Compute shaders, storage buffers and multi-draw indirect need GL 4.3. macOS stops at 4.1 and the web
build is ES 3.0 (WebGL2), so a fully GPU-driven pipeline needs a new backend; see
[G6](#g6--backend-decision).
Everything before G6 is designed to work on both. Watch points for that work:

- `DrawMeshInstanced` allocates and uploads a new instance buffer on each call and carries only a
  transform, so per-instance colour needs a persistent buffer through `rlgl`
  (`rlSetVertexAttributeDivisor`).
- `rlgl` has no uniform buffer helper; those would need direct GL calls.
- Rendering to float textures in WebGL2 needs `EXT_color_buffer_float`. Plan an 8-bit fallback.
- Shaders are `#version 330` and converted for the web in `cmake/Web.cmake`, so new shader features
  must survive that step.

## Package execution process

Status, 2026-11-01: this is a **planning delivery**, not a renderer implementation. Eleven
package briefs split G0–G6 into one future implementation PR apiece; G6 remains conditional.
The planning PRs themselves form a documentation stack, each adding one brief and kickoff prompt.
Merge that stack in order before implementation, or branch an implementation from the approved
planning head and target that parent while it remains open. Do not confuse a planning PR with the
implementation PR it prepares. **Next implementation: G0.** No overhaul result is user-accepted.

### Work loop and delivery

1. Read [AGENTS.md](../AGENTS.md), this process, the assigned brief, [testing](TESTING.md) and
   its named owners. Confirm symbols against the source; the entry points below were checked on
   `b0f1233`. Adapt moved code and record the difference rather than creating a parallel subsystem.
2. Check the package's dependencies and G0 budget/tolerance table. Work in a separate worktree,
   on `feat/graphics-<package in lowercase>`, from current main if dependencies have merged. If
   explicitly continuing an unmerged stack, branch from the dependent implementation parent and
   target it, never main. Merge updated parents into open children before review, per AGENTS.md.
   Planning-PR order is an editorial stack; it does not grant readiness to implementation packages.
3. Complete only the assigned package, in the task order below. Use focused commits per coherent
   task so a simpler agent can checkpoint/recover; keep the build and quick lane green. Do not
   start the next package automatically. Ask Astra for advice if profiling contradicts the chosen
   design, a portability decision has no safe fallback, or the conditional G6 trigger is met;
   record its recommendation and the resulting choice. Routine code decisions need no approval.
4. Run the common verification below and the package-specific acceptance checks. Fix regressions
   immediately, even when its human review batch is later. Collect local fixes/docs before opening
   the implementation PR; push the completed branch and open one PR for the whole package. Do not
   merge, release or deploy. Human playtesting may remain pending when that PR is opened.
5. Update this plan's package result and table: current status, completed tasks, next task,
   exact revision/platform/options, automated evidence, agent-run capture/demo evidence, human
   verdict and limits. Keep those categories distinct. Update affected live guides in the same PR;
   search README.md/docs and asset guides for superseded claims. Change docs/README.md only if
   the area-level summary or next milestone changes, not for every package checkpoint.
6. The PR body contains scope, validation, measured deltas, remaining limits, the next kickoff
   prompt and **Needs the user** with the pending batch (or "none yet"). Stop after this package.
   On pause, leave reproducible commands and unfinished items in its result so another agent can
   resume. Do not mark a package accepted based on tests, captures or an agent-run demo.

A dependency means implementation and required machine checks have passed; a named human gate
also needs an actual recorded verdict with blocking feedback resolved. A missing machine/browser
measurement may be reported in a PR, but dependent work needing that evidence stays unready.
If a package would need physics/network changes, a weakened parity gate or an unplanned backend,
finish independent work, document the issue and report it. Do not silently widen scope.

### Common verification and evidence

Every implementation package runs a warning-clean Debug build and full enabled CTest suite:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DPROTEUS_GPU_TESTS=ON
cmake --build build -j
ctest --preset quick
ctest --preset full
```

The first configure fetches pinned dependencies. Existing `build/_deps/*-src` may be copied from
the main checkout into the worktree, then configure with `-DFETCHCONTENT_FULLY_DISCONNECTED=ON`.
Keep build output out of Git. GPU cases require a graphics context and socket cases require local
sockets; report unavailable checks and rerun with the needed environment rather than claiming a
CPU-only pass qualifies the renderer. Tests use virtual time and assertions about behavior; never
make frame timings into wall-clock CTest thresholds.

For each render change, inspect G0 before/after captures and short moving sequences, run an
agent-observed windowed demo, and build/smoke WebGL2 with the documented fallback. The browser smoke
checks shader compilation, resources, effect/fallback activation, resize and teardown; it is not
human browser acceptance. Follow [WEB_BUILD.md](WEB_BUILD.md) for actual commands and current CI
lanes. Use local/offline play; no hosted-room join is needed. Full manual browser/device review is
batched at C, unless an earlier regression makes it necessary. Native/browser timing claims require
real hardware, identified browser/GL renderer, Release and repeated runs. Null GPU timer results stay
null. State exact input sequences when comparing physical `final_state` or `state_hash`.

Use G0's checked-in manifest/commands after it exists. Existing baseline commands are:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DPROTEUS_GPU_TESTS=ON
cmake --build build-release -j
mkdir -p build-release/graphics
./build-release/boat_water_review --benchmark build-release/graphics/waves-aaoff --aa off --waves
./build-release/boat_water_review --benchmark build-release/graphics/waves-aa4x4 --aa 4x4 --waves
```

GPU equality always means the same platform, backend, build, time, view and options. CPU output
stays a geometry/capture reference, not an oracle for new GPU effects. G1 and lossless G2a substeps
need byte parity; quantized geometry and texture-resampled water use **predeclared G0 numerical and
image bounds**, plus motion review. Preserve exact physical inputs/results throughout. Report per-
pass/group CPU/GPU cost and memory cumulatively against G0 as well as the previous package.

### Batched human playtesting

Three planned user sessions minimize repeated setup. Each package still supplies its own agent-run
demo and automated/capture evidence. Save comparison toggles or reproducible prior builds until
its batch is accepted; users must be able to isolate the contributing package. No wall-clock delay
or test pass supplies a missing verdict. A pending user session does not prevent opening its PR.

| Batch | When | Combined route and questions | What it gates |
| --- | --- | --- | --- |
| A — engine and geometry | After G2b | G1/G2a/G2b: river long views, bridge/quay, basin and outer; forward/backward LOD crossings, camera turns, reflected/animated actors and both shadow ranges. Any popping, missing actors, shadow trails or changed facets? | G3a and later visual expansion |
| B — all living water | After G4 | G3a/G3b/G4: river and outer sheltered/open water, four-boat wake overlap, fast turns/stops, quays, rain/gull fixtures, gust/squall approach, scroll/reset and both resolutions. Is water alive/cozy, stable moving, shore-aware, with distinct cube foam? Compare stamp-only, history and place layers. | G5a and later artistic expansion |
| C — final presentation | After G5c | G5a/G5b/G5c: native and hardware browser at low/high presets, HUD/resize, calm/storm/day/dusk, lamps/windows/streaks, AO and cube spray; revisit A/B interactions. Is navigation clear and does the integrated look retain its identity? | Final G0–G5 acceptance; never automatically changes the default |

Batch B deliberately combines G3 and G4 rather than asking for a separate water session between
them: atlas boundaries/depth/shore foam are easier to judge with persistence in place. Keep a
stamp-only comparison to distinguish causes; fix any observed failure before adding more effects.
If that interaction cannot be isolated or feedback would invalidate downstream work, schedule a
focused earlier check and record why. Do not expand a batch to include unrelated gameplay work.

For each batch record: revision/branch, launch commands, settings, route, who played, date, verdict,
actions and unverified platforms. Until that happens write **human review pending**. Keep low
resolution default throughout; any future default switch requires an explicit separate user decision.

### Package register

"Prepared" below describes instructions, not code. All implementation is unstarted. Dependencies
are implementation dependencies; the eleven planning PRs are stacked in this listed order.

| Package | Deliverable | Implementation dependencies | Brief | Code / human review |
| --- | --- | --- | --- | --- |
| [G0](#g0--measurement-and-review-harness) | Measurement and review harness | current main | Prepared | Unstarted; review pending |
| [G1](#g1--persistent-instances-and-pass-culling) | Persistent instances and pass culling | G0 budgets and review manifest | Prepared | Unstarted; review pending |
| G2a | Geometry storage and ownership | G1 | Queued in next planning PRs | Unstarted; review pending |
| G2b | Distance detail and valid shadow reuse | G2a | Queued in next planning PRs | Unstarted; review pending |
| G3a | Stamped wake shading | G2b and Batch A verdict resolved | Queued in next planning PRs | Unstarted; review pending |
| G3b | Foam history and cosmetic ripples | G3a | Queued in next planning PRs | Unstarted; review pending |
| G4 | Shore-aware and far-water character | G3b | Queued in next planning PRs | Unstarted; review pending |
| G5a | Optional high resolution and restrained HDR | G4 and Batch B verdict resolved | Queued in next planning PRs | Unstarted; review pending |
| G5b | Clustered cozy lighting and dusk water | G5a; external sunset shared frame and fixed dusk prerequisites below | Queued in next planning PRs | Unstarted; review pending |
| G5c | Ambient detail, cube spray and final qualification | G5b | Queued in next planning PRs | Unstarted; review pending |
| G6 | Conditional backend decision spike | G1–G5 measured results exceed G0 budgets and profiling identifies a backend-limited fix | Queued in next planning PRs | Unstarted; G6 dormant |

## Implementation package briefs

These briefs refine the stage summaries below. If an older summary uses a provisional gain or
parity claim, the package-specific gate and measured G0 budget govern. No numeric target is
qualified until G0 records it.

### G0 — Measurement and review harness

Stage coverage: G0. **Depends on:** current main.
Human review: No human gate; establishes all three review batches.

**Read first:** `tools/boat_water_review.c`, `tools/validate_boat_water_benchmark.py`,
`src/renderer_gpu.[ch]`, [performance](BOATING_PERFORMANCE.md), [testing](TESTING.md).

**Tasks, in order:**

1. Extend the existing benchmark rather than create a second timing tool. Preserve its default river
   route and report contract. Add a deterministic four-boat stress scenario with saturated
   player/traffic wake records; report emitter and active-piece counts so the load is proven. Keep
   fixture setup in review/presentation code, with unchanged production physics.
2. Add repeatable basin, river and outer review poses: close hull/wake, quay/bridge, long river
   skyline, sheltered dock and open coast. Record map, world position, camera, tick, wave/weather
   options and resolution in a manifest. Include movement through a tile boundary, fast turn,
   stop/restart, camera turn and teleport/map reload. Add capture/replay commands for the manifest;
   do not invent flags in documentation before implementing them.
3. Build Release on a recorded main commit including the merged near/far shadows. Run at least three
   repetitions after the same warmup of flat/waves × AA off/4x4 × ordinary/stress route. Record
   p50/p95/p99, sample availability, frame interval, CPU submission, GPU, pass/group
   submissions/triangles, and CPU/GPU mesh/target memory estimates. Separate unpaced throughput from
   visible paced behavior.
4. Take an Instruments Time Profiler trace for the visible river/stress path. Identify whether
   uniform lookup, draw submission, uploads or shader work is actually limiting. Build and smoke the
   WebGL2 browser on the M1 Max; report browser/version, renderer and unsupported timers. Measure
   equivalent ordinary/stress browser workloads with the same scenario inputs and in-app counters,
   including frame intervals, CPU submission and available GPU timers; add a small browser review
   entry point if needed. Do not substitute SwiftShader for a hardware browser performance claim.
5. Publish the review manifest, command matrix and compact results in this plan/performance guide.
   Replace provisional G1 targets with measured feasible submission and CPU p95 goals; allocate
   CPU/GPU and memory ceilings per pass/group, reserve explicit water headroom and a margin under
   16.67 ms. Set numerical/image tolerances for lossy geometry and stamped shading before those
   packages start. Give every later package a baseline and measurable gate; label missing
   measurements as open instead of manufacturing numbers.

**Acceptance:** Benchmark contract tests cover scenario selection, counts, report schema and
deterministic final physical state. Repeat captures agree on the same platform. Full suite passes;
browser startup/render and trace evidence are recorded. Budget table has actual numbers and units,
target revision/platform and measurement commands; timing thresholds stay outside CTest.

**Limits and stop conditions:** If Instruments or a hardware browser cannot run, deliver the harness
PR with those limits explicit; packages depending on the missing budgets remain unready. The current
Debug table is history, not a substitute. No look/default change.

**Result:** instructions prepared 2026-11-01; implementation unstarted. Automated implementation
checks, agent-run playback and human acceptance: none yet. Next: satisfy dependencies, then execute
this package; stop afterward and report the next package and pending human batch gate.

**Kickoff prompt:**

```text
Implement package G0 in docs/GRAPHICS_OVERHAUL_PLAN.md. Read its execution process and complete the measurement/review harness, repeated Release baseline and budget table. Use one implementation PR, update the plan with actual evidence and limits, and leave human acceptance unclaimed.
Follow the worktree, scope, verification and documentation rules in the plan's execution process.
Stop after this package; report the next package and pending human batch gate.
```

### G1 — Persistent instances and pass culling

Stage coverage: G1. **Depends on:** G0 budgets and review manifest.
Human review: Batch A after G2b; agent checks still run here.

**Read first:** `src/renderer_gpu.[ch]` (`draw_colored_mesh`, `draw_life`, `box_in_view`,
`set_water_uniforms`), `src/waterfront_scene.[ch]`, `assets/shaders/waterfront_city.*`, [3D](3D.md),
[performance](BOATING_PERFORMANCE.md).

**Tasks, in order:**

1. Cache all shader locations when each shader is loaded, invalidate them on reload/unload, and
   retain optional-uniform handling. Remove name lookups from per-draw/per-frame paths.
2. Build a caller-owned instance list from the already evaluated scene frame, grouped by
   mesh/material and pass-relevant state. Cover cubes, gull parts, boat parts, harbour/traffic
   pieces and actors; keep cloth dynamic and preserve transparency/order-sensitive paths. Preserve
   model/normal transforms, per-instance colour, emissive behavior and reflection clipping.
3. Allocate persistent instance VBO/VAO state with rlgl-compatible divisors; grow at a bounded
   capacity, reuse storage and release it before context destruction. Avoid DrawMeshInstanced
   allocation per call. Supply a scalar reference path for validation and a safe initialization-
   failure fallback.
4. Cull transformed conservative bounds separately for main, mirrored reflection and both shadow
   frusta. Include full animation extents. A light behind the main camera can still cast a visible
   shadow; never reuse the main visibility list for other passes.
5. Update submission statistics to count actual instanced submissions, instances and multiplied
   triangle work. Keep historical counter definitions scoped by schema; update validators,
   performance guide and renderer description.

**Acceptance:** On the same build/platform, scalar and instanced/culling paths produce
byte-identical G0 captures in every pass, including cloth and reflected/clipped actors. Existing
tile_culling and GPU lifecycle/view tests pass; add relevant instance-boundary/resource checks. Meet
G0 submission/CPU p95 ceilings with repeated ordinary/stress measurements and no GPU budget
regression.

**Limits and stop conditions:** No visual redesign, physics changes, compute, storage buffers or
direct GL-only dependency that breaks WebGL2. A parity failure is a bug to fix, not a reason to
widen tolerance.

**Result:** instructions prepared 2026-11-01; implementation unstarted. Automated implementation
checks, agent-run playback and human acceptance: none yet. Next: satisfy dependencies, then execute
this package; stop afterward and report the next package and pending human batch gate.

**Kickoff prompt:**

```text
Implement G1 from docs/GRAPHICS_OVERHAUL_PLAN.md after G0 is ready. Complete cached uniforms, persistent coloured instances and per-pass object culling in one PR; prove scalar/culling parity and measured budget gains. Update the plan; prepare Batch A evidence without requesting a separate human playtest.
Follow the worktree, scope, verification and documentation rules in the plan's execution process.
Stop after this package; report the next package and pending human batch gate.
```

## Stages

These are direction summaries, not agent task briefs; execute the packages above. Targets are
provisional until G0 sets real budgets. Each stage's gain figures are estimates.

### G0 — Measure and set budgets

1. Repeat the benchmark matrix on Release at current `main`, and take an Instruments trace to find
   where submission time goes.
2. Add a stress scenario: four boats with a saturated wake pool, which today's route does not cover.
3. Measure the M1 Max as the reference machine, plus a browser on it. Record what the browser
   build can hold, since its floor is [still open](#open-decisions).
4. Pick a fixed set of review stops and cameras for before/after captures.
5. Record per-pass and per-group budgets in this plan, set well inside the M1 Max's frame time
   rather than filling it (see [hardware floor](#decisions)).

Done when: every later stage has a number to beat, including worst-case wakes.

### G1 — Draw submission

No visual change.

1. Cache uniform locations.
2. Add a persistent per-instance buffer (transform, colour, emissive flag) and instance cubes,
   gulls, boats and harbour pieces.
3. Cull objects per pass, using the existing `box_in_view` test.

Done when: same-platform GPU captures are byte-identical at the G0 review stops, `tile_culling`
still passes, and
submissions fall below about 150 with submission p95 near 1 ms (estimate, to be revised by G0). The
`app_submissions` definition in the performance guide is updated, since one draw is no longer one
object.

### G2 — Geometry

1. Index and interleave vertices and shrink the format; release the CPU-side copies where no
   consumer needs them. Flag cloth still updates its arrays.
2. Add distance detail: merged or simplified distant tiles and impostors for far buildings, to pull
   down p95 triangles. This one is allowed a visual change.
3. Evaluate static shadow reuse with valid light, projection and caster keys; a fixed sun alone
   is insufficient because the near/far shadow cameras move. See G2b for measured skip criteria.

Done when: map memory and p95 triangles have measured reductions, with no popping visible on the
review route.

### G3 — Living water

The largest gain for both speed and identity. Add a boat-following, world-anchored texture of
height, foam and ripples.

1. **Stamp wakes.** Draw wake pieces as small additive quads into the texture, replacing the
   fragment `objectWake` loop within G0 shading tolerances. The vertex loop stays analytic in G3a.
2. **Foam memory.** Surface foam lingers, drifts and fades instead of following only the current
   crest. It adds to the boat's cube foam and never replaces it.
3. **Ripples.** Run a small wave simulation in a fragment shader, so that ripples bounce off quays
   and hulls, and gulls and rain leave marks.

Hard constraints:

- **Strictly cosmetic.** Physics and multiplayer keep the analytic waves. No GPU readback, nothing
  in `proteus_boat_core` or `proteus_boat_net`, and `state_hash` unchanged. This does not reopen the
  retired physical wake field in the [wake rewrite plan](WAKE_REWRITE_PLAN.md).
- **Hulls still sit on what you see.** Where the texture drives displacement, it must stay within a
  stated tolerance of the physical surface, with a test. If it cannot, displacement stays analytic
  and the texture drives only shading.
- **Deterministic captures.** The layer takes explicit simulation time as input, never wall clock.

Done when: the four-boat stress case costs less than today's loop on the same machine, the hash is
unchanged, and the user has played it and judged the water.

### G4 — Water with a sense of place

1. Extend `tools/map_pack.py` to bake a per-map water map: shore distance, depth, shelter and chop.
   This generalizes the hand-written `exposure()`. Keep it outside the network content
   digest: presentation-only data must not break room compatibility.
2. Make scene depth available under the water, for shore fade, depth tint and quay foam.
3. Far-water life: wind gusts crossing the surface, a chunky sun glitter path, squalls visible on
   the water as they approach.

Done when: glassy docks and choppy open water read clearly apart in captures, on both `river` and
`outer`, and the user has reviewed them.

### G5 — Resolution, light and mood

1. **High-resolution preset.** Offer plain high resolution as an option, starting from the
   configuration the user liked in the [look test](#water-look-test--2026-10-31): low resolution
   stays the default. Changing the default needs explicit user acceptance later; only then update
   the nearest-neighbour convention in `AGENTS.md`, along with HUD scaling and the guides.
2. **HDR and grading.** Unclamp lighting, add restrained bloom and authored grading per time of day.
3. **Clustered lights.** Replace the 32-light loop with a CPU-built cluster texture, removing the
   lamp selection and its cap.
4. **Dusk on the water.** Hundreds of windows and lamps as long stepped streaks in the reflection.
   Requires the [gameplay sunset](SUNSET_PLAN.md) shared lighting frame and fixed dusk input,
   currently unimplemented; G5b must wait for that prerequisite.
5. **Finishing.** Baked ambient occlusion in the scenery pack; GPU particles for foam and spray
   within what the platform allows, keeping the cubes' look and lifetimes.

Done when: the user accepts the look at the chosen presets on desktop and in a browser.

### G6 — Backend decision

Do not start this by default. Open it only if content from G1–G5 exceeds the G0 budgets and the
Instruments trace shows that compute or indirect drawing would fix it.

If opened, the leading candidate is WebGPU through `webgpu.h` (wgpu-native or Dawn), since one API
covers Metal, Vulkan, D3D12 and browsers. It needs a spike covering: replacing raylib's drawing while
keeping its window, input and audio; rewriting about 1,100 lines of shaders; and current browser
coverage, which this plan has not checked. That spike decides between one backend and keeping
WebGL2 as a fallback. Then: compute culling, indirect draws, GPU particles and simulation.

## Decisions

From the user, 2026-10-31:

- **Order:** "probably right". Keep the stages as they are.
- **Default resolution:** low resolution stays the default "until this comes a lot further". New
  resolutions are options until then.
- **Hardware floor:** "we don't have to go much below a M1 Max". The M1 Max is the reference
  machine, and no stage needs to hold up on much weaker hardware.
- **Not a licence for waste:** "no need to be sloppy due to that". Stages still have to earn their
  cost, with budgets and measured gains, even where the M1 Max could absorb it.
- **Early look test:** run ahead of G0 as "cheap and prudent". After playing it, the user rejected
  water squares and liked plain high resolution "more than I expected". The default stays low
  resolution, as above, since they did not ask to change it.

For the implementer, decided here unless reversed:

- **CPU renderer:** stays a geometry and capture reference. New effects are GPU-only, and it is not
  expected to match them.
- **Browser parity:** every stage before G6 must also run in the browser build, or state its
  fallback.

## Open decisions

1. **Bring the high-resolution option forward?** It was simple to run in the spike and the user
   liked it, so it may not need to wait for G5. It does need real HUD scaling, and it shows off the
   plainer nearby buildings. Current choice: keep it in G5.
2. **Browser floor:** the hardware floor above was stated for machines generally. Whether browsers
   must hold 60 Hz on the same machine, or may fall back to fewer effects, is not decided. Current
   choice: fallbacks are allowed, and G0's browser measurement informs this.

## Water look test — 2026-10-31

A throwaway test of identity rule 1 on the draft PR branch `spike/water-look`, not for merging. It
shades each water pixel from the centre of a world-anchored square, whose size doubles with distance
so that squares stay at least four pixels wide. Captures are river drives at 1280×800.

![Four variants of the river at 3 s](evidence/graphics/water-look-wake.png)

Top: today's 320×200, enlarged; 0.5 m squares. Bottom: high resolution with no squares; 1 m squares.
A [second sheet](evidence/graphics/water-look-waves.png) compares today with 0.5 m squares in waves.

**User verdict after playing it:** squares fail. "It looks horrible in-game. One screen shot look ok
but not over temporal space." The surprise was the configuration without them, which the user "liked
more than I expected", run from that branch's build:

```sh
PROTEUS_SPIKE_SCALE=4 PROTEUS_SPIKE_REFLECTION=4 PROTEUS_SPIKE_SQUARE=0 \
  ./build/wake_velocity --location river --waves
```

That is a 1280×800 scene with a 640×480 nearest-filtered reflection and a 320×200 HUD. Its one Debug
benchmark run below is about the same as today's `--aa 4x4` path, which also renders 1280×800.

Pre-play findings, kept as history. They came from stills and three consecutive clip frames, and the
conclusion that the mosaic survives was wrong once the squares moved:

- **The mosaic survives high resolution.** At 0.5 m the water still reads as chunky retro mosaic,
  while the city, bridge and skyline get sharper. It also matches the sky's existing world-anchored
  cloud squares.
- **0.5 m beats 1 m.** At 1 m, reflections begin to dissolve into patches.
- **No measurable cost.** One Debug `--aa 4x4 --waves` run each on the M1 Max, GPU p50 / p95: today
  3.48 / 3.94 ms; 4× reflection 3.52 / 3.97 ms; with squares 3.46 / 3.93 ms. These differences are
  within noise. `final_state` was identical in all three, as expected of a shading-only change.
- **Defects to fix if adopted.** Thin slivers appear between squares, probably from the
  derivative-based size picking on triangle edges; a distance-based size should remove them. The
  squares align to world axes, so they turn into diamonds and zigzags on diagonal headings, which
  may be charm or noise.
- **Where it runs out.** Nearby buildings look plainer at high resolution, since nothing equivalent
  applies to them. Rule 1 covers only water, and G5 must consider the city too.

## Related work and conflicts

- **[#103](https://github.com/doomdavve/proteus/pull/103), merged before package planning,** adds a
  second distant shadow map. Its own Debug comparison moves submissions from 1,006 to 1,249 and CPU
  submission from 3.756 to 4.334 ms, which raises the case for G1. G0 must measure the merged
implementation, and G2b shadow reuse must account for both maps.
- **[Gameplay sunset](SUNSET_PLAN.md)** owns the shared lighting frame. G5 builds on it rather than
  adding another.
- **[Living city](LIVING_CITY_PLAN.md) and [plugin location
life](PLUGIN_PLAN.md#location-life-track-proposed-2026-10-30)**
  add more actors, which G1 should make cheap.
- **[Boating performance](BOATING_PERFORMANCE.md)** owns the benchmark contract, and
  [the 3D guide](3D.md) the renderer description. Update them as stages land.

## Verification policy

- Every stage: the build and `ctest --preset full`, with `PROTEUS_GPU_TESTS=ON` for renderer changes.
- G1 and lossless geometry/shadow reuse: same-platform byte-identical captures at the review
  stops. Quantized geometry and stamped shading use predeclared G0 error bounds, per the briefs.
- Visual packages: inspected captures and a windowed demo per PR, with human review batched
  at A/B/C in the execution process. Stills cannot accept
  anything that changes over time, as the look test showed. A passing run is not user acceptance;
  record them separately.
- Performance claims: Release, repeated runs, stating machine, options and commit.
- Tests never wait on wall-clock time, per the `test_time` rule.

## Log

- 2026-10-31: plan written from the GPU path review. Code reading, the two benchmark runs above and
  three inspected GPU captures (river at 320×200 and `--gpu-aa`, basin at `--gpu-aa`) on `d3467ff`.
  No renderer change, no user acceptance. Browser coverage for WebGPU and all gain estimates are
  unverified.
- 2026-10-31: recorded the user's first reactions under [decisions](#decisions). Documentation only.
- 2026-10-31: ran the [water look test](#water-look-test--2026-10-31) on `spike/water-look`, at the
  user's request: captures, clip frames, benchmark runs and a 120-frame windowed demo that exited
  cleanly. The suite was not run on the spike.
- 2026-10-31: the user played it. Water squares rejected, plain high resolution liked. Rule 1 and
  G5 rewritten to match. This accepts no shipped feature, since nothing has shipped.

- 2026-11-01: split the direction into eleven agent-sized package briefs and an explicit
  one-package/one-implementation-PR process; planning delivery itself is a documentation stack.
  Astra reviewed boundaries and the shader/geometry/shadow/platform acceptance traps. Human
  playtesting is batched at geometry, combined living water and final presentation. G6 stays dormant;
  sunset prerequisites stay owned by SUNSET_PLAN.md. No renderer implementation, new performance
  result, audiovisual review or user acceptance is claimed. Planning verification is recorded below.

## Planning verification — 2026-11-01

Package entry points, stage coverage, dependencies and current sunset/shadow ownership were checked
against `b0f1233`. Local links and example commands are checked in the planning worktree.
Verification on 2026-11-01, macOS 26.6.2 / AppleClang 21.0.0, unchanged application code at
`b0f1233` in `/private/tmp/proteus-graphics-packages`: Debug configure/build succeeded using copied
pinned dependencies and `-DFETCHCONTENT_FULLY_DISCONNECTED=ON -DPROTEUS_GPU_TESTS=ON`. No project
compiler warnings; bundled raylib's stb_vorbis emitted one pointer-comparison warning. The initial
sandboxed suite hit local-socket restrictions and was stopped during graphics checks. A complete
rerun with desktop/socket access passed **365 enabled tests, including 28 GPU tests**, in 81.63 s;
15 retired wake-field tests remained disabled. This is regression evidence for unchanged code,
not verification of the unimplemented graphics packages. Local Markdown links/anchors, package
coverage, kickoff prompts, balanced fences and `git diff --check` pass for the planning documents;
command options were checked against current sources and the configure/build/full-preset commands
were executed. No browser build, new performance measurement or audiovisual review was performed
for this documentation-only change. Human package/batch acceptance remains pending.
Markdown-only PRs deliberately skip native CI under current workflow path filters; skipped checks
are not passing CI. No audiovisual review is required for this documentation-only stack.
