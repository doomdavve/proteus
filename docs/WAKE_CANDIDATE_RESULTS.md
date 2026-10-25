# Native basin wake candidate

2026-10-21, macOS / Apple M1 Max / Apple clang. Owner:
[wake rewrite plan](WAKE_REWRITE_PLAN.md). **Opt-in review candidate, not an accepted
replacement. D3 remains the default. The 2 ms per rendered 60 Hz frame target fails.**
The user found Debug+AA too slow, reported better performance with the optimized build,
and found both the initial and intermediate sources too weak. **Source revision 3** below
provides a substantially larger ramp. The user subsequently reported that it looks and
feels much better, and proposed improving the water/ocean appearance as a separate track.
The window identifies this build with `WAKE FIELD / REVIEW 3`. This is positive play feedback,
not a request to switch the default or acceptance of every scenario/platform.

Subsequent performance follow-up: the user likes the basin results as a foundation but
requires much faster wakes over arbitrarily large water spaces. Real DFT/cache optimization
reduces measured full-basin physics from 8.91 to 6.31 ms/frame; the 2 ms target still fails.
[Scaling results](WAKE_SCALING_RESULTS.md) supersede the integration-checkpoint timings and
storage below and define the next large-water experiment. No simulation distance is implemented.

## Scope and play recipe

```sh
cmake -S . -B build/review -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/review --target wake_velocity -j
./build/review/wake_velocity --wake-field --profiler
```

Use the native GPU renderer without AA initially. The CPU renderer is a deterministic
capture reference, not the recommended interactive path. Debug builds retain the same
physics but are substantially slower and print a launch notice. `--gpu-aa` renders
1280x800 instead of 320x200, sixteen times as many pixels. `--demo` drives a repeatable accelerating loop;
`R` retires the remaining field energy and restarts. The window labels the candidate,
F3 shows frame/tick costs, and exit prints field timings, storage and the energy ledger.

Drive straight, turn back across the wake after 10–30 s, make tight and wide loops,
cross obliquely, reverse, stop and coast through the remaining trains. Judge ramp visibility,
crossing/jump strength, reinforcement/cancellation, landing control, persistent waves after
foam fades, and frame pacing. These judgments and the default switch belong to the user.

This candidate covers the **entire existing basin**, with a fixed 288x192 m field around
its 240x160 m navigable area. It rejects mapped locations, authored currents, the old
scripted crossing source, course/replay/ghost modes, and browser launches. `--waves`
can still add the existing ambient waves; source calibration and conservation measurements
below use still water. There is no moving simulation window, camera-driven deletion,
age fade, overwrite, or hidden quality fallback.

## Operator, ownership and cost reduction

On an entirely wet rectangle, the water-only Neumann axis graphs share a cosine basis.
The eighth-order spatial operator and its positive order-256 polynomial are therefore
diagonal in that basis. `boat_wake_use_cosine()` evaluates **the same symbol**, including
the zero constant mode and polynomial tail. This is not a periodic FFT or a lower-degree
dispersion approximation. Spacing stays .75 m and the gameplay field stays at 120 Hz.
No lower-cadence temporal interpolation or prediction is used.

The implementation contains a portable double mixed-radix transform (factors 2 and 3)
and uses double Accelerate DFT plans on macOS when available. Land masks are explicitly
refused by this fast path. The original graph evaluator and independent scalar evaluator
remain available; thin barriers and islands retain their original connectivity. This does
not solve fast mapped-water gameplay. The general query API also refuses interpolation
stencils that touch land, rather than interpolating through an island.

The cosine forced step reuses endpoint `A psi` for queries, energy audits and work:

```
first half work / (rho dx²) = .5 deltaᵀ A psi_before + .125 deltaᵀ A delta
last half work / (rho dx²)  = .5 deltaᵀ A psi_after  - .125 deltaᵀ A delta
```

The second endpoint already includes the last half impulse. Parseval computes the source
self term with only a forward transform. Both signed half events remain separately booked;
positive work, negative work, absorption, explicit retirement and measured numerical
residual are distinct. The audit reconstructs the Hamiltonian from state; it does not
declare conservation by assigning retained energy from the ledger.

