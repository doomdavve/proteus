# A0.1 original river reference — 2026-10-25

## Environment and compatibility

- Starting revision: `7165ba5f6cf06ee364e17c6b8389aaf2bd7e528b` on a dirty working tree.
  Existing modified planning files: `README.md`, `docs/BOATING_FOUNDATIONS_PLAN.md`,
  `docs/BOATING_PLAN.md`, `docs/README.md`, `docs/river/README.md`; new planning files:
  `docs/FREE_RIDING_ADVENTURE_FIRST_STEPS.md`, `docs/FREE_RIDING_ADVENTURE_PLAN.md`.
  These were present before A0.1 and preserved.
- Machine: Apple M1 Max, 24-core integrated GPU, arm64; macOS 26.6.2 (Darwin 25.6.0).
  Compiler: Apple clang 21.0.0 (`clang-2100.1.1.101`). CMake Debug. CPU model query was
  denied by the sandbox; the chipset identification comes from `system_profiler`.
- Simulation: 120 Hz; physics revision 8. Network protocol v5; replay format v2 and course
  revision 1. The review route is scripted and is not a replay file.
- Current mapped location: river crop west/south/east/north `−1500, −1600, 950, 800` metres
  relative to the crane in EPSG:3007; simulation X is opposite projected east. Compiled
  geometry is `assets/geometry/river/geometry.h`. No second mapped location remains.
- Review captures: 320 × 200 virtual image. GPU review requests a hidden 960 × 600 window,
  4 × 4 supersampling (1280 × 800 internal target) and nearest-neighbour presentation.
  The CPU review uses 320 × 200. Physical monitor resolution, refresh rate, VSync setting,
  driver GPU completion and display frame cap for the hidden review are **unknown**. The
  review sets 60 FPS only in visible mode, which was not used for this task.

| Reference file | SHA-256 |
| --- | --- |
| `assets/geometry/river/geometry.h` | `1a992cc9df07338b573edbf1cf58efd26668cc6be114f46c9557dbc0d28e60a1` |
| `assets/geometry/river/manifest.json` | `5932e28abd3ad041b1db30f1689da1ae907911954e256bec35ab070ccc7490f1` |
| `docs/river/osm-survey.json.gz` | `a0b721a97e5ceb79e6256a31ef2b1068a233b16040da854d47f263470350dbac` |
| `docs/river/summary.json` | `f77f0b07c25538eaab0d32d43d7566a1341e0db696a8b74b456b79969ec59308` |

The source importer pins the **decompressed** OSM JSON to
`688c7cf277af904c44ce820c941b83cd23f383b9d5a492040edcf46cfcb6778a`.
The compressed-file hash above is a separate check. Source coverage, assumptions and
attribution remain in [the river guide](../river/README.md).

## Map, cache and caller inventory

This inventory follows `rg` references to `waterfront_city_mesh`, `boat_map_water`,
`boat_water_*mesh/patch/surface`, `waterfront_corner_boxes`, and the location/world/session
types across `src/` and `tools/` at the starting revision.

