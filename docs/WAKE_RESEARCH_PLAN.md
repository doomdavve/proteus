# Wake coverage, turning continuity and lifetime research

Status, 2026-11-19: R2b experimental CPU basin trial implemented; handling preservation failed.
Current step: retain production baseline; offer reset-based experimental comparison.
Open: changed curved encounters/player feedback; full-frame, multi-source, GPU/platform and HDR review.
Done when: each step has matched before/after motion demos and performance numbers,
a reviewed candidate meets the agreed budgets, and its contract is moved into the specs.
Next: user basin comparison and review the changed encounter contract; no geometry selected.

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

**Status:** baseline reviewed; user authorized continuation with provisional screens. [R0 evidence bundle](evidence/wakes/r0/README.md)
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

**Status:** cosmetic-first candidate implemented, measured and natively verified locally.
[R1 evidence](evidence/wakes/r1/README.md) owns the policy, paired motion, cost and limits. Establish a per-hull wake policy and test a shared
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

**Status:** R2a CPU-only screen complete; all alternatives rejected. R2b design reviewed;
[the transport proposal](WAKE_R2B_DESIGN.md) and [algebra evidence](evidence/wakes/r2b/README.md)
define the next private experiment. Its capture ledger and p=1 evaluator are implemented privately;
[field evidence](evidence/wakes/r2b/field/README.md) retains the coverage benefit, CPU tail failure
and unresolved coupled-hull gate. No geometry is selected.
[R2a evidence](evidence/wakes/r2/README.md) owns candidate definitions, numerical results,
matched diagnostic motion, CPU costs, raw artifact locations and limitations.
No production geometry or shader change is selected. GPU/gameplay adaptation is conditional on
surviving the derivative, straight-behavior and signed-superposition screens.
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

First R0 verification checkpoint (native verification superseded below): corrected Release CPU matrix completed 240 runs / 60 passing
neutrality comparisons; the source coverage and 27-case encounter tables are retained. Astra
reviewed the corrections and found no remaining blocking implementation defect. Straight/chase
production and height/foam paired reels completed with identical PNGs; other reels and the
corrected GPU matrix are pending. Debug/Release builds passed. The final full CTest attempt
passed 581/616; all 35 failures were GPU cases unable to initialize the missing monitor.
The earlier 614/614 full pass predates final probe corrections and is not final verification.
The evidence bundle owns commands, raw checksums, numbers and the provisional screening budget.
R0 stays open until its remaining verification and agreement gates are resolved.

### R0 resumed native qualification — 2026-11-15

