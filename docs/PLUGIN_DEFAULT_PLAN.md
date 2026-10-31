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
| [PC](#pc--live-console-and-edits-without-reset) | In-game Lua console; edits apply without resetting the session, on the path a later agent uses | PD1 | Not started |
| [PD2](#pd2--package-defined-rooms) | Rooms run from a package; clients receive data, protocol v9 | PD1 | Not started |
| [PD3](#pd3--authored-scenes-and-revisions-in-rooms) | Authored scenes with contacts in rooms; revisions between rounds | PD2 | Not started |
| [PD4](#pd4--browser-parity) | Browser solo play runs from bundled packages, scenes included | PD3 | Not started |
| [PD5](#pd5--cutover) | The compiled selection and the off switch are gone | PD4 | Not started |

PC does not depend on PD2–PD5 and can run at any point after PD1. The recommended order is PD1,
PC, then PD2. The remaining PDs must run in order.

## Target and decisions

Definition of done:

- Every session in every mode (solo, time trial, rooms, server, browser) starts from a package
  revision through one path. There is no off switch.
- Plain play consumes data compiled at build time and starts no Lua VM. Lua runs only where it has
  to: authoring and imports, the live console, a room authority given a package directory, and
  objective callbacks.
- `--plugin DIR` remains the file-based authoring override and is the only way to enable save
  polling. The live console edits the running session in memory.
- Physics, contacts, wake math, rendering, networking and the race, match and voyage engines
  stay native. So do the registered boat recipes, routes and flock behaviours, which packages
  select from. Only the *choice* of content moves out of compiled code.
- `proteus_boat_core` and `proteus_boat_net` still do not link Lua or the plugin host.

Decisions already made. Do not reopen them inside a PR; report a problem instead.

| Decision | Why |
| --- | --- |
| Packages are complete session definitions. No base-plus-overlay composition. An absent subsystem means the engine's registered default, as it does today. | Composition is a mechanism with no current need. `harbor-life` already declares its own voyage. |
| **Bundled packages are compiled at build time (user decision, 2026-10-31).** A generator evaluates each one, runs full validation and embeds its decoded definition plus its source. All clients and the server use that catalog. | Launch cannot fail on working directory, install layout, worker or storage. The route sweep runs once per build, so no runtime "trusted, skip the check" flag exists. It keeps the rule from rooms: clients consume validated data. It is also easy to reverse, whereas Lua on the game thread is not. |
| Rooms carry a validated, pointer-free **room definition**, not source. Lua runs on the authority at room start; room clients run no plugin code. | It avoids downloaded-script trust and browser worker stalls in rooms. A scene is at most 7,944 bytes in memory, so it fits the existing paged bootstrap. |
| The river race is the first room scenario. | It has a physical parity test, a human-accepted course and no callbacks. |
| Catalog packages have no objective callbacks; the generator rejects them. | The browser simulation waits at each imported callback. Measured delivery was 70 ms mean on software-rendered Chromium ([P4.9](PLUGIN_PLAN.md#p49-result-2026-10-30)). |
| An invalid bundled package **fails the build**. If a catalog entry still cannot start at runtime, the title shows the diagnostic. Nothing silently substitutes other content. | The output is immutable and produced by the same build. Players should know what they are playing. |
| The browser game's Wasm contains no Lua VM. Imported code, console edits and objective callbacks evaluate in workers. A check enforces this. | The guarantee comes from the build, not from a convention. Strings from the P4.9 game Wasm show Lua's parser and VM messages absent, so this holds today. |
| A test proves each catalog entry equals evaluating its embedded source. | Decoded and evaluated revisions must not drift apart. |
| The console, tool bridge and later in-game agent share one command layer and only ever submit candidates through staging. No command writes to the live world, and no Lua runs in the tick. | The P6 agent becomes another caller of a path humans already use. Simulation stays deterministic and the existing validation, Undo and revision history apply unchanged. |
| A live edit never moves the player's boat. An edit needing that is held until the boat is clear or the player asks. | An agent editing during play must not teleport or reset the player. |
| Full browser qualification stays manual-only. | The user reduced it deliberately. Dispatch it once per PR where this plan says to. |

Out of scope for all six PRs: authority objective callbacks and commands in rooms (the rest of
[P5](PLUGIN_PLAN.md#p5--authority-prediction-and-recorded-revisions)), session recordings, room
downloads and dynamic maps (room stages R2 and R4), the in-game agent itself (P6, which PC prepares
for), lighting and other
[location-life](PLUGIN_PLAN.md#location-life-track-proposed-2026-10-30) additions from L5 on,
living-city actors, package signing and new physics primitives. Rooms are not recorded today, so
this plan adds no recording identity to them.

Baseline measured 2026-10-31 on `e335b84`, Release, Apple Silicon/macOS, display-free CPU capture
of a three-second drive: compiled `outer` 0.80 s and `langedrag` 0.80 s (four runs each, within
0.01 s), `harbor-life` 1.56 s (one run), so about +0.75 s. With `--start 2,170,90`, `harbor-life`
took 4.90 s (four runs) against 4.17 s for compiled (one run), about +0.73 s. The traffic sweep at
`src/boat_plugin.c:599` is the likely cause, but no profile has confirmed it.
`check_scene_traffic` samples a 4,160 s horizon every 0.25 s. Build-time compilation should remove
this cost from bundled launches; a console edit pays it in a worker while the game keeps running.

## How to execute this plan

### One step, one PR

Each step below (PD1–PD5 and PC) is **one branch and one PR**, however long it takes. Make as many
small, focused commits inside it as the work needs. Do not split a step into several PRs and do not
start the next step unless told to.

Reduce CI churn. Workflows run on pull requests and pushes to `main`, not on ordinary branch pushes:

1. Work in a separate worktree, on a branch named `feat/plugin-default-pdN` (`-pc` for PC), from
   current `main`.
   If the parent PD is still unmerged and you were told to continue, branch from the parent and
   target it, following the stacking rule in [AGENTS.md](../AGENTS.md).
2. Commit often. Push the branch whenever you like as a backup. **Do not open the PR yet.**
3. Open the PR only when the step's whole acceptance list passes locally.
4. After that, push rarely: collect every fix for a CI failure, then push once. Do not push to
   poll CI, and do not push documentation tweaks separately.
5. Run the manual browser workflow only where a PD says to, once, on the final revision.
6. Do not merge the PR, cut a release or deploy. The user does those.

### Working loop

Before starting, read this plan, [AGENTS.md](../AGENTS.md), the [testing guide](TESTING.md) and
the documents named in your step. This plan names files and symbols from `e335b84`, so confirm they
are still accurate. Where the source disagrees with this plan, the source wins: adapt, and record
the difference in your step's result.

Work through the step's tasks in order and commit each one with the tree building and
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

### What every step must leave behind

- A dated result subsection under the step here: what was implemented, deviations, commands
  actually run with their platform and counts, measurements, known limits, and **Needs the user**.
- The status table above updated, and the **Next task** line moved on.
- Affected guides updated in the same PR: at least [PLUGIN_API.md](PLUGIN_API.md),
  [COMMAND_LINE.md](COMMAND_LINE.md), [TESTING.md](TESTING.md) and `docs/README.md`'s row when the
  area summary changes. Search for claims your change makes false; do not only add new text.
- A PR body stating the outcome, verification performed, limits and needed human checks.

Every step's local acceptance includes: a warning-clean Debug build; `ctest --preset full` with
sockets; `git diff --check`; every changed local Markdown link resolving; and CI green on the PR
after opening it.

## PD1 — bundled runtime and complete solo coverage

**Outcome.** On native builds, `./build/wake_velocity` and the title's solo choices run every
session from a catalog entry compiled at build time, with no Lua VM started. They look and play
exactly as they do now. Rooms and the browser
still use compiled content. A temporary `--no-plugins` restores the compiled path for parity.

Read first: `tools/wake_velocity_plugin.[ch]`, `tools/wake_velocity.c`, `tools/boat_title.[ch]`,
`src/boat_plugin.[ch]`, `src/boat_plugin_host.[ch]`, `src/boat_plugin_stage.c`,
`src/boat_course.[ch]`, `src/boat_course_view.c`, `src/boat_replay.c`,
`tools/boat_race_plugin.[ch]`.

### Tasks

1. **Split runtime from authoring.** Runtime state opens from a decoded catalog entry and has no
   file watch, stage worker, bridge, hotkeys or panel. Authoring is today's behaviour and remains
   `--plugin DIR`, including its fail-fast exit on a bad package. Do not fix this at open for the
   whole session: PC must be able to move a running session from runtime to authoring, so keep
   revision 1's source available and make the transition one-way rather than a launch-time mode.
   In `tools/wake_velocity.c`, replace the `plugin_path` gates with separate "plugin active" and
   "authoring" conditions. The F5–F7 help line, `wake_plugin_draw` and the browser control calls
   depend on authoring only. Static storage and the 1 MiB owner assertion stay.
2. **Add the generator and catalog.**
   - New `tools/boat_plugin_catalog_gen.c` links `proteus_plugins` and takes package directories. For
     each it evaluates the source, runs the same checks as staging (map, scene and the full traffic
     sweep), rejects objective callbacks, and writes a generated C file holding the decoded definition,
     manifest, source and digest as **byte arrays**, not string literals (MSVC limits literals).
   - Reuse the fieldwise stage wire in `src/boat_plugin_stage.c` as the encoded form, extended to all
     kinds. Producer and consumer come from one build, so it needs no compatibility promises.
   - Avoid a link cycle: generated output and the lookup (`src/boat_plugin_catalog.[ch]`, by ID and by
     default for a kind and location) form a new static library, `proteus_plugin_catalog`, that depends
     on `proteus_plugins`. The generator must not link it.
   - Use `add_custom_command` with `$<TARGET_FILE:...>` so multi-config generators work. Rebuild when a
     package or the generator changes. An invalid package fails the build with its normal diagnostic.
   - Add `boat_plugin_host_start_decoded`, which installs a catalog entry as revision 1 without
     evaluating anything. Follow the checks and trust comment around `boat_plugin_host_stage_decoded`.
   - Do not cross-compile yet. Emscripten continues on compiled content until PD4.
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
7. **Measure startup.** Extend `boat_plugin_bench` so it accepts packages without objectives. Profile
   the current `harbor-life` cost once to confirm or correct the baseline's explanation. Record default
   and `harbor-life` launch costs, and the generator's build-time cost in Debug and Release. If a Debug
   sweep is slow enough to hurt iteration, optimize the generator target, not the check.
8. **Add the title choice.** On native builds add a saved content row, shown for `outer`: Standard
   or Harbor life. `title_single` passes `--package`. Failure to open returns to the title with the
   diagnostic rather than exiting. Older settings files must still load. Hide the row on web until
   PD4. Capture and inspect the title.
9. **Add the parity gate.** New CTest `plugin_default_parity` (label `cpu`) runs `wake_velocity
   --renderer cpu` with default and `--no-plugins` for basin, river, outer and `--course`, and
   compares captures byte for byte. Add validator cases proving each catalog entry equals evaluating
   its embedded source, the `basin`, `river`, `outer` and time-trial entries equal the compiled
   definitions, and a solo session trace hash matches over a long drive (reuse `compiled_parity`).

### Acceptance

- `plugin_default_parity`, the catalog equality cases and the legacy-best case pass. A deliberately
  invalid package fails the generator in a test. The
  legacy case proves a pre-existing `course-*-physics*.run` file loads as the best, with its path
  unchanged, and is never overwritten by an equal or worse run.
- Default launches show no plugin panel, poll no files and create no Lua state. Add a test for this
  (for example a counter in `boat_plugin_lua`).
- `--plugin` behaviour, including the existing refusal tests, is unchanged.
- Default `outer` startup is no slower than the baseline within measurement noise, and bundled
  `harbor-life` no longer shows the +0.75 s delta. Report the numbers against the baseline above.
- Captures of the title row inspected; a windowed `--demo` run performed or reported as impossible.
- Windows CI builds and runs the generator and exercises the catalog.

**Needs the user:** play default solo modes for regressions, and judge Harbor life from the title.

## PC — live console and edits without reset

**Outcome.** During solo play, a key opens a Quake-style console. Typing Lua changes the session's
definition, and the game rebuilds around the player without a reset: the boat, clock, water and wakes
carry on. Anything unsafe is held rather than moving the boat. Every operation can be undone and saved
as a package. The tool bridge gets the same commands, which gives the later [P6](PLUGIN_PLAN.md#p6--in-game-asynchronous-agent)
agent its tool surface.

Today's blocker is reload policy, not the console. Most useful edits restart: any scene layout change
is `BOAT_PLUGIN_RESTART_SESSION`, returning the boat to the spawn (`tools/wake_velocity_plugin.c`,
`commit`), and voyage route changes return it to the start shelter. Only renames and gull cues keep
progress. So this PR's core is a new live-apply policy, which also improves `--plugin` saves and F5.

Read first: [hot reload](PLUGIN_PLAN.md#hot-reload-is-the-normal-development-path), the
[replacement rules](PLUGIN_API.md#replacement-restart-and-owner-lifetime), the
[live bridge](PLUGIN_API.md#live-tool-bridge-p46), [P6](PLUGIN_PLAN.md#p6--in-game-asynchronous-agent),
`tools/wake_velocity_plugin.[ch]`, `tools/wake_velocity_bridge.c`, `src/boat_plugin_host.c`,
`src/boat_plugin_stage.c`, `src/boat_session.c`, `src/boat_scene_definition.[ch]`, and the scene
consumers in `src/boat.c`, `src/boat_combat.c`, `src/waterfront_*.c`, `src/boat_view.c`, `src/boat_gpu.c`.

### Language

A plain line is a Lua **edit** of `def`, the table the package's entry returns. A line starting `=` is a
read-only **query**. A line starting `/` is a **command**.

```text
> def.scene.marina.berths[1].x = 15
R2 ACTIVE, LIVE / BOAT KEPT
> def.scene.gulls.flocks[1].count = 4
R3 ACTIVE, LIVE / BOAT KEPT
> = def.scene.marina.berths[1]
{ id = "skiff", recipe = "skiff", x = 15, z = 167, ... }
> def.voyage.exposure.height = 3
R4 HELD: WATER CHANGE. /apply RESTARTS THE SESSION, /discard DROPS IT
> /undo
R3 ACTIVE, UNDONE / BOAT KEPT
> /save ~/drafts/my-harbor
SAVED ~/drafts/my-harbor
```

Commands: `/help`, `/status`, `/capabilities`, `/recipes`, `/anchors`, `/apply`, `/discard`, `/undo`,
`/restart`, `/capability NAME`, `/save DIR`, `/clear`. A few developer conveniences may be added when
they map onto existing session operations, such as `/tp ANCHOR`, but only as ordinary solo commands.
Console Lua cannot read or write the live boat, world or files. C changes still require a relaunch, and
the console says so if asked for something with no capability instead of pretending.

### Tasks

1. **Add the command layer.** New `tools/wake_command.[ch]`, with no raylib or file access: parse a line,
   submit it to `WakePlugin`, and append bounded result lines to a caller-owned buffer. Results are
   asynchronous, so it tracks pending work and reports completion. Route the bridge's existing `stage`,
   `apply`, `undo`, `discard` and `inspect` operations through the same functions, keeping their
   base-revision and epoch pins. Add a bridge `console` operation and `plugin_author.py live-console`.
2. **Promote a running session.** The first edit moves the session from runtime to authoring, using the
   source embedded with the catalog entry as the base. Plain play before that stays VM-free. Edits use
   the isolated stage process natively and the worker on the web, so Lua never runs on the game thread
   and the game keeps running while the sweep completes. A second edit while one is staging queues in
   order, bounded, with a clear message when full.
3. **Compose edits as source.** A candidate remains one immutable entry file, so staging, revisions,
   Undo and export need no new concept:

   ```lua
   local def = (function()
     -- the package's main.lua, unchanged
   end)()
   do def.scene.marina.berths[1].x = 15 end -- console 1
   do def.scene.gulls.flocks[1].count = 4 end -- console 2
   return def
   ```

   Map diagnostics back to `console N`, and to the entry file for base lines. Syntax and schema failures
   install nothing and drop that chunk. Enforce the 64 KiB source cap with a clear message. `/undo` steps
   through revisions and their chunks together. In a `--plugin` session, disk stays the base: a save
   rebases the chunks on the new bytes, and a chunk that no longer applies is reported, not hidden. Check
   the interplay recorded for P4.6, where save polling can drop a held Undo if bytes do not match.
4. **Add queries.** `= expr` evaluates against the current candidate in the isolated worker and returns a
   bounded rendering, capped by depth and size, with functions shown as `<function>`. Add a size-limited
   reply field to the stage wire. A query never creates a revision.
5. **Add capabilities.** `/capability scene.define` adds a grant to the session's manifest copy, in the
   host's canonical order. An edit that needs a missing grant fails with a hint naming this command.
6. **Add the live-apply policy.** Add `BOAT_PLUGIN_LIVE` to the classification. It applies to every
   staging source: console, `--plugin` saves, F5 and the bridge.

   | Change | Today | After |
   | --- | --- | --- |
   | Name, gull cue | Progress kept | Same |
   | Berth, module, entrance | Session restart | Live if the boat clears the new envelopes with margin; otherwise held and retried each tick boundary until clear |
   | Travelling craft, schedule, flock layout | Session restart | Live under the same clearance check, with actors evaluated at the current time |
   | Shelter, destination, arrival | Voyage restart | Live: map progress by shelter ID. A removed active shelter falls back to the start shelter for recovery; the boat stays put |
   | Exposure, weather range | Session restart | Held for `/apply`. Water sources are out of scope here |
   | Objective | Migrate or voyage restart | Same, explaining what would reset. Add an objective-only reset if it proves simple |
   | Time-trial course, race | Run restart | Same; fairness comes first |
   | Kind, location, API | Relaunch | Same |

   Audit every scene consumer for a pointer swap at a tick boundary: contacts, combat, camera clearance,
   mesh and light collection, GPU and CPU drawing, the rearview, traffic wake pools and preview checkpoints.
   Wakes from removed craft must expire without new emissions, and added craft must not emit a burst. Keep
   retired revisions alive under the existing lifetime rule. Update the API's replacement rules, and the
   `harbor-life` README and tests that describe layout edits as restarts: this is a deliberate contract
   change, so replace those tests with stronger ones rather than deleting coverage.
7. **Build the console UI.** New `tools/wake_console.[ch]` for input, history and bounded scrollback, with
   drawing separated from logic so tests stay headless. Draw a low-resolution drop-down overlay with the
   game's bitmap fonts and integer scaling. Input comes from `GetCharPressed`; suspend driving input as
   the existing `active`/`wake_plugin_editing` logic does, and do not pause simulation.
   - **Key:** accept `KEY_GRAVE`, GLFW's world-1 key (161, where macOS ISO keyboards may report `§`) and
     `F2`. None is used in the game today. Do not put the opening key's character in the input.
   - **Availability:** solo free boating and voyages, in release builds too, because the sandbox and
     staging are the same as imports. It is read-only (`/status`) in rooms and off in replay, `--demo` and
     screenshots. Editing a time trial runs under the existing `practice` latch, so it never records a best.
8. **Add saving.** `/save DIR` writes a complete package natively, reusing the pinned single-file
   replacement from [P4.4](PLUGIN_PLAN.md#p44-result-2026-10-30). It refuses a non-empty directory that
   is not this session's package, and never writes into the installed catalog. Add the browser's export
   path if PD4 has merged; otherwise PD4 adds it.
9. **Document.** Add the console to [PLAYING.md](PLAYING.md), [COMMAND_LINE.md](COMMAND_LINE.md) and the
   [API](PLUGIN_API.md), and update the P6 section of the [plugin plan](PLUGIN_PLAN.md) so its tool
   dispatcher is this command layer.

### Acceptance

- **The key proof:** with a scripted input, one session gets a berth edit far from the boat at tick T and
  a control gets none. Boat and voyage traces are identical tick for tick, and the edit is visible in a
  capture. Repeat the run to show the same edit at the same tick gives the same hash.
- Moving a berth under the boat holds it; the boat is never displaced. The edit commits at the first
  clear boundary after the boat leaves.
- `/undo` restores the prior scene with the boat kept, tested through consumers with contact against the
  edited hull before and after. Failed syntax, schema and budget edits leave the active revision running.
- Adding and removing craft produces no wake bursts or stale emissions, across the bounded pools.
- Every command works headless under a virtual clock, including queue limits, stale results and `/save`.
- `plugin_author.py live-console` drives the same results as typed lines.
- Plain play still creates no Lua state until the first edit, with PD1's test extended to prove it.
- Representative CPU captures before and after edits inspected; a windowed run performed or reported as
  impossible. Frame cost with the console open and during a staged edit recorded.

If a consumer cannot take a live swap safely, leave that change class held, record why and land the rest.

**Needs the user:** try the console while driving, and check which key opens it on your keyboard. Judge
whether live edits read as continuous, and whether held edits are understandable.

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
3. **Make the authority load packages.** `tools/boat_server.c` gains `--package ID|DIR` and defaults
   from `--mode` and `--location`. A catalog ID uses the decoded entry with no Lua. A directory is
   evaluated once at start-up with full validation. Either way it builds the definition and passes it to `boat_net_server_init` through the config,
   replacing `race` rules and match fields. A bad directory package refuses to start the room with the
   diagnostic. Existing rule-override flags apply after loading. `proteus_boat_net` stays free of
   Lua: the executable links `proteus_plugins` and `proteus_plugin_catalog`. The in-game host passes the same flag to its
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

**Outcome.** Browser solo play starts from the same compiled catalog as native, including Harbor life.
Imported voyages may carry scenes. Normal play starts no worker, and the game's Wasm has no Lua VM.

Read first: [Browser builds](WEB_BUILD.md), including its plugin sections,
[browser source bundles](PLUGIN_API.md#browser-source-bundles-p43), `web/plugin_author.js`,
`web/plugin_worker.js`, `src/boat_plugin_stage.c`, `tools/wake_velocity_plugin.c`, `cmake/Web.cmake`,
`tools/package_web.py`, `tools/check_web_plugin_author.py`.

### Tasks

1. **Generate the catalog for Wasm.** Build `boat_plugin_catalog_gen` for Emscripten as a Node target
   (`-sENVIRONMENT=node -sNODERAWFS=1`), run it through `CMAKE_CROSSCOMPILING_EMULATOR` during the web
   build, and link the output into the game. If running it under Node proves impractical, fall back to
   a host-built generator, provided the wire stays fieldwise and portable; record which you chose and
   why. Do not add Lua to the game as a fallback. Add a check that the game Wasm lacks Lua's VM and parser
   (for example its message strings or symbols) and that `proteus_lua` is not in its link. Record game
   Wasm size before and after, raw and compressed.
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
5. **Bring the console to desktop browsers.** If PC has merged, enable it where a physical keyboard
   exists, with edits and queries going to the worker and `/save` using the export path. Do not add it to
   mobile. If PC has not merged, it adds this instead.
6. **Update qualification.** Extend `check_web_title.py`, `check_web_plugin_author.py` and the Node
   contracts for default sessions, Harbor life, scene imports, tab freeze and repeated sessions. Do
   not enable the full matrix on PRs.

### Acceptance

- Browser default-session CPU/GPU comparison shows no unexplained difference from before.
- No worker is created in a default session, and the Wasm check proves no Lua is on the game thread.
- Local three-engine `check_web_plugin_author.py` and `check_web_title.py` pass, with captures
  inspected and costs recorded: game start, memory, Harbor life frame time. Run the manual browser
  workflow once on the final revision.
- `check_web_multiplayer.py` and `check_web_mobile.py` still pass.

**Needs the user:** Harbor life and normal play on an iPhone and in physical Safari. Those results
decide whether Harbor life is offered on mobile.

## PD5 — cutover

**Outcome.** There is one path for play. The compiled selection, the switch and the plugin compile-time flag
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
   `boat_race_trial`, the review tools) use the catalog or the room definition. The generator's equality
   cases then compare with fixtures. Add a check that no production target links the fixture library.
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

- **[In-game agent (P6)](PLUGIN_PLAN.md#p6--in-game-asynchronous-agent):** builds on PC. The agent adapter
  submits the same lines and commands as a player, so its work appears in the console, is undone the same
  way and cannot move the boat. Provider, credentials and cost controls stay P6's work.

## Planning verification

Reviewed 2026-10-31 against `e335b84`: the plugin, multiplayer, room content, deathmatch playtest,
living city, sunset, adventure and architecture documents and the source seams cited above, plus the
reload classification, session commit, tool bridge and key bindings for PC. The console design is a
proposal: none of it has been prototyped, and generating the catalog under Node has not been tried. CI
triggers were read from the workflow files, and start-up was measured as stated in the baseline.
Nothing here is implemented, and no user acceptance is recorded or implied.

Handoff verification, 2026-10-31, Apple Silicon/macOS, documentation-only, on this branch before the
build-time catalog and console revision (source unchanged since): Debug build passed and
`ctest --preset full` passed all 323 enabled tests with local sockets (13 retired tests disabled,
GPU CTests off). All 433 local links and anchors across the five changed
documents resolve, and `git diff --check` passes. No audiovisual review was needed.
