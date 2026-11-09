# Architecture restructuring plan

Status, 2026-11-09: **A0 complete; no production refactor started.**
PR #175 merged as `1fb9950`; the user resumed this track. The architecture worktree is based
on that merge and retains the local planning changes. This file owns restructuring status,
contracts, evidence and limitations. Completed: merged baseline, module/temporal contracts,
preservation fixtures and CPU verification below. The user selected an authority/client-only
game architecture on 2026-11-09. Next: A0b design and migration contracts, before A1a. Baseline checks
do not imply user acceptance or complete real-time/platform qualification.

When the plan is complete, turn it into `docs/ARCHITECTURE.md`: rewrite it as a present-tense
specification of the resulting system, remove proposed work from the live specification, and
update incoming links. Preserve implementation history in Git and any evidence records that
remain useful. This plan also subsumes the [architecture review](ARCHITECTURE_REVIEW.md): its
recommendations must be reconciled below, and the closing specification replaces both documents.
The review remains a dated baseline during execution, not a description of the current system.
See the [documentation index](README.md).

## Purpose and scope

**Target: every playable mode uses an isolated authoritative server and a client.** Single
player is a private local authority with one client, not a separate gameplay implementation.
The authority owns committed simulation, rules, objectives, revisions and progression, and
coordinates authoritative persistence. The client owns input, speculative prediction, camera,
rendering, audio and UI. Prediction can reuse core simulation but cannot commit authoritative outcomes.
This is a selected direction, not a claim about today's separate solo/work/net paths.

Isolation means exclusive mutable ownership and explicit command/state boundaries. It does
not yet select a process, thread, socket or universal session struct. Assess an in-process,
single-thread, queued transport first; choose scheduling/execution topology with evidence in
A0b. Local play must work offline without directory registration, public hosting, credentials
or ICE/STUN/TURN. A local authority is not the existing external-server launch option imposed
on every user.

Make directories show ownership, build targets express dependencies, and platform-specific
implementations sit behind small interfaces. Preserve the C17/raylib playground's simplicity.
Agents benefit from discoverable structure and enforced boundaries just as human contributors do.

**Firm real-time behavior is an architectural concern: timely delivery is part of correctness
for time-sensitive work.** Each affected boundary must define when work becomes eligible,
when its result remains useful, and what happens if it cannot complete or be accepted in time.
The 120 Hz simulation step alone does not establish responsiveness, a verified execution deadline
or an end-to-end response guarantee. Preserve existing timing behavior during extraction;
identify unresolved timing requirements and qualification gaps rather than silently changing it.
This is the design direction, not a claim that the current system already meets every deadline
or that desktop/browser scheduling provides hard real-time guarantees.

Preserve physics, timing, protocol bytes, save formats, plugin behavior, controls and rendering.
Keep executable names, asset paths and development commands working. A refactor must not silently
introduce a format or behavior change. Authority migration may require new commands, state,
protocol/save contracts or timing policy: select these explicitly in A0b and implement them
as identified compatibility changes with the relevant protocol/plugin/gameplay owner, separate
from mechanical refactors. Do not constrain the new target to today's network feature subset.

Use ordinary C structs and functions. Use function pointers for runtime substitution or meaningful
test seams, and CMake-selected implementations for platform differences. Dependencies should be
explicit and one-way; modules need not be unaware of their direct dependencies. Avoid a universal
platform framework or one library per file. Share authoritative gameplay ownership; do not force
every mode into one monolithic session struct or make networking synonymous with sockets.

Review **all** platform selection: `__EMSCRIPTEN__`, `_WIN32`, `__APPLE__`, POSIX checks,
feature macros and platform decisions in CMake. Emscripten was an example, not the scope limit.
Classify each by responsibility and placement. Checks inside platform implementations may be
appropriate; repeated branches inside shared application/domain logic merit extraction. The
goal is clear ownership, not a zero-preprocessor target. Windows/Apple cases have not yet been
assessed as defects.

## Reviewed starting point

Read-only source review on 2026-11-08 against `e25b47a`, macOS:

- Core and networking already have source and archive-symbol boundary checks in
  [check_core_boundary.py](../tools/check_core_boundary.py); preserve their enforcement.
- [BoatNetTransport](../src/boat_net_transport.h) is an existing small backend interface.
- The root [CMakeLists.txt](../CMakeLists.txt) declares libraries but exports the entire `src`
  include directory from core. The flat tree obscures ownership and private headers.
- Application code in `tools/` mixes production glue, platform services and presentation with
  validators and developer utilities. `wake_velocity.c` contains most browser integration.
- `proteus_boat_session` combines simulation coordination, presentation and replay ownership,
  and links rendering/files. This may be useful as a game coordinator; a split needs a consumer.
- The save codec depends on the broad plugin target, which also includes Lua, package I/O and
  worker staging. Declared dependencies do not prove all those objects enter every executable;
  inspect actual symbols before making binary-content claims.
- Core is device/I/O-free by policy, but uses raylib types/math. Full raylib independence is
  deferred unless a concrete consumer needs it.

The [P5 plan](PLUGIN_P5_PLAN.md) owns multiplayer behavior. PR #175 (P5a–P5f) merged on
2026-11-09; A0 below supersedes the pre-merge structural baseline. P5g recording, P5h broader
qualification and outstanding human checks remain with P5 and do not block this track.

## Reconciliation of the 2026-10-19 architecture review

This plan is the successor to the review, including its recommendations beyond directory and
platform structure. Subsuming it does not reopen completed work or authorize unrelated gameplay,
performance or persistence changes. A0 must classify every remaining recommendation as verified
implemented, selected here, owned by another linked plan, or explicitly deferred with a trigger.
The following is the initial disposition, based on the review's history note rather than fresh
qualification of every implementation.

