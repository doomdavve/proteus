# Adventure survey and reference evidence

This directory holds the small, versioned evidence for the staged Gothenburg–Vinga work.
The [A0/A1 task ledger](../FREE_RIDING_ADVENTURE_FIRST_STEPS.md) owns execution status;
the [adventure plan](../FREE_RIDING_ADVENTURE_PLAN.md) owns scope. The original river
reference and the A0.4 controlled river frame baseline are in [baseline.md](baseline.md).
They precede any new coastal data or runtime map change.

Run review commands from the repository root after a Debug build. Full PNGs, CSVs and logs
belong under the ignored `build/adventure/a0-original/` directory. The baseline gives exact
paths and hashes for comparison. The retained [river source and crop guide](../river/README.md)
documents the current EPSG:3007 origin and OSM attribution. The compiled river is untouched;
the coastal sources below are survey inputs only.

## Coastal sources (A0.2, 2026-10-25)

[`sources.json`](sources.json) is the manifest. Its `acquisition` section records URLs,
retrieval times, snapshot dates, raw hashes, licences and unavailable sources by hand.
Its `derived` section is rewritten by `tools/prepare_adventure_sources.py`, which reads only
local files and records tool versions, input hashes, output content hashes, feature counts,
incomplete relations and the coastline comparison. Normal configure, build, tests and play
never fetch or read these sources.

| File | Contents |
| --- | --- |
| [`sources/osm-corridor.json.gz`](sources/osm-corridor.json.gz) | OSM ways (lon/lat geometry), landmark/seamark/member nodes and relations with IDs, versions and tags. Coast/water, buildings/parts, bridges/supports, major roads, ferry routes, harbour and landmark features. 6.3 MB compressed. |
| [`sources/land-polygons.json.gz`](sources/land-polygons.json.gz) | Processed OSM land polygons, unioned to remove split seams and clipped to the envelope. Edges on the clip box are source boundaries, not shoreline. |
| [`sources/weather-2026-10-25.json`](sources/weather-2026-10-25.json) | Vinga A hourly wind/gust/direction from 2026-10-24T00Z and two regional wave buoys' latest day, UTC, with SMHI quality codes and listed gaps. |

The envelope is the river crop united with SMHI's Vinga A position, expanded by 2 km in
EPSG:3007: longitude 11.5702598…11.9585149, latitude 57.6141283…57.7225589. It is a
first source box, not the playable boundary.

