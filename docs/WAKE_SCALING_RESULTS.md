# Wake performance and large-water follow-up

Historical physical-field follow-up, 2026-10-21. Owner: [wake rewrite plan](WAKE_REWRITE_PLAN.md). The user likes the
revision-3 basin results as a foundation, but requires much faster wakes that can apply
to arbitrarily large water spaces. Performance and world scaling take priority over the
separate water-appearance pass. **The current candidate is still basin-only and opt-in;
D3 remained default at this checkpoint. Neither large-world support nor the 2 ms wake target had passed.**

On 2026-10-22 the user selected [bounded arcade wake objects](WAKE_OBJECT_RESULTS.md)
as a third alternative, subsequently promoted to the free-boating default by user request.
That is now the active track; the physical representation gates
and proposed next experiment below remain historical requirements of the field track.

## Implemented optimization

The same cosine operator now uses double-precision real DFTs on supported Accelerate
lengths, keeping only the independent half spectrum. Native complex DFT and portable
mixed-radix fallbacks remain available. Packing and normalization follow Apple's
[real DFT contract](https://developer.apple.com/library/archive/documentation/Performance/Conceptual/vDSP_Programming_Guide/USingDFTFunctions/USingDFTFunctions.html).
Both axes use contiguous transforms, with blocked transposes in existing graph scratch.
There is no per-tick allocation. The field owner grows by 16 bytes on this native build.

This changes evaluation cost, not the spatial operator, useful wavelength range, source
revision, double precision, 120 Hz cadence, .75 m grid, physical domain or absorption.
The query and renderer contracts are unchanged. No simulation distance, culling or wave
retirement policy has been added. The basin remains the behavior/comparison reference.

Backend tests now also exercise mixed native/portable lengths and partial transpose
blocks. The original graph comparison, signed forcing and cancellation, clone/reset,
manufactured queries, moving-source convergence and physical crossing tests remain the
acceptance checks. The five-size native/portable comparisons differ from the graph oracle
by at most `2.002e-14` relative L2. The unchanged domain/island proof in
[candidate results](WAKE_CANDIDATE_RESULTS.md) remains applicable; it does not validate
a future scrolling or tiled domain.

## Measured cost

Apple M1 Max, macOS 26.6.2, Apple clang, `RelWithDebInfo`, native 320x200 GPU renderer,
no AA. `wake_play_review --gpu` measures 120 frames after nine simulated seconds,
two real hull/field ticks per frame, source construction, all gameplay queries, independent
energy audit, height upload, complete scene rendering and `glFinish`. Setup, audio,
presentation and frame limiting are outside the measurement. These are desktop observations,
not an exclusive-device or worst-case guarantee.

| Full basin workload | Before this change | Final optimized run |
| --- | ---: | ---: |
| Hull, source, field, queries and audit | 8.909 ms/frame | **6.310 ms/frame** |
| Whole renderer, upload and GPU completion | 5.480 ms/frame | 4.010 ms/frame |
| Combined | 14.389 ms/frame | **10.320 ms/frame** |

The physics reduction is **29.2%**. Intermediate runs measured 6.637 ms with real DFTs
alone and 6.370 ms after adding transposes. Renderer timings vary independently; do not
attribute their entire reduction to this CPU optimization. The final same-pose flat-water
renderer measured 5.832 ms, demonstrating why subtracting these noisy independent renderer
samples cannot establish an incremental wake rendering cost.

The diagnostic now reports frame distributions as well as averages:

| Final run | Median | 95th percentile | Maximum observed |
| --- | ---: | ---: | ---: |
| Physics including wake | 6.381 ms | 6.556 ms | 6.598 ms |
| Complete measured frame | 9.793 ms | 13.612 ms | 14.361 ms |

The field/source integration alone is 5.687 ms/frame; source construction is .018 ms and
remaining preparation/audit .342 ms. **The 2 ms complete-wake target still fails.**
An observed frame below 16.67 ms is not sufficient headroom for the rest of a growing game.
The unchanged source/ledger and production shader-query results match the previous run
at the printed precision; both shader stages have relative L2 error `3.0662e-6`.

Explicit candidate storage is now 8,962,752 bytes for the world and 13,082,520 bytes
(12.48 MiB) including the previously counted upload, texture and mesh resources.
Allocator overhead, opaque Accelerate plans and existing renderer/driver allocations remain
excluded. Native real DFT acceleration is macOS-specific; this is not a Windows or browser
performance result.

## Why increasing the grid is not the large-world solution

`wake_candidate_review --costs` provides a repeatable synthetic size sweep. Each measured
frame constructs a compact moving balanced source, advances two 120 Hz ticks, performs six
complete queries and an independent Hamiltonian audit per tick, and copies one float height
endpoint. It excludes the hull, GPU transfer and rendering, so it is **not** a complete
gameplay cost or permission to select a smaller domain. There are 12 warmup frames and
120 measured frames per size; times are process CPU milliseconds.

| Field extent | Cells | Mean ms/frame | p95 | Maximum | Field-owned bytes |
| --- | --- | ---: | ---: | ---: | ---: |
| 72x48 m | 96x64 | .546 | .796 | .954 | 552,944 |
| 144x96 m | 192x128 | 1.504 | 1.510 | 1.526 | 2,098,672 |
| 288x192 m | 384x256 | 6.196 | 6.503 | 6.718 | 8,176,112 |
| 576x384 m | 768x512 | 25.682 | 27.798 | 29.715 | 32,274,928 |

Source and float-copy buffers add 12 bytes/cell, separate from the field column. The
benchmark's short, central source is a cost workload, not a leave/return or shoreline
proof. In particular, the previously rejected 72 m domain does not become acceptable
because its timing is attractive. Doubling both dimensions costs about four times as
much here. Extending a full-resolution rectangle across a large map is not selected.

## Required next representation

Map extent and active wave work must be separate. For a fixed number of interacting
boats and a declared wave-resource budget, enlarging empty water must not allocate or
step a proportionally larger grid. This does not promise constant cost for unlimited
emitters or indefinitely accumulated undamped waves.

The next isolated experiment should keep phase-carrying wave state in world space and
build a bounded detailed query/render surface around interacting boats. Test a sparse
dispersive packet representation against the existing field before committing to a runtime
design. [Water Wave Packets](https://visualcomputing.ist.ac.at/publications/2017/WWP/)
is a research starting point for this approach, not proof that its reconstruction meets
this project's signed-interference or energy tolerances. The rejected W1 Hermite strips
stay rejected: folding, source reconstruction and spatial energy convergence must be solved,
not hidden by reviving the same strip under another name.

Simulation distance should bound detailed computation and uploads. It must not silently
erase the physical state just outside the camera. A distant representation needs phase,
direction, dispersion and signed superposition sufficient to reconstruct a returning
encounter. Simply increasing distant grid spacing loses short wavelengths that may still
matter on return. Independently solved rectangular tiles also create false boundaries;
the present cosine field cannot just be cut into moving boxes.

Before enabling large-world gameplay, require:

1. **Matched field accuracy:** the declared 3–12 m band, mean level, signed source work,
   interference, long propagation and all shared velocity/gradient queries meet the
   existing oracle gates. Include source resampling and actual moving stern history.
2. **Travel and return:** leave a strong wake behind, travel well beyond the detail region,
   then return through it. Compare the entire query history with a sufficiently large
   reference. Include reversals, repeated boundary crossings, two separated boats and
   later overlap of their regions; no duplicate waves or seam impulses.
3. **Topology and accounting:** rerun matched island/barrier/absorption checks. Promotion,
   coarsening, merging and explicit retirement must each report reconstructed energy and
   error. Absorption, deliberate retirement and numerical residual remain separate. A
   fixed hard cutoff or age limit is not yet an approved physical compromise.
4. **Bounded resources:** long straight travel and repeated loops must stay inside declared
   storage/work limits. Define behavior when the budget fills without overwriting strong
   live wakes. If preserving the existing checks is impossible within the budget, present
   the measured tradeoff before changing the physical contract.
5. **Complete cost:** measure source, update, queries, accounting, promotion/retirement,
   synchronization/upload and rendering access, including p95 and observed spikes, on
   named desktop hardware. Retain 2 ms/frame as the provisional whole-wake target. Measure
   browser/other native paths separately; neither a cheap isolated packet update nor a
   CPU/GPU split with hidden readback latency is a pass.

No representation switch, detail radius or distant-retirement threshold is selected yet.
The first isolated [compact source-packet experiment](WAKE_PACKET_RESULTS.md), completed
2026-10-22, rejects fixed spectral quadrature: fine reconstruction matches the sampled
finite-horizon reference but is too expensive; cheap versions reconstruct false copies.
The historical proposed next step was spatially localized, adaptively refined reconstruction, with stronger
return probes and reconstructed-energy convergence. The gates above remain open.
Water appearance remains a later, separate pass preserving the accepted feel.

## Reproduction and verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure

cmake -S . -B build/review -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/review --target wake_candidate_review wake_play_review wake_velocity -j
./build/review/wake_candidate_review
./build/review/wake_candidate_review --costs
./build/review/wake_play_review --gpu
```

Current-change verification, 2026-10-21 on the development Mac:

- Debug configure/build and all **91 CTests passed**, including **22 GPU checks**, in
  349.92 s. Debug, optimized native and Emscripten incremental builds emitted no warnings.
- Native Accelerate and portable C17 builds pass the expanded backend/query/interference
  checks under ASan/UBSan. Moving-source convergence, actual stern resampling and physical
  crossing/recovery pass in the full suite.
- Time-9 CPU and GPU captures were inspected; both are byte-for-byte identical to their
  original revision-3 captures. The 600-frame scripted desktop run completed with 1,242
  hull/field ticks, finite state and a separate signed work/absorption/residual ledger.
- The Emscripten baseline builds successfully (3,962,916 bytes before compression).
  Candidate browser launches remain unsupported. This follow-up does not add browser
  gameplay/performance testing or a Windows timing measurement.
- Formatting, changed local documentation links/anchors and `git diff --check` pass.

The separate 80/160 s domain studies were not repeated for this evaluation-only optimization;
the operator, domain, grid, cadence and boundaries are unchanged and compare against the
same graph oracle. Any new operator or active-domain policy must repeat those proofs.
Scripted animation and captures are not agent hands-on driving, audio listening or user
acceptance of a default switch. No large-world, distant-state or retirement behavior is
implemented by this follow-up.

Artifacts: `build/wake-scaling-before.log`, `wake-scaling-real.log`,
`wake-scaling-transpose.log`, `wake-scaling-final-gpu.log`, `wake-scaling-costs.log`,
`wake-scaling-ctest.log`, `wake-scaling-sanitized.log`, `wake-scaling-native-sanitized.log`,
`wake-scaling-cpu-9.png`, `wake-scaling-gpu-9.png`, `wake-scaling-desktop.log`, and
`wake-scaling-web-build.log`.