The application owns `BoatWakePlayer` and its field separately from copyable `BoatWater`.
`Boat.wake_surface` is an explicitly borrowed, read-only endpoint, valid until the owning
world advances or is destroyed; it is not a separately owned boat snapshot. A field clone
is deep, including transform plans and all mutable state. Course/replay paths cannot use
the candidate. Reset retires the actual retained Hamiltonian and preserves cumulative
accounting. No actor frame contains a pointer into the mutable field: the renderer owns
one uploaded endpoint for the entire draw.

## Matched numerical and boundary evidence

- Forced cosine/graph state comparisons at 24x16, 192x128 and 384x256 cells differ by at
  most `7.93e-15` relative L2. Work, retained energy, absorption, snapshots and reset are
  compared as well. This includes the optimized signed source-work path.
- The original six 80 s runs were repeated at .75 m / spatial order 8 / polynomial 256 /
  120 Hz. Open rectangles use cosine evaluation; islands/walls retain the graph backend.
  All original gates pass. Maximum sampled 144 m versus 288 m central difference is
  **0.68924%**; disconnected transmission remains exactly zero. Artifacts:
  `build/wake-cosine-domain/`.
- The larger gameplay basin has absorption beginning 10.5 m before the visible banks,
  rising quadratically to 2/s at the bank and continuing through the exterior. No
  potential damping mask is introduced: eta damping remains a nonnegative energy sink.
  A central packet plus a packet beside the bank is compared against a 576x384 m domain,
  at the same spacing/operator/timestep and the same world-space absorption.
- Sampled central and bank-region differences at 12, 40, 80 and 160 s are at most
  **0.03615%**, normalized by the initial Hamiltonian-equivalent height norm. The initial
  80 s basin run retained 14.06% energy: outgoing waves were still in transit in this
  larger basin. It was not an absorption pass. At 160 s retained energy is **0.02964%**,
  absorption **99.97890%**, and the separately measured residual **0.00854%**. The doubled
  domain retains 0.02940%. The original smaller-domain 80 s thresholds were not weakened.
  Artifact: `build/wake-candidate-domain.log`.

The all-wet exterior is an absorbing continuation of the rectangular basin, not navigable
water beyond its banks. These comparisons demonstrate the chosen absorbing boundary;
they do not claim shore reflection, arbitrary island geometry on the cosine backend,
arbitrary unresolved long waves, or an infinitely powered closed mechanical system.

## Wetted stern source and units

The handling model uses acceleration and torque per unit mass. This candidate declares an
**effective mass of 2000 kg**, density **1000 kg/m³**, metres and seconds. These are explicit
model units, not a measured displacement of the illustrated boat. Existing longitudinal
drag times along speed supplies the *available* resistance work in joules. No extra drag
is applied. The actual wave work is the signed Hamiltonian change and is reported separately.

Each fixed hull tick records the transformed stern position, attachment velocity including
yaw/pitch/roll, and the two actual aft support loads. Their clamped support fraction gates
the source. Dry flight is exactly zero. Two consecutive endpoint records supply midpoint
pressure quadrature; the source follows turns and reverse motion without reusing a single
stern offset across a long emission segment.

The source is a moving pressure dipole with Gaussian width 1.5 m, truncated at a 7.5 m
square footprint. Its local weighted mean is removed to balance the discretized source.
With stern-relative speed `v`, existing longitudinal resistance `a_drag`, wetted fraction
`w`, and `sigma=1.5`, the pressure-potential scale is:

```
u = clamp((v - 6) / 16, 0, 1)
gain = 8 min(v / 3, 4) [1 + 2 u⁴ (5 - 4u)]
psi_t scale = gain * 2000 * a_drag * w / (1000 * 2 pi sigma²)
```

This is an explicitly calibrated pressure surrogate, not a hull-panel solution or a fixed
V-shaped wake recipe. Its stationary/moving response follows the field dispersion. It
contains smooth spectral tails outside 3–12 m; that range is the declared useful numerical
band, not a claim of an exactly band-limited compact source. The gain is continuous at
zero speed, time integration uses duration rather than sample count, and no overlap
normalizer or amplitude clipping changes signed superposition.

