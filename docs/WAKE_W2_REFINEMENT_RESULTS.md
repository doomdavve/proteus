# W2.1b: operator accuracy and bounded-domain refinement

Historical record: the physical wake-field experiment and its review tools were retired on 2026-11-14. Commands below describe the earlier revision and are no longer runnable on current source.

2026-10-21, macOS / Apple Silicon. Owner: [wake rewrite plan](WAKE_REWRITE_PLAN.md).
Historical baseline: commit `801970a`, after [initial W2.1 at 78ad300](WAKE_W2_RESULTS.md).
Current status and next task: [runtime feasibility checkpoint](WAKE_RUNTIME_RESULTS.md).
The intervening [matched-accuracy proof](WAKE_W2_FIELD_RESULTS.md) completed W2.1's numerical gates.
The decisions/measurements below describe this earlier checkpoint; exact baseline reproduction
requires that commit.
Implementation: boat_wake.c,
review tool.

The owned candidate now has a positive polynomial-tail correction and optional fourth/eighth
order water-only spatial operators. The new tests cover manufactured modes, irregular land,
thin barriers, symmetry, constants, mean level, Hamiltonian drift and snapshots. A finite-band
modal packet passes <1% continuum height error and coarse/fine height agreement through 76 s.
A separate **coarse-grid** 144x96 m domain passes the declared sampled central-region gate
against a doubled reference; the 72x48 m domain fails and is rejected.

**Baseline decision (numerical gates subsequently completed at 5e8a5ba):**
retain this as a numerical reference, not a runtime solver selection. The accurate
144x96 m configuration costs about **109 ms per 1/120 s step** in the optimized standalone
build. Domain comparison has not yet been repeated at that accurate configuration. W2.1
therefore remains open on a faster boundary-aware operator and a domain study at matching
accuracy, before W2.2 moving-source calibration. The player/runtime is unchanged.

## Operator change

The original truncated Chebyshev square-root sum loses the positive tail's approximately
constant contribution away from zero frequency. Increasing order slowly reduces that bias;
refining grid spacing alone does not. Add an explicitly positive, constant-preserving closure:

```text
B = I - 2 L/a; a >= maximum eigenvalue of L
A = sum(j=1..p) 4 sqrt(a)/(pi(4 j^2-1)) (I-T_j(B)) + C (I-F_p(B))
C = 2 sqrt(a)/(pi(2p+1))
F_p(B) = I/(p+1) + 2/(p+1)^2 sum(j=1..p) (p+1-j) T_j(B)
```

F is the normalized Fejer kernel. On the spectrum of B in [-1,1], F lies in [0,1].
The correction can be evaluated by adding positive weights to the same existing polynomial
terms: no extra operator passes or buffers. It annihilates constants exactly and preserves
symmetry, positive energy and disconnected-component blocking. This is an approximation,
not an exact summation of the missing tail. Its error can oscillate with order; measured
field convergence is required instead of assuming monotonic improvement at every frequency.
At order 128, dx 1.5 m, the cardinal operator's 6 m symbol improves from 0.938120 to
0.942789, against the exact graph value 0.942809. Cardinal-grid continuum dispersion remains.

For higher spatial accuracy, let Lx/Lz be the positive Neumann graph Laplacians of each
contiguous horizontal/vertical water segment. Define:

```text
order 2: L = Lx + Lz
order 4: L = sum(d=x,z) [Ld + dx^2 Ld^2/12]
order 8: L = sum(d=x,z) [Ld + dx^2 Ld^2/12 + dx^4 Ld^3/90 + dx^6 Ld^4/560]
```

Each axis polynomial is positive. Their sum is positive even where the axes do not commute
around land. The equivalent radius-2/4 stencils mirror at the end of **each contiguous water
segment**. Precomputed neighbor indices never cross a land cell, even if it is only one cell
wide. Multiple bounces handle a one-cell water channel. This preserves topology instead of
using an unobstructed high-order stencil over land. Bounds a are 8, 32/3 and 13.0031746,
divided by dx squared. The dt stability check uses the selected bound.

`BoatWakeConfig.spatial_order` accepts 0/2, 4 or 8 (0 retains the cardinal graph). Every clone
rebuilds its own neighbor map. The Hamiltonian, source/sink ledger and fixed-tick step remain
the same. This does not validate geometric shoreline accuracy or the oceanic boundary
Dirichlet-to-Neumann problem. There is still no chosen FFT library or production backend.

