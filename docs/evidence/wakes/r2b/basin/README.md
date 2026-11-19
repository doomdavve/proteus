# R2b CPU basin trial and coupled-hull screen

Status, 2026-11-19: experimental trial implemented; production baseline retained.
Implementation/profiling/encounter data: 2026-11-17. Resumed qualification: 2026-11-19.
Owner: [wake research plan](../../../../WAKE_RESEARCH_PLAN.md).
Decision: offer a private CPU trial; **curved handling preservation failed**. No geometry is selected.
Next: human comparison and review of the changed encounter contract before any promotion or R3.

## Try the basin

From the research worktree:

```sh
cmake --build build-release --target wake_transport_basin -j
./build-release/wake_transport_basin
```

W/S or Up/Down adjust throttle; A/D or Left/Right steer. Throttle holds when released.
R resets. B changes baseline/spread and resets the boat, clock and wake history. Esc closes.
The trial starts with spread, 1.2 m strength and 16-second lifetime. Drive turns and cross the
trail again. This is a CPU-only, single-player research executable with no audio, networking,
plugin loading or saved preferences. Normal `wake_velocity` keeps its existing wake recipe.

The changed outer coverage can produce substantially larger jumps. The radius-50, eight-second
gap case at 24 m/s changes heave .447→2.519 m and launch velocity 2.110→6.112 m/s. This trial
is for assessing changed gameplay; it has not passed the provisional handling-preservation screen.

## Shared physics and rendering

`BoatWater.object_sample` optionally replaces only the player-object contribution; null keeps
the existing object query. Its borrowed context must outlive all session/view copies and must
not mutate the sampled surface during a query. The normal and current-advection contribution
are composed once, after sampling, alongside unchanged ambient/D3/traffic/storm/vessel terms.
Callback-only object water retains the same arcade recovery as an ordinary object pool.
The CPU mesh uses that same callback and clock for displacement and foam; the GPU draw path
explicitly refuses it before graphics calls because it has no matching shader recipe.

The private tool owns each capture and rebinds context after copy/reset. Live stepping follows
advance → physics → actual wet-stern tracking. New births retain their zero birth envelope.
B uses reset-based comparison instead of changing the surface beneath a moving boat. The
callback is process-local and never serialized; no wire, replay or production source recipe
changes are introduced. Two borrowed pointer fields are added to the local water value; no
new per-record descriptor allocation is introduced beyond the earlier private ledger.

## CPU profiling and optimization

Two 15-second Instruments Time Profiler recordings sampled the original private dense-four
circle workload. Exported running samples number 14,986 baseline and 14,977 spread. These
include warmup, source/query work and formatting, rather than isolating a query phase. Baseline
leaf time is 59.8% in the base scalar evaluator. Spread leaf time is 52.8% there, 15.0% in
transport and 6.1% in the baseline piece wrapper. [Baseline](baseline-profile.json) and
[spread](spread-profile.json) reports retain weighted samples; trace/XML files stay local.
The time-limited recordings saved successfully and exported despite xctrace returning 54.

Behavior-preserving changes reuse computed inner-side coordinates, reject outside material
support before derivative reconstruction, and skip reconstruction when every returned component
is exactly zero. No curvature deadband, source/height retune or changed transport parameter is
used. All six previous field tables, including derivatives and arrival traces, remain byte-identical.

[Optimization comparisons](optimization.json) retain three alternating pre/post pairs for each
path, four dense emitters, 20 simulated seconds warmup and 60 measured seconds per run:

| Enclosing workload | Pre-opt p50/p95/p99 ms | Optimized p50/p95/p99 ms |
| --- | --- | --- |
| Straight | .059/.062/.063 | .033/.035/.039 |
| Tight circle | .078/.083/.085 | .068/.073/.075 |
| Reversal | .080/.084/.086 | .068/.073/.075 |

A separate contemporary 72-run baseline-versus-optimized matrix repeats one/four emitters,
dense/dispersed probes and all three paths. Dense-four p95 is approximately .026→.037 ms
straight and .059/.060→.074 ms circle/reversal. No individual run exceeds the provisional
.10 ms p95/.15 ms p99 fixture screens in that matrix; the earlier .257 ms tail did not recur.
This does not prove that rare tails are eliminated. Relative overhead still requires review.
These are private source/30-query workloads, not authority ticks or full CPU frame budgets.
Isolated callback, CPU render preparation, GPU, network and driver allocation costs remain
unmeasured; no integrated performance acceptance is claimed. R0/R1 HDR issues remain separate.

## Coupled outcomes

