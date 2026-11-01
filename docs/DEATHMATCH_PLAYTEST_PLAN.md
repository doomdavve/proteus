# Deathmatch playtest follow-up

Status, 2026-10-30: planning only, based on `11b5e1a`. No gameplay changes have been
implemented or verified by this plan. Owner: [deathmatch](DEATHMATCH_PLAN.md). Shared
handling work also belongs in [arcade stability](ARCADE_STABILITY.md) and
[boat physics](BOAT_PHYSICS_PLAN.md); networking follows the
[protocol](MULTIPLAYER_PROTOCOL.md). The user confirmed that “tornado” meant torpedo.

The latest playtest finds the grenade fun and balancing, but wants greater damage near
detonation; unexpected waves should upset boats more; a torpedo hit should sink its target;
and boats should be able to pass above/below one another. Free-boating difficulty is an
open feel decision. This feedback supersedes earlier acceptance as the next tuning task,
without invalidating the earlier recorded playtests.

User report, 2026-11-01, after v0.0.24 (build and platform not stated): deathmatch "worked well",
and contact in a two-person harbor room worked, but a boat still cannot jump over another boat. No
step of this plan has been implemented yet, so that matches steps 4–5c rather than being a new
finding.

## Execution contract

Execute one numbered step per PR. Use a separate worktree, keep the main checkout available
to the user, push each branch and open its PR for CI. A dependent branch starts at its
parent and targets that parent in its PR. Merge in order; merge any updated parent into
open children before asking for review. Do not implement this entire plan in one change.
No sub-agents are required. Each step below is small enough for a fresh execution context.

Before each step, read its listed files, the owning document and applicable AGENTS.md.
Confirm the current interfaces/revision numbers: this baseline will age. Implement only
that step, run its focused checks, then the build and full CTest preset. Record actual
date/platform/options/results/limits here and update affected live guides. Do not copy this
ledger into the documentation index. Update the index only at an area milestone.

Numbers below are proposed starting settings, not measured balance or user acceptance.
Preserve the cannon's accepted controls/feel, grenade timing/range/crest, torpedo speed,
ammo/reload/arming, steering/throttle, camera and music unless a step explicitly changes
them. Keep fixed ticks, bounded storage, canonical ordering and allocation-free core ticks.
Do not add a general rigid-body engine, a new weapon, difficulty menus or hosting work.

## What the current code actually does

| Area | Baseline and consequence | Read first |
| --- | --- | --- |
| Grenade | 7 m astern, 1 s fuse, 4 m crest, 10 m/s propagation, 70 m range, 8 s recharge. Creates a radial wave; **no hull damage**. Wave amplitude already falls as `(1 - radius/range)^2`. | `src/boat_combat.[ch]`: default rules, `boat_combat_advance`; `src/boat_water.c`: `boat_water_radial_query` |
| Stability | Ordinary wet-turn guard starts at 25 degrees; object worlds also receive automatic airborne and wet roll recovery. Capsize latches after 0.6 s of inverted wet contact. Several independent assists can erase a wave upset. | `src/boat.c`: constants, `hull_wet`, `motion_step`, `buoyancy_step`; `src/boat_pose.c` |
| Torpedo | 40 damage against 100 health; 32 m/s, three per life, 0.25 s arming. Actual health removed drives damage feedback. | `src/boat_combat.c`: defaults, `boat_combat_hits`; `tools/validate_boat_combat.c`: `hits`, `torpedoes` |
| Boat pairs | Whole-boat transformed vertical intervals reject separated boats, then heading-only rectangles choose a **horizontal** separation/impulse. A cabin corner can make intervals overlap even where hulls clear. No vertical pair response. | `src/boat.c`: `hull_vertical_bounds`, `boat_pair_*`; `src/boat.h`: `BoatPairContact`; `src/boat_fleet.c`: `boat_fleet_solve` |
| Projectile hits | Moving-position slab sweep uses final heading and one box; pitch/roll are ignored. “3D” here means a height slab, not full-pose collision. | `src/boat_combat.c`: `hull_hit` |
| Defeats | Match observation hard-codes hull destruction with a killer as **torpedo**. Grenade attribution currently comes from wave exposure. New grenade hull damage needs a real cause path. | `src/boat_match.c`: `touches`, `boat_match_observe`; `src/boat_match.h` |
| Compatibility | Physics revision 9 / protocol 8 at this baseline. Default combat rules enter `boat_net_content_digest` through checkpoint encoding. Combat has full and compact codecs with fixed capacities. | `src/boat.h`, `src/boat_combat.[ch]`, `src/boat_net_protocol.[ch]`, `src/boat_prediction.c` |