| Review topic | Initial disposition and successor responsibility |
| --- | --- |
| Long-session clock | Integer clock and time contracts are recorded as implemented. A0 confirms the current ownership and tests; A8 documents tick ownership, derived time and bounded presentation phases. Do not repeat the historical float-clock fix. |
| World/resource ownership | Explicit world/resources and scenery packs are recorded as implemented. A0 reconciles remaining lifecycle/cache work with the [foundations plan](BOATING_FOUNDATIONS_PLAN.md); A5/A7 preserve ownership and A8 documents resource lifetimes. Repeated map/session load/unload remains a verification concern where affected. |
| Simulation, rules and presentation | Core separation and command input are recorded as implemented. A1–A7 address the remaining application/dependency concerns. Preserve cosmetic isolation, interpolation rules and post-tick persistence. Dropped-time/race-validity policy remains an explicit product decision before competitive leaderboards; it is deferred here, not implicitly solved by moving code. |
| Collision and visibility scaling | Indexed collision and tile culling are recorded as implemented. Preserve exhaustive/indexed oracles and per-pass visibility checks. A0 identifies any remaining swept-contact gap and its owner; general continuous collision detection, streaming and local origins require concrete correctness/scale evidence and separate scoped work. |
| Workload measurement | Preserve instrumentation, deterministic workloads and bounded budgets. Reconcile unfinished profiling/baseline tasks with the foundations plan in A0. Re-measure only when a change or unresolved concern warrants it; old wake capacities, ABI sizes and timing observations are historical, not current requirements. |
| Explicit content and persistence | Typed definitions, compiled water and scenery packs have superseded parts of the proposal. A0 checks remaining semantic metadata, coordinate-conversion and content-identity concerns against current owners. Preserve pose ghosts versus resumable saves as distinct contracts. Content/revision-keyed bests remain deferred before competitive leaderboards; do not change formats in this restructuring. |
| Build and validation loop | Shared libraries, boundary checks and test lanes are already present. A5/A6 improve ownership and header/build scope. A0 checks current sanitizer, platform CI, asset-copy and relocation coverage rather than repeating the review's obsolete CI assumptions; remaining gaps get an owner or explicit deferral. |

Carry forward these design constraints: 120 Hz simulation and deterministic scenarios; owned
render snapshots; CPU/GPU parity for physical surfaces, coordinates, poses and gameplay visibility;
bounded visual work; independent visual recipes and handling; portable recordings and atomic
persistence; and source/asset attribution. CPU rendering need not acquire every GPU cosmetic effect.
Do not change accepted handling or introduce an ECS/general engine as part of this plan.

The review's wider product questions (exploration saves, AI versus ghosts, input remapping,
camera comfort, recovery, audio settings and support hardware) remain design inputs owned by
their feature tracks. A0 records any that actually constrain the selected architecture; they
are not prerequisite features for closing this plan.

At closure, no recommendation may remain actionable solely in the old review. An unfinished
item must have a named current owner or an explicit deferral and trigger. Preserve dated
measurements as history without presenting the old review as a competing live architecture.

## Workflow and coordination

- Feature work continues against current implemented contracts. A feature requires an
  architecture step only when its own plan identifies that concrete dependency; this track
  is not an umbrella prerequisite for P5 or other features. The foundations plan retains its
  unfinished profiling/frame/validator tasks until A0 explicitly reconciles their ownership;
  gameplay and competitive-validity decisions remain with their existing feature owners.
- Initial planning was documentation-only. The multiplayer prerequisite landed and the user
  resumed the track on 2026-11-09. Complete A0 before production extractions; do not treat
  P5's remaining feature work as another blanket prerequisite.
- Implement every change in a separate Git worktree. Keep work local until the user explicitly
  approves a PR; approval of this plan is not approval to push or open one.
- Use small reviewable branches. For dependent branches, follow the repository's parent-target
  stacking and parent-update rules. Prefer landing steps before starting broad file moves.
- Separate interface changes, target changes and mechanical moves where practical. Keep moves
  short-lived, especially for networking and shared plugin/application files.
- Reserve a project-wide quiet window for A5 and A6's broad include/build changes: merge
  outstanding affected PRs first and keep moves mechanical. A0b, A1–A4 and migration slices
  require coordination with affected files/contracts, not a blanket freeze on other features.
  Complete the session/layout decisions and selected migration before broad moves.
- Before each handoff update this plan's current status, completed task, next task, verification
  evidence and known limits. Update the documentation index only for area-level changes, not
  each substep. Update affected commands, guides and links in the same change.

## Sequence and completion gates

### Real-time contracts

For each affected responsibility, establish:

- Clock domain, activation cadence/trigger, acceptance/commit boundary and applicable deadline.
- Bounded work per tick/frame/poll: catch-up steps, messages/events processed, bytes transferred,
  retries and result validation, including synchronous callbacks and allocation behavior.
- Queue/storage capacities and admission, overflow, supersession or backpressure policy.
- Disposition of late, stale, cancelled and duplicate results, including whether a result
  remains usable after a deadline. Authoritative state and persistence need their own policies;
  do not apply a blanket discard rule to every late operation.
- Observable failure/degradation, priority of optional work, and recovery behavior under overload.

Capacity and instruction limits bound particular resources; they do not prove wall-time bounds.
Use existing requirements and limits. Where a deadline or bound is absent, record the gap and
its owner rather than inventing numeric budgets here. Preserve correctness when optional work
is skipped/deferred; never introduce unbounded catch-up or queue draining. Distinguish deliberate
startup/loading and frame-pacing waits from waits on external work in latency-sensitive paths.

A0 is complete (dated assessment below); A0b/A0c and A1–A8 are **not started**. Suggested substeps are
separate review units, not a
commitment to a fixed number of branches or PRs. Reassess scope at each gate.

Execution order: A0b design first; then A1–A4 and A0c pilot in the dependency order A0b
selects; A7 mode migration/retirement gate before A5 and broad A6 moves; A8 closes the work.
The retained step IDs avoid invalidating earlier references; numeric order is not execution order.

### A0 — Refresh the merged baseline

After multiplayer integration:

1. Record the source revision and library dependency graph, including transitive dependencies.
2. Inventory platform conditionals by responsibility: input, lifecycle, filesystem, timing,
   graphics/audio, discovery, networking setup, workers, import/export and asset acquisition.
3. Identify public contracts and implementation-only headers; record shared ownership/lifetimes.
4. Trace plugin values/codecs, evaluation, host orchestration, package I/O and workers.
5. Record representative native/browser behavior and reproducible verification commands.
6. Reconcile this plan with new multiplayer interfaces, especially authority/client separation,
   rules adapters, revisions and recordings. Reuse new seams rather than competing with them.
7. Complete the review reconciliation above, checking source and owning plans before claiming
   an item implemented. Name owners or deferral triggers for every remaining recommendation.
