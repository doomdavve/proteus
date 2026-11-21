# R1 cosmetic serviced-vessel coverage

Owner: [wake research plan](../../../WAKE_RESEARCH_PLAN.md). Local research candidate,
following the user's cosmetic-first decision. Physical wakes and network contracts are unchanged.

## Policy and implementation

Players retain their existing physical/dry-stern recipes; original compiled scenery remains
opt-in physical, the special ferry remains independent, and ordinary authored/upstream traffic
remains cosmetic. Serviced residents/visitors now supply cosmetic trails once from historical
service hulls while their route actors stay hidden. Static/moored hulls emit nothing.
Authored/service physical-wave promotion is explicitly deferred to a separate resource proposal.

Eight segments per side span 5.6 seconds. Endpoints require travelling ownership, speed above
0.3 m/s, matching service identity and center translation above 0.21 m per 0.7-second span.
Every intersecting dwell is rejected, including one-tick stops. Negative history is rejected.
Historical trails persist after docking/disappearance and expire independently of current ownership.
Stateless evaluation supports seeks/restarts without stale trail state. The actual service vessel's
resolved dimensions control stern geometry, rather than its path actor's recipe.

The location validator checks these kinematic routes afloat. No dynamic wetness or arbitrary
map-dry filtering is claimed: the trail API has no map/wetness input. No surface/derivative or
hull-encounter retuning is introduced. Shared ribbon construction rejects nonfinite or degenerate
segments; ordinary zero-area quads may therefore be omitted, without adding surface displacement.

## Resources and coverage

The 288-quad traffic snapshot capacity is unchanged. Per-vessel counters add 16 CPU snapshot
bytes. At most four serviced hulls contribute 16 quads each; each replaces a hidden ordinary
actor's generator. No physical pool, shader record, field allocation, record upload format,
authority work or replicated birth is added. Driver allocations and isolated CPU render
preparation remain unavailable; zero physical-source growth is not a claim of zero render cost.

The shipped `marina-berth-demo` audit samples 30 seconds at 10 Hz. All 367 moving observations
were missing in R0; 351 now have attributed service trails. The remaining 16 are all within
the first approximately 0.8 seconds after initial departure: a full eligible 0.7-second span
has not yet formed (including the speed threshold). This is a declared source-maturation limit,
not an unexplained missing class. Stationary or slow hulls intentionally have no new emission.

## Matched motion and performance method

Artifacts are local under `/private/tmp/proteus-wake-research/build-release/wake-r1/`; the
manifest/archive retain exact commands, raw CSV/JSON, binaries, fixture source and checksums.
The before binary is the corrected R0 scene-capture harness at `f35111f`. Candidate sources
are identified by SHA-256 because evidence was collected before the R1 commit.

The real shipped scene's 7–17-second chase/overview reels compare before and after at identical
120 Hz ticks, 320x200/no-AA rendering. Each 100-frame source sequence is at 10 Hz. Diagnostic
neutrality is checked on both revisions. All 200 before/after physical-height/object-foam
PNGs agree exactly, while all 200 regular scene PNGs change. Temporal contact sheets at
7/11/16 seconds show trails behind moving serviced hulls; agent inspection is separate from
human appearance/handling acceptance. The labeled `service-*-before-after.mp4` files pair
R0 on the left with R1 on the right.

The uncapped performance fixture is deliberately separate: four service hulls and four ordinary
actors, made by duplicating the example identities and extending its path durations/dwells/start
ticks fourfold. Geometry overlaps deliberately to stress simultaneous work; this is a valid
scene definition, not a location-clearance-qualified authored marina. Not every service is
moving at every instant, and source strength is lower at these slower speeds. Four continuously
emitting player sources keep their shared pool saturated. Three alternating before/after repetitions at low
and HDR settings each retain 20 seconds preroll and 60 simulated seconds of measurement.
Use contemporary comparisons, not subtraction from the differently configured R0 table.
GPU time covers the scene; enclosing time includes synchronous completion and synthetic
CPU queries. Submission includes driver waits; it is not isolated CPU render preparation.

## Measured results and review

Native Release on the R0 target (M1 Max/macOS), 2026-11-15. Milliseconds, median of three
per-run percentiles; [all component statistics and deltas](timings.csv) retain p50/p95/p99.

