# Continuous voyage to Vinga

Status, 2026-11-15: planning only; no map, gameplay or release changes implemented.
Completed: source/access investigation and the staged integration plan below.
Open: full terrain acquisition and raster inspection, corridor selection, implementation and playtest.
Done when: a continuous Gothenburg–Vinga voyage and nearby start are playable, qualified and documented.
Next task: V0, acquire and validate the corridor's ground terrain and freeze its playable boundary.
Owner: this plan executes adventure A4; the [adventure plan](FREE_RIDING_ADVENTURE_PLAN.md) retains direction.
Verification: planning evidence below; implementation checks belong to each step's result.
Known limits: metadata and a TIFF byte-range probe establish access, not usable land-height coverage.

## Scope and decisions

Extend the existing `outer` map west to Vinga while retaining Gothenburg Harbour, the upstream
city/canals and the Långedrag outing. Free boating can traverse the whole connected map without
an objective. Add an optional Vinga destination voyage and a nearby start for short outings.
Keep metre-scale geography, the existing crane origin, integer-scaled visuals and current boat
handling initially. The lighthouse and rocky island silhouettes should make arrival recognizable.

This is a content and integration milestone, not a new world engine. Reuse indexed collision,
map tiles, culled scenery, local water rendering and the existing voyage/recovery/weather system.
Begin with the current scenery-pack loading model; add residency/streaming only if V2's measured
memory, loading or frame costs require it. Do not enlarge the far plane indiscriminately.

