# AGENTS.md

## Project

Proteus is a small C17/raylib playground with low-resolution, integer-scaled visuals and
tracker music. Its main program is the boating game `wake_velocity` (released as Wake Velocity) in
the basin and the Gothenburg river. Keep the code and project structure simple; this is a
playground for iterating on effects, timing, audio, and presentation.

## Development workflow

- Implement all changes, including features and bug fixes, in a separate Git worktree.
- Push the worktree branch and open a pull request for CI checks.
- Reserve the main checkout for the user to explore and run different branches; do not use it
  for implementation work.

## Layout

- `tools/wake_velocity.c`: the boating game's command line, title, application loop and presentation.
- `src/waterfront_scene.[ch]`, `src/waterfront.c`: river scenery, sky, gulls and street actors
  evaluated into a frame shared by the CPU (`renderer3d`) and GPU (`renderer_gpu`) renderers.
- `src/boat_model.[ch]`: parameterized visual boat recipes and caller-owned generated meshes.
- `src/waterfront_harbor.[ch]`: moored fleet, berth geometry, shared wave poses, and contact envelopes.
- `src/waterfront_traffic.[ch]`: travelling craft, ferry routes, visual wakes, and moving contacts.
- `src/waterfront_city.[ch]`: the compiled river map crop shared by shoreline queries and renderers.
- Boating code builds as static libraries in `CMakeLists.txt`. `proteus_boat_core` (physics, water
  queries, wakes, course crossing, contacts and geometry data) must not draw, read input, open
  windows or audio, or touch files; `tools/check_core_boundary.py` enforces this in CTest. See
  `docs/TESTING.md` for the libraries and test labels.
- `src/boat_net_*.[ch]`, `src/boat_prediction.[ch]`, `src/boat_remote_view.[ch]`: multiplayer
  protocol codec, authority/client sessions, client prediction, remote boat view and transports.
  `proteus_boat_net` obeys the core boundary (`boat_net_boundary`); only `boat_net_enet.c` uses
  ENet and sockets. `tools/boat_server.c` and `tools/boat_bot.c` are the server and scripted
  client; `docs/MULTIPLAYER_PROTOCOL.md` owns the byte layout.
- `src/profiler.c`: rolling frame statistics and the F3 overlay.
- `src/xm_audio.c`: XM-specific playback helpers layered over raylib's bundled decoder.
- `assets/music/`: XM music for the boating game and the dance study.
- `assets/samples/`: source WAV samples for tracker editing.
- `tools/generate_audio.py`: deterministic generator for the starter samples and XM.
- `tools/validate_xm.c`: XM parser/render smoke test used by CTest.
- `docs/`: focused workflow notes, including music and editor setup.

## Build and verify

The first configure fetches pinned raylib 6.0 sources (and, for native builds, ENet v1.3.18) and
therefore needs network access.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --preset full
```

While iterating, `ctest --preset quick` runs the tests labelled `quick` (each under about 100 ms;
about a second in all). Heavy validators are split into `<prefix>.<case>` tests; see
`docs/TESTING.md` for the lanes, the case runner and `tools/test_timing.py`.

Tests never wait for wall-clock time. Code that depends on time takes `now` as an input, and its
test drives a virtual clock; wall-clock limits are only failure deadlines. The `test_time` test
(`tools/check_test_time.py`) enforces this.

The routine suite disables retired physical wake-field experiments by default
(`PROTEUS_WAKE_FIELD_TESTS=OFF`); re-enable them only when revisiting that work.
Arcade-wake and gameplay checks remain active. Numerical validators retain assertions
with optimization in Clang/GCC Debug builds; `PROTEUS_OPTIMIZE_TESTS=OFF` restores
unoptimized validator debugging. See `docs/TESTING.md` for timings and opt-in commands.

Run the game from the repository root so its relative asset paths resolve:

```sh
./build/wake_velocity
```

For deterministic CPU visual verification, render and inspect representative frames without opening
a window. `wake_velocity` defaults to GPU and therefore needs a graphics context; select
`--renderer cpu` for display-free captures. `--time` is the simulated seconds of the scripted
screenshot drive.

```sh
./build/wake_velocity --renderer cpu --screenshot build/basin.png --time 3
./build/wake_velocity --renderer cpu --location river --screenshot build/river.png --time 3
```

On macOS, deeper CPU profiling can use Xcode's Instruments command-line tools when full Xcode is the
active developer directory. For example, record a Time Profiler trace against a windowed scripted
river drive:

```sh
xcrun xctrace record --template 'Time Profiler' --output build/river.trace \
  --launch -- ./build/wake_velocity --location river --demo --frames 3600
```

## Conventions

- Format C with the checked-in `.clang-format`; use four spaces and keep lines near 100 columns.
- Keep the project warning-clean under the compiler flags in `CMakeLists.txt`.
- Preserve nearest-neighbour, integer-scaled rendering unless a change explicitly targets it.
- The separate dance study uses the original 125 BPM starter XM.
- Treat `assets/music/starter.xm` and the sample WAVs as generated outputs. Running
  `python3 tools/generate_audio.py` overwrites them; do so only when the generated audio is meant to
  change, and validate the resulting XM.
- Update the nearby documentation when controls, asset names, arrangement, or development commands
  change.
- Do not commit `build/`, caches, or machine-specific compile databases.

## Documentation maintenance

- Treat documentation as part of each implementation step. Before handing off, update the owning
  plan's current status, completed task, next task, verification evidence, and known limitations.
  Use [the documentation index](docs/README.md) to find the owner for each area.
- Update affected user guides and commands in the same change. Search for superseded claims across
  `README.md`, `docs/`, and relevant asset/workspace guides; do not fix only the newest log entry.
- Keep the index's status overview consistent when a milestone or next task changes. Each plan owns
  its detailed task status; link to it rather than copying detailed checklists into other documents.
- Distinguish implementation, automated verification, manual playback, and user acceptance. Record
  only checks actually performed, with date and relevant platform/options. Never turn an old test
  count into a claim about the current suite or infer user approval from a successful run.
- Keep dated history as history: label superseded proposals and baseline findings, and correct live
  summaries instead of appending contradictory updates. Editor autosaves are not planning sources.
- Check changed local links and command examples. Report unresolved documentation uncertainty and
  verification limits before handoff; documentation-only changes need no new audiovisual review.

## Before handing off

Run the build and the full CTest preset above. Capture and inspect representative frames when visual effects
change. Add a manual demo check when motion, input, windowing, audio playback, or asset loading
changed. Use Xcode Instruments when performance or runtime behavior warrants deeper investigation.
Report anything you could not verify.
