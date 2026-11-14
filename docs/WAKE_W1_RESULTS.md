# W1: baseline, strip rejection and heightfield fallback

Historical record: the physical wake-field experiment and its review tools were retired on 2026-11-14. Commands below describe the earlier revision and are no longer runnable on current source.

2026-10-21, macOS / Apple Silicon, Debug. Owner: [wake rewrite plan](WAKE_REWRITE_PLAN.md).
W1 is implemented as an isolated diagnostic. **The connected-strip prototype is rejected
for player integration.** Tight inner fronts fold, and the measured reconstructed energy
has not converged with mesh/grid refinement. The subsequent
[linear heightfield fallback](#heightfield-fallback--2026-10-21) passes its isolated periodic
free-propagation gates. W2 is revised around an owned field, boundary/island proof and
calibrated source work. The [W2.1 owned-field proof](WAKE_W2_FIELD_RESULTS.md) and subsequent
[runtime feasibility checkpoint](WAKE_RUNTIME_RESULTS.md) own the current evidence; cheaper
operator investigation now precedes source integration. Gameplay still uses D3.

## Reproduce

From the repository root, using Python 3 with only its standard library:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DPROTEUS_GPU_TESTS=ON
cmake --build build -j
python3 tools/wake_front_review.py --check --baseline build/wake_baseline
python3 tools/wake_front_review.py --baseline build/wake_baseline --output build/wake-w1
ctest --test-dir build --output-on-failure
```

The plots and sampler need no graphics context. For display-free CTest runs, configure
with `-DPROTEUS_GPU_TESTS=OFF` and report that GPU checks were omitted.

The full review writes `measurements.json`, plus `straight`, `loop`, `s-turn` and `crossing`
SVG/PNG plots into `build/wake-w1/`. Each SVG contains the sequence at 16, 24 and 32 s:
old height, prototype height, old slope, prototype slope, then a side section along Z at
X approximately zero. Orange is old height; cyan is prototype height. The PNG repeats the
same panels and section without labels; use the SVG for legends. Top-down views cover
X/Z ±96 m at 1.5 m spacing. Height uses a fixed ±0.6 m color scale; slope uses 0–0.5.
Larger values saturate the plot color, **not the simulated height**. Displayed slopes use
centered differences on that grid. These plots sample the analytic/model field without
production water geometry, shading, camera-distance fades, foam or a moving hull.

`wake_baseline` queries the actual `boat_water_query()` implementation; no old wave equation
is duplicated in Python. It also exposes the production analytic gradient and vertical
velocity for arbitrary X/Z points on stdin. For example:

```sh
printf '0 16\n0 17\n' | ./build/wake_baseline straight 16
```

Sources run for 12 s with exact 0.4 s intervals: 30 stations per train, or 60 for two
crossing trains. Both fit the 64-slot ring, isolating expiry from overwrite. Straight paths
travel at 8 m/s; the loop has radius 12 m and speed 8 m/s. The S turn uses X=8t−48,
Z=12 sin(π(t/6−1)); its speed varies, but all baseline emissions deliberately use the
same 8 m/s source-strength input. Crossing sources are orthogonal straight paths. The
baseline applies the existing common 2 m stern offset to each interval's two endpoints;
the prototype uses a continuous stern curve. These are controlled prescribed paths, not
new gameplay recordings or a calibrated moving-source wake model.

## Baseline measurements

An isolated production patch, sampled along its moving envelope centreline at 0.025 m
spacing, has these peak absolute heights:

| Patch age (s) | 1.4 | 8 | 14 | 17 | 19 | 20 | 24 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Peak (m) | 0.27193 | 0.26681 | 0.27558 | 0.12193 | 0.01875 | 0 | 0 |

The source amplitude is 0.39 m before the existing envelope/overlap normalization. Phase
turnover changes the measured peak before fading starts. The forced 14–20 s fade is
measured separately from source-history capacity. The full straight run's height-squared
area falls from 23.0751 m⁴ at t=16 to 8.3710 m⁴ at t=24 and 2.23e−10 m⁴ at t=32.
The tiny t=32 residual comes from the path-axis emission-time adjustment near the last
segment; the isolated centre station is exactly zero at age 20. Height squared alone is
not a physical energy audit.

At t=16, probe the outgoing front near source station s=5.8 and the join s=6.0, across a
6 m transverse section with 121 samples. Locate each section using the continuous stern
path and the old 1.6 m/s envelope speed. This gives:

| Prescribed path | Station RMS height (m) | Join RMS height (m) | Join / station |
| --- | --- | --- | --- |
| Straight | 0.15130 | 0.13402 | 0.8858 |
| Tight loop, outer front | 0.15124 | 0 | 0 |
| S turn | 0.15179 | 0.13411 | 0.8835 |

This isolates a genuine absent-support gap in the loop's analytic surface. Straight/S
ratios include phase variation, local overlap and the endpoint-offset approximation;
they are not percentages of the user-visible fragmentation. The loop plots show separated
outer patches; production mesh undersampling and camera fades were not isolated here.

## Prototype and declared gates

The standalone Python experiment joins source positions,
normal directions and emission times with cubic Hermite curves. Each front has material
coordinates (source time s, transverse distance q). Its envelope travels at group speed;
its carrier travels at phase speed. A compact derivative profile balances transverse
crests and troughs. Independent fronts add signed height and surface potential, with no
global overlap divisor. Splitting a Hermite cell preserves the curve and its derivatives,
emission clock and phase exactly, halves its material budget and footprint, and preserves
the reconstructed density. The budget is a relative material/source weight, **not an
engine-work budget in joules**. Strength is intentionally uncalibrated at W1.

A 6 m deep-water band gives phase speed 3.060696 m/s and group speed 1.530348 m/s. The
24 m-wide compact train spans four carrier wavelengths. Analytic time evaluation avoids
a timestep integrator in this experiment; no timestep-halving claim is applicable. There
is one narrow band, not yet a finite-band dispersive widening implementation.

Before accepting a replacement, require:

- Less than 1% reconstructed energy drift over a source-off 60 s interval, and less than
  1% change under simultaneous node/transverse/grid refinement.
- Subdivision field error below 1e−8 m; conserved material budget within 1e−12; phase/group
  travel errors below 1e−9 m/s in the straight-front probe.
- Relative mean level below 0.001 of the 0.3 m source scale; exactly zero invalid/folded
  front cells. An inward front may not disappear merely because it focuses.

Transport scales amplitude by the inverse square root of the local area Jacobian. On
a spreading radius-40 m arc's outer front, the represented area grows from 3237.32 to
8526.20 m² between t=16 and 76, while the centre amplitude falls from 0.25309 to 0.15595 m.
Material-coordinate potential-energy quadrature stays near 66,635.45 J, with the two
refinements differing by approximately 0.000012%. Integrated displaced volume stays below
1.1e−5 m³ in this case. This establishes the local spreading rule in the benign example;
it is only potential energy, not proof of full wave conservation.

The reconstructed field audit uses the linear deep-water functional
`E = ρ/2 ∫(g η² + ψ |D| ψ) dA`, with ρ=1000 kg/m³ and FFT evaluation of `|D|`.
The surface potential ψ is assigned from a narrow-band quadrature of the compact profile;
it is not independently evolved by the exact free-surface equations. The domain is
384×384 m, contains the full train at every sampled time, and has zero boundary height.
Samples at t=16, 31, 46, 61 and 76 give:

| Reconstruction | Energy at t=16 (J) | Energy at t=76 (J) | Max deviation from t=16 |
| --- | --- | --- | --- |
| 0.4 s nodes, 0.5 m transverse mesh, 1.5 m FFT grid | 236025.48 | 235592.37 | 0.184% |
| 0.2 s nodes, 0.25 m transverse mesh, 0.75 m FFT grid | 269943.15 | 269927.31 | 0.00587% |

Small temporal drift does **not** pass conservation: the coarse energy differs from the refined value by 12.565%. The spatial reconstruction is not converged within the declared
1% gate, and the assigned potential still needs a dispersion-equation consistency check.
The energy values describe this experiment under its declared density, not work extracted
from the player or a closed boat/water system.

At t=16, two perpendicular finite trains reach 0.40861 m combined height versus 0.27087 m
for either isolated train. At 1.5 m grid spacing their summed-field energy is 472055.47 J,
versus 472050.97 J from the independent energies: the 4.51 J cross term is explicitly
included. Reversing one train's phase gives 472046.46 J. Cancellation of coincident
opposite-phase trains and signed addition of rotated finite fields are checked numerically.
Later samples show the two outgoing trains continuing independently. These are useful
interference diagnostics; coarse energy resolution and the prescribed potential preclude
a claim of physically audited crossing conservation.

## Initial strip rejection and historical next task

The tight loop produces 999 invalid cells at t=16 and 1440 at t=32 on the coarse material
mesh; the S turn produces 175 and 790 respectively. Counts depend on sampling density;
the failure criterion is any nonpositive area Jacobian. The plots deliberately omit cells
whose orientation reverses and mark their counts. This omission is a visible diagnostic
hole, not acceptable transport or absorption. Near focusing, heights can grow substantially
(the sampled S-turn peak reaches 0.954 m at t=24). There is no amplitude clamp concealing it.

The model works for a straight train and the outer spreading example, but the single-valued
strip inverse fails at inward focusing/folds. Cell subdivision preserves that singular
geometry and cannot fix it. W1 therefore stops the connected-strip approach before player
integration, as the owning plan requires. At that checkpoint, the next task was to prototype
a linear dispersive heightfield solver against these same four paths and declared energy/volume
gates, then revise W2's representation/source work around the result. The continuation below
completes that isolated fallback and records the new next task. Do not advance W2 by shipping this diagnostic.

## Verification and limits

Numerical checks cover an analytic Fourier-mode energy reference, raster coverage, geometric/phase-preserving subdivision, use of the
stored material budget in reconstruction, balanced compact profiles, phase/group travel,
explicit fold detection, signed finite-front addition and coincident cancellation. CTest
runs these checks when Python 3 is available; they pass while the separate physical gate
remains rejected. The full experiment also writes its failed gates into `measurements.json`.
The experiment does not return a failing process status for an expected physical rejection;
process/test failure denotes a broken diagnostic or failed numerical assertion.

Top-down height/slope PNG sequences and side sections for all four paths were inspected.
Debug configure/build and all 78 CTests passed, including 20 GPU checks with desktop
access and `PROTEUS_GPU_TESTS=ON`; the final sampler guard was rebuilt and the W1 check
repeated. The [owning plan](WAKE_REWRITE_PLAN.md#w1-handoff--2026-10-21) records the sandbox
attempt, full-run command and artifact locations. No runtime
physics, shaders, controls, audio, replay settings or defaults changed. No new manual
boating/playback or user acceptance is claimed. Source work, horizontal orbital velocity,
multiple bands, spatially varying current, shoreline absorption, capacity/retirement,
CPU/GPU integration, performance and browser/Windows verification remain future work.

Continuation checkpoint, 2026-10-21, macOS / Apple Silicon: Debug configure/build and
all 58 display-free CTests passed with `PROTEUS_GPU_TESTS=OFF` before committing the
W1 diagnostic baseline. GPU checks were omitted in this continuation; the earlier
78-test desktop run above remains historical evidence. At this checkpoint the next step
was the heightfield fallback, completed in the continuation below.

## Heightfield fallback — 2026-10-21

The continuation implements a separate linear heightfield diagnostic.
The connected-strip rejection above remains in force. Both experiments stay outside the
player, hull queries, CPU/GPU water rendering and defaults.

Reproduce with Python 3 and its standard library after building `wake_baseline`:

```sh
python3 tools/wake_heightfield_review.py --check
python3 tools/wake_heightfield_review.py --baseline build/wake_baseline --output build/wake-heightfield-final
```

The full run writes `measurements.json` and the same four named PNG/SVG sequences.
The labelled SVG identifies the linear heightfield; the layout, display color scales and
16/24/32 s times match the strip review. These plots sample X/Z from −96 to 94.5 m at
1.5 m spacing, including X=0 in the side section. They crop a larger simulation domain;
the energy audits integrate the entire domain. Plot colors can saturate; simulated height
is neither clamped nor age-faded.

### Equations, source and declared gates

The deep-water linear surface equations are `η_t = |D| ψ`, `ψ_t = −g η`.
For each nonzero Fourier wavenumber magnitude k, evolve the pair exactly:

```text
η(t+Δt) = cos(ωΔt) η(t) + (k/ω) sin(ωΔt) ψ(t)
ψ(t+Δt) = cos(ωΔt) ψ(t) − (g/ω) sin(ωΔt) η(t)
ω = sqrt(g k)
```

The zero mode evolves separately and stays exactly zero for these sources. Energy uses
the same full Hamiltonian as the strip audit, with density 1000 kg/m³. The source-off
propagator has no time integration truncation error; halving its interval is checked via
composition. Source sampling still needs a separate time-refinement study.

Use the continuous W1 stern paths, for 12 s. At midpoint sample times, add a prescribed
surface-potential impulse with temporal weight `−(Ȧ g/ω₀) sin²(πs/12) Δs`, where
`Ȧ = 0.3 m/s` is a prescribed height-rate scale. The potential forcing has units m²/s²
and its integrated impulse has units m²/s; the mean-level gate retains its 0.3 m reference.
Its spatial profile is `−∂²/∂n² / k₀²` applied to a Gaussian times `cos(k₀ n)`;
tangent/normal Gaussian standard deviations are 2.5/5 m and the carrier wavelength is
6 m. Analytic Fourier deposition balances each impulse exactly at k=0 and avoids
spatial splat interpolation. Nyquist lines are omitted symmetrically. This is a finite-band,
bidirectional wave source with smoothly switched forcing, **not** the old strip emission
recipe, a resistance-work-calibrated source, or a validated moving-hull wake pattern.
Source strength and opening pattern remain W2 work.

The field is a 384×384 m **periodic** deep-water domain, with no land, absorption,
retirement, currents or camera. Energy remaining inside a periodic box is conserved even
when waves wrap. Edge-height measurements are diagnostic only; this experiment does not
establish an open-boundary or shoreline solution. The nonlinear steepness/breaking and
reciprocal hull-work limitations of the owning plan still apply.

Declare the same <1% energy-drift/refinement and relative mean-level <0.001 gates before
adoption. Compare source steps 0.4, 0.2 and 0.1 s at 1.5 m grid spacing, then halve the grid
to 0.75 m at fixed 0.1 s source spacing. Also require <1% relative L2 error between
co-located coarse/fine height samples at t=16, so matching scalar energy cannot conceal
field disagreement. At t=16, 31, 46, 61 and 76, inverse-transform height and potential and
independently reconstruct energy using the W1 FFT audit. There is no strip inverse or
front-cell omission at a focus: all grid samples evolve by the same linear equations.

Source work is the actual Hamiltonian change at each potential impulse, including its
cross term with the existing field. Kicks are processed in chronological order across
both crossing sources. Negative work is allowed and reported; the prescribed forcing can
remove energy. This closes the diagnostic source ledger, not an engine/hull energy budget.
Two independent trains add signed height **and** potential; their combined Hamiltonian
includes the interference cross term. Coincident opposite phases cancel.

A separate one-direction finite-band packet checks group travel from its envelope-intensity
centroid and measures its RMS widening over 60 s. A traveling Fourier mode checks carrier
phase/sign against its analytic field; uniform translation preserves the Hamiltonian.
Numerical tests also cover inverse reconstruction, forward/backward and composed propagation,
time derivatives, work closure, exact zero mean and two-source signed addition.

### Measured decision and next task

All declared heightfield diagnostic gates pass. Fine-grid source-off energies below use
0.75 m spacing and 0.1 s source steps. Errors are **fractions**, not percentages; source
refinement compares 0.2/0.1 s, grid refinement compares 1.5/0.75 m at fixed 0.1 s.

| Path | Energy at t=12 (J) | Grid energy error | Height L2 grid error at t=16 | Source energy error |
| --- | --- | --- | --- | --- |
| Straight | 18676.07297 | 1.02e−12 | 1.27e−6 | 4.32e−9 |
| Tight loop | 22044.18588 | 4.48e−10 | 1.97e−5 | 1.84e−8 |
| S turn | 21130.37409 | 5.50e−12 | 2.51e−6 | 2.90e−9 |
| Crossing | 37352.14596 | 1.02e−12 | 1.27e−6 | 4.32e−9 |

Across all paths/resolutions, reconstructed source-off energy differs from its t=12
Hamiltonian by at most 3.78e−14 relative over the sampled interval through t=76.
This roundoff-scale drift is expected for an exact modal propagator; it is supported by
the independently inverse-transformed/FFT-reconstructed energy and spatial field study.
The largest absolute full-domain mean height is 8.94e−21 m. Fine-grid source-work closure
is within 6.78e−15 relative. Straight forcing removes 0.08497 J in its negative-work kicks;
the crossing removes 0.16993 J. These are included in net injected work, not hidden sinks.
The largest fine-grid sampled edge height is 1.52e−6 m; this small value is not a proof
of an open-domain limit or island occlusion. No domain-doubling study was performed.

The packet probe's intensity RMS width grows from 3.92837 to 7.00338 m over 60 s while
its envelope peak decreases. Its centroid moves at 1.53911808676 m/s, versus the
band-weighted group reference 1.53911808678 m/s (error 1.22e−11 m/s). The carrier reference
remains 3.060696 m/s, with group speed 1.530348 m/s at the central wavelength; a finite
band's centroid need not travel at that single central value. Envelope-intensity width
is this probe's measure, not a claim of full two-dimensional energy spreading.

For the crossing, independent train energies sum to 37352.1459357 J and the combined
field has 37352.1459563 J, including a +2.06e−5 J interference term. Reversing one source
phase gives 37352.1459151 J. At t=16 the combined peak is 0.13622 m versus 0.07687 m for
one straight train. The subsequent fields retain both outgoing trains. The near-zero
*global* cross term in this orthogonal-source example does not imply weak local interference;
coincident opposite-phase cancellation and signed field addition are separate numerical
checks. Peak readings and cross terms depend on the uncalibrated source and periodic box.

**Decision:** retain the linear height/potential field as the W2 propagation direction.
The next task is **W2.1: an owned C field with safe snapshots, explicit energy accounting,
and boundary/island proof**, followed by tick-source calibration and consistent queries.
The [revised W2 steps](WAKE_REWRITE_PLAN.md#heightfield-direction-after-w1) own that work.
The strip remains rejected. No replacement is enabled for play.

### Continuation verification and limits

2026-10-21, macOS / Apple Silicon: Debug configure/build and all **59 CTests** passed with
`PROTEUS_GPU_TESTS=OFF` (`ctest --test-dir build --output-on-failure -j 4`). This includes
both wake diagnostics. The final full heightfield review above completed, wrote an empty
`failed_gates` list, and generated `build/wake-heightfield-final/measurements.json` plus
four PNG/SVG sequences; the log is `build/wake-heightfield-final.log`. All four final PNG
height/slope/side-section sequences were inspected. Changed-document local file links and
`git diff --check` passed. The plots are model diagnostics, not new runtime captures.

GPU checks were omitted. No runtime motion, input, audio, asset loading or defaults changed,
so no new manual boating/playback or user acceptance is claimed. Source-time **energy**
convergence was measured; source-time field convergence, source pattern/work calibration,
finite-depth/steep-wave validity, nonperiodic/domain convergence, absorption/islands, resource
stress, reciprocal coupling, native CPU/GPU performance, renderer integration and other
platforms remain unverified. The uniform-current numerical probe checks a common Fourier
translation phase and energy; full advected-emitter/current integration remains W2.
