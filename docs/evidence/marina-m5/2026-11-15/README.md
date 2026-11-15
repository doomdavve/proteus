# M5 W6 integrated example evidence — 2026-11-15

Source: branch `feat/marina-m5` at W5 (`3f74d29`, `v0.0.31-53-g3f74d29`) plus the W6 example
`plugins/marina-integrated-demo`, its tests and the manual tool
`tests/plugins/measure_marina_package.c`. Platform: Apple M1 Max, macOS (Darwin 25.6), native
Debug build (AppleClang 21), `--renderer cpu` for CPU stills and the CPU sweeps, the desktop GPU
for the GPU stills and windowed runs. Local only; nothing pushed. Debug is what #131's note used
("Apple Silicon/macOS, Debug"); no Release build was made.

## Checks

- `python3 tools/plugin_author.py validate plugins/marina-integrated-demo`: ok (14 berths, 15
  modules, 6 traffic vessels, 1 flock). `sync-shared --check` and CTest `plugin_shared`: the
  package's `eriksberg_marina` copy is byte-equal to `plugins/_shared`.
- New CTest cases: `boat_plugin_session.marina_integrated_apply_undo` (owner at ticks, half-open
  windows, module-only edit, Apply, Undo, four failed module edits), a real stage-worker block in
  `boat_plugin_modules`, and `boat_plugin_author` `SharedModules.test_integrated_marina_example`
  (export/import round trip, worker, module-only edit, rejected crossing, module syntax error).
- Valid visitor `start_tick` on a 120-tick grid (validated with the worker, one copy of the
  package per value): 0, 120, 240 and 1440-3600 are ok; 360-1320 are rejected with
  `crosses scene.traffic.vessels[5]`.

## Stills (inspected by the agent)

CPU, 320x200, `--renderer cpu`, boat parked north of the docks so the tender cannot push it
(`--start 0,246,0 --camera-distance 14 --camera-height 30 --fixed-fov`, `--time T`):

- [cpu-dock-01s.png](cpu-dock-01s.png): two brown platforms; a small hull with a red deck beside
  the left platform (tender at home); the right platform and slip are empty (visitor absent).
- [cpu-dock-15s.png](cpu-dock-15s.png): a red-deck hull mid-water left of centre (tender on its
  racetrack), a blue-deck hull beside the right platform (guest moored), left platform empty.
- [cpu-dock-22s.png](cpu-dock-22s.png): a grey hull in the upper left on the racetrack; right
  platform empty (visitor gone, tender still away).
- [cpu-dock-26s.png](cpu-dock-26s.png): the red-deck hull back at the left platform, right
  platform empty (tender home, visitor gone). Matches the 1 s frame.
- [cpu-close-tender-01s.png](cpu-close-tender-01s.png) and [cpu-close-guest-15s.png](cpu-close-guest-15s.png)
  (`--camera-distance 8 --camera-height 6`, boat at x = -16 and 12): the platform and the red
  or blue hull beyond it, distinct deck colours; names are not legible at 320x200.
- [cpu-marina-03s.png](cpu-marina-03s.png): the #131 camera `outer.marina` (`--start 2,161,90`): the
  two-row marina with its hulls, masts and walkways. The guest dock is not in this view.

GPU, 1280x800 (`--renderer gpu --resolution high`; a graphics context was available):

- [gpu-dock-15s.png](gpu-dock-15s.png) (`--camera-distance 24 --camera-height 36`): the blue
  striped guest hull moored at the right platform, a red-and-white hull on the racetrack at the
  top middle, the left platform empty, and another red hull with a wake trail at the top left
  (the shared marina's passing craft, not identified from the still).
- [gpu-dock-26s.png](gpu-dock-26s.png): a red-and-white hull beside the left platform, nothing at
  the right.
- [gpu-guest-moored-15s.png](gpu-guest-moored-15s.png): `GUEST` painted on the blue hull's side
  and `TENDER` on the red hull on the racetrack behind it, the marina rows in the distance.
- [gpu-tender-south-01s.png](gpu-tender-south-01s.png): the tender at its platform from the south,
  `TENDER` on its side.
- [gpu-names-01s.png](gpu-names-01s.png): the shared marina's row two, `MIRA` on a blue hull, the
  yellow hull `SALT` and a cream-and-red hull from the fairway.

**Finding, not caused by W6.** In these in-game GPU stills the painted lettering (`MIRA`,
`GUEST`, `TENDER`) is legible but reads mirrored, from both sides of the hulls. The same `MIRA`
view rendered from `plugins/harbor-life` is a byte-identical PNG, so the example does not change
it, and `./build/harbor_review --view names-port|names-starboard --renderer gpu` reads `SALT` and
`MIRA` correctly, so the cause is somewhere in the game's GPU path or its options (not
isolated). The plan's M2 note says names read correctly from each side. A still cannot show
motion; handoff pops and wave bobbing are unjudged.

## Measurements against #131

Method of the #131 note (`validate_harbor_life`): 320x200 CPU frames, 180 frames, camera
`(2, 161)` at 90 degrees, simulated time `frame / 60` from 0 s, the default camera, one frame
timed per `boat_draw_scene`. `measure_marina_package` reproduces it for any package and prints
the same quantities. The #131 numbers (9.171/9.510 ms compiled/populated) were recorded on
2026-11-01 and are not reproduced by today's tree on this host, so the baseline below is
`harbor-life` at the same HEAD, measured now. `validate_harbor_life` today printed compiled
13.685/14.024 ms, populated 14.260/14.704 ms and supported maximum 18.488/79.278 ms (p50/p95).

