# AGENTS.md

## Project

Proteus is a small C17/raylib playground with low-resolution, integer-scaled visuals and
tracker music. Its main program is the boating game `wake_velocity` (released as Wake Velocity) in
the basin and the Gothenburg river. Keep the code and project structure simple; this is a
playground for iterating on effects, timing, audio, and presentation.

## Development workflow

- Implement all changes, including features and bug fixes, in a separate Git worktree.
- Keep implementation work local until the user explicitly approves opening a pull request.
  After approval, push the worktree branch and open the pull request for CI checks. Do not
  automatically create a pull request for each experimental iteration.
- Keep one branch and one pull request per milestone or track, built from small, focused commits
  (one per plan step). Do not open a pull request per step or stack pull requests on each other.
- Keep per-step status in the owning plan. Update the `docs/README.md` overview only when its
  area-level summary changes; copying each step into the same overview row makes otherwise
  independent branches conflict.
- Reserve the main checkout for the user to explore and run different branches; do not use it
  for implementation work.

## Layout

- `apps/wake_velocity/wake_velocity.c`: the boating game's command line, title, application loop and presentation.
- `src/render/waterfront_scene.[ch]`, `src/render/waterfront.c`: river scenery, sky, gulls and street actors
  evaluated into a frame shared by the CPU (`renderer3d`) and GPU (`renderer_gpu`) renderers.
- `src/core/boat_model.[ch]`: parameterized visual boat recipes and caller-owned generated meshes.
- `src/core/waterfront_harbor.[ch]`: moored fleet, berth geometry, shared wave poses, and contact envelopes.
- `src/core/waterfront_traffic.[ch]`: travelling craft, ferry routes, visual wakes, and moving contacts.
- `src/core/waterfront_city.[ch]`: the compiled Eriksberg river crop's water. It is not a location (the
  outer map holds the same water): its scenery pack lays out the river kit on the outer map, and
  validators use it as a small mapped fixture (`tests/support/boat_test_location.h`).
- `src/scenery/boat_scenery.[ch]`: loads each map's land, buildings and bridge from
  `assets/geometry/<map>/scenery.bin` (written by `tools/map_pack.py`) and checks it against the
  compiled water in `water.h`. Collision and the server need the compiled water and maps; room
  definitions supply the course, rules and optional scene as data.
- Boating code builds as static libraries in `CMakeLists.txt`. `proteus_boat_core` (physics, water
  queries, wakes, course crossing, contacts and geometry data) must not draw, read input, open
  windows or audio, or touch files; `tests/lint/check_core_boundary.py` enforces this in CTest.
  `tests/support/boat_fixtures.[ch]` keeps the shipped voyage, course, race course and rule values for
  validators only; production targets never link it. See `docs/TESTING.md` for the libraries and
  test labels.
- `src/boat_net_*.[ch]`, `src/net/boat_prediction.[ch]`, `src/net/boat_remote_view.[ch]`: multiplayer
  protocol codec, authority/client sessions, client prediction, remote boat view and transports.
  `proteus_boat_net` obeys the core boundary (`boat_net_boundary`); only `boat_net_enet.c` uses
  ENet and sockets. `src/room/boat_server.c` and `tools/boat_bot.c` are the server and scripted
  client; `docs/MULTIPLAYER_PROTOCOL.md` owns the byte layout.
- `src/session/boat_work_session.[ch]`: the working-harbour session (boat, work sites, crate, job host, save
  and resume, autosave watch) shared by `cargo_sandbox` and the game; no drawing, input or files.
  `src/session/boat_work_save.[ch]` (`proteus_work_save`) adds its save file and automatic saving.
  `src/session/boat_work_local.[ch]` (`proteus_work_local`) runs it as a private local authority and
  client mirror for the game (ledger L3).
- `apps/wake_velocity/wake_velocity_work.[ch]`: the game's glue for the working-harbour session (`--work`); it
  copies the session into the game's `BoatSession` and hands renderers `BoatViewProps`.
- `src/boat_plugin*.[ch]`, `plugins/`: Lua plugin host (bounded, data-only voyage descriptors
  over a pinned Lua 5.4.9 in `proteus_lua`; P1 revisions, reload policies and Undo in
  `boat_plugin_host`) and reference packages; core and net never link it. The game's native
  `--plugin` glue is `apps/wake_velocity/wake_velocity_plugin.[ch]`.
  `docs/PLUGIN_API.md` owns the contract and `docs/PLUGIN_PLAN.md` the roadmap.
