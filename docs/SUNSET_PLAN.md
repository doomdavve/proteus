# Sunset and dusk in gameplay

Status, 2026-11-06: steps 1 and 2 merged to `main` (#138) and the fixed sunset and dusk
presets approved by the user; the fixed menu setting from step 3 is merged
(#157). The user liked the dusk river in title mock-up B and asked for sunset in the game;
this document owns that work. Gameplay stays daytime by default. SETTINGS → LIGHTING
remembers DAY, SUNSET or DUSK for the local player's new solo and multiplayer sessions.
Explicit CLI launches still use `--daylight` independently. Transitions and a day/night cycle remain deferred;
Auto is not a committed follow-up.

The proposed [plugin location-life track, L5/L6](PLUGIN_PLAN.md#location-life-track-proposed-2026-10-30)
will expose approved daylight/dusk presets, phase transitions and building/street lighting
schedules after the native prerequisites here are implemented and reviewed. No lighting
capability exists in the current Lua API. This plan continues to own the shared lighting
frame, renderer/cache agreement and visual acceptance.

## Intended result

Make the river and outer voyage playable from warm late afternoon through sunset into
blue dusk. Sky, sun direction, ambient/direct light, distant haze, water/reflections and
occupied windows should agree about the time of day. Keep navigation, shorelines and
boats legible at 320×200. Preserve the integer-scaled aesthetic and the existing storm
readability. Start with an explicit fixed sunset/dusk preset; decide the duration and
whether a session transitions automatically after visual review.

## Implementation sequence

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
4. **Qualification.** Check CPU/GPU sky, water, scenery, material and fog agreement;
   inspect captures at afternoon/sunset/dusk and transition midpoints. Run full CTest,
   meaningful lighting/cache checks and a windowed transition demo. Measure frame cost,
   review storm/course/weapon visibility, and qualify browser/native packaged behavior.
   Physical playback and user acceptance remain separate gates.

## Acceptance and limits

The user should be able to recognize sunset from the light on the scene and the water,
then reach dusk without a sudden color jump. Window lights should belong to buildings
and remain stable as the camera moves. The title should eventually use the approved
shared dusk recipe, avoiding a second independent palette. Night sailing, a full
24-hour clock, dates/astronomy, weather changes and new gameplay visibility rules are
outside this first slice.

## Current status

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

**Next task:** review the fixed selector in play. Any later transition or room-wide
lighting ownership needs a separate decision. This change does not add in-session lighting
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