## Step 0 — reproducible encounter baseline

Branch from current main. Change test/review fixtures and documentation only.

1. Extend existing combat/fleet validators and review tools rather than building a parallel
   simulator. Reuse `boat_fleet_step`, real inputs and virtual time. Add named cases using
   `tools/boat_test_cases.h` when needed; keep heavy sweeps outside the quick lane.
2. Capture fixed-input encounters in open basin water: stern grenade chase, an armed torpedo
   hit/miss, crossing another boat's wake at 0/5/12 m/s with head/beam/oblique headings, a
   clear airborne crossing, a pitched near-clearance crossing and a landing onto another boat.
   Keep fixtures away from shore unless explicitly testing shore interaction.
3. Emit deterministic CSV: tick, speed, height, pitch/roll and rates, world-up dot, wet/flight/
   landing/capsize state, health, pair normal/depth/impulse, killer/cause. Reports must identify
   fixture, settings and starting revision. Repeating a fixture must reproduce its report
   within the same build; cross-platform comparisons use invariants/tolerances.
4. Save the neutral and held-turn wave fixtures and calm straight/turn/docking controls as
   reusable baselines. Do not tune against one lucky crest phase. Include mirrored cases,
   several encounter phases, a severe but recoverable hit and an inversion that must capsize.

Done: reports explain current close/far grenade outcomes and false boat contacts; existing
tests still pass. Record measured baseline values here. Do not mark future behavior tests
as passing or weaken an existing physical bound to make the baseline pass.

## Step 1 — one armed torpedo hit sinks a healthy boat

Parent: step 0. Primary files: `src/boat_combat.c`, `tools/validate_boat_combat.c`,
`tools/validate_boat_match.c`, the deathmatch plan and `docs/PLAYING.md`.

1. Set default torpedo damage to default full hull health (100 at this baseline). Continue
   using `min(remaining_health, configured_damage)` for actual damage and HUD numbers.
   Custom rule sets still obey their configured health/damage; do not special-case every hit
   into a defeat independent of the rules.
2. Preserve unarmed harmless breaks, scenery blocking, grace, swept hits, ammo and life fences.
   Reuse the existing defeat/respawn path; no new sinking animation is required.
3. Replace stale three-hit expectations. Check one armed hit on full and partial health,
   no overkill number, one score/feed event, simultaneous hits, unarmed break, invulnerability,
   owner exclusion, reused slot and respawn inventory. Retain rejection/serial-wrap checks.
4. Check rule digest changes and mixed-rule peer rejection. A value-only rules change needs
   the content digest change, not a gratuitous packet-layout revision.

Done: a legitimate armed hit defeats a healthy default boat exactly once. Review the current
slow/difficult aim in play before considering speed or ammo changes; those are outside this step.

## Step 2 — localized grenade hull damage

Parent: step 1. Primary files: combat core/header/codecs, match core/header, net protocol,
combat/match/net validators and HUD/audio consumers of defeat cause.

Use a **single blast at detonation**, plus the existing physical travelling wave. This gives
near-centre damage without a per-victim hit mask or damage every tick along the entire ring.
Proposed rule: maximum 80 hull damage at distance 0, falling linearly to 0 at 10 m;
`damage = max_damage * clamp(1 - distance / radius, 0, 1)`. For the first candidate, distance
is from the blast point on the water surface to the nearest point of a documented physical
boat envelope, including height. Thus a boat high above the blast is not hit through the air
column. Use the current full-pose physical envelope initially; step 4 will refine it.