| Resource / callers | Owner, construction and release | Map dependency and A1 consequence |
| --- | --- | --- |
| Compiled city mesh (`waterfront_city.c`, `geometry.h`) | Static arrays and function-local `Mesh3D`; process lifetime, no destructor | `boat_map.c`, `boat_water_mesh.c`, CPU `waterfront.c`, GPU `renderer_gpu.c`, `waterfront_corner.c`, `boat_net_content_digest()` and validators all select this single mesh implicitly. Bridge triangle offset is another global layout fact. |
| Shore/contact cache (`boat_map.c`) | Static heap `shore` and counts, lazy `init()`, `boat_map_destroy()` frees edges; cached mesh pointer survives destroy until next init | Water-triangle first ordering and boundary edge extraction drive hull contact, clearance, spawns, route review, particle filtering and gate/validator queries. All mapped worlds currently share it. |
| CPU water surfaces (`boat_water_mesh.c`) | Static `surfaces[2]`, `candidate_surface`, four `patches` and their centre keys; `realloc` on demand, `boat_water_mesh_destroy()` frees them | Mapped subdivision reads the city water triangles. Patch key uses mapped/fine and centre, not map identity. Returned meshes borrow storage invalidated by rebuild/destroy. CPU draw/review and GPU patch upload use these APIs. |
| Corner dressing (`waterfront_corner.c`) | Static 16,384-box arrays, bounds, counters and `initialized`; lazily assembled from city mesh; no reset/destructor | CPU `waterfront.c`, GPU `renderer_gpu.c` and `validate_city_dressing.c` read it. A new map cannot reuse the first map's frontages/quays. |
| CPU renderer (`waterfront.c`, `renderer3d.c`) | Caller-owned `Renderer3D`; `renderer3d_destroy()` frees its render buffers | `waterfront_draw_land()` obtains city mesh each draw, skips water prefix, derives facade windows and corner dressing. `boat_view.c` also uses map water for particles/course markings. |
| GPU scene (`renderer_gpu.c`, `boat_gpu.c`) | Caller-owned `BoatGpu` and `WaterfrontGpuScene`; init builds/uploads city, facade, roof, quay, water, window, corner and bridge meshes; destroy unloads GPU resources. `BoatGpu` owns wave meshes, target textures and mapped patch centre/capacity. | Main, reflection and shadow passes reuse uploaded mapped geometry. `build_city()`, `build_windows()` and `build_corner()` read the implicit city. Wave patch topology/capacity is map-sensitive; view switching and concurrent screenshots need explicit binding. |
| Other process caches | `waterfront_harbor.c` and `waterfront_traffic.c` lazily create fixed location boat/quay meshes; `waterfront_scene.c` caches sky image; `boat_net_protocol.c` caches content digest forever | Harbor/traffic/scenery are tied to the current authored river presentation, though their visual mesh recipes do not derive from the city triangles. Sky is location-independent today. Digest includes every location definition and each mapped mesh triangle's indices/vertex float values, so any map replacement must invalidate or recompute compatibility. |

Direct and indirect callers include `boat.c` hull contact; `boat_view.c` particle/marker
filtering; `boat_fleet.c` placement, spawn and contact; `boat_net_server.c` and
`boat_net_client.c` compatibility; `wake_velocity.c`, `wake_velocity_drive.c`,
`wake_velocity_net.c`, `boat_server.c` and `boat_bot.c`; `boat_water_review.c`,
`boat_race_trial.c`, `boat_banking_review.c`, `harbor_review.c`,
`wake_objects_review.c`, `wake_play_review.c`; plus the boat/core/world/net/race/banking/
clock/environment/turn/harbor/traffic/city validators. The review tools explicitly destroy
water meshes and map edges where used; `wake_velocity.c` also calls both destructors at
shutdown. The static harbor/traffic/corner caches have no release path.

### Copy and lifetime contract

`BoatWorld` is assignable: its definition, optional wake field and `BoatWater.objects` are
borrowed pointers; its water history is by value. `BoatSession` is often copied by value in
validators and reviews (`validate_boat.c`, `validate_boat_banking.c`, `wake_play_review.c`,
`wake_objects_review.c`, `boat_banking_review.c`). This is safe only while its `runs` has no
owned recording. `boat_session_destroy()` releases recordings; `boat_session_restart()`
flushes, saves the runs, reinitializes and restores them. `boat_session_respawn()` resets
boat/presentation but retains world/clock. The main application's location switches and
restart path, screenshot/session construction, and network view replacement must carry a
borrowed map reference without freeing it from copied sessions.

`BoatFleet` contains a `BoatWorld` and its own wake pool. `boat_fleet_world()` returns a
**copy** with `water.objects` rebound to that fleet's pool. `boat_fleet_restore()` rebinds
after checkpoint restore; `boat_prediction_reset()` copies an authority fleet and rebinds
again. `boat_net_client.c` late-join/resync builds a new fleet from the negotiated location,
assigns it into the mirror and restores pointer-free `BoatFleetState`; it may reset
prediction. `boat_net_server_init()` constructs the authority fleet; server spawn/reset and
client correction then use that fleet's map. Checkpoints and network snapshots contain
mutable state, not resource ownership. `wake_velocity_net.c` replaces the session world with
`boat_fleet_world()` for connected views; `wake_velocity_drive.c` also obtains a temporary
world copy. A1 must keep the referenced map alive across all of these copies, correction,
late join, reset and cleanup paths, and validate definition/map consistency. Heap ownership
inside `BoatWorld` or copyable `BoatSession` would make plain assignment unsafe.

## Route and capture reference

