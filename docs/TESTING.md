# Test workflow and timing

Multiplayer M2's local authority and M3's protocol and transport have display-free checks
(see [below](#core-boundary-gate) and [network checks](#network-checks)). The M3 suite covers
the codec, sessions over a seeded in-memory network (including profile F one way) and a real
ENet loopback process test. Still [planned](MULTIPLAYER_PLAN.md#verification-matrix-and-telemetry):
the full impairment matrix, an OS-level impairment lane, prediction/collision feel gates and
long-session/four-player stress.

Status, 2026-10-22: the user requested a quick routine suite and suspension of the
retired physical wake-field tests until that research resumes. Arcade wakes, handling,
course/replay, environment, audio and renderer checks remain active.

The cosmetic player skin check `boat_skin` runs `validate_boat_skin`: matching geometry
across liveries, valid triangles, custom palette generation, footprint and ID fallback;
national flag field/cross colors, Norway's white border and independent render storage.
It is device-free; visual review commands live in [the skin guide](BOAT_SKINS.md).
`boat_fleet_render` runs `boat_fleet_review --check`: a four-boat fleet drawn from slot 0 with
three rivals through the shared scene call; each rival must change the CPU image, a repeat must
be identical and drawing must not change the fleet or presentations. For a look, run
`./build/boat_fleet_review --renderer gpu --waves --time 4 --screenshot build/fleet.png`.

`boat_handling` (`validate_boat`, `cpu`) also checks view-owned water-mesh caches
(adventure A1.3). Two river caches at different patch centres and a basin cache are held
together, one is destroyed and rebuilt, and a cache rebinds between the river and a synthetic
pond map. It also checks the water index (A2.4): indexed patches equal full-scan ones around
index seams and corners, and a view-bounded CPU surface is an ordered subset of the full one.
Every mesh must match its reference built alone, and the 5.99/6.01 m snap-boundary
pair must not disturb another cache's borrowed meshes.

`boat_world_isolation` (`validate_boat_world`) also checks CPU view binding (adventure A1.4):
two river views and a view of a synthetic harbor map draw interleaved, and destroying one view
or rebinding another reproduces every frame exactly. `gpu_view_binding`
(`boat_water_review --check-gpu-views`, `gpu`, serial) does the same with two GPU views,
including destroy and recreate with the context alive.

Adventure A1.5 adds the lifecycle gate. `boat_world_isolation` runs five scripted owners
(basin, two river owners with different water, camera and steering, the synthetic harbor and a
second basin), each with its own world resources, session, CPU view and recorded run. Each runs
drive, respawn, restart (the basins start the course first) and a replay viewed from a playback
world on the same owner, with the live boat as its ghost. Every phase's physics and frame
hashes must match the owner's solo reference while one slot reloads basin → river A → basin,
river B and the harbor run alongside, and A is released mid-run. A failed init over a stale
owner and double destroys are checked too. `boat_map` releases a fleet's owner and checks that
a fleet on a surviving owner continues like one restored onto a fresh owner, and `boat_net`'s
`prediction_owners` releases one room's owner while the other prediction replays exactly.
`gpu_view_lifecycle` (`boat_water_review --check-gpu-lifecycle`, `gpu`, serial) renders two
private river owners (one with an arcade world, so both patch caches build), the harbor and the
basin. Shared views must reproduce the fresh-view references interleaved, after the first
owner and its view are destroyed, and over three view recreate cycles with the context alive.
`tile_culling` (`boat_water_review --check-culling`, `gpu`, serial, about 9 s; adventure A2.3)
renders 144 river poses (512 m and 1 km tiles, six route stops, four headings, default, close
and overview cameras) culled and unculled, on the GPU with waves and on the CPU on flat water.
The pixels must be identical, and every pass must cull at least one tile.

The original HUD/control worktree checks below remain legacy numerical fixtures; live pedal
controls are verified in [the current controls section](#pedal-controls-verification).
It also checks all 49 transitions between the seven thrust detents
through the real integrator in `boat_handling`, and forward/reverse target settling on both
the authority and predicted client in `boat_net`. Mouse steering adds 49 wheel transitions
with both float and quantized rates, sustained target holding and virtual network target
checks. Relative mouse mapping covers accumulated movement and lock reversal; scroll thrust
covers fractional/multiple-step input, neutral crossing and overscroll reversal; held-key thrust
covers the 0.5 s repeat, re-press and limits. See [the HUD guide](BOAT_HUD.md) for power
settings, visual fixtures and the remaining manual checks. When Node.js is installed,
`web_mouse_controls` runs the browser event-contract tests without a browser. Real Wasm
checks use the opt-in [Playwright command](WEB_BUILD.md#mouse-and-scroll-qualification-2026-10-24).
`web_rtc_transport` checks browser network bounds and reconnect callbacks; `boat_net_mux`
checks colliding peer IDs, routing and disconnect isolation across two transports. Browser
title/persistence and mixed WebRTC/ENet gameplay use the opt-in
[title and multiplayer checks](WEB_BUILD.md#webrtc-rooms), with the optional native endpoint.

## Pedal controls verification

The isolated controls branch adds all eight pedal/precision key combinations, 0.25-second
command response, precision transitions, coasting versus reverse braking, reset cancellation,
one-trigger-per-hold and invalid/stalled-frame cases to `boat_handling`. `boat_net` verifies
quantized target settling within one command quantum and deliberately seeds an inverted
checkpoint so safer steering does not remove capsize replay coverage. The older calm-water
reference scales its authored rate input to preserve its physical travel and hydrodynamics.
Existing detent/scroll helpers remain numerical/review fixtures, not live controls.
[Browser pixel checks and integration evidence](CONTROLS_PEDALS_HANDOFF.md).

## Routine checks

Boating music checks are `boating_xm_is_valid` (A little boat) and `acid_xm_is_valid`
(Acid Wake): fresh decoder startup, seeking, finite unclipped PCM and two loop boundaries.
The Acid Wake check renders 235 seconds at 132 BPM. These do not establish listening
acceptance or menu interaction; see [boating music evidence](BOATING_PLAN.md#selectable-soundtracks--2026-10-24).

The [arcade stability candidate](ARCADE_STABILITY.md) extends `wake_objects_contracts_jumps`
with held-turn automatic stabilization, neutral/held recovery equivalence, dry-flight momentum and inverted
capsize checks. Its encounter envelope covers both the earlier 1.8 m stress setting and
the new 1.2 m default; the old 1.8 m geometry/derivative fixtures remain explicit stress tests.
`boat_clock` retains per-tick equality across start times, with emission/landing/expiry
milestones rather than a physical-recipe-specific birth count. `boat_net` reports smooth
and contact-adjacent remote errors separately: below 5 cm in smooth motion, 15 cm within
four ticks of a recorded pair impact, and below 5 mm mean. Snapshot interpolation cannot
reproduce the precise tick of an impulse; [candidate evidence](ARCADE_STABILITY.md) records
the observed outliers. This does not change remote rendering or protocol bytes.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build -j 4 --output-on-failure
```

`PROTEUS_WAKE_FIELD_TESTS` defaults to `OFF`. Fifteen registered experiments are marked
disabled, covering the owned field, refinement, cadence, candidate sources/boats/GPU,
compact packets and Python prototypes. CTest lists them as disabled, not failed. Their
sources, targets and manual diagnostic commands remain available. All arcade-object tests
stay enabled. With `PROTEUS_GPU_TESTS=ON` and Node.js available, this HUD/control worktree runs 103 active
checks as of 2026-10-24 (including the browser event contract; counted by the current CTest run). GPU cases retain serial scheduling while independent
CPU checks may run concurrently; fixture dependencies still govern capture/replay comparisons.

The river frame benchmark has two checks in `tools/validate_boat_water_benchmark.py`.
`boat_river_benchmark_options` (`cpu`) checks option and output-path rejection without
opening a window. With GPU tests enabled, `boat_river_benchmark_contract` (`gpu`, serial;
about 32 s in Debug on an M1 Max) runs a hidden `--waves --aa off` voyage. It checks row
counts, warmup flags, tick continuity, recomputed percentiles, deadline counts, finite values,
route completion and exact final-state parity with `--check --waves`. Since A2.1 (schema 2)
it also checks that the per-pass and per-group submission/triangle columns partition the
totals, apart from the two sky quads. Since A2.3 (schema 3) it also checks the 1,024 m
default `tile_size`, the per-pass `*_tiles_culled` columns and that the shadow pass culls
tiles on every row. It asserts no timing thresholds. See [the benchmark guide](BOATING_PERFORMANCE.md#river-frame-benchmark--2026-10-25).

`frame_pacer` checks the window frame pacer's deadline chain on synthetic clocks: exact
period boundaries despite oversleep, repayment of a late frame, restart after a stall, and no
waiting under a VSync-rate backstop. Option checks also reject a stray or unknown
`--pacing`. Real cadence needs a visible run; see
[frame pacing](BOATING_PERFORMANCE.md#frame-pacing--2026-10-25).

Every active test carries one of three labels: `cpu` (display-free), `render` (CPU
captures and checksums) or `gpu` (graphics context). Skin geometry carries both `cpu` and
`render` labels. The `core` checks are also
`cpu`:
- `boat_core` runs `validate_boat_core`, which links only the boating core library.
- `boat_map` runs `validate_boat_map` (adventure A1.1/A1.2). It checks explicit map
  descriptions, caller-owned collision caches, failed/zeroed preparation, two river owners and a
  synthetic map alive together, and destroy/recreate. It also matches a river query digest
  recorded before the ownership change. `BoatWorldResources` lifecycle and validation are
  covered too. Worlds on separate owners step interleaved with bit-identical results, and
  fleet copies and checkpoint restores keep borrowing their owner.
- `boat_map_index` runs `validate_boat_map_index` (adventure A2.2, about 14 s in Debug). It
  compares the indexed collision queries at 8, 16 and 32 m cells with the exhaustive scan,
  bit for bit, on about 3.6 million queries. These cover a cell-seam lattice, shore offsets,
  tile seams one float either side, tile corners, random and far points. It checks the 512 m
  and 1 km tile catalogues' lists, land ownership and bounds, and reports index, tile and
  candidate counts. Since A2.3 it also checks that the renderers' land-only catalogue equals
  the full one, and that land-only construction rejects bad input.
- `boat_map_scaling` runs `validate_boat_map_scaling` (adventure A2.5, about 5 s in Debug). It
  copies the river into synthetic corridors of up to 16 copies (25 km at 5), and requires the
  first copy's collision answers, tiles, water patches and windowed CPU water to be identical
  at every size, visiting the same candidates, with a water cache that does not grow. Run it
  with `--report` for build costs, per-operation times and memory per owner.
- `boat_outer_map` runs `validate_boat_outer_map` (adventure A3.1, about 1 s in Debug). It loads
  the baked first outer-river slice (`assets/geometry/outer/`, regenerated offline by
  `tools/prepare_outer_map.py`), requires every A0.3 route waypoint and sampled leg to be water
  with the 8 m envelope, compares the indexed queries with the exhaustive scan on 20,000 points,
  and checks that points outside the region and on the quay are not water. Since A3.1b it
  also requires real relief: land tops never below 1 m, hills between 60 and 100 m, and at
  least 100,000 triangles.
- `boat_voyage` runs `validate_boat_voyage` (adventure A3.4, under 1 s in Debug). It checks that
  the outer voyage's shelters and harbour are navigable, that the exposed patch crosses the A0.3
  route's southern leg while a coastal line, the shelters and the harbour stay outside it, the
  shelter/exposure/arrival/recovery events, the bearing cue's sign against real right steering,
  and two autopiloted sessions (coastal and direct, from the fjord turn) that must arrive at
  controlled speed. It prints both times, the exposed seconds and any capsizes.
- `boat_core_boundary` runs `tools/check_core_boundary.py` over the core sources and archive,
  and `boat_net_boundary` does the same for the core and net libraries together.
- `boat_fleet` runs `validate_boat_fleet` (multiplayer M2 fleet step, contacts, spawns and
  checkpoints), and `boat_server_scripted` runs the scripted authority for 2400 ticks.
- `boat_race_trial` runs the [R0 ferry fixture](RACE_PLAN.md#r0-prototype--2026-10-24):
  source derivatives, bounded support/clock translation, source hull clearance/contact,
  four-emitter fleet restore, speed/angle/player-wake overlap and timed route/arrival sweeps.
  `boat_race_trial_gpu` samples both production GLSL source recipes against CPU height,
  slopes and vertical velocity when GPU tests are enabled, then verifies the mapped ferry
  contributes pixels to the main, reflection and shadow passes with identical physical water.
  It also samples both production storm recipes against CPU height, gradients and vertical velocity.
  The fixture also checks independent physical scenery emission/restore and visible trail
  bounds; `wake_objects_gpu` checks both object pools beyond slot 127, independent lifetimes,
  exact uploads and 4/8 KiB texture-resize round trips.
- `boat_race` runs `validate_boat_race` ([race R1](RACE_PLAN.md#r1-race-core--2026-10-24)):
  skill-gate endpoints and centre/outer-lane shoreline clearance, a full-width finish,
  mandatory line changes through consecutive openings (including edge cuts), compact storm
  support, analytic derivatives (including patch translation), clock translation and storm
  rendezvous/speed, swept/reverse/lateral/multi-gate crossings, skipped-gate
  finish rejection, held grid through a full-throttle countdown and a same-tick release,
  same-tick places and exact ties,
  recovery behind the last cleared gate, joiners, leavers, abandonment, deadlines and grid
  rotation, and finisher parking (R2). It ends with a four-bot ferry race with a mid-race
  checkpoint restore; finishers must take 45–85 s, and one bot may DNF (see below).
- `boat_match` runs `validate_boat_match` ([deathmatch](DEATHMATCH_PLAN.md) credit window,
  self-capsize, grace, respawn, bailout, leavers, rounds and planted-wake capsizes), and
  `boat_server_scripted_deathmatch` runs a scripted 10 s round through its intermission.
- `boat_net`, `boat_net_loopback`, `boat_net_deathmatch` and `boat_net_race` are the
  [network checks](#network-checks).
- Twelve argument checks expect a failure: `boat_server --scripted` with a room option, an invalid
  `--port` and an invalid `--bind`, `--round-seconds` without `--mode deathmatch`, `boat_bot`
  without a port, `boat_bot --discover` with `--connect`, `wake_velocity --connect` combined with `--waves` or `--skin`, `--name`
  without `--connect`, `wake_velocity --host race --location basin`, `--host` with `--connect`, and
  `--port` without `--host`.
- `wake_velocity_serve_scripted` runs the scripted authority through `wake_velocity --serve`, the
  game's own room mode.
Select a group with `-L`, for example `ctest --test-dir build -L core`; CTest adds fixture
setup tests that a selected test requires, whatever their label.

The former demo's `cube_render_checksum` (an exact opening-frame CPU baseline) was removed
with the demo on 2026-10-25; no test now pins a rendered image hash.

`PROTEUS_OPTIMIZE_TESTS=ON` compiles the long numerical validators and the shared boating
libraries (core, render, session and drive) at `-O2` even in Clang/GCC Debug builds. The
validators are boat handling, banking, turn entry, environment, replay, clock, world isolation,
core, fleet, scripted server, network library and validator, water routes and object-wake
review. Debug symbols and assertions remain enabled; there
is no fast-math, reduced simulation duration or sampled-down scenario coverage. Because
validators and applications link one copy of each library, Debug `wake_velocity` also runs this
optimized boat code; the GPU library and application sources keep the selected build type. For
stepping through these validators or library code:

```sh
cmake -S . -B build -DPROTEUS_OPTIMIZE_TESTS=OFF
cmake --build build -j
```

Clang/GCC builds, raylib included, compile with `-ffp-contract=off` (since 2026-10-26), so no
compiler fuses multiply-adds on its own and the `boat_map` and `city_dressing` digests match
between Apple Silicon macOS and x86-64 Linux. Explicit `fma()` calls are unaffected. MSVC does
not contract by default.

The optimization override currently applies to Clang/GCC only. MSVC keeps its selected
configuration; Windows timings have not been measured. GPU checks are opt-in through
`PROTEUS_GPU_TESTS=ON` and need a desktop context. A display-free suite with GPU checks
off has less coverage; report that omission.

## Network checks

`boat_net` runs `validate_boat_net`, which links only the core and net libraries and takes
a few seconds. It covers:

- golden bytes and pinned snapshot layout, all truncations, invalid fields, and checkpoint
  round trips that resume bit-identically;
- seeded in-memory rooms: handshake rejections, the input-loss policy, command idempotence,
  sequence wrap, a flood against the bounded queue, profile F one way for 60 s, blackouts and
  timeouts, and wake resync;
- client mirrors that reproduce the authority's `boat_fleet_hash`;
- client prediction: replays from mid-flight, landing and capsized checkpoints that match the
  authority bit for bit, and predicting rooms (steady 30 ms, profile F both ways with smooth
  and keyboard-style bots) that print correction sizes and replay cost;
- wake reconciliation: digest-confirmed carry of missing births, held and incomplete
  corrections, early births by identity, and gated wake-change counts in the predicting rooms;
- remote view: interpolation, life and teleport rules, extrapolation limits and proximity
  hysteresis, plus the drawn delay and position error against the authority in the rooms;
- LAN discovery: query and answer bytes, truncations and invalid answers, and the search
  list's merge and expiry;
- a deathmatch room: every client, including a mid-round joiner, holds the
  authority's scoreboard through capsizes, a round end and a leaver, and names both boats of a
  credited capsize in its kill feed;
- M5 four-player profile F (seed 51), impaired from the handshake onward: bootstrap arriving
  before Accept, late join, room-full rejection, simultaneous resets, inactive input, 1 s
  blackout recovery, 6 s timeout, slot reuse and stale-life rejection. A lossless tail requires
  all surviving mirrors to match the authority at their snapshot ticks exactly.

`boat_net_loopback` runs `tools/boat_net_smoke.py`, which starts real `boat_server` and
`boat_bot` processes on a free 127.0.0.1 UDP port for about 10 s. Two bots drive and a
wrong-token bot is refused. `boat_bot --discover` on a second free port (the room's
`--lan-port`) must find the room once, with its token flag and player count. A second server on
the same port must fail, and the message,
datagram and MTU bounds plus a bounded exit are checked. Each bot must send commands for over
half of its ticks, and the server must use more than 85% of that share (`received_ratio` against
command coverage). Busy CI runners deschedule processes for 50-200 ms, and commands for ticks the
server has already run cannot be used; a fixed 90% ratio failed macOS CI at 0.74 for that reason
(2026-10-24, reproduced locally by sleeping the bot). Run it alone with:

```sh
python3 tools/boat_net_smoke.py --server build/boat_server --bot build/boat_bot
```

`boat_host` runs `tools/boat_host_check.py` (about 1 s): `wake_velocity --serve --exit-with-stdin`
on free ports, one `boat_bot` session, then closing the room's standard input, as the hosting
game does when it leaves or dies. The room must report it, keep `accepted=1 left=1` and exit 0
within 2 s. With GPU tests enabled, `wake_velocity_host_gpu` hosts a windowed river deathmatch with
`--host` on port 24779 for 120 frames and requires the room to close after the session.

`boat_net_session` runs `tools/boat_net_session.py` for about 36 s over real ENet/UDP loopback.
Three predicting bots stay connected while a fourth leaves, joins in progress, crashes, times
out and is replaced. A fifth peer must receive a room-full rejection. Bots request reset every
2 s and send inactive controls for 1 s every 3 s; summaries require completed resets, all four
roster slots, bounded wakes/messages/datagrams, continued snapshots and no decode errors.
The server must run every scheduled tick, release every slot and report the expected timeout
and rejection counts without protocol/emission faults. Child startup, exit and failure cleanup
are bounded; output uses temporary files, with completed process logs printed by the harness.

```sh
ctest --test-dir build -R '^boat_net_session$' --output-on-failure
python3 tools/boat_net_session.py --server build/boat_server --bot build/boat_bot --seconds 1800
```

`--seconds` accepts 24–7200 and repeats fourth-slot churn during longer runs while the other
three bots stay. This provides an opt-in soak driver; it does not measure memory plateaus or
CPU/bandwidth budgets, or apply OS-level impairment. The 30-minute, one-hour and two-hour M5
gates still require recorded runs and resource measurements. `boat_bot --predict`,
`--reset-every S` and `--inactive-every S` can also be used independently (integer S: 2–86400).

`boat_net` also runs four predicted clients in the mapped river: shoreline-safe spawns,
a deathmatch round, starts >200 m from the dock and >50 m apart with 35 m shoreline clearance,
a snapshot outside the former basin coordinate envelope, a local loading pause, reset and exact
mirrors at a birth-batch snapshot. The protocol-v6 codec rejects invalid map IDs and reserved
bytes and checks the RaceState/RaceEvent rules; a race room on the in-memory transport checks
predicted holds released at GO, ferry-exact mirrors, a mid-race joiner and results, and a
prediction replay through GO matches the authority; discovery uses the complete compiled-map bundle digest.

`boat_net_river` runs the four-player lifecycle harness with `--location river --mode deathmatch`
(about 36 s). The harness requires clients to select the server's location without
client-side map arguments. Run this map check with:

```sh
ctest --test-dir build -R '^boat_net_river$' --output-on-failure
```

`boat_net_deathmatch` runs the same script with `--deathmatch`: a deathmatch room with a 10 s
round and two `boat_bot --hunt` clients for 16 s (about 20 s in all). Each bot's received
scoreboard for round 1 must equal the server log's round-end line, and each must have received
the round start, every logged capsize and the round end. Whether the hunters capsize anyone in a
given run varies; the check is that clients agree with the authority. Run it alone with:

```sh
python3 tools/boat_net_smoke.py --deathmatch --server build/boat_server --bot build/boat_bot
```

`boat_net_race` (about 14 s) finds a `--mode race` room through LAN discovery and races two
`boat_bot --predict` clients for 10 s over ENet. Each bot must see round 1 gridded and released
(at least 150 observed 120 Hz simulation ticks held, a GO tick), receive the server's logged
race events, and finish without resyncs or decode faults. Run it alone with:

```sh
python3 tools/boat_net_smoke.py --race --server build/boat_server --bot build/boat_bot
```

The bot accumulates snapshot tick differences while held; repeated polling of the same
snapshot adds no time. Releases and inactive slots break the interval. Earlier versions,
including v0.0.14 and v0.0.15, counted service-loop iterations instead, which could fail
the held-time check on slower polling loops despite a correct four-second grid/countdown.

None of these tests impairs a real network interface, and none replaces the
[planned matrix](MULTIPLAYER_PLAN.md#verification-matrix-and-telemetry).

## Core boundary gate

`proteus_boat_core` holds physics, pose, locations, world and shoreline queries, water queries,
wakes, course crossing, harbor/traffic contacts and boat/city geometry data. The boundary check
rejects any core include outside the core headers, `raylib.h`, `raymath.h`, compiled map
geometry and a short system-header list without `<stdio.h>`. It also rejects raylib calls
other than raymath and C library console, file and process calls. When `nm` is available
(not MSVC), every undefined symbol in the core archive must be defined in core or be raymath,
libm, libc memory/sort/assert, `clock()` or the Accelerate DFT backend. The script first runs
a self-test that each rule rejects a known violation:

```sh
python3 tools/check_core_boundary.py --self-test
```

Add a new core source and its header to `PROTEUS_BOAT_CORE_SOURCES` in `CMakeLists.txt`.
Presentation, GPU, session/replay and the wake_velocity drive/profile code live in the
`proteus_render`, `proteus_gpu`, `proteus_boat_session` and `proteus_boat_drive` libraries.
Network codec, session and in-memory transport sources go in `PROTEUS_BOAT_NET_SOURCES` and
obey the same rules (`boat_net_boundary`); only `src/boat_net_enet.c` may use ENet and sockets.
The script accepts `--archive` more than once and resolves symbols across the listed archives.

## Long-session clock gate

`boat_clock` checks direct-time cases (one day, 30 days) without simulating the preceding
ticks; `boat_clock_soak_smoke` runs a 60 second wave soak. The actual one-hour soak is opt-in
and should use a Release build; it took about 95 s CPU on an M1 Max on 2026-10-23:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --target validate_boat_clock -j
mkdir -p build/bench/c3
./build/release/validate_boat_clock --soak 3600 build/bench/c3
./build/release/validate_boat_clock --soak 3600 build/bench/c3 --waves
```

The soak accepts 60..86400 seconds and writes CPU frames at one minute, halfway and the end.
Its printed hash is same-build evidence, not a cross-platform contract.

## Revisit the physical field

```sh
cmake -S . -B build -DPROTEUS_WAKE_FIELD_TESTS=ON
cmake --build build -j
ctest --test-dir build -L wake_field --output-on-failure
# Return to the normal suite afterwards:
cmake -S . -B build -DPROTEUS_WAKE_FIELD_TESTS=OFF
```

The label includes only retired research tests, not the active arcade model. This is
suspended verification of the opt-in field implementation, not evidence that the field
meets its original performance or numerical acceptance goals.

## Measurements and verification

2026-10-22, M1 Max/macOS, Debug, GPU tests enabled. The original serial suite passed
all 98 tests in **748.21 s (12 min 28 s)**. The fifteen retired checks used **434.33 s**;
`wake_candidate_moving_source` alone used **247.92 s**. The remaining checks summed to
313.90 s, including 83.91 s banking and 40.15 s handling in unoptimized Debug builds.
This explains why disabling only the field would still leave a five-minute serial suite.

The revised routine command passes **all 83 active tests in 97.40 s (1 min 37 s)**,
including **20 active GPU checks**: an **87% wall-time reduction** from the old serial
suite. This combined improvement includes field suspension, optimized numerical tests
and four CPU jobs; it is not an isolated compiler benchmark. Banking drops from 83.91 s
to 23.00 s and handling from 40.15 s to 11.01 s despite concurrent CPU work. The largest
remaining check is `gpu_review` at 23.99 s; active GPU checks total 60.31 s. These are
single local runs, not cross-platform guarantees.

Debug configure/full build passes without project warnings. Registration checks confirm
exactly fifteen disabled research cases and all 83 remaining checks enabled; a separate
configure with `PROTEUS_WAKE_FIELD_TESTS=ON` enables all fifteen research cases. The
CTest GPU label summary counts three disabled field cases too (23 listed), so the actual
active GPU count is twenty. CMake-generated validator flags retain `-g` with `-O2`,
without `NDEBUG` or fast-math. No wave recipe or gameplay physics changed here.

Next: keep the routine suite active; revisit field verification only when physical-field
work resumes. Timings and suite logs are in `build/test-speed/`. The prior default-promotion
run also completed before the suite policy changed, so its all-98 result remains valid
historical evidence. New manual motion/audio review was unnecessary for these test-only
changes; the preceding default switch has its own capture/desktop evidence.

## Deathmatch weapons (protocol v6)

`boat_combat` verifies deterministic weapon cadence, overheating/release cooling, grace,
numbered throws and replay, pool limits, fuses, relative moving-hull sweeps, water blocking,
life fences, one lethal defeat, respawn, radial derivatives and physical self-waves. It also
round-trips combat checkpoints and checks malformed pages and the 1100-byte ceiling.
`boat_net` adds four predicted deathmatch clients, late joins, loss/duplication/reordering,
a one-second outage, complete page-set recovery and departure generations. Free/race continue
through the existing trace/replay cases. `boat_race_trial --gpu-check` now compares both
production radial shader recipes against CPU height/gradient/vertical velocity.

```sh
./build/validate_boat_combat
./build/boat_combat_review --time 2.3 --screenshot build/combat-cpu.png
./build/boat_combat_review --gpu --play --screenshot build/combat-gpu.png
./build/boat_server --scripted --mode deathmatch --boats 4 --ticks 7200
```

The review fixture captures the shared scene at 320×200. CPU screenshots are display-free;
GPU and `--play` require a desktop and include the production weapon HUD and procedural audio.
For a display-free WebGL2 comparison of both production radial shader recipes:

```sh
python tools/check_web_combat_wave.py
```

This optional command needs Playwright and Chrome; its float probes independently check height,
gradient and vertical speed. `check_web_multiplayer.py --mode deathmatch` also exercises gun and
numbered grenade commands, page reception and captures the weapon HUD and burst over WebRTC.

Current automated/manual evidence and unavailable platform checks belong to the
[deathmatch owner](DEATHMATCH_PLAN.md#weapon-milestone--2026-10-26).