1. Add explicit blast damage/radius rules; do not reuse the obsolete gun `damage` field.
   Add a core helper for clamped actual health removal/feedback so torpedo and blast agree.
   Implement blast resolution during the grenade-to-wave transition in `boat_combat_advance`,
   before that tick's integration. Sample the pre-integration boat pose and detonation-time
   water height. Document this tick convention and test its boundary.
2. The consumed grenade detonates only once. Iterate grenade slots then target slots in
   canonical order; simultaneous blasts stop damaging a defeated boat. Apply self-damage,
   but never award self-hit feedback/kill credit as an opponent kill. Reject inactive,
   pending, already defeated/capsized and grace-protected targets. Keep existing projectile
   birth/life fencing: a grenade predating a target life cannot damage that replacement.
   Departure invalidates owner objects as it does today.
3. Add a persisted, validated lethal cause (or equivalent explicit lethal record) so grenade
   hull kills are reported as grenade, torpedo kills as torpedo, and self-blasts use existing
   self/environment scoring. Audit all `BOAT_MATCH_*` consumers. Do not guess cause from
   whichever wave/pressure happens to be nearby when observation runs.
4. Update full checkpoint codec, compact codec, hashes, constants and decode validation for
   every new field. Rules use the checkpoint path; mutable lethal records also need the
   compact path. Recalculate maximum packet/page/checkpoint sizes, reject malformed causes
   atomically, and update protocol documentation. A serialized layout change requires the
   next protocol version; use the actual next revision when executing.
5. Assert damage at centre, halfway, boundary and beyond; just above/below radius; tilted
   boat; actual partial-health damage; self-damage; grace; stale life/owner; four simultaneous
   grenades; one blast over many ticks; no hull damage from the distant travelling ring;
   grenade versus torpedo lethal cause; nonlethal grenade followed by cannon capsize; respawn
   clears cause. Round-trip active and just-defeated states through both codecs and reconnect.

Done: damage falls monotonically with distance, is applied once, and the existing crest
remains unchanged. This candidate adds a damage model; its exact 80/10 settings need play.
Do not silently add blast occlusion against buildings: current radial waves are unoccluded.
Record that first-candidate limitation and check a quay encounter during review.

## Step 3 — let strong wave encounters overcome assistance

Parent: step 2. Primary files: `src/boat.c`, stability/arcade/banking validators and shared
review fixtures; `docs/ARCADE_STABILITY.md`, `docs/BOAT_PHYSICS_PLAN.md`.

Start with shared behavior, because the physical problem exists in free boating too. Do not
key physics on `world->combat`, renderer choice, local player identity or wake-pointer presence
beyond existing semantics. Do not raise every wave or change steering to fake instability.

1. Use step 0 reports to isolate ordinary turn guard, wet recovery and airborne recovery.
   Sweep **one coefficient group at a time**. First try wet extra roll recovery/damping at
   75%, then 50% of baseline. If strong wet encounters are still erased, test ordinary guard
   stiffness/damping at 75% only. If takeoff rotation is immediately erased, separately test
   air roll damping/recovery at 75%. These are experiment candidates, not all-at-once edits.
2. Select the least change producing a visible, lasting beam-wave upset while retaining calm
   turns and recoverable small crossings. Preserve inversion fade, capsize latch and reset.
   Do not reintroduce steering-commanded air rolls. Avoid new persistent “surprise” timers
   or arbitrary random forces; approach, crest phase and speed create the surprise.
3. Before choosing a candidate, require calm full-turn bank below the existing 30-degree
   envelope and release below 2 degrees within the current validator's documented limit;
   small-wave fixtures must remain recoverable. For designated strong beam encounters,
   seek at least 15 degrees more peak heel or a newly sustained >45-degree heel, with at
   least one physical capsize in the severe set. Require both survival and failure cases
   across the set rather than every wave forcing defeat. If these proposed feel proxies
   cannot coexist with existing bounds, report the measured conflict and revise the target
   explicitly; do not loosen unrelated safety assertions.