8. Produce and review a concise module contract table: responsibility, permitted/forbidden
   dependency edges (including indirect paths), public entry headers, ownership/copy rules,
   and enforcement. Distinguish observed dependencies from intended rules. Cover core/net and
   the new data/codecs boundary explicitly; leave exact APIs and directory names provisional.
9. Select existing deterministic traces and serialized fixtures for affected consumers, with
   reproducible same-configuration pre-change comparisons. Record baseline outputs before
   extraction, not after a mismatch appears.
10. Inventory both plugin worker paths: descriptor staging/queries in `boat_plugin_stage.c`
    and browser objective evaluation in `wake_velocity_plugin.c`. Assign the latter to a
    selected slice or explicitly defer it with a reason and trigger; A3 alone does not cover it.
11. Trace tick, frame, transport and worker critical paths, including work immediately before
    and after fixed-step loops. Record synchronous I/O/evaluation, waits, allocations and other
    variable work, known bounds and missing evidence. Inventory accumulator clamping/discarded
    time without reopening the deferred competitive-validity policy. Extend the module table
    with timing ownership, clock/commit semantics, work/queue limits and overload disposition.

Done when every extraction has callers and a concrete responsibility, baseline failures and
unavailable platforms are recorded, the intended module contracts and preservation evidence
are reviewed, and each selected extraction has a timing contract or an explicit gap with an
owner. The plan must reflect the merged implementation. Other plans can rely on these
rules before directory migration or A8; they need not wait for the final specification.

### Contracts required for each extraction

Introduce the applicable boundary enforcement with the extraction, not at A6. Check indirect
dependencies as well as direct links. A smaller exported directory or qualified include does
not itself make adjacent implementation headers private: select a concrete layout or include
check that enforces the public/private contract. Newly data-only codecs must have evidence
that they can be consumed without evaluation/worker libraries, and checks preventing Lua/I/O
dependencies appropriate to their promised purity. Reuse existing boundary tooling or a
minimal consumer; avoid a general architecture-validation framework.

For each asynchronous service or worker, record a short contract covering:

- Owner and lifetime of the operation, copied versus borrowed inputs, and result lifetime.
- Who starts/progresses/polls it, when completion becomes visible, and the commit boundary.
- Distinguish monotonic elapsed time for operational deadlines/scheduling from integer
  simulation ticks for deterministic transitions and authored timers. State timestamp origin,
  units, deadline comparison, acceptance point, and pause/suspension/delayed-polling behavior.
  Calendar time must not substitute for an elapsed-time deadline.
- Cancellation/close behavior, resource release, and stale-result identity where applicable
  (epoch, revision, tick and checkpoint).

Polling/completion handling must have bounded work and must not introduce synchronous waits
for external results on tick/frame paths. Document inherited blocking work and qualification
gaps. Moving work outside the fixed-step loop does not remove it from the frame latency budget.
This does not prohibit intentional frame pacing or initialization I/O.

Preserve existing ownership constraints, including non-copyable resource owners, copyable
borrowing worlds and stable revision addresses until borrowers rebind. Preserve native/browser
execution differences rather than standardizing timing or blocking behavior to obtain one API.

### A0b — Authority/client architecture and migration design

Produce the design before A1 implementation; update A0's intended contracts without rewriting
its observed baseline. Decide responsibilities before APIs/layout and preserve core/net purity.

1. Map authority, client, storage and platform adapter ownership for every live mode. Build a
   behavior matrix covering free boating, voyage/tour/shelter progression, time trial and race,
   working harbour/cargo/jobs, hosted free/race/deathmatch/hunt, scripted demos and replay/ghost
   viewing. Offline validators and reference renderers can call core directly; they are not a
   second production gameplay path.
2. Define command intake, authoritative tick/commit, client prediction/reconciliation and
   presentation order. Reuse core simulation, `BoatNetTransport`, bounded memory transport and
   rules adapters where suitable. No client reads/writes authority structs by pointer. Immutable
   resources can be shared read-only with explicit lifetimes; messages/snapshots cannot contain
   borrowed pointers into mutable authority state. Local delivery must not invoke the opposite
   endpoint reentrantly from send callbacks.
3. Define a shared gameplay command/state contract for local and remote compositions. Identify
   what already fits protocol v14, what needs extensions, and what remains local host management
   outside the network protocol. Local mode must exercise the same authoritative rules; it
   must not become a privileged direct-call gameplay bypass. Do not require every local storage
   or lifecycle operation to be a remotely invokable command. Preserve validation and bounds.
   Do not force every mode into today's fleet representation. New co-op Working Harbour or
   exposing private-only capabilities remotely remains separate product work, not implied scope.
4. Compare queued in-process scheduling, threads, worker and process options. Begin with the
   simplest measured viable local composition; no topology is selected merely by this plan.
   Specify service order, queue capacities/backpressure, authority/client fairness, input-to-tick
   latency, update cadence and late-result policies. Preserve supplied clocks for tests. Threads
   require explicit ownership/synchronization/shutdown evidence; they are not a default fix for
   synchronous callbacks. Never add sleep/wait cycles inside authority tick or client frame.
5. Resolve private pause, focus loss/suspension, time scaling, restart and recovery separately
   from shared-room policies. Map real elapsed scheduling to simulation ticks explicitly;
   freeze/rebase local deadlines where required rather than allowing pause to trigger peer
   timeouts or catch-up bursts. Client menu pause cannot silently pause a shared authority.
   Retain existing supported behavior unless a change is explicitly selected and documented.
   The current server uses `now` for both peer timeouts and tick scheduling: merely scaling or
   freezing that argument would change operational deadlines. Separate scheduling inputs and
   client command/prediction clocks deliberately. Preserve private menu/help/focus pause and
   continued public-room simulation. Bound work across the complete host frame, not just each
   service call; repeatedly calling a bounded service must not create unbounded aggregate work.
6. Place course results, saves/resume, autosave, authored revisions and objective state on the
   authority side, with file I/O in a storage adapter outside core/net. Specify safe checkpoint
   capture and commit acknowledgements. Preserve personal save locations and bests. Pose ghosts
   remain presentation recordings; distinguish them from resumable authoritative state and
   P5g reconstruction records. Reuse P5g where appropriate without creating a competing format.
