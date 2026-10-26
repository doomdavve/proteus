# Free-riding adventure: first implementation tasks for Sol

Prepared 2026-10-25 against `7165ba5` and the uncommitted adventure-planning documents.
**Status: A0 (A0.1–A0.4) completed 2026-10-25; [A0 decision](#a0-decision-record--2026-10-25) recorded; [A1.1](#a11--explicit-map-data-and-collision-cache-construction), [A1.2](#a12--wire-physical-map-ownership-through-all-callers), [A1.3](#a13-result-2026-10-25), [A1.4](#a14-result-2026-10-25) and [A1.5](#a15-result-2026-10-25) completed 2026-10-25, [completing A1](#a1-completion--2026-10-25). [A2](#a2--tiled-current-river) began with [A2.1](#a21-result-2026-10-25) (application submission and triangle counters) and [A2.2](#a22-result-2026-10-25) (in-memory tile catalogue and indexed collision, exact against the full scan), both completed 2026-10-25, and [A2.3](#a23-result-2026-10-25) (per-tile land culling on both renderers, 1 km default tiles) completed 2026-10-25, and [A2.4](#a24-result-2026-10-26) (indexed water patches and a view-bounded CPU water surface) completed 2026-10-26 on branch `adventure-a2-4`, and [A2.5](#a25-result-2026-10-26) (same-view scaling against synthetic corridors up to 25 km; lazy CPU water tessellation) completed 2026-10-26 on branch `adventure-a2-5`, completing A2. [A3.1](#a31-result-2026-10-26) (offline bake of the first slice's water and land, route proven against collision) completed 2026-10-26 on branch `adventure-a3-1`, and [A3.1b](#a31b-result-2026-10-26) (real land heights from Baskarta, 20 m inland and 5 m near the shore) the same day. [A3.2](#a32-result-2026-10-26) (a mapped `outer` location that loads the baked slice, both renderers; GPU and CPU captures and the full suite passed) completed 2026-10-26 on branch `adventure-a3-2`. [A3.3](#a33-result-2026-10-26) (the river's buildings, roads, gardens and bridge baked into the outer map; CPU/GPU captures and the full suite passed) completed 2026-10-26 on branch `adventure-a3-3`; A3.4 is next.**
The [adventure plan](FREE_RIDING_ADVENTURE_PLAN.md) owns the larger direction and budgets;
this document owns the detailed A0, A1 and A2 task ledgers. The
[foundations plan](BOATING_FOUNDATIONS_PLAN.md#phase-w--explicit-world-ownership) owns the
world/cache contracts which A1 completes for mapped locations. [All plans](README.md).

The first batch produces a reproducible map survey and trustworthy measurements. The second
makes the current river safe to split into tiles. Completing these tasks does not yet add
Vinga, a new playable area, streaming, new weather physics or adventure rules.

## Execution contract

- Execute the requested task or batch in order. Each row is a reviewable handoff; do not
  silently implement later milestones. These boundaries create no new permission requirements.
- Inspect `AGENTS.md`, Git status and the current APIs first. Preserve existing user changes,
  including the planning files. Record the actual starting revision and relevant dirty changes;
  do not assume this preparation revision is still current.
- Keep C17/raylib, 120 Hz simulation, accepted handling, current river geometry, replay formats,
  network byte layout and nearest-neighbour presentation during A0/A1. New diagnostic options
  belong to review tools. Do not regenerate music or expand `assets/geometry/river/geometry.h`.
- Asset acquisition is explicit and separate from offline processing. Normal configure/build,
  tests and play must not fetch geodata. Keep downloads, traces and full-resolution captures
  under ignored `build/adventure/`; version small reproducible sources, manifests and summaries.
- A task completes with its artifacts, checks, evidence, limitations and next task recorded
  here and in the appropriate owner. Run focused checks while editing, then the required full
  build/CTest gate at handoff. Add visual/manual checks when the task changes those paths.
- If an external source or graphics context is unavailable, finish independent work and state
  exactly which acceptance condition remains unverified. Do not invent measurements or broaden
  scope to replace unavailable infrastructure. No accounts or paid data are prerequisites.

## Task ledger

| Task | Depends on | Deliverable | Status |
| --- | --- | --- | --- |
| A0.1 | — | Current implementation inventory, compatibility references and original river captures | Complete 2026-10-25; [evidence](adventure/baseline.md) |
| A0.2 | A0.1 | Pinned coastal source extract and acquisition manifest; terrain/weather access findings | Complete 2026-10-25; [sources](adventure/README.md#coastal-sources-a02-2026-10-25) |
| A0.3 | A0.2 | Offline coverage map, candidate route, first-slice polygon and tile/cost inventory | Complete 2026-10-25; [survey](adventure/README.md#route-and-coverage-survey-a03-2026-10-25) |
| A0.4 | A0.1 | Repeatable river frame report and controlled baseline; A0 decision record | Complete 2026-10-25; [baseline](adventure/baseline.md#a04-controlled-river-baseline--2026-10-25), [decision](#a0-decision-record--2026-10-25) |
| A1.1 | A0 complete | Explicit immutable map description and independently constructible collision cache | Complete 2026-10-25; [result](#a11--explicit-map-data-and-collision-cache-construction) |
| A1.2 | A1.1 | Borrowed map resources wired through simulation, session, fleet and networking | Complete 2026-10-25; [result](#a12-result-2026-10-25) |
| A1.3 | A1.2 | Caller-owned water-mesh caches with unchanged topology | Complete 2026-10-25; [result](#a13-result-2026-10-25) |
| A1.4 | A1.3 | Explicit CPU/GPU map binding and map-dependent scenery ownership | Complete 2026-10-25; [result](#a14-result-2026-10-25) |
| A1.5 | A1.4 | Reload/isolation/failure gate, parity evidence and A2 handoff | Complete 2026-10-25; [result](#a15-result-2026-10-25), [A2 handoff](#a2-handoff) |

A0.2/A0.3 and A0.4 have independent inputs after A0.1, but controlled performance runs must
run alone. If terrain credentials are unavailable, document that result and continue the OSM
survey; terrain import is optional in this batch. A1 requires a useful survey of the first
extension, not complete terrain coverage or a complete Boelo observation archive.

**Correction, 2026-10-26:** the A1.1/A1.4 digests recorded below (river query
`63225c36181d381d`, box `8a15797b4f84d3f3`) were Apple-clang values with multiply-add fusion,
and failed on x86-64 Linux CI. All Clang/GCC builds now use `-ffp-contract=off` (see
[testing](TESTING.md)); the current values are `2cb7d1c44dfa671a` (still 20,356 water answers)
and `ea3fae73bd61a0df` (still 7,944 boxes); the clearance digest is unchanged. The generated
`river_bridge_first_triangle` is now an enum constant so MSVC accepts it as a static
initializer. Result sections below keep the values as recorded at the time.

## A0.1 — preserve the current reference

**Read:** `docs/river/README.md`, `docs/river/survey.py`, `tools/prepare_waterfront.py`,
`src/boat_map.c`, `src/boat_water_mesh.c`, `src/boat_location.[ch]`, `src/boat_world.[ch]`,
`src/boat_session.[ch]`, `src/boat_fleet.[ch]`, `tools/boat_water_review.c`,
`tools/boat_benchmark.c`, `src/boat_profile.h`, and `docs/BOATING_PERFORMANCE.md`.

**Create:** `docs/adventure/README.md` as the survey/evidence guide and
`docs/adventure/baseline.md` as a dated summary. These paths are proposed and do not exist yet.
Use `build/adventure/a0-original/` for raw outputs. No runtime code changes in this task.

1. Record hardware, OS, compiler, build type, Git revision/dirty state, physics/protocol
   versions, source and geometry hashes, virtual/display resolution, AA and VSync/frame cap.
   Label unknown fields rather than assuming monitor or GPU settings.
2. Inventory map/cache callers using `rg`. Include `waterfront_corner.c`'s lazy dressing,
   CPU drawing, GPU main/reflection/shadow builders, `boat_net_content_digest()`, validators
   and review tools. Record owners, allocation/destruction points and hidden global caches.
3. Record the copy contract: `BoatWorld` and some `BoatSession` uses are copied by value;
   `boat_fleet_world()` returns a copy and rebinds wake pointers; checkpoints contain mutable
   fleet state, not resources. List reset, prediction, late-join and cleanup callers affected
   by adding a borrowed map reference. Do not put heap ownership into these copies casually.
4. Run the current route checks and captures below. Inspect dock, under-bridge, beyond and
   look-back views in CPU/GPU output. Record current physical route end tick/pose and the
   basin benchmark's full named-state trace. Retain full logs/images outside Git and publish
   concise observations/checksums. Obtain the current content digest through existing tools
   if exposed; otherwise add its before/after comparison when A1's validator is implemented.

Existing commands, run from the repository root after the normal Debug build:

```sh
mkdir -p build/adventure/a0-original
./build/boat_water_review --check
./build/boat_water_review --check --waves
./build/boat_water_review --renderer cpu --output build/adventure/a0-original/river-cpu
./build/boat_water_review --renderer gpu --output build/adventure/a0-original/river-gpu
./build/boat_water_review --renderer gpu --waves --output build/adventure/a0-original/river-waves
./build/boat_benchmark --scenario wake-loop --mode simulation --renderer none --warmup 120 --frames 1800 --output build/adventure/a0-original/wake-loop
```

The GPU review currently always uses 4×4 AA and reports **submission** timings. Its hidden
sampling is 10 Hz, whereas visible playback renders at nominal 60 Hz. The basin benchmark
supports only `wake-loop`, `simulation`, `none`. Neither tool currently proves full river
display-frame performance. The profiler retains only 240 frames; a final overlay percentile
is not a whole-voyage percentile. Record these limitations explicitly.

**Done when:** the inventory identifies every map-dependent cache and affected copy/lifetime
path; build/CTest and route results are recorded; representative captures are inspected or
their unavailable status is explicit. The baseline guide links to the actual artifact paths.

**A0.1 result, 2026-10-25:** The [survey/evidence guide](adventure/README.md) and
[dated baseline](adventure/baseline.md) record the current map/cache/copy inventory,
compatibility versions and hashes, physical flat/wave route endpoints, CPU/GPU flat/wave
inspections and the full basin named-state trace. Raw images, CSVs and logs are in ignored
`build/adventure/a0-original/`. Debug configure/build and all 110 active CTests passed
(15 retired tests disabled); exact commands, platform and limits are in the baseline.
No runtime code or map data changed. A0.2 is next;
A0.3/A0.4 and all A1 tasks remain pending.

## A0.2 — acquire a reproducible coastal sample

**Read:** the source table and licence/provenance notes in the
[adventure plan](FREE_RIDING_ADVENTURE_PLAN.md#data-sources-and-acquisition), plus the retained
river source manifest. Recheck provider documentation at acquisition time; do not guess
current download paths, authentication requirements or observation parameter IDs.

**Create:** `docs/adventure/sources.json`, a small clipped source under
`docs/adventure/sources/`, and `tools/prepare_adventure_sources.py`. The latter processes
already-downloaded files offline. Keep acquisition commands in `docs/adventure/README.md`;
a separate small fetch helper is optional if it materially improves repeatability.

1. Use a dated Sweden/regional OSM extract, pinning its checksum. Download once into
   `build/adventure/downloads/`, then extract locally. Avoid expanding the existing approach
   into hundreds of requests to the editing map API. Record extraction tool versions.
2. Start the source envelope with the union of current river coverage and Vinga, expanded by
   approximately 2 km. Transform bounds from the existing EPSG:3007 origin. Expand further
   whenever coastline assembly or route exploration needs more context; the first download
   box is not the final playable boundary.
3. Preserve required nodes, complete ways and relation members through extraction. Retain
   coastline/water, buildings/parts, bridge/supports, roads and named landmarks. Preserve
   source IDs/versions and log incomplete features. Omit contributor identity metadata.
4. Use processed land polygons if needed to close coastal topology, with their own source
   date/hash. Log conflicts with OSM water/coastlines instead of silently overlaying differing
   snapshots. Keep the existing compiled river untouched even if newer OSM differs locally.
5. Attempt one small terrain sample covering a candidate island only if access is already
   available. Record resolution, CRS, vertical datum, nodata and terms; never commit tokens.
   If unavailable, record the specific access dependency and an authored-relief fallback.
6. Preserve an available dated Vinga A wind/gust/direction sample and nearby wave-station
   metadata while the event is recent. Discover parameter/period links from SMHI. Include
   UTC times, units and quality flags. If the reported event cannot be retrieved, record the
   gap; this does not block geography work or justify calling a later sample Boelo data.

Manifest records must include source URL/product, retrieval UTC, source snapshot/valid time,
raw and clipped hashes, source envelope, coordinate/height references, licence/attribution,
tool versions, processing command, feature counts and known omissions. Optional sources have
an explicit `unavailable`/`not_acquired` state and reason, not fictitious empty observations.
Keep nondeterministic retrieval metadata separate from deterministic derived content hashes.

Define a compact normalized format consumed by A0.3. Prefer the retained river extract's
geometry/tag conventions where possible. GeoJSON, if used, carries longitude/latitude;
projected east/north routes belong in a separately labelled JSON record. Use stable sorting,
fixed numeric precision and deterministic gzip metadata for checked-in compressed output.
Keep large national PBF/terrain/coastline files out of Git; retain a bounded derived sample
with attribution and reproducible extraction instructions.

**Done when:** the clipped source can be regenerated from the pinned inputs; another offline
run yields the same content hash; source gaps are listed. A small fixture catches missing
relation members and rejects malformed coordinates. Normal build/test still needs no geodata
network requests. Update `docs/adventure/README.md` with exact installed tool versions/commands.

**A0.2 result, 2026-10-25:** Started from `0f45971`, with the previous session's
uncommitted `tools/prepare_adventure_sources.py` draft and an untrimmed 9.6 MB sample. Both
were replaced. [`sources.json`](adventure/sources.json) pins the Geofabrik Sweden
`sweden-261024` PBF (MD5 rechecked against Geofabrik; snapshot 2026-10-24T20:20:51Z), the
`osmium extract` corridor and the 2026-10-25 processed land polygons. Each source records
its URL, retrieval time, hashes and licence. The [source guide](adventure/README.md#coastal-sources-a02-2026-10-25)
holds exact acquisition/offline commands and tool versions. The checked-in OSM corridor,
clipped land polygons and Vinga A/wave-buoy storm slice regenerate offline; a second
extraction gave an identical PBF; two normalizations gave byte-identical outputs; `--check`
passed. Fixtures cover missing relation members, malformed coordinates and the land-shapefile
reader/seam union. OSM coastline and land polygons agree (0.000 m maximum deviation).
Ten relations crossing the box or Denmark remain incomplete and are listed. Terrain was not
acquired (authenticated Geotorget STAC account required); the authored-relief fallback is
recorded. Vinga A's quality-G observations peak at a 29.7 m/s gust at 07:00Z. The nearest
current wave buoys are 72–103 km north and give regional references only; mean wave
direction and Vinga-area waves are unavailable. EMODnet was not acquired. The OSM sample is
6.3 MB compressed, which A0.3 may narrow. No runtime code or compiled geometry changed.
Debug build passed; all 89 non-GPU CTests passed. The 21 GPU tests could not create a
window in this session (no display, also with graphics access) and remain unverified; see
the [verification record](adventure/README.md#a02-verification-2026-10-25).
A0.3 is next; A0.4 remains independent.

## A0.3 — survey the route and choose the first slice

**Create:** `docs/adventure/survey.py`, `coverage.svg`, `coverage.png`, `summary.json`,
`route.json` and `first-slice.json`. Reuse small geometry/projection helpers from the river
survey where sensible; do not import it in a way that rewrites its historical artifacts.
Pin actual Python dependencies in a local requirements file, using the existing project
versions where compatible. Do not upgrade working repository-wide tooling for this survey.

1. Assemble land/ocean/river geometry before clipping. The old survey assumes a single river
   polygon without holes; that is unsuitable for archipelago islands. Support multipolygons,
   islands, inland holes and bridge legs, and explicitly distinguish source boundaries from
   real shoreline. Preserve mapped boundaries through repairs; list unresolved topology.
2. Plot current coverage, source margin, candidate extension, islands/landmarks and unknown
   areas in one metre-based frame. Mark the existing dock and bridge, river-mouth approaches,
   Vinga, candidate sheltered stops and an optional shorter-start location.
3. Trace one candidate water route and at least one useful shelter detour where data permits.
   Measure length and minimum mapped shore clearance. Use a conservative 8 m route envelope
   for this survey, consistent with the earlier river study, and distinguish it from exact
   swept-hull verification. Mark unresolved depth/overhead issues; no navigation certification.
4. Select a small connected first-slice polygon extending a few kilometres beyond the bridge
   toward one candidate shelter. Include the joining area and surrounding view context.
   Report travel-time estimates at 10/15/20 m/s separately from the 5–10 minute experience
   target. Do not stretch geography or assert a play duration from distance alone.
5. Overlay a 512 m grid anchored at the existing projected origin. Compute tile IDs with
   mathematical floor, including negative coordinates. Compare 1 km tiles. Count empty sea,
   shore and urban tiles; water/land area, shoreline edges, buildings, metadata bytes and
   representative estimated geometry/LOD bytes. Distinguish estimates from actual bake sizes.
6. Inspect the terrain sample if acquired: island relief, datum and coastline alignment, with
   nodata clearly masked. If unavailable, keep that gap visible on the map/cost summary.

Add offline self-checks for two adjacent coastal tiles, an island spanning their seam, a
water/land hole, negative tile IDs, an incomplete coastline and a bridge opening with solid
legs. Tile boundaries must not become shoreline. Validate the surveyed route against the
assembled geometry and source margin. Repeat with networking disabled and compare deterministic
JSON/SVG content; inspect the map visually rather than accepting only geometry assertions.

**Done when:** there is a measured route, justified first-slice polygon, source-gap map and
tile/cost table. `summary.json` identifies all inputs and its CRS/origin. The report chooses
512 m or 1 km as the initial experiment, with a reason, without committing to a runtime format.

**A0.3 result, 2026-10-25:** Started from a clean `66ccfc6`. Added
[`survey.py`](adventure/survey.py), pinned [`requirements.txt`](adventure/requirements.txt)
and the generated [coverage map](adventure/coverage.png) (with SVG),
[`summary.json`](adventure/summary.json), [`route.json`](adventure/route.json) and
[`first-slice.json`](adventure/first-slice.json). Water is assembled from the land polygons
plus OSM inland water, because the land polygons treat the river as land. It keeps islands,
holes and 15 in-water bridge supports. Source-box edges are classified separately from the
583 km of mapped shoreline. No water topology was unresolved. The candidate route runs from
the dock under Älvsborgsbron through Rivöfjorden to Vinga gästhamn: 19.63 km, minimum 27.6 m
mapped shore clearance under the 8 m envelope, 16.4 min at 20 m/s. A Långedrag old-harbour
shelter detour measures 2.09 km. The optional Saltholmen start reaches Vinga in 14.95 km.
The 10.9 km² first slice joins the crop and runs past Nya Älvsborg to Långedrag. Its
dock-to-shelter route is 6.02 km (5.0–10.0 min at 20–10 m/s). These travel times are not
play-time estimates. **512 m tiles** are the initial experiment, not a runtime format. Reason:
a smaller largest tile (123 vs 429 KiB modelled detail) and 40% geometry-free sea tiles along
the route (14% at 1 km). Terrain, depth and overhead clearance remain gaps. The cost model
gives 0.40× the compiled river arrays, so its bytes are estimates only. Outputs were
byte-identical across normal, network-denied and fresh-venv runs. The coverage PNG was
inspected (agent review). Debug configure/build passed, and
`ctest --test-dir build -j 4 --output-on-failure` passed all 110 active tests in 142.37 s,
including the 24 GPU-labelled ones (15 retired tests disabled; log
`build/adventure/a0.3-ctest.log`). See the
[A0.3 verification](adventure/README.md#a03-verification-2026-10-25). No runtime code, compiled geometry or assets changed. A0.4 is next; A1 remains pending.

## A0.4 — measure the real river workload

**Edit:** `tools/boat_water_review.c`, `src/boat_profile.[ch]` only for reusable statistics,
and `CMakeLists.txt` for a focused report-contract test. Add a small report validator under
`tools/`; update `docs/BOATING_PERFORMANCE.md`, `docs/TESTING.md` and the adventure baseline.
Keep the production simulation/renderer algorithms unchanged.

Add an explicit benchmark mode to the existing route review, preserving all current modes.
This was the proposed interface. It is implemented as written, with `--renderer gpu` optional
because it is the default:

```text
boat_water_review --benchmark PREFIX --renderer gpu --aa off|4x4 [--waves] [--visible]
```

1. Reuse the existing route controller and tick ordering. Draw every two 120 Hz ticks, with
   120 rendered warmup frames; keep the route continuous through warmup. Finish at the normal
   destination or existing timeout. Exclude screenshot/export/shore-distance diagnostics from
   timed work; capture comparison images in a separate run. Keep baseline `--output` behavior.
2. Use the monotonic `boat_profile_time()` clock, not the review's current `TIME_UTC` helper.
   Preallocate bounded sample storage and write JSON/CSV after measurement. Reject unsupported
   option combinations, malformed output paths and truncated report writes with nonzero exit.
3. Record per-frame tick range, pose/route segment, tick count, simulation duration, CPU render
   submission duration, visible presentation/wait duration and complete wall-frame duration.
   Include water-patch rebuild count/capacity and available scene submission/triangle counts;
   label application submissions separately from actual driver draw calls. Report unavailable
   metrics as null. Do not add a profiling framework or change the main application loop.
4. Report p50/p95/p99/max, sample/warmup counts and render-deadline misses with their precise
   threshold. Mark hidden versus visible, pacing and AA. A fixed-two-tick review measures a
   render workload; it is not the app's real-time accumulator or dropped-tick validation.
5. GPU submission is not GPU completion. If supported, collect asynchronous GPU timer samples
   without blocking each frame; otherwise leave `gpu_ms` unavailable and explicitly defer the
   completed-GPU budget claim. Do not use per-frame `glFinish()` as the normal benchmark.
   Actual scan-out timing is also unavailable without separate measurement; CPU frame intervals
   must not be labelled hardware presentation times.
6. On a Release build, run three sequential trials of flat/waves × AA off/4x4. No CTest,
   downloads, other profiler or competing game processes during these trials. Save full samples
   under `build/adventure/a0-performance/`; publish median-of-run summaries and all outliers.
   Run a separate visible voyage and main-app profiler check to expose pacing/accumulator issues.

Existing commands for preparing the Release build and checking the app (these work today):

```sh
cmake -S . -B build/adventure-release -DCMAKE_BUILD_TYPE=Release
cmake --build build/adventure-release -j
./build/adventure-release/wake_velocity --location river --renderer gpu --waves --profiler --demo --frames 600
```

The exact benchmark invocations are in the
[benchmark guide](BOATING_PERFORMANCE.md#river-frame-benchmark--2026-10-25). The
report-contract test checks row counts, warmup exclusion, percentile recomputation, finite
values, route completion and final physical-state parity against the existing controller.
Performance thresholds are engineering reports, not flaky universal wall-time CTest assertions.

**Done when:** the baseline distinguishes simulation, render submission, completed GPU work
if available, and frame/pacing costs. Record machine-specific targets and remaining measurement
gaps. Combine A0.2/A0.3's findings into the A0 decision: first slice, tile-size experiment,
resident collision/coarse-view estimate and next ownership task. Do not declare 60 FPS solved.

**A0.4 result, 2026-10-25:** Started from a clean `ba45569`.
`boat_water_review --benchmark PREFIX --aa off|4x4 [--waves] [--visible]` writes per-frame
CSV and summary JSON. Frames are drawn every two ticks on the continuous route, with 120
warmup frames. It reports simulation, CPU submission, present/wait, frame and start-to-start
times, plus asynchronous `GL_TIME_ELAPSED` scene GPU time. It also reports wave rebuilds,
deadline misses and the exact final state; unavailable counters are null.
`boat_metric_stats_sort()` gives `boat_profile` unbounded whole-run percentiles. The
production simulation, renderer and main loop are unchanged. Existing `--output` captures
are byte-identical to A0.1.

Two tests were added: `boat_river_benchmark_options` (cpu) and the GPU
`boat_river_benchmark_contract` (row/warmup/tick/percentile/deadline recomputation, route
completion, final-state parity with `--check --waves`). On an M1 Max, Release, the three-trial
hidden matrix completed the route with identical final states in all runs. Frame p95 was
5.2–5.7 ms and p99 6.0–6.9 ms, with no frame above 16.67 ms. GPU scene p95 was 2.6–3.6 ms.
CPU submission, at 3.7–3.9 ms p95, is the largest cost.

The visible paced voyage and the app profiler both run at **58.4–58.6 Hz**, not 60 Hz,
because the 60 FPS cap overshoots. The app's accumulator uses periodic three-tick frames.
A0.4 recorded this as a gap without fixing it; the
[frame pacer](BOATING_PERFORMANCE.md#frame-pacing--2026-10-25) addressed it afterwards. Debug CTest passed 112/112 active tests in 175.96 s.
Remaining gaps: draw-call/triangle counters, scan-out timing, other hardware and human
acceptance. Full evidence is in the
[A0.4 baseline](adventure/baseline.md#a04-controlled-river-baseline--2026-10-25).

### A0 decision record — 2026-10-25

- **First slice:** A0.3's [10.9 km² Långedrag slice](adventure/first-slice.json). It joins
  the river crop and runs past Nya Älvsborg to Långedrag old harbour, a 6.02 km mapped
  route with 27.6 m minimum clearance. It needs no source edge, invented closure or terrain.
  Terrain, depth and overhead clearance remain gaps.
- **Tile experiment: 512 m**, compared against 1 km. This is not a runtime format. The
  largest modelled tile is smaller (123 vs 429 KiB), and 40% of route tiles have no geometry.
  A0.4 shows that CPU submission, not GPU or simulation time, dominates the current river.
  A2 should therefore judge tile size by submissions and culling. A2 must first add
  application submission and triangle counters, which A0.4 could only report as unavailable.
- **Resident estimate (not measured):** the current collision cache holds 547 water triangles
  and allocates 13,128 B of shore-edge pairs. Each query scans every water triangle and every
  shore edge. The surveyed crop plus slice has 5,779 shore edges, about 7.6× the crop's
  760. That is roughly 46 KB of edge pairs plus shared vertices, well under 0.5 MB. Linear
  query cost is the problem, not memory, so the A2 indexed collision is required. The
  modelled crop-plus-slice coarse view is 1.42 MB and detail 2.65 MB (A0.3 model). Scaled to
  the river bake, that is about 6.9 MB of resident detail. These are survey estimates, not
  bake or process sizes.
- **Budget on this machine:** the current river uses under half of the provisional ≤12 ms
  p95 active-work target. Display pacing missed 60 Hz before any map growth (fixed after A0
  by the [frame pacer](BOATING_PERFORMANCE.md#frame-pacing--2026-10-25)). A1.5 repeats this
  matrix and uses >10% p95 growth as its investigation trigger.
- **Next task: A1.1**, an explicit immutable map description and an independently
  constructible collision cache. Ownership changes begin there; nothing in A0 changed
  runtime map ownership.

## A1 resource contract — decide this before editing callers

Use a small external resource owner, provisionally `BoatWorldResources`, for the prepared
immutable collision map. `BoatWorld` borrows it; `BoatSession`, fleets and prediction copies
do not free it. The application/test owner creates it before sessions and destroys it after
all borrowers and views. A basin resource is a valid empty-map resource. Independent worlds
may use separate owners; predicted copies may share the same immutable owner.

This adapts the original foundations W3 proposal to the subsequently implemented copy and
checkpoint contracts. Never shallow-copy heap-owning cache structs. Keep resource pointers
out of checkpoints, named physical-state hashes and protocol serialization. Do not add a
default-world singleton or a general resource manager. Map preparation can allocate in the
core during explicit init; file loading, windows and GPU work remain outside the core.

CPU and GPU views own separate mutable water/scenery caches bound to explicit map descriptions.
Destruction order is GPU resources with context alive, CPU view caches, sessions/fleets, map
resources, then graphics context. Reset/respawn reuses unchanged resources. Partial init is
safe to destroy and must report failure before the first mapped simulation tick.

## A1.1 — explicit map data and collision-cache construction

**Edit:** `src/boat_location.[ch]`, `src/waterfront_city.[ch]`, `src/boat_map.[ch]`,
`tools/validate_boat_world.c` or a focused map validator, and its CMake registration.

1. Add a read-only map description containing the existing mesh and bridge-triangle metadata;
   bind it to the river definition, with no map for the basin. Keep current compiled arrays,
   triangle ordering, colours and metadata inference. Explicit new bake metadata comes in A2.
2. Move `shore`, `shore_count`, `water_count` and mesh identity into a caller-owned `BoatMap`.
   Its init takes explicit data and returns failure; queries take `const BoatMap *` and allocate
   nothing. Keep the exhaustive algorithms, clearance comparison, tie order, X reflection and
   contact normals unchanged. This task is ownership work, not collision optimization.
3. Allow a narrowly scoped legacy wrapper for existing callers during this intermediate task,
   backed by the same implementation, not a second algorithm. List every remaining wrapper
   caller. A1.2 must remove it; A1 is not complete while it remains.
4. Test two map owners simultaneously, including a tiny synthetic map different from the
   river, destroy/recreate one, and query the survivor. Include zero/invalid/partial-init
   cleanup and allocation failure where the existing test infrastructure can inject it.

**Done when:** explicit map construction/query/destruction works independently, existing
river behavior is unchanged, and the temporary compatibility surface is fully enumerated.

**A1.1 result, 2026-10-25:** Started from a clean `413460d`.

- **Description:** `BoatMapDescription` (`src/boat_map.h`) holds the mesh and
  `bridge_first_triangle`. The compiled river crop is `waterfront_city_river_map`, with
  unchanged arrays, ordering, colours and water-first inference. `BoatWorldDefinition.map`
  points to it for the river and is NULL for the basin. The `mapped` flag remains; A1.2
  validates that the two agree.
- **Cache:** `BoatMap` is caller-owned and holds the borrowed description, sorted shore edges
  and counts. `boat_map_init()` returns false for a missing description or mesh, or no leading
  water, and leaves a zeroed map. `boat_map_destroy()` is idempotent, and an unready map has
  no water. Queries take `const BoatMap *` and do not allocate. The exhaustive point-in-triangle
  and nearest-edge algorithms, tie order, X reflection and normals are the previous code moved
  over unchanged.
- **Compatibility surface (removed by A1.2, kept here as history):** `boat_map_legacy_water()`,
  `boat_map_legacy_water_contact()` and `boat_map_legacy_destroy()`. They lazily prepare one
  process-global `BoatMap` of the river through the same functions. Callers by file (call count):
  - Runtime: `src/boat.c` (1, hull contact) and `src/boat_view.c` (4, particle/spray
    filtering). Also `tools/wake_velocity.c` (2: spawn check and shutdown) and
    `tools/boat_water_review.c` (11).
  - Validators: `validate_boat` (8), `validate_traffic` (7), `validate_harbor` (5),
    `validate_boat_race` (5), `boat_race_trial` (4), `validate_boat_core` (4),
    `validate_boat_clock` (2), `validate_boat_banking` (2), `validate_boat_environment` (2),
    `validate_boat_net` (2) and `validate_boat_world` (1). `validate_boat_map` exercises
    the wrapper deliberately.

  The description accessors `waterfront_city_mesh()` and
  `waterfront_city_is_bridge_triangle()` also remain. They are used by `boat_net_protocol.c`
  (content digest, A1.2), `boat_water_mesh.c` (A1.3), and `waterfront.c`,
  `waterfront_corner.c` and `renderer_gpu.c` (A1.4). Review/validator tools also use them.
- **Test:** the new `boat_map` CTest (`tools/validate_boat_map.c`, labels `core;cpu`, core
  library only) does the following:
  - Checks definition binding and bridge metadata.
  - Covers zeroed, NULL, mesh-less, empty and land-only preparation, which must fail cleanly
    and overwrite stale fields.
  - Uses a synthetic 10 m square map with exact shore count, clearance and normals.
  - Keeps two river owners and the square alive together, destroys and recreates one, and
    queries the survivors.
  - Rebuilds the legacy cache while an explicit owner is alive.

  Both the explicit and legacy paths reproduce a 103,684-query river digest
  (`63225c36181d381d`, 20,356 water answers). That grid covers clearances 0/0.5/3/12 m over
  the crop plus a 40 m margin, and was recorded from the pre-change code at `413460d`.
- **Verification (M1 Max, macOS 26 / Darwin 25.6, Apple clang 21, Debug):**
  - The build is warning-clean, and CTest passed 114/114 active tests in 197.84 s
    (15 retired tests disabled).
  - The `boat_water_review --check` and `--check --waves` logs are identical to A0.1 apart
    from the `final_state` line, which A0.1 did not yet print. The routes end at tick 12125
    at (1088.337, −947.588) and (1088.350, −947.653).
  - All 18 CPU and 18 GPU review PNGs (`build/adventure/a1-1/`) are byte-identical to
    `build/adventure/a0-original/`.
  - `validate_boat_map` built standalone with `-fsanitize=address,undefined` passed. macOS
    ASan has no leak detection, so leak freedom is shown by the destroy paths, not a tool.
- **Limitations:**
  - No allocation-failure injection exists, so `malloc` failure is code-inspected
    (it returns false with the map zeroed), not tested.
  - Physical callers still share the one global cache, so two worlds cannot use different
    maps yet. Nothing in sessions, fleets, prediction or the content digest changed.
  - No visual or manual playback check was needed: rendering code was not touched and the
    captures are identical.

**Next: A1.2.**

## A1.2 — wire physical map ownership through all callers

**Edit:** `src/boat_world.[ch]`, `src/boat_session.[ch]`, `src/boat.c`,
`src/boat_fleet.[ch]`, affected prediction/net client/server initialization,
`src/boat_net_protocol.c`, `tools/wake_velocity*.c`, server/bot/review tools and validators.
Use A0.1's caller inventory; these are entry points, not an exhaustive filename list.

1. Introduce the resource owner above and bind its read-only map to each `BoatWorld`.
   Thread the reference through init/reset/join paths. Validate map/definition consistency;
   an invalid mapped resource fails initialization, not an ordinary shoreline query.
2. Replace every physical no-context map call with the correct world's map. Fleet separation,
   spawn checks, scripted actors and standalone validators must use explicit resources too.
   Delete the A1.1 legacy cache/wrappers after the last caller moves.
3. Preserve value-copy semantics and wake-pointer rebinding. Copying/resetting a session or
   restoring a fleet checkpoint neither clones nor destroys shared immutable map resources.
   Headless authority/bots load no renderer and open no graphics context.
4. Make the existing content digest walk the definition-bound geometry in the same canonical
   order. For the unchanged basin/river content its value must remain unchanged. Compare old
   and new values, checkpoint bytes and named physical traces; do not bless new fixture hashes
   to hide an ownership regression. No protocol/physics revision bump should be needed.

**Done when:** all physical map queries have explicit ownership; two worlds can step interleaved;
copy/reset/prediction/restore, core boundaries and loopback checks pass. No resource leaks or
double frees, and unavailable lifecycle checks are identified.

### A1.2 result, 2026-10-25

Started from a clean `ef2be24`.

- **Owner:** `BoatWorldResources` (`src/boat_world.h`) pairs a definition with its prepared
  `BoatMap`. `boat_world_resources_init()` fails, leaving the owner zeroed, for a missing
  definition, a `mapped` flag that disagrees with `map`, or a map that cannot be prepared.
  The basin is a valid owner with an empty map and no allocation. Destroy is idempotent.
- **Borrowing:** `BoatWorld` keeps `definition` and gains `resources`. Shoreline queries use
  `&world->resources->map`. `boat_world_init()`, `boat_session_init()` and
  `boat_fleet_init()` take the owner. World init asserts that it is ready; fleet init returns
  false instead. `boat_world_restart()` gives `boat_session_restart()` new water and keeps
  the borrowed owner. Value copies of worlds, sessions and fleets, prediction copies and
  checkpoint restores copy only the pointer. Nothing in them frees it, and `BoatFleetState`,
  physical hashes and the wire format still hold no pointer.
- **Networking:** `BoatNetServerConfig.resources` replaces `location`; the accepted location
  is the owner's definition ID. `BoatNetClientConfig.resources` is a table of
  `BOAT_LOCATION_COUNT` owners indexed by location. A bootstrap for an unready or mismatched
  entry takes the existing bootstrap-failure path. `boat_net_content_digest()` walks
  `definition->map->mesh` in the same order, and its value is unchanged at
  `f557546865c93b69`. That was checked by linking `ef2be24`'s `boat_net_protocol.c`
  against the new core and printing both. No protocol or physics revision changed.
- **Callers:** `boat.c` (hull contact) and `boat_view.c` (race buoys, current markers,
  particles) query their world's map. `wake_velocity` owns one resource per location for its
  lifetime and passes the table to `net_study_connect()`. Its spawn check and replay playback
  worlds use the session's owner. `boat_server` owns the room location's resources.
  `boat_bot` owns all locations, because the room picks one, and releases them after the
  session loop. Validators and review tools open per-location owners in `main` through
  `tools/boat_test_world.h`. Fixed river-geometry checks use its river map
  (`test_river_water`), and session/fleet checks use the world's own map.
- **Removed:** `boat_map_legacy_water()`, `boat_map_legacy_water_contact()`,
  `boat_map_legacy_destroy()`, and the process-global cache behind them. No physical caller
  queries a map without an owner. The description accessors `waterfront_city_mesh()` and
  `waterfront_city_is_bridge_triangle()` remain for the water mesh (A1.3), scenery and GPU
  (A1.4), and review/validator tools.
- **Test:** `validate_boat_map` (`boat_map`, core) no longer exercises the wrapper. It adds:
  - Resource-owner lifecycle: zeroed, basin, repeated destroy, and four invalid definitions
    (NULL, unmapped river, mapped basin, land-only map) that fail with stale fields cleared.
  - A ready check that rejects a foreign description, and the recorded 103,684-query digest
    through an owner.
  - Two river owners, a shared owner, a value copy and a synthetic 10 m pond world, stepped
    interleaved. All river runs match a run made alone bit for bit. The drive ends held at
    the east bank 25 m from its start, so the map shapes the trace. The pond hull is held by
    its own shore.
  - A fleet checkpoint restored into a fleet on a second owner, plus a value copy. After
    1200 more ticks both have the original's hash and still borrow their own owners.

  `validate_boat_net`'s mapped room also checks that mirror, prediction and authority borrow
  the same owner.
- **Verification (M1 Max, macOS 26 / Darwin 25.6, Apple clang 21, Debug):**
  - The build is warning-clean. CTest passed 114/114 active tests in 190.77 s (15 retired
    tests disabled), including `boat_core_boundary`, `boat_net_boundary` and
    `boat_net_loopback`. No fixture hash or expected value was changed.
  - The `boat_water_review --check` and `--check --waves` logs match A0.1 apart from the
    `final_state` line. That line is bit-identical to A1.1 in the CPU review log.
  - All 18 CPU, 18 flat-GPU and 18 wave-GPU review PNGs (`build/adventure/a1-2/`) are
    byte-identical to `build/adventure/a0-original/`.
  - `wake_velocity --renderer cpu --screenshot` (basin and river, `--time 3`) completed.
  - A real ENet room, `boat_server --location river`, served one predicting `boat_bot` and
    one resetting `boat_bot`. Both joined `location=river`, ended `final=consistent`, and
    the reset completed. The server reported no faults.
- **Limitations:**
  - No sanitizer run for A1.2. A standalone ASan/UBSan build of `validate_boat_map`,
    `validate_boat_net` and `validate_boat_fleet` was prepared but not run. Leak and
    double-free freedom rests on code inspection and the destroy paths.
  - There is still no allocation-failure injection.
  - Validators and review tools exit through `CHECK` without closing their owners.
  - `boat_bot` keeps a dangling owner pointer in its static client after releasing the
    owners. Nothing reads it.
  - Harbor/traffic queries, the water mesh, scenery and GPU still read the compiled river
    through process-lifetime data or caches (A1.3/A1.4).
  - No windowed or manual playback check: input, motion, windowing and audio were unchanged,
    and the review captures are identical.

**Next: A1.3.**

## A1.3 — make water-mesh caches belong to views

**Edit:** `src/boat_water_mesh.[ch]`, `src/boat_gpu.[ch]`, `src/boat_view.[ch]`,
their CPU/GPU callers and water/world validators.

1. Move `surfaces`, `candidate_surface`, `patches` and `patch_centers` into an explicit
   `BoatWaterMeshCache`. Bind it to the map description; replace the basin/mapped boolean as
   cache identity. GPU and CPU views receive their own cache lifetimes.
2. Preserve tessellation, fine/ordinary grids, 12 m snap rule, wave recipes and patch layout.
   Keep allocations/rebuilds out of the physical tick. A queried borrowed mesh remains valid
   only until mutation/destruction of its own cache, never another view's cache.
3. Update all destroy/reset/failure paths. Do not copy a cache containing interior pointers.
   A screenshot or alternate view must not invalidate the live view's pending mesh reference.
4. Compare topology, vertex values, physical traces and representative A0.1 frames. Interleave
   two river caches at different patch centres and a basin cache; destroy/rebuild one and
   verify the others. Include the existing 5.99/6.01 m snap-boundary fixture.

**Done when:** no mutable process-global water cache remains; CPU/GPU rendering retains the
current appearance and rebuild behavior. Indexing and bounded outer water rings remain A2 work.

### A1.3 result, 2026-10-25

Started from a clean `2ea4755`.

- **Cache:** `BoatWaterMeshCache` (`src/boat_water_mesh.h`) holds what used to be four static
  arrays: the CPU reference surface, the basin field grid, the ordinary and arcade GPU rest
  patches, and their snapped centres. It starts zeroed and is bound to the
  `BoatMapDescription` it last built from; NULL means the 240 × 160 m basin. That pointer
  replaces the basin/mapped boolean as the cache's identity. Map triangles come from
  `map->mesh`, so `boat_water_mesh.c` no longer includes `waterfront_city.h`.
  `boat_water_mesh_cache_destroy()` frees only its own storage, leaves the cache zeroed and
  is idempotent. `boat_water_mesh_destroy()` and the statics are gone, and no mutable
  process-global water-mesh state remains.
- **Rebinding:** a call for a different description rebinds the cache. Its map-dependent
  surface and patches rebuild on that call and reuse their storage; the field grid is
  map-independent and kept. Foundations W2 item 2 says one cache per definition. This is
  looser: one map at a time, because a single `wake_velocity` view outlives location changes.
- **Views and lifetimes:** a returned mesh is borrowed from its cache until the next call on
  that cache or its destruction. Other caches never invalidate it.
  - `BoatGpu` embeds its own cache, which `boat_gpu_destroy()` releases (including after a
    failed init). `wave_mapped` became `wave_map`, the description whose patch is uploaded.
  - CPU `boat_draw()`, `boat_draw_with_ghost()` and `boat_draw_scene()` take the view's
    cache after the renderer. `wake_velocity` owns one CPU cache next to its renderer, so a
    CPU screenshot cannot invalidate the GPU view's patch.
  - Validators and review tools own one cache per renderer and destroy it with the renderer.
    `harbor_review` passes `waterfront_city_river_map` explicitly (scenery binding is A1.4).
  - The header says never to copy an initialized cache, because its meshes point into its
    own storage. Neither `BoatGpu` nor any cache is copied.
- **Unchanged:** tessellation, the 0.75/1.5 m grids, the 12 m snap, colours and patch layout.
  The physical tick never touches a cache.
- **Behaviour differences:**
  - The old globals kept basin and river slots side by side. A view that changes location now
    rebuilds that location's surface or patch once, at the change (settings, title or network
    join), never per frame. The GPU view already re-uploaded its patch on a map change.
  - A failed surface allocation frees that surface only, where the old code freed every
    cache. A failed patch or field build resets its count and retries, as before.
- **Test:** `boat_handling` (`validate_boat`) runs the existing surface and patch checks
  through explicit caches. Its new `validate_water_mesh_caches()`:
  - builds each reference alone in a fresh cache;
  - holds two river caches together (ordinary at (12, −125), arcade at (170, −250)) with a
    basin cache, and checks all three against their references;
  - drives the 5.99/6.01 m snap-boundary pair through the second river cache while the
    others' borrowed meshes stay byte-identical;
  - destroys that cache twice and rebuilds it, checking that the survivors' storage pointers
    and values do not change;
  - clips a patch to a synthetic 10 m pond map and rebinds pond → river → pond, reproducing
    each reference;
  - builds pond and basin CPU surfaces in separate caches, which survive another cache's
    destroy and rebuild.
- **Topology parity:** a scratch digest tool (not checked in) hashed the vertex bytes,
  triangle order and colours of 55 products, built once against `2ea4755`'s
  `proteus_render` and once against the new one. The output was identical. The products:
  - basin and river CPU surfaces (river 2,237,423 triangles) for three water recipes at two
    times;
  - ordinary and arcade patches at ten centres on both maps, including 5.99/6.01;
  - basin ↔ river surface alternation.
- **Verification (M1 Max, macOS 26 / Darwin 25.6, Apple clang 21, Debug):**
  - The build is warning-clean. Desktop CTest passed 114/114 active tests in 183.06 s
    (15 retired tests disabled). The log is `build/adventure/a1-3-ctest.log`. No fixture
    hash or expected value changed.
  - The `boat_water_review --check` and `--check --waves` logs are identical to A1.2.
  - All 18 CPU, 18 flat-GPU and 18 wave-GPU review PNGs (`build/adventure/a1-3/`) are
    byte-identical to `build/adventure/a0-original/`. Their logs differ from A1.2 only in
    timing lines.
  - `wake_velocity --renderer cpu --screenshot` (basin and river, `--time 3`) is
    byte-identical to A1.2.
  - `validate_boat` and the digest tool, built standalone with `-fsanitize=address,undefined`,
    passed with no reports. macOS ASan has no leak detection.
- **Environment note:** earlier runs this session, with the display asleep and on battery,
  could not create a GPU context and starved the real-time `boat_net_*` tests until they
  timed out. Those runs are not evidence. The 114/114 run above was made with the display
  awake and on mains power.
- **Limitations:**
  - There is still no allocation-failure injection.
  - No windowed or manual playback check: input, motion, windowing and audio were
    unchanged, and every capture is identical.
  - The CPU river surface is about 2.2 M triangles per cache. Two CPU river views would each
    hold one; bounded outer water rings remain A2 work.
  - Scenery, harbor/traffic and GPU city builders still read the compiled river (A1.4).

**Next: A1.4.**

## A1.4 — bind scenery and both renderers to explicit map data

**Edit:** `src/waterfront.[ch]`, `src/waterfront_city.[ch]`, `src/waterfront_corner.[ch]`,
`src/waterfront_scene.[ch]`, `src/renderer_gpu.[ch]`, `src/boat_gpu.[ch]` and CPU view callers.

1. Pass the selected description into CPU city/window drawing and GPU city, facade, roof,
   water and bridge builders. GPU initialization currently takes only an AA flag; add the
   necessary map/view binding. Remove implicit calls to the one global river mesh.
2. Audit map-derived dressing in `waterfront_corner.c`: its `initialized` flag, construction
   scratch, boxes and bounds cannot silently belong to whichever map was used first. Give
   these caches an explicit scene owner and propagate it to dressing queries/builders.
3. Keep genuinely immutable shared resources (unit/model meshes, fixed authored recipes,
   panorama) shared and identify them. This does not require copying every static object into
   every view. Map-dependent data must not masquerade as a universal immutable asset.
4. Ensure main, reflection and shadow paths use the same bound data. Preserve existing
   scenery generation order and material rules. No tile culling, LOD changes or actor redesign
   in this task; synthetic empty/different-map fixtures must not inherit river dressing.

**Done when:** two scene/view owners can initialize, render and destroy independently, and
the old river's inspected CPU/GPU frames match the reference. Investigate differences rather
than loosening visual/geometry expectations as part of an ownership change.

### A1.4 result, 2026-10-25

Started from a clean `d73688a`.

- **Scenery owner:** `WaterfrontScenery` (`src/waterfront_corner.h`) replaces the corner
  dressing's 16,384-box static arrays, bounds, counters and `initialized` flag. It is built
  from one borrowed `BoatMapDescription`, and the construction scratch (local frame, ground
  surfaces, counters) lives in a builder that does not outlive `waterfront_scenery_init()`.
  - Box storage grows up to the same 16,384-box cap; overflow or allocation failure fails the
    init and leaves the owner zeroed.
  - A NULL map (the basin) is ready, empty and allocates nothing.
  - `waterfront_scenery_bind()` keeps a ready owner of the same map or rebuilds it.
  - `waterfront_scenery_clear()` replaces `waterfront_corner_clear()`.
- **Map-derived versus authored content:** frontages, quays and forecourts derive from
  whichever map is bound. The cafe, the street furniture, the crane, the bridge deck box, and
  the cafe and crane lights are authored for the compiled river crop. They appear only when
  the owner's map is `waterfront_city_river_map` (its `river` flag). A synthetic map therefore
  gets its own derived dressing and none of the river's.
- **CPU binding:** `BoatCpuView` (`src/boat_view.h`) holds a view's water-mesh cache and
  scenery, and replaces the bare cache in `boat_draw()`, `boat_draw_with_ghost()` and
  `boat_draw_scene()`. A mapped draw binds the scenery to the world's map.
  `waterfront_draw_boating()` takes the scenery and draws its map's mesh, windows and
  waterline fittings, plus the crane and bridge deck when it is the river.
- **GPU binding:** `waterfront_gpu_scene_init()` now builds only map-independent resources.
  The new `waterfront_gpu_scene_bind()` builds the city, facades, roofs, ground, water,
  windows, dressing and (for the river) crane meshes from a scenery owner, replacing any
  previous binding; the scene records the bound map.
  - The main, reflection and shadow passes draw the same bound meshes, skip empty surface
    classes, and gate the bridge deck, crane lights and authored light candidates on the
    binding.
  - A non-basin frame drawn on an unbound scene is refused with a warning.
  - `BoatGpu` owns its own `WaterfrontScenery` and binds on the first draw of a mapped world
    or on a map change. `harbor_review` and `gpu_fixture` bind the river explicitly.
- **Removed:** `waterfront_corner_boxes()`, `waterfront_corner_clear()`,
  `waterfront_city_mesh()`, `waterfront_city_is_bridge_triangle()` and
  `waterfront_city_map()`. The compiled river is reached only as `waterfront_city_river_map`,
  through its location definition, a scenery/water binding, or by name in river-only review
  tools and validators.
- **Shared immutable resources (unchanged, identified):**
  - the compiled river description itself;
  - harbor and traffic boat models, the pontoon and quay models (lazily generated once from
    fixed recipes);
  - the sky panorama, gull and skin meshes, and the authored street/bridge/actor recipes in
    `waterfront_scene.c`.

  None of these depends on the bound map. Per-call static scratch frames in `boat_view.c` and
  `boat_gpu.c` are single-threaded drawing scratch, not caches.
- **Cost:** a river scenery build takes about 1.26 s in this Debug build (three runs) and
  holds 7,944 boxes in about 1.0 MB. The old global was built once per process, at GPU init
  or the first CPU draw. Each view now builds its own, on its first mapped draw or a map
  change. A GPU `wake_velocity` session builds one.
- **Tests:**
  - `city_dressing` builds explicit owners. The river owner matches the box and
    clearance-grid digests recorded from the old global (`8a15797b4f84d3f3`,
    `081bde4a44942f73`; 7,944 boxes).
  - The same test checks a second river owner alive together (equal content, separate
    storage), the basin owner, and the synthetic harbor (`tools/boat_test_maps.h`): 139
    derived boxes, 24 of them waterline, none outside the harbor.
  - It also checks failed init, double destroy, and rebinding harbor → river → harbor →
    basin with the survivor unchanged.
  - `boat_world_isolation` adds CPU view binding: two river views and a harbor view draw
    interleaved; destroying one and rebinding another reproduce every frame exactly.
  - The new serial GPU test `gpu_view_binding` (`boat_water_review --check-gpu-views`) does
    the same with two `BoatGpu`s, including destroy and recreate with the context alive.
    The harbor scene has no crane and 4,890 dressing vertices against the river's 266,094.
- **Verification (M1 Max, macOS 26 / Darwin 25.6, Apple clang 21, Debug, mains power):**
  - The build is warning-clean. Desktop CTest passed 115/115 active tests in 175.70 s
    (15 retired tests disabled). The log is `build/adventure/a1-4/ctest.log`.
  - The 18 CPU, 18 flat-GPU and 18 wave-GPU review PNGs are byte-identical to
    `build/adventure/a0-original/`. The `--check` and `--check --waves` logs are identical
    to A1.3, and the capture logs differ only in timing lines.
  - The CPU `wake_velocity` basin and river screenshots match A1.3.
  - Eight more captures are byte-identical to the same tools built from `d73688a`:
    `harbor_review` marina (CPU and GPU) and ferry (GPU), `gpu_fixture --city` and its shadow
    map, and GPU `wake_velocity` basin, river and river waves with 4×4 AA. They are under
    `build/adventure/a1-4/extra-{old,new}/`.
  - `validate_city_dressing` and `validate_boat_world`, with the scenery and view sources
    compiled with `-fsanitize=address,undefined`, passed with no reports.
  - Synthetic-harbor CPU and GPU frames (scratch tool) showed its quay rail, cleats,
    frontage and forecourts, and no cafe, crane or bridge deck.
  - A windowed `wake_velocity --location river --waves --demo --frames 900` ran to
    completion. That is agent playback only; no human playback or acceptance was performed.
- **Limitations:**
  - Street cars and lamps, harbor boats, traffic and gulls are authored river actors. They
    are still selected by the definition's `mapped` flag, so a synthetic mapped world draws
    them. Per-location actor recipes are outside A1.4.
  - The CPU river surface and the scenery are each held once per view.
  - There is no allocation-failure injection.
  - GPU lifecycle coverage is the two-view test above; the full reload matrix is A1.5
    ([result](#a15-result-2026-10-25)).

**Next: A1.5.**

## A1.5 — prove lifecycle parity and hand off to tiling

**Edit:** `tools/validate_boat_world.c`, focused map/water tests, a GPU lifecycle review if
needed, `CMakeLists.txt`, `docs/BOATING_FOUNDATIONS_PLAN.md`, `docs/BOATING_PERFORMANCE.md`,
this ledger and the adventure/index status.

- Test basin → river owner A → river owner B → basin, with A/B alive together and different
  mutable water/settings/view centres. Also use the synthetic different map so equal river
  geometry cannot hide a global lookup. Release one owner and continue using the other.
- Cover zero/partial init, repeated reload, respawn, course reset, replay/ghost viewing,
  fleet checkpoint restore and client prediction with a surviving shared immutable resource.
- Exercise GPU recreate/destroy with its context alive, both view caches, and second-owner
  rendering after first-owner destruction. Register graphics tests as serial GPU tests.
- Run repeat init/destroy under ASan/UBSan where supported. Keep sanitizer output separate
  from performance results; document any platform/tooling gap.
- Repeat the controlled A0.4 matrix on the same machine/options. Ownership should not produce
  a material unexplained regression. Use >10% p95 growth across three controlled runs as an
  investigation trigger, not a cross-machine CTest pass/fail threshold. Check trace/digest
  parity first; no tuning handling or rendering to compensate for a structural regression.
- Inspect representative captures and manually run river waves, pause/reset, course/replay
  and a river network room. Record agent playback separately from human acceptance.

**A1 complete:** explicit map ownership, independent view caches, correct lifetime/failure
behavior and compatibility evidence. Summarize resource bytes and remaining process-lifetime
assets. A2 starts by partitioning the **unchanged current river** into in-memory tiles and
checking exhaustive collision parity; source expansion and streaming remain subsequent tasks.
Write A2's exact interfaces after A0/A1 evidence instead of prebuilding a general asset system.

### A1.5 result, 2026-10-25

Started from a clean `853f25d`. A1.5 changes only validators, a review mode and
documentation; runtime code is unchanged.

- **CPU lifecycle (`boat_world_isolation`):** five scripted owners each hold their own
  `BoatWorldResources`, session, `BoatCpuView` and recorded run:
  - basin with waves and current;
  - river A with gentle waves;
  - river B, calm, with a different camera and mirrored steering;
  - the synthetic harbor;
  - a second calm basin.

  Each owner runs four phases: drive; respawn and drive; restart (the basins start the
  course first) and drive; and view its recorded run from a playback world built on the
  same owner with `boat_replay_water()`, with the live boat as the ghost. Each phase hashes
  the physics (tick, boat, water, presentation, course) and the 320 × 200 frame.

  The solo references run each owner alone with a fresh renderer. The interleaved run uses
  one renderer, with three slots:
  - Slot 0 reloads basin → river A → basin.
  - River B and the harbor run alongside A.
  - A is released (session, view, run and owner) before B and the harbor finish.

  All 40 hashes match. All five scripts differ, and the replay frame differs from the live
  one. A zeroed owner and view destroy twice safely, and a failed init over a stale owner
  leaves it zeroed. The test adds about 13 s.
- **Fleets (`boat_map`):** after the existing cross-owner checkpoint test, the first owner
  is released while fleets still name it. A fleet on the surviving owner and a fleet
  restored onto a fresh third owner then step 1,200 ticks to equal hashes.
- **Prediction (`boat_net` `prediction_owners`):** two rooms on private river owners run
  authority plus prediction. One owner is released mid-run. The surviving prediction
  matches its authority after 30 ticks, and a correction from tick 20 replays 10 ticks to the
  same hash, still borrowing its own owner.
- **GPU lifecycle:** new serial GPU test `gpu_view_lifecycle`
  (`boat_water_review --check-gpu-lifecycle`).
  - Four private owners (river A, river B with an arcade world, the harbor, the basin) all
    have gentle waves, so both patch kinds build. Each renders once in a fresh `BoatGpu`
    as its reference.
  - Two shared views reproduce every reference interleaved; one view holds both patch
    kinds (14,771 and 29,260 triangles).
  - River A's owner, session and last view are destroyed. The survivor keeps reproducing
    river B and the harbor.
  - Three cycles recreate a view with the context alive, draw basin, river B and harbor
    against the survivor, and destroy twice.
- **Harness fix:** UBSan found `room_step` in `validate_boat_net.c` (from `6f26862`, M4)
  converting a negative remote-view tick to `uint64_t` before the history lookup. It now
  returns first; the counters are unchanged, because such a lookup could never match.
- **Sanitizers:** separate tree `build/adventure-asan` (Debug, `-fsanitize=address,undefined
  -fno-sanitize-recover=undefined`, reusing `build/_deps` sources), kept apart from the
  performance runs.
  - Clean with no reports, logs under `build/adventure/a1-5/sanitizers/`:
    `validate_boat_world`, `validate_boat_map`, `validate_boat_net` (after the harness fix),
    `validate_city_dressing`, `validate_boat`, and `boat_water_review --check-gpu-views`
    and `--check-gpu-lifecycle`.
  - macOS ASan has no leak detection, so leak freedom rests on the destroy paths and the
    zeroed-owner checks.
- **Performance:** the A0.4 matrix was repeated with the same script, options and Release
  tree, on the same machine and mains power, 20:25–20:31 UTC.
  - The build reported `853f25d-dirty`: only tests were dirty, and the benchmark path is
    unchanged.
  - Before the matrix started, the script waited for another session's CTest run in a
    separate worktree to finish. A watcher logged no other test or game process during it.
  - **Trace parity first:** all 12 final states equal A0.4's. Waves rebuilt 177 patches
    with a 37,404-triangle peak, as before.
  - Frame p95, A1.5 median against A0.4:

    | Preset / AA | A1.5 frame p95 (ms) | A0.4 (ms) | Change |
    | --- | --- | --- | --- |
    | flat / off | 5.068 | 5.178 | −2.1% |
    | flat / 4x4 | 5.245 | 5.668 | −7.5% |
    | waves / off | 5.220 | 5.600 | −6.8% |
    | waves / 4x4 | 5.392 | 5.691 | −5.3% |

  - Submission p95 is 3.633, 3.766, 3.825 and 3.919 ms (A0.4: 3.686–3.875), and GPU p95 is
    within 0.04 ms.
  - The lower frame p95 is mostly present time: present p95 is 1.46–1.51 ms against
    1.60–1.95 ms. That is not attributed to ownership; the commit before A1.1 (`413460d`,
    the frame pacer) and machine state are untested candidates.
  - No measured frame missed 16.67 ms. Maxima were 7.1–9.5 ms. Nothing reached the 10%
    trigger. Raw data is in `build/adventure/a1-5/performance/`.
- **Captures:**
  - The 18 CPU, 18 flat-GPU and 18 wave-GPU review PNGs are byte-identical to
    `build/adventure/a0-original/`.
  - The `--check` and `--check --waves` logs are identical to A1.4. The CPU basin and
    river screenshots match A1.4.
  - The eight A1.4 extra captures (harbor, ferry, fixture city and shadow, and GPU
    basin/river/river waves) are byte-identical.
  - I inspected `river-waves/beyond-default.png` and the CPU river screenshot.
- **Agent playback (no human playback or acceptance):**
  - A windowed river waves demo ran 900 frames.
  - A windowed basin course with the replay fixture as its ghost ran 900 frames, using a
    scratch `--data-dir`.
  - A windowed `--replay` of the fixture ran 600 frames.
  - A river room ran: `boat_server --location river --waves`, a windowed
    `wake_velocity --connect` (1,200 frames: 269 corrections, own error 0 m, no snaps) and
    `boat_bot --predict --reset-every 6`.
    - The bot completed three resets, with no resyncs or dropped packets, and ended
      consistent.
    - Server service cost was 0.169 ms per tick.
  - Pause and reset need key input. The agent did not drive them; they remain for human
    playback.
- **Verification (M1 Max, macOS 26 / Darwin 25.6, Apple clang 21, Debug, mains power):**
  the build is warning-clean. Desktop CTest passed 116/116 active tests in 183.51 s (15
  retired tests disabled); the log is `build/adventure/a1-5/ctest.log`. 354 local links in
  the changed documents resolve, and `git diff --check` passed.
- **Limitations:**
  - Course and pause/reset playback by a human is pending, and so is human acceptance.
  - The CPU lifecycle replay is a test-built recording sampled mid-run, not a recorded
    course finish. Course finishes and file round-trips stay in `boat_replay`.
  - There is no allocation-failure injection.
  - The GPU test compares pixels within one process and context.

### A1 completion — 2026-10-25

A1 is complete. The physical map, collision cache, water meshes, scenery and both renderers
take explicit, caller-owned resources. Two independent owners per map are proven by
reload, release and failure tests, and the existing river is unchanged in traces, digests,
captures and controlled frame time.

**Resource bytes per owner**, measured in this Debug build with a scratch tool (not checked
in) after one draw at spawn:

| Owner | River | Synthetic harbor | Basin |
| --- | --- | --- | --- |
| `BoatWorldResources` collision cache | 13,128 B allocated (545 shore edges used) | 48 B | 0 |
| `WaterfrontScenery` (per CPU or GPU view) | 1,016,832 B (7,944 boxes) | 17,792 B (139) | 0 |
| CPU water-mesh cache, waves | 218 MB allocated (2,237,423-triangle surface) | 106 KB | 3.4 MB |
| GPU wave patch, ordinary / arcade | 0.85 / 1.70 MB | 53 KB | 1.70 MB ordinary |
| GPU map meshes | 1,040,430 vertices, 37.5 MB of buffers | 5,076 vertices, 183 KB | 0 |

- Raylib keeps a CPU copy of each uploaded map mesh, so a GPU view holds about the same
  again in RAM.
- Flat CPU water allocates no surface.
- Struct sizes: `BoatWorldResources` 40 B, `BoatCpuView` 288 B, `BoatGpu` 8,608 B,
  `BoatSession` 23,664 B.

**Remaining process-lifetime assets** are immutable and map-independent:
- the compiled river description;
- the lazily generated harbor/traffic boat, pontoon and quay models;
- the sky panorama, gull and skin meshes;
- the authored street, bridge and actor recipes.

Per-call static drawing scratch is not a cache. Authored river actors (cars, lamps, harbor,
traffic, gulls) still follow the definition's `mapped` flag.

### A2 handoff

**Next: A2**, starting by partitioning the **unchanged current river** into in-memory tiles and
proving exhaustive collision parity. Start from A0's 512 m tile experiment, with 1 km as the
comparison.

- Add application submission and triangle counters before judging tile size, since A0.4
  and A1.5 could report submission time only.
- Build tiles from `waterfront_city_river_map` in memory. Do not change the source,
  geometry header or bake format yet.
- Add the indexed collision query and compare it with the current `BoatMap` scan:
  - every `boat_map_water()` and `boat_map_water_contact()` result and normal, on a dense
    grid including bridge legs, crop edges and tile seams;
  - the river query digest;
  - the `--check` traces.
- The owners and lifecycle tests above are the harness. A tiled map must pass
  `boat_world_isolation`, `gpu_view_lifecycle` and the matrix unchanged.
- Write A2's exact interfaces from this evidence. Do not prebuild a general asset system.
  Source expansion and streaming remain later tasks.

The [A2 ledger](#a2--tiled-current-river) below splits this into A2.1–A2.5.

## A2 — tiled current river

A2 splits the **unchanged current river** into in-memory tiles, makes collision queries
inspect nearby candidates only, and bounds per-frame land and water work by visibility. It
keeps the source, `assets/geometry/river/geometry.h`, the bake format, 120 Hz handling,
replay and network formats and nearest-neighbour presentation. All tiles stay resident;
streaming and source expansion are A3 work. The A1 owners and lifecycle tests are the
harness throughout.

| Task | Depends on | Deliverable | Status |
| --- | --- | --- | --- |
| A2.1 | A1 complete | Application submission and triangle counters on both renderers; counter baseline of the untiled river | Complete 2026-10-25; [result](#a21-result-2026-10-25) |
| A2.2 | A2.1 | In-memory tile catalogue of the river map (512 m, 1 km comparison) and an indexed collision query with exhaustive parity | Complete 2026-10-25; [result](#a22-result-2026-10-25) |
| A2.3 | A2.2 | Per-tile land culling in the GPU shadow, reflection and main passes and the CPU reference; 512 m vs 1 km judged by submissions, triangles and time | Complete 2026-10-25; [result](#a23-result-2026-10-25) |
| A2.4 | A2.3 | Water coverage bounded by intersecting tiles: GPU patch rebuilds and the CPU reference surface, crack-free across seams | Complete 2026-10-26 (GPU CTests passed; Release matrix not rerun, see limitations); [task](#a24--water-bounded-by-intersecting-tiles), [result](#a24-result-2026-10-26) |
| A2.5 | A2.4 | Same-view scaling against enlarged synthetic catalogues (up to a ≈25 km corridor), lifecycle and performance gates, A3 handoff | Complete 2026-10-26 (GPU timing, lifecycle and the Release matrix not run, see limitations); [result](#a25-result-2026-10-26) |

## A2.1 — application submission and triangle counters

**Edit:** `src/renderer3d.[ch]`, `src/renderer_gpu.[ch]`, `tools/boat_water_review.c`,
`tools/validate_renderer3d.c`, `tools/validate_boat_water_benchmark.py` and the benchmark
guide. Counting must not change pixels, traces or the benchmark's timed work beyond a few
integer additions.

1. Count the CPU reference's mesh/box submissions, submitted, camera-facing and rasterized
   triangles per frame.
2. Count every GPU scene `DrawMesh` and its triangles by pass (shadow, reflection, main) and
   group (map, water, objects), plus the batched sky quads. Label them application
   submissions, not driver draw calls.
3. Report them per frame in the benchmark CSV and as statistics in its JSON; report a
   summary from the `--output` review runs. Record the untiled baseline.

### A2.1 result, 2026-10-25

Started from a clean `642ca78`.

- **CPU:** `Renderer3D.stats` (`Renderer3DStats`, reset by `renderer3d_begin`) counts
  `meshes`, `triangles`, `front_triangles` and `rasterized`. `validate_renderer3d` checks
  them for two boxes and a near-clipped triangle.
- **GPU:** `WaterfrontGpuScene.stats` (`WaterfrontGpuStats`, reset by each
  `waterfront_gpu_scene_draw`) holds a `WaterfrontGpuCount` (submissions, triangles) per
  `WaterfrontGpuPass` × `WaterfrontGpuGroup`, plus `sky_quads`;
  `waterfront_gpu_stats_total()` sums them. All four `DrawMesh` call sites go through one
  `submit()`. The current pass is a render-thread pointer set only during the scene draw,
  so submissions outside it are not counted.
- **Benchmark:** report `schema_revision` 2 adds per-frame `app_submissions`,
  `scene_triangles` and their pass and group splits to the CSV, and statistics for
  submissions, triangles, map submissions and map triangles to the JSON.
  `driver_draw_calls` stays `null`. The contract test checks that the splits partition the
  totals apart from the two sky quads. `--output` runs print a `scene_work` line.
- **Counter baseline** (Release, M1 Max, hidden, `--aa off`, one run per preset; counts do
  not depend on AA or timing). Measured frames after warmup, p50 (range):

  | Count per frame | Flat | Waves |
  | --- | --- | --- |
  | Application submissions | 939 (p95 951, max 960) | 943 (p95 963, max 975) |
  | Scene triangles | 885,348 | 920,480 |
  | Map submissions / triangles | 22 / 861,251, every frame | the same |
  | Object submissions / triangles | 914 (699–935) / 23,546 | 917 / 23,582 |
  | Water triangles | 547 | 35,603 (14,944–37,951) |
  | Shadow pass submissions / triangles | 214 / 175,627 | 214 / 175,627 |
  | Reflection pass | 228 / 353,309 | 228 / 353,309 |
  | Main pass | 496 / 356,420 | 498 / 391,505 |

  The CPU reference (`--output --renderer cpu`, 22 sampled frames) submits 3,847 meshes
  and boxes on average (max 7,468) with 106,112 triangles; 8,655 clipped triangles reach the rasterizer.
- **Findings for A2.3:**
  - The whole map is drawn in every pass, so map triangles never change along the route.
    The shadow pass draws 175 k city triangles into a 96 m shadow map. Land culling should
    cut triangles sharply in all three passes.
  - Map meshes are only 22 of about 940 submissions. Objects (cars, lamps, harbour and
    traffic boats, gulls, particles, the bridge) are 97% of them. Tiling the map adds map
    submissions (up to seven per visible tile and pass). A2.3 must therefore report submission
    cost as well as triangles, and may find that object culling, not tile count, decides
    the CPU submission time. Object culling is not in A2's scope unless A2.3's measurements
    show it limits the tiled map.
- **Unchanged output (Debug):** all 18 CPU, 18 flat-GPU and 18 wave-GPU review PNGs are
  byte-identical to `build/adventure/a0-original/`. The `--check` and `--check --waves`
  logs equal A1.5's. The three review logs differ from A1.5 only by timings and the new
  `scene_work` line. The benchmark final states equal the `--check` states. Evidence:
  `build/adventure/a2-1/`.
- **Timing:** one hidden Release run per preset gave frame p95 4.66 (flat) and 6.33 ms
  (waves) and submission p95 3.81 and 4.06 ms. This is not the controlled three-trial
  matrix; the counters add one pointer check and two additions per submission.
- **Verification (M1 Max, macOS 26 / Darwin 25.6, Apple clang 21, Debug):** the build
  is warning-clean. Desktop CTest passed 116/116 active tests in 183.52 s (15 retired tests
  disabled; log `build/adventure/a2-1/ctest.log`). After a whitespace-only reformat, the
  build and the six renderer, view and benchmark tests were rerun and passed. 247 local
  links in the changed documents resolve, and `git diff --check` passed.
- **Limitations:** driver-reported draw calls, GPU-side primitive counts and the AA
  downsample/presentation draws are not counted. No visual, manual or human check was
  needed, as pixels are unchanged.

**Next: A2.2.**

## A2.2 — tile catalogue and indexed collision

**Edit:** `src/boat_map.[ch]`, a tile module if `boat_map.c` would otherwise mix concerns,
`tools/validate_boat_map.c`, the core boundary test inventory, and the review tool for
counts. Keep everything in `proteus_boat_core`: no drawing or file I/O.

1. Build a tile catalogue from a `BoatMapDescription` in memory: a fixed grid origin and
   size (512 m, with 1 km as the comparison), and per tile its bounds, water triangles,
   shore edges and land/building triangles. A triangle or edge crossing a seam is listed in
   every tile it overlaps. Seams are bookkeeping only: they add no shore edges.
2. Add a spatial index to `BoatMap` and answer `boat_map_water()` and
   `boat_map_water_contact()` from nearby candidates. Return **exactly** the exhaustive
   answer: the same containment, nearest edge (first in shore order on equal distance),
   clearance comparison and normal, including points outside the crop. Choose the index
   cell size by measured candidate counts; it need not equal the render tile.
3. Keep the exhaustive scan as a named reference in the core, used by tests and review.
   Compare every result and normal bit for bit on a dense grid with fine sampling at bridge
   legs, crop edges, tile seams and corners (±1 ulp around seam coordinates), at clearances
   0, 0.5, 3 and 12 m. The river query digest (`63225c36181d381d`), the `--check` traces,
   `boat_world_isolation` and `gpu_view_lifecycle` must be unchanged.
4. Report resident index bytes, tile counts, empty tiles and candidate counts per query
   (mean, p95, max) against the exhaustive 547 triangles and 545 edges.

**Done when:** indexed queries equal the exhaustive reference everywhere tested, the
catalogue is enumerated for both tile sizes, and the river is otherwise unchanged.

### A2.2 result, 2026-10-25

Started from a clean `0fc2517`.

- **Collision index:** `BoatMap` now also holds a `BoatMapGrid`. This is a uniform grid of
  16 m cells (`BOAT_MAP_CELL_SIZE`) aligned to multiples of the cell size, so cells nest in
  zero-origin tiles. Each cell lists, in ascending order, the water triangles and shore edges
  within 0.5 m (`BOAT_MAP_MARGIN`) of it. Registration uses a separating-axis test, not
  bounding boxes, because the river's long, thin water triangles span up to 683 m.
  `boat_map_init()` builds the grid; `boat_map_init_cell()` takes another cell size for
  comparison. A failed build, including allocation failure, leaves the map zeroed.
- **Queries:** `boat_map_water()` and `boat_map_water_contact()` now answer from the grid:
  - **Containment** tests only the triangles listed in the point's cell.
  - **Clearance:** when the point is in water, only the edges within the clearance square
    (plus 5 cm of slack) are compared with the clearance.
  - **Normal:** only when one is needed, a window grows by one cell at a time until the best
    edge is provably nearer than any unvisited edge. Ties keep the lower shore index, as
    the scan did.
  - **Fallback:** positions beyond 10⁶ m, or not finite, take the exhaustive answer. These
    are never boat positions; the guard stops the window from growing forever.

  `boat_map_water_contact_reference()` keeps the previous exhaustive scan for tests and
  review, and `boat_map_water_contact_cost()` also reports the candidates a query inspected.
- **Tile catalogue:** the new `src/boat_map_tiles.[ch]` (core) builds `BoatMapTiles` from
  a prepared map at a chosen tile size, with the origin on a tile multiple.
  - **Water triangles and shore edges** are listed in every tile within the margin.
  - **Land** differs from the plan: each land triangle, bridge included, belongs to exactly
    one tile, the one holding its centroid, so that A2.3 can draw every tile without
    drawing anything twice. Each tile records the 3-D bounds of its land, which may cross
    the seam.

  Seams add no shore edges. The source, geometry header and bake format are unchanged.
- **Catalogue** (the river crop, 2,450 × 2,400 m, 78,612 triangles):

  | Tile size | Grid | Non-empty | Most land / water / shore per tile | Listed water / shore / land | Bytes |
  | --- | --- | --- | --- | --- | --- |
  | 512 m | 5 × 6 | 30 of 30 | 6,817 / 96 / 83 | 731 / 568 / 78,065 | 319,388 |
  | 1 km | 3 × 3 | 9 of 9 | 16,715 / 185 / 165 | 654 / 557 / 78,065 | 317,692 |

  No crop tile is empty: A0.3's 40% empty route tiles belong to the outer corridor. Seams
  duplicate 34% of water triangles and 4% of shore edges at 512 m (20% and 2% at 1 km).
- **Index sizes** (whole map allocation, shore pairs included):

  | Cell | Grid | Water / shore entries | Bytes |
  | --- | --- | --- | --- |
  | 8 m | 307 × 246 | 33,217 / 2,290 | 759,348 |
  | **16 m (default)** | 154 × 124 | 11,629 / 1,378 | **217,940** |
  | 32 m | 77 × 63 | 4,766 / 959 | 74,852 |

  At A1 the map allocated 13,128 B. 16 m keeps candidate counts small at a fifth of the
  8 m grid's memory.
- **Candidates per query** (16 m cells, normal requested, 3 m clearance), against the scan's
  547 triangles and 545 edges every time:
  - **Open water** (83,899 points): 5.2 edges on average (p95 20, max 38) and 2.5 triangles
    (p95 7, max 24).
  - **Land points**, where the nearest-edge window must reach the shore (215,203 points):
    64.6 edges (p95 194, max 431).

  Release timing on the M1 Max, 200,000 open-water points at the hull clearance (0.35 m)
  with a normal: **0.035 µs indexed against 3.43 µs** for the scan (a scratch tool, not
  checked in). Six hull samples per tick drop from about 21 to 0.2 µs. That is small for
  today's crop, but it no longer grows with the map.
- **Parity test:** the new `boat_map_index` CTest (`tools/validate_boat_map_index.c`,
  labels `core;cpu`, about 14 s in Debug) compares 8, 16 and 32 m grids with the exhaustive
  scan. At clearances 0, 0.5, 3 and 12 m it compares the boolean, the clearance-only result
  and the normal bit for bit, on these point sets:
  - an 8 m lattice over the crop plus 40 m, so points sit exactly on every cell seam;
  - nine points along every shore edge (crop edges and bridge-leg holes included), offset
    0, ±0.01, ±0.49/0.51, ±2.99/3.01, ±11.99/12.01 and ±30 m;
  - every 512 m and 1 km tile seam every 2 m, at the seam and one float either side;
  - a 0.25 m patch around every tile corner;
  - 50,000 seeded random points over the crop plus 100 m;
  - far points out to 250 km, and the >10⁶ m fallback.

  **3,589,224 comparisons, no mismatches.** A first run with denser sets (4 m lattice,
  1 m seams, 100,000 random points: 8,490,276 comparisons) also had none; it took 44 s and
  was thinned for routine use. The test also checks every tile list against a recomputed
  overlap, that each land triangle has exactly one owner and lies within its tile's bounds,
  tile lookup, and failure on an unready map or a non-positive size (no tile is created).
  `boat_map` additionally checks that a failed or destroyed map has a zeroed grid.
  The test prints the catalogue, index and candidate reports above, so the review tool
  needed no new mode.
- **Unchanged river (Debug):**
  - The river query digest is still `63225c36181d381d` (20,356 water answers). The
    `--check` and `--check --waves` logs equal A1.5's.
  - All 54 CPU, flat-GPU and wave-GPU review PNGs are byte-identical to
    `build/adventure/a0-original/`. The review logs equal A2.1's apart from timings.
  - `boat_world_isolation`, `gpu_view_lifecycle` and the benchmark contract pass in the full
    run below. Evidence: `build/adventure/a2-2/`.
- **Verification (M1 Max, macOS 26 / Darwin 25.6, Apple clang 21, Debug):** the build
  is warning-clean. Desktop CTest passed 117/117 active tests in 191.65 s, including the new
  `boat_map_index` (13.8 s) and both boundary checks with the new core source (15 retired
  tests disabled; log `build/adventure/a2-2/ctest.log`). 217 local links in the changed
  documents resolve, and `git diff --check` passed.
- **Limitations:**
  - Allocation failure is still code-inspected, not injected.
  - Exactness rests on the tested point sets and the margin argument, not on a proof
    over every float. The margin and slack exceed float rounding at these coordinates
    many times over.
  - The catalogue is not yet used by any renderer. No visual or manual check was needed:
    rendering is unchanged and captures are identical.

**Next: A2.3.**

## A2.3 — per-tile land culling

**Edit:** `src/renderer_gpu.[ch]`, `src/boat_gpu.c`, the CPU drawing of the map in
`src/waterfront.c`/`src/boat_view.c`, the scenery owner if it holds the catalogue, the review
tool and the benchmark guide. The core catalogue stays as A2.2 built it.

1. Build the GPU map meshes per tile from one catalogue that the view owns. These are city,
   facades, roofs, ground and windows; the authored corner, crane and bridge stay single
   objects. Keep each class's triangle order within its tile, and skip empty
   class-and-tile pairs.
2. Cull tiles against each pass's volume using the tile's land bounds: the shadow pass's
   orthographic box (96 m while boating), the reflection camera's widened frustum and the
   main frustum with its far plane. Use the same culling in the CPU reference.
3. Build both 512 m and 1 km and compare them with the A2.1 counters over the benchmark
   route: map submissions and triangles per pass, total submissions, CPU submission and
   GPU p95. Pick the default from that evidence, using the controlled three-trial matrix
   for the final pair. Objects remain about 915 submissions; report them, but leave object
   culling to a later task unless the tiled map's added submissions decide the result.
4. Captures should stay byte-identical: culling removes only out-of-volume triangles, and
   order within a class is kept. If a capture changes (for example a depth tie between
   tiles now drawn in a different order), inspect it and explain each difference before
   accepting it. `gpu_view_lifecycle`, `boat_world_isolation` and the matrix must pass.

**Done when:** every pass draws only tiles in its volume on both renderers, the chosen tile
size is justified by measured submissions, triangles and time, and captures are unchanged or
explained.

### A2.3 result, 2026-10-25

Started from a clean `d1f5646`.

- **Catalogue for renderers:** `boat_map_tiles_init_land()` builds the land lists and bounds
  from a description without a collision map. The test checks that they equal the full
  catalogue's. `boat_map_tiles_owner()` names the ownership rule (the tile holding a point,
  clamped to the edge tiles), and `boat_map_leading_water()` is shared with `boat_map_init()`.
  The view-owned `WaterfrontScenery` now holds a land catalogue at `tile_size`
  (`WATERFRONT_TILE_SIZE`, 1,024 m). `waterfront_scenery_init_tiled()` picks another size,
  and a rebind keeps the previous one.
- **GPU:** the city, facades, roofs, ground (quay), windows and the authored corner and its
  emissive layer are now `WaterfrontGpuTiled`: one mesh and box per non-empty tile, in
  triangle order. The water, crane and bridge stay single meshes. Assignment rules:
  - city triangles go to their owner tile;
  - the second triangle of each facade pair joins the first one's tile, so a wall quad
    never splits across a seam (this differs from the core rule);
  - windows follow their wall triangle's tile;
  - each corner box, and the ground top it adds, follows the tile of its centre.

  Each tile mesh's box is grown by 5 cm. `draw_tiled()` tests it against the current
  model-view-projection matrix in clip space, so the shadow box, the reflection camera and
  the main frustum, far plane included, each cull with their own volume. `tiles_culled[pass]`
  counts the skipped tile meshes.
- **CPU reference:** `waterfront_draw_boating()` draws the flat water, then, for each tile
  whose land box (grown by 10 cm) passes `renderer3d_box_visible()`, that tile's land
  triangles with the new `renderer3d_draw_mesh_triangles()`. Windows iterate the same
  visible tiles. `boxes_culled` counts the skipped boxes.
- **Review-only switches:** `waterfront_gpu_scene_set_culling_enabled()` and
  `Renderer3D.culling_disabled` draw every tile without changing order. `boat_water_review`
  gains `--tile-size M` (64–65,536 m) and `--no-cull` for captures and the benchmark. The
  benchmark (schema 3) records `tile_size` and adds `shadow_`, `reflection_` and
  `main_tiles_culled` columns.
- **Culling check:** the new GPU CTest `tile_culling` (`--check-culling`, serial, about
  9 s) renders 144 poses: 512 m and 1 km tiles, six route stops, four headings and the
  default, close and overview cameras. Each pose is rendered culled and unculled, on the GPU
  with gentle waves and on the CPU on flat water. Pixels must be identical, and every pass
  must cull at least one tile. It printed `tile_culling=ok poses=144 gpu_culled shadow=10128
  reflection=6663 main=7804 cpu_culled=2152`.
- **Tile size:** this was a controlled Release matrix on the M1 Max, 2026-10-25 21:35–21:50
  UTC. It ran flat and waves at AA off and 4×4, three trials each, for the untiled `d1f5646`
  (built in a scratch worktree), 512 m and 1 km tiles: 36 runs. Final states were equal in
  every run, with 0–2 deadline misses per run. Medians of the per-trial p50/p95:

  | Flat, AA off | Untiled | 512 m | 1 km |
  | --- | --- | --- | --- |
  | Map submissions p50 / p95 | 22 / 22 | 110 / 201 | 58 / 98 |
  | Map triangles p50 / p95 | 861,251 / 861,251 | 90,755 / 559,903 | 143,770 / 705,201 |
  | Total submissions p50 | 939 | 1,024 | 976 |
  | Scene triangles p50 | 885,348 | 114,948 | 167,951 |
  | Submission p95, ms | 3.22 | 3.63 | 3.23 |
  | GPU p50 / p95, ms | 2.47 / 2.67 | 2.15 / 2.61 | 2.11 / 2.45 |

  Across all four configurations, submission p95 was 3.17–3.48 ms untiled, 3.63–3.78 ms at
  512 m and 3.23–3.44 ms at 1 km. GPU p95 was 2.67/3.16/2.78/3.75 ms untiled,
  2.61/3.21/2.79/3.67 ms at 512 m and 2.45/3.11/2.63/3.52 ms at 1 km. GPU p50 fell by about
  13–15% with either size. At 1 km the passes cull a median of 30 tile meshes each.
  **1 km is the default.** 512 m halves the median map triangles again, but its extra
  submissions cost 0.3–0.45 ms of CPU submission p95, and it saves almost no further GPU
  time. Objects remain 915 submissions and are not culled. Per-tile class merging could
  make smaller tiles cheaper later. The p95 triangle counts stay high on long views down the
  river, so distance-based detail is later work. Evidence:
  `build/adventure/a2-3/performance/` (run script, summary and 36 reports).
- **Captures (Debug):**
  - **CPU:** all 18 river captures are byte-identical to `build/adventure/a0-original/`.
    Mean triangles submitted fell from 106 k to 77.5 k; rasterized triangles are unchanged
    at 8,655.
  - **GPU, culled against unculled:** byte-identical at both sizes: 36 tiled frames and the
    `--no-cull` set, as well as the CTest poses.
  - **GPU against the original:** at 1 km, 46 of 54 frames are identical. In the other 8, a
    single pixel differs (delta ≤3) in mouth-close/default (flat and waves) and in waves
    river-close/default. The connections-overview differs in 157 pixels (flat and waves
    alike, delta ≤73). These are scattered dots on distant rooftops and walls
    (`build/adventure/a2-3/default-1024/overview-diff.png`). Since culled and unculled
    frames are identical, they come from draw order alone: classes are now drawn tile by
    tile, so ties between coplanar, overlapping faces resolve differently under the
    `LEQUAL` depth test. At 512 m the overview differed in 646 pixels. Accepted as
    explained.
- **Verification (M1 Max, macOS 26 / Darwin 25.6, Apple clang 21, Debug):** the build is
  warning-clean. Desktop CTest passed 118/118 active tests in 183.81 s, including the new `tile_culling`
  (8.5 s); 15 retired tests are disabled (log `build/adventure/a2-3/ctest.log`). `gpu_view_lifecycle`, `boat_world_isolation`, the
  benchmark contract (now requiring `tile_size` 1024, the culled columns and shadow culling
  on every row) and `boat_map_index` (now also checking the land-only catalogue) passed in
  that run. The changed C lines are clang-formatted, 250 local links in the changed
  documents resolve, and `git diff --check` passed.
- **Limitations:**
  - Water is drawn whole on both renderers; A2.4 bounds it.
  - Objects (moored and travelling boats, gulls, actors) are not culled.
  - The facade pair rule makes the GPU's land assignment differ from the core catalogue
    for those triangles. Both draw every triangle exactly once.
  - Allocation failure in the tiled GPU builders is code-inspected, not injected.
  - No manual playback or human visual acceptance was performed. Input, timing and audio
    are unchanged.

**Next: A2.4.**

### A2.4 result, 2026-10-26

Started from a clean `65434fc` (v0.0.17) on branch `adventure-a2-4`, in a worktree.

- **Water index:** `BoatWaterMeshCache` now holds a `BoatWaterIndex` of the bound map's
  leading water triangles on a 256 m grid (`BOAT_WATER_INDEX_CELL`), built on the first patch
  or surface call after a bind and dropped on a rebind. A triangle is listed in every cell
  its clipped rows overlap. The cache owns it rather than borrowing the view's `BoatMapTiles`,
  because the renderers hold only a land catalogue and the water lists need no collision map;
  the public patch and surface signatures are unchanged. A gather over a cell range dedupes by
  an epoch stamp and sorts ascending, so triangles are clipped in map order.
- **GPU patches:** a rebuild clips only the triangles gathered from the cells touching the
  192 m patch square. On the river that is a mean of 43 of the 547 water triangles. In
  `validate_boat` (opt-Debug, M1 Max) a rebuild took 0.094 ms against 0.120 ms for a full
  scan: the old per-triangle bounds rejection was already cheap at 547 triangles, so the gain
  here is small and is the scaling for larger maps (A2.5 measures it with synthetic
  catalogues).
- **CPU surface:** the undisplaced two-metre tessellation is now built once per bind
  (`rest`, with per-source ranges), and `boat_water_surface_visible()` copies, displaces and
  colours only the source triangles in cells whose box, padded 8 m vertically for waves,
  passes `renderer3d_box_visible()`. `boat_view.c` passes the renderer; `boat_water_surface()`
  keeps every triangle. Kept triangles retain full-surface order and values.
- **Flat GPU water:** left as one mesh. It is 547 triangles drawn in the reflection and main
  passes, negligible beside the 0.14 M median land triangles; splitting it would add
  submissions for no measured gain. Decided from the A2.1 counters, not re-measured.
- **Review switch:** `BoatWaterMeshCache.full_scan` (review/test only) makes patches visit
  every triangle and the surface ignore visibility; `--no-cull` already makes the CPU box test
  accept everything.
- **Parity test (`boat_handling`, new `validate_water_index`):** 144 patches (both grids,
  eight points near index seams and corners, offsets −3/0/+3 m and edge-on-seam variants) equal
  the full-scan patches bit for bit in topology, vertices and colours, with 90 non-empty; the
  index visits less than a quarter of the triangles in total. The full surface equals the
  surface through an accept-all callback and through `boat_water_surface()`. Three windows
  (two in the river, one outside the map) give an ordered subsequence of the full surface with
  no triangle that has a vertex in the window missing.
- **CPU captures (Debug, `boat_water_review --renderer cpu --waves`):** 18 frames, culled and
  `--no-cull` are byte-identical. Mean triangles submitted fell from 2.34 M to 1.30 M and mean
  frame time from 602 ms to 340 ms (Debug, single runs; indicative only); rasterized
  triangles are unchanged at 531 k. Flat-water CPU captures render no displaced surface; they
  are unaffected. Evidence: `build/adventure/a2-4/`.
- **Verification (M1 Max, Darwin 25.6, Apple clang, Debug):** warning-clean build
  (apart from the existing raylib `stb_vorbis` note); CTest without the GPU label passed
  94/94 active tests, 12 retired tests disabled (`build/adventure/a2-4/ctest.log`); the
  changed C is clang-formatted.
- **Limitations:**
  - **GPU verified except timing.** This agent session had no display, so the GPU CTests, the
    A2.3 matrix pair and flat-water GPU captures were not run by it. The user ran
    `boat_water_review --renderer gpu --waves` from the main checkout (`65434fc`) and from this
    branch on 2026-10-26 (M1 Max, Debug): all 18 captures were byte-identical, with 176 patch
    rebuilds, 37,404 wave triangles and 311,584 mean scene triangles in both. The user also
    reported `ctest -L gpu` (`-DPROTEUS_GPU_TESTS=ON`) passing on this branch the same day
    (summary reported, log not kept). The A2.3 Release matrix pair, for patch rebuild time and
    frame cost, was not rerun.
  - The 8 m wave padding is a constant, not derived from the wave model; the CPU
    capture identity covers the review poses only.
  - Allocation failure in the index and rest builders is code-inspected, not injected.
  - No manual playback or human visual acceptance was performed.

**Next: A2.5.** (Done; see [A2.5 result](#a25-result-2026-10-26).)

## A2.4 — water bounded by intersecting tiles

**Edit:** `src/boat_water_mesh.[ch]`, `src/boat_view.c`, `src/boat_gpu.c` (if callers pass
the catalogue), the review tool, `tools/validate_boat_map_index.c` or a new water-mesh test,
and `docs/3D.md`. The bake, the collision grid and the A2.3 land tiles stay as they are.

Today the GPU wave patch (`boat_water_patch[_arcade]`, a 192 m square around the boat,
rebuilt when its centre moves 12 m) clips **every** leading water triangle of the map
against its grid on each rebuild. The CPU reference (`boat_water_surface`) subdivides
and displaces every water triangle every frame, and the flat GPU water mesh is one mesh.

1. Give the water-mesh cache access to a water catalogue: the view's `BoatMapTiles` for the
   prepared map, or a cache-owned one at a cell or tile size chosen by measurement. A patch
   rebuild then visits only the water triangles listed in the tiles intersecting the patch
   square. A triangle listed by several tiles must be clipped once (dedupe by index, or
   visit in ascending order with a marker), so the patch's triangle order and output
   stay identical to today's.
2. Bound the CPU reference's subdivided surface to what the camera can see. Use the
   visible tiles' water, deduplicated, with the same frustum test as land. Keep the
   tessellation and displacement per triangle so that pixels do not change.
3. Split the flat GPU water into per-tile meshes and cull them like land, or show that
   the single mesh is already cheap (it is drawn in the reflection and main passes).
   Decide from the A2.1 counters.
4. Prove that nothing cracks or changes:
   - patches built from tiles must equal the full-map patches, topology and vertices,
     at centres on and around every tile seam and corner;
   - the CPU and GPU captures must equal A2.3's, including new poses where a seam
     crosses the patch;
   - the A2.3 matrix pair is repeated to measure patch rebuild time and frame cost.

**Done when:** patch rebuild and CPU water work scale with the tiles in view, not with
the map; patches equal the full-map result across seams; and captures are unchanged or
explained.

## A2.5 — same-view scaling

- **A2.5:** with the same local view, compare the river against synthetic catalogues
  with many distant tiles. Tick and render cost must not grow with them. Repeat the
  controlled matrix and lifecycle tests and record memory per owner. Hand off to A3.

### A2.5 result, 2026-10-26

Started from `origin/main` at `d66a8a8` (A2.4 merged) on branch `adventure-a2-5`.

- **Synthetic corridors:** `tools/validate_boat_map_scaling.c` (CTest `boat_map_scaling`,
  about 5 s in Debug) copies the river 1, 2, 4, 5 (about 25 km), 8 and 16 times along its long
  (map east) axis at a stride of 5,120 m, a whole number of tiles, so the first copy keeps the
  river's tile grid and indices. Water, land and bridge triangles are ordered as a description
  requires. The local view is always the first copy.
- **Same-view equality (asserted at every size):** 4,000 collision samples (half off the
  water) return the river's containment and normal bit for bit and visit the same candidate
  triangles and shore edges (2.49 triangles per query), every 40th also equalling the
  exhaustive scan; the tiles within the first copy list the same water, shore and land counts;
  three water patches are identical in topology, vertices and colours and clip the same source
  triangles; a windowed CPU surface is identical and displaces the same triangles. Only the
  collision query's count of cells touched differs (by 105 of 33,888 in total), from samples
  that fall outside the river's smaller grid, so it is not asserted.
- **Cost (Debug, M1 Max, single runs; `validate_boat_map_scaling --report`):** the per-query
  time (0.075–0.13 µs), patch rebuild (0.20 ms) and windowed surface (136 ms) do not change
  from 1 to 16 copies (547 to 8,752 water triangles, 78.6 k to 1.26 M triangles, 9 to 234
  tiles). Build costs scale with the map, once per bind: collision map 2.7 to 23.9 ms, tile
  catalogue 1.5 to 36.8 ms, water index and first patch 0.26 to 0.61 ms.
- **Defect found and fixed:** A2.4 tessellated every water triangle into the CPU reference's
  rest surface on first use: 218 MB for the river, growing linearly (3.5 GB at 16 copies).
  `BoatWaterMeshCache` now tessellates a triangle the first time it is in view
  (`rest_first`/`rest_end`, `UINT_MAX` until used), so the cache is 112 MB at every size
  (54.5 MB of it rest tessellation of the view's window, the rest the displaced surface and
  patches). The test now requires the cache to stay within 5% of the river's. The 18 CPU
  `--waves` review captures are byte-identical to a rebuild of `origin/main`
  (`build/adventure/a2-5/`), and mean frame time is unchanged (338 against 337 ms, Debug).
- **Memory per owner at 1 / 5 (25 km) / 16 copies:** collision map 0.22 / 1.7 / 6.0 MB; full
  tile catalogue 0.32 / 1.6 / 5.1 MB; land-only render catalogue 0.31 / 1.6 / 5.0 MB; water
  cache 112 MB at every size, and a GPU-only view does not build the rest or surface (patches
  and the index only). The three map-sized owners together grow linearly, by about 1 MB per
  5.1 km copy of river; nothing is quadratic.
- **Verification (M1 Max, Darwin 25.6, Apple clang, Debug):** warning-clean build; CTest
  without the GPU label passed 95/95 active tests (the new one included; 13 retired tests
  disabled); the changed C is clang-formatted.
- **Limitations:**
  - **Not run, no display in this session:** GPU CTests and captures, the GPU frame and
    submission cost against enlarged maps, `--check-gpu-lifecycle`, and the A2.3 Release
    matrix. Scaling is therefore proved for the core and CPU-reference paths and the
    renderer-independent water and tile data, not measured for GPU frames. The GPU land
    passes cull by the same land catalogue, whose local lists the test shows are unchanged.
  - The corridors repeat the river's shape, so they test distance and map size, not new
    geometry. ASan/UBSan were not run on the new test.
  - Build and bind costs still grow with the whole map, which A3's bounded asset loading must
    not hide behind a frame hitch. The Debug timings are single runs.
  - No manual playback or human visual acceptance was performed.

**A2 is complete.** Its hand-off to A3 is that detailed assets must load per region, since
bind costs and the water-index, tile and collision memory scale with the loaded map.

## A3 — first outer-river adventure

A3 ([plan](FREE_RIDING_ADVENTURE_PLAN.md)) makes the A0.3 first slice playable: the river crop
joined to the 10.9 km² run past Nya Älvsborg to the Långedrag old harbour, one shelter, one
exposed-water encounter and recovery. It is split like A2 so each step is verifiable alone.
Steps after A3.1 are proposals, to be revised from what the earlier steps find.

| Task | Depends on | Deliverable | Status |
| --- | --- | --- | --- |
| A3.1 | A2 complete | Offline bake of the first slice's water, land and quay walls into a compiled map description; route and shelter proven against the collision queries | Complete 2026-10-26; [result](#a31-result-2026-10-26) |
| A3.1b | A3.1 | Real land heights from Göteborg's Baskarta contours, replacing the flat 3.5 m ground; 20 m inland, 5 m near the shore | Complete 2026-10-26; [result](#a31b-result-2026-10-26) |
| A3.2 | A3.1b | A mapped `outer` location starting at the dock: spawn, camera, CPU/GPU water and land rendering, fog or fade at the playable boundary; scenery, harbor and traffic for the added geography decided here | Complete 2026-10-26; [result](#a32-result-2026-10-26) |
| A3.3 | A3.2 | Bake the existing river buildings, bridge and roads into the combined map so the outing is continuous under Älvsborgsbron | Complete 2026-10-26; [result](#a33-result-2026-10-26) |
| A3.4 | A3.3 | Långedrag shelter destination, checkpoint recovery and the exposed-water encounter | Proposed |
| A3.5 | A3.4 | Local weather transition and bounded per-region detailed-asset loading; cold-load, rapid-turn and recovery tests; clear/storm performance and memory targets | Proposed |

### A3.1 result, 2026-10-26

- **Generator:** [`tools/prepare_outer_map.py`](../tools/prepare_outer_map.py) reuses A0.3's
  assembly (`docs/adventure/survey.py`: land polygons plus OSM inland water, bridge legs
  removed) and the river bake's triangulation, then clips to the river crop joined to the first
  slice. Water keeps only the part connected to the dock, simplified to 0.5 m before clipping.
  Land is the rest of the region at the river's assumed 3.5 m ground, with quay walls to -2 m.
  A 1 mm opening removes zero-width spikes where a shore meets the region edge along the same
  line, which the constrained triangulation rejected (applied only to polygons that fail).
  It needs the `docs/adventure/requirements.txt` pins and reads only checked-in sources.
- **Output (as of A3.1; the land is flat here and was replaced by A3.1b):**
  `assets/geometry/outer/geometry.h` (1.15 MB) and `manifest.json`: 7,796
  vertices, 15,556 triangles (2,668 water first), 8.0 km² water and 8.5 km² land in a 16.5 km²
  region. Two runs produced identical bytes (2.4 s each). The collision cache is 1.2 MB.
  [`src/waterfront_outer.[ch]`](../src/waterfront_outer.h) expose it as `waterfront_outer_map`
  in the core library; nothing uses it at runtime yet, and its bridge range is empty (as of A3.1; A3.3 fills it).
- **Route and shelter:** the A0.3 slice route (6.02 km, dock to Långedrag old harbour) stays in
  the baked water with the 8 m envelope (generator assertion, 27.6 m minimum shore clearance),
  and the new `boat_outer_map` CTest repeats it through `boat_map_water` at every waypoint and
  597 samples. Indexed queries equal the exhaustive scan on 20,000 points.
- **Visual check:** [`outer-map.png`](adventure/outer-map.png) (agent review) draws the baked
  water, land and route; coastline, islands and the river join read as in the A0.3 map. It is
  not a game capture.
- **Verification (2026-10-26, macOS arm64, Debug):** configure and build passed warning-free
  and `ctest --test-dir build -j 4 --output-on-failure` passed all 122 active tests in
  186.75 s (15 retired tests disabled), including the new `boat_outer_map` and the core
  boundary checks.
- **Limitations:**
  - **Playable boundary:** where the region cuts open water (5,151 m of edge, mostly south-west),
    water ends at an edge that collision treats as shore with no land behind it. A3.2 must hide
    it (fog, fade or authored land); it is not a coastline.
  - (As of A3.1; A3.3 added the river's.) The map has no buildings, bridge, roads or planting, and the bridge legs are holes in the
    water only. Terrain, depth, overhead clearance and hazards (rocks near the route) remain
    gaps. Shoreline vertices are simplified 0.5 m from the A0.3 assembly.
  - No runtime location, rendering, manual playback or human acceptance. The route test copies
    `SLICE_ROUTE` from the survey by hand.
  - GPU CTests were part of the full run but no GPU capture of this map exists.

### A3.1b result, 2026-10-26

The user asked for real land heights; the Lantmäteriet model was ordered but not yet approved,
so the first slice uses Göteborg's CC0 Baskarta, which the user downloaded.

- **Source:** [`tools/prepare_terrain_sources.py`](../tools/prepare_terrain_sources.py) clips
  the Baskarta `hojdkurvor` (0.5 m contours) and `hojdpunkt` (spot heights) to the slice
  window into [`terrain-baskarta.json.gz`](adventure/sources/terrain-baskarta.json.gz)
  (5.5 MB; 105,521 contours, 32,917 spot heights). See
  [the source notes](adventure/README.md#terrain-baskarta-a31b-2026-10-26). The datum is
  **not confirmed** (assumed RH 2000).
- **Bake:** [`tools/outer_terrain.py`](../tools/outer_terrain.py) interpolates a height model
  (linear over a Delaunay triangulation of the contour vertices and spot heights; scipy) and
  builds the land top as 20 m cells inland and 5 m cells within 20 m of the shore. A coarse
  cell beside fine cells carries their 5 m vertices on that side, so the surfaces share
  heights along it. Heights are floored at 1 m so waves do not wash over the surface; shore
  walls run from the water up to the vertex heights, with the old -2 m underwater skirt.
  Colour is quay grey near the water, greener with height and bare rock on steep triangles.
  Generation takes about 23 s.
- **Output:** 165,210 triangles (2,668 water first; ~119 k land tops), 87,049 vertices,
  heights 1.0–77.7 m, `geometry.h` 12.9 MB. Water, collision, the playable boundary and the
  A3.1 route test are unchanged (the same 2,668 water triangles and 2,590 shore edges).
- **Accuracy:** a contour-only model against the 11,529 spot heights on land: mean error
  0.73 m, RMSE 2.41 m, 95th percentile 2.48 m, maximum 96 m. The outliers are not
  understood; spot heights on bridges and rooftops are the likely cause. Contours are
  0.5 m apart, so open ground is much better than the maximum suggests.
- **Generator checks:** the surface area equals the land area to 1 m²; no land piece was
  dropped (zero-width spikes are opened and zero-area triangles removed); every interior edge
  is shared by exactly two triangles (no T-junctions between the 20 m and 5 m cells, no
  holes); a synthetic tilted plane is reproduced exactly. `clip_by_rect` returned geometry
  outside the region near the slice's south edge, so exact intersection is used.
- **Visual check (agent review, not a game capture):**
  [`outer-relief.png`](adventure/outer-relief.png) draws the baked land tops with hill
  shading. The Hisingen and Älvsborg hills, the river valleys and the islands read correctly,
  and the Arendal quays are flat. The thin hatching in the image is plot anti-aliasing.
- **Verification (2026-10-26, macOS arm64, Debug):** build warning-free; the 96 non-GPU
  active tests passed, including the extended `boat_outer_map`. **The 23 GPU-labelled tests
  could not run in this session** (`GLFW: Failed to initialize platform`, no display); they
  passed before A3.1b and do not load this map.
- **Limitations:**
  - **Datum unconfirmed**, and the hold-out maximum error is unexplained.
  - Nothing renders this land yet. At 165 k triangles it is about twice the river (78.6 k triangles,
    buildings and bridge included), so its draw and culling cost is **unmeasured** until A3.2
    wires it up; the 20 m inland cell is a candidate to coarsen then.
  - Vinga and the islands west of E ≈ 132 km have no Baskarta heights; the Lantmäteriet
    order (pending) is the intended source. Heights on bridges, rooftops and quay edges are
    approximations of ground, and buildings and the bridge are still absent.
  - The 12.9 MB generated header is large for the repository; a binary format is a later
    option if clone or compile time becomes a problem.
  - No manual playback or human acceptance.

### A3.2 result, 2026-10-26

Branch `adventure-a3-2`, cut from `adventure-a3-1` because the A3.1 pull request (#5) was not
yet merged; rebase onto `origin/main` once it is.

- **Location:** `--location outer` (`BOAT_LOCATION_OUTER`, a third `BoatWorldDefinition`) binds
  `waterfront_outer_map`. The map shares the river's frame, so the spawn (the dock), fleet
  start, current and wake-test placement are the river's; the camera is the river's close view
  with a 4,500 m far plane. No new renderer code: both renderers already bound any mapped
  description (A1.4), and `outer` takes the tiled land culling and indexed water of A2.
- **Scenery, harbor and traffic (decision):** the river kit (cafe, street furniture, crane,
  bridge deck) stays river-only, so `outer` has none yet; quays, frontages and forecourts are
  derived from the map as for any mapped location. The moored fleet, travelling craft and
  gulls are authored in the river's frame and keep running unchanged inside the river part of
  the slice (contacts included); nothing is added for the new geography. A3.3 bakes the river's
  buildings and bridge into the map.
- **Playable boundary (decision):** no authored land or extra geometry. Collision already keeps
  the boat inside; where the region cuts open water, the water ends and the view meets the
  horizon (GPU fog; CPU background colour). The boundary captures below show it reading as
  open sea from the boat's start 100 m inside the south-west edge; closer approaches and
  other edges were not captured.
- **Developer option:** `wake_velocity --start X,Z[,DEGREES]` places the boat in a mapped
  location for captures (simulation X/Z; `--location outer --start 5200,-2300,90` faces the
  south-west boundary). It is rejected for the basin, course, replay, ghost and `--connect`.
- **Other changes:** the title's solo place row names it OUTER RIVER and the host row cycles
  it; `boat_server` accepts it; multiplayer Accept's location byte allows 2 and the content
  digest covers the outer map ([protocol note](MULTIPLAYER_PROTOCOL.md)); race rooms
  still require the river. `boat_outer_map` now also checks the location record, that the spawn
  and fleet start are navigable and that a prepared world answers queries.
- **Captures (agent review, 2026-10-26, macOS arm64, scripted drive):**
  [dock GPU](adventure/outer-game-dock-gpu.png), [open leg GPU](adventure/outer-game-mid-gpu.png),
  [boundary GPU](adventure/outer-game-edge-gpu.png) and
  [boundary CPU](adventure/outer-game-edge-cpu.png). The dock capture shows hills beyond the
  river banks and the river's moored/travelling craft; the quays are the baked dark walls with
  no buildings (as of A3.2; superseded by A3.3).
- **Verification (2026-10-26, macOS arm64, Debug, display available):** configure and build
  warning-free; `ctest --test-dir build -j 4 --output-on-failure` passed all 122 active tests
  (15 retired disabled) in 192 s, including the GPU-labelled tests that A3.1b could not run.
- **Limitations:**
  - **Performance is unmeasured:** the 165 k-triangle land has not been timed per frame on
    either renderer, and the 20 m inland cell is still a coarsening candidate. Memory of the
    loaded map is unmeasured.
  - No manual playback; the captures are 1–12 s scripted frames. Only the dock, one open-water
    leg and the south-west boundary were looked at: the route past Nya Älvsborg and the
    Långedrag shore, and the other boundary edges, were not.
  - Moored boats and traffic that sit on the river's flat quays were authored for flat ground;
    their contact with the baked quay heights in the outer map is only as good as the
    river's matching crop, and has not been examined.
  - Everything listed under A3.1 and A3.1b still applies (datum; the buildings and bridge arrived in A3.3).
  - The multiplayer digest changed, so an outer room needs matching builds; no networked outer
    session was run.

### A3.3 result, 2026-10-26

Branch `adventure-a3-3`, cut from `adventure-a3-2` (pull request #7, still in CI when this was
written); rebase onto `origin/main` once it is merged.

- **Approach (decision):** the river's own bake is the source for the river crop, so the outing
  looks and behaves there as the river location does. `tools/prepare_outer_map.py` keeps the
  A3.1 water, route proof and collision exactly (water and its 2,668 triangles are unchanged)
  and imports the river's triangles above ground from `assets/geometry/river/geometry.h`:
  70,940 buildings, roads, gardens and trees, then the river's 4,388 bridge triangles last,
  which become the outer map's bridge range (`outer_bridge_first_triangle`).
  The river's own water, flat land top and quay walls are dropped; the outer map has its own.
- **Ground (decision):** inside the river crop the land is flat at the river's 3.5 m, so the
  imported buildings, roads and approaches sit on it as they do in the river; the Baskarta
  terrain blends in over 200 m outside the crop (smoothstep) so there is no step at the crop
  edge. The river part of the outer map therefore loses A3.2's relief (hills inside the crop
  are flat, as in the river location). Hills outside it are unchanged.
- **Seam check (scratch script, not a test):** the river's water and the outer water within the
  crop differ by 458 m² of 1.28 km² (96 m² outer-only, 362 m² river-only, largest sliver
  114 m²). The 32 imported triangles that overlap outer water (1,948 m² in total) are the same
  32 (1,950 m²) that overlap the river's own water, so the overhang is inherited from the
  river bake and not new.
- **Output:** `assets/geometry/outer/geometry.h` is 19.8 MB (was 12.9 MB): 148,441 vertices and
  240,538 triangles (165,210 before), 118,938 of them terrain land. Two runs produced identical
  bytes (about 25 s each). The manifest gained `river_overlay`. The bridge reaches the river's
  107.6 m tower tops, so the validator's height bound moved from under 100 m to 100–110 m.
- **Captures (agent review, 2026-10-26, macOS arm64, scripted drive):**
  [dock with buildings, GPU](adventure/outer-dock-buildings-gpu.png),
  [under the bridge, GPU](adventure/outer-bridge-gpu.png) and
  [under the bridge, CPU](adventure/outer-bridge-cpu.png) (`--start 250,-640,90`, facing the
  bridge). The deck and tower overhead and the building frontages read as in the river.
- **Measured cost (Debug, M1 Max, CPU renderer, 3 s scripted screenshot):** peak resident
  memory 214 MB river against 231 MB outer; wall time 1.96 s against 3.39 s including load.
  Single runs, not controlled.
- **Verification (2026-10-26, macOS arm64, Debug, display available):** configure and build warning-free; `ctest --test-dir build -j 4 --output-on-failure` passed all 122 active tests (15 retired disabled) in 197 s, including the GPU-labelled tests and the extended `boat_outer_map` (bridge range, height bound).
- **Limitations:**
  - **GPU frame time is unmeasured.** The benchmark (`boat_water_review --benchmark`) is
    river-only and the app's profiler overlay is not in screenshots; the 240 k-triangle map
    has no per-frame GPU or Release figure, and the 20 m inland cell is still a coarsening
    candidate. The numbers above are Debug CPU-renderer memory and load only.
  - The route past Nya Älvsborg and the other boundary edges were still not looked at, and
    there was no manual playback or human acceptance. A boat passing under the bridge was
    captured only from the water, not driven through.
  - The river scenery kit (cafe, street furniture, crane, bridge deck props) is still
    river-only; the baked map brings only what the river bake held.
  - The flat ground inside the crop discards real relief there (decision above).
  - The multiplayer content digest changed again (the mesh changed), so an outer room needs
    matching builds; no networked outer session was run.
  - The generator needs the pinned Python packages; this session installed them into a scratch
    virtual environment outside the repository.

**Next: A3.4**, the Långedrag shelter destination, checkpoint recovery and the exposed-water
encounter.

## Verification and handoff template

Every implementation handoff runs the repository's required commands:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build -j 4 --output-on-failure
```

Use desktop/localhost-capable execution for GPU and socket tests; this workspace's sandbox
previously blocked those checks. Retired physical wake-field experiments remain disabled.
Use the existing test-name inventory for focused checks rather than assuming old test counts.
New physical ownership/query tests belong in the core/CPU suite; GPU tests require a context.

For each completed task record: starting revision and related edits; changed behavior/APIs;
artifact paths/hashes; actual commands/options/platform; test results; inspected frames;
manual playback and human acceptance separately; remaining limitations; next task. Update
the detailed ledger here, the adventure's milestone status and the index's short next task.

Historical A0.1 instruction (completed 2026-10-25):

> Execute A0.1 in `docs/FREE_RIDING_ADVENTURE_FIRST_STEPS.md`. Preserve the existing planning
> changes. Produce the current-map/copy/cache inventory and original river reference evidence,
> run the required verification, and update the ledger. Complete A0.1 only; report A0.2 as next.

Historical follow-up batch (A0.2–A0.4 completed 2026-10-25 in separate handoffs):

> Execute A0.2–A0.4 in `docs/FREE_RIDING_ADVENTURE_FIRST_STEPS.md`. Finish the reproducible
> coastal survey and controlled river baseline, with separate evidence per task. Continue
> independent work if optional terrain/weather access is unavailable. Finish with the A0
> first-slice/tile-size decision and the A1.1 handoff; do not begin ownership changes yet.

## Planning evidence — 2026-10-25

Reviewed current CLI parsers, route/benchmark timing paths, map and water-mesh globals,
map-derived dressing, world/session/fleet copy/reset contracts and geometry digest consumers.
No runtime implementation or source acquisition was performed for this handoff plan. Source
recommendations retain the parent plan's research; provider access will be rechecked in A0.2.
Verification on macOS arm64, Debug, GPU checks enabled and retired physical wake-field
experiments disabled: the normal configure/build passed; the desktop/localhost-capable
`ctest --test-dir build -j 4 --output-on-failure` run passed all 110 active tests in 145.36 s,
with 15 experimental tests disabled. Log: `build/adventure-first-steps-ctest.log`.
Checked 24 local links/anchors across the two plans and their owner references;
`git diff --check` passed. Existing command examples were checked against current parsers;
the proposed benchmark interface is labelled unimplemented. These checks verify the current
application/documentation, not execution of A0/A1. No new audiovisual review was performed
or required for this documentation-only change.
