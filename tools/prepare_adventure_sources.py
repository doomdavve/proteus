#!/usr/bin/env python3
"""Normalize already-downloaded adventure sources offline.

Acquisition (the Geofabrik country PBF, `osmium extract`, processed land polygons and SMHI
observations) is an explicit separate step documented in docs/adventure/README.md. This
script reads only local files, never fetches anything, and runtime builds never invoke it.

Outputs under docs/adventure/sources/ are deterministic for identical inputs and library
versions. The `derived` section of docs/adventure/sources.json records their content
hashes and counts; the hand-maintained `acquisition` section is left untouched.
"""

import argparse
import csv
import datetime
import gzip
import hashlib
import io
import json
import math
import struct
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOWNLOADS = ROOT / "build/adventure/downloads"
SOURCES = ROOT / "docs/adventure/sources"
MANIFEST = ROOT / "docs/adventure/sources.json"
CORRIDOR_PBF = DOWNLOADS / "gothenburg-vinga-261024.osm.pbf"
LAND_ZIP = DOWNLOADS / "land-polygons-split-4326.zip"
LAND_SHP = "land-polygons-split-4326/land_polygons.shp"
OCOBS_DIR = DOWNLOADS / "smhi-ocobs"

# The `osmium extract --bbox` envelope, west/south/east/north. Land polygons are clipped to
# the same box so A0.3 can distinguish source boundaries from real shoreline.
ENVELOPE = (11.5702598, 57.6141283, 11.9900000, 57.7225589)
VINGA_A = (11.6048, 57.6322)
ORIGIN = (11.9089495, 57.6973921)
WEATHER_FROM = "2026-10-24T00:00:00Z"

MAJOR_HIGHWAYS = {
    "motorway", "motorway_link", "trunk", "trunk_link", "primary", "primary_link",
    "secondary", "secondary_link", "tertiary", "tertiary_link", "unclassified",
    "residential", "living_street", "pedestrian", "service", "busway", "road",
}
# Since A3.5f: tram and rail lines, for the upstream street life.
RAILWAYS = {"tram", "rail", "light_rail"}
LANDMARK_MAN_MADE = {"lighthouse", "beacon", "pier", "breakwater", "bridge", "groyne",
                     "quay", "mast", "tower", "crane", "chimney", "water_tower"}
LANDMARK_NATURAL = {"coastline", "water", "rock", "bare_rock", "stone", "beach", "wetland",
                    "peak", "cape", "bay", "strait", "reef", "shoal", "cliff", "scrub",
                    "heath", "wood"}


def point(lon, lat):
    lon, lat = float(lon), float(lat)
    if not (math.isfinite(lon) and math.isfinite(lat) and -180 <= lon <= 180
            and -90 <= lat <= 90):
        raise ValueError(f"malformed longitude/latitude: {lon}, {lat}")
    return [round(lon, 7), round(lat, 7)]


def keep_feature(tags):
    """Coast/water, buildings, bridges and landmarks; footpaths and street furniture omitted."""
    if any(k.startswith("seamark:") for k in tags):
        return True
    if (tags.get("natural") in LANDMARK_NATURAL or "waterway" in tags or "building" in tags
            or "building:part" in tags or "bridge" in tags or "bridge:support" in tags
            or tags.get("man_made") in LANDMARK_MAN_MADE or "place" in tags
            or "historic" in tags or tags.get("route") == "ferry"
            or tags.get("landuse") in {"harbour", "port", "industrial"}
            or tags.get("leisure") == "marina" or tags.get("amenity") == "ferry_terminal"
            or tags.get("tourism") in {"attraction", "viewpoint", "museum"}):
        return True
    if tags.get("railway") in RAILWAYS:
        return True
    return tags.get("highway") in MAJOR_HIGHWAYS


def keep_node(tags):
    # Road nodes (crossings, stops, signals) are geometry detail, not landmarks.
    return bool(tags) and "highway" not in tags and keep_feature(tags)


