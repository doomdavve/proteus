# Plugin P5: authored jobs in rooms, the co-op working harbour first

Status, 2026-11-07: **planned, nothing implemented; the user's decisions are
[settled](#decisions-settled-2026-11-07).** Scheduled by the user "as soon as possible", with the
co-op working harbour as the first room scenario: every player has their own workboat, crane and
crate on one shared job. Open: the [sub-decisions](#sub-decisions-with-recommended-defaults) that
the per-player model raises. Done when: two players (then four) share the `proteus.outer-work`
job in a hosted room with Lua only on the authority, the room autosaves to its own save and
resumes, a session is recorded and reconstructed with its revision, definition revisions commit
at round boundaries, existing free/race/deathmatch rooms still pass, and the closing step has
moved the contract into specs. Next task: confirm the sub-decision defaults, then
[P5a](#p5a-one-native-work-step-for-several-boats).

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
- Cargo/cargo contact, crates landing on another player's deck, stacking, several crates per
  player, money, fuel and progression (sandbox S2–S6).
- R2 loaded maps and content distribution ([room content plan](ROOM_CONTENT_PLAN.md)): clients
  receive definitions as data and need no package, so P5 needs no download path.
- M5's impairment matrix, soaks and fuzzing beyond the work-room cases listed here; M4/M6–M8
  human acceptance; the deathmatch playtest; the free-package scene map-check gap from S1a.
- Seamless mid-round definition edits, shared-room agent editing (P6) and browser-side Lua.
- A Winterfell work room (settled: not in P5; see [Hosting](#hosting-and-browsers)).

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

### Room shape: one workboat each, one shared job

A **work room** gives every player a workboat in their own fleet slot, with their own crate.
The package's sites are shared: one platform, crane, berth and pad per site, as in solo. Each
player drives and works the crane alone, with TAB between helm and crane view as solo. The first
slice admits two players; P5h raises the cap to four with no new rules.

Sharing rules (each a [sub-decision](#sub-decisions-with-recommended-defaults) with its default):

- **Berth lease.** A site's berth holds one workboat. The first boat whose assisted mooring
  starts there leases it until it departs; another boat's moor request is refused with
  `BERTH TAKEN`. The lessee alone drives that site's crane, so two cranes never work one pad.
- **One crate per pad.** A pad holds one resting crate. The hook will not release a crate over
  a pad occupied by another crate (`PAD BUSY`), a supply waits while its pad is occupied
  (today's `SUPPLY WAITING` path), and a player's first crate appears on the source pad as soon
  as it is free. Two crates therefore never meet on a pad or receiver.
- **Shared job total.** The job is one objective for the room. Cargo events gain a `crew` field
  (the player's slot, from 1; omitted in solo and read as 1), so the script can tell crates
  apart, and deliveries count toward one total. `cargo.supply {id}` keeps its shape: it uses up
  the oldest delivered, ready crate and places that crate owner's next load. The bundled job is
  updated in P5c to supply each crew's next load after their landing delivery and to count jetty
  supplies; with one crew it behaves exactly as today.
- **Contacts.** Workboats collide through the fleet's pair contacts, using the workboat hull. A
  crate touches only its owner's deck, the pads and the water; it passes through other boats.

### Where the simulation runs

**The cargo core stays single-boat.** One `BoatCargoSandbox` instance per fleet slot holds that
player's crate, mooring state and crane slew/reach/rope for each site; `boat_cargo_step` keeps
taking one boat. The multi-player rules live one layer up, in a small core module over the
instances: shared site state (built flags, berth lessee, pad occupant per site) copied into each
instance before its step, the lease check before a moor, and the pad check before a release,
supply or first placement. Only a site's lessee moves its crane; renderers draw each site's crane
from its lessee's instance, or at rest. This keeps the tested solver and SV1 snapshot code as they
are and adds the coordination as plain checks, rather than turning the sandbox into a multi-body
solver now.

P5a refactors the solo tick for that shape: the physical part (motors, aim assist, workboat
handling, load effect, mooring, quay circle, cargo step) becomes one core function over a boat
and one instance, with two optional predicates, `berth_free(site)` and `pad_free(site)`, that
solo passes as always-true. The intent check and apply of `boat_work_job_tick` becomes a shared
function over the instances. The fleet gains an optional `BoatFleetWork` beside `combat`: the
instances, the shared site state, the workboat preset index (no pointers), guide and target, the
last consumed action serial per slot, and a bounded cargo event queue tagged with the slot.
`boat_fleet_step` steps each active slot with the workboat's handling and its instance, in the
documented stage order, before wakes are tracked. Free, race and deathmatch fleets leave it
disabled and keep their hashes and checkpoint bytes unchanged.

The authority drains the event queue after each step; the Lua job never runs inside
`proteus_boat_core` or `proteus_boat_net`.

### Lua runs only on the authority

`BoatNetServerConfig` gains an optional rules adapter: function pointers with a context, the same
pattern as `BoatNetTransport`. After each tick the server calls `observe(context, fleet, events)`;
at a 12-tick boundary the adapter runs the objective update and returns a committed batch (built
sites, supply and its crew, guide, HUD rows, notice, waiting reason) which the server applies to the
fleet between ticks. `tools/boat_server.c` implements it with today's `boat_work_job_tick` logic,
factored so the solo session and the adapter share it. Net stays Lua-free.

The P3 transaction holds unchanged: a failed intent check keeps the previous checkpoint and
the unacknowledged events, a script error quarantines the job, the boat and crate keep
simulating, and the HUD shows the job as suspended. The objective checkpoint (up to 16 KiB of
state) stays on the authority; clients never receive Lua state, only the HUD as data.

### Commands, actions and exactly-once

`BoatNetCommand` gains three `i8` crane rates (slew, reach, hoist, ×127 like steering) and an
action pair: `u8` action kind (hook, lash, moor, recover, aim 1–3) and `u8` serial. Each player
steers only their own slot, as today. The client bumps the serial for each press and repeats the
latest pair in every command until a WorkPage reports it consumed. The authority applies an
action once when its serial differs from that slot's consumed one. Duplicates, repeats in later
packets, reordering and a late arrival of a stored tick therefore apply at most once, and a lost
tick is recovered by the next packet. Commands still carry the slot `life`; a reset or RESTART
JOB starts a new life, so stale actions from before it are dropped by the existing life fence.

Job effects are exactly-once by construction: deliveries are decided per crate by its native
state (`delivered`, `receivers`), events are created once on the authority, and the objective
acknowledges them in the same committed batch. A supply uses up exactly one delivered crate.
Clients cannot send job commands at all.

### What clients predict and what they interpolate

- **Predicted** (in the fleet replay): the client's own workboat, its crane at the leased site
  and its crate. Other players' boats, cranes and crates replay with their last rates, as remote
  boats do today.
- **Not predicted** (settled until play check 1): actions. A hook, lash, moor or recover shows
  after the authoritative state arrives (a round trip); the HUD marks a pending action. A lease
  or pad refusal is decided on the authority only.
- **Data only:** HUD rows, notices, waiting text, guide/target, built sites, leases and supply.
  They enter the fleet state at the commit tick; prediction picks them up at the next
  correction. A build is refused while any boat or crate is inside the site's envelope
  (`boat_cargo_site_occupied`, checked for every instance), so it cannot push a predicted hull.

Corrections use the existing blend/snap thresholds for boats; crates get a separate blend
because a pendulum amplifies small differences. Native and Wasm clients run the same C, but
`atan2f`/`hypotf` in the aim assist and cargo step may differ by libm; P5e measures it.

### State on the wire and byte budget

| Item | Where | Budget (estimate, checked in P5d) |
| --- | --- | --- |
| Work sites, presets, headings, hidden flags, workboat preset, notices | Room definition v4 (scene v4 `work` block) | ≤ 600 B of the 16 KiB definition |
| Shared site state (built, lessee, pad occupant per site), guide, target | Checkpoint work section, then WorkPage | ≤ 16 B |
| Per slot (N cargo bodies): crate pose/velocity/state/support/flags/load, 4 cranes' slew/reach/rope, active/source/moored/mooring, aim, consumed serial | Checkpoint work section after combat, for every slot with an instance; hashed by `boat_fleet_hash` | ≤ 160 B each: N = 2 → ≤ 340 B, N = 4 → ≤ 660 B; free checkpoint 5352 → ≤ 6012 B |
| The same sections, active slots only | **WorkPage** (new type) on the registry channel at snapshot cadence, tied to the snapshot tick like CombatPage | one page ≤ 700 B at N = 4; ≈ 10 KB/s (N = 2) to 20 KB/s (N = 4) per client at 30 Hz |
| HUD rows (≤ 8 × 24-byte label + `i64`), notice kind/site, waiting code, quarantine flag, revision/generation | **WorkState** (new type), reliable control channel, on change and after Roster for a joiner | ≤ 400 B, one message |
| Crane rates and action pair | InputBatch commands: 6 → 11 B each | 12 × 11 = 132 B per packet |
| Lua state, timers, RNG, unacknowledged events | Authority only (save and recording) | none on the wire |

### Join, late join, reconnect and leave

A joiner gets Accept (mode `work`), Roster, WorkState, the definition and the checkpoint, then
WorkPages. Its workboat spawns unmoored at the source site's approach (fleet spawn rules, clear
of other hulls) and its crate appears on the source pad when the pad is free. A mid-lift join
installs every crate as it is, from the checkpoint; no event is re-sent. A reconnect is a new
peer in the lowest free slot.

A leaving player's workboat is removed between ticks and its lease ends. Its crate stays in the
world, owned by the slot: a hooked or loose crate is released to its last stable support, a
lashed one returns to its last pad (the SV1 rule, since the deck is gone), and a delivered one
stays delivered. The next player in that slot inherits it. If everyone leaves, the room keeps
simulating and autosaves; with `--until-empty` the host stops after saving.

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

### Persistence: the room autosaves to its own save

Settled: a hosted room has **a separate save**, written automatically and discovered in the HOST
menu; Single Player progress stays apart. The authority saves; clients never do. `boat_server`
gets `--save PATH` and `--data-dir DIR` (the game passes its data directory to the `--serve`
child). Policy is SV1's `BoatAutosave` on room ticks: a milestone write after a delivery, supply,
build or mooring change; a periodic write after 3600 dirty ticks; and a write when the room
empties, on shutdown and when the host's pipe closes. Writes happen in the server loop between
service calls, never in a tick.

Identity and slot rules:

- **One room slot per package per data directory:** `room-cargo-<package id>.sav`. Single
  Player's discovery lists only `cargo-*.sav`, so it never sees room saves, and the HOST panel's
  WORKING HARBOUR row lists only `room-cargo-*.sav` with CONTINUE/NEW/DELETE as SV1 does.
- **Format 2** of the SV1 file: the header adds a room flag and the crew capacity; the compared
  identity is SV1's (package id, work digest, `state_version`, physics revision, tick rate) plus
  the room flag. A format-1 solo file is never offered to a room, nor the reverse. Every SV1
  verdict (`DAMAGED`, `PACKAGE CHANGED` and so on) and the `.rejected` set-aside apply unchanged.
- **Body:** the shared site state, the objective checkpoint, queued events and sequence, and one
  cargo record per slot that has a crate, keyed by slot, each restored at its last stable support
  with `delivered` and `receivers` kept. Boats, leases and players are not saved: on resume
  nobody is moored, and each joiner takes a slot and that slot's crate.
- A dedicated server writes only with an explicit `--save` or `--data-dir`; `--scripted` never
  touches a data directory. No lock file is needed, because no other program writes room saves.

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
  players, the host's room save. This is the first playable path.
- **Browsers** need no Lua and no package: the room sends data. The web build must draw a work
  room from fleet state and WorkState (the solo work glue is native-only today,
  `CMakeLists.txt:309`). Because release builds are ENet-only, browser joining is qualified on a
  locally built WebRTC server (`tools/check_web_multiplayer.py`), not on HOST A ROOM, and human
  browser co-op needs that local build too.
- **Winterfell** keeps its one deathmatch room; settled: no work room in P5.

### Protocol versions and compatibility

- **v13 (P5d):** room mode `work` (3), room definition and scene v4 carrying the work block,
  11-byte commands in every mode, the checkpoint work section, WorkPage and WorkState.
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

### P5a: one native work step for several boats

Factor the solo tick and job apply as described in [Where the simulation
runs](#where-the-simulation-runs): a core step over one boat and one instance with the
`berth_free`/`pad_free` predicates, a shared-site module over several instances (leases, pad
occupants, built flags), and the job's intent check/apply over the instances. The solo session uses
one instance and always-true predicates. No solo behaviour change.
Files: new `src/boat_work_step.[ch]` (core), `src/boat_cargo.[ch]` (release/supply/placement
predicates), `src/boat_work_session.c`, `tools/validate_boat_cargo.c`.
Tests: the scripted landing delivery and quay pickup produce identical boat/cargo/objective
hashes per tick before and after (recorded golden digest); two instances on one site list: a
second moor is refused while leased, a release over an occupied pad is refused, a supply waits
for its pad, the first crate of a second instance waits for a free source pad; core boundary.
Gate: byte-identical solo traces; CPU capture of WORKING HARBOUR unchanged.

### P5b: workboats and crates in the fleet

Add `BoatFleetWork` to `BoatFleetState` and enable it with the sites and a workboat preset; step
every active slot through the P5a step with its instance; queue slot-tagged events; include it
in save/restore and `boat_fleet_hash`. Workboat footprint in pair contacts and spawn checks uses
its hull (today all slots use the speedboat footprint).
Files: `src/boat_fleet.[ch]`, `src/boat_fleet.c` stage comment, `tools/validate_boat_fleet.c`.
Tests: a one-slot fleet delivery matches the solo session's hashes; two slots deliver in turn
through one landing; workboat pair contact at a berth; checkpoint at a mid-lift tick restores and
resumes 600 ticks bit-identically; free/race/deathmatch fleet hashes and checkpoint bytes
unchanged.
Gate: fleet and solo agree for one slot; two-slot rules hold; no other mode changes.

### P5c: the job on the authority

Add the rules adapter to `BoatNetServerConfig` and call it from the server tick; implement it in
`tools/boat_server.c` over `BoatPluginObjective`. Let `boat_room_package_load` accept a free
package with `scene.work` and an objective for mode `work` (server side only; the definition
still refuses work sites until v13). `boat_server --scripted --mode work` runs the landing
delivery headless and prints the HUD.
Files: `src/boat_net_server.[ch]`, `tools/boat_server.c`, `tools/boat_room_package.[ch]`,
`plugins/outer-work/main.lua`, `docs/PLUGIN_API.md` (the `crew` field).
Also update `plugins/outer-work/main.lua` for the shared total (`crew` on events; each crew's
landing delivery supplies their next load) with solo behaviour unchanged.
Tests: scripted runs with one and two crews reach the jetty; the solo golden digest still holds;
an injected script error quarantines the job while boats and crates continue; a refused build
keeps the checkpoint and retries; a supply goes to the oldest delivered crate's owner; boundary
check proves `proteus_boat_net` still links no Lua.
Gate: the job runs on the authority with identical committed digests to solo.

### P5d: protocol v13 and mirrored clients

Room definition/scene v4 with the work block; mode `work`; command extension and action serials;
checkpoint work section; WorkPage and WorkState; two-player cap. Clients install and mirror
without predicting the work state.
Files: `src/boat_net_protocol.[ch]`, `src/boat_room_definition.[ch]`, `src/boat_net_server.c`,
`src/boat_net_client.[ch]`, `tools/boat_bot.c` (a work script),
`tools/validate_boat_net.c`,
`docs/MULTIPLAYER_PROTOCOL.md` (v13 section).
Tests: golden bytes and round trips of the new types; every truncation; v12 Hello rejected;
measured definition, checkpoint and page sizes within the budget table; on the memory transport
two bot clients each deliver their crate and share the berths and pads, with authority and
mirror hashes equal at every snapshot tick; duplicated, reordered and late action packets
consumed exactly once; contested berth and pad refused on one side only; stale-life actions
dropped after a restart; a third connection refused as full; existing free/race/deathmatch room
tests pass with re-pinned fixtures.
Gate: two bots deliver and supply over loss profile F with no divergence.

### P5e: prediction and the game client

Prediction replays the work state with the own slot's crane rates and remote rates for the
rest; actions wait for authority. The game draws a work room from the fleet and WorkState
(reusing the SV1f props builder per instance), with the job HUD, `BERTH TAKEN`/`PAD BUSY` notices
and a pending-action mark.
Files: `src/boat_prediction.[ch]`, `src/boat_remote_view.[ch]`, `tools/wake_velocity.c`,
`tools/wake_velocity_work.[ch]` (split a state-only props builder), `docs/BOAT_HUD.md`,
`docs/PLAYING.md`.
Tests: prediction replay bit-identical to the authority from held states on a steady link;
own and remote crate/boat correction statistics under profile F recorded with bounds; a remote
crane mid-lift replays without a snap; CPU captures of a two-client room via a scripted
connected capture (or a validator frame dump, since `--connect` refuses `--screenshot`).
Gate: corrections within the recorded bounds; captures inspected.

### P5f: HOST A ROOM working harbour (first playable)

`--host work` and a WORKING HARBOUR row in the host panel; join from LAN.
Files: `tools/wake_velocity_host.[ch]`, `tools/boat_title.c`, `tools/boat_server.c`,
`docs/PLAYING.md`, `docs/COMMAND_LINE.md`.
Tests: `boat_net_process.py` work case (server + two bots, ENet, event-driven); title menu
validator for the new row.
Gate: **user play check 1** — two people on two machines on a LAN, each in their own workboat:
both deliver a crate to the landing, share a berth and a pad in turn, and finish the jetty leg;
judge the action latency, crate corrections and whether the shared job feels cooperative. Their
verdict decides whether actions must be predicted.

### P5g: the room autosaves and resumes

`--save`/`--data-dir` on `boat_server`, `BoatAutosave` on room ticks, writes between service
calls, format 2 with per-slot cargo records, resume into the fleet, and HOST panel discovery of
`room-cargo-*.sav`.
Files: `tools/boat_server.c`, `src/boat_sandbox_save.[ch]`, `src/boat_work_save.[ch]`,
`tools/wake_velocity_host.c`, `tools/boat_title.c`, `docs/FREE_RIDE_SANDBOX_PLAN.md` (SV1 note),
`docs/PLAYING.md`.
Tests: virtual-tick autosave policy in the room (milestone, periodic, empty room, shutdown);
save with one crate hooked and one lashed, then resume: both on their last stable supports, no
duplicate or lost delivery, queued events counted once; a format-1 solo save is not offered to
the room and a room save is not offered to Single Player; a format-1 reader reports `NEWER
FORMAT` for a room file; `--scripted` or no save path never touches the data dir.
Gate: **user play check 2** (short): host, deliver, close the room, host again and continue from
the HOST menu; Single Player progress is unchanged.

### P5h: late join, reconnect, impairment and four connections

Raise the cap to four workboats. Mid-lift joiner; a player leaving with a hooked, lashed or
delivered crate and a joiner inheriting it; reconnect; four players contending for berths and
pads; loss, reordering, duplication and delay profiles; resource and page limits.
Files: `tools/validate_boat_net.c`, `tools/validate_boat_room_package.c`, `docs/TESTING.md`.
Tests: hashes equal for every mirror including late joiners; no action applied twice across a
reconnect; no crate lost or duplicated across leaves; WorkState converges after loss; four clients'
bandwidth, server tick cost (Lua update included) and client receive queue measured against the
deathmatch baseline; maximum work definition and a four-crate checkpoint and WorkPage within the
budget table.
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
Tests: record a two-bot job (two workboats) with a revision and a reconnect; reconstruct every
keyframe and compare fleet hash, objective digest and HUD; seek to arbitrary ticks; a truncated file
fails cleanly; replay runs no Lua by default; the diagnostic re-run matches.
Gate: bit-identical reconstruction on the recording build.

### P5k: browsers and mixed clients

The web build draws work rooms; `check_web_multiplayer.py` gains a work case with one native and
one browser player on a local WebRTC server.
Files: `tools/wake_velocity_work_web.c` or the shared state-only builder,
`tools/check_web_multiplayer.py`,
`docs/WEB_BUILD.md`.
Tests: the automation completes a delivery with mixed clients; an old (v12) browser bundle is
rejected with the version message.
Gate: **user play check 3** — one native and one browser player on a locally built WebRTC room
(Winterfell has no work room in P5).

### P5l: closing step

Move the contract into specs: the authority job adapter, room job events and limits into
`docs/PLUGIN_API.md`; v13/v14 into `docs/MULTIPLAYER_PROTOCOL.md` (already done per step; remove
plan wording); create `docs/WORKING_HARBOUR.md` (cargo rules, sharing rules, solo and room save
formats, autosave policy) and `docs/MULTIPLAYER.md` (authority, prediction, rooms, revisions,
hosting). Turn leftovers into Open loops rows or drop them explicitly, retire the "v10" label, mark
this plan done or move it to history, and update the index.

## Decisions settled 2026-11-07

The user answered on 2026-11-07:

| # | Decision | Settled |
| --- | --- | --- |
| 1 | Role model | **One workboat each**, with its own crane and crate, on one shared job (the shared HELM/CRANE boat was rejected) |
| 2 | Players 3–4 | Each joins with their own workboat; the cap stays at two until P5h |
| 3 | Room save | **A separate room save**, automatic and discovered in the HOST menu; solo progress stays apart |
| 4 | Winterfell | No work room in P5; browser checks use a locally built WebRTC server |
| 5 | Room mode | A new `work` mode (as recommended) |
| 6 | Action prediction | Not predicted until play check 1 (as recommended) |
| 7 | Revision exposure | Host-only `--package-reload` (as recommended) |
| 8 | Recording | Opt-in `boat_server --record` (as recommended) |
| 9 | Protocol bumps | v13 for work rooms, then v14 for revisions (as recommended) |
| 10 | Players in the first playable step | Two until P5h (as recommended) |

### Sub-decisions with recommended defaults

The per-player model raises these; each default is what the steps above assume.

| # | Question | Recommended default |
| --- | --- | --- |
| A | Core shape | Keep the cargo core single-boat, one instance per slot, plus a shared-site layer; no multi-body solver in P5 |
| B | Two boats at one site | Berth lease: first to start mooring holds it until departure; its lessee alone drives the crane |
| C | Two crates on one pad or receiver | One resting crate per pad: no release over an occupied pad, supplies and first placements wait for it |
| D | Job total | One shared total for the room; events carry `crew` |
| E | Who a supply goes to | The owner of the oldest delivered, ready crate; `cargo.supply {id}` keeps its shape |
| F | Crate contacts | Own deck, pads and water only; crates pass through other boats; no crate/crate contact |
| G | A leaver's crate | Stays in the world on its last stable support, owned by the slot; the next player in that slot inherits it |
| H | Joiner spawn | Unmoored at the source site's approach; the crate appears on the source pad when free |
| I | Room save slot | `room-cargo-<package id>.sav`, one per package per data directory, format 2 with per-slot crates; boats and leases not saved |

## Risks

- **Crate determinism across clients.** A hanging crate is a damped pendulum on a moving deck;
  small libm or ordering differences grow, now for every player's crate. Mitigation: separate
  crate blend, P5e statistics, WorkPages at 30 Hz; worst case, remote crates are interpolated
  rather than replayed.
- **Crates through other boats.** Default F lets a dropped crate pass through another player's
  boat; it may look broken in play. Real crate-to-hull contact is new physics, not P5.
- **Contention feels bad.** One berth and one pad per site may make two players wait on each
  other more than cooperate; play check 1 judges it. A second berth per site is a package and
  preset change, not a P5 rule.
- **Job script change.** The bundled job must learn `crew` without changing solo; the golden solo
  digest guards it.
- **Action latency feel.** Unpredicted hooks feel late over the internet; LAN may hide it.
- **Fleet contact footprint.** Pair contacts and spawn tests assume the speedboat footprint;
  workboats need their hull there (P5b), which touches a shared, well-tested solver.
- **Refactor of a just-accepted solo mode.** P5a must stay byte-identical, or solo regresses.
- **Bandwidth and server cost.** WorkPages grow with crates (≈ 20 KB/s per client at four), and
  a fresh Lua instance per 10 Hz batch now runs inside the 120 Hz authority loop. Measure in P5h.
- **Browser receive queue** already overflowed in a deathmatch run (PD3 review); WorkPages add load.
- **Release ENet-only:** browsers cannot join HOST A ROOM; browser checks need a WebRTC build.
- **Scope:** twelve steps; v13 (P5d) is the largest and may need splitting at review.

## Verification of this plan

Planning only, 2026-11-07: no code changed and no build or test run (documentation-only).
Read the plugin, plugin-API, default, multiplayer, protocol, room-content, sandbox and hosting
documents and the cited sources at commit `fae65e9`. `git diff --check` passed and every added
local link and anchor was checked. Byte sizes marked as estimates are not measured. Revised the
same day for the user's decisions (per-player workboats, separate room save).