## Independent propagation and refinement checks

`wake_field_review --check` tests actual C steps against independent Neumann cosine modes
for all spatial orders, including a one-cell water segment. The first step checks eta=dt A psi;
240 actual steps check the exact discrete oscillator phase/sign in eta and psi. A separate
axis-power symbol (rather than the stencil coefficients) checks the polynomial approximation.
An irregular mask checks symmetry via p dot A q versus q dot A p, positive energy, zero mean
and annihilation of constants; its source-off drift stays below 1% and quarters on dt halving.
Snapshots reproduce continuation, and a one-cell land wall has exactly zero transmission.

The modal packet study uses exact cosine coefficients of a reflected Gaussian-carrier
potential: carrier 6 m, sigma x/z 5/6 m, centre (-18,0), in a 72x48 m Neumann rectangle.
It is finite-band and contains wavelengths shorter than its carrier. The same continuous
coefficients are used at every spacing/order. Continuum reference propagation uses sqrt(g k).
Candidate propagation uses the selected discrete symbol and the exact discrete Verlet map,
with dt=1/120 s. This reconstruction is corroborated by actual C manufactured-mode steps;
it is not a long stepped C packet run, a moving hull, an open-boundary reference or a land
reference. All fields are sampled at the same fixed 48x32 physical points. The fine comparison
is dx .375 m / polynomial order 1024 / spatial order 8. There is no spatial interpolation.

| dx (m), polynomial order; spatial order 8 | Continuum eta L2 at 16 s | Fine eta L2 at 16 s | Continuum eta L2 at 76 s | Fine eta L2 at 76 s |
| --- | --- | --- | --- | --- |
| 1.5, 128 | 16.1477% | 16.2544% | 78.2742% | 78.8340% |
| .75, 256 | **0.1286%** | **0.1618%** | **0.6371%** | **0.8988%** |
| .375, 512 | 0.1782% | 0.0773% | 0.9801% | 0.3678% |
| .375, 1024 (fine reference) | 0.1373% | 0 | 0.7990% | 0 |

The .75/256 case passes the <1% checks at both times. Results do not support increasing
spatial resolution without also increasing polynomial order. The fine reference still has
measurable approximation error; it is not exact ground truth. Long-wave convergence,
source-time refinement, moving-source source-work calibration and all W1 path replays remain
unverified for this C representation.

## Domain/island experiment

Actual C steps compare the same compact potential impulse in nested domains. All runs here
use **dx 1.5 m, spatial order 8, polynomial order 64, dt .05 s, 80 s duration**. This cheaper
configuration isolates domain sensitivity; the modal study above shows why it is insufficient
for accurate long-lived gameplay waves. Tests comprise open water, an irregular finite island
with a narrow waist, and a one-cell full land wall. Shores absorb eta in adjacent water; outer
strips retain the original physical 10.5 m width and quadratic rates up to 2/s. Source support
is exactly zero beyond radius 9 m, centred at (-18,0), so the far side is not seeded.

The comparison region is the same central 24x24 m patch in both domains. The reported error
is its eta-difference L2 divided by a fixed source scale
`sqrt(sum(psi_initial^2) * (2 pi/6)/g)`, rather than a vanishing late-time eta norm. This gate
bounds central returning contamination relative to the prescribed initial packet. It does
not measure global energy equality: outward energy legitimately stays longer in a larger
reference domain. Threshold: <1% at 12, 40 and 80 s for open water and the island. Both wall
controls must stay exactly zero on the opposite side throughout. Small-domain retained energy
must be <1% of injected work at 80 s; this is intentionally not imposed on the larger reference.

The initial 72x48 m trial fails at 12 s: open error 1.8846%, island 1.1391%. Its JSON reports
`domain_gates_passed: false` and the tool exits 1. It is retained as a rejected diagnostic
size, not silently relabelled or made to pass by widening the tolerance.

The 144x96 m trial against 288x192 m passes:

| Time | Open normalized difference | Island normalized difference |
| --- | --- | --- |
| 12 s | 0.0311% | 0.0393% |
| 40 s | 0.6283% | 0.4483% |
| 80 s | 0.2675% | 0.1452% |

All full-wall probes remain exactly zero for 80 s, in both domains. The 144x96 m finite-island
shadow peaks are 2.865e-5 / 5.822e-5 (early/late eta-squared sums), compared with 0.09272 /
0.12011 open; the ratios are 0.0309% / 0.0485%. The larger reference gives a similar weak
shadow. Both pass the fixture's 1% intensity-ratio bounds. A finite-island shadow may contain
waves going around the ends; zero transmission is only asserted for disconnected water.
This is not a comparison against a physical shore/diffraction reference.

