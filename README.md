# Proteus

A small C/raylib playground with deliberately low-resolution output and tracker music.
Its main program is a boating game, Wake Velocity (`wake_velocity`), in a basin and on the
Gothenburg river towards Älvsborgsbron.

[Play Proteus Boating in your browser](https://doomdavve.github.io/proteus/).

The game provides free exploration, a timed basin course, ghosts, replays and multiplayer rooms. See the [documentation and current plans](docs/README.md) for project status,
next tasks, and the [Gothenburg boating design](docs/BOATING_PLAN.md).

[Multiplayer is in progress](docs/MULTIPLAYER_PLAN.md): a native-first, four-player shared
boating world, starting with two players colliding and jumping each other's wakes. Native
builds can now join a [diagnostic room](#multiplayer-diagnostic-room) on one machine or a LAN.
Your boat is now predicted and answers the keys at once, but the other boat, wakes and
rendering are still diagnostic; it is not the finished multiplayer game. The title's
MULTIPLAYER > HOST A ROOM starts a room on your machine and joins it
([hosting](#hosting-from-the-game)). Browser builds can join an
[optional WebRTC-enabled authority](docs/WEB_BUILD.md#webrtc-rooms); no public service is bundled.

The [river race](docs/RACE_PLAN.md) is being tuned towards a 45–60 second sprint,
with a shared starting grid, frequent ferry-wake jumps and confirmed finishing positions.
A [playable R0 ferry-wake trial](docs/RACE_PLAN.md#r0-prototype--2026-10-24) now measures a
shortened Eriksberg-to-bridge stretch of the river (about 49 s at full throttle):
`./build/boat_race_trial --play`. Race rooms (`./build/boat_server --mode race`) grid two to
four players, hold them through a 3-2-1 countdown, pass six alternating skill gates and a
moving rough-water storm with rain and lightning, and award places at the finish.
Preview the current layout with
`./build/boat_race_trial --skill-course --play`; see
[race rooms](#multiplayer-diagnostic-room) below. Human race play is still to be checked.

## Generate the original sample pack and starter XM (archived sketch)

```sh
python3 tools/generate_audio.py
```

The generated WAV files are source material for editing in FastTracker II Clone.
`starter.xm` is an intentionally simple, editable sketch: 125 BPM, eight patterns,
four-on-the-floor drums, bass, minor chords, and a small lead motif.

## Build and run

The first configure downloads the pinned raylib source and, for native builds, the pinned
ENet networking library. Native builds also require libcurl development files; macOS uses
the system library, Linux setup is linked below, and Windows uses static libcurl through
vcpkg (`x64-windows-static`). Later builds are offline.

Linux builds enable native Wayland by default, with X11 support for X11 sessions.
See [Linux setup and verification](docs/LINUX_BUILD.md) for dependencies and backend options.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --preset full   # or --preset quick (about 1 s) while editing
./build/wake_velocity
```

The normal suite keeps arcade wakes and gameplay checks enabled. Retired physical wake-field
experiments are disabled until that work resumes; re-enable with
`-DPROTEUS_WAKE_FIELD_TESTS=ON`. Numerical validators use `-O2` in Clang/GCC Debug
builds while retaining assertions; use `-DPROTEUS_OPTIMIZE_TESTS=OFF` for source stepping.
See [test workflow and timings](docs/TESTING.md) for the fast suite and optional field checks.

## Boating sketch

The boating game also builds for browsers from the same C sources. See
[browser builds and GitHub Pages](#browser-build-and-github-pages) below.

For a downloadable Windows executable built on GitHub Actions, see
[Windows Wake Velocity builds](docs/WINDOWS_BUILD.md).
For an Apple Silicon Mac app ZIP built on GitHub Actions, see
[macOS app builds](docs/MACOS_BUILD.md), including the first-launch security approval.
Version tags and published releases automatically build and attach both app ZIPs;
see [automated releases](docs/RELEASES.md).

Explore the bundled river with the same boat and controls:

```sh
./build/wake_velocity --location river
./build/wake_velocity --location river --gpu --gpu-aa
./build/wake_velocity --location basin
./build/wake_velocity --renderer cpu --location river --screenshot build/river-boat.png --time 3
```

The player craft is a simple speedboat with a pointed bow and low swept windscreen.
Choose a livery with `--skin sweden|finland|denmark|norway|explorer` (default `sweden`), for
example `./build/wake_velocity --skin finland --location river`, or BOAT in the title's settings.
Every livery has identical geometry. The four countries share the handling. `explorer` (graphite
hull, sand deck, orange stripe and pennant) is the long-outing boat. When free boating solo it
has a stronger engine: about 30 m/s (58 knots) flat out against 23 m/s. Courses always use the
standard engine, so runs and bests compare, and rooms colour boats by slot as before. The choice
is local; multiplayer skin synchronization remains pending.
See [player skins](docs/BOAT_SKINS.md) for palette customization and close review commands.

Each skin carries its matching Swedish, Finnish, Danish or Norwegian flag on the stern staff. A light breeze moves the cloth
at rest; apparent wind and moving attachments make it stream aft, flutter, and lag through
turns. Playback and ghosts animate it too. See [the flag guide](docs/BOAT_FLAG.md) for
model limits and close inspection commands, including
`./build/boat_flag_review --renderer gpu --scenario cruise --time 3 --frames 300`.

For the repeatable boat-level water audit, run `python3 tools/boat_water_review.py --sequence`.
It captures a driven dock/river/quay circuit in both water presets and camera heights;
GPU captures need a desktop graphics context. Findings, artifact names, optional movie
encoding, and the implemented surface pass are in [the water review](docs/WATER_REVIEW.md).

The hull banks into established forward turns according to lateral load: speed and turn
tightness both matter. Roll inertia carries the lean through steering changes and coasting;
waves continue moving the same hull, while the chase camera keeps a level horizon.
Dry flight preserves horizontal momentum. The wheel provides modest yaw control; the engine,
hull grip, skeg and automatic banking need water contact. Brief inverted flight is allowed;
0.6 seconds of inverted water contact capsizes the boat and stops propulsion. Hold Backspace for one second or choose RESET BOAT in the Escape menu to
restart (on `outer`, RECOVER BOAT returns to the last shelter instead). The HUD reports flight, landing and capsize. Traffic and berth obstacles require
vertical overlap, so jumps can clear low craft while tall masts/quays remain solid.
The optional `--wake-objects-crossing` basin rival is a second boat in the multiplayer M2 fleet:
both boats step together, share the waves and wakes, and meet with simultaneous equal-mass hull
impacts. Two seconds after capsizing it asks for a reset and returns at a basin spawn point that
is calm and clear of both boats. See the
[arcade review implementation](docs/WAKE_ARCADE_IMPLEMENTATION.md) for rules and limits.

### Title screen

`./build/wake_velocity` without options (and the packaged macOS app and Windows `launch.bat`)
opens the **Wake Velocity** title: SINGLE PLAYER, MULTIPLAYER, SETTINGS and EXIT.
Its cyan workstation interface sits over a boat-free dusk river capture, with A PROTEUS GAME beneath
the chunky bitmap title and the build version at lower left. Menu text uses Daniel
Linssen's m5x7 font, with m3x6 for compact hints and footer data; SETTINGS retains the
map and font credits. [Font sources and regeneration](assets/fonts/README.md).
This dusk palette is title-only; [gameplay sunset is planned](docs/SUNSET_PLAN.md).
The version is resolved at CMake configure time (reconfigure to refresh it); source archives
can set `-DPROTEUS_VERSION=v0.0.19`. Up/Down or
W/S select and Enter/Space or a click activates. SETTINGS holds NAME, BOAT, STEERING and SOUNDTRACK: Enter on
NAME starts typing (Enter keeps, Esc cancels), Left/Right change BOAT, and Left/Right or Enter
switch STEERING between KEYBOARD and MOUSE. Sessions start in that mode; switching in game (M
or the Esc menu) is remembered when the session returns to the title.
SOUNDTRACK cycles through A LITTLE BOAT, ACID WAKE and OFF with Left/Right, Enter or a click.
The music choice applies locally in both single player and multiplayer.
SINGLE PLAYER starts the river with current, crossing wake
and waves in the chosen livery. MULTIPLAYER defaults to SERVER, which lists published native
rooms. Left/Right selects a room and Enter joins it; the list refreshes when MULTIPLAYER opens. HOST A ROOM opens MODE (free
boating, deathmatch or river race), PLACE, WATER and START ROOM; START ROOM runs a room open
to the local network and joins it (see [hosting](#hosting-from-the-game)). Below it,
MULTIPLAYER searches the local network for rooms. Its LAN row names the first one found (host machine and players). With
several rooms it shows LAN 1/2 and so on; Left/Right switch rooms, and Enter joins under NAME.
The hint below the menu gives the room's mode and address, and says when a room needs a token or
runs another game version. ADDRESS and CONNECT still join any `HOST:PORT` directly. Rooms colour
boats by slot, so BOAT applies to single player only. Esc in a session opens a menu (RESUME,
STEERING, CONTROLS, BACK TO TITLE, LEAVE ROOM or, when hosting, CLOSE ROOM), and a failed or
lost connection shows why back on the title.
NAME, BOAT, STEERING, SOUNDTRACK, the address and the host choices are remembered in `title.txt` in the personal-best data
directory.
Any explicit option skips the title; `--title --screenshot FILE` captures it.

### Multiplayer diagnostic room

The headless authority, `boat_server`, hosts basin or river rooms for up to four
native clients over UDP (basin by default):

```sh
./build/boat_server                                   # loopback room on UDP port 24680
./build/wake_velocity --connect 127.0.0.1:24680 --name me
./build/boat_bot --connect 127.0.0.1:24680 --seconds 60   # optional scripted second boat
```

For a river deathmatch on your LAN, either host it from the game and play in it, or run
only the room:

```sh
./build/wake_velocity --host deathmatch --location river --name me   # room + your boat
./build/boat_server --bind 0.0.0.0 --mode deathmatch --location river
```

Deathmatch has three weapons: hold **Space** for a short-range water cannon, press **E**
to drop a wave grenade 7 m astern, and press **Q** to launch a straight-running torpedo.
A small rectangular cannon on the starboard foredeck throws water cubes in a gravity-driven
arc, reaching about 50 m on calm water. It sits on a low deck mounting foot; the cubes expand after
leaving its narrow opening.
Sustained cannon hits build recoverable heel pressure, strongest against a broadside, and can
capsize a boat without hull damage. The cannon overheats after four seconds and cools on release.
Grenades splash after 0.3 s, detonate after 1 s and recharge in 8 s. Their 4 m peak wave
propagates at 10 m/s for 7 s, fading out at 70 m; it can also capsize the thrower. Each life starts with 100 hull health and three 40-damage torpedoes;
torpedoes arm after 0.25 s and reload in 1.5 s. Bottom instruments show hull, heel pressure,
cannon heat, grenade readiness and torpedo ammunition. Numbers above opponents show only your
confirmed hull damage, never their health. A rearview mirror at the top of the screen shows
pursuers, the stern grenade and its wave while you keep steering forward. Press **V** to toggle
it in deathmatch; it starts hidden. Starboard appears on the mirror's right. Spawn protection blocks launches and direct hits for
two seconds. Kills include hull destruction and capsizes; defeats respawn after three seconds.
`boat_bot --hunt` uses all three weapons.
[Rules and verification](docs/DEATHMATCH_PLAN.md#arsenal-milestone--2026-10-27).

`wake_velocity --serve` accepts every `boat_server` option, so a package needs only the game.

Clients join through the title or `--connect HOST:24680` and automatically load the server's
location, scenery and water. Multiplayer starts are out in the river, about 60 m apart, with
shoreline/boat/wake checks for safe resets. Protocol v7 requires rebuilt server and clients;
older packages are rejected with a version mismatch.

The server runs until Ctrl+C, `--seconds` or `--ticks`. `--waves` gives the room gentle waves,
`--players 1-4` caps it, and `--token TEXT` requires the same `--token` from clients. To accept
other machines, bind a LAN address (`--bind 192.168.1.20`) and allow the UDP port through the
firewall (`--bind 0.0.0.0` accepts every interface). Such rooms also answer LAN searches on
UDP port 24681 (`--lan-port N` moves it, `--no-lan` turns it off). A loopback room answers only
searches from its own machine. `./build/boat_bot --discover` lists the rooms it finds. Internet
hosting uses manual port forwarding or a VPN; see the
[Winterfell hosting guide](docs/MULTIPLAYER_HOSTING.md) for native and browser access.
Native clients have no automatic NAT traversal or transport encryption; there are no player accounts.
The room's players are listed at the top right.

`--mode deathmatch` (2026-10-24) runs timed capsize rounds once two players are
in: capsizing another boat with your wake or a hit within the last 5 s scores +1, capsizing
yourself −1, and requesting a reset right after a hit still counts for the hitter. Capsized boats
respawn after 3 s (a reset request after 1 s). `--round-seconds S` (default 300) sets the round, followed by
a 10 s intermission. In `wake_velocity --connect` the top of the screen shows the round clock, your
score and place, a kill feed under the player list names who capsized whom, and a capsized boat
says who did it and when it respawns. Hold Tab for the scoreboard; it also stays up between
rounds. The server log prints every capsize and each round's result. `boat_bot --hunt` drives
at the nearest boat to give you something to play against. The new capsize mechanics are still
to come; see the [deathmatch plan](docs/DEATHMATCH_PLAN.md).

`--mode race` (2026-10-24) hosts the river sprint on the river. With two or more players the
room lines everyone up behind the start, holds the boats through a 3-2-1 countdown (set your
throttle) and releases them together at GO. Pass between the tall buoy markers at each of
six 32–40 m skill gates; the middle pair forms a chicane. Between gates 4 and 5, a local
storm travels down-course at 8 m/s, meeting racers around the chicane and adding steep
irregular directional waves beneath a high dark cloud, with gusting, varied rain and
periodic lightning. Storm and ferry waves now keep their blue-green faces, with pale foam
confined to narrow crests. The ferry now leaves three crests per side, with a larger middle
wave and smaller outer waves; the [shorter wake](docs/RACE_PLAN.md#three-crest-ferry-wake--2026-10-24)
was user-approved on 2026-10-24. Yellow marks your next
gate, orange ones come later, and teal ones are cleared. The chequered line is the finish.
The current stronger layout takes bots about 63–71 s; play tuning is still in progress.
The HUD shows your time,
position and gate; capsized boats are put back behind their last gate after 2 s (a reset request after
0.5 s). Finishers coast for 2 s and are parked beside the river. Racing closes 15 s after the
winner (90 s at most); results show for 8 s, then the next grid forms. Hold Tab for the
standings. Late joiners wait for the next grid. `boat_bot` races the course in race rooms. See
the [race plan](docs/RACE_PLAN.md#r2-network-race--2026-10-24).

In `--connect` mode the room decides the location, water, wakes and spawns. Your boat is predicted: it runs
ahead of the server with your own keys and is corrected when snapshots arrive, with small
corrections blended over about a tenth of a second. Every other boat is drawn smoothly
about 100 ms behind the server, and blends toward its predicted position when it comes within
about 12 m of yours or is about to hit you. Each room slot has its own country on every client:
1 Sweden, 2 Finland, 3 Denmark, 4 Norway (`--skin` does not apply). On exit `prediction:` and `other boat:` lines report
the corrections and how the other boat was drawn. Holding Backspace for one second or choosing REQUEST RESET asks the server for a new life at a safe spawn; `RESETTING` shows until it arrives (with
the wait when no spawn point is clear yet), and the view fades in at the new spot. On exit a
`resets:` line reports them. Help and focus loss make
the server ease your controls to neutral, but the room keeps running. Course, replay, wake, water,
location, demo and screenshot options are refused with `--connect`. Wrong tokens, version or
handling mismatches and full rooms are refused with a message. The
[protocol](docs/MULTIPLAYER_PROTOCOL.md) and the
[M3](docs/MULTIPLAYER_PLAN.md#m3-protocol-and-transport-handoff--2026-10-23) and
[M4 prediction](docs/MULTIPLAYER_PLAN.md#m4-step-1-client-prediction-handoff--2026-10-23)
handoffs describe the details and limits.

#### Hosting from the game

Since 2026-10-25, choose MULTIPLAYER > HOST A ROOM on the title, pick MODE, PLACE and WATER,
then START ROOM. The game starts a second copy of itself in the background as the room
(`--serve`), open to the local network on UDP port 24680, and joins it. Allow Wake Velocity
through the firewall when asked. The Esc menu's CLOSE ROOM ends the room for everyone; it also
closes if the game quits or crashes. From a terminal, the same is
`wake_velocity --host free|deathmatch|race [--location L] [--waves] [--port N]`.

To run a room without playing in it, double-click `host.bat` on Windows (it runs
`wake_velocity.exe --serve --bind 0.0.0.0` and passes extra options through) or, on a Mac:

```sh
"/Applications/Wake Velocity.app/Contents/MacOS/wake_velocity" --serve --bind 0.0.0.0
```

Friends on the same network choose MULTIPLAYER on the title. The room appears in its LAN row,
named after the host machine; Enter joins. If it does not appear (a firewall, a separate guest
network or a VPN), enter the host's LAN address with port 24680 under ADDRESS. The title has no
token field, so leave `--token` off rooms meant for it. Internet play needs UDP port
forwarding or a VPN; the [Winterfell hosting guide](docs/MULTIPLAYER_HOSTING.md) covers the
released native and browser room. A standalone Linux server package remains part of
[M6](docs/MULTIPLAYER_PLAN.md#m6--friends-over-the-internet-and-native-packaging).

For browser play, open [Winterfell](https://wake.davve.net/), press PLAY and
choose MULTIPLAYER → WINTERFELL. The browser fetches a known-server list, so the common
join needs no address entry. Manual URL / CONNECT remain available. The deployed client
and room now run v0.0.18 with both deathmatch weapons; the public host stays outside the
VPN's private Winterfell DNS override. Native clients fetch the same directory and use its
native addresses, alongside LAN discovery and manual joining. Routine hosting updates are
owned by `davve-srv`
([operator workflow](docs/MULTIPLAYER_HOSTING.md#routine-deployments-davve-srv)).

T5 tuning lets the bank ease out sooner as turning load fades. Deterministic presentation/
replay fixtures cover banked poses, straightening and paused seeking. The
[turn-banking plan](docs/BOAT_TURN_BANKING_PLAN.md) records the implementation and checks.

The [H3 carving pass](docs/BOAT_PHYSICS_PLAN.md#h3--stern-led-carving-feel-2026-10-20)
adds firmer hull grip as the boat planes: the outboard pushes the stern around while the
bow holds its arc with less sideways drift. The engine and hull retain physical response
time, and the bank follows the measured water forces. The user accepted the combined
banking/carving feel on 2026-10-20.

Capture the shared banking measurement fixtures at native 320x200 with
`./build/boat_banking_review --output build/banking-review/cpu`; add `--gpu` for GPU+AA
captures with desktop graphics access. The original six fixtures include default, close rear and side
views; three steering-release fixtures add default/rear views at 0, 0.5, 1 and 1.5 seconds.
Seven turn-entry phases (0–2 s) also have default and close side/rear views.
Running without options performs the headless attachment/spray/horizon checks.
`./build/validate_boat_banking --straightening` prints the focused recovery measurements;
`./build/validate_boat_turn_entry --report build/entry.csv` records the engine-led entry.
The replay validator also writes a physically driven banked S-course under its test directory;
`./build/wake_velocity --replay build/replay-tests/banked.run` plays it after CTest
(use the configured replay test directory if different).

Run `./build/wake_velocity --course` for the first timed S course in the basin. Cross the
start line northbound to start the clock, pass the two offset buoy gates in order, then cross
the chequered finish line to freeze the time. Yellow marks the next gate; the small route map
shows the boat and gate order. Cross between the posts in the course direction; missed gates
can be approached again, and the timer keeps running. Buoys are non-colliding markers.
Hold Backspace for one second or choose RESTART RUN in the Escape menu to reset the boat and timer. Losing focus pauses the timer with the
simulation. Course mode is basin-only; free boating remains the default.

Completed attempts are recorded automatically. The fastest completed run becomes your personal best
and appears as a cyan, stippled ghost on your next attempt. It waits at START, then follows its
recorded route against your course clock; it has no collisions, wake, shadows, or reflections.
The HUD shows your best time and the time difference at each gate (negative means ahead).
A deliberate reset abandons an unfinished attempt while preserving your last completed run and best.

- `G`: show/hide the ghost.
- `P`: watch the last completed run; `B`: watch your personal best. Available before START or after
  FINISH. Replays follow the recorded hull and camera heading; water particles are omitted.
- During replay, `Space` pauses/resumes, `Enter` restarts at the selected speed, and Escape > RESTART RUN returns to
  racing. Backspace reset is inactive during replay. Finishing pauses playback; `Space` then plays again from the beginning.
- `Left`/`Right` seek one second backward/forward; hold `Shift` for five seconds. `Home`/`End` jump
  to START/FINISH. Seeking preserves pause state; jumping to FINISH pauses.
- `[`/`]` select 0.25×, 0.5×, 1×, 2×, or 4× playback. Selecting another recording starts at 1×.
- Click or drag the replay timeline to seek and pause; press `Space` to continue. The timeline marks
  START, both buoy gates, and FINISH. Camera controls remain available. Losing focus pauses playback
  without advancing the clock.

Personal bests persist between launches, with separate files for calm water and `--waves`.
The browser version stores these in IndexedDB for the current site; clearing site data removes them.
On macOS they live in `~/Library/Application Support/Proteus/`; Windows uses `%LOCALAPPDATA%/Proteus/`,
and Linux uses `$XDG_DATA_HOME/proteus/` (or `~/.local/share/proteus/`). The files are
`course-calm-physics9.run` and `course-waves-physics9.run`. Use `--data-dir DIR` to select another
directory. Handling revision 9 (pedal response and automatic aerial stabilization) starts a separate set of bests; revision-8, revision-7,
revision-6, revision-5, revision-4, revision-3, revision-2 and original best files remain on disk and are not loaded or overwritten
automatically. Version-1/revision-5 recordings are rejected as replays/ghosts by this version.
Incompatible or damaged saved bests are ignored with a message. Failed saves preserve the best in
memory. Recording is limited to ten minutes per attempt; longer attempts can still finish, but
cannot replace your recorded best.

```sh
./build/wake_velocity --course --save-run build/my-run.run
./build/wake_velocity --replay build/my-run.run
./build/wake_velocity --course --ghost build/my-run.run
./build/wake_velocity --renderer cpu --replay build/my-run.run --time 5 --screenshot build/replay.png
```

`--save-run FILE` exports each completed attempt, including slower runs, to that path.
`--replay FILE` and `--ghost FILE` imply course mode. Replay selects the recording's water preset;
an imported ghost must match your selected water preset (`--waves` for a wave run).
An imported ghost stays selected across retries while your personal best is tracked separately.
Live simulation time now comes from an integer 120 Hz tick; the float physics step is unchanged.
Recorded world time is absolute session time, so a course started long after launch still saves
and loads.
Version-2 recordings store double-precision world time alongside poses at the 120 Hz simulation
rate, with exact START/FINISH boundary samples and gate splits, so playback does not rerun
physics. File, course, and physics revisions are checked when loading; bump `BOAT_PHYSICS_REVISION` in `src/boat.h` when changing handling or wave presets,
and the course revision in `src/boat_replay.c` when changing gates.
Screenshot `--time` accepts up to 600 seconds with `--replay` and clamps to the recorded finish;
other screenshot modes retain the 60 second limit.

`basin` remains the default. The river spawns in the Eriksberg dock facing the crane and river;
R returns there. The map, buildings, Dockbron, crane, and daytime sky come from the bundled offline
river crop.
Two authored berths now give the dock an inhabited edge: six generated leisure boats on a
floating pontoon along the west bank, and a workboat with quay equipment on the east bank.
Their hulls, fenders, cabin shapes, masts, and engines come from shared parameterized recipes.
They follow the gentle-wave surface, with attached mooring lines, reflections, and shadows.
Moored boats and pontoons block the player with forgiving contacts; the central channel stays open.
Inspect them directly with `./build/harbor_review --view marina --waves` or `--view workboat`.
See [generated boats and berths](docs/HARBOR_BOATS.md) for parameters, review commands,
and the foundation for future playable boat variants.

Three travelling leisure craft and a compact passenger ferry now share the waterfront. A yellow
launch loops inside the dock; a cruiser, motoring sailboat, and ferry follow separate river
circuits. They have visual wakes, navigation lights, reflections, shadows, and forgiving moving
hull contacts. Inspect `./build/harbor_review --view ferry --waves` or `--view launch`;
see [harbor traffic](docs/HARBOR_TRAFFIC.md) for routes and limits.

Across the river from the crane a white ro-pax ferry of the made-up Vinga Line lies docked at
the south bank, in Stena Line's colours at Stena Danica's 155 m, on both the river and `outer`. It rides the gentle waves on its
mooring lines, and its hull blocks boats and projectiles like the moored fleet
([A3.5e](docs/FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a35e-result-2026-10-27)).

River boating also has 32 gulls in four small flocks. They circle, dip toward
imagined food, follow the cruiser's disturbed water, and spread out with staggered alarm climbs.
They bank into turns, alternate individual wingbeats with gliding, and climb away from passing
vessels and your boat; faster approaches provoke a wider reaction. The basin stays clear for
handling practice. See [harbor gulls](docs/HARBOR_GULLS.md) for behavior and review commands.

Shoreline collisions use an oriented bow, stern, and beam footprint; glancing contacts scrub speed,
turn the craft away, and rebound gently while retaining most speed along the dock. Translation
and rotation keep the entire footprint in water, so a dock contact can be reversed out of. The
outer map boundary also blocks travel.
GPU rendering is the default for all locations; `--renderer cpu` selects the software renderer.
GPU rendering provides a depth-tested player hull, steerable engine, wake and spray, with
reflections and shadows.
The hull, engine and airborne spray cast shadows; surface foam receives them without casting solid
slab shadows. `--gpu-aa` only enables anti-aliasing: it averages 4×4 samples per pixel, retaining
the 320×200 integer-scaled presentation. It does not select a renderer or location. `--gpu` is an
alias for `--renderer gpu`. GPU screenshots require a desktop graphics context; use
`--renderer cpu` for window-free captures. Buildings and quays remain static; overhead
structures have no collision. Follow-camera occlusion near buildings is not resolved.
The river covers a local waterfront area, not all of Gothenburg.

`--location outer` starts at the same dock but loads the baked 16.5 km² first outer-river slice:
the river's water and shore joined to Älvsborgsfjorden, Nya Älvsborg and the run to the Långedrag
old harbour, with real land heights from Göteborg's laser-scanned Höjdmodell 2022 (see the
[adventure ledger](docs/FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a35b-result-2026-10-27)). The river's
buildings, roads and Älvsborgsbron are baked into it (A3.3); its houses, trees, street cars and
lamps stand on that terrain, so the outer river is hillier than the flat river location. The
river's own kit comes along too: the Eriksberg crane, the bridge deck, the cafe, street
furniture and quay fittings. Beyond the river (A3.5a) stand
the mapped buildings, the container cranes of the port, channel marks and scattered trees; there
are no roads there yet. Where the region cuts open water the water ends at the horizon and collision keeps
the boat inside.

Solo `outer` outings are a small voyage
([A3.4](docs/FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a34-result-2026-10-26)). A panel at the top
shows an arrow (the way to steer) and the distance to Långedrag's old harbour, about 6 km by
water. Arrive by entering the harbour basin at 6 m/s or less; the panel then shows the time
since the start. The dock, the river past the bridge and the outer fjord's east shore are
shelters: passing one makes it the recovery point, and holding Backspace for 1 s (or RECOVER
BOAT in the Esc menu) returns the boat there while the clock and water keep running. West of
the direct line down the outer fjord lies a fixed patch of rough water with rain. The direct
line crosses its flank, the east shore stays calm, and its middle has swells of several
metres. The patch is authored, not observed weather. Rooms on `outer` keep calm water and no
voyage. `--start X,Z[,DEGREES]` places the boat anywhere in a
mapped location (simulation X/Z; heading 0 faces -Z), for example
`--location outer --start 1980,-1196,75` on the open leg of the route.

`--location river` is a 2,450 × 2,400 m waterfront with an Älvsborgsbron blockout. Start in the
dock, leave it, and follow the river southwest to sail under the bridge and continue beyond it. Both bridge ends meet descending approach roads on grassy
embankments, with concrete abutments connecting the deck to the land. The approaches follow mapped
road alignments; their elevations are authored for the flat scene. The four mapped tower legs
block the boat; the elevated deck remains passable underneath. The river location defaults to a close follow view (12 m back,
6 m high) for a stronger sense of speed; C restores that view. Both renderers support it;
GPU is recommended for waves because the CPU reference tessellates the whole water footprint.
Geometry and height assumptions are in the [river manifest](assets/geometry/river/manifest.json).
See the [bridge survey and blockout notes](docs/river/README.md) for the route and
repeatable CPU/GPU captures.

Map data © [OpenStreetMap contributors](https://www.openstreetmap.org/copyright), ODbL 1.0.
See the bundled [source manifest](assets/geometry/river/manifest.json) for provenance.

Run `./build/wake_velocity --location basin` for the first handling prototype: a small craft in a simple basin,
viewed by a third-person camera following behind and above the boat. The basin is 240x160 world
units (four times wider and deeper than the original). The camera follows position immediately,
smooths heading changes, and looks ahead along the course. Hold left/right (or A/D) to turn the
wheel; release to let it spring smoothly back to centre without overshooting. The return is
stronger farther from centre, reaching within 1% of centre from full lock in about 1.5 seconds.
One second of held input moves it from centre to either limit. Opposite input unwinds full lock
in about a third of a second, then builds the new turn at the normal rate. This makes catching a
turn quicker while retaining progressive steering and hull inertia. Hold W/Up for forward thrust
or S/Down for reverse thrust; releasing both, or holding both, selects neutral so water drag
slows the boat. Reverse thrust also brakes forward travel. Normal power is 100% forward and
20% reverse; hold either Shift for 5% power in either direction for docking. The command moves
from neutral to full in 0.25 seconds, with separately smoothed engine response. Scrolling is
unassigned. R does nothing. Hold Backspace for one second, with a visible progress bar, or
choose the explicit reset/restart/request action in the Escape menu. A short tap does nothing;
release, help/menu, loss of focus or replay cancels progress. Each hold triggers at most once.
Space operates the water cannon in deathmatch, E drops a stern wave grenade, Q launches a torpedo, and V toggles the rearview mirror; Space is unassigned in free/race play; replay/menu controls retain their existing use.
Press `M` to capture the mouse for steering, or launch with `--mouse-steering`. The pointer
is hidden and horizontal movement accumulates wheel angle: move right/left to turn the wheel,
then stop moving to hold that setting. About 300 mouse pixels moves the requested wheel from
centre to full lock, independently of window size. Move back to straighten it; a deliberate reset centres the
wheel and selects neutral thrust. The steering needle shows the actual wheel as it follows the target smoothly.
M switches between mouse and keyboard steering. Esc never changes the steering mode: it opens
a menu and releases the pointer. Single player pauses while the menu is open; in a room the
match keeps running and your boat idles (neutral thrust, centred wheel), as with help or focus
loss. The menu offers RESUME, STEERING (mouse/keyboard), CONTROLS (the `?` guide) and BACK TO
TITLE / LEAVE ROOM (QUIT when started with explicit options; no leave row in browsers).
Esc or RESUME returns to play and recaptures the pointer in mouse mode.
Help and focus loss release capture. Desktop builds resume capture on return. Browsers
consume Escape themselves to unlock the pointer, so an unlock the game did not ask for (Escape
or focus loss) opens the menu; after resuming, click the canvas to capture again. Browsers that deny or lack capture show a keyboard
fallback message. Mouse mode is inactive during scripted demos and replay playback.

In arcade object-wake jumps, steering retains modest yaw but no longer commands aerial rolls.
Automatic roll damping/upright assistance remains active even with a held mouse turn. The
mouse wheel setting itself still holds; move back to straighten your course. Impacts and
inverted/bad landings can still capsize the boat. See the current
[stability behavior](docs/ARCADE_STABILITY.md) and [controls handoff](docs/CONTROLS_PEDALS_HANDOFF.md).
Hard landings now cost some speed (more when the bow digs in) and land with a splash and a
hull slap; the user liked this [landing feel](docs/BOAT_PHYSICS_PLAN.md#l1-candidate--2026-10-24).

The scene uses the full window with a compact [marine instrument panel](docs/BOAT_HUD.md)
showing actual commanded thrust, a speed needle, and port/starboard steering as visual ranges
without numbers or units. Its slightly translucent case sits at the bottom of the scene;
all HUD pixels share the scene's 320x200 grid and integer scaling. Press `?` for a
modal keyboard guide; `?` or Esc dismisses it. Boating and audio pause while help is open or the
window loses focus. The view remains 320x200 with integer scaling and letterboxing as needed.
Frames follow the display: vsync paces presentation, and an absolute-deadline cap just above
the monitor's refresh rate holds hidden or occluded windows whose swaps stop blocking. The
120 Hz simulation therefore takes one tick per frame on a 120 Hz display and two at 60 Hz.
`--no-vsync` paces to exactly 60 Hz instead; the title screen always uses vsync.
Press `F3` to toggle the debug profiler overlay, or pass `--profiler` to show it on startup.
Alongside the rolling frame graph, boating rows show simulation, physics tick
total/max and count, course/replay, audio, rendering/upload, present/wait, and active time
discarded by the 0.1-second catch-up clamp. Paused help or focus-loss frames show no discarded
time. Frame wall includes all sections; simulation includes physics and in-loop course work, so
parent and child totals overlap. The scene row retains its rendering/upload meaning. The boating
rows show latest, mean, p95, and maximum over up to 240 frames. GPU rendering time measures CPU
submission, not GPU execution; GPU mode uploads no CPU framebuffer.
The study mixes a quiet, low engine with soft hull-water wash. The engine follows thrust;
water follows hull speed and swells gently with live bow entry when using `--waves`.
Wash continues while coasting and quiets as the boat stops. The default 750–4800 RPM acoustic
voice is a sound-design setting; future craft can use higher-revving voices. Audio fades when
the window loses focus or a replay pauses. Replays have speed-driven wash but lack recorded
wave-contact swells. Background music plays independently of boat speed. The default,
“A little boat”, has soft keys, humming melody and rounded bass, repeating every 91.43 seconds
at 84 BPM without drums. “Acid Wake” adds an original 132 BPM acid techno loop with a
TB-303-style resonant bass, slides and accents over kick, clap and hats; its 64 bars repeat
after 116.36 seconds. Choose either track or Off under SETTINGS → SOUNDTRACK, or use
`./build/wake_velocity --soundtrack acid-wake` (`a-little-boat` and `off` are also accepted).
Music pauses on focus loss or replay pause. Off keeps engine and water sounds enabled.
The shared overlay's music-analysis readings remain off/zero. See [music notes](docs/MUSIC.md)
for regeneration and editing.

The view gently widens from 55° to 62° vertical field of view as forward ground speed rises
from 4 to 18 units/s. Reverse and docking keep the 55° view; `--fixed-fov` keeps it at all speeds,
including replay. The handling and speed-view pass was accepted by the user on 2026-10-19.
The horizon stays level and camera height stays fixed.

The follow camera defaults to 20 units back and 14 units above the water. Hold `-` to pull back,
`+` (or `=`) to move closer, and PageUp/PageDown to raise/lower the camera. `C` restores the default
view; a deliberate reset restarts the boat while keeping your view settings. Distance is limited to 8..48 and height
to 4..36. Settings last for the current run; reuse a preferred view with command-line options,
which also work for screenshots:

```sh
./build/wake_velocity --camera-distance 24 --camera-height 18
```

Steering angles the outboard at the stern. Its off-centre thrust pushes the stern sideways and
builds rotation; speed-dependent resistance at the bow and stern damps sliding and turning. The engine follows
the wheel setting. Release steering to let the wheel centre, or countersteer to catch a turn. At idle,
turning the engine cannot start a turn from rest. Propeller wash shows the water being pushed in the
opposite direction to thrust. This is a simplified planar model with tunable forces and inertia.

While coasting, water flowing across the angled outboard creates a passive steering force.
It fades as the boat slows; no flow means no coasting steering. Forward drag includes a quadratic
term, while hull grip grows with forward speed and local sideways slip. Try building speed,
lowering the throttle to 0%, and holding a modest wheel angle through the coast. These are tunable
approximations for calm water. Above a forward speed threshold, a rate-limited planing blend
gradually reduces hull resistance; it disengages at a lower speed so it cannot chatter around the
transition. Reverse never planes. The bow rises during the transition and settles to a small
cruise angle; wake and spray strengthen with planing. Six damped buoyancy supports provide physical
heave, pitch, and roll, shared by CPU/GPU hull, cabin, outboard, reflections, and shadows. Planing
changes the support distribution rather than adding a cosmetic tilt. The follow camera keeps a
level horizon and does not follow the hull’s bobbing.

The basin walls bounce the craft back gently; the two orange markers are visual turning references
and have no collision yet. Varied foam traces your path and fades; short-lived stern wash
separates and erodes as it travels aft. Hard turns throw spray
according to sideways slip. Reset clears both effects. The default basin has no geographic scenery; `--course` adds timed gates, and the river provides mapped scenery.

Bounded arcade wake objects are the default in basin and river free boating:

```sh
./build/wake_velocity --location river --profiler
./build/wake_velocity --demo
./build/wake_velocity --no-player-wake
./build/wake_velocity --player-wake
```

Tall crests travel outward, live for sixteen seconds and add together at crossings.
Visible geometry, normals and hull response sample the same objects. Short-lived foam
and front spray remain. `--no-player-wake` selects the visual-only player trail and,
without other water options, flat still-water practice. `--player-wake` explicitly selects
the legacy D3 model: twenty-second travelled-path packets with 3 units/s crests and
1.6 units/s envelope spread. `--wake-objects` explicitly selects the new default.
The default demo accelerates, turns into a loop after 3.5 seconds, crosses its older trail,
and returns to neutral after 12 seconds. R clears the wake world and restarts the demo.
Courses, saved replays and ghosts retain their original water settings automatically;
explicit player-wake modes are rejected in those modes to preserve existing comparisons.

A native, still-water basin candidate is available with `--wake-field`. Use an optimized build for play review (the Debug field is substantially
slower):

```sh
cmake -S . -B build/review -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/review --target wake_velocity -j
./build/review/wake_velocity --wake-field --profiler
```

Drive a straight run, turn back across the trail after 10–30 seconds, then try tight loops,
oblique crossings, reverse, neutral recovery and landings. A deliberate reset restarts the boat and explicitly
clears the field; `--demo` provides a scripted loop. Start without `--gpu-aa`: that option
renders 1280x800 rather than 320x200 (16 times as many pixels). The complete
wake exceeds its provisional 2 ms/frame target; [candidate results](docs/WAKE_CANDIDATE_RESULTS.md)
record measured cost, storage and limitations. This flag is rejected for mapped locations,
current/scripted-source options, courses/replays and the browser. The full-speed source was
strengthened after initial user feedback; the window identifies `REVIEW 3`. The user reports
improved feel. [Performance and large-water scaling](docs/WAKE_SCALING_RESULTS.md) are now
historical field-track evidence; this candidate still simulates only the bounded basin.
The field candidate remains opt-in.

The default **arcade wake objects** build tall, rideable jump waves:

```sh
./build/review/wake_velocity --wake-objects --profiler
./build/review/wake_velocity --wake-objects-crossing --profiler
./build/review/wake_velocity --wake-objects --wake-height 1.2 --wake-life-ticks 1440
```

The default crest scale is 1.2 m and pieces live for 1920 ticks (16 seconds). Use
`--wake-height 1.8` to compare the earlier taller setting. Nearby
pieces add directly, so crossings can produce taller waves. Half-second emission, smoothed
strength and age-aligned fading reduce visible packet joins; the sixteen-second lifetime
is retained. `--wake-height` accepts
.1–3 m; `--wake-life-ticks` accepts 240–1920. A deliberate reset clears the shared wake world and
restarts the optional crossing boat. The extra craft follows a basin loop and shares
the same water/contact queries; it does not add hard hull collision with the player.
Ordinary object mode also supports the river, waves and current. Course/replay/ghost,
field mode and explicit D3 `--player-wake` combinations are rejected. The HUD identifies
object mode. [Object-mode results](docs/WAKE_OBJECT_RESULTS.md) own behavior, performance,
verification and remaining issues. The user selected this as the default after enjoying
the continuity revision; occasional waves appearing stuck remain an open issue.

Currents and the separate scripted crossing wake remain opt-in in **both locations**:

```sh
./build/wake_velocity --current
./build/wake_velocity --location river --current --crossing-wake --waves --gpu-aa
./build/wake_velocity --no-player-wake --current --crossing-wake --demo
```

`--current` adds an authored smooth channel with drifting surface streaks. Water-relative
speed drives resistance,
planing, wake/spray, and water audio. The basin channel is centred on simulation X=0;
the mapped channel is centred on X=-12. These are test flows, not measured Gothenburg currents.
`--crossing-wake` adds a separate scripted boat for the first 12 seconds after launch/reset.
In the basin it crosses X=-24..24 at Z=-8; in mapped locations it crosses open river
X=-164..-116 at Z=-228, away from the dock spawn. Its wave packet fades completely by
24 seconds. The source has no hull contact or traffic AI. Surface streaks are masked to
mapped water, and the same translated source drives the visible boat and physical wave.
R restarts the passage. With `--no-player-wake` and either test option, the demo waits six
seconds, approaches, and returns to neutral after ten seconds. Screenshot time uses the
same selected demo script. Current/scripted-wake options reject courses/replays/ghosts.
See [Phase D](docs/BOAT_PHYSICS_PLAN.md#phase-d--currents-and-crossing-wakes) for
verification and the provisionally accepted handling baseline.

Opt in to gentle crossing waves with `--waves` (all locations, CPU or GPU).
Ambient water remains flat by default; moving player boats now generate their own wakes.
Three low wave components drive the optional ambient surface:
16/9/5.5-unit wavelengths and 0.13/0.06/0.025-unit amplitudes, with different travel directions.
The hull responds to the same water query that drives the rendered surface; foam follows the
water and spray starts at the moving hull. GPU water has restrained reflections distorted by the
physical surface normals. Quay walls extend two units below the rest waterline so troughs cannot
expose gaps beneath the architecture. CPU water uses simple, subdued slope shading without scene reflections.
For a closer view of the motion:

```sh
./build/wake_velocity --waves --camera-distance 12 --camera-height 6
./build/wake_velocity --waves --location river --gpu-aa
```

With waves enabled, the six hull supports resist motion into and out of the local water surface.
Faster crossings produce more resistance and bow spray; easing off reduces the encounter load.
Try the dominant wave train head-on, across the beam, and following it. The camera stays level,
and the same flat-water steering/coasting response remains available without `--waves`.
Foam follows both water height and slope; spray inherits the moving hull’s vertical velocity.

This completes C3 of the [physics plan](docs/BOAT_PHYSICS_PLAN.md). The model uses vertical support
springs plus damped motion normal to the local surface, with a modest speed-dependent gain.
It does not model hull volumes, orbital water flow, ventilation, or capsizing. Moving waves can
supply energy; resistance dissipates energy relative to their local velocity. GPU reflections
still use a planar source, and the two-unit surface mesh approximates analytic heights between
vertices. `--demo` adds the scripted acceleration/turn/reverse sequence; reset restarts wave time.

The [boating plan](docs/BOATING_PLAN.md) tracks gameplay direction. The
[free-riding adventure plan](docs/FREE_RIDING_ADVENTURE_PLAN.md) proposes staged world expansion
from the river toward Vinga, with data sources, tiled scenery and performance gates; that
expanded geography is not implemented yet.

For display-free captures of scripted acceleration, planing, and a right turn, or a windowed check:

```sh
./build/wake_velocity --screenshot build/boat-start.png
./build/wake_velocity --screenshot build/boat-transition.png --time 1.8
./build/wake_velocity --screenshot build/boat-cruise.png --time 2.9
./build/wake_velocity --screenshot build/boat-turn.png --time 4
./build/wake_velocity --frames 180
./build/wake_velocity --demo --frames 720
./build/wake_velocity --location river --gpu --gpu-aa --demo
```

`--demo` previews the same scripted run as screenshots: raise thrust for two seconds, hold it to
show cruise, turn after three seconds, then lower thrust after four seconds through neutral into
reverse. R replays it.
Screenshot `--time` selects a point in that run (0..60 seconds). The water effects use the same
fixed simulation ticks as handling.

## Dance study

For a procedural house-inspired shuffle, run `./build/dance_study`. Edit
[`assets/dance/house.dance`](assets/dance/house.dance) to arrange named phrases, then press `L`
to reload. The default pairs fast footwork with slower torso and arm phrasing. Compare the
relaxed version with `--choreography assets/dance/house-groove.dance`, or use `--classic`
for the earlier cross-and-reach study. See the
[dance study notes](docs/DANCE_STUDY.md) for the language, controls and screenshots.

## GPU checks

GPU checks are deliberately opt-in because they need a desktop graphics context. Configure them on
a machine that can create a raylib/OpenGL window, then run the labelled CTest cases:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DPROTEUS_GPU_TESTS=ON
cmake --build build -j
ctest --test-dir build -L gpu --output-on-failure
```

If no graphics context is available in an automated session, these tests fail. Ordinary CTest
does not include them and remains display-free. `gpu_fixture` checks render target dimensions,
orientation, depth, winding, normal submission, shader rejection and, with `--city --check`,
sky coverage and sampling against the CPU panorama at native and supersampled sizes, with a
resized window and shadows/reflections toggled independently.

The GPU waterfront enables its directional shadow map, distance atmosphere and a 160x120
reflected water pass by default. The focused fixture renders the city from a fixed dock view and
can switch each off, compare shadow resolutions and export the light-frustum depth map:

```sh
./build/gpu_fixture --city --time 5 --shadow-resolution 512 --screenshot build/shadow-512.png
./build/gpu_fixture --city --time 5 --shadow-resolution 1024 --screenshot build/shadow-1024.png
./build/gpu_fixture --city --time 5 --shadow-map --screenshot build/shadow-depth.png
./build/gpu_fixture --city --time 10 --reflections off --screenshot build/reflection-off.png
./build/gpu_fixture --city --time 10 --atmosphere off --screenshot build/atmosphere-off.png
```

The sunny morning sky fades from clear blue to a pale cyan horizon, with cream-white
flat cloud banks built from square tiles and a warm sun. The sun direction also drives city shadows
and water highlights. CPU and GPU share a fixed world panorama, so the sky follows camera rotation
naturally; GPU water reflections use the same sky through the mirrored camera.

The `wake_velocity` profiler (`F3`) keeps a rolling 240-frame history (four seconds at 60 FPS, two at 120)
and graphs total frame wall time, scene rendering plus texture upload, and presentation time
(including any vertical-sync wait). It reports rolling min/average/95th-percentile/max timing,
display dimensions and framebuffer traffic. Samples and the graph update every frame, while the
numeric readout refreshes four times per second so it remains readable. For repeatable headless
measurements, see the [boating performance guide](docs/BOATING_PERFORMANCE.md).

With full Xcode selected as the active developer directory, Instruments can record a Time
Profiler trace of a windowed scripted drive:

```sh
xcrun xctrace record --template 'Time Profiler' --output build/river.trace \
  --launch -- ./build/wake_velocity --location river --demo --frames 3600
```

## Music

The boating game plays the XM modules in `assets/music/`; see the
[music workflow](docs/MUSIC.md). The starter XM remains an archived sketch and is still used by
the separate dance study.

## Editor setup

See the [Emacs development guide](docs/emacs.md) for `c-ts-mode`, Eglot,
clangd, formatting, compilation, navigation, and LLDB debugging.

## Browser build and GitHub Pages

The [live browser game](https://doomdavve.github.io/proteus/) is the playable `wake_velocity`
distributed in the native ZIPs. The landing page shows its build version (release tag or
Git development description). It includes
River, Basin, and the basin time trial with ghosts/replays and saved bests.
Development tools remain native targets.

Install CMake 3.24+, Python 3, and the [official Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html).
Use the SDK version pinned in `web/emsdk-version` (currently 6.0.12), then activate it in
this terminal. For an SDK checkout at `../emsdk`, from the Proteus repository root:

```sh
../emsdk/emsdk install "$(cat web/emsdk-version)"
../emsdk/emsdk activate "$(cat web/emsdk-version)"
source ../emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-web --target web --parallel
cmake --build build-web --target web-serve
```

Open **http://127.0.0.1:8000/** and press **Play** to start with sound. Stop the server
with Ctrl+C. Serve `build-web/site/` over HTTP; opening its HTML as a local file will
not work. The CMake targets provide the build/serve shortcuts without a second build system.
Keep native builds in `build/`, using the commands above.

The **Web boating / GitHub Pages** workflow builds and deploys when you push a `v*` tag,
alongside the existing native Release workflow. Ordinary branch pushes do not update the site.
Set up GitHub once:

1. Select **Settings → Pages → Build and deployment → Source → GitHub Actions**.
2. Under **Settings → Environments → github-pages → Deployment branches and tags**, if
   deployment restrictions are enabled, use **Selected branches and tags** and add a
   **Tag** rule for `v*`. Retain/add a **Branch** rule for `main` for manual deployments.
   A branch rule named `v*` does not authorize tags.

Commit and push the web support before tagging, so the tagged commit contains the workflow
and all its build inputs. For example, using a new version:

```sh
git tag -a v0.0.6 -m "Proteus boating v0.0.6"
git push origin v0.0.6
```

This starts the browser build/deployment and native ZIP release independently. Any `v*` tag,
including a prerelease tag, updates the same site; the last successful deployment wins,
without comparing version numbers. Manual runs deploy from `main` or a `v*` tag; other
branches only build. To deploy manually, first commit the workflow to the default branch.
The deployment job reports the site URL (normally `https://doomdavve.github.io/proteus/`).
Existing native build and Release workflows are unchanged. Only `build-web/site/` is published;
generated files stay out of Git. Deployments use content-hashed JavaScript, Wasm, and asset
URLs, with no service worker; reload to load the deployed version. GitHub's CDN can briefly
cache the previous HTML after a deployment.

A desktop browser with WebAssembly, WebGL 2, and a keyboard is required. The canvas uses
nearest-neighbour scaling in multiples of the 320x200 scene, with a 320x200 minimum.
Press **PLAY** to start audio and open the shared Wake Velocity title. The page's small
**Settings** toggle remembers fullscreen; in-game SETTINGS remembers name, livery, steering,
soundtrack and smoothing. SMOOTHING defaults to ON and applies the same 4×4 supersampling
to single-player and multiplayer, including WebRTC rooms. Turn it OFF in SETTINGS on slower
devices. SINGLE PLAYER selects River, Basin or Basin trial, waves and the same smoothing setting.
The launcher disappears during play; the game uses integer scaling. Press `?` for controls
and Escape for the in-game menu (the browser may also leave fullscreen). BACK TO TITLE
returns to setup without reloading. M enables captured mouse steering. W/S or Up/Down apply forward/reverse thrust while held;
release selects neutral and Shift selects precision power. Scrolling is unassigned. After help or RESUME, click the canvas to recapture. If capture
is unavailable, keyboard steering stays available. MULTIPLAYER joins an operator-provided
WebRTC room URL; browser hosting and LAN discovery are unavailable. Browser throttling
can affect animation/audio in background tabs. Bests are local to this browser and site,
can be lost when storage is cleared or unavailable, and do not sync with native saves.
Local recording import/export and native screenshot/profiling commands are not exposed by
the page. Touch controls are not implemented. See [web build details and verification](docs/WEB_BUILD.md)
for the implementation trade-offs and remaining browser checks.
