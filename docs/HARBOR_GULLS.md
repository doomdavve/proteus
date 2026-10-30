# Harbor gulls

Implemented 2026-10-18 for `wake_velocity` in the then Eriksberg and river locations; since
2026-10-25 the river is the only mapped location. The basin/course has no gulls. The main demo's
five-bird route was removed with the demo on 2026-10-25.

Four groups of eight birds occupy the inner dock, dock mouth, and river. One follows behind
the travelling cruiser. Shared circling paths provide cohesion and alignment; soft personal
space offsets discourage overlapping flockmates. Each bird follows feeding and alarm cues
with a small delay, making a change spread through the group. Imagined splashes/food trigger
low passes, opening circles, and recovery climbs. Individual sizes, heights, wingbeat rates,
glide intervals, and asymmetric wing trim break up synchronized movement.

Since L4.1 (2026-10-30), the four groups, counts, centres, radii, heights, turn rates and cue
intervals live in `src/waterfront_flock.[ch]` as a caller-owned bounded definition. The compiled
definition remains the default; traffic avoidance reads the accepted traffic routes. L4.2 exposes
`scene.gulls.flocks` with up to four flocks and 16 half-open 120 Hz cue intervals each. Cue-only
edits are cosmetic and keep the session clock; flock layout edits follow the restart policy.

Passing traffic and the player's current position and velocity cause smooth outward displacement
and climbing. A faster player approach increases the response radius and effort. Flight headings,
banks, and climbing/diving pitch come from neighboring path samples. These are analytic visual
responses, not bird physics or persistent memory: birds resume their routine as a stimulus leaves.
There are no bird contacts, sound effects, perching, nesting, terrain avoidance, or feeding controls.
The routes are authored for the waterfront; arbitrary building/bridge clearance is not guaranteed.

`waterfront_boating_gulls` in `src/waterfront_scene.c` writes renderer-independent transforms
into `WaterfrontSceneFrame`. CPU boating draws the same poses as GPU color, reflection, and
shadow passes. The snapshot has room for 32 gulls. Explicit time and player pose fully determine the result, including
backward seeks. The social paths are authored cues rather than an integrated boids simulation.

## Review

Run from the repository root:

```sh
mkdir -p build/living-city/gulls
./build/wake_velocity --location river --gpu-aa --demo --frames 360
./build/wake_velocity --renderer cpu --location river --time 0 --screenshot build/living-city/gulls/dock-start.png
./build/wake_velocity --location river --gpu-aa --time 0 --screenshot build/living-city/gulls/dock-gpu.png
./build/wake_velocity --location river --camera-height 6
```

The default opening view shows the nearest flock ahead of the boat. Lower the chase camera
with PageDown to watch flight against the skyline. Travel toward a circling group at idle and
under power to compare its reaction. GPU commands require a desktop graphics context.

The `harbor_traffic` CTest includes 120 seconds of gull continuity, flockmate spacing, finite
poses, wing attachment, repeated/backward seek consistency, a moving-player crossing,
player proximity/speed response, and boat-frame integration checks. Verification evidence and remaining human review belong to the
[living-city progress log](LIVING_CITY_PLAN.md#progress-log).
