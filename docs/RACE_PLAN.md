# River sprint: multiplayer race plan

Prepared 2026-10-24. **R0 prototype implemented; multiplayer race mode is not implemented.**
This document owns race rules, course pacing, jump encounters and acceptance. The
[multiplayer plan](MULTIPLAYER_PLAN.md) owns transport and prediction; the
[traffic guide](HARBOR_TRAFFIC.md) owns existing scenery vessels; the
[physics](BOAT_PHYSICS_PLAN.md) and [wake](WAKE_REWRITE_PLAN.md) plans own handling.

Status: R0's shortened route and physical wake fixture are implemented and automatically
measured. Initial user feel review found the source unreadable/artificial; the missing GPU
ferry and passing-boat wakes are now corrected. Next: review the revised encounter, then
R1's authoritative race core. R1–R4 remain unimplemented. The full downtown start is outside the current map; no human race play,
network race, complete course acceptance or user approval is claimed. See the
[R0 implementation and evidence](#r0-prototype--2026-10-24).

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
- Use ordered, wide checkpoint gates and a visible finish spanning the navigable corridor.
  Accept only forward swept crossings within a gate's lateral bounds, including an airborne
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
  choose a finish runout/spectator transition in R2. Their places remain fixed.
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
2. **R1 — authoritative race core.** Add a small `boat_race.[ch]` in `proteus_boat_core` for
   phases, locked entrant identities, grid, swept gates, progress, finish order, recovery
   and DNF. Supply authored course/source data without I/O or rendering. Adapt the existing
   basin crossing approach where useful; keep the basin course and replay semantics intact.
   Add scripted fleet trials and a race validator.
3. **R2 — network and presentation.** Add the server-selected race mode, versioned course
   identity/source settings, reliable phase/results and recoverable progress state. Update
   the owning protocol byte tables and compatibility revision before changing codecs.
   Bootstrap/resync includes current phase, GO tick, entrant identities and confirmed
   finishes; deterministic source state also reaches prediction/checkpoints. Add room
   discovery labels, client start restrictions, finish runout, HUD and results, plus a
   route-following bot. Publish runnable commands only once implemented.
4. **R3 — full course and pacing.** Add enough source encounters for the jump rhythm and the
   selected downtown/Eriksberg/bridge route. Tune using clean runs and four-boat races, not
   top-speed arithmetic alone. Check source traffic against ordinary traffic and landings.
   Extend mapped assets only after R0 identifies the necessary bounds.
5. **R4 — multiplayer acceptance.** Repeat complete rounds with bots, then two or more
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

Next: review the corrected visible-source encounter with the user. R1–R4 and human
acceptance remain open; the analytic ferry packet has not been replaced or declared natural.

## Verification and limitations

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