| Fixture | GPU before p50/p95/p99 | GPU after p50/p95/p99 | Enclosing before p50/p95/p99 | Enclosing after p50/p95/p99 |
| --- | --- | --- | --- | --- |
| Outer low, four serviced + four ordinary + four player emitters | 6.115 / 7.421 / 8.245 | 6.419 / 7.775 / 8.802 | 7.769 / 9.203 / 10.033 | 8.321 / 9.796 / 10.824 |
| Same scene, 1280x800 HDR | 4.503 / 5.044 / 6.005 | 4.530 / 5.110 / 6.951 | 5.953 / 6.620 / 7.649 | 6.249 / 7.007 / 9.031 |

Low GPU p95 increases .354 ms (+4.8%); enclosing p95 increases .593 ms (+6.4%).
HDR median GPU p95 increases .066 ms (+1.3%) and enclosing p95 .387 ms (+5.8%), but HDR
GPU p95 spans **5.030–6.279 ms before / 5.011–6.732 ms after**, and enclosing p95 spans
**6.592–7.836 / 6.853–8.584 ms**. Low GPU p95 spans 7.201–7.463 / 7.610–7.824 ms.
Do not interpret the lower HDR result than low resolution as a resolution scaling improvement:
settings differ and the cause of the large run variation is not established.

HDR submission p95/p99 (+11.3/+11.6%), GPU p99 (+15.8%), enclosing p99 (+18.1%) and wait p99
(+22.5%) trigger investigation flags. Submission includes extra historical pose/ribbon work
and driver waits; this matrix does not isolate their individual costs or explain the variation.
Physical-query p95 is .042→.040 ms low and .036→.037 ms HDR, not an optimization claim.
All twelve runs retained a saturated player pool and identical before/after record/probe hashes.
The raw report contains memory, stamp/upload counters and binary hashes; no physical/GPU-field
resource expansion is introduced. Unsupported authority/network throughput and allocator counters
remain unavailable, rather than measured zero.

Astra's design and candidate-code reviews found no blocking interval/resource defect. It confirmed
the finite-history policy, dwell rejection and capacity bound, and retained dynamic wetness,
initial maturation and untested separate-visit identity regression as limitations. The supervisor
retains R1 as a locally reviewable cosmetic candidate, **without performance acceptance**.
The user explicitly allowed optimization later: tail profiling/optimization remains open before
R4 promotion, alongside the original R0 outer HDR concern. These measurements do not establish
a pass against the differently configured gameplay graphics ceilings.

The additional 19–29-second overview reel shows visitor disappearance and its trail clearing,
plus resident arrival with retained old history. Contact sheets at 19/23/28 seconds were
inspected. Automated expiry checks cover complete clearing after the 5.6-second horizon.
Full verification and visible playback are recorded below. No browser/slower-GPU,
hands-on input, audio listening or human appearance/handling acceptance is inferred.

### Verification checkpoints

Debug and Release builds completed without project warnings. The initial full preset passed
616/617, including all 36 GPU-labelled cases; the new maximum-load test incorrectly assumed
every ordinary actor contributed 16 quads even when its historical spans were degenerate.
Corrected the assertion to compare the ordinary actors' evaluated count plus all four service
counts; the production candidate is unchanged by this test correction. The focused service
validator then passed. The completed full rerun and visible demo are recorded below.

Astra reviewed the completed performance report and matching physical hashes and found no
blocking interpretation issue. It recommended retaining a local checkpoint, with HDR variability
and >10% tails/submission costs investigated before R4. The combined fixture is not a claim
of sustained maximum service emission or isolated per-emitter cost. R2 design may proceed;
performance and appearance acceptance remain independent open gates.

Final native Debug `ctest --preset full`: **617/617 passed**, all 36 GPU-labelled cases,
102.19 seconds. The 80-second / 9,600-tick visible scripted service rig completed all 3,600
measured frames (source-off player control; no saturation claim for that playback). All three
paired movies have 100 frames and decode completely; 300 before/after physical diagnostic
PNG pairs match. The 27-case hull-encounter CSV is byte-identical to R0. Changed local link
targets and `git diff --check` pass. No input/audio listening, browser, slower-GPU or human
appearance/handling acceptance is claimed.

[Manifest](manifest.json) records retained files/checksums and the local review archive. Large
artifacts remain outside Git. The archive includes exact executables, raw timing CSV/JSON,
fixture and comparison script, matched movies/captures, source diff and verification logs.
R2 design is next; HDR tail/variation profiling and performance acceptance remain open before R4.
