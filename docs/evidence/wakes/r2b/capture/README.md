# R2b private capture ledger — 2026-11-17

Owner: [wake research plan](../../../../WAKE_RESEARCH_PLAN.md).
Design: [historical transport proposal](../../../../WAKE_R2B_DESIGN.md).
Completed infrastructure step only: no outward-dilation evaluator, shader, protocol or production
source policy is implemented. Baseline geometry and 16-second lifetime remain unchanged.

## Decision and capture behavior

Retain the private `tools/wake_capture.[ch]` ledger for the next CPU field experiment. Each
successor freezes its divergence estimate from actual adjacent captured chords, with copied
predecessor witnesses independent of live ring slots. A strict positive chord dot product gates
turns below 90 degrees. A sharper pair freezes zero without disqualifying its otherwise valid
successor as the next predecessor. No existing descriptor acquires future geometry.

Dry/slow/missed tracking, invalid or unknown identity, omitted short spans and explicit resets
break provenance. The existing dry-to-wet tracker can emit a span starting at its last dry sample.
Preserve these raw baseline births and strengths; mark that carry-in descriptor ineligible and
zero, and do not use it as a predecessor. This remains a baseline physical source limitation.
Explicit teleport/session reset restarts the private tracker; callers must supply the signal.
Finite inputs whose chord/midpoint arithmetic overflows are rejected before publication.
Same-tick already-tracked duplicate calls leave all state unchanged.

Versioned snapshots retain records, frozen descriptors, witnesses and source state. A whole-image
checksum detects accidental corruption; semantic checks validate physical-record correspondence,
serial uniqueness, finite geometry, adjacency and still-live witness identity/epoch. Validation
precedes destination mutation. A missing expired predecessor is permitted; no neighbor-based
reconstruction occurs. This is same-build in-memory copying, not a portable save or wire format,
and its checksum provides no adversarial integrity guarantee.

## Automated behavior and resources

The contract runs five continuous source histories (straight, tight/wide circle, figure-eight
and reversal), four emitters and 80 simulated seconds each. All physical `R2Field` bytes match
the baseline throughout. A snapshot at 25 seconds reproduces the subsequent 55 seconds exactly.
Separate dry/slow/missed-call histories also preserve raw baseline records. Tests cover first
birth/restart, zero-length omitted span, invalid inputs and finite overflow, explicit teleport,
identity change/unknown identity, duplicates, parent expiry and slot reuse, invalid/missing
snapshot metadata, and failed restore leaving its destination unchanged. Numeric probes check
circle curvature at radius 12 m and below/exact/above plus alternating near-90-degree turns.
These are sampled private contracts, not exhaustive source/gameplay proof.

Native arm64 structure sizes (`sizeof`) measured in both Debug and Release; these are not
heap measurements, stack high-water marks or process memory:

| Native structure | Bytes |
| --- | ---: |
| Existing private R2a field (includes unused short-mode slots) | 10,440 |
| One descriptor, including full predecessor witness | 104 |
| 128 descriptor sidecars | 13,312 |
| Four source capture states | 256 |
| Complete private capture field | 24,008 |
| Complete same-build snapshot | 24,024 |

Added ledger storage is **13,568 bytes**. This deliberately witness-heavy audit representation
is not a shipping memory/network allocation. Snapshot storage is additional when requested.
Production record layouts, 128-record player capacity, GPU fields/uploads and network remain
unchanged. GPU/driver allocations, production authority/network throughput and integrated frame
cost are unavailable for an unintegrated capture harness.

## Capture-only overhead

M1 Max, macOS 26.6.2 arm64, Apple clang 21.0.0, native Release, 2026-11-17. Twelve runs:
one/four dense continuously emitting circle sources; three alternating baseline/capture pairs.
Each pair runs baseline first; repetition does not remove this ordering bias.
Each run has 20 simulated seconds preroll and 60 seconds / 3,600 measured frames, with two
120 Hz ticks and thirty unchanged baseline queries per frame. Every measured pool remains
saturated (32 or 128 records). The final timing run had no concurrent tests or media processing;
this was not an exclusive-machine measurement. An earlier overlapping test/benchmark run is
retained locally as exploratory history and excluded from this report.

Milliseconds, median of three per-run percentiles:

| Emitters / component | Baseline p50/p95/p99 | Capture p50/p95/p99 |
| --- | --- | --- |
| One / tracking | .000 / .001 / .001 | .001 / .001 / .001 |
| One / queries | .025 / .026 / .028 | .025 / .026 / .027 |
| One / enclosing | .025 / .026 / .028 | .025 / .026 / .027 |
| Four / tracking | .000 / .001 / .001 | .001 / .001 / .001 |
| Four / queries | .041 / .043 / .049 | .041 / .043 / .049 |
| Four / enclosing | .041 / .044 / .050 | .041 / .044 / .050 |

[The report](timings.json) retains full absolute/percentage deltas and p95 repetition ranges.
Observed timing quantization is roughly .001 ms: zero samples and equal p95 do not establish
zero overhead. Tiny percentage differences are not useful optimization claims. Enclosing p95
ranges are .026–.026 ms baseline / .026–.027 capture for one source and approximately .044 ms
for both four-source variants. Query code is identical; small tail differences are variation.
Tracking includes source-path evaluation, expiry and ledger work; queries include probe creation
and accumulation. Snapshot copying/validation is outside timing. These measurements qualify
neither the eventual transform nor arbitrary source histories, server ticks or graphics budgets.

All 21,600 paired measured frames have identical births, occupancy and printed signed-height
query sums. Exact field/descriptor replay is checked separately by the C contract. No new
motion movie, physical-surface comparison or human feedback is claimed for this capture-only
step; the unchanged evaluator has the retained R0/R1/R2a media. R2b remains open and its actual
field screen must deliver the next matched diagnostic motion and behavior/cost bundle.

## Review and verification

Astra reviewed the design and implementation/test sources. Addressed findings: preserve raw
dry/slow birth timing, mark carry-in ineligible, reject arithmetic overflow, validate still-live
witnesses, and break provenance on zero identity before a same-tick valid retry. Its final code
review found no remaining blocking lifecycle issue. Its final evidence review found the timing
and storage claims supported, with ordering bias and observed quantization kept explicit. The
supervisor retains this infrastructure;
Astra did not independently run tests or review an implemented transform. The original tradeoffs
of startup asymmetry, a hard turn gate and full-field concentration remain unresolved for geometry.

Debug full build and Release research target build passed without project compiler warnings.
`ctest --preset full` passed **619/619**, including **36 GPU-labelled** tests, in **104.55 s**
with desktop/local-socket access. The initial sandbox run was interrupted after graphics-service
errors/timeouts; it is not the qualification result. The capture contract also passed standalone
Release and direct ASan/UBSan (`-O1 -g -fsanitize=address,undefined`). Production audiovisual
behavior is unchanged; no new hands-on input, audio, browser, slower-GPU or player acceptance
is claimed. R0/R1 HDR variation/tails remain open before R4.

Commands are in [Testing](../../../../TESTING.md#private-capture-lifecycle-r2b).
[Manifest](manifest.json) records exact source/report/artifact checksums and local raw paths.
Artifacts are under `/private/tmp/proteus-wake-research/build-release/wake-r2b/capture/`.

Next: implement only the private `p=1` outward-dilation evaluator, then screen straight endpoints,
source-off outer coverage, derivatives, full-history concentration, independent signed crossings,
matched diagnostic motion and saturated CPU costs before considering hull/GPU integration.
No geometry candidate, production resource increase, push or PR is selected by this step.