def validate(data):
    """Reject malformed geometry; return sorted missing relation members."""
    ways = {w["id"] for w in data["ways"]}
    nodes = {n["id"] for n in data["points"]}
    relations = {r["id"] for r in data["relations"]}
    for w in data["ways"]:
        if len(w["geometry"]) < 2:
            raise ValueError(f"way {w['id']} has fewer than two nodes")
        for coord in w["geometry"]:
            point(*coord)
    for n in data["points"]:
        point(*n["coord"])
    missing = []
    for r in data["relations"]:
        for m in r["members"]:
            known = {"w": ways, "n": nodes, "r": relations}.get(m["type"], set())
            if m["ref"] not in known:
                missing.append(f"{r['id']}:{m['type']}/{m['ref']}")
    return sorted(missing)


def relation_record(obj):
    members = [{"type": m.type, "ref": m.ref, "role": m.role} for m in obj.members]
    return {"id": obj.id, "version": obj.version, "tags": dict(obj.tags), "members": members}


def prepare_osm(source):
    import osmium

    relations = {}
    for obj in osmium.FileProcessor(str(source), osmium.osm.RELATION):
        tags = dict(obj.tags)
        if keep_feature(tags):
            relations[obj.id] = relation_record(obj)
    # Keep nested relations referenced by selected ones, then collect every member.
    nested = {m["ref"] for r in relations.values() for m in r["members"] if m["type"] == "r"}
    for obj in osmium.FileProcessor(str(source), osmium.osm.RELATION):
        if obj.id in nested and obj.id not in relations:
            relations[obj.id] = relation_record(obj)
    member_ways = {m["ref"] for r in relations.values() for m in r["members"] if m["type"] == "w"}
    member_nodes = {m["ref"] for r in relations.values() for m in r["members"] if m["type"] == "n"}

    ways, points = [], []
    processor = osmium.FileProcessor(str(source), osmium.osm.NODE | osmium.osm.WAY)
    for obj in processor.with_locations("flex_mem"):
        tags = dict(obj.tags)
        if obj.is_node():
            if obj.id in member_nodes or keep_node(tags):
                points.append({"id": obj.id, "version": obj.version, "tags": tags,
                               "coord": point(obj.location.lon, obj.location.lat)})
        elif obj.id in member_ways or keep_feature(tags):
            if not all(n.location.valid() for n in obj.nodes):
                raise ValueError(f"way {obj.id} has a missing node location")
            ways.append({"id": obj.id, "version": obj.version, "tags": tags,
                         "geometry": [point(n.lon, n.lat) for n in obj.nodes]})
    data = {
        "schema": 1,
        "generator": "tools/prepare_adventure_sources.py",
        "source": CORRIDOR_PBF.name,
        "crs": "EPSG:4326",
        "coordinate_order": "lon,lat",
        "envelope_wsen": list(ENVELOPE),
        "attribution": "© OpenStreetMap contributors, ODbL 1.0",
        "attribution_url": "https://www.openstreetmap.org/copyright",
        "ways": sorted(ways, key=lambda w: w["id"]),
        "points": sorted(points, key=lambda n: n["id"]),
        "relations": sorted(relations.values(), key=lambda r: r["id"]),
    }
    data["incomplete_members"] = validate(data)
    return data


def osm_summary(data):
    def count(pred):
        return sum(1 for w in data["ways"] if pred(w["tags"]))

    outside = []
    west, south, east, north = ENVELOPE
    for w in data["ways"]:
        lons = [c[0] for c in w["geometry"]]
        lats = [c[1] for c in w["geometry"]]
        if min(lons) < west or max(lons) > east or min(lats) < south or max(lats) > north:
            outside.append(w["id"])
    return {
        "ways": len(data["ways"]),
        "points": len(data["points"]),
        "relations": len(data["relations"]),
        "coastline_ways": count(lambda t: t.get("natural") == "coastline"),
        "water_ways": count(lambda t: t.get("natural") == "water" or "waterway" in t),
        "building_ways": count(lambda t: "building" in t or "building:part" in t),
        "bridge_ways": count(lambda t: "bridge" in t or t.get("man_made") == "bridge"),
        "road_ways": count(lambda t: t.get("highway") in MAJOR_HIGHWAYS),
        "seamark_points": sum(1 for n in data["points"]
                              if any(k.startswith("seamark:") for k in n["tags"])),
        "incomplete_members": len(data["incomplete_members"]),
        "incomplete_relations": sorted({int(m.split(":")[0])
                                        for m in data["incomplete_members"]}),
        "ways_extending_beyond_envelope": len(outside),
    }


