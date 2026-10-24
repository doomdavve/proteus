# Capsize deathmatch: multiplayer game mode plan

Prepared 2026-10-24 against `7a072c6`. **D1 (match core) and D2 (protocol v2 and the in-game
HUD) are implemented; no human has played a deathmatch yet.**
This document owns the deathmatch rules, phases, verification and acceptance. The
[multiplayer plan](MULTIPLAYER_PLAN.md) owns transport, prediction and room hosting; the
[physics](BOAT_PHYSICS_PLAN.md) and [wake](WAKE_REWRITE_PLAN.md) plans own handling and wake
feel, including tuning of the new mechanics below.

Status: D1 (match core, `boat_server --mode deathmatch`) implemented and automatically verified
on 2026-10-24; see the [D1 handoff](#d1-match-core-handoff--2026-10-24). D2 (protocol v2, round
clock, kill feed, scoreboard, capsize notice, `boat_bot --hunt`) implemented and automatically
verified on 2026-10-24, agent-run in a window against two hunting bots, not user-played; see the
[D2 handoff](#d2-protocol-v2-and-hud-handoff--2026-10-24). Next: the D2 manual check (the user
against `boat_bot --hunt`, then two humans), then D3, side-ram roll. D3–D6 are pending. Remaining M4 acceptance checks are deferred by user direction while M5 proceeds.
River/Eriksberg hosting was pulled forward from M7 on 2026-10-24: launch
`boat_server --bind 0.0.0.0 --mode deathmatch --location river` and join normally. The server
selects the map; rebuilt protocol-v3 clients load it automatically. See the
[river hosting handoff](MULTIPLAYER_PLAN.md#river-hosting-handoff--2026-10-24).

## Product decision

On 2026-10-24 the user chose to give multiplayer a purpose: a timed deathmatch in which players
score by capsizing each other. This supersedes the earlier "no scoring in the first release"
default in the multiplayer plan's product boundary. Decisions recorded with the user:

| Question | Decision |
| --- | --- |
| Round structure | Timed rounds (default 5 min), scoreboard intermission, then a new round |
| Capsizing yourself | −1 point |
| New capsize mechanics | All four, in order: side-ram roll, wake surge, swell mine, shrinking arena |
| Sequencing | Start now, in parallel with M4 manual acceptance |

Free mode (current shared free boating) stays the default room mode and must remain unchanged
byte for byte; deathmatch is a room option chosen by the server.

## Rules

The authority alone decides credit and scores. Clients display confirmed state and never
predict credit.

**Touches.** Each boat keeps one most-recent touch `{slot, tick, cause}` from another slot:

- *Contact*: the pair's accumulated impulse in `boat_fleet_solve` is at least 1.5 normalized
  units (initial guess; tune in D1). Both boats record a touch by the other.
- *Wake*: another emitter's records under the hull sum to at least 0.35 m of |height|, or that
  emitter has the largest share on the tick the boat becomes `BOAT_AIRBORNE`. A boat's own
  emitter never counts.
- *Later causes*: `SURGE` (D4), `MINE` (D5) and `ARENA` (D6).

**Credit.** When a boat first latches `BOAT_CAPSIZED` (the existing rule: 0.6 s inverted in
water, `motion_step` in `src/boat.c`), the latest touch within a 5 s credit window scores +1
for its slot. Without such a touch the victim scores −1 (self-capsize). Rows track score,
capsizes caused, times capsized and self-capsizes.

**Bailout.** R while upright within the credit window after a touch counts as a capsize
credited to the toucher; R otherwise is free.

**Respawn.** After a confirmed capsize the authority requests a spawn itself after 3 s; R
skips the remaining wait after 1 s. Spawns use the existing `boat_fleet_request_spawn` safe
spawn rules. A new life has a 2 s credit grace: touches by or against it are not recorded.
This is not physical invulnerability, which the multiplayer plan rules out.

**Rounds.** `WARMUP` (fewer than two players: free play, no scoring) → `PLAYING` (300 s,
`--round-seconds`) → `INTERMISSION` (10 s scoreboard; boats keep moving) → scores reset and
every slot respawns → `PLAYING`. Mid-round joiners start at zero; a leaver's row is removed and
pending credit to them is dropped. Ties share first place.

## Architecture

- `src/boat_match.[ch]` (new, `proteus_boat_core`, obeys the core boundary): `BoatMatchRules`
  (round, intermission, window, thresholds, grace, respawn delay, mechanic toggles) and
  `BoatMatch` (phase, phase end tick, score rows, last touches, lives and latches seen,
  respawn deadlines). `boat_match_observe` runs after each `boat_fleet_step`, returns bounded
  events (`CAPSIZE{victim, attacker|none, cause}`, `ROUND_START`, `ROUND_END`) and spawn
  requests for the server to apply. It never changes physics, the fleet hash or prediction.
- `src/boat_fleet.[ch]`: diagnostic `pair_impulse[BOAT_FLEET_PAIRS]` in `BoatFleetStats`,
  filled from the solver's accumulated impulses; physics never reads it.
- `src/boat_wake_objects.[ch]`: read-only `boat_wake_objects_query_emitter`, the ordinary
  evaluation restricted to one emitter's 32 slots.
- `src/boat_net_server.[ch]`: room mode and rules in `BoatNetServerConfig`, a `BoatMatch`
  beside the fleet, automatic respawns and bailouts through the spawn request handler.
- Protocol v2 ([protocol](MULTIPLAYER_PROTOCOL.md)): reliable control messages
  `MATCH_STATE` (mode, phase, phase end tick, round, per-slot rows; also sent during bootstrap
  for join-in-progress) and `MATCH_EVENT` (tick, kind, victim, victim life, attacker, cause),
  with golden, round-trip, truncation and invalid-field fixtures.
- `src/boat_net_client.[ch]`: latest match state and a six-entry event ring.
- `tools/boat_study.c`, `src/boat_hud.c`: in deathmatch only, round timer and own score, a
  four-line kill feed (5 s per entry, roster names), a Tab-held scoreboard also shown during
  intermission, and `CAPSIZED BY <name> / RESPAWN n`. Free mode HUD unchanged.
- `tools/boat_server.c`: `--mode free|deathmatch`, `--round-seconds`, logged match events and a
  scripted deathmatch run. `tools/boat_bot.c`: a `--hunt` script that lays wakes across and
  rams the nearest boat.

## Phases

Every phase ends with a warning-free build, the full active CTest suite and an unchanged
offline `boat_study`.

1. **D1 — match core.** `boat_match`, the impulse diagnostic, the per-emitter query, server
   mode, automatic respawn, bailout and scripted server output. New
   `tools/validate_boat_match.c` (core label): synthetic credit-window edges, self −1, bailout,
   grace, leaving attacker, round transitions and ties; physical fleet cases where A's wake
   capsizes B (credited `WAKE`), an own-wake capsize (−1) and a contact touch expiring after
   5 s. Fleet/replay traces and single-player captures stay identical.
2. **D2 — protocol v2 and HUD.** Messages, client state, kill feed, timer, scoreboard,
   intermission and `boat_bot --hunt`. Extend `validate_boat_net` and add a loopback
   deathmatch smoke (server plus two hunting bots for one short round; both clients' scores
   equal the server log). Inspect CPU/GPU HUD captures. Manual: user against bots, then two
   humans.
3. **D3 — side-ram roll.** A contact also applies a roll impulse to the struck boat, scaled by
   lateral impulse and contact height above its centre of mass, so a fast T-bone can tip it.
   Room rule `ram_roll` in the checkpoint and content digest, off in free mode, so single
   player and the crossing rival stay unchanged. Gate: glancing hits never capsize; a square
   T-bone above a named speed does. Extend prediction-replay identity tests.
4. **D4 — wake surge.** `BoatInput.surge` and a command flag bit; a checkpointed, hashed meter
   in `Boat`: 1.5 s of about ×2 crest strength, then 8 s recharge. Records already carry
   amplitude, so births replicate unchanged; confirm the protocol range envelope accepts the
   taller crests. HUD meter, an unused key, cause `SURGE`.
5. **D5 — swell mine.** Starts with a feasibility spike recorded here. A buoy dropped at the
   stern (one per player, recharging) bursts after 1.5 s into a radial ring wave: a ring record
   kind or a small separate pool, CPU query, GPU pack/shader parity, protocol births,
   checkpoint and a small buoy mesh. Cause `MINE`.
6. **D6 — shrinking arena.** Match state carries an arena centre and a radius that is a
   function of the round tick. Outside it a deterministic storm swell, a function of tick and
   position only, is added in `boat_water_query` with a matching GPU uniform, so prediction
   stays exact. Buoy ring, HUD warning, cause `ARENA` (−1 unless another touch is in the
   window).

Each mechanic gets a user feel gate before it is on by default in deathmatch rooms.

## Verification and acceptance

- Build and `ctest --test-dir build -j 4 --output-on-failure`, warning-free, at every phase.
- Free mode byte identity: Release worktree before/after diffs of fleet and replay validator
  output, P3a traces and single-player CPU captures.
- D1: `validate_boat_match` and the scripted `boat_server --mode deathmatch` event log.
- D2: loopback deathmatch smoke, inspected CPU/GPU HUD captures, an agent windowed run against
  `boat_bot --hunt`, then user play.
- D3–D6: validator cases, prediction-replay identity, captures and a user feel gate.

Acceptance for the mode: two or more humans finish several rounds where credits match what
they saw, self-capsizes feel fair, nobody can deny a capsize by resetting, and the round loop
runs without a room restart. Record only checks actually performed, with dates and platform,
in dated handoff sections below.

## Handoffs

### D1 match core handoff — 2026-10-24

Implemented:

- `src/boat_match.[ch]` in `proteus_boat_core`: `BoatMatchRules` (defaults as in the rules
  above), `boat_match_observe`, `boat_match_reset` and `boat_match_rank`. Presence is derived
  from the fleet (active or pending), lives from `BoatFleetSlot.life`.
- `BoatFleetStats.pair_impulse`, the solver's accumulated impulse per pair (diagnostic), and
  `boat_wake_objects_query_emitter`, the ordinary record evaluation over one emitter's slots.
- `BoatNetServerConfig.deathmatch`/`.match`: the server observes the match after every tick,
  queues its spawns, routes `SPAWN_REQUEST` through `boat_match_reset` and keeps the last 16
  events for the caller. No protocol change: clients see automatic respawns as new lives, but
  their HUD still says `CAPSIZED / R TO RESTART` and shows no score.
- `boat_server --mode free|deathmatch --round-seconds S` (rooms and `--scripted`), logging
  round starts, capsizes with attacker and cause, round-end scoreboards and abandoned rounds.
  Scripted deathmatch bots leave recovery to the match.

Deviations and choices:

- Capsizes outside `PLAYING` (warmup, intermission) still mark the boat out and respawn it but
  are neither scored nor reported. A round below `min_players` is abandoned to warmup.
- A round start respawns every present boat, including the one already playing in warmup.
- Wake touches compare one emitter's |height| at the hull centre (`Boat.position`), not at the
  six support points; the launch rule credits the strongest emitter there on the tick the boat
  becomes airborne.
- Contact and wake thresholds (1.5, 0.35 m) are untuned first guesses.

Verification, 2026-10-24, M1 Max/macOS 26.6.2, Debug:

- Build warning-free in project sources; all 108 active CTests pass, including the new
  `boat_match`, `boat_server_scripted_deathmatch` and a rejected `--round-seconds` without
  deathmatch; the core and net boundary checks pass with `boat_match` in the core.
- Free mode unchanged: `boat_server --scripted` output for 2400 and 18000 ticks (fleet hashes,
  wake, contact and life counters) is identical before and after the change.
- Scripted 18000-tick deathmatch, 60 s rounds, four bots: two complete rounds and a third
  started; 21 capsizes, all credited (20 `WAKE`, 1 `CONTACT`), no self-capsizes, no emission
  failures.
- ENet room on loopback with two `boat_bot` clients for 45 s, 20 s rounds: the round started
  when the second bot joined, a wake capsize was credited, round 1 ended with its scoreboard and
  round 2 was abandoned as the bots left.

Not verified: no human play, no `boat_study --connect` session against a deathmatch room, no
Windows/Linux build. That bots' capsizes are all credited suggests the wake threshold may be
generous; credit fairness needs human play (D2).

### D2 protocol v2 and HUD handoff — 2026-10-24

Implemented:

- [Protocol v2](MULTIPLAYER_PROTOCOL.md#matchstate-and-matchevent-deathmatch-rooms): Accept's
  reserved byte carries the room mode; MatchState (type 17, 104-byte payload: phase, respawn and
  skip delays, round, round length, phase end, four score rows) and MatchEvent (type 18,
  16 bytes) travel on the reliable control channel. v1 clients and servers are refused with the
  existing version Reject. Snapshot, checkpoint and birth layouts are unchanged.
- `boat_net_server`: events are broadcast as they happen, followed by MatchState whenever it
  changed; a joining peer gets MatchState right after Roster, before the checkpoint.
- `boat_net_client`: `match`, `result` (the finished round's scoreboard as first announced) and a
  six-entry feed that keeps the roster names of the moment, so a leaver's name survives.
- `boat_study --connect` in deathmatch rooms (`tools/boat_study_net.c`): round clock, own score
  and place at the top centre (orange under 30 s), a four-line kill feed under the player list
  (5 s per entry; orange when you were capsized, green when you scored), `CAPSIZED BY <name> /
  RESPAWN n` (or `/ -1` for a self-capsize, and `/ R TO SKIP` once R may skip the wait), a
  scoreboard while Tab is held and throughout the intermission, and a help line. Free rooms
  draw nothing new.
- `boat_bot --hunt` (`boat_fleet_bot_hunt_input`): full throttle at a point 0.6 s ahead of the
  nearest upright boat. In deathmatch rooms bots leave recovery to the match. The bot prints
  each finished round's scoreboard and a `match:` summary.

Deviations and choices:

- The room mode lives in Accept, not MatchState; free rooms send no match messages at all.
- The HUD is drawn at window resolution beside the roster and network line in
  `tools/boat_study_net.c`, not in the 320×200 `boat_hud.c` panel, so it has no display-free
  CPU/GPU capture; it was inspected in screen captures of a windowed run instead.
- The capsize countdown uses the event's tick, or for an unscored capsize (warmup, intermission)
  the mirror tick the client first saw it capsized.
- Rows are cleared when a player leaves, also during the intermission, so a leaver drops off the
  shown result; `result` on the client keeps the announced version.

Verification, 2026-10-24, M1 Max/macOS 26.6.2, Debug:

- Build warning-free; all 109 active CTests pass, including the new `boat_net_deathmatch` and
  the core and net boundary checks with `boat_match.h` now included by the protocol header.
- `boat_net`: golden MatchState and MatchEvent bytes; v2 snapshot digest `c02fda7dbe1ffe1b`, which
  with the version byte set back to 1 is still the v1 digest; 18 types round-trip, all
  truncations and 16 new field faults rejected; a v1 Hello gets Reject `VERSION`; a seeded
  in-memory deathmatch room in which three clients (one joining mid-round) match the authority's
  MatchState through a planted-touch capsize, a second capsize, the round end and a leaver, with
  names in each feed.
- New `boat_net_deathmatch` (ENet loopback, 10 s round, two hunting bots): both bots' round-1
  scoreboards equal the server log's. Agent runs on 2026-10-24 credited one or two wake capsizes
  per round.
- Agent-run windowed `boat_study --connect` (GPU renderer, 960×600) beside two `boat_bot --hunt`
  clients and a 20 s round, inspected in screen captures: round clock and place, a two-entry
  kill feed with both wake capsizes, the intermission scoreboard matching the server log, and
  the capsize notice. The first run showed scoreboard cells overwritten by raylib's
  `TextFormat` buffer rotation and overlapping columns; both were fixed and rechecked. Tab was
  not pressed (no keyboard input in the agent run).

Not verified: no human play; Tab-held scoreboard, capsize notice after a credited capsize of the
own boat and the bailout feed line were not seen on screen; no Windows/Linux build. Free mode
physics is untouched by D2 (match code runs only in deathmatch rooms), but no before/after
capture diff was made for this slice.
