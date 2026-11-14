# Wake runtime feasibility checkpoint (historical at 6deaba2)

Historical record: the physical wake-field experiment and its review tools were retired on 2026-11-14. Commands below describe the earlier revision and are no longer runnable on current source.

2026-10-21, macOS / Apple M1 Max, native Apple clang. Owner:
[wake rewrite plan](WAKE_REWRITE_PLAN.md). Previous numerical baseline:
[W2.1 field/domain proof](WAKE_W2_FIELD_RESULTS.md).

Superseded runtime selection: the [native basin candidate](WAKE_CANDIDATE_RESULTS.md) now
uses exact rectangular cosine evaluation and is integrated behind `--wake-field`. The
measurements below describe the original graph/direct-GPU checkpoint, not the current
candidate's total cost. At that checkpoint D3 remained default and the 2 ms integrated target failed.
[Arcade wake objects](WAKE_OBJECT_RESULTS.md) now provide the free-boating default.

**Decision: the current order-256 operator fails the provisional 2 ms per rendered
60 Hz frame budget on both CPU and the direct fragment-shader GPU prototype.**
Keep the owned C field as a numerical reference. Revisit the operator approximation
before selecting a runtime backend or integrating the player. Do not solve this by
shrinking the already-proved domain or deleting off-camera state. The user authorized
this feasibility investigation; no gameplay or visual acceptance is implied.

60 Hz water updates pass the limited temporal experiment below; 30 Hz does not meet
its 1% velocity threshold. This supports further investigation at 60 Hz, not a runtime
cadence/default change. The D3 player, water queries, rendering and audio are unchanged.

## Source work and bounded ownership

boat_wake.c now uses symmetry of the same positive operator A:

`delta H = rho dx^2 (psi + delta_psi/2)^T A delta_psi`.

A prescribed kick takes one operator evaluation instead of subtracting two full
Hamiltonians. This retains the signed interference cross term and avoids subtracting
unrelated retained height energy. Zero impulses validate and return without an operator.

`boat_wake_step_with_impulse()` accepts the total prescribed interval impulse, using
midpoint source quadrature and half injections before and after the existing field step.
It caches A delta_psi across that step and books each half's signed work separately.
This takes two operators total, rather than the three needed by two separate optimized
half kicks plus a field step. Sum simultaneous emitters into a common impulse array;
the ledger then describes each combined half event, not separate emitter attribution.
Actual stern history, engine work and source calibration remain W2.2 work.

The cached source operator is one additional owned double array. At 192x128 cells,
owned storage is **1,871,184 bytes** including the owner, excluding allocator overhead,
caller source/history/query buffers and renderer resources. Reset/clone/destroy retain
owner semantics; scratch is recomputed after cloning. Invalid impulse validation precedes
mutation. Energy scaling also avoids intermediate overflow for extreme valid units, including
zero damping loss at very large density. The numerical grid/order, boundaries, damping and
fixed timestep are unchanged.

Independent checks compare direct Hamiltonian differences with source work, fused steps
with separate half kicks (all ledger channels), damped/land cases, snapshots, zero and
invalid impulses, and a constant-potential gauge plus a small modal kick.

## Temporal convergence

CPU diagnostic, `--check`: actual C fields on a
24x16 grid at .75 m, spatial order 8, polynomial order 256; 16 seconds of smooth
prescribed moving/turning/stopping forcing projected onto five zero-mean Neumann modes
(wavelengths include 3–12 m). The reference independently integrates the modal forced
ODEs using RK4 at 1920 Hz. It uses the same discrete spatial symbol to isolate time error.

Sample/interpolate endpoint eta and psi at 120 Hz. Reconstruct eta_t = A psi by modal
projection of that same interpolated potential; compare all grid cells throughout the
run, including the stopped source's free tail. Relative space/time L2 errors:

| Water Hz | Height eta | Potential psi | Vertical velocity eta_t | All below 1% |
| --- | --- | --- | --- | --- |
| 30 | 1.0413% | .8583% | 2.0016% | No |
| 60 | .2598% | .2139% | .4995% | Yes |
| 120 | .0648% | .0528% | .1243% | Yes |

Errors decrease approximately fourfold when doubling cadence. This is an interpolation
and source-quadrature diagnostic, not hull/source convergence or the W2.3 query contract.
Bracketing endpoints require a presentation delay of up to one water interval unless
prediction is separately validated. The derivative of linearly interpolated height is
not exactly A psi; consistent interpolation of height and velocity still needs design.
No islands, absorbing shores, currents, actual stern forcing or human motion review are
covered by this temporal test. The earlier 80 s domain proof remains at 120 Hz.

## Measured native workload

CPU: Apple clang `-O2`, 192x128 cells (144x96 m), .75 m, spatial order 8,
polynomial order 256, nonzero field/source, four warm updates then 24 timed updates.
Timings use process CPU time. Fused symmetric source/field step, six raw cell queries,
and a full Hamiltonian audit on every update:

| Water Hz | CPU ms/update | CPU ms per rendered 60 Hz frame |
| --- | --- | --- |
| 30 | 28.12 | 14.06 |
| 60 | 28.02 | 28.02 |
| 120 | 28.07 | 56.15 |

The audit adds a third operator, also a conservative cost proxy for exact endpoint A psi
queries. CPU gradient reconstruction, interpolation, upload, rendering, source-history
construction and real hull integration are **not** included. Omitting the audit leaves
about 18.7 ms/update for source plus stepping; even at 30 Hz that averages about
9.4 ms per 60 Hz frame. The proposed 2 ms frame budget fails before those missing costs.
An average per-frame projection does not remove individual long-update stalls.