# --- Processed land polygons -------------------------------------------------------------

def shapefile_polygons(stream, bbox):
    """Yield polygon rings (lists of (x, y)) from a type-5 shapefile whose box hits bbox."""
    header = stream.read(100)
    if len(header) != 100 or struct.unpack(">i", header[:4])[0] != 9994:
        raise ValueError("not a shapefile")
    if struct.unpack("<i", header[32:36])[0] != 5:
        raise ValueError("expected polygon shapefile")
    west, south, east, north = bbox
    while True:
        record = stream.read(8)
        if not record:
            return
        if len(record) != 8:
            raise ValueError("truncated shapefile record header")
        length = struct.unpack(">i", record[4:])[0] * 2
        content = stream.read(length)
        if len(content) != length:
            raise ValueError("truncated shapefile record")
        if struct.unpack("<i", content[:4])[0] == 0:
            continue
        xmin, ymin, xmax, ymax = struct.unpack("<4d", content[4:36])
        if xmax < west or xmin > east or ymax < south or ymin > north:
            continue
        parts, count = struct.unpack("<2i", content[36:44])
        starts = list(struct.unpack(f"<{parts}i", content[44:44 + 4 * parts])) + [count]
        offset = 44 + 4 * parts
        coords = struct.unpack(f"<{2 * count}d", content[offset:offset + 16 * count])
        xy = list(zip(coords[0::2], coords[1::2]))
        for x, y in xy:
            point(x, y)
        yield [xy[starts[i]:starts[i + 1]] for i in range(parts)]


def rings_to_polygons(ring_sets):
    from shapely.geometry import LinearRing, Polygon

    polygons = []
    for rings in ring_sets:
        # Shapefile outer rings are clockwise and holes counter-clockwise.
        shell, holes = None, []
        for ring in rings:
            if not LinearRing(ring).is_ccw:
                if shell is not None:
                    polygons.append(Polygon(shell, holes))
                shell, holes = ring, []
            else:
                holes.append(ring)
        if shell is None:
            raise ValueError("shapefile polygon has holes but no outer ring")
        polygons.append(Polygon(shell, holes))
    return polygons


def clip_land(polygons):
    from shapely.geometry import box
    from shapely.geometry.polygon import orient
    from shapely.ops import unary_union

    # Union removes the processed dataset's artificial split seams before clipping.
    land = unary_union(polygons).intersection(box(*ENVELOPE))
    parts = getattr(land, "geoms", [land])
    out = []
    for part in parts:
        if part.geom_type != "Polygon" or part.is_empty:
            continue
        part = orient(part, sign=1.0)
        out.append([[point(x, y) for x, y in part.exterior.coords]]
                   + [[point(x, y) for x, y in hole.coords] for hole in part.interiors])
    return sorted(out, key=lambda rings: (min(c[0] for c in rings[0]),
                                          min(c[1] for c in rings[0]), len(rings[0])))


def prepare_land(path):
    with zipfile.ZipFile(path) as archive, archive.open(LAND_SHP) as stream:
        polygons = rings_to_polygons(shapefile_polygons(io.BufferedReader(stream), ENVELOPE))
    return {
        "schema": 1,
        "generator": "tools/prepare_adventure_sources.py",
        "source": f"{path.name}:{LAND_SHP}",
        "crs": "EPSG:4326",
        "coordinate_order": "lon,lat",
        "clip_box_wsen": list(ENVELOPE),
        "clip_box_note": "Edges on the clip box are source boundaries, not shoreline.",
        "attribution": ("© OpenStreetMap contributors, ODbL 1.0; "
                        "processed by osmdata.openstreetmap.de"),
        "polygons": clip_land(polygons),
    }


