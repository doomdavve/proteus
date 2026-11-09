# Architecture restructuring plan

**Status, 2026-11-09:** AR0 (merged baseline and contracts) complete at `1fb9950`; no
production refactor started. The user selected authority/client composition for every playable
mode, including offline single player, on 2026-11-09.
**Open:** local-authority scheduling versus solo time scaling and stall discard (see
[timing](#timing-critical-paths-and-inherited-gaps)); browser local authority with Lua;
execution topology; platform, browser and real-time qualification.
**Done when:** every shipped mode runs as authority/client (AR7), modules sit in owning
directories with enforced boundaries (AR8/AR9), and this plan has become `docs/ARCHITECTURE.md`
(AR10).
**Next:** AR1, authority/client design and migration ledger.

This file owns restructuring status, contracts, evidence and limitations. It subsumes the
[architecture review](ARCHITECTURE_REVIEW.md), which stays a dated baseline until AR10 replaces
both documents with `docs/ARCHITECTURE.md`. See the [documentation index](README.md).

## Rules in force now

Other plans and code can rely on these rules today, before any step below lands. Later sections
explain them; a change to one is a change to this list.

1. **Authority/client target.** New gameplay rules, progression, objectives and authoritative
   persistence are written as authority-side code reachable through commands and state, not as
   client-only logic. Do not add a new solo-only gameplay path.
2. **No shared mutable state.** A client never reads or writes authority structs by pointer.
   Messages and snapshots never carry pointers into mutable authority state. Immutable resources
   may be shared read-only with explicit lifetimes.
3. **Core and net purity.** `proteus_boat_core` and `proteus_boat_net` do not draw, read input,
   open windows/audio/sockets, touch files or execute Lua; existing source and symbol checks stay.
4. **One-way dependencies.** Follow the [module contracts](#intended-module-contracts); add the
   enforcement for a boundary in the same change that creates it.
5. **Two clocks.** Integer simulation ticks drive deterministic transitions and authored timers.
   Monotonic elapsed time drives operational deadlines and scheduling. Calendar time never
   stands in for an elapsed deadline. Tests drive supplied time, never wall time.
6. **State a timeliness class.** Every time-sensitive boundary states its class (below), its
   bounded work per call, its queue limits and what happens to late, stale or duplicate results.
7. **Bounded work, no waits.** No unbounded catch-up or queue draining, and no synchronous wait
   for external results on tick or frame paths. Repeated calls to a bounded service must not add
   up to unbounded work per frame. Intentional frame pacing and startup loading are allowed.
8. **Explicit compatibility changes.** Refactors preserve physics, timing, protocol bytes, save
   formats, plugin behavior, controls and rendering. A needed format or behavior change is
   made separately, versioned, and agreed with its owner.
9. **Platform code behind small interfaces.** New platform branches go in CMake-selected or
   backend files, not in shared UI/application/domain logic.
10. **Ordinary C.** Structs and functions; function pointers only for runtime substitution or
    test seams. No ECS, universal session struct, general engine or platform framework.

### Timeliness classes

No path in this project is hard real-time: desktop and browser scheduling give no completion
guarantee. Each boundary is one of:

| Class | Meaning | Examples |
| --- | --- | --- |
| Firm | A result accepted after its deadline has no value and is rejected; the system continues without it. | Browser objective result (rejected at `now >= deadline`), descriptor/query worker receipts. |
| Soft | A late result is still used; lateness degrades responsiveness or presentation, and is measured. | Frame presentation, snapshot/command delivery, input-to-tick latency. |
| Must-complete | Never dropped for lateness; delay is bounded where possible and otherwise recorded as a gap. | Authoritative tick commit, revision commit, saves and autosave. |

A boundary's class is part of its contract, so changing it is a behavior change under rule 8.

## Purpose and scope

**Target: every playable mode uses an isolated authoritative server and a client.** Single
player is a private local authority with one client, not a separate gameplay implementation.
The authority owns committed simulation, rules, objectives, revisions and progression, and
coordinates authoritative persistence. The client owns input, speculative prediction, camera,
rendering, audio and UI. Prediction can reuse core simulation but cannot commit outcomes.
Today the solo, work and network paths are still separate.

Isolation means exclusive mutable ownership and explicit command/state boundaries. It does not
select a process, thread, socket or universal session struct. AR1 assesses an in-process,
single-thread, queued transport first and chooses the execution topology from evidence. Local
play works offline without directory registration, public hosting, credentials or
ICE/STUN/TURN; a local authority is not the external-server launch option imposed on every user.

Make directories show ownership, build targets express dependencies, and platform-specific
implementations sit behind small interfaces, while keeping the C17/raylib playground simple.
Discoverable structure and enforced boundaries help agents as much as human contributors.

Timely delivery is part of correctness for time-sensitive work. The 120 Hz simulation step does
not by itself establish responsiveness or an end-to-end deadline. Preserve existing timing during
extraction and record unresolved timing requirements as gaps with owners.

Authority migration may need new commands, state, protocol/save contracts or timing policy.
AR1 selects them explicitly and they land as identified compatibility changes with the relevant
protocol/plugin/gameplay owner, separate from mechanical refactors. The target is not limited to
today's network feature subset. Keep executable names, asset paths and development commands.

Review **all** platform selection: `__EMSCRIPTEN__`, `_WIN32`, `__APPLE__`, POSIX checks,
feature macros and CMake platform decisions. Classify each by responsibility and placement.
Checks inside platform implementations can be appropriate; repeated branches in shared logic
merit extraction. The goal is clear ownership, not zero preprocessor use.

### What this track defers

Checked against the [Open loops board](README.md#open-loops) on 2026-11-09. This track does not
take over, block or complete:

- **Multiplayer** M4–M8 acceptance, the M5 impairment matrix, resource counters, parser fuzzing
  and soaks ([multiplayer plan](MULTIPLAYER_PLAN.md)). Local authority reuses the net code, so
  AR6/AR7 run the existing net regression suite and record that M5 coverage is still absent.
- **Plugins** P5g recordings, P5h qualification and human checks ([P5 plan](PLUGIN_P5_PLAN.md));
  marina M3; free-package scene map checks; plugins-by-default human checks; R2 loaded maps.
- **Sandbox** S1 fun review, S0c qualification, structured HUD and T0/S2
  ([sandbox plan](FREE_RIDE_SANDBOX_PLAN.md)). AR7 migrates the working harbour as it exists.
- **Gameplay and content** loops: deathmatch playtest, race pacing, Gothenburg Harbour, sunset,
  landmarks, living city, controls feel, graphics Batch C.
- **Platform and release** human checks on macOS 13, Windows, Linux, browsers and devices.
  Steps that touch those platforms record what they could not verify.

## Reviewed starting point

Read-only source review on 2026-11-08 against `e25b47a`, macOS (historical; AR0 supersedes it):

- Core and networking have source and archive-symbol boundary checks in
  [check_core_boundary.py](../tools/check_core_boundary.py).
- [BoatNetTransport](../src/boat_net_transport.h) is an existing small backend interface.
- The root [CMakeLists.txt](../CMakeLists.txt) exports the entire `src` include directory from
  core. The flat tree obscures ownership and private headers.
- `tools/` mixes production glue, platform services and presentation with validators and
  developer utilities. `wake_velocity.c` contains most browser integration.
- `proteus_boat_session` combines simulation coordination, presentation and replay ownership,
  and links rendering/files.
- The save codec depends on the broad plugin target, which also holds Lua, package I/O and worker
  staging. Inspect linked symbols before making claims about binary contents.
- Core is device/I/O-free by policy but uses raylib types/math. Full raylib independence is
  deferred until a concrete consumer needs it.

## Reconciliation of the 2026-10-19 architecture review

This plan succeeds the review, including recommendations beyond directory and platform
structure. It does not reopen completed work or authorize unrelated gameplay, performance or
persistence changes. Every remaining recommendation is verified implemented, selected here,
owned by another linked plan, or deferred with a trigger; the
[post-integration disposition](#historical-review-disposition-after-integration) records the result.

| Review topic | Disposition and successor responsibility |
| --- | --- |
| Long-session clock | Integer clock implemented. AR10 documents tick ownership, derived time and bounded presentation phases. |
| World/resource ownership | Explicit world/resources and scenery packs implemented. Remaining lifecycle/cache work stays with the [foundations plan](BOATING_FOUNDATIONS_PLAN.md); AR7/AR8 preserve ownership and AR10 documents resource lifetimes. |
| Simulation, rules and presentation | Core separation and command input implemented. AR2–AR7 address the remaining application/dependency concerns. Preserve cosmetic isolation, interpolation and post-tick persistence. Dropped-time/race validity stays a product decision before competitive leaderboards. |
| Collision and visibility scaling | Indexed collision and tile culling implemented; preserve exhaustive/indexed oracles and per-pass visibility checks. Continuous collision, streaming and local origins need concrete evidence and separate work. |
| Workload measurement | Preserve instrumentation, deterministic workloads and budgets. Old wake capacities, ABI sizes and timings are history, not requirements. |
| Explicit content and persistence | Typed definitions, compiled water and scenery packs supersede parts of the proposal. Pose ghosts and resumable saves stay distinct contracts. Revision-keyed bests deferred before competitive leaderboards. |
| Build and validation loop | Shared libraries, boundary checks and test lanes present. AR8/AR9 improve ownership and header/build scope. |

Carry forward: 120 Hz simulation and deterministic scenarios; owned render snapshots; CPU/GPU
parity for physical surfaces, coordinates, poses and gameplay visibility (CPU need not gain every
GPU cosmetic); bounded visual work; independent visual recipes and handling; portable recordings
and atomic persistence; source/asset attribution. Do not change accepted handling.

The review's product questions (exploration saves, AI versus ghosts, input remapping, camera
comfort, recovery, audio settings, support hardware) stay with their feature tracks. At AR10 no
recommendation may remain actionable only in the old review.

## Workflow and coordination

- Feature work continues against implemented contracts and the
  [rules in force](#rules-in-force-now). A feature needs an architecture step only when its
  own plan names that dependency.
- Implement in a separate Git worktree. Keep work local until the user approves a PR; approval
  of this plan is not approval to push or open one.
- One branch and one PR per milestone, built from small commits (one per step or substep); do
  not open a PR per step or stack PRs. Milestones: AR1; the seams and pilot (AR2–AR6); each AR7
  migration group; AR8/AR9 moves; AR10.
- Separate interface changes, target changes and mechanical moves into separate commits. Keep
  move branches short-lived, especially for networking and shared plugin/application files.
- Broad include/build moves (AR8/AR9) need a project-wide quiet window: merge outstanding
  affected PRs first and keep moves mechanical. Other steps need coordination on affected
  files, not a freeze.
- Before each handoff update this plan's status header and evidence. Update the documentation
  index only for area-level changes. Update affected commands, guides and links in the same change.

## Sequence and completion gates

Steps are numbered in execution order. AR2–AR6 run in the dependency order AR1 selects; AR8a
can start once AR1 is reviewed. Suggested substeps are commits, not separate PRs.

| Step | Purpose | Depends on |
| --- | --- | --- |
| AR0 | Merged baseline, module and timing contracts (done) | — |
| AR1 | Authority/client design and migration ledger | AR0 |
| AR2 | Application input and capabilities | AR1 |
| AR3 | Application platform services | AR1 |
| AR4 | Plugin worker backends | AR0 |
| AR5 | Plugin data and execution dependencies | AR0 |
| AR6 | Local authority/client pilot | AR1 and its selected seams |
| AR7 | Migrate modes, retire separate production sessions | AR6 |
| AR8a | Move modules with settled ownership | AR1 (plugin data after AR5) |
| AR8b | Move session, persistence and application files | AR7 |
| AR9 | Localize CMake and header access, alongside AR8 | AR8a/AR8b |
| AR10 | Close as `docs/ARCHITECTURE.md` | AR7–AR9 |

Step IDs before 2026-11-09 differed (A0b→AR1, A1→AR2, A2→AR3, A3→AR4, A4→AR5, A0c→AR6, A7→AR7,
A5→AR8, A6→AR9, A8→AR10); `AR` avoids collision with the adventure plan's A-steps.

### AR0 — Refresh the merged baseline (complete)

AR0 recorded the source revision and dependency graph; inventoried platform conditionals by
responsibility; identified public and implementation-only headers and ownership/lifetimes;
traced plugin values, evaluation, host, I/O and workers; reconciled the review and the merged
multiplayer interfaces; produced the module contract table; selected preservation fixtures; and
traced tick, frame, transport and worker critical paths. Results are in
[AR0 merged baseline](#ar0-merged-baseline-and-contracts--2026-11-09).

### Contracts required for each extraction

Introduce the boundary enforcement with the extraction, not later in AR9. Check indirect
dependencies as well as direct links. A smaller exported directory or qualified include does
not make adjacent implementation headers private: use a layout or include check that enforces
it. New data-only codecs need evidence that they build without evaluation/worker libraries, and
checks against Lua/I/O dependencies. Reuse existing boundary tooling or a minimal consumer.

For each asynchronous service or worker, record:

- Owner and lifetime of the operation, copied versus borrowed inputs, and result lifetime.
- Who starts, progresses and polls it, when completion becomes visible, and the commit boundary.
- Its clock (rule 5): timestamp origin, units, deadline comparison, acceptance point, and
  behavior under pause, suspension and delayed polling.
- Its timeliness class, bounded work per poll, and cancellation/close behavior, resource
  release and stale-result identity (epoch, revision, tick, checkpoint).

Moving work outside the fixed-step loop does not remove it from the frame budget. Preserve
non-copyable resource owners, copyable borrowing worlds and stable revision addresses until
borrowers rebind. Preserve native/browser execution differences rather than forcing one timing
or blocking model into one API.

### AR1 — Authority/client architecture and migration design

Produce the design before AR2 implementation; update AR0's intended contracts without rewriting
its observed baseline. Decide responsibilities before APIs/layout and preserve core/net purity.

1. Map authority, client, storage and platform adapter ownership for every live mode. Build a
   behavior matrix covering free boating, voyage/tour/shelter progression, time trial and race,
   working harbour/cargo/jobs, hosted free/race/deathmatch/hunt, scripted demos and replay/ghost
   viewing. Offline validators and reference renderers can call core directly; they are not a
   second production gameplay path.
2. Define command intake, authoritative tick/commit, client prediction/reconciliation and
   presentation order. Reuse core simulation, `BoatNetTransport`, bounded memory transport and
   rules adapters where suitable. Local delivery must not invoke the opposite endpoint
   reentrantly from send callbacks.
3. Define a shared gameplay command/state contract for local and remote compositions: what fits
   protocol v14, what needs extensions, and what stays local host management outside the
   protocol. Local mode exercises the same authoritative rules, never a direct-call bypass. Not
   every local storage or lifecycle operation needs to be remotely invokable. Do not force every
   mode into today's fleet representation. Co-op Working Harbour or remote exposure of
   private-only capabilities is separate product work.
4. Compare queued in-process scheduling, threads, worker and process options, starting from the
   simplest measured viable local composition. Specify service order, queue capacities and
   backpressure, authority/client fairness, input-to-tick latency, update cadence and
   late-result policies. Threads need explicit ownership/synchronization/shutdown evidence. Never
   add sleep/wait cycles inside an authority tick or client frame.
5. **Resolve local scheduling against the server's elapsed-time model.** The server derives due
   ticks from elapsed `now`, runs at most 8 catch-up ticks per service call and flags overload
   after a backlog above 60 ticks lasts over a second; it also uses `now` for peer timeouts.
   Solo play instead clamps each frame's real delta to 0.1 s and then scales by ¼× to 8×
   (discarding stalled time), and the work session adds no time across a gap over 1 s. At 8×
   and 60 fps a local authority needs 16 ticks per frame, so driving today's server with scaled
   `now` would trip overload, and scaling or freezing `now` would also move peer timeouts.
   Choose deliberately, for example a client-granted tick budget for private authorities that
   keeps elapsed time for operational deadlines. Preserve private menu/help/focus pause,
   suspension handling, time scaling and stall discard; keep public-room simulation running when
   a client pauses. Bound work across the whole host frame, not only each service call.
6. Place course results, saves/resume, autosave, authored revisions and objective state on the
   authority side, with file I/O in a storage adapter outside core/net. Specify safe checkpoint
   capture and commit acknowledgements. Preserve personal save locations and bests. Pose ghosts
   stay presentation recordings, distinct from resumable state and P5g records; reuse P5g where
   appropriate without a competing format.
7. Select browser local-authority composition and mode support. The client Wasm excludes Lua
   execution, while local authored objectives need an authority runtime. Compare authority
   orchestration with the existing isolated evaluator against a separate authority Worker/Wasm
   with bounded messaging and lifecycle. Do not put Lua in the rendering client or require a
   remote server for offline solo play. The current T/T+1 objective gating is baseline behavior
   to preserve or explicitly revise, not proof that the evaluator worker is a complete authority.
8. Decide command precision. Solo control currently feeds simulation directly; the protocol
   quantizes commands. Adopting quantization or a different prediction policy can change
   handling and latency, so it is a recorded decision with evidence.
9. Publish a migration ledger: mode, missing commands/state, execution topology, compatibility
   changes, prerequisite AR2–AR5 slices, fixtures, acceptance gates and removal point for the
   old production path. List unresolved product choices (pause and save semantics among them)
   for the user rather than inventing defaults.

Done when the ownership/temporal contract, native/browser strategy, mode matrix and staged
migration ledger are reviewed and timing/compatibility gaps have named owners. AR1 implements
no scheduler, worker, protocol extension or mode.

### AR2 — Normalize application input and capabilities

**AR2a: menu/pointer input.** Introduce a small input value for shared UI navigation,
activation, pointer position and pointer activation. Acquire platform input in selected
implementations; migrate menu/title/console consumers incrementally, starting with duplicated
pointer/touch code. Preserve event consumption, precedence, focus behavior and steering. Keep
window, canvas and low-resolution coordinate conversion explicit. Produce UI actions and
gameplay commands for the AR1 client boundary; shared UI gains no new direct mutation of
authoritative state. Keep predicted controls distinct from commits.

**AR2b: capabilities and operations.** Express actual capabilities such as
application-controlled fullscreen and pointer capture, distinguishing support from mutable state
(capture active). Move platform bridge functions into CMake-selected implementations; keep
gameplay decisions in common code. No speculative capability fields.

Done when migrated shared UI code has no platform branches, menus expose the same actions, and
keyboard, mouse, touch, focus loss and capture keep their behavior. Verify focused
selection/row/input-consumption contracts, native windowed menus/console/steering/fullscreen,
and browser keyboard/mouse/touch layouts, resizing and focus transitions.

### AR3 — Extract application platform services

Take separate slices with their existing lifecycles:

| Responsibility | Common contract | Implementation ownership |
| --- | --- | --- |
| Scenery acquisition | Start, progress, ready/failure, release | Browser fetch and native loading |
| Plugin import/export and browser objective bridge | Request, result, cancellation | Browser file/download APIs, native acquisition, the objective worker bridge in `wake_velocity_plugin.c` |
| Server discovery | Refresh, status, available entries | Existing native/browser directory clients |
| Application lifecycle | Startup, frame scheduling, shutdown | Native loop and browser integration |

Use explicit asynchronous states; do not force browser work into blocking native-shaped APIs.
Keep message delivery behind the transport boundary and the merged multiplayer interfaces for
discovery/connection setup.

The browser objective bridge gates solo simulation after dispatch at tick T and before T+1 while
rendering/UI continue; waiting time is discarded, not converted into catch-up. Preserve this
transaction boundary, its pinned identity, staged-result commit, reset deferral, BUSY behavior and
failure/quarantine recovery. Its five-second deadline is firm: it limits result validity, and
does not promise completion or cancellation within five seconds. Keep rejecting a successful
result completed before expiry but first accepted at or after it, tested with virtual time at
the exact boundary. Removing the gate is a separate approved behavior change.

Done when migrated common code contains no platform bridge bodies and success, failure,
cancellation and repeated-session cleanup stay defined and verified, including the existing
objective-worker checks (descriptor staging tests do not cover the bridge).

### AR4 — Separate plugin worker backends

Extract browser, Windows and POSIX worker implementations from `boat_plugin_stage.c`. Keep
common request/result handling, revision validity and lifecycle policy separate from launch,
polling, byte transfer, termination and backend cleanup. Preserve the public staging API where
practical, wire framing, limits and platform deadlines. Keep staging and console queries
consistent without forcing unrelated internals together.

Done when common staging code contains no OS handles, process-launch calls or JavaScript
bridges, every close path releases backend resources, and stale/malformed/oversized/late
results keep their rejection behavior. Verify staging/query validators, lifecycle cases with
supplied time, browser worker checks, and Windows/POSIX builds and runtime paths; record
unavailable platform coverage.

### AR5 — Narrow plugin data and execution dependencies

Using AR0's graph, establish these provisional responsibilities:

| Responsibility | Owns |
| --- | --- |
| Data/codecs | Runtime-independent values, encoding and decoding |
| Evaluation | Lua execution and evaluation-specific validation |
| Host | Revision ownership and transaction orchestration |
| I/O | Package acquisition from files |
| Staging | Isolated worker execution |

Extract only values/codecs required by data-only consumers. Make sandbox save depend on the
narrower contracts. Update catalog generation and native/browser consumers. Keep Lua-dependent
operations in execution targets and keep Lua out of the browser game's main Wasm. Enforce the
data/codecs contract with a minimal data-only consumer or link evidence and negative checks for
runtime/I/O dependencies. Keep application adapters such as `BoatRoomObjective` outside net, and
generic plugin values out of multiplayer.

Done when data-only consumers do not require evaluation/workers, core/net stay independent of
plugin execution, formats and transaction behavior are unchanged, and targets have no cycles.

### AR6 — Local authority/client pilot

After the seams AR1 selects, migrate private native free boating first unless the ledger
identifies a better minimal slice. Start/stop one local authority and one client through bounded
command/state exchange. Exercise bootstrapping, movement, prediction/render views, time scaling,
pause, reset, teardown and repeated sessions without external networking. Keep the remote server
path passing; reuse the authority rather than writing a second local rule engine. Preserve input
responsiveness and assess scheduling cost on named hardware.

Verify commands/state at the authority boundary against AR0 traces, zero-delay and impaired
memory delivery, malformed/full queues, endpoint failure, release after partial startup, real
offline startup and no stale commands across session generations. A temporary rollout switch can
support comparison; AR7 removes parallel production paths. Done when the pilot passes the
reviewed temporal and behavior contracts. It does not qualify voyages, cargo, persistence or
browser authority.

### AR7 — Migrate modes and retire separate production sessions

Execute AR1's ledger in reviewable groups after the pilot: voyages/tour/objectives; time
trials/course/replays/bests; working harbour/cranes/cargo/jobs/save/resume; and browser local
authority. AR1 refines the order. Preserve hosted room modes throughout. Authority-owned mode
adapters may differ; reuse simulation and the client boundary rather than one monolithic session.

For each mode prove equivalent authoritative outcomes from the same command stream, input and
presentation behavior, pause/scaling/restart policy, objective/revision behavior, persistence
compatibility and overload recovery. Keep irreversible file effects out of speculative client
execution. Pure refactors keep byte/trace equality; identified extensions get version and
compatibility tests and recorded decisions.

Before removing an old path, run its selected comparisons, the net regression suite and affected
native/browser manual acceptance. Then remove superseded branches, duplicate progression/tick
ownership, fallback switches and dead targets. Helpers used by core validators, ghost playback
or presentation are not automatically obsolete.

Done when every shipped playable mode uses authority/client composition with ledger evidence,
local and remote share authoritative rule implementations, and no independent solo gameplay
path remains. Missing platform/mode support blocks this gate unless the user approves a scope
change.

### AR8 — Move files into owning directories

Proposed layout (AR1 refines it):

```text
src/
  core/
  net/
  render/
  plugins/
  platform/
  session/
apps/
  wake_velocity/
tools/
tests/
cmake/
```

**AR8a: settled modules.** Core and boundary inputs, rendering/scenery, net and plugins (data
after AR5) already have settled ownership and can move once AR1 is reviewed, in a quiet window.
**AR8b: session, persistence and application.** Session, work session, storage adapters,
authority adapters (`BoatRoomObjective` and `boat_room_package` now in `tools/`) and the
production application out of `tools/` move after AR7 settles their ownership. Validators move
into `tests/`, leaving developer utilities in `tools/`.

Shared platform services belong in `platform`; application-specific platform glue stays with the
application; plugin worker implementations can stay with plugins. Keep header/implementation
pairs together. Allocate scenery and persistence explicitly rather than into a miscellaneous
directory. Reorder for active work, not symmetry.

For each move preserve target/executable names and runtime asset paths; update includes,
scripts, packaging, CI and documentation links; adapt boundary checks without weakening them.
Windows CI selects targets and test names explicitly, and Linux/macOS and browser have separate
lanes: update all of them. Done when a fresh build resolves all consumers and
production/test/tool ownership is visible.

### AR9 — Localize CMake and header access

Apply alongside each AR8 move, without unrelated build-policy changes:

- Give modules local `CMakeLists.txt` files; keep root setup/options/composition readable and
  dependency/packaging helpers in `cmake`.
- Move test registration near its owners; preserve test names, labels, presets and coverage.
- Replace whole-`src` include exposure with scoped paths; keep implementation headers private.
- Carry forward boundary checks from each extraction, including indirect forbidden paths and
  private-header access; preserve source/symbol gates during moves.

Done when source additions normally touch their owning module, unrelated headers are not
inherited, boundary checks still reject deliberate violations, and quick/full lanes keep their
coverage. Add focused negative checks, not tests that restate file lists.

### AR10 — Close the plan as the architecture specification

Rewrite this file as `docs/ARCHITECTURE.md`: implemented modules, dependency graph,
public/private contracts, clocks and timeliness classes, world/resource ownership, platform
implementations, boundary enforcement, local/remote authority-client composition, migrated
modes, tool/viewer exceptions, scheduling ownership, work/queue bounds, overload and late-result
policies, and requirements versus measured qualification. Describe the actual result, not the
provisional layout. Resolve or defer every remaining proposal, including those from the review,
then remove `ARCHITECTURE_REVIEW.md`. Update links across README/docs to the specification or
task owners, keeping dated citations as history (pinned Git references if needed).

Spec boundaries: `ARCHITECTURE.md` owns modules, dependencies, authority/client composition,
clocks, timing contracts and platform placement. The candidate `MULTIPLAYER.md` owns rooms,
hosting, remote prediction/reconciliation tuning and network failure behavior; the
[protocol spec](MULTIPLAYER_PROTOCOL.md) keeps the bytes. The candidate `WORKING_HARBOUR.md`
owns cargo rules, the save format and autosave policy. Each links to `ARCHITECTURE.md` for
composition instead of restating it.

Any reduction in target scope needs an explicit user decision; passing tests do not imply
acceptance.

## Verification policy

For each implementation handoff, configure/build Debug and run `ctest --preset full`; use
`ctest --preset quick` while iterating. Follow [testing](TESTING.md) and [AGENTS.md](../AGENTS.md).
Use a fresh build directory for path/build restructuring. Do not replace the full lane with an
unrecorded subset, and distinguish disabled GPU tests from passing GPU verification.

When shared/browser targets or packaging change, also run the web build from
[WEB_BUILD.md](WEB_BUILD.md), the existing Wasm checks and relevant browser checks:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --preset full
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-web --target web --parallel
```

For application/render wiring, compare deterministic CPU captures before and after. For affected
simulation and serialization consumers, compare AR0's selected traces and protocol/save/replay/
catalog bytes with the pre-change build under the same configuration. Preserve expression,
update and emission order. Fixture changes, tolerance relaxation and revision bumps must not
disguise an incompatible refactor.

Authority migration explicitly tests private versus hosted pause/time policies, time scaling,
offline startup, repeated local lifecycle, authority loss, save acknowledgement, bounded queue
overload and browser authority/client teardown. Preserve room wire bytes during pure refactors;
version extensions under the protocol contract. Inspect frames if visual effects change; add
windowed/manual checks when input, motion, lifecycle, audio or asset loading change; exercise
affected joins, reconnects and room modes.

Supplied virtual time checks exact deadline boundaries, late/stale disposition, queue
saturation, overload, bounded catch-up and pause/resume, without sleeping. These establish
semantics, not speed. Qualify timing claims separately with native/browser measurements when a
change warrants it, recording workload, build/options, hardware, sampling clock, tail latency,
deadline misses and worst observed stalls; the worst observed time is not a proven bound. Avoid
strict millisecond CI gates on shared runners. Check each harness's clock: existing benchmark
elapsed measurements use `timespec_get(TIME_UTC)` and can be affected by clock adjustment.

After moves, check documentation links and commands, CI paths, release packaging and generated
catalogs. Record date, revision, platform/options, commands/results and unverified behavior. Do
not infer user acceptance or cross-platform success from automated checks on one machine.

## AR0 merged baseline and contracts — 2026-11-09

Source: `1fb9950b7492a45eec813de309454628a6b78245` (PR #175). No production sources changed.

### Observed dependencies and P5 integration

An arrow means "depends on":

```text
core -> raylib (types/raymath; source and symbol policy restrict device/I/O access)
net -> core
net_enet -> net + ENet; optional RTC adapter remains a transport implementation
scenery -> core; render -> core + scenery; gpu -> render
session -> render + file; drive -> session
plugins -> core + JSON + Lua (also contains host, I/O, staging, eval/wire code)
catalog -> plugins; sandbox_save -> plugins
work_session -> sandbox_save + core; work_save -> work_session + file
application/server -> selected modules and platform adapters
```

P5 adds `boat_marks` to core, protocol v14 and room definition v4, HUD/revision data to net,
and the application-side `BoatRoomObjective` (`tools/boat_room_objective.c`) over
`BoatNetRules`. Net invokes the adapter but does not link Lua. Rules callbacks must not touch
the server (non-reentrancy). Each server tick runs: fleet step, match/race observation, mark
events, rules `after_tick`, objective publication, revision processing, then births/snapshots.
Objective callbacks run at 12-tick boundaries. Source reload stages a copied candidate;
definition revisions use server-owned staged/announced values and commit at the existing safe
boundaries.

### Intended module contracts

Permitted dependency directions, not claims of complete current enforcement. For each
extraction the named step closes its enforcement gap. Public headers are representative entry
contracts; audit their transitive includes when touching them. New filenames are provisional.

| Responsibility / entry contracts | Permitted dependencies and ownership | Forbidden dependencies / enforcement |
| --- | --- | --- |
| Core: `boat.h`, `boat_world.h`, `boat_fleet.h`, definition/input/clock values | Math/types, compiled geometry, core modules. World resources/maps are non-copyable owners; copyable worlds borrow them. | No device/input/files/sockets/Lua/plugins. Source and archive-symbol tests, including marks; no callback execution hidden in core. |
| Net: protocol/client/server/prediction and `boat_net_transport.h` | Core; byte/event transport and application-supplied rules callbacks with caller-owned contexts. | No transport OS APIs or plugin execution. Existing core/net gates; AR5/AR9 add indirect enforcement as targets split. |
| Presentation: `boat_view.h`, scene/presentation values | Core/scenery; CPU and GPU own their caches/resources. Cosmetic updates cannot feed simulation. | No reverse core dependency. Simulation parity with cosmetics disabled; AR8/AR9 enforce header ownership. |
| Plugin data/codecs: extracted value/state/descriptor interfaces | Core values, bounded buffer helpers and JSON where required; caller-owned bytes/results. | No Lua evaluation, filesystem, processes, browser bridges, rendering or input. AR5 adds source/symbol restrictions and a standalone consumer. |
| Plugin evaluation/host: `boat_plugin.h`, `boat_plugin_state.h`, `boat_plugin_host.h` | Data/codecs/core; evaluation uses private Lua. Host owns stable revisions; borrowers rebind at commit. | No platform execution dependency in the data layer. AR5 splits combined headers; AR4 isolates worker mechanics. |
| Package I/O and workers: `boat_plugin_io.h`, `boat_plugin_stage.h` | Evaluation/data and an explicitly selected OS/browser backend; each operation owns handles and copied requests. | No OS/browser handles in common request policy. AR4 backend selection, lifecycle tests and include checks. |
| Save/file: sandbox byte codec versus `boat_file.h`/work/replay persistence | Codec consumes data values only; authority coordinates checkpoint capture; storage adapters own paths/handles and atomic replacement. | Codec acquires no files and executes no plugins. AR5 standalone consumer; AR1/AR7 map replay, saves and ghosts without a competing P5g format. |
| Authority/session/work/room adapters | Committed simulation, rules/progression, objective/revision transactions and persistence coordination behind explicit commands/results. `BoatRoomObjective` is an existing authority adapter. | No renderer/input/device access in authority rules; no mutable state shared with the client. AR1 defines, AR6 pilots, AR7 migrates. |
| Client composition | Controls, prediction, camera, UI/render/audio; consumes authoritative state and immutable resources with explicit lifetimes. | No authoritative progression or save effects from prediction; no direct authority mutation, reentrant endpoint delivery or parallel solo gameplay. AR2 supplies commands; AR6/AR7 prove behavior. |
| App input/services | Normalized values, small capability/operation contracts, selected native/web backend. UI consumes one input snapshot per frame. | No common UI-to-JavaScript/OS dependency for migrated functions. AR2/AR3 enforce bridge placement and event consumption. |

A smaller include directory alone cannot enforce private headers. AR2–AR4 use backend-private
headers without exporting their directories, plus include checks for migrated consumers; AR5
applies the pattern to codec/runtime headers; AR8/AR9 generalize it.

### Platform inventory and disposition

Inventory command: `rg -n '#.*(EMSCRIPTEN|_WIN32|__APPLE__|POSIX|__linux__)' src tools
--glob '*.[ch]'`, plus CMake platform/feature branches and `WAKE_ACCELERATE` /
`BOAT_WAKE_PORTABLE` selection. Re-run before moving each group.

| Group | Current locations / disposition |
| --- | --- |
| Shared UI and browser bridges | `wake_velocity`, title, menu, console, plugin/net glue, touch helper. AR2 input/capabilities and AR3 services; application policy stays. |
| Process/worker ownership | Plugin stage/worker, game host, console bridge, server stdin exit, Windows binary I/O. Stage backends in AR4; host/bridge mechanics stay in their adapter files unless duplication justifies AR3 extraction. |
| Files/assets/packages | `boat_file`, plugin I/O, replay, `boat_assets`. Windows filesystem/binary handling, Apple application-support/bundle paths and POSIX/Darwin declarations are legitimate adapter concerns. AR5 separates codec/I/O; AR8 assigns ownership. |
| Timing/diagnostics | Profile clock, frame pacer, process/memory sampling and GPU timer. Keep platform implementations; do not merge pacing, measurement and simulation clocks into one service. |
| Math/transport | Apple Accelerate versus portable wake transforms; ENet Windows/POSIX sockets and the Linux reuse-port exception. Keep backend-local selection and parity tests. |
| Audio/GPU/review tools | Browser audio teardown, GPU availability and native-only visual review tools. Appropriate build restrictions; no abstraction needed. |
| Build/tests | CMake Emscripten/native source selection, Linux Wayland patch, Apple framework, Windows libraries/flags, optional RTC and GPU lanes; platform fixture setup. AR8/AR9 preserve conditions and paths. |

### Timing, critical paths and inherited gaps

Each gap has an owning step that closes it with evidence or keeps it with a reason. No new
numeric wall-time budget is asserted. Rows describe the observed baseline.

| Path | Existing limits, clock and class | Gap owner |
| --- | --- | --- |
| Solo frame/input/ticks | `boat_time_frame_seconds` clamps real delta to 0.1 s, then scales by ¼× to 8× (`boat_time_control.h`); fixed 120 Hz steps. Inactive/replay/objective-waiting paths reset the accumulator. Stalled time is discarded. Soft. | AR2/AR3: end-to-end input/frame deadline and maximum frame work unqualified. Competitive dropped-time validity deferred. |
| Work session frame | `boat_time_work_frame_seconds` adds no time when the session was inactive or a gap exceeds 1 s (suspension without a focus event); shorter stalls use the solo clamp. Soft. | AR1 item 5 keeps this suspension rule in the local-authority clock; AR7 working-harbour group verifies it. |
| Server/client service | At most 128 messages per service call (`BOAT_NET_MESSAGES_PER_SERVICE`); at most `BOAT_NET_CATCH_UP_TICKS` (8) ticks per call; due ticks follow elapsed `now`, with no stall discard; overload after a backlog above 60 ticks lasts over 1 s. Peer timeout 5 s from the same `now`. Input repeats six ticks, neutral after 30. Authoritative state uses ticks. Tick commit must-complete; delivery soft. | AR3/AR5: per-message/callback cost is no proven service-time bound. Preserve timeout, backpressure and overload behavior. |
| Local authority scheduling (conflict) | The server model above cannot host solo time scaling: 8× at 60 fps needs 16 ticks per frame against an 8-tick cap, and scaling `now` would also scale peer timeouts. Solo and work sessions discard stalled time; the server catches up. | AR1 item 5 selects the private-authority scheduling input; AR6 verifies it at every speed. |
| Native/authority objective | Synchronous evaluation every 12 ticks; 64 queued objective events; overflow quarantines. Callback budget 100,000 instructions, migration 500,000 (`boat_plugin.h`). Reload/revision can initialize or migrate synchronously. Must-complete. | AR5: compilation, C calls, allocation and callbacks can hitch; instruction budgets do not bound wall time. No off-thread authority redesign selected. |
| Browser objective | Request copies source/checkpoint/events and pins epoch/revision/tick/digest. Poll rejects `now >= deadline` (5 s), stages an accepted update for the transaction boundary, closes the job and quarantines failure. Simulation gates at T; UI/render continue; no catch-up. Firm. | AR3 extracts this bridge with plugin import/export glue, separately from AR4. Qualify polling/decode/allocation cost without changing transaction policy. |
| Descriptor/query workers | The stage job owns backend resources and copies its request. Deadline 5 s native, 15 s browser; elapsed `now` passed to start/poll. Close cancels and releases; stale receipts rejected. Firm. | AR4: OS termination/reaping and result validation need bounded-cost evidence; cancellation is not immediate OS/browser scheduling. |
| Persistence | Replay flush and work autosave run after ticks in the same frame; atomic replacement protects data. Allocation/copy and synchronous file I/O can stall a frame. Must-complete. | AR3 inventories placement; AR7 keeps or separates it only for a real consumer. Do not describe post-tick I/O as outside the frame budget. |
| Render/audio/resources | View-owned caches, bounded wakes/fleet; scenery loading, GPU rebuild/upload, audio update and present run in frame/startup paths. Pacing waits are intentional. Soft. | AR2/AR3/AR8: frame latency, rebuild spikes and browser suspension stay measured requirements, not guarantees. |

Borrowed resources outlive worlds, sessions, fleets and copies. Host revisions stay at stable
addresses until commit/rebind; a room objective's initial source is borrowed until an accepted
reload installs an owned copy. Server revision installation rebinds the borrowed scene before
later consumers use it. Late authoritative data is not discarded by a generic policy.

### Historical review disposition after integration

Integer clocks, explicit resource owners, copied-world contracts, indexed map queries,
view-owned caches, static libraries and boundary gates are implemented; keep their validators.
Shared scene evaluation exists; the foundations plan still owns S1 frame consolidation, full S4
validator splitting, full P2/P3 workload qualification, mapped W1 and S5.

Deferred with triggers: swept-contact/continuous collision when a reproducible miss requires it
(boating foundations/adventure owners); importer surface semantics and coordinate consolidation
when importer/map work changes them (adventure/map owners); streaming/local origins only with
load/precision evidence; dropped-time validity and revision-keyed bests before competitive
leaderboards (boating/race owners). P5g recordings and P5h/browser/impairment acceptance stay
with P5.

The blanket asset copy target remains; making it incremental is a build/performance follow-up if
measured useful. No sanitizer configuration exists in root CMake or the workflows; sanitizer
recipes are a foundations/testing follow-up before any claim of sanitizer coverage. No CI,
platform or optimization change is authorized here.

### Baseline preservation selection

AR2 uses CPU captures plus menu/input tests and later manual native/browser checks. AR3/AR4 keep
the virtual-time worker/authoring checks. AR5 also compares protocol goldens, the generated
catalog, save/replay bytes and deterministic physics traces, not only round trips.

To regenerate, check out `1fb9950`, configure Debug with `-DPROTEUS_GPU_TESTS=OFF` (AR0 pointed
`FETCHCONTENT_SOURCE_DIR_*` at already-fetched copies of the same pinned raylib/Lua/ENet
sources instead of downloading them), build, run `ctest --preset full` (which writes the
save/replay fixtures), then:

```sh
mkdir -p build/a0-baseline
./build/boat_benchmark --scenario wake-loop --mode simulation --renderer none --warmup 120 --frames 1800 --presentation on --output build/a0-baseline/wake-on
./build/boat_benchmark --scenario wake-loop --mode simulation --renderer none --warmup 120 --frames 1800 --presentation off --output build/a0-baseline/wake-off
./build/validate_boat_net --case golden > build/a0-baseline/net-golden.txt
./build/wake_velocity --renderer cpu --screenshot build/a0-baseline/basin.png --time 3
./build/wake_velocity --renderer cpu --location outer --screenshot build/a0-baseline/outer.png --time 3
```

SHA-256 of the deterministic outputs on arm64 macOS, AppleClang 21.0.0 (the benchmark's timing
CSV/JSON files vary per run and are not compared):

| Output | SHA-256 |
| --- | --- |
| `wake-on.state.csv` = `wake-off.state.csv` | `bff8116963d95f64c1e80590c981715c3261d2cc19a542f25c716521d09c89ce` |
| `net-golden.txt` | `5587722b8267d4d0ce1377dd626dea1fadb3aff33a164458607608f26226adf1` |
| `generated/boat_plugin_catalog_data.c` | `fb62b1798e383957f95a0606442d5997db52b59f895f9678a2928657b839dbdf` |
| `work-save-tests/work.sav` | `a8e5a14e15cdd0ba4bffa0a01241549d81ac98a51f930ab970f02806266f25c8` |
| `replay-tests/replay-fixture.run` | `87bafc830f5716e92da3568bebde18633958d86a9bfbe4509955d68c9a76126e` |
| `basin.png` | `45b690634f288c4e37214113b8c5a1e07acebaaaca93c374f2333318e08f28c6` |
| `outer.png` | `ad4a277fc752c2be1cfd4c446429db11827f06c189b0b8cae6bf0c7d156cebb5` |

The on/off traces are identical, final state hash `1335acbdcc23076e` (hash revision 2), 3,840
ticks including warmup: one deterministic scenario, not wall-time qualification. Both captures
were inspected (boat and wake visible, outer scenery present). Compare same-configuration bytes
when touched and do not normalize format fields. Build-revision strings in application output
need separate treatment, not relaxed physics/codec comparisons.

## Current evidence and limitations

- 2026-11-08: read-only source review by Codex and Astra, agreed direction, and this plan.
  Astra xhigh plan review found the direction sound with no need to block other features;
  its recommendations on early module contracts, asynchronous ownership/time, preservation
  evidence and discoverability were integrated. Astra xhigh real-time consultation added the
  timing/work contracts, critical-path inventory, objective gating and temporal verification.
  Source/document reviews, not runtime qualification.
- 2026-11-09: AR0 completed from merged PR #175 (dependencies, platform responsibilities,
  ownership, critical paths, contracts, review reconciliation, preservation fixtures).
- 2026-11-09: user decision and Astra xhigh consultation: authority/client composition for
  every playable mode, including offline single player. Added design, pilot and mandatory
  migration before session/application moves; execution topology open.
- 2026-11-09: Opus review of PR #176. Added the rules-in-force list and timeliness classes,
  renumbered steps to `AR` in execution order, recorded the local-scheduling conflict and work
  session suspension rule, allowed settled-module moves before AR7, adopted one PR per
  milestone, named Open-loops deferrals, set spec boundaries and recorded baseline hashes.
- AR0 verification, 2026-11-09, arm64/macOS 26.6.2, AppleClang 21.0.0: fresh Debug configure with
  `PROTEUS_GPU_TESTS=OFF` and `cmake --build build -j 8` passed. Configure reported raylib's
  macOS OpenGL deprecation; the build emitted one third-party `stb_vorbis.c` warning.
  `ctest --preset full` failed 12 loopback/socket/process tests under sandbox restrictions; the
  rerun with socket/process access passed all 472 enabled tests in 41.27 s, with 13
  retired/opt-in cases disabled. GPU tests were configured off, not verified.
- Not performed: browser build, Windows/Linux runs, GPU/windowed input/audio playback,
  cross-machine play, performance deadline qualification and user acceptance. Later steps
  carry their own platform/manual gates.
- Documentation links and whitespace checked after each planning change. Build/test results
  qualify the unchanged baseline, not the proposed migration.