At 80 s the 144x96 m open field retains 0.19195% of its initial energy; absorbed fraction is
1.00123747 and signed propagation residual is +0.00315693. The ledger closes to 1e-9 relative.
Larger reference water retains more outgoing energy, as expected; no energy is retired to
force domain agreement. The six 80 s runs take about 162 optimized CPU seconds together.
The .75/256 configuration still needs its own doubled-domain/land-resolution study.

## Cost and decision

Standalone Apple Clang `-O2`, 20 steps, with all Hamiltonian audits and no zero-field shortcut:

| Physical domain, dx .75 / polynomial 256 / spatial order 8 | CPU ms/step | Owned payload |
| --- | --- | --- |
| 72x48 m (96x64 cells) | 27.0 | 743424 bytes |
| 144x96 m (192x128 cells) | 108.6 | 2973696 bytes |

Payload includes seven double fields, copied land and the radius-four neighbor map, excluding
owner/allocation overhead. The adequate coarse-domain footprint is thus expensive at accurate
resolution. A step takes about thirteen times the 8.33 ms fixed-tick interval, before source,
hull, queries or rendering. No claim about a full-game frame time or browser capability follows.
This measured cost prevents selecting the current implementation for gameplay. Next work is
a faster positive boundary-aware square-root evaluation, retaining these independent modes,
energy accounting, topology checks and domain fixtures as gates. Then repeat domain/order/grid
comparisons at the actual chosen accuracy before W2.2 source calibration.

## Reproduction and verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/wake_field_review --check
cc -std=c17 -O2 -Wall -Wextra -Wpedantic -Isrc tools/wake_field_review.c src/core/boat_wake.c -lm -o build/wake_field_review_optimized
./build/wake_field_review_optimized --accuracy > build/wake-w2b-accuracy.json
./build/wake_field_review_optimized --small > build/wake-w2b-small-rejected.json
# The --small command above intentionally exits 1: the small domain fails its declared gate.
./build/wake_field_review_optimized build/wake-w2b-domain.ppm > build/wake-w2b-domain.json
sips -s format png build/wake-w2b-domain.ppm --out build/wake-w2b-domain.png
cc -std=c17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc tools/wake_field_review.c src/core/boat_wake.c -lm -o build/wake_field_review_sanitize
./build/wake_field_review_sanitize --check
xcrun xctrace record --template 'Time Profiler' --output build/wake-w2b-cost.trace --launch -- ./build/wake_field_review_optimized --accuracy
```

The optional PPM has six columns: open/island/wall in 144x96 m water, then the matching
three in 288x192 m water, all cropped to the same central 72x48 m region. Rows: 4/8/12/40/80 s.
All rows were inspected. The images show the expected outgoing/absorbed wave and weak
island shadow; the disconnected side stays empty. sips/Xcode commands are macOS-specific.
Artifacts/logs are under `build/wake-w2b-*`; they are not committed. The long domain study
is a standalone review, while CTest runs the smaller analytic/geometry/refinement checks.

2026-10-21 verification: Debug configure/build and all **61 CTests** passed with
`PROTEUS_GPU_TESTS=OFF` (271.17 s). The final full build and strengthened
`wake_field_refinement` test passed again (8.48 s) after adding the 240-step mode checks
and the fine-reference row. Final AddressSanitizer + UndefinedBehaviorSanitizer `--check`
passed with no reported diagnostics. The optimized accuracy/cost and long domain studies
passed; `--small` produced a complete JSON rejection with exit 1 as intended. Clang-format,
changed local documentation links and `git diff --check` passed.

An Xcode Instruments Time Profiler recording of the optimized `--accuracy` study completed
and its samples were exported and inspected. Of 3022 sampled milliseconds, about 94.04%
had `apply` as the leaf frame. This includes manufactured/irregular tests, modal reconstruction
and cost loops, not a full-game trace. It supports improving the polynomial evaluation rather
than optimizing rendering or queries. Artifacts: `build/wake-w2b-cost.trace`,
`build/wake-w2b-time-profile.xml`, `build/wake-w2b-profile-summary.json`.
No runtime visuals, controls, audio, asset loading or defaults changed, so no new manual
boating/playback or user acceptance is claimed. GPU/browser/Windows execution is unverified.