4. Compare phase/heading/speed sweeps, neutral/held steering, head/following/beam waves,
   grenade self-waves, landing scrub, repeated crossings and long-run boundedness. Run race
   and outer-voyage scripted fixtures: report finish/DNF and route-recovery changes instead
   of retuning their drivers to conceal harder water.
5. Bump `BOAT_PHYSICS_REVISION` for changed integration; verify authority/prediction replay,
   old recording rejection/preservation and advertised room compatibility. Record changed
   trajectories and limits in owning physics docs; fix stale live stabilization claims in
   playing/HUD guides without rewriting historical evidence.

Done: deterministic measurements show greater strong-wave vulnerability with retained calm
handling. Human comparison must include deathmatch and ordinary basin/river/outer boating.
If shared tuning makes free boating unpleasant, stop before broad rollout and present the
measured candidate for a choice: milder shared tuning or an explicit room handling profile.
A profile requires authority-advertised settings, checkpoint/prediction plumbing and a new
scoped follow-up; do not improvise a client-only deathmatch multiplier.

## Step 4 — full-pose boat contact detection

Parent: step 3. Change geometry/detection first; vertical response follows in step 5.
Primary files: `src/boat.[ch]`, `src/boat_pose.[ch]`, `src/boat_model.[ch]`,
`tools/validate_boat_fleet.c`, `tools/boat_fleet_review.c`. A small core contact module is
acceptable if these helpers would otherwise overgrow `boat.c`; no general framework.

1. Define a shared, skin-independent physical hull plus smaller cabin as a fixed compound
   of oriented boxes in hull-local metres. Start from the current 3.6 m by 1.5 m hull,
   .35 m draft/freeboard and .8 m cabin model. Inspect actual generated hull/cabin extents
   before fixing offsets: render parts have differing scales. Do not wrap the cabin's full
   height around the entire footprint. Exclude decorative cannon/engine details initially.
   Document the physical envelope's deliberate visual tolerance for every skin.
2. Transform primitives with the same pitch/roll/heading convention as `boat_pose_rotation`;
   simulation X is mirrored in renderer coordinates. Centralize conversion and test it.
   Retain cheap conservative bounds as broad phase only. Use 3D box SAT (face axes and
   nondegenerate cross axes) as narrow phase; canonical primitive/axis order resolves ties.
   Normalize axes, handle parallel axes safely and use explicit metre-based tolerances.
3. Produce one stable representative contact per boat pair for the current fixed pair
   solver, including world normal, depth and point/arms. Select the deepest primitive
   contact with deterministic ties; log residual compound overlap. If it cannot converge in
   step 5's landing/crossing cases, add a small fixed manifold in a separately scoped fix.
4. Keep the old public solver behavior for this geometry-only PR. Test the new geometry
   independently before changing response; do not route vertical normals through the
   planar impulse code. Wire full-pose detection and response together only in step 5.
5. Check level side/head/glancing overlap, fully separated heights, bow-up crossing whose
   whole-boat Y intervals overlap but primitives clear, rolled/inverted cases, cabin strike,
   near-parallel axes, swapped pair order, exact touching and epsilon separation. Render
   diagnostic physical boxes over all skins from side/top/oblique views on CPU/GPU.

Done: clear over/under poses have no narrow-phase contact; real hull/cabin intersections
still do. These tests must expose why a smaller global height threshold is insufficient.

## Steps 5a–5c — vertical contacts and crossings during a tick

Parent: step 4. Primary files: pair solver in `src/boat.c`, `src/boat_fleet.c`, pose motion
helpers, fleet/match/net/prediction validators and review fixtures.

Execute these as three mandatory stacked PRs: **5a** implements paragraphs 1–2 and the
endpoint response tests from 4–5; **5b** implements and independently validates the swept
query in paragraph 3; **5c** integrates time-of-impact response and runs every case below.
Each PR records its own evidence. Step 5a enables full-pose endpoint contact; 5b does not
change production scheduling. The full over/under requirement remains open until 5c.

