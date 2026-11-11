# W2.1 owned-field and boundary prototype

2026-10-21, macOS / Apple Silicon. Owner: [wake rewrite plan](WAKE_REWRITE_PLAN.md).
Historical baseline: commit `78ad300`, before the polynomial-tail/higher-order refinement.
Current evidence and next task: [runtime feasibility checkpoint](WAKE_RUNTIME_RESULTS.md).
The equations and measurements below describe that initial baseline; the commands show
the workflow and reproduce those exact numbers only at that commit.

Implementation: [boat_wake.h](../src/core/boat_wake.h), [boat_wake.c](../src/core/boat_wake.c),
[validation tool](../tests/core/validate_boat_wake.c).

The isolated C owner, signed energy ledger, snapshots and boundary/island fixtures are
implemented. The existing D3 player/runtime is unchanged. **At this initial checkpoint, the solver/domain selection
part of W2.1 remained open:** this land-aware candidate is slower and less spatially accurate
than W1's periodic Fourier diagnostic. Do not advance to a player switch or describe these
fixtures as validated harbor propagation. The initial next step was W2.1b domain/order/grid refinement. Its
[first continuation](WAKE_W2_REFINEMENT_RESULTS.md) recorded passing modal/coarse-domain checks
and a remaining cost/domain gate. The [matched-accuracy proof](WAKE_W2_FIELD_RESULTS.md)
subsequently passed; the [runtime checkpoint](WAKE_RUNTIME_RESULTS.md) now requires a cheaper
operator before source/player integration. No runtime backend is accepted.

## Representation and ownership

An opaque `BoatWake` owns height eta, potential psi, copied immutable land/rate maps and
scratch storage. Create/clone return NULL on failure; destroy accepts NULL. Clone deep-copies
state, topology, absorption and the cumulative ledger. There are no pointers in `BoatWater`
and no new shared globals. Sample is read-only. Reset records remaining Hamiltonian energy
as retired before clearing eta/psi and restarting the integer tick; cumulative work, sinks
and residual remain available. Destroy releases the owner; callers needing a final audit
must read the ledger (or reset and read it) before destroy. Dynamic topology/domain movement
and portable serialized snapshots are outside this prototype.

Time is a fixed double dt plus uint64 tick, with no absolute float phase. Config checks reject
invalid sizes, rates and unstable steps; impulse checks reject nonfinite/overflowing entries
or land forcing before mutating any state. Allocation failures unwind owned storage. A clone
survives destruction/reset of the original and reproduces subsequent steps exactly.

The water-only cardinal graph has positive Laplacian L with no periodic edges and no edge
through land. Approximate sqrt(L) using the positive Chebyshev sum

```text
a = 8 / dx^2; B = I - 2 L/a
A = sum(j=1..p) 4 sqrt(a) / [pi (4 j^2 - 1)] (I - T_j(B))
eta_t = A psi; psi_t = -g eta
H = rho dx^2 / 2 [g sum(eta^2) + psi dot A psi]
```

Each summand is positive semidefinite, symmetric and annihilates a water-component constant.
Every polynomial path follows water edges. A disconnected component has exactly zero
coupling at every order, unlike multiplying a global FFT field by a damping mask. Around a
finite island paths can go around the ends; these legitimate paths can produce a shadow
signal, and this model does not claim zero wave height everywhere behind an island.
This is a different boundary approximation from an oceanic Dirichlet-to-Neumann solver;
its island diffraction/reflection accuracy has not been established against such a reference.
No new FFT dependency is selected: W1's FFT remains the periodic reference, while this
candidate tests a topology-aware alternative. Comparing practical solvers is still required.

Kick-drift-kick advances the free Hamiltonian. The fixed dt is conservatively bounded by
the maximum graph frequency. eta-only exponential absorption has exactly nonnegative loss
`rho g dx^2/2 sum(eta_old^2 (1-exp(-2 rate dt)))`. A spatial mask on psi would not have
that guarantee because kinetic energy is nonlocal. Without absorption the outer graph
boundary reflects. Absorbing strips approximate open edges and absorbing shores; they are
not perfect nonreflecting boundaries. Absorption can change integrated water volume;
source-off undamped propagation preserves mean eta. No source injects height directly.

The ledger reports signed injected work, separate positive/negative work, retained energy,
absorbed energy, explicit retirement and **signed numerical residual** from propagation.
It checks `retained + absorbed + retired = injected + residual`. Residual is measured before
each absorption, never relabelled as a sink. Prescribed potential kicks report the actual
Hamiltonian change, including interference. This is not resistance-calibrated engine work.
Density is 1000 kg/m^3, g is 9.81 m/s^2; fields use metres/seconds and H uses joules.

## Fixtures and measurements

