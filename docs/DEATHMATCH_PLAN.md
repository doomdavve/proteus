# Deathmatch: water cannon, stern waves and torpedoes

This document owns deathmatch rules, implementation, verification and acceptance. Multiplayer
transport is owned by [the multiplayer plan](MULTIPLAYER_PLAN.md).

## Arsenal milestone — 2026-10-27

Current status: implemented in `deathmatch-arsenal`, based on `a6bec2c`. This supersedes the
machine-gun and forward-grenade rules in the dated 2026-10-26 milestone below.

| Control | Current behavior |
| --- | --- |
| Hold Space | Water cannon: 28 m range, 60 m/s packets at 8 Hz, four-second overheat, release cooling over 3 s; unlock below 30% |
| Press E | Wave grenade: 7 m astern, splash after 0.3 s, detonation after 1 s, recharge 8 s; unchanged 2 m crest, 10 m/s propagation, 30 m range |
| Press Q | Straight torpedo: three per life, 40 hull damage, 32 m/s, 160 m range, 0.25 s arming, 1.5 s reload; no homing |

Water packets add 0.1 pressure per broadside hit, scaled down to 20% end-on. Pressure recovers
at 0.22/s. Hits push and roll the hull; accumulated pressure of 1 latches a capsize, with
cannon attribution under the existing five-second credit rule. The cannon never damages hulls.
Torpedoes follow the water surface, stop at scenery, and break harmlessly on unarmed hull
contact. Swept hull collision, spawn grace and projectile birth/life fences apply. Repeated
nonzero grenade/torpedo requests, including rejected requests, are consumed once per life;
rejection consumes neither ammo nor cooldown. Respawn restores hull, ammunition and cooldowns.
Grenade waves remain dangerous to their thrower.

The bottom control panel gains matching hull/heel and weapon instruments only in deathmatch.
Opponent numbers show authority-confirmed damage dealt by the viewer, merge hits within 0.75 s,
float for 1 s, and are fenced by target life and connection generation. No opponent health bar
is drawn. Cannon contact uses a cyan hit marker and visible hull heel instead of damage numbers.
Shared CPU/GPU boxes draw water packets, surface torpedoes, bubble trails and impact splashes.
The procedural weapon sound is a water wash, with low splashes for torpedo launches/impacts.
Hunting bots use the cannon nearby, torpedoes ahead, and grenades against trailing targets.