1. Extend `BoatPairContact` and pair speed/effective-mass/apply functions to a 3D normal and
   contact arms. Reuse the full point velocity and pitch/roll/yaw Jacobians from
   `boat_pose_motion`; map linear impulse to X/Z plus `vertical_velocity`, and generalized
   angular impulse to yaw/pitch/roll rates using existing inertias. State angular rates are
   Euler/generalized rates, so do not insert an unrelated world-inertia formula.
   Check the effective-mass derivation against finite differences and impulse work.
2. Extend positional separation to height as well as X/Z with a small fixed slop/correction
   cap; no velocity injection from overlap correction. Preserve canonical simultaneous
   Jacobi passes, accumulated one-bounce impulse, inverse-mass split and shore containment.
   Keep stats/attribution based on normal impulse magnitude. Preserve held-grid behavior;
   test that post-solve held-slot restoration cannot leave an exploding overlap.
3. Add pair collision checks over `before` to candidate poses, not just at the endpoints.
   Use deterministic conservative advancement with conservative linear + rotational speed
   bounds for the compound boxes, a fixed iteration budget, and explicit tolerances. On
   budget exhaustion, retain a conservative contact at the last safe fraction and record
   the event; never silently declare clearance. Choose earliest time, then slot/primitive
   order for ties. Resolve at contact and advance the remaining fractional tick without
   advancing the wake pool or launching weapons twice. Refactor integration only as needed
   to support this; keep the public 120 Hz tick and once-per-tick emission unchanged.
   Keep the pure sweep query in 5b and production fractional integration in 5c.
   In 5c, use a fleet-wide earliest-event loop so every boat advances to the same fractional
   time before resolving simultaneous pairs. Start with an eight-event budget per tick;
   guard zero-time repeats with contact slop and a minimum time advance. If exhausted,
   stop unresolved approaching motion conservatively for that remaining interval, increment
   a diagnostic counter, and require zero exhaustion in the ordinary review fixtures.
   Recompute forces at explicit fractional time without emitting wakes between events.
   Do not integrate different boats to inconsistent times or multiply gravity by retries.
4. Test a complete clear flyover with overlapping X/Z and **zero** impulse/score touch;
   a pitched clearance; landing onto another hull with vertical response and no sideways
   teleport; underside/cabin strike; head-on ramming; mid-tick collision with clear endpoints;
   fast rotation; initially deep overlap; four boats; shore pin; reset/spawn and race grid.
   Define finite speed/rate envelope coverage from actual gameplay/protocol limits; report
   fallback counts and worst residual depth. Never call endpoint-only substeps a sweep.
5. Extend isolated passive contact tests to vertical kinetic energy and generalized angular
   energy, with no gravity/buoyancy/engine work during the measured solver operation. Check
   momentum, non-increasing energy within numerical tolerance, symmetric pair reversal and
   bounded separation. Do not claim global conservation for an entire powered water tick.
6. Verify checkpoint resume and prediction correction through a crossing and landing under
   delay/loss/reordering. Bump physics compatibility again when production response changes.
   If only interfaces/internal state change, prove existing snapshot fields suffice; any
   new persistent solver state must join hash/checkpoint/prediction codecs and versioning.

Done: boats can actually travel above/below one another, and contact during descent still
works. Endpoint geometry tests alone do not complete this requirement. Kinematic moored
fleet/traffic and shoreline remain separate envelope systems; audit and document their
remaining approximations rather than expanding this step into scenery physics.

## Step 6 — projectile geometry and combined qualification

Parent: step 5. Reuse step 4 primitives for `hull_hit` and grenade distance. Replace the
heading-only projectile slab with a full-pose moving-compound sweep, using the bounded
time-of-impact machinery from step 5 where applicable. Respect projectile radius and
cannon curvature, and choose the earliest scenery/water/hull hit with deterministic ties.
Test torpedoes passing under airborne boats, rolled/pitched actual hits, cabin misses,
rotating targets, unarmed contact and cannon contact. Keep accepted cube appearance intact.