The standalone kick is **9.41 ms**, versus **18.79 ms** in the previous measured baseline;
the plain field step remains **9.31 ms**. Compiler/OS variation applies to these short
runs. No browser/Windows or worst-case guarantee is claimed.

## Direct GPU experiment

GPU diagnostic: isolated desktop OpenGL 3.3,
RGBA32F textures, the same mirrored water-only neighbors and order-256 factored
Clenshaw recurrence. No compute shaders or graphics-backend change. The shader port is
not wired into the demo or web packaging.

The host is compiled with Apple clang `-O2`; raylib/context setup uses the existing
Debug library, outside timing. Four warm updates, twelve timed updates per cadence,
on the M1 Max. Wall time includes source construction/upload, symmetric source/field
update, six batched height/eta_t/gradient/potential/horizontal-velocity queries, GPU
Hamiltonian/work/absorption reductions with small synchronous readbacks, and a simple
320x200 field visualization with `glFinish()` before ending the update.

| Water Hz | GPU pipeline wall ms/update | Wall ms per rendered 60 Hz frame |
| --- | --- | --- |
| 30 | 32.36 | 16.18 |
| 60 | 32.25 | 32.25 |
| 120 | 32.14 | 64.28 |

Three operator evaluations require **771 sequential fullscreen draws**; queries, state
updates, ledger reductions and visualization bring the total to 786 draws/update.
The port does not meet the budget. This does not reject GPU wave simulation generally:
a better operator, fewer passes, packed operations and different synchronization could
behave differently. These measurements are not a full waterfront rendering benchmark.

Float GPU state is compared against the double C oracle after 32 forced 60 Hz steps on
open, finite-island and full-wall geometries. Combined eta/psi L2 error is at most
1.36e-5, six-query L2 error at most 7.51e-5, retained/work relative error below 2.51e-7;
the full wall's far side is exactly zero. Near-boundary forcing exercises nontrivial
absorption; its relative error is 1.39e-6 in the open case. Island/wall absorption is very
small and has .31–.41% relative error; checks use .5% plus 1e-6 of injected work as an
absolute tolerance. These are short arithmetic equivalence checks, not the C backend's
80 s domain proof or long-duration FP32 acceptance. The prototype accumulates total
signed work/absorption; production positive/negative work classification, safe snapshots,
reset/retirement and capability fallback are not implemented on GPU.

## Reproduce

Normal Debug configure/build and CPU tests:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Optimized CPU measurements on this native compiler:

```sh
clang -std=c17 -O2 -Wall -Wextra -Wpedantic -Isrc tools/wake_runtime_review.c src/core/boat_wake.c -o build/wake_runtime_review_optimized
./build/wake_runtime_review_optimized --check > build/wake-runtime-cadence.json
./build/wake_runtime_review_optimized --costs > build/wake-runtime-cpu-costs.json
clang -std=c17 -O2 -Isrc tools/wake_field_review.c src/core/boat_wake.c -o build/wake_field_review_optimized
./build/wake_field_review_optimized --costs-fast > build/wake-runtime-costs.json
```

The GPU tool requires a graphics context. Its optimized host command below is macOS
specific; use CMake's target/link settings on other platforms. Numerical check exit status
reports correctness, not performance acceptance. GPU CTest registration is conditional
on `PROTEUS_GPU_TESTS=ON`; it is not part of the default CPU suite.

```sh
clang -std=c17 -O2 -Wall -Wextra -Wpedantic -Isrc -Ibuild/_deps/raylib-src/src tools/wake_gpu_runtime_review.c src/core/boat_wake.c build/_deps/raylib-build/raylib/libraylib.a -framework OpenGL -framework Cocoa -framework IOKit -framework CoreFoundation -o build/wake_gpu_runtime_review_optimized
./build/wake_gpu_runtime_review_optimized > build/wake-runtime-gpu-optimized.json
./build/wake_gpu_runtime_review --check
./build/wake_gpu_runtime_review --capture build/wake-runtime-gpu.png
```

## Verification performed

2026-10-21, macOS / Apple Silicon:

- Debug configure and full build completed with warning-clean C compilation.
- All 62 default CPU CTests passed (275.73 s). The three wake CTests were repeated after the final
  energy-scaling guard and passed (43.49 s); GPU checks were run separately.
- AddressSanitizer/UndefinedBehaviorSanitizer field/source checks passed after the final
  guard. The cadence diagnostic also passed under both sanitizers.
- The final native GPU `--check` passed all three geometry comparisons against the rebuilt
  C oracle. The optimized GPU benchmark passed its numerical checks and failed the
  performance target as reported above.
- Captured and inspected `build/wake-runtime-gpu.png`: the amplified field visualization
  contains the expected compact signed disturbance. This is a diagnostic image, not
  waterfront appearance or human play acceptance.
- Formatting, changed local documentation links and `git diff --check` passed.

No manual boating demo was run: the isolated tools and field are not connected to player
motion, input, rendering or audio. No 80 s GPU proof, browser or Windows run was performed.

## Next work and verification limits

Next: investigate an operator with substantially fewer sequential stages, using the
current C evaluator as the oracle. Compare accuracy at the declared wavelength range,
energy positivity/mean preservation and island blocking, then repeat the complete cost
checkpoint. Rational approximations/solvers or a deliberately narrower wave band are
candidates, not selected implementations. Coarse distant regions need a proved coupling;
field retirement must remain explicit in the ledger. Do not revisit a smaller domain
without repeating the leave/return proof.

W2.2 source calibration and W2.3 queries remain pending. W3 retains integrated
history/query/upload/render, storage and platform acceptance. The 2 ms/frame target is a
provisional native checkpoint, not a measured supported-device guarantee or user-approved
quality tradeoff. Browser, Windows, long-running GPU conservation and manual boating
playback have not been checked for this isolated experiment.
