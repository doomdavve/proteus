# Travelling harbor boats

Implemented 2026-10-18 at the user's request: a yellow launch in the dock, a river cruiser,
a motoring sailboat, and a compact passenger ferry. They appear in the river boating location
(and appeared in the demo, storyboards and Eriksberg location removed on 2026-10-25). The basin
course remains traffic-free.
These are invented vessels and authored routes, not surveyed ferry services or timetables.
The compact ferry replaces the original large Stena vessel proposal for this pass.

## Routes and shared state

`src/core/waterfront_traffic.[ch]` owns the compiled fleet's stable actor IDs, generated meshes,
continuous routes, velocities, lights and wake histories. The route/timetable inputs live in
`src/core/waterfront_route.[ch]` as a bounded caller-owned definition; `NULL` selects the compiled
defaults. `scene.traffic.vessels` supplies explicit retained actors, registered presets or one
bounded authored path. Added craft share pose, contact, camera and visual trail data; their
wakes remain visual. `waterfront_scene_evaluate_actors` gathers cars, lamps, moored boats and
travelling vessels at explicit scene time. CPU/GPU drawing, reflections and shadows consume
the evaluated transforms. Direct/backward seeks require no warm-up.

The following table describes the four compiled river craft. Plugin stopped services and
open/loop paths use their own declared stops and dwell intervals.

| Vessel | Route | Pace |
| --- | --- | --- |
| Yellow runabout | Dock lanes at world X=2/30 m, Z=-45…85 m, joined by 14 m semicircles south of Dockbron | 2.2 m/s; approximately 158 s per lap |
| Cruiser and motoring sailboat | River ellipse, radii 210/28 m, half a lap apart | 260 s per lap; speed varies around the ellipse |
| Passenger ferry | Outer river ellipse, radii 300/65 m | 320 s per lap; approximately 1.3–5.9 m/s |

River circuits share centre world (50, -290) and axis (0.8, 0.6) in X/Z. The outer
corridor separates the ferry from the smaller craft. Routes fit both map crops and avoid
moored boats, shores, bridge supports, and the authored camera paths. Traffic follows its
schedule regardless of player position; it has no navigation AI or yielding. These four
compiled circuits have no docking stops.

The [boat generator](HARBOR_BOATS.md) supplies hulls and the new 24 m passenger-ferry
recipe: cream/navy livery, segmented cabin windows, upper bridge, red funnel, open aft deck,
rails, and benches. Three attached emissive navigation lights mark port, starboard, and stern.
They do not consume additional local-light slots or illuminate nearby surfaces. Four water
samples provide shared heave/pitch/roll when waves are enabled; routes remain kinematic.