Protocol **v7** repurposes the command's reserved byte for a torpedo request serial. Combat
checkpoints include pressure, ammo, recent damage bursts and eight torpedoes; 40 short-lived
water packets replace 64 bullets. Combat remains four pages per snapshot, now 3716 bytes;
free/race omit combat. Rebuild native/browser peers together. See the
[wire layout](MULTIPLAYER_PROTOCOL.md#combatpage-and-combat-checkpoint).

Completed: arsenal, authoritative state/prediction, input, bots, damage feedback and instruments.
Next: human balance playtest, especially cannon recovery/roll strength, torpedo evasion and
rear-grenade timing; browser/Windows/Linux qualification and public deployment remain pending.
No user acceptance or public deployment is implied by local verification.

Verification (macOS/Apple Silicon, Debug, 2026-10-27): deterministic combat checks cover gradual
cannon heel/capsize/recovery, torpedo ammo/arming/serial wrap, rearward fuses, swept collisions,
actual damage/overkill, life fences, respawn, attribution and checkpoint/page round trips.
The warning-clean Debug build and all **230 enabled CTests** pass (13 retired wake cases
remain disabled), including four predicted clients with late joins, loss/duplication/reordering
and complete combat-page recovery. The final full run used local socket access outside the
restricted sandbox. GPU-context CTests remain off.

Inspected [GPU HUD/damage feedback](evidence/deathmatch-arsenal/gpu-hud.png),
[CPU weapon scene](evidence/deathmatch-arsenal/cpu-weapons.png), and a CPU river frame at 3 s.
An eight-second native GPU fixture demo completed and the audio device initialized/closed
successfully; this is an agent-run demo, not a human listening or balance acceptance check.
Browser, Windows/Linux, coordinated deployment and human play are unverified for this revision.

## Weapon milestone — 2026-10-26 (superseded rules; historical evidence)

Every deathmatch life has 100 hull health, no regeneration, a cool bow gun and a ready grenade.
Free boating, race and offline simulation keep combat disabled. `boat_combat_default_rules()`
is the single tuning definition:

| Weapon | Defaults |
| --- | --- |
| Hold Space: machine gun | 8 shots/s, 8 damage, 100 m/s, 100 m range; overheats after 2 s; release cools from full in 3 s; unlocks below 30% |
| Press E: wave grenade | 24 m forward, splash at 0.65 s, detonate at 1.5 s, recharge 8 s; 2 m crest at 10 m/s, smoothly zero at 30 m |

Grenades cause physical waves, including against the thrower; they never reduce hull health.
The existing two-second spawn grace blocks firing and bullet damage, while waves remain
physical. Warmup permits weapons without scores; intermission blocks launches. Menus, help,
focus loss and respawn require weapon keys to be released before firing resumes.

Zero health marks a hull defeated immediately, with a sinking presentation and lethal-shooter
credit. Capsizes retain the five-second attribution window and uncredited self-capsize −1.
The match latches one defeat per life, including coincident destruction/capsize. The existing
three-second automatic respawn and one-second earliest reset skip remain. Scoreboard columns
now read kills/deaths; the internal row member names remain compatible with the match code.

Implementation:

- Fixed-capacity, allocation-free tick module `boat_combat.[ch]`: 64 bullets, four grenades and
  four independently evaluated radial waves. Every grenade reserves its ring until expiration;
  a rejected full-pool request consumes no cooldown. Identities contain owner, connection
  generation, life and birth tick. Departure removes that connection's objects; round restart
  clears all objects; respawn resets health/heat/cooldowns. Bullet birth fences protect new lives.
- Fleet tick order: pending lifecycle operations, accepted launches/fuses, water and hull
  integration/contact solve, swept hits, then the authority's match observation. Moving hulls
  use relative slab sweeps; scenery uses conservative 5 cm clearance/probes, including water,
  mapped shore, harbor and traffic. Pool order and slot order break simultaneous-hit ties.
- Radial height, gradient and vertical speed use a smooth compact crest and quadratic fade;
  the CPU reference and both GPU stages share that recipe, separate from the wake-object pool.
- Shared boxes draw traces, muzzle flashes, impacts and lobbed/fusing grenades. CPU/GPU water
  adds crest foam. The integer-scaled HUD shows hull/heat/recharge and authority-confirmed hits;
  gun/grenade causes appear in the feed. Procedural audio uses event tick high-water marks so
  corrections cannot replay sounds. Deterministic visual effects have no accumulating event queue.
- Protocol [v6](MULTIPLAYER_PROTOCOL.md#combatpage-and-combat-checkpoint): held gun plus numbered
  throw requests, complete combat/rules checkpoints, four bounded unreliable pages per snapshot.
  Eight in-flight sets tolerate reordering; only complete matching sets correct prediction, and
  an applied set supersedes older incomplete ones. Native/browser share the C codec/prediction.
- `boat_bot --hunt` steers its bow at targets, shoots when aligned, slows nearby and throws.

Completed: combat, physical water, checkpoint/prediction, controls, presentation and bots.
Next: native desktop controls/audio play and native GPU parity; Firefox/Safari and other
platform qualification; then human aim/visibility/balance acceptance. Side-ram roll, wake surge,
mines, shrinking arenas, independent aim and pickups are deferred.

Verification record (macOS/Apple Silicon, Debug, 2026-10-26):

- Baseline: warning-free project build and 95 active CTests pass, 13 retired cases disabled,
  GPU tests off. This is automated evidence, not user acceptance.
- `boat_combat`: deterministic cadence/overheat/cooling/grace, duplicate throws, pool rejection,
  fuse/ring lifetime, save/restore replay, relative swept hits, water occlusion, old-life fences,
  lethal tie, single defeat, automatic respawn, analytic radial derivatives and physical self-wave.
- `boat_net`: v6 golden bytes (snapshot digest `7aa25c376fac8ec5`), 21 codec types, full combat
  checkpoint/hash tests; four predicted clients with late joins, 12% loss, duplicated/reordered
  pages, one-second outage and complete correction recovery. Maximum message 1064 bytes;
  measured server application traffic about 147.7 KiB/s/client during that synthetic stress,
  including control/bootstrap/duplicates, excluding transport headers. This is an initial
  correctness-first bandwidth budget, not a low-bandwidth qualification.
- Display-free `boat_combat_review` CPU wave and sinking fixtures captured and inspected.
  No HUD text/audio/window was exercised by these captures. Production radial shader parity
  checks were added to `boat_race_trial --gpu-check`, but GLFW could not initialize a desktop
  window here; no GPU or windowed playback pass is claimed.
- Final Debug project build warning-free; full routine CTest: 97 active cases pass, 13 retired
  cases disabled, GPU tests off. ENet loopback/host/session tests ran with socket access.
  All 17 baseline offline CPU captures remain byte-identical. The new combat validator
  also passes a dedicated Release build with its assertions explicitly retained. Free/race trace and replay tests pass.
- Four held boats, 600-tick synthetic stress: 64 continuously replenished bullets plus four
  waves average 0.113 ms/tick, p95 0.118, p99 0.125; combat disabled average 0.022 ms/tick.
  Separate 7200-tick scripted four-hunter run: mean 0.0404 ms/tick, p99 0.0601; one gun kill,
  30 flights and no wake emission failures. These are CPU timings on this Mac, not broad
  performance or balance acceptance.
- Emscripten MinSizeRel `web` build passes. Optional WebRTC native endpoint built successfully.
  Chrome/SwiftShader mixed browser/native deathmatch automation passes: held gun commands,
  one numbered grenade, 772 received combat pages, rendered burst/HUD, two resets, leave/rejoin,
  no page errors or prediction snaps. [Browser burst](evidence/deathmatch-weapons/browser-burst.png)
  and [weapon HUD](evidence/deathmatch-weapons/browser-weapons.png) inspected after moving the
  HUD to the left to avoid the roster. Headless automation is not audible or human playback.
- `check_web_combat_wave.py`: both production VS/FS radial recipes pass 2873 WebGL2 float
  probes each; maximum absolute height/gradient/vertical-speed error 0.00001442 against an
  independent reference. This verifies SwiftShader, not the unavailable native GPU context.
- [CPU crest](evidence/deathmatch-weapons/cpu-wave.png) and
  [sinking fixture](evidence/deathmatch-weapons/cpu-sinking.png) are retained for review.
  Native windowed playback/sound listening, native GPU probes, Windows/Linux runtime and
  human acceptance remain unverified locally; CI results belong to the PR.

Review commands (run from repository root):

```sh
./build/boat_server --mode deathmatch --round-seconds 30
./build/boat_bot --connect 127.0.0.1:24680 --hunt --seconds 40
./build/wake_velocity --connect 127.0.0.1:24680 --name captain
./build/boat_combat_review --time 2.3 --screenshot build/combat-cpu.png
./build/boat_combat_review --gpu --play --screenshot build/combat-gpu.png
./build/boat_race_trial --gpu-check
```

## Product decision

On 2026-10-24 the user chose to give multiplayer a purpose: a timed deathmatch in which players
score by capsizing each other. This supersedes the earlier "no scoring in the first release"
default in the multiplayer plan's product boundary. Decisions recorded with the user:

| Question | Decision |
| --- | --- |
| Round structure | Timed rounds (default 5 min), scoreboard intermission, then a new round |
| Capsizing yourself | −1 point |
| Current weapon milestone | Water cannon, rearward wave grenade, torpedoes and bottom instruments; see the arsenal milestone above |
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
- Torpedo hull defeats, cannon capsizes and grenade-wave capsizes extend the causes above.

**Credit.** When a boat first latches `BOAT_CAPSIZED` (the existing rule: 0.6 s inverted in
water, `motion_step` in `src/boat.c`), the latest touch within a 5 s credit window scores +1
for its slot. Without such a touch the victim scores −1 (self-capsize). Rows track score,
kills, deaths and self-capsizes. Hull health reaching zero credits the lethal shooter immediately; the same per-life latch prevents a second capsize score.

The controls branch uses Escape > REQUEST RESET or a one-second Backspace hold for manual
requests; R no longer acts. [Current integration status](CONTROLS_PEDALS_HANDOFF.md).

**Bailout.** An explicit reset request while upright within the credit window after a touch counts as a capsize
credited to the toucher; an untouched upright reset is otherwise free.

**Respawn.** After a confirmed defeat the authority requests a spawn itself after 3 s; a reset request
skips the remaining wait after 1 s. Spawns use the existing `boat_fleet_request_spawn` safe
spawn rules. A new life has a 2 s credit and weapon grace: touches are not recorded, weapons cannot fire,
and bullets cannot damage it. Waves remain physically active.

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
- `tools/wake_velocity.c`, `src/boat_hud.c`: in deathmatch only, round timer and own score, a
  four-line kill feed (5 s per entry, roster names), a Tab-held scoreboard also shown during
  intermission, and `CAPSIZED BY <name> / RESPAWN n`. Free mode HUD unchanged.
- `tools/boat_server.c`: `--mode free|deathmatch`, `--round-seconds`, logged match events and a
  scripted deathmatch run. `tools/boat_bot.c`: a `--hunt` script that lays wakes across and
  rams the nearest boat.

## Original phases (superseded after D2)

The D1/D2 records remain historical implementation context. The former D3–D6 sequence below
is deferred; the weapon milestone above owns current work.

Every phase ends with a warning-free build, the full active CTest suite and an unchanged
offline `wake_velocity`.

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
- Weapon milestone: deterministic combat/paging/replay cases, CPU/GPU captures, playback and a human balance gate. Deferred D3–D6 proposals have no current acceptance claim.

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

Not verified: no human play, no `wake_velocity --connect` session against a deathmatch room, no
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
- `wake_velocity --connect` in deathmatch rooms (`tools/wake_velocity_net.c`): round clock, own score
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
  `tools/wake_velocity_net.c`, not in the 320×200 `boat_hud.c` panel, so it has no display-free
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
- Agent-run windowed `wake_velocity --connect` (GPU renderer, 960×600) beside two `boat_bot --hunt`
  clients and a 20 s round, inspected in screen captures: round clock and place, a two-entry
  kill feed with both wake capsizes, the intermission scoreboard matching the server log, and
  the capsize notice. The first run showed scoreboard cells overwritten by raylib's
  `TextFormat` buffer rotation and overlapping columns; both were fixed and rechecked. Tab was
  not pressed (no keyboard input in the agent run).

Not verified: no human play; Tab-held scoreboard, capsize notice after a credited capsize of the
own boat and the bailout feed line were not seen on screen; no Windows/Linux build. Free mode
physics is untouched by D2 (match code runs only in deathmatch rooms), but no before/after
capture diff was made for this slice.
