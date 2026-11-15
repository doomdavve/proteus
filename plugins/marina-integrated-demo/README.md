# Marina integrated demo

The M5 example that uses the whole marina feature set in one package built from local modules. It
is an example, not a shipped default: it is not in the bundled catalog or the release package
lists. On the outer map it shows the shared twelve-hull Eriksberg marina (customized hulls and
painted names such as `MIRA` and `SALT`, two rows, fingers, a gangway and the compiled crane), a
guest dock to its north, a resident's round trip and a visitor's complete stay.

## Modules

`plugin.json` declares four modules and `main.lua` is only the voyage and the assembly:

| Module | File | Role |
| --- | --- | --- |
| `eriksberg_marina` | `modules/eriksberg_marina.lua` | The shared marina scene, a byte-identical copy of [`plugins/_shared/eriksberg_marina.lua`](../_shared/eriksberg_marina.lua). Never edit it here; run `python3 tools/plugin_author.py sync-shared`. |
| `marina` | `modules/marina.lua` | Requires the shared scene, deep-copies it (the module result is cached for the whole evaluation) and appends two empty guest berths and two floating platforms. |
| `guests.routes` | `modules/guests/routes.lua` | The resident's racetrack and the visitor's approach as authored open paths. |
| `guests.services` | `modules/guests/services.lua` | Requires `guests.routes`; the two vessels and their one-shot services. |

`main.lua` requires `marina`, `guests.routes` and `guests.services`, appends the two paths to the
marina's traffic and sets `scene.marina.vessels` and `scene.marina.services`. The requires nest
(`marina` requires `eriksberg_marina`; `guests.services` requires `guests.routes`), and every
module is checked by `validate` and staged with the whole package, so a module-only edit is a
new revision with the same Apply/Undo rules as an entry edit.

## Timetable

120 simulation ticks per second. The guest dock is at `z = 231` (berth `home` at `x = -16`, berth
`slip` at `x = 12`), north of the passing craft's loop (`z` 184..198), because a service path is
forward-only and starts and ends at its berth pose, which the marina's slips (stern against a
walkway) cannot do; the dock's side-to platform berths can.

| Seconds | Resident `tender` (berth `home`) | Visitor `guest` (berth `slip`) |
| --- | --- | --- |
| 0-2 | Moored | Absent |
| 2-14 | Moored until 5; then the racetrack | Arrives from the north: straight run, turn, slow approach |
| 14-16 | Racetrack | Moored at the slip (dwell) |
| 16-21 | Racetrack | Departure turn |
| 21-25 | Racetrack until 25 | Absent again |
| After 25 | Moored | Absent |

Each vessel has one owner at every tick: the berth owns it while moored, its path while
travelling, and a visitor has no hull outside its window. The services are one-shot. Valid
visitor `start_tick` values on a 120-tick grid are 0, 120, 240 and 1440 onwards; 360 to 1320 are
rejected because the visit's path crosses the resident's racetrack
(`crosses scene.traffic.vessels[5]`). To move the visit, edit `start_tick` in
`modules/guests/services.lua`; it restarts the session and Undo restores it.

## Run

From the repository root:

```sh
python3 tools/plugin_author.py validate plugins/marina-integrated-demo
./build/wake_velocity --plugin plugins/marina-integrated-demo --demo --demo-idle \
  --start 0,246,0 --camera-distance 24 --camera-height 36 --fixed-fov --no-vsync --frames 3300
./build/wake_velocity --plugin plugins/marina-integrated-demo --demo-idle \
  --start 0,246,0 --camera-distance 14 --camera-height 30 --fixed-fov \
  --renderer cpu --time 15 --screenshot build/marina-integrated-15.png
./build/wake_velocity --plugin plugins/marina-integrated-demo --start 2,161,90
```

Use `--time` equal to `tick / 120` for handoff views (599/600 tender leaves, 1679/1680 visitor
moors, 1919/1920 visitor leaves, 2519/2520 visitor gone, 2999/3000 tender home). Keep the player
boat out of the racetrack (the tender pushes it): the start above sits north of the docks. The
`--camera-height` option takes 4 to 36. Hull colours show from above through `deck`; the `hull`
colour is the side. The marina's names are small at 320x200; `--renderer gpu --resolution high`
shows them.

Measure its cost against the harbor-life baseline with the manual tool (not a CTest):

```sh
./build/measure_marina_package plugins/marina-integrated-demo
./build/measure_marina_package plugins/harbor-life
```

## Limits

Not covered: serviced vessels have no visual wake, a parked visitor and repeat are deferred, depth
is not surveyed, and the guest dock's vessels use the `runabout` recipe only. A failed module
edit names its file when Lua rejects it (a syntax or runtime error); a rejected layout or
timetable names the scene field with the file `main.lua`, because the decoder sees the assembled
scene, not a module. The [API](../../docs/PLUGIN_API.md) owns the fields and the module rules.
[Dated evidence](../../docs/evidence/marina-m5/2026-11-15/README.md).
