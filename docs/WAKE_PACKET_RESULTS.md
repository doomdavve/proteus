# Compact source-packet experiment

Historical record: the physical wake-field experiment and its review tools were retired on 2026-11-14. Commands below describe the earlier revision and are no longer runnable on current source.

2026-10-22. Owner: [wake rewrite plan](WAKE_REWRITE_PLAN.md). Implements the first
isolated W3.S representation experiment. **Fixed spectral quadrature is rejected as
the runtime representation.** It preserves useful short-history behavior with much
less storage, but its accurate configuration is much slower than the basin solver.
At this checkpoint the opt-in basin candidate and default D3 gameplay were unchanged.
[Arcade wake objects](WAKE_OBJECT_RESULTS.md) subsequently became the free-boating default.

## What was tested

`tools/wake_packet_review.c` retains eight world-space source records: position,
direction, signed strength and integer emission tick. No spatial grid follows the
camera. Each record represents a balanced Gaussian pressure-dipole impulse, matching
the production source's spatial recipe before truncation. Two perpendicular moving
source histories emit four impulses each at 0, .5, 1 and 1.5 s. These are prescribed
impulses, **not actual wetted stern trajectories, resistance calibration, or the
production symmetric midpoint source integration**.

A shared midpoint quadrature over `[-4,4]^2 rad/m` reconstructs the combined signed
height/potential spectrum from those source records at any requested tick. Only the
independent half-plane is stored. Propagation uses the existing order-256/spatial-8
operator symbol at .75 m and its exact discrete kick-drift-kick recurrence at 120 Hz:

```text
theta = acos(1 - dt² g A(k)/2)
eta_hat = dt A(k) sin(age_ticks theta)/sin(theta) impulse_hat
psi_hat = cos(age_ticks theta) impulse_hat
```

This deliberately isolates representation/reconstruction error from a changed
dispersion law or timestep. Height, both analytic height gradients, vertical velocity
`A psi`, and both horizontal velocities `grad psi` share this reconstruction. A
separate height-only evaluator is used for surface samples. A common world origin
keeps source phases independent of camera position.

The comparison calls the **existing C `BoatWake` cosine solver**, with the same
eight impulses sampled on its .75 m grid, truncated at +/-7.5 m and locally balanced
as in production. There is no absorption or land in either model. The main reference
is 384x256 cells (288x192 m); the control doubles both dimensions. The reference
remains a finite reflecting rectangle, not an infinite ocean.

At each integer second from 2 through 24, compare 25 off-grid samples in each of
three regions. Two regions travel apart, overlap and return (up to about 90 m away); a
third stays near the old source. The first path returns through the outgoing wake
at X=35.2 m, rather than returning only to the now-quiet emission position. Errors include all sampled regions while away.
The complete-history metric sums squared error and reference norm over these
**1 Hz samples**, separately for all six physical outputs. Its declared accuracy
gate is <1% for every output. This is not a 120 Hz gameplay-history proof.

## Results

Development Mac / M1 Max, macOS 26.6.2, Apple clang 21, `RelWithDebInfo`, native
Accelerate reference. Packet reconstruction itself uses portable C17 scalar math.
Times are process CPU milliseconds, with one warmup and eleven measured trials.
Other development work was running: these are feasibility observations, not an
exclusive-device or worst-case performance claim.

| Quadrature width | Shared nodes | Owned bytes | Height history L2 | Worst of six history L2 | Median workload ms | p95 / max ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 32 | 512 | 37,320 | 146.66% | 159.32% | 1.058 | 1.129 |
| 64 | 2,048 | 147,912 | 71.49% | 89.75% | 3.862 | 4.001 |
| 128 | 8,192 | 590,280 | 10.745% | 18.788% | 14.471 | 14.716 |
| 256 | 32,768 | 2,359,752 | .04313% | .04313% | 56.647 | 57.762 |

