# Plugin P5: authored jobs in rooms, the co-op working harbour first

Status, 2026-11-07: **planned, nothing implemented.** Scheduled by the user "as soon as possible"
(2026-11-07), with the co-op working harbour as the first scenario that must work in a room.
Open: the [decisions](#decisions-for-the-user) below, chiefly the role model (recommended: one
shared workboat with HELM and CRANE roles). Done when: two players (then four connections) share
the `proteus.outer-work` job in a hosted room with Lua only on the authority, the room autosaves
and resumes, a session is recorded and reconstructed with its revision, definition revisions
commit at round boundaries, existing free/race/deathmatch rooms still pass, and the closing step
has moved the contract into specs. Next task: the user's answers, then
[P5a](#p5a-one-native-work-step-for-solo-and-rooms).

This plan owns P5's design, steps and gates. The
[plugin plan](PLUGIN_PLAN.md#p5--authority-prediction-and-recorded-revisions) keeps the P5
milestone summary and the general
[multiplayer contract](PLUGIN_PLAN.md#multiplayer-prediction-and-recording);
the [sandbox plan](FREE_RIDE_SANDBOX_PLAN.md#playable-slices-and-gates) owns S7's gameplay goal;
the [protocol](MULTIPLAYER_PROTOCOL.md) owns bytes once each step lands. [All plans](README.md).

## Scope and what P5 defers

In scope: the P5 milestone as written in the plugin plan (authority-side objective callbacks and
commands, the command/checkpoint contract, readiness and rejection, late join and reconnect,
loss/reordering/duplicates, stale revisions, failed room reload, prediction replay under virtual
time, mixed native/browser clients, old-client rejection, round-boundary definition revisions,
recording/reconstruction, page limits and four-connection load), proven on the co-op job.

Deferred, checked against the [Open loops](README.md#open-loops) board on 2026-11-07:

- Sandbox S1 fun review and S0c qualification stay open; P5 does not wait for them, but a "not fun
  solo" verdict would also apply to the room.
- A second workboat, several crates, cargo/cargo contact, money, fuel and progression (sandbox
  S2–S6). Per-player workboats need a multi-boat cargo core and are not P5.
- R2 loaded maps and content distribution ([room content plan](ROOM_CONTENT_PLAN.md)): clients
  receive definitions as data and need no package, so P5 needs no download path.
- M5's impairment matrix, soaks and fuzzing beyond the work-room cases listed here; M4/M6–M8
  human acceptance; the deathmatch playtest; the free-package scene map-check gap from S1a.
- Seamless mid-round definition edits, shared-room agent editing (P6) and browser-side Lua.
- A Winterfell work room (see [Hosting](#hosting-and-browsers)).

Naming correction: older documents call round-boundary revisions "protocol v10". The protocol is
already v12 ([v10 was the dimensioned marina](MULTIPLAYER_PROTOCOL.md#multiplayer-protocol-v12)),
so this plan numbers its bumps v13 and v14 and the closing step retires the "v10" label.

## Starting point in the code

- **Solo session.** `BoatWorkSession` (`src/boat_work_session.h:18`) owns one `Boat`, the
  `BoatCargoSandbox`, the tick, the job's `BoatPluginObjective` and its event queue. Its tick
  (`src/boat_work_session.c:314`) runs crane motors and aim assist, `boat_step_handling` with the
  workboat hull, the secured-load effect, assisted mooring and the quay circle, then the cargo
  step and `boat_work_job_tick` (`:122`), which runs Lua every 12 ticks and checks
  `work.enable`/`cargo.supply`/`work.guide` against the live sandbox before committing.
- **Cargo core is single-boat.** `BoatCargoSandbox` (`src/boat_cargo.h:92`) has one workboat
  preset, one `moored`/`mooring`/`active` and one crate (`BOAT_CARGO_SNAPSHOT_LOADS 1`, `:105`).
  It is in `proteus_boat_core` (`CMakeLists.txt:65`) and pointer-light.
- **Rooms.** `boat_fleet_step` (`src/boat_fleet.c:286`) steps four slots with the speedboat's
  `boat_step` (`:316`); `BoatFleetState` (`src/boat_fleet.h:29`) already carries optional
  combat state, which is checkpointed, hashed, predicted by fleet copy and sent as compact pages
  (`src/boat_net_server.c:602`). The server tick (`src/boat_net_server.c:638`) observes match
  or race rules after the fleet step. Room definitions refuse work sites
  (`src/boat_room_definition.c:640`).
- **Boundaries.** `proteus_boat_net` may not link Lua or the plugin host
  (`CMakeLists.txt:1216`); `tools/boat_server.c` links `proteus_plugins` and can.
- **Wire.** Protocol v12 (`src/boat_net_protocol.h:18`), 1100-byte messages, 16 KiB checkpoint
  cap (`:35`), free/race checkpoint 5352 B and deathmatch 9068 B
  ([checkpoint](MULTIPLAYER_PROTOCOL.md#world-checkpoint)); `BoatNetCommand` (`:126`) is six
  bytes with a torpedo request serial as precedent for exactly-once actions.
- **Prediction** copies the whole fleet and replays own commands, repeating remote rates
  for 24 ticks (`src/boat_prediction.h:14`, `:48`). Anything in `BoatFleetState` is predicted.
- **Saves.** SV1's `BoatSandboxSave` (`src/boat_sandbox_save.h:38`) holds the cargo snapshot,
  objective checkpoint, queued events and sequence; `BoatAutosave` is pure tick logic (`:76`).
- **Hosting.** HOST A ROOM runs the game as a `--serve` child (`tools/wake_velocity_host.h:7`)
  with modes free/deathmatch/race. Release builds are ENet-only; browsers join only rooms
  built with the optional WebRTC endpoint ([web build](WEB_BUILD.md#webrtc-rooms)).

## Design

### Room shape: one shared workboat, two roles (recommended)

A **work room** has one room-owned workboat in fleet slot 0 and the package's sites and crate.
Players are crew. **HELM** steers and moors; **CRANE** slews, reaches, hoists, hooks and aims.
Lash and recover belong to HELM. A lone crew member holds both roles and TAB swaps view, exactly
as solo. With two, the first joiner takes HELM, the second CRANE; TAB asks to swap and the
authority swaps when both agree or the other role is free. Leaving or timing out frees a role,
which the remaining player inherits.

Why this rather than per-player boats: S7 asks for shared helm/crane work; the cargo core stays
single-boat and unchanged; there is one crate, so per-player boats would make one player idle;
and the room's save is the solo save's shape, so one harbour world can continue solo or hosted.
The cost is that players have no hulls of their own in the first slice.

Toward four: the third and fourth connections join as crew **without a role** (they ride, see
the HUD and take a role when one frees). This gives the four-connection load and join paths
without new cargo physics. Own speedboats for them (fleet slots 1–3) or a second crew are later
decisions. In work rooms the roster lists players, not hulls; only slot 0 has a boat.

### Where the simulation runs

P5a moves the physical part of the solo tick into one core function, used by the solo session
and by the fleet. The fleet gains an optional `BoatFleetWork` beside `combat`: enabled flag,
the sandbox's dynamic state (crate, crane slew/reach/rope, hidden flags, active, source, moored,
mooring), the workboat preset index (no pointers), role holders, aim, guide and target, and the
last consumed action serial per role. `boat_fleet_step` steps slot 0 with the workboat's
handling, load effect, mooring and quay contact, then the cargo, in the documented stage order,
before wakes are tracked. Free, race and deathmatch fleets leave it disabled and keep their
hashes and checkpoint bytes unchanged.

The native cargo events of a tick go to a bounded queue in the fleet state (as today's
`BoatCargoSandbox.events`). The authority drains it after each step; the Lua job never runs
inside `proteus_boat_core` or `proteus_boat_net`.

### Lua runs only on the authority

`BoatNetServerConfig` gains an optional rules adapter: function pointers with a context, the
same pattern as `BoatNetTransport`. After each tick the server calls `observe(context, fleet,
events)`; at a 12-tick boundary the adapter runs the objective update and returns a committed
batch (built sites, supply, guide, HUD rows, notice, waiting reason) which the server applies to
the fleet between ticks. `tools/boat_server.c` implements it with today's `boat_work_job_tick`
logic, factored so the solo session and the adapter share it. Net stays Lua-free.

The P3 transaction holds unchanged: a failed intent check keeps the previous checkpoint and
the unacknowledged events, a script error quarantines the job, the boat and crate keep
simulating, and the HUD shows the job as suspended. The objective checkpoint (up to 16 KiB of
state) stays on the authority; clients never receive Lua state, only the HUD as data.

### Commands, actions and exactly-once

`BoatNetCommand` gains three `i8` crane rates (slew, reach, hoist, ×127 like steering) and an
action pair: `u8` action kind (hook, lash, moor, recover, aim 1–3, role swap) and `u8` serial.
The client bumps the serial for each press and repeats the latest pair in every command until a
WorkPage reports it consumed. The authority applies an action once when its serial differs from
the consumed one for that player, if the player holds the action's role; otherwise it counts a
`forbidden` and ignores it. Duplicates, repeats in later packets, reordering and a late arrival
of a stored tick therefore apply at most once, and a lost tick is recovered by the next packet.
Commands still carry the slot `life`; RESTART JOB starts a new workboat life, so stale actions
from before it are dropped by the existing life fence.

Merged input: slot 0 integrates HELM's steering/throttle and CRANE's rates, each through the
existing repeat/zero/neutral policy for that player. The snapshot's `used` command for slot 0
is the merged one, so remote repeat in prediction already uses both roles' last rates.

Job effects are exactly-once by construction: deliveries are decided by the native cargo state
(`delivered`, `receivers`), events are created once on the authority, and the objective
acknowledges them in the same committed batch. Clients cannot send job commands at all.

### What clients predict and what they interpolate

- **Predicted** (in the fleet replay, by every crew client): the workboat and the crane, with
  the client's own fields for the roles it holds and the authority's last rates for the rest.
  The hanging crate follows from the same replay, so the rope stays attached on screen.
- **Not predicted:** actions. A hook, lash, moor or recover shows after the authoritative state
  arrives (a round trip); the HUD marks a pending action. Predicting them risks a crate that
  jumps back on a refused hook. Revisit after the first play check.
- **Data only:** HUD rows, notices, waiting text, guide/target, built sites and supply. Built
  sites and a supplied crate enter the fleet state at the commit tick; prediction picks them up
  at the next correction. A build is refused while a boat or the crate is inside the site's
  envelope (`boat_cargo_site_occupied`), so it cannot push a predicted hull.
- Riders without a role predict nothing of their own and replay remote rates like any client.

Corrections use the existing blend/snap thresholds for slot 0; the crate gets a separate blend
because a pendulum amplifies small differences. Native and Wasm clients run the same C, but
`atan2f`/`hypotf` in the aim assist and cargo step may differ by libm; P5e measures it.

### State on the wire and byte budget

| Item | Where | Budget (estimate, checked in P5d) |
| --- | --- | --- |
| Work sites, presets, headings, hidden flags, workboat preset, notices | Room definition v4 (scene v4 `work` block) | ≤ 600 B of the 16 KiB definition |
| Fleet work section (crate pose/velocity/state/support/flags, 4 cranes' slew/reach/rope/hidden, active/source/moored/mooring, roles, aim, guide, target, consumed serials) | Appended to the checkpoint after combat; hashed by `boat_fleet_hash` | ≤ 256 B; free checkpoint 5352 → ≤ 5608 B |
| The same section | **WorkPage** (new type) on the registry channel at snapshot cadence, tied to the snapshot tick like CombatPage | one page, ≤ 300 B; ≈ 9 KB/s per client at 30 Hz |
| HUD rows (≤ 8 × 24-byte label + `i64`), notice kind/site, waiting code, quarantine flag, revision/generation | **WorkState** (new type), reliable control channel, on change and after Roster for a joiner | ≤ 400 B, one message |
| Crane rates and action pair | InputBatch commands: 6 → 11 B each | 12 × 11 = 132 B per packet |
| Lua state, timers, RNG, unacknowledged events | Authority only (save and recording) | none on the wire |

### Join, late join, reconnect and leave

A joiner gets Accept (mode `work`), Roster with roles, WorkState, the definition and the
checkpoint, then WorkPages. A mid-lift join installs the crate hanging, from the checkpoint; no
event is re-sent. A reconnect is a new peer: it gets a free role or rides. If the last crew
member leaves, the room keeps simulating (the crate settles; the workboat drifts at idle while
moored or unmoored) and autosaves; with `--until-empty` the host stops after saving. HELM leaving
mid-mooring keeps the assist running (it is one-way and bounded); CRANE leaving mid-lift leaves
the hook where it is, and the inheriting player takes over.

### Revisions and reload

Two kinds, handled differently:

- **Authority-only revision**: the Lua source or display text changes, the definition bytes do
  not. Clients run no Lua, so no readiness is needed. With `--package-reload` the host stages the
  candidate with the P3 reload contract at an objective boundary (same `state_version` keeps
  state, otherwise `migrate` or refuse), bumps the revision in WorkState, and keeps the running
  revision on failure. This also gives the live authoring loop on a hosted room.
- **Definition revision**: the sites, scene, course or rules change. It commits only at a round
  boundary (race RESULTS/WAITING, deathmatch between rounds) or, for free and work rooms, when the
  room is empty or restarted. A changed work digest starts a new harbour world (and a new save
  identity), as solo. The authority sends RevisionBegin/Page/End with a revision number and a
  commit tick; clients validate and stage, answer RevisionReady, and everyone switches at that
  tick. A missed deadline disconnects that client with a `REVISION` reason; a failed candidate
  keeps the running revision; duplicate and stale announcements are ignored by revision number;
  the fleet, prediction history and WorkState carry the revision so nothing crosses a commit.

### Persistence: the room autosaves

The authority saves; clients never do. `boat_server` gets `--save PATH` and `--data-dir DIR`
(the game passes its data directory to the `--serve` child). It reuses SV1's format, identity
and `BoatAutosave` policy, driven by room ticks: a milestone write after a delivery, supply,
build or mooring change; a periodic write after 3600 dirty ticks; and a write when the room
empties, on shutdown and when the host's pipe closes. Writes happen in the server loop between
service calls, never in a tick. The workboat is saved as SV1 saves the solo boat (moored at the
active berth, or its plane pose at rest); crew and roles are not saved.

Recommended default: **the hosted room continues the same harbour world as Single Player**
(`cargo-<package>.sav`): the HOST A ROOM panel's WORKING HARBOUR row offers the discovered
CONTINUE/NEW exactly as Single Player does, and both write the same file. The SV1 format needs no
change if crew state stays out of it. A lock file (`.lock` beside the save, removed on exit, stale
after the owning process dies) keeps a second game from writing the same world at once.

### Recording and reconstruction

`boat_server --record PATH` writes an authority-side session file: a header with the content
identity (package id, source hash, revision, generation, protocol, physics, tick rate), the room
definition bytes, an initial keyframe (fleet checkpoint with the work section, objective
checkpoint, queued events), then per tick the integrated `BoatNetSlotInput` set and the consumed
actions, each committed job batch (commands, HUD, notice) at its tick, joins/leaves, and a
keyframe every 10 s and at every revision commit. Viewing replays the fleet natively and applies
the recorded job batches: no Lua runs by default. A diagnostic mode may re-run the exact pinned
source and compare. Seek restores the nearest keyframe and replays. It is separate from `.run`
pose files and bests. Size estimate for a two-player hour: about 10 MB of raw inputs and
2 MB of keyframes, before any delta encoding.

### Hosting and browsers

- **HOST A ROOM first:** a `work` choice (`--host work`) on Gothenburg Harbour, LAN, two
  players, the host's saved world. This is the first playable path.
- **Browsers** need no Lua and no package: the room sends data. The web build must draw a work
  room from fleet state and WorkState (the solo work glue is native-only today,
  `CMakeLists.txt:309`). Because release builds are ENet-only, browser joining is qualified on a
  locally built WebRTC server (`tools/check_web_multiplayer.py`), not on HOST A ROOM.
- **Winterfell** keeps its one deathmatch room in P5. A hosted work room (second port or a
  switch) is a user decision after the LAN play check; it would also be the only path for human
  browser co-op.

### Protocol versions and compatibility

- **v13 (P5d):** room mode `work` (3), room definition and scene v4 carrying the work block,
  11-byte commands in every mode, the checkpoint work section, WorkPage, WorkState, roster roles.
  Every v12 peer is rejected at Hello with the existing version reject; free, race and
  deathmatch rooms run unchanged otherwise (fixtures re-pinned, behaviour identical).
- **v14 (P5i):** RevisionBegin/Page/End, RevisionReady, RevisionCommit and the `REVISION`
  leave reason; all modes.
- Winterfell moves only at a release, which takes whichever version is current; the release
  checklist already rebuilds native, browser and the hosted room together.

## Steps

Each step is one PR (one commit where small), stacked on its parent per AGENTS.md. Every step
runs the Debug build and `ctest --preset full`; tests drive virtual time (memory transport,
scripted ticks), never sleep, and pass the `test_time` lint. Each updates this status header.

### P5a: one native work step for solo and rooms

Move the physical part of `boat_work_session_tick` (motors, aim assist, workboat handling, load
effect, mooring, quay circle, cargo step) into a core function over a boat and a sandbox, and
the intent check and apply of `boat_work_job_tick` into a shared function over a sandbox and a
boat. The solo session calls both. No behaviour change.
Files: `src/boat_cargo.[ch]` or a new `src/boat_work_step.[ch]` (core), `src/boat_work_session.c`.
Tests: the scripted landing delivery and quay pickup produce identical boat/cargo/objective
hashes per tick before and after (recorded golden digest); `validate_boat_cargo` and
`validate_boat_work_menu` pass unchanged; core boundary check.
Gate: byte-identical solo traces; CPU capture of WORKING HARBOUR unchanged.

### P5b: the workboat and cargo in the fleet

Add `BoatFleetWork` to `BoatFleetState`, enable it with sites and a workboat preset, step slot 0
through the P5a function, queue cargo events, include it in save/restore and `boat_fleet_hash`.
Workboat footprint in pair contacts and spawn checks uses its hull (today all slots use the
speedboat footprint).
Files: `src/boat_fleet.[ch]`, `src/boat_fleet.c` stage comment, `tools/validate_boat_fleet.c`.
Tests: a fleet-driven landing delivery matches the solo session's hashes; checkpoint at a
mid-lift tick restores and resumes 600 ticks bit-identically; free/race/deathmatch fleet hashes
and checkpoint bytes unchanged.
Gate: fleet and solo agree; no other mode changes.

### P5c: the job on the authority

Add the rules adapter to `BoatNetServerConfig` and call it from the server tick; implement it in
`tools/boat_server.c` over `BoatPluginObjective`. Let `boat_room_package_load` accept a free
package with `scene.work` and an objective for mode `work` (server side only; the definition
still refuses work sites until v13). `boat_server --scripted --mode work` runs the landing
delivery headless and prints the HUD.
Files: `src/boat_net_server.[ch]`, `tools/boat_server.c`, `tools/boat_room_package.[ch]`.
Tests: scripted run reaches `JETTY SUPPLIED: 1`; an injected script error quarantines the job
while the boat and crate continue; a refused build keeps the checkpoint and retries; boundary
check proves `proteus_boat_net` still links no Lua.
Gate: the job runs on the authority with identical committed digests to solo.

### P5d: protocol v13 and mirrored clients

Room definition/scene v4 with the work block; mode `work`; command extension and action serials;
checkpoint work section; WorkPage and WorkState; roster roles; role assignment, swap and
inheritance; merged slot-0 input. Clients install and mirror without predicting slot 0.
Files: `src/boat_net_protocol.[ch]`, `src/boat_room_definition.[ch]`, `src/boat_net_server.c`,
`src/boat_net_client.[ch]`, `tools/boat_bot.c` (a work script per role),
`tools/validate_boat_net.c`,
`docs/MULTIPLAYER_PROTOCOL.md` (v13 section).
Tests: golden bytes and round trips of the new types; every truncation; v12 Hello rejected;
measured definition, checkpoint and page sizes within the budget table; on the memory transport
two bot clients (HELM, CRANE) complete the landing delivery with authority and mirror hashes
equal at every snapshot tick; duplicated, reordered and late action packets consumed exactly
once; an action from the wrong role ignored and counted; stale-life actions dropped after a
restart; existing free/race/deathmatch room tests pass with re-pinned fixtures.
Gate: two bots deliver and supply over loss profile F with no divergence.

### P5e: prediction and the game client

Prediction merges own role fields with remote rates for slot 0 and replays the work section;
actions wait for authority. The game draws a work room from the fleet and WorkState (reusing the
SV1f props builder), with the job HUD, role line, pending-action mark and role-aware keys (TAB
asks to swap).
Files: `src/boat_prediction.[ch]`, `src/boat_remote_view.[ch]`, `tools/wake_velocity.c`,
`tools/wake_velocity_work.[ch]` (split a state-only props builder), `docs/BOAT_HUD.md`,
`docs/PLAYING.md`.
Tests: prediction replay bit-identical to the authority from held states on a steady link;
crate/boat correction statistics under profile F recorded with bounds; role change mid-lift
replays without a snap; CPU captures of a two-client room via a scripted connected capture (or
a validator frame dump, since `--connect` refuses `--screenshot`).
Gate: corrections within the recorded bounds; captures inspected.

### P5f: HOST A ROOM working harbour (first playable)

`--host work` and a WORKING HARBOUR row in the host panel; join from LAN.
Files: `tools/wake_velocity_host.[ch]`, `tools/boat_title.c`, `tools/boat_server.c`,
`docs/PLAYING.md`, `docs/COMMAND_LINE.md`.
Tests: `boat_net_process.py` work case (server + two bots, ENet, event-driven); title menu
validator for the new row.
Gate: **user play check 1** — two people on two machines on a LAN: one at the helm, one on the
crane, complete a landing delivery and a jetty pickup, swap roles, and judge the action latency
and crate corrections. Their verdict decides whether actions must be predicted.

### P5g: the room autosaves and resumes

`--save`/`--data-dir` on `boat_server`, `BoatAutosave` on room ticks, writes between service
calls, resume into the fleet, the lock file, and HOST A ROOM discovery of the shared world.
Files: `tools/boat_server.c`, `src/boat_work_save.[ch]`, `tools/wake_velocity_host.c`,
`tools/boat_title.c`, `docs/FREE_RIDE_SANDBOX_PLAN.md` (SV1 note), `docs/PLAYING.md`.
Tests: virtual-tick autosave policy in the room (milestone, periodic, empty room, shutdown);
save mid-lift then resume: crate on its last stable support, no duplicate or lost delivery, queued
events counted once; a solo save continued in a room and back; a held lock refuses a second
writer; a server started with `--scripted` or without a save path never touches the data dir.
Gate: **user play check 2** (short): host, deliver, close the room, host again and continue;
Single Player shows the same progress.

### P5h: late join, reconnect, impairment and four connections

Mid-lift joiner; HELM and CRANE each disconnect and reconnect; role inheritance; riders; loss,
reordering, duplication and delay profiles; four connections; resource and page limits.
Files: `tools/validate_boat_net.c`, `tools/validate_boat_room_package.c`, `docs/TESTING.md`.
Tests: hashes equal for every mirror including late joiners; no action applied twice across a
reconnect; WorkState converges after loss; four clients' bandwidth, server tick cost (Lua update
included) and client receive queue measured against the deathmatch baseline; maximum work
definition and busy checkpoint within caps.
Gate: measurements recorded; no divergence or unbounded queue.

### P5i: revisions (protocol v14)

Authority-only revisions with `--package-reload` at objective boundaries, and definition
revisions with the staged readiness protocol at round boundaries (race, deathmatch) or empty
rooms (free, work).
Files: `src/boat_net_protocol.[ch]`, `src/boat_net_server.c`, `src/boat_net_client.c`,
`src/boat_prediction.c`, `tools/boat_server.c`, `docs/MULTIPLAYER_PROTOCOL.md` (v14).
Tests (virtual time): success; a joiner mid-download; a missing readiness disconnects only that
client; an invalid candidate keeps the running revision; duplicate and stale announcements;
a commit exactly on a round boundary; a source-only revision keeps the job state and bumps the
HUD revision; a migrating revision; prediction history never crosses a commit.
Gate: all revision cases pass in every mode; existing room tests unchanged.

### P5j: recording and reconstruction

`--record PATH` on `boat_server`, the session file, and a reconstruction tool or validator mode
that seeks and compares.
Files: new `src/boat_session_record.[ch]` (codec, no files), `tools/boat_server.c`,
`tools/validate_boat_session_record.c`.
Tests: record a two-bot job with a revision and a reconnect; reconstruct every keyframe and
compare fleet hash, objective digest and HUD; seek to arbitrary ticks; a truncated file fails
cleanly; replay runs no Lua by default; the diagnostic re-run matches.
Gate: bit-identical reconstruction on the recording build.

### P5k: browsers and mixed clients

The web build draws work rooms; `check_web_multiplayer.py` gains a work case with one native and
one browser crew member on a local WebRTC server.
Files: `tools/wake_velocity_work_web.c` or the shared state-only builder,
`tools/check_web_multiplayer.py`,
`docs/WEB_BUILD.md`.
Tests: the automation completes a delivery with mixed clients; an old (v12) browser bundle is
rejected with the version message.
Gate: **user play check 3** — one native and one browser player on a local WebRTC room, or on a
Winterfell work room if the user chooses one.

### P5l: closing step

Move the contract into specs: the authority job adapter, room job events and limits into
`docs/PLUGIN_API.md`; v13/v14 into `docs/MULTIPLAYER_PROTOCOL.md` (already done per step; remove
plan wording); create `docs/WORKING_HARBOUR.md` (cargo rules, roles, save format, autosave policy)
and `docs/MULTIPLAYER.md` (authority, prediction, rooms, roles, revisions, hosting). Turn leftovers
into Open loops rows or drop them explicitly, retire the "v10" label, mark this plan done or move
it to history, and update the index.

## Decisions for the user

| # | Decision | Recommended default |
| --- | --- | --- |
| 1 | Role model | One shared workboat with HELM and CRANE roles; per-player workboats later with multi-crate cargo |
| 2 | Players 3–4 in a work room | Ride as crew without a role and take a freed one; own speedboats later |
| 3 | Room mode | A new `work` mode (clear menus, hosting and server branching) rather than an optional block on `free` |
| 4 | Hosted save | The same harbour world as Single Player (`cargo-<package>.sav`), with a lock file |
| 5 | Action prediction | Not predicted at first; decided by play check 1 |
| 6 | Revision exposure | Host-only `--package-reload`; no in-game UI in P5 |
| 7 | Recording | Opt-in `boat_server --record`; no client UI; outcomes replayed without Lua |
| 8 | Winterfell | No work room in P5; revisit after play check 1 (it is the only human browser co-op path) |
| 9 | Protocol bumps | v13 for work rooms and v14 for revisions as separate steps; a release takes the current one |
| 10 | Max players in the first playable step | Two (P5f); four connections only from P5h |

## Risks

- **Crate determinism across clients.** The hanging crate is a damped pendulum on a moving deck;
  small libm or ordering differences grow. Mitigation: separate crate blend, P5e statistics,
  periodic WorkPages; worst case, the crate is interpolated rather than predicted for non-crane
  clients.
- **Action latency feel.** Unpredicted hooks feel late over the internet; LAN may hide it.
- **Fleet contact footprint.** Pair contacts and spawn tests assume the speedboat footprint;
  the workboat needs its hull there (P5b), which touches a shared, well-tested solver.
- **Refactor of a just-accepted solo mode.** P5a must stay byte-identical, or solo regresses.
- **Server tick cost.** A fresh Lua instance per 10 Hz batch (P3 contract) now runs inside the
  authority's 120 Hz loop; P3.4 measured solo costs, not a busy room. Measure in P5h.
- **Browser receive queue** already overflowed in a deathmatch run (PD3 review); WorkPages add load.
- **Release ENet-only:** browsers cannot join HOST A ROOM; human browser co-op needs Winterfell
  or a WebRTC build.
- **Shared save world:** a room and a solo session must never write together; the lock and its
  stale-lock rule need tests on every platform.
- **Scope:** twelve steps; v13 (P5d) is the largest and may need splitting at review.

## Verification of this plan

Planning only, 2026-11-07: no code changed and no build or test run (documentation-only).
Read the plugin, plugin-API, default, multiplayer, protocol, room-content, sandbox and hosting
documents and the cited sources at commit `fae65e9`. `git diff --check` passed and every added
local link and anchor was checked. Byte sizes marked as estimates are not measured.
