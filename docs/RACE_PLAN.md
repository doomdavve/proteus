# River sprint: multiplayer race plan

Prepared 2026-10-24. **R0 prototype and R1 race core implemented and approved by the user;
R2 network race mode and R3 slalom/storm implemented; storm, crest shading and three-crest ferry wake user-accepted; first two-human river race played
2026-10-24 ("successful and fun").**
This document owns race rules, course pacing, jump encounters and acceptance. The
[multiplayer plan](MULTIPLAYER_PLAN.md) owns transport and prediction; the
[traffic guide](HARBOR_TRAFFIC.md) owns existing scenery vessels; the
[physics](BOAT_PHYSICS_PLAN.md) and [wake](WAKE_REWRITE_PLAN.md) plans own handling.

Status: the user approved R0 (shortened route, visible-ferry encounter) and R1 (authoritative
race core) on 2026-10-24. R2 is implemented and automatically verified: `boat_server --mode
race`, protocol v4 (hold flag, race GO tick, RaceState/RaceEvent), client prediction of the
grid hold and ferry clock, finisher parking, course markers, HUD, standings and results, a
route-following `boat_bot` and LAN labels. Bots have raced over ENet, and on 2026-10-24 two
humans raced the river course ([first human race](#first-human-race--2026-10-24)).
The first four-gate layout was user-played and rejected as too easy/boring on 2026-10-24.
The current [R3 layout](#r3-skill-gate-layout--2026-10-24) has six 32–40 m openings alternating
across the river, a committed middle chicane, and the user-selected local rough-water storm
with rain and lightning, now travelling down-course at 8 m/s on the shared race clock.
The ferry wake and broad finish remain. The user accepted the final higher cloud, irregular
waves/rain and independent movement on 2026-10-24 ("I like it. Commit."). A later
[crest shading tweak](#crest-shading-tweak--2026-10-24) addresses tall waves whitening and
was user-approved ("Much better"). The [three-crest ferry wake](#three-crest-ferry-wake--2026-10-24)
is implemented and user-approved ("Looks great!"). Two humans then raced the river course and
reported it a successful, fun race with things to improve. Next: record those improvements,
then assess pacing, missed-gate recovery and turn/wave overlap before extending the route. The current bot runs take
63–71 s, exceeding the original 45–60 s aim; this pacing tradeoff awaits feedback. Full
downtown coverage remains outside the current map. See the [R2 implementation](#r2-network-race--2026-10-24),
the [R1 core](#r1-race-core--2026-10-24) and the [R0 evidence](#r0-prototype--2026-10-24).

## Direction

The user requested the next multiplayer mode, **race**: a high-paced downstream sprint
from downtown Gothenburg, past Eriksberg towards Älvsborgsbron, lasting about 45–60 seconds,
with many jumps, boats lined up at a start, and a finish that awards each player's position.
The user then suggested a large ferry or other vessel as the source of the jump wakes.

Recommended first prototype: a large, visibly moving ferry makes the main wake encounter,
with other authored vessels supplying further encounters along the course. Weather waves
and ramps remain alternatives if the vessel-wake trial cannot deliver the rhythm. The
following counts, timings and recovery rules are working proposals, not user-approved tuning.

## Route and pace

Use one point-to-point course for the existing four-player room. Aim for a roughly
50–55 second clean run, measured from GO to the finish; countdown and results are additional.

The current [river crop](eriksberg/bridge/README.md#implemented-coverage-and-original-survey)
contains Eriksberg and the bridge. It does not establish a downtown start or a complete
downtown-to-bridge race. Survey the desired start, navigable centreline, shoreline clearance,
bridge approach and landing areas before choosing coordinates or extending the map.
The documented 1,649 m dock-to-turnaround survey is a different route, not a race baseline.

Measure clean-run average speed including acceleration, turns, takeoffs and landings.
Required average speed is route length divided by race time: for example, a hypothetical
2 km course in 50 s needs 40 m/s on average. Do not assume current handling can do that.
R0 must resolve the distance/time fit: map expansion, a shorter downtown-facing start for
the prototype, a deliberately compressed race map, or a race-specific speed tune. Record the
tradeoff before implementation; the full downtown route remains the requested destination.
Any shorter prototype must be labelled as such.

Proposed rhythm, relative to GO:

| Race time | Encounter |
| --- | --- |
| 0–8 s | Broad launch corridor, room to accelerate and establish a line |
| 8–22 s | Large ferry crosses safely ahead, leaving a readable train of jump crests |
| 22–38 s | Eriksberg section: smaller workboat wakes, alternating approach angles and passing lines |
| 38–55 s | Largest clear jump encounter, generous landing runout, then a visible bridge-side finish |

Target roughly 8–12 distinct jump opportunities per clean run, grouped into several encounters.
This is a tuning target; count actual airborne/landing transitions, not foam strips. Leave
calm gaps to regain control and overtake. A safer line should remain usable, while a good
approach and landing preserve enough speed to reward taking the jumps.

## Vessel-made jumps

The ferry is part of the course: its hull, direction and widening wake should explain where
the next jump comes from. Start with one crossing vessel and several crests, then assess
whether additional vessels are needed. A large ferry is a proposed new or scaled recipe;
the current scenery ferry is a compact 24 m passenger boat.

Ordinary scenery traffic has deterministic routes, moving contacts, meshes and **visual foam only**.
Those trails do not displace water or launch a boat. A race needs a physical wake source
whose height, slope and surface velocity agree with its CPU and GPU appearance. Adding
foam or increasing the scenery ferry's speed alone does not implement this.

Prototype a bounded vessel wake derived from the source's historical motion: paired spreading
crests with smooth onset/decay, enough width for different racing lines and no height seams.
Evaluate whether the arcade wake representation can support a ferry's scale and duration;
do not silently consume a player emitter slot or overwrite player wakes. Bound source count,
history and query cost before adding more encounters. Keep scenery traffic and free boating
unchanged unless separately requested.

Course sources follow authored routes on the authoritative race clock, with a fixed pre-roll
history so their wakes are already formed when needed. They do not react to the leader or
rubber-band to individual players. Every boat samples the same physical surface at the same
position and tick; late racers naturally encounter older wakes. Test a several-second spread
through each encounter so trailing boats still have useful jumps and a traversable route.

Place the source hull clear of the intended racing corridor when racers arrive. The jump
is the wake; avoid forcing a blind collision with the ferry. Keep broad landing areas clear
of shore, berths, bridge legs and other source vessels. Retain existing player contacts and
player wakes, and check overlaps with the authored crests for excessive launches/capsizes.

If wake shape or lifetime cannot provide reliable encounters, compare a fixed wave zone or
an optional floating ramp in R0. A ramp requires a driveable surface, takeoff and side-contact
rules, and matching rendering/collision; it is not just a new mesh. General local weather is
not required for the ferry prototype.

## Start, progress and results

Proposed round loop: **WAITING → GRID → COUNTDOWN → RACING → RESULTS → GRID**.

- Wait for at least two ready players. Freeze that round's roster before placing boats.
  Solo route trials belong in a development fixture.
- Put all boats abreast behind one visible start line, facing down-course. Use equal forward
  positions, generous lateral separation and checked hull/shore clearance. Rotate grid slots
  between rounds. Hold position and clear launch motion through a 3–2–1 countdown; prepare
  throttle during countdown and release everyone on the same authority tick. The hold and
  release must also exist in client prediction, including wave forces and wake emission.
- Use six ordered skill gates with 32–40 m openings alternating across the river, with a
  middle chicane, and a visible finish spanning the navigable corridor. Accept only forward swept crossings within a gate's lateral bounds, including an airborne
  boat crossing over it. Missing or reversing through a gate cannot advance progress.
  Gate layout must allow different racing lines and prohibit shoreline shortcuts.
- Show elapsed time, live position and the next gate. Rank unfinished boats by completed
  gates then distance along the current course segment, rather than straight-line distance
  to the finish. Live position is provisional.
- The authority records finish time and position once, after all checkpoints. Resolve multiple
  finishes in one simulation tick by interpolated crossing time; exact ties share position.
  Show `FINISHED — 2nd / 00:52.34` immediately on confirmation, then retain the full results.
  Never award places from packet arrival order or a client's claim.
- Proposed recovery: a short automatic capsize delay, then a safe placement at or behind the
  last cleared gate. R uses the same recovery. Keep elapsed time running, preserve only
  cleared progress, reset motion and crossing history, and never respawn beyond an uncleared
  gate. Select a safe lateral position and retry if occupied; no reset shortcut or finish
  crossing caused by teleportation.
- Finished boats must clear the racing corridor and stop interfering with unfinished racers;
  R2 parks them beside the river past the finish ([rule](#r2-network-race--2026-10-24)).
  Their places remain fixed.
- Allow 15 s after the winner for remaining boats to finish, with a 90 s absolute racing cap
  as a stalled-round fallback. Then mark unfinished entrants DNF. These are proposed limits;
  45–60 s is the intended clean-run duration, not a forced finish for every entrant.
- A mid-race joiner waits for the next grid; a disconnected entrant is DNF and keeps its
  identity in that round's results. A replacement in the same network slot cannot inherit
  progress or a finish. If all entrants leave, abandon the round. Show results for about
  8 s, then re-grid the current ready roster. Completed finishes survive later disconnects.

## Implementation sequence

1. **R0 — route and one ferry-wake trial.** Measure route distance/speed against 45–60 s;
   identify missing downtown map coverage. Build one deterministic source trial with a
   visible vessel and matching physical/rendered wake. Sweep speed, approach angle, arrival
   time and player-wake overlap; count jumps, flight time, speed loss and capsizes. Inspect
   CPU/GPU frames and run a windowed trial. Deliver route choice and a playable encounter
   before building a complete course. User feel feedback remains a separate gate.
2. **R1 — authoritative race core** ([implemented 2026-10-24](#r1-race-core--2026-10-24)). Add a small `boat_race.[ch]` in `proteus_boat_core` for
   phases, locked entrant identities, grid, swept gates, progress, finish order, recovery
   and DNF. Supply authored course/source data without I/O or rendering. Adapt the existing
   basin crossing approach where useful; keep the basin course and replay semantics intact.
   Add scripted fleet trials and a race validator.
3. **R2 — network and presentation** ([implemented 2026-10-24](#r2-network-race--2026-10-24)). Add the server-selected race mode, versioned course
   identity/source settings, reliable phase/results and recoverable progress state. Update
   the owning protocol byte tables and compatibility revision before changing codecs.
   Bootstrap/resync includes current phase, GO tick, entrant identities and confirmed
   finishes; deterministic source state also reaches prediction/checkpoints. Add room
   discovery labels, client start restrictions, finish runout, HUD and results, plus a
   route-following bot. Publish runnable commands only once implemented.
4. **R3 — full course and pacing** ([first skill-gate layout implemented](#r3-skill-gate-layout--2026-10-24)).
   Add enough source encounters for the jump rhythm and the selected downtown/Eriksberg/bridge route. Tune using clean runs and four-boat races, not
   top-speed arithmetic alone. Check source traffic against ordinary traffic and landings.
   Extend mapped assets only after R0 identifies the necessary bounds.
5. **R4 — multiplayer acceptance** ([first two-human race played](#first-human-race--2026-10-24)). Repeat complete rounds with bots, then two or more
   humans, including delayed/lossy clients, trailing racers, resets and reconnects. Record
   measured durations, completed jumps, unfair crashes, result agreement and replay of the
   round loop. Qualify shipping platforms separately.

Tests should cover countdown release, directional/swept/airborne gates, high-speed skips,
multiple gates in a tick, same-tick finishes/ties, frozen places, recovery without progress
gain, late joins, leave/rejoin slot reuse, all-leave and deadlines. Physical source tests
must cover deterministic restore/prediction, CPU/GPU parity, bounded storage/cost and stable
landings. Network checks must include missed phase events, bootstrap in each phase and
result convergence. Each implementation milestone requires a warning-free build and full
active CTest run; changed visuals require inspected captures and changed motion needs a
windowed check. Human acceptance is not inferred from automation.

## R0 prototype — 2026-10-24

Implemented [`boat_race_trial`](../tools/boat_race_trial.c), a separate, silent development
fixture. It provides scripted runs, a playable boat, deterministic CPU/GPU captures, route
measurements and source/physics sweeps. It does not add a room mode or change the basin time
trial. Follow the river southwest toward the bridge; the HUD reports world elapsed seconds,
speed, actual landing count and the next route waypoint. W/S taps select the existing thrust
detents; A/D or arrows steer, including existing air roll; R restarts the entire fixture,
including the source clock and player wake history. There is no race countdown or recovery yet.

```sh
./build/boat_race_trial --play
./build/boat_race_trial                         # scripted route in a window
./build/boat_race_trial --play --no-source       # calm comparison
./build/boat_race_trial --check                 # deterministic report and contracts
./build/boat_race_trial --gpu-check             # both production shader recipes
./build/boat_race_trial --renderer cpu --output build/race-cpu.png --time 12
./build/boat_race_trial --renderer gpu --output build/race-gpu.png --time 12
./build/boat_race_trial --frames 3300            # bounded windowed motion check
```

**Route choice:** retain the existing mapped metres and handling, and use a shorter,
**downtown-facing Eriksberg prototype**, not a downtown-to-bridge course. Its simulation X/Z
centreline is `(-180,-250) → (-40,-360) → (220,-510) → (454,-678) → (600,-800)`,
956.54 m from open river to the bridge-side runout. The polyline has at least 12 m of mapped
clearance in 101 samples per segment; the simulated conservative hull also stays clear.
The last segment passes under the bridge; the fixture has no finish gate. The source crop
ends at east +950 m, so a real downtown start still requires a separate map extension/survey.
This route deliberately trades the full requested geography for a measurable playable R0;
no map assets or handling constants were changed.

**Source:** [`boat_vessel_wake.[ch]`](../src/boat_vessel_wake.h) implements one straight,
authored 40 × 10 m ferry with a box presentation, 12 m/s travel, 12 s pre-roll, 24 s emission
and 40 s finite crest lifetime. Emission at each query's along-track coordinate reconstructs
historical motion analytically. Smooth spatial and temporal envelopes bound paired spreading
crests, with analytic height, slope and vertical velocity. The recipe is an arcade trial,
not a calibrated ferry simulation. The ferry stops at its clear runout after emission and
remains visible until the source expires; it has a padded, height-aware hull contact envelope.
No adaptive chasing, accumulated history, allocations or player emitter slots are used.
Storage is one 56-byte source on this platform, query work is one bounded evaluation, and
all four ordinary player emitters remain available. The mixed 100,000-query microbenchmark
reports its timing each run; it is not a renderer/frame-cost guarantee.

CPU water queries, displaced CPU geometry/foam and both production water shaders consume the
same source settings and bounded world clock. The GPU retains its existing local displacement
patch/far fade, while the CPU reference tessellates the mapped surface; shading and distant
geometry are not pixel-identical. The corrected shared box presentation appears in both renderers. The trial also opts its
four ordinary passing boats into a separate physical wake pool with visible broken foam
trails. Ordinary free boating remains unchanged; see the [feedback correction](#r0-feedback-correction--2026-10-24).

**Historical ferry-only measurements** (before the feedback correction), 2026-10-24,
Apple Silicon/macOS 26.6.2, Debug with optimized core. Current combined-source measurements
are in the [feedback correction](#r0-feedback-correction--2026-10-24):

| Fixture | Measured result |
| --- | --- |
| Calm, full-throttle centre line | 44.94 s, 956.98 m travelled, 21.29 m/s mean, no jumps |
| Ferry, full-throttle centre line | 48.89 s, 957.02 m, 19.57 m/s mean, 9 jumps/9 landings, 2.70 s airborne |
| Ferry, full-throttle, ±10 m start offsets and 0/3/6 s arrival delay | 48.36–49.57 s after launch, 8–9 jumps/landings, no capsizes |
| Ferry, 75% thrust, same lines/delays | 57.20–58.62 s, 8–10 jumps/landings, no capsizes |
| Source hull / shoreline | ≥108.3 m racer/source centre separation in route sweeps; source centre ≥64.55 m from shore, full ferry corners remain clear |
| Local 12/18/24 m/s approaches at −20°/0°/+20°, with/without a crossing player trail | All 18 trials jump and land without capsize or hull obstruction; seven-second observation windows can end during another flight |
| Four-emitter fleet save/restore through the encounter | Per-tick fleet hashes agree after restore, no emission failures |
| Source-only centre line, no player wakes | Same 48.89 s and 9 jumps/landings; the ferry causes the encounter |
| CPU derivatives / long world clocks | Finite-difference slope/velocity checks pass; translating the source clock by 30 days preserves samples exactly |
| Production VS and FS sampled against CPU | Both pass; maximum sampled absolute error 0.0000363 across height, both slopes and vertical velocity |

These count actual whole-hull airborne transitions and landings, rather than foam crests.
The candidate has substantial speed dips: full-throttle encounter minima are about 5–7 m/s,
and slower runs reach up to 6.59 m boat height. No tested capsize does not establish forgiving
human handling. One packet currently supplies the jumps in a cluster, rather than the plan's
several spaced encounters; alternate safe lines, four-boat racing, repeated rounds and the
full requested geography remain future work. Do not claim the full race pacing is accepted.
R0 establishes a physical source and a feasible shortened speed budget; review launch strength,
readability and recovery feel before selecting additional encounters. No ramp fallback was
needed to obtain jumps in this trial.

**Verification:** deterministic contracts/route/approach sweeps and both shader probes pass.
CPU captures at 8/12 s and GPU captures at 3/12 s were inspected. The GPU review incorrectly
identified ordinary scenery traffic as the source ferry; the source itself was missing from
the mapped GPU scene. That visual-verification claim is withdrawn and corrected below. A 3,300-frame scripted
GPU window run completed. This is agent-run motion verification, not hand-driven play or
human acceptance. Build/full-suite results are recorded below.

## R0 feedback correction — 2026-10-24

The user reported that the wake felt artificial without a readable large source boat and that
passing scenery boats left no wakes. This was negative feel feedback, not acceptance.
Investigation found that the GPU renderer drew frame scenery only in the basin. The river
therefore contained the ferry's physical wave field without its source hull. The earlier
GPU visual review failed to catch this; seeing an ordinary compact scenery ferry did not
verify the authored source. CPU drawing was already showing it.

**Implemented correction:** mapped frame scenery now enters the main, reflection and shadow
passes. The same 40 m ferry remains visibly associated with its wave field. It also has a
broken, spreading foam trail behind its stern. The four passing scenery boats in the trial
now publish their actual historical stern paths into an independent, bounded arcade-wake
pool, seeded with 16 seconds of pre-roll. Gentle 0.5 m emission recipes include the slow
2.2 m/s runabout, which is below the ordinary player planing-wake threshold. Its emission
strength uses a separate speed gain; record lengths/ages still use actual vessel motion.
Trails are wider, longer and broken into foam patches for visibility at 320×200.

The separate scenery pool adds at most 128 objects without using any of the four player
emitters. CPU water height, slope, velocity and foam include both pools. GPU packing supports
up to 256 records with independent lifetimes; upload stays 4 KiB with a player pool alone
and becomes 8 KiB with scenery, using at most 8 KiB staging. Tests cover the texture resize
round trip and exact uploaded records, plus production VS/FS parity beyond record 127 with
a different scenery lifetime. Ordinary free boating and rooms opt into none of this new
scenery physics; no protocol bytes or player handling constants changed.

**Current measured combined-source runs:** full-throttle lines with 0/3/6 s delay finish in
49.23–50.18 s with 6–10 airborne transitions/landings; 75% thrust finishes in 58.08–59.38 s
with 9–11. No capsizes or hull obstruction occurred in those scripted sweeps. The centre-line
full-throttle run is 49.42 s, 6 jumps/landings and 4.06 s airborne. These replace the earlier
ferry-only table as the live trial measurements. The isolated ferry-only comparison still
runs with both player and scenery pools removed. Speed dips and high launches remain tuning
issues: combined-source full-throttle encounter minima are 4.73–6.34 m/s and slower runs
reach 6.70 m height. Do not treat counting 8–12 jumps as acceptance of their feel.

**Verification:** deterministic route/approach checks pass; all four scenery emitters
produce sampled physical displacement (including the runabout), and copied scenery state
continues identically. Both source shader probes pass. Removing only the ferry geometry,
while keeping the physical water identical, changes 2,231 main-image pixels, 335 reflection
pixels and 31,864 shadow-depth pixels in the regression camera. `boat_race_trial_gpu` now
requires this visibility check to complete; its success marker is printed after all checks.

Debug configure/build passes without project compiler warnings. The full
`ctest --test-dir build -j 4 --output-on-failure` passes all 115 active tests, including
24 GPU checks, in 141.79 s on Apple Silicon/macOS 26.6.2; 15 retired wake-field cases remain
disabled. Revised CPU/GPU 8 s captures were inspected, and a 1,200-frame (about 20 s) scripted
GPU window run completed through the encounter. These are agent-run checks; no human play of
the corrected version is claimed. All 281 local file/heading links in the seven affected
documents resolve, and `git diff --check` passes. Other platforms remain unqualified.

Next (superseded on 2026-10-24 by [R1](#r1-race-core--2026-10-24), which proceeded on the
user's request): review the corrected visible-source encounter with the user. That feel review
and human acceptance remain open; the analytic ferry packet has not been replaced or declared
natural.

## R1 race core — 2026-10-24

Implemented [`boat_race.[ch]`](../src/boat_race.h) in `proteus_boat_core` and
[`validate_boat_race`](../tools/validate_boat_race.c) (CTest `boat_race`). The race is an
authority-side observer like the deathmatch `boat_match`, but it also owns race placements:
it grids and holds entrants, recovers them and sets the course ferry's clock. A `BoatRace` is
pointer-free; with a fleet checkpoint and `boat_race_bind`, it resumes identically.
No server mode, protocol, client or HUD uses it yet.

**Rules as implemented** (the plan's proposals; the user has not approved any value):

- **WAITING → GRID → COUNTDOWN → RACING → RESULTS → GRID**, or back to WAITING when fewer
  than two players remain. Waiting players boat freely from ordinary fleet spawns. Presence
  means "ready" for now; there is no ready control.
- GRID freezes the roster from the players present. Entrants are placed abreast 12 m behind
  the start line, 14 m apart, facing down-course; grid places rotate by one each round.
  GRID lasts 1 s and the countdown 3 s. Held boats keep plane position and heading with no
  planar velocity, but throttle and engine response follow input. Waves still heave and
  pitch the hull, and a held boat emits no wake. Every hold is released at the same GO tick.
- Gates are lines across the river, ordered, with the last as the finish. Only a forward
  swept crossing of the *next* gate within its lateral bounds counts; reversing or passing
  outside gains nothing. One tick's segment can clear several gates in order. Crossings are
  planar, so airborne boats count. A new fleet life (spawn, recovery, teleport) starts a fresh
  segment, so placement never crosses a gate.
- Live position: fixed places first, then running boats by gates cleared and metres along
  the current segment, then DNF. Finish times are interpolated within the tick and stored in
  1/1024-tick units after GO. Same-observation finishers are placed by time, and equal times
  share a place. Places never change once awarded and survive a disconnect.
- Recovery: 2 s after a capsize, or on R (R after a capsize waits 0.5 s), an entrant is
  placed 25 or 45 m behind its last cleared gate (the start line before any). The first clear
  lateral candidate is used, retrying every tick. Progress and the race clock carry on.
  Finished boats recover 25/45 m past the finish. R is ignored while held; outside a round,
  R and capsizes use ordinary fleet spawns.
- Racing ends when no entrant is still running, 15 s after the first finish, or 90 s after
  GO. Unfinished entrants are then DNF. RESULTS lasts 8 s.
- A joiner after GRID stays unplaced until the next grid. Leaving during GRID, countdown or
  racing marks a running entrant DNF and keeps its row. A replacement in the same slot gets a
  new identity and inherits nothing. If every entrant leaves, the round is abandoned to
  WAITING and waiting joiners spawn.
- The course carries the R0 ferry with `started` relative to GO (12 s pre-roll). Gridding
  writes it into the fleet's water on the round clock.

**Historical R1 course (superseded by the [R3 skill gates](#r3-skill-gate-layout--2026-10-24)):** the R0 route with three lines centred across the mapped river: two checkpoints
at the route's third and fourth vertices and a finish about 30 m short of the route's end,
near the bridge. Each line reaches water at both ends and land 12 m beyond, so it cannot be
bypassed. Straight lines between successive gate centres keep 12 m of mapped clearance.
The start-to-finish centre line is 927.5 m. The first R0 bend is not a gate: its line would
run along the ferry's track, and the river there is too straight to cut.

**Fleet change:** `BoatFleetSlot.held` and `boat_fleet_hold`, plus
`boat_fleet_spawn_clear`, which exposes the fleet's existing safe-spawn test for race
recovery. The hold enters the fleet hash only while set. Unheld fleets keep their hashes,
and every existing CTest, including byte-for-byte and hash checks, still passes. Protocol v3
does not carry `held` or the race's ferry clock. Until R2 versions them, a race must not be
hosted.

**Measured four-bot race** (validator, 2026-10-24, Apple Silicon/macOS 26.6.2, Debug with
optimized core). Bots aim at each gate centre in their own 14 m lane at full throttle:

| Lane (slot) | Place | Finish | Jumps | Recoveries |
| --- | --- | --- | --- | --- |
| 1 | 4 | 51.66 s | 9 | 0 |
| 2 | 3 | 46.85 s | 10 | 0 |
| 3 | 2 | 46.10 s | 9 | 0 |
| 4 | 1 | 45.81 s | 6 | 0 |

All finish within the 45–60 s target (the validator accepts 40–60 s), each with at least one
jump. A mid-race checkpoint restored 20 s after GO into a fresh fleet produces identical
per-tick fleet hashes and the same places. The second round grids and reaches RACING. These
are scripted bot lines, not human pacing or feel. Jump counts are whole-hull airborne
transitions.

**Verification:** see [verification and limitations](#verification-and-limitations).
**Limitations and R2 inputs (as of R1):** finished boats were not moved out of the corridor;
they coasted past the finish (R2 now parks them). Recovery candidates are checked
against hulls, land and wake water, not against the ferry's moving hull, so R2/R3 should
add a source-hull check. The ferry appears 8 s into its crossing when the grid forms, about
200 m ahead. Its previous round's wake is replaced at the next grid (normally already
expired). The scenery-traffic wake pool from the R0 fixture is not part of the race.

## R2 network race — 2026-10-24

The user approved R0 and R1 and asked to continue. R2 hosts `boat_race` in network rooms.

**Commands** (one machine; use `--bind 0.0.0.0` for a LAN room):

```sh
./build/boat_server --mode race                      # river is the default location here
./build/boat_study --connect 127.0.0.1:24680 --name me
./build/boat_bot --connect 127.0.0.1:24680 --seconds 120   # races the course
```

Two players start a round; the title screen's MULTIPLAYER panel and `boat_bot --discover`
label the room "race". `--mode race` refuses another `--location` and `--scripted`.

**Protocol v4** ([byte tables](MULTIPLAYER_PROTOCOL.md)): room mode 2; slot flag bit 2 carries
the hold; snapshots and checkpoints carry the latest round's GO tick; RaceState (whole phase,
clock and rows, sent on join and on every change) and RaceEvent (gate, finish, DNF, recovery,
park, phase, abandon) are reliable control messages; the content digest covers the course.
v3 builds cannot join v4 rooms; rebuild every package.

**Prediction:** clients bind the course ferry to their mirror and prediction worlds from the
GO tick, so the ferry and its wake run on the authority's clock. Held boats predict as held;
a replay from an authoritative state before GO releases them at GO, as the authority does.
Gates, places, recoveries and deadlines remain authority-only; the HUD displays them.

**Finish runout (new rule, proposal):** a finisher coasts for 2 s, then is parked 70 or 100 m
past the finish beside the course (lateral offsets 40, 80 and 120 m alternating sides, first
clear spot), facing up-course and held until the next grid or waiting. R or a capsize after
finishing parks at once. A parked boat ignores R and counts no recovery; a PARK event is sent.

**Original R2 presentation** (intermediate markers superseded by the [R3 endpoint gates](#r3-skill-gate-layout--2026-10-24)) (`boat_study --connect`): course markers on mapped water — buoys every 12 m
across each line with a pole every fourth, white start, yellow next gate, orange later gates,
teal cleared and a chequered finish. HUD: waiting/next-grid notices, "SET YOUR THROTTLE" on
the grid, a large 3-2-1-GO!, then elapsed time, live position and gate (or "FINISH NEXT"),
"CLOSES IN n" after the first finish, `FINISHED - 2ND / 00:52.34` or DNF, a feed of finishes,
DNFs and abandons, the standings board on Tab and the full results during RESULTS, and the
race capsize notice (recovery countdown, "R TO SKIP"). R is not sent while held.
`boat_race_trial` captures now draw the same markers.

**Bot:** `boat_fleet_bot_race_input` (shared by the validator and `boat_bot`) aims at each next
gate centre in its own 14 m lane at full throttle; held bots prepare throttle.

**Verification** (2026-10-24, Apple Silicon/macOS 26.6.2, Debug): `validate_boat_race` adds
parking (coast, R-park, hold until next grid) with the four-bot result unchanged.
`validate_boat_net` checks v4 goldens, every race field fault, a race room on the in-memory
transport (predicting entrants released at GO with 0.0000 m own-boat correction, 1837/1837
mirror ticks identical to the authority including the ferry, a mid-race joiner gridded next
round, results on every client) and a held-to-GO prediction replay. New CTest
`boat_net_race` runs a real ENet race room with two predicting bots found through LAN
discovery. A manual ENet room with three bots finished 46.23, 46.98 and 52.48 s, parked them,
broadcast results and gridded round 2 with no decode faults; each bot's results matched the
server log. CPU captures of `boat_race_trial` at 1, 21, 30 and 44 s show the start, next-gate
and finish markers.

**Not verified:** the HUD and markers in `boat_study` on screen, GPU captures and any windowed
run (GLFW could not open a window here); human play; lossy-network races. **Limitations:**
recovery and parking spots are not checked against the ferry's moving hull; a mid-race joiner
has no spectator camera (it waits in place with a notice); a lone remaining entrant races on
to the deadline; distant gate buoys are small at 320×200; the course is still R0's shortened
route.

## R3 skill-gate layout — 2026-10-24

**Current implementation:** six required 32–40 m openings alternate across the shortened
Eriksberg/bridge route. Four consecutive middle turns cannot be cut by any straight line
through the surrounding gate openings, even at their edges. Gate spacing is about 144–173 m;
the authored centre line is 1,106.9 m. The start/grid, ferry source, broad finish and deadlines
remain from R2. Current four-bot runs take 64.90–75.64 s; improving the route's challenge has
increased duration beyond the original 45–60 s aim, pending user feedback.

| Gate | Centre (simulation X/Z, metres) | Opening |
| --- | --- | --- |
| 1 — left setup | −72.3, −376.6 | 36 m |
| 2 — right sweep | 99.7, −371.8 | 36 m |
| 3 — chicane entry | 146.2, −535.5 | 32 m |
| 4 — chicane exit / storm meeting | 315.3, −516.9 | 32 m |
| 5 — rough-water turn | 375.9, −678 | 36 m |
| 6 — final switch | 520.1, −678.5 | 40 m |
| Finish | 588.5, −767.2 | 316 m |

Skill gates retain tall endpoint poles, buoy bases and inward-facing flags, with an open
centre. Yellow marks the next gate, orange later gates, and teal cleared gates; start and
finish keep their rows. Markers are visual, without physical buoy collisions. Forward swept
crossings count in the air; skipping a gate cannot award a finish. Bots inset lane targets
12 m from the edges, so the tighter openings also compress the fleet.

### Local storm obstacle

The user selected **rough-water storm patch with rain and lightning** on 2026-10-24.
The race now carries a moving patch, radius 140 m and height scale 1.6 m. After seeing the
static patch, the user requested movement along the course, then clarified that it should
follow its own path rather than any particular boat. Its centre starts at `(46.2, -399.12)` at GO and
travels at `(6.64, -4.464)` m/s (8 m/s down-course), passing `(345, -600)` at GO+45 s. It
meets the pack around the chicane and continues towards the finish; faster boats can catch
it and drive through, while trailing boats meet it later. It follows the common race clock
and never tracks one player's position or changes speed for an entrant.

A compact cubic radial envelope brings height and slope smoothly to zero at the moving
boundary. Waves change physical support, slope and vertical surface velocity; the time
derivative includes the translating envelope and crests, so the moving visuals and physical
water agree. Outside the patch the river stays calm except for existing wakes. Unlike the
static prototype, the rough-water encounter can continue into the finish approach.

On 2026-10-24 the user also requested a higher cloud near the background cloud layer and
more irregular waves instead of the recognisable crossing grid. The current recipe mixes
four wave trains with nearby headings and different spacing/speeds, long curved phase bends
and asymmetric crests. This replaces the two crossing trains while preserving compact
support and analytic derivatives. The pattern is deterministic on the shared race clock.

The dark storm cloud is now at 180–192 m (previously 33–39 m). Background clouds are a sky
panorama without physical altitude; the raised placement puts the storm in that visual
layer rather than just above the boat. Dense rain near the water is joined by a sparser
upper curtain, and lightning spans down from the higher cloud every 9 seconds.
Rain now uses independently cycling, jittered drops rather than aligned falling columns.
Speeds vary from 18–40 m/s. Fine blue-white streaks are 3.5–8 cm wide and 0.9–3.1 m
long near the water (2–4.2 m in the upper curtain), shrinking at either end of each fall.
They retain varied brightness and slant, gaps on
individual falls, and a travelling gust shape. Drops relocate at the top of each fall; all
variation comes from the race clock and deterministic hashes, with no mutable random state.
These cues advertise the area. Rain/cloud/lightning are cosmetic; lightning has no damage,
thunder or screen-wide flash, and no physical wind is implemented in this pass. The water darkens
locally in the GPU renderer and its crests foam in both renderers. Weather boxes are bounded
and use the shared scene path for CPU/GPU, reflections and shadows.

Clouds, rain, lightning and physical water use the same moving centre. The storm clock is
relative to GO, shared by the authority, mirrored client, prediction and
checkpoint restore. Course geometry, storm parameters and recipe revision participate in
the content digest; rebuild both server and clients. Network message bytes are unchanged.
Storm physics lives in `boat_water`, with no rendering, file or network access in the core.

For standalone practice, without the room countdown, results or race recovery:

```sh
./build/boat_race_trial --skill-course --play
./build/boat_race_trial --skill-course --frames 4800
./build/boat_race_trial --skill-course --renderer cpu --output build/storm.png --time 45
./build/boat_race_trial --skill-course --no-source --play
```

`--no-source` removes both the ferry and storm for a calm comparison. The preview follows
the current required gates and updates marker colours. It omits R0's extra scenery-traffic
physical wakes. The original R0 route, default trial and `--check` remain the historical
ferry fixture; `--gpu-check` now also samples both production storm shader recipes.

**Automated evidence:** mapped endpoint and centre/outer-lane clearance; mandatory line
changes even when cutting gate edges; swept gate ordering, skipped-gate finish rejection,
recovery, ties and parking. Storm checks cover exact zero outside/disabled, analytic spatial
and time derivatives against finite differences, a one-day clock translation and the shared
GO clock, translating envelope velocity, 8 m/s speed and GO+45 s rendezvous. Four bots
finish in 63.06–70.57 s, with 6–10 jumps and no recoveries after the three-crest ferry change.
Checkpoint restore remains identical. Full-suite, shader and visual evidence is recorded
below.

**Acceptance / next / limitations:** the user accepted the combined storm/course baseline
on 2026-10-24 ("I like it. Commit."). The later crest shading tweak was also user-approved ("Much better"); the shorter ferry wake
below was user-approved ("Looks great!"). Multiplayer handling, pacing and race-room acceptance
remain open. Next, check speed/line choices and turn/wave overlap with multiple human racers. Human R2 HUD acceptance, missed-gate
recovery, full downtown coverage, more wake sources, wind and weather audio remain pending.
The course is currently longer/slower than the initial race-duration goal. Storm speed and
its GO origin are authored in `boat_race.c`; changing speed requires retiming the origin if
the same GO+45 s meeting point is desired.

### Crest shading tweak — 2026-10-24

Implemented a presentation-only revision after the user reported tall waves turning white.
Storm and authored ferry foam previously saturated at 0.8 m of positive displacement,
blending 65% toward pale grey across increasingly broad wave faces. CPU and GPU now use
height together with the spatial gradient and a representative wave number to estimate
crest phase. A narrow phase band selects the crest; a steepness gate suppresses gentle
swells. Storm foam blends at most 28%, ferry foam 38%. The separate source heights are
removed from the ordinary wake foam gate, preventing overlap from restoring the broad
whitening. Water geometry, normals, physical wave recipes and network content are unchanged.

This is a cheap crest-location approximation, not curvature evaluation or breaking-wave
simulation. The four-component storm uses a representative wave number of 0.42 rad/m;
interference and envelopes can alter the selected bands. Existing player/scenery arcade
foam keeps its narrow broken ridge recipe. No new foam textures, history or simulation.

The standalone preview accepts `--wave-scale 0..4` (default 1), multiplying storm and authored
ferry heights. In windowed play it also changes the preview physics; with `--output` it
scales only the captured water after driving the default route, preserving the camera for
comparison. It never changes the race-room course. For example:

```sh
./build/boat_race_trial --skill-course --wave-scale 2 --output build/waves-high.png --time 45.05
./build/boat_race_trial --skill-course --wave-scale 0.5 --output build/waves-low.png --time 45.05
./build/boat_race_trial --skill-course --wave-scale 2 --play
```

Verification, 2026-10-24, macOS Debug: configure/build passed without project compiler
warnings (CMake retains raylib's OpenGL deprecation notice). The required full CTest run
passed all 117 active tests in 153.68 s, including 22 active GPU checks and ENet race;
15 retired wake-field checks remained disabled. GPU captures at 0.5×, 1× and 2× storm scale
(0.8/1.6/3.2 m authored height scale) at 45.05 s and the default ferry at 12 s were inspected,
along with CPU storm captures. The larger waves retain dark faces and restrained crest
bands. An 80-second/4,800-frame scripted windowed GPU preview exited 0 without diagnostics;
this is a runtime smoke check, not human playback acceptance. Graphics/network checks used
desktop/loopback access after sandboxed graphics startup stalled and was interrupted.
Changed document file links/new anchor resolve and `git diff --check` passes. The user then
accepted the shading ("Much better") and requested fewer ferry crests, tracked below. Human
handling and other-platform visual checks remain pending.

### Three-crest ferry wake — 2026-10-24

Implemented after the user accepted the revised water shading and requested three or four
ferry waves, larger in the middle and smaller outside. The authored race ferry and standalone
trial now use a 14 m half-width instead of 28 m, keeping the 8 m wavelength. Centring the
carrier on a crest produces three positive crests per side, with the existing cubic envelope
making the middle crest largest and tapering both outer crests. Height scale (3 m), spreading
speed, source route, timing and lifetime are retained. This changes physical water and visual
geometry together; both production GLSL stages use the same carrier and analytic derivative
as the CPU query. Existing player/scenery arcade wakes and the storm recipe are unchanged.

The race content digest now includes ferry recipe revision 2 after storm recipe revision 3;
rebuild server and clients together. Protocol v4 message bytes are unchanged.

Verification, 2026-10-24, macOS Debug: configure/build passed without project compiler
warnings (existing CMake OpenGL deprecation notice retained). Full CTest passed all 117
active tests in 151.52 s, including 22 active GPU checks and ENet race; 15 retired wake-field
checks remained disabled. The ferry fixture counts three actual positive maxima with peak heights
1.005 / 3.000 / 1.005 m at full development for both authored sources. Its finite-difference,
clock, checkpoint and encounter checks passed. Four race bots finish in 63.06–70.57 s with
6–10 jumps and no recovery. CPU/GPU ferry captures at 12 s, GPU approach at 8 s and race
encounter at 21 s were inspected. A 30-second/1,800-frame scripted windowed GPU preview
exited 0 without diagnostics. Both production shader recipes match CPU height, gradients
and vertical velocity with maximum absolute error 3.72e-5. Graphics/network checks used
desktop/loopback access. Changed document file links/new anchors resolve and
`git diff --check` passes. Next: multiplayer handling/pacing checks. The user accepted the
shorter packet on 2026-10-24 ("Looks great!"). This records visual acceptance, not multiplayer
handling or other-platform verification; those checks remain pending.

**Superseded first layout / user feedback:** the initial four 60–70 m openings and gentle
chicane had a 956.5 m centre line; four bots finished in 48.48–49.82 s with 8–9 jumps and no
recoveries. CPU/GPU captures and a 3,300-frame windowed run were checked. The user then
played it and described it as too easy and boring. That layout is not the current course,
and those successful checks did not establish user acceptance.

**Superseded static storm trial:** with the same tighter gates, a fixed storm at `(345,-600)`
produced 64.84–74.31 s runs, 10–16 jumps and no recoveries. Without a storm, the stronger
course produced 63.94–73.32 s and 6–11 jumps. The static storm revision passed all 117 active
CTests (22 active GPU checks) in 151.66 s and a 4,800-frame windowed preview; CPU/GPU storm
captures were inspected. The user described the static storm as looking cool, then requested
movement timed to meet the player along the course. That superseded the fixed patch; it does
not establish acceptance of the moving revision.

**Superseded first moving-wave recipe:** the two crossing wave trains with a low cloud
produced 66.44–80.74 s runs, 15–20 jumps and one recovery across the fleet. The user approved
the independent course movement, requested the cloud closer to the background cloud layer,
and described the regular wave pattern as unlike storm waves. This led to the higher cloud
and directional irregular recipe above, followed by the user's request for irregular rain;
the user subsequently accepted the combined appearance on 2026-10-24, as recorded above.

### Closer camera and finer rain — 2026-10-24

**Implemented:** the river follow camera now defaults to 12 m back / 6 m high, including
multiplayer joins and C reset. Title single player uses that shared default. Basin and
Eriksberg keep their handling-study views. The skill-course preview also uses the river
camera default; older captures used its 40 m back / 14 m high overview.

The user described the rain as falling concrete. The old opaque grey bars were 22–67 cm
wide and 3–17 m long. They are now fine blue-white streaks, 3.5–8 cm wide and 0.9–3.1 m
long near the water (2–4.2 m aloft), shrinking smoothly at the start and end of each fall.
A 20×20 lower curtain follows the viewer on a storm-relative 5 m grid, keeping fine
streaks visible nearby; cell-based hashes preserve existing drops as the view moves. The
sparse upper curtain remains storm-wide. The bounded scene budget, deterministic clock,
independently staggered falls and gusts remain.

**Verification:** 2026-10-24, macOS Debug: configure/build passed without project compiler
warnings; all 117 active CTests passed in 155.58 s, with 15 retired wake-field checks disabled.
CPU/GPU captures at 45.55 s and a later GPU capture at 63.05 s were inspected. A final
70-second/4,200-frame scripted windowed GPU storm preview exited 0 without diagnostics.
Graphics/network checks used desktop/loopback access after sandboxed startup failed and
was stopped. Changed documentation links resolve and `git diff --check` passes.

**Acceptance / next / limitations:** user-approved on 2026-10-24 ("Looks good. Commit.").
Next: multiplayer play and pacing assessment. Scripted previews do not establish human handling, audible
playback or other-platform acceptance.

### Race smoke-test timing — 2026-10-24

**Implemented:** corrected `boat_bot`'s `held_ticks` diagnostic to accumulate advancing
snapshot tick differences during consecutive active/held observations. Repeated polling of
the same snapshot adds no ticks; release/inactivity breaks the interval. The 150-tick smoke
gate remains, now measuring observed 120 Hz simulation time. Race rules, networking and
player input are unchanged.

Both v0.0.14 and v0.0.15 contain the old iteration counter and identical smoke check.
A slower service loop could report fewer than 150 iterations during the correct four-second
grid/countdown. This explains the reported failure without treating its low counter as
evidence of an early release.

**Verification:** 2026-10-24, macOS Debug: configure/build passed without project compiler
warnings. A temporary bot build replacing its ENet wait with a 40 ms sleep reproduced the
failure before the fix (90 counted iterations for both bots), then passed after the fix
(472 observed simulation ticks for both bots). Both runs logged the full 480-tick hold
between GRID and GO. The temporary sources/binaries are review artifacts in `build/`.
The normal-cadence `boat_net_race` check also passed. Full-suite follow-up, 2026-10-24, macOS
Debug at `73e2c4f` (the `v0.0.16` commit): rebuild and all 118 active CTests passed, including
the GPU checks; 15 retired wake-field checks stayed disabled.
The user requested this fix for release v0.0.16; native release CI remains the next check.

**Next / limitations:** rerun Windows release CI to qualify the original environment. No
native Windows run, shipped-tag replacement or new human multiplayer acceptance is claimed.
No audiovisual behavior changed; no new frame or listening review is needed for this fix.

### First human race — 2026-10-24

**Played:** two humans raced the river course in a race room and reported it a successful and
fun race, with things to improve ("There are things to improve for sure, but still fun").
This is the first human R2/R3 race play and the first two-human step of R4.

**Not recorded:** the platforms, build revision, bot count, lap times, network conditions and
the specific improvements were not reported. HUD, pacing (63–71 s bot runs) and missed-gate
recovery are not separately accepted by this report. **Next:** collect the improvement list
and turn it into the next R3/R4 tasks; delayed/lossy clients, resets and reconnects remain.

## Verification and limitations

Higher cloud / irregular waves and rain, 2026-10-24, macOS, Debug: configure/build passed
without project compiler warnings. The final `ctest --test-dir build -j 4 --output-on-failure`
passed all 117 active tests, including 22 active GPU checks, the production storm shader
probes, both core/net boundary checks, race physics and ENet race; 15 retired wake-field
tests stayed disabled. A preliminary suite for the wave/cloud change was interrupted when
the rain request arrived; this final run covers the combined revision.

GPU captures at 45.05, 45.55 and 63.05 s and CPU captures at 45.05 and 63.05 s were inspected;
an 80-second (4,800-frame) scripted GPU preview exited 0 with no diagnostic output. The
one-day clock and derivative checks still pass with the irregular recipe. Rain uses at most
510 footprint-intersecting drop cells before gaps/distance culling, retaining room for cloud,
lightning and markers within the 704-piece scene. Documentation links and new R3 anchors
resolve, and `git diff --check` passes. Current bot results and next steps belong to the
[R3 status](#r3-skill-gate-layout--2026-10-24). After these checks, the user accepted the
current appearance and requested a commit. Multiplayer acceptance, network impairment play
and other platforms remain pending.

Historical first moving-wave recipe / stronger R3 course, 2026-10-24, macOS, Debug: configure/build passed without
project compiler warnings. `ctest --test-dir build -j 4 --output-on-failure` passed all
117 active tests, including 22 active GPU tests, the core/net boundary checks and ENet race;
15 retired wake-field checks stayed disabled. Both production storm recipes match CPU
height, spatial gradients and moving-surface vertical velocity with maximum absolute error
2.25e-5 across the probe sweep. The motion/clock/derivative and four-bot results are detailed
in the [current R3 status](#r3-skill-gate-layout--2026-10-24).

GPU captures at 27.05, 45.05 and 63.05 s and CPU captures at 45.05 and 63.05 s were inspected.
An 85-second (5,100-frame) scripted GPU windowed preview exited 0 with no diagnostic output.
Graphics and ENet checks used desktop/loopback access. Documentation link paths and new R3
anchors resolve; `git diff --check` passes. Human play/acceptance of this moving revision,
network impairment play and other platforms remain unverified. The current 66–81 s bot
pacing exceeds the original duration goal and is explicitly awaiting feedback.

Historical first R3 layout (before the user's difficulty feedback), 2026-10-24, macOS,
Debug: configure/build passed without project compiler warnings (the first build retained raylib's vendored CoreMIDI typedef warnings and CMake's
OpenGL deprecation notice). `ctest --test-dir build -j 4 --output-on-failure` passed all
117 active tests in 148.44 s, including 22 active GPU tests, `boat_race` and `boat_net_race`;
15 retired wake-field tests stayed disabled. Graphics and ENet checks used desktop/loopback
access after sandboxed window startup stalled and was interrupted. CPU/GPU captures and
the complete scripted windowed run are recorded in the [R3 status](#r3-skill-gate-layout--2026-10-24).
Changed local documentation link paths and the new R3 anchors resolve; `git diff --check`
passes. Human race play, HUD acceptance and other-platform verification remain pending.

R2 network race, 2026-10-24, Apple Silicon/macOS 26.6.2: Debug build without project compiler
warnings. `ctest --test-dir build -j 4 --output-on-failure` ran 117 active tests in 110.05 s:
all 95 non-GPU tests passed, including `boat_race`, `boat_net` and the new `boat_net_race`;
22 of the 24 GPU tests failed because GLFW could not initialise a window platform in the
agent's environment (also with the sandbox disabled), so GPU parity for R2 is unverified.
R2 changes GPU-visible content only by the race markers in `boat_view`. All 313 local links in
the eight changed documents resolve and `git diff --check` passes. See
[R2](#r2-network-race--2026-10-24) for the scenario checks and what remains unverified.

R1 race core, 2026-10-24, Apple Silicon/macOS 26.6.2: Debug build without project compiler
warnings (raylib's own vendored-source warnings unchanged). The full
`ctest --test-dir build -j 4 --output-on-failure` run passed all 116 active tests, including
24 GPU checks and the new `boat_race`, in 135.93 s; 15 retired wake-field experiments stayed
disabled. Existing fleet, server, net, prediction and hash checks pass unchanged with the new
hold field. All 287 local file/heading links in the six changed documents resolve, and
`git diff --check` passes. R1 adds no visuals or runtime behaviour to any playable program,
so no captures or windowed run were made. No human play, network race or user acceptance is
claimed.


R0 implementation, 2026-10-24, Apple Silicon/macOS 26.6.2: Debug configure/build passed
without project compiler warnings (CMake retains raylib's existing OpenGL deprecation notice).
`ctest --test-dir build -j 4 --output-on-failure` passed all 115 active tests, including
24 GPU checks, in 145.67 s; 15 retired wake-field experiments stayed disabled. After the final
fixture render-failure assertion/waypoint-label cleanup, the warning-free build and both race
checks passed again (13.17 s), and playable-window startup exited successfully after five
frames. The full 3,300-frame scripted window run completed before that UI-only cleanup.
Graphics/network checks used desktop/loopback access after the sandboxed graphics attempt
could not reach macOS window services; that stuck attempt was terminated. All 277 local file
and heading links across the seven affected documents resolve, and `git diff --check` passes.
This is the initial implementation baseline, before user feedback and the visibility fix
below. No human acceptance, race network mode, downtown coverage extension or other
shipping-platform qualification is claimed.

**Historical design baseline (before R0), 2026-10-24:** reviewed the existing map coverage, basin course, room modes, match lifecycle,
water query and traffic interfaces. Race remains a proposal. The ferry currently supplies
no physical wake; downtown coverage, speed budget, crest recipe and jump count are open.
Verification of this documentation change, 2026-10-24, Apple Silicon/macOS 26.6.2:
Debug configure/build passed (CMake emitted raylib's existing OpenGL deprecation notice).
The full `ctest --test-dir build -j 4 --output-on-failure` run passed all 113 active tests,
including 23 GPU checks, in 131.29 s; 15 retired wake-field experiments remained disabled.
The first sandboxed attempt was interrupted after localhost socket binds were denied;
the complete rerun used desktop/loopback access. All 233 local link targets across the six
changed documents resolve, and `git diff --check` passes. The new coverage-section anchor
was checked against the river guide. No manual playback or audiovisual review was performed;
no new visuals or runtime behavior are introduced by this design document.
