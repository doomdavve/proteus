# Harbor life

A native solo `outer` package that keeps the Långedrag voyage and replaces the compiled moored
fleet and dock furniture with a twelve-berth, two-row Eriksberg marina. Two dimensioned
floating walkways, eight independent finger piers, a floating platform, fixed quay deck and
shore gangway sit beneath the compiled crane and apartment blocks.

The package also adds a small passing craft on a ~25 s loop just north of the pontoon, retains
one compiled upstream ferry with an edited Stenpiren dwell, the compiled river runabout and the
compiled schooner whose passage raises the Hisingsbron lift. One eight-bird gull flock circles
just north of the slips, with a short feeding interval and alarm climb in the first ten seconds.
The free `outer` room package [`harbor-room`](../harbor-room/main.lua) carries the same marina
layout, traffic and flock.

## Layout

Simulation metres use `x = -east`, `z = north`; headings follow the voyage API. Seven hulls
sit at `x = 13`, `z = 146..176` in 5 m steps. Five runabouts form the second row at `x = -15`,
`z = 146..166`. Walkways are at `(20, 161.5)` and `(-22, 157)`; a floating platform joins
the second walkway at `(-22, 175)`. Eight fingers attach along the two walkways. The fixed
`quay_deck` stands wholly on land at `(38, 173)`. The gangway runs from land at `(34.1, 161.5)`
to the first walkway's shore-side midpoint. The rows leave the central approach lane clear.

