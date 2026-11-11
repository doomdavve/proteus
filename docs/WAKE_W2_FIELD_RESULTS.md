# W2.1: optimized owned field and accurate-domain proof

2026-10-21, macOS / Apple Silicon. Owner: [wake rewrite plan](WAKE_REWRITE_PLAN.md).
Previous baseline: [W2.1b at 801970a](WAKE_W2_REFINEMENT_RESULTS.md).
Implementation: [boat_wake.c](../src/core/boat_wake.c), [API](../src/core/boat_wake.h),
[C review](../tools/wake_field_review.c), [domain runner/collector](../tools/wake_domain_review.py).

**Current status:** the isolated W2.1 numerical proof remains complete. The subsequent
[runtime feasibility checkpoint](WAKE_RUNTIME_RESULTS.md) optimizes source work and tests
lower cadence plus a GPU port, but both backends fail the provisional runtime budget.
W2.R cheaper operator investigation now precedes source/player integration. The C field
remains the numerical oracle; no runtime backend is accepted.

**Historical decision at 5e8a5ba:** select the optimized graph-polynomial C field at
**144x96 m, dx .75 m, spatial order 8, polynomial order 256, dt 1/120 s** for isolated W2.2
source calibration. The matching-accuracy 80 s domain study passed against a doubled
reference; ownership, snapshots, signed work/sinks and propagation checks remained covered.
The following implementation/storage/cost record describes that revision. Its two-operator
kick, eight-array storage and next-task recommendation are superseded by the checkpoint;
the field/domain measurements remain evidence for the original numerical settings.

## Evaluation and accounting changes

The polynomial, spatial stencils, land topology and kick-drift-kick integrator are unchanged.
Optimization changes their evaluation, not their selected dispersion:

- Split water into horizontal runs whose x stencils are straight and whose mirrored z
  offsets match. These include reflecting top/bottom rows and thin water channels. All
  other wet cells use compact scalar neighbor maps. Every offset is derived from the
  original water-only graph; none crosses land. Clang interleaves the regular vector loops;
  other compilers use the same portable C operations without that guarded hint.
- Evaluate the same positive polynomial with a factored Clenshaw recurrence. For
  `A = sum(w_j (I-T_j(B)))`, factor `A=(I-B)R(B)`. Its coefficients are
  `R_0=sum(j w_j)` and `R_k=2 sum((j-k) w_j), j>k`. The final water-only Laplacian
  annihilates constants explicitly. No order reduction, new frequency fit or wave-band
  cutoff is introduced. The scalar forward evaluator remains selectable with
  `BoatWakeConfig.reference_operator` as an independent implementation oracle.
- Compute the Hamiltonian when the ledger is requested. Normal stepping uses one operator
  evaluation, rather than three with two hidden energy audits. At this revision, source kicks
  measured actual before/after Hamiltonians, and reset explicitly measures and retires the remaining
  field. Independently accumulate signed work and nonnegative absorption every time they
  occur. At an audit, measure retained energy from the actual fields and report the signed
  balance residual `H + absorbed + retired - injected`.
- Cache `delta=expm1(-rate*dt)` for immutable absorption. Apply eta damping by `1+delta`;
  record loss with `rho g dx^2/2 sum(eta_old^2 (-delta)(2+delta))`. No transcendental work is
  needed during normal stepping. The copied original rates still reconstruct independent
  clones. Source guards also bound Clenshaw intermediates, using logarithms to avoid
  overflow while constructing the bound.

Residual is an **on-demand numerical error measurement**, not an independently predicted
per-tick error. Ledger closure follows its definition and does not establish conservation.
Independent Hamiltonian drift/time-refinement checks, explicit source-removal checks, the
independently sampled absorption comparison and reference-operator agreement provide the
verification. Auditing cannot suppress a residual or change the physical trajectory.

At this revision the owner reports actual requested allocation bytes with `boat_wake_storage_bytes()`.
This includes the owner, eight double fields (one new cached damping field), land, polynomial
coefficients, run descriptors and compact neighbor maps. It excludes allocator overhead,
transient construction allocations, RSS and a second owner for a snapshot. Peak construction
memory is not measured. The eight fields and each map are independently owned; no mutable
buffer or configuration map is shared by a clone.

## Correctness and snapshot checks

`wake_field_review --check` retains the independent cosine-mode/dispersion tests, actual
240-step phase checks, irregular-mask symmetry and mean-level tests, source-off Hamiltonian
refinement, deep copies and thin-wall probes. New comparisons exercise spatial orders 2/4/8
on open water, an irregular island, one-cell water channels, an isolated wet cell and a
one-cell full wall. They include absorption, positive/negative kicks, retirement and
continuation after destroying/resetting the original owners. Odd dimensions and short runs
exercise vector tails and scalar/vector interfaces.