All raw evidence is under `build/adventure/a0-original/` (ignored by Git). The command set
is the one in [A0.1](../FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a01--preserve-the-current-reference).
`check.log` and `check-waves.log` passed the support/water/crop and hull-clear route checks.
The flat route ends at tick **12125**, time **101.042 s**, pose **(1088.337, −947.588)**,
heading **1.367 rad**. The wave route ends at the same tick/time, pose
**(1088.350, −947.653)**, heading **1.370 rad**. Under-bridge markers occur at ticks
6928 and 6917 respectively. These are scripted physical route outcomes, not navigation
certification or a human playthrough.

The CPU review completed: `river-cpu.log`, `river-cpu/route.csv` and 18 PNGs in
`river-cpu/`. Inspected `dock-default.png`, `under-bridge-default.png`,
`beyond-default.png`, `beyond-lookback.png` and `connections-overview.png`. Dock shows
the player with both built banks; under-bridge and beyond views show open water and wake;
look-back shows the complete bridge span between shores; overview shows the connected
river crop and both landfalls. No obvious missing land or blank frame in these views.
The CPU review reports 547 base water triangles and 2,237,423 subdivided wave triangles.

The first hidden GPU attempt stalled at window creation inside the sandbox with macOS
`hiservices-xpcservice` errors. Repeating both GPU presets with graphics access succeeded:
`river-gpu.log`, `river-gpu/route.csv`, `river-waves.log`, `river-waves/route.csv`, and
18 PNGs per preset. Inspected dock, under-bridge, beyond, look-back and overview in both
GPU presets. Both show the same route landmarks and
connected banks as CPU; GPU shows reflective blue flat water, while waves darken and texture
it. The broad dark region at the under-bridge marker is present in both GPU presets and
appears to be the bridge shadow; its exact cause was not isolated. No blank frame or absent
bridge in the inspected views. This is visual reference evidence, not a pixel parity claim.

The flat GPU review reports 1012 sampled frames, 120 excluded warmup frames, mean/p95/max
CPU submission **3.917/4.219/4.753 ms**. GPU waves reports **4.210/5.230/5.820 ms**,
37,404 peak wave triangles, 176 patch rebuilds and 2,359,296 reported wave vertex bytes.
These runs were sequential and the full CTest suite was run afterward. Times are useful
reference observations, not controlled A0.4 performance results.

Selected SHA-256 image/trace references (full images and logs remain outside Git):

| Artifact under `build/adventure/a0-original/` | SHA-256 |
| --- | --- |
| `river-cpu/dock-default.png` | `75bee430073c5afa50c6128b8ae46ffdb98506d1cd0e25294541436ccfe8ed62` |
| `river-cpu/under-bridge-default.png` | `a94107bdc3073c12c4674b9531b45e4bbc6032df6adfa1f6419dcec7cbe9c323` |
| `river-cpu/beyond-lookback.png` | `c17fd1ebc44957fd33d2ed7c56f9a575e4d0bdafc8ea45be1074c63b318b8343` |
| `river-gpu/dock-default.png` | `2ba5b505710c16dc772e8d34050e31eb32b14c56653da67f413564461c06f492` |
| `river-gpu/under-bridge-default.png` | `e70f0b7f45ee7b270d3410020685cc94136eabcf81174dff9c478484a51e73a5` |
| `river-gpu/beyond-lookback.png` | `42e4bb5011027f14f91b4de8f9374385a9ca1f5b67deda7499c4748e260de06b` |
| `river-waves/under-bridge-default.png` | `d09742711ec8822200b36bad549457126d8845a86a38c0fdb9f6ad9718c0ee29` |
| `river-waves/beyond-lookback.png` | `48789f324755fe80a4aaf7c6d4a351c26b14fce7a13057e20e4c231e3ebe8018` |
| `wake-loop.state.csv` | `259b0fa61f62872439ed13880c0159aea9a4c3e9737e5a30dee6b4bd866d398a` |

The basin benchmark completed. Raw samples: `wake-loop.csv`; **full named-state trace**:
`wake-loop.state.csv` (end tick, time, x/z, vx/vz, height, vertical velocity, motion,
landings, live wake objects, births, rolling state hash); summary: `wake-loop.json`.
Warmup 120 frames / 240 ticks, then 1800 frames / 3600 ticks at two ticks per sample;
final tick **3840**, state hash **`82a88bf33eff247a`**, 18 births, peak 18 live objects,
one landing. Simulation p50/p95/p99/max: **0.068/0.151/0.158/0.247 ms** on this run.
This is the basin `wake-loop` simulation workload with renderer `none`, not river render
performance. The trace is the comparison reference for future ownership changes.