The deterministic boundary fixture is 48x32 cells at 1.5 m spacing (72x48 m), order 64,
dt 0.05 s, run for 80 s after one compact signed potential impulse. These are diagnostic
sizes, not a chosen gameplay domain. The source is strictly zero beyond a 9 m radius,
so the opposite component is not accidentally seeded by Gaussian tails. Its height starts
at zero. The four columns of the optional image are reflecting water, absorbing water,
absorbing finite island and absorbing full barrier. Rows are t=4, 8, 12, 40, 80 s.
The finite island spans x cells 23..26, z cells 7..24; its adjacent water has an explicit
absorbing shore. Open-edge strips are seven cells wide, with quadratic rates up to 2/s.

Predeclared executable checks require <1% source-off energy drift, quartering of drift
on timestep halving (ratio <0.3), <1% polynomial symbol error against the discrete graph,
<0.001 m manufactured-mode height error, zero full-barrier transmission, finite-island
shadow intensity <10% early / <20% later of open water, <5% retained energy after open
absorption, and late returning intensity <20% of the reflecting control. These thresholds
bound this fixture; they do not replace W1 continuum/refinement gates for a production solver.

Measured values:

- Undamped max relative energy drift over 20 s: 0.002821 at dt .05, 0.0007045 at dt .025.
  Mean height stays below 1e-12 m. Ledger closure tolerance is 1e-9 relative to source work.
- Manufactured Neumann cosine mode, 2 s: max eta error 7.29e-5 m against its independent
  discrete symbol and analytic oscillator. Independent cloned trajectories match exactly.
- Full barrier: shadow eta-squared sum exactly zero throughout 80 s, including return times.
- Finite-island peak shadow eta-squared sum at 0..12 s: 3.699e-6 versus 0.03470 open;
  at 12..80 s: 1.203e-5 versus 0.04098 open. These are spatial sums over a fixed 7x9 probe
  region, not total wave energy. Ratios are 0.0107% and 0.0294%, respectively.
- Open field at 80 s: retained/start energy 0.000260, absorbed/start 1.002710,
  numerical residual/start +0.002970. Absorbed exceeding initial energy is explained by
  measured numerical drift and is not presented as exact conservation.
- Open/reflecting late (40..80 s) return-region peak intensity: 2.955e-5 / 0.05332,
  a ratio of 0.0554%. Reflecting control retains 1.001450 of initial energy.

Cost samples use 20 zero-source steps without skipping any operators, order 128, dx .75 m,
dt .025, including Hamiltonian audits and absorption. They measure CPU process time,
not frame-time guarantees. A 48x48 field owns 131328 bytes plus owner/allocation overhead;
96x96 owns 525312 bytes. Debug: approximately 12.9 / 52.3 ms per step. Standalone Apple
Clang `-O2`: approximately 1.94 / 7.62 ms per step. W2.1b must measure larger physical domains,
full source histories and the accuracy/cost tradeoff before choosing runtime storage.

Order-128 polynomial |D| at 6 m wavelength, dx 1.5: 0.938120 versus exact graph 0.942809
and continuum 1.047198 inverse metres. Relative continuum phase-speed errors at dx 1.5,
.75, .375 m are -5.35%, -1.74%, -1.20%. Refining dx alone at fixed order does not remove
polynomial truncation error. **This is not a pass of W1's continuum/grid convergence gates.**
A domain-doubling study, order/grid convergence, longer low-frequency absorption tests,
thinner/irregular islands and a shore-reference comparison remain required. These are why
solver/domain selection remains open instead of calling all of W2.1 complete.

## Reproduction and verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/validate_boat_wake build/wake-w2.ppm
sips -s format png build/wake-w2.ppm --out build/wake-w2.png
cc -std=c17 -O2 -Wall -Wextra -Wpedantic -Isrc tests/core/validate_boat_wake.c src/core/boat_wake.c -lm -o build/validate_boat_wake_release
./build/validate_boat_wake_release
cc -std=c17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc tests/core/validate_boat_wake.c src/core/boat_wake.c -lm -o build/validate_boat_wake_sanitize
./build/validate_boat_wake_sanitize
```

The sips conversion is macOS-specific; the portable artifact is binary PPM. All five rows
of the diagnostic image were inspected: the absorbing case loses its outgoing waves, the
finite-island shadow stays weak, and the disconnected side stays empty. These are numerical
diagnostic images; runtime visuals were not changed. No new manual boating, audio playback,
GPU/browser/Windows execution or human play acceptance is claimed.

2026-10-21 verification: Debug configure and full build passed with
`PROTEUS_GPU_TESTS=OFF`; all **60 CTests** passed (269.34 s). After the final overflow-input
validation changes, the full build and `boat_wake_owned_field` CTest passed again (36.77 s).
The standalone `-O2` numerical/cost run passed. AddressSanitizer + UndefinedBehaviorSanitizer
passed with no reported diagnostics. Clang-format checks, changed local documentation links
and `git diff --check` passed. Logs/artifacts: `build/wake-w2-*.log`, `build/wake-w2.ppm`,
`build/wake-w2.png`. GPU tests and runtime manual playback were not performed.