Fast and scalar states must agree within **2e-9 m in eta and 2e-9 m^2/s in psi**; energy/work/
sink channels must agree within 2e-9 of the injected-work scale. Polynomial orders 1/2/3/256/
1024 cover the minimum-degree branches and maximum supported degree. A second fast owner
is audited each tick while the first is mostly unaudited: their eta/psi states match **bit for
bit**, so query cadence does not alter simulation. Clones preserve the selected evaluator
and exact requested storage size. Extreme finite unit scales verify atomic overflow rejection.

A separate undamped/damped first-step comparison checks eta against `exp(-rate*dt)`, equal
psi, and loss from independently sampled undamped eta using the analytic exponential loss.
This guards the cached absorption implementation independently of ledger closure.

## Matched-accuracy 80 s domain study

All six cases use the selected **.75 m / order 256 / spatial order 8 / dt 1/120 s** configuration
for 9600 actual C steps. The small domains are 144x96 m (192x128 cells); references are 288x192 m
(384x256 cells). Cases are open water, the irregular island, and a one-cell-wide full wall.
They repeat the earlier source and absorption in the same world units:

- Compact prescribed potential impulse centred at (-18,0), exactly zero beyond radius 9 m.
  No moving hull or resistance-work calibration is implied.
- Island half-width .8 m at its waist / 2.3 m elsewhere, z within +/-13.5 m. Grid resolution
  determines its occupied cells; this is the fine-grid geometry, not the previous coarse mask.
- Absorbing water within a 3 m square fringe of land and open-edge strips 10.5 m wide,
  with the same rates as the baseline. The wall is one fine cell wide; wide stencils cannot
  sample through it.
- ROI: central 24x24 m, 32x32 fine samples at identical physical coordinates in each pair.
  Difference L2 is normalized by `sqrt(sum(psi_initial^2) * (2 pi/6)/g)` as before. The
  unchanged limit is <1% at 12/40/80 s. Initial potential normalization agrees across domains.
- Both walls must have exactly zero early/late/return probe intensity, and actual C steps
  also assert zero eta/psi immediately beyond the wall. Finite-island peak shadow intensity
  must be <1% of open water, in both domains. Small-domain retained energy at 80 s must be
  <1% of initial work. Signed propagation residual magnitude must be <1% in every case.
  The larger reference may still contain outgoing energy.

All declared gates pass; `failed_gates` is empty:

| Time | Open normalized domain difference | Island normalized domain difference |
| --- | --- | --- |
| 12 s | 0.0226% | 0.0334% |
| 40 s | 0.6892% | 0.4351% |
| 80 s | 0.2784% | 0.1374% |

The full-wall probes stay exactly zero for 80 s in both domains. The finite-island early/late
shadow-to-open intensity ratios are **0.0491% / 0.0687%** at 144x96 m and **0.0486% / 0.0720%**
in the doubled reference. A finite shadow can contain waves going around the island; the
zero-coupling claim applies to disconnected water.

| Domain/case | Retained/initial energy at 80 s | Signed numerical residual/initial energy |
| --- | --- | --- |
| 144x96 m open | 0.14705% | +0.008771% |
| 144x96 m island | 0.02889% | +0.008786% |
| 144x96 m wall | 0.02087% | +0.008786% |
| 288x192 m open | 13.50969% | +0.007964% |
| 288x192 m island | 1.85218% | +0.008652% |
| 288x192 m wall | 2.70485% | +0.008602% |

The small open field absorbs 99.86172% of its initial energy; sinks are reported separately
from the positive residual. The larger reference legitimately retains outgoing energy away
from the cropped observation area. Sampling, leaving/returning to the ROI and producing the
plots never cull or retire field state. The six cases consumed about 1390 CPU seconds in total,
run as independent processes. This is a domain study at a fixed grid/order; it does not prove
shoreline grid convergence or establish physical diffraction/reflection accuracy.

The Python runner writes each case's raw ROI samples, metadata, PPM and progress log before
collecting the result. Collection checks the exact grid/order/dt/tick count and source scale,
then independently compares the paired samples. Negative fixtures with a wrong tick count
and an added region contaminant were both rejected. Cases can run in parallel because each
owns its complete C field; no simulation data is shared between processes.

## Measured costs and runtime limit

Apple Clang `-O2`, nonzero 6 m cosine potential, four warm-up steps and timed stepping/audit/
kick loops, CPU process time. Kicks retain their full signed-work audit. One-step and
one-step-plus-audit timings are both reported; changing audit frequency is part of the
speedup and is not presented as free energy accounting.

