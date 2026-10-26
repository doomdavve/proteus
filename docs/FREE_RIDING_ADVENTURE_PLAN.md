# Free-riding adventure: Gothenburg to Vinga

Status, 2026-10-25: [A0.1 original-river inventory and reference](adventure/baseline.md),
the [A0.2 pinned coastal sources](adventure/README.md#coastal-sources-a02-2026-10-25) and the
[A0.3 route/coverage survey](adventure/README.md#route-and-coverage-survey-a03-2026-10-25)
and the [A0.4 controlled river baseline](adventure/baseline.md#a04-controlled-river-baseline--2026-10-25)
completed, closing A0 with its [decision record](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a0-decision-record--2026-10-25).
[A1.1](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a11--explicit-map-data-and-collision-cache-construction)
(explicit map description and caller-owned collision cache, with river queries and captures
unchanged) completed the same day. [A1.2](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a12-result-2026-10-25)
then threaded caller-owned `BoatWorldResources` through worlds, sessions, fleets and
networking and removed the global wrapper, with traces, captures and the content digest
unchanged. [A1.3](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a13-result-2026-10-25) moved the
water-mesh caches into view-owned `BoatWaterMeshCache`s bound to a map description, with
topology and all review captures unchanged.
[A1.4](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a14-result-2026-10-25) bound both renderers and
the city dressing to explicit, view-owned scenery built from a map description, with every
river capture unchanged. [A1.5](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a15-result-2026-10-25)
proved reload, owner release, failure and GPU recreate parity under ASan/UBSan, with traces,
captures and the controlled frame matrix unchanged (no p95 growth), which
[completes A1](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a1-completion--2026-10-25).
A2, the tiled current river, is [split into A2.1–A2.5](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a2--tiled-current-river).
[A2.1](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a21-result-2026-10-25) added application
submission and triangle counters to both renderers (2026-10-25): about 940 GPU submissions
per frame, 97% of them objects, and the whole 0.86 M-triangle map drawn in every pass.
[A2.2](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a22-result-2026-10-25) added the in-memory tile
catalogue (512 m and 1 km) and a 16 m collision grid. Its queries matched the exhaustive scan
in 3.6 million comparisons, with river traces and captures unchanged.
[A2.3](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a23-result-2026-10-25) culled land per tile in
the shadow, reflection and main passes on both renderers (2026-10-25). With the 1 km default
chosen over 512 m by a controlled matrix, median map triangles per frame fell from 0.86 M to
0.14 M and GPU p50 by about 14%, with submission p95 within 0.1 ms. CPU captures were
unchanged; the GPU differences are a few depth-tie pixels, explained.
No expanded geography, streaming, adventure rules or new weather is implemented.
[A2.4](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a24-result-2026-10-26) indexed the water
(256 m grid) so GPU patch rebuilds visit only nearby triangles and the CPU surface displaces
only what the camera sees (2026-10-26, branch `adventure-a2-4`): CPU captures byte-identical,
mean CPU triangles 2.34 M to 1.30 M; GPU waves captures identical to main, GPU CTests passed.
[A2.5](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a25-result-2026-10-26) then showed, on synthetic
corridors up to 25 km, that queries, patches and the visible CPU water at an unchanged view
cost and return exactly the river's, and made the CPU water tessellate lazily (218 MB to a
constant 112 MB). A2 is complete; GPU timing at enlarged scale was not run.
A3 began with [A3.1](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a31-result-2026-10-26) (2026-10-26),
an offline bake of the first slice's water and land with the A0.3 route proven against the
collision queries, and A3.1b (2026-10-26) gave its land real heights from Göteborg's Baskarta
([result](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a31b-result-2026-10-26)); and A3.2 (2026-10-26) added the mapped `outer` location that loads it on both renderers
([result](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a32-result-2026-10-26); frame cost unmeasured,
no buildings or bridge yet).
**Next: A3.3, bake the river's buildings, bridge and roads into the combined map.**
That handoff details A0's offline route/data survey and measured baseline, followed by A1's
mapped ownership tasks, all now complete. The proposed first playable is a short
extension beyond Älvsborgsbron with one sheltered destination and one exposed-water encounter.
Vinga is the eventual destination, reached through independently playable expansions.
[All plans](README.md) · [Gameplay direction](BOATING_PLAN.md).

This document owns expansion scope, data preparation, bounded world costs and the free-riding
adventure. The [foundations plan](BOATING_FOUNDATIONS_PLAN.md#phase-w--explicit-world-ownership)
owns the prerequisite mapped-world/cache ownership work; the [river record](river/README.md)
owns existing coverage. Race, multiplayer and physics plans retain their existing work.
This planning request does not start implementation or create additional approval gates.

## Experience and geographic scope

Leave the familiar river, pass under the bridge, follow increasingly open water, choose shelter
when the weather builds, and eventually find Vinga's lighthouse through spray and rain. Make
the journey itself the adventure: recognizable landmarks, changes in exposure, a reason to
slow down or choose another channel, and a satisfying arrival. Free riding remains available
without accepting an objective.

Start with one optional destination and a return-to-shelter objective. Use a bearing/distance
cue and a few discoverable stopping places; recovery returns to the last suitable sheltered
checkpoint. An arrival requires entering a generously sized approach area at controlled speed.
No docking precision challenge, mission framework, inventory, economy or progression system
is needed for the first slice. A small explicit session state is sufficient.

Proposed progression, subject to the A0 map survey:

1. **Outer river:** extend a few kilometres beyond current coverage toward the river mouth;
   prove crossing map tiles and the contrast between sheltered and exposed water. Target a
   5–10 minute outing including exploration and arrival, rather than a prescribed speed run.
2. **Inner approaches:** add the Nya Älvsborg / southern-archipelago approaches, a few useful
   stopping places and alternative sheltered passages. Survey the route before choosing exact
   island stops; this is a game itinerary, not a navigation route.
3. **Vinga:** complete a continuous passage into exposed water, with recognizable rocky islands,
   an authored lighthouse and an arrival/recovery area. Add a nearby start for short storm
   sessions as well as the full voyage from the river.

The current crop is 2,450 × 2,400 m. A spherical distance calculation from the generator's
crane origin (57.6973921 N, 11.9089495 E) to SMHI's Vinga A station (57.6322 N, 11.6048 E)
is approximately **19.5 km straight-line**. For pacing, 20–25 km at an assumed average 20 m/s
takes 17–21 minutes before stops; rough water and route choices increase this. These are
planning estimates, not observed play times. The A0.3 survey measured one candidate water route
from the dock to Vinga gästhamn at **19.63 km** (16.4 min at a constant 20 m/s), with a 2D 8 m
clearance check only. It proposes a 10.9 km² first slice to Långedrag's old harbour, about 6 km
from the dock ([survey](adventure/README.md#route-and-coverage-survey-a03-2026-10-25)).
[SMHI station listing](https://www.smhi.se/data/hitta-data-for-en-plats/ladda-ner-vaderobservationer/maxMeanWindSpeed).

Keep metre-scale geography initially. Build a route corridor with enough surrounding islands
for exploration and convincing views; avoid making all of Gothenburg equally detailed. A0
will define its polygon, source margin and playable boundary. If travel feels too long, add
start locations and chapter entry points before changing geographic scale.

## Current implementation and scaling constraints

| Existing component | What expansion needs |
| --- | --- |
| `tools/prepare_waterfront.py` generates one compiled OSM crop with an assumed 3.5 m flat quay | An offline tiled bake, real island relief and explicit land/water/collision metadata |
| `waterfront_city_river_map` is one compiled, static river description, bound explicitly since A1 | A world definition containing a spatial tile catalogue and independently addressable geometry |
| `boat_map.c` finds water triangles and shoreline edges from that mesh; since A2.2 a 16 m candidate grid answers queries exactly like the former scan, and `boat_map_tiles` catalogues the crop | Caller-owned map data plus a spatial index; queries inspect nearby candidates (done for the crop) |
| `boat_water_mesh.c` caches are view-owned since A1.3, but the CPU reference subdivides all mapped water | Bounded visible water coverage on both renderers |
| GPU water already has a 192 m local patch with a finer centre, but rebuilding it visits all mapped water triangles | Preserve the local-detail approach; query only intersecting tiles and add coarser outer water rings |
| GPU city, reflection and shadow passes use whole-scene meshes | Per-tile visibility and detail selection in every pass |
| `BoatWorld`/`BoatSession`, 120 Hz ticks and bounded arcade wakes already exist | Extend these owners; keep tick cost independent of total geography |
| `BoatStorm` and the race presentation already provide physical rough water and storm effects | Reuse recipes while separating persistent regional weather from the moving race obstacle |

Increasing the crop and far plane alone would increase query cost, geometry, initialization
and reflection work. The river record's earlier approximately 9.1 ms GPU submission sample
is historical, not a complete display-frame measurement or a Vinga performance estimate.

## Data sources and acquisition

Use offline source snapshots and generated assets. Normal play should work without map or
weather services. Preserve source date, bounding box, coordinate/height reference, product
version, attribution, licence, checksums and the bake-tool version in each manifest.

| Need | Recommended source | Use and limitations |
| --- | --- | --- |
| Coastlines, islands, roads, buildings, place names and mapped landmarks | [OpenStreetMap Sweden extract from Geofabrik](https://download.geofabrik.de/europe/sweden.html), supplemented by [processed OSM land polygons](https://osmdata.openstreetmap.de/data/land-polygons.html) | Continue the current OSM pipeline. Extract locally from PBF; assemble complete coastal topology before clipping into game tiles. Tags and small-island coverage need inspection. Pin both datasets and reconcile their snapshot dates. |
| Island relief and rocky silhouettes | Lantmäteriet [Markhöjdmodell Nedladdning](https://geotorget.lantmateriet.se/dokument/projects/markhoejdmodell-nedladdning/released/1/) | One-metre terrain grid, downloaded through the authenticated STAC service. Bake simplified terrain at several detail levels; shipping one-metre meshes everywhere is unnecessary. Acquire a small island sample first. |
| Alternative detailed topographic features | Lantmäteriet [Topografi 10](https://geotorget.lantmateriet.se/geodataprodukter/topografi-10-nedladdning-vektor?page=index.html&version=2026.05&view=dokumentation) | Optional replacement/comparison source. Current product is fee-free GeoPackage, but has purpose review and product-specific conditions. OSM keeps the first survey independent of that access process. |
| Broad offshore depth | [EMODnet Bathymetry](https://emodnet.ec.europa.eu/en/bathymetry) | Regional depth/exposure input, optional for the first playable. The 1/16-arc-minute grid is about 116 m north–south and 62 m east–west here; grid spacing does not establish source accuracy. |
| Wind, gusts, direction, rain, visibility and cloud | [SMHI meteorological observations](https://opendata.smhi.se/metobs/demo) and its [API examples](https://opendata.smhi.se/metobs/examples) | Discover available parameters/periods for Vinga A (71380); retain timestamps, units and quality flags. Freeze a dated scenario snapshot. A gust maximum and a ten-minute mean are distinct inputs. |
| Wave height, period and direction | [SMHI wave observations](https://www.smhi.se/data/hav-och-havsmiljo/vagor) | Use available offshore buoy observations as regional references. Confirm station coverage/time overlap; do not assume a wave buoy at Vinga or that another buoy exactly represents it. |

Two constraints affect source selection:

- Swedish territorial-water input to EMODnet was supplied as 300 m minimum/maximum values,
  according to [Sjöfartsverket's contribution description](https://www.sjofartsverket.se/sv/tjanster/sjokortsprodukter/projekt-och-samarbeten/emodnet-bathymetry/).
  Near-shore rocks, grounding and harbour depth therefore need separate authored geometry or
  a suitable finer source. [Detailed Sjöfartsverket depth data](https://www.sjofartsverket.se/sv/tjanster/havsgranser/djupdata/)
  requires a separate request/review; it is not an assumed freely downloadable dependency.
- Keep OSM attribution and the ODbL obligations with the distributed derived map database
  ([OSM terms](https://www.openstreetmap.org/copyright)). Lantmäteriet's terrain product links
  to [CC BY 4.0 product terms](https://www.lantmateriet.se/globalassets/geodata/geodataprodukter/anvandningsvillkor_for_vardefulla_datamangder.pdf),
  including product/source credit and identification of processing. Do not label every
  Lantmäteriet product CC0. Record the selected EMODnet/SMHI dataset terms when acquiring data.

A0 acquires samples and establishes usable coverage; research of product documentation alone
does not establish that those samples import correctly. Keep source layers separately traceable
when baking a combined asset. Hand-author a few distinctive landmarks, starting with Vinga's
lighthouse, from suitable references instead of expecting map extrusion to capture them.

## World representation and loading

Keep the current EPSG:3007 projected metre frame and crane origin for this expansion, verifying
the surveyed corridor in A0. Normalize incoming sources offline. Retain double precision in
the bake, preserve the map-east / simulation-west X convention at one documented boundary,
and record height datum conversions before combining terrain, water level and bathymetry.
EMODnet depths use LAT; they cannot be pasted directly into an arbitrary game water zero.

At roughly 20–30 km from the origin, float position spacing is still approximately 2 mm.
A general floating-origin simulation is unnecessary for the first corridor. Use tile-local
vertices and camera-relative render transforms where useful. If rendering coordinates shift,
carry a bounded phase offset into water shaders so waves, wakes and physics stay aligned.
Keep persistent object/tile IDs and canonical simulation coordinates stable across loading.

Proposed bake and ownership:

```text
Pinned OSM + terrain + optional depth
                 |
     offline validation and tile bake
                 |
   manifest + collision index + render tiles + landmark records
                 |
      application loader / resource owner
          |                         |
  read-only world queries     CPU/GPU view caches
  boat core, no I/O           visible detail and uploads
```

Start by benchmarking **512 m square tiles**, comparing 1 km if metadata/draw overhead is
excessive. This is a starting parameter, not a fixed architectural requirement. Each tile has
explicit shoreline segments, land/water classification, bounds, geometry detail levels and
stable actor/landmark references. A spatial grid with per-tile indexed edges/triangles is
sufficient initially; an ECS or general scene graph is unnecessary.

Generate coastline topology before cutting tiles. Shared tile edges are internal boundaries,
never new quay walls. Preserve small islands and channels in collision data independently of
render simplification; use overlapping query margins for a hull that crosses several tiles.
Bake matching terrain edges and water masks, and test tile corners as well as straight joins.

Initially keep the whole corridor's compact collision index and coarse distant silhouettes
resident, measuring their footprint. Stream only detailed scenery. This avoids loading-induced
changes to physics and keeps a dedicated server simple. If collision residency later exceeds
the memory budget, treat physical-data streaming as a separate task with deterministic
activation and enough look-ahead to cover swept hull motion, wakes and recovery destinations.

The application owns files, decoding and loading; the core borrows immutable prepared data.
GPU uploads/destruction stay on the render thread. First establish tiled rendering with all
small-slice assets resident, then add a bounded loading queue and worker decoding if measured
costs require it. Do not add a thread system merely to split the current crop.

Prefetch around both the view and predicted travel direction. Size the lead from maximum boat
speed × measured worst-case preparation latency plus a turn/recovery margin. Use a byte-capped
cache and eviction hysteresis. A delayed detail tile retains its coarse view and real collision;
missing mandatory collision data fails world activation or holds at a known loaded boundary.
Never silently reinterpret missing data as open ocean. Reset/teleport prepares its destination
before resuming ticks. Ship a manifest and tile packs through native packaging; qualify a
separate bounded download/cache strategy for the browser after the native slice works.

## Keeping the frame rate stable

World extent should principally increase installed data, not work performed each frame.

| Work | Bound it by |
| --- | --- |
| Land/buildings | Visible tiles and screen-space detail; merge geometry by material within a tile, cull before drawing |
| Distant islands/lighthouse | Coarse silhouettes and a small landmark budget; a large visible distance need not imply detailed geometry |
| Water | A camera-centred fine patch and progressively coarser rings; shared shore masks and crack-free transitions |
| Boat physics | Existing 120 Hz tick, a fixed number of hull samples and nearby indexed collision candidates |
| Waves | A small deterministic set of directional components queried analytically at the hull and visible water vertices |
| Traffic, gulls and wakes | A bounded active neighbourhood; distant scheduled traffic derives position from route/time, then activates before interaction range |
| Rain, spray, foam, flags and audio | Local capped emitters/voices; do not populate weather particles over the entire world |
| Reflections and shadows | Their own culled tile sets/detail levels and bounded targets; include off-camera reflection/caster requirements |
| Loading | Prefetch, capped memory, capped work per frame and small uploads; no whole-tile triangulation in a simulation tick |

Do not simulate a fluid grid spanning Gothenburg–Vinga. Extend the analytic water approach and
bounded wakes already present. Sample physical weather independently of rendering detail.
Use scene detail/visibility and reflection quality as the first performance controls, keeping
physics cadence, collisions and the existing nearest-neighbour presentation consistent.
Clear weather must pass too; storm fog is atmosphere, not the only culling mechanism.

Initial **engineering targets**, to qualify against named hardware in A0. A0.4 qualified the
current river on an M1 Max: active-work p95 is at most 5.7 ms, but paced display ran at
58.6 Hz ([baseline](adventure/baseline.md#a04-controlled-river-baseline--2026-10-25)). The
[frame pacer](BOATING_PERFORMANCE.md#frame-pacing--2026-10-25) since holds 60.00 Hz without
VSync and the display rate with it; the user judged it smooth on macOS. Scan-out timing
and other platforms remain unmeasured.

- Native GPU: 60 displayed frames/s, 16.67 ms frame deadline. Aim for active work p95 ≤12 ms
  and p99 ≤16.67 ms, and separately report presented-frame intervals, missed deadlines and
  maximum stalls. Measure VSync waiting separately; CPU submission time is not GPU completion.
- Four-boat authority: retain the foundations/multiplayer target of ≤1 ms p95 and ≤2 ms p99
  per 120 Hz fleet tick. Two nominal ticks per 60 Hz frame leave useful rendering headroom;
  catch-up/discarded-time counters must remain visible.
- Main-thread streaming work: begin with a ≤1 ms per-frame allowance and report upload/driver
  stalls separately. Bound individual asset sizes so a single job cannot defeat the allowance.
- Start with a 256 MiB detailed scenery cache target, counting CPU and GPU allocations, and
  report always-resident collision, silhouettes, render targets and transient decode memory
  separately. This is a proposed budget, not a measured footprint or total-process limit.

Measure the existing river baseline and the same local view with increasingly large tile
catalogues (including a roughly 25 km corridor). Distant inactive detail must not cause a
proportional rise in tick/render cost. Record triangles, draw calls, candidate edges, active
actors/wakes, queue length, cache bytes and high-water memory. Test dense harbour views,
clear-weather distance, worst storm/foam, rapid turns, cold boundary crossings, teleport,
and repeated out-and-back travel. Extend `boat_profile` and the benchmark workload where
needed. Use GPU completion timing and Instruments when a measured stall requires attribution.
CPU rendering remains a deterministic reference with bounded coverage; 60 FPS CPU parity
and browser performance are separate qualification goals, not assumed outcomes.

## A storm inspired by Boelo

Use the user's 2026-10-25 report of 27 m/s gusts as inspiration for a named, dated preset.
It is neither a sustained wind value nor a measured wave height. A faithful historical
reconstruction needs observations with matching time/quality and local wave evidence. A0.2
retained Vinga A's hourly wind/gust/direction for 2026-10-24/25 (29.7 m/s peak gust at 07:00Z)
and two distant regional wave buoys; no local wave observations exist, so the event dataset
remains incomplete ([sources](adventure/README.md#findings-and-gaps)).

Define a compact weather recipe: mean wind vector, gust envelope/seed, a few wave components
with amplitude/period/direction, visibility, rain, cloud and optional water-level offset.
Keep observed fields distinct from authored gameplay values. Missing direction or wave data
uses an explicitly authored fallback, never an inferred measurement. SMHI directions describe
where waves come from; convert once to the engine's propagation vector and reflected X frame.

Bake a coarse directional exposure/fetch field from surrounding land. Blend sheltered harbour,
channels and offshore water smoothly; avoid instantaneous wave-height steps at tile boundaries.
Wind, flag, spray and sound should agree. The same position/time recipe drives CPU hull samples
and GPU displacement; derivative calculations must include spatial blending. Waves depend on
fetch, duration, depth and swell, so a gust should not instantly grow the entire sea.

Begin with modest rough water using the accepted landing/capsize model, then tune a storm
preset through play. Preserve legible steering, reasonable recovery and an achievable shelter
route. Rain/lightning from the race may be reused artistically; do not describe unobserved
lightning as historical Boelo data. A shared small component recipe is enough initially;
full spectral ocean simulation and calibrated coastal wave propagation remain deferred.

A future live-weather option fetches outside the tick, caches its last good dated snapshot,
shows its age, and falls back to an offline preset. Replays store the recipe/version/seed
and timed changes. A multiplayer authority chooses and distributes the weather; clients must
not independently fetch differing conditions. Add map-content hashes, compatibility/version
changes, coordinate-envelope and late-join checks before making the new region networked.
Retain bounded wake histories and activate shared actors by gameplay interest, not by an
individual client's camera. Four widely separated boats multiply local active regions and
need their own budget test.

## Implementation sequence and acceptance

The [first-steps handoff for Sol](FREE_RIDING_ADVENTURE_FIRST_STEPS.md) owns the detailed
A0/A1 task ledger, proposed interfaces, exact existing commands and acceptance checks.
A0 is complete: inventory, coastal sources, route survey and the controlled river baseline
(a review-tool benchmark only). Runtime map ownership changes began with A1.1; A1 (A1.1–A1.5)
completed 2026-10-25. A2 is in progress: A2.1 (counters) and A2.2 (tile catalogue and indexed
collision) and A2.3 (per-tile land culling) completed 2026-10-25 and A2.4 (indexed water) and A2.5 (same-view scaling) 2026-10-26, completing A2; A3.1 (baked first-slice map) and A3.1b (its land heights), both 2026-10-26, and A3.2 (the `outer` location) are complete and A3.3 is next.

| Step | Deliverable | Gate before expanding further |
| --- | --- | --- |
| **A0 — survey and baseline (complete 2026-10-25)** | Pin a corridor extract, inspect coastline completeness, sample terrain access, measure a water route and useful stops; document package/triangle/collision counts and baseline frame times | Reviewable coverage map with unknowns marked, reproducible manifests, no invented coastline closures; hardware/resolution/AA recorded |
| **A1 — world ownership (complete 2026-10-25)** | Complete the necessary mapped W1/W2/W3/W4 foundations work on current river data | Two independent map/view owners, repeat init/destroy, partial-failure cleanup and unchanged existing river behaviour; use two river instances now that Eriksberg is retired |
| **A2 — tiled current river (complete 2026-10-26: A2.1–A2.5)** | Bake tiles and indexed collision; cull land in main/reflection/shadow passes; bound CPU/GPU water coverage | Match current shoreline/bridge queries against exhaustive reference; no seams or false tile-edge contacts; measured same-view scaling; all small-slice assets may remain resident |
| **A3 — first outer-river adventure** | New connected geography, one destination/shelter, local weather transition, checkpoint recovery; bounded detailed-asset loading | Playable 5–10 minute outing, cold-load/rapid-turn/recovery tests, clear/storm performance and memory targets; human feedback on travel and exposure |
| **A4 — islands and Vinga** | Terrain silhouettes, selected harbour/landmark detail, continuous full voyage and nearby start | Actual route/time measurements, long out-and-back cache test, storm tuning, readable lighthouse/arrival and user playtest |
| **A5 — shared and current-weather voyages** | Network world/weather identity, spread-out players, optional cached live data; browser asset strategy if pursued | Replay/late-join consistency, four separated boats, service failure/stale data, packaged/native and browser-specific verification |

Complete each small step with its owning status/evidence update. A3 does not depend on detailed
bathymetry, live APIs or multiplayer streaming. If terrain access is unavailable, authored
island relief can unblock a clearly labelled prototype while acquisition continues. The first
commit should be A0 survey artifacts and measurements, not a vastly enlarged `geometry.h`.

For code changes, run the normal Debug build and full active CTest suite. Add meaningful
geometry/index parity and seam checks, owner isolation/lifetime checks, bounded loading stress,
and CPU/GPU weather height/normal/velocity comparisons. Capture and inspect representative
harbour, tile-boundary, island, open-sea and storm frames; manually drive the voyage for motion,
input, sound and loading behaviour. Record platform/options and distinguish automated results,
agent playback and user acceptance. Existing course/race/replay behaviour remains a regression
gate when shared infrastructure changes.

## Planning verification and open questions

- 2026-10-25: reviewed current source, existing river coverage, foundations/performance plans
  and primary provider documentation. Calculated the approximate 19.5 km straight-line distance
  using the existing origin and SMHI station coordinates. No route survey or live observation
  acquisition was performed.
- 2026-10-25, macOS arm64, Debug, GPU tests enabled and retired wake-field experiments off:
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` and `cmake --build build -j` passed.
  The initial sandboxed CTest run was interrupted after localhost permission failures and a
  stalled GPU check. The complete desktop/localhost-capable rerun of
  `ctest --test-dir build -j 4 --output-on-failure` passed all 110 active tests in 144.91 s;
  15 experimental tests remained disabled. Log: `build/adventure-plan-ctest-desktop.log`.
  This verifies the existing application, not the proposed expansion or frame budgets.
- At the original planning handoff, all 13 new-plan/owner local links and anchors checked
  successfully; `git diff --check` passed. Provider links were opened during research;
  authenticated downloads and new terrain
  imports were not exercised. No runtime options were introduced. The subsequent
  [Sol handoff](FREE_RIDING_ADVENTURE_FIRST_STEPS.md) distinguishes existing commands from a
  proposed review-tool interface; its verification is recorded there.
- No audiovisual changes were made; no new captures or manual playback are required for this
  planning change. Adventure feel and the proposed budgets have no user acceptance yet.
- Still to determine in A0: actual route/coverage, small-island topology, terrain access and
  datum metadata, selected dataset redistribution details, source/download/runtime footprint,
  weather station/time coverage, and named minimum hardware. Journey pacing and severe-storm
  handling need A3/A4 playtests rather than assumptions from the map or wind headline.
