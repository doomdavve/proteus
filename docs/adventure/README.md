# Adventure survey and reference evidence

This directory holds the small, versioned evidence for the staged Gothenburg–Vinga work.
The [A0/A1 task ledger](../FREE_RIDING_ADVENTURE_FIRST_STEPS.md) owns execution status;
the [adventure plan](../FREE_RIDING_ADVENTURE_PLAN.md) owns scope. The original river
reference is summarized in [baseline.md](baseline.md). It precedes any new coastal data
or runtime map change.

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
(PROJ 9.8.1), in the ignored `build/eriksberg-survey/venv`. Exact pins for a fresh
environment belong to A0.3's requirements file.

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
