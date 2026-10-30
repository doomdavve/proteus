# Harbor life

A native solo `outer` package that keeps the Långedrag voyage and replaces the compiled moored fleet and
dock furniture with two editable berths, a small pontoon and a gangway. It also adds a small
passing craft on a ~25 s loop just north of the marina, retains one compiled upstream ferry with
an edited Stenpiren dwell, and retains the compiled river runabout. The docked ferry and bridge
remain independent compiled actors.

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
compiled water, the marina entrance, berths, dock modules and compiled/retained traffic. Their
wakes stay visual; no physical traffic wake recipe is exposed.

## Verify

```sh
./build/validate_scene_site
./build/validate_harbor
./build/wake_velocity --renderer cpu --plugin plugins/harbor-life \
  --screenshot build/harbor-life.png --start 2,170,90 --time 3
```

The PNG is a display-free reference; it does not establish human acceptance or motion/audio.
