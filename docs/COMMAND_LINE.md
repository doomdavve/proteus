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
`basin`, while the title's default outing is Gothenburg Harbour (`outer`).

## Experimental cargo toy (native)

```sh
./build/cargo_sandbox
./build/cargo_sandbox --renderer cpu
./build/cargo_sandbox --location basin
./build/cargo_sandbox --survey
./build/cargo_sandbox --help
```

This separate native S0 experiment defaults to the Eriksberg inlet on `outer`, with a small
service crane, one crate and a borrowed deck boat. `--location basin` retains the isolated
reference (CPU draw distance is 240 m; the GPU view uses 4500 m). `--survey` checks the mapped work site without a window. Reset is immediate and
it saves automatically (see below), and it stays the review harness; the game runs the same session as `wake_velocity --work`. Holding Space brakes; G
starts/cancels assisted mooring when BERTH READY appears; N cycles the guided site (the job may
choose it) and C returns an unhooked, unlashed crate to its source pad. The HUD shows persistent throttle, the landing
footprint/deck gap before contact and the package's job row. See the [sandbox experiment](FREE_RIDE_SANDBOX_PLAN.md#s0a-try-the-cargo-toy)
for the first lift, controls, capture/measurement commands and current limitations.

`--shadow-review` is a developer-only, virtual-clock footprint visibility check (use
`--location basin --renderer cpu` for display-free checks, or `--renderer gpu` with a
desktop context). With `--screenshot FILE` it saves only the first failing frame; no image
is written on success. `--scenario shadow-bow` and `shadow-water` show normal-colour
wave-phase regression fixtures. See the [shadow correction](FREE_RIDE_SANDBOX_PLAN.md#s0b-wave-phase-shadow-correction).

## Working harbour in the game (`--work`, native)

```sh
./build/wake_velocity --location outer --work
./build/wake_velocity --location outer --work --data-dir build/work-profile
./build/wake_velocity --renderer cpu --location outer --work --screenshot build/work.png --time 3
```

`--work` runs the location's bundled working-harbour package (on `outer`, `outer-work`) in place
of the free-ride boat step, with the game's camera, water and harbour, plus the work sites, crate,
crane and the package's job rows. It is the SV1f/SV1g step of the
[save and resume plan](FREE_RIDE_SANDBOX_PLAN.md#sv1-local-save-and-resume). It cannot be combined
with `--plugin`, `--package`, rooms, `--course`, replays, wake, water, `--start`, `--skin` or
`--plugin-bridge`. The title offers it as GOTHENBURG HARBOUR, CONTENT `WORKING HARBOUR` (native
builds), with a SAVE row (CONTINUE, NEW, DELETE; NEW and DELETE ask first). START passes
`--location outer --work` (plus `--new` for NEW), so these options stay the scripted spelling.

The session saves itself: there is no save key and F5 stays the plugin reload key. Plain
interactive play reads and writes `cargo-<package id>.sav` in the game's data directory
(`--data-dir DIR` replaces the directory) at every milestone, every 30 s of simulated play, and
when the menu opens, the window loses focus, or you quit or close the window; the next `--work`
run resumes it. A small `SAVED` mark appears after milestone and leave writes, and `SAVE FAILED`
stays until a write succeeds. `--save FILE` uses an explicit file, `--new` ignores any save, and a
save that cannot be used (damaged, a changed package or physics) is set aside as `NAME.rejected`,
never deleted. Scripted runs (`--screenshot`, `--demo`, `--frames`) touch no save unless given
`--save` or `--data-dir`. `--save-now` (a test fixture, with `--time` and `--save`) runs the
scripted drive, writes one save and exits.

`--time` drives a scripted capture: idle at the berth, and after 10 s of simulated time the landing
delivery script (load, lash, depart, moor at the landing, unload), so `--time 3` shows the berth
and `--time 14` the delivered crate. Screenshots draw no HUD.

By default Working Harbour follows its session clock: a new session starts at noon, and a full
day takes 900 active scaled seconds at 1×. The solo ¼×–8× control (`[` / `]`, typed brackets,
`,` / `.`, with `\` restoring 1×) also moves the light, so 8× takes 112.5 active wall seconds
(4.6875 s per game hour). Menu, help and focus loss freeze the clock; load/crate/job resets and
recovery keep the hour, and a new session resets to noon. A saved title LIGHTING choice is
ignored here; explicit `--daylight` or `--lighting-hour` fixes the render while the clock keeps
advancing and saving. The [lighting plan](SUNSET_PLAN.md#t4-implementation-2026-11-08) owns the
full contract.

Two developer-only review options belong to bounded `--work` runs. `--work-clock-report FILE`
needs `--work` and `--frames` in a visible native window, rejects `--screenshot` and
`--performance-report`, and writes one CSV with actual GL sun/mask observations and optional GPU
times. `--work-review-pose swing|deck|approach` prepares a real cargo-api pose for bounded
screenshots or frame runs; it adds no normal menu or HUD feature.

## Locations and voyages

```sh
./build/wake_velocity --location outer
./build/wake_velocity --location basin --waves
./build/wake_velocity --location outer --skin explorer
./build/wake_velocity --location outer --start 1980,-1196,75
```

`outer` covers the Eriksberg waterfront towards Älvsborgsbron, upstream past Hisingsbron and out
along the fjord, with the solo voyage to Långedrag. The former `river` location, a crop of the same
water, was retired on 2026-11-04; `--location river` now names `outer` and exits.
`--start X,Z[,DEGREES]` places a boat on mapped water using simulation coordinates; heading zero
faces −Z. It is for solo mapped free boating, not courses or rooms. Rooms on `outer` have no voyage
or solo squall.

`--skin sweden|finland|denmark|norway|explorer|cabin|grodan` selects a solo boat. Explorer has a stronger
engine in free boating; the cabin boat reaches about 25 knots on calm water; the Grodan is a
15 m canal tour boat, about 8 knots, whose default camera sits 12 m farther back and 4 m higher.
Courses retain
the standard engine. Multiplayer assigns liveries
by slot. `--soundtrack a-little-boat|acid-wake|off` selects local music. `--mouse-steering`
starts with captured mouse steering. See [skins](BOAT_SKINS.md) and [music](MUSIC.md).

### Bundled packages

```sh
./build/wake_velocity --location outer
./build/wake_velocity --location outer --package proteus.harbor-life
./build/wake_velocity --location outer --package proteus.grodan --skin grodan
./build/wake_velocity --course
```

Every native solo session starts from a package compiled into the executable at build time; the
default for the basin is free boating, for `outer` the Långedrag voyage, and for
`--course` the basin time trial. `--package ID` selects another bundled package, which must
belong to the session's location and mode (for example `proteus.harbor-life` on `outer`).
Launching a package starts no Lua VM, polls no files and shows no authoring panel. There is no
compiled-content switch: every solo session takes this path. Browser solo sessions use the same
catalog, built into the game Wasm; rooms receive their definition as data. An unknown ID, or a
package for another location, fails with an explicit message. The title's single-player
panel selects Standard, Harbor life, the Grodan tour or the canal tour for `outer` on native and
web builds. Both tours are `proteus.grodan`: the Grodan tour always uses the Grodan, as
`--package proteus.grodan --skin grodan` does, and the canal tour keeps your livery. `--title-start PLACE` takes the title's START path with default settings, without the menu,
so tests can prove each PLACE opens (`basin`, `outer`, `outer-standard`, `outer-harbor-life`,
`outer-grodan`, `outer-canal-tour`, `outer-working-harbour`, `basin-trial`); further options follow it.

### Live Lua authoring

```sh
./build/wake_velocity --plugin plugins/langedrag
./build/wake_velocity --plugin plugins/harbor-life --start 2,161,90
./build/wake_velocity --plugin plugins/harbor-life --start -6,210,20
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

CLI voyage, free and course packages select their own location and run in native solo play.
The browser offers [descriptor source bundles](PLUGIN_API.md#browser-source-bundles-p43)
through its PLUGIN VOYAGE panel, including free/course kinds and authored scenes. Saving
reloads the package; F5 reloads now, F6 undoes/discards and F7 applies a pending migration or restart.
Invalid edits leave the running descriptor intact. Packaged plugin examples live under
`assets/plugins/`; copy one somewhere writable to edit it. Packages also ship the
[authoring tools](PLUGIN_API.md#packaged-authoring-tools-p47) in `authoring/`. The race tool previews local
course edits; network rooms use their own package definition. The harbor-life example edits seven
Eriksberg marina berths; a layout change is live when the boat is clear of the new hulls. See the
[plugin API](PLUGIN_API.md#playing-and-live-authoring-p1) and
[race example](../plugins/river-sprint/README.md) for reload policies and limits.

### Live console

In native solo free boating and voyages, including release builds, press `` ` ``, `F2` or the
world-1 key (`§` on some macOS ISO keyboards). Desktop browsers with a physical keyboard also
offer it; touch-only phones do not. A plain line edits the package's `def` table; `=`
starts a read-only query; `/` starts a command. The simulation keeps running with a neutral helm.
The first opening shows the build version and quick help. `/help` explains commands and
`/help status` explains one. Ctrl/Cmd+C copies the latest command and output; add Shift for all
scrollback. Ctrl/Cmd+V pastes a single line without executing it. See [console controls](PLAYING.md#controls)
for editing, Tab command completion, history and scrolling.

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
that is not this session's package; in the browser the same command downloads a source bundle. See the
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
exports and bests. A course whose gameplay digest equals the standard S course keeps the
established `course-*.run` best path; another `--package` course adds its digest to the filename
and refuses `--replay`/`--ghost`. Applying an edited course restarts on it with that course's own
best and no imported ghost or last run; Undo selects the earlier course's best again. See [saved runs](PLAYING.md#saved-runs) for storage and
compatibility.

## Water settings

| Option | Behavior |
| --- | --- |
| `--waves` | Gentle crossing ambient waves in any location |
| `--wake-objects` | Rideable arcade wakes; default in free boating and time trials |
| `--wake-height 0.1..3` | Arcade crest scale in metres; default 1.2 |
| `--wake-life-ticks 240..1920` | Arcade wake lifetime at 120 Hz; default 1920 (16 seconds) |
| `--current` | Authored current, not measured local conditions |
| `--crossing-wake` | Separate scripted passing boat and physical wake |

Current and scripted crossing-wake options reject courses/replays/ghosts. Courses ride the arcade
wake pool. A deliberate reset clears player wakes and restarts scripted passages. The title's Gothenburg Harbour outing
includes current, crossing wake and gentle waves; explicit launches use their supplied options.

### Developer studies (`wake_study`)

`wake_study` is a developer build of the game's own sources (native only; no package or release
carries it). It accepts every game option plus the wake studies below, which run on the direct
solo path rather than the local authority. `wake_velocity` rejects these options and names
`wake_study` (architecture decision D6).

| Option | Behavior |
| --- | --- |
| `--wake-objects-crossing` | Basin rival sharing wake and hull contacts; free boating only |
| `--no-player-wake` | Visual-only player trail |
| `--player-wake` | Legacy D3 trail (research); free boating only |

```sh
./build/wake_study --renderer cpu --player-wake --time 9 --screenshot build/player-wake.png
```

Their historical behavior, restrictions and measurements live in the
[wake rewrite plan](WAKE_REWRITE_PLAN.md), [field results](WAKE_CANDIDATE_RESULTS.md)
and [arcade object results](WAKE_OBJECT_RESULTS.md). Field mode is unavailable in browsers.

## Rendering, captures and profiling

`--daylight day|sunset|dusk` selects a fixed legacy lighting preset (default day); a numeric
phase 0..1 blends between them. In the title, SETTINGS → LIGHTING cycles 27 fixed choices:
DAY (default), SUNSET, DUSK, then 00:00 through 23:00. The choice is remembered per local player
and injected into new solo, joined and hosted sessions. The same `lighting` key stores exact
legacy names or versioned `hour-v1:HH`; malformed values and unknown versions fall back to Day.
Explicit CLI launches use their own options rather than the saved title choice.

`--lighting-hour H` is the fixed full-day override. Any finite hour wraps into
`[0,24)` (`24` is midnight, `-1` is 23:00); fractional hours remain supported, and non-finite
values are rejected. It overrides `--daylight` regardless of argument order. If either explicit
option is present, the title's saved lighting is not injected. `--time` still selects the
scripted drive and wave time, not the light, so the same hour can be captured at different
moments:

```sh
./build/wake_velocity --renderer cpu --location outer --lighting-hour 21 --time 3 \
  --screenshot build/outer-21h.png
./build/boat_water_review --review build/lighting-review --renderer cpu --lighting-hour 6
```

`boat_water_review --lighting-qualify PREFIX` is the bounded T2c developer audit. It draws the
same full physical trajectory twice with production scene drawing: once holding
`--lighting-hour` and once sampling the virtual 900 s cycle (`0..900` inclusive). It defaults to
241 measured samples (`--lighting-samples` accepts 25..3601), excludes a separate eight-frame
warmup, and writes `PREFIX.json` plus `PREFIX.csv` with per-frame hour, simulation, submission,
presentation, frame and actual `GL_TIME_ELAPSED` where available, plus CPU sky/reference/cache
counters and GPU counters. It never sleeps or advances the game clock; pose ticks are independent
of the light. The CPU renderer supports only its existing 320x200 low, non-HDR path; use
`--renderer gpu --resolution high --hdr on` for those presets. Reports contain no pass/fail
timing threshold.

```sh
./build/boat_water_review --lighting-qualify build/lighting-qualify --renderer cpu \
  --scenario stress --lighting-samples 25 --waves
```

The deathmatch capture fixture accepts the same fixed full-day hour for weapons and mirror
review; invalid or missing values are rejected before a window opens:

```sh
./build/boat_combat_review --lighting-hour 21 --screenshot build/combat-night.png
./build/boat_combat_review --gpu --mirror --play --lighting-hour 6.5 \
  --screenshot build/combat-dawn.png
```

The named Day, Sunset and Dusk recipes keep their exact legacy behavior. The CPU basin now
shares the selected hour's sky and light, and both CPU flat-water paths apply the frame's water
multiplier once with ordinary flat shading, so basin captures from earlier daytime-only builds
differ intentionally. T2c readability/performance limits and T3's fixed-hour title controls are
recorded in the [lighting plan](SUNSET_PLAN.md#t3-implementation-2026-11-08).

GPU is the default. `--renderer cpu` selects the software reference; `--gpu` is an alias
for `--renderer gpu`. `--gpu-aa` enables 4×4 supersampling while preserving the logical
320×200 pixel grid and integer-scaled desktop presentation. It does not select a location
or renderer. GPU captures need a graphics context; CPU captures need no window.

```sh
./build/wake_velocity --renderer cpu --location basin --screenshot build/basin.png --time 3
./build/wake_velocity --renderer cpu --location outer --screenshot build/outer.png --time 3
./build/wake_velocity --location outer --gpu-aa --screenshot build/outer-gpu.png --time 3
./build/wake_velocity --title --screenshot build/title.png
./build/wake_velocity --title --screenshot build/title-save.png --content working-harbour --data-dir build/work-profile --save-mode new --ask
./build/wake_velocity --renderer cpu --replay build/my-run.run --screenshot build/replay.png --time 5
```

Screenshots use a deterministic scripted drive, not interactive elapsed time. `--time`
selects simulated seconds: 0–60 normally, or 0–600 for replay, clamped to its finish.
`--demo` runs the scripted drive in a window; `--frames COUNT` limits a smoke run.

Plain free boating (a free package without a voyage, objective or live authoring, on the native
game) and the time trial (`--course`, `--ghost` or a course package) run as a private local
authority and client in the game process, live, under `--demo` and in `--screenshot` captures
(whose scripted drive runs on virtual time); the time trial's course, recording, best selection
and practice latch run on the authority, and its best and last runs are saved after the frame as
before. Replay viewing is client-side (`--replay` opens no local session; the L/B keys pause the
attempt's ticks). The developer studies, voyages, `--work`, rooms and the browser keep the direct
session path. At exit the game
prints a `Local session:` line with its generation, ticks, chunks and restarts.

`--time-scale N` sets the initial solo live speed to 0.25, 0.5, 1, 2, 4 or 8 (default 1). The
existing `[` / `]` (or typed brackets and `,` / `.`) steps and `\` restore continue to work. It
is rejected with `--connect` or `--host`; rooms keep their own time. A scripted `--time` capture
still uses its own scripted ticks rather than this scale. In `--work`, the same scale moves the
session light as well as the world because both use fixed ticks.

```sh
./build/wake_velocity --location outer --demo --frames 720
./build/wake_velocity --location outer --profiler
./build/wake_velocity --camera-distance 24 --camera-height 18 --fixed-fov
```

Camera distance accepts 8–48 units, height 4–36. Defaults are 20/14, or 12/6 on `outer`.
`--fixed-fov` holds 55° instead of widening with speed. `--no-vsync` paces gameplay at
60 Hz; the title uses vsync. `--verbose` reports graphics/window and resource startup.
F3 or `--profiler` shows rolling frame timings; GPU scene time measures CPU submission,
not GPU execution. See [performance tools](BOATING_PERFORMANCE.md),
[renderer details](3D.md) and [GPU tests](TESTING.md#gpu-checks).

On macOS with full Xcode selected, record a windowed drive in Instruments:

```sh
xcrun xctrace record --template 'Time Profiler' --output build/outer.trace \
  --launch -- ./build/wake_velocity --location outer --demo --frames 3600
```

## Rooms and scripted clients

Host and join from a native game process:

```sh
./build/wake_velocity --host free --location outer --name me
./build/wake_velocity --host deathmatch --location outer --name me
./build/wake_velocity --host race --name me
./build/wake_velocity --host hunt --name me
```

`--host free|deathmatch|race|hunt` starts a background authority open to the LAN and joins it;
leaving closes the room. `--location`, `--waves` and `--port N` configure the room. Like race,
hunt always runs on `outer` (`--host hunt --location basin` is refused) and starts a room equal
to `boat_server --mode free --location outer --package proteus.outer-hunt`.
For a separate authority, use the packaged game's `--serve`, or the development server:

```sh
./build/wake_velocity --serve --bind 0.0.0.0 --mode deathmatch --location outer
./build/boat_server --bind 0.0.0.0 --mode race
./build/wake_velocity --connect 127.0.0.1:24680 --name me
./build/boat_bot --connect 127.0.0.1:24680 --seconds 60 --hunt
./build/boat_bot --discover
```

On Windows, `host.bat` starts `wake_velocity.exe --serve --bind 0.0.0.0` and forwards
extra arguments. On macOS, a packaged headless room can run as:

```sh
"/Applications/Wake Velocity.app/Contents/MacOS/wake_velocity" --serve --bind 0.0.0.0
```

The room runs from a package. With no `--package`, `--mode free` uses the location's free
package, `--mode deathmatch` uses `proteus.deathmatch` at any location, `--mode race` uses
`proteus.river-sprint` on `outer`, where every race room runs. The shelter hunt is a free room:
`--mode free --location outer --package proteus.outer-hunt`. `--package ID|DIR` selects another
bundled package by ID or evaluates a package directory once at start-up. The authority validates the package, sends the
resulting definition to clients as data, and refuses to start with a diagnostic if it is
invalid; rule overrides such as `--round-seconds` then apply to the loaded definition.

`--package-reload` re-reads that package while the room runs. It needs `--package` naming a
directory, or a bundled ID such as `proteus.outer-hunt` (which resolves to `plugins/outer-hunt`
when run from the repository root), whose package has a room objective; otherwise the server is
refused with exit status 2. Every 120 server ticks (one second) the package is read again; when
`main.lua` or the state version changed, the new objective is staged and applies at the next
12-tick objective boundary. With a `migrate` callback the state is kept; without one the
objective restarts and re-announces present crews. The server prints
`objective reload: revision N accepted (migrate|restart)` or
`objective reload rejected: MESSAGE`; a failing candidate changes nothing. A change to the marks
(any change to the room definition) is a definition revision instead
([protocol v14](MULTIPLAYER_PROTOCOL.md#definition-revisions-v14)): a free room commits it only
while it has no players, printing `objective reload: marks changed; revision N waits for an empty
room` and later `definition revision N committed at tick T`, and the objective restarts on the
new marks; players who join afterwards receive them. `wake_velocity --serve` shares the flag. See
[room objective reload](PLUGIN_API.md#room-objective-reload-p5e) and
[its revisions](PLUGIN_API.md#definition-revisions-from-reload-p5f).

`--record PATH` creates a new authority session recording (existing paths are refused).
Reconstruct it with `./build/validate_boat_session_record PATH`, seek to a simulation tick with
`--seek TICK`, or re-evaluate embedded objective callbacks with `--diagnostic`. Ordinary playback
runs no Lua. Finish the server normally to write the required end marker. This is a headless
playback artifact; [session recordings](SESSION_RECORDING.md) owns the format and limits.

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
