# Architecture restructuring plan

**Status, 2026-11-11:** AR0 complete at `1fb9950`; AR1 [design](ARCHITECTURE_AR1.md) and
[decisions D1–D10](ARCHITECTURE_AR1.md#product-decisions) merged (PR #177). AR2–AR6 milestone
(AR2a/b, AR3a–f, AR4, AR5, AR6 pilot) merged as PR #178. AR7's L6, L0's removal and L2 (time
trial on the wake pool and the local authority, [D11](ARCHITECTURE_AR1.md#product-decisions))
merged as PR #181 ([AR7 status](#ar7-status)). The user played both on macOS; nothing
browser-built or Windows-built.
**Open:** remaining manual native, browser and Windows checks per step; the menu-gated touch
tap (AR7); untested MIGRATE evaluation paths (AR3e); impaired-delivery qualification; pinned `saved_at` (L3).
**Done when:** every shipped mode runs as authority/client (AR7), modules sit in owning
directories with enforced boundaries (AR8/AR9), and this plan has become `docs/ARCHITECTURE.md`.
L1 (voyages, tours, objectives and authoring, L9 folded in) merged as PR #184
([L1 status](#l1b-status)). L7, L8 and L3 implemented on `arch/ar7-rest`
([L7/L8](#l7-l8-status), [L3](#l3-status), [L4a](#l4-status)). The user defers windowed checks
until every L* row is done.
L5 closed by evidence ([L5](#l5-decision)); L4b objectives implemented ([L4b](#l4b-status)).
The user accepted [L4-1, L4-2](#l4b-decision) and [L5-1](#l5-decision) on 2026-11-11.
**Next:** L4b authoring (L4-2 (b)): browser staging, migration and Undo on the authority.

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
   Classify delivery, acceptance and commit separately where their policies differ; state
   admission, cancellation, supersession and failure outcomes for obligations that must complete.
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
guarantee. Classify the relevant operation/stage, not an entire message or subsystem:

| Class | Meaning | Examples |
| --- | --- | --- |
| Firm | A result outside its acceptance deadline/window is rejected; lateness cannot justify applying it to finalized state. Recovery follows the operation's policy. | Browser objective result (`now >= deadline`), descriptor/query receipts, commands targeting an already finalized tick. |
| Soft | Missing a responsiveness target does not alone invalidate otherwise eligible work; lateness is measured. Freshness, identity and supersession checks still apply. | Frame presentation and input-to-tick response; delivery latency of a snapshot that is still eligible at acceptance. |
| Must-complete | An admitted obligation is not silently abandoned solely for lateness. Its contract requires completion or an explicit failure/cancellation/supersession outcome where permitted, not guaranteed success or unlimited retries. | Authoritative tick transaction, announced revision commit, admitted save attempt. |

Transport arrival is not application acceptance. A command can arrive after its target tick
and be rejected; a snapshot can arrive after a newer state and be ignored. Neither is made
usable by a soft delivery-latency target. State deadline comparisons and freshness rules per
stage rather than treating reliable transport as a completion guarantee.

For must-complete work, define admission, the point after which cancellation/replacement is
forbidden, allowed terminal outcomes, acknowledgement/diagnostics and retry policy. Proposed
or staged work may be refused or superseded under its contract; committed obligations cannot
be forgotten because a latency target was missed. Failure must not be reported as success.
Bounds or unresolved gaps still apply to each attempt and to retries; the label does not
authorize indefinite blocking, infinite retry, partial authoritative commits or crash durability.

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
- Separate delivery, acceptance and commit policies; for admitted obligations, permitted
  failure/cancellation/supersession outcomes, observability and bounded retry behavior.

Moving work outside the fixed-step loop does not remove it from the frame budget. Preserve
non-copyable resource owners, copyable borrowing worlds and stable revision addresses until
borrowers rebind. Preserve native/browser execution differences rather than forcing one timing
or blocking model into one API.

### AR1 — Authority/client architecture and migration design

Drafted 2026-11-09 in [the AR1 design](ARCHITECTURE_AR1.md); summary in
[AR1 authority/client design](#ar1-authorityclient-design--2026-11-09). Produce the design before
AR2 implementation; update AR0's intended contracts without rewriting
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
   capture, commit acknowledgements and failure/cancellation/supersession outcomes where permitted.
   Preserve personal save locations and bests. Pose ghosts
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

**AR2a status (2026-11-09, branch `arch/ar2-pilot`, not merged; nothing played).**
Implemented: `BoatAppInput` and `boat_app_input_sample(poll_touch)` in
`tools/wake_velocity_input.[ch]`, application-layer code outside core and net. It is sampled once
per frame at the start of the `study()` frame body and once per frame in `boat_title_run`. It
carries pressed navigation (up/down/left/right, confirm, activate, back, pause, time-scale
bracket and backslash keys), held Backspace, the pointer in
window pixels (position, delta, pressed, held; the web touch tap replaces position and press when
polled) and held helm keys (steer left/right, throttle up/down, precision). Consumers migrated:
the pause menu (`boat_menu_update` takes the value and reads no raylib input; its per-frame gate
is the pure `boat_menu_gate`, covered by `app_input`), the Escape/P menu toggle, the time-scale
bracket/backslash keys, the Backspace reset hold, the title's
navigation/pointer/turn reads, the game loop's keyboard steering, throttle, precision, native
mouse-steering delta and the replay timeline pointer. Scene-coordinate conversion stays in each
consumer. Deliberately unchanged: precedence and event consumption (the menu still runs only
when open and focused; `GetCharPressed`/`GetKeyPressed` text queues, the console, F-keys, camera
and replay keys, `M`, the work glue (`wake_velocity_work.c`), the net weapon reads in
`wake_velocity_net.c` (Space held for the gun, E and Q presses for grenade and torpedo), the
`GetCharPressed` time-key characters and the browser mobile/mouse bridges still read their
sources directly; AR6 owns moving the pausing/time-scale/restart consequences behind host
management). The web touch tap is consumed by `boat_app_input_poll_touch` only on frames where
`boat_menu_gate` holds, because the bridge's `click()` drains it; browser behaviour is not
verified. Open for AR7: touch drain depends on menu state (AR2b kept it; see its status). Not
done: turning the value into per-tick commands (AR6's client) and the remaining consumers above.
Verification (macOS arm64 Debug, 2026-11-09): `cmake --build build -j` warning-clean;
`ctest --preset full` 475 passed, 0 failed (includes the new quick `app_input`, which drives
`boat_menu_update` with synthetic values, and `boat_title_settings`); CPU captures
`--renderer cpu --screenshot ... --time 3` for basin and `--location outer` are byte-identical to
a build of the AR1 parent. Owed: manual native windowed menu, title, steering and replay
timeline check; browser keyboard/mouse/touch check.

**AR2b: capabilities and operations.** Express actual capabilities such as
application-controlled fullscreen and pointer capture, distinguishing support from mutable state
(capture active). Move platform bridge functions into CMake-selected implementations; keep
gameplay decisions in common code. No speculative capability fields.

Done when migrated shared UI code has no platform branches, menus expose the same actions, and
keyboard, mouse, touch, focus loss and capture keep their behavior. Verify focused
selection/row/input-consumption contracts, native windowed menus/console/steering/fullscreen,
and browser keyboard/mouse/touch layouts, resizing and focus transitions.

#### AR2b status

2026-11-10, branch `arch/ar2-pilot`, three commits on `f1ed07b`; reviewed 2026-11-10; not merged,
played or browser-built (emcc was not installed).

Implemented: the 14 inline browser bridges left in `wake_velocity.c` moved, bodies unchanged,
into CMake-selected backends behind C wrappers (`wake_js_*` imports, called only in their file):

| Capability | Contract | Native | Browser |
| --- | --- | --- | --- |
| Viewport, `wake_app_viewport` (app service) | The logical frame size the page settled on, read at the top of each frame | False: the frame does not follow the window | `Module.boatViewportSize`; the common resize path reallocates targets when it changes |
| Touch controls, `wake_touch_*` in `wake_velocity_controls.h` | Session start/stop; MENU press; held buttons (bits) and tilt target; button height; presentation state, whose bits keep fullscreen support (8) apart from fullscreen active (16); hit rectangles; status line; tap | None: 0, NAN, 26, empty | `Module.boatMobile` |
| Pointer capture, `wake_mouse_*` | Toggle press; capture request returning active (1), released by the platform (2), denied or unsupported (4); steering movement | M; cursor disabled/enabled when the request changes, always granted; the frame's pointer delta | `Module.boatMouse`; movement also drains the scroll wheel |
| Fullscreen key, `wake_fullscreen_key_pressed` | The application's fullscreen toggle key | F11 | False: the browser keeps F11 |

`study()` now runs one code path for viewport resizing, the touch drawing and hit layout, the MENU
button, steering inputs, capture fallback and its hint, and the cleanup; `draw_touch_controls`
returns at once natively (state 0). Gameplay decisions (when controls drive, the keyboard
fallback, opening the menu on a platform release) stay in `study()`. `wake_velocity_input.c`
reads the tap through `wake_touch_tap`; `tools/boat_web_touch.h` is gone. The call order is the
same on both builds: viewport, input sample, MENU button, the gated tap, toggle, touch sample,
target and height, capture (and its second request), movement and scroll, touch drawing; the
session starts before the first frame and the cleanup releases capture, then the session. Audio
unlock and the first frame stay with the app service (AR3f).

Enforcement: `platform_bridges` drops `wake_velocity.c` from the legacy list (14 → 0; the stage
worker's 1 remains) and forbids it the page objects, the emscripten header, cursor capture and
the F11/M key reads; `wake_velocity_input.c` may not use inline JavaScript or `Module`. A
self-test case covers the cursor rule.

Decisions: touch, mouse, viewport and F11 are capabilities over the existing values, not new
`BoatAppInput` fields, since their reads are ordered within the frame (each page call consumes
or samples state) and moving them to the frame-start sample would change that order. No
speculative fields: native returns the values the old `#else` paths used. The tap stays drained
only on `boat_menu_gate` frames: the page keeps a tap while the game is inactive and drops it on
active touch-sample frames, so draining every frame would drop taps that today select a menu row
after help closes or focus returns. That is a behaviour change for AR7 (the mode migration)
or the user, not AR2b.
Remaining `__EMSCRIPTEN__` branches in `study()` (the browser's room-leave row, its plugin
controls, the native tool bridge and room host) select policy or native-only adapters, not
bridges.

Evidence (macOS 26.6.2, Apple M1 Max, AppleClang 21.0.0, Debug, 2026-11-10): each of the three
commits builds warning-clean and passes `platform_bridges` and its self-test; a script that extracts
every `EM_JS(...)` from `f1ed07b:tools/wake_velocity.c` and from the new files finds all 14
byte-identical apart from the renamed import, none left in `wake_velocity.c`, and the app backend's
existing import unchanged; the stub `cc -fsyntax-only -D__EMSCRIPTEN__` check passes for every
`*_web.c`, `wake_velocity.c`, the title, input, console and plugin glue; basin and outer CPU
captures are byte-identical to an `f1ed07b` build; `ctest --preset full`: 508/508 passed (13
disabled) in 40 s.

Owed (manual). Native windowed: F11 toggles borderless fullscreen in a session; M toggles mouse
steering, the cursor hides while captured and returns on the menu, on M again and on leaving;
mouse steering still turns the boat. Desktop browser: resizing the window re-fits the frame;
M and a canvas click capture the pointer, Escape releases it and opens the menu with mouse mode
kept, a denied capture shows MOUSE UNAVAILABLE and falls back to the keyboard; scrolling does
nothing; F11 is the browser's. Phone browser: the touch buttons draw and drive (forward, reverse,
left, right), MENU opens the menu and a tap selects a row, tilt and recenter work, FULLSCREEN
toggles and its label follows, the status line shows, rotation re-lays the buttons, and leaving
then starting another session leaves no held control.

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

#### AR3 status

2026-11-10, branch `arch/ar2-pilot`, not merged; nothing played; no browser build (emcc was not
installed).

Implemented (AR3a–AR3f; AR3e is the objective seam under Decisions). Each service is a header
in `tools/` with a native `.c` and a browser `_web.c` that CMake selects for `wake_velocity`; no
call waits, and state keeps its previous owner (free functions, rule 10):

| Service | Contract | Native | Browser | Migrated consumers |
| --- | --- | --- | --- | --- |
| Server discovery, `wake_velocity_servers.h` | Start/stop once per process; refresh; poll each title frame; count, compatibility, entry text, status; LAN open/query/receive/close (the title keeps its hint text) | `boat_directory`'s background client; ENet LAN broadcast | `Module.boatServers` page fetch; LAN open returns NULL | Title directory and LAN calls; `main` start/stop |
| Scenery acquisition, `wake_velocity_scenery.h` | Startup; request; poll: pending with progress, ready or failed. Release stays `boat_scenery_unload` after every view | Every pack read at startup; request/poll ready at once | `Module.boatScenery` fetch; the caller's loading frame yields between polls | `scenery_ready` (the loading screen stays common) and startup |
| Browser plugin bridges, `wake_velocity_plugin_web.h` | Authoring panel owner, editing and actions; package export; reset/menu controls; objective evaluator start/status/read/close/shutdown/checkpoint | None: native evaluates synchronously and saves package directories | EM_JS bodies private to the backend, behind C wrappers | `wake_velocity_plugin.c`; console export and controls in `wake_velocity.c` |
| Application lifecycle, `wake_velocity_app.h` | Process clock; window flags; title open/close; canvas scale and refit; session canvas; session open | `GetTime`; hidden, or resizable with vsync; nothing; resolution scale; nothing; nothing; `--no-vsync` clears vsync | `performance.now`; no flags (the page sizes the canvas); 60 fps and audio inside the Play gesture, audio closed after; mobile-aware scale and `boatFitCanvas`; grow a smaller canvas; 60 fps | Plugin clock, both `SetConfigFlags`, title start/stop; `study()` window open and the resize path's scale and refit |
| Scenery deferral, `wake_scenery_deferred` (scenery service) | Whether acquisition waits for the window | False | True: the first CPU draw and its capture wait for `scenery_ready` | `study()` |
| Console bridge, `wake_console_bridge.h` | Transcript sync; closed; clipboard step; queued completions; whether Tab completes; text-entry availability; page state | Nothing; nothing; Ctrl/Cmd+C/V through raylib; nothing; yes; yes; nothing | Page transcript; drop queued Tab and paste; page paste and clipboard notice; page Tab queue; no; physical keyboard only; `Module.boatConsole*` | `wake_console.c`; console availability and state in `study()` |

Enforcement: `platform_bridges` (quick) runs `tools/check_platform_bridges.py`. Inline JavaScript
lives only in `*_web.c` backends or in listed legacy files at exactly their recorded counts
(`wake_velocity.c` had 14 for AR2b touch, mouse and viewport capabilities, now 0 and delisted; the
native stage worker `tools/boat_plugin_stage_worker.c` 1; AR4's merge brought
`src/boat_plugin_stage.c` to 0 and dropped its entry). The title, game, console and plugin glue may
not reach a service's backends directly, and `CMakeLists.txt` builds `*_web.c` only in EMSCRIPTEN
branches or in the native test targets the check allowlists with a reason
(`validate_boat_plugin_stage_web`, which compiles the browser staging backend against a C model of
its page bridges). The CMake parser reads only the top-level file and single-line `if(...)` forms.
`platform_bridges_detects` is its negative control. The title validator now links the protocol, not
the ENet transport.

Decisions (technical, recorded here):

- Only bridge bodies moved for the objective evaluator. Its request copy and pinned identity,
  stale and `now >= deadline` checks, staged commit, BUSY, reset deferral, quarantine and the
  T/T+1 gate are unchanged in `wake_velocity_plugin.c` under the browser selection.
- AR3e (after the AR4 merge): the browser objective branches in `wake_velocity_plugin.c` are
  selected by `WAKE_PLUGIN_ISOLATED_OBJECTIVE` (1 under `__EMSCRIPTEN__`, else 0) instead of
  `__EMSCRIPTEN__`, so both builds compile exactly what they did. `validate_wake_plugin_objective`
  compiles the file with it set natively over a C model of `wake_velocity_plugin_web.h` and
  evaluates requests with `boat_plugin_eval_execute`. Its cases (`wake_plugin_objective.*`,
  quick) check a reply first accepted exactly at its 5 s deadline (rejected, quarantined) and
  just before it (accepted, staged at T and published at T+1), stale and failed receipts, BUSY
  controls, reset closing the in-flight job (a late reply is never read) and close releasing
  every job over repeated sessions. Page bridges other than the objective's stay
  `__EMSCRIPTEN__`, and the model is not the page: `web/plugin_objective.js` stays untested here.
  The test opens `plugins/shelter-tour` once with its file watch off, so no frame reads the
  package directory. Open: the MIGRATE evaluation paths (begin, accept and commit, cancel
  through page actions 2 or 8) compile under the seam but have no case, since starting one
  needs a staged revision with a changed objective.
- EM_JS imports are called only inside their backend file, through C wrappers. Game and tools
  bridges are named `wake_js_*`; the plugin stage backend (`src/boat_plugin_stage_web.c`, AR4)
  keeps its `browser_*` names, since renaming them would be churn.
- AR3f keeps the browser's ordering: canvas growth before the UI loads, then 60 fps, the scenery
  wait and the deferred first CPU draw; the console's paste and notice before typed text and its
  queued Tab after it. Native now calls `IsWindowFocused()` for the console state call, which
  does nothing there. The console shader's GLSL version line stays a renderer branch.
- Review fixes (2026-11-10): exact legacy counts, wider bridge patterns, string-aware comment
  stripping, the CMake check, one source for the LAN hint, `wake_js_` names, and the clock
  comment (a direct native `--plugin` launch stamps its first request before `InitWindow`).
- Browser row layout and text, the default `ws://` address, touch, mouse, viewport and keyboard
  bridges and native F11 are capabilities (AR2b), not services; AR2b has since moved the
  touch, mouse, viewport and F11 ones ([status](#ar2b-status)).

Persistence placement (the AR0 persistence row), unchanged by AR3; L2/L3 move these writes behind
authority storage requests:

| Write | When | Adapter today |
| --- | --- | --- |
| Course best/last replays (`boat_session_flush`, `boat_replay_save`) | After the frame's ticks; at session end | Temporary file and rename in the data directory; browser `/saves` IDBFS with `autoPersist` (`web/runtime.js`) |
| Work autosave and leave-time save (`wake_work_save_frame`, `wake_work_save_leave`) | After the frame; on quit or title return | `boat_work_save` over `boat_file_write_atomic` (native only) |
| Title settings (`boat_title_save`) | Each title choice and at exit | Plain `fopen` write in the data directory |
| Plugin package (`wake_plugin_save`) or browser export | Console `save` | Temporary file and rename; browser download |
| Performance report (F4), screenshots | On request | `wake_performance_export`, `ExportImage` |

Left outside AR3:

- AR2b, not AR3: the per-frame viewport bridge, mobile session start/stop, touch and mouse
  bridges and native F11 remained in `study()` (moved by AR2b since).
- Native-only adapters keep their files (AR0): `--host` child process, `--plugin-bridge`,
  `--serve`, worker entry points.

Evidence (macOS 26.6.2, Apple M1 Max, AppleClang 21.0.0, Debug):

- `cmake --build build -j` warning-clean; `ctest --preset full`: see
  [current evidence](#current-evidence-and-limitations).
- CPU captures (`--renderer cpu --screenshot ... --time 3`, basin and `--location outer`) are
  byte-identical to a build of the parent `37316a4`. The title capture needs a display and was
  not run.
- Browser sources (`*_web.c`, title, game and plugin glue) pass `cc -fsyntax-only -Wall -Wextra
  -D__EMSCRIPTEN__` against a stub `emscripten.h` that drops the JavaScript (scratch harness,
  not committed); a call with wrong arguments fails it. This is not an Emscripten build.
- Every AR3 commit builds natively warning-clean and passes the syntax check and
  `platform_bridges` (`git rebase -x` over the range).
- After the AR4/AR5 merge (2026-11-10, same machine): `ctest --preset full` and again with
  `-j8` each passed 508/508 (13 disabled); every first-parent commit after the merge builds
  warning-clean and passes the lint, plugin staging, objective and save tests (the merge passes
  `platform_bridges` since its resolution was rewritten to carry the legacy-entry removal and
  the native-test allowlist; the trees after it are unchanged); basin and outer CPU captures
  are byte-identical to the pre-merge `8b6be48` build; the stub syntax check passes for every
  `*_web.c`, title, game, console, plugin glue and staging source.

Owed to the user. Native: the title's MULTIPLAYER panel lists public servers (or the directory
status) and finds a LAN room hosted from a second instance; joining both works; leaving and
reopening the panel searches again. `--location outer` and the outer title entry load
scenery; settings persist across launches; a plugin console `save` writes the package. Browser:
the title plays music after Play; the outer location shows LOADING with progress, then plays;
the public server row and join work; authoring STAGE/APPLY/UNDO, console export download and an
objective package (HUD updates, BUSY while evaluating) behave as before; fullscreen and
resizing unchanged. AR3f, native: in a plugin session, open the console (`` ` `` or F2); Tab
completes and Shift+Tab reverses; Ctrl/Cmd+C copies the latest output (Shift: scrollback) and
Ctrl/Cmd+V pastes; Esc closes; `--no-vsync` still paces at 60 Hz. AR3f, browser: on desktop the
console opens, paste, Tab and the copy notice work, and closing drops queued input; on a
touch-only phone no console opens; a high-resolution and a tall mobile session size the canvas
as before; an outer session shows LOADING, then its first frame.

User report, 2026-11-10, macOS, merged `main` (PR #178): the game works; the title's
MULTIPLAYER panel, LAN join, settings persistence, steering and the menu work. The plugin
console `save` was not tested. The other native items above and every browser item stay owed.

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

**AR4 status, 2026-11-10 (branch `arch/ar4-plugin-workers`, for the AR2–AR6 milestone PR):**
implemented; not played or browser-tested. `boat_plugin_stage.c` keeps framing, limits, deadlines
and revision validity; `boat_plugin_stage_{posix,windows,web}.c` implement the private
`boat_plugin_stage_backend.h` (launch, poll, byte transfer, termination), selected in CMake.
Platform policy differences (5 s/15 s deadline, executable, browser launch receipt, solo-only
kinds, browser query deadline message) are backend traits, not `#if` in common code. The public
staging API, wire and limits are unchanged. Two browser fixes, in their own commit: close now
releases every transport job (before, after two failed, unreadable or page-expired console
queries the page refused all further staging and queries until a reload), and query replies
are read into a buffer sized to the 160 KiB bridge limit (it was 12 KiB).
Evidence (macOS arm64, AppleClang, Debug): warning-free build; `ctest --preset full` passed
477/477 (13 disabled), including the new `plugin_stage_backend` include check and query
lifecycle cases in `boat_plugin_stage` (exact 5 s virtual deadline, cancel, faults), its
self-test, `boat_plugin_stage_web` (browser backend against a C model of the page transport)
and the Node `web_plugin_author` transport test.
**Open:** Emscripten and Windows builds and the `tools/package_web.py` browser checks
(`check_web_plugin_author.py`, `check_web_plugin_objective.py`, `check_web_wasm.py`) were not run
(no emcc or Windows here); OS termination/reaping still blocks in close without bounded-cost
evidence; manual native/browser reload, Undo and Harbor life default checks owed.

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

**AR5 status, 2026-11-10 (branch `arch/ar4-plugin-workers`, for the AR2–AR6 milestone PR):**
implemented; not played or browser-tested. `proteus_plugin_data` (manifest parsing, decode and
location checks, digests and classification in `boat_plugin.c`/`boat_plugin_race.c`, the stage
wire codec, and the objective checkpoint codec and digest; core and JSON only) is split out of
`proteus_plugins`, which keeps Lua evaluation (`boat_plugin_load.c` now holds the five
`boat_plugin_load_*` calls), the host, objective transitions, package I/O and staging and links
the data library publicly. Data headers `boat_plugin_decode.h`, `boat_plugin_revision.h` and
`boat_plugin_objective.h`, plus the evaluation header `boat_plugin_load.h`, take declarations
moved verbatim from the combined headers. The sandbox save and the bundled catalog link only
plugin data; the work session, which steps objectives, links the host explicitly. The dependency
chain is acyclic: data -> core/JSON; plugins -> data + Lua; catalog/sandbox save -> data.
Enforcement: `plugin_data_boundary` (sources, CMake links and archive symbols), its slow-lane
self-test, and the data-only consumer `validate_boat_plugin_data`. Net still includes no plugin
header, and `BoatRoomObjective` stays in `tools/`.
Evidence (macOS arm64, AppleClang, Debug): warning-free build; `ctest --preset full` passed
480/480 (13 disabled); under `git rebase -x` every commit built warning-free and passed the
plugin, boundary and lint tests at `-j8`, except one unidentified failure on the final docs-only
commit that did not recur in six reruns (the full preset on the same tree passed). Against the
pre-AR5 build, the generated catalog source, `net-golden` output and every save fixture are
byte-identical except `sandbox-save-tests/resume.sav` and `work-save-tests/work.sav`. Those
differ between two runs of the same build (already on `main`) only in the wall-clock `saved_at`
field (offset 103) and the checksums over it, so they are not a byte-equal gate until a pinned
`saved_at` exists (open item owned by L3, whose byte-equal-save gate it blocks).
`cargo_sandbox_save_rejected` failed under `ctest --repeat`: it set `stale.sav` aside as
`.rejected` and the repeat skipped the fixture setup that writes it. The merge into
`arch/ar2-pilot` fixes this: `--expect rejected` moves the file back after the check.
Review follow-ups at the merge: `plugin_data_boundary` compares its file list with the
`proteus_plugin_data` CMake sources, its self-test drives the `--nm` path through a stand-in nm,
and it nm-checks the sandbox save and catalog archives as consumers.
**Open:** browser game Wasm still links `proteus_plugins` (the host and worker bridge) and relies
on the linker dropping Lua; `check_web_wasm.py`, the Emscripten build and Windows were not run.
Header narrowing is limited to data-only consumers: `boat_plugin_host.h` still includes I/O and
state headers for its callers. No physics trace comparison was run (no simulation code changed).

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

#### AR6 status

2026-11-09, branch `arch/ar2-pilot`, not merged; nothing played.

Implemented (ledger row L0):

- `src/boat_local.[ch]` (`proteus_boat_net`): `BoatLocal` owns one `BoatNetServer`, one
  predicting `BoatNetClient` and an unimpaired memory transport in the game's thread. Start runs
  the handshake in bounded service rounds (3 in practice, at most 8) at tick 0; a frame's grant
  runs in chunks of at most 12 ticks ending on multiples of 12 (at most 9 per frame): the client
  commands each tick, one batch is flushed, the authority runs the chunk, the client handles the
  results. Restart recreates both endpoints with the next generation (the authority's epoch).
- Private schedule: `boat_net_server_grant` runs exactly the granted ticks with no peer timeout,
  catch-up or overload and refills the input and spawn buckets per granted tick;
  `boat_net_client_command`/`flush`/`local_service` address commands to explicit ticks, derive
  target rates from the predicted boat as solo does, and count stall resync (120 granted ticks
  without a snapshot), wake-mismatch resync (120) and the prediction hold (12) in granted ticks,
  with no peer timeout, snapshot-silence close or ping. Hosted service is unchanged.
- Full-precision commands (D4): in-memory float rates on `BoatNetCommand`, carried by the
  private range's PrivateInputBatch (`src/boat_net_private.[ch]`, type 0xC0, rejected by hosted
  rooms). The v14 codec and golden bytes are unchanged.
- Fleet per-slot handling (`BoatFleet.handling`, NULL for the speedboat in every room).
- Game (`tools/wake_velocity_local.[ch]`, native only): free packages without a voyage, objective
  or authoring (in practice basin free boating) start a local session; the solo accumulator's
  ticks become the grant (at most 96 per frame; `boat_local_frame` refuses more and the glue
  carries any excess in the accumulator); the client's prediction fills the game's `BoatSession`
  boat, clock, presentation and arcade pool. Presentation steps per predicted tick against the
  pool the boat read (the previous tick's pool advanced once, before the tick's emission), as
  `boat_session_step` does. A restart recreates both endpoints; an endpoint failure returns to the
  title with its reason (D10; free boating has no save). `--direct-session` (D8) was parsed only
  inside `#ifndef NDEBUG`; [L6](#ar7-status) removed it with the direct free-boating path.
- Repeated failure is endpoint failure (§5): a private client that is still stalled (snapshot or
  wake mismatch) when a fourth resync falls due, after `BOAT_NET_LOCAL_RESYNCS` = 3 resyncs with
  no clean state between them, closes with its reason (about 480 granted ticks). The in-process
  link is lossless, so one checkpoint repairs a stall; three allow for a checkpoint superseded by
  a restart or a second mismatch. The authority serves a private resync once per 120 granted
  ticks, the client's clock (elapsed time would refuse its retries at 8×). Hosted rooms keep
  elapsed time and their clients are unchanged.

Deviations from the [AR1 design](ARCHITECTURE_AR1.md):

- Handling class, current/crossing water, the wake pool recipe and the spawned boat reach both
  endpoints as host-supplied creation config (`BoatNetPrivateSession`), not private-range
  messages; a restart is endpoint recreation, so no message is needed for them. Only the
  full-precision command uses the private range.
- The player's boat is placed when the authority accepts the client, before the bootstrap
  checkpoint, instead of by a spawn request after READY, so the first granted tick is commanded.
- The resync rate limit counts granted ticks (one resync per 120) at both ends of a private
  session, not elapsed time; hosted rooms keep elapsed time. The private client sends no ping.
- A command batch dropped by a full unreliable queue is an endpoint failure (the host compares
  the transport's dropped count around each flush), not a repeated command.
- Outer: every shipped outer solo session is a voyage package, so L0 reaches the game on the
  basin only; outer with current and crossing water is qualified in CTest, and outer play
  migrates with L1.
- Presentation is stepped while the client takes each predicted tick (the observer inside the
  chunk), not at §2 stage 7 after the chunk's intake. It reads the same pool and boat as stage 7
  would, so the result is identical; moving it would need a per-tick history of the prediction.
- Not done: impaired memory delivery for private sessions (T1 selects none; hosted impairment
  stays in `boat_net`), browser private sessions (L4), the objective-gated stop (no objective in
  L0), real offline startup and the native windowed manual check.

Evidence (macOS 26.6.2, Apple M1 Max, AppleClang 21.0.0; virtual frame times, no wall clock):

- `boat_local` (17 quick cases, each under 100 ms on Debug) drives the game's own glue
  (`proteus_local_drive`, the library `wake_velocity` links) and compares every predicted tick's
  boat, `BoatPresentation` and pool digest, and every frame's authority tick, bit for bit with the
  game's direct path (`timed_boat_step`): basin at ¼×, 1× and
  8×; outer with waves, current and crossing at 1× and 8×; `G = 96` with 0.25 s stalls clamped at
  8× (9 chunks, a partial first chunk); uneven frames (odd remainders, partial first chunks);
  explorer handling; 30 s of pause; restart with stale old-generation traffic; 100 repeated
  sessions; startup failure and release; queue saturation; the private range in a hosted room;
  the 96-tick grant cap; an injected wake mismatch that one resync repairs, then one whose
  checkpoints the client's transport loses, which fails the session after three served resyncs;
  two quick mismatches at 8× with an advancing host clock, both repaired (fails if the authority
  rate-limits on elapsed time).
  Each drive has zero rate-limited, late, future or duplicate commands and every tick integrates
  a received command. Negative checks during development: quantizing the commands at the
  authority fails `basin_1x` and `outer_8x`; presenting against the session's stale pool fails
  `basin_1x`'s per-tick presentation check. Presenting against the post-emission pool passes:
  a newborn wake object contributes nothing at its birth tick (its birth envelope is zero).
- Debug build warning-clean; `ctest --preset full`: see [current evidence](#current-evidence-and-limitations).
- CPU captures (`--renderer cpu --screenshot ... --time 3`, basin and `--location outer`) are
  byte-identical to an `e1b1175` build (main's tree): captures drove the direct scripted path
  before the frame loop (L6 has since migrated basin captures; see [AR7 status](#ar7-status)).
- Cost, Release `validate_boat_local --bench` (processor time via `clock()`, 1,200 frames each,
  outer with waves, current and crossing, through the game glue with presentation): 16-tick
  frames at 8× mean 1.66 ms (worst 2.13 ms) against 1.40 ms (worst 1.84 ms) for the direct path
  with presentation; `G = 96` frames mean 8.48 ms (worst 15.6 ms) against 6.95 ms (worst
  12.2 ms) direct.
- The Windows CI lane builds and runs `boat_local`; not yet run on CI (no push).
- Hidden-window smoke run (`--renderer cpu --frames 240 --performance-report`): the local
  session advanced 737 ticks in 280 chunks with no failure. Not a manual check.

Owed to the user (native windowed): steering/throttle feel with keyboard, mouse and Shift; menu,
help and focus-loss pause; time scale `[`/`]`/`\` up to 8×; Backspace reset and menu restart
(repeatedly); skins (explorer, cabin, Grodan); quit to title. The browser keeps the direct path
(L4), so a browser smoke check only confirms nothing changed there.

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

#### AR7 status

2026-11-10, branch `arch/l6-scripted-drives` (from `main` after PR #178); merged as PR #181.

L6 (scripted demos and captures) and L0's removal point, implemented:

- Scripted command source: `boat_local_frame_scripted` (`src/boat_local.[ch]`) takes each tick's
  helm from a `BoatLocalScript` callback given the predicted boat the authority will step; its
  rates are sent as they are. `demo_input` (`tools/wake_velocity_drive.[ch]`) is the scripted
  drive's helm, shared by `demo_step` and the glue's `wake_local_scripted_frame` (`--demo` in
  the frame loop) and `wake_local_scripted_drive` (a capture's drive before the window, in grants
  of at most 96 on virtual time from 0).
- The game: a `--screenshot` or `--demo` of plain free boating (the `LOCAL_ELIGIBLE` sessions,
  no longer excluding demos) runs through the local session. With a local session active the
  frame loop's direct tick loop is skipped, so its ticks never fall back to `timed_boat_step`
  (L0's removal point); a grant above 96 waits in the accumulator. The D8 switch
  `--direct-session` is gone.
- Unchanged and still direct at L6: outer captures and demos (every outer solo session is a
  voyage, so L1), the developer studies (wake field, crossing), courses (since moved by L2),
  `--work`, rooms and the browser (L4). `demo_step` remains for them and for `boat_benchmark` and
  `validate_boat_clock`.

Evidence (macOS 26.6.2, Apple M1 Max, AppleClang 21.0.0, Debug, 2026-11-10):

- `boat_local` gains `scripted_demo` and `scripted_capture` (slow lane, about 0.5 s each) and
  `scripted_needs_script` (quick); per-tick boat, presentation and pool equal `demo_step`'s past
  the helm changes at ticks 420 and 1440. Negative check during development: indexing the script
  by the produced tick instead of the tick before it fails both drives.
- Basin captures at `--time` 3, 10, 15 and 20 (the last three cross both helm changes) and the
  outer capture at 3 are byte-identical to a build of `main` (`3080e4c`); the basin run prints
  `Local session: generation 1, 360 ticks in 30 chunks`. All 57 PNGs the full preset writes match
  that build's except `wake-work-resumed.png`, which also differs between two runs of the same
  build (working-harbour resume, direct path; not investigated here).
- `ctest --preset full -j8`: see [current evidence](#current-evidence-and-limitations).

User report, 2026-11-10, macOS: a native windowed `./build/wake_velocity --demo` on the basin
(now through the local session) works. Owed: the L0 manual items still open from AR6. The check
ran L6's glue; it predates L2's change to the glue's frame `run()` (practice forwarding, course
sync and the script seam).

L2 (time trial, runs, bests and ghosts), implemented 2026-11-10 on the same branch:

- Product change first (user decision, 2026-11-10: "Switch the time trial to the wake pool"):
  `--course`, `--ghost` and course packages ride the arcade wake pool like free boating instead
  of plain water. Bests move to `course-{calm|waves}-physicsN-wakes.run`; older plain-water bests
  are left on disk and never selected. `BOAT_PHYSICS_REVISION` is unchanged (bumping it would
  break hosted Hello compatibility and every replay file). Recorded as
  [D11](ARCHITECTURE_AR1.md#product-decisions), confirmed by the user on 2026-11-10. The real
  effect is the player's own wake under the hull and the jumps it causes; how much it changes a
  human lap is unknown. The test autopilot's lap drop (57.80 → about 41.8 s) is an artifact
  (independent review, 2026-11-10): it circles every gate (about 1363° of turning over 526 m on a
  course about 145 m long), and crossing its own wakes (2.6 s airborne, 3 landings) knocks it
  onto a line with one loop fewer, at a lower mean speed (9.15 → 8.81 m/s). With the pool present
  but empty the lap stays 57.80 s; a full-throttle pilot is slower on the pool (30.5 → 32.4 s).
  The local path equals the direct path, and the drop already appears at `b76cace`.
- Authority adapter: `tools/wake_velocity_course.[ch]` (`proteus_local_drive`; it records
  replays, which `proteus_boat_net` may not) is the local authority's `BoatNetRules`. Two new
  optional callbacks, used only by private servers: `committed` (the fleet after each tick,
  read-only) and `private_state` (bytes for the client after each grant). The adapter steps the
  course from the committed boat, records the run with its own camera-follow value (it links no
  presentation code), and settles a finish at its tick: best selection
  against the generation's best time, the practice latch, and one pending finish request holding
  the copied run. The camera follow is one pure inline helper (`boat_camera_follow`,
  `src/boat_presentation.h`) that both the presentation and the recorder call.
- Wire: private type `0xC1` PrivateState ([protocol](MULTIPLAYER_PROTOCOL.md#private-range-in-process-sessions-only)),
  sent at the end of a grant when it changed; the client keeps the newest
  (`boat_local_state`). The glue decodes it into the session's course and practice latch after
  each frame; the HUD, ghost and L/B keys read those as before.
- Host management and storage: the game's practice latch (altered time, console) is forwarded to
  the authority before the frame's ticks. After the frame the glue takes the finish request into
  the session's runs through `boat_replay_runs_finish` (factored out of `boat_replay_runs_step`,
  so notices and requests are the direct path's) and the existing `boat_session_flush` writes
  it. A restart recreates the endpoints and the adapter from the restarted session (best and
  ghost carried).
- The game: `LOCAL_ELIGIBLE` admits course sessions (the course plugin kind) and course
  captures run the scripted drive through the local session. `--replay` keeps the direct path;
  L/B playback during an attempt grants no ticks.
- Deviations from the [AR1 design](ARCHITECTURE_AR1.md#6-authority-side-results-and-persistence):
  - The finish request has no generation-tagged storage ack back to the authority (AR1 §6
    "Ack `saved`/`failed(reason)` with run identity and session generation"); deferred to L3.
    The course adapter keeps no state an ack could change (a failed save keeps the run as last
    and reports it, as before), so the outcome is the flush's notice on the client. A restart
    cannot meet a pending request: the host takes it after every frame.
  - L6 removed `--direct-session` (D8's comparison switch) before the owed L0 manual items were
    run. Since then the user checked the merged AR2–AR6 milestone on macOS
    ([AR3 status](#ar3-status), recorded in `a294509`): the game works, with steering and the
    menu. Of the L0 list ([AR6 status](#ar6-status)) that covers steering and the menu only;
    throttle feel, mouse and Shift, help and focus-loss pause, time scale to 8×, repeated
    reset and restart, the three skins and quit to title remain unchecked, and there is no
    longer a direct path to compare them against.
- Removal point not reached: `boat_session_step` keeps its course recording for the browser
  (L4) and the validators.

Evidence (macOS 26.6.2, Apple M1 Max, AppleClang 21.0.0, Debug, 2026-11-10):

- `boat_local` gains `course_trace` (3 attempts, about 3.5 s), `course_practice` (about
  1.2 s), `course_restart_mid` (about 0.15 s), `course_practice_mid` (about 0.9 s) and
  `course_saved_ghost` (about 2.1 s; a best saved by the flush, selected on a fresh launch and
  raced as the ghost), all slow lane, and `course_state` and `private_state_rejected` (a hosted
  server and a playing hosted client reject 0xC1), quick. Negative checks during development: a
  different camera-follow rate and a best comparison that accepts equal times each fail
  `course_trace`.
- Course captures (`--course`, calm and `--waves`, `--time` 0, 3, 10, 25 and 40), `--ghost` at 4,
  `--replay` at 5 and basin 3/15 and outer 3 free-boating captures are byte-identical to a build
  of the wake-pool commit `b76cace` (the direct path with the same product change).
- `ctest --preset full` and again with `-j8` each passed 514/514 (13 disabled); the
  browser-selected `wake_velocity.c` passes the stub syntax check (not an Emscripten build).

Owed manual checks: a native windowed time trial (feel on the wake pool, finish, new best saved
and loaded as the ghost on the next launch, practice at altered time, restart), plus the L0 items
still open from AR6. Browser and Windows: nothing run.

L1 is split into two steps on branch `arch/l1-voyages` (from `main` after PR #181): L1a
voyages and tours without an objective, and L1b, authoring and objectives on the authority
(ledger L9 folded in). The first split had a separate L1b for the objective adapter; it was
merged into the authoring step on 2026-11-11 (the user's choice) because a voyage objective exists only while
authoring: the catalog generator refuses an objective on any bundled package except a free one
(`tools/boat_plugin_catalog_gen.c`), so only `--plugin DIR` (Shelter tour) and a package the
console has promoted can run one, and authoring stayed on the direct path.

L1a (voyage state, events, weather and recover), implemented 2026-11-11:

- Authority adapter: `tools/wake_velocity_voyage.[ch]` (in `proteus_local_drive`) is the local
  authority's `BoatNetRules` for a session with a voyage route. `committed` steps the voyage from
  the committed boat as `boat_session_step` does. Two new optional private-only callbacks:
  `weather` (the storm the world's water carries from the next tick; the server writes it into
  its fleet's water) and `recover`. Voyage state reaches the client as PrivateState (40 bytes,
  [protocol](MULTIPLAYER_PROTOCOL.md#private-range-in-process-sessions-only)); the glue decodes
  it into the session's voyage and storm after each frame. The route is an immutable resource
  both sides borrow.
- Weather on the client: `BoatLocalConfig.weather` reads each new private state after a chunk and
  sets the storm in the client's mirror and prediction (`boat_net_client_private_storm`). Ticks
  predicted between the weather's start and the end of that chunk use the previous storm; the
  chunk's snapshot corrects them. Shipped voyages start the weather 2.5 km from a storm of 700 m
  radius, so the hull never meets that difference, and the validator finds every predicted tick
  equal.
- Recover: new private type `0xC2` PrivateRecover (client to server, header only). Between
  frames the glue computes the recovery from the client's voyage view, the client predicts it and
  sends the request, and the authority's adapter places its own pose before the next frame's
  first tick (`boat_fleet_relocate`: the same life, so in-flight commands stay valid; the wake
  span restarts). The session shows the recovered boat, voyage and presentation at once, as
  `boat_session_recover` does.
- The game: `LOCAL_ELIGIBLE` admits voyage packages with a route and no objective (the bundled
  Långedrag voyage, Harbor life and the Grodan and canal tours). A session that stops being
  eligible between frames (the console promoted its plugin to authoring) closes the local session
  and continues on the direct path from the client's view, before an authoring change reaches it.
  Voyages with an objective (Shelter tour) and `--plugin` authoring keep the direct path until
  L1b.

Evidence (macOS 26.6.2, Apple M1 Max, AppleClang 21.0.0, Debug, 2026-11-11):

- `boat_local` gains `voyage_trace` (about 0.7 s) and `tour_trace` (about 0.14 s), slow lane, and
  `voyage_state` and `recover_ignored`, quick ([cases](TESTING.md#network-checks)). Negative
  checks during development: a server that ignores the adapter's storm fails `voyage_trace`
  (`same_voyage`), and one that ignores the recovery fails it (`same_authority`).
- Captures byte-identical to a build of `dba5bc1` (the direct path): `outer` at `--time` 3, 20
  and 60 (the Långedrag voyage, now local), `basin` at 3, 20 and 60, `--course` at 3 and 25,
  `--package proteus.harbor-life` and `--package proteus.grodan --skin grodan` at 5 and 40 (local),
  and `--plugin plugins/shelter-tour` at 5 and 40 (direct path).
- `ctest --preset full -j8` passed 522/522 (13 disabled). No browser or Windows build.

Owed manual checks: a native windowed voyage and tour (recover after passing a shelter by holding
Backspace, the weather building near the storm, arrival, restart), and promoting a running
voyage to authoring from the console.

L1b (authoring and objectives on the authority), design 2026-11-11; the user accepted
[L1-1 to L1-4](#l1b-decisions) as recommended on 2026-11-11:

- Scope: native `--plugin DIR` sessions of every solo kind (voyage, course, free) and runtime
  packages the console promotes. The browser keeps its authoring and isolated objective on the
  direct path until L4. The developer review tools (`boat_plugin_review`, `boat_plugin_worker`
  preview checkpoints and seeks) stay on the direct path as isolated tools, like the studies.
- Ownership. Host management, in the glue (`WakePlugin`), unchanged: files, save polling, the
  staging workers, the revision table and Undo, the held candidate and its policy (offer, F7,
  the live-ready check from the client's view), the console, the bridge and the status text.
  Immutable resources: a committed revision (route, shelter ids, objective source, scene,
  course) is borrowed by the authority; the host keeps `BOAT_PLUGIN_UNDO + 2` slots and only a
  later commit reuses one, after the authority has moved its borrow. The authority's adapter
  (the voyage adapter, extended): voyage progress and the objective transaction (checkpoint,
  event queue, sequence, the batch staged for T+1, timers, quarantine and diagnostic), as the
  room objective does (`tools/boat_room_objective.c`). Lua runs on the native authority, as in
  hosted rooms.
- The objective on the authority: after each committed tick the adapter queues the voyage's
  events and, every 12 ticks, runs the update; the batch commits before the next tick
  (`wake_plugin_before_tick`/`after_tick` semantics, moved, not reinvented). A recovery
  queues `voyage.recovered` at once, as the direct path does after `boat_session_recover`.
- To the client: the HUD rows and suspension through the existing ObjectiveState message
  (8 rows of 24-byte ids and labels fit the plugin HUD exactly; `rules.state`); the suspension
  text (160 bytes) joins the voyage's 40 bytes of PrivateState. The authoring panel draws from
  these instead of `plugin.objective`.
- Commit: the host decides as today; between frames the glue sends a new private request
  (`0xC3` PrivateCommit: the candidate's revision number and change) and holds the candidate.
  The authority applies it before the next tick: unchanged or renamed rebind the route; live
  remaps the current shelter by id; restart voyage restarts the voyage and starts a new
  objective generation; migrate runs the candidate's `migrate` on the authority's checkpoint.
  The result returns in PrivateState and the host commits its table (or reports "MIGRATION
  FAILED; KEEPING Rn") on the next frame, so the host's active revision is always the
  authority's. While a commit is in flight, staging, Undo and F7 wait one frame. A session
  restart (new water or marina layout) and every course or free commit that changes the session
  are a new local generation (`wake_local_restart`), as plain restarts are.
- Inspection: `wake_plugin_inspect` and the bridge's live-inspect keep the host's lines
  (revision, scene, memory) and take the objective's (tick, digests, queue, HUD, timers, state
  bytes, trace) from the authority (decision L1-2).
- The direct path keeps no authoring role in the native game: L1a's hand-back on promotion is
  removed, and `LOCAL_ELIGIBLE` admits authoring and objectives. `wake_plugin_before_tick` and
  `after_tick` remain for the browser (L4) and the review tools.
- Tests: an objective trace (`boat_local`) that runs Shelter tour on the direct path and the
  local session from the same commands and requires equal objective digests, events, HUD and
  quarantine on every tick; one per commit kind (unchanged, live, migrate accepted and refused,
  restart voyage, restart session) landing between the same ticks as the direct path; Undo;
  a failing callback quarantining on both; the existing plugin session, stage, console and
  bridge validators passing unchanged; Shelter tour captures byte-identical.

<a id="l1b-decisions"></a>Decisions (all accepted as recommended, 2026-11-11):

| ID | Question | Options | Recommendation |
| --- | --- | --- | --- |
| L1-1 | When a migrate runs | (a) on the authority, two-phase: the host commits a frame later; (b) the host commits first and the authority runs migrate, rolling the host back on failure | (a): the host's active revision never differs from the authority's |
| L1-2 | Objective inspection (live-inspect, `--inspect`) | (a) a private inspect request the authority answers in reliable chunks (the state can be 16 KiB); (b) a summary only (tick, digests, queue, HUD, trace), with state bytes from the review tool; (c) the glue reads the adapter directly, a documented exception to rule 2 for developer inspection | (a): keeps rule 2 and today's output |
| L1-3 | Staging while a commit is in flight | (a) hold staging, Undo and F7 for that frame; (b) let a new stage replace the candidate and cancel the request | (a): one frame, never visible to the player |
| L1-4 | Branch and PR | (a) L1a and L1b in one PR (the milestone); (b) open the L1a PR now and L1b as the next milestone | (a), after your windowed L1a check |

<a id="l1b-status"></a>L1b implemented 2026-11-11 (steps: objective transaction extracted to
`tools/wake_velocity_objective.[ch]`, the objective on the voyage adapter, PrivateCommit,
PrivateInspect/Inspection, then the game's wiring):

- The objective transaction (`WakeObjective`: checkpoint, event queue, sequence, staged batch,
  generation, diagnostic, trace) is one module used by the direct path (browser, review tools)
  and the voyage adapter. The client sees it as `WakeObjectiveView`: the HUD via ObjectiveState
  and an 18-byte tail plus text in PrivateState ([protocol](MULTIPLAYER_PROTOCOL.md#private-range-in-process-sessions-only)).
- Commits: `BoatNetRules.commit` returns `BoatNetCommitResult` (relocated boat, new scene); the
  server installs the scene and relocation and reads the storm again, the client predicts both.
  The voyage adapter handles rebind, live, migrate (two-phase, L1-1) and restart voyage; the
  course adapter rebinds an unchanged or renamed course. Session restarts stay new local
  generations.
- The game (`tools/wake_velocity_plugin.[ch]`, `wake_velocity.c`): `WakePluginAuthority`
  (`wake_plugin_attach`) forwards every host commit; a migration holds staging, Undo and F7 with
  `busy` until its result arrives (L1-3), then commits or reports `migration_failed`.
  `wake_plugin_inspect` and the bridge's `inspect` take the objective's lines from the
  authority's latest inspection (L1-2). `LOCAL_ELIGIBLE` admits authoring and objectives; L1a's
  hand-back on promotion is gone, and an authority that cannot be asked stops the session
  ("Single player stopped").

Evidence (macOS 26.6.2, Apple M1 Max, AppleClang 21.0.0, Debug, 2026-11-11):

- `boat_local` gains `objective_trace`, `objective_quarantine`, `objective_commits` and
  `authoring_trace` (slow) and extends `voyage_state`; `wake_velocity_bridge` checks the
  deferred inspection with a fake authority ([cases](TESTING.md#network-checks)).
  `wake_plugin_inspect` on the local path prints the same text as the direct host
  (`authoring_trace`) and as a `main` build through `boat_plugin_review`.
- Captures byte-identical to a build of `190f17b` (L1a): `outer` and `basin` at `--time` 3, 20
  and 60, `--course` at 3 and 25, `--location outer` with `--package proteus.harbor-life` and
  `proteus.grodan --skin grodan` at 5 and 40, and `--plugin` Shelter tour (now local),
  Långedrag, Harbor life, Time trial and Basin free at 5 and 40. Captures do not draw the
  authoring panel.
- `ctest --preset full -j8` passed 526/526 (13 disabled). No browser or Windows build.

Limitations: the objective's inspection lines can be one frame older than the host's owner
fields; F7 on a migration takes effect a frame later than on the direct path; a predicted tick
after a commit uses the new scene and storm only once the snapshot corrects it if the client's
prediction differed; the browser and review tools keep the direct path until L4.

Owed manual checks (windowed, native; deferred by the user on 2026-11-11 until all L* rows
are done): `--plugin plugins/shelter-tour` with the panel HUD, a
save that stages a migration and F7 (MIGRATION IN PROGRESS for one frame, then applied), a
failing migration, F6 Undo, a live route edit and a live marina layout edit, the console
promoting a running bundled voyage to authoring, and `plugin_author.py live-inspect` against a
running game; plus L1a's owed voyage and tour checks.

<a id="l7-l8-status"></a>L7 and L8, implemented 2026-11-11 on `arch/ar7-rest` (from `main` after
PR #184):

- L7 (replay and ghost viewing): viewing runs no authority, as designed; the replay branch of
  the frame loop grants nothing. One gap closed: a session launched with `--replay FILE` never
  opened the local session, so its first restart drove the course on the direct path. Its
  restart now opens the local session when the session is eligible.
- L8 (developer studies, [D6](ARCHITECTURE_AR1.md#product-decisions) (b)): `--wake-field`,
  `--player-wake`, `--no-player-wake` and `--wake-objects-crossing` run in `wake_study`, the
  game's own sources and target settings built with `WAKE_VELOCITY_STUDY` (native only, never
  packaged). `wake_velocity` (native and browser) rejects them and names `wake_study`, so its
  native free boating, time trial, voyages and authoring always have the wake pool and the local
  authority. `--wake-height` and `--wake-life-ticks` stay in the game: they were already private
  world options.

Evidence (macOS 26.6.2, Apple M1 Max, AppleClang 21.0.0, Debug, 2026-11-11): `wake_study`
captures byte-identical to `main`'s `wake_velocity` (`aaafd8d`) for `--player-wake`,
`--no-player-wake`, `--wake-objects-crossing`, `--player-wake --current --waves` (all
`--time 9`), `--wake-field --time 3` and outer `--no-player-wake --time 5`; the game's basin
capture unchanged; new
`wake_velocity_study_rejected.<option>` tests; `ctest --preset full -j8` passed 531/531 (13
disabled). Not verified: the `--replay` restart in a window (no scripted restart exists), any
browser or Windows build.

<a id="l3-status"></a>L3 (working harbour), implemented 2026-11-11 on `arch/ar7-rest`, as AR1
designed it ([section 3](ARCHITECTURE_AR1.md#3-shared-commandstate-contract), work mode):

- Endpoint: `src/boat_work_local.[ch]` (`proteus_work_local`, links the work session's Lua; not
  part of `proteus_boat_net`). The authority owns a `BoatWorkSession`; the client owns a mirror
  filled only from its messages over the memory transport, with no prediction. The client sends
  each frame's actions, helm targets and crane axes (`0xC6`); the authority applies them before
  the frame's first tick and derives each tick's helm from its own boat
  (`boat_work_apply_frame`, `boat_work_helm`: the game's former expressions, moved). It sends each
  committed tick's motion with the trail segments the tick appended (`0xC7`), the job's HUD,
  notice and waiting text after a chunk when they changed (`0xC8`), and on every (re)start a
  checkpoint with the whole trail (`0xC9`)
  ([protocol](MULTIPLAYER_PROTOCOL.md#private-range-in-process-sessions-only)). Presentation steps
  per applied tick from the mirror. Grants and chunks follow L0 (at most 96 ticks, chunks of 12).
- Host management, as L2's course take: resume, the fixture scripts (`--screenshot` drive,
  `--work-review-pose`, `--save-now`) prepare the session before the endpoints start; the menu's
  RESTART JOB resets the authority's session and starts a new generation; autosave, the
  leave-time save and `--save-now` capture from the authority's session between frames through
  the unchanged `BoatWorkSave`. RETURN CRATE travels as an action with the next frame; a paused
  frame still sends its actions with a zero grant. An endpoint failure returns to the title
  after the leave-time save (D10).
- The game's direct work tick loop is removed (`tools/wake_velocity.c`); `cargo_sandbox` stays a
  direct developer tool.

Evidence (macOS 26.6.2, Apple M1 Max, AppleClang 21.0.0, Debug, 2026-11-11):

- `boat_work_local` cases `codec`, `failures` (quick), `trace`, `restart`, `resume` (slow)
  ([cases](TESTING.md#network-checks)). Negative check: dropping trail segments from the motion
  fails `trace`.
- Captures byte-identical to a build of `main` at `aaafd8d`: `--location outer --work` at
  `--time` 3, 12, 14, 30 and 60, the `swing`, `deck` and `approach` review poses, and basin
  `--work` at 3 and with `swing`. The plain outer capture is unchanged too.
- The 69 work, cargo, sandbox and title-start tests pass, including `--save-now`, resume and
  set-aside.

Not verified: the window loop (no display was available to this session, even outside the
sandbox): keys, crane and helm feel, autosave while playing, RESTART JOB and RETURN CRATE from
the menu, pause and focus loss. Browser: working harbour stays native-only (D7).

<a id="l4-status"></a>L4a (browser private sessions without objectives), implemented 2026-11-11
on `arch/ar7-rest`:

- The browser game links `proteus_local_drive` and runs plain free boating, the time trial and
  voyages and tours without an objective on the same in-process local authority and client as
  native (`LOCAL_ELIGIBLE` in `tools/wake_velocity.c`). Course bests keep the existing storage
  (IndexedDB in the browser).
- Objectives and authoring stay on the browser's direct path with its isolated worker gate. A
  package with an objective, or one opened for authoring, never starts a local session. When the
  console's first edit or `/save` promotes a running package to authoring, play restarts on the
  direct path before any staged revision can reach the authority.
- `wake_velocity_voyage.c`'s objective calls are inert in the browser (the authority never gets a
  revision with an objective there); `strnlen` became `memchr` for the browser's strict C17.

Evidence (macOS 26.6.2, Apple M1 Max, emcc 6.0.12 MinSizeRel and AppleClang 21.0.0 Debug,
2026-11-11; headless Chromium 156 through Playwright 1.64.1 with SwiftShader):

- `ctest --preset full -j8`: 536/536 (13 disabled). `tools/check_web_wasm.py`: no Lua in the game.
- `tools/check_web_plugin_author.py --browser chromium` and `tools/check_web_scenery.py` pass.
- A scratch drive of the default Langedrag voyage printed `Local session: generation 1, 301 ticks
  in 93 chunks` on leaving, with no page errors; a console edit during it restarted play at the
  dock and continued on the direct path.
- `tools/check_web_controls.py` and `tools/check_web_title.py` fail identically on a web build of
  `main` at `aaafd8d`: the first treats the plugin status line on stderr as a console error, the
  second's console edit `def.scene.marina.berths[1].x = 15` stages a syntax error. Neither comes
  from L4a.

Not verified: Firefox and WebKit, mobile touch, a time trial in the browser, and any manual play.

<a id="l4b-decision"></a>L4b (browser objectives and authoring on the authority), decisions
2026-11-11; the user accepted L4-1 (a) and L4-2 (b) as recommended on 2026-11-11. AR1's B1 moves the worker gate "to the authority's grant". In the local
session the client predicts each tick before the authority runs it, so the authority cannot
simply refuse ticks: the host must stop granting at the evaluated boundary, and something must
poll the worker while no ticks run. Today `boat_local_frame` with a zero grant does nothing on
the authority (commit and recover requests wait for the next tick).

| ID | Question | Options | Recommendation |
| --- | --- | --- | --- |
| L4-1 | Where the gate lives | (a) the host ends each grant at the objective cadence's next boundary and grants nothing while the client's objective view reports a pending evaluation; the authority services its evaluator (and pending requests) on zero-grant frames, a change to `boat_local`'s L0 path; the result is seen one frame later than today; (b) the host polls the authority's adapter between frames as host management (as L3's persistence capture), same latency as today, but the gate decision reads authority state | (a): keeps rule 2; one frame (about 17 ms) per update against a worker round trip of 35–100 ms |
| L4-2 | Browser authoring | (a) stays on the direct path with L4a's restart on promotion, the one remaining direct path; (b) moves too: the worker's migrate runs for the authority and its result commits frames later (L1-1's two-phase commit, asynchronous), staging and Apply wait as L1-3 | (b), as a step after the objectives, since "done when" needs every shipped mode on the authority; it needs Chromium, Firefox and WebKit qualification |

<a id="l4b-status"></a>L4b objectives (L4-1 (a)), implemented 2026-11-11 on `arch/ar7-rest`:

- The voyage adapter evaluates through a `WakeObjectiveWorker` when the host gives it one (the
  browser's `wake_web_objective_*`; NULL natively, which evaluates at once as before). INIT and
  every boundary's UPDATE start a job (`wake_objective_begin`); a new `service` rules hook, called
  by every grant after its messages and before its ticks, collects the reply
  (`wake_objective_poll`): INIT becomes the objective, an UPDATE is staged for the tick after its
  boundary, with the native path's trace; a failed callback, an invalid or stale reply or the
  5 s elapsed-time deadline quarantines. A tick committed while a job is open quarantines as an
  overrun. The private state's objective tail gains a pending byte (19 bytes before the text).
- `boat_local_frame` with zero ticks now still services the authority and the client. With a
  worker and an objective, `wake_local_frame` ends each grant at the next 12-tick boundary and
  grants nothing (emptying the accumulator, as the direct path's gate does) while the client's
  view is not yet valid or reports a pending job (`wake_local_waiting`); the game also holds a
  reset then. The rule-2 boundary holds: the host reads only the client's view.
- The browser's `LOCAL_PLATFORM_ELIGIBLE` no longer excludes objectives, only authoring.
  `wake_plugin_attach` closes the direct path's INIT job, and a restart that leaves the local
  session detaches before the direct path starts its own objective.
- Reach: the browser catalog (basin-free, time-trial, langedrag, harbor-life, grodan) has no
  objective voyage, and imported packages always open as authoring, so shipped browser play
  reaches this path only once authoring moves too (the next step).

Evidence (macOS 26.6.2, Apple M1 Max, AppleClang Debug and emcc MinSizeRel, 2026-11-11; headless
Chromium 156 through Playwright 1.64.1 with SwiftShader):

- `ctest --preset full -j8`: 539/539, including `boat_local.isolated_objective`,
  `isolated_quarantine` and `isolated_deadline` (see [TESTING](TESTING.md)). A mutation that
  never stages a worker UPDATE fails `isolated_objective`.
- Browser game builds clean; `tools/check_web_wasm.py`: no Lua in the game.
  `tools/check_web_plugin_author.py --browser chromium` passes, including its objective lifecycle
  (22 normal callbacks, deadline across freeze, restart). A scratch drive of the default Langedrag
  voyage ran 309 ticks on the local authority with no page errors.

Not verified: Firefox and WebKit, an objective voyage on the browser's authority (unreachable in
shipped play until authoring moves), any manual play.

<a id="l5-decision"></a>L5 (hosted rooms), decision 2026-11-11; the user accepted L5-1 (a) as
recommended on 2026-11-11. AR1's ledger names no
missing commands or state and an unchanged topology; its removal point is "`net_study` glue
merges into the common client". Hosted rooms already run as authority/client: the game is a
`BoatNetClient` of `boat_server` (or its `--host` child), and private sessions run the same
fleet server rules in process. What the two client glues share is small: `wake_velocity_local.c`
presents the own boat from prediction, tick by tick, with no remote boats; `wake_velocity_net.c`
(`NetStudy`) presents remote boats from delayed snapshots, blends corrections and proximity,
and owns the combat, match, race, roster and objective HUDs, on elapsed time. No common client
was ever specified.

| ID | Question | Options | Recommendation |
| --- | --- | --- | --- |
| L5-1 | What L5 changes | (a) close L5 by evidence: rooms already share the authority and client libraries; rename `NetStudy` to a room client (`WakeRoom`), and record that the two glues differ by design (prediction-only private view against remote interpolation); (b) extract one shared view step (own boat, tick and presentation from a `BoatNetClient`) used by both glues, the rest stays room-only; (c) one client composition for every mode, a private or remote transport behind it, with mode adapters | (a): no behaviour or wire change, nothing to re-qualify in rooms; (b) touches multiplayer feel and needs human play to accept; (c) is a redesign |

L5 closed by evidence, 2026-11-11 on `arch/ar7-rest`: `NetStudy` and its `net_study_*` calls
are now the room client `WakeRoom` and `wake_room_*` (`tools/wake_velocity_net.[ch]`, whose
header records why it stays separate from `wake_velocity_local.c`). No behaviour, wire or test
change; the `boat_net_study_*` test names stay. Evidence (macOS 26.6.2, AppleClang Debug,
2026-11-11): the 91 net, room, host, connect, combat and hunt tests pass; the browser game target
builds clean. Hosted-room play is not re-qualified, as nothing it runs changed.

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
| Net: protocol/client/server/prediction and `boat_net_transport.h` | Core; byte/event transport and application-supplied rules callbacks with caller-owned contexts. AR1: a private schedule mode (host-granted ticks) and a private extension message range with its own codec, never sent remotely. | No transport OS APIs or plugin execution. Existing core/net gates; AR5/AR9 add indirect enforcement as targets split. |
| Presentation: `boat_view.h`, scene/presentation values | Core/scenery; CPU and GPU own their caches/resources. Cosmetic updates cannot feed simulation. | No reverse core dependency. Simulation parity with cosmetics disabled; AR8/AR9 enforce header ownership. |
| Plugin data/codecs: extracted value/state/descriptor interfaces | Core values, bounded buffer helpers and JSON where required; caller-owned bytes/results. | No Lua evaluation, filesystem, processes, browser bridges, rendering or input. AR5 adds source/symbol restrictions and a standalone consumer. |
| Plugin evaluation/host: `boat_plugin.h`, `boat_plugin_state.h`, `boat_plugin_host.h` | Data/codecs/core; evaluation uses private Lua. Host owns stable revisions; borrowers rebind at commit. | No platform execution dependency in the data layer. AR5 splits combined headers; AR4 isolates worker mechanics. |
| Package I/O and workers: `boat_plugin_io.h`, `boat_plugin_stage.h` | Evaluation/data and an explicitly selected OS/browser backend; each operation owns handles and copied requests. | No OS/browser handles in common request policy. AR4 backend selection, lifecycle tests and include checks. |
| Save/file: sandbox byte codec versus `boat_file.h`/work/replay persistence | Codec consumes data values only; authority coordinates checkpoint capture; storage adapters own paths/handles and atomic replacement. AR1: the authority queues copied bytes with an identity; the adapter answers with an identified ack, consumed at the authority's next service. | Codec acquires no files and executes no plugins. AR5 standalone consumer; AR1/AR7 map replay, saves and ghosts without a competing P5g format. |
| Authority/session/work/room adapters | Committed simulation, rules/progression, objective/revision transactions and persistence coordination behind explicit commands/results. `BoatRoomObjective` is an existing authority adapter. AR1: mode adapters may differ (fleet room with course/voyage/objective adapters; a work-session endpoint); host management is local calls between service calls. | No renderer/input/device access in authority rules; no mutable state shared with the client. AR1 defines, AR6 pilots, AR7 migrates. |
| Client composition | Controls, prediction, camera, UI/render/audio; consumes authoritative state and immutable resources with explicit lifetimes. AR1: computes the private tick grant; replay and ghost viewing are client-only. | No authoritative progression or save effects from prediction; no direct authority mutation, reentrant endpoint delivery or parallel solo gameplay. AR2 supplies commands; AR6/AR7 prove behavior. |
| App input/services | Normalized values, small capability/operation contracts, selected native/web backend. UI consumes one input snapshot per frame. | No common UI-to-JavaScript/OS dependency for migrated functions. AR2/AR3 enforce bridge placement and event consumption. |

AR1 (2026-11-09) amended the net, save/file, authority and client rows; the rest of this
section is the AR0 record.

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
| Server/client service | At most 128 messages per service call (`BOAT_NET_MESSAGES_PER_SERVICE`); at most `BOAT_NET_CATCH_UP_TICKS` (8) ticks per call; due ticks follow elapsed `now`, with no stall discard; overload after a backlog above 60 ticks lasts over 1 s. Peer timeout 5 s from the same `now`. Authoritative tick transaction is must-complete; delivery/presentation responsiveness is soft, subject to the separate acceptance rules below. | AR3/AR5: per-message/callback cost is no proven service-time bound. Preserve timeout, backpressure and overload behavior; overload can terminate service rather than promise all future ticks. |
| Command and snapshot acceptance | Firm command eligibility: authority rejects target ticks `<= finalized`; it also rejects too-far-future, wrong-life and duplicate commands. Missing input repeats six ticks, neutral after 30. Client ignores snapshots `<= mirror_tick`; fresher snapshots must still pass validity/assembly rules. A snapshot has no blanket elapsed-time expiry merely because delivery is late. | AR1/AR6: separate input-response/delivery targets from tick eligibility and state supersession; retain late/stale counters and recovery tests. |
| Definition revision lifecycle | Staging may fail validation; a newer waiting candidate can replace the previous waiting one, not an announced revision. Announcement pins the revision/commit tick; at that tick the authority commits and disconnects peers that are not ready. Announced authority commit is must-complete; client acceptance has a commit-tick cutoff and stale-revision checks. | AR1/AR7: preserve staged versus announced ownership and outcomes; shutdown/failure behavior must be explicit, not inferred as guaranteed delivery or crash durability. |
| Local authority scheduling (conflict) | The server model above cannot host solo time scaling: 8× at 60 fps needs 16 ticks per frame against an 8-tick cap, and scaling `now` would also scale peer timeouts. Solo and work sessions discard stalled time; the server catches up. | AR1 item 5 selects the private-authority scheduling input; AR6 verifies it at every speed. |
| Native/authority objective | Synchronous evaluation every 12 ticks; 64 queued objective events; overflow quarantines. Callback budget 100,000 instructions, migration 500,000 (`boat_plugin.h`). Reload/revision can initialize or migrate synchronously. Admitted transaction has a must-complete outcome: commit or the existing failure/rollback/quarantine policy, not guaranteed script success. | AR5: compilation, C calls, allocation and callbacks can hitch; instruction budgets do not bound wall time. No off-thread authority redesign selected. |
| Browser objective | Request copies source/checkpoint/events and pins epoch/revision/tick/digest. Poll rejects `now >= deadline` (5 s), stages an accepted update for the transaction boundary, closes the job and quarantines failure. Simulation gates at T; UI/render continue; no catch-up. Firm. | AR3 extracts this bridge with plugin import/export glue, separately from AR4. Qualify polling/decode/allocation cost without changing transaction policy. |
| Descriptor/query workers | The stage job owns backend resources and copies its request. Deadline 5 s native, 15 s browser; elapsed `now` passed to start/poll. Close cancels and releases; stale receipts rejected. Firm. | AR4: OS termination/reaping and result validation need bounded-cost evidence; cancellation is not immediate OS/browser scheduling. |
| Persistence | Replay flush and work autosave run after ticks in the same frame; atomic replacement protects data. Admitted write attempt has a must-complete success/failure outcome. Replay flush reports failure and clears its request; failed autosave retains dirty state for a later policy-driven attempt. Coalescing dirty state does not promise one durable write per event. Allocation/copy/file I/O can stall a frame. | AR3 inventories placement; AR7 preserves acknowledgement, failure and retry/coalescing policy. Do not imply indefinite retries, guaranteed durability or a frame-budget exemption. |
| Render/audio/resources | View-owned caches, bounded wakes/fleet; scenery loading, GPU rebuild/upload, audio update and present run in frame/startup paths. Pacing waits are intentional. Soft. | AR2/AR3/AR8: frame latency, rebuild spikes and browser suspension stay measured requirements, not guarantees. |

Borrowed resources outlive worlds, sessions, fleets and copies. Host revisions stay at stable
addresses until commit/rebind; a room objective's initial source is borrowed until an accepted
reload installs an owned copy. Server revision installation rebinds the borrowed scene before
later consumers use it. Authoritative origin alone does not determine acceptance: apply each
boundary's deadline, freshness and lifecycle policy.

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

## AR1 authority/client design — 2026-11-09

Reviewed; product decisions accepted 2026-11-09; implementation not started.
[ARCHITECTURE_AR1.md](ARCHITECTURE_AR1.md) holds the selections, mode matrix, migration ledger
and the accepted decisions; this plan keeps only the step status (header and
[AR1 step](#ar1--authorityclient-architecture-and-migration-design)).

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
- L6, 2026-11-10, same machine, Debug: warning-clean build; `ctest --preset full` and again with
  `-j8` each passed 511/511 (13 disabled); `test_time`, boundary and `platform_bridges` checks
  pass; the browser-selected `wake_velocity.c` passes the stub `emscripten.h` syntax check (not
  an Emscripten build).
- L2, 2026-11-10, same machine, Debug: warning-clean build; `ctest --preset full` and again with
  `-j8` each passed 514/514 (13 disabled); course, ghost, replay and free-boating captures
  byte-identical to the wake-pool commit's build; stub web syntax check passes.
- Not performed by the agent: browser build, Windows/Linux runs, GPU/windowed input/audio
  playback, cross-machine play, performance deadline qualification and user acceptance. The user
  has since played native builds on macOS (the merged AR2–AR6 milestone and L6's windowed
  `--demo`; see the [AR3](#ar3-status) and [AR7](#ar7-status) status). Later steps
  carry their own platform/manual gates.
- Documentation links and whitespace checked after each planning change. Build/test results
  qualify the unchanged baseline, not the proposed migration.
- 2026-11-09: timing clarification with Astra: separated transport responsiveness from command/
  snapshot acceptance, and staged revisions from announced commit obligations. Must-complete
  now requires contract-defined outcomes rather than guaranteed success or unlimited retries;
  persistence failure/retry behavior remains unchanged. Documentation-only clarification.
- 2026-11-09: AR1 design drafted on `arch/ar1-design` ([design](ARCHITECTURE_AR1.md)): mode
  matrix, frame order, contract layers, topology, scheduling resolution, persistence, browser
  composition, precision and ledger; product decisions listed for the user. An independent
  review the same day required chunked grants, restart by endpoint recreation, tick-based
  client stall rules, work targets, the full-precision hosted-path risk and proxy labelling;
  applied.
- AR1 measurement, 2026-11-09, scratch harness (not committed), Release `proteus_boat_net`, Apple
  M1 Max, macOS 26.6.2, Apple clang 21.0.0: a proxy run of today's elapsed-`now` server and
  client in-process (scaled `now` at 8×, 16 ticks per frame) cost about 2–3× direct one-boat
  stepping. Not the selected scheme; the 96-tick figure in the design is extrapolated.
- AR1 resync nit (PR #176): a checkpoint at the client's mirror tick is installed while a
  snapshot at that tick is ignored. Investigated and kept as the wake-repair path; comment and
  [protocol](MULTIPLAYER_PROTOCOL.md) and [testing](TESTING.md) updated, and
  `boat_net.resync_same_tick` / `boat_net_enet.resync_same_tick` added (they fail under `<=`).
- AR1 verification, 2026-11-09, arm64/macOS 26.6.2, AppleClang 21.0.0: Debug build with
  `PROTEUS_GPU_TESTS=OFF` (fetched sources reused from the main checkout) passed; `ctest --preset
  full` with socket/process access passed all 474 enabled tests in 38.84 s (13 disabled). No
  browser, Windows/Linux, GPU or manual checks; documentation-only beyond the resync commit.
- 2026-11-09: the user accepted AR1 product decisions D1–D10 as recommended. Product direction
  only: no implementation exists, and nothing was played or browser-tested.
- AR6 verification, 2026-11-09, `arch/ar2-pilot`, arm64/macOS 26.6.2, AppleClang 21.0.0: Debug
  build warning-clean; `ctest --preset full` passed all 492 enabled tests (13 disabled), including
  the 17 `boat_local` cases and the unchanged `boat_net` golden-byte and regression cases. No
  windowed, browser, Windows/Linux or GPU check. See the [AR6 status](#ar6-status).
- AR3 verification, 2026-11-10, `arch/ar2-pilot`, arm64/macOS 26.6.2, AppleClang 21.0.0: Debug
  build warning-clean; `ctest --preset full` passed all 494 enabled tests (13 disabled),
  including the new `platform_bridges` and `platform_bridges_detects`; rerun after the review
  fixes and AR3f, again 494 of 494. No windowed, browser,
  Windows/Linux or GPU check. See the [AR3 status](#ar3-status).