The timed workload rebuilds the spectral cache, evaluates six complete queries,
reconstructs **only a 16x16 height surface**, and evaluates the spectral energy
quadrature. It is one endpoint evaluation, not two 120 Hz ticks per rendered frame.
It excludes new emission, hull integration, GPU transfer/rendering, promotion,
merging and retirement. Thus it is a **lower bound on a complete wake workload**;
the accurate configuration fails 2 ms before those costs. With eleven samples the
nearest-rank p95 is the observed maximum, not a robust tail estimate.

For width 256, median stage costs are 5.701 ms cache construction, 1.458 ms for six
queries, 49.361 ms for the surface, and .045 ms for spectral accounting. Stage medians
need not sum to the total median. Storage includes the owner, all eight source slots
and the shared cache; no per-query/tick allocation occurs. Reference field-owned
storage is 8,176,112 bytes, or 32,274,928 bytes when doubled. Comparison buffers and
allocator/native-transform overhead are excluded from these ownership figures.

The doubled reference gives width-256 height-history error .03698%, which is also its worst
component. A separate 181x49 height crop, X=-90..90 / Z=-24..24 m, compares
both references directly:

| Time | Reference vs doubled reference L2 | Width-256 packet vs doubled reference L2 |
| --- | ---: | ---: |
| 4 s | .0007534% | .01630% |
| 24 s | .08213% | .35113% |

This supports the finite-horizon cropped comparison; it does not prove arbitrary
long propagation or domain independence everywhere. The returning path encounters a 6.83 mm RMS wake at 24 s; its width-256 height
error is .04624% against the basin and .04070% against the doubled reference.
This stronger return probe replaced an initial path that returned to the quiet source
position and mostly measured micrometre tails. Nevertheless, individual-time errors
exceed 1% at 13 and 17–20 s; the largest is 4.23% (4.46% in the doubled control).
Absolute RMS errors are reported alongside relative errors so weak reference signals
remain visible: **the full set of existing return/accuracy gates has not passed**.

The inspected contact sheet shows compact overlapping waves at 4 s and the expanded
wave train at 24 s. Width 128 also reconstructs unwanted signed copies at the crop
edges, already visible at 4 s. Width 256 removes the obvious copies in these crops.
The labelled SVG and PNG use the same height scale within each time row; colors
saturate at the reference peak and are not simulated height clamps.

## Why this variant is rejected

Uniform spectral samples have an implicit replica separation `2 pi / delta_k`:
25.13, 50.27, 100.53 and 201.06 m in this sweep. Midpoint offsets cause sign reversals
between some copies. These are reconstruction artifacts, not waves that physically
travelled back. Increasing quadrature width delays the error and increases the cost.
The implementation allocates independently of declared map extent, but keeping a
fixed quadrature accurate as propagation distance/time grows is **not established**.
It is not an adaptive dispersive packet implementation of the Water Wave Packets paper.

This also demonstrates why energy alone cannot select a representation. At 24 s,
width 32's spectral energy differs from the reference by only .00867%, although its
sampled height field is badly wrong. Width 256's energy difference is 3.68e-8 relative.
The spectral Hamiltonian integrates the **combined** eta/psi coefficients, including
interference cross terms. Source work is its signed before/after difference at each
emission; free-propagation residual remains separate (about .10464 J on 1263.50 J).
But this is a **quadrature estimate of continuum energy**, not a proof of the energy
of the repeated reconstructed field across infinite space. The C field's independent
Hamiltonian comparison is useful evidence, not a replacement for a converged spatial
audit of a future localized representation.

An explicit eight-record capacity rejects a ninth emission before changing wave
state or work. The contract check repeats rejection 100,000 times, checks unchanged
retained state, and checks explicit whole-state retirement separately. No strong
wave is overwritten or silently aged out. **Rejecting further emission is not an
acceptable production exhaustion policy**; no long-emitting world stress gate passes.
Retirement currently accounts for the spectral energy estimate, not a validated
localized-field energy transfer. There are no promotion or merging operations yet.

## Decision and next task (superseded by arcade direction)