CPU render p50 ms (p95 in brackets), 3 runs each, run order interleaved; "compiled" is the
engine's own scene in the same process, the noise reference:

| Sweep | harbor-life (12 berths) | integrated (14 berths, 2 vessels) | compiled |
| --- | --- | --- | --- |
| `outer.marina`, 0-3 s | 14.00, 14.05, 14.02 (14.34, 14.30, 14.31) | 14.01, 14.23, 14.29 (14.91, 14.48, 14.60) | 13.73, 13.66, 13.61 and 13.63, 13.45, 13.71 |
| `outer.marina`, 10-13 s | 13.99, 13.97, 13.95 | 13.95, 14.19, 14.31 | 13.4-13.8 |
| guest dock `(0, 246)` at 0 degrees, 12-15 s | 20.33, 20.30, 20.46 | 20.52, 20.60, 20.66 | 20.0-20.2 |

The integrated example is about 0.2 ms (about 1.5%) slower in the means (0.16 ms, 0.18 ms and
0.23 ms in the three sweeps), in the same direction in every run but within about
0.1-0.2 ms run-to-run spread of the compiled reference; treat it as small, not as a measured
regression. At the marina camera the guest dock is out of view, so that sweep mostly measures
the 14 compiled-versus-12 berth lists and two model variants; the dock sweep sees both vessels.

Staging and memory (Debug, in one process; 8 samples per run at the marina sweep, 3 runs):

| Quantity | harbor-life | integrated |
| --- | --- | --- |
| Source image bytes (limit 64 KiB) | 7,700 (entry plus one module) | 12,013 (entry plus four modules) |
| In-process stage p50 | 786.0, 785.1, 787.4 ms | 831.3, 831.2, 833.6 ms |
| Worker-process stage p50 (real `boat_plugin_worker`) | 1613.3, 1613.0, 1613.2 ms | 1704.0, 1705.1, 1706.1 ms |
| Lua heap peak, one evaluation | 39,134 B | 61,321 B |
| Lua instructions (hook steps), fields decoded | 300, 249 | 4,000, 376 |
| Native owner (`sizeof` host, reserved) | 907,640 B | 907,640 B |
| Hull model CPU bytes, builds | 162,704 B, 3 | 269,264 B, 5 |

Staging is about 46 ms (5.8%) slower in process and about 92 ms (5.7%) slower through the worker.
The Lua evaluation itself is small (above); the rest of the 786 ms was not profiled. The native owner
is a fixed-size slot array, so it does not grow with the package; the added cost is the Lua heap
(2.9% of its 2 MiB limit) and the larger per-session hull model cache. The wall-clock readings
come from a manual tool run only; no test measures time.

Windowed GPU runs (`--demo --demo-idle --no-vsync --frames 1800 --start ...`, the rolling
capture's `summary.json`; 3 runs each, order interleaved, all exit 0, 3,607 ticks per run so the
whole 25 s timetable plays):

| View | harbor-life gpu_ms p50 | integrated gpu_ms p50 | frame p50 |
| --- | --- | --- | --- |
| `--start 2,161,90` (the #131 camera) | 4.06, 4.03, 4.37 | 4.06, 4.18, 4.34 | 16.65 ms for all |
| dock, `--start 0,246,0 --camera-distance 24 --camera-height 36` | 3.29, 3.29, 3.55 | 3.36, 3.46, 3.46 | 16.65 ms for all |

The GPU figures show no difference beyond the run-to-run spread. CPU `render_ms` in the same
captures varied from 2.1 to 4.6 ms between runs for both packages alike (the third run of each
pair is higher), so it is not used. Frame p50 sits at the 60 Hz pacing; the first frame carries
the roughly 800 ms stage (max frame about 800-880 ms in every run, both packages). A 3,300-frame
run of the integrated example with the performance report completed 6,610 ticks. The agent did
not watch any of these runs: they show the package loads and plays through the resident round
trip and the whole visit without a crash or restart, and nothing about smoothness or handoff
pops.

## Commands

From the worktree root:

```sh
./build/wake_velocity --plugin plugins/marina-integrated-demo --demo-idle --start 0,246,0 \
  --camera-distance 14 --camera-height 30 --fixed-fov --renderer cpu --time 15 --screenshot FILE
./build/wake_velocity --plugin plugins/marina-integrated-demo --demo-idle --start 0,246,0 \
  --camera-distance 24 --camera-height 36 --fixed-fov --renderer gpu --resolution high \
  --time 15 --screenshot FILE
./build/measure_marina_package plugins/marina-integrated-demo --stage-samples 8
./build/measure_marina_package plugins/harbor-life --stage-samples 8
./build/measure_marina_package plugins/marina-integrated-demo --camera 0,246,0 --time 12 --stage-samples 1
./build/wake_velocity --plugin plugins/marina-integrated-demo --demo --demo-idle --start 2,161,90 \
  --no-vsync --frames 1800 --performance-report build/m5/perf/RUN
```

[measurements.txt](measurements.txt) holds the raw tool output and the windowed summaries.

## Not run

Browser/Wasm (no Emscripten here), a room end to end with the module package, independent
sessions, Windows/Linux, a Release build, a watched motion review and user acceptance.