Scene v2 supports 32 berths and 64 structures. Each structure specifies finite `length`,
`width` and `deck_height` in metres. Floating `attaches` references an earlier floating parent,
with an orthogonal edge join and equal deck height; connected pieces share heave. See the
[API contract](../../docs/PLUGIN_API.md#scene-schema-v2) for ranges and placement rules.

The declared entrance is `(4, 140)`, width 8 m. The approach lane runs north along `x = 4`
from open water into the marina. The pinned camera bookmark `outer.marina` is `(2, 161)` at
90°, facing east across the finger slips. The `outer.eriksberg` bookmark at `(-6, 210)` (20°)
frames the marina from the north with the crane overhead, closer to the reference photograph.
The passing craft's registered loop stays north of the pontoon, so no berth occupies its path.

## Run

From the repository root:

```sh
./build/wake_velocity --plugin plugins/harbor-life --start 2,161,90
./build/wake_velocity --plugin plugins/harbor-life --start -6,210,20
```

F5 reloads and F6 undoes. Compatible layout/traffic edits apply live when clear of the player;
an obstructing edit is held until clear water or an explicit F7 restart. A saved edit stages once it has settled; an
invalid edit keeps the running revision. To watch the passing craft without moving the marina
camera, use the `outer.marina-pass` anchor at `(0, 205)` facing the loop.

## Edit

Change a berth's `recipe`, `x`, `z`, `heading`, `mooring`, the entrance, or remove a berth. The
game checks the hull and wave/mooring envelope against the compiled water, other berths, the
entrance and the spawn/recovery shelters. Depth is not available in the compiled map, so no
depth claim is made. Live staging holds an obstructing edit rather than moving a hull under the boat.

`scene.traffic.vessels` is a 0–8 entry replacement for all travelling craft. An entry either
retains a compiled actor (`{ id = "river_runabout", retain = "river_runabout" }`) or selects a
registered route and craft recipe with `speed`, `phase` and `period`. A retained actor may also
override its named stops:

```lua
{ id = "ferry", retain = "alvsnabben",
  stops = { { id = "stenpiren", dwell = 35 }, { id = "lindholmspiren" },
            { id = "eriksberg" } } }
```

The stop IDs must match the registered route in order. An omitted `dwell` shares the period's
remaining time. Authored hulls are sampled over the combined river timetable and must clear the
compiled water, the marina entrance, berths, dock modules and compiled/retained traffic. Retaining
`schooner` keeps its Hisingsbron passage and lift dependency; removing every bridge actor leaves
the span down. Traffic wakes stay visual; no physical traffic wake recipe is exposed.

`scene.gulls.flocks` is a 0–4 flock replacement with 1–8 birds each (32 total). Cue intervals use
half-open 120 Hz ticks, for example:

```lua
gulls = { flocks = {
  { id = "harbor", recipe = "gull", x = -4, z = 182, count = 8, seed = 3, phase = 0,
    radius = 10, height = 6, rate = 0.3,
    cues = { { kind = "alarm", start = 900, stop = 1200, fade = 80, amplitude = 1 } } }
} }
```

Editing only cue timing/fade/amplitude keeps the session clock. Moving or resizing a flock is a
layout edit and follows F7 restart/Undo.

## What the scene API cannot express yet

The current marina is assembled from the registered native recipes and the existing scene
budgets. These parts of the reference photograph remain outside the plugin system and need
native work before Lua can author them:

- **The crane and landmarks.** The Eriksberg gantry crane is compiled scenery. A package cannot
  select it, move it, recolor it orange, add its lettering or place a new landmark.
- **Buildings and windows.** The apartment blocks, their windows and the street dressing are
  compiled from the map. There is no building, window-group, sign or street-furniture grant.
- **Sculptures and arbitrary meshes.** Dimensioned fixed decks and floating platforms are
  available, but packages cannot import concrete sculpture or custom mesh geometry.
- **Per-boat appearance and names.** A berth selects one of seven fixed native hull recipes.
  Lua cannot set length, beam, mast height, hull/deck/trim colours, lettering or a boat name, so
  the fleet cannot match the photograph's individual white sailboats closely.
- **Moored occupancy.** Authored berths are static. There is no slip assignment, arrival,
  departure, fuel stop or owner schedule.
- **Freeform traffic routes and departures.** Travelling craft select registered route presets
  (`marina_pass`, `river_*`, `alvsnabben`, `frihamnen`, `schooner`) and timetable values. A
  package cannot draw a new route or make a boat leave its authored berth.
- **Lighting and weather.** Gameplay uses the compiled daylight. Fixed overcast/dusk and lighting
  schedules are planned in L5/L6 but are not scriptable yet, so the reference photograph's grey
  sky and wet quay are not reproducible.
- **People and animals beyond gulls.** The living-city people/cyclist work is not exposed, and
  gulls are the only authorable wildlife recipe. Seals and trams are later native slices.
- **Shared scene composition.** `harbor-room` duplicates the marina block from this package.
  There is no include, dependency or composition mechanism, so the two copies must be edited
  together.
- **Assets, depth and custom meshes.** Packages cannot import meshes, textures or shaders, and
  the compiled map has no depth query. Site validation reports depth as unverified.

## Verify

```sh
./build/validate_scene_site
./build/validate_harbor
./build/validate_traffic
./build/validate_harbor_life
./build/wake_velocity --renderer cpu --plugin plugins/harbor-life \
  --screenshot build/harbor-life.png --start 2,161,90 --time 3
```

The PNG is a display-free reference; it does not establish human acceptance or motion/audio.
`validate_harbor_life` drives the deterministic approach/idle/departure fixture, checks the
ferry dwell and gull cue, and reports compiled-versus-populated CPU frame p50/p95 plus
mesh/light/wake counts and host memory. `validate_scene_site` mirrors the expanded marina water and
placement and rejection rules through native staging of the actual Lua package.

## Pinned review recipes

Discover recipes and anchors:

```sh
python3 tools/plugin_author.py capabilities
python3 tools/plugin_author.py anchors
```

| Recipe | Camera/start | Time | Watch |
| --- | --- | --- | --- |
| Marina slips | `outer.marina` `(2, 161, 90°)` | 3 s | twelve hulls, two rows, finger piers, gangway, quay deck |
| Eriksberg composition | `outer.eriksberg` `(-6, 210, 20°)` | 3 s | marina row, apartment blocks, crane overhead |
| Passing craft | `outer.marina-pass` `(0, 205, 0°)` | 3–12 s | small craft loop and retained river runabout |
| Gull cue | `outer.marina-pass` or `(4, 215, 0°)` | 2–11 s | feeding pass, then alarm climb |
| Lift | `harbor_review --view bridge` | 300 s down, 342/382/432 s raised, 502/542 s down | schooner passage and lift phases |

`wake_velocity --time` is capped at 60 s; longer lift/timetable boundaries belong to
`validate_traffic` direct-time seeks. Use `harbor_review --renderer cpu --view bridge
--screenshot FILE --time SECONDS` for a display-free lift still.

## Worker and live bridge

The isolated worker and live mailbox are exercised headlessly with the combined package:

```sh
./build/validate_boat_plugin_stage
./build/validate_wake_bridge --serve-harbor /tmp/harbor-mailbox &
python3 tools/plugin_author.py live-inspect --bridge /tmp/harbor-mailbox
python3 tools/plugin_author.py live-stage DRAFT --bridge /tmp/harbor-mailbox \
  --base-revision 1 --base-epoch 0 --expect-bundle BUNDLE_ID
python3 tools/plugin_author.py live-undo --bridge /tmp/harbor-mailbox \
  --base-revision 2 --base-epoch 1
printf stop > /tmp/harbor-mailbox/stop
```

A cue-only edit applies as a compatible revision and keeps the tick. A flock move holds for
session restart. The windowed path is the same `--plugin-bridge` mailbox on
`./build/wake_velocity`.

## Human review (pending)

Start the game from the repository root and approach the marina:

```sh
./build/wake_velocity --location outer --plugin plugins/harbor-life \
  --start -6,210,20
```

Ask whether the twelve-berth, two-row layout reads as a marina from the boat; whether the crane, buildings
and quay composition resemble the reference photograph; whether the entrance remains clear; and
whether the scene feels alive rather than merely populated. Automated tests and captures do not
establish this acceptance. Record the platform, renderer, camera, exact package revision and any
clipping or readability concern.
