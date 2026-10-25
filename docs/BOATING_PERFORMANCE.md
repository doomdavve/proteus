# Boating performance tools

2026-10-24: [landing candidate L1](BOAT_PHYSICS_PLAN.md#l1-candidate--2026-10-24) advances
handling to revision 8; its wake-loop trace has 18 births, one landing and final hash
`82a88bf33eff247a`. Revision-7 figures below are historical.
Earlier 2026-10-24: [arcade stability tuning](ARCADE_STABILITY.md) advanced handling to revision 7.
The benchmark retains its explicit 1.8 m stress setting; historical physics-6 trajectories
and timings below do not establish a baseline for the revised handling.
Revision-7 reports include `physics_revision`; the updated three-run trace has 19 births,
peak 19 objects, one landing and final hash `a991b713315ef1e4`. See the
[candidate evidence](ARCADE_STABILITY.md) for validation and its limits.

The foundations [P2a task](BOATING_FOUNDATIONS_PLAN.md#p2a-completion--2026-10-23)
owns the finite scripted arcade wake-return benchmark. Full P2 renderer counters and the
P3 workload matrix remain pending.

## Shared driver extraction — 2026-10-23

`tools/boat_study_drive.[ch]` now owns the app's existing scripted controls and step wrapper.
It preserves advance → boat step → stern track, float time arithmetic, optional D3/field
paths and the rival's existing route/contact/reset sequence. There is no fleet scheduler.
`tools/boat_drive_baseline.h` freezes the original driver from `2994e0d` for regression checks.

Apple Silicon/macOS, Debug: `boat_drive_extraction` matches named physical/wake hashes at
all 3840 ticks against that driver and matches CPU pixels at ticks 1080 and 2400.
App captures `build/bench/before9.png`/`after9.png` and `before20.png`/`after20.png`
match byte for byte and were inspected. A desktop `boat_study --demo --frames 180` run
completed (378 physics ticks). This is scripted playback evidence, not human handling or
audio acceptance. The extraction changes no controls or intended simulation behavior.

## Report contract

Run from the repository root after the normal Debug configure/build:

```sh
mkdir -p build/bench
./build/boat_benchmark --scenario wake-loop --mode simulation --renderer none --warmup 120 --frames 1800 --output build/bench/arcade-baseline
```

Only scenario `wake-loop`, mode `simulation`, and renderer `none` are supported (also the
defaults). `--presentation on|off` (default `on`, as in the P3a baseline) steps or skips the
cosmetic particles, flag cloth and camera follow after each tick; the physical trace and
`state_hash` are identical either way, and the report records the choice as `presentation`. `--output` is required: it is a file prefix, with an existing parent directory;
the tool writes `<prefix>.json` and `<prefix>.csv`. Existing reports are overwritten.
`--warmup` defaults to 120 and accepts 0..100000 nominal frames; `--frames` defaults to
1800 and accepts 1..100000. Unknown options, invalid values, allocation and output errors
return nonzero. Count bounds precede size/tick arithmetic; all timing storage is preallocated.
No window, audio device, replay/course storage or personal-best writes are involved.

Each nominal frame advances exactly two existing 120 Hz ticks. Warmup and measurement form
one continuous drive. Steering is commanded from tick 420 through 1439; at tick 1440 the
script commands neutral throttle and steering. This is a finite scripted wake-return fixture,
not sustained powered driving or a full-pool workload. Births, peak objects and landings cover
the entire run, including warmup. The pool remains 128 slots, 32 per emitter; one emitter is used.

JSON schema/scenario/hash revisions are currently 1. The report identifies the build's configure-time
Git revision (with `-dirty` for tracked changes), build type/compiler, platform, selected options,
presentation choice, warmup/measured/total tick counts, wake lifetime/height and observed births/peak/landings.
`simulation_ms` contains p50/p95/p99/max over **all measured frames**, using sorted index
`floor((frames - 1) * percentile / 100)`. CSV columns are zero-based `frame`, absolute
`end_tick`, and `simulation_ms`. The values use `boat_profile_time(false)` and include the
existing shared tick wrapper. `render_cpu_ms` and `gpu_ms` are null because they are unavailable.
The report is written and samples sorted after all ticks; timing excludes hashing and file I/O.

`state_hash` folds FNV-1a over named numerical `%a` values after every nominal frame, including
warmup. `boat_study_state.c` lists its contract: physical pose, velocities, controls, motion state,
motion timers/landing counter, physical wake bookkeeping, world tick/emission/expiry counters,
all object records and all emitter fields. It excludes raw structs, padding, pointers and cosmetic
particles/flag/camera state. This is same-build deterministic evidence, not a collision-proof state
comparison, portable replay format or claim of cross-platform bit identity. CPU pixel comparisons
add coverage for the extracted app path.

## P2a completion evidence — 2026-10-23

Apple Silicon/macOS, Debug, `PROTEUS_GPU_TESTS=ON`, default retired field tests disabled:

- Normal configure/build pass without compiler warnings (raylib emits its existing macOS OpenGL
  deprecation CMake notice).
- `ctest --test-dir build -j 4 --output-on-failure` passes all 85 active tests, including
  20 GPU cases, in 113.79 seconds with desktop access (`build/bench/ctest-desktop.log`).
  Fifteen retired field cases remain disabled. The initial sandbox attempt stalled on the first
  GPU case and was stopped; it is not counted as a passing full run.
- The final focused driver/benchmark checks pass after the metadata and output-error updates.
  Benchmark checks cover schema, all CSV rows/percentiles, repeat hashes, warmup continuity,
  one-frame/zero-warmup boundaries, malformed/overflow/unsupported options and output failures.
- Two 120-warmup/1800-measured runs agree on state hash `8bca0cb6394cc24f`, 3840 total
  ticks, 17 births, peak 17 objects and one landing. Reports are
  `build/bench/arcade-baseline.{json,csv}` and `build/bench/arcade-repeat.{json,csv}`;
  build identifier `b06ecd2-dirty/Debug/AppleClang-21.0.0.21000101`.
  Observed simulation p50/p95/p99/max were 0.327/0.562/0.595/0.735 ms and
  0.282/0.567/0.586/0.673 ms. These are smoke measurements, partly concurrent with verification,
  not a controlled P3a performance baseline.

At the P2a handoff, P3a was next: capture the current arcade baseline under controlled conditions,
with the required representative CPU/GPU evidence before clock migration. Full P2/P3 remain
pending. No new physics tolerance, pixel baseline, handling change or user acceptance is claimed.
Windows/Linux/browser qualification and human motion/audio review were not performed.

## P3a trace preparation — 2026-10-23

The benchmark now additionally writes `<prefix>.state.csv` after all simulation ticks.
Its columns record every nominal frame, including warmup: absolute end tick, accumulated float
simulation time, X/Z position and velocity, height/vertical velocity, motion state, landings,
live wake objects, births and the rolling physical/wake hash. Storage is bounded and allocated
before warmup; copying these values happens after the timer stops. Existing JSON/timing CSV
formats and scenario/hash revisions remain unchanged. The new trace is diagnostic output,
not a change to the simulation or a portable replay contract.

The contract test checks trace row counts, finite values, end ticks, report agreement and exact
repeat traces, plus failure to open a trace output. `boat_benchmark_baseline.py` captures three
sequential 120-warmup/7200-measured runs and requires matching traces and the fixture's actual
17 births/peak, one landing, flight/landing states and eventual complete expiry.
This was an intermediate checkpoint; the controlled result is recorded below.

## P3a current arcade baseline — 2026-10-23

P3a is implemented for the finite `wake-loop` simulation fixture. Reproduce the controlled
measurement from the repository root after other test/build jobs finish:

```sh
cmake -S . -B build/p3a-release -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_RAYLIB="$PWD/build/_deps/raylib-src"
cmake --build build/p3a-release --target boat_benchmark boat_study -j
python3 tools/boat_benchmark_baseline.py build/p3a-release/boat_benchmark build/bench/p3a
```

This was run sequentially on a MacBookPro18,2 (Apple M1 Max, 64 GiB), macOS 26.6.2
(build 25G83), connected to AC with `pmset` AC `powermode=2`. The Release build uses
AppleClang 21.0.0.21000101, `-O3 -DNDEBUG`; configure-time identifier is
`e0440b2-dirty` because tracked documentation edits existed before the task. No benchmark
runs overlapped the build or CTest. Other system background activity and thermal state were
not measured. Times are development-machine observations, not hardware certification.

Each run has 120 warmup plus 7200 measured nominal frames: 14,640 ticks total, with
14,400 measured ticks. All 7,320 trace rows match across three sequential runs, as do the
final hash `c29e5d3d13d462c3`, 17 births, peak 17 live objects, one landing and eventual
zero live objects. The trace records airborne at tick 1118, landing at 1328, capsize at 1450
and wake expiry after tick 3468. At tick 14,640, accumulated `Boat.time` is 121.992271 s;
the nominal 122.0 s illustrates the float clock drift that C1/C2 must address.

| Run | Simulation p50 | p95 | p99 | max | Units |
| --- | ---: | ---: | ---: | ---: | --- |
| 1 | 0.065 | 0.136 | 0.158 | 0.250 | ms per two-tick frame |
| 2 | 0.065 | 0.136 | 0.156 | 0.249 | ms per two-tick frame |
| 3 | 0.065 | 0.136 | 0.155 | 0.189 | ms per two-tick frame |

The fixture stops powered turns at tick 1440, capsizes soon afterward and has no live wakes
after tick 3468. Thus 5585 of 7200 measured frames (77.6%) are after wake expiry. From
run 1, the 1615 measured frames through tick 3470 have p50/p95/p99/max
0.070/0.157/0.164/0.250 ms; the 5585 later frames have
0.064/0.067/0.075/0.154 ms. This baseline represents the specified finite return,
not sustained full-pool simulation. It does not establish the complete P3 workload matrix or
render/GPU timing; those fields remain `null` in the benchmark JSON.

Raw reports and trace are `build/bench/p3a/run-{1,2,3}.{json,csv,state.csv}`. The runner's
comparison output is `build/bench/p3a-runs.log`; `build/bench/p3a/sha256.json` records hashes
of those files and the images below. These ignored build artifacts are retained locally;
reproduction commands and the final state hash provide the durable baseline contract.

Representative Release captures were inspected at 320x200:

| Local artifact | Evidence |
| --- | --- |
| `build/bench/p3a/views/{cpu,gpu,gpu-aa}-t9.png` | Wake approach and visible return path; GPU AA smooths edges |
| `build/bench/p3a/views/{cpu,gpu,gpu-aa}-t11.png` | Airborne hull and wake encounter |
| `build/bench/p3a/views/{cpu,gpu,gpu-aa}-t20.png` | Capsized/settled hull and remaining wake |
| `build/bench/p3a/views/cpu-t60.png` | Wake-free later state |

CPU and GPU use different water/presentation paths, so these images are representative views,
not pixel-equality baselines. The capsize and long settled portion are observed fixture behavior;
there is no new visual or handling acceptance claim. A scripted Release
`./build/p3a-release/boat_study --demo --frames 180` desktop run completed 376 ticks. No
independent human control/audio review was performed.

Debug `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` and `cmake --build build -j` pass.
`ctest --test-dir build -j 4 --output-on-failure` passes all 85 active cases, including
20 GPU cases, in 114.10 s with desktop access (`build/bench/p3a-ctest.log`); fifteen retired
physical-field cases remain disabled. The focused trace/schema and frozen-driver checks also
pass. Release raylib compilation emitted one bundled `stb_vorbis.c` tautological-comparison
warning; project sources remained warning-clean. Windows/Linux/browser results were not
collected for P3a.

The multiplayer prerequisite track proceeds to C1, followed by C2/C3 before world/session
extraction. Full P2 renderer counters and remaining P3 scenarios stay pending.

## C1 clock comparison — 2026-10-23

The C1 Release smoke command below uses the same 120-warmup/7200-measured finite arcade fixture
as P3a, after building the changed sources. It is a behavior comparison; C3 will establish the
replacement performance baseline after the remaining time consumers migrate.

```sh
cmake -S . -B build/p3a-release -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_RAYLIB="$PWD/build/_deps/raylib-src"
cmake --build build/p3a-release --target boat_benchmark boat_study -j
mkdir -p build/bench/c1
./build/p3a-release/boat_benchmark --scenario wake-loop --mode simulation --renderer none \
  --warmup 120 --frames 7200 --output build/bench/c1/wake-loop
```

The C1 trace at `build/bench/c1/wake-loop.state.csv` has the same 7320 rows, births 17,
peak 17, one landing, motion transitions at ticks 1118/1328/1370/1450 and last live wake
at tick 3468 as P3a. Every CSV physical/wake column except `time` and the rolling hash matches
P3a at every row. C1 ends at exactly 122 s versus P3a's accumulated 121.992271 s; its
rolling hash is `24d582aae45971b4`. The one-run p50/p95/p99/max of
0.063/0.140/0.152/0.243 ms are diagnostic only, collected on the same M1 Max/AC development
machine, not a controlled replacement baseline.

Release CPU and GPU captures at 9, 11 and 20 s in `build/bench/c1/` match the corresponding
P3a PNG files byte for byte and were inspected. A scripted Release windowed demo and basin
course run of 180 frames each completed (379 and 376 ticks respectively). No human handling,
focus, pause or audio listening acceptance is inferred. At C1, the water/traffic and shader
time consumers still narrowed to float; the C2 comparison below records their migration.

## C2 time-consumer comparison — 2026-10-23

C2 carries double world time into water, traffic, scene actors and shader payloads (see the
[C2 handoff](BOATING_FOUNDATIONS_PLAN.md#c2-handoff--2026-10-23)). The same Release fixture
was run three times after building the changed sources:

```sh
cmake --build build/p3a-release --target boat_benchmark boat_study harbor_review -j
mkdir -p build/bench/c2
for run in 1 2 3; do
  ./build/p3a-release/boat_benchmark --scenario wake-loop --mode simulation --renderer none \
    --warmup 120 --frames 7200 --output build/bench/c2/run-$run
done
```

All three `build/bench/c2/run-{1,2,3}.state.csv` traces are byte-identical to
`build/bench/c1/wake-loop.state.csv`: 7320 rows, final time 122 s, hash `24d582aae45971b4`.
That is expected because this fixture uses arcade wake objects (already age-packed) and no
ambient waves or D3 trail. Simulation p50 is 0.064 ms in each run; p95/p99/max are
0.133/0.147/0.199, 0.129/0.146/0.255 and 0.131/0.147/0.228 ms. These are diagnostic
same-machine observations, not the C3 replacement baseline.

Release `boat_study --renderer cpu|gpu --time 9|11|20 --screenshot ...` captures in
`build/bench/c2/` match C1 byte for byte, except `gpu-t11.png`: two pixels differ by one
level. The basin's ambient amplitudes are zero there; the difference is GPU rounding from the
restructured shader expression, not a time change. Traffic captures from
`./build/p3a-release/harbor_review --renderer cpu|gpu --view traffic --waves --time T` at
T = 40, 86400.5 and 2591999.5 s (`build/bench/c2/{cpu,gpu}-traffic-*.png`) were inspected:
vessels, trails, waves and reflections remain coherent at 24 hours and 30 days.

The water path no longer copies `BoatWater` for every internal query. On the same M1 Max, a
Release CPU `boat_study --location eriksberg --waves --player-wake --time 60` screenshot
run took 1.29 s versus 1.48 s for `6500a65`; the default basin run took 0.33 s versus
0.35 s. These are wall-clock observations that include startup and one render.

## C3 replacement baseline — 2026-10-23

This is the adopted baseline for W/S work on the multiplayer prerequisite track. It was
measured sequentially after the build and CTest jobs finished, on the same MacBookPro18,2
(Apple M1 Max, 64 GiB), macOS 26.6.2, on AC with `pmset` `powermode=2`. Release
AppleClang 21.0.0.21000101; configure-time identifier `ce3b53d/Release` (clean tree).

```sh
cmake -S . -B build/p3a-release -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_RAYLIB="$PWD/build/_deps/raylib-src"
cmake --build build/p3a-release --target boat_benchmark boat_study validate_boat_clock -j
python3 tools/boat_benchmark_baseline.py build/p3a-release/boat_benchmark build/bench/c3/baseline
mkdir -p build/bench/c3
./build/p3a-release/validate_boat_clock --soak 3600 build/bench/c3
./build/p3a-release/validate_boat_clock --soak 3600 build/bench/c3 --waves
```

| Run | Simulation p50 | p95 | p99 | max | Units |
| --- | ---: | ---: | ---: | ---: | --- |
| 1 | 0.063 | 0.132 | 0.144 | 0.240 | ms per two-tick frame |
| 2 | 0.063 | 0.130 | 0.153 | 0.193 | ms per two-tick frame |
| 3 | 0.063 | 0.126 | 0.144 | 0.813 | ms per two-tick frame |

All three traces (`build/bench/c3/baseline/run-{1,2,3}.state.csv`, runner output
`build/bench/c3/baseline-runs.log`) are identical to each other and to C1/C2: 7320 rows,
17 births/peak, one landing, airborne at 1118, landing at 1328, capsize at 1450, wake expiry
after 3468, final time exactly 122 s, hash `24d582aae45971b4`. Relative to P3a, only derived
time and the rolling hash (which folds time) changed. P3a's 0.065 ms p50 and this 0.063 ms
are the same within run-to-run noise. Run 3's 0.813 ms max is a single outlier; other
background activity was not measured.

Release views in `build/bench/c3/views/` match the P3a files for `cpu-t{9,11,20,60}`,
`gpu-t20` and `gpu-aa-t20`; `gpu-t11`, `gpu-aa-t9` and `gpu-aa-t11` differ in two pixels
by one level (C2 shader restructure). They were inspected.

The simulated hours each run 432,000 ticks; every tick checks finiteness, basin containment,
exact tick-derived time, wake-world tick agreement, `emitted == expired + live` and capacity.

| Soak | Births | Peak live | Landings | Recoveries | Max speed | Max height | CPU | Final hash |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Calm | 11,401 | 63/128 | 263 | 86 | 23.5 m/s | 14.4 m | 93.9 s | `43dfc74ddac0055d` |
| Waves | 11,587 | 64/128 | 256 | 86 | 23.2 m/s | 12.9 m | 97.6 s | `56e31ce5c3d7a7c4` |

Logs and CPU frames at 60/1800/3600 s are `build/bench/c3/soak{,-waves}.log` and
`build/bench/c3/soak{,-waves}-{60,1800,3600}s.png`. Both hours ran concurrently on separate
cores, so their CPU seconds are indicative only. The soak is a stability gate, not a timing
baseline; full P2 render counters and the remaining P3 workloads are still pending.

## W1/W3 basin comparison — 2026-10-23

The [W1/W3 basin slice](BOATING_FOUNDATIONS_PLAN.md#w1w3-basin-slice-handoff--2026-10-23)
is a structural change: location, water and clock moved from `Boat` into explicit
`BoatWorld`/`BoatSession` owners. It is compared against the C3 baseline and against a
detached `e83f545` worktree built the same way, on the same MacBookPro18,2 (M1 Max, macOS
26.6.2), Release AppleClang 21.0.0.21000101, `-DFETCHCONTENT_SOURCE_DIR_RAYLIB` as for C3.

```sh
cmake -S . -B build/w3-release -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_RAYLIB="$PWD/build/_deps/raylib-src"
cmake --build build/w3-release -j
python3 tools/boat_benchmark_baseline.py build/w3-release/boat_benchmark build/bench/w3/baseline
./build/w3-release/validate_boat_clock --soak 3600 build/bench/w3/soak-new
./build/w3-release/validate_boat_clock --soak 3600 build/bench/w3/soak-new --waves
```

| Check | Result |
| --- | --- |
| P3a traces, three runs | Byte-identical to `build/bench/c3/baseline/run-{1,2,3}.state.csv`; hash `24d582aae45971b4`; p50 0.064/0.063/0.064 ms (C3 0.063) |
| Hour soaks, calm/waves | Identical to `e83f545` and C3: hashes `43dfc74ddac0055d` / `56e31ce5c3d7a7c4`, same births, landings, recoveries; all six frames byte-identical |
| Validators/review tools | 17 outputs (handling, banking, turn entry, environment, replay, profile, drive, clock, harbor, traffic, banking review, four water routes, object wakes, flag) identical except one object-wake timing line |
| `boat_study` captures | 19 CPU/GPU frames (basin, waves, crossing+waves, D3, current/crossing, Eriksberg waves+D3 and current, river waves, course, CPU wake field) byte-identical |
| Replay views | Calm banked replay at 3/8 s, ghost and wave replay, CPU and GPU, byte-identical |
| C3 views | `cpu-t{9,11,20,60}` byte-identical |

The soaks for both trees ran concurrently on separate cores, so their CPU seconds are not a
timing comparison. Artifacts: `build/bench/w3/{ref,new,replay,soak-head,soak-new,val-head,
val-new,baseline}` and `build/bench/w3/baseline-runs.log`. No intended simulation or render
behavior changed, so this slice adopts no new baseline; C3 remains the reference.

## S1/S2 basin comparison — 2026-10-23

The [S1/S2 basin slice](BOATING_FOUNDATIONS_PLAN.md#s1s2-basin-slice-handoff--2026-10-23)
moves pose math, views and cosmetic state out of the boat physics. It is compared against a
detached `5a33f8d` worktree built the same way, on the same MacBookPro18,2 (M1 Max, macOS
26.6.2), Release AppleClang 21.0.0.21000101, `-DFETCHCONTENT_SOURCE_DIR_RAYLIB` as before.
`build/bench/s2/compare.sh BUILD OUT FIXTURES` runs the validators, a default 120+1800-frame
benchmark and the captures for one tree; the hour soaks used
`validate_boat_clock --soak 3600 DIR [--waves]`.

| Check | Result |
| --- | --- |
| Validators/review tools | 18 outputs (handling, banking, turn entry, environment, replay, profile, drive, clock, harbor, traffic, banking review, four water routes, object wakes, flag, benchmark) exit 0 and match except one object-wake timing line |
| Benchmark trace | Byte-identical state CSV, hash `2c318477117e222e`; p50 0.070 ms vs 0.069 ms (presentation on) |
| Hour soaks, calm/waves | Identical to `5a33f8d` and C3: hashes `43dfc74ddac0055d` / `56e31ce5c3d7a7c4`, same births, landings, recoveries; all six frames byte-identical |
| Captures | 54 CPU/GPU frames byte-identical: default 3/9/11/20 s, waves, crossing boat (calm, waves), Eriksberg (calm, waves, current/crossing), river (calm, waves, current/crossing), D3, current/crossing/waves, course, banked replay 3/8 s, fixture replay, ghost, wave replay, ferry, marina, demo 48 s, CPU wake field, flag |

Both trees' soaks ran concurrently, so CPU seconds are not a timing comparison. Artifacts:
`build/bench/s2/{out-head,out-new,soak-head,soak-new,fixtures}`. No intended simulation or
render behavior changed, so this slice adopts no new baseline; C3 remains the reference.

## S3 basin comparison — 2026-10-23

The [S3 basin slice](BOATING_FOUNDATIONS_PLAN.md#s3-basin-slice-handoff--2026-10-23) moves course
and run orchestration into the session, adds `BoatViewState` and defers finish saves until after
the tick loop. It was compared against a detached `65804ea` worktree with the same setup:
MacBookPro18,2 (M1 Max, macOS 26.6.2), Release AppleClang 21.0.0.21000101 and
`-DFETCHCONTENT_SOURCE_DIR_RAYLIB`. Both trees ran `build/bench/s3/compare.sh` (a copy of the S2
script) concurrently. Afterwards, the hour soaks ran on the new tree.

| Check | Result |
| --- | --- |
| Validators/review tools | 18 outputs exit 0 and match except the object-wake timing line and the replay validator's new S3 flush notices |
| Benchmark trace | Byte-identical state CSV, hash `2c318477117e222e`; p50 0.069 ms vs 0.068 ms |
| Hour soaks, calm/waves | Hashes `43dfc74ddac0055d` / `56e31ce5c3d7a7c4`, equal to C3 and S1/S2; the six frames match the S1/S2 soak frames byte for byte |
| Captures | The same 54 CPU/GPU frames as S1/S2 are byte-identical to `65804ea` |

The concurrent runs make the timings unsuitable for comparison. Artifacts are in
`build/bench/s3/{out-head,out-new,soak-new,fixtures}`. No simulation or render behavior was
intended to change, so C3 remains the reference baseline.

## S4 basin comparison — 2026-10-23

The [S4 basin slice](BOATING_FOUNDATIONS_PLAN.md#s4-basin-slice-handoff--2026-10-23) moves
boating code into static libraries and splits water meshes and city geometry out of
simulation files, without formula changes. It was compared against a detached `d65d469`
worktree with the same setup: MacBookPro18,2 (M1 Max, macOS 26.6.2), Release AppleClang
21.0.0.21000101 and `-DFETCHCONTENT_SOURCE_DIR_RAYLIB`. Both trees ran
`build/bench/s4/compare.sh` (a copy of the S3 script). The new tree was rerun after its
final header rename/formatting pass, and the hour soaks ran on the new tree.

| Check | Result |
| --- | --- |
| Validators/review tools | 18 outputs exit 0 and match except the object-wake timing line |
| Benchmark trace | Byte-identical state CSV, hash `2c318477117e222e`; p50 0.067 ms vs 0.068 ms |
| Hour soaks, calm/waves | Hashes `43dfc74ddac0055d` / `56e31ce5c3d7a7c4`, equal to C3; the six frames match the S3 soak frames byte for byte; 92.6 s / 96.5 s CPU |
| Captures | The same 54 CPU/GPU frames are byte-identical to `d65d469` |

The `d65d469` outputs came from a concurrent run, so the timings are not comparable.
Artifacts are in `build/bench/s4/{out-head,out-new,soak-new,fixtures}`. No simulation or
render behavior was intended to change, so C3 remains the reference baseline.

## S3 `BoatInput` comparison — 2026-10-23

The [`BoatInput` change](BOATING_FOUNDATIONS_PLAN.md#s3-boatinput-handoff--2026-10-23)
replaces the two step-rate floats with one input value, without formula changes. It was
compared against a detached `ba83d53` worktree with the same setup: MacBookPro18,2 (M1 Max,
macOS 26.6.2), Release AppleClang and `-DFETCHCONTENT_SOURCE_DIR_RAYLIB`. Both trees ran the
S4 comparison script one after the other.

| Check | Result |
| --- | --- |
| Validators/review tools | 18 outputs exit 0 and match except the object-wake timing line |
| Benchmark trace | Byte-identical state CSV, hash `2c318477117e222e`; p50 0.067 ms |
| Captures | The same 54 CPU/GPU frames are byte-identical to `ba83d53` |

The hour soaks were not rerun. Artifacts are in `build/bench/input/{out-head,out-new}`. No
simulation or render behavior was intended to change, so C3 remains the reference baseline.

## M2 fleet comparison — 2026-10-23

The [multiplayer M2 handoff](MULTIPLAYER_PLAN.md#m2-local-authority-handoff--2026-10-23) adds
the local fleet authority. Only `--wake-objects-crossing` was meant to change. Same setup as
the `BoatInput` comparison: MacBookPro18,2 (M1 Max, macOS 26.6.2), Release AppleClang,
`-DFETCHCONTENT_SOURCE_DIR_RAYLIB`, a detached `1ed6b70` worktree, and the S4 comparison
script run on both trees one after the other.

| Check | Result |
| --- | --- |
| Validators/review tools | 18 outputs exit 0 and match except the object-wake timing line |
| Benchmark trace | Byte-identical state CSV, hash `2c318477117e222e` |
| Captures | 52 of 54 CPU/GPU frames byte-identical; the CPU and GPU `--wake-objects-crossing --waves --time 9` frames differ as intended (the rival now shares the player's waves). Calm crossing at 12 s is identical (no contact or recovery yet) |

Scripted authority, Release `boat_server --scripted --ticks 432000` (one simulated hour, run
as fast as possible, timing only `boat_fleet_step`):

| Boats | Mean / p50 / p95 / p99 / max tick (ms) | Contact pairs | Max passes / pair tests | Residual ticks | Peak live wakes | Resets |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 0.0049 / 0.0050 / 0.0060 / 0.0069 / 0.061 | 0 | 0 / 0 | 0 | 32 | 0 |
| 2 | 0.0111 / 0.0110 / 0.0131 / 0.0150 / 0.124 | 932 | 1 / 2 | 0 | 64 | 258 |
| 4 | 0.0268 / 0.0269 / 0.0310 / 0.0341 / 0.378 | 5126 | 4 / 30 | 53 | 109 | 727 (22 blocked retries) |

Four boats stay far inside the proposed 1 ms p95 / 2 ms p99 authority budget, with no
emission failures and at most six pair tests per pass. Thirty is five detection rounds of six
pairs (four applied passes plus the residual check). The bots capsize often, so resets are
frequent. These are same-machine engineering measurements with no networking, encoding or
snapshots. Artifacts: `build/bench/m2/{out-head,out-new,server-release.txt}`. The hour soaks
were not rerun; the C3 soak hashes no longer apply because the soak drives crossing mode.