Check against the [Open loops board](README.md#open-loops): this work defers cargo/economy and
sandbox progression, marina occupancy/schedules, city building overhaul, living-city actors,
Grodan low-bridge contact and sights, graphics/cloud/sunset reviews, server hardening follow-ups
and general multiplayer/platform acceptance. It does not resume the user-paused harbour track.
A3's old street/moored-life backlog is not a prerequisite for this geographic expansion; newer
harbour, living-city, marina and sandbox plans own those systems. Keep remaining OSM landmark
polish with the [adventure ledger](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#landmark-backlog-2026-10-27).
A5 live-weather and shared regional-weather work remains deferred.

Keep one local implementation branch for this milestone and one focused commit per step.
Open/push a PR only after explicit user approval. Publishing and Winterfell deployment remain
separate release work; this planning request does not authorize either.

## Evidence and source choices — 2026-11-15

- A0's pinned OSM/land-polygon extract and [route survey](adventure/README.md#route-and-coverage-survey-a03-2026-10-25)
  already reach Vinga. The candidate dock–Vinga harbour route is 19.63 km; the Långedrag outing
  is 6.02 km on a different route. Revalidate the full route against the new compiled collision;
  the old 2D survey is not a gameplay or clearance acceptance test.
- Local `baskarta_dwg_v2_639_12.zip` contains two DWGs, `6390_126` and `6390_127`.
  LibreDWG 0.14 decoded both successfully. The former contains 29 roof polylines/313 vertices
  (Z 6.70–43.50 m); the latter one roof polyline/73 vertices (Z 3.10 m). Their contour,
  spot-height and shoreline layers are empty. Roof heights are not ground relief. Each file
  also has one unsupported entity on `AJOURHALLNINGSSTATUS`; individual structures remain
  unidentified. The overview image does not establish vector coverage.
- All 73 DWGs in local `baskarta_dwg_v2_639_13.zip` were inspected: the approaches have actual
  shoreline, building, contour and spot-height entities, with uneven coverage. Existing
  `hojdmodell_2022_639_13.zip` supplies inner-approach GeoTIFFs, not Vinga. Prefer the existing
  Shapefile/OSM pipeline over a new production DWG dependency; use DWGs selectively where useful.
- The existing Geotorget product entitlement works with Basic authentication against
  `https://api.lantmateriet.se/stac-hojd/v1` (HTTP 200). API portal/OAuth access is not required
  for this acquisition path. Keep credentials in the user's password manager; never write
  them into manifests, logs, command arguments, generated assets or shipped code.
- An authenticated `dtm-cog` search around Vinga returned item `639_29`, with 1 m resolution,
  laser-scanned ground terrain, SWEREF 99 TM + RH 2000 (`EPSG:5845`), acquisition interval
  2019-11-29–2020-03-21 and declared vertical uncertainty 0.1 m. Its GeoTIFF is 15,854,247 bytes,
  10,000 rows by 5,000 columns. Metadata has a companion breakline GeoPackage (528,384 bytes).
  Use the actual item footprints rather than assuming every product tile is 10 × 10 km.
- The TIFF asset at `https://dl1.lantmateriet.se/hojd/data/grid/mhm/63_2/m639_29.tif`
  requires authentication too. A 32-byte authenticated request returned HTTP 206 and a valid
  TIFF signature. No full raster was downloaded or inspected in this investigation. The
  catalogue also lists surface models and laser data: explicitly select ground `dtm-cog`.

These are dated investigation results, not implementation claims. V0 must record reproducible
source manifests and full-file hashes. Full-corridor coverage and distribution terms still need
checking against the selected products. See the [source guide](adventure/README.md).

## Staged implementation

| Step | Deliverable | Completion evidence | Status |
| --- | --- | --- | --- |
| V0 | Reproducible terrain acquisition and agreed route corridor | Coverage/gaps map, raster inspection, pinned source manifest and bake inputs | Next |
| V1 | Continuous navigable outer map to Vinga | Collision-validated route, terrain/coast seams, existing-map regression checks | Planned |
| V2 | Both renderers and bounded full-map costs | CPU/GPU captures, clear/storm benchmarks, load/memory report | Planned |
| V3 | Optional full voyage and nearby start | Plugin validation, arrival/recovery/weather tests and measured drive | Planned |
| V4 | Recognizable Vinga and selective route dressing | Lighthouse/harbour/island review from water in clear and storm conditions | Planned |
| V5 | Integration qualification and human voyage review | Full tests, native/browser and map compatibility evidence, user playtest | Planned |
| V6 | Close contracts and handoff | Current specs, attribution, resolved/deferred items and final status | Planned |

### V0 — acquire terrain and freeze the corridor

1. Reconcile source frames and route coordinates against the current crane origin; check the
   survey's rounded display-origin fields before reusing generated coordinates. Preserve the
   current upstream/canal coverage when constructing the union with the western corridor.
2. Query `dtm-cog` for the surveyed route plus surrounding islands and a view margin. Download
   the required ground tiles and relevant metadata/breaklines offline, including Vinga's item.
   Record item IDs, footprints, timestamps, CRS, nodata, hashes, source terms and attribution.
   Store raw downloads under ignored `build/adventure/`; commit compact manifests/derived inputs.
3. Inspect actual valid pixels on Vinga, the harbour approach and intervening islands. Produce
   a coverage image separating valid land, water and missing terrain; metadata bounds alone
   cannot pass this step. Check heights near shore and across the city-source overlap.
4. Add a reproducible offline import/reprojection path from SWEREF 99 TM to the existing
   EPSG:3007 metre frame. Retain RH 2000 heights and document game water-zero treatment. Pin
   geospatial dependencies. Explicitly mask nodata: the current nearest-land fill must not
   silently invent relief on an uncovered island. Choose and record source priority/blending
   at the city/national DEM seam, preserving existing city heights where possible.
5. Review a corridor map: river mouth → inner approaches/Rivöfjorden → Vinga harbour. Name
   selected shelter candidates and the short-start candidate (initially Saltholmen, subject
   to usable navigable water and terrain). Finalize the polygon from the survey and views,
   not an arbitrary rectangular expansion. Mark playable boundary separately from view context.

Gate: valid land-height coverage or explicitly labelled authored exceptions for every island
needed by the route; no unresolved terrain hole on Vinga. Document exact remaining gaps before V1.

### V1 — bake a continuous map

Extend `tools/prepare_outer_map.py`, terrain preparation and scenery packing while retaining the
compiled water/scenery-pack split. Keep core free of I/O. Bake real island relief, connected
water, shore collision and stable tile identity. Clip open-water boundaries beyond intended
routes, shelters and recovery areas; interior tile joins must never behave as shores.

Validate the full route and boat envelopes against production collision queries, including
narrow passages, harbour entry and turns. Exercise both directions, boundary joins and all
spawns/recovery poses. Record selected craft dimensions and clearance margins. Preserve the
existing bridge/canal water, overhead queries, race courses, work sites and plugin scene poses.
The survey route is a candidate, so reroute around rocks or inadequate channels as necessary.

Gate: one continuous collision-valid passage with no false interior barriers, and no unexplained
regression inside the pre-expansion map. Record geometry counts and compiled collision footprint.

### V2 — render and measure before adding detail

Load the extended scenery pack on CPU and GPU. Start with terrain silhouettes and sparse OSM
buildings; compare identical city views before/after expansion. Capture inner approaches,
mid-route islands and Vinga, including rapid turns and clear weather. Extend coarse distant
silhouettes only where the measured visual need justifies them; keep the lighthouse readable
near arrival without rendering detailed scenery across the entire voyage.

Measure active-work p50/p95/p99, displayed intervals/stalls, triangles/draw calls, collision query
cost, startup/cold-load time, scenery size, CPU/GPU allocations and process high-water memory.
Use the current main baseline on named hardware, plus the adventure plan's engineering targets
(native GPU 60 Hz, active-work p95 ≤12 ms/p99 ≤16.67 ms). Do not use old October timings as
current evidence. Run repeated out-and-back travel, recovery jumps, worst clear view and storm.

Gate: a measured fit within agreed budgets. If all-resident scenery exceeds memory/load budgets,
implement bounded residency/prefetch as a focused substep here; qualify unload/reload/failure
and upload stalls before proceeding. Define browser download/memory limits from its baseline.

### V3 — make the voyage playable

Author voyage content through the current plugin route contract, following `plugins/langedrag`,
rather than compiling a second mission framework into C. Add a separate selectable Vinga
outing and a nearby-start variant; keep Långedrag independently available. Both use the same
`outer` map and stable location ID. Check the title/plugin selection and the native/browser
start path rather than assuming alternative spawn selection already exists.

Use at most the existing four destination-voyage shelters initially: start, up to two useful
intermediate shelters and Vinga arrival. Validate names, poses, approach areas, recovery headings
and controlled-speed arrival with actual boats. Keep the route advisory rather than forcing
ordered checkpoints. Retain free boating without an objective.

Begin with the existing deterministic local exposure recipe, tuned for the open crossing;
do not make new regional weather physics a dependency. Test clear and rough passage, shelter
entry/exit and recovery. Use simulation time in automated checks. Record actual full/short
outing times and stops with the current explorer boat before changing speed or map scale.

Gate: full and short outings can start, arrive and recover reliably; a human can understand the
destination cue. Extending the shelter or weather contract needs an explicit demonstrated need.

### V4 — lighthouse, harbour and route character

Identify the DWG structures against suitable references before assigning them to the lighthouse.
Author its recognizable silhouette and selected adjacent buildings/harbour structures. Use roof
geometry as evidence, with ground sampled separately. Add rocky silhouettes, sparse vegetation,
channel landmarks and shelters where they make the passage legible. Avoid extending dense city
actors, traffic or unique assets across every island.

Review from normal boat cameras on both renderers, near/far and clear/storm, in motion as well
as stills. Check landmark visibility, shoreline contact and a satisfying controlled arrival.
Do not rely on storm fog to hide gaps or on an implausibly early lighthouse reveal to guide play.

### V5 — compatibility, qualification and playtest

Run Debug configure/build and `ctest --preset full`; add meaningful map topology, source/nodata,
route/arrival/recovery and plugin checks as their behavior changes. Capture representative CPU
and GPU frames and drive windowed full/short voyages for motion, audio and loading. Use Instruments
if measured stalls or scaling regressions need attribution. Repeat established city/canal,
race, work-site and multiplayer map checks; test boats separated between city and Vinga.

The changed compiled water changes map content identity even with the same `outer` ID. Verify
native/server/browser agreement, scenery-versus-water validation, mixed-version rejection and
browser stale-pack/cache behavior. Inventory saved work sessions/recordings and document their
actual compatibility; do not silently invalidate them or promise migration without checking.
Qualify cold browser asset loading and full/short native/browser outings. No live weather or
new shared regional-weather system is required. Wider platform gaps remain explicit.

Ask for user playtest of travel length, route legibility, shelter usefulness, storm handling and
arrival. Record automation, agent playback, human feedback and acceptance separately. Failed
pacing should first lead to better chapter starts/shelters, not geographic rescaling.

### V6 — close the milestone

Move the implemented geographic/bake/source/loading contract into a new `docs/WORLD_MAP.md` spec,
including coordinate frames, source precedence, nodata, collision boundaries, scenery identity,
visibility/loading limits and offline regeneration. Update `PLAYING.md`, `COMMAND_LINE.md`,
`PLUGIN_API.md` if its contract changes, `TESTING.md`, asset attribution and affected plugin guides.
Update the adventure plan/index and reconcile live source-access claims in the manifest/source
guide; keep October evidence labelled as history. Search README/docs/guides for superseded claims
and check changed links/commands. Close or explicitly defer every residual item. Mark A4 done
only once the playable/verification gates are met; release/deployment stays with its release owner.

## Planning verification

2026-11-15: read the current map/terrain pipeline, location and voyage interfaces, shipped
Långedrag plugin, adventure history and Open loops board. Authentication and source inspection
above were performed during this conversation. No new terrain bake, gameplay implementation,
performance measurement, manual game review or user acceptance is claimed by this plan.

Planning handoff checks, 2026-11-15, macOS arm64: Debug configure/build passed in the
separate planning worktree, using the existing pinned raylib/ENet source checkouts. The full
CTest preset ran all 571 tests: 554 passed in the sandbox; 17 socket/process checks failed
there and all 17 passed with local networking available on `--rerun-failed`. Local file links,
new-plan heading anchors, source-manifest JSON parsing and `git diff --check` passed. These
checks qualify the documentation branch; no new audiovisual review was needed or performed.