Map data © [OpenStreetMap contributors](https://www.openstreetmap.org/copyright),
[ODbL 1.0](https://opendatacommons.org/licenses/odbl/1-0/); land polygons processed by
[osmdata.openstreetmap.de](https://osmdata.openstreetmap.de/). Weather and wave observations:
source [SMHI](https://www.smhi.se/data/om-smhis-data/villkor-for-anvandning) open data,
CC BY 4.0; the values were selected and reformatted. The Brofjorden buoy is owned by
Sjöfartsverket.

### Acquisition (network; run only to replace the pinned inputs)

```sh
mkdir -p build/adventure/downloads/smhi-ocobs
cd build/adventure/downloads
curl -fLO https://download.geofabrik.de/europe/sweden-261024.osm.pbf
curl -fLO https://download.geofabrik.de/europe/sweden-261024.osm.pbf.md5
md5 -r sweden-261024.osm.pbf; cat sweden-261024.osm.pbf.md5   # must match
curl -fLO https://osmdata.openstreetmap.de/download/land-polygons-split-4326.zip
for p in 3 4 21 25; do
  curl -fL -o smhi-vinga-a-$p-latest-months.csv \
    https://opendata-download-metobs.smhi.se/api/version/1.0/parameter/$p/station/71380/period/latest-months/data.csv
done
for s in 33033 33015; do for p in 1 11 9 8; do
  curl -fL -o smhi-ocobs/wave-$s-p$p-latest-day.json \
    https://opendata-download-ocobs.smhi.se/api/version/latest/parameter/$p/station/$s/period/latest-day/data.json
done; done
cd ../../..
```

Geofabrik keeps dated files for a limited time; the land-polygon and SMHI URLs always serve
the latest data. A new download is a new source version: update `acquisition` and expect
different derived hashes. The SMHI `latest-months`/`latest-day` windows roll forward, so the
2026-10-25 storm can only be re-fetched later from SMHI's `corrected-archive` periods.

### Offline extraction and normalization

Installed and used on 2026-10-25 (macOS arm64): osmium-tool 1.19.1 from Homebrew; Python
3.14.2 with pyosmium 4.2.0 (libosmium 2.22.0), shapely 2.1.2 (GEOS 3.13.1) and pyproj 3.8.0
(PROJ 9.8.1), in the ignored `build/eriksberg-survey/venv`. A0.3 added the exact pins in
[`requirements.txt`](requirements.txt); see the [survey section](#route-and-coverage-survey-a03-2026-10-25).

```sh
osmium extract --bbox 11.5702598,57.6141283,11.9585149,57.7225589 \
  --strategy smart -S types=multipolygon --set-bounds \
  build/adventure/downloads/sweden-261024.osm.pbf \
  -o build/adventure/downloads/gothenburg-vinga-261024.osm.pbf
PY=build/eriksberg-survey/venv/bin/python
$PY -I tools/prepare_adventure_sources.py --self-test   # fixtures only, no inputs needed
$PY -I tools/prepare_adventure_sources.py               # writes sources/ and derived
$PY -I tools/prepare_adventure_sources.py --check       # regenerate in memory, compare hashes
```

The extraction takes about 5 s and normalization about 80 s. The `smart` strategy completes
multipolygon relations, so some member ways extend beyond the box. The self-test checks
missing relation members, malformed or out-of-range coordinates, feature selection, and a
synthetic two-tile land shapefile with a hole, a filtered distant polygon and a truncated record.

### Findings and gaps

- **Reproducibility.** Re-running the extraction produced a byte-identical corridor PBF.
  Two full normalization runs produced byte-identical outputs and manifest, and `--check` passed.
- **OSM.** 88,932 ways, 2,454 points and 1,854 relations were retained. Ten relations
  (150 members) are incomplete and are listed in `derived.osm`, not repaired.
  The `natural=bay` Kattegat multipolygon (29 of 42 members missing) and the
  Göteborg–Frederikshavn ferry routes cross into Denmark: their missing ways are absent
  from the Sweden download itself. The Göta älv, three stream relations and two Färja 281
  routes are not multipolygons, so `smart` did not complete them beyond the box. No
  coastline, water or building multipolygon inside the box is incomplete.
  Footways, paths, steps, cycleways, tracks, platforms and road nodes are omitted on purpose.
  Contributor names, changesets and timestamps are not retained.
- **Land polygons versus OSM coastline.** All 2,396 coastline ways lie on the clipped land
  boundary (maximum vertex deviation 0.000 m). The land snapshot is 3.7 h newer than the
  PBF, with no coastline difference here. Land polygons ignore inland `natural=water`
  areas such as the river and lakes, so A0.3 must combine both layers. That is a layer
  difference, not a conflict.
- **Terrain.** Not acquired. Lantmäteriet's 1 m Markhöjdmodell needs an authenticated
  Geotorget STAC account; none was available or created. Fallback: authored island relief
  informed by OSM coastline/rock/peak features until a terrain sample can be fetched.
- **Weather.** Vinga A (71380, 10 m mast) recorded a 29.7 m/s gust at 2026-10-25T07:00Z,
  a 23.0 m/s 10-minute mean in the same hour (23.5 m/s maximum 3-hour mean), from
  154–171° during 03–08Z. All retained values carry quality code G. These are station
  observations of the event the user reported on 2026-10-25 (27 m/s gusts), not a route
  reconstruction.
- **Waves.** No current SMHI wave station is near Vinga. Vinga Bottensystem (33040) has data
  only for 2019. Brofjorden (72 km north, Hs max 2.62 m, Tp max 7.66 s) and Väderöarna 3
  (103 km north, Hs max 3.06 m) are regional references starting 2026-10-24T10Z. Mean wave
  direction is unavailable. Some recent Väderöarna values carry an undocumented code O.
- **Bathymetry.** EMODnet was not acquired; it is optional for A0.2.
- **Size.** The OSM sample is 6.3 MB compressed, dominated by about 54,000 buildings. That
  is comparable to the compiled river geometry header. A0.3 may narrow the retained set once
  the route and first slice are chosen.

### A0.2 verification, 2026-10-25

macOS 26.6.2 arm64 (Apple M1 Max), from `0f45971` plus these uncommitted changes. Debug
configure and build passed. `ctest --test-dir build -j 4 --output-on-failure` ran 110 active
tests (15 retired tests disabled): all 89 non-GPU tests passed. All 21 `gpu`-labelled tests
failed at window creation (`GLFW: Failed to determine Monitor`, `Failed to initialize
platform`), both in the sandbox and in a repeated `ctest -L gpu` run with graphics access.
No display was available to this session. A0.2 changed no C code, assets or GPU paths;
those tests remain unverified for this handoff. Logs: `build/adventure/a0.2-ctest.log` and
`build/adventure/a0.2-ctest-gpu.log`. Source fixtures, extraction reproduction, double
normalization and `--check` passed as described above. Local links and `git diff --check`
passed. This documentation/tooling change needed no audiovisual review.

## Route and coverage survey (A0.3, 2026-10-25)

[`survey.py`](survey.py) reads only the checked-in A0.2 sources, verifying their content
hashes against [`sources.json`](sources.json), plus the compiled river manifest. It writes:

| File | Contents |
| --- | --- |
| [`coverage.png`](coverage.png), [`coverage.svg`](coverage.svg) | Overview of source coverage, river crop, first slice, routes, landmarks and the 512 m grid; first-slice detail with tile IDs and classes. Hatched areas are outside the source box. Red dashes mark source boundaries, which are not shoreline. |
| [`summary.json`](summary.json) | Inputs and hashes, CRS/origin, topology counts, route measurements, first-slice figures, tile/cost tables for 512 m and 1 km, river calibration, tile-size choice and gaps. |
| [`route.json`](route.json) | Main route, shelter detour, first-slice route and shorter start, in projected east/north metres. |
| [`first-slice.json`](first-slice.json) | First-slice polygon (east/north and lon/lat), its tile lists and per-tile records at both sizes. |

All coordinates are metres east/north of the crane origin in EPSG:3007, the river crop's frame.
Simulation X points west (X = −east). Tile IDs are `floor(east / size), floor(north / size)`,
so the tile just west or south of the origin is −1.

```sh
PY=build/adventure/survey-venv/bin/python
"$(readlink -f build/eriksberg-survey/venv/bin/python)" -m venv build/adventure/survey-venv
$PY -m pip install -r docs/adventure/requirements.txt   # Python 3.14.2; network, once
$PY -I docs/adventure/survey.py --self-test   # topology fixtures only
$PY -I docs/adventure/survey.py               # about 25 s; rewrites the files above
```

The fixtures cover two adjacent coastal tiles with an island across their seam, where tile
edges must not count as shoreline. They also cover a lake with an island hole, negative tile
IDs, an incomplete coastline that stays open, and a bridge opening between solid legs. A
lon/lat assembly fixture checks that source-box edges are classified as source boundaries.

### Method

- **Topology.** The processed land polygons treat Göta älv and lakes as land, so water is
  the source box minus land, plus OSM `natural=water`/riverbank multipolygons and closed
  ways. Mapped `bridge:support` areas are subtracted from water as solid. Assembly happens in
  lon/lat before any clipping, so box edges are exact source boundaries. The result is then
  projected vertex by vertex. No water multipolygon was unresolved. 15 bridge supports stand
  in water. The connected sea and river component is 155.2 km² and reaches Vinga. The 583 km
  of mapped shoreline are kept separate from 36 km of source boundary.
- **Routes.** Waypoints were found once by a coarse offline shortest path with generous
  clearance, then rounded, hand-checked against mapped rocks and fixed in the script. Every
  run validates each leg against the water eroded by **8 m**, a ≥250 m distance from the
  source boundary, and an Älvsborgsbron crossing. Mapped rocks, reefs, wrecks and obstructions
  within 50 m are counted. This is a 2D survey envelope, not a swept-hull check, depth survey
  or navigation route.
- **Costs.** The tile estimates use an explicit model: extruded building footprints, a quay
  wall and fills per shoreline edge, 64 B metadata per tile and 24 B per landmark. The coarse
  level uses box buildings and an 8 m simplified shoreline. Applied to the river crop, the model
  gives 0.40× the compiled arrays (0.81 MB against 2.01 MB). The bake also has authored
  quays, ground fill, building parts, landscape and the bridge. A scaled figure is therefore
  reported beside the raw estimate. Neither number is a bake size.

### Findings

| Route | Length | Min. shore clearance | Minutes at 10 / 15 / 20 m/s | Mapped hazards ≤50 m |
| --- | --- | --- | --- | --- |
| Dock → Älvsborgsbron → Rivöfjorden → Vinga gästhamn | 19.63 km | 27.6 m | 32.7 / 21.8 / 16.4 | 1 submerged rock at the Vinga approach |
| First slice: dock → Långedrag old harbour | 6.02 km | 27.6 m | 10.0 / 6.7 / 5.0 | 0 |
| Shelter detour from the fjord into Långedrag old harbour | 2.09 km | 36.8 m | 3.5 / 2.3 / 1.7 | 0 |
| Optional shorter start: Saltholmen → main route | 4.59 km (14.95 km to Vinga) | 15.4 m | 24.9 / 16.6 / 12.5 to Vinga | 5 awash rocks |

The travel times are distance divided by constant speed. They are not play-time estimates. The
adventure plan's 5–10 minute target covers exploration and arrival, so it is not derived from
these figures.

**First slice.** [`first-slice.json`](first-slice.json) is a 10.9 km² polygon. It overlaps
the river crop's west/south edges by 0.26 km², reaches at most 5.4 km from the bridge and
holds 6.8 km² of connected water and 26.2 km of shoreline. It adds Älvsborgsfjorden past
Nya Älvsborg (landmark), the Arendal port shore as view context and the Långedrag shore. The
shelter is Långedrag's old harbour basin. Open water toward Rivöfjorden at its west edge gives
the exposed-water contrast. It needs no source edge, invented closure or terrain.
Other candidate stops are marked on the map: Nya Varvet (inside the current crop),
Saltholmen, Hönö Klåva and Vinga gästhamn. Saltholmen, the southern archipelago ferry
terminal, is the optional shorter start. It saves about 4.7 km.

**Tiles.** Totals over the tiles touching each region. Estimated bytes are model values.

| Region | Size | Tiles | Sea / shore / urban / land | Water / land km² | Shore edges | Buildings | Metadata B | Detail / coarse MB | Largest tile KiB |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| River crop | 512 m | 30 | 0 / 5 / 23 / 2 | 1.4 / 6.5 | 760 | 2,671 | 1,968 | 1.17 / 0.67 | 118 |
| River crop | 1 km | 9 | 0 / 0 / 9 / 0 | 1.8 / 7.7 | 1,019 | 3,065 | 624 | 1.44 / 0.76 | 326 |
| First slice | 512 m | 58 | 9 / 26 / 20 / 3 | 8.7 / 6.5 | 5,184 | 3,033 | 3,808 | 1.59 / 0.81 | 123 |
| First slice | 1 km | 18 | 0 / 8 / 10 / 0 | 10.5 / 8.4 | 5,761 | 4,685 | 1,248 | 2.12 / 1.22 | 429 |
| Crop + slice (resident) | 512 m | 84 | 9 / 30 / 41 / 4 | 9.7 / 12.3 | 5,779 | 5,471 | 5,496 | 2.65 / 1.42 | 123 |
| Crop + slice (resident) | 1 km | 25 | 0 / 8 / 17 / 0 | 11.5 / 14.7 | 6,356 | 7,190 | 1,720 | 3.24 / 1.84 | 429 |
| Route ±1 km | 512 m | 219 | 87 / 94 / 34 / 4 | 45.1 / 12.3 | 14,815 | 4,801 | 14,160 | 3.13 / 1.37 | 131 |
| Route ±1 km | 1 km | 73 | 10 / 44 / 19 / 0 | 57.7 / 18.8 | 19,994 | 7,110 | 4,816 | 4.43 / 1.99 | 429 |
| Source box | 512 m | 1,139 | 296 / 413 / 377 / 53 | 158.2 / 121.6 | 100,264 | 54,213 | 73,112 | 27.62 / 14.61 | 215 |
| Source box | 1 km | 294 | 43 / 112 / 137 / 2 | 158.2 / 121.6 | 99,547 | 54,213 | 19,032 | 27.49 / 14.51 | 511 |

An urban tile has at least 25 building-outline centroids; a sea tile has no shoreline or land.
Scaled to the river bake, the resident crop and slice is about 6.9 MB at 512 m and 8.4 MB at 1 km.

**Initial experiment: 512 m tiles.** Metadata is a few kilobytes at either size. 512 m bounds
the largest first-slice tile at about 123 KiB of modelled detail, instead of 429 KiB. 40% of
the route corridor's 512 m tiles are geometry-free open sea, against 14% at 1 km. 1 km stays
the comparison once A0.4/A2 measure real submission and draw costs. This is a survey
parameter, not a runtime or bake format.

### Gaps

- **Terrain:** not acquired (authenticated Geotorget account needed); island relief stays
  authored. The map footer and `summary.json` repeat this.
- **Depth:** no bathymetry. The rock counts above come from OSM seamark/natural tags only.
  Saltholmen's exit passes five awash rocks within 50 m.
- **Overhead:** only Älvsborgsbron crosses a route. Its `maxheight:marine=45` tag is unverified.
- **Outside the source box:** unknown, hatched on the map. The routes stay ≥1.9 km inside.
- **Source size:** the 6.3 MB OSM corridor sample was not narrowed. The survey needs the whole
  box for coverage; trimming waits for A2's bake inputs.
- **Snapshot:** the survey uses the 2026-10-24 OSM data. The compiled river still uses its
  2026-10-17 snapshot and is unchanged.

### A0.3 verification, 2026-10-25

macOS 26.6.2 arm64, Python 3.14.2, starting from a clean `66ccfc6`. The self-test passed.
Two full runs produced byte-identical JSON, SVG and PNG. One was normal; the other ran under `sandbox-exec` with all
network access denied. That sandbox profile made `curl` fail to resolve a host. A fresh
virtual environment installed from `requirements.txt` reproduced the same five hashes
offline. `prepare_adventure_sources.py --self-test` passed there too. The coverage PNG was
inspected: water, land, source edges, hatched unknown area, routes, slice, landmarks and
tile labels are legible. This is agent review, not user acceptance. Debug configure/build
passed. The full CTest run passed all 110 active tests in 142.37 s, including the 24
GPU-labelled tests that A0.2 could not run (15 retired tests disabled). Logs:
`build/adventure/a0.3-build.log` and `build/adventure/a0.3-ctest.log`. No C code, assets or
runtime paths changed, so no gameplay or audio check was needed.

| Output | SHA-256 |
| --- | --- |
| `summary.json` | `fb79589e6d60a5a8402fe9fa87105cf77461efec196e76b1e3ba72cf5f1e97ea` |
| `route.json` | `4d2ef75b72c613c2d304ef5053ed66d1a7e974a5496d9a142d38ccf9d04360b7` |
| `first-slice.json` | `00382af3d61416ec53065786ae6a4fe780de5e2844fba21bd607ba1fb4ee7ffb` |
| `coverage.svg` | `cb6f69f5191abb97d0c3056910cef9bab5fcf4cdcb5fb9f6262a410ffccf12d9` |
| `coverage.png` | `89fc967ea8afa0953f68a0296eb0722c3075cc819f93a852e310cfa0da8a9e5c` |
