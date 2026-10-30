# Harbor life

A native solo `outer` package that keeps the Långedrag voyage and replaces the compiled moored fleet and
dock furniture with two editable berths, a small pontoon and a gangway. It also adds a small
passing craft on a ~25 s loop just north of the marina, retains one compiled upstream ferry with
an edited Stenpiren dwell, the compiled river runabout and the compiled schooner whose passage
raises the Hisingsbron lift. One eight-bird gull flock circles just north of the slips, with a
short feeding interval and alarm climb in the first ten seconds.

The berths sit at simulation `(13, 167)` and `(13, 173)` facing east toward the small authored
pontoon at `(20, 170)`. A gangway runs from land at `(34.1, 170)` to that pontoon.
The declared entrance is `(6, 168)`, width 8 m. The approach lane is `x = 6` from `z = 150` to
`z = 176`; the pinned camera/start bookmark is `outer.marina` at `(2, 170)`, facing east
(`heading = 90°`) across both slips.

## Run

From the repository root:

```sh
./build/wake_velocity --plugin plugins/harbor-life --start 2,170,90
```

F5 reloads, F6 undoes, and F7 applies a held layout change with a session restart at the
compiled outer start dock. A layout or traffic edit therefore returns the boat to the start of
the voyage; travel back to the marina to inspect it. A saved edit stages once it has settled; an
invalid edit keeps the running revision. To watch the passing craft without moving the marina
camera, use the `outer.marina-pass` anchor at `(0, 205)` facing the loop.

## Edit

Change a berth's `recipe`, `x`, `z`, `heading`, `mooring`, the entrance, or remove a berth. The
game checks the hull and wave/mooring envelope against the compiled water, other berths, the
entrance and the spawn/recovery shelters. Depth is not available in the compiled map, so no
depth claim is made. A layout change restarts the session rather than moving a hull under the
boat.

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

## Verify

```sh
./build/validate_scene_site
./build/validate_harbor
./build/validate_traffic
./build/validate_harbor_life
./build/wake_velocity --renderer cpu --plugin plugins/harbor-life \
  --screenshot build/harbor-life.png --start 2,170,90 --time 3
```

The PNG is a display-free reference; it does not establish human acceptance or motion/audio.
`validate_harbor_life` drives the deterministic approach/idle/departure fixture, checks the
ferry dwell and gull cue, and reports compiled-versus-populated CPU frame p50/p95 plus
mesh/light/wake counts and host memory.

## Pinned review recipes

Discover recipes and anchors:

```sh
python3 tools/plugin_author.py capabilities
python3 tools/plugin_author.py anchors
```

| Recipe | Camera/start | Time | Watch |
| --- | --- | --- | --- |
| Marina | `outer.marina` `(2, 170, 90°)` | 3 s | both berths, pontoon, gangway, entrance |
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
  --start 2,170,90
```

Ask whether the passing craft, ferry activity and gull cue are noticeable from the boat; whether
the entrance remains clear; and whether the scene feels alive rather than merely populated.
Automated tests and captures do not establish this acceptance. Record the platform, renderer,
camera, exact package revision and any clipping or readability concern.