The frozen-source matrix has 252 baseline/spread pairs: 28 fixtures × three initial speeds
(8/16/24 m/s) × three initial angles (0/30/60 degrees). It includes 27 straight plumbing controls,
radius-12/50 outer/gap/inner fronts in both turn directions and several ages, old fronts,
figure-eight crest/trough probes and reversal. Receivers do not emit into the fixed history.
Sources are translated analytically to an origin-centred encounter in a wall-free physical
fixture. Initial speed/angle are setup values, not controlled contact conditions.

Both wave modes and the calm reference retain arcade handling. Runs continue for at least
eight seconds and until the entire source field expires followed by 120 stable ticks, with a
24-second cap. All pairs reached this settling criterion. The result does not imply recovery
from every possible trajectory. First wet-stern markers, dominant contributing record age,
absolute heading and peak stern inputs are retained; they are not six-support global maxima.
The landing velocity delta is a proxy, not a measured contact impulse.

[Hull summary](hulls-summary.json) records the findings and every discrete outcome change:

- All 27 straight controls match exactly, including trajectory hashes.
- 167/252 pairs exceed at least one provisional ±10% heave/roll/pitch/launch/landing screen.
- 35 pairs change landing count: sixteen 0→1, sixteen 1→2, one 2→1, one 2→3 and one 3→1.
- No sampled pair changes capsize outcome or final motion state.

Ratio eligibility floors (.05 m heave, .01 rad roll/pitch and .1 m/s launch/landing proxy) only
avoid unstable percentages. They are not newly accepted tolerances; all near-zero-baseline
absolute changes remain in the local detailed report. Fixtures deliberately target changed
fronts, so these counts are not ordinary-play frequencies. Many landing changes are in gap
probes, consistent with stronger encounters from filled coverage; that is an interpretation,
not a demonstrated cause. Changes also occur in old, inner, figure-eight and reversal cases.
The absence of discrete capsize/state changes does not waive changed handling.

At-time figure-eight trough probes place an actual support at (19.2214966,-12.6661835) in source
coordinates. The first physics step samples it at 29 s, four seconds after the source stops.
Its baseline/spread heights exactly match the previously flagged 1 m grid trough:
-.582893729/-.586606503 m. This closes the timing/placement issue for that sampled extremum;
it is not a claim about the continuous worst case. Controlled multi-source coupled encounters
remain open; earlier signed field-algebra checks are not a substitute.

## Motion and verification

Four paired CPU-view movies retain identical scripted inputs/settings: a ten-second closed-loop
self-wake drive, and two-second frozen gap12, gap50 and at-time trough encounters. The large-launch
gap50 case uses 24 m/s. Receivers in frozen clips add no wake history. Closed-loop boat paths
can diverge because altered response changes subsequent emission. Frozen clips show only the
near-origin initial encounter, within the finite rendered basin mesh; the numerical screen
uses a larger wall-free world and continues through expiry/settling. Baseline is left, spread
right, with nearest-neighbor integer enlargement. All movies decode completely (320 source PNGs).
Self and gap50 temporal sheets and both six-second CPU screenshots were visually inspected.
Visible demo/playback evidence and final checks are recorded in the manifest.

Astra reviewed architecture, source ordering, ownership, optimization, outcome reports and
trial controls. Addressed findings include callback-only arcade recovery, a mechanically equal
calm reference, expiry-before-settling, exact-time support targeting, a specific GPU refusal test
and the large-launch comparison. Its recommendation is an experimental CPU trial only; the
supervisor retains production baseline and marks handling preservation failed. Human acceptance,
full-frame CPU qualification, multi-source coupled response, GPU/platform/room integration and
R3 remain open. No shader implementation or production geometry promotion is included.

The full Debug build, Release research targets and fully instrumented ASan/UBSan private check
passed. On 2026-11-19 `ctest --preset full` passed 622/622, including 36 GPU-labelled cases,
in 108.35 s with desktop/socket access. Tests check null-path query parity, 480-tick whole-boat
parity with a callback-only airborne control, shared physics/render callback clock, GPU refusal
and deterministic curved replay. Final help/label-only updates passed three focused checks, including the CI lane check. No new human steering, audio listening, browser or slower-GPU acceptance is claimed.

[Testing commands](../../../../TESTING.md#experimental-cpu-basin-trial-r2b) describe reproduction.
[Manifest](manifest.json) retains source/raw/report/movie hashes, exact commands and local paths
under `/private/tmp/proteus-wake-research/build-release/wake-r2b/basin/`. Large artifacts and
sanitizer build trees remain outside Git. Work remains local; no push or PR is authorized.