def coastline_conflicts(osm, land):
    """Compare OSM coastline vertices with the land boundary in EPSG:3007 metres."""
    from pyproj import Transformer
    from shapely.geometry import LineString, Point, Polygon, box
    from shapely.ops import transform, unary_union

    project = Transformer.from_crs(4326, 3007, always_xy=True).transform
    boundary = unary_union([transform(project, Polygon(r[0], r[1:])).boundary
                            for r in land["polygons"]])
    inner = transform(project, box(*ENVELOPE)).buffer(-25)
    deviations = []
    for w in osm["ways"]:
        if w["tags"].get("natural") != "coastline":
            continue
        line = transform(project, LineString(w["geometry"]))
        worst = 0.0
        for x, y in line.coords:
            p = Point(x, y)
            if inner.contains(p):
                worst = max(worst, boundary.distance(p))
        deviations.append((w["id"], worst))
    over = sorted([(round(d, 1), i) for i, d in deviations if d > 1.0], reverse=True)
    return {
        "method": "max distance of each OSM coastline vertex (25 m inside the clip box) "
                  "to the clipped land boundary, EPSG:3007 metres",
        "coastline_ways_compared": len(deviations),
        "ways_over_1m": len(over),
        "ways_over_10m": sum(1 for d, _ in over if d > 10),
        "max_deviation_m": round(max((d for _, d in deviations), default=0.0), 3),
        "largest": [{"way": i, "deviation_m": d} for d, i in over[:20]],
    }


# --- SMHI observations -------------------------------------------------------------------

METOBS = {
    3: ("wind_direction", "deg", "Vindriktning: medelvärde 10 min, 1 gång/tim"),
    4: ("wind_speed_10min_mean", "m/s", "Vindhastighet: medelvärde 10 min, 1 gång/tim"),
    21: ("gust_max_hourly", "m/s", "Byvind: max, 1 gång/tim"),
    25: ("max_10min_mean_3h", "m/s",
         "Max av MedelVindhastighet: maximum av medelvärde 10 min, under 3 timmar, 1 gång/tim"),
}
OCOBS = {
    1: ("significant_wave_height_30min", "m"),
    11: ("maximum_wave_height_30min", "m"),
    9: ("peak_wave_period_30min", "s"),
    8: ("wave_direction_at_peak_30min", "°"),
}
WAVE_STATIONS = (33033, 33015)


def utc(text):
    return datetime.datetime.strptime(text, "%Y-%m-%dT%H:%M:%SZ").replace(
        tzinfo=datetime.timezone.utc)


def metobs_series(path, since):
    rows = list(csv.reader(io.StringIO(path.read_text(encoding="utf-8-sig")), delimiter=";"))
    station = rows[1]
    start = next(i for i, r in enumerate(rows) if r[:2] == ["Datum", "Tid (UTC)"]) + 1
    values = []
    for r in rows[start:]:
        if len(r) < 4 or not r[0]:
            continue
        when = datetime.datetime.strptime(f"{r[0]} {r[1]}", "%Y-%m-%d %H:%M:%S").replace(
            tzinfo=datetime.timezone.utc)
        if when >= since:
            values.append([when.strftime("%Y-%m-%dT%H:%M:%SZ"), float(r[2]), r[3]])
    return station, values


def km(a, b):
    lon1, lat1, lon2, lat2 = map(math.radians, (*a, *b))
    h = (math.sin((lat2 - lat1) / 2) ** 2
         + math.cos(lat1) * math.cos(lat2) * math.sin((lon2 - lon1) / 2) ** 2)
    return round(2 * 6371.0088 * math.asin(math.sqrt(h)), 1)


