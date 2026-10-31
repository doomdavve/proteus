# Wake Velocity command-line reference

Run commands from the repository root after a native build. Packaged apps include the
same game executable; see [macOS](MACOS_BUILD.md) and [Windows](WINDOWS_BUILD.md) for paths.
For controls and rules, see [Playing Wake Velocity](PLAYING.md).

```sh
./build/wake_velocity --help
./build/wake_velocity --serve --help
./build/boat_server --help
./build/boat_bot --help
```

Each `--help` prints a grouped summary of modes, options and controls; this page
carries the details, restrictions and examples. Launching without options opens the title. Explicit gameplay options start a session
directly; `--title` selects the title explicitly. The direct-launch default location is
`basin`, while the title's default outing is the river.

## Locations and voyages

```sh
./build/wake_velocity --location river
./build/wake_velocity --location basin --waves
./build/wake_velocity --location outer --skin explorer
./build/wake_velocity --location outer --start 1980,-1196,75
```

`river` covers the Eriksberg waterfront towards Älvsborgsbron. `outer` adds the larger
upstream and outer-fjord map and the solo voyage to Långedrag. `--start X,Z[,DEGREES]`
places a boat on mapped water using simulation coordinates; heading zero faces −Z. It
is for solo mapped free boating, not courses or rooms. Rooms on `outer` have no voyage
or solo squall.

`--skin sweden|finland|denmark|norway|explorer|cabin` selects a solo boat. Explorer has a stronger
engine in free boating; the cabin boat reaches about 25 knots on calm water. Courses retain
the standard engine. Multiplayer assigns liveries
by slot. `--soundtrack a-little-boat|acid-wake|off` selects local music. `--mouse-steering`
starts with captured mouse steering. See [skins](BOAT_SKINS.md) and [music](MUSIC.md).

### Bundled packages

```sh
./build/wake_velocity --location river
./build/wake_velocity --location outer --package proteus.harbor-life
./build/wake_velocity --course
./build/wake_velocity --location basin --no-plugins
```

Every native solo session starts from a package compiled into the executable at build time; the
default for the basin and river is free boating, for `outer` the Långedrag voyage, and for
`--course` the basin time trial. `--package ID` selects another bundled package, which must
belong to the session's location and mode (for example `proteus.harbor-life` on `outer`).
Launching a package starts no Lua VM, polls no files and shows no authoring panel. The temporary
`--no-plugins` switch restores the compiled-content path for parity checks. Rooms and the browser
continue on compiled content until later steps. An unknown ID, or a package for another location,
fails with an explicit message. The title's single-player panel selects Standard or Harbor life
for `outer` on native builds.

### Live Lua authoring

```sh
./build/wake_velocity --plugin plugins/langedrag
./build/wake_velocity --plugin plugins/harbor-life --start 2,170,90
./build/boat_race_trial --plugin plugins/river-sprint --play
./build/boat_plugin_review plugins/shelter-tour 120 360
python3 tools/plugin_author.py schema
python3 tools/plugin_author.py preview plugins/shelter-tour --ticks 360
python3 tools/plugin_author.py export plugins/langedrag > build/langedrag.wake-plugin.json
```