Run the complete encounter matrix with all changes together. Check lethal torpedoes plus
weaker assistance do not make the cannon irrelevant, grenade self-risk has an escape path,
multi-grenade damage/feed is readable, deaths award once and respawn/grace work. Compare
CPU/GPU captures of near/far blast, upset/recovery/capsize, clear flyover and landing contact.
Run a native windowed demo and actual human deathmatch/free-boating sessions separately;
record who performed each check. Browser and native peers must agree on outcomes and
compatibility rejection. Run platform CI and the existing four-client impairment checks.
Measure core collision cost with full four-boat contacts against step 0; report p50/p95,
worst iteration/fallback count and residuals. Profile with Instruments if runtime cost or
stutter warrants it. Optimize only a demonstrated regression.

Done: automated checks and recorded visual evidence pass; list human/platform checks still
pending. Human acceptance is required to call the balance accepted, but missing acceptance
does not prevent preparing and pushing the reviewable implementation PR. Release and public
deployment are separate work, governed by the release/hosting guides.

## Commands and handoff ledger

Run from the implementation worktree root. First configure may fetch pinned dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --preset quick
ctest --test-dir build --output-on-failure -R 'boat_combat|boat_match|boat_fleet|boat_net|boat_prediction'
ctest --preset full
./build/wake_velocity --renderer cpu --screenshot build/basin.png --time 3
./build/wake_velocity --renderer cpu --location river --screenshot build/river.png --time 3
```

Use the existing `boat_combat_review` `--screenshot`, `--time`, `--torpedo`, `--gpu` and
`--play` options for weapon captures/demos. Read its argument parser before use; its review
path opens a window even for CPU rendering. Add explicit scenario options in step 0 for
repeatable new captures instead of claiming the generic screenshots exercise combat.
GPU tests require a graphics context and `PROTEUS_GPU_TESTS=ON`; report when off. Socket
tests may need execution with local socket access. Do not substitute partial reruns for
the required full suite or report upstream/platform failures as passes.

| Step | Current status | Next task / evidence required |
| --- | --- | --- |
| 0 | Not started | Measure existing encounters and false contacts |
| 1 | Not started | Lethal default torpedo and preserved arming/grace |
| 2 | Not started | Blast falloff, exact-once damage, cause/codec changes |
| 3 | Not started | Measured assistance candidates and shared feel comparison |
| 4 | Not started | Full-pose compound geometry and diagnostic captures |
| 5a | Not started | 3D endpoint response and passive invariants |
| 5b | Not started | Independently tested conservative swept query |
| 5c | Not started | Time-of-impact integration and full crossing matrix |
| 6 | Not started | Projectile parity, combined qualification and human balance review |

Each completed row links to a dated result section here containing commit/PR, implementation,
settings, automated checks, capture/demo evidence, human feedback, next task and limitations.
Current next task is **step 0**. No gameplay implementation, audiovisual verification or new
user acceptance is claimed by this planning change.


### Planning verification — 2026-10-30

Apple Silicon/macOS, separate `docs/deathmatch-playtest-plan` worktree, baseline `11b5e1a`:
Debug configure and full build passed using existing pinned raylib/Lua/ENet source trees.
The build emitted the existing upstream `stb_vorbis.c` pointer-comparison warning.
`git diff --check` passed; all 217 local Markdown links/anchors in the three changed
files resolve. Existing review-tool argument names were checked against their parsers.

The initial sandboxed full run passed 308/321 enabled tests: twelve socket/hosting tests
failed without socket access, and `boat_plugin_session.game_controls` failed its expected
status-text assertion at `tools/validate_boat_plugin_session.c:867` (reported marina layout
rather than exposed water). The complete rerun with local socket access passed all **321**
enabled tests in 33.12 s, including that plugin case; 13 retired field tests were disabled.
The plugin assertion discrepancy was not investigated or fixed in this documentation change.
GPU tests were off. No new game captures, manual playback, browser/platform qualification
or gameplay acceptance were performed or required for this planning-only change.

Completed: inspected production paths, wrote the executable plan and updated its owner/index.
Next: step 0. Known limits: all tuning numbers and collision designs remain proposals until
implemented, measured and reviewed; the first-run plugin discrepancy remains unexplained.
