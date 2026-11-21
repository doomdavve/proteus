# R2b p=1 private field screen — 2026-11-17

Owner: [wake research plan](../../../../WAKE_RESEARCH_PLAN.md).
Design: [historical transport](../../../../WAKE_R2B_DESIGN.md).
Decision: retain the prototype for further private investigation; **no geometry selection or
performance acceptance**. CPU profiling/tail investigation and coupled-hull probes are next.
Production source policy, geometry, shaders, capacities, protocol and 16-second lifetime remain
unchanged. R3 still depends on a selected geometry.

## Evaluator and scope

`wake_transport` is never linked to a game, renderer, authority or network target. It uses the
[capture ledger](../capture/README.md), original 60-tick cadence, strength and finite windows,
fixed epsilon=.25 and the single proposed signed-density exponent p=1. Outer support widens
and its contribution attenuates by 1/D. A direct baseline branch preserves K=0 and each record's
inner half-plane exactly. Gradient includes both shear and gain derivatives; vertical velocity
uses the original full derivative divided by D. No time-dependent descriptor, amplitude retune,
exponent sweep, connected strip, added cap or global normalization is introduced.

A local-coordinate entry point in the private R2a evaluator avoids reconstructing a world point
from material coordinates. Float frame norm corrections apply to gradient recovery and support
bounds. Concentration bounds include expanded longitudinal support plus transverse phase support,
all retained records and an outward margin, independently of the camera. Foam divides by D as
an explicitly cosmetic policy; it is not continuity or transport evidence.

## Straight, derivative and superposition screens

The [straight table](straight.csv) samples 600 along-path stations and 90 normal offsets at
1/4/8/12/15 seconds after emission stops. The source-coordinate range 8.5–25.5 s covers the
retained train's actual first/last finite-support tails, including endpoints; every sampled height,
gradient and velocity difference is zero. All 1,921 fixed-probe arrival samples match exactly,
so the measured peak-arrival shift is zero ticks. Late-life near-zero reference scales remain
in the data rather than becoming whole-front acceptance claims. The contract separately checks
finite straight histories with unequal speed, length and filtered strength.

[Derivative sweeps](derivatives.csv) use rotated and translated records, transformed support
seams u=±D(v)L, the dilation seam, profile support and birth/rise/fade/expiry. Spatial finite
differences use the actual represented p±h separation. The original nominal-2h check at translated
coordinates failed from float probe-step quantization and was corrected before retaining evidence.
Spatial errors at requested .01/.002/.0005 m steps are .000212/.0000882/.0004505; the finest
step hits a float precision floor, not monotonic convergence. Temporal errors are .09063/.01870/
.004725 m/s. Finest errors meet the provisional .003 spatial/.02 velocity screens. Sampled
inner/zero-K identity and identical-descriptor signed cancellation residuals are zero.

[Mixed crossings](crossings.csv) use two synthetic signed, nonzero-curvature contributions in
independent emitters and nonadjacent slots of one emitter. Exact sums match isolated-piece sums
for height/gradient/velocity; overlap counts distinguish actual overlap from later separated
trains. Negative synthetic source amplitude tests linear superposition, not production emission
or a valid captured snapshot. Real curved figure-eight/reversal histories are scanned separately.
The four-straight-source crossing movie is an unchanged rendered regression; nominally straight
captured chords can contain tiny nonzero K (see the audit below).
No hull encounter, impulse, capsize or GPU parity is established by these field contracts.

## Turning coverage and concentration

[Turning probes](turns.csv) compare truncated quarter-turn sources (complete half-second spans)
at radii 12/50 m with an unchanged straight reference. Shared source/radial resolutions are
.1/.05/.025 m, with a primary ±2 m radial search and a ±6 m sensitivity search only at .05 m.
Inner stations are excluded if any part of their corridor could reach the opposite radial ray;
empty/zero-eligible rows are not success. The score is historical source-path length below a
fraction of the baseline straight crest, not physical crest length. Ridge displacement and
nearest-pair attribution at the expected ridge remain separate from full-field maxima.

Finest .025 m grid, outer side, primary ±2 m corridor:

| Radius / time after stop | Eligible source length | Below 25% crest: baseline → spread |
| --- | ---: | ---: |
| 12 m / 4 s | 7.5 m | 2.300 → .900 m |
| 12 m / 8 s | 7.5 m | 3.825 → 2.825 m |
| 50 m / 8 s | 67.5 m | 2.925 → 0 m |

This is a useful coverage benefit, not proof of correct encounters or a seamless front.
Widening deliberately changes outer local fields and their crest orientation; startup remains
asymmetric. The hard source-turn gate remains a model limitation even though each frozen record
is continuous over its own lifetime.

[Concentration data](concentration.csv) scan the complete union support for both modes:
truncated quarter turns at 1/.5 m grids; 25-second full tight/wide circles, figure-eight,
reversal and four-source crossing histories at 1 m. Samples at 1/4/8/12/15 seconds after stop
record surviving occupancy and bounds. These are finite-grid extrema, not exact worst cases.
No positive-height, gradient-magnitude or vertical-speed maximum increased in these grids.
**Figure-eight negative trough magnitude increases .582894→.586607 m (+.637%) at 4 s**.
This flagged redistribution must be targeted by coupled-hull tests. Unchanged extrema elsewhere
do not establish local-field identity or a universal concentration bound.

## CPU costs and resources