Six-second source tests cover straight 6, 12, 18, 20, 22 and 24 m/s runs, acceleration, turns, reverse,
smooth stopping with a free tail, fully dry flight and varying wetted load. Against
240 Hz source/field integration, the 120 Hz path has at most **0.02091% height error**,
**0.05479% combined velocity error**, **0.02769% slope error**, and **0.01934% signed-work
error**. Positive source work in these tests is below 89% of the measured existing drag
work. This is a calibration result for these paths, not an enforced universal power cap.
Strong incoming waves can exchange signed work with the prescribed pressure source.

The integrated boat additionally records its actual fixed-tick stern history and replays
each interval as two half intervals at 240 Hz: combined height/velocity L2 difference
**0.05727%**, signed-work difference **0.01447%**. The integrated loop contains negative
work as well as positive work. Independent crossed-mode tests verify signed superposition,
zero mean, free propagation and a deliberately negative cancellation kick. Hull encounters
remain one-way: jumping does not debit wave energy. No closed boat-plus-water energy
conservation, breaking-wave physics or unlimited steepness is claimed.

The initial full-throttle straight trial produced only a 9.19 cm nodal peak. An intermediate
32.41 cm revision remained too small in user driving. Revision 3 explicitly recalibrates the
effective hull from 650 to 2000 kg and delays the high-speed pressure rise, rather than hiding
extra source work or scaling only the rendered surface. The normalized handling model retains
its calm-water acceleration; all force-to-joule conversions use the new effective mass.
This is a gameplay unit calibration, not a measurement of the illustrated hull's displacement.
The late rise, with zero endpoint derivatives, keeps the tested intermediate-speed wave work
inside the existing resistance work as well.

The eight-second actual full-throttle straight run now reaches 24.30 m/s with an **86.25 cm
peak**, slope 0.547, and 1.090 MJ positive work against 2.454 MJ available resistance work
(44.4%). A second boat then crosses the freely propagating trail at right angles, entering
at 16 m/s. Compared with an identical calm-field twin using the same buoyancy/damping path,
its peak differences are **1.232 m hull height, 3.998 m/s vertical velocity and 16.31 degrees
pitch**. Its stern is fully dry for 72 ticks. There is no added jump impulse. After the crossing,
six seconds easing to neutral and recovering in the basin remain finite and settle to within
0.15 m of water and 0.3 m/s vertical speed; the observed final differences are about 4 micrometres
and 0.0012 m/s. This recovery includes the basin's existing bank contact behavior.

Source resampling and signed accounting are rerun for this stronger calibration. Numerical
accuracy and boundary thresholds are unchanged. These steep gameplay waves exceed a strict
small-amplitude interpretation of linear water: nonlinear dispersion, breaking and reciprocal
hull/wave work are **not modeled or physically validated**. The measured response is the
existing one-way hull interaction with this coherent field. User judgment of ramp strength,
repeated crossings, surfing and landing control remains essential.

## One query and render state

Every 120 Hz endpoint publishes eta, psi and `A psi`. Height and both gradients use a
degree-eleven tensor Lagrange reconstruction. Horizontal surface velocity is the derivative
of that same potential reconstruction; vertical velocity is the same-state
`A psi - absorption_rate * eta`. The latter is the semidiscrete field derivative; the
fixed-step damping split retains its measured time error. The six hull supports and stern
queries consume the prior published endpoint during the hull tick; the field then advances
with the measured stern interval and publishes the endpoint used for rendering. This is
explicit sequential hull/field coupling, without hidden presentation prediction.

The shorter degree-seven interpolator was rejected: at 3 m its height and gradient errors
were 1.58% and 3.03%. The selected interpolator's manufactured-mode RMS errors are:

| Wavelength | Height | Gradient / horizontal velocity |
| --- | --- | --- |
| 3 m | 0.33281% | 0.63737% |
| 6 m | 0.0001693% | 0.0006771% |
| 12 m | <0.000001% | <0.000001% |

Analytic derivatives also match finite differences of the actual interpolant. Height is
continuous across cell joins; this local Lagrange reconstruction does not promise exact
gradient continuity at a knot. It passes the declared query accuracy, including the shortest
useful wavelength. No independently interpolated velocity clock is used.