The content digest is implemented and cached by `boat_net_content_digest()` but no existing
CLI prints it. Record an explicit before/after digest in A1's validator rather than
claiming a value here.

The GPU review's hidden sampling is 10 Hz and its timing is CPU submission, not GPU
completion. Visible mode renders nominally at 60 Hz. The current basin benchmark supports
only `wake-loop` + `simulation` + `none`. Neither measures full river display frames.
The in-app profiler holds only 240 frames; its final percentiles do not summarize a
whole voyage. A0.4 owns a controlled river frame report and performance baseline.

## Verification and next task

On 2026-10-25, macOS 26.6.2 / arm64 / Apple clang 21 / Debug:
`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` and `cmake --build build -j` passed.
Both headless route checks, CPU capture, both GPU capture presets and the basin benchmark
completed as described above. The GPU runs and full suite needed macOS graphics/loopback
access outside the sandbox; the initial sandboxed CTest was interrupted after loopback
permission failures and a stalled GPU check. The subsequent complete
`ctest --test-dir build -j 4 --output-on-failure` passed **110/110 active tests** in
**144.54 s**; 15 retired wake-field tests remained disabled. Full output:
`build/adventure/a0-original/ctest.log`. `git diff --check` and local Markdown
target/anchor checks passed. No runtime, input, audio or asset-loading code changed, so
no new manual gameplay/audio check was performed. CPU/GPU visual inspection here is
agent review, not user acceptance. GPU completion timing, physical monitor settings and
whole-voyage performance remained unverified at the time. A0.2 and A0.3 have since been
completed ([coastal sources](README.md#coastal-sources-a02-2026-10-25),
[route survey](README.md#route-and-coverage-survey-a03-2026-10-25)). A0.4 added GPU timer
samples and whole-voyage reports ([below](#a04-controlled-river-baseline--2026-10-25)).

## A0.4 controlled river baseline — 2026-10-25

### Setup

Started from a clean `ba45569`. The benchmark is the new `boat_water_review --benchmark` mode
([guide](../BOATING_PERFORMANCE.md#river-frame-benchmark--2026-10-25)). The tool reported
build `ba45569-dirty/Release/AppleClang-21.0.0.21000101`, because the A0.4 source edits were
uncommitted. Release tree: `build/adventure-release`. Platform `Darwin/arm64`, GL renderer
`Apple M1 Max`, version `4.1 Metal - 90.5`. The monitor reported 120 Hz. Window 960 × 600,
framebuffer 960 × 600, virtual image 320 × 200. Internal target: 320 × 200 with AA off,
1280 × 800 with 4 × 4. Physics revision 8.

All 12 matrix runs were sequential: three trials of flat/waves × AA off/4x4, hidden and unpaced,
14:10–14:16 UTC. Command: `build/adventure/a0-performance/run-matrix.sh`. No CTest, download,
profiler or other game process ran. Other desktop applications stayed open. Every run
completed the route: 6,064 frames (120 warmup, 5,944 measured) and 12,125 ticks. The three
trials' final states are identical within each preset. They also match `--check` and
`--check --waves` exactly. Full CSV/JSON/logs and `summary.txt` are under
`build/adventure/a0-performance/`. Clock values on macOS are quantized to about 1 µs.

### Hidden matrix: median of three runs (ms)

The ranges are the minimum and maximum across the three trials. The sim, submit, GPU and
present columns are p95. Frame is `simulation + submission + present`.

| Preset / AA | Sim | Submit | GPU scene | Present | Frame p50 | Frame p95 | Frame p99 | Frame max |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| flat / off | 0.184 | 3.686 | 2.597 | 1.604 | 4.074 | 5.178 (5.154–5.638) | 6.012 | 11.062 (8.777–14.081) |
| flat / 4x4 | 0.183 | 3.801 | 3.193 | 1.949 | 4.221 | 5.668 (5.417–6.346) | 6.912 | 10.049 (8.635–15.804) |
| waves / off | 0.210 | 3.851 | 2.783 | 1.794 | 4.234 | 5.600 (5.476–6.005) | 6.530 | 12.853 (8.582–13.564) |
| waves / 4x4 | 0.211 | 3.875 | 3.593 | 1.796 | 4.277 | 5.691 (5.688–5.702) | 6.453 | 9.345 (8.319–10.537) |

GPU scene p99 medians are 2.708, 3.344, 2.861 and 3.653 ms in the same order; maxima are
3.4–4.8 ms. All 71,328 measured frames have a GPU timer sample (none skipped).

**Outliers:** no measured frame or start-to-start interval exceeded 16.667 ms in any hidden
run. The maxima above are all the stalls; the largest was 15.80 ms (flat/4x4). Present
dominates them; simulation never exceeded 0.29 ms.

Waves rebuilt the water patch 177 times during measurement in every trial. Capacity was
196,608 vertices and the peak was 37,404 triangles. Flat water rebuilt none. Application
submission, driver draw-call and scene-triangle counts are unavailable (`null`).

### Visible paced voyage and app profiler

`--visible --waves --aa 4x4` used the existing `SetTargetFPS(60)`. It completed with the same
final state. Simulation p95 was 0.279 ms, submission 5.052 ms, GPU scene 4.101 ms and
present/wait 12.727 ms. The start-to-start interval was p50/p95/p99/max
**17.108/17.213/17.262/25.040 ms**, mean 17.062 ms (**58.6 Hz**). 5,537 of the 5,944 intervals
exceed 16.667 ms; 6 exceed 17.5 ms, 3 exceed 20 ms and 1 exceeds 25 ms. The frame cap
consistently overshoots, so the miss count does not show long stalls.

The main app was run with
`./build/adventure-release/wake_velocity --location river --renderer gpu --waves --profiler --demo --frames 600`.
It exited normally after 1,244 ticks, or 2.07 ticks per frame; exactly 60 Hz would give 1,200.
The overlay was read at about 7 s of simulation. It showed frame 17.1 ms average and 17.2 ms
p95, 58.4 FPS observed on a 120 Hz monitor, scene 3.9/4.2 ms, present/wait 12.9 ms, simulation
p95 0.34 ms, ticks/frame p95 and maximum 3, and no discarded time. The accumulator keeps
simulation at 120 Hz with periodic three-tick frames. The display cadence is therefore below
60 Hz on this machine. That is a pacing issue for later work; A0.4 did not change the main
loop. The overlay was read from a temporary desktop capture, which was deleted because it
included other windows.

### Interpretation for this machine

- **Active work meets the provisional targets on this route:** hidden frame p95 is at most
  5.7 ms and p99 at most 6.9 ms, against ≤12 / ≤16.67 ms. This is the current river crop
  only. **60 FPS is not solved**: the paced cadence is 58.6 Hz.
  (Later on 2026-10-25 the [frame pacer](../BOATING_PERFORMANCE.md#frame-pacing--2026-10-25)
  measured 60.00 Hz on this route; this A0.4 finding stays as recorded.)
- CPU submission (3.7–3.9 ms p95) costs more than completed GPU scene work (2.6–3.6 ms p95)
  or simulation (≈0.2 ms). AA 4×4 adds at most 0.12 ms of submission p95 and 0.6–0.8 ms of
  GPU p95. Waves add about 0.2 ms of submission and 0.2–0.4 ms of GPU p95. Reducing
  submissions and culling are the first scaling levers.
- For A1.5/A2 comparisons on this machine, use these medians. Treat >10% p95 growth across
  three controlled runs as the investigation trigger.
- **Gaps:** no submission, draw-call or triangle counters. GPU time covers only the scene
  passes. Scan-out/VSync timing and the visible run's GPU clock state are not measured.
  There is a single machine and no Instruments trace (no stall needed attribution). The
  benchmark measures a render workload, not real-time accumulator behaviour.

### Verification

On 2026-10-25, the Debug configure/build passed with no project warnings (log
`build/adventure/a0.4-build.log`). `ctest --test-dir build -j 4 --output-on-failure` passed
**112/112 active tests** in 175.96 s, including 25 GPU-labelled tests and the new
`boat_river_benchmark_options` and `boat_river_benchmark_contract` (32.44 s). The 15 retired
tests remained disabled. Log: `build/adventure/a0.4-ctest.log`. The Release build also
passed; its only compiler warning comes from raylib's bundled `stb_vorbis.c`.

The unchanged `--output` GPU mode produced `under-bridge-default.png` byte-identical to the
A0.1 reference (`e70f0b7f…`). The visible voyage was judged only from its report, and the
600-frame app demo from one overlay capture. Neither was watched in full, and neither is human
acceptance. Audio was not assessed.
