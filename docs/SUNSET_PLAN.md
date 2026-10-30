# Sunset and dusk in gameplay

Status: proposed 2026-10-27. The user liked the dusk river in title mock-up B and asked
for sunset in the game. This document owns that work. The title redesign supplies a
static dusk preview; gameplay still uses its existing daytime lighting. No sunset
renderer, time control or day/night cycle is implemented or verified here.

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

1. **Shared lighting frame (next).** Introduce a small caller-owned presentation value
   for sun direction, direct/ambient color and strength, sky/horizon and haze colors,
   and window-light blend. Evaluate it from an explicit normalized daylight phase,
   never the wall clock. Keep daytime as the default and preserve its existing output.
   Trace consumers through `waterfront_scene`, CPU `renderer3d`/`waterfront` and GPU
   `renderer_gpu`, including river/outer scenery and water. Sky panorama caching must
   follow the supplied lighting value rather than the current fixed ready-once sky.
2. **Fixed sunset and dusk.** Feed the same lighting frame into both renderers and their
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

Completed task: design direction and staged scope recorded, linked from the title plan
and documentation index. Verification: repository lighting consumers inspected on
2026-10-27; no sunset runtime tests or user acceptance claimed. Next task: shared lighting
frame and unchanged-daytime parity, followed by reviewable fixed sunset/dusk captures.
