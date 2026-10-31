# Test workflow and timing

P4.9 `boat_plugin_eval` checks the bounded objective worker protocol against the native
transaction engine: canonical state, lossless wide integers, exact framing, capacities,
protected fields and stale/malformed replies. `web_plugin_author` includes virtual-time
objective transport reuse, cancellation, late watchdog/result and failure checks. The optional
three-engine `check_web_plugin_author.py` also runs `check_web_plugin_objective.py` for browser
init/update/timers, simulation gating, recovery events, migration/Undo/cancel/restart, failure,
timeout/freeze and repeated-session cleanup. Delivery timings and worker Wasm linear memory are
evidence, not asserted performance budgets; physical devices and process memory remain open.
See [P4.9](PLUGIN_PLAN.md#p49-result-2026-10-30).

P3.1 `boat_plugin_state.*` covers fresh-instance objective callbacks, canonical integer state,
atomic state/RNG/event/HUD commit, quarantine/reset, candidate migration, isolated checkpoint
replay, resource/event limits and budget failures. These are headless harness checks; they do
not assert a live objective or HUD integration. P3.2 adds `boat_plugin_session.lossless_events`,
`live_objective`, `objective_revision_failure`, `objective_overflow` and `objective_tick_parity`: simultaneous native
notices, exact T/T+1 output staging, optional arrival success/miss, recovery, immutable revision
source, source-only reload/Undo and quarantine/overflow without preventing native arrival.
A 1,200-tick repeated run also checks objective hash determinism and native boat/voyage/weather
parity with callbacks disabled. These use virtual ticks; HUD appearance needs a windowed review. See the
[objective API](PLUGIN_API.md#objective-transactions-p31--p32).

P3.3a adds `boat_plugin_state.timer_capabilities`, `timers_delivery`, `timers_atomicity` and
`timers_capacity_and_checkpoint`: grant rejection, deterministic due/ID ordering, checkpoint
resume, exact wide deadlines, migration retention, cancellation/re-arm, rollback and malformed
checkpoints. `boat_plugin_session.objective_timers_reload` drives production T/T+1 staging,
metadata rename, explicit restart/Undo and queue overflow with due timers. All use virtual time.

P3.3b adds `boat_plugin_state.schema_timer_retirement` and
`boat_plugin_session.live_migration`, `resource_composition`, `preview_seek`. They verify
schema/generation migration, candidate failure with pending output, HUD/timer retirement,
second-owner rejection, atomic namespaced composition and native checkpoint seek against
uninterrupted physics/presentation/player/traffic-wake and event/command traces. Seek equality
compares declared fields, excluding C struct padding; the fixture deliberately changes wake
padding and checks that an actual emitter-state change still fails equality. The developer
`boat_plugin_review` command exposes the same display-free checkpoint/inspection harness;
see the [API](PLUGIN_API.md#live-migration-and-review-p33b) for its configuration and limits.

P3.4 adds `boat_plugin_state.independent_budgets` and
`boat_plugin_session.memory_accounting`: separate entry/callback/migration instruction ceilings,
rollback/quarantine and full-source history/staging/Undo/eviction accounting. The explicit
`./build/boat_plugin_bench plugins/shelter-tour` experiment reports 100-sample staging/update
and failure-path costs; it is outside CTest and has no timing assertion. Native inspection
reports fixed owner reservation, retained source payload and allowed Lua heap separately.
See [runtime qualification](PLUGIN_API.md#native-runtime-qualification-p34) for commands and limits.

P2 plugin checks: `boat_plugin_race.*` covers descriptor parity/rejections, revision/Undo,
real-file trial controls with virtual time, and tick-for-tick four-boat fleet/event parity.
`boat_race_plugin.*` runs the existing eleven native race cases against the loaded Lua
reference. `boat_race_plugin_preview` renders a display-free CPU frame. See the
[plugin API](PLUGIN_API.md#river-race-authoring-p2).

`release_command` runs `tools/test_cut_release.py` in the full CPU suite on every platform.
It uses temporary local Git repositories and mocked external commands to check immutable
tags, detached worktrees, hosting baseline/push recovery, archive identity/integrity and
failure gates. Workflow polling uses a virtual clock. These tests never publish a release,
contact Winterfell or require controller/browser credentials. The
[release guide](RELEASES.md#release-command) owns the real operator command and live evidence.

`boat_time_control` advances the same 32 simulated ticks at all six live time rates and
compares the tick count and complete physical boat state with a fixed-tick reference; it also
checks rate bounds and the real-frame stall clamp before scaling. `boat_replay` covers practice
finishes with/without an existing best, preserved export files, replayable last runs and eligibility
after restart. All clocks are virtual. The [boating plan](BOATING_PLAN.md#play-testing-time-controls--2026-10-28)
owns implementation status and manual review limits.

The Linux, macOS and Windows Actions builds run on pull requests and pushes to `main`, skipping
events that change only Markdown files. Any non-Markdown change runs the normal builds and tests,
including on mixed documentation and code pull requests. Manual workflow runs and the macOS and
Windows release builds remain available regardless of these filters. GitHub does not create a
successful check for a skipped workflow; do not require these checks for Markdown-only pull requests.

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
flag field/cross colors (the explorer's too), Norway's white border and independent render storage.
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
(`boat_water_review --check-gpu-views`, `gpu`) does the same with two GPU views,
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
`gpu_view_lifecycle` (`boat_water_review --check-gpu-lifecycle`, `gpu`) renders two
private river owners (one with an arcade world, so both patch caches build), the harbor and the
basin. Shared views must reproduce the fresh-view references interleaved, after the first
owner and its view are destroyed, and over three view recreate cycles with the context alive.
It also resizes a view through portrait, wide and default targets with smoothing off/on;
returning to the default target must reproduce the exact original pixels. Invalid dimensions
must leave the existing renderable targets intact.
`tile_culling` (`boat_water_review --check-culling`, `gpu`, about 9 s; adventure A2.3)
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
`web_server_directory` checks fetched known-server schema, version matching, refresh and
failure behavior under Node. The opt-in [directory browser check](WEB_BUILD.md#known-browser-servers-2026-10-26)
joins the named room without typing and can simulate a failed list with manual fallback.

## Lanes, cases and timing

Every test carries exactly one lane label. `quick` tests take under about 100 ms on their own;
`slow` holds everything heavier: long sweeps, soaks, the process smoke and every GPU test.
Use the quick lane while editing and the full suite before handing off:

```sh
cmake --preset dev            # Same as: cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --preset quick          # Under 1 s: 140 tests on an M1 Max (2026-10-26)
ctest --preset full           # About 11 s without GPU tests; everything enabled
ctest --preset full -L core   # Presets combine with the usual filters and -j N
```

Both test presets use 8 jobs; pass `-j N` to change that. Plain `ctest --test-dir build` still
works. GPU tests share `RESOURCE_LOCK gpu`: they run one at a time, but CPU tests keep running
beside them. CTest also runs the fixture setups a selected test requires, so a quick run can
include a slow setup such as `boat_replay`.

**Case tests.** The heavy validators are tables of small cases (`tools/boat_test_cases.h`).
`proteus_add_case_tests(prefix target LABELS ...)` in `CMakeLists.txt` lists a target's cases
after each build (`target --list`) and registers each as its own test, `prefix.case`, so cases
run in parallel and a failure names the case. A case table entry is `TEST_CASE(fn)` or
`SLOW_CASE(fn)`, where `fn` returns 0 on success. The binaries keep their old command line:
without arguments they run every case and print their `=ok` line. Each case prints
`case=<name> ms=<t>`.

```sh
./build/validate_boat_banking --list
./build/validate_boat_banking --case sides
ctest --test-dir build -R '^boat_banking\.' --output-on-failure
```

Case tests: `wake_objects` (`wake_objects_review`), `boat_handling` (`validate_boat`),
`boat_banking`, `boat_world_isolation` (`validate_boat_world`), `boat_map_index`, `boat_clock`,
`boat_net`, `boat_environment`, `boat_fleet`, `boat_race` and `boat_map`. Their tests are named
`<prefix>.<case>`; for example, the former `wake_objects_contracts_jumps` is now
`wake_objects.contracts`, `wake_objects.jumps` and so on. Whole-program tests heavier than the
quick budget are listed in `PROTEUS_SLOW_TESTS`; configure warns about a name that matches no test.

**Keeping cases fast.**
- Physics-only checks use `physics_session()` or `physics_basin_session()`
  (`tools/boat_test_session.h`), which turn presentation off. The flag cloth was about 75–90% of
  a session tick. `boat_world_isolation.presentation_toggle` proves that physics stay
  bit-identical without presentation.
- `test_world(id)` prepares a location on first use, so a basin-only case never builds the river.
- A sweep can stop once its asserted end state holds. It keeps a hard cap and a fixed length
  wherever the length is the contract.
- A large sweep gets a sampled quick case and full slow cases, for example
  `boat_map_index.sampled_queries` and `boat_map_index.lattice_queries`.

### No wall time

No test may wait for time to pass. Code that depends on time takes `now` as an input, and its
test drives a virtual clock: the room harness steps `boat_net_server_service(server, now)`, and
sessions step whole ticks. Sleeping, fixed-duration runs and decisions on elapsed time make
tests slow and flaky, and usually mean state that has not been extracted yet. Wall-clock limits
appear only as failure deadlines (a subprocess `timeout`, or a wait that returns as soon as its
event arrives). Measured costs may be printed but never asserted.

`test_time` (`tools/check_test_time.py`) enforces this. It scans the validators, the test
headers, every script CMake registers as a test, and the `add_test` commands. It rejects
sleeps and waits, wall-clock reads in test scripts, and `--seconds`/`--frames` runs. A wait
bounded by a deadline must say so in a `deadline` comment on its line. `*_rejected` argument
checks are exempt. The one listed exception is `wake_velocity_host_gpu`, whose windowed frames
follow the display.

**Measuring.** `tools/test_timing.py` ranks a CTest JUnit report. It prints buckets (≤20 ms,
≤50 ms, ≤1 s, >1 s) and the quick tests over a budget (`--budget`, default 0.1 s). `--enforce`
makes it exit non-zero for those. It is not a CTest test, so machine load cannot make the
suite flaky; times under `-j` contention run high.

```sh
ctest --test-dir build -j 8 --output-junit test-speed/run.xml
python3 tools/test_timing.py build/test-speed/run.xml --top 20
```

On macOS, the first launch of a freshly relinked binary can take 0.3–0.7 s longer while the
system scans it, so the first run after a build is slower. Adding the terminal app under
System Settings → Privacy & Security → Developer Tools removes that delay.

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

The [arcade stability candidate](ARCADE_STABILITY.md) extends the `wake_objects` cases
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
ctest --preset full
```

`PROTEUS_WAKE_FIELD_TESTS` defaults to `OFF`. Fifteen registered experiments are marked
disabled, covering the owned field, refinement, cadence, candidate sources/boats/GPU,
compact packets and Python prototypes. CTest lists them as disabled, not failed. Their
sources, targets and manual diagnostic commands remain available. All arcade-object tests
stay enabled. With `PROTEUS_GPU_TESTS=ON` and Node.js available, this HUD/control worktree runs 103 active
checks as of 2026-10-24 (including the browser event contract; counted by the current CTest run;
historical, before the case split). GPU cases run one at a time under `RESOURCE_LOCK gpu` while
independent CPU checks run concurrently; fixture dependencies still govern capture/replay comparisons.

The river frame benchmark has two checks in `tools/validate_boat_water_benchmark.py`.
`boat_river_benchmark_options` (`cpu`) checks option and output-path rejection without
opening a window. With GPU tests enabled, `boat_river_benchmark_contract` (`gpu`;
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
- `boat_plugin` runs `validate_boat_plugin` (Lua plugins P0, twelve quick cases, about 0.5 s
  together in Debug). The reference package `plugins/langedrag` must decode field for field and
  bit for bit to `boat_voyage_outer`, with the same digest. It also checks a definition computed
  with the allowed libraries, syntax, runtime and bytecode rejection, and every forbidden
  facility. Invalid fields must report their path, as must bad manifests and host limits.
  Memory and instruction exhaustion must stop with zero leaked bytes, and repeated failures
  must leave a last good definition unchanged with identical diagnostics. Package files are
  read from a build-tree scratch directory, where missing files, an escaping entry, a symbolic
  link, a directory and oversized input are refused. Finally the decoded shelters must lie in
  the outer map's water. Instruction budgets are counted by a hook, never timed.
- `boat_plugin_session` runs `validate_boat_plugin_session` (Lua plugins P1/P3; bounded quick cases
  of about 37 ms together in Debug, plus a slow parity case of about 0.2 s). It covers staging
  against the outer map with field paths, and the classification of renames, live route/layout
  changes, water changes and other plugins. It applies each policy to a live `BoatSession`: a
  route change maps progress by shelter ID with the boat kept, while a removed reached shelter
  falls back to the start; a water change still restarts. Broken stages must leave the active
  revision byte-identical, and Undo is checked over the eight-deep history. The save poller runs
  on a virtual clock. `game_controls` drives the game's `--plugin` F5/F6/F7 and polling glue on
  files in a build-tree scratch directory. `compiled_parity` steps a compiled and a plugin
  session with identical input to Långedrag and compares the boat, the water and every voyage
  field at each tick. The `wake_velocity_plugin_*` tests render a CPU frame with the plugin and
  check that `--plugin` is refused with a room, the course, another location or a missing package.
- `boat_plugin_catalog` (PD1) decodes every generated entry and proves it equals evaluating its
  embedded source, that the basin-free, river-free, outer-voyage and time-trial defaults equal
  the compiled definitions, and that catalog decoding creates no Lua state (the
  `boat_plugin_lua_states` counter is unchanged). `boat_plugin_session.catalog_runtime` opens the outer voyage and Harbor life entries and checks
  that no Lua state, watch, staging worker or authoring panel appears. `boat_plugin_catalog_invalid` runs the
  build-time generator on objective, bad-schema and duplicate packages and expects its normal
  diagnostics. `plugin_default_parity` captures basin, river, outer and course sessions with
  `--renderer cpu` through the catalog and `--no-plugins` and compares the images byte for byte.
  `boat_replay`'s legacy-best case proves a pre-existing `course-calm-physicsN.run` keeps its
  path as the best and is not overwritten by an equal or slower run. The catalog now also
  includes the outer free room, `proteus.deathmatch` (whose rules and combat values equal the
  native defaults at every location) and the two harbor room packages.
- `boat_room_package` (PD2/PD3) evaluates edited race and deathmatch packages, builds rooms from
  their definitions and compares every client's mirror hash with the authority at snapshot
  ticks; predicting clients must correct and stay within the prediction bounds. It also loads
  every bundled free/match/race package through the authority path, validates the authored scene
  against the room's spawn and course, and measures each room definition and a maximum scene
  against the 16 KiB cap. `boat_net.room_definition_codec` covers the fieldwise definition,
  truncation, trailing bytes, non-finite values, mode/location/rule faults and paging.
  `boat_room_course_source` rejects any compiled `boat_race_river_course` reference in the
  networked client files.
- `boat_map` runs `validate_boat_map` (adventure A1.1/A1.2). It checks explicit map
  descriptions, caller-owned collision caches, failed/zeroed preparation, two river owners and a
  synthetic map alive together, and destroy/recreate. It also matches a river query digest
  recorded before the ownership change. `BoatWorldResources` lifecycle and validation are
  covered too. Worlds on separate owners step interleaved with bit-identical results, and
  fleet copies and checkpoint restores keep borrowing their owner.
- `boat_map_index` runs `validate_boat_map_index` (adventure A2.2). It
  compares the indexed collision queries at 8, 16 and 32 m cells with the exhaustive scan,
  bit for bit, on about 3.6 million queries. These cover a cell-seam lattice, shore offsets,
  tile seams one float either side, tile corners, random and far points. It checks the 512 m
  and 1 km tile catalogues' lists, land ownership and bounds, and reports index, tile and
  candidate counts. Since A2.3 it also checks that the renderers' land-only catalogue equals
  the full one, and that land-only construction rejects bad input. The quick
  `sampled_queries` case compares every 512th query of all families (about 70 ms); the slow
  `lattice_queries`, `shore_queries`, `seam_queries` and `random_queries` cases compare all of
  them (about 13 s together in Debug).
- `boat_map_scaling` runs `validate_boat_map_scaling` (adventure A2.5, about 5 s in Debug, `slow`). It
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
- `boat_scenery` runs `validate_boat_scenery` (adventure A3.5i, about 0.3 s in Debug, `slow`). It
  checks the scenery packs under `assets/geometry/`: nothing is drawn before a load; cut packs,
  wrong magic or version, wrong counts, a moved bridge range and the river pack offered as
  `outer` are all refused; a good pack loads once with water identical to the compiled
  `water.h`; `outer` needs the river pack for its kit ground; and unload clears them all. The
  map validators and review tools load the packs from the source tree
  (`tools/boat_test_scenery.h`).
- `map_pack` runs `tools/map_pack.py --self-test` (quick): float32 rounding, the leading-water
  count and a pack round trip.
- `boat_minimap` runs `validate_boat_minimap` (quick). It requires a chart for `river` and
  `outer` and none for the basin, checks that both frames fit the 320×200 scene (the chart
  above the instruments), that the spawn, every voyage shelter and the destination are water
  pixels and inland Hisingen is not, that composition marks the boat and destination without
  touching the trim, and the N cycle. With arguments it writes review captures
  ([HUD guide](BOAT_HUD.md#review-commands)).
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
- `boat_net`, `boat_net_enet` and `boat_net_process` are the
  [network checks](#network-checks).
- Fourteen argument checks expect a failure: `boat_server --scripted` with a room option or
  `--until-empty`, an invalid `--port` and an invalid `--bind`, `--round-seconds` without
  `--mode deathmatch`, `boat_bot` without a port, `boat_bot --discover` with `--connect`,
  `boat_bot --snapshots` with `--seconds`, `wake_velocity --connect` combined with `--waves` or `--skin`, `--name`
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

## GPU checks

GPU checks are opt-in and require a desktop graphics context. Configure and run the
labelled cases on a machine that can create a raylib/OpenGL window:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DPROTEUS_GPU_TESTS=ON
cmake --build build -j
ctest --test-dir build -L gpu --output-on-failure
```

The default display-free suite omits these checks. Report that omission when recording
verification. Fixture capture commands live in the [renderer guide](3D.md#gpu-review-captures).

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

**Real transport, virtual clock.** `boat_net_enet` runs `validate_boat_net_enet`: the same
room harness, built with `BOAT_NET_TEST_ENET`, over real ENet sockets on ephemeral 127.0.0.1
ports, while the room keeps its virtual clock. Loopback delivery is asynchronous, so after
every service the room calls `boat_net_enet_settle`. It services every host in the process until
each datagram sent has been received, keeping the events for the next polls. Its 5 s limit is
only a failure deadline. Cases:
- `enet_loopback`: two bots, a wrong-token client refused, a second listen on the port
  refused. Over 4 s of room time it checks snapshots, one bootstrap, no resyncs or decode
  errors, applied births and boats that moved, plus message ≤1100, datagram ≤1200 and MTU
  ≤1200 bytes. It also checks graceful leaves, no faults, and that every mirror matches the
  authority's hash.
- `enet_lan`: a loopback LAN answer reaches a search socket with its mode, players, token,
  compatibility and endpoint.
- `enet_lifecycle` (slow): three predicting bots stay while a fourth leaves and a fifth is
  refused as full. Another bot falls silent, times out on the room clock after
  `BOAT_NET_TIMEOUT`, and a replacement takes its slot.
- Unchanged room cases over UDP: `rejections`, `resync`, and `deathmatch`, `race_room` and
  `mapped_rooms` (all three slow). `mapped_rooms` checks that clients take the server's
  location.

**Processes.** `boat_net_process` runs `tools/boat_net_process.py` (about 0.25 s). It ends on
events, not elapsed time:
- `boat_server --until-empty` closes the room once every accepted player has left.
- `boat_bot --snapshots 3` leaves after three snapshots.
- `boat_bot --discover --first` stops at the first answer.

It checks a port clash, a LAN search with the room's flags, two token-holding bots and a
wrong-token refusal, bot summaries and bounds, and the server's accept/leave/rejection counts
without faults. Its timeouts are failure deadlines. Run it alone with:

```sh
python3 tools/boat_net_process.py --server build/boat_server --bot build/boat_bot
```

`boat_host` runs `tools/boat_host_check.py` (about 0.3 s): `wake_velocity --serve --exit-with-stdin`
on free ports and one `boat_bot --snapshots 3` session. It then closes the room's standard input,
as the hosting game does when it leaves or dies. The room must report it, keep
`accepted=1 left=1` and exit 0 within a 2 s deadline. With GPU tests enabled,
`wake_velocity_host_gpu` hosts a windowed river deathmatch with `--host` on port 24779 for 120
frames and requires the room to close after the session. It is the one listed exception to
[the no-wall-time rule](#no-wall-time): windowed frames follow the display.

**Manual soak.** `tools/boat_net_session.py` is not a CTest test. It runs real processes on the
wall clock: three predicting bots stay while a fourth leaves, joins in progress, crashes, times
out and is replaced. Use it for long churn runs:

```sh
python3 tools/boat_net_session.py --server build/boat_server --bot build/boat_bot --seconds 1800
```

`--peer-timeout` (1–60, default 5) sets the room's silence timeout through
`boat_server --peer-timeout`. `--seconds` (default 30) accepts one churn cycle plus a second up
to 7200. It does not measure memory plateaus or CPU/bandwidth budgets, or apply OS-level
impairment. The 30-minute, one-hour and two-hour M5 gates still require recorded runs and
resource measurements. `boat_bot --predict`, `--reset-every S`, `--inactive-every S`, `--hunt`
and `boat_server --mode deathmatch|race --round-seconds S` remain available for manual runs.

`boat_net` also runs four predicted clients in the mapped river: shoreline-safe spawns,
a deathmatch round, starts >200 m from the dock and >50 m apart with 35 m shoreline clearance,
a snapshot outside the former basin coordinate envelope, a local loading pause, reset and exact
mirrors at a birth-batch snapshot. The protocol-v8 codec rejects invalid map IDs and reserved
bytes and checks the RaceState/RaceEvent rules; a race room on the in-memory transport checks
predicted holds released at GO, ferry-exact mirrors, a mid-race joiner and results, and a
prediction replay through GO matches the authority; discovery uses the complete compiled-map bundle digest.

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

The Lua plugin host (`proteus_plugins`, [plugin API](PLUGIN_API.md)) and the pinned interpreter
(`proteus_lua`) sit outside both boundaries. Configure fails if `proteus_boat_core` or
`proteus_boat_net` links either of them. The self-test also rejects a core `#include "lua.h"`
or `"boat_plugin.h"`, and the archive check rejects any `lua_*` reference. Strict JSON reading
for the server directory and plugin manifests lives in `proteus_json` (`src/boat_json.c`).

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

### Fast lanes, 2026-10-26

M1 Max/macOS, Debug, `PROTEUS_OPTIMIZE_TESTS=ON`, GPU tests off unless noted. Each step was a separate commit on the `test-speed` branch.

| Run | Tests | Wall | Summed test time |
|---|---|---|---|
| Baseline, `-j 4` | 98 | 71.6 s | 285.8 s |
| After the case split and presentation-free physics, `-j 10` | 171 | 36.2 s | 216.5 s |
| After shortening the ENet tests, `-j 10` | 171 | 19.0 s | 144.8 s |
| `ctest --preset full` (`-j 8`) | 220 | 18.7 s | 145.4 s |
| `ctest --preset quick` | 133 | 0.8–1.1 s | 5.6 s |
| `ctest --preset full`, network on a virtual clock | 227 | 11.2 s | 88.7 s |
| `ctest --preset quick`, network on a virtual clock | 140 | 0.75 s | – |
| `ctest --preset full`, `PROTEUS_GPU_TESTS=ON` | 246 | 60.7 s | 207.7 s |

With GPU tests on, the GPU tests run one at a time, so they bound the wall time. The largest is
`boat_river_benchmark_contract` at 27 s. The last full GPU run before this work (2026-10-25)
took 183 s at `-j 4`.

Before the virtual-clock change, the five real-time ENet tests (9–16 s each) bounded the full
suite's wall time. They are now `boat_net_enet` cases and a 0.24 s process smoke, and three
oversubscribed `-j 40` runs passed. The longest tests are now sweeps:
`boat_world_isolation.lifecycle` and `boat_map_index.lattice_queries` (about 6 s each) and
`boat_map_scaling`. Largest per-test changes:
- `wake_objects_contracts_jumps`: 35 s became 0.12 s of quick cases plus 1.7 s of slow ones.
- `boat_banking`: 22.3 s became 0.3 s quick plus 1.5 s slow.
- `boat_world_isolation.view_binding`: 3.9 s → 73 ms; `city_dressing`: 4.2 s → 0.24 s. Both
  come from the scenery ground index in `src/waterfront_corner.c`, which keeps the pinned
  dressing digest and identical captures.
- `boat_environment`: 2.9 s → 0.3 s.
- `boat_net_session` and `boat_net_river`: 36 s → 16 s each, then replaced by
  `boat_net_enet.enet_lifecycle` (0.34 s) and `boat_net_enet.mapped_rooms` (0.2 s).

The state hash moved to `hash_revision` 2 (see [the benchmark guide](BOATING_PERFORMANCE.md)).
For banking, world isolation and the scenery index, outputs, captures and the banking CSV
were compared byte for byte with the previous binaries. `boat_environment` printed the same
values. The other conversions were checked by their own assertions passing.

Not done yet:
- `boat_map_scaling` (5 s), the XM checks (1.4 s each) and `boat_clock_soak_smoke` (1.8 s)
  are still single slow tests.
- `boat_river_benchmark_contract` (27 s) and `boat_race_trial_gpu` (12 s) are unsplit GPU
  tests.
- The CPU water surface, the map build and in-game flag cloth cost remain product follow-ups.

### Routine-suite reduction, 2026-10-22 (history)

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

## Deathmatch weapons (protocol v8)

`boat_combat` verifies cannon ascent/apex/descent, water landing, three-dimensional descending
hits/overhead misses and trajectory replay, the shared muzzle attachment on a pitched/rolled
boat, 10 m/45 m capsize targets, progressive capsizes/recovery, torpedo ammo/arming/serial wrap,
actual damage/overkill, rearward grenades with a 4 m peak/70 m range/seven-second lifetime
and four-player ring reuse at recharge, deterministic cadence, overheating/release cooling, grace,
numbered throws and replay, pool limits, fuses, relative moving-hull sweeps, water blocking,
life fences, one lethal defeat, respawn, radial derivatives and physical self-waves. It also
round-trips combat checkpoints, checks the compact combat payload over 2000 real fleet ticks,
malformed pages and the 1100-byte ceiling. `boat_net` adds four predicted deathmatch clients,
late joins, loss/duplication/reordering, a one-second outage, complete page-set recovery,
snapshot liveness/resync and departure generations. Free/race continue
through the existing trace/replay cases. `boat_race_trial --gpu-check` now compares both
production radial shader recipes against CPU height/gradient/vertical velocity.

```sh
./build/validate_boat_combat
./build/boat_combat_review --time 2.3 --screenshot build/combat-cpu.png
./build/boat_combat_review --gpu --play --screenshot build/combat-gpu.png
./build/boat_combat_review --gpu --close --time 1.025 --screenshot build/cannon-close.png
./build/boat_combat_review --gpu --mirror --play --screenshot build/rearview.png
./build/boat_combat_review --gpu --torpedo --time 1 --screenshot build/torpedo.png
./build/boat_server --scripted --mode deathmatch --boats 4 --ticks 7200
```

`boat_fleet_render` also probes the actual transformed hull triangles beneath every corner
of the cannon mounting foot on all multiplayer room-slot skins and checks that the housing overlaps that foot.
This guards against the skin mesh's normalization cancelling the hull pose scale. The same
gate renders the aft camera at three headings, checks that only trailing boats appear, verifies
starboard placement after mirroring and confirms read-only fleet state.

The review fixture captures the shared scene at 320×200. CPU screenshots are display-free;
GPU and `--play` require a desktop and include the production weapon HUD and procedural audio.
`--mirror` adds a pursuing boat, rear grenade/wave and the production mirror (initially shown;
V toggles it during `--play`, whereas the game defaults to hidden); it also requires
a graphics context for HUD compositing when the scene renderer is CPU.
`--torpedo` is a close presentation view that hides the HUD so the nose and tail are visible.
For a display-free WebGL2 comparison of both production radial shader recipes:

```sh
python tools/check_web_combat_wave.py
```

This optional command needs Playwright and Chrome; its float probes independently check height,
gradient and vertical speed. `check_web_multiplayer.py --mode deathmatch` also exercises gun and
numbered grenade commands, page reception and captures the weapon HUD and burst over WebRTC.

Current automated/manual evidence and unavailable platform checks belong to the
[deathmatch owner](DEATHMATCH_PLAN.md#weapon-milestone--2026-10-26-superseded-rules-historical-evidence).


`boat_plugin_author` (native, slow CPU lane) checks the P4.1/P4.2 JSON validator/preview tools and disposable
worker: voyage/race/objective examples, descriptor/init evidence, malformed framing, restricted
Lua failures and budgets, immutable snapshot pins, input type/size rejection, and virtual-time
cancellation/deadline cleanup. Cancellation lifecycle and malformed output use a fake process; an intentionally stuck real
process also verifies kill/reap at a virtual deadline. Normal validation uses the native worker. No test waits for wall-clock time to pass.
See [native authoring tools](PLUGIN_API.md#native-authoring-tools-p41).

P4.2 extends `boat_plugin_author` with JSON Schema discovery and every numeric descriptor
bound checked against the native decoder, repeatable voyage/race scenario artifacts and
preview callback rejection. `boat_plugin_stage` (native, slow CPU lane) checks checked-data
handoff, native stepping during staging, held Apply/rebind, repeated candidates, same-number
stale epochs after Undo, virtual deadlines against a stuck process, F6 cancellation,
malformed/oversized output, syntax failure and the live F5/completion adapter. Its process
completion loops await an external event; CTest supplies the failure deadline. No simulated
clock is advanced to wait for a child. See [live descriptor staging](PLUGIN_API.md#live-descriptor-staging-p42).

P4.5 adds `boat_plugin_capture` (native, slow render lane): the same script with `--game`
runs real CPU captures of every outer anchor, repeatable pinned evidence, a rejected candidate and
the CLI's atomic `--output`. Its child waits are failure deadlines; the capture lifecycle uses
the fake process in `boat_plugin_author`. See [frame capture](PLUGIN_API.md#frame-capture-and-anchors-p45).

P4.6 adds `wake_velocity_bridge` (native, CPU lane): `validate_wake_bridge` drives the game's
bridge over request files on a virtual clock (authentication, pins, deferred isolated staging,
Apply/Undo/Discard, rejection, restart, cancellation, save-poll agreement, and the PC `console`
operation). `boat_plugin_author`
gains client checks against a fake game and an end-to-end CLI run against
`validate_wake_bridge --serve` (`--bridge-server`). Its loops await external process/file events;
CTest supplies the deadline. See [live tool bridge](PLUGIN_API.md#live-tool-bridge-p46).

PC adds `wake_command` (native, CPU lane): `validate_wake_command` drives the console's input,
history and bounded scrollback headlessly; composes edits as one entry and checks the `console N`
attribution; stages a far berth edit through the real isolated worker and proves the boat and
voyage trace match a control tick for tick and that repeating the same edit at the same tick
hashes identically; holds a berth moved under the hull, commits it at the first clear boundary
without displacing the boat, and Undo restores the prior envelope with the boat kept. It also
covers a failed edit keeping the active revision, `/capability scene.define`, `/save` reading back,
a bounded query, and that no Lua state is created on the game thread. The child waits are failure
deadlines, not simulated time.

P4.7 adds the `Packaged` case to both `boat_plugin_author` and `boat_plugin_capture`: it installs a
release layout with `tools/package_plugin_tools.py` in a temporary path containing spaces, then runs
the copied tool from another directory (capabilities, anchors, validate of every example, preview,
no bytecode cache, refusal of an unknown layout). Only the render lane, with `--game`, adds a
capture. See [packaged authoring tools](PLUGIN_API.md#packaged-authoring-tools-p47).

P4.3 adds `web_plugin_author` (Node, CPU quick lane): portable bundle/UTF-8 byte caps, immutable
framing, bounded worker lanes, one-use receipts, stale epochs, virtual deadlines, cancellation,
late output and worker/exit/framing faults. PD4 adds `web_plugin_wire` (quick), which fails if
`web/plugin_author.js`'s wire magic differs from `BOAT_PLUGIN_WIRE_VERSION`, and extends the Node
contract with scene and free/course imports plus room-kind rejection. `tools/package_web.py`
runs `tools/check_web_wasm.py` on every packaged site: the game Wasm must lack Lua parser/VM
strings, with `plugin_stage.wasm` as the positive control. The opt-in
`python3 tools/check_web_plugin_author.py --browser chromium --url http://127.0.0.1:8000/ --output build-web/plugin-browser-check`
(also `firefox` or `webkit`) drives an actual served Wasm build through bundle export/import,
capability rejection, scene/free/course staging and launch, repeated worker validations, launch,
live rename and syntax failure, held water Apply/Undo, stale output, cancellation, navigation
mid-validation, repeated source restore/revalidation and storage denial. On Chromium it also
freezes the tab mid-validation. The title check now covers the bundled Harbor life catalog session,
the desktop console query/export/live-edit path and the absence of a worker in a normal session.
It awaits application/worker conditions; the only fixed waits are the freeze durations themselves.
The browser PR workflow runs the Node contracts and packages the Wasm build. Full three-engine
qualification is manual-only; dispatch it for browser/plugin lifecycle changes and before a
release, using `gh workflow run web-plugin.yml --ref BRANCH`. It is not run on ordinary pushes
to `main`. Superseded PR builds are canceled. The [browser guide](WEB_BUILD.md#plugin-source-bundles-p43) owns setup/limits.
