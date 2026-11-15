# R2b design and algebra checkpoint

Owner: [wake research plan](../../../WAKE_RESEARCH_PLAN.md).
Proposal: [historical weighting and divergence](../../../WAKE_R2B_DESIGN.md).
Completed design-only checkpoint, 2026-11-15. No candidate evaluator, source adapter, shader,
protocol or production behavior is implemented or selected by this step.

## Reviewed decision

Astra reviewed the R2a failure evidence, derived the curved-map fold and proposed an immutable,
successor-only outward dilation. The supervisor retains that design for **one proposed private
CPU experiment**, with signed-density transport `p=1`, baseline cadence/strength/lifetime,
and provisional `epsilon=.25` and `|turn|<pi/2` domain gate. These are experiment choices,
not measured optima, player acceptance or permission to increase shipping budgets.

The proposal preserves each original finite window, phase, birth and fade, with no neighboring
weight normalization. It gives exact algebraic baseline identity for zero curvature and each
record's inner half-plane. A real implementation must use a direct baseline branch to avoid
floating-point coordinate round trips. Outer spreading attenuates each transported contribution;
there is no claim it will fill useful gaps within handling tolerances.

The normal coordinate stays in the historical frame; **shear introduces tangential drift and
changes crest orientation**. Retaining the historical frame does not retain the same world-space
propagation normal. The proposed hard turn-domain gate is not smooth across source histories;
near-threshold alternating spans are an explicit future test.

Astra's final review found no blocking equation/invariant error after this wording correction.
It independently checked source/report/CSV hashes and the equal-circle-chord curvature identity.
It reviewed the numerical report, not a wake implementation or gameplay performance run.

## Numerical audit scope and results

Run separately from CTest:

```sh
python3 tools/wake_divergence_design.py --output build-release/wake-r2b/algebra-new
```

The script uses Python double arithmetic and a **synthetic** signed scalar reference,
`sin(q)*exp(-q*q/10)` with `q_test=v-2.4*age`, finite cosine support and birth/rise/fade gates.
It does not evaluate the production crest/trough or its radial regularization. The separate
phase-map audit includes the proposed regularized-radius chart and emission-age tilt.
These checks validate algebra on declared samples; they are not C-float/GPU parity, actual
source histories, coverage, hull encounters or resource/performance measurements.

The retained [report](algebra.json) contains platform/Python versions and source/CSV checksums.
There are 3,780 scalar cases over 420 Cartesian parameter configurations at nine times;
repeated times are not independent geometry cases. The separate phase audit has 216 parameter
configurations. Additional cases use unequal finite support, inverse speeds, births and signs.

| Audit | Largest recorded discrepancy |
| --- | ---: |
| Zero-curvature finite-source scalar/derivative identity | 0 |
| Per-record inner-side identity | 0 |
| Identical-descriptor signed cancellation | 0 |
| Direct map/inverse | 8.88e-16 |
| Cartesian Jacobian **determinant** | 2.42e-10 |
| Full phase-map derivative/determinant checks | 6.35e-10 |
| Scalar derivative, centered step 1e-6 | 9.14e-6 |
| Finest independent-grid moment quadrature residual | 6.54e-7 |

[Map samples](maps.csv) retain dilation, normal derivative and determinant for 84 `(epsilon,K,v)`
combinations. The Cartesian audit does not individually check every Jacobian matrix entry;
phase/scalar audits separately exercise shear and gain terms.

The scalar derivative maxima at steps 1e-4/1e-5/1e-6 are .000913 / .0000914 / .00000914.
They show the expected linear finite-difference truncation near C1 temporal seams. The first
exploratory audit incorrectly demanded a 1e-6 residual at a 1e-5 step; it was replaced by the
three-resolution check before retaining evidence. The final 1e-6-step check uses a 2e-5
numerical tolerance, not a new gameplay threshold.

[Moment quadrature](transport.csv) has 72 rows at four resolutions, using different source/world
cell counts. It confirms the declared `D^(1-p)` signed/absolute and `D^(1-2p)` squared slice
scalings for `p=0,.5,1`. These exponent comparisons explain the invariant choice; they are not
an alternate-candidate tuning sweep or evidence of physical energy conservation.

## Counterexamples retained as constraints

[Curved-map cases](folds.csv) cover pre-focus, focus and post-focus at radii 12 and 50 m.
At focus, crest tangent length remains nonzero while area determinant is zero; two distinct
phase-chart points map to the same location. Crest-length stretch is therefore insufficient
for an invertible transport. These are algebraic configurations, not active wake observations:
the 50 m crest-focus age is about 20.46 s, beyond the present 16-second lifetime; the 12 m
focus at about 4.63 s is within it.

The outward map is globally invertible in Cartesian coordinates and has no per-record focusing
gain, but it cannot promise that every summed inner-region peak stays below baseline. The
synthetic counterexample at `(7.6,12.7)` gives:

| Contribution | Value |
| --- | ---: |
| First record, inner half-plane (unchanged) | .976480 |
| Second record, original support | 0 |
| Second record, expanded outer support | .239209 |
| New sum | 1.215689 |
| Baseline amplitude bound for the chosen region | 1.2 |

The second baseline record vanishes for `x>7.5`; its expanded outer support enters a region
on the first record's inner side. The new sum exceeds that region's bound without either record
having a transport gain. This is a synthetic counterexample to a universal bound, not a
reconstructed boat trajectory, exact game-focus maximum or capsize outcome. Total-field
concentration remains an empirical rejection gate.

## Verification and limits

The Debug full build passed without project warnings. `ctest --preset full` passed **618/618**,
including **36 GPU-labelled** cases, in **101.89 s**. These are regressions for the unchanged
production system; the algebra script is run explicitly and is not registered as a new CTest.
No new audiovisual review is needed for this design/algebra-only update. No new captures,
movies, playback, input/audio check, candidate GPU qualification or player acceptance is claimed.

Capture/reset/restore, predecessor expiry and slot-reuse behavior are **design requirements,
not implemented lifecycle tests**. Source-side curvature metadata, its quantization and exact
tracker/structure sizes are unmeasured. The naive scalar-payload arithmetic in the proposal is
not a memory/network allocation approval or measured throughput. CPU/GPU/frame costs, coverage,
encounters, stamps/fallback and platform qualification are unavailable for this unimplemented
proposal. The R0/R1 budget and HDR-tail issues remain open.

[The manifest](manifest.json) retains exact audit command/source/report/CSV checksums and local
build/CTest log locations. Raw retained data are also under
`/private/tmp/proteus-wake-research/build-release/wake-r2b/algebra-retained/`.
Earlier `algebra*` directories are exploratory history, not the retained result.

Next: decide the private descriptor/capture ledger, then implement only the proposed `p=1`
baseline-cadence field experiment if work continues. Measure baseline-relative straight/endpoints,
outer coverage and full-history concentration before considering coupled hull or GPU integration.
No C join, lifetime change or production compatibility change is selected.
