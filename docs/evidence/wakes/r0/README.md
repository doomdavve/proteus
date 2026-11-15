# R0 baseline review bundle

Owner: [wake research plan](../../../WAKE_RESEARCH_PLAN.md). Research baseline only; the
production wake source, surface, shaders, physics, capacity and protocol are unchanged.

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

R0 implementation and baseline evidence are recorded below when collected. No R1–R3 candidate
has been implemented. Budget agreement, the remaining continuity metric design, full moving
scene human review, browser/slower-GPU measurements and player acceptance remain open.
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

### Corrected baseline results — 2026-11-15

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
614/614 before the final probe/CSV corrections. The final corrected run attempted
`ctest --preset full`: **581/616 passed; all 35 failures were GPU tests whose graphics
context could not initialize** (`GLFW: Failed to determine Monitor to center Window`).
All non-GPU cases, including the three new baseline contracts and boundary/time lints, passed.
Desktop access had been granted; direct GPU retries failed with the same missing-monitor
condition. The unavailable corrected GPU matrix, stationary/turn/transient/fleet reels,
outer/high-resolution captures and paced visible demo remain explicit R0 verification debt.
No corrected GPU baseline or high-resolution performance number is claimed.

### Proposed next decision

Astra found no remaining blocking code defect after the corrections. Keep R0 open for
external verification and budget/criteria agreement. A provisional native **screening**
allocation for the current four-emitter/30-query CPU workload is .10 ms p95 / .15 ms p99
for the enclosing aggregate workload, with component regressions above 10% investigated.
This allocation is for this synthetic fixture, not an authority/server tick or arbitrary
future fleet size. The graphics ceilings above remain unchanged; a separate absolute wake
GPU allocation awaits corrected measurements. Keep production memory/network unchanged until
an explicit R1/R3 resource proposal: the present 5,320-byte player pool, 128 records and
2 births/s per continuously wet player are baselines, not permission to increase replication
traffic. CPU/GPU resource and protocol growth need their own numeric proposal before promotion.
These screening criteria have not been agreed by the user.

When a desktop monitor/context is available, resume with fresh output directories:

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