`boat_plugin_review DIRECTORY [CHECKPOINT_TICK [SEEK_TICK]]` runs a display-free calm solo
fixture with player/traffic wakes, restores a complete compatible checkpoint and prints state,
events and command traces. Ticks default to 120/360, within 0..72000. See the
[migration/review contract](PLUGIN_API.md#live-migration-and-review-p33b) for inputs and limits.

CLI voyage packages select their own location and run in native solo free boating.
The browser offers [descriptor source bundles](PLUGIN_API.md#browser-source-bundles-p43)
through its PLUGIN VOYAGE panel. Saving
reloads the package; F5 reloads now, F6 undoes/discards and F7 applies a pending migration or restart.
Invalid edits leave the running descriptor intact. Packaged plugin examples live under
`assets/plugins/`; copy one somewhere writable to edit it. Packages also ship the
[authoring tools](PLUGIN_API.md#packaged-authoring-tools-p47) in `authoring/`. The race tool previews local
course edits; network rooms retain their compiled course. The harbor-life example edits two
marina berths; a layout change is live when the boat is clear of the new hulls. See the
[plugin API](PLUGIN_API.md#playing-and-live-authoring-p1) and
[race example](../plugins/river-sprint/README.md) for reload policies and limits.

### Live console

In native solo free boating and voyages, including release builds, press `` ` ``, `F2` or the
world-1 key (`§` on some macOS ISO keyboards). A plain line edits the package's `def` table; `=`
starts a read-only query; `/` starts a command. The simulation keeps running with a neutral helm.

```text
def.scene.marina.berths[1].x = 15
= def.scene.marina.berths[1]
/status
/capability scene.define
/undo
/save ~/drafts/my-harbor
```

The first edit moves a bundled session to authoring; the catalog's embedded source is the base.
Each edit is composed as one entry and evaluated in the isolated worker, so Lua never runs on the
game thread and the boat is never moved. Layout edits install live when the hull is clear and are
otherwise held until it is. `/save DIR` writes a complete package and refuses a non-empty directory
that is not this session's package. See the
[console contract](PLUGIN_API.md#live-console-and-command-layer-pc) for the command list and limits.
`--plugin-bridge DIR` exposes the same lines as `plugin_author.py live-console --line TEXT`.

## Courses and recordings

```sh
./build/wake_velocity --course
./build/wake_velocity --course --waves --save-run build/my-run.run
./build/wake_velocity --replay build/my-run.run
./build/wake_velocity --course --waves --ghost build/my-run.run
./build/wake_velocity --course --data-dir build/practice-saves
```

`--course` selects the basin time trial, which starts from the bundled `proteus.time-trial`
package. `--replay FILE` and `--ghost FILE` imply course mode. Replay uses the recording's water
preset; an imported ghost must match the selected preset. Imported ghosts remain selected across
retries while your personal best is tracked separately. `--save-run FILE` exports every completed
normal-time attempt, including slower runs; altered-time practice attempts preserve existing
exports and bests. A course whose gameplay digest equals the compiled S course keeps the
established `course-*.run` best path; another `--package` course adds its digest to the filename
and refuses `--replay`/`--ghost`. See [saved runs](PLAYING.md#saved-runs) for storage and
compatibility.

## Water settings

| Option | Behavior |
| --- | --- |
| `--waves` | Gentle crossing ambient waves in any location |
| `--wake-objects` | Rideable arcade wakes; default in free boating |
| `--wake-height 0.1..3` | Arcade crest scale in metres; default 1.2 |
| `--wake-life-ticks 240..1920` | Arcade wake lifetime at 120 Hz; default 1920 (16 seconds) |
| `--wake-objects-crossing` | Basin rival sharing wake and hull contacts |
| `--no-player-wake` | Visual-only player trail |
| `--current` | Authored current, not measured local conditions |
| `--crossing-wake` | Separate scripted passing boat and physical wake |

Current and scripted crossing-wake options reject courses/replays/ghosts. Courses and
recordings preserve their water settings and reject explicit player-wake modes. A deliberate
reset clears player wakes and restarts scripted passages. The title's standard river outing
includes current, crossing wake and gentle waves; explicit launches use their supplied options.

Legacy `--player-wake` and native basin-only `--wake-field` remain research options.
Their historical behavior, restrictions and measurements live in the
[wake rewrite plan](WAKE_REWRITE_PLAN.md), [field results](WAKE_CANDIDATE_RESULTS.md)
and [arcade object results](WAKE_OBJECT_RESULTS.md). Field mode is unavailable in browsers.

## Rendering, captures and profiling

GPU is the default. `--renderer cpu` selects the software reference; `--gpu` is an alias
for `--renderer gpu`. `--gpu-aa` enables 4×4 supersampling while preserving the logical
320×200 pixel grid and integer-scaled desktop presentation. It does not select a location
or renderer. GPU captures need a graphics context; CPU captures need no window.

```sh
./build/wake_velocity --renderer cpu --location basin --screenshot build/basin.png --time 3
./build/wake_velocity --renderer cpu --location river --screenshot build/river.png --time 3
./build/wake_velocity --location river --gpu-aa --screenshot build/river-gpu.png --time 3
./build/wake_velocity --title --screenshot build/title.png
./build/wake_velocity --renderer cpu --replay build/my-run.run --screenshot build/replay.png --time 5
```

Screenshots use a deterministic scripted drive, not interactive elapsed time. `--time`
selects simulated seconds: 0–60 normally, or 0–600 for replay, clamped to its finish.
`--demo` runs the scripted drive in a window; `--frames COUNT` limits a smoke run.

```sh
./build/wake_velocity --location river --demo --frames 720
./build/wake_velocity --location river --profiler
./build/wake_velocity --camera-distance 24 --camera-height 18 --fixed-fov
```

Camera distance accepts 8–48 units, height 4–36. Defaults are 20/14, or 12/6 on `river`.
`--fixed-fov` holds 55° instead of widening with speed. `--no-vsync` paces gameplay at
60 Hz; the title uses vsync. `--verbose` reports graphics/window and resource startup.
F3 or `--profiler` shows rolling frame timings; GPU scene time measures CPU submission,
not GPU execution. See [performance tools](BOATING_PERFORMANCE.md),
[renderer details](3D.md) and [GPU tests](TESTING.md#gpu-checks).

On macOS with full Xcode selected, record a windowed drive in Instruments:

```sh
xcrun xctrace record --template 'Time Profiler' --output build/river.trace \
  --launch -- ./build/wake_velocity --location river --demo --frames 3600
```

## Rooms and scripted clients

Host and join from a native game process:

```sh
./build/wake_velocity --host free --location river --name me
./build/wake_velocity --host deathmatch --location river --name me
./build/wake_velocity --host race --location river --name me
```

`--host free|deathmatch|race` starts a background authority open to the LAN and joins it;
leaving closes the room. `--location`, `--waves` and `--port N` configure the room.
For a separate authority, use the packaged game's `--serve`, or the development server:

```sh
./build/wake_velocity --serve --bind 0.0.0.0 --mode deathmatch --location river
./build/boat_server --bind 0.0.0.0 --mode race --location river
./build/wake_velocity --connect 127.0.0.1:24680 --name me
./build/boat_bot --connect 127.0.0.1:24680 --seconds 60 --hunt
./build/boat_bot --discover
```

On Windows, `host.bat` starts `wake_velocity.exe --serve --bind 0.0.0.0` and forwards
extra arguments. On macOS, a packaged headless room can run as:

```sh
"/Applications/Wake Velocity.app/Contents/MacOS/wake_velocity" --serve --bind 0.0.0.0
```

The standalone authority defaults to loopback, basin and UDP port 24680. `--bind` selects
an interface; `0.0.0.0` accepts all interfaces. It runs until Ctrl+C or a `--seconds` /
`--ticks` limit. `--players 1-4` caps room size, `--waves` enables ambient waves,
`--token TEXT` requires the same client token, and `--round-seconds S` sets deathmatch
round length (default 300). LAN discovery uses UDP 24681; change it with `--lan-port N`
or disable it with `--no-lan`. A loopback room answers only local discovery.

For joining and firewall guidance see [multiplayer](PLAYING.md#multiplayer). For internet
and browser endpoint operation see [hosting](MULTIPLAYER_HOSTING.md) and
[WebRTC rooms](WEB_BUILD.md#webrtc-rooms). Wire layout belongs to the
[protocol reference](MULTIPLAYER_PROTOCOL.md).