GPU upload is one 384x256 R32F height endpoint (384 KiB). Vertex and fragment shader probes
execute the actual production reconstruction and compare it with the CPU sampler. A static
full-basin .75 m mesh is split into two indexed strips to respect 16-bit mesh indices.
Interior vertices coincide with field cell centres and take one height fetch; fragments
use the full matching interpolant. There is no distance fade or moving patch in this path.
The displayed mesh remains piecewise planar between .75 m vertices. The sampler error
figures above do not promise 1% geometric accuracy between triangle vertices, particularly
for a manufactured 3 m wave. Tessellation and surface appearance can be reviewed separately
while retaining the physical field and query contract.

## Complete runtime and storage — integration checkpoint 8eec6cc

The following measurements describe the initial integrated candidate. The subsequent
[performance follow-up](WAKE_SCALING_RESULTS.md#measured-cost) owns current timings and storage.

The full-basin field exceeds the provisional **2 ms/frame target**. Do not substitute an
isolated 144 m operator timing for the complete candidate cost. `wake_play_review --gpu` measures
two real hull/field ticks per rendered frame, actual support/stern/particle queries, source
construction, signed work and independent audit, height upload, the full scene and `glFinish`.
It reports a same-pose rest-water renderer baseline separately. Setup/allocation is outside
the steady-state timing. The paired CPU/direct-GPU tool uses the same 144x96 m open geometry,
forcing, cadence, source construction, six queries, audit, visualization and synchronization;
the CPU also pays the prototype's unnecessary source upload, conservatively.

Apple M1 Max, macOS 26.6.2, Apple clang, `RelWithDebInfo`, 120 measured frames after nine
simulated seconds of actual hull/stern warmup, two fixed ticks per frame:

| Complete integrated workload | No AA, 320x200 | AA, 1280x800 then downsample |
| --- | ---: | ---: |
| Hull, all physics queries, source, field, endpoint and energy audit | 8.90 ms/frame | 8.91 ms/frame |
| Whole renderer, height upload and GPU completion | 5.51 ms/frame | 7.55 ms/frame |
| Combined measured cost | **14.41 ms/frame** | **16.46 ms/frame** |
| Same-pose rest-water renderer | 5.89 ms/frame | 6.23 ms/frame |

Within the first row, source construction is 0.021 ms/frame, field/source integration
8.176 ms/frame, and remaining endpoint preparation/independent audit 0.430 ms/frame.
Forced steps publish their endpoint inside the integration timing; dry/free steps publish
it in the preparation/audit timing. All actual hull,
stern and particle queries are included in the total; particle-pose queries during drawing
are included in the renderer row. The scripted application reports about 0.079 ms/frame
for height copy/upload alone. Rendering timings vary: another user-launched candidate window
was open during the final benchmark, so these are **observed desktop costs, not an exclusive
device guarantee**. The small negative difference between the no-AA renderer and its flat
baseline is timing variation, not negative wake cost. Field work alone fails the target;
subtracting noisy renderer baselines cannot turn this into a pass. Presentation, frame limiting
and audio are outside the benchmark, and covered only by the scripted application smoke run.

The 144x96 m matched backend experiment at 120 Hz measures **1.416 ms/update** for CPU cosine
versus **32.813 ms/update** for the direct fragment-pass GPU (2.832 versus 65.626 ms per 60 Hz
frame). Both include source construction, update, queries, ledger, state access, diagnostic
rendering and completion. This smaller diagnostic does not cover the full gameplay basin.
Its 30/60 Hz timings are cost observations only; they are not a cadence acceptance.

Instruments Time Profiler recorded a 600-frame scripted desktop run. Of 8.455 s sampled CPU,
6.557 s included `boat_wake_player_step`, 5.404 s included `cosine_line`, and 2.284 s were
self samples in `cosine_line`. This led to removing redundant interleaved/split copies around
Accelerate; the final evaluator retains identical graph-comparison results. The full physics
row fell from approximately 10.5 to 9.2 ms/frame before the final source revision. Revision 3
measures 8.9 ms/frame in this loop, which includes dry intervals and fewer source transforms.
Indexed full-basin mesh
strips, direct nodal vertex fetches and linear-time interpolation-weight construction also
reduce render cost without changing the field or query polynomial. The old GPU operator
is not pursued for gameplay: it is much slower and requires synchronized query readback.

Owned world allocations are **8,962,736 bytes** on this build, excluding allocator overhead
and opaque Accelerate plan allocations. The GPU path adds 393,216-byte CPU upload and GPU
texture buffers, and 1,666,668 bytes each of CPU mesh storage and GPU vertex/index buffers.
Total explicitly counted candidate storage is **13,082,504 bytes** (12.48 MiB), excluding
existing renderer targets, driver overhead and native FFT plans. No per-tick allocation or
unbounded history is used. The CPU capture mesh has separate, larger cached storage and is
not allocated by the normal GPU path.

The portable cosine backend is numerically tested on this Mac; that is not a Windows
performance measurement. Browser candidate launches are refused and its sampler is removed
from generated WebGL shaders. No mapped-world, browser candidate or Windows playability is
claimed. The old direct fragment-pass wave solver remains an isolated diagnostic; its many
sequential passes and query synchronization make it an inferior route for this candidate.

## Reproduction and verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DPROTEUS_GPU_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

cmake -S . -B build/review -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/review -j
./build/review/wake_candidate_review
./build/review/wake_candidate_review --source
./build/review/wake_candidate_review --basin-domain
python3 tools/wake_domain_review.py --executable build/review/wake_field_review --cosine --output build/wake-cosine-domain --jobs 2
./build/review/wake_play_review
./build/review/wake_play_review --crossing
./build/review/wake_play_review --gpu
./build/review/wake_play_review --gpu-aa
./build/review/wake_gpu_runtime_review
./build/review/wake_velocity --wake-field --renderer cpu --time 9 --screenshot build/wake-field-cpu-9.png
./build/review/wake_velocity --wake-field --time 18 --screenshot build/wake-field-gpu-18.png
```

The domain runs are deliberately separate from the CTest suite. GPU tools need a native
graphics context. On 2026-10-21, final Debug configure/build and **all 91 CTests passed,
including 22 GPU checks** (410.52 s, `PROTEUS_GPU_TESTS=ON`). Project C17 sources are
warning-clean; a full dependency rebuild also emitted three warnings in pinned raylib's
CoreMIDI/stb_vorbis includes, outside the changed project sources. The final incremental
native and web builds emitted no warnings. The native and portable backend/query checks pass;
portable C17 ASan+UBSan reports
no errors. The stronger source's twelve moving-path cases, actual-stern resampling, physical
crossing/recovery comparison and production VS/FS query probes pass (GPU L2 `3.07e-6`). CPU at 9 s
and GPU at 9/18 s captures were inspected, along with the domain/island comparison frames.
The 900-frame revision-3 desktop demo completed with 1,922 real hull/field ticks, finite state and a
distinct signed/sink/residual ledger. The Emscripten WebGL baseline builds with warning-clean project sources
(3,962,916 bytes before HTTP compression); local headless Chrome/SwiftShader launch, focus,
input and help checks pass without JavaScript or console errors. This is baseline browser
verification, not browser candidate performance or audio-listening acceptance.

Artifacts are under `build/`: `wake-ctest-final.log`, `wake-source-large.log`,
`wake-crossing-before.log`, `wake-crossing-after.log`, `wake-crossing-large.log`, `wake-play-source-large.log`,
`wake-play-release.log`, `wake-play-release-aa.log`, `wake-backends-release.log`,
`wake-candidate.trace`, `wake-profile-summary.txt`, `wake-candidate-sanitized.log`,
`wake-field-cpu-9.png`, `wake-field-gpu-9.png`, `wake-field-gpu-18.png`,
`wake-desktop-smoke.log`, `wake-browser-check.log`, and `wake-web-default.png`.
The agent performed scripted checks, not hands-on play testing. User feedback is recorded
above; prolonged/scenario-complete driving, broader landing feel, hearing audio playback,
other-device performance and default-switch acceptance remain unverified. The separate
[water appearance follow-up](WAKE_REWRITE_PLAN.md#water-appearance-follow-up--2026-10-21)
preserves the revision-3 physical field while reviewing the rendering.
