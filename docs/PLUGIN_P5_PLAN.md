# Plugin P5: authored objectives in rooms, a shelter hunt first

Status, 2026-11-08: **P5a-P5f implemented on `feat/plugin-p5`: the hunt's objective runs on
`boat_server`'s authority, clients receive its marks and HUD over protocol v13, the game
draws them and hosts the room (`--host hunt`, SHELTER HUNT in HOST A ROOM),
`boat_server --package-reload` reloads the objective source on a running room, and protocol v14
carries definition revisions (marks, scene, course, rules) in every room mode, committing at
round boundaries or in an empty free room. Play check 1 and the P5e reload check are accepted;
the user's moved-mark check (P5f) is owed; next: P5g. P5a–P5f are in PR #175.** Scheduled by the user "as soon as possible"
(2026-11-07). On 2026-11-08 the user replaced the first room scenario: a **shelter hunt in a free
room** instead of the co-op working harbour, so that P5 starts with as few moving parts as
possible ([decisions](#decisions)). Done when: two players (then four) play the
`proteus.outer-hunt` package in a hosted room with Lua only on the authority, late joiners and
reconnects see the same HUD, the authority can reload its source and commit definition revisions
at safe boundaries, a session is recorded and reconstructed with its revision, existing
free/race/deathmatch rooms still pass, and the closing step has moved the contract into specs.
Next task: the user's live-reload check of [P5e](#p5e-authority-only-reload), then
[P5g](#p5g-recording-and-reconstruction).

This plan owns P5's design, steps and gates. The
[plugin plan](PLUGIN_PLAN.md#p5--authority-prediction-and-recorded-revisions) keeps the milestone
summary and the general [multiplayer contract](PLUGIN_PLAN.md#multiplayer-prediction-and-recording);
the [protocol](MULTIPLAYER_PROTOCOL.md) owns bytes once each step lands. [All plans](README.md).

## Why a shelter hunt

The co-op working harbour (the [superseded draft](#superseded-co-op-working-harbour-draft)) needed
a cargo refactor, workboats in the fleet, crane commands with exactly-once actions, predicted
crates and a room save before anyone could play it. A shelter hunt needs none of that:

- **No new physics and no new client input.** Boats are the room's speedboats; marks are static
  circles. The native side only notices when a boat enters one.
- **No prediction change.** Objective results are data from the authority; clients show them
  after a round trip, which is fine for a score.
- **Proven objective shape.** It is the solo [shelter tour](../plugins/shelter-tour/main.lua)
  (shelter events in, `hud.set` rows out), extended with a crew number. That keeps the
  milestone's "already proven authored scenario" requirement.

What it still proves is P5's core: Lua runs only on the authority inside the P3 transaction,
events carry which player caused them, the HUD reaches every client as data (including late
joiners), quarantine leaves the room running, and the steps after it add revisions, recording
and browsers on the same base.

## The scenario

A free room on Gothenburg Harbour (`outer`) with five named **marks** spread over the harbour.
Each player collects every mark by driving into its circle. The HUD lists each crew's count
(`CREW 1  3/5`) and, once someone has all five, `FIRST: CREW 2`. When every connected crew has
all five, the script announces it and starts a new hunt (counts back to zero). Crews are fleet
slots, numbered from 1; the script does not know player names. A crew that leaves loses its
count; a new player in that slot starts at zero.

The hunt is a free room, so there are no rounds; boats still collide, capsize and respawn as in
any free room. Marks show on the minimap (as shelters do) and as markers in the world.

## Starting point in the code

- **Objectives.** `BoatPluginObjective` and `boat_plugin_objective_step`
  (`src/boat_plugin_state.h:80`) run `init`/`update` as a P3 transaction with a 12-tick cadence;
  the solo glue turns voyage notices into events (`tools/wake_velocity_plugin.c:371`).
- **Free packages** have no objective grant unless they author work sites
  ([free schema](PLUGIN_API.md#free-schema)).
- **Rooms.** `BoatRoomDefinition` (`src/core/boat_room_definition.h:32`) carries mode, location, course,
  rules and an optional scene; modes are free, deathmatch and race. `boat_room_package_load`
  (`tools/boat_room_package.h:20`) builds it on the authority. The server observes match or race
  rules after each fleet step (`src/net/boat_net_server.c`).
- **Boundaries.** `proteus_boat_net` may not link Lua or the plugin host (`CMakeLists.txt`);
  `tools/boat_server.c` links `proteus_plugins` and can.
- **Wire.** Protocol v12 before P5c, v13 after it (`src/net/boat_net_protocol.h`), message types up to
  `BOAT_NET_DEFINITION_END` (24); reliable control channel for match and race state.

## Design

### Marks: native data, native detection

A free package granted `objective.hud` may author `marks`: up to 16 entries of
`{ id, name, x, z, radius }` (radius 10–200 m, on open water, non-overlapping). They travel in the
room definition as data. A small core module (`src/core/boat_marks.[ch]`, in `proteus_boat_core`)
keeps, per fleet slot, which mark the boat is inside, and after each fleet step emits
`mark.reached { id, crew }` on entry (outside → inside). A respawn or a new life clears that slot's
inside flag, so re-entering counts as a new event; whether it scores is the script's business.

The authority also emits `crew.joined { crew }` and `crew.left { crew }` when a slot fills or
empties. Solo voyages are untouched: `crew` is a new optional field that only rooms set.

### Lua runs only on the authority

`BoatNetServerConfig` gains an optional **rules adapter**: function pointers with a context, the
same pattern as `BoatNetTransport`. After each tick the server hands it the tick's events; at a
12-tick boundary the adapter runs the objective update and returns the committed HUD (rows,
quarantine flag, revision), which the server sends when it changes. `tools/boat_server.c`
implements the adapter over `BoatPluginObjective`; net stays Lua-free.

The P3 transaction holds: a script error quarantines the objective, the room keeps running, and
the HUD shows the objective as suspended. Lua state stays on the authority; clients never
receive it.

### What goes on the wire (protocol v13)

| Item | Where | Budget (estimate, checked in P5c) |
| --- | --- | --- |
| Marks | Room definition v4, free mode, optional `marks` block | ≤ 16 × 40 B = 640 B of the 16 KiB definition |
| HUD rows (≤ 8 × 24-byte label + `i64`), quarantine flag, objective revision | **ObjectiveState** (new type), reliable control channel, on change and after Roster for a joiner | ≤ 300 B, one message |
| Lua state, timers, events | Authority only | none |

Nothing is added to commands, snapshots or checkpoints, so prediction is unchanged and free, race
and deathmatch rooms without marks keep their bytes apart from the version number. Every v12 peer
is rejected at Hello with the existing version reject.

### Join, reconnect and leave

A joiner gets Accept, Roster, ObjectiveState, the definition and the checkpoint. The authority
emits `crew.joined`; the script adds a row on its next update. A reconnect is a new peer in the
lowest free slot, so it is a new crew. A leaver's slot emits `crew.left`. No event is re-sent to
anyone: clients only ever see the resulting HUD.

### Revisions (protocol v14)

- **Authority-only revision**: the Lua source or display text changes, the definition does not.
  With `--package-reload` the host stages the candidate with the P3 reload contract at an
  objective boundary (same `state_version` keeps state, otherwise `migrate` or restart), bumps the
  revision in ObjectiveState and keeps the running revision on failure. This gives a live
  authoring loop on a hosted room without any client change, so it lands before v14.
- **Definition revision**: marks, scene, course or rules change. It commits only at a round
  boundary (race RESULTS/WAITING, deathmatch between rounds) or, for free rooms, when the room is
  empty. The authority sends RevisionBegin/Page/End with a revision number and commit tick;
  clients validate, stage, answer RevisionReady and switch at that tick. A missed deadline
  disconnects that client with a `REVISION` reason; a failed candidate keeps the running revision;
  duplicate and stale announcements are ignored by revision number; prediction history never
  crosses a commit.

Naming correction: older documents call round-boundary revisions "protocol v10". The protocol is
already v12 ([v10 was the dimensioned marina](MULTIPLAYER_PROTOCOL.md#multiplayer-protocol-v14)),
so this plan uses v13 and v14 and the closing step retires the "v10" label. Revisions landed in
v14 ([P5f](#p5f-definition-revisions-protocol-v14); bytes in the protocol document).

### Recording and reconstruction

`boat_server --record PATH` writes an authority-side session file: a header with the content
identity (package id, source hash, revision, protocol, physics, tick rate), the room definition
bytes, an initial keyframe (fleet checkpoint, objective checkpoint), then per tick the integrated
slot inputs, joins/leaves and each committed objective batch, with a keyframe every 10 s and at
every revision. Viewing replays the fleet natively and applies the recorded batches; no Lua runs
by default, and a diagnostic mode re-runs the pinned source and compares.

### Hosting and browsers

- **HOST A ROOM**: a SHELTER HUNT choice (`--host hunt`, a free room with the `proteus.outer-hunt`
  package) on LAN. This is the first playable path.
- **Browsers** need no Lua: the room sends data. Release builds are ENet-only, so browser joining
  is qualified on a locally built WebRTC server (`tools/check_web_multiplayer.py`).
- **Winterfell** keeps its one deathmatch room; no hunt room in P5.
- **No room save.** A hunt is a session: it starts over when the room starts. The working
  harbour's room save is deferred with it.

## Steps

All of P5 lands as **one PR from `feat/plugin-p5`, with one small commit per step** (the user,
2026-11-08: no stacks of PRs). Every step runs the Debug build and
`ctest --preset full`; tests drive virtual time (memory transport, scripted ticks), never sleep,
and pass the `test_time` lint. Each updates this status header.

### P5a: marks and the hunt package

The `marks` schema for free packages with `objective.hud`, its validation (open water, radius,
overlap, count), the core detector over fleet slots, and the bundled `plugins/outer-hunt`
package (five marks, the script above). No networking yet.
Files: new `src/core/boat_marks.[ch]`, the free-package loader and validator, `plugins/outer-hunt/`,
`tools/validate_boat_marks.c` (or a case in an existing validator), `docs/PLUGIN_API.md`.
Tests: the package loads and its marks validate on `outer`; invalid marks are refused with field
diagnostics; a scripted fleet driving through marks emits exactly one `mark.reached` per entry
with the right crew, and a respawn re-arms the slot; evaluating the script over scripted events
yields the expected HUD rows, `FIRST`, and the restart; core boundary check.
Gate: headless events and HUD match the expected tables.

**P5a result, 2026-11-08 (commit on `feat/plugin-p5`).** `src/core/boat_marks.[ch]` in
`proteus_boat_core` (validation, digest, the per-slot entry detector); `free.marks` parsed,
decoded, map-checked and carried over the browser wire; `objective.hud` may now follow
`free.define` directly; objective events gain an optional `crew`; `plugins/outer-hunt` (five river
marks, radius 25 m) is in the catalog. Rooms refuse it until P5b ("marks need a room objective");
`--plugin` refuses it as before for free objectives. The marks' positions were chosen from a
30 m-clearance scan of the outer map. Verification: Debug build warning-clean; new `boat_marks`
cases (validation, detector) and `boat_plugin` cases `marks` and `hunt_objective` pass, with
`boat_room_package.catalog_rooms` expecting the refusal; full results are in the PR. No visual
change, so no captures; nothing is playable yet.

### P5b: the objective on the authority

The rules adapter in `BoatNetServerConfig`, called from the server tick; its implementation in
`tools/boat_server.c` over `BoatPluginObjective`; join/leave events; quarantine.
`boat_room_package_load` accepts the hunt package for free rooms (marks stay server-side until
v13). `boat_server --scripted --package outer-hunt` drives scripted boats and prints the HUD.
Files: `src/net/boat_net_server.[ch]`, `tools/boat_server.c`, `tools/boat_room_package.[ch]`.
Tests: scripted one- and two-crew hunts produce the expected HUD per tick; an injected script
error quarantines the objective while boats keep moving; join/leave rows appear and disappear;
`proteus_boat_net` still links no Lua.
Gate: the objective runs headless on the authority.

**P5b result, 2026-11-08 (commit on `feat/plugin-p5`).** `BoatNetServerConfig` gains borrowed
`marks` (free rooms only) and a `BoatNetRules` adapter: crew joins and leaves are reported
between ticks, mark entries after the tick that made them, then `after_tick`. The server keeps a
`BoatMarkTracker` and runs no scripts. `boat_room_package_load_objective` returns the package's
marks and source (the plain loader refuses objectives), and `tools/boat_room_objective.[ch]`
runs the P3 transaction every 12 ticks, quarantining on a callback error or more than 64 queued
events. `boat_server` (also the game's `--serve` child) prints `objective tick N: ...` on each HUD
change. Instead of a `--scripted --package` flag (scripted mode has no peers to join or leave),
`validate_boat_room_objective` drives the adapter directly and runs a memory-transport room with
two clients through the marks. Verification: `boat_net.rule_events` and the four
`boat_room_objective` cases pass; a manual ENet run on macOS (`boat_server --package
proteus.outer-hunt` with two `boat_bot` joins in turn) printed a crew row on join and removed it
on leave; full suite results are in the PR.

### P5c: protocol v13 and mirrored HUD

Room definition v4 with marks; ObjectiveState; version bump; client stores the HUD; `boat_bot`
prints it.
Files: `src/net/boat_net_protocol.[ch]`, `src/core/boat_room_definition.[ch]`, `src/net/boat_net_server.c`,
`src/net/boat_net_client.[ch]`, `tools/boat_bot.c`, `tools/validate_boat_net.c`,
`docs/MULTIPLAYER_PROTOCOL.md` (v13).
Tests: golden bytes and round trips; every truncation; v12 Hello rejected; definition and
message sizes within the budget; on the memory transport two bots hunt and both see identical
HUD after each change; a late joiner gets the current HUD before its first snapshot; HUD
converges after loss and reordering; free/race/deathmatch room tests pass with re-pinned fixtures.
Gate: two bots finish a hunt over loss profile F with matching HUD.

**P5c result, 2026-11-08 (commit on `feat/plugin-p5`).** `BOAT_NET_VERSION` is 13 and
`BOAT_ROOM_VERSION` 4. `BoatRoomDefinition.marks` is carried by flags bit 2; the server uses the
definition's marks (`BoatNetServerConfig.marks` is gone; `boat_room_package` copies them).
ObjectiveState (type 25, control channel, server to client) and `boat_net_objective_state_equal`
are new. `BoatNetRules.state` is polled at init and after every `after_tick`; the server assigns
the revision, broadcasts on change and sends the current state to each joiner after Roster.
`tools/boat_room_objective.c` implements `state` from `hud_changes` and quarantine. The client
keeps `objective` and `objective_valid` (newest revision wins) and `boat_bot` prints
`boat_bot objective: LABEL=value ... [suspended]`. The wire is big-endian like the rest of the
codec. Measured: the hunt's room definition is 213 bytes (5 marks); a definition with 16
maximum-length marks is 979 bytes (34 without; budget 1200); the largest ObjectiveState payload is
456 bytes (488 with the header). Verification (macOS, Debug): `cmake --build build -j`
warning-clean; `ctest --test-dir build --preset full -j8` 443 of 443 passed;
`validate_boat_net --case objective_state` (three clients identical at revision 6, loss profile F
for the last 7 s) plus the extended `golden`, `codec`, `room_definition_codec`, `rule_events`,
`boat_room_objective.room` and `room_package` catalog checks. Limits: no ENet process run
exercised the `boat_bot` print, and the game draws nothing until P5d.

### P5d: the game client and HOST A ROOM (first playable)

The game draws marks (world markers, minimap) and the objective HUD in rooms; SHELTER HUNT in the
host panel and `--host hunt`.
Files: `tools/wake_velocity.c`, `tools/wake_velocity_host.[ch]`, `tools/boat_title.c`,
the minimap, `docs/PLAYING.md`, `docs/COMMAND_LINE.md`, `docs/BOAT_HUD.md`.
Tests: `boat_net_process.py` hunt case (server + two bots, ENet, event-driven); title menu
validator for the new row; a CPU capture of marks and HUD (validator frame dump, since
`--connect` refuses `--screenshot`), inspected.
Gate: **user play check 1 (owed)** — two people on a LAN hunt the marks; judge whether the marks are
findable, the HUD readable, and the score delay acceptable.

**P5d result, 2026-11-08 (commit on `feat/plugin-p5`).** `--host hunt` and SHELTER HUNT (fourth
MODE row entry) start the child room `--serve --mode free --location outer --package
proteus.outer-hunt`; hunt, like race, fixes PLACE to the harbour and `--host hunt --location basin`
is refused. Marks reach the renderers as `BoatViewState.marks`
and `boat_view.c` draws each as a ring of buoys (alternating orange and white, a mast and flag
at the centre) through the same box visitor as the race gates, so the CPU and GPU renderers share
it; the minimap draws `BoatMinimap.room_marks` as a magenta dashed circle with a centre dot, named on
the chart; `net_study_draw_objective` draws the rows as a panel under the roster. Verification
(macOS, Debug): `cmake --build build -j` warning-clean; `ctest --test-dir build --preset full -j8`
444 of 444 passed (new: `wake_velocity_host_hunt_location_rejected`; `boat_net_process` gained a
hunt case with a server and two ENet `boat_bot` joins that must print `CREW` rows, and the server
`objective tick`). `./build/boat_hunt_review --screenshot build/hunt.png` (new CPU fixture, hidden
window) was inspected: the ring, mast and flag are readable from about 40 m, the panel aligns
labels and values, and the corner and chart minimaps show the marks. Limits: there is no title
menu validator in the repository, so the new row is only compiled and read; the GPU renderer
shares the code but its frame was not captured; labels on the chart overlap landmarks when marks
are close; no windowed `--host hunt` run, no browser build run, and **user play check 1 is still owed**
(findability of marks, HUD readability, score delay).

**P5d fix, 2026-11-08.** The user's first `--host hunt` failed (`--port needs a UDP port 1-65535,
not ''`): the POSIX spawn array had 14 slots and the hunt command line overflowed it into the port
text; behind that, the child got `--mode hunt` and the default basin location. The command line is
now built in one place, `net_host_arguments` (hunt maps to a free room on outer with its package),
sized by `NET_HOST_ARGUMENTS`, and `validate_wake_velocity_host` (quick) checks each mode's exact
line and that the longest one fits. Verified: a windowed `./build/wake_velocity --host hunt
--frames 300` started the room on outer and printed `objective tick 12: HUNT=1 CREW 1=0`; full
suite 446 of 446. The user then hosted SHELTER HUNT from the game and reported "Works great!"
(2026-11-08). The run was two game instances on one Mac joined over the local network (host plus
a LAN join), not two machines; the user judged it working. Play check 1 is accepted on that basis;
a two-machine run is still worth doing before the closing step.

### P5e: authority-only reload

`--package-reload` on the host: source changes staged at an objective boundary with the P3
contract, revision bumped in ObjectiveState, failure keeps the running revision. No protocol
change.
Files: `tools/boat_server.c`, `docs/PLUGIN_API.md`.
Tests (virtual time): same-`state_version` reload keeps counts; a migrating reload; a broken
candidate keeps the running revision and reports it; the revision reaches clients.
Gate: a live script edit on a hosted hunt reaches both players without a restart.

**P5e result, 2026-11-08 (commit on `feat/plugin-p5`).** `BoatRoomObjective` gained
`boat_room_objective_stage`, a pending candidate applied at the next 12-tick boundary before that
boundary's update, a `reload` result and a `revision` counter; the contract is in
[PLUGIN_API](PLUGIN_API.md#room-objective-reload-p5e). "Has migrate" reuses the loader's rule
(`BoatPluginFree.migrates`, now carried in `BoatRoomObjectiveSource.migrates`). Deviation from the
step text: the migration is stamped with the last committed tick, not the boundary, because the
P3 update that follows must be strictly later. The hunt's `main.lua` has a `migrate`, so edits keep
every crew's marks. Verification (macOS, Debug, 2026-11-08): build warning-clean; six new
`boat_room_objective.reload_*` cases (keeps counts, migrates, restart with crew replay, broken
candidates and quarantine, marks refused, two memory-transport clients seeing the revision move
with identical rows) and `boat_server_package_reload_rejected`; `ctest --test-dir build --preset full -j8` 453 of 453 passed (446 before). A real process
smoke (`boat_server --package-reload` on a copy of `plugins/outer-hunt`, one `boat_bot`, the copy's
`CREW` label edited to `TEAM`) printed `objective reload: revision 2 accepted (migrate)` on the
server and `boat_bot objective: HUNT=1 TEAM 1=0` on the bot, no restart. Limits: a file that
fails to load is reported once per distinct message, not per distinct content; the game's own
clients were not run against a reload; marks and every other definition change still need a room
restart until P5f (which lifted it). **Gate accepted 2026-11-09:** the user hosted with `boat_server --package-reload`,
joined two game clients over the LAN on one Mac, edited the label and reported "Tested it and
it works well!". The check: a live script edit on a hosted hunt reaches both players without a
restart. Host with `./build/boat_server --bind 0.0.0.0 --location outer
--package plugins/outer-hunt --package-reload`, join two game clients over the LAN, then edit
`plugins/outer-hunt/main.lua` (for example the `"CREW "` label) and save; both HUDs should change
within about a second and keep their counts.

### P5f: definition revisions (protocol v14)

RevisionBegin/Page/End, RevisionReady, the commit tick and the `REVISION` leave reason, in all
modes, committing at round boundaries (race, deathmatch) or empty free rooms.
Files: `src/net/boat_net_protocol.[ch]`, `src/net/boat_net_server.c`, `src/net/boat_net_client.c`,
`src/net/boat_prediction.c`, `tools/boat_server.c`, `docs/MULTIPLAYER_PROTOCOL.md` (v14).
Tests: success; a joiner mid-download; a missing readiness disconnects only that client; an
invalid candidate keeps the running revision; duplicate and stale announcements; a commit exactly
on a round boundary; prediction history never crosses a commit.
Gate: all revision cases pass in every mode; existing room tests unchanged.

**P5f result, 2026-11-08 (commit on `feat/plugin-p5`).** Protocol v14 adds RevisionBegin, Page,
End (types 26-28, server to client) and RevisionReady (29, client to server) on the control
channel, leave reason 5, and the running revision in Accept (a joiner must know it to ignore
stale announcements; the room's first definition is revision 1). `boat_net_server_stage_revision`
validates a candidate like the room's own definition and requires the same mode and location. It
commits at a boundary: race WAITING/RESULTS, deathmatch WARMUP/INTERMISSION, a free room only
while empty (immediately, no announcement). With players it announces to every one and commits
240 ticks (2 s) later, rounded up to a snapshot tick; clients not ready by then are dropped with
reason 5. At the commit tick the server installs the definition (owned copy; `config.room` and the
fleet's scene follow), replaces match/race rules, the course (rebinding its source), combat rules
and marks, and tells `BoatNetRules.definition`. A client switches when its mirror reaches the
commit tick and resets prediction there; a checkpoint at or after it is taken as new. Decisions
and deviations from the sketch above: (1) the announcement waits until the boundary is certain to
hold through the commit tick (a timed phase must outlast it) and no peer is still joining,
because the protocol has no cancel message and a client switches unconditionally at the commit
tick; (2) a Hello during an announcement is held in the handshake and served the new definition
after the commit (the alternative, serving the old definition then the announcement, would let a
join start a round under the announcement); (3) revision numbers are assigned at announcement or
commit, a newer staged candidate replaces a waiting one but not an announced one (it waits for
the next boundary); (4) `boat_server --package-reload` stages a marks change (any definition
change) as a revision, and the objective restarts on the new source and marks at the commit
(`boat_room_objective_stage_revision`; P5e's "marks changed; restart the room" refusal is gone).
Verification (macOS, Debug, 2026-11-08): `cmake --build build -j8` warning-clean;
`ctest --test-dir build --preset full -j8` 460 of 460 passed (453 before); new cases
`boat_net.revision_free`, `revision_race`, `revision_joiners`, `revision_not_ready`,
`revision_boundary` (free: waits while occupied, commits at once when empty, invalid and replaced
candidates, a scene; race: commit in RESULTS with two predicting clients, prediction restart,
duplicate/stale/invalid announcements; a joiner mid-download and a held Hello; a silent client
dropped alone with reason 5; a deathmatch commit on an intermission's last tick and one that did
not fit waiting for the next) and `boat_room_objective.revision_marks`, `revision_refused`
(waits while occupied, commits when empty, objective restarts on the new marks, a new crew scores
on the moved mark); the version re-pins are the golden header bytes, the snapshot digest and the
LAN query. Real-process smoke: `boat_server --location outer --package <copy of outer-hunt>
--package-reload`, one `boat_bot` joined (revision 1), a mark moved in the copy: the server
printed `objective reload: marks changed; revision 2 waits for an empty room`, and after the bot
left `definition revision 2 committed at tick 1925 (5 marks)` and `objective reload: revision 2
accepted (restart)`; a second bot then joined with `revision=2`. Limits: race and deathmatch
commits are covered by the validator only (no command-line trigger exists); the game's own
clients were not run against a revision (the game reads marks from `boat_net_client_room` each
frame, which changes in place at the commit); a timed phase shorter than the 2 s lead never
fits, so such a room never commits; a revision keeps mode and location (no migration between
modes); the browser build was not exercised. Next task: [P5g](#p5g-recording-and-reconstruction).

### P5g: recording and reconstruction

`--record PATH`, the session file and a reconstruction validator mode with seek.
Files: new `src/boat_session_record.[ch]` (codec, no files), `tools/boat_server.c`,
`tools/validate_boat_session_record.c`.
Tests: record a two-bot hunt with a reload and a reconnect; reconstruct every keyframe and compare
fleet hash, objective digest and HUD; seek; a truncated file fails cleanly; no Lua by default;
the diagnostic re-run matches.
Gate: bit-identical reconstruction on the recording build.

### P5h: four players, impairment and browsers

Four crews; reconnect and leave cases; loss, reordering, duplication and delay; the web build
draws marks and the HUD; `check_web_multiplayer.py` gains a hunt case with one native and one
browser player on a local WebRTC server.
Files: `tools/validate_boat_net.c`, `tools/check_web_multiplayer.py`, `docs/TESTING.md`,
`docs/WEB_BUILD.md`.
Tests: HUD equal on every client including late joiners; server tick cost with the Lua update
measured against the deathmatch baseline; mixed clients finish a hunt; an old browser bundle is
rejected with the version message.
Gate: measurements recorded; **user play check 2** — one native and one browser player on a
locally built WebRTC room.

### P5i: closing step

Move the contract into specs: the authority objective adapter, room events (`mark.reached`,
`crew.*`) and limits into `docs/PLUGIN_API.md`; v13/v14 already in `docs/MULTIPLAYER_PROTOCOL.md`
(remove plan wording); create `docs/MULTIPLAYER.md` (authority, prediction, rooms, revisions,
recording, hosting). Turn leftovers into Open loops rows, retire the "v10" label, mark this plan
done or move it to history, and update the index.

## Deferred

Checked against the [Open loops](README.md#open-loops) board on 2026-11-08:

- **The co-op working harbour** (sandbox S7's first slice) becomes a follow-up milestone on top
  of P5's authority, ObjectiveState, revisions and recording. Its earlier decisions are kept
  [below](#superseded-co-op-working-harbour-draft) as input, not as scheduled work.
- Client commands to objectives, exactly-once actions and predicted objective state (needed by
  the working harbour, not by the hunt).
- Room saves, R2 loaded maps and content distribution ([room content plan](ROOM_CONTENT_PLAN.md)):
  clients receive definitions as data and need no package.
- M5's full impairment matrix, soaks and fuzzing beyond the cases above; the deathmatch playtest;
  shared-room agent editing (P6) and browser-side Lua.
- A Winterfell hunt room.

## Decisions

2026-11-08, the user: start P5 "as simple as possible"; first scenario the **shelter hunt in a
free room** (chosen over Lua on the race room and Lua on deathmatch).

Kept from 2026-11-07 where they still apply: Winterfell gets no new room in P5 and browser checks
use a locally built WebRTC server; revisions are host-only `--package-reload`; recording is opt-in
`boat_server --record`; protocol bumps are v13 (objectives in rooms) then v14 (revisions); two
players until the four-player step.

Defaults this plan assumes (change any before P5a):

| # | Question | Default |
| --- | --- | --- |
| A | Mark count and size | Up to 16 per package, radius 10–200 m; the bundled hunt has five |
| B | Who scores | Per crew (fleet slot); the script decides scoring, the native side only reports entries |
| C | A leaver's score | Lost; a new player in the slot starts at zero |
| D | End of a hunt | When every connected crew has all marks, the script restarts it |
| E | Persistence | None: a hunt restarts with the room |
| F | Solo play of the hunt | Not in P5; the package is room-only (marks could later run solo with crew 1) |

## Risks

- **Too thin to be fun.** The hunt is a vehicle for the contract, not a game mode the user asked
  for; play check 1 may say it is dull. That is acceptable if the plumbing holds.
- **Fixture churn.** v13 re-pins every room fixture even though behaviour is unchanged.
- **Server cost.** A fresh Lua evaluation per 10 Hz batch runs inside the 120 Hz authority loop;
  P5h measures it.
- **Browser receive queue** already overflowed once in a deathmatch run (PD3 review).
- **Release ENet-only:** browsers cannot join HOST A ROOM; browser checks need a WebRTC build.

## Superseded: co-op working harbour draft

History, 2026-11-07, replaced 2026-11-08. The first draft made the co-op working harbour P5's
first room: one workboat each with their own crane and crate on one shared job, berth leases and
one crate per pad, a separate autosaved room save (`room-cargo-<package id>.sav`, SV1 format 2),
crane rates and action serials in an 11-byte command, WorkPage/WorkState messages, predicted
crates and twelve steps (P5a–P5l). The user settled those choices on 2026-11-07 and then, on
2026-11-08, chose to start with something simpler because the working harbour involves many more
parts. The full draft is in this file's git history at commit `12c6bf8`.

## Verification of this plan

Planning only, 2026-11-08: no code changed and no build or test run. Re-read the previous draft,
the shelter-tour package, the free schema, the room definition and the cited sources at commit
`12c6bf8`. Byte sizes are estimates.
