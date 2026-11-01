# Plugins by default: implementation plan

Status, 2026-10-31: **PD1, PC, Rooms, Browser and Cutover (PD5) are implemented on
`feat/plugin-default-cutover`, ready for review.** Native and browser solo sessions run from
packages compiled at build time, `--plugin` is the separate authoring path, and there is no
compiled-content switch. The live console edits the running session without a reset, natively
and in desktop browsers. This document owns the sequence, per-PR status and acceptance for that
move. The [plugin plan](PLUGIN_PLAN.md) and [API](PLUGIN_API.md) keep the existing plugin
contract, the [multiplayer protocol](MULTIPLAYER_PROTOCOL.md) keeps byte layouts, and the
[room content plan](ROOM_CONTENT_PLAN.md) keeps downloads and dynamic maps.
[All plans](README.md).

Next task: the user's iPhone run, which decides whether Harbor life becomes the `outer` default;
`outer` stays on Standard until then. Cutover merged in
[#108](https://github.com/doomdavve/proteus/pull/108) and shipped as
[v0.0.24](RELEASES.md#status-and-verification) on 2026-11-01, with the hosted room verified. No
implementation step remains in this plan; the [PD5 result](#pd5-result-2026-10-31) lists the human
checks.

User play report, 2026-11-01, after the v0.0.24 release (build and platform not stated): a
two-person harbor room with deliberate contact "worked fine", and deathmatch "worked well". The user
still cannot jump over another boat; the
[deathmatch playtest plan](DEATHMATCH_PLAYTEST_PLAN.md#steps-5a5c--vertical-contacts-and-crossings-during-a-tick)
owns that.

| Step | Outcome | Depends on | Status |
| --- | --- | --- | --- |
| [PD1](#pd1--bundled-runtime-and-complete-solo-coverage) | Every native solo session runs from a bundled package; authoring is separate | — | Implemented 2026-10-31; merged in [#96](https://github.com/doomdavve/proteus/pull/96) |
| [PC](#pc--live-console-and-edits-without-reset) | In-game Lua console; edits apply without resetting the session, on the path a later agent uses | PD1 | Implemented 2026-10-31; merged in [#98](https://github.com/doomdavve/proteus/pull/98) without PD4 |
| [PD2](#pd2--package-defined-rooms) | Rooms run from a package; clients receive data, protocol v9 | PD1 | Implemented 2026-10-31 in [#100](https://github.com/doomdavve/proteus/pull/100) |
| [PD3](#pd3--authored-scenes-and-revisions-in-rooms) | Authored scenes with contacts in rooms; revisions between rounds | PD2 | Implemented 2026-10-31 in [#100](https://github.com/doomdavve/proteus/pull/100); round-boundary revisions deferred to v10; [follow-ups](#rooms-follow-ups) open |
| [PD4](#pd4--browser-parity) | Browser solo play runs from bundled packages, scenes included | PD1, PC | Implemented 2026-10-31; merged in [#106](https://github.com/doomdavve/proteus/pull/106) |
| [PD5](#pd5--cutover) | The room gaps a release needs are closed; the compiled selection and the off switch are gone | PD3, PD4 | Implemented 2026-10-31; merged in [#108](https://github.com/doomdavve/proteus/pull/108); released as v0.0.24 |

### Packages

PD1 took about 30 minutes (user report, 2026-10-31), so the user asked for bigger units of work. Each
remaining **package is one branch and one PR**. The step sections below stay the specification.

| Order | Package | Steps, in order | Branch | Why these belong together |
| --- | --- | --- | --- | --- |
| Done | **Rooms** | PD2, PD3 | `feat/plugin-default-rooms` | One protocol bump and one hosted cutover instead of two. PD3 only extends the definition PD2 introduces. |
| Done | **Browser** | PD4 | `feat/plugin-default-browser` | The rest of the original Solo package. [#98](https://github.com/doomdavve/proteus/pull/98) delivered PC alone. |
| Done | **Cutover** | PD5 | `feat/plugin-default-cutover` | Proves the last room gaps first, then removes the fallback that those proofs and the other packages still compare against, and leaves one releasable tree. |

Rooms goes before Browser because rooms are the largest remaining gap and the riskiest work, while
browser solo already works on compiled content. They do not depend on each other: a browser room client
consumes the room definition and needs neither the catalog nor PD4. Do not combine them. Their risks are
unrelated, and the room PR needs careful review.

Inside a package:

- Work through its steps in order and do not stop at the boundary between them. Run the full acceptance
  list of each step, then push the branch as a backup before starting the next.
- Keep a separate result subsection and status row for each step.
- A step's "stop and report" or "land the rest" clause limits that step. If the first step lands in a
  reduced form, continue with the second where it is still correct and report the reduction. Stop only if
  the second depends on what was dropped.
- If time or confidence runs out, open the PR with what is complete, and name what remains. A complete
  first step is a good PR; an unstable second step should not be included in it.

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

Out of scope for every step: authority objective callbacks and commands in rooms (the rest of
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

### One package, one PR

Each [package](#packages) is **one branch and one PR**, however long it takes. Do not split a package
into several PRs and do not start the next package unless told to.

Make small, focused commits and **do not squash them**, with at least one commit per task. A large PR
stays reviewable only through its commit history, and PD1 arrived as one commit of about 2,700 lines.
Finish every step of the package unless a stop rule applies. The PC PR stopped before PD4 with no blocker
recorded, which left Solo half done.

Reduce CI churn. Workflows run on pull requests and pushes to `main`, not on ordinary branch pushes:

1. Work in a separate worktree, on the package's branch, from current `main`. If the previous package
   is still unmerged and you were told to continue, branch from it and target it, following the stacking
   rule in [AGENTS.md](../AGENTS.md).
2. Commit often. Push the branch whenever you like as a backup. **Do not open the PR yet.**
3. Open the PR only when the whole acceptance list of every included step passes locally.
4. After that, push rarely: collect every fix for a CI failure, then push once. Do not push to
   poll CI, and do not push documentation tweaks separately.
5. Run the manual browser workflow only where a step says to, and only once per package, on its final
   revision.
6. Do not merge the PR, cut a release or deploy. The user does those.

### Working loop

Before starting, read this plan, [AGENTS.md](../AGENTS.md), the [testing guide](TESTING.md) and
the documents named in your step. The Cutover references were rechecked on `381e802`; earlier steps name files
and symbols from older revisions. Confirm they are still accurate. Where the source disagrees with this plan, the source wins: adapt, and record
the difference in your step's result.

Work through the step's tasks in order and commit each one with the tree building and
`ctest --preset quick` green. Run the full acceptance list before opening the PR. Do not stop to
ask about anything this plan or the code can settle. If you pause, a fresh agent must be able to
continue from your commits and the result section.

Keep these rules throughout:

- Format with `.clang-format`, stay warning-clean, and write Windows-safe C. Windows is only
  checked in CI, so avoid POSIX-only calls and very long string literals (MSVC limits them). Any file
  including `<windows.h>` must define `NOGDI` and `NOUSER` first if raylib's headers can also reach it
  (`Rectangle`, `CloseWindow` and `ShowCursor` clash, which broke PD1's first Windows run).
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
- **Browser checks must never join the hosted room.** The join panel's default server is Winterfell, so
  a script's blind key presses can connect to production. Type a local URL, and read any new key sequence
  against the current menu. A stale script joined it once on 2026-10-31.
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

### PD1 result, 2026-10-31

**Implemented.** Native solo sessions open a build-time catalog entry through
`boat_plugin_host_start_decoded`: no Lua state, file poll, staging worker, bridge or panel. The
F5/F6/F7 controls, save polling, bridge and panel now belong to `--plugin DIR` only. The
handwritten generator `tools/boat_plugin_catalog_gen.c` evaluates each package with the normal
loader, runs the staging checks (manifest, schema, map, scene and the full traffic sweep),
rejects objective callbacks and writes `build/generated/boat_plugin_catalog_data.c` as byte
arrays. `proteus_plugin_catalog` (lookup by ID and by kind/location default, plus decode) depends
on `proteus_plugins`; the generator does not link it. `add_custom_command` uses
`$<TARGET_FILE:...>` and depends on every package file.

The new `free` and `course` kinds, their `free.define`/`course.define` grants, the stage wire
(PST6), `BoatPluginHost.kind`, revisions, classification and Objective/Undo paths are implemented
for all four kinds. `plugins/basin-free`, `plugins/river-free` and `plugins/time-trial` join
`langedrag`, `river-sprint` and `harbor-life` in the catalog. The time trial's start and gates are
bound into `BoatSession`/`BoatCourse`; the compiled digest keeps
`course-{calm|waves}-physicsN.run`, while another course appends its digest and refuses
`--replay`/`--ghost`. `--package ID` selects a bundled entry; `--no-plugins` restores the compiled
path; unknown IDs and location/kind mismatches fail explicitly. The title's native single-player
panel gains a saved CONTENT row for `outer` (Standard or Harbor life), and a failed open returns
to the title with the diagnostic.

**Deviations.** The optional `free` scene on the river was deferred: a free package has no voyage
route, so the existing scene checks would need a new no-route validation path; the river therefore
keeps its compiled scene and `harbor-life` remains a selectable `outer` voyage. The generated C
file stores the bulk definition, manifest text and source as byte arrays but keeps short ID/name/
kind/location strings as literals. `boat_plugin_bench` accepts packages without objectives and
skips the callback benches for them. `boat_plugin_classify` now considers source bytes only when
an objective is present, because non-objective revisions now retain their source for a later
console.

**Verification actually run, Apple Silicon/macOS, 2026-10-31.** Debug configure/build from the
pinned dependency sources: warning-clean apart from raylib's `stb_vorbis.c` warning.
`ctest --preset quick` passed all 231 enabled tests. `ctest --preset full` passed all 329 enabled
tests (13 retired field tests disabled, GPU CTests off). That includes
`plugin_default_parity` (4.6 s), the three `boat_plugin_catalog.*` cases,
`boat_plugin_catalog_invalid`, `boat_plugin_session.catalog_runtime`, `boat_plugin_author`, `boat_plugin_capture`,
`boat_plugin_stage`, `wake_velocity_bridge` and the extended `boat_replay` legacy-best case.
`git diff --check` passes; changed local links were checked. CPU captures of basin, river, outer
and `--course` are byte-identical between the catalog and `--no-plugins`. A windowed
`--location river --demo --frames 60` run completed. Title captures of the SOLO panel with
`CONTENT < STANDARD >` and `CONTENT < HARBOR LIFE >` were inspected. The game was run from the
repository root so relative assets resolved.

**Measurements, Release, Apple Silicon/macOS, display-free CPU capture of a three-second drive.**
Four runs each after one warm-up: compiled `outer` 0.79 s (range 0.79–0.80), default catalog
`outer` 0.79 s (0.79–0.80), bundled `harbor-life` 0.79 s (0.79–0.80). The first uncached default
run took 1.12 s while the system scanned the new binary. Against the baseline (compiled
`outer` 0.80 s, `harbor-life` 1.56 s) the +0.75 s sweep is gone. The generator takes 0.80 s p50
(0.81 s max) for all six packages in both Debug and Release builds on this machine; most of that
is `harbor-life`'s traffic sweep.

**Known limits.** Emscripten still uses compiled content and was not built for this step; the
browser worker and constructor reject `free`/`course` explicitly. GPU CTests and
the manual three-engine browser workflow were not run. The `free` river scene and package-defined
rooms remain for later steps. Linux, macOS, Windows and Browser plugin authoring CI passed on the
merged revision of [#96](https://github.com/doomdavve/proteus/pull/96), after a fix for a `windows.h`
clash in `validate_boat_plugin_session.c`.

**Needs the user.** Play the default basin, river, outer and time-trial sessions for regressions;
judge Harbor life from the title's CONTENT row; confirm the title row's Standard/Harbor life
wording.

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
   is not this session's package, and never writes into the installed catalog. PD4 adds the browser's
   export path.
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

### PC result, 2026-10-31

**Implemented.** `tools/wake_command.[ch]` is the one command layer for the console and the tool
bridge. It parses edits, `=` queries and `/` commands, composes every accepted edit onto the
package's unchanged entry as one immutable file, submits it through the isolated staging worker,
and appends bounded result lines. The first edit promotes a bundled session to authoring with the
catalog revision's embedded source. A second edit while one is staging is refused with a clear
message; syntax and schema failures drop that chunk and keep the active revision. `/undo` steps
revisions and chunks together; `/capability scene.define` rewrites only the session manifest copy;
`/save DIR` writes `plugin.json` and `main.lua` atomically and refuses a non-empty directory that is
not this session's package. Queries run in the same worker, render a depth- and size-bounded value
and create no revision.

`BOAT_PLUGIN_LIVE` is the new classification for voyage route gameplay and scene layout edits.
`wake_plugin_live_ready` checks the hull against the candidate berth, module and moving-craft
envelopes with 0.5 m to spare; a clear change installs at the next tick boundary, and a blocked one
is held and retried each frame until clear. `boat_session_rebind_voyage_live` maps a reached shelter
by ID and falls back to the start when it is removed. `boat_scene_clear` is the shared clearance
query. Exposure/weather changes and course edits still hold for F7 and restart; objective
migrations and restarts are unchanged.

`tools/wake_console.[ch]` owns input, history and bounded scrollback; drawing is separate. The key
is `` ` ``, `F2` or 161 (GLFW world-1, `§` on some macOS ISO keyboards). The console is available in
native solo free boating and voyages, including release builds; it is unavailable in rooms,
replays, `--demo` and screenshots. While it is open, driving input is suspended but the simulation
continues with a neutral helm. A time-trial edit sets the existing `practice` latch, so no best is
recorded. The bridge gains `console` (one line, epoch-pinned, repeated `output` lines) and
`plugin_author.py` gains `live-console --line TEXT`.

**Deviations and limits.** PD4 (browser parity) is not included in this revision, so the browser
keeps compiled solo content and the console is native-only. The bridge's existing `stage`, `apply`,
`undo` and `discard` operations still call the same plugin primitives directly rather than the
command layer's line parser; they keep their revision/epoch pins and the new `console` operation
uses the shared state. A `--plugin` save that lands while console chunks exist stages one
composition against the new disk base; if it cannot apply, the last chunk is reported and dropped
rather than rebased chunk by chunk. A second edit while one is staging is refused with a message
naming the staging state rather than queued, so the console cannot build an unbounded queue.
Rooms have no package host yet, so the planned read-only
`/status` console in rooms is not offered. Shift/AltGr-modified key handling follows
`GetCharPressed` as before; there is no completion or multiline entry.

**Verification actually run, Apple Silicon/macOS, 2026-10-31.** Debug build from the pinned
dependencies: warning-clean apart from raylib's `stb_vorbis.c` warning. `ctest --preset full`
passed all 330 enabled tests (13 retired field tests disabled, GPU CTests off), including the new
`wake_command` case, `wake_velocity_bridge` with its console operation, `boat_plugin_session`
(classification, live route policy, Undo, game controls, Harbor life scene),
`plugin_default_parity`, `boat_plugin_catalog.*`, `boat_plugin_author` and
`boat_plugin_capture`. `git diff --check` passes. The Emscripten `wake_velocity` target builds
(the pre-existing `boat_race_trial` web link failure is unchanged and outside this step). A
windowed `--demo` run and an interactive console session were not possible in this environment
because the harness opened no window; the console input/history/scrollback and every command were
driven headlessly instead. Two CPU captures of the Harbor life marina at `--start 6,190,180`, one
from the package unchanged and one with the runabout's `z` changed from 173 to 179, were inspected:
the moored hull visibly moves and the rest of the frame is unchanged.

The key proof runs in `wake_command`: one session receives a berth edit far from the boat at tick
20 and a control receives none; their boat and voyage traces are identical tick for tick
(`9419d4ffbd71b8a5` in the run recorded here) and a repeat of the same edit at the same tick gives
the same hash. A berth moved under the hull is held and commits at the first clear boundary with the
boat at its new position; Undo restores the prior envelope with the boat kept and the chunk count
back at 1. A malformed edit is rejected and keeps the active revision. No Lua state is created on
the game thread; the child worker performs every evaluation.

**Needs the user.** Open the console while driving on your keyboard (`` ` ``, `F2` or `§`) and
judge whether live edits read as continuous and held edits are understandable. Play Harbor life
with a berth edit near the boat and check that the boat is never nudged. Decide whether the
native-only console is acceptable until PD4.

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

### Settled from the source, 2026-10-31

Each of these was checked on `4e11c2a`. The tasks below assume them.

| Finding | What to do |
| --- | --- |
| The hosted room runs `--mode deathmatch --location river` (`ansible/roles/wake_velocity/defaults/main.yml` in `davve-srv`). Its service line passes both flags. | That command must keep working with no `davve-srv` change. One `plugins/deathmatch` serves every location: a `match` package without a scene takes its location from `--location`, and the definition always carries the resolved location. A package with a scene fixes its location, and a conflicting flag refuses to start. |
| No free package exists for `outer`, and `--location outer` is hostable. | Add `plugins/outer-free`. Defaults with no `--package`: free uses `basin-free`, `river-free` or `outer-free`; deathmatch uses `deathmatch`; race uses `river-sprint`. Solo is unaffected, because the title's rows and `--package` kind checks already exclude these. |
| Race rooms are river-only in `boat_net_server_init`, the server's flags and the `race` manifest rule. | Keep that. Only the course's values become data, not the map. |
| `boat_combat_enable` hardcodes the default rules (`src/boat_combat.c:60`). Clients already install rules from the checkpoint, and compact pages omit them. | Give the enable call the rules. Do not add a second path for them on the client: it compares the checkpoint's rules with the definition at install and closes on a mismatch. |
| `BOAT_NET_EARLY_BOOTSTRAP` holds one checkpoint's messages until Accept and **drops the rest**, which on a reliable channel leaves the client stuck in bootstrap. | Size it for the largest definition plus the largest checkpoint, and test every bootstrap message arriving before Accept. Bootstrap and control are separate channels, so this ordering is real. |
| `validate_boat_net` links only `proteus_boat_net`, and `boat_server` links only the ENet library. | Keep `validate_boat_net` free of plugins and have it build definitions in code. A new `validate_boat_room_package` links plugins, catalog and net for the edited-package proof. Add plugins and catalog to `boat_server`. `boat_server.c` also links into `wake_velocity` for `--serve`. |
| The hosted build runs `cmake --build --target boat_server boat_bot`. | It will now build and run the catalog generator, so check that exact target list from a clean configure, with `PROTEUS_WEBRTC=ON` where available. No new configure flag may be needed. |
| Protocol types run 1–21 and `MULTIPLAYER_PROTOCOL.md`'s header table still says version 6, while the code says 8. | Add `DEFINITION_BEGIN`, `_PAGE` and `_END` with the world page layout and separate assembly state. Correct the table to 9. |
| The deathmatch playtest changes are [still only planned](DEATHMATCH_PLAYTEST_PLAN.md). | `plugins/deathmatch` equals today's native defaults, and the catalog equality test keeps it so. |

Overrides such as `--round-seconds` apply before the definition is built, so clients always receive the
effective values. Discovery rows do not change: compatibility follows version and digest, and the package
name is shown only after joining.

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
   digest here detects corruption, not authorship. Send it once per connection; a resync reuses it.
   Decode must reject truncation, extra bytes,
   non-finite values, out-of-range counts, reordered pages and digest mismatch, and treat the sender
   as untrusted. Remove `BoatNetRaceCourse` and the race state's `course` byte;
   `boat_net_race_bind` takes the course. Update fixtures, goldens, the protocol document and the
   version rules. v8 clients are rejected explicitly.
5. **Install on the client.** `BoatNetClient` owns the validated definition for the connection and
   holds READY until it is installed; a failure closes with a clear reason and never enters the
   world. Bind it in `boat_net_race_bind`, prediction, `tools/wake_velocity_net.c`, the course view
   and `boat_bot`. Display the package name in the existing room UI. Resync and late join reuse the
   installed definition; reconnect receives it again. Offer the console's read-only `/status` in
   rooms, as PC originally specified, showing the package and its digest. It is the last and smallest
   item here: leave it out if it complicates the console, and say so.
6. **Check discovery and hosting.** LAN and directory compatibility must keep working with the new
   digest meaning. Record what `../davve-srv` or the release needs for v9 in
   [RELEASES.md](RELEASES.md) and [MULTIPLAYER_HOSTING.md](MULTIPLAYER_HOSTING.md), but make no
   deployment. Pages only publishes on tags, so merging changes nothing live.

### Acceptance

- **The key proof:** an in-memory race room with a *non-default* course (different gates, grid and
  ferry timing) and non-default rules, created from an edited package. Clients, including a
  mid-race joiner and a predicting client, finish with authority and client hashes identical and
  prediction replay through GO bit for bit. No client code path may read the compiled course: add a
  CTest source check that `boat_race_river_course` is not referenced by `boat_net_client.c`,
  `boat_net_protocol.c`, `boat_prediction.c`, `boat_view.c`, `wake_velocity_net.c` or `boat_bot.c`,
  with the solo race trial passing its course to the view explicitly.
- The same with non-default deathmatch rules, including combat values that affect prediction.
- Every existing `boat_net_*` loopback, four-player and impairment test passes on v9 with default
  packages. Bots finish the default race in the same time range as now.
- Malformed, oversized, truncated, stale and mismatched definitions all fail safely under virtual
  time, with no world installed. So does a definition whose combat rules differ from the checkpoint's.
- A maximum definition and checkpoint delivered entirely before Accept still bootstrap.
- `boat_server --mode deathmatch --location river`, with no new flag, behaves as it does on v8 and
  reports `deathmatch` as its package.
- Bytes on the wire are measured for each bundled package and for the maximum definition.
- Bootstrap time and bandwidth are compared with v8 for a four-client room.
- Local `check_web_multiplayer.py` passes free, deathmatch and race against a native server, using a
  browser client with no Lua evaluation. Include one edited course. Run the manual browser workflow
  once on the final revision.
- `boat_server`'s package and the Windows and Linux server builds link and run in CI. The hosted
  target list builds from a clean configure.

**Needs the user:** one two-machine race and one deathmatch on v9, one with a browser client, and
a hosted cutover at the next release.


### PD2 result, 2026-10-31

**Implemented.** `src/boat_room_definition.[ch]` is the core's pointer-free room definition
(package ID/name, mode, location, course and race rules, match and combat rules, optional scene)
with a structural validator, a shared `boat_race_course_validate` used by plugin staging and
rooms, and a canonical fieldwise codec capped at 16 KiB. The new `match` kind and
`match.define` grant decode `match.rules` and `match.combat`; `plugins/deathmatch` equals
`boat_match_default_rules` and `boat_combat_default_rules` at every location, and
`plugins/outer-free` completes the three free locations. `tools/boat_room_package.c` is the
authority loader: a catalog ID decodes without Lua, a directory is evaluated once with the map
checks, a package with a scene fixes its location, and a conflicting `--location` refuses to
start. `boat_server` and `wake_velocity --serve` use it, default by mode/location, apply rule
overrides after loading, and report the package ID. `boat_combat_enable` now takes the rules.
`boat_net_server` owns the encoded definition and sends it once per connection on the reliable
bootstrap channel (DefinitionBegin/Page/End, types 22–24) before the checkpoint; a resync reuses
it. `boat_net_client` validates and installs it before READY, never enters the world without it,
and compares a deathmatch checkpoint's combat rules with the definition. Protocol v9 removes the
race `course` byte and `BoatNetRaceCourse`; `boat_net_race_bind` takes the definition's course;
Hello's digest no longer covers the course or combat rules. The client status line shows the
package name.

**Deviations.** Recovery of a match package whose manifest location is `any` is evaluated by
the authoring worker against the basin map; a room always supplies the real location. The
`free` and `race` kinds now accept an optional scene (PD3 needs it), so PST moves to 7. The
in-game HOST panel's package row and the read-only room console `/status` were left out: the
host already receives the mode's default package with no new flag, and PC's console is gated to
solo. They remain UI work, not protocol work. A full-fleet prediction hash is not asserted
against the authority (remote boats are speculative); the room tests require exact mirror
hashes and bounded own-boat prediction error instead.

**Verification actually run, Apple Silicon/macOS, 2026-10-31.** Debug build from the pinned
dependency sources, no project warnings. `ctest --preset quick` passed; `ctest --preset full`
passed all **336 enabled tests** (13 retired field tests disabled, GPU CTests off), including
`boat_net` and `boat_net_enet` on v9, `boat_net_process`, `boat_room_course_source`,
`boat_plugin_author`, `boat_plugin_capture`, `plugin_default_parity`, the catalog equality cases
and `boat_core_boundary`/`boat_net_boundary`. A source check proves no networked client file
references `boat_race_river_course`. `boat_server --mode deathmatch --location river` with no new
flag starts on protocol 9 and reports `room package: proteus.deathmatch (Deathmatch)`; free,
race, outer and `--package proteus.harbor-room` start too. CPU captures of the basin, river and
the Harbor life outer scene were inspected. `git diff --check` passes.

**Measurements.** Encoded room definitions: basin-free 42 B, river-free 42 B, outer-free 48 B,
river-sprint 352 B, deathmatch 159 B, harbor-room 877 B, harbor-race 416 B. The constructed
maximum scene definition is 2,765 B (3 pages), well inside the 16 KiB cap. A lossless two-client
room sends 29 B of definition per client (the validator's free room) plus the unchanged 5,352 B
checkpoint. No runtime Lua or map sweep runs for a catalog room.

**Needs the user.** Two-machine race and deathmatch on v9, one with a browser client, and the
hosted cutover at the next release. Judge whether the package name in the room status line is
enough until the HOST row and room console arrive.

## PD3 — authored scenes and revisions in rooms

**Outcome.** Rooms can run authored scenes with berth and traffic contacts in agreement on every
machine, and a room can move to a new package revision between rounds. Physical harbor data leaves
the handshake digest.

Read first: `src/boat_world.[ch]`, `src/boat_fleet.[ch]`, `src/boat_prediction.[ch]`,
`src/boat_scene_definition.[ch]`, `src/waterfront_harbor.[ch]`, `src/waterfront_traffic.[ch]`,
the [scene contract](PLUGIN_API.md#location-life-scene-contract-planned-l11) and
[multiplayer prediction contract](PLUGIN_PLAN.md#multiplayer-prediction-and-recording).

`BoatWorld.scene` is the seam: physics and presentation read that pointer, and rooms leave it NULL.
This is the riskiest step in the plan because prediction copies fleets and replays them.

Checked on `4e11c2a`, which narrows the risk:

- **Where the pointer must be bound.** Hull and projectile contacts (`src/boat.c:123`, `:183`,
  `src/boat_combat.c:279`) read `world->scene`. `boat_fleet_world` and `boat_prediction_reset` copy the
  fleet's world by value, so they carry the pointer. The sites to change are `boat_net_server_init`, and
  `install` in `src/boat_net_client.c`, which builds a new fleet with `boat_fleet_init` at each bootstrap.
  Confirm this with a test, not by inspection.
- **Clock.** Physics poses use `boat_fleet_time`, which is `tick` seconds and so already shared. Check
  only the presentation path.
- **Moving craft create no wake state.** Rooms never set `water.traffic_objects`, and authored routes
  are visual-only by rule (`src/waterfront_traffic.c:151`). Keep both, so checkpoints need no traffic wake
  pool. Physical traffic wakes in rooms are out of scope.
- **Size.** In memory on Apple Silicon: scene 7,944 bytes, course 312, race rules 72, match rules 64 and
  combat rules 92, about 8.5 KiB. The fieldwise encoding should fit the 16 KiB cap in about nine pages.
  The acceptance still measures it.

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
   stale prediction and messages cannot cross a commit. This is the one new synchronisation mechanism
   in the package and the only part of it that nothing else depends on. Do it last, in its own commits.
   If it is not stable, open the PR without it and with no unused message types on the wire; it would
   then arrive later as protocol v10.

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


### PD3 result, 2026-10-31

**Implemented.** The room definition carries the full `BoatSceneDefinition` through the same
codec; a maximum constructed scene measures 2,765 B. The authority binds its scene in
`boat_fleet_init_scene`; prediction copies the fleet by value and `boat_fleet_world` copies the
borrowed pointer, so every mirror and replay evaluates the same contacts. The client binds the
definition's scene when it installs a world. Ferry and storm poses use the shared tick clock;
room traffic remains visual-only as before. Authority validation checks the scene against the
room: berths must be on open water and the scene's contact envelopes must leave the fleet spawn,
every gate line and every recovery candidate clear. The engine digest no longer includes the
authored harbour berths or ferry; only the contact recipe revision remains, and a source guard
proves those values are not read. A room definition with no scene still falls back to the
compiled harbour contacts, which keeps current default rooms working; an authored scene is
self-contained and independent of compiled berth values. Builds that change the compiled
contacts without a scene therefore rely on being the same release, as before. `plugins/harbor-room` is a free `outer` room sharing Harbor
life's scene, and `plugins/harbor-race` is a river race with an authored obstacle berth clear of
the course; both join the catalog. `validate_boat_room_package` evaluates edited race and
deathmatch packages, runs rooms with predicting clients and a mid-race joiner, and compares exact
mirror hashes.

**Deviations and limits.** Round-boundary revisions (task 6) are **not implemented**. They are
the package's only new synchronisation mechanism; the plan allows opening the PR without them
and delivering protocol v10 later. No message types were added for them, so the wire is clean.
The networked scene is proven through the room-package validator and the shared scene data,
not a windowed client capture: `--connect` refuses `--screenshot`, so no networked authored-scene
frame could be captured in this environment. Physical traffic wakes in rooms remain out of scope
as planned. The client's close-on-malformed-definition branch is covered by the codec rejection
tests and review, not by injecting a corrupt server.

**Verification actually run, Apple Silicon/macOS, 2026-10-31.** The same Debug build and full
run as PD2: **336 enabled tests** pass, including `boat_room_package.catalog_rooms`,
`definition_size`, `race_room`, `match_room`, `boat_net.room_definition_codec`, the four
bundled room packages and the harbor scene's catalog equality. `race_room` compares 9,058/9,058
authority hashes at snapshot ticks with predicting clients and a mid-race joiner; `match_room`
compares 1,705/1,705 with non-default health (150), damage and cannon timing. `catalog_rooms`
loads and measures every bundled free/match/race package, including the authored-contact
clearance checks. CPU capture of the Harbor life outer scene was inspected; `--renderer cpu`
basin and river captures were inspected for regressions. `git diff --check` passes.

**Review after the PR opened, 2026-10-31, Apple Silicon/macOS.** Windows failed
`boat_net.prediction_network` on a Profile F wake bound. Running that case over 120 seeds on `main` and
on this branch gave the same spread: both exceed the old wake or remote bounds in about 12% of rows, so
it is not a regression. The old bounds fitted one random run, and any new message or float difference
reshuffles its draws. They now sit about twice above the worst seed. That widening also means Profile F
does not hold `visible_appeared == 0`: on `main` and here a visible wake pops in on about one row in
eight. The review also fixed the plain race trial losing its race lines, added the missing grid-slot
clearance check, made a rejected definition disconnect at once and refuse a second definition once
installed (`boat_net.definition_rejected`).

`boat_room_package.scene_contact_room` now places moored berths on a bot's real track. Prediction holds
the scene through contact (3,225 ticks near a berth, removing the scene from prediction raises own error
to 1.02 m) and hashes agree in 1,088 of 1,088 checks on a steady link and 2,577 of 2,654 under Profile F
with a late joiner and an outage. **Contact prediction is not bit for bit:** own error reaches 0.126 mm
against exactly zero with no contact. The cause was not investigated. Forced contact with a moving
craft and contact through a resync (this run had none) remain unproven.

`check_web_multiplayer.py` was stale, so its keys never reached the URL row. It now types the local room
for both joins, and fails if a log names the hosted room. **Before that fix, one run of the old rejoin
path from a `main` build joined the hosted Winterfell room briefly as slot 1.** On this branch, a browser
client with a WebRTC native server joins and rejoins a race (`web_multiplayer=ok`). Free and deathmatch
join and receive their definitions but stop at a later step of the script, the same step as on `main`,
which includes deathmatch's `Browser network receive queue is full`. Not run: `--four-players`, an
edited course in a browser, the manual browser workflow, and the v8 bootstrap and four-client bandwidth
and tick-cost comparisons that PD2 and PD3 list.

**Needs the user.** A two-person harbor room with deliberate berth and traffic contact; judge
whether any correction is visible. Decide whether revisions should be exposed beyond the
`--package-reload` idea now that they are deferred to v10.

### Rooms follow-ups

[Cutover](#pd5--cutover) closes the ones needed for a v9 release. State as of its preparation, 2026-10-31:

| Item | State |
| --- | --- |
| Forced contact with a moving authored craft, and contact through a resync | Unproven. `scene_contact_room` covers moored berths and had no resync. PD5 task 3 |
| Contact prediction is 0.126 mm from the authority, not bit for bit | Cause not investigated. A wake reaching the berth's height query is only a guess. PD5 task 3 |
| Four-client bandwidth and tick cost with the maximum scene; v8 bootstrap comparison | Not measured; PD5 task 3 |
| `--four-players` and an edited course in a browser client | `--four-players` passes on Metal with corrected keys in a scratch copy; PD5 task 1 lands the fix. Edited course not run; PD5 task 3 |
| Round-boundary revisions | Deferred to protocol v10; the user decides whether they are worth building |
| HOST panel package row and read-only room `/status` | Left out. The host uses the default package. |

## PD4 — browser parity

**Outcome.** Browser solo play starts from the same compiled catalog as native, including Harbor life,
and desktop browsers get the console. Imported voyages may carry scenes. Normal play starts no worker, and the game's Wasm has no Lua VM.

Read first: [Browser builds](WEB_BUILD.md), including its plugin sections,
[browser source bundles](PLUGIN_API.md#browser-source-bundles-p43), `web/plugin_author.js`,
`web/plugin_worker.js`, `src/boat_plugin_stage.c`, `tools/wake_velocity_plugin.c`, `cmake/Web.cmake`,
`tools/package_web.py`, `tools/check_web_plugin_author.py`.

### Settled from the source and a trial, 2026-10-31

Each of these was checked on `a949d7a`, the reviewed Rooms branch, in headless Chrome on Apple
Silicon/macOS. The tasks below assume them.

| Finding | What to do |
| --- | --- |
| **Browser import is broken on `main`.** `web/plugin_author.js` hard-codes the wire magic as `0x50535435` (PST5). #96 moved the C side to PST6 and #100 to PST7, so every valid result reports "Worker failed or reached its five-second deadline". v0.0.23 predates this, so no release has it. | Task 1. With the magic alone corrected, staging succeeds and the check reaches its next failure, a stale expected message at `check_web_plugin_author.py:125`. Later cases have not been reached. |
| **The generator runs under Node.** Built with `-sENVIRONMENT=node -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB`, it evaluates all ten packages in 1.36 s, against 0.83 s natively, and its 111,266-byte output is **byte-identical** to the native generator's. | Use Node. The host-built fallback is no longer needed. Only the build step was tried, not linking the result into the game. |
| The kind rejection is in three places: `boat_plugin_stage.c`, `plugin_author.js` and `plugin_worker.js`, which #96 added. | Change all three, and keep race rejected with the message pointing to rooms. |
| The worker and `plugin_author.js` both cap results at 4,096 bytes, and a revision carrying a scene may exceed that. No scene revision was measured against it. | Measure Harbor life's result, then raise both caps together from one stated limit. |
| The worker has descriptor and objective modes only. | Console queries need a new mode. |
| Rooms send their definition as data, so the browser never hosts or loads a room package. | Link only the solo entries into the game: basin and river free, time trial, Långedrag and Harbor life. |

Baseline for the size comparison, from `a949d7a` in MinSizeRel:

| Output | Raw | Brotli | Gzip | Lua message strings |
| --- | ---: | ---: | ---: | --- |
| `wake_velocity` Wasm | 1,986,419 | 774,623 | 970,154 | None |
| `plugin_stage` worker Wasm | 390,072 | 151,856 | 184,113 | Present, as expected |

Check baselines, on the same build with #100's script fixes. Fix what is stale as part of this package,
and do not count a stale script as a pass:

| Check | Result on `a949d7a` | Same on `main` |
| --- | --- | --- |
| `check_web_title.py` | Passes | Not run |
| `check_web_mobile.py` | Passes | Not run |
| `check_web_plugin_author.py` | Fails on the wire magic, above | Yes |
| `check_web_multiplayer.py --mode race` | Passes, including rejoin | `main`'s pass was false: its rejoin reached the hosted room |
| `check_web_multiplayer.py --mode free` | Joins, then times out on `spawnRequests === 1` | Yes |
| `check_web_multiplayer.py --mode deathmatch` | Joins, then `Browser network receive queue is full` and no weapon counters | Yes |

The deathmatch error matters most, because the hosted room runs deathmatch. Find out whether it comes from
the headless script or would also hit a player, and record the answer even if it needs no fix.

### Tasks

1. **Restore browser import first.** Correct the magic, and add a CTest that fails if the magic in
   `web/plugin_author.js` differs from `BOAT_PLUGIN_WIRE_VERSION`, so the next wire bump cannot repeat
   this silently. Report an unrecognised result as what it is, not as a deadline. Update the check's stale
   expectations, then get it passing before starting the rest.
2. **Generate the catalog for Wasm.** `proteus_plugin_catalog` is native-only today (`CMakeLists.txt`). Build `boat_plugin_catalog_gen` for Emscripten as a Node target
   (flags above), run it through `CMAKE_CROSSCOMPILING_EMULATOR` during the web
   build, and link the output into the game. Do not add Lua to the game as a fallback. Add a check that the game Wasm lacks Lua's VM and parser
   by its message strings and symbols. `wake_velocity` already links `proteus_plugins` on the web, which
   puts `proteus_lua` on the link line and relies on the linker discarding it, so the check must test the
   output and not the link line. Record game
   Wasm size before and after, raw and compressed.
3. **Default browser solo.** Remove the compiled path from browser solo sessions, using runtime mode
   from PD1. Add the title's content row on web. Keep save settings compatible. Time trial bests in
   IndexedDB must remain valid, using the same digest rule as PD1.
4. **Render scenes in the browser.** Verify authored scene paths through the web GPU renderer
   (berths, modules, traffic, gulls) against native at pinned cameras and times. Fix actual
   differences. Check lazy scenery loading and repeated sessions.
5. **Allow imported scenes.** Remove the rejections listed above, and return the existing scene wire from the worker, with the traffic
   sweep off the game thread. Add worker deadlines suited to a slow device. Imported `free` and
   `course` kinds behave the same way. Imported race packages stay rejected with a message pointing
   to rooms, because the browser has no solo race trial.
6. **Bring the console to desktop browsers.** Enable it where a physical keyboard exists, with edits and
   queries going to the worker and `/save` using the export path. Add the worker's console queries and
   live-apply checks to the qualification below. Do not add it to mobile.
7. **Update qualification.** Extend `check_web_title.py`, `check_web_plugin_author.py` and the Node
   contracts for default sessions, Harbor life, scene imports, tab freeze and repeated sessions. Do
   not enable the full matrix on PRs.

### Acceptance

- Browser default-session CPU/GPU comparison shows no unexplained difference from before.
- No worker is created in a default session, and the Wasm check proves no Lua is on the game thread.
- Local three-engine `check_web_plugin_author.py` and `check_web_title.py` pass, with captures
  inspected and costs recorded: game start, memory, Harbor life frame time. Run the manual browser
  workflow once on the final revision.
- `check_web_multiplayer.py` passes in free, race and deathmatch, and `check_web_mobile.py` still
  passes. None of them contacts the hosted room.
- Harbor life's revision result is measured against the raised cap.
- The deathmatch receive-queue question has a recorded answer.


### PD4 result, 2026-10-31

**Implemented.** Browser import works again: `web/plugin_author.js` uses the current PST7 magic,
`web_plugin_wire` compares it with `BOAT_PLUGIN_WIRE_VERSION` in CTest, and an unrecognised reply
is reported as a wire mismatch rather than as a worker deadline. The catalog generator builds for
Emscripten as a Node program (`-sENVIRONMENT=node -sNODERAWFS=1`) and CMake runs it through the
cross-compiling emulator; the generated table is linked into the game Wasm with only the five solo
entries (basin and river free, time trial, Långedrag and Harbor life). `tools/check_web_wasm.py`
runs from `package_web.py` and proves the game Wasm lacks Lua parser/VM messages, using
`plugin_stage.wasm` as the positive control.

Browser solo now opens the same build-time catalog as native through `wake_plugin_open_catalog`;
normal play creates no worker. The title's CONTENT row is available on web, so Outer offers
Standard or Harbor life. `--package` works in browser solo, and the time-trial course keeps the
compiled digest in the best-run name. An imported course now sets the session's course mode, so
`--plugin` free and course packages behave as solo content.

Imported scenes are accepted through the worker: the Emscripten kind/scene rejections are gone,
`race` and `match` stay rejected with messages pointing to rooms, and the stage response cap rose
from 4 KiB to 16 KiB to carry Harbor life's 5,505-byte scene revision. The browser worker deadline
is 15 seconds. Desktop browsers with a physical keyboard get the live console; edits and `= queries`
use the worker, `/save DIR` downloads a source bundle, and the canvas keydown handler now lets
printable characters (including space) reach `GetCharPressed` while the console is open.

**Verification actually run, Apple Silicon/macOS, 2026-10-31.** Native Debug build is
warning-clean; `ctest --preset quick` passed all 241 enabled tests, and `ctest --preset full`
passed all 339 enabled tests with local sockets (13 retired field tests disabled, GPU CTests off).
The Emscripten MinSizeRel `wake_velocity` and `plugin_stage` targets build; `package_web.py` passes
the Wasm Lua check. Local Playwright Chromium against the served site passes
`check_web_title.py`, `check_web_mobile.py`, and the full `check_web_plugin_author.py`
(20 validation cycles, freeze/resume, three restored sessions, objective transactions, scene import,
free/course launch and storage denial). Captures inspected: Harbor life from the title, the console
open/query/export/live-edit flow, and free/course imports. `check_web_multiplayer.py --mode race`
passes against a local WebRTC native server.

**Measurements.** Game Wasm raw/Brotli/gzip: 1,986,419 / 774,623 / 970,154 bytes before this
package, 2,001,499 / 778,037 / 975,309 after (+15,080 raw, +3,414 Brotli, +5,155 gzip). Worker Wasm
390,072 → 395,778 bytes raw. The Node generator evaluates the five web entries in 1.34 s and writes
48,616 bytes of C; the build runs it once per catalog change. Headless Chromium/SwiftShader on this
machine measured 47.7 ms from Play click to session return, a 15.1 MB used JS heap, and 253 ms median
frame interval (261 ms max) for Harbor life; the software renderer makes the frame figure
non-representative.

**Deviations and limits.** Only Chromium was run locally; Firefox, WebKit and the manual three-engine
`web-plugin.yml` workflow were not dispatched, and no iPhone or physical Safari check was made.
Harbor life's browser capture is at the voyage start rather than a pinned marina camera: the
authored traffic craft and outer scenery render, and the marina scene is carried by the same 5,505
byte wire and covered by native captures, but a browser marina capture is still owed.
`check_web_multiplayer.py` still fails free and deathmatch on this branch exactly as recorded on the
pre-package baseline: free joins and plays but the headless client sends no spawn request on
Backspace, and deathmatch reports `Browser network receive queue is full` with no combat counters.
Race passes. The room client was not changed by this package, and the cause of the two failures was
not established. `--no-plugins` and the compiled fallback remain until PD5.

**Needs the user.** Harbor life and normal play on an iPhone and in physical Safari, which decides
whether Harbor life is offered on mobile; a browser marina capture or play check at the dock; and a
decision on whether the free/deathmatch browser room failures block the Cutover package.

Later finding, 2026-10-31: those failures come from SwiftShader's frame rate and one script key press, and they do
not block Cutover. The [PD5 preparation](#settled-from-the-source-and-a-trial-2026-10-31-1) has the evidence.

## PD5 — cutover

**Outcome.** There is one path for play. The compiled selection, the switch and the plugin compile-time flag
are gone, the room gaps a v9 release needs are closed, and every doc says so. The tree is ready for the user
to cut the release.

### Settled from the source and a trial, 2026-10-31

Read and run on `381e802`, the merge of the Browser package and distant shadows. Browser trials used the
`7289250` site and WebRTC server; `git diff` shows no change since then in the room client, server or `web/`
network code.

**The browser room failures are not room bugs, so they do not block this package.** Both came from running
on SwiftShader, which here shows 3-second gaps between frames (about 5 fps) in a room. The same flow on a real
GPU (headless Chrome, `--use-angle=metal`, steady 60 fps):

| Check, on Metal | Result |
| --- | --- |
| `--mode free` | Passes, including two resets and the rejoin |
| `--mode race` | Passes |
| `--mode deathmatch` | Passes once the script's `press('q')` gets a delay (gun 3,108 inputs, 1 grenade, 1 torpedo, 189 combat pages, 2 resets, rejoin). With no delay the press and release land in one frame, so the game never sees it |
| `--four-players`, free | Passes after correcting the extra client's join keys, with `wake.davve.net` unresolvable in the browser |

A healthy deathmatch client receives about 72 messages a second. The 256-entry receive queue therefore fills
only after a stall of about 3.5 s, or during catch-up after a long stall. The results on Metal:

| Main-thread stall in deathmatch | Result |
| --- | --- |
| 1.5 s, 3 s | Survives |
| 5 s | The server times the client out, by design (`BOAT_NET_TIMEOUT`) |
| 8 s | `Browser network receive queue is full` |

One defect remains: `web/rtc_transport.js` ends the session when a reliable message meets a queue full of
*disposable* snapshots. A slow device's stall during join can hit this, as SwiftShader does. Task 2 fixes it.

**Do not run `--four-players` as it stands.** The extra client presses `ArrowDown, Enter, ArrowUp, Enter`, which
no longer matches the menu, and the check for the hosted room reads only the first page's logs. It has the same
kind of stale key sequence that joined production on 2026-10-31.

**Saved bests depend on the compiled course.** `boat_replay_best_path` (`src/boat_replay.c:363`) keeps the plain
`course-calm-physicsN.run` name only when the digest equals `boat_course_compiled_digest()`, and
`tools/wake_velocity.c:953` uses the same value to gate `--replay` and `--ghost`. Deleting the compiled course
without a pinned value would orphan every player's best, which is a stop rule. The value is
`0xa27ccadf8bd7a754`. It was computed outside the code from the digest's definition, so the first test that uses
it must confirm it.

**Byte-pinned captures do not belong in CTest.** The original task replaced `plugin_default_parity` with pinned
capture checks. That test only ever compared two paths on one machine, and every CPU rendering change alters the
PNG bytes. #103 did so just now, and the graphics overhaul will keep doing so. Pins would also need separate
values for Windows and Linux. So the capture comparison is a one-off proof in this PR, and the durable gate is
definition equality against the fixtures. See task 4.

Other findings:

- **Definitions:** Hello's location digest loop (`src/boat_net_protocol.c:1122`) reads named fields and not
  `voyage`, so removing that field should leave the content digest unchanged and needs no protocol bump. Prove it.
- **Equality cases:** they live in `tools/validate_boat_plugin_catalog.c:129-147`, not in the generator as this
  plan first said. The generator links only `proteus_plugins`, so it stays clean.
- **Production uses to replace:**
  - `boat_session.c:21` and `:38`
  - `boat_course.c:37` and `:55`
  - `boat_course_view.c:20` and `:77`
  - `boat_combat.c:74`
  - `boat_location.c:40`
  - `boat_replay.c:363`
  - `tools/wake_velocity.c:953`
  - `tools/wake_velocity_plugin.c:412`, `:495` and `:747-751`, which compare with or restore `definition->voyage`
    and a NULL course
  - `tools/boat_server.c:218` and `:700`, the `--scripted` path
- **Dev tools to move:** `boat_hud_review`, `boat_race_trial`, `boat_combat_review` and `boat_fleet_review`.
- **Validators:** scope is small. Across `tools/`, `boat_voyage_outer` has 16 uses, `boat_race_river_course` 14,
  `boat_race_default_rules` 14, `boat_match_default_rules` 10 and the rest under 5. Include
  `validate_boat_minimap`'s `location->voyage`.
- **Keep:** a room with no scene falls back to the compiled harbour contacts. This is the registered default the
  decisions table allows, not compiled selection. Physics paths also stay: `RIVER_SPAWNS` and the recipes.
- **Moving contact seams:** moving craft do collide, through `waterfront_traffic_contact_height_scene` at
  `src/boat.c:183`. `tools/validate_boat_room_package.c:458` has the room harness. `room.server.tick_limit` in
  `snapshot_resync` (`tools/validate_boat_net.c:1018`) forces a resync.
- **First hosted build with the Node generator:** v0.0.23 predates it. The role configures with `emsdk_env.sh`
  sourced, then builds `--target web` with only `EMSDK` and `EM_CONFIG` added. The Node emulator is cached as an
  absolute path, so this should work, but it has never run that way. Task 9 reproduces it.
- **`cut_release.py --check`:** it refuses any commit other than `origin/main`, so it cannot pass from the PR
  branch. It also needs a `davve-srv` checkout at its baseline, and the local one is 7 commits behind.

### Tasks

Use at least one commit per task, and more for task 6. Tasks 1–3 stay independently correct even if a later task
hits a stop rule.

**Browser rooms and the last room proofs**

1. **Repair `check_web_multiplayer.py`.**
   - Give the torpedo press a delay.
   - Use one join helper for every client, with the extra client's keys read against the current menu.
   - Add `--host-resolver-rules=MAP wake.davve.net ~NOTFOUND` to the browser launch, and check every page's logs
     for the hosted room. Add the same guard to any other script that presses keys through the join panel. Leave
     out `check_web_server_directory.py`, which the release runs against the hosted site on purpose.
   - Add `--angle`, defaulting to `metal` on macOS and `swiftshader` elsewhere.
   - Measure the frame rate and fail with a plain "too slow to qualify a room" message below 20 fps, so software
     rendering can never look like a room failure again.
2. **Survive a stall during join.** When the queue is full and a reliable message arrives, drop the oldest
   disposable event instead of failing, and fail only when every queued event is reliable. Keep reliable order.
   Update `tools/test_web_rtc_transport.cjs` for both cases. After this, SwiftShader deathmatch must no longer
   report a full queue, though the frame-rate guard will still refuse to qualify it. The 5-second timeout for a
   silent client stays. Document in [WEB_BUILD.md](WEB_BUILD.md) that a hidden browser tab leaves the room, and
   do not change that here.
3. **Close the [room follow-ups](#rooms-follow-ups) a release needs.**
   - Add `boat_room_package` cases that force contact with a moving authored craft and force contact through a
     resync. Compare hashes as `scene_contact_room` does. If moving contact needs a physics change, stop and report.
   - Find out where the 0.126 mm prediction error comes from: run the steady case with waves and wakes removed,
     and compare the first differing tick. Fix it if it is a bug in non-physics code. If it is a physics change or
     harmless float noise, tighten the test's bound to what you measure and record the reason. Do not spend more
     than this comparison on it.
   - Measure four-client bandwidth and server tick cost in `boat_server --package proteus.harbor-room`, the maximum shipped
     scene, against a plain river room. Measure bootstrap bytes against v8 using the existing v0.0.23 worktree at
     `/private/tmp/v23`, or a fresh worktree of the tag if that one is gone. Measurements are evidence, not asserts.
   - Run an edited race package in a browser client through `boat_server --package DIR`.

**Removing the compiled path**

4. **Pin before removing, on the last commit with both paths.** Record each of these for basin, river, outer,
   course and harbor-life, on this machine, in the result:
   - the SHA-256 of the CPU capture;
   - the route or course digest;
   - Hello's content digest;
   - the game's Wasm size.

   After task 6, recapture with the same commands and require identical bytes. Keep the capture script out of the
   repo; a CTest pin was ruled out above.
5. **Remove the switch.** Delete `--no-plugins`, `PROTEUS_BOAT_PLUGINS` (17 sites in `tools/wake_velocity.c`, one
   in `CMakeLists.txt`) and the paths it guards, including the unconditional-branch leftovers and the stale "rooms
   use compiled content" message. Delete `plugin_default_parity` and its script, and replace its session coverage
   as in task 6.
6. **Remove compiled selection,** in this order, with a building tree and a green quick lane at each commit:
   1. **Fixtures.** Add `proteus_boat_fixtures` (`tools/boat_fixtures.[ch]`) holding `boat_voyage_outer`, the course
      table and default, `RIVER`, and the race, match and combat default rules. Validators link it. Nothing else
      moves yet.
   2. **Course.** Add `BOAT_COURSE_STANDARD_DIGEST` to core, and use it in `boat_replay_best_path` and the
      `--replay`/`--ghost` gate. Add a test that the fixture's digest and `proteus.time-trial`'s digest both equal
      it, and keep the legacy-best case. Then remove `boat_course_gates`, the NULL fallbacks and
      `boat_course_compiled_digest`. An enabled course requires a definition.
   3. **Voyage.** Delete `BoatWorldDefinition.voyage` and the session fallback. The session's route starts NULL,
      `wake_plugin_open*`'s owner check tests for NULL, and close restores NULL.
   4. **Rules and race course.** `boat_combat_enable` requires rules from its caller. `boat_server --scripted` and
      the review tools decode the catalog's `proteus.deathmatch`. `boat_race_trial` and `boat_hud_review` use the
      catalog.
   5. **Guards.** Add a check that no production target (`wake_velocity`, `boat_server`, `boat_bot`,
      `boat_plugin_worker`, `boat_plugin_catalog_gen`, `plugin_stage`) links the fixtures, and that the removed
      symbols are absent from `src/` and those targets' sources. Add `boat_plugin_session.default_sessions`, which
      opens each default entry through the game's selection path and compares its route, course, rules and scene
      with the fixtures. The `.lua` comments naming `boat_voyage_outer` may change only if every pinned digest in
      task 4 stays the same.

**Documents and release**

7. **Update durable rules.** Rewrite the rules that assumed opt-in: the
   [plugin plan's milestone rule](PLUGIN_PLAN.md#milestones-and-acceptance), "existing rooms continue using their
   compiled content" (`PLUGIN_PLAN.md:443`, and the live claim at `:1320`), the room plan's starting point, and the
   layout note in [AGENTS.md](../AGENTS.md) that the server needs only compiled water. Sweep for `--no-plugins` and
   compiled-content claims in `README.md`, [PLAYING.md](PLAYING.md), [COMMAND_LINE.md](COMMAND_LINE.md),
   [TESTING.md](TESTING.md), [PLUGIN_API.md](PLUGIN_API.md), [WEB_BUILD.md](WEB_BUILD.md),
   [RELEASES.md](RELEASES.md), the hosting guide and `plugins/*/README.md`. Dated results stay as history.
8. **Harbor life stays a title choice** unless the user's acceptance is recorded here before you start. It has not
   been as of this writing, so do not change the default `outer` package, and say so in the PR.
9. **Prepare the release.**
   - Update [RELEASES.md's release notes](RELEASES.md#v0024-wire-and-content-changes) for this package.
   - Reproduce the hosted browser build locally in a scratch directory: the role's `emcmake` command with
     `emsdk_env.sh` sourced, then `cmake --build ... --target web` under `env -i` with only `PATH`, `HOME`, `EMSDK`
     and `EM_CONFIG`. Read `../davve-srv/ansible/roles/wake_velocity/tasks/main.yml` for the exact commands, and do
     not change that repository.
   - Run `tools/cut_release.py vX.Y.Z --check` from a clean detached `origin/main` worktree with a `davve-srv` at its
     baseline, and report what it says. Expect it to validate only prerequisites, since it refuses the PR branch by
     design.

### Acceptance

- The common list in [What every step must leave behind](#what-every-step-must-leave-behind) passes.
- On Metal, `check_web_multiplayer.py` passes all of `free`, `race`, `deathmatch` and `--four-players`. Every run
  stays local, and the hosted-room guard is in place.
- After removal, the task 4 captures, digests and legacy-best behaviour are identical. A best saved by the current
  build still loads from the same file name, and the compiled-digest course still replays and ghosts.
- `git grep` finds no production use of the removed symbols, and `nm` finds none in `wake_velocity` or `boat_server`.
  The new link guard fails when deliberately broken, once, locally.
- The game Wasm still has no Lua (`check_web_wasm.py`), and its size is reported against task 4.
- Dispatch the manual `web-plugin.yml` workflow once on the final revision, on Chromium, Firefox and WebKit.
- Startup and frame costs for default sessions match [PD1's measurements](#pd1-result-2026-10-31): 0.79 s for a
  three-second Release CPU drive of `outer`, four runs after one warm-up.
- The hosted-build reproduction succeeds, and the `--check` outcome is reported.

**Needs the user:** merge, cut the release, verify the hosted room and make the Harbor life decision. Still open
from earlier packages: a two-person harbor room with deliberate contact, Harbor life and normal play on an iPhone
and physical Safari, and a browser marina play check. None of these blocks the PR.

### PD5 result, 2026-10-31

**Implemented.** `tools/check_web_multiplayer.py` now uses one local-room join helper for every
client, adds a 100 ms torpedo press delay, maps `wake.davve.net` to `NOTFOUND`, rejects any page
log naming the hosted room, chooses Metal on macOS and SwiftShader elsewhere, and refuses to
qualify a room below 20 fps. `web/rtc_transport.js` drops the oldest disposable event when a
reliable message meets a full queue and fails only when every queued event is reliable; the Node
contract covers both cases. `WEB_BUILD.md` records that a hidden tab leaves the room.
`boat_room_package.scene_moving_contact_room` and `scene_resync_contact_room` force contact with an
authored moving craft and through a snapshot-stall resync; the steady contact bound is tightened
to 0.00025 m.

`--no-plugins`, `PROTEUS_BOAT_PLUGINS` and `plugin_default_parity` are gone. `proteus_boat_fixtures`
(`tools/boat_fixtures.[ch]`) holds the shipped voyage, course, race course and rule values for
validators only. `BOAT_COURSE_STANDARD_DIGEST` (`0xa27ccadf8bd7a754`) keeps legacy best paths and
the `--replay`/`--ghost` gate; an enabled course requires a bound definition. `BoatWorldDefinition`
and sessions have no compiled voyage, and `boat_combat_enable` requires rules. `boat_server
--scripted` and `boat_race_trial` decode the catalog (`proteus.deathmatch` and
`proteus.river-sprint`). `boat_plugin_session.default_sessions` opens every default through the
catalog selection and compares route, course, rules and scene with the fixtures;
`compiled_selection_guard` rejects the removed symbols and the fixture library in production
sources, CMake targets and the built `wake_velocity`/`boat_server` symbols. The guard fails when a
production source includes `boat_fixtures.h`.

**Deviations.** The plan expected `cut_release.py --check` to stop at a behind hosting baseline;
the local clean `davve-srv` checkout was at `db996af`/`origin/main`, so the full preflight passed
and made no tag, output or hosting change. The hosted-build reproduction also pinned the Lua
source and used `FETCHCONTENT_FULLY_DISCONNECTED=ON`; the role passes only the raylib source and
normally fetches Lua. The moving-contact fixture uses small authored loops sampled from the bot's
resolved track so the test is deterministic rather than a particular ferry timetable. The 0.126 mm
prediction error remains; it is speculative contact over a snapshot interval, not a replay
difference (below). Harbor life stays a title choice because no user acceptance was recorded
before this package; `outer` still defaults to Standard.

**Verification actually run, Apple Silicon/macOS, 2026-10-31.** Debug build from the pinned
dependencies, no project warnings. `ctest --preset quick` passed all 243 enabled tests;
`ctest --preset full` passed all **342 enabled tests** with local sockets (13 retired field tests
disabled, GPU CTests off). That includes the two new room cases, `default_sessions`,
`compiled_selection_guard`, `boat_plugin_stage`, `boat_server_scripted(_deathmatch)`,
`boat_race_trial`, the catalog equality cases and the legacy-best case. `git diff --check` passes.
Linux, macOS and Windows CI passed on the final revision after two review fixes: the race fixture
comparison uses the gameplay digest instead of a padding-sensitive `memcmp`, and the relocated
authoring-tools check passes an explicit 20 s validation deadline on a loaded runner. The manual
`web-plugin.yml` workflow then passed Chromium, Firefox and WebKit ([run
41253441627](https://github.com/doomdavve/proteus/actions/runs/41253441627)) on the last code
revision.

Browser checks used the final native WebRTC `boat_server` and the final browser build, Chrome
headless with `--use-angle=metal`, against local rooms only. `check_web_multiplayer.py` passed
`free`, `race`, `deathmatch`, `--four-players` and an edited race directory (`--server-package`),
at 60.5–60.9 measured fps. The edited race joined and rejoined with two resets and no decode
errors. Every run mapped `wake.davve.net` to `NOTFOUND`; no page log named the hosted room.

**Task 4 pins, before and after removal, Debug CPU captures at `--time 3` (SHA-256).** Basin
`02c13bc38ac31b57200572209614b4e6a40c8f2c97c5f4b586a92f224e9116be`; river
`015b41c0c0a4bcbbb86f65feb92e0f62fd05e260d991ccd6791dce1215159bb8`; outer
`ad4a277fc752c2be1cfd4c446429db11827f06c189b0b8cae6bf0c7d156cebb5`; course
`ebb830e1558a7b0d71089dccbfe8e465fdc1d85e37571e22b5523e1100cc5430`; harbor-life
`f95630596bee40aa7a347174952348cd7ad00ad108194dac43cb054687b9018e`. Every capture was
byte-identical after removal. Route digest `e8509272172cb7c4`; standard course digest
`a27ccadf8bd7a754`; Hello content digest `7d466f9f85a9d4d4`; all unchanged. Game Wasm went from
2,003,169 to 2,002,389 raw bytes, 978,247 to 978,105 gzip and 779,912 to 778,288 Brotli.
`check_web_wasm.py` still reports no Lua in the game Wasm.

**Measurements.** The steady authored-contact room has no ambient waves and its live arcade wake
pool is empty. The first own-slot difference is 12 µm at tick 36, the first berth contact; the
maximum is 0.126 mm at a later snapshot tick, so the bound is now 0.00025 m. Four-client 12 s room
runs: plain river 879.7 kbit/s, `service_ms_per_tick` 0.1005, bootstrap 5,352 B/client;
`proteus.harbor-room` 884.2 kbit/s, 0.1530 ms, 5,352 B/client. The v0.0.23 v8 worktree's river room
measured 859.9 kbit/s, 0.0826 ms and the same 5,352 B/client bootstrap. Single wall-clock runs are
evidence, not budgets. Release CPU three-second outer drives after one warm-up: 0.794, 0.797,
0.797, 0.797 s (mean 0.796); bundled Harbor life 0.796, 0.795, 0.799, 0.797 s (mean 0.797), both
matching PD1's 0.79 s.

The hosted browser build was reproduced in a scratch directory with `emcmake`, MinSizeRel and
`env -i` carrying only `PATH`, `HOME`, `EMSDK` and `EM_CONFIG`; `--target web` passed with
`web_wasm_lua=absent`. `tools/cut_release.py v0.0.24 --check` passed from a clean detached
`origin/main` worktree (`b0f1233`) with the clean local `davve-srv` controller and reported
"Preflight passed. No tag, release or hosting changes made."

**Review fixes, 2026-11-01.** A review found that a free or course revision's union was read as a
voyage route, so renaming such a package (new with `eaf3123`) or applying a course edit (already
present before this package) crashed on the next tick. `commit` now passes a route only for voyage
hosts. A course edit also never reached the session: the session now follows the active revision's
course, and an applied edit selects that course's best file (`boat_replay_runs_select_best`) so it
cannot overwrite another course's best. New `wake_command` and `boat_replay` cases
pin this. `compiled_selection_guard` never matched executables on macOS because Mach-O symbols carry
a leading underscore; it now strips it, and `compiled_selection_guard_detects` is a negative control.
Stale compiled-content comments were corrected.

Verified after these fixes, Apple Silicon/macOS, 2026-11-01: `ctest --preset quick` passed 244 tests
and `ctest --preset full` passed all 343 enabled tests with local sockets (13 retired tests disabled,
GPU CTests off). Removing the route check makes `wake_command` segfault. The five CPU captures above
still match their pins. The restart's best-file selection in `wake_velocity.c` was not driven in a
window, and local browser rooms were not rerun. On `25619cc`, Linux, macOS and Windows CI passed, and
the manual `web-plugin.yml` workflow passed Chromium, Firefox and WebKit ([run
41288764544](https://github.com/doomdavve/proteus/actions/runs/41288764544)).

**Known limits.** GPU CTests were off. Only headless Chromium/Metal was run locally; Firefox and
WebKit were covered by the manual CI workflow. No physical Safari/iPhone or two-machine room was
tried. The four-client bandwidth and tick figures are one
local wall-clock run each, not a budget or a remote-network measurement. The `--four-players`
extra client still types the local room through the shared helper; it does not exercise the
hosted directory.

**Needs the user.** Merge, cut the release, verify the hosted room and decide whether Harbor life
should replace Standard for `outer`. Still open from earlier packages: a two-person harbor room
with deliberate berth and traffic contact, Harbor life and normal play on an iPhone/physical
Safari, and a browser marina play check.

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

Cutover preparation, 2026-10-31, Apple Silicon/macOS, documentation-only, on `381e802`: `ctest --preset full` passed all 339 enabled tests with
local sockets (13 retired tests disabled, GPU CTests off).
Browser trials ran with Chrome headless from scratch copies of the check script against a local `boat_server`, and
no client reached the hosted room. Firefox, WebKit, physical devices and `--use-angle=metal` under a headed window
were not tried. `tools/cut_release.py v0.0.24 --check` was run against the local `davve-srv`: it stopped at the hosting baseline check because
that checkout is behind `origin/main`, so the later prerequisites (vault file, SSH) were not reached.
The PD5 design remains an unimplemented proposal, and no user acceptance is implied.

Browser preparation, 2026-10-31, Apple Silicon/macOS, on `a949d7a`, documentation-only: `ctest --preset
full` passed all 338 enabled tests with local sockets (13 retired tests disabled, GPU CTests off). The
generator trial and the browser baselines ran in throwaway worktrees and are recorded under PD4. The Wasm
sizes come from `emcmake` MinSizeRel with the local emsdk. Firefox, WebKit and physical devices were not
tried. The PD4 design remains unimplemented, and no user acceptance is implied.

Rooms preparation, 2026-10-31, Apple Silicon/macOS, documentation-only, on merged PC (`4e11c2a`): Debug
build with no warning outside raylib's `stb_vorbis.c`, and `ctest --preset full` passed all 330 enabled
tests with local sockets (13 retired tests disabled, GPU CTests off). The PD2 and PD3 source references
and the findings recorded under those steps were read from this revision. The struct sizes were compiled
and printed once on this machine. `davve-srv` was read, not changed. The Rooms and Browser designs remain
unimplemented proposals, nothing was prototyped, and no user acceptance is implied.

Packaging revision, 2026-10-31, Apple Silicon/macOS, documentation-only, on merged PD1 (`692104a`):
Debug build passed and `ctest --preset full` passed all 329 enabled tests with local sockets (13 retired
tests disabled, GPU CTests off). All 239 local links and anchors in the two changed documents resolve. PD4's dependency on PD3 was a sequencing choice, not a technical one, so it was removed.

Reviewed 2026-10-31 against `e335b84`: the plugin, multiplayer, room content, deathmatch playtest,
living city, sunset, adventure and architecture documents and the source seams cited above, plus the
reload classification, session commit, tool bridge and key bindings for PC. That review was made
before PD1: the console design remains a proposal, and generating the catalog under Node has not
been tried. CI triggers were read from the workflow files, and start-up was measured as stated in
the baseline. PD1 is now implemented as recorded in its result subsection; later steps remain
unimplemented, and no user acceptance is recorded or implied.

Handoff verification, 2026-10-31, Apple Silicon/macOS, documentation-only, on this branch before the
build-time catalog and console revision (source unchanged since): Debug build passed and
`ctest --preset full` passed all 323 enabled tests with local sockets (13 retired tests disabled,
GPU CTests off). All 433 local links and anchors across the five changed
documents resolve, and `git diff --check` passes. No audiovisual review was needed.