M1 Max, macOS 26.6.2 arm64, Apple clang 21.0.0, native Release, 2026-11-17. The retained matrix
has **72 runs**: straight/tight circle/reversal; one/four emitters; dense/dispersed probes; three
contemporary alternating baseline/spread pairs. Pair order alternates between repetitions.
Each run has 20 simulated seconds warmup and 60 seconds / 3,600 measured frames, two source
ticks and 30 synthetic queries per frame. Continuous pools are saturated throughout. No tests,
encoding or playback ran concurrently with retained timings; this was not an exclusive-machine
measurement. Both modes share the capture ledger, source work and query fixtures.

Milliseconds, median of three per-run percentiles, dense four-emitter enclosing workload:

| Path | Baseline p50/p95/p99 | Spread p50/p95/p99 | p95 delta |
| --- | --- | --- | ---: |
| Straight | .022/.026/.029 | .060/.065/.072 | +150% |
| Tight circle | .055/.060/.069 | .080/.086/.099 | +43.3% |
| Reversal | .057/.061/.083 | .081/.089/.124 | +45.9% |

[All components and deltas](timings.csv) include repetition ranges. Source timings near .001 ms
are too coarsely resolved for useful percentage claims. Query and enclosing work include synthetic
probe construction/accumulation; these are not pure evaluator cycles or authority-tick timings.
A separate straight-source audit at 25 s found 57 nonzero descriptors among four emitters,
max |K|=3.234e-14, versus all-zero K for one emitter. Microscopic captured curvature can enter
the full outer path; this is an investigation lead, not a measured attribution of the cost.
No curvature deadband or altered geometry was introduced to hide it.

**Performance acceptance remains open.** Median enclosing p95/p99 values fit the provisional
.10/.15 ms fixture screens, but individual four-dense reversal repeat 0 reaches **.257 ms p99**,
exceeding .15 ms. Its p95 is .093 ms. [Report](report.json) explicitly lists this per-run failure;
median summaries do not waive it. Relative regressions also exceed the 10% investigation flag.
Profile these paths and tails before any promotion; a low absolute median does not establish
production affordability or explain the outlier.

Both modes allocate the same private capture workspace (24,008 bytes plus its mode tag/padding),
including 128 descriptor sidecars and the R2a field's unused short-mode slots. Logical baseline
record payload remains 5,120 bytes; source birth density stays two per second per wet emitter.
The evaluator adds no persistent pool, per-frame allocation or replicated record. The ledger's
13,568-byte audit metadata increment is not a shipping allocation. GPU/frame/render-preparation,
field upload, driver memory, network and authority costs are unavailable for this unintegrated
prototype. No production resource or compatibility increase is selected.

## Motion, verification and provenance

**Twelve paired ten-second movies** cover straight, tight/wide turns, figure-eight, reversal
and four-source crossing, in fixed wide/source-centered detail views. Each movie has 100 frames
at 10 Hz: source emission stops at 25 s and frames cover 25–34.9 s. Each revision's left panel
shows signed physical height (red positive/blue negative, clipped at ±2.4 m), right panel foam;
paired baseline is left and spread right. Views sample a 96×96 grid across 160 m (wide) or
80 m (detail), with integer nearest-neighbor enlargement. Their framing does not cover every
expanded-support bound; use the numeric full-domain scans for concentration, not these movies.
They are diagnostic field movies, not production-renderer, chase-camera or coupled-hull footage.

All movies decode fully. Straight and crossing pairs have **400 byte-identical PNG pairs**.
Circle, figure-eight and wide-turn temporal contact sheets were inspected. Five movies completed
visible paced playback (straight/tight-circle/figure-eight/crossing/reversal). Playback completion
and sampled frame inspection are distinguished from human handling or appearance acceptance.

During timing audit, the new local-coordinate wrapper was found to compute baseline coordinates
before its empty/expired guard, unlike the original baseline. That created an unfair comparison.
Restored the original early guard, added a zero-K direct return and skipped dead records in the
candidate query, then reran the complete matrix. Earlier `cpu`, `retained-cpu`, `cpu-final` and
`report*` folders are superseded by **cpu-retained/report-retained**. All six numerical screen
tables and all 2,400 PNGs match before/after guard cleanup. Retained screen/media were regenerated
with the final binary; earlier sources/binary are archived locally as history. No optimization
claim is based on the discarded timing comparisons.

Astra reviewed design, code, corrected probes and evidence, checked all six screen hashes and
all 72 raw CPU hashes, and recommended retaining the prototype only for investigation. Addressed
findings include full support bounds, transformed seams, actual finite-train endpoints, mixed-K
signed overlap, opposite-ray exclusion, CSV schema, unsigned probe coordinates, the negative
trough change and per-run p99 failure. Astra advice is not player acceptance. The supervisor
retains the baseline in production and the prototype only for the next private screen.

Debug full build and Release research-target builds passed without project compiler warnings.
Final desktop-enabled `ctest --preset full` passed **620/620**, including **36 GPU-labelled**
cases, in **102.82 s**. The private contract also passed Release and ASan/UBSan. An earlier full
run was interrupted before the guard correction and is not qualification evidence. Existing GPU
tests cover unchanged production behavior, not GPU execution of this transform. No new human
input/audio, browser/slower-GPU or gameplay acceptance is claimed. R0/R1 HDR concerns remain open.

[Manifest](manifest.json) retains commands, source/report/media/raw hashes and local artifact paths.
Commands are in [Testing](../../../../TESTING.md#private-outward-transport-screen-r2b).
Artifacts live under `/private/tmp/proteus-wake-research/build-release/wake-r2b/field/`.
Next: CPU profiling/tail investigation, then private coupled-hull encounters with targeted outer
fronts and the figure-eight trough change. Geometry selection, GPU/protocol integration and R3
remain open; no push or PR is authorized by this checkpoint.