7. Select browser local-authority composition and mode support before migration. The client
   Wasm currently excludes Lua execution, while local authored objectives need an authority
   runtime. Assess authority orchestration with the existing isolated evaluator versus a
   separate authority Worker/Wasm and bounded messaging/lifecycle; do not
   silently put Lua in the rendering client or require a remote server for offline solo play.
   Existing T/T+1 objective gating is baseline behavior to preserve or explicitly revise, not
   proof that the current evaluator worker is a complete authority. Identify supported browser
   mode restrictions and required extensions without silently dropping current functionality.
8. Publish a migration ledger with mode, missing commands/state, execution topology, compatibility
   changes, prerequisite A1–A4 slices, fixtures, acceptance gates and removal point for the old
   production path. Record unresolved product choices for user decision; routine implementation
   details can be chosen from evidence. Avoid inventing defaults for pause/save semantics.

Done when the ownership/temporal contract, native/browser strategy, mode matrix and staged
migration ledger are reviewed and timing/compatibility gaps have named owners. This planning
gate does not implement a scheduler, worker, protocol extension or mode. A0's 472-test baseline
does not qualify these new contracts.

### A0c — Local authority/client pilot

After the prerequisite seams selected in A0b, migrate private native free boating first unless
the reviewed ledger identifies a better minimal slice. Start/stop one local authority and one
client through explicit bounded command/state exchange. Exercise bootstrapping, movement,
prediction/render views, reset, teardown and repeated sessions without external networking.
Keep the existing remote server path passing; reuse the authority rather than writing a second
local rule engine. Preserve input responsiveness and assess scheduling cost on named hardware.

Verify deterministic commands/state at the authority boundary against A0 traces, zero-delay
and impaired memory delivery, malformed/full queues, endpoint failure and release after partial
startup. Verify real offline startup and no stale commands crossing session generations. A
temporary rollout switch may support comparison, but A7 must remove parallel production paths.
Done when the pilot passes the reviewed temporal and behavior contracts; it does not qualify
voyages, cargo, persistence or browser authority by inference.

### A1 — Normalize application input and capabilities

**A1a: menu/pointer input.** Introduce a small input value for shared UI navigation, activation,
pointer position and pointer activation. Acquire platform input in selected implementations;
migrate menu/title/console consumers incrementally, starting with duplicated pointer/touch code.
Preserve event consumption, precedence, focus behavior and steering. Keep window, canvas and
low-resolution coordinate conversion explicit. Do not replace every raylib input call at once.
Produce UI actions and gameplay commands for the A0b client boundary; shared UI must not gain
new direct mutations of authoritative state. Keep predicted controls distinct from commits.

**A1b: capabilities and operations.** Express actual capabilities such as application-controlled
fullscreen and available pointer capture. Distinguish support from mutable state, such as capture
being active. Move relevant platform bridge functions into CMake-selected implementations;
keep gameplay decisions in common code. Do not add speculative capability fields.

Done when migrated shared UI code has no platform branches, menus expose the same actions,
and keyboard, mouse, touch, focus loss and capture retain their behavior.

Verify focused selection/row/input-consumption contracts, native windowed menus/console/
steering/fullscreen, and browser keyboard/mouse/touch layouts, resizing and focus transitions.

### A2 — Extract application platform services

Take separate slices with their existing lifecycles:

| Responsibility | Common contract | Implementation ownership |
| --- | --- | --- |
| Scenery acquisition | Start, progress, ready/failure, release | Browser fetch and native loading |
| Plugin import/export | Request, result, cancellation | Browser file/download APIs and native acquisition |
| Server discovery | Refresh, status, available entries | Existing native/browser directory clients |
| Application lifecycle | Startup, frame scheduling, shutdown | Native loop and browser integration |

Use explicit asynchronous states; do not force browser work into blocking native-shaped APIs.
Preserve existing feature availability. Keep network message delivery behind the transport
boundary and preserve the merged multiplayer interfaces for discovery/connection setup.

Done when migrated common code contains no platform bridge bodies and success, failure,
cancellation and repeated-session cleanup remain defined and verified.

### A3 — Separate plugin worker backends

Extract browser, Windows and POSIX worker implementations from `boat_plugin_stage.c`.
Keep common request/result handling, revision validity and lifecycle policy separate from
launch, polling, byte transfer, termination and backend resource cleanup. Preserve the public
staging API where practical, wire framing, limits and current platform-specific deadlines.
Keep staging and console queries consistent without forcing unrelated internals together.
The separate browser objective-evaluation bridge is governed by A0's explicit disposition;
if selected, preserve its pinned identity, staged-result commit and simulation-gating behavior
and run its existing objective-worker checks. Do not assume descriptor staging tests cover it.

The browser objective evaluator currently gates solo simulation after dispatch at T and before
T+1 while rendering/UI continue. Waiting time is discarded rather than converted into simulation
time or a catch-up burst. Preserve this transaction boundary, reset deferral, BUSY behavior and
failure/quarantine recovery. Its current five-second acceptance deadline limits result validity;
it does not promise completion, cancellation delivery or uninterrupted simulation within five
seconds. Removing the gate requires a separate approved behavior change. Recheck these baseline
details in A0 after multiplayer integration.

Done when common staging code contains no OS handles, process-launch calls or JavaScript
bridges, every close path releases backend resources, and stale/malformed/oversized/late
results retain their rejection behavior.

Verify existing staging/query validators, lifecycle cases with supplied time, browser worker
checks, and Windows/POSIX builds and runtime paths. Record unavailable platform coverage.
For the objective bridge, retain rejection of a successful result completed before expiry but
first accepted at/after expiry, using virtual time to test the exact acceptance boundary.

### A4 — Narrow plugin data and execution dependencies

Use A0's actual graph to establish these provisional responsibilities:

| Responsibility | Owns |
| --- | --- |
| Data/codecs | Runtime-independent values, encoding and decoding |
| Evaluation | Lua execution and evaluation-specific validation |
| Host | Revision ownership and transaction orchestration |
| I/O | Package acquisition from files |
| Staging | Isolated worker execution |

