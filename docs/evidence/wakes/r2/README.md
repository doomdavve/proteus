# R2a turning-geometry field screen

Owner: [wake research plan](../../../WAKE_RESEARCH_PLAN.md). Completed local rejection
checkpoint, 2026-11-15. **No candidate selected.** Production geometry, shaders, lifetime,
physical source policy, capacities and protocol are unchanged from R1 (`69bd70b`).

## Decision

Astra reviewed the design before implementation and the evaluator, numerical results and
provenance afterward. The supervisor rejects all four tested alternatives under the provisional
unchanged-straight screens (2% normalized RMS, 5% maximum pointwise error, two-tick crest arrival).
Correct derivatives and improved geometric coverage do not offset changed mechanics.
Retain the baseline. R2 remains open; R3 lifetime work still depends on a geometry selection.

| Candidate | Straight height RMS at 4 s | Maximum pointwise error / baseline peak | Fixed-probe peak arrival shift | Decision |
| --- | ---: | ---: | ---: | --- |
| A0 literal .25 s spans | 22.02% | 33.05% | -15 ticks | Reject: phase/fade change, little coverage benefit, more work |
| B-open capsule | 124.61% | 133.64% | +40 ticks | Reject: excessive straight reinforcement and added faces |
| B45 angular capsule | 108.72% | 133.64% | +29 ticks | Reject: angular taper does not preserve the straight train |
| A1 phase/fade-aligned short control | 1.84% | 18.42% | 0 ticks | Reject: endpoint change and altered inner focus; not an interior-straight failure |

The full [straight table](straight.csv) covers 1/4/8/12/15 seconds after stopping. RMS is
`sqrt(sum((candidate-reference)^2)/sum(reference^2))`, with recorded nonzero height, gradient
and velocity scales; maximum error divides by the baseline field's peak absolute height.
The finite straight train is sampled at 600 along-path stations and 90 normal offsets per age.
The 15-second baseline RMS height is only .00449 m: retain it as a near-expiry diagnostic,
not the primary rejection. [Arrival summaries](arrivals.csv) select the maximum positive
height at a fixed `(-30,10)` point over ticks 3000–4920; full signed traces remain archived.
This is one declared crest-arrival probe, not a claim about every hull approach.

A1's interior RMS is about 2e-7 and interior peak error about 4e-7 at 4 seconds. Its finite-train
endpoint mismatch still fails the screen; it is not evidence that all shorter-span designs fail.
The additional inner-focus result is also material: radius 12 m / four seconds after stopping,
on the full-domain .5 m grid, baseline→A1 peak height is **2.252→4.159 m**, maximum gradient
**1.706→2.875**, and maximum vertical surface speed **4.202→6.917 m/s**. These are sampled
field extrema, not contact forces, launch velocities or hull outcomes.

## Candidate definitions and invariants

The private `wake_geometry` target is never linked into a game, renderer, authority or network
target. All sources use the existing filtered 1.2 m strength setting and 16-second lifetime.
B variants use the exact baseline cadence/records. No amplitudes were retuned.

- **Baseline:** private copy of the production source/evaluator, checked against production
  records and height/gradient/vertical velocity/foam through saturation, dry gaps and explicit
  tracker resets. Release maximum query-component discrepancy is **5.3312e-7**; records match
  exactly. Small query differences reflect arithmetic ordering.
- **A0:** literal 30-tick cadence, private 64 slots/emitter. Production uses 60 ticks and 32
  slots/emitter. A0 changes effective phase by .125 seconds and the local fade offset, as well
  as density/windows/birth gates; it is explicitly not a geometry-only comparison.
- **A1:** A0 with local age reduced by .125 seconds and fade-start reduced by .25 seconds.
  This aligns the mature 15 m/s straight phase/fade convention. Birth gates, windows, emission
  timing and source-strength averaging remain different; it is not a fully matched replacement.
