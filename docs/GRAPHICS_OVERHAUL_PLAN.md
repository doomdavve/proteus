# Graphics overhaul plan

Status: proposed 2026-10-31. No stage has started and nothing here is implemented. The user asked
for this plan after a review of the GPU drawing path, to iterate on later. Their first reaction is
under [decisions](#decisions): the order is "probably right" and the new water layer "sounds
exciting". That is a steer, not acceptance of any result, and targets and identity rules are still
a draft to argue with.
**Next task: G0.**

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

1. **Chunkiness lives in the world, not the framebuffer.** Water, foam and reflection offsets may
   be shaded in fixed-size squares anchored to the water surface. They stay crisp and stable at any
   resolution. 320×200 stays available as a preset.
2. **Water comes first.** It gets the largest share of new frame budget and is the first thing
   reviewed in any capture.
3. **Toy-like shapes stay.** Keep flat facets, chunky cube foam and spray, and procedural finishes
   rather than photographic textures.
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
build is ES 3.0 (WebGL2), so a fully GPU-driven pipeline needs a new backend; see [G6](#g6--backend-decision).
Everything before G6 is designed to work on both. Watch points for that work:

- `DrawMeshInstanced` allocates and uploads a new instance buffer on each call and carries only a
  transform, so per-instance colour needs a persistent buffer through `rlgl`
  (`rlSetVertexAttributeDivisor`).
- `rlgl` has no uniform buffer helper; those would need direct GL calls.
- Rendering to float textures in WebGL2 needs `EXT_color_buffer_float`. Plan an 8-bit fallback.
- Shaders are `#version 330` and converted for the web in `cmake/Web.cmake`, so new shader features
  must survive that step.

## Stages

Targets are provisional until G0 sets real budgets. Each stage's gain figures are estimates.

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

Done when: captures are byte-identical at the review stops, `tile_culling` still passes, and
submissions fall below about 150 with submission p95 near 1 ms (estimate, to be revised by G0). The
`app_submissions` definition in the performance guide is updated, since one draw is no longer one
object.

### G2 — Geometry

1. Index and interleave vertices and shrink the format; release the CPU-side copies where no
   consumer needs them. Flag cloth still updates its arrays.
2. Add distance detail: merged or simplified distant tiles and impostors for far buildings, to pull
   down p95 triangles. This one is allowed a visual change.
3. Cache static shadow casters where the sun is fixed, and reassess once the sunset work moves it.

Done when: map memory and p95 triangles have measured reductions, with no popping visible on the
review route.

### G3 — Living water

The largest gain for both speed and identity. Add a boat-following, world-anchored texture of
height, foam and ripples.

1. **Stamp wakes.** Draw wake pieces as small additive quads into the texture, replacing the
   per-pixel `objectWake` loop. Visually equivalent to today.
2. **Foam memory.** Foam lingers, drifts and fades instead of following only the current crest.
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
   This generalizes the hand-written `exposure()`. Decide whether it joins the network content
   digest; presentation-only data should not break room compatibility.
2. Make scene depth available under the water, for shore fade, depth tint and quay foam.
3. Far-water life: wind gusts crossing the surface, a chunky sun glitter path, squalls visible on
   the water as they approach.

Done when: glassy docks and choppy open water read clearly apart in captures, on both `river` and
`outer`, and the user has reviewed them.

### G5 — Resolution, light and mood

1. **High-resolution preset.** Add world-anchored water squares (identity rule 1) and compare with
   320×200 side by side, as an option: low resolution stays the default. Changing the default needs
   explicit user acceptance later; only then update the nearest-neighbour convention in `AGENTS.md`,
   along with HUD scaling and the guides.
2. **HDR and grading.** Unclamp lighting, add restrained bloom and authored grading per time of day.
3. **Clustered lights.** Replace the 32-light loop with a CPU-built cluster texture, removing the
   lamp selection and its cap.
4. **Dusk on the water.** Hundreds of windows and lamps as long stepped streaks in the reflection.
   Depends on the [gameplay sunset](SUNSET_PLAN.md) shared lighting frame.
5. **Finishing.** Baked ambient occlusion in the scenery pack; GPU particles for foam and spray
   within what the platform allows.

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

For the implementer, decided here unless reversed:

- **CPU renderer:** stays a geometry and capture reference. New effects are GPU-only, and it is not
  expected to match them.
- **Browser parity:** every stage before G6 must also run in the browser build, or state its
  fallback.

## Open decisions

1. **Look spike first?** The order starts with work that pays off regardless of the look, and
   compares the look in G5. A throwaway water-square spike during G0 would test rule 1 earlier, at a
   small cost. Current choice: keep the order.
2. **Browser floor:** the hardware floor above was stated for machines generally. Whether browsers
   must hold 60 Hz on the same machine, or may fall back to fewer effects, is not decided. Current
   choice: fallbacks are allowed, and G0's browser measurement informs this.

## Related work and conflicts

- **[#103](https://github.com/doomdavve/proteus/pull/103), open at the time of writing,** adds a
  second distant shadow map. Its own Debug comparison moves submissions from 1,006 to 1,249 and CPU
  submission from 3.756 to 4.334 ms, which raises the case for G1. G0 should measure after it
  settles, and G2's shadow caching must account for it.
- **[Gameplay sunset](SUNSET_PLAN.md)** owns the shared lighting frame. G5 builds on it rather than
  adding another.
- **[Living city](LIVING_CITY_PLAN.md) and [plugin location life](PLUGIN_PLAN.md#location-life-track-proposed-2026-10-30)**
  add more actors, which G1 should make cheap.
- **[Boating performance](BOATING_PERFORMANCE.md)** owns the benchmark contract, and
  [the 3D guide](3D.md) the renderer description. Update them as stages land.

## Verification policy

- Every stage: the build and `ctest --preset full`, with `PROTEUS_GPU_TESTS=ON` for renderer changes.
- Engine-only stages (G1, and G2 parts 1 and 3): byte-identical captures at the review stops.
- Visual stages: inspected captures, a windowed demo, and user review. A passing run is not user
  acceptance; record them separately.
- Performance claims: Release, repeated runs, stating machine, options and commit.
- Tests never wait on wall-clock time, per the `test_time` rule.

## Log

- 2026-10-31: plan written from the GPU path review. Code reading, the two benchmark runs above and
  three inspected GPU captures (river at 320×200 and `--gpu-aa`, basin at `--gpu-aa`) on `d3467ff`.
  No renderer change, no user acceptance. Browser coverage for WebGPU and all gain estimates are
  unverified.
- 2026-10-31: recorded the user's first reactions under [decisions](#decisions). Documentation only.