Keep the existing basin solver and the new diagnostic as comparison tools. Do not
integrate this fixed-quadrature evaluator into gameplay or move its full direct
reconstruction to GPU as the selected solution.

Historical proposed next task: test **spatially localized, adaptively refined dispersive packets**, using
this source recipe and matched field probes. Measure whether each detail patch can
reconstruct from a bounded local packet set without introducing the quadrature's
replicas or losing returning phase. Splitting/local spectral refinement needs its
own field and reconstructed-energy convergence checks; an amplitude envelope alone
must not be used to discard live waves. Extend the strong returning encounter and
add a stationary wake-encounter probe; the near-source control becomes quiet late
in this experiment.
Only after this discriminator passes should actual stern-history resampling,
120 Hz query histories, islands/barriers, continuous-emission limits and GPU surface
reconstruction be brought forward. The original [W3.S gates](WAKE_SCALING_RESULTS.md#required-next-representation)
remain requirements, not results of this experiment.

The user subsequently selected the [bounded arcade object alternative](WAKE_OBJECT_RESULTS.md).
That supersedes physical packet refinement as the active next task; the rejection and
measurements above remain historical evidence.

## Reproduction and verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure

cmake -S . -B build/review -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/review --target wake_packet_review -j
./build/review/wake_packet_review --check
./build/review/wake_packet_review 32 > build/wake-packet-32.jsonl
./build/review/wake_packet_review 64 > build/wake-packet-64.jsonl
./build/review/wake_packet_review 128 build/wake-packet-128-capture.csv > build/wake-packet-128.jsonl
./build/review/wake_packet_review 256 build/wake-packet-256-capture.csv > build/wake-packet-256.jsonl
./build/review/wake_packet_review 256 --double build/wake-packet-double-capture.csv > build/wake-packet-double.jsonl
python3 tools/wake_packet_plot.py build/wake-packet-128-capture.csv build/wake-packet-256-capture.csv build/wake-packet-comparison
```

`--check` fails on broken contracts: analytic initial impulse, gradient consistency,
leave/query/return identity, million-metre translation, signed cancellation, hard
capacity, explicit retirement, and a short six-output/energy comparison with the
existing C solver. Review mode exits successfully when it completes measurements,
including expected candidate rejection; `history.all_below_one_percent` reports
the aggregate accuracy decision only, not runtime acceptance.

2026-10-22 verification, macOS / Apple Silicon:

- Debug configure and full build pass; optimized diagnostic builds without compiler
  warnings. All **92 CTest cases pass across split runs**, including 22 GPU checks.
  The first full run passed tests 1–6, then stalled in sandboxed desktop initialization
  at test 7. That runner/child were stopped; tests 7–92 passed with desktop access via
  `ctest --test-dir build --output-on-failure -I 7,92,1 --timeout 180` (403.35 s).
  The final packet contract executable was rebuilt and its CTest rerun passed (.39 s).
- The portable C backend passes the packet contracts under ASan/UBSan. This is a
  contract/reference smoke check, not a sanitized full quadrature sweep.
- Four optimized sweeps and the doubled-reference control complete with finite JSON
  output. Crop coordinates match for the direct reference-domain comparison. The
  generated PNG was inspected; the SVG is standalone with labels and an embedded image.
- C formatting, Python compilation, changed local documentation file links and
  `git diff --check` pass.

Logs/artifacts: `build/wake-packet-build.log`, `wake-packet-ctest.log`,
`wake-packet-ctest-desktop.log`, `wake-packet-contracts.log`, `wake-packet-{32,64,128,256}.jsonl`,
`wake-packet-double.jsonl`, `wake-packet-domain-comparison.json`, the three capture CSVs,
and `wake-packet-comparison.{png,svg}`. All paths after the first are also under `build/`.

No gameplay simulation, motion, input, audio, loading, renderer or defaults changed,
so no new manual demo or user acceptance is claimed. No browser/Windows timing or
GPU packet backend was tested. The per-time accuracy, continuous-emission exhaustion,
topology and reconstructed-energy limitations above remain unresolved.
