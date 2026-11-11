# AR1 authority/client design — 2026-11-09

**Status:** reviewed (independent reviews, 2026-11-09); the user accepted
[product decisions](#product-decisions) D1–D10 as recommended on 2026-11-09. Ledger row L0 is
implemented on `arch/ar2-pilot` (AR6, not merged) with the deviations its
[status](ARCHITECTURE_PLAN.md#ar6-status) lists; no other row, worker or mode is, and nothing
here has been played or browser-tested. The [architecture plan's AR1 step](ARCHITECTURE_PLAN.md#ar1--authorityclient-architecture-and-migration-design)
owns the step status. Observed behaviour is cited at `ab01d92`; it extends the
[AR0 baseline](ARCHITECTURE_PLAN.md#ar0-merged-baseline-and-contracts--2026-11-09) and
does not restate it.

Selections in one place:

- **Topology:** in-process, single-thread, queued (T1) for every private session, over the
  existing bounded memory transport with no impairment. Hosted rooms keep the `boat_server`
  process. Threads, workers and processes are not selected for private play.
- **Scheduling:** private authorities run a **host-granted tick budget** computed by today's
  solo/work frame functions, in chunks of at most 12 ticks that alternate with the client,
  instead of deriving due ticks from elapsed `now`. Elapsed time
  keeps operational deadlines only. Public and LAN rooms keep the elapsed-time model.
- **Contract:** protocol v14 messages for everything they already express; a private extension
  range for private-mode state and commands that never reaches remote peers; host management
  as local calls between service calls, outside any protocol.
- **Persistence:** authority adapters capture checkpoints and results between ticks and queue
  copied bytes; storage adapters in the application write them and return identified acks.
- **Browser:** authority in the game's main Wasm (no Lua), with the existing isolated objective
  evaluator as its compute service and the T/T+1 gate moved to the authority's tick grant.
- **Precision:** private sessions keep full-precision commands during migration (D4);
  hosted rooms keep v14 quantization.
- **Restart:** recreates both endpoints at tick 0.

## 1. Mode behaviour matrix

"Today" is the production path in `wake_velocity`. Authority-owned state is never read by
pointer from the client (rule 2); storage and platform adapters are application code outside
core/net.

| Mode | Today | Authority owns | Client owns | Storage adapter | Platform adapters |
| --- | --- | --- | --- | --- | --- |
| Free boating (basin/outer, standard and harbor-life packages, skins) | `BoatSession` stepped in the frame loop (`tools/wake_velocity.c:2062-2111`); the title adds `--current --crossing-wake` (`:2735-2736`); skins pick a handling class (`:1283-1290`); reset restarts tick, water and wakes (`:2036-2039`). | Fleet room (one slot) with handling class, current and crossing water, wake objects, scene traffic contacts; restart generation. | Input, prediction view, camera, presentation (spray, flag), HUD, audio. | None (no saves). | Input/capability (AR2), lifecycle (AR3). |
| Voyage / canal tour / Grodan tour / imported voyages | Session voyage observed per tick (`src/boat_session.c:151-154`); recover to shelter (`tools/wake_velocity.c:2030-2035`); objective in `wake_plugin_after_tick` (`tools/wake_velocity_plugin.c:361-415`). | Route, shelters, exposure/weather, voyage events, objective transaction and timers, revision commit. | Voyage/objective HUD from state messages; notices. | Plugin source/import (existing plugin I/O, not authority). | Objective evaluator bridge (browser, AR3); plugin staging workers (AR4). |
| Time trial (basin course, course packages) | Course swept per tick; runs recorded per tick and flushed after the frame (`src/boat_session.c:150-160`, `tools/wake_velocity.c:2120-2126`); practice latch on altered time or console use (`:1695`, `:2074`). | Course state, elapsed/splits/finish, practice latch, run recording, best selection request. | Course HUD, ghost/replay playback, replay transport controls. | Best and last replay files (atomic replace, digest-named best). | Native data directory / browser IndexedDB. |
| Race (hosted) | `boat_server` race room; client mirror/prediction. | Unchanged server race rules. | Unchanged. | None. | ENet / WebRTC transport. |
| Working harbour (native only) | `BoatWorkSession` ticked by the game (`tools/wake_velocity.c:2079-2094`); autosave after the frame (`:2122-2125`); browser stubs (`tools/wake_velocity_work_web.c:7-9`, `CMakeLists.txt:309-316`). | Work session: boat, cranes, crate, sites, job host and its 12-tick update, lighting clock, autosave watch, checkpoint capture. | Crane view, HUD, notices, SAVED mark, menus. | `cargo-<package>.sav` with `.bak` and `.rejected` (`src/boat_work_save.h:1-7`). | Files (native); none in browser. |
| Hosted free / deathmatch / shelter hunt | `boat_server` (dedicated or the `--host` child process, `tools/wake_velocity_host.c:142-185`, `:254-305`); the game is a client (`tools/wake_velocity_net.c:172-272`). | Unchanged: fleet, match, marks, `BoatRoomObjective`, revisions. | Unchanged; joins the common client composition. | None. | ENet / WebRTC, host process. |
| Scripted demos and captures (`--demo`, `--screenshot --time`, work scripts) | `--demo` steps `demo_step` inside the frame loop (`tools/wake_velocity.c:2095-2096`); `--screenshot` drives and work scripts run outside it (`:1367-1390`). | The same private authority as the mode, fed by a scripted command source. | Capture rendering. | `--save-now` through the work storage adapter. | Headless CPU renderer. |
| Replay and ghost viewing (L/B, `--replay`, `--ghost`) | Pose samples played in recorded water; no simulation (`tools/wake_velocity.c:2060-2064`). | None: viewing runs no authority. A ghost race reads a file beside a live time-trial authority. | Playback, seek, pause, speed, ghost drawing. | Read-only replay files. | — |
| Plugin authoring (console, bridge, F5–F7) | Staging/commit in `wake_plugin_*` and the bridge (`tools/wake_velocity.c:1968-1977`). | Commit of a staged revision at a tick boundary, as for rooms. | Editors, console, diagnostics. | Package files/bundles (plugin I/O). | Staging workers (AR4), browser bridge (AR3). |
| Developer studies (`--wake-field`, `--player-wake`, object tuning, scripted crossing boat) | Flags on free boating (`tools/wake_velocity.c:1104-1124`); the crossing boat steps its own fleet (`tools/wake_velocity_drive.c:82-99`). | Decision D6. | — | — | — |

Offline validators, benchmarks and review tools keep calling core directly; they are not a
production gameplay path.

## 2. Frame order in a private session

One host frame of a private T1 session runs these stages in order. "Send" only enqueues; no
stage calls the opposite endpoint, so delivery is never reentrant.

1. **Acquire input** once (AR2a value) and derive UI actions and host-management requests.
2. **Host management** between service calls: pause state, time-scale index (and its practice
   notice), restart, staged revision, resume-at-start. These are local calls on the authority
   host, ordered before this frame's ticks.
3. **Grant:** `G` ticks from the existing accumulator (`boat_time_frame_seconds` or
   `boat_time_work_frame_seconds`, `src/boat_time_control.h:24-37`); `G = 0` while paused,
   replay is playing or an objective transaction gates the authority. `G <= 96`
   (0.1 s × 8 × 120 Hz), the same bound solo has today. Pause states and rates follow the
   accepted D1 and D2.
4. **Chunks:** the grant runs as chunks of at most 12 ticks, each ending at the next multiple
   of 12 (the objective cadence) or at the end of the grant. Per chunk, in this order:
   1. **Client intake:** produce the chunk's commands, addressed to authority ticks
      `N+1 .. N+k` (`k <= 12`), recomputing target-derived rates per tick from the predicted
      boat as the net client does (`src/boat_net_client.c:723-788`); record them in prediction;
      send. A chunk therefore stays within the 12 commands one batch repeats
      (`src/boat_net_protocol.h:29`), the authority's 24-tick future window
      (`src/boat_net_protocol.h:30`, `src/boat_net_server.c:418`) and prediction's 32-tick lead
      (`src/boat_prediction.h:11`). The client normally sends a batch only every
      `input_ticks` commands (2, `src/boat_net_protocol.h:28`, `src/boat_net_client.c:780`), so
      an odd chunk would hold back its last command and the authority would repeat the previous
      one. **Private mode flushes one batch at every chunk end**, carrying the chunk's
      commands (≤ 12, one packet). Chosen over `input_ticks = 1` because it sends one message
      per chunk instead of up to 12, leaves the Accept value and hosted behaviour untouched,
      and flushes exactly where the authority needs the commands.
   2. **Authority service:** handle at most 128 inbound messages, then run the chunk's ticks.
      Each tick keeps today's order: commands, fleet/session step, rules events, `after_tick`,
      objective cadence, emission (AR0 tick order). The authoritative tick is must-complete.
      An objective dispatched at the chunk's last tick T in the browser stops the grant there
      and the remainder is discarded, as the frame loop does today
      (`tools/wake_velocity.c:2107-2110`). No command past T was sent, so none has to be
      withdrawn (received commands are immutable, `src/boat_net_server.c:421-424`).
   3. **Client service:** handle at most 128 messages: snapshots/state, births, objective and
      private state; reconcile prediction once. This advances the mirror so the next chunk's
      prediction stays inside its lead cap.
5. **Persistence capture** after the last chunk: course finish requests, autosave watch and
   checkpoints become queued storage requests with copied bytes (section 6).
6. **Storage service:** the application performs at most one write per request kind and queues
   the ack for the authority's next service (never a callback into authority state).
7. **Present:** step presentation for the ticks advanced; render, audio, UI.

Per frame this is at most 9 chunks (a partial first chunk up to the cadence boundary, then full
ones), each bounded by its 12 ticks, the 128-message caps and one correction replay.
Input-to-authoritative-tick latency equals today's solo: commands sampled in stage 1 are applied
in the same frame and presented in stage 7.

## 3. Shared command/state contract

Local and remote compositions use the same authority rule code; private mode is never a
direct-call bypass. The contract has three layers.

**Protocol v14, unchanged.** InputBatch (steering/throttle rates, inactive flag, gun, grenade
and torpedo serials), SpawnRequest, Ready, Ping/Pong, ResyncRequest, Disconnect; Snapshot,
WakeBirths, world checkpoint, room Definition (mode, location, course, scene, marks, rules),
ObjectiveState, Revision*, Match/Race state and events, Roster
([protocol](MULTIPLAYER_PROTOCOL.md)). Private free boating, voyage objectives HUD and a fleet
time trial fit these as far as they reach.

**Private extension range (new, never on the remote wire).** Message types in a reserved range
encoded by a separate private codec that obeys the net boundary. Remote peers never send them;
a remote server that received one would still count it as malformed, so hosted bytes and
version stay v14. Candidates, each tied to its ledger row:

| Need | Why v14 lacks it |
| --- | --- |
| Handling class per slot | Fleet steps every slot with `boat_step` (`src/core/boat_fleet.c:316`); solo uses `boat_step_handling` with the skin's class (`src/boat_session.c:148-149`). |
| Current and crossing water | Rooms use calm water without them (`tools/wake_velocity.c:365-371`). |
| Restart session (new generation) | SpawnRequest respawns one boat; solo reset sets the tick back to 0 (`src/boat_session.c:20`) and restarts water and wakes. See [restart](#restart-and-session-generations). |
| Full-precision commands | v14 rates are `int8` ×127 (`src/boat_net_protocol.h:141`, `src/boat_net_protocol.c:1109-1121`); see D4 and the hosted-path risk below. |
| Voyage state and recover | Route, shelter, exposure, weather and voyage events are session-only. |
| Course state and run results | Gates, elapsed, splits, finish and practice are session-only; race state is the room race. |
| Work commands and state | Crane axes per tick, hook/lash/moor/recover/view/aim/brake/return-crate actions; crane, crate, site, lighting, notice and job HUD state. |
| Storage acks surfaced to the client | SAVED mark, best-saved and save-failure notices. |

**Hosted-path risk.** Full precision is more than a message range: the server's command ring
and missing-input policy (`src/boat_net_server.c:425`) and the prediction history
(`src/boat_prediction.h:45`, `:53`) store `int8` `BoatNetCommand` values, and handling class
and current/crossing water need fleet/core changes that prediction also runs. These touch code
hosted rooms use, so L0 gates include the net golden bytes and the net regression suite.

Course and voyage observation run inside the authority tick, like race and mark observation
today (AR0 tick order), as authority adapters given a read-only view of the committed state
after each tick. Adapters still never call back into the server.

Promoting any of these to remote play (co-op working harbour, remote voyages) is separate
product work with a protocol version change.

### Restart and session generations

Net code assumes ticks only increase within a session: the checkpoint install rule
(`src/boat_net_client.c:188`), the finalized-tick checks (`src/boat_net_server.c:410-418`) and
the tick-indexed input ring. **Selected: a restart recreates both endpoints** (new authority at
tick 0 with a new epoch, new client, fresh transport pair), rather than keeping ticks increasing
with an offset. Tick 0 keeps today's water phase and trace, so restart traces stay comparable
with solo, and no net invariant changes. Teardown and recreation are the repeated-session case
the L0 gates already require. The host runs the in-process handshake in bounded service rounds
within the restart frame, so the player sees what solo shows today (the accepted D5 keeps the full
restart). If the gates show it needs more than one frame, how that frame looks joins D10.

What crosses a restart follows today's `boat_session_restart` (`src/boat_session.c:60-75`), the
caller's reset branch (`tools/wake_velocity.c:2036-2058`) and `boat_session_init`
(`src/boat_session.c:30-37`):

| Carried into the new endpoints | Reset |
| --- | --- |
| Location, waves and water options; handling class; voyage route and course definition; the active plugin revision; time-scale index and pause state (host-owned); best, last and reference runs; personal save paths. | Tick (to 0), world water and wakes, boat and presentation, voyage progress, course progress, objective (new generation), in-progress recording, practice latch; the ghost opponent is copied again from the reference or best run (`src/boat_replay.c:404-418`). |

Before teardown the host captures the old authority's pending storage requests (a finished
run's best/last save, a dirty work checkpoint) and lets the storage adapter complete them, as
today's restart flushes first. Every request and ack carries the session generation; an ack
for an earlier generation is reported to the client but never applied to the new authority.

**Local host management (outside any protocol).** Start/stop and teardown, pause and grant,
time-scale index, restart, resume from a save at start, staging a plugin revision, storage
service and acks, leave-time save, diagnostics counters. Not every local operation becomes
remotely invokable.

Work mode does not use the fleet representation: its authority is `BoatWorkSession`
(already free of drawing, input and files, `src/boat_work_session.h:1-6`) behind a private
endpoint using the same transport, framing and frame order. In T1 its state message is
emitted once at the end of each service, so the client presents tick `N` without prediction.
Because the helm rates are derived per tick from the boat's wheel and throttle
(`tools/wake_velocity.c:2085-2090`), the work client sends **targets**, not rates: the steering
target or keyboard rate, the thrust target, and the frame's crane axes. The work authority
derives the per-tick rates from its own state with today's expressions, so its trace matches
solo without client prediction.

## 4. Execution topology

| Option | Ownership and synchronization | Queues, backpressure, fairness | Latency and cadence | Late results | Verdict |
| --- | --- | --- | --- | --- | --- |
| **T1 in-process, single thread, queued** | Separate authority and client structs, one owner each; no locks. | Memory transport, 256 messages per direction (`src/boat_net_transport.h:65`); unreliable overflow drops, reliable overflow disconnects (treated as endpoint failure, not silent). Fixed order per chunk: client intake (≤ 12 commands) → authority (≤ 12 ticks, ≤ 128 messages) → client (≤ 128 messages); at most 9 chunks per frame (8 when the grant starts on a cadence boundary). | Same frame as solo; one snapshot per 4 ticks, births per 12. | Commands for finalized ticks rejected (firm, unchanged); explicit addressing makes them a test failure, not an expected path. Stale snapshots ignored; superseded storage acks ignored by identity. | **Selected.** Proxy measurement below. |
| T2 authority thread | Needs SPSC queues, atomics, join on shutdown and a rule for capture while ticking; raylib stays on the client thread. | As T1 plus cross-thread wakeups. | Overlaps rendering; adds a hand-off per frame. | As T1. | Not needed at the proxy-measured cost; browser would need Wasm threads, which the web build does not enable. Reopen only with a measured frame-budget miss. |
| T3 child process (existing `--host`) | Process isolation; ENet loopback. | ENet queues. | Elapsed-time server: no pause or scaling. | Server policies. | Kept for hosted rooms only; violates private pause/scaling. |
| T4 browser authority Worker | Separate Wasm with its own world resources and lifecycle. | `postMessage` queues to bound. | At least one event-loop hop per exchange. | Needs deadlines/epochs. | Deferred (section 7). |

**Proxy measurement, extrapolated (scratch harness, not committed or reproducible from the
repository).** It ran today's elapsed-`now` server and client, not the selected scheme: the 8×
case scaled `now` (the rejected option) and reached 16 ticks per frame with repeated service
calls. Release build of `proteus_boat_net`, Apple M1 Max, macOS 26.6.2, Apple clang 21.0.0,
processor time via `clock()`; one predicting client over the unimpaired memory transport, bots
driving, 1,200 frames per rate:

| Location | Rate | Ticks/frame | Server ms/tick | Client ms/tick | Worst frame | Own prediction error |
| --- | --- | --- | --- | --- | --- | --- |
| basin | 1× | 2 | 0.0086 | 0.0172 | 0.13 ms | 0 m |
| basin | 8× | 16 | 0.0071 | 0.0064 | 0.32 ms | 0 m |
| outer | 1× | 2 | 0.0159 | 0.0371 | 0.18 ms | 0 m |
| outer | 8× | 16 | 0.0162 | 0.0161 | 0.58 ms | 0 m |

Stepping a one-boat fleet directly costs 0.0050 ms/tick (basin) and 0.0141 ms/tick (outer), so
the proxy costs about 2–3× the bare simulation. Extrapolated linearly, not measured, the
96-tick bound would cost about 3 ms per frame on this machine. The scaled-`now` run also
produced 7 late commands at 8×, which explicit tick addressing is meant to remove. One machine,
one workload and a different scheme: an indication of feasibility, not qualification of T1 on
any hardware. AR6 measures the selected scheme on named hardware, including `G = 96`.

## 5. Local scheduling resolution

Today the server derives due ticks from elapsed `now`, runs at most 8 per service call and
flags overload after a backlog above 60 ticks lasts over a second
(`src/boat_net_server.c:945-981`); input and spawn token buckets refill from elapsed `now`
(`:440-447`); peer liveness uses `now` (`:953-964`). The client paces commands from `now`
(`src/boat_net_client.c:866-872`) and targets `floor(now × 120 + offset) + lead` (`:757`); its
snapshot-stall resync fires after 1 s of elapsed time (`:846-856`).

**Selected: a private schedule mode for both endpoints.** Policies split by clock:

| Policy | Hosted rooms (unchanged) | Private schedule |
| --- | --- | --- |
| Ticks to run | Due from elapsed `now`, ≤ 8 per call, overload flag | Exactly the host grant `G` (≤ 96 per frame) in chunks of ≤ 12; no backlog, so no overload flag; frame-work measurement replaces it |
| Command tick addressing | `now`, offset and lead | The chunk's ticks; no lead |
| Input rate limit | 120 packets/s from elapsed time | Refill per granted tick (same 1× rate), so 8× is not rate-limited |
| Snapshot-stall resync | 1 s elapsed without a snapshot (`src/boat_net_client.c:846-847`) | Granted ticks past the last snapshot cadence, so pauses never resync |
| Snapshot-silence close | 10 s elapsed without an applied state (`src/boat_net_client.c:826`) | Disabled in-process; a missing state after granted ticks is the stall resync above, and repeated failure is an endpoint failure (D10) |
| Wake-mismatch resync | Digest mismatch older than 1 s (`src/boat_net_client.c:843`) | Mismatch older than 120 granted ticks |
| Prediction hold for missing births | 0.1 s (`src/boat_net_client.c:860`, `BOAT_NET_HOLD_INCOMPLETE`) | 12 granted ticks (one birth batch) |
| Peer liveness, handshake | 5 s / 10 s elapsed | Disabled: both endpoints are serviced by the same host frame; failure is an explicit endpoint result. Startup has a bounded failure deadline on the elapsed clock. |
| Ping/RTT, resync and reset rate limits, objective-worker deadline, save attempts | Elapsed | Elapsed (operational, rule 5) |

With the accepted D1, D2 and D5, this preserves solo semantics exactly: private
menu/help/focus pause and replay grant 0; ¼×–8× scaling, the 0.1 s clamp and stall discard come
from the same functions; the work session's over-1 s suspension rule is its grant function;
browser suspension resumes through the same clamp. Hosted rooms keep running when a client
pauses: its commands carry the inactive flag and the authority eases that boat's controls to
neutral (`src/boat_net_server.c:632`).

Total work per host frame is bounded by `G`, the 128-message caps and one correction replay
(≤ 32 ticks, `src/boat_prediction.h:10-12`), plus the AR0-recorded unbounded-wall-time gaps
(objective callbacks, file I/O) that remain with AR3/AR5. `tick_limit`
(`src/boat_net_server.h:144`) is precedent for an externally bounded tick count, not the
mechanism.

Rejected: scaled or frozen `now` (trips catch-up/overload at 8×, scales timeouts and rate
limits, and produced late commands), and raising the catch-up cap (same clock coupling).

## 6. Authority-side results and persistence

| Item | Capture (authority, between ticks) | Request queue | Storage adapter | Outcomes |
| --- | --- | --- | --- | --- |
| Course run and best | Finish commits the result at its tick (must-complete); the recorder keeps the per-tick replay samples. Practice runs never request a save. | One pending best/last request; a newer finish supersedes an unstarted one. | Atomic replace in the data directory; digest-named best (`src/boat_replay.h:55-80`); IndexedDB in the browser. | Ack `saved`/`failed(reason)` with run identity and session generation; failure keeps the run as "last" and reports it, as today. |
| Work autosave | `boat_work_frame_due` after the frame's ticks (`src/boat_work_session.h:139-141`); `boat_work_session_save` copies a checkpoint with generation, tick and work digest. | One request; an unstarted older checkpoint is superseded (coalescing like today's dirty state). | `cargo-<package>.sav`, `.bak`, `.rejected` unchanged. | Ack with checkpoint identity (session generation, tick, digest); clean only if it matches the latest capture; failure keeps dirty for the next policy-driven attempt. No crash durability or infinite retry. |
| Work resume | Before the first tick. | — | Startup read (allowed blocking), backup fallback, set-aside. | Resumed / fresh / set aside, as `boat_work_save_load` (`src/boat_work_save.h:33-36`). |
| Leave-time save | On quit/title return, after the last tick. | Synchronous at shutdown (not a frame path). | As autosave. | Reported success/failure, as `boat_work_save_leave` (`:41-44`). |
| Objective state | Authority adapter owns checkpoint, events, HUD and quarantine (room pattern: `tools/boat_room_objective.c:225-250`). | — | Not persisted (unchanged). | Commit, or the existing failure/quarantine policy. |
| Authored revisions | Staged by host management; committed at the authority's boundary (solo: between ticks with restart, as today; rooms: P5f staged/announced). | — | Package I/O stays in plugin I/O. | Staged refusable; announced must-complete. |

The replay recorder needs the camera-follow value that today comes from presentation
(`src/boat_replay.h:28-30`). It is one exponential follow of heading
(`src/render/boat_presentation.c:258-260`); the time-trial group gives the recorder its own copy of that
expression so the authority links no renderer and replay bytes stay identical. Pose ghosts stay
`BoatReplay` files; P5g session records, when they land, are the format for authority
command/state recordings; resumable state stays the sandbox save. Personal save paths and best
names are unchanged.

## 7. Browser local authority

The client Wasm carries no Lua (`tools/check_web_wasm.py`), the objective evaluator is a
separate reusable worker that owns no committed state (`web/plugin_objective.js:1`), and the
browser cannot host rooms today.

| Option | For | Against |
| --- | --- | --- |
| **B1 authority in the main Wasm, evaluator as its service** | Net and authority adapters are Lua-free; reuses the firm 5 s evaluator contract, pinned identities, staged commit and quarantine; the gate moves from the client tick loop (`tools/wake_velocity.c:2026-2028`, `tools/wake_velocity_plugin.c:201-302`) to the authority's grant, so T/T+1 behaviour is preserved, not reinvented. | Objective cadence still gates simulation on worker round trips; at high speed the requested rate may not be reached (as today). |
| B2 authority Worker/Wasm with Lua | Synchronous objectives like native. | Second simulation Wasm with duplicated world resources, cross-thread grants, snapshot hops, worker lifecycle and failure cases; Lua beside authoritative state in the browser. |

**Selected: B1.** Under the accepted D7, browser private modes stay those shipped today: free
boating, voyages and tours with optional objectives and timers, and the basin time trial.
Working harbour stays native-only (D7). Offline solo needs no remote server.

## 8. Command precision

Solo feeds float rates to the simulation; throttle is always target-derived
(`src/core/boat_throttle.h:72-76`) and mouse/touch steering too (`src/core/boat_steering.h:25-31`); only
keyboard steering is exactly ±1/0. v14 rounds rates to 1/127, so adopting it would change solo
handling slightly everywhere, including the throttle approach to its target.

**Selected for migration:** private sessions carry full-precision commands so AR6/AR7 can prove
trace equality against today's solo; hosted rooms keep v14. Unifying on a quantized command is
decision D4. Evidence needed before adopting it: per-tick state differences, quantized versus
float, for the course fixture and scripted outer drives; finish-time deltas on the course;
throttle/wheel settle error; and a human feel check with keyboard, mouse and touch.

## 9. Migration ledger

Order: L0 → L6 → L2 → L1 (with L9) → L3 → L4. Scripted drives ride on L0 because they are its
fixtures; the time trial is the simplest persistence; voyages add objective and revision
transactions; the working harbour carries the largest state and the save; the browser follows
the modes it ships. L5 and L7 change no ownership.

| Row | Mode | Missing commands/state | Topology | Compatibility changes | Prerequisite slices | Fixtures | Acceptance gates | Removal point |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| L0 (AR6) | Private native free boating | Private schedule mode with chunked grants; handling class; current/crossing water; restart by recreation; full-precision commands (ring, missing-input policy, prediction history); solo wake-object parameters | T1 | None on the wire; private range added; fleet/core handling and water changes also run in hosted rooms | AR2a input value → commands | AR0 basin/outer captures and wake traces; new solo-versus-local per-tick hash trace; net golden | Trace equality at ¼×, 1×, 8×, a 0.1 s clamped frame at 8× (`G = 96`) and odd-sized chunks (a partial first chunk, an odd remainder, an objective-gated stop); pause, focus loss, suspension; restart and 100 repeated sessions; offline start; zero late/future/rate-limited commands; queue-saturation and partial-startup release on virtual time; net golden bytes unchanged and the net regression suite passing; native windowed manual check | Free-boating branch of the frame loop after L6 passes |
| L6 | Scripted demos and captures | Scripted command source | T1 | None | L0 | Capture hashes, `--demo` traces, `--title-start` checks | Byte-equal captures | `demo_step` direct path |
| L2 | Time trial, runs, bests, ghosts | Course state and results; practice notice; course restart; recorder camera-follow; storage requests/acks | T1 | Best names: the standard course's best is `course-{calm\|waves}-physicsN-wakes.run` (D11); plain-water bests are not loaded and cannot be compared. Replay format unchanged | AR3 persistence placement | `replay-fixture.run` hash, course/replay validators | Equal results/splits from the same commands; best selection; practice; save failure | Course recording/flush in `BoatSession` |
| L1 | Voyages, tours, objectives | Voyage state/events, recover, objective adapter on the authority, solo revision staging | T1 | None | AR5 (data codecs) for the adapter's dependencies | Plugin objective traces and voyage validators | Equal objective traces and revisions from the same command stream; quarantine; weather | `wake_plugin_before/after_tick` ownership |
| L3 | Working harbour | Work commands as targets (helm rates derived by the authority) and crane axes; work state; autosave/resume via storage; lighting clock | T1, work endpoint, no client prediction | None to the `cargo-<package>.sav` format | AR5 sandbox codec; AR3 persistence | `work-save-tests/work.sav` fixture hash, work/autosave/clock validators | Byte-equal saves for scripted drives; autosave triggers and acks; suspension rule; resume/set-aside | `wake_velocity_work.c` session copy |
| L4 | Browser private modes | Browser objective adapter on the authority; IndexedDB storage adapter | T1 in main Wasm | None | AR3 objective bridge extraction | Browser objective/worker checks, Wasm no-Lua check | Exact-deadline virtual-time tests; Chromium/Firefox/WebKit manual runs; mobile touch | Browser client-side gate |
| L5 | Hosted rooms | None | Unchanged process/network | None | — | Net regression suite | Joins, reconnects, room modes after the common client lands | `net_study` glue merges into the common client |
| L7 | Replay/ghost viewing | None | Client only | None | — | Replay fixtures | Playback unchanged | — |
| L8 | Developer studies | Per D6 | T1 if kept (D6 a); none if moved | None | L0 | Wake validators | D6 (a): each kept flag's trace equal under the local authority; D6 (b): the moved tools reproduce their current outputs and the game rejects the flags with a pointer to the tool | Per D6 |
| L9 | Plugin authoring (console, bridge, F5–F7) | Staged revision as host management; commit at the authority's boundary; diagnostics back to the editors | T1 | None to package bundles | AR4 staging backends; AR3 browser bridge; L1 | Plugin staging/console validators, bridge tests, browser objective checks | Reload, Undo and rejection outcomes unchanged in solo; commit lands between the same ticks as today; browser console reload | Authoring paths in `wake_velocity_plugin.c` that commit into `BoatSession` |

Unresolved compatibility or timing items with owners: the resync nit (resolved, see evidence);
browser and phone frame cost of T1 (AR6/L4); objective callback and file I/O wall time
(AR3/AR5, unchanged); the camera-follow copy for replays (L2); hosted-room behaviour under the
common client (L5, net regression suite); full-precision and handling/water changes in code
hosted rooms share (L0, net golden and regression gates).

## Product decisions

The user accepted every recommendation below on 2026-11-09. Acceptance settles product direction;
it is not acceptance of any implementation, which each ledger row's gates still decide.

| ID | Question | Options | Accepted (2026-11-09) |
| --- | --- | --- | --- |
| D1 | Private pause | (a) as today: focus loss, help and menu pause; open console simulates with a neutral helm; (b) pause only from the menu | (a) — accepted |
| D2 | Time scaling | (a) keep ¼×–8× in every private mode, practice latch for trials; (b) restrict it | (a) — accepted |
| D3 | Saves | (a) unchanged: working-harbour autosave, course bests, no voyage progress saves; (b) add resumable voyage progress now | (a); new saves are feature work — accepted |
| D4 | Command precision | (a) full precision in private sessions; (b) v14 quantization everywhere after evidence; (c) widen the protocol (v15) | (a) now, revisit with the section 8 evidence — accepted |
| D5 | Free-boating reset | (a) keep the full restart; (b) respawn like rooms | (a) — accepted |
| D6 | Developer studies in the game | (a) private world options; (b) move to review tools, keep validators | (b) — accepted |
| D7 | Browser working harbour | (a) stays native-only; (b) make it browser-capable through the evaluator gate | (a) — accepted |
| D8 | Rollout switch | (a) developer-only switch, never in a release; (b) a user-visible fallback for one release | (a) — accepted |
| D9 | Browser objectives at high speed | (a) keep the T/T+1 gate even if 8× is not reached; (b) revise the gate | (a) — accepted |
| D10 | What the player sees when the private endpoint fails (reliable-queue overflow, failed startup deadline, failed restart) | (a) return to the title with a message, after the leave-time save where one exists; (b) restart the session in place with a notice; (c) stop the game with a diagnostic | (a) — accepted |
| D11 | Time-trial water (raised by L2; accepted by the user on 2026-11-10) | (a) keep the bundled trial on plain water while course packages ride the arcade wake pool; (b) every time trial rides the wake pool, as free boating and rooms do | (b) — accepted 2026-11-10. Bests move to `course-{calm\|waves}-physicsN-wakes.run`; plain-water bests stay on disk, are never loaded and cannot be compared. The real effect is the player's own wake under the hull and the jumps it causes; how much it changes a human lap is unknown. A test autopilot's lap fell from 57.80 s to about 41.8 s, but that is an artifact: it circles every gate (about 1363° of turning over 526 m on a course about 145 m long), and crossing its own wakes (2.6 s airborne, 3 landings) puts it on a line with one loop fewer; its mean speed drops from 9.15 to 8.81 m/s. With the pool present but empty the lap stays 57.80 s, and a full-throttle pilot is slower on the pool (30.5 → 32.4 s). |

## Evidence

- Resync nit from PR #176: a checkpoint at the mirror tick is installed (`<`), a snapshot at it
  is not (`<=`). It is the repair path, not a defect: the checkpoint carries the full wake pool
  that a snapshot only digests. Kept; comment and protocol spec corrected; `resync_same_tick`
  (memory and ENet) fails under `<=`.
- Proxy measurement above (rejected scaled-`now` scheme, 16 ticks per frame; the 96-tick figure
  is extrapolated); scratch harness only, nothing committed.