- **B-open/B45:** historical tangent coordinate `u`, normal `v`, length `L`, inverse speed `k`.
  Set `a=L/2`, `delta=min(.1L,.25)`, and smooth excess `e=0` for `x=abs(u)-a<=0`,
  `e=x*x/(2*delta)` for `0<x<delta`, otherwise `e=x-delta/2`. Then
  `s=u-sign(u)*e`, `r=sqrt(v*v+e*e+.25)`, `age=clock-k*s`, `q=r-.9-2.4*age`.
  Preserve the original signed crest/trough and envelope, including `fade_start=12-L*k`;
  use `cos²(pi*s/(2L))`. B45 multiplies a cubic smooth angular mask, one at `e/r<=.5`,
  zero at `e/r>=sqrt(.5)`. Every coordinate, mask and envelope derivative is included.

[Derivative sweeps](derivatives.csv) use .01/.002/.0005 spatial/time increments, capsule
shoulders, angular-mask boundaries, rotated axes and several birth/mature/fading ages. Fine
maximum spatial error is .00255 and vertical-velocity error .00301 m/s, within the provisional
.003/.02 screens. The smaller step is often worse than .002 because float cancellation becomes
visible; do not call the sequence monotone convergence. Signed cancellation residual is zero
in these samples. Separate current contracts strengthen finite-gradient checks and sample exact
central birth/rise/fade/expiry boundaries. They also test independent nonadjacent fronts from
one emitter and unchanged historical-piece queries while the source later turns/dries/restarts.
These are sampled contracts, not an exhaustive proof over all possible hull trajectories.

## Turning diagnostics and their limits

[The original R0 rows](r0-turns.csv) are retained unchanged. [Refined turns](turns.csv) contain
400 rows at common source/radial spacings .1/.05/.025 m, plus a ±6 m search at the middle
resolution. The ±2 m search remains primary. R0's tight-radius source spacing was already
.01875 m; the new common finest source spacing is .025 m, while the radial grid is finer than
R0. This is a three-level common-grid sensitivity study, not proof that every row has converged.

At radius 12 m / four seconds after stopping, on the .025 m grid, source-path length below
25% of each mode's straight crest is baseline **2.30 m**, A0 **2.35 m**, A1 **2.25 m**,
and both B variants **0 m**, out of 7.5 eligible metres. At eight seconds the corresponding
values are **3.825 / 3.850 / 3.800 / 0 / 0 m**. Radius 50 m / eight seconds gives
baseline **2.925 m** and A1 **2.900 m** out of 67.5 m. The apparent B coverage benefit is
rejected because its straight field changes substantially.

**Each turn ratio divides by that candidate's own straight field.** B reinforcement can
partly normalize away; these ratios do not establish preserved baseline amplitude or mechanics.
Tables retain 10/25/50% thresholds, longest contiguous 25% gap, eligibility counts, peak ratios,
local nearest-pair samples, gradient/vertical-speed extrema and ridge-search offsets. Local-pair
values are at the expected ridge, not independent searched crest maxima or joins.

`source_length` measures the historical source arc. `expanded_length` integrates the derivative
of the expected offset curve, including the along-source local-age change; it is not a measured
physical crest length. The inner curve is excluded within 2 m of the center only for the turn
score. Some **±6 m inner searches can cross the center and choose another front**: retain them
as ambiguous diagnostics, never evidence of physical crest displacement or a candidate win.
Near-zero straight references (<.05 m) are excluded; empty rows are not success.

[Concentration samples](concentration.csv) independently scan the full domain, including focus
and post-focus behavior, on 1 m/.5 m grids. These finite grids estimate extrema; they establish
neither exact worst-case gradients nor gameplay launch/capsize outcomes.

## CPU cost and resources

M1 Max/macOS 26.6.2, AppleClang 21, native Release, 2026-11-15. **288 runs**: straight, tight
circle and reversal; one/four emitters; dense/dispersed queries; three alternating paired runs
per alternative. Every run has 20 simulated seconds preroll and 60 seconds / 3,600 measured
frames, 30 synthetic queries/frame. Every continuously emitting pool stayed saturated; no live
record overwrite was permitted. Timing is separate from field rendering, encoding and tests.

Representative four-emitter dense-circle private query-batch milliseconds, median of three
per-run statistics (each candidate has its contemporary paired baseline):