Extract only values/codecs required by data-only consumers. Make sandbox save depend on the
narrower contracts. Update catalog generation and native/browser consumers. Keep Lua-dependent
operations in execution targets and preserve the browser game's restriction against Lua
execution in its main Wasm. Inspect both target dependencies and linked symbols.
Enforce the data/codecs contract from A0 in this step, including a minimal data-only consumer
or equivalent link evidence and negative checks for forbidden runtime/I/O dependencies.

Done when data-only consumers do not require evaluation/workers, core/net remain independent
of plugin execution, formats and transaction behavior are unchanged, and targets have no cycles.
Split type extraction and target separation into smaller changes if needed for clear review.

### A5 — Move files into owning directories

Begin broad moves only after the earlier interfaces settle. Proposed layout (refine in A0):

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

Shared platform services belong in `platform`; application-specific platform glue belongs
with the application, and plugin worker implementations can stay with plugins. Keep adjacent
implementation/header pairs together. Allocate scenery and persistence explicitly rather than
letting ambiguous files fall into a miscellaneous directory.

Suggested move order: core and boundary inputs; rendering/scenery; plugins; sessions/persistence;
production application out of `tools`; networking after multiplayer is settled; validators into
`tests`, leaving developer utilities in `tools`. Reorder for active work, not aesthetic symmetry.

For each move preserve target/executable names and runtime asset paths; update includes,
scripts, packaging, CI and documentation links; adapt boundary checks without weakening them.
Done when a fresh build resolves all consumers and production/test/tool ownership is visible.

### A6 — Localize CMake and header access

Apply alongside each A5 move where practical, without unrelated build-policy changes:

- Give modules local `CMakeLists.txt` files. Keep root setup/options/composition readable and
  dependency/packaging helpers in `cmake`.
- Move test registration near its owners; preserve test names, labels, presets and coverage.
- Replace whole-`src` public include exposure with scoped paths. Use qualified cross-module
  includes where useful and keep implementation headers private.
- Carry forward the boundary checks introduced with each extraction, including indirect
  forbidden paths and private-header access; preserve source/symbol gates during moves.

Done when source additions normally touch their owning module, unrelated headers are not
inherited, boundary checks still reject deliberate violations, and quick/full lanes retain
their intended coverage. Add focused negative checks for rules that need enforcement, not
tests that merely restate file lists.

### A7 — Migrate modes and retire separate production sessions (before A5)

Session ownership is no longer an optional late assessment. Execute A0b's ledger as small,
reviewable slices after the local pilot and needed A1–A4 seams. Suggested groups are voyages/
tour/objectives; time trials/course/replays/bests; working harbour/cranes/cargo/jobs/save/resume;
and browser local authority. A0b must refine order from actual dependencies. Preserve hosted
room modes throughout. Authority-owned mode adapters may differ; reuse simulation and the
client boundary rather than forcing one monolithic session type.

For each mode prove equivalent authoritative outcomes from the same command stream, input and
presentation behavior, pause/scaling/restart policy, objective/revision behavior, persistence
compatibility and failure/overload recovery. Keep irreversible file effects outside speculative
client execution. Pure refactors retain byte/trace equality; identified protocol/behavior
extensions get separate expected-version/compatibility tests and explicit recorded decisions.

Before removing the old path, run its selected comparisons, current net regression suite and
affected native/browser manual acceptance. Remove superseded application branches, duplicate
progression/tick ownership, fallback switches and dead targets only after parity. Helpers used
by core validators, ghost playback or presentation are not automatically obsolete sessions.

Done when every shipped playable mode uses an authority/client composition and its ledger row
has evidence; remote and local paths share authoritative rule implementations; no independent
solo gameplay path remains. Missing platform/mode support blocks this gate unless the user
explicitly approves a scope/support change. Broad A5/A6 moves follow the settled ownership.
Full raylib independence remains deferred without a concrete consumer.

### A8 — Close the plan as the architecture specification

Rewrite this file as `ARCHITECTURE.md` describing the implemented modules, dependency graph,
public/private contracts, clocks, world/resource ownership, platform implementations and
boundary enforcement. Describe local/remote authority-client composition, migrated modes and
noninteractive viewer/tool exceptions. Include scheduling ownership, work/queue bounds, overload and late-result
policies, critical-path exceptions, and requirements versus measured qualification. Describe
the actual result rather than the provisional layout. Resolve
or explicitly defer remaining proposals, including every recommendation inherited from the
review. Remove `ARCHITECTURE_REVIEW.md` once its useful contracts are incorporated and its
remaining work has current owners. Update links across README/docs (including foundations,
boating, plugin and multiplayer plans) to the specification or the appropriate task owner;
preserve dated historical citations as such, using a pinned Git reference if needed. Update
the documentation index so neither this plan nor the review appears as a competing live
architecture document. Keep evidence/history distinct from the present-tense contract.

The local pilot and individual migrations are useful review points, not completion of the new
authority/client target. A7 is its required migration gate; A5/A6 finish the structural layout.
A8 documents the actual result. Any reduction in target scope requires an explicit user decision;
successful tests do not imply acceptance.

## Verification policy

For each implementation handoff, configure/build Debug and run `ctest --preset full`; use
`ctest --preset quick` during iteration. Follow [testing](TESTING.md) and [AGENTS.md](../AGENTS.md).
Use a fresh build directory for path/build restructuring. Do not replace the full lane with
an unrecorded subset, and distinguish disabled GPU tests from passing GPU verification.