- `src/render/profiler.c`: rolling frame statistics and the F3 overlay.
- `src/audio/xm_audio.c`: XM-specific playback helpers layered over raylib's bundled decoder.
- `assets/music/`: XM music for the boating game and the dance study.
- `assets/samples/`: source WAV samples for tracker editing.
- `tools/generate_audio.py`: deterministic generator for the starter samples and XM.
- `tests/audio/validate_xm.c`: XM parser/render smoke test used by CTest.
- `docs/`: focused workflow notes, including music and editor setup.

## Build and verify

The first configure fetches pinned raylib 6.0 sources (and, for native builds, ENet v1.3.18)
and therefore needs network access. Lua 5.4.9 is extracted from the checksum-verified
archive shipped in `third_party/lua`, without a network request.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --preset full
```

While iterating, `ctest --preset quick` runs the tests labelled `quick` (each under about 100 ms;
about a second in all). Heavy validators are split into `<prefix>.<case>` tests; see
`docs/TESTING.md` for the lanes, the case runner and `tools/test_timing.py`.

Routine PR/main CI builds `--target ci` and runs `ctest --preset ci` under a three-minute
workflow cap; exhaustive checks and packaging are manual/release qualification. See
`docs/TESTING.md#routine-ci-budget`. The required local full verification below is unchanged.

Tests never wait for wall-clock time. Code that depends on time takes `now` as an input, and its
test drives a virtual clock; wall-clock limits are only failure deadlines. The `test_time` test
(`tests/lint/check_test_time.py`) enforces this.

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
./build/wake_velocity --renderer cpu --location outer --screenshot build/outer.png --time 3
```

On macOS, deeper CPU profiling can use Xcode's Instruments command-line tools when full Xcode is the
active developer directory. For example, record a Time Profiler trace against a windowed scripted
Gothenburg Harbour (`outer`) drive:

```sh
xcrun xctrace record --template 'Time Profiler' --output build/outer.trace \
  --launch -- ./build/wake_velocity --location outer --demo --frames 3600
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

### Plans and specs

- Plans describe work to do; specs describe the system as it is, in the present tense with no
  dated results. Existing specs: `docs/PLUGIN_API.md`, `docs/MULTIPLAYER_PROTOCOL.md`,
  `docs/TESTING.md`, `docs/PLAYING.md`, `docs/COMMAND_LINE.md`.
- Each plan keeps a status header of at most about ten lines: current state, open items, a
  "done when" line and the next task. Dated results and evidence go below it or in history.
- Every milestone ends with a closing step: move its contract and behaviour into the owning
  spec (create one if none exists), turn leftovers into open items or drop them explicitly,
  then mark the plan done or move it to history.
- Before starting a new track or feature, check the Open loops board in `docs/README.md` and
  name in the new plan what it defers.
- Candidate specs to create at closing steps (listed in the index, not yet written):
  `docs/ARCHITECTURE.md` (supersedes `ARCHITECTURE_REVIEW.md`), `docs/MULTIPLAYER.md` and
  `docs/WORKING_HARBOUR.md`.

## Cutting a release

- Use `tools/cut_release.py` from the separate, reviewed source worktree; see the
  [release command](docs/RELEASES.md#release-command) for preflight, controller setup and retries.
  It builds/tests, publishes, verifies downloads, updates the private hosting baseline and
  deploys/verifies Winterfell. Use `--check` to check prerequisites without publishing/deploying.
- A release includes upgrading the hosted Winterfell room and its matching browser client.
  Publishing native packages and GitHub Pages alone does not complete a release.
- After publication succeeds, update `wake_velocity_version` in
  `../davve-srv/ansible/host_vars/winterfell/main.yml`, commit and push in `davve-srv`, then
  run `scripts/ansible-run.sh --limit winterfell --tags wake_velocity` from that repository.
  Use its existing vault/controller setup and the routine role, not the legacy Proteus playbook.
- Verify the running release, room/relay health, hosted browser version, directory compatibility
  metadata and matching native/browser joins. Record deployment evidence and any limits in
  `docs/RELEASES.md`, `docs/MULTIPLAYER_HOSTING.md` and `davve-srv/docs/wake-velocity.md`.
  See [the hosting guide](docs/MULTIPLAYER_HOSTING.md#routine-deployments-davve-srv).

## Before handing off

Run the build and the full CTest preset above. Capture and inspect representative frames when visual effects
change. Add a manual demo check when motion, input, windowing, audio playback, or asset loading
changed. Use Xcode Instruments when performance or runtime behavior warrants deeper investigation.
Report anything you could not verify.