| Candidate | Baseline p50/p95/p99 | Candidate p50/p95/p99 | Logical record payload |
| --- | --- | --- | ---: |
| A0 | .050 / .055 / .063 | .068 / .074 / .084 | 10,240 B |
| B-open | .050 / .054 / .061 | .082 / .088 / .100 | 5,120 B |
| B45 | .050 / .054 / .061 | .084 / .091 / .103 | 5,120 B |
| A1 | .050 / .054 / .057 | .068 / .074 / .083 | 10,240 B |

[All timings](timings.csv) retain source/query/enclosing p50/p95/p99 and absolute/percentage
deltas. Percentages are suppressed for references <=.005 ms; source calls near .001 ms do not
support precise percentage claims. Raw run tables retain repetition spread, occupancy, births,
checksums and exact commands. These are **private harness costs**, not production authority/tick
budget measurements or isolated geometry cost. Source time includes fixed-path evaluation;
query time includes synthetic probe construction and sample accumulation. Baseline advance
scans the private 256-entry allocation too. All modes actually allocate the same **10,440-byte**
private workspace, including unused slots. Logical payload and active capacity differ: baseline/B
128 records, A0/A1 256; steady source births double from two to four per boat per second.
These are research requirements, not an approved production memory/network expansion.

GPU, complete gameplay frame, isolated CPU render preparation, stamp/fallback work, GPU memory,
uploads, replication throughput and coupled hull outcomes are **unavailable** for these rejected
models: none is integrated into those paths. No production graphics gate or authority budget
pass is claimed. R0/R1 HDR cost/tail concerns remain open.

## Motion, verification and provenance

**48 matched ten-second movies**, all 100 frames at 10 Hz, compare four alternatives with
baseline over straight, tight/wide turns, figure-eight, reversal and four-source crossing.
Each has fixed wide and source-centered detail views. All sources stop at tick 3000 (25 s);
frames show 25–34.9 s, not continuing source emission or coupled hull driving. Each revision's
left panel is signed height (red positive/blue negative, clipped at ±2.4 m), right is foam.
The paired movie's left half is baseline, right half the labeled alternative.

These are **CPU diagnostic field movies**, not production-renderer/chase footage, GPU parity,
or gameplay appearance acceptance. Wide grids are 96×96 over 160 m; detail grids cover 80 m.
Nearest-neighbor integer scaling is preserved. Coarse grids and clipped colors can conceal
small gaps or large extrema; use the numerical tables rather than the movies to score them.
Temporal sheets were inspected for straight A1/B-open, tight B-open and wide B45. Four movies
(straight/open, tight/B45, reversal/A0, crossing/A1) completed visible paced playback separately.

Debug full build and Release research-target builds are project-warning-clean. The final
`ctest --preset full` passed **618/618**, including all **36 GPU** cases, in **112.14 s**.
The new test exercises the CPU research contract; existing GPU checks cover unchanged production
code. Current Release parity is 5.3312e-7; the independently reviewed Debug contract reported
9.5367e-7. No human input/audio, browser/slower-GPU or player acceptance is claimed.

Artifacts remain local at `/private/tmp/proteus-wake-research/build-release/wake-r2/`.
The [manifest](manifest.json) points to raw data, 48 movies, source hashes and the review archive.
`wake_geometry-measured` and `timing-sources/` retain the exact executable/source used for
screens, timing and movies. Later verification-only additions strengthen finite checks and
central envelope seam tests; evaluator/header bytes are unchanged. Their build/current source
provenance is separate; no timing equivalence claim is needed. Earlier `screen`, `screen-final`
and `screen-qualified` folders are superseded exploratory history, not retained final evidence.

**Next R2b design question:** can longitudinal support spread with historical front divergence
while transporting the existing finite-source weighting, preserving collinear interior and
endpoint envelopes, and bounding inner-focus concentration—without adding cap strength or
coupling unrelated fronts? Derive that on a pair of historical spans before implementation.
This is a design investigation, not authorization for C joins, protocol growth or lifetime work.
