# Free-riding adventure: first implementation tasks for Sol

Prepared 2026-10-25 against `7165ba5` and the uncommitted adventure-planning documents.
**Status: A0 (A0.1–A0.4) completed 2026-10-25; [A0 decision](#a0-decision-record--2026-10-25) recorded; A1.1 is next.**
The [adventure plan](FREE_RIDING_ADVENTURE_PLAN.md) owns the larger direction and budgets;
this document owns the detailed A0/A1 task ledger. The
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
| A1.1 | A0 complete | Explicit immutable map description and independently constructible collision cache | Pending; next |
| A1.2 | A1.1 | Borrowed map resources wired through simulation, session, fleet and networking | Pending |
| A1.3 | A1.2 | Caller-owned water-mesh caches with unchanged topology | Pending |
| A1.4 | A1.3 | Explicit CPU/GPU map binding and map-dependent scenery ownership | Pending |
| A1.5 | A1.4 | Reload/isolation/failure gate, parity evidence and A2 handoff | Pending |

A0.2/A0.3 and A0.4 have independent inputs after A0.1, but controlled performance runs must
run alone. If terrain credentials are unavailable, document that result and continue the OSM
survey; terrain import is optional in this batch. A1 requires a useful survey of the first
extension, not complete terrain coverage or a complete Boelo observation archive.

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
This is recorded as a gap, not fixed. Debug CTest passed 112/112 active tests in 175.96 s.
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
  p95 active-work target. Display pacing misses 60 Hz before any map growth. A1.5 repeats this
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