Each vessel leaves eight fading foam segments on either side, evaluated from historical
route positions, including across the lap boundary. All 64 segments follow the water surface.
GPU foam receives depth/shading in the main pass without casting solid slab shadows. Hulls
and lights appear in planar reflections; hulls cast shadows. Foam has no physical effect on
the player. [Physics D1/D2](BOAT_PHYSICS_PLAN.md#phase-d--currents-and-crossing-wakes)
now supplies separate authored current/scripted-wake options in every free-boating
location, alongside default physical player wakes; these fleet trails
remain visual only. The [river race R0 fixture](RACE_PLAN.md#r0-prototype--2026-10-24)
now uses a separate 40 m ferry with bounded physical jump wakes and a height-aware contact
envelope. Its [feedback correction](RACE_PLAN.md#r0-feedback-correction--2026-10-24) also
opts the four passing scenery boats into a separate bounded physical wake pool with clearer
foam trails, including the slow runabout. Run `./build/boat_race_trial --play` to try it.
These physical scenery wakes are local to the trial; ordinary free boating and rooms retain
their existing visual trails.

## Plugin-authored paths

The [Plugin API](PLUGIN_API.md#location-life-scene-contract-l11l43) owns the strict route schema
and clearance contract. Each `route = "open"` actor has one to four tangent line/arc
pieces at 120 Hz, optional dwell and bounded speed/acceleration; `loop = true` repeats the
complete travel/dwell schedule. Finite paths hold their terminal pose. The eight-actor scene
budget includes retained services. There is no interactive route editor.

Native evaluation supplies the same pose for hull rendering, contacts, camera and visual wakes.
Zero-dwell joins preserve speed; a positive dwell holds the endpoint and tangent heading.
Restart Apply/Undo installs the complete scene revision atomically. Room definition v6 and
[protocol v16](MULTIPLAYER_PROTOCOL.md#room-definition) carry finite/loop paths and M4 vessel
services; browser staging uses PSTC checked data.

The conservative survey checks water, static hulls/structures and scheduled craft between
samples. Finite routes include terminal occupancy over a full period of each periodic obstacle.
Loops first try spatial separation, then simultaneous poses over a bounded exact-tick common
schedule for supported POINTS obstacles. Unsupported repeat semantics, overflow and exhausted
200,000-actor-evaluation work reject explicitly. Legacy preset pairs keep the 4,160-second
sampled check; it is separate from this authored-path proof. Safe paths can reject conservatively.

Several authored paths are checked pairwise over the ticks in which both can be present (service
windows included), at 12-tick samples within the 1,800-sample budget, using oriented hull boxes
and a travel margin; after the last moving tick both are static and the final sample covers them.
A service vessel travels with its own customized hull, may touch only the pier or quay nearest its
berth and only during its docking pieces, and treats empty berths as open water.

## Marina vessel services (M4)

A `marina.services` entry binds a vessel, an empty berth and one open-path route. The native
owner (`waterfront_service_hull`) answers moored, travelling or absent at a simulation tick, so the
harbor frame, traffic frame, contacts and camera exclusion agree and a vessel is never drawn or
collided twice. Moored and travelling hulls both use the shared wave pose at the berth pose, so
there is no step at the handoff. A serviced route actor is not drawn, lit or given a wake, and
serviced vessels have no visual wake yet. Services are one-shot: windows are half-open and nothing
repeats.

The [route demo](../plugins/marina-route-demo/README.md) provides a platform approach, two turns,
a pause and departure with retained ferry/bridge services. It maintains stand-off clearance;
berth occupancy, reservations, ropes/handoffs and snug docking belong to M4. Depth remains
unverified. Reactive avoidance and physical traffic wakes are outside this contract.

## Player contacts

The 120 Hz boating simulation checks full oriented rectangles against padded moving hulls.
The existing forgiving shoreline response now uses obstacle-relative contact velocity,
including the vessel's turn rate. Only the player separates, rebounds, and loses relative
speed; traffic remains on its authored track. This is a forgiving obstacle model, not a
coupled two-body simulation. Berths and shore retain stationary contacts. Basin handling,
course state, recordings, and replay revisions remain unchanged.

## Review

```sh
./build/wake_velocity --location outer --gpu-aa --waves
./build/harbor_review --view launch --waves
./build/harbor_review --view ferry --waves --time 40
./build/harbor_review --view traffic
./build/harbor_review --view ferry --waves --time 40 --frames 600
./build/harbor_review --renderer cpu --view ferry --time 40 \
  --screenshot build/ferry-cpu.png
./build/harbor_review --view ferry --waves --time 40 --resolution 320x240 \
  --screenshot build/ferry-gpu.png
ctest --test-dir build --output-on-failure
```

The launch/ferry cameras follow the selected vessel; traffic gives a wider river view.
The existing marina/workboat/overview views remain available. GPU defaults to 4×4 AA and
needs desktop graphics; CPU screenshots need no window. See the berth guide for review flags.

`harbor_traffic` checks complete route footprints at 12 Hz in both maps, analytical motion,
loop continuity, pairwise clearance over the 4,160-second combined river schedule, direct
seeks, flat/wave consistency, and wake bounds. Independent physical hull-overlap checks
exercise actual 120 Hz player encounters, beam-on rams, and powered recovery against all
four vessels. Actor snapshot checks preserve the legacy street equations and exercise seeks
through the full demo. CTest includes CPU and opt-in GPU ferry captures.

Night deck lighting, passenger animation, ferry docking/boarding, dynamic traffic avoidance,
physical fleet crossing wakes, and a large ship are future work. Human handling/artistic acceptance
remains separate from successful automated checks; see the living-city progress log for the
current verification evidence.

## Arcade height-aware contacts — 2026-10-22

The player now uses vertical overlap against conservative transformed hull/cabin/mast
bounds evaluated at the shared water pose. Horizontal SAT and obstacle-relative impact
response are retained. A clear jump over a low craft does not collide; a tall mast still
blocks. The scenery fleet remains kinematic. The optional basin crossing craft instead
uses real `Boat` dynamics, shared wake emission and mutual mass/inertia-aware impacts;
[arcade implementation](WAKE_ARCADE_IMPLEMENTATION.md) owns its recovery and verification.
These are conservative envelopes, not triangle-accurate collisions or walkable decks.
