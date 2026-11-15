# Wake coverage, turning continuity and lifetime research

Status, 2026-11-15: R0 implementation reviewed; corrected CPU baseline complete.
Current step: R0 graphics/media qualification; macOS currently reports no monitor.
Open: corrected GPU matrix, remaining reels/visible demo, budget agreement and human checks.
Done when: each step has matched before/after motion demos and performance numbers,
a reviewed candidate meets the agreed budgets, and its contract is moved into the specs.
Next: restore graphics access and finish R0 verification; no R1/R2 candidate or PR promotion.

## Scope and existing work

Investigate the user's reports that some boats leave no wakes, turning wakes separate into
straight pieces, and waves disappear too soon. Preserve enjoyable crossing, jumping and landing.
This is a bounded arcade approximation study, not a return to the retired physical field.
Parent: [wake history](WAKE_REWRITE_PLAN.md). Read the earlier
[continuity experiment](WAKE_OBJECT_SMOOTHING.md) before repeating its shorter-span approach.

The [open loops board](README.md#open-loops) was checked. This track defers graphics Batch C
and Windows acceptance, general multiplayer impairment/soak work, race pacing, stability tuning,
marina authoring, and the paused Gothenburg Harbour work. Exercise serviced marina boats without
redesigning their schedules or ownership. Record any necessary integration dependency explicitly.
User acceptance of past wake fun does not establish acceptance of a new recipe.

## Mechanical realism and performance decisions

Realism here means credible wake behavior that the player can learn and use: emission from a
moving wet hull, travel away from the historical source path, persistence after the source stops,
independent crossing trains, and repeatable boat response to encounter angle, speed and wave age.
Graphical detail, foam continuity and photographic appearance are not the success criteria.
These are proposed qualitative targets; R0 must define their measurable scope and tolerances.
Arcade height and handling remain explicit tuning choices, not claims of physical accuracy.

For every approximation, report which mechanical behavior it preserves or changes, the measured
CPU/GPU/memory/network saving, and the resulting encounter error. Prefer the cheapest candidate
that meets the behavioral thresholds, rather than maximizing lifetime or visual smoothness alone.
Keep geometry/source-path probes separate from coupled gameplay outcomes so a prettier surface
cannot conceal changed forces. Physical reference means the unsimplified comparison recipe;
it does not imply a validated real-world hydrodynamic model.

Use **Astra review** for the complicated decisions: R0 behavioral criteria and experiment design,
R2 propagation/joins and overlap, R3 age simplification/resource and protocol bounds, and R4
combined mechanics/performance acceptance. Delegate these reviews to `gpt-6-astra` with the
relevant code, raw measurements, matched demos and explicit questions. Review the design before
implementation and the evidence before promoting the candidate; request another pass when a
material issue changes the design. Routine documentation and emission wiring do not need a
separate Astra pass unless they change physical source behavior.

Each review records recommendations, addressed findings, unresolved tradeoffs and the supervisor's
decision. Astra advice is not measured evidence or user acceptance. An initial planning review was
completed on 2026-11-15; the experimental reviews remain pending until their evidence exists.

## Source findings to reproduce in R0

- Object wakes emit every 60 ticks at 120 Hz, with 32 records per player boat and a
  1920-tick/16-second maximum life; the source speed threshold is 3 m/s with a wetness gate.
- Straight independent pieces expand along their own normals. Adjacent normals diverge on turns;
  the longitudinal taper and separate birth envelopes can also affect visible joins.
- Original traffic, authored traffic, serviced actors and the special ferry use different paths.
  Serviced actors skip the traffic wake path; establish their actual replacement coverage on the
  current marina implementation before calling this a confirmed missing-emitter bug.
- Nearby GPU fragment shading uses a 256 m stamped field. Vertex displacement and distant
  fallback shading still evaluate records analytically; CPU hull queries scan the object pools.
- Pool capacity is coupled to emission interval and lifetime. Network record slots use one byte,
  and the renderer has a separate combined-pool bound. Capacity changes require a compatibility audit.

These are source observations, not fresh visual or performance results. Historical M1 Max G3a
stress GPU p95 of about 3.3 ms is whole-scene timing, not isolated wake cost or a current budget.

## Evidence required at every step

Each R0–R5 checkpoint delivers a review bundle before the next dependent step:

1. **Before/after demos:** baseline and candidate clips or replayable scripted demos with exact
   commands, revisions, scene/plugin versions, seeds, camera, controls and simulation ticks.
   Use chase and fixed overhead views, the same framing and timestamps, and a short labeled
   side-by-side comparison. Stills supplement motion; they do not substitute for it.
2. **Performance table:** baseline, candidate, absolute delta and percentage delta for CPU
   simulation/wake-query time, CPU render preparation, GPU time and complete frame time;
   report p50/p95/p99, live/peak records, field work, CPU/GPU memory and upload bytes.
   For replicated changes add authority tick cost, birth rate, bytes/s and checkpoint size.
   Unsupported counters are marked unavailable with the reason, never reported as zero.
3. **Reproducibility:** Release build and identical settings/hardware, explicit warmup,
   at least three alternating baseline/candidate runs and a measured saturated interval of at
   least 60 simulated seconds. Separate uncapped timing from visible playback. Record OS/GPU,
   resolution, scene resolution, AA, build flags, sample count, spread and raw JSON/CSV.
4. **Decision:** observed benefit, regressions, accepted/rejected/deferred hypothesis, automated
   checks performed, agent playback and human feedback separately. Rejected experiments retain
   their demos and numbers. Do not manufacture a visual change for harness/documentation steps:
   show unchanged paired demos and quantify harness overhead or final-vs-baseline results.

Store small manifests/reports under `docs/evidence/wakes/rN/`; keep generated binaries and large
captures out of Git and record their retrievable artifact locations. Each manifest includes
commands and artifact checksums. Add any new harness commands to the testing documentation.

The shared demo matrix covers straight acceleration and slow travel; wide turns, tight circles,
a figure-eight and turn reversal; stopping, dry-stern flight and landing; crossing wakes from
four boats; moving/departing/arriving serviced craft and ordinary traffic; a later return across
old wakes; and teleport/reset. Run basin and outer, default low resolution and the current
high-resolution mode. R0 selects short representative clips; later steps retain the relevant
clips plus a common straight/turn/crossing regression reel.

## R0 — Baseline and measurement harness

**Status:** implemented locally; review/exit gates pending. [R0 evidence bundle](evidence/wakes/r0/README.md)
owns commands, fixtures, source coverage, raw artifact locations, measurements and limits.
Audit every moving-hull class and location/scene, naming the source,
visual-only versus physical behavior, thresholds, and omissions. Include player/remote boats,
original and authored traffic, serviced residents/visitors and the special ferry. Confirm gaps
in motion with foam and surface displacement distinguishable in diagnostic views.

Create deterministic source-path fixtures and separate coupled hull-driving demos. A fixed
source path makes geometry comparisons fair even when a changed physical surface alters handling.
Measure one and four player emitters, maximum representative scene traffic, and combined load.
Keep emission active throughout the saturated interval: the existing finite wake-loop benchmark
spends much of its long run after expiry and cannot alone answer this research question.

Define behavioral probes before selecting a geometry: track crest arrival at fixed points after
source stopping/turning; measure crossing reinforcement/cancellation and post-crossing travel;
compare hull encounters at several angles, speeds and ages. Record peak heave/roll/pitch,
vertical acceleration, launch height, landing impulse and capsize outcome where available.
Include slow displacement traffic: a player planing threshold is not a universal source rule.
Report normalized measures where arcade wave heights make real-world magnitudes inappropriate.
Separate hard invariants (finite outputs, deterministic replay, no discontinuity-spanning source)
from allowed encounter ranges; changed contacts need not preserve an entire driven trajectory.
Measure hull-query count, records examined and expensive evaluations, under both dispersed and
densely overlapping loads. Declare primary target hardware. Astra reviews the probe design and
proposed tolerances before candidate implementation.

**Demo:** current build versus harness build, identical behavior, plus annotated reproductions.
**Numbers:** current 16-second cost, wake-off control and harness overhead; separate simulation,
rendering and total frame costs. Wake-off deltas include interaction effects, not perfect isolation.
**Exit:** reviewable baseline and a proposed numeric CPU/GPU/memory/network budget agreed before
promoting candidates. Use current graphics budgets as an outer constraint; do not silently
replace them. Record unresolved platform measurements.

## R1 — Consistent moving-hull coverage

**Status:** not started; depends on R0. Establish a per-hull wake policy and test a shared
emission adapter for missing classes, using actual stern motion, speed, dimensions and wetness.
Decide explicitly which displacement boats need physical waves and which need cosmetic trails.
Avoid duplicate wakes when an actor switches between traffic and serviced harbour ownership.
No wake across teleport, stationary rotation, a dry interval or a discontinuous route wrap.

**Demo:** paired fleet departure/arrival and low-speed passes, every previously missing class
labeled; show stationary and ownership-transfer cases too.
**Numbers:** incremental cost per added emitter and maximum combined traffic/player load,
including CPU pose/query work and server cost if physical sources are authoritative.
**Exit:** coverage table resolved, bounded source count and no unexplained missing/duplicate wakes.

## R2 — Turning geometry experiments

**Status:** not started; depends on R0, can use isolated sources before R1 integration.
Compare the current recipe against (A) shorter overlapping straight pieces as a control,
(B) rounded/arc-ended overlapping wavelets, and (C) a bounded local join between adjacent
pieces if B leaves gaps. Do not select a representation in advance or revive a connected
strip known to fold in tight turns. Change geometry independently of lifetime and source strength.

Measure uncovered crest length near joins, join amplitude dips/overshoot and their growth with
age and turn radius. Define sampling and thresholds from R0. Check gradients and vertical
velocity, CPU/GPU agreement and tight-circle accumulation. Preserve signed crossings; any
local overlap weighting must retain cancellation and useful jumps, with its tradeoff documented.

Astra reviews propagation and overlap before implementation and the measured result afterward.
Check whether outer turn fronts separate and inner fronts concentrate; filling every geometric
gap is not itself a physical requirement. Reject joins that pull an old wake along with the
source's new heading, connect unrelated emission times, or add artificial ridges at endpoints.
Any spatial/time-dependent blending must contribute its derivatives to the returned gradient
and vertical surface velocity. Compare encounter outcomes against unchanged straight and crossing
fixtures, including tight-turn amplification, rather than scoring continuity alone. Check extra
sideways/backward launch faces from rounded caps and finite-difference derivatives at seams.
Compare unchanged parameters first; report amplitude-retuned variants separately against straight
crest height, slope, arrival and impulse. Restrict overlap correction to adjacent pieces of the
same local front: even one emitter can leave legitimately crossing old and new trails. Do not
use emitter-wide normalization or a global height clamp as the continuity fix.

**Demo:** every candidate versus baseline on wide/tight turns, reversal and figure-eight,
including the same wake several seconds later; show the physical surface as well as foam.
**Numbers:** equal source-density comparisons and each candidate's required record count,
CPU/GPU timings and memory. Distinguish geometry cost from increased emission rate.
**Exit:** select the simplest candidate with a measured continuity benefit and acceptable handling;
reject candidates that conceal gaps only with foam or introduce folds or excessive reinforcement.

## R3 — Lifetime and old-wave simplification

**Status:** not started; depends on R2 selection. Compare 16, 24 and 32 seconds with unchanged
young-wave behavior. First measure a capacity-expanded reference, then test fewer/broader old
wavelets or another bounded age-based simplification. Quantify transition error and pop risk;
do not merge unrelated crossing wakes into one front merely to meet a record budget.

Account for earlier fade onset as well as hard expiry. Longer physical persistence and cosmetic
foam persistence are separate settings and measurements. Define retention/eviction when full;
never silently overwrite live records. Audit protocol, snapshots, prediction/reconciliation,
replay compatibility, renderer constants, shader loops and far-field fallback before integration.

Astra reviews the age approximation and capacity/compatibility design, then its evidence.
Match crest arrival, signed height, slope and vertical velocity against the unsimplified recipe;
a small height RMS alone can conceal a shifted crest or a changed launch. Include errors in
hull encounters across the age-transition boundary. Widening/merging old waves must not create
an unexplained new impulse, erase independent crossing trains or make waves follow the boat.
Measure affordability as useful persistent encounters per budget, not seconds retained alone.
Specify the age range meant by unchanged young-wave behavior: longer life also delays fading.
Compare simplification with an unsimplified reference of the same lifetime, then compare that
reference with 16 seconds. Merge only compatible neighboring source segments/ages/directions/
phases, never across reversals or discontinuities. Test behavior-preserving optimizations too:
active-record iteration, conservative support rejection and a bounded spatial index. Physical
retention must not depend on the camera or visibility.

**Demo:** paired timed returns across 16/24/32-second wakes, stopping and watching expiry,
four-boat circles, and a fixed camera that exposes age-transition popping.
**Numbers:** steady-state cost and memory for each lifetime, simplification error, upload/network
cost and fallback work. Identify the longest lifetime within budget; 32 seconds is a hypothesis.
**Exit:** measured quality/cost choice, explicit compatibility strategy and no abrupt disappearance.

## R4 — Integrated gameplay and platform qualification

**Status:** not started; depends on R1–R3. Combine the selected coverage, geometry and lifetime.
Check crossings, jump/landing/capsize outcomes, smooth derivatives, bounded resources,
reset/reload, remote prediction and authored scenes. Preserve core/net boundaries and deterministic
clock behavior. Run the build and full CTest preset; inspect CPU/GPU captures and run visible
scripted demos plus hands-on driving. Browser fallback and native/browser joins need explicit
checks when their paths or wire contract change; report unavailable platforms as open items.

**Demo:** original R0 build versus integrated candidate, full common reel and playable commands.
**Numbers:** repeat the R0 matrix, including combined maximum load, browser and a slower GPU
when available. Investigate frame-tail regressions; use Instruments if profiling warrants it.
**Astra gate:** review the final behavioral/performance tradeoff table, including rejected cheaper
options, encounter errors and remaining assumptions. Recommend the least costly candidate meeting
the mechanical criteria; if none does, retain the baseline and record the unresolved hypothesis.
**Exit:** agreed budgets pass, no unexplained gameplay regression, and user feedback is recorded.
Promotion requires three independent gates: mechanical correctness (including consistent
derivatives and encounter ranges), measured performance budgets, and player acceptance of
readable approaches, controllable jumps, landings and useful old wakes. No gate substitutes for
another. A passing automated check is not human acceptance.

## R5 — Close and document the chosen behavior

**Status:** not started; depends on R4. Move the wake recipe, coverage, persistence, resource
bounds and limitations into `docs/WAKES.md`; update playing/command-line/testing docs and the
multiplayer protocol spec as affected. Search README, docs and plugin guides for superseded
claims. Link the spec from the documentation index and mark this plan done only after the
review decision; turn deferred work into named open items or explicitly drop it.

**Demo:** retained baseline/final review bundle with verified reproduction commands.
**Numbers:** final before/after table and raw-report links, including all remaining platform limits.
**Exit:** changed local links checked, verification evidence dated, implementation/agent playback/
human acceptance distinguished. Keep one local branch with focused commits per accepted step;
only push/open the track PR after explicit user approval.

## Planning evidence — 2026-11-15

Read the current object emission/query, traffic paths, stamped-field implementation, wake
continuity history, graphics measurements and performance benchmark limitations. R0–R5 are
unexecuted; no new demos, performance measurements, build or audiovisual verification are
claimed by this documentation change. Existing stability and wider platform reviews stay open.

### Initial Astra design review — 2026-11-15

`gpt-6-astra` reviewed the plan, object recipe and continuity history. Incorporated its
recommendations on encounter-based criteria, derivative/front identity, comparable tuning,
same-lifetime references, query cost drivers, behavior-preserving acceleration and independent
mechanics/performance/player gates. This review produced planning advice, not experimental
verification. No candidate is selected; later Astra design and evidence gates remain open.

## R0 implementation — 2026-11-15

Added `wake_research` with continuously emitting fixed paths, sustained one/four-emitter pools,
dense/dispersed query loads, fixed observation probes, wake-off and aggregate-timer controls,
production GPU rendering/timing, traffic-pool stress and matched motion captures. Added a
three-repetition matrix/report driver and a separately paced/rendered paired-media driver.
`wake_coverage` samples the shipped marina plugin through production scene/service evaluators;
`wake_encounters` retains source-off signed/derivative traces and a 27-case calm-twin hull grid;
`wake_turn_probes` scores outer separation/inner concentration against matched straight fields.
Production simulation, source policy, rendering recipes and network layout are unchanged.

The Astra R0 design review required saturation preroll longer than the full lifetime, explicit
raw-tracker versus integration-reset teleport cases, separate synthetic/actual query costs,
source-off observations, diagnostic overhead measurement and unavailable counters rather than
invented zeros. These are incorporated; proposed thresholds remain proposals for agreement.
The supervising decision is to retain this baseline and review its evidence before R1/R2.

Completed task: baseline harness and source-coverage audit. Next task: complete the R0 evidence
review and budget/criteria agreement before the first coverage/geometry candidate. Browser,
slower-GPU/platform, complete network/authority counters and human acceptance remain open.
Detailed dated verification and review findings belong to the linked evidence bundle.

R0 verification checkpoint: corrected Release CPU matrix completed 240 runs / 60 passing
neutrality comparisons; the source coverage and 27-case encounter tables are retained. Astra
reviewed the corrections and found no remaining blocking implementation defect. Straight/chase
production and height/foam paired reels completed with identical PNGs; other reels and the
corrected GPU matrix are pending. Debug/Release builds passed. The final full CTest attempt
passed 581/616; all 35 failures were GPU cases unable to initialize the missing monitor.
The earlier 614/614 full pass predates final probe corrections and is not final verification.
The evidence bundle owns commands, raw checksums, numbers and the provisional screening budget.
R0 stays open until its remaining verification and agreement gates are resolved.