def prepare_weather():
    since = utc(WEATHER_FROM)
    series, station = {}, None
    for key, (name, unit, description) in METOBS.items():
        station, values = metobs_series(DOWNLOADS / f"smhi-vinga-a-{key}-latest-months.csv", since)
        series[name] = {"smhi_parameter": key, "unit": unit, "description": description,
                        "values": values}
    gusts = series["gust_max_hourly"]["values"]
    peak = max(gusts, key=lambda v: v[1])
    waves = []
    for station_id in WAVE_STATIONS:
        record = {"station": station_id, "series": {}}
        for key, (name, unit) in OCOBS.items():
            path = OCOBS_DIR / f"wave-{station_id}-p{key}-latest-day.json"
            raw = json.loads(path.read_text())
            if raw["parameter"]["unit"] != unit:
                raise ValueError(f"{path.name}: unexpected unit {raw['parameter']['unit']}")
            position = raw["position"][-1]
            record.update(name=raw["station"]["name"], owner=raw["station"]["owner"],
                          lon=position["longitude"], lat=position["latitude"],
                          distance_from_vinga_a_km=km(VINGA_A, (position["longitude"],
                                                                position["latitude"])))
            values = [[datetime.datetime.fromtimestamp(v["date"] / 1000, datetime.timezone.utc)
                       .strftime("%Y-%m-%dT%H:%M:%SZ"), float(v["value"]), v["quality"]]
                      for v in raw["value"]]
            record["series"][name] = {"smhi_parameter": key, "unit": unit,
                                      "smhi_name": raw["parameter"]["name"], "values": values}
        waves.append(record)
    return {
        "schema": 1,
        "generator": "tools/prepare_adventure_sources.py",
        "attribution": "Source: SMHI open data (CC BY 4.0); values selected and reformatted",
        "attribution_url": "https://www.smhi.se/data/om-smhis-data/villkor-for-anvandning",
        "time_reference": "UTC; each value is [time, value, SMHI quality code]",
        "event_note": ("Observations retained for the storm the user reported on 2026-10-25 "
                       "(27 m/s gusts). They are station observations, not a reconstruction "
                       "of conditions along the route."),
        "quality_codes": {
            "G": "SMHI: controlled and approved values",
            "Y": "SMHI: suspect or aggregated values; roughly checked or uncontrolled real-time",
            "O": "Present in oceanographic latest-day data; not defined in the response",
        },
        "vinga_a": {
            "station": 71380,
            "name": station[0],
            "network": station[2],
            "measurement_height_m_above_ground": float(station[3]),
            "lon": VINGA_A[0],
            "lat": VINGA_A[1],
            "station_height_m_above_sea": 18.205,
            "from_utc": WEATHER_FROM,
            "peak_gust": {"time": peak[0], "value": peak[1], "unit": "m/s"},
            "series": series,
        },
        "wave_stations": waves,
        "wave_gaps": [
            "No SMHI wave station reports from Vinga for this event. Vinga Bottensystem "
            "(33040) has significant-wave-height data only for 2019-05..2019-11.",
            "Wave buoys expose only latest-day and corrected-archive periods; the retained "
            "slice starts 2026-10-24T10:00Z, after the storm had begun.",
            "Mean wave direction (parameter 7) is unavailable for both buoys.",
            "Brofjorden and Väderöarna are 72 km and 103 km north of Vinga A, in other "
            "exposure; they are regional references, not Vinga wave measurements.",
        ],
    }


# --- Output ------------------------------------------------------------------------------