The user confirmed the machine had entered power save. Holding the display awake restored
GPU access. Corrected Release matrices completed 48 runs / 12 matching-hash comparisons across
basin low (one/four emitters), outer low and outer HDR (four emitters), all with the supported
physical traffic pool. The primary timing baseline is retained at `1031166`; its exact binary
and source hashes are in the evidence archive. The native fixture results and proposed screening
ceilings are in the [evidence bundle](evidence/wakes/r0/README.md#resumed-native-qualification--2026-11-15).

Completed twenty matched scene/view pairs (forty ten-second movies), including stationary
views, service arrivals/departures and outer/high modes. Inspection found that the harness's
old `SESSION_VIEW` omitted the authored scene: the initial service reels showed the compiled
marina. Switched the harness to `boat_session_view`, added a CPU render-frame scene contract,
and recaptured both service views. Both circle control views remain byte-identical across
this wiring fix; no timing equivalence or performance improvement is inferred from that.
The archive distinguishes timing sources/binary from corrected scene-capture sources/binary.
Production mechanics, renderer recipes and protocol remain unchanged.

The 80-second / 9,600-tick visible scripted demo completed. Debug/Release builds are project
warning-clean. The final full CTest run passed **617/617**, including 36 GPU-labelled cases and
the new scene contract. Astra reviewed the native metrics and narrow scene fix with no blocking
issue. Agent-run capture/sequence inspection and playback are recorded separately from human
input/audio review and acceptance. R0 now awaits criteria/budget agreement, rather than graphics
access. Platform/authority counters and human acceptance remain explicit open items.

### R0 user review and continuation — 2026-11-15

The user reviewed the R0 evidence as looking okay and the numbers as approximately okay,
flagged the outer HDR row as expensive, and explicitly requested continuation with possible
optimization later. This authorizes R1 research locally; it does not accept a new wake recipe,
raise the gameplay graphics ceilings, or authorize a push/PR. Use R0's behavioral tolerances
and CPU/whole-fixture GPU screens provisionally for comparisons. These are research screens,
not a newly agreed final shipping budget. Outer HDR's synchronous enclosing p95 is 8.378 ms;
its GPU p95 is 7.421 ms versus the wake-off interaction control's 2.425 ms. The difference is
not isolated wake cost. Retain this concern in R4 and profile before promotion if needed.

Completed task: user review of the baseline and permission to continue. Next: R1 source policy
and candidate. Browser/slower-GPU measurements, unavailable authority counters and final
mechanics/performance/player acceptance remain open. No additional tests are claimed here.

### R1 design and source policy — 2026-11-15

The user selected “Cosmetic trails first; preserve current physics.” Astra reviewed the
minimal policy before implementation: preserve the four player slots, compiled opt-in traffic
pool and special ferry; replace only missing serviced-vessel visual trails. Authored ordinary
traffic and upstream scenery remain cosmetic. Physical promotion of authored/service hulls
is deferred until it has its own source/authority/resource proposal. This resolves visible
coverage, not physical moving-hull coverage. Working-harbour's D3 player stays independent.

The candidate uses historical service hulls (resolved vessel dimensions) and a shared ribbon
append helper. Eight samples/spans per side cover 5.6 seconds; speed/center translation above
0.3 m/s gate emission. Historical travelling ownership, matching service identity and explicit
dwell-interval rejection prevent joins across docking/stops, absent windows or different visits.
Old trails survive current docking/disappearance. Negative history is rejected. Scenery routes
are validated afloat; no dynamic wetness or arbitrary map-dry filtering is claimed by this API.
Astra's recommendation to keep these limitations explicit is adopted.

The fixed 288-quad traffic frame remains the allocation bound; four per-vessel counters add
16 bytes to its CPU snapshot. No physical record/pool, GPU field, upload format, network or
authority change is introduced. Focused checks exercise historical expiry, one-tick dwells,
resolved beam, deterministic seeks and all four service slots with four ordinary actors.
Matched motion/performance evidence and full verification follow below.

### R1 measurement and retention decision — 2026-11-15

Completed candidate: cosmetic historical service trails, unchanged physical sources. Twelve
alternating native Release GPU runs retain 60 measured simulated seconds per run, plus matched
chase/overview and late-arrival/expiry reels. Three hundred before/after physical diagnostic
frames and the 27-case hull-encounter CSV match R0 exactly. All 351 post-maturation moving
observations in the shipped scene have attributed trails; 16 early observations have no full
eligible span yet. The initial span delay and lack of dynamic scenery wetness are explicit limits.

Low fixture enclosing p95 increases .593 ms; HDR .387 ms, with broad HDR repetition spreads
and >10% HDR submission/p99 regressions. Astra reviewed design, code and final measurements
and found no blocking correctness/interpretation issue. The supervisor retains the cosmetic
candidate for local review. Performance acceptance is **open**: investigate HDR variability
and tails before R4 promotion; the user allowed optimization later. This fixture does not
qualify the existing graphics ceilings or establish isolated per-emitter/CPU preparation cost.

Completed task: R1 implementation, source policy, matched media/performance and review. Next:
R2 propagation/overlap design with Astra before geometry changes.
No physical serviced-vessel promotion, lifetime change, push or PR is authorized by this step.
Browser/slower-GPU, missing authority counters and final human appearance/handling remain open.

R1 final verification: Debug/Release builds were project-warning-clean. After correcting the
capacity assertion, the final `ctest --preset full` passed **617/617**, including all 36 GPU
cases, in 102.19 seconds. The 80-second / 9,600-tick visible scripted service demo completed.
All three labeled before/after movies decode fully with 100 frames each; temporal CPU-field
and GPU-scene samples were inspected. Changed local link targets and `git diff --check` pass.
The [R1 bundle](evidence/wakes/r1/README.md) retains raw timing/capture provenance and limits.
This is automated/agent verification, not human appearance acceptance or a shipping-budget pass.

### R2a design review — 2026-11-15

The user requested continued wake research. Astra recommended an isolated CPU screen before
production integration: freeze R1 and the physical recipe; compare literal 30-tick spans in
a private 64-record/emitter pool, an explicitly phase/fade-matched mature-straight control,
plus equal-density open and 30–45-degree-limited capsule
wavelets over the unchanged 60-tick records. All retain 16 seconds and the same source strength.
No amplitude retuning, protocol growth, shader switch or game option is authorized by the screen.

The capsule uses a C1 smooth excess beyond half the historical span, a clamped historical
tangent coordinate and radial propagation; every spatial, angular-mask and envelope derivative
contributes to returned gradient/vertical motion. The open cap deliberately exposes possible
fore/aft faces; the angular variant screens whether a restricted cap avoids them. The literal
short-span control shifts effective phase by 0.125 seconds and its fade offset: those are
confounds to measure, not geometry-only improvements. A matching production-baseline contract
checks records and all returned query components through saturation, dry gaps and reset.

R2a refines whole-field crest coverage at common source/radial spacings .1/.05/.025 m, retains
inner-focus exclusion only for the offset-front metric, and reports 10/25/50-percent threshold
sensitivity, longest gaps, expanded-front length, ridge displacement and local-pair samples.
These diagnostics do not substitute for straight/derivative/encounter criteria. Rounded caps
cannot reuse the production stamp rectangles if they advance: enclosing support bounds,
central exclusion, VS/FS/stamp/cache/fallback all require a later reviewed integration.

Supervisor decision: implement the private field screen and retain rejected hypotheses with
matched physical-height/foam motion and CPU cost. Coupled hull/GPU/frame measurements are
unavailable until a candidate survives and receives an integration design; do not invent them.
C adjacency joins remain deferred until demonstrated useful gaps survive B's mechanical screens.
Existing R1/R0 tail concerns and the other open loops remain deferred to their current owners.

### R2a rejection checkpoint — 2026-11-15

Completed private CPU experiment with production-baseline parity, literal short spans (A0),
mature-straight phase/fade-aligned short spans (A1), open capsule (B-open) and angular capsule
(B45). No production source/evaluator, shader, lifetime, capacity, protocol or R1 cosmetic-policy
change is selected. C joins and R3 lifetime work remain deferred.

A0 changes phase by 15 ticks and has approximately 22% mature straight RMS error. B-open/B45
fill the measured outer gaps but have roughly 96–132% straight RMS error at 1–8 seconds. A1
recovers interior straight agreement (~2e-7 normalized RMS), but finite-train endpoint error
is about 18.4%, and the sampled tight-turn inner-focus peak changes 2.252→4.159 m at 4 s.
Its representative gap improvement is only .025–.05 m. The supervisor rejects these alternatives
under the provisional straight criteria, retaining a tested control rather than retuning strength
or hiding the changes with foam. Astra reviewed design, code and final evidence and supports
that decision. These findings do not reject every possible shorter-span or rounded-front model.

Verification: Debug full build and Release research target are warning-clean; final full
CTest passes **618/618**, including 36 GPU-labelled cases, in 112.14 seconds. Production GPU
checks exercise the unchanged recipe; they are not candidate GPU qualification. Release
baseline query parity is 5.3312e-7, and derivative/signed/history contracts pass. Retained
400 turn rows, 100 concentration grids, 25 straight rows and full fixed-probe arrivals;
288 separate CPU runs retain saturated occupancy, plus 48 paired ten-second CPU diagnostic
movies. Temporal samples were inspected and four movies completed visible paced playback.
No coupled hull, production-renderer candidate, browser/slower-GPU, input/audio or player
acceptance is claimed. CPU costs are private harness costs; unavailable GPU/frame/network
counters are explicitly unavailable. The source/payload/camera limits belong to the bundle.

Next R2b design question from Astra: can longitudinal support spread with historical front
divergence while transporting existing finite-source weighting, preserving collinear interior
and endpoint envelopes, and bounding inner-focus concentration without added cap strength
or coupling unrelated fronts? Derive this for a pair of historical spans before implementation.
This is not C authorization or permission to spend additional performance/memory/network budget.
The ±6 m inner-corridor ambiguity, finite grid/color clipping, R0/R1 HDR tails, human review
and wider platform/authority-counter gaps remain explicit limits.

### R2b historical weighting/divergence design — 2026-11-15

Completed the requested design investigation with Astra's pre-design and final review.
[The proposal](WAKE_R2B_DESIGN.md) derives the true curved normal-ray fold, including
emission-age tilt, and a narrower globally invertible Cartesian outward dilation. Preserve
each original finite source and its endpoints/phase/gates; do not normalize neighboring weights.
Freeze predecessor-derived divergence in the successor only, retaining startup asymmetry.
No existing record can acquire future geometry without abandoning historical immutability.

Supervisor decision: retain one proposed private signed-density (`p=1`) experiment, baseline
cadence/lifetime/strength, provisional epsilon .25 and a declared hard turn-domain gate. This
transports the original integrated contribution and deliberately attenuates widened outer
fronts. The normal coordinate stays in the historical frame, but shear changes physical crest
orientation and adds tangential drift. Inner-side identity is per-record, not a bound on arbitrary
summed-field focus. Exact derivatives, support growth, metadata/restore constraints and
baseline-relative rejection gates are documented. No C join or production implementation is selected.

Algebra evidence uses synthetic signed reference functions, not the production wave profile:
3,780 scalar cases, 420 Cartesian and 216 phase-map parameter configurations; exact sampled
collinear/inner identity and signed cancellation; determinant discrepancy 2.42e-10 and phase
chain discrepancy 6.35e-10. Three-step scalar FD residuals decrease to 9.14e-6; fine moment
quadrature residual is 6.54e-7. The regional counterexample raises a baseline bound of 1.2
to 1.21569 even though individual inner-side fields are unchanged. Capture/lifecycle behavior
and actual geometry benefits/costs remain untested. Astra reviewed the code/report and confirmed
the equation/invariant reasoning after correcting the initial propagation-normal wording.

Debug build is project-warning-clean; the full CTest preset passed **618/618**, including
36 GPU-labelled cases, in 101.89 s. Those tests cover unchanged production; no candidate GPU,
frame-cost or gameplay acceptance is inferred. No audiovisual behavior changed.

Next: define a private immutable descriptor/capture ledger, then test only the proposed
`p=1` transform against straight endpoints, source-off coverage, full-domain concentration,
near-threshold captures, frozen history, signed crossings and existing resource screens.
Metadata/quantization, shader/compatibility/authority costs, R0/R1 HDR tails, human and wider
platform review remain open. R3 waits for a selected geometry. Nothing is pushed or promoted.


### R2b private capture ledger — 2026-11-17

Completed the immutable descriptor/capture infrastructure in the existing research worktree.
[Capture evidence](evidence/wakes/r2b/capture/README.md) owns lifecycle details, source/review
findings, exact resource sizes, overhead data and provenance. The private wrapper retains raw
baseline records and birth timing, including dry/slow carry-in spans: those freeze zero curvature
and are ineligible to seed a pair. Explicit reset/identity changes break source epochs. Frozen
successor descriptors and copied witnesses survive predecessor expiry and slot reuse. Versioned,
checksum-protected same-build snapshots validate before destination mutation; this is no wire
or portable-save proposal. No new field evaluator or production behavior is implemented.

Astra design/code review findings on baseline timing, overflow, live witness identity and unknown
provenance were addressed; final inspection found no blocking ledger issue. The supervisor retains
this infrastructure only. Full Debug build and Release target builds passed without project
warnings; the desktop-enabled full CTest preset passed 619/619 (36 GPU) in 104.55 s. Standalone
Release and ASan/UBSan capture contracts passed. Tests compare five 80-second four-source histories
exactly with baseline and replay 55 seconds after restoration, plus explicit lifecycle/threshold
cases. These are automated checks, not player acceptance or a transform qualification.

Twelve alternating Release runs retain 60 measured seconds each after 20 seconds warmup.
All 21,600 paired frames agree on births, occupancy and printed signed-height sums. Four-source
enclosing p50/p95/p99 is approximately .041/.044/.050 ms for baseline and ledger; source tracking
is near the .001 ms timer quantum, so no zero-overhead claim is made. Added audit ledger storage
is 13,568 bytes, including full predecessor witnesses; production allocation is unchanged.
No new motion capture is claimed for this capture-only infrastructure step; R2b's eventual field
checkpoint still needs matched diagnostic movies and its mechanical/performance review bundle.

Next: private p=1 field evaluator and baseline-relative straight/endpoints, turn coverage,
derivative, full-domain concentration, signed-crossing and saturated-cost screens. GPU, protocol,
quantization and authority integration remain unimplemented; R0/R1 HDR tails, human and wider
platform reviews stay open. R3 still waits for a selected geometry. Work remains local.


### R2b private p=1 field screen — 2026-11-17

Completed the proposed single fixed-parameter outward-dilation CPU evaluator, separate from
all production targets. [Field evidence](evidence/wakes/r2b/field/README.md) owns detailed
mechanics, matched motion, all measurements/provenance and limitations. Baseline cadence,
strength and lifetime are unchanged; no exponent/height tuning or geometry fallback is added.

Sampled straight height/gradient/velocity and 1,921 arrival samples match exactly, including
finite-train tails; derivative and mixed signed-superposition contracts pass. Finest-grid
outer 25%-crest gap length at radius 12 m / 4 s improves 2.30→.90 m, at 8 s 3.825→2.825 m;
radius 50 m / 8 s improves 2.925→0 m. Full-support grids find one flagged increase: figure-eight
trough magnitude .582894→.586607 m (+.637%) at four seconds after stop. Finite-grid extrema
and better coverage do not establish preserved encounters or a universal concentration bound.

The final 72-run Release matrix uses three alternating paired runs, 20 s preroll and 60 s
measured saturation per run. Dense-four enclosing p95 is .060→.086 ms circle, .061→.089 ms
reversal and .026→.065 ms straight. Relative regressions exceed investigation screens. Median
p99 fits the provisional .15 ms ceiling, but an individual reversal run reaches .257 ms p99:
**the performance gate is open**. Source audit finds tiny nonzero curvature in rotated straight
float histories; profiling is needed before attributing cost or changing source behavior.

Twelve paired diagnostic field movies are retained; all decode, five complete visible paced
playback, and temporal sheets are inspected. Straight/crossing supply 400 identical PNG pairs.
These are CPU field views, not production chase or coupled hull driving. The baseline timing
wrapper's early guard was corrected and final timings rerun; pre/post guard field tables and
2,400 PNGs agree. Discarded timing folders are explicitly superseded in the evidence.

Astra reviewed design, code and evidence and verified screen/raw CPU hashes. Findings on
seams, endpoints, overlap, bounds/probes, trough growth and hidden p99 outlier were addressed.
Supervisor decision: retain only the private prototype for further investigation; keep the
production baseline and do not select geometry. Full Debug build/Release targets pass without
project warnings; final full CTest passes 620/620 (36 GPU, 102.82 s), plus Release/ASan/UBSan
private contracts. No GPU transform, browser/slower-GPU, player or human handling acceptance.

Next: profile CPU paths and tails, then private coupled-hull encounters targeting changed outer
fronts and figure-eight concentration. R3 still waits for geometry selection. Production resource,
wire, GPU and authority work, R0/R1 HDR issues and wider review remain open. Work stays local.


### R2b CPU profiling, coupled hulls and basin trial — 2026-11-19

Implemented the requested experimental CPU basin trial in the existing worktree. Run
`./build-release/wake_transport_basin`: W/S throttle, A/D steer, R reset, B baseline/spread
comparison with reset, Esc quit. [Playing](PLAYING.md#experimental-wake-research-basin) and
[trial evidence](evidence/wakes/r2b/basin/README.md) own commands, measurements and limits.
Production `wake_velocity` keeps its wake recipe; no GPU transform or network option is added.

Instruments profiles guided exact support rejection and avoided redundant transform work.
All six field tables remain byte-identical. Eighteen alternating same-geometry pre/post runs
reduce dense-four circle/reversal p95 from .083/.084 to .073/.073 ms. A separate 72-run matrix
has no individual provisional CPU limit exceedance, but relative overhead and rare tails remain
open; full-frame CPU and authority budgets are not qualified.

The optional borrowed CPU object sampler feeds production hull physics and CPU displacement/
foam with one surface/clock; the GPU explicitly refuses it. Null-path and airborne whole-boat
parity, borrowed-pointer resets and deterministic replay are checked. The calm reference keeps
arcade recovery. Live stepping preserves advance → physics → stern-source ordering.

The 252 frozen-source pairs all settle after field expiry, with 27 exact straight controls.
**Curved handling preservation fails:** 167 pairs exceed a provisional ±10% metric screen and
35 change landing count; none changes capsize outcome or final state. The radius-50 eight-second
gap at 24 m/s changes launch 2.110→6.112 m/s. At-time supports match the previously flagged
figure-eight trough at four seconds after stop. These are targeted probes, not ordinary-play
rates or a universal safety/concentration bound. Multi-source coupled response remains open.

Astra reviewed design/code/outcomes and supports a clearly experimental CPU trial after checks;
the supervisor retains production baseline, with no geometry selection. Four matched CPU-view
movies cover self-wake feedback, gap12, large-launch gap50 and the trough. Capture inspection,
playback, automated tests and human acceptance remain distinct in the evidence. Full Debug build,
Release targets and fully instrumented ASan/UBSan check pass. Full CTest on 2026-11-19 passes
622/622 (36 GPU, 108.35 s); final help/label-only updates passed three focused checks including the CI lane check.

Next: user comparison and review of the changed curved encounter contract. Do not infer acceptance
from launching the trial. R3 remains dependent on a selected geometry; full-frame/relative CPU,
HDR, GPU, wider platforms, networking and multi-source checks retain their owners. No push or PR.