When shared/browser targets or packaging change, run the supported web build from
[WEB_BUILD.md](WEB_BUILD.md), existing Wasm checks, and relevant browser checks. Example build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --preset full
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-web --target web --parallel
```

For application/render wiring, compare representative deterministic CPU captures before/after.
For affected simulation and serialization consumers, compare A0's selected physical/command
traces and protocol/save/replay/catalog bytes with the pre-change build under the same
configuration. Preserve expression, update and emission ordering. Reuse existing tools and
fixtures; this does not require cross-platform bit equality or blanket new tests. Fixture
changes, tolerance relaxation and revision bumps must not disguise an incompatible refactor;
handle intentional compatibility changes separately under the owning plan.
Authority migration must explicitly test private versus hosted pause/time policies, offline
startup, repeated local lifecycle, authority loss, save acknowledgement, bounded queue overload
and browser authority/client teardown. Compare command precision: adopting existing network
quantization or a different prediction policy may change handling/latency and requires an
explicit decision and evidence, not a claim of unchanged traces. Preserve existing room wire
bytes during pure refactors; version identified extensions under the protocol contract.
Inspect frames if visual effects change. Add windowed/manual checks when input, motion,
lifecycle, audio or asset loading changes. Exercise affected multiplayer joins, reconnects,
room modes and the new extension's behavior. Use virtual time in tests; wall time is only a
failure deadline. Preserve existing worker/browser timing semantics.

Use supplied virtual time to check exact deadline boundaries, late/stale result disposition,
queue saturation, overload, bounded catch-up and pause/resume without sleeping. These tests
establish temporal semantics and work-limit policies, not actual execution speed. Separately
qualify affected timing claims with representative native/browser measurements when a change
or unresolved concern warrants it, reusing current performance harnesses and applicable budgets.
Record workloads, build/options, hardware, sampling clock, tail latency, deadline misses,
worst observed stalls and unavailable coverage. Averages and successful functional checks alone
do not establish timely delivery; worst observed time is not a proven worst-case bound. Avoid
strict millisecond CI gates on variable shared runners. Browser suspension and OS scheduling
preclude universal completion guarantees. Check each harness's actual clock: existing plugin
benchmark elapsed measurements use UTC and can be affected by clock adjustment; do not label
all existing measurements monotonic.

Check documentation links and commands, CI paths, release packaging and generated catalogs
after moves. Use focused contract/failure tests rather than implementation-mirroring tests.
Record date, revision, platform/options, actual commands/results and any unverified behavior.
Do not infer user acceptance or cross-platform success from automated checks on one machine.

Documentation-only preparation requires link/command review, not a new audiovisual review.

## A0 merged baseline and contracts — 2026-11-09

Source: `1fb9950b7492a45eec813de309454628a6b78245` (PR #175). Assessment and artifacts are in
the separate architecture worktree; the main checkout and multiplayer worktrees were not
modified. No production sources changed. The pre-merge review above remains historical.

### Observed dependencies and P5 integration

An arrow means "depends on"; transitive dependencies follow the arrows:

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
and application-side `BoatRoomObjective` over `BoatNetRules`. Net invokes the adapter but
does not link Lua. Preserve the callback non-reentrancy rule: rules callbacks must not touch
the server. In each server tick preserve fleet step, match/race observation, mark events,
rules after_tick, objective publication, revision processing, then wake/snapshot delivery.
Objective callbacks run at 12-tick boundaries. Source reload stages a copied candidate;
definition revisions use server-owned staged/announced values and commit at the existing
safe boundaries. A4 must retain these application adapters rather than pulling execution into
net or relocating generic plugin values into multiplayer.

### Intended module contracts (effective before file moves)

These are permitted dependency directions, not claims of complete current enforcement.
This table records A0's pre-migration contracts with the user-selected direction now applied
to ownership below; A0b must refine the authority/client entries and timing gaps before extraction.
For every selected extraction, the named step closes its enforcement gap before handoff.
Public headers below are representative entry contracts; audit their full transitive includes
when touching them. Exact new filenames remain provisional.

| Responsibility / entry contracts | Permitted dependencies and ownership | Forbidden dependencies / enforcement |
| --- | --- | --- |
| Core: `boat.h`, `boat_world.h`, `boat_fleet.h`, definition/input/clock values | Math/types, compiled geometry, core modules. World resources/maps are non-copyable owners; copyable worlds borrow them. | No device/input/files/sockets/Lua/plugins. Preserve source and archive-symbol tests, including marks; do not bypass them with callback execution hidden in core. |
| Net: protocol/client/server/prediction and `boat_net_transport.h` | Core; byte/event transport and application-supplied rules callbacks with caller-owned contexts. | No transport OS APIs or plugin execution implementation. Existing combined core/net gates; A4/A6 add indirect dependency enforcement as targets split. Callback effects belong to adapter contracts. |
| Presentation: `boat_view.h`, scene/presentation values | Core/scenery; CPU and GPU own their caches/resources. Cosmetic updates cannot feed simulation. | No reverse core dependency. Preserve simulation parity with cosmetics disabled; A5/A6 enforce header ownership. |
| Plugin data/codecs: extracted value/state/descriptor interfaces | Core values and bounded buffer helpers/JSON where actually required; caller-owned bytes/results. | No Lua evaluation, filesystem, processes, browser bridges, rendering or input. A4 introduces source/symbol restrictions and a standalone consumer; do not re-export execution headers through data headers. |
| Plugin evaluation/host: `boat_plugin.h`, `boat_plugin_state.h`, `boat_plugin_host.h` | Data/codecs/core; evaluation uses private Lua. Host owns stable revisions; borrowers must rebind at commit. | No platform execution dependency in the extracted data layer. A4 resolves current combined headers; A3 isolates worker mechanics. |
| Package I/O and workers: `boat_plugin_io.h`, `boat_plugin_stage.h` | Evaluation/data and explicitly selected OS/browser backend; each operation owns handles and copied requests. | No OS/browser handles in common request policy. A3 backend selection and lifecycle tests; exact public/private include checks land with extraction. |
| Save/file: sandbox byte codec versus `boat_file.h`/work/replay persistence | Codec consumes data values only; authority coordinates committed checkpoint capture; storage adapters own paths/handles and atomic replacement. | Codec must not acquire files or execute plugins. A4 standalone consumer; A0b/A7 map replay, authoritative saves and client ghost viewing without a competing P5g format. |
| Authority/session/work/room adapters | Own committed simulation, rules/progression, objective/revision transactions and persistence coordination; compose core and runtime behind explicit commands/results. `BoatRoomObjective` is an existing authority adapter. | No renderer/input/device access inside authority rules; no shared mutable authority state with client. A0b defines contracts, A0c proves pilot, A7 migrates production modes; threads/processes remain topology choices. |
| Client composition | Owns controls, prediction, camera, UI/render/audio; consumes authoritative state and immutable resources with explicit lifetimes. | No authoritative progression or save effects from prediction; no direct authority mutation, reentrant endpoint delivery or parallel solo gameplay implementation. A1 supplies commands; A0c/A7 prove local and remote behavior. |
| App input/services | Normalized values, small capability/operation contracts, selected native/web backend. UI consumes one acquired input snapshot per frame. | No common UI-to-JavaScript/OS dependency for migrated functions. A1/A2 enforce bridge placement and event consumption with focused checks. |

A smaller include directory alone cannot enforce private headers. A1/A3 use backend-private
headers without exporting their directories and focused include checks for migrated consumers;
A4 applies that pattern to codec/runtime headers. A5/A6 then generalize public include layout
and retain those checks. Add only rules exercised by real consumers, not a second framework.

### Platform inventory and disposition

Inventory command: `rg -n '#.*(EMSCRIPTEN|_WIN32|__APPLE__|POSIX|__linux__)' src tools
--glob '*.[ch]'`, supplemented by CMake platform/feature branches and `WAKE_ACCELERATE` /
`BOAT_WAKE_PORTABLE` selection. Re-run before moving each group; this is a responsibility
inventory, not a requirement to remove every macro.

| Group | Current locations / disposition |
| --- | --- |
| Shared UI and browser bridges | `wake_velocity`, title, menu, console, plugin/net glue, touch helper. Select A1 input/capabilities and A2 services; preserve application policy. |
| Process/worker ownership | Plugin stage/worker, game host, console bridge, server stdin exit, Windows binary I/O. Stage backends selected in A3; host/bridge mechanics remain in their existing adapter files until an actual duplication justifies A2 extraction. |
| Files/assets/packages | `boat_file`, plugin I/O, replay, `boat_assets`. Windows filesystem/binary handling, Apple application-support/bundle paths and POSIX/Darwin declarations are legitimate adapter concerns. Preserve platform behavior; A4 separates codec/I/O dependencies, A5 assigns ownership. |
| Timing/diagnostics | Profile clock, frame pacer, performance process/memory sampling and GPU timer. Keep platform implementations; do not combine pacing, measurement and simulation clocks into one generic clock service. |
| Math/transport | Apple Accelerate versus portable legacy wake transforms; ENet Windows/POSIX sockets and Linux reuse-port exception. Keep backend-local selection and parity tests. No full math/transport rewrite is selected. |
| Audio/GPU/review tools | Browser audio teardown, GPU availability and native-only visual review tools. Appropriate backend/build restrictions; no blanket abstraction needed. |
| Build/tests | CMake Emscripten/native source selection, Linux Wayland patch, Apple framework, Windows libraries/compiler flags, optional RTC and GPU lanes; platform fixture setup in validators. Keep build selection and platform tests; A5/A6 preserve conditions and paths. |

### Timing, critical paths and inherited gaps

Each gap below has an owning step; no new numeric wall-time budget is asserted. Affected steps
must either close the gap with evidence or retain it explicitly with a reason and follow-up.
These rows describe the observed baseline. A0b additionally owns the local-authority clock,
pause/scaling, scheduling fairness and browser topology decisions; A7 must close their mode
migration gates rather than treating the current separate solo loop as the target design.

| Path | Existing limits / clock / disposition | Gap owner |
| --- | --- | --- |
| Solo frame/input/ticks | `boat_time_frame_seconds` clamps real delta to 0.1 s before scaling (up to 8x); fixed 120 Hz steps. Inactive/replay/objective waiting paths reset the accumulator. Preserve order and discarded-time accounting. | A1/A2: end-to-end input/frame deadline and maximum frame-work qualification absent. Do not turn variable work into unlimited queue drains. Competitive dropped-time validity stays deferred. |
| Server/client service | At most 128 transport messages/service; server at most eight catch-up ticks. Server flags overload after backlog exceeds half a second for more than one elapsed second. Input repeat six ticks, neutral after 30. Caller supplies elapsed `now`; authoritative state uses ticks. | A2/A4: per-message/callback cost is not a proven service-time bound. Preserve timeout, backpressure and overload behavior. |
| Native/authority objective | Synchronous evaluation, every 12 ticks; 64 queued objective events; overflow quarantines. Callback budget 100,000 instructions, migration 500,000. Reload/revision can initialize/migrate synchronously. | A4: compilation/C calls/allocation and callbacks can hitch; instruction budgets do not prove tick deadlines. No off-thread authority redesign selected. |
| Browser objective | Request copies source/checkpoint/events; pins epoch/revision/tick/digest. Poll rejects `now >= deadline` (five seconds), stages accepted update for the transaction boundary, closes job and quarantines failure. Simulation gates at T, UI/render continue; no wait-time catch-up. | A2 explicitly owns extracting this bridge with plugin import/export application glue, separately from A3. Preserve gate, identity and late-success rejection; qualify polling/decode/allocation cost rather than changing transaction policy. |
| Descriptor/query workers | Stage job owns backend resources and copies request. Native timeout five seconds, browser 15; elapsed `now` passed into start/poll. Close cancels/releases; stale receipts rejected. | A3: OS termination/reaping and result serialization/validation work need bounded-cost evidence; cancellation acceptance is not a guarantee of immediate OS/browser scheduling. |
| Persistence | Replay flush/work autosave run after ticks in the same frame; atomic replacement protects data. Allocation/copy and synchronous files may stall a frame. | A2 inventories service placement; A7 retains or separates only for a real consumer. Do not describe post-tick I/O as outside the frame budget or silently add threads. |
| Render/audio/resources | View-owned caches, bounded wakes/fleet; scenery loading and GPU rebuild/upload, audio update and present occur in frame/startup paths. Pacing waits are intentional. | A1/A2/A5 preserve ownership and startup/loading distinction. Frame latency, rebuild spikes and browser suspension qualification remain measured requirements, not assumed guarantees. |

Borrowed resources must outlive worlds, sessions, fleets and copies. Host revisions remain at
stable addresses until commit/rebind; room objective initial source is borrowed until accepted
reload installs an owned copy. Server revision installation rebinds the borrowed scene before
subsequent consumers use it. Preserve the existing tick-relative acceptance and client revision
readiness rules; late authoritative data is not subject to a generic discard policy.

### Historical review disposition after integration

Integer clocks, explicit resource owners, copied-world contracts, indexed map queries,
view-owned caches, static libraries and boundary gates are implemented in the merged source;
retain their existing validators. Shared scene evaluation exists, while the foundations ledger
still lists S1 frame consolidation and full S4 validator splitting as pending: their remaining
scope stays with that plan until a focused reconciliation, not a new implicit A1 prerequisite.
Foundations full P2/P3 workload qualification, mapped W1 residual work and S5 remain owned there.

Deferred triggers: general swept-contact/continuous collision work when a reproducible miss
requires it (boating foundations/adventure owners); importer surface semantics and coordinate
consolidation when importer/map work changes them (adventure/map owners); streaming/local origins
only with load/precision evidence; leaderboard dropped-time validity and revision-keyed bests
before competitive leaderboards (boating/race owners). Product questions remain with gameplay,
controls, audio and platform guides. P5g recordings and P5h/browser/impairment acceptance remain
with P5. This plan preserves their contracts and does not mark their work complete.

Windows CI already covers substantial CPU/plugin/net tests but explicitly selects targets and
test names; Linux/macOS and browser workflows have separate lanes. A5/A6 must update all of
them rather than assuming all validators run on every platform. The blanket asset copy target
still exists; making it incremental stays with build/performance follow-up if measured useful.
No dedicated sanitizer configuration was identified in root CMake/workflow setup during this
assessment; sanitizer recipes/qualification remain a foundations/testing follow-up before a
future claim of sanitizer coverage. No CI/platform or optimization change is authorized here.

### Baseline preservation selection

Artifacts live under ignored `build/a0-baseline/`. Keep the unchanged source revision and build
options available for regeneration; do not commit machine-specific generated output. A1 uses
CPU captures plus menu/input tests and later manual native/browser checks. A2/A3 retain current
virtual-time worker/authoring checks. A4 additionally compares protocol goldens, generated catalog,
save/replay bytes and deterministic physics traces, not just round trips.

Commands run from this worktree:

```sh
./build/boat_benchmark --scenario wake-loop --mode simulation --renderer none --warmup 120 --frames 1800 --presentation on --output build/a0-baseline/wake-on
./build/boat_benchmark --scenario wake-loop --mode simulation --renderer none --warmup 120 --frames 1800 --presentation off --output build/a0-baseline/wake-off
./build/validate_boat_net --case golden > build/a0-baseline/net-golden.txt
./build/wake_velocity --renderer cpu --screenshot build/a0-baseline/basin.png --time 3
./build/wake_velocity --renderer cpu --location outer --screenshot build/a0-baseline/outer.png --time 3
```

The on/off traces are byte-identical, final state hash `1335acbdcc23076e` (hash revision 2),
3,840 total ticks including warmup. This is one deterministic scenario, not broad wall-time
qualification. Both CPU captures were inspected: boat/wake visible, outer scenery present.
Baseline copies also select `generated/boat_plugin_catalog_data.c`, `work-save-tests/work.sav`
and `replay-tests/replay-fixture.run` from the full suite. Compare same-configuration bytes
when touched; don't silently normalize format fields. Build-revision strings in application
output need separate explicit treatment, not relaxed physics/codec comparisons.

## Current evidence and limitations

- Completed: read-only source review by Codex and Astra, agreed direction, and this plan.
- Astra xhigh plan review, 2026-11-08: direction sound; no fundamental contradiction or need
  to block other features. Integrated its recommendations for early module contracts and
  enforcement, asynchronous ownership/time, preservation evidence and discoverability. This
  was a source/document review, not implementation or runtime qualification.
- Astra xhigh real-time consultation, 2026-11-08: integrated firm real-time intent, timing/work
  contracts, critical-path inventory, objective gating and temporal verification requirements.
  Known baseline concerns for A0: trusted native objective evaluation remains synchronous;
  instruction limits do not interrupt compilation or every C-library call; post-tick saves can
  delay a frame; browser suspension can delay watchdog delivery. These are qualification gaps
  to reconcile, not claims already solved or permission to change behavior during extraction.
- Completed A0, 2026-11-09: refreshed from merged PR #175, assessed direct/transitive
  dependencies, platform responsibilities, ownership and critical paths, established intended
  contracts/enforcement owners, reconciled historical work and selected preservation fixtures.
- User decision and Astra xhigh consultation, 2026-11-09: every playable mode will use isolated
  authority/client composition, including offline single player. Added A0b design, A0c local
  pilot and mandatory A7 migration before directory moves; execution topology remains open.
  Astra highlighted operational versus simulation clocks, browser Lua isolation and explicit
  compatibility changes. This is planning/source review, not implementation qualification.
- Next: A0b authority/client design and migration contracts, before A1 input extraction.
- Deferred: production changes, concrete execution topology, full raylib independence, absent wall-time budgets and
  platform qualification gaps assigned in A0. No hard deadline guarantee is established.
- Current source baseline: `1fb9950`; initial review baseline `e25b47a` remains historical.
  The separate multiplayer worktree and main checkout were not modified.
- Preparation verification, 2026-11-08, macOS: local Markdown links resolve; command
  examples were checked against the current project workflow and web guide; whitespace checked.
- A0 verification, 2026-11-09, arm64/macOS 26.6.2, AppleClang 21.0.0: fresh Debug configure
  with `PROTEUS_GPU_TESTS=OFF`, existing pinned raylib/Lua/ENet source directories reused through
  `FETCHCONTENT_SOURCE_DIR_*` overrides; `cmake --build build -j 8` passed. Configure reported
  raylib's macOS OpenGL deprecation; the build emitted one third-party `stb_vorbis.c` pointer
  comparison warning, not a project-source warning. Log: `build/a0-build.log`.
- `ctest --preset full` initially failed 12 loopback/socket/process tests under sandbox
  restrictions (`build/a0-ctest.log`). The complete rerun with local socket/process access
  passed all 472 enabled tests in 41.27 s (`build/a0-ctest-desktop.log`); 13 retired/opt-in cases
  remained disabled. GPU tests were configured off, not verified.
- Baseline commands/artifacts above completed; two CPU frames inspected, on/off state traces
  identical. `build/a0-baseline/SHA256SUMS` records selected artifact hashes. Full CPU tests
  include protocol, room revisions/objectives, core/net boundary and virtual-time checks.
- Documentation links/whitespace checked on 2026-11-09. No browser build, Windows/Linux run,
  GPU/windowed input/audio playback, cross-machine play, new performance deadline qualification
  or user acceptance was performed. A0 records these as limits, not passing evidence; future
  affected extraction steps still require their platform/manual verification gates.
- Authority/client planning amendments were link/whitespace checked only; the A0 build/test
  results above qualify the unchanged baseline, not the proposed migration or new contracts.
