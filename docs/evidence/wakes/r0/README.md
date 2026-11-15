# R0 baseline review bundle

Owner: [wake research plan](../../../WAKE_RESEARCH_PLAN.md). Research baseline only; the
production wake source, surface, shaders, physics, capacity and protocol are unchanged.
Current: native baseline verified and reviewed by the user on 2026-11-15; local continuation
is authorized. Behavioral/performance screens remain provisional, with existing graphics gates
unchanged. [The plan](../../../WAKE_RESEARCH_PLAN.md#r0-user-review-and-continuation--2026-11-15)
records the outer HDR cost concern and the decision to continue.

## Method and interpretation

Primary target: Apple M1 Max (24-core GPU, 64 GB), macOS 26.6.2, AppleClang 21, native Release.
Measurements use the pinned dependencies already available on the development machine.
Commands run from the research worktree root. `build-release/wake-r0/` holds raw artifacts;
the small committed manifest records their absolute locations and SHA-256 checksums.
These locations are local to the development machine, not a published artifact service.

`wake_research` advances fixed source paths at 120 Hz, grouping two ticks into each reported
60 Hz frame. Twenty simulated seconds of preroll precede 3,600 measured frames (60 seconds).
Every continuously emitting player fixture must remain at 32 records per emitter throughout.
Stop/dry/teleport/slow fixtures are transient behavior probes, not saturation measurements.
The source is prescribed, not steered by changed water, and no receiving hull feeds it back.
The rendered boat follows the prescribed source; other synthetic emitters are not drawn as hulls.
The stationary overhead rig uses a stationary observer hull at the fixture origin. It is an
oblique overview, not a vertical orthographic camera. Neither rig is a coupled driving demo.

Straight travel is 15 m/s (slow control 2 m/s), with `x=speed*(t-25)`;
dry fixtures use `x=speed*(t-10)`. These translations frame the stopping/drying event at
the fixture origin without changing source speed or relative wake geometry. Circle radii are 12 and 50 m at 15 m/s.
Figure-eight coordinates are `(30 sin(.35t + phase), 20 sin(.7t + 2phase))`, with speed from
the analytic derivative; each emitter's phase is `id*pi/2`. Turn reversal retraces the 12 m
circle every 12 seconds without a position discontinuity. Straight emitters rotate through
four compass directions. Dispersed source origins differ by 300 m along X; dense origins
coincide. Scene origins use the location spawn, or `(0,185)` for the marina observer.
The stop is at 25 s; dry intervals alternate every 10 s. The raw teleport adds 1,000 m at
25 s; `--restart` separately exercises explicit emitter restart. The raw tracker does not
detect a teleport on its own; this known baseline failure is not a new production fix.

There are 30 **synthetic** queries per frame, not measured production hull-query counts.
The first 24 are four balanced six-probe groups, one per emitter (repeated on the single
emitter in one-source runs). They sample the historical source 1.5/2/2.5 seconds earlier,
using its local normal and the production crest-distance convention on both sides. This
keeps straight-path queries inside supported wakes rather than ahead of them. The last six
stay at event-centred world offsets `(0,4+4*j)`. Straight diagnostic runs must have nonzero
support hits in every measured frame. Fixed probes expose source-off travel; historical
probes exercise the moving trail throughout a straight run.
`expensive_derived` repeats the production longitudinal/radial support predicates outside
the timed query batch. It estimates full recipe evaluations; it is not an execution counter
inside the production evaluator. Slot/active counts are derived from pool sizes/occupancies.
With `--traffic`, queries also evaluate the four original compiled traffic sources.
This is the representative maximum **currently supported physical scenery pool**, not every
possible authored actor promoted to physical waves.

Each matrix group runs three alternating repetitions of aggregate timing, phase timing,
phase timing with diagnostic counters, and wake-off. All enabled modes must have identical
packed-record/probe digests. The aggregate mode measures the enclosing workload with only
its end-point clock calls; component timing fields are unavailable in this mode. Clock
calibration records the smallest **positive** observed call-to-call interval. Component
measurements near that floor, notably source/pack phases, cannot support fine percentage claims.
Percent deltas are unavailable when their reference is within five clock quanta; the report
also flags resolution-limited components. `source_ms` combines advance, source calculation and
tracking; their individual timing split is unavailable.
Diagnostic overhead is reported separately; it is deliberately not hidden inside query time.

GPU runs use the production renderer, no AA, either 320x200 LDR or 1280x800 HDR. GPU elapsed
queries bracket the complete scene draw. Each frame waits for GL completion; this gives an
uncapped synchronous workload comparison, **not** the game's pipelined frame/presentation
latency. `submit_ms` includes CPU preparation, driver submission and possible driver waits;
isolated CPU render preparation is unavailable. `total_ms` includes fixed sources, synthetic
queries, optional counters, diagnostic packing, scene submission and completion; it excludes
CSV writes, hashing, capture, video encoding, presentation and frame limiting. The GPU sample
is whole-scene time, not isolated wake GPU time. The wake-off delta includes interaction effects.
Stamp piece/texel/outside counts, field/history bytes and object texture upload bytes are read
from production state. Outside-piece count does not measure actual fragment fallback evaluations.
Object upload bytes include all three RGBA32F rows, including the outside index row.
CPU/GPU allocator and driver overhead, exact analytic fallback work, authority tick cost and
measured network throughput are unavailable. They are not zero.

Media runs are separate from uncapped timing. Each reel pairs diagnostics-off/on at the same
simulation ticks and camera. All source-frame PNG checksums must agree before encoding. The
field reel evaluates the production CPU query at 1.5 m spacing over 120x120 m: red positive
height, blue negative height, clipped at +/-2.4 m; the right panel shows object crest foam.
This coarse/clipped diagnostic separates height from foam, but cannot establish sub-metre
join continuity. It omits cosmetic traffic ribbons and is not CPU/GPU parity evidence.
The regular production-renderer reel retains those cosmetic trails. Source-off and dry clips
show the transient rather than pretending to maintain pool saturation.

## Moving-hull coverage audit

| Hull/path | Physical surface | Cosmetic trail | Policy and current limitation |
| --- | --- | --- | --- |
| Solo arcade player, basin/outer | `boat_wake_objects_track`, actual `boat_wake_stern` | Presentation foam/spray | Speed >3 m/s, stern wetness >.05; 60-tick spans, 32 slots, 16 s maximum |
| Fleet/local rival/remote player | Shared `boat_fleet_step` source; authoritative births replicated | Client presentation | Same four-slot recipe; remote view is not an additional emitter |
| Working-harbour player | D3 `boat_water_emit_wake`, enabled by `boat_work_session` | Presentation foam/spray | Different legacy source; speed >2 m/s, distance >=.2 m; not arcade coverage |
| Original four river craft | Optional `waterfront_traffic_wakes_*` pool | Traffic-frame ribbons | Explicit caller opt-in; speed scaled by four before player threshold, .5 m crest scale; default `ObjectStudy` leaves this off |
| Original upstream ferries/schooner | No object-pool source | Longer speed-faded traffic ribbons | Outside the original four physical-source slots; water.upstream controls presence |
| Authored ordinary traffic | No automatically enabled physical source | Traffic-frame ribbons | `routes != NULL` explicitly excludes the compiled physical path; scene actor capacity is not a physical-source budget |
| Serviced resident/visitor | No replacement physical emitter found | Traffic actor hidden; harbor draws hull without a replacement trail | Confirmed on the shipped marina scene: 367 moving samples without scene trail quads or physical scenery pool |
| Moored/static harbor fleet | No travelling physical source | No travelling trail | Static moored motion is a wave pose, not translating propulsion; absence is intentional |
| Special race ferry | `boat_vessel_wake_query`, separately authored analytic source | Explicit ferry traffic-frame trail | Course/source recipe owns duration/lifetime/height; independent of four player slots |
| Scripted crossing source | `crossing_wake`/`--crossing-wake` | Separate scripted presentation | Analytic diagnostic, not evidence of moving-hull coverage |

Audit sources: `src/core/boat_wake_objects.c`, `boat_fleet.c`, `boat.c`, `boat_water.c`,
`boat_vessel_wake.c`, `waterfront_traffic.c`, `waterfront_service.c`, `waterfront_harbor.c`;
`src/session/boat_work_session.c`; `apps/wake_velocity/wake_velocity_drive.c`.
`waterfront_service` tests already assert hidden serviced traffic and zero traffic wake quads
at ownership handoffs. `wake_coverage` additionally samples the actual
`plugins/marina-berth-demo` scene through the plugin loader and production evaluators for 30 s.
Its `scene_trail_quads` is a scene aggregate, not per-vessel attribution. No production path is
changed to make this audit pass. R1 must decide cosmetic versus physical policy before wiring.

## Behavioral criteria and provisional budgets

The R0 Astra design review recommended the following starting criteria. These are proposed
for user agreement; this checkpoint does not approve a candidate or authorize promotion.

| Class | Proposed comparison criterion |
| --- | --- |
| Hard invariants | Finite outputs; identical deterministic same-platform replay; bounded occupancy; no live overwrite; integration restart/dry gaps never bridge a source span |
| Harness neutrality | Identical packed records, probes and paired frame PNGs with diagnostics off/on |
| Signed crossing | Residual <= `1e-5 * max(1,sum(abs(contributions)))`, including cancellation |
| Derivatives | Initial spatial error <=.003 and vertical-velocity error <=.02 m/s; refine sample spacings near seams before adopting geometry |
| Unchanged straight behavior | Normalized RMS <=2%, peaks <=5%, identifiable crest arrival within two ticks; declare nonzero normalization scales |
| Coupled hull encounters | Initially +/-10% in peak heave/roll/pitch/launch and available landing measures; review every capsize/state change individually |
| Timing instrumentation | <=2% of aggregate workload or measured noise floor; otherwise use aggregate totals and report the overhead explicitly |
| Candidate performance screen | Report absolute deltas; flag component p95/p99 regressions >10%; retain the existing graphics ceilings |
| Resource/network change in R0 | None in production; 5,320-byte player pool, 128 records, 32/emitter; wire slot remains one byte |

The [existing graphics gates](../../../BOATING_PERFORMANCE.md#budgets-for-p1) remain outer
constraints: ordinary 4x4 GPU p95 <=4.05 ms and frame p95 <=8 ms; G3 stress <=16.5 ms at
4x4 and <=12 ms at 320x200. Those workloads/settings differ from this synchronous fixed-path
harness, so passing this harness cannot assert those gates pass. No separate absolute wake
CPU/GPU or extra memory/network allocation has been agreed. Authority's existing proposed
1 ms p95/2 ms p99 allocation is not measured here. Capacity-expanded persistence remains R3.

`wake_encounters` stores source-off fixed-point height/slope/time traces, signed crossings and
cancellation through expiry, plus 27 calm-twin encounters at 0/30/60 degrees, 8/16/24 m/s,
and initial wave ages 1/6/12 s. It emits heave above calm, absolute roll/pitch in radians,
vertical acceleration, launch velocity, landings, any capsize and final motion state over
four seconds. `landing_delta_v_proxy` is a per-tick velocity change at the landing count
transition: **not** a contact impulse or force measurement. Zero means none was observed in
that finite window. Initial speed and age are setup parameters, not controlled contact conditions. The table
also records speed and source-record age at the first stern probe with wetness >.05 and
absolute wake height >.05 m. This is a declared wet-probe encounter marker, not the first
contact of any of the hull's six supports; blank values mean no such marker was observed.
The source-off arrival traces, not driven trajectory equality, are the geometry reference. Existing `wake_objects_review` provides the longer gameplay/stress checks;
its 1.8 m seeded results and coarse `clock()` timing are not current 1.2 m cost measurements.

`wake_turn_probes` adds a provisional, independently reproducible continuity score. Emit a
quarter-turn (rounded down to complete half-second spans) and a matched straight source at
15 m/s, then stop. At 1/4/8/12/15 seconds after stopping, sample 400 stations between the
source endpoints trimmed by .25 seconds. Search +/-2 m normal to the expected offset curve
in .05 m increments and compare each maximum signed height with the straight field's maximum.
Report source-path length below 25% of the straight crest, and minimum/maximum ratios. The
source timestamp uses the production recipe's half-span local-age offset in both fields.
Exclude straight heights below .05 m and inner fronts after passing within 2 m of the centre;
blank ratios mean no eligible samples, not a perfect result. Length is along the historical
source arc, not the expanded front. This metric can flag crest displacement beyond the
corridor as well as missing coverage; inspect the field reel and arrival traces with it.
The 25% threshold and 2 m corridor are declared diagnostic choices, not accepted tolerances.
R2 must repeat at finer along-path/radial resolution and inspect derivative/encounter errors
before interpreting improvement. The coarse field reel alone cannot score continuity.

## Review status and limits

R0 native implementation and automated baseline verification are complete locally. No R1–R3
candidate has been implemented. Budget agreement, refinement of diagnostic continuity metrics
for R2 acceptance, human scene review, browser/slower-GPU measurements and player acceptance
remain open.
The old continuity work already shortened pieces from one second to half a second and aligned
local-age envelopes; R2's shorter-span candidate is a control, not a new selected solution.

### Astra evidence review and corrections — 2026-11-15

Astra identified four blocking evidence defects in the first harness: straight probes missed
supported water, replacing six probes left emitter four uncovered, fixed cameras missed the
straight/transient event, and zero-eligible turn rows omitted a CSV field. Corrected these
before retaining the final CPU evidence: balanced historical-source probes plus fixed probes,
event-centred path coordinates, explicit support-hit assertions, and CSV-schema regression
checks. Added actual wet-stern marker speed/age and checks for all signed query components;
checked every GL timer entry point; reject stale or incomplete media frame sets. Source timing
remains combined and is labeled accordingly. Fine component percentages are suppressed near
the clock floor. The post-correction Astra review found no remaining blocking implementation
defect; it kept external GL/media verification and full-suite results as closure gates.

Superseded exploratory folders (`cpu`, `cpu-corrected`, `gpu-smoke`, `gpu-basin-low`,
`gpu-basin-low-final`, `gpu-outer-low`, `media-corrected`) are not final R0 evidence. In
particular, earlier GPU whole-scene measurements used the old query workload and are not used
to close the corrected baseline. Preserve them as discarded-harness history, not a candidate win.
The retained CPU matrix is `cpu-final`; `media-final` records only completed, checksum-matched
reels. No movie is claimed from an incomplete frame folder.

### Corrected CPU baseline and first verification checkpoint — 2026-11-15

The Release CPU matrix completed **240 runs / 60 neutrality comparisons**: five paths,
one/four emitters, dense/dispersed sources, four measurement modes and three repetitions.
All continuous fixtures retained full occupancy throughout all 3,600 measured frames; all
aggregate/phase/diagnostic record-and-probe hashes agreed. Straight diagnostic support passes
never fell below 48 per frame. The [timing table](timings.csv) contains every workload and
mode, median per-run percentiles and the three-run p95 spread; [the manifest](manifest.json)
records commands indirectly through the checksum-addressed raw JSON/CSV and review archive.
This was not an exclusive-machine run; substantial p99 and reversal-run variation remains.

Representative dense-source CPU samples, **milliseconds**, median of three per-run statistics:

| Source path / emitters | Query batch p50/p95/p99 | Enclosing phase-timed p95 | Diagnostics-on enclosing p95 | Wake-off enclosing p95 |
| --- | --- | --- | --- | --- |
| Straight / 1 | .004 / .004 / .005 | .006 | .008 | .002 |
| Straight / 4 | .005 / .007 / .010 | .009 | .014 | .002 |
| 12 m circle / 1 | .007 / .008 / .023 | .010 | .013 | .002 |
| 12 m circle / 4 | .026 / .028 / .081 | .030 | .039 | .002 |
| Figure-eight / 4 | .013 / .018 / .036 | .020 | .071 | .002 |
| Turn reversal / 4 | .026 / .078 / .083 | .086 | .039 | .003 |

The diagnostic mode's separate predicate scan has measurable cost. The reversal and
figure-eight variation also shows why a lower value in a later run is not an optimization
claim. Use the full spread and alternating raw runs, not this short table, for comparisons.
Source and packing phases are often within the approximately .001 ms clock quantum; no
sub-microsecond cost or precise percentage improvement is established.

The source-off signed sum residual was **5.96e-8**, maximum sampled spatial derivative error
**.000296**, and vertical-velocity error **.00936 m/s**. All 27 encounter rows are retained in
[the outcome table](encounters-hulls.csv), including first qualifying wet-stern speed/record
age. The existing longer `wake_objects_review` run also passed: its default-height stress
envelope had 83 flights, 83 landings and six final capsizes across 108 artificial cases;
these are seeded stress outcomes, not an ordinary-play capsize frequency.

The [turn table](turns.csv) measures outer separation and inner concentration without changing
source strength. At radius 12 m / four seconds after stop, 2.2875 m of the 7.5 m eligible
source corridor falls below 25% of the straight crest; by eight seconds it is 3.84375 m.
At radius 50 m / eight seconds, 2.86875 m of 67.5 m falls below that level, while inner
maximum crest ratio reaches 2.04866. Late-life extrema with only one/six eligible samples
are not robust whole-front coverage results. These observations justify experiments, not a
particular rounded/joined representation.

Two straight/chase reels were completed before graphics availability was lost: production
rendering and the separated physical-height/object-foam diagnostic, each pairing baseline
and diagnostics. All **200 corresponding PNG pairs** were byte-identical; each encoded
movie has 100 frames at 10 Hz (10 s). Three temporal samples from each movie were inspected
as contact sheets, and the movies decoded completely. This is capture/sequence inspection,
not visible paced playback, hands-on driving, listening or user acceptance.

Debug configure/build passed without project warnings. The initial full CTest run passed
614/614 before the final probe/CSV corrections. The pre-resume corrected run attempted
`ctest --preset full`: **581/616 passed; all 35 failures were GPU tests whose graphics
context could not initialize** (`GLFW: Failed to determine Monitor to center Window`).
All non-GPU cases, including the three new baseline contracts and boundary/time lints, passed.
Desktop access had been granted; direct GPU retries failed with the same missing-monitor
condition. At this checkpoint the corrected GPU matrix, remaining reels and visible demo
were verification debt. The resumed native qualification below supersedes this limitation.

### Proposed next decision

Astra found no remaining blocking code defect after the corrections. Keep R0 open for
budget/criteria agreement and human review. A provisional native **screening**
allocation for the current four-emitter/30-query CPU workload is .10 ms p95 / .15 ms p99
for the enclosing aggregate workload, with component regressions above 10% investigated.
This allocation is for this synthetic fixture, not an authority/server tick or arbitrary
future fleet size. The graphics ceilings above remain unchanged; a separate absolute wake
GPU allocation is not isolated by these whole-scene measurements; fixture screens are proposed
below. Keep production memory/network unchanged until
an explicit R1/R3 resource proposal: the present 5,320-byte player pool, 128 records and
2 births/s per continuously wet player are baselines, not permission to increase replication
traffic. CPU/GPU resource and protocol growth need their own numeric proposal before promotion.
These screening criteria have not been agreed by the user.

Recorded qualification commands (for a repeat, choose fresh output directories):

```sh
python3 tools/wake_research.py --binary build-release/wake_research --output build-release/wake-r0/gpu-verified --gpu --traffic --paths circle --emitters 1 4 --layouts dense
python3 tools/wake_research.py --binary build-release/wake_research --output build-release/wake-r0/outer-verified --gpu --traffic --location outer --paths circle --emitters 4 --layouts dense
python3 tools/wake_research.py --binary build-release/wake_research --output build-release/wake-r0/high-verified --gpu --traffic --location outer --high --paths circle --emitters 4 --layouts dense
python3 tools/wake_research_media.py --binary build-release/wake_research --output build-release/wake-r0/media-verified
./build-release/wake_research --output build-release/wake-r0/visible --path circle --gpu --visible
ctest --preset full
```

Run uncapped matrices without simultaneous GPU tests or encoding/playback. Retain the paired
media separately from timing; review their framing/motion, then request the R0 evidence review
and budget decision before R1/R2. Refresh the manifest/archive after new evidence is collected.


### Resumed native qualification — 2026-11-15

The user confirmed the earlier graphics failures followed power save. `caffeinate -di` held
the display awake during the sequential matrices, captures, visible demo and tests; `-diu`
was used for the later scene-capture correction and final suite. These temporary assertions
do not change saved power settings. All native graphics initialization succeeded on the resume.

The corrected Release GPU matrix completed **48 runs / 12 neutrality comparisons** across
four configurations, with three alternating repetitions per measurement mode. All continuously
emitting player pools remained saturated. Traffic occupancy varied from 88 to 128; combined
load peaked at 256 pieces, not 256 continuously. Substantial outside-field work remained;
record uploads were 12,288 bytes/frame, with 8 MiB stamp and 5 MiB history fields. Driver,
allocator and other scene/HDR resources are excluded from these field-memory counts.

Milliseconds, median of three baseline run percentiles:

| Fixture, all with physical traffic | GPU p50/p95/p99 | Enclosing p50/p95/p99 | GPU p95 range across runs |
| --- | --- | --- | --- |
| Basin 320x200, one emitter | 2.210 / 2.412 / 2.525 | 2.875 / 3.145 / 3.527 | 2.395–2.782 |
| Basin 320x200, four emitters | 2.894 / 3.174 / 3.355 | 3.628 / 4.002 / 4.265 | 3.172–3.188 |
| Outer 320x200, four emitters | 5.539 / 6.419 / 6.627 | 6.528 / 7.464 / 7.742 | 6.408–6.468 |
| Outer 1280x800 HDR, four emitters | 6.621 / 7.421 / 7.668 | 7.517 / 8.378 / 8.697 | 7.407–7.443 |

[GPU timings](gpu-timings.csv) retain all modes, component statistics and repetition spread;
the raw summary reports also retain absolute/percentage deltas. Wake-off GPU p95 medians
were 1.236/1.261/2.505/2.425 ms in the four rows respectively. Those differences include
interaction effects and are not isolated wake cost. The one-emitter basin's third repetition
reached 2.782 ms GPU p95 and 3.290 ms p99; preserve this variation rather than hiding it behind
the median. No cause is established. These are synchronous fixed-fixture timings, not gameplay
latency or evidence that the differently configured original graphics gates passed.

Astra reviewed the corrected GPU reports and recommends these provisional **whole-fixture GPU
screens**, based on the four-emitter median p95 plus 10%: basin low 3.491 ms, outer low
7.061 ms, outer HDR 8.164 ms. The CPU .10 ms p95 / .15 ms p99 aggregate-workload screens remain
proposals; observed corrected aggregate maxima were about .089/.092 ms. Require contemporary
alternating baseline/candidate runs and investigate relative regressions as well as absolute
ceilings. Available headroom is not permission to spend it. Existing gameplay graphics gates
remain independently applicable. These proposals await user agreement.

All **twenty scene/view pairs** completed: straight, circle, figure-eight, stop, dry interval,
reversal, teleport, serviced marina, outer and high/HDR, each with chase and stationary overview.
Each pair has one production-render movie and one height/foam movie: forty movies, each 100
frames at 10 Hz. All 200 corresponding source PNG pairs per view matched exactly, totaling
4,000 matching PNG pairs. Temporal samples from circle, stop, dry, reversal, teleport, service,
outer and high captures were inspected. Fixed-source circle patterns retain concentrated inner
fronts and separated outer pieces; stop/dry fields travel after emission ceases. The raw teleport
fixture intentionally exposes the missing-tracker-discontinuity case. These are agent inspections
of scripted fixtures, not human handling or appearance acceptance.

Capture inspection caught an additional harness issue: `SESSION_VIEW` omitted `world.scene`,
so the first service movies rendered the compiled marina despite their scene digest. Those two
movies are superseded. The harness now passes `boat_session_view()` to the renderer; its new
`wake_research_scene` contract checks authored harbor slots and hidden serviced routes at
7/12/17 seconds in production render frames. Both service views were recaptured; inspected
frames show the two authored docks and distinct moving boats. Circle chase/overview controls
remain identical across the fix (400 PNG pairs). Neutrality checks alone did not prove that
the intended authored scene was exercised; the render-frame contract and inspection address it.

**Provenance:** CPU/GPU timings belong to the original `1031166` harness; its exact executable
is retained as `build-release/wake-r0/wake_research-timing-baseline`. The manifest distinguishes
`timing_sources`/`binary` from `current_sources`/`current_binary`. Corrected service captures
and the scene contract use the updated harness. Ordinary circle image equality establishes
unchanged sampled imagery, not measured timing equivalence or an optimization. No timing win
is claimed for this scene-only wiring correction. Original service captures remain history;
the manifest's current media list points to the replacements in `media-scene-verified`.

The visible scripted rig completed **80 seconds / 9,600 ticks**, including all 3,600 measured
frames. Debug/Release builds passed without project warnings. The first awake full suite passed
616/616; after adding the scene contract, the final `ctest --preset full` passed **617/617**,
including all 36 GPU-labelled cases, in 100.33 seconds. Astra found no blocking issue in the
native evidence or narrow scene correction. No new hands-on input, audio listening, browser,
slower-GPU or user-acceptance claim is made. The next decision is R0 budget/criteria agreement
before R1 coverage or R2 geometry candidates.
