# Lighting through the day

Status, 2026-11-07: the shared frame and user-approved sunset/dusk presets (#138) and the
remembered fixed menu selector are on `main`. Gameplay stays daytime by default.
SETTINGS → LIGHTING remembers DAY, SUNSET or DUSK for the local player's new solo and
multiplayer sessions; explicit CLI launches use `--daylight` independently. The revised
direction is to represent any hour of an authored 24-hour day, beginning with fixed-time
evaluation and review. A later sandbox-specific step now targets a 15-real-minute
day/night cycle (900 seconds = 24 game hours). General Auto controls, room-wide time and
plugin schedules remain deferred. T1 adds the fixed-hour evaluator, artistic anchors and
a developer review input, approved by the user on 2026-11-07. The named presets and player
settings are unchanged. Next is T2a, the GPU sky upload path; cloud/reflection attenuation
is a documented deferred issue.

The proposed [plugin location-life track, L5/L6](PLUGIN_PLAN.md#location-life-track-proposed-2026-10-30)
will expose approved fixed lighting choices, then separately scoped transitions and
building/street lighting schedules after the native prerequisites here are implemented
and reviewed. No lighting capability exists in the current Lua API. This plan continues
to own the shared lighting frame, renderer/cache agreement and visual acceptance.

## Intended result

Make the basin and Gothenburg Harbour playable at any selected hour: night, dawn,
morning, day, late afternoon, sunset and blue dusk. Sky, sun direction, ambient/direct
light, distant haze, water/reflections, grading and occupied windows should agree about
the selected time. Keep navigation, shorelines and boats legible at 320×200. Preserve the
integer-scaled aesthetic, calm glitter and storm readability. The day is an artistic
lighting recipe with a coherent sun path; it need not match Gothenburg's latitude,
season or any particular date. Real-world astronomy and a running clock are unnecessary.

A fixed hour is a complete useful result: selecting or seeking a time must work without
waiting through a cycle. Smooth evaluation between hours supports later transitions
and remains the foundation for the sandbox's separately planned 15-minute cycle. Other
modes keep their fixed lighting choices and Day default.

## Evening foundation and original qualification (2026-11-03–06)

1. **Shared lighting frame (implemented 2026-11-03).** Introduce a small caller-owned presentation value
   for sun direction, direct/ambient color and strength, sky/horizon and haze colors,
   and window-light blend. Evaluate it from an explicit normalized daylight phase,
   never the wall clock. Keep daytime as the default and preserve its existing output.
   Trace consumers through `waterfront_scene`, CPU `renderer3d`/`waterfront` and GPU
   `renderer_gpu`, including river/outer scenery and water. Sky panorama caching must
   follow the supplied lighting value rather than the current fixed ready-once sky.
2. **Fixed sunset and dusk (implemented and approved by the user 2026-11-03).** Feed the same lighting frame into both renderers and their
   water/reflection paths. Add warm low-angle sunlight and a restrained horizon glow;
   keep blue dusk's sky/cloud contrast and warm occupied windows. Do not fake gameplay
   sunset with the title's screen-space palette or authored window coordinates.
   Review fixed river/bridge, basin and outer-coast views before exposing a setting.
3. **Controls (fixed selector implemented 2026-11-06); transitions deferred.** The
   remembered selector is a local presentation preference in solo and rooms. Automatic
   transitions are not part of this step. If separately requested, interpolate approved
   presets with an explicit presentation time/phase input and decide the title's phase.
   Keep visual time separate from 120 Hz physics,
   water simulation time and course clocks; scripted captures must be repeatable.
   Define replay phase persistence and room-wide phase ownership before enabling
   automatic changes there. A room setting may need protocol/content-version work;
   this plan does not silently introduce one or infer host authority from local clocks.
4. **Original qualification scope (partly open; evidence below).** Check CPU/GPU sky,
   water, scenery, material and fog agreement; inspect captures at afternoon/sunset/dusk
   and transition midpoints. Run full CTest,
   meaningful lighting/cache checks and a windowed transition demo. Measure frame cost,
   review storm/course/weapon visibility, and qualify browser/native packaged behavior.
   Physical playback and user acceptance remain separate gates.

## Review and direction, 2026-11-07

**Baseline finding, before T1:** the shared `WaterfrontLighting` is the right consumer
boundary, but its original input is a clamped evening blend, not a clock. `waterfront_lighting(phase)` maps
0 → day, 0.5 → sunset and 1 → dusk; all three use the same sun bearing. Extending this
scalar past 1 or reversing dusk back into day would not give a coherent full day. Dawn
needs its own sky and opposite-side low sun, and night needs a readable recipe beyond
the approved dusk. The shader/cache and presentation consumers must follow that model.

### Fixed-time contract

- Evaluate an explicit presentation hour in [0, 24), wrapping 24 to 0. Document finite
  input normalization and invalid-input handling. A new full-day input must remain
  distinct from the legacy evening phase; do not reinterpret `--daylight 0.5` as noon
  or change the meaning of saved DAY/SUNSET/DUSK names. Keep the existing zero-initialized
  day default and exact approved recipes available through a compatibility path.
- Use a small native table of artistic lighting anchors and a coherent sun path. Give
  dawn/sunrise, morning, day, late afternoon, sunset, dusk and night distinct treatment;
  interpolate every hour, including the night-to-dawn and midnight seams. Choose and
  document anchor hours during implementation; they are art decisions, not solar claims.
  Keep the approved day, sunset and dusk recipes as regression references. Any intentional
  change to those named looks, including sun bearing, needs explicit visual review.
- Keep sun elevation and bearing continuous, with no zero-length direction or noon
  singularity in sky/shadow calculations. Sunrise and sunset must occur on opposite
  sides of the scene. Gate direct sunlight, disc, glitter and directional shadows around
  the horizon together; no direct sunlight at night. A readable ambient night and harbour
  lights are sufficient initially; moon phases, stars and a second shadow-casting light
  are optional art work, not prerequisites.
- Produce the same caller-owned lighting frame for sky, CPU/GPU materials, water,
  reflections, haze, windows/lamps and HDR grading. Audit `WaterfrontLighting.phase`,
  `waterfront_grade`, title grading and existing dusk-dependent effects so an hour never
  accidentally passes through the old clamped evening evaluator. Keep window occupancy
  stable; vary emission/intensity without re-rolling occupied rooms on each selection.
- Keep visual time separate from physics, wave time, course clocks and weather. A fixed
  hour stays fixed while boats and waves move. Evaluation and direct/backward seeks must
  not depend on previously rendered hours, local wall time or accumulated frame deltas.
  If later transitions vary sparkle speed, derive its phase from an explicit timeline
  (or a deterministic integral), rather than multiplying wave time by the current speed.
- Preserve the current local presentation preference in solo and multiplayer. A fixed-hour
  renderer does not imply shared room time, a protocol change, replay persistence, Lua
  access or an automatic schedule. Define ownership and persistence before adding those.

### Implementation slices

| Step | Scope | Completion evidence |
| --- | --- | --- |
| T1 — full-day evaluator and fixed-hour review input (implemented locally) | Explicit hour contract, artistic anchors and sun path behind a developer review input. Legacy preset/CLI/save behavior and default Day remain. One shared frame, no Auto. | Evaluator, CLI and capture checks passed; T1 preview approved by the user on 2026-11-07. Detailed evidence is in the [T1 result](#t1-implementation-2026-11-07). |
| T2 — renderer agreement and readable night/morning | Refine the new looks and wire every consumer, including grading, materials, lamps, water and reflections. Resolve the CPU basin's independent light/no-sky path and textured-box lighting gap, or document an explicitly reviewed fallback. Bound sky repaint/upload work before exposing continuous scrubbing; the present ~25 ms Debug repaint cannot happen every changing frame. | Inspect matched CPU/GPU basin, outer harbour/bridge/quay and outer-coast views at anchors, all integer hours and difficult midpoints. Review seam/horizon/azimuth boundaries, emissive vs illuminated surfaces, storm, course/weapon visibility and glitter in motion. Record measured frame/cache cost. |
| T3 — fixed-hour player controls and qualification | Expose the reviewed hours through the local LIGHTING setting, keeping the existing named choices and Day default. Choose a simple hour control/names after the review; persist a versioned choice without repurposing the old numeric phase. Add documented CLI/review examples when implemented. | Verify native/browser save/load, invalid and old settings, explicit CLI precedence, new solo/join/host sessions and return to title. Run build/full CTest, inspect representative captures and a windowed manual demo; qualify platform/package limits. Fixed-selector and new-look user acceptance remain separate. |
| T4 — sandbox 15-minute day/night cycle | After T1–T3 and their rendering/performance gates, drive the free ride sandbox's shared lighting from a session-owned presentation timeline: 900 active real seconds = 24 game hours. Wire the custom workboat/crane/cargo view into that frame. See the clock and upload contracts below and the [sandbox owner](FREE_RIDE_SANDBOX_PLAN.md#sandbox-daynight-cycle-t4). | Virtual-clock tests prove rate, pause/resume, seeks and repeated midnight wrap; matched CPU/GPU captures and a windowed cycle review prove smooth light and readable crane/cargo work at night. Measure sky work/upload and frame cost through a whole cycle. Planned, not implemented or visually accepted. |
| Later — general Auto and script/room ownership | General Auto controls, title phase, replay persistence and room authority remain separate decisions. Coordinate plugin L5/L6 bounds and versioning before adding scripts or shared time. | Room/replay/Apply/Undo and synchronization evidence where those features are introduced. These remain deferred and are not dependencies of the solo sandbox cycle. |

T1 is implemented and user-approved. Split T2 into reviewable steps:

- **T2a — GPU sky upload path (next).** Upload static cloud structure once per GPU scene,
  shade changing sky light with uniforms, preserve accepted appearance and the existing
  CPU panorama as the reference. Prove zero steady-state sky texture writes/allocations.
- **T2b — CPU and material agreement.** Move changing-hour CPU sky work to visible samples
  and resolve the basin/material gaps. Keep actor, water and grading inputs coherent.
- **T2c — qualification.** Review readability and intermediate hours, native/browser
  behavior and measured CPU/GPU/frame cost before T3/T4.

The cloud/reflection mismatch below is deferred separately; T2a must preserve its current
behavior rather than silently bundling an artistic fix into the upload migration.

### Sky rendering and GPU uploads (T2 prerequisite for T4)

The current `draw_scene()` in `src/renderer_gpu.c` calls `waterfront_sky_image()` and
`UpdateTexture()` whenever `waterfront_lighting_same_sky()` detects a change. That is a
1024×512 RGBA8 image: **2 MiB per update**, about **120 MiB/s at 60 updates/s**, before
driver overhead. The recorded ~25 ms Debug cost is CPU panorama painting, not a measured
GPU transfer cost. A slowly moving clock would still invalidate this exact-value cache
on every frame; a 15-minute cycle alone does not solve the problem.

**Planned strategy:** separate fixed sky structure from changing light. Generate the
existing cloud underside/rim mask once, retain it in caller-owned scene resources and
upload a point-sampled texture once at scene creation or GPU context recreation. Extend
`assets/shaders/waterfront_sky.fs` to compute the gradient, horizon glow, sun disc/halo,
cloud colours and fixed grain from the shared frame's small set of uniforms. Preserve
panorama texel-centre sampling/quantization so the new path retains the pixel aesthetic.
The sun is evaluated at its current direction, avoiding doubled suns from cross-fading
complete preset panoramas. Main, reflection and mirror passes use the same frame values;
camera and squall inputs retain their existing pass-specific behavior.

Time-of-day changes must cause **zero sky texture uploads or texture allocations** in
steady state; only lighting uniforms change. This applies to sky uploads, not the
existing light-list/wake textures used elsewhere. Keep the CPU sky as a reference and
move continuous CPU rendering to the same mask/recipe evaluated at visible sky samples,
avoiding a full panorama repaint for every new hour. Preserve a fixed-preset comparison
path while qualifying the migration; do not introduce a second independently tuned sky.

Qualification records texture-write/allocation counts, CPU sky preparation/upload time,
GPU sky pass time and whole-frame p50/p95 against fixed-hour baselines at low/high
resolution, including native and browser contexts. Test a full virtual cycle, direct
seeks, two scene instances and context recreation, plus seam/horizon views and approved
presets. Measure steady state separately from first-use costs. GPU fragment work may
increase, so compare with the graphics plan's existing budgets before enabling T4;
this upload strategy is planned, not yet benchmarked.

### Deferred: clouded sun and water reflection

On 2026-11-07 the user observed that clouds can obscure the visible sun while the water
still shows a strong, apparently unobstructed road of sunlight. They asked to document
it and explicitly defer the fix. It may predate T1; its first introduction was not
investigated. The supplied screenshot is retained as evidence:

![User capture: cloud-obscured sun with a strong water reflection](evidence/lighting/2026-11-07/clouded-sun-reflection.png)

A later change should make cloud visibility and the sun's water highlight/glitter agree,
with restrained, stable attenuation as clouds cover the sun. Define the exact artistic
recipe then, and review CPU/GPU, direct seeks, camera motion and clear/partially covered/
fully covered cases. This is not a request for a general weather or astronomy simulation.
**Status: documented, not scheduled or implemented.** Keep it outside T2a, and retain the
mismatch in its regression references so a deliberate later correction remains reviewable.

### Sandbox clock contract (T4)

The requested initial tuning is **15 real minutes per full day**, equivalent to one
game hour every **37.5 real seconds** (96× game time). Use
`hour = wrap24(start_hour + active_elapsed_seconds * 24 / 900)`, with an explicit input
timeline so captures and tests can seek directly without waiting 15 minutes. T4 starts a
new sandbox at T1's 12:00 Day anchor after its visual review.

Advance during active play, including crane mode. Proposed solo behavior: pause freezes
the presentation clock with the world; suspended/background time causes no catch-up jump
on resume. Keep this clock separate from physics/wave/course rates: it changes light,
not boat or crane speed. Resetting a load or recovering the boat retains the hour;
a new session restarts it. When sandbox saves arrive, store the current hour and cycle
rate in versioned session state and resume without offline progression. Shared multiplayer
time belongs to the later room-authority contract. Keep a fixed-hour developer override
for comparisons, and review night-time handling before enabling the cycle by default
in the sandbox. The 900-second target is initial tuning, not a measured fun result.

### Acceptance and limits

The user should recognize morning, day, sunset, dusk and night from the scene and water,
and be able to select intermediate hours without discontinuities. Night retains readable
shorelines, boats and navigation; it must not simply darken dusk until play becomes hard.
Window lights belong to buildings and remain stable as the camera moves. The title should
eventually use an approved shared recipe, avoiding a second independent palette; its
phase is a separate decision. Dates/astronomy, seasonal daylight lengths, weather changes
and new gameplay visibility rules remain outside this work. T4 adds cycling only to the
sandbox; general Auto remains deferred.

## Current status

### T1 implementation, 2026-11-07

**Completed:** `waterfront_lighting_hour()` evaluates a fixed artistic day with anchors at
0 night, 4.5 dawn, 6 sunrise, 9 morning, 12 day, 15 afternoon, 18 sunset, 19.5 dusk and
21.5 night; hour 24 repeats night at the seam. Elevation and azimuth are interpolated as
angles, sunrise/sunset use opposite bearings, and the maximum elevation stays below the
zenith. Direct light, disc, glitter, highlight and cast water shadow share a continuous
horizon gate, so all are zero at or below the horizon. Night and dawn have their own recipes.
Day, sunset and dusk reuse the approved palettes at their anchor hours with new bearings;
the user approved the T1 preview on 2026-11-07 (see acceptance below). The legacy evaluator,
`--daylight`, named saves and the zero-initialized Day default are unchanged.

Finite hours wrap into `[0,24)`; negative values come back from the other side of
midnight. Signed zero, and any wrapped result that rounds to 24, are canonicalized to 0.
Non-finite input and bad strings fail explicitly and preserve outputs.
`waterfront_grade_hour()` supplies a neutral new-hour grade without calling the legacy
evening evaluator with an hour. `BoatGpu`/`BoatCpuView` carry a zero-safe `hour`/`hour_set`
override, and `--lighting-hour H` is available in `wake_velocity` and `boat_water_review`;
it overrides `--daylight` independent of order. The game's `--time` remains drive/wave
time; review tools keep their explicit pose ticks and clip frames. Review metadata
reports the mode, legacy daylight value and hour separately.

**Automated verification (2026-11-07, Apple Silicon/macOS, Debug):** the final incremental
build passed without compiler warnings. `ctest --preset full` passed all 387 enabled tests
with local sockets enabled (`PROTEUS_GPU_TESTS=OFF`, 13 retired checks disabled). The
focused lighting/grade/CLI selection passed 8 of 8 and the quick preset passed 268 enabled
tests. Coverage includes legacy fixtures, invalid/wrapped inputs with output preservation,
all integer hours, a dense sweep with separate field scales, positive near-horizon
attenuation, midnight/horizon boundaries, unit sun directions, neutral grades and
direct/backward/reordered seeks. CLI precedence is compared against an hour-only reference.
Review caught and corrected a degrees/radians error in the five-degree fade; the new
near-horizon checks reject the original error.

**Agent visual and integration checks:** six legacy CPU captures (basin/outer at
day/sunset/dusk) and three outer GPU captures remained byte-identical to `c7ab6d8`.
Main-CLI checks
passed `0=24`, `-1=23`, both hour-over-dusk orders, title-injected preset override and
invalid/missing hours. After the fade correction, CPU/GPU captures at 0, 4.5, 6, 6.1, 6.5,
9, 12, 15, 18, 19.5 and 21.5 hours were inspected. Both review-tool runs produced 31
captures across 13 pose groups at hour 6.5 with correct mode/hour metadata; basin, bridge,
coast and dock samples were inspected. A high/HDR night capture and a 180-frame windowed
GPU HDR night demo (music Off, exit 0, final frame inspected) passed. Local artifacts are
in ignored `build/t1-review/`; the saved pre-change references are in `build/t1-baseline/`.
Changed links, command examples and `git diff --check` were checked before handoff.

**Limits:** T1 preview acceptance does not qualify every hour, view or platform. Browser/Windows and the GPU CTest
matrix were not run. CPU basin sky/light, textured-box lighting, night shadow-map cost
and storm/course/weapon readability remain T2 work; no continuous-clock performance is
claimed. An initial GLFW monitor-enumeration failure also affected the unchanged binary;
macOS/GLFW display probes and subsequent baseline/new GPU runs succeeded without a source
workaround. Its exact cause was not established.

**User acceptance (2026-11-07):** after trying the fixed-hour preview, the user reported
“Looks nice! Approved” and asked to continue the OpenCode/DeepSeek implementation-and-review
workflow. Their screenshot records the cloud/reflection issue above. Exact hours, settings
and platform coverage were not stated; this is T1 preview acceptance, not blanket
qualification of every hour, T2 performance, player controls or the future cycle.

**Next task:** T2a, the static cloud mask and uniform GPU sky upload path. T2b/T2c retain
CPU/material agreement, readability and broader qualification. T3 menu controls, T4 cycle, Lua access, room
time and replay persistence remain planned.

### Plan review, 2026-11-07

**Completed:** reviewed the current evaluator, sun path, scene/sky cache, renderer and
grading consumers and the native/plugin plan boundaries. Expanded the goal from evening
presets to any fixed hour of an artistic day, with T1–T3 above. Added the requested T4
sandbox cycle (15 real minutes = 24 game hours) and a concrete static-mask/uniform sky
upload strategy; general Auto remains deferred. Existing fixed recipes retain their
approval; new hours, controls and transitions have no visual
or user acceptance yet.

**Verification (2026-11-07, Apple Silicon/macOS):** native Debug configure/build passed
using the existing pinned dependency sources, with one upstream `stb_vorbis.c` warning.
`ctest --preset full` ran 381 enabled tests (`PROTEUS_GPU_TESTS=OFF`, 13 retired checks
disabled): 369 passed in the sandbox; all 12 socket-dependent failures passed with local
socket access via `ctest --preset full --rerun-failed`. The initial review's 22 changed/owning-plan
local link references and `git diff --check` passed. This is documentation-only work; no new
captures, playback, GPU/browser/Windows checks or audiovisual acceptance were performed.

**Cycle/upload follow-up verification (2026-11-07, same platform/options):** incremental
Debug build and `ctest --preset full` with local sockets passed all 381 enabled tests.
All 48 changed/owning-plan local link references and `git diff --check` passed. The
sandbox cycle and upload strategy are documentation only; neither has runtime or
performance evidence yet.

**Next task at the plan-review baseline:** T1, now implemented above. The current menu still
needs the player's playback review. The historical verification and limits below apply to
their dated changes, not to the T1 full-day work.

### Menu setting, 2026-11-06

**Completed:** SETTINGS → LIGHTING selects DAY, SUNSET or DUSK with Left/Right, Enter
or a click. `BoatTitleSettings.lighting` persists named presets in native `title.txt`
and the existing browser save mount. Missing or invalid names leave the default Day.
New solo, joined and hosted sessions receive the choice through `--daylight`; each player
owns their local view. Settings rows use 14-pixel spacing to retain room for all seven
rows, hints and footer. The title backdrop and the three approved lighting recipes are
unchanged. The user requested this selector and expressed uncertainty about whether Auto
would be fun; Auto, astronomy, transitions and new presets are not scheduled by this change.

**Verification (2026-11-06, Apple Silicon/macOS):** native Debug and Emscripten MinSizeRel
builds passed. `ctest --preset full` passed all 370 enabled tests with
`PROTEUS_GPU_TESTS=OFF` (13 retired checks disabled); local socket access was required for
the network tests. A focused Chrome/SwiftShader check passed the initial Day default,
Left/Right wrap, Enter and pointer activation, IndexedDB saves and reload persistence,
and three solo session/return cycles. Captured GPU sun-direction uniforms agreed with
Day, Sunset and Dusk. Settings and all three browser gameplay captures were inspected.
A native windowed GPU sunset demo (`--location outer --daylight sunset --demo --frames 120`)
and a CPU dusk capture through `--title-start outer-standard --daylight dusk` passed;
their captures were inspected. Local evidence is in ignored `build/lighting-review/`.
Changed documentation links and `git diff --check` passed. These are agent checks, not
human acceptance. Windows, Safari, physical input/audio, native title save/reload and
non-default lighting in a joined/hosted room were not exercised in this step.

**Next task at the 2026-11-06 baseline (superseded by the plan review above):** review the
fixed selector in play. Any later transition or room-wide lighting ownership needs a
separate decision. This change does not add in-session lighting
controls or change replay persistence; the existing renderer/readability limits below remain.

### Fixed preset result, 2026-11-03

**Completed (steps 1–2):** `src/waterfront_lighting.[ch]` evaluates one
caller-owned `WaterfrontLighting` from an explicit phase (0 day, 0.5 sunset, 1 dusk, linear
between them). `WaterfrontSceneFrame.lighting` carries it to the GPU sky, city lighting, fog,
shadow direction, water and reflections and the CPU sky, flat shading, displaced water and
self-coloured actors; windows share an occupancy rule and light up as `window_light` rises.
The sky panorama repaints when the light's sky changes, and each GPU scene re-uploads its
texture. `BoatGpu.daylight`/`BoatCpuView.daylight` choose the phase; `wake_velocity --daylight`
and `boat_water_review --review DIR --daylight` exposed the three presets for review at this step;
the menu setting above now exposes them to players.
Design and API notes are in [3D.md](3D.md#scene-lighting).

- Sunset: sun 7° above the horizon on the daytime bearing; warm direct light, cool
  violet fill, an orange horizon with a glow and halo under the sun, warm cloud undersides,
  orange haze, blue-violet water with a warm, dashed road of light toward the sun and a few
  slow sparkles, long shadows and a few warm windows.
- Dusk: sun 5° below the horizon, so no direct light, glitter or water shadow; deep blue
  scenery after the title grade (land darker than water), a lavender horizon with a fading
  pink afterglow, dark cloud deck and warm occupied windows (about three in eight).

**Verification (2026-11-03, M1 Max, macOS, Debug with `PROTEUS_GPU_TESTS=ON`):**

- Daytime parity (before the calmer glitter below): all 29 `boat_water_review --review` poses in each renderer and
  `wake_velocity --screenshot ... --time 3` for basin, river and outer in CPU and GPU are
  byte-identical to `main` at `90b3e92`.
- `waterfront_lighting` (quick lane): exact day constants and preset returns, no step larger
  than its share of a preset difference over a 2048-step sweep, sky repaint and exact
  restore, occupancy near 3/8 in every glass colour, parsing, and renderer defaults.
- Agent-inspected captures: `--review --daylight sunset|dusk` in both renderers (river
  bridge/skyline/quay, outer coast/dock, basin hull) and outer `wake_velocity` screenshots;
  windowed 600-frame outer `--demo` runs at sunset and dusk exited cleanly. These are agent
  checks, not user acceptance.
- `ctest --preset full`: 385 of 385 enabled tests passed (GPU tests included).

**Calmer glitter (2026-11-03, after the user's first look):** the user found the sunset
reflection "basically static noise" without `--gpu-aa` and "far from soothing" with it, and
then allowed calmer daytime glitter too. Sunset/dusk now draw the road of light described in
[3D.md](3D.md#place-aware-water) with 30% of the sparkles at 40% speed; daytime keeps 55% at
55% speed. Measured with `boat_water_review --place-clip DIR 120 calm --clip-aa off|4x4
--daylight ...` on rows 38–110 (the glitter band) against the clip's no-far-water control (mean
frame-to-frame change above the control; share of pixels changing by more than 32): sunset
without AA 3.32 → 0.50 and 6.6% → 1.9%; with AA 1.42 → 0.34; day without AA 0.77 → 0.44 and
2.7% → 2.1%. Daytime captures are therefore no longer byte-identical in GPU glitter areas
(16 river/outer review poses); CPU captures and the basin still are. Agent-inspected clips
only; the change needs the user's play in motion.

**User acceptance (2026-11-03):** the user approved the fixed sunset and dusk presets
("I'm approving dusk and sunset"), after the calmer glitter that #138 merged. Which locations and platform they played was not stated.
This approves the fixed presets only, not a transition, setting or default.

**Known limitations at the 2026-11-03 baseline:** storm/squall readability, course and
weapon visibility at sunset/dusk not reviewed; the squall's own cloud and rain colours and
the overcast overlay are unchanged; the CPU basin has no sky and keeps its own light
direction; the CPU textured-box material path is unlit by the frame; the shadow maps still
render at dusk; a panorama repaint costs about 25 ms (Debug), which a transition must avoid
paying every frame; no frame-cost measurement, browser or Windows check yet; intermediate
phases are untested visually. The title keeps its own grade.

**Next task at the 2026-11-03 baseline (superseded by the menu setting above):** step 3
(transition, controls and ownership), which needed the user's
decisions on names, defaults and the title phase; don't start it unasked. The graphics
overhaul's G5b builds its dusk lamplight, streaks and harbour lamps on the frame's window
light (machine-verified 2026-11-03 on `feat/graphics-presentation`; see
[the plan](GRAPHICS_OVERHAUL_PLAN.md#g5b--clustered-cozy-lighting-and-dusk-water)).
