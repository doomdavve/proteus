# Sunset and dusk in gameplay

Status, 2026-11-03: steps 1 and 2 merged to `main` (#138) and the fixed sunset and dusk
presets approved by the user; step 3 awaits their decisions. The user liked
the dusk river in title mock-up B and asked for sunset in the game; this document owns that
work. Gameplay stays daytime by default. Sunset and dusk are reachable only through the
review option `--daylight`; there is no game setting, transition or day/night cycle.

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
3. **Transition and controls.** Interpolate approved presets continuously with an
   explicit presentation time/phase input. Decide solo setting names/defaults and the
   title's fixed phase with the user. Keep visual time separate from 120 Hz physics,
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

**Completed (2026-11-03, steps 1–2):** `src/waterfront_lighting.[ch]` evaluates one
caller-owned `WaterfrontLighting` from an explicit phase (0 day, 0.5 sunset, 1 dusk, linear
between them). `WaterfrontSceneFrame.lighting` carries it to the GPU sky, city lighting, fog,
shadow direction, water and reflections and the CPU sky, flat shading, displaced water and
self-coloured actors; windows share an occupancy rule and light up as `window_light` rises.
The sky panorama repaints when the light's sky changes, and each GPU scene re-uploads its
texture. `BoatGpu.daylight`/`BoatCpuView.daylight` choose the phase; `wake_velocity --daylight`
and `boat_water_review --review DIR --daylight` expose the three presets for review only.
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

**Known limitations:** storm/squall readability, course and
weapon visibility at sunset/dusk not reviewed; the squall's own cloud and rain colours and
the overcast overlay are unchanged; the CPU basin has no sky and keeps its own light
direction; the CPU textured-box material path is unlit by the frame; the shadow maps still
render at dusk; a panorama repaint costs about 25 ms (Debug), which a transition must avoid
paying every frame; no frame-cost measurement, browser or Windows check yet; intermediate
phases are untested visually. The title keeps its own grade.

**Next task:** step 3 (transition, controls and ownership), which needs the user's
decisions on names, defaults and the title phase; don't start it unasked. The graphics
overhaul's G5b builds its dusk lamplight, streaks and harbour lamps on the frame's window
light (machine-verified 2026-11-03 on `feat/graphics-presentation`; see
[the plan](GRAPHICS_OVERHAUL_PLAN.md#g5b--clustered-cozy-lighting-and-dusk-water)).
