# Plugins by default: implementation plan

Status, 2026-10-31: **planned; no implementation.** The user asked for plugins to become the
default and always enabled, and for this plan to be executable by a long-running agent. This
document owns the sequence, per-PR status and acceptance for that move. The
[plugin plan](PLUGIN_PLAN.md) and [API](PLUGIN_API.md) keep the existing plugin contract, the
[multiplayer protocol](MULTIPLAYER_PROTOCOL.md) keeps byte layouts, and the
[room content plan](ROOM_CONTENT_PLAN.md) keeps downloads and dynamic maps. [All plans](README.md).

Next task: **[PD1](#pd1--bundled-runtime-and-complete-solo-coverage)**.

| PR | Outcome | Depends on | Status |
| --- | --- | --- | --- |
| [PD1](#pd1--bundled-runtime-and-complete-solo-coverage) | Every native solo session runs from a bundled package; authoring is separate | — | Not started |
| [PD2](#pd2--package-defined-rooms) | Rooms run from a package; clients receive data, protocol v9 | PD1 | Not started |
| [PD3](#pd3--authored-scenes-and-revisions-in-rooms) | Authored scenes with contacts in rooms; revisions between rounds | PD2 | Not started |
| [PD4](#pd4--browser-parity) | Browser solo play runs from bundled packages, scenes included | PD3 | Not started |
| [PD5](#pd5--cutover) | The compiled selection and the off switch are gone | PD4 | Not started |

## Target and decisions

Definition of done:

- Every session in every mode (solo, time trial, rooms, server, browser) starts from a package
  revision through one path. There is no off switch.
- `--plugin DIR` remains as the authoring override and is the only way to enable save polling,
  F5–F7 and the authoring panel.
- Physics, contacts, wake math, rendering, networking and the race, match and voyage engines
  stay native. So do the registered boat recipes, routes and flock behaviours, which packages
  select from. Only the *choice* of content moves out of compiled code.
- `proteus_boat_core` and `proteus_boat_net` still do not link Lua or the plugin host.

Decisions already made. Do not reopen them inside a PR; report a problem instead.

| Decision | Why |
| --- | --- |
| Packages are complete session definitions. No base-plus-overlay composition. An absent subsystem means the engine's registered default, as it does today. | Composition is a mechanism with no current need. `harbor-life` already declares its own voyage. |
| Bundled packages are embedded in the binary as bytes. | Launch cannot fail on working directory, install layout or browser storage. The same path serves the server and the browser. |
| Rooms carry a validated, pointer-free **room definition**, not source. Lua runs on the authority at room start; room clients run no plugin code. | It avoids downloaded-script trust and browser worker stalls in rooms. A scene is at most 7,944 bytes in memory, so it fits the existing paged bootstrap. |
| The river race is the first room scenario. | It has a physical parity test, a human-accepted course and no callbacks. |
| Base packages have no objective callbacks. | The browser simulation waits at each imported callback. Measured delivery was 70 ms mean on software-rendered Chromium ([P4.9](PLUGIN_PLAN.md#p49-result-2026-10-30)). |
| A failing bundled package is a test failure; at runtime the title shows the diagnostic. Nothing silently substitutes other content. | The bytes are immutable and validated in CTest, and players should know what they are playing. |
| First-party catalog bytes may be evaluated on the browser's game thread. Imported code stays worker-only. | Base packages evaluate in under a millisecond natively. A worker at every PLAY would add a cold start and a new way to fail. PD4 measures the size cost and has a stated exit. |
| Full browser qualification stays manual-only. | The user reduced it deliberately. Dispatch it once per PR where this plan says to. |

Out of scope for all five PRs: authority objective callbacks and commands in rooms (the rest of
[P5](PLUGIN_PLAN.md#p5--authority-prediction-and-recorded-revisions)), session recordings, room
downloads and dynamic maps (room stages R2 and R4), the in-game agent (P6), lighting and other
[location-life](PLUGIN_PLAN.md#location-life-track-proposed-2026-10-30) additions from L5 on,
living-city actors, package signing and new physics primitives. Rooms are not recorded today, so
this plan adds no recording identity to them.

Baseline measured 2026-10-31 on `e335b84`, Release, Apple Silicon/macOS, display-free CPU capture
of a three-second drive: compiled `outer` 0.80 s and `langedrag` 0.80 s (four runs each, within
0.01 s), `harbor-life` 1.56 s (one run), so about +0.75 s. With `--start 2,170,90`, `harbor-life`
took 4.90 s (four runs) against 4.17 s for compiled (one run), about +0.73 s. The traffic sweep at
`src/boat_plugin.c:599` is the likely cause, but no profile has confirmed it.
`check_scene_traffic` samples a 4,160 s horizon every 0.25 s.

## How to execute this plan

### One step, one PR

Each PD section is **one branch and one PR**, however long it takes. Make as many small, focused
commits inside it as the work needs. Do not split a PD into several PRs and do not start the next
PD unless told to.

Reduce CI churn. Workflows run on pull requests and pushes to `main`, not on ordinary branch pushes:

1. Work in a separate worktree, on a branch named `feat/plugin-default-pdN`, from current `main`.
   If the parent PD is still unmerged and you were told to continue, branch from the parent and
   target it, following the stacking rule in [AGENTS.md](../AGENTS.md).
2. Commit often. Push the branch whenever you like as a backup. **Do not open the PR yet.**
3. Open the PR only when the PD's whole acceptance list passes locally.
4. After that, push rarely: collect every fix for a CI failure, then push once. Do not push to
   poll CI, and do not push documentation tweaks separately.
5. Run the manual browser workflow only where a PD says to, once, on the final revision.
6. Do not merge the PR, cut a release or deploy. The user does those.

### Working loop

Before starting, read this plan, [AGENTS.md](../AGENTS.md), the [testing guide](TESTING.md) and
the documents named in your PD. This plan names files and symbols from `e335b84`, so confirm they
are still accurate. Where the source disagrees with this plan, the source wins: adapt, and record
the difference in your PD's result.

Work through the PD's tasks in order and commit each one with the tree building and
`ctest --preset quick` green. Run the full acceptance list before opening the PR. Do not stop to
ask about anything this plan or the code can settle. If you pause, a fresh agent must be able to
continue from your commits and the result section.

Keep these rules throughout:

- Format with `.clang-format`, stay warning-clean, and write Windows-safe C. Windows is only
  checked in CI, so avoid POSIX-only calls and very long string literals (MSVC limits them).
- Core and net stay free of files, sockets, Lua and windows; `check_core_boundary.py` and the
  link guard in `CMakeLists.txt` stay on.
- Tests use virtual clocks only (`test_time` enforces this). Timing is measured with tools outside
  CTest and recorded as evidence, never asserted.
- Prefer a small number of concrete owners. Do not build a general content or mod framework.
- Any byte-layout change updates the protocol version, fixtures and protocol document together.

### Build and verification notes

- Worktree build: copy `build/_deps/{raylib,lua,enet}-src` from the main checkout into the new
  `build/_deps`, then configure with `-DFETCHCONTENT_FULLY_DISCONNECTED=ON`.
- Socket tests need an environment that allows local sockets. If they fail in a sandbox, rerun
  with sockets allowed and record both runs.
- `GLFW: Failed to initialize platform` means no windowed or GPU checks can run in that
  environment. Report it as a limit, and use `--renderer cpu`.
- Browser: `source ~/src/emsdk/emsdk_env.sh`, then copy the `-src` dependencies into
  `build-web/_deps` and use `emcmake` with the same disconnected flag. Playwright has no global
  install; make a scratch venv and run `pip install playwright` and
  `playwright install chromium firefox webkit`.
- Keep scratch files out of the repo. Do not use the main checkout for implementation.

### Stop and report

Finish what is independently correct, record it, and report to the user instead of pressing on if:

- preserving a parity gate would require changing physics, or an existing test would have to be
  weakened to pass;
- a saved best, ghost or settings file could be lost or invalidated;
- a design decision above proves wrong;
- CI fails for a reason that cannot be reproduced or reasoned about after one honest fix round;
- something requires secrets, a release or changes in `../davve-srv`.

Human checks never block a PR. List them under **Needs the user** in the result and in the PR
body. Never infer acceptance from a passing test.

### What every PD must leave behind

- A dated result subsection under the PD here: what was implemented, deviations, commands
  actually run with their platform and counts, measurements, known limits, and **Needs the user**.
- The status table above updated, and the **Next task** line moved on.
- Affected guides updated in the same PR: at least [PLUGIN_API.md](PLUGIN_API.md),
  [COMMAND_LINE.md](COMMAND_LINE.md), [TESTING.md](TESTING.md) and `docs/README.md`'s row when the
  area summary changes. Search for claims your change makes false; do not only add new text.
- A PR body stating the outcome, verification performed, limits and needed human checks.

Every PD's local acceptance includes: a warning-clean Debug build; `ctest --preset full` with
sockets; `git diff --check`; every changed local Markdown link resolving; and CI green on the PR
after opening it.

## PD1 — bundled runtime and complete solo coverage

**Outcome.** On native builds, `./build/wake_velocity` and the title's solo choices run every
session from an embedded package. They look and play exactly as they do now. Rooms and the browser
still use compiled content. A temporary `--no-plugins` restores the compiled path for parity.

Read first: `tools/wake_velocity_plugin.[ch]`, `tools/wake_velocity.c`, `tools/boat_title.[ch]`,
`src/boat_plugin.[ch]`, `src/boat_plugin_host.[ch]`, `src/boat_plugin_stage.c`,
`src/boat_course.[ch]`, `src/boat_course_view.c`, `src/boat_replay.c`,
`tools/boat_race_plugin.[ch]`.

### Tasks

1. **Split runtime from authoring.** Give `WakePlugin` a mode. Runtime mode opens from an in-memory
   `BoatPluginBundle` and has no file watch, stage worker, bridge, hotkeys or panel. Authoring mode
   is today's behaviour and remains `--plugin DIR`, including its fail-fast exit on a bad package.
   In `tools/wake_velocity.c`, replace the `plugin_path` gates with separate "plugin active" and
   "authoring" conditions. The F5–F7 help line, `wake_plugin_draw` and the browser control calls
   depend on authoring only. Static storage and the 1 MiB owner assertion stay.
2. **Add the catalog.** Add `src/boat_plugin_catalog.[ch]` to `proteus_plugins`, with lookup by ID
   and the default for a kind and location. Embed packages at configure/build time with a CMake
   script (`file(READ ... HEX)`) producing **byte arrays**, not string literals, in a generated
   source under the build tree. Editing a package must rebuild it. Do not add a Python build
   dependency for native games.
3. **Add the missing kinds.** Today only `voyage` on `outer` and `race` on `river` exist
   (`src/boat_plugin.c:123`).
   - `free`: location `basin` or `river`, capability `free.define`, descriptor
     `return { free = { version = 1 } }`, with an optional `scene` on `river` only if the river
     site passes the existing scene validation. Otherwise defer that scene option and record why.
     `outer` keeps the `voyage` kind.
   - `course`: location `basin`, capability `course.define`, exactly `BOAT_COURSE_GATES` gates and
     the start position and heading.
   - Replace `BoatPluginHost.racing` with a kind enum. Extend revisions, classification, Undo and
     the stage wire. Bump the PST wire version.
   - Extend `plugin_author.py` capabilities and schema, the validators and the API document.
   - The browser constructor and Emscripten worker reject the new kinds with explicit messages
     until PD4.
4. **Create the packages.** Add `plugins/basin-free`, `plugins/river-free` and `plugins/time-trial`.
   `plugins/langedrag` and `plugins/river-sprint` *are* the outer and race defaults; do not
   duplicate their sources. `plugins/harbor-life` is embedded as a selectable variant, not a
   default. Add `harbor-life` and the new packages to `EXAMPLES` in
   `tools/package_plugin_tools.py`.
5. **Make the time trial data-driven.** `boat_course_gates` and `BOAT_COURSE_START` become values
   bound into `BoatCourse`/the session, used by `boat_course_step` and `boat_course_view.c`. Keep
   the gate count and the `PROTEUS_RUN` format.
   - When the course digest equals the compiled course's, the best-run path stays exactly
     `course-%s-physics%d.run` (`tools/wake_velocity.c:852`), so existing bests remain valid.
   - Any other course adds its digest to the filename.
   - Allow `--replay` and `--ghost` with the bundled course. Refuse them with an explicit message
     for other courses (`--plugin` keeps refusing as now).
6. **Default every solo path.** Resolve the bundled default for `--location` and `--course`, in
   windowed, `--demo` and `--screenshot` runs. Add `--package ID` for a bundled package in runtime
   mode, and temporary `--no-plugins`. `boat_race_trial` defaults to the bundled `river-sprint`.
   Connected and hosted sessions continue on compiled content, and `--plugin` with rooms is still
   refused. Fail clearly if `--package` names an unknown ID or a package for another location.
7. **Handle startup cost.** Profile first to confirm the cost above. Catalog bytes skip the
   expensive traffic sweep at runtime because a CTest case runs full validation on every bundled
   package. User packages keep the full sweep. Extend `boat_plugin_bench` so it accepts packages
   without objectives and record the default and `harbor-life` start costs.
8. **Add the title choice.** On native builds add a saved content row, shown for `outer`: Standard
   or Harbor life. `title_single` passes `--package`. Failure to open returns to the title with the
   diagnostic rather than exiting. Older settings files must still load. Hide the row on web until
   PD4. Capture and inspect the title.
9. **Add the parity gate.** New CTest `plugin_default_parity` (label `cpu`) runs `wake_velocity
   --renderer cpu` with default and `--no-plugins` for basin, river, outer and `--course`, and
   compares captures byte for byte. Add a validator case proving the embedded `basin`, `river`,
   `outer` and time-trial definitions equal the compiled ones and a four-boat-free session trace
   hash matches over a long drive (reuse `compiled_parity`).

### Acceptance

- `plugin_default_parity`, the bundled-package validation case and the legacy-best case pass. The
  legacy case proves a pre-existing `course-*-physics*.run` file loads as the best, with its path
  unchanged, and is never overwritten by an equal or worse run.
- Default launches show no plugin panel and do not poll any files. Add a test for this.
- `--plugin` behaviour, including the existing refusal tests, is unchanged.
- Default `outer` startup is no slower than the baseline within measurement noise, and the
  `harbor-life` delta is reported against the baseline above.
- Captures of the title row inspected; a windowed `--demo` run performed or reported as impossible.
- Windows CI exercises the generated catalog.

**Needs the user:** play default solo modes for regressions, and judge Harbor life from the title.

## PD2 — package-defined rooms

**Outcome.** A room's race course, race rules, match rules and combat rules come from a package
evaluated by the authority and sent as data. A client with no knowledge of an edited course can
join and predict it bit for bit. Protocol v9. Scenes remain compiled until PD3.

Read first: [MULTIPLAYER_PROTOCOL.md](MULTIPLAYER_PROTOCOL.md) (handshake, world checkpoint, version
rules), `src/boat_net_protocol.[ch]`, `src/boat_net_server.[ch]`, `src/boat_net_client.[ch]`,
`src/boat_prediction.[ch]`, `tools/boat_server.c`, `tools/boat_bot.c`, `tools/wake_velocity_net.c`,
`tools/wake_velocity_host.c`, and [ROOM_CONTENT_PLAN.md](ROOM_CONTENT_PLAN.md).

Compiled content used by rooms today: `boat_net_server.c:30`, `tools/boat_server.c:421` and `:699`,
`boat_net_race_bind` (`src/boat_net_protocol.c:1494`), `tools/wake_velocity_net.c:574`,
`src/boat_view.c:165`, `tools/boat_bot.c:240`, and the handshake digest at
`src/boat_net_protocol.c:1086`.

### Tasks

1. **Add the room definition.** New `src/boat_room_definition.[ch]` in `proteus_boat_core`: a
   versioned, pointer-free struct with package ID and display name, mode, location,
   `BoatRaceCourse`, `BoatRaceRules`, `BoatMatchRules` and `BoatCombatRules`, plus a validator
   usable without Lua or files. Move the structural parts of the race checks out of the plugin
   library so client and server share them. Map-dependent checks take the map as an argument.
2. **Add the match kind and room packages.** New kind `match` with capability `match.define`:
   location, match rules and combat rules. Add `plugins/deathmatch`, whose values equal
   `boat_match_default_rules` and `boat_combat_default_rules`. Free rooms use the `free` packages,
   adding a free `outer` room package if that location is hostable. Race rooms use `river-sprint`.
3. **Make the authority evaluate packages.** `tools/boat_server.c` gains `--package ID|DIR` and
   defaults from `--mode` and `--location`. It evaluates the package once at start-up with full
   validation, builds the definition and passes it to `boat_net_server_init` through the config,
   replacing `race` rules and match fields. A bad package refuses to start the room with the
   diagnostic. Existing rule-override flags apply after loading. `proteus_boat_net` stays free of
   Lua: the executable links `proteus_plugins`. The in-game host passes the same flag to its
   `--serve` child and gains a package row in the HOST panel, listing bundled room packages for the
   chosen mode.
4. **Define protocol v9.** Hello's digest now means *engine and maps*. Remove the race course and
   combat rule bytes from `boat_net_content_digest`, and keep geometry, cannon constants, fleet and
   wake contract and recipe revisions. Harbor data stays until PD3. Encode the definition fieldwise
   into canonical bytes capped at 16 KiB. After Accept and before the world checkpoint, send it on
   the reliable bootstrap channel using paged messages with the checkpoint's assembly pattern. The
   digest here detects corruption, not authorship. Decode must reject truncation, extra bytes,
   non-finite values, out-of-range counts, reordered pages and digest mismatch, and treat the sender
   as untrusted. Remove `BoatNetRaceCourse`. Update fixtures, goldens, the protocol document and the
   version rules. v8 clients are rejected explicitly.
5. **Install on the client.** `BoatNetClient` owns the validated definition for the connection and
   holds READY until it is installed; a failure closes with a clear reason and never enters the
   world. Bind it in `boat_net_race_bind`, prediction, `tools/wake_velocity_net.c`, the course view
   and `boat_bot`. Display the package name in the existing room UI. Resync and late join reuse the
   installed definition; reconnect receives it again.
6. **Check discovery and hosting.** LAN and directory compatibility must keep working with the new
   digest meaning. Record what `../davve-srv` or the release needs for v9 in
   [RELEASES.md](RELEASES.md) and [MULTIPLAYER_HOSTING.md](MULTIPLAYER_HOSTING.md), but make no
   deployment. Pages only publishes on tags, so merging changes nothing live.

### Acceptance

- **The key proof:** an in-memory race room with a *non-default* course (different gates, grid and
  ferry timing) and non-default rules, created from an edited package. Clients, including a
  mid-race joiner and a predicting client, finish with authority and client hashes identical and
  prediction replay through GO bit for bit. No client code path may read the compiled course. Add a
  test hook or link-level check that proves this.
- The same with non-default deathmatch rules, including combat values that affect prediction.
- Every existing `boat_net_*` loopback, four-player and impairment test passes on v9 with default
  packages. Bots finish the default race in the same time range as now.
- Malformed, oversized, truncated, stale and mismatched definitions all fail safely under virtual
  time, with no world installed.
- Bytes on the wire are measured for each bundled package and for the maximum definition.
- Bootstrap time and bandwidth are compared with v8 for a four-client room.
- Local `check_web_multiplayer.py` passes free, deathmatch and race against a native server, using a
  browser client with no Lua evaluation. Include one edited course. Run the manual browser workflow
  once on the final revision.
- `boat_server`'s package and the Windows and Linux server builds link and run in CI.

**Needs the user:** one two-machine race and one deathmatch on v9, one with a browser client, and
a hosted cutover at the next release.

## PD3 — authored scenes and revisions in rooms

**Outcome.** Rooms can run authored scenes with berth and traffic contacts in agreement on every
machine, and a room can move to a new package revision between rounds. Physical harbor data leaves
the handshake digest.

Read first: `src/boat_world.[ch]`, `src/boat_fleet.[ch]`, `src/boat_prediction.[ch]`,
`src/boat_scene_definition.[ch]`, `src/waterfront_harbor.[ch]`, `src/waterfront_traffic.[ch]`,
the [scene contract](PLUGIN_API.md#location-life-scene-contract-planned-l11) and
[multiplayer prediction contract](PLUGIN_PLAN.md#multiplayer-prediction-and-recording).

`BoatWorld.scene` is the seam: physics and presentation read that pointer, and rooms leave it NULL.
This is the riskiest PR in the plan because prediction copies fleets and replays them.

### Tasks

1. **Carry scenes in the definition.** Extend the definition and codec with the full
   `BoatSceneDefinition`. Measure the maximum encoded size against the 16 KiB cap. Client validation
   is structural and bounded. The authority alone runs the expensive sweep.
2. **Bind scenes through authority and prediction.** The authority and client own the definition,
   which outlives their fleets. Bind `scene` in `boat_fleet_init` and everywhere a fleet world is
   copied, restored from a checkpoint or replayed, including `boat_fleet_world`. Checkpoints stay
   pointer-free. Drive `BoatViewState.scene` in networked views. Confirm actor poses use the shared
   authoritative clock on every machine, not a local one.
3. **Validate against the room.** A scene package for a room must clear `fleet_spawn`, every grid
   slot and gate line, and recovery positions. Add these to authority validation with field paths.
4. **Move harbor identity.** Remove berth and ferry values from `boat_net_content_digest`, and keep
   a contact-recipe revision. Default rooms send a default scene or an explicit absent-scene marker;
   choose whichever keeps compiled contacts testable and record why.
5. **Add a proven scene room.** Make `harbor-life` usable in a free `outer` room. If its voyage
   content does not apply to rooms, add a small `plugins/harbor-room` that shares the scene. Also
   add a test race package with an authored obstacle berth clear of the course.
6. **Add round-boundary revisions.** Explicit opt-in `--package-reload`: the authority polls its
   package directory only between rounds (`RESULTS`/`WAITING`), never during play. On a valid edit:
   validate, send the new definition with a revision number and a future commit tick, collect
   readiness, then all commit together at that tick. A client that cannot get ready by the deadline
   is disconnected with an explicit reason and everyone else continues; a failed stage keeps the
   running revision. Free rooms apply only at room restart or when empty. Add a generation fence so
   stale prediction and messages cannot cross a commit.

### Acceptance

- Authority and clients hold identical hashes, and prediction replay is bit for bit, through
  repeated hull contact with an authored moored boat and an authored moving craft, under the memory
  transport's loss, reordering and delay profiles, with a late joiner and a resync.
- A CTest proves two clients with differing compiled default harbor data agree under an authored
  scene, or, if that cannot be built, the digest test proves no harbor bytes remain.
- Revisions: success, mid-download joiner, missing readiness, invalid candidate, duplicate and stale
  announcements, and a commit landing exactly on a round boundary, all under virtual time.
- Four-client bandwidth and server tick costs measured with the maximum scene; CPU captures of a
  networked authored scene inspected; existing room suite passes unchanged.
- Run the manual browser workflow once on the final revision.

If bit-identical contact with moving authored craft cannot be achieved without a physics change,
stop and report. Moored and visual-only room scenes may land alone, with the limit recorded.

**Needs the user:** a two-person harbor room with deliberate berth and traffic contact; judge whether
any correction is visible. Decide whether revisions should be exposed beyond the flag.

## PD4 — browser parity

**Outcome.** Browser solo play starts from the same catalog as native, including Harbor life.
Imported voyages may carry scenes. Normal play starts no worker.

Read first: [Browser builds](WEB_BUILD.md), including its plugin sections,
[browser source bundles](PLUGIN_API.md#browser-source-bundles-p43), `web/plugin_author.js`,
`web/plugin_worker.js`, `src/boat_plugin_stage.c`, `tools/wake_velocity_plugin.c`, `cmake/Web.cmake`,
`tools/package_web.py`, `tools/check_web_plugin_author.py`.

### Tasks

1. **Link catalog evaluation in the game.** On Emscripten, compile the evaluator for catalog bytes
   only. The compile-time absence of a synchronous imported path must remain true: the only caller
   reachable on the game thread takes a catalog entry, not arbitrary bytes. Add a test or link check
   for it. Measure the game Wasm's size before and after, raw and compressed. **Exit:** if the
   compressed growth exceeds 300 KiB, or evaluation is not fast on the slowest engine, generate
   decoded receipts at package time with the existing worker under Node instead, and record why.
2. **Default browser solo.** Remove the compiled path from browser solo sessions, using runtime mode
   from PD1. Add the title's content row on web. Keep save settings compatible. Time trial bests in
   IndexedDB must remain valid, using the same digest rule as PD1.
3. **Render scenes in the browser.** Verify authored scene paths through the web GPU renderer
   (berths, modules, traffic, gulls) against native at pinned cameras and times. Fix actual
   differences. Check lazy scenery loading and repeated sessions.
4. **Allow imported scenes.** Remove the rejection at `src/boat_plugin_stage.c:344` and
   `web/plugin_author.js:17`, and return the existing scene wire from the worker, with the traffic
   sweep off the game thread. Add worker deadlines suited to a slow device. Imported `free` and
   `course` kinds behave the same way. Imported race packages stay rejected with a message pointing
   to rooms, because the browser has no solo race trial.
5. **Update qualification.** Extend `check_web_title.py`, `check_web_plugin_author.py` and the Node
   contracts for default sessions, Harbor life, scene imports, tab freeze and repeated sessions. Do
   not enable the full matrix on PRs.

### Acceptance

- Browser default-session CPU/GPU comparison shows no unexplained difference from before.
- No worker is created in a default session, and no imported Lua is reachable on the game thread.
- Local three-engine `check_web_plugin_author.py` and `check_web_title.py` pass, with captures
  inspected and costs recorded: game start, memory, Harbor life frame time. Run the manual browser
  workflow once on the final revision.
- `check_web_multiplayer.py` and `check_web_mobile.py` still pass.

**Needs the user:** Harbor life and normal play on an iPhone and in physical Safari. Those results
decide whether Harbor life is offered on mobile.

## PD5 — cutover

**Outcome.** There is one path. The compiled selection, the switch and the plugin compile-time flag
are gone, and every doc says so.

### Tasks

1. **Remove the switch.** Delete `--no-plugins`, `PROTEUS_BOAT_PLUGINS` and the conditional paths it
   guards. Replace `plugin_default_parity` with pinned-digest and capture checks; pin values from
   the last commit with both paths available.
2. **Remove compiled selection.** Delete `BoatWorldDefinition.voyage` and the session's fallback to
   it. Move `boat_voyage_outer`, the `RIVER` course table, `boat_course_gates`,
   `boat_race_default_rules`, `boat_match_default_rules` and `boat_combat_default_rules` into a
   test-only fixture library for validators. Keep registered recipes, routes and flock behaviour.
   `boat_combat.c:60`-style internal defaults take their rules from the caller. Tools (`boat_bot`,
   `boat_race_trial`, the review tools) use the catalog or the room definition. Add a check that no
   production target links the fixture library.
3. **Update durable rules.** Rewrite the rules that assumed opt-in: the
   [plugin plan's milestone rule](PLUGIN_PLAN.md#milestones-and-acceptance), "existing rooms continue
   using compiled content" in its multiplayer section, the room plan's starting point, and the layout
   notes in [AGENTS.md](../AGENTS.md) that say the server needs only compiled water. Sweep
   `README.md`, [PLAYING.md](PLAYING.md), [COMMAND_LINE.md](COMMAND_LINE.md), [TESTING.md](TESTING.md),
   [WEB_BUILD.md](WEB_BUILD.md), [RELEASES.md](RELEASES.md) and the hosting guide.
4. **Harbor life as default, only if accepted.** Change the default `outer` package only when the
   user's acceptance is recorded here. Otherwise it stays a title choice, and this PR says so.
5. **Prepare the release.** Update release notes and checks for v9 and the new package set. The
   user runs `tools/cut_release.py` afterward.

### Acceptance

- The full native suite and captures match the pinned values. `git grep` finds no production use of
  the removed symbols. Run the manual browser workflow once on the final revision.
- Startup and frame costs for default sessions are unchanged against PD1's measurements.
- `tools/cut_release.py --check` passes, or its failure is reported.

**Needs the user:** merge, cut the release, verify the hosted room, and make the Harbor life decision.

## Interactions with other tracks

- **[Deathmatch playtest](DEATHMATCH_PLAYTEST_PLAN.md):** may proceed independently. Handling changes
  stay native. Before PD2 merges, its rule changes land in the native defaults; PD2's package must
  match whatever `main` holds. After PD2, rule numbers belong in `plugins/deathmatch`.
- **[Living city](LIVING_CITY_PLAN.md), [sunset](SUNSET_PLAN.md), L5–L8:** neither block nor are
  blocked. New selectable content should arrive as package data where a scene field already exists.
- **[Free-riding adventure](FREE_RIDING_ADVENTURE_PLAN.md):** maps stay compiled, so map changes still
  alter the engine digest until room stage R2.
- **[Room content](ROOM_CONTENT_PLAN.md):** PD2 and PD3 deliver the descriptor part of R1 and R3 on
  built-in maps. They do not deliver R2 (loaded maps) or R4 (distribution).

## Planning verification

Reviewed 2026-10-31 against `e335b84`: the plugin, multiplayer, room content, deathmatch playtest,
living city, sunset, adventure and architecture documents and the source seams cited above. CI
triggers were read from the workflow files, and start-up was measured as stated in the baseline.
Nothing here is implemented, and no user acceptance is recorded or implied.

Handoff verification, 2026-10-31, Apple Silicon/macOS, documentation-only: Debug build passed and
`ctest --preset full` passed all 323 enabled tests with local sockets (13 retired tests disabled,
GPU CTests off). All 420 local links and anchors across the five changed
documents resolve, and `git diff --check` passes. No audiovisual review was needed.