| Domain/evaluator | Step (ms) | Step + audit (ms) | Audit (ms) | Kick (ms) | Owned bytes |
| --- | --- | --- | --- | --- | --- |
| 72x48 m fast | 2.89 | 5.92 | 2.88 | 5.87 | 439112 |
| 72x48 m scalar oracle | 8.84 | 17.79 | 8.88 | 17.75 | 792904 |
| 144x96 m fast | **9.32** | 18.79 | 9.39 | 18.79 | **1674568** |
| 144x96 m scalar oracle | 35.85 | 71.76 | 35.78 | 71.40 | 3170632 |

The fast operator is about 3.8 times faster than the scalar oracle at the same audit cadence.
Compared with the prior approximately 109 ms step, the new approximately 9.3 ms step also
removes two per-step audits. `-O3` measured approximately 9.18 ms/step / 18.35 ms/kick for the
large fast configuration. Complex coastlines cost more: the long accurate runs averaged
roughly 9.7/10.2/11.3 CPU ms per tick for small open/island/wall cases, including probes and
occasional images while other cases ran concurrently.

This is still above the 8.33 ms tick interval even before adding a hull/render. An emitter
that called that revision's kick API each tick added two more full-field evaluations. No real-time
source budget or browser/Windows performance was established. The recommendation at that time
was to calibrate from actual fixed-tick stern histories while measuring source-work cost. Before
runtime integration, choose a measured update/source/audit schedule or a faster backend and
repeat numerical/source convergence at the actual schedule. W3 retains whole-frame/history,
query/upload/render and scene-specific storage acceptance. The selected 144x96 m box is an
isolated test/source-calibration domain, not an accepted moving gameplay world extent.

## Reproduction and verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
cc -std=c17 -O2 -g -Wall -Wextra -Wpedantic -Isrc tools/wake_field_review.c src/core/boat_wake.c -lm -o build/wake_field_review_optimized
./build/wake_field_review_optimized --check
./build/wake_field_review_optimized --costs > build/wake-w21-costs.json
python3 tools/wake_domain_review.py --executable build/wake_field_review_optimized --output build/wake-w21-accurate --jobs 6
# Collect existing complete case files without running the simulations again:
python3 tools/wake_domain_review.py --output build/wake-w21-accurate --collect-only
cc -std=c17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc tools/wake_field_review.c src/core/boat_wake.c -lm -o build/wake_field_review_sanitize
./build/wake_field_review_sanitize --check
xcrun xctrace record --template 'Time Profiler' --output build/wake-w21-cost.trace --launch -- ./build/wake_field_review_optimized --costs-fast
```

The portable PNG/SVG comparison has six columns (small open/island/wall, then doubled
open/island/wall), cropped to the same central 72x48 m. Rows are 4/8/12/40/80 s; the SVG labels
times, domains and the +/- .03 m display scale. All rows were inspected. Display colors may
saturate; the simulated height is not clamped. Generated artifacts remain under `build/`.
The long review is separate from CTest; the normal suite exercises the smaller independent
mode, energy, geometry and optimized/scalar checks.

2026-10-21 verification: Debug configure/full build and all **61 CTests** passed with
`PROTEUS_GPU_TESTS=OFF`; the final full-suite run after the Clenshaw guard/minimum-order
checks passed in 254.72 s. The final AddressSanitizer + UndefinedBehaviorSanitizer `--check`
passed with no reported diagnostics. The six optimized accurate-domain cases all exited 0;
independent collection produced an empty `failed_gates` list. Wrong-tick and contaminated-
region collector fixtures were rejected. Python compilation, Clang-format, changed local
documentation links and `git diff --check` passed.

Xcode Instruments Time Profiler recorded and exported the optimized `--costs-fast` run.
Of 926 sampled milliseconds, leaf frames were 44.49% `differences`, 35.21% `regular8` and
19.22% `recurrence`; the remaining cost is still field evaluation. This trace includes field
creation and the separate tick/audit/kick microbenchmarks, not a full-game frame profile.
Artifacts: `build/wake-w21-cost.trace`, `build/wake-w21-time-profile.xml`,
`build/wake-w21-profile-summary.json`; verification logs are `build/wake-w21-*.log`.
No runtime motion/input/windowing/audio/asset-loading behavior changed, so no new manual
boating/playback or human acceptance is claimed. GPU/browser/Windows execution, physical shore
accuracy, arbitrary long waves, sustained engine forcing/resource pressure, spatial currents,
reciprocal hull work and calibrated sources remain unverified.