def encode(data):
    return (json.dumps(data, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
            + "\n").encode()


def write_gzip(path, raw):
    buffer = io.BytesIO()
    with gzip.GzipFile(fileobj=buffer, mode="wb", filename="", mtime=0, compresslevel=9) as out:
        out.write(raw)
    path.write_bytes(buffer.getvalue())
    return hashlib.sha256(buffer.getvalue()).hexdigest()


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def versions():
    import osmium.version
    import pyproj
    import shapely

    return {"python": sys.version.split()[0], "pyosmium": osmium.version.pyosmium_release,
            "libosmium": osmium.version.libosmium_version,
            "shapely": shapely.__version__, "geos": shapely.geos_version_string,
            "pyproj": pyproj.__version__, "proj": pyproj.proj_version_str}


def self_test():
    fixture = {"ways": [{"id": 5, "geometry": [[11.1, 57.1], [11.2, 57.2]]}],
               "points": [], "relations": [{"id": 7, "members": [
                   {"type": "w", "ref": 5, "role": "outer"}]}]}
    assert not validate(fixture)
    fixture["relations"][0]["members"].append({"type": "w", "ref": 6, "role": "inner"})
    assert validate(fixture) == ["7:w/6"], "missing relation member not reported"
    for bad in ([181, 57.0], [11.0, float("nan")], [11.0, 91]):
        fixture["ways"][0]["geometry"][0] = bad
        try:
            validate(fixture)
        except ValueError:
            continue
        raise AssertionError(f"malformed coordinate accepted: {bad}")
    assert keep_feature({"natural": "coastline"}) and not keep_feature({"highway": "footway"})
    assert keep_feature({"highway": "footway", "bridge": "yes"})
    assert not keep_node({"highway": "crossing"}) and keep_node({"seamark:type": "rock"})

    # A two-tile land square split down the middle merges without a seam, then clips.
    def record(number, rings):
        pts = [p for ring in rings for p in ring]
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        starts, offset = [], 0
        for ring in rings:
            starts.append(offset)
            offset += len(ring)
        body = struct.pack("<i4d2i", 5, min(xs), min(ys), max(xs), max(ys), len(rings), len(pts))
        body += struct.pack(f"<{len(starts)}i", *starts)
        body += b"".join(struct.pack("<2d", *p) for p in pts)
        return struct.pack(">2i", number, len(body) // 2) + body

    left = [[(11.6, 57.62), (11.6, 57.66), (11.7, 57.66), (11.7, 57.62), (11.6, 57.62)]]
    right = [[(11.7, 57.62), (11.7, 57.66), (11.8, 57.66), (11.8, 57.62), (11.7, 57.62)],
             [(11.72, 57.63), (11.74, 57.63), (11.74, 57.64), (11.72, 57.64), (11.72, 57.63)]]
    far = [[(20.0, 60.0), (20.0, 61.0), (21.0, 61.0), (20.0, 60.0)]]
    shp = struct.pack(">7i", 9994, 0, 0, 0, 0, 0, 0) + struct.pack("<2i", 1000, 5)
    shp += bytes(64) + record(1, left) + record(2, right) + record(3, far)
    rings = list(shapefile_polygons(io.BytesIO(shp), ENVELOPE))
    assert len(rings) == 2, "far polygon should be filtered by its box"
    clipped = clip_land(rings_to_polygons(rings))
    assert len(clipped) == 1 and len(clipped[0]) == 2, "seam not removed or hole lost"
    try:
        list(shapefile_polygons(io.BytesIO(shp[:-8]), ENVELOPE))
    except ValueError:
        pass
    else:
        raise AssertionError("truncated shapefile accepted")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--self-test", action="store_true", help="run fixtures only")
    parser.add_argument("--check", action="store_true",
                        help="regenerate in memory and compare with the manifest's hashes")
    args = parser.parse_args()
    self_test()
    if args.self_test:
        print("source fixtures OK")
        return

    osm = prepare_osm(CORRIDOR_PBF)
    land = prepare_land(LAND_ZIP)
    weather = prepare_weather()
    outputs = {"osm-corridor.json.gz": encode(osm), "land-polygons.json.gz": encode(land),
               "weather-2026-10-25.json": json.dumps(weather, ensure_ascii=False, indent=1,
                                                      sort_keys=True).encode() + b"\n"}
    derived = {
        "tool_versions": versions(),
        "inputs": {p.name: file_sha256(p) for p in (CORRIDOR_PBF, LAND_ZIP)},
        "outputs": {name: {"content_sha256": hashlib.sha256(raw).hexdigest(),
                           "content_bytes": len(raw)} for name, raw in outputs.items()},
        "osm": osm_summary(osm),
        "land": {"polygons": len(land["polygons"]),
                 "holes": sum(len(p) - 1 for p in land["polygons"]),
                 "vertices": sum(len(r) for p in land["polygons"] for r in p)},
        "coastline_conflicts": coastline_conflicts(osm, land),
    }
    manifest = json.loads(MANIFEST.read_text())
    if args.check:
        expected = manifest.get("derived", {}).get("outputs", {})
        bad = [n for n, o in derived["outputs"].items()
               if expected.get(n, {}).get("content_sha256") != o["content_sha256"]]
        if bad:
            sys.exit(f"content hashes differ from manifest: {', '.join(bad)}")
        print("content hashes match manifest")
        return

    SOURCES.mkdir(parents=True, exist_ok=True)
    for name, raw in outputs.items():
        path = SOURCES / name
        if name.endswith(".gz"):
            derived["outputs"][name]["gzip_sha256"] = write_gzip(path, raw)
        else:
            path.write_bytes(raw)
    manifest["derived"] = derived
    MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(derived["outputs"], indent=2))


if __name__ == "__main__":
    main()
