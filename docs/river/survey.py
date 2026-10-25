"""Recreate the bridge expansion survey offline.

Requires pyproj 3.8.0, shapely 2.1.2, matplotlib 3.10.8.
Run from any directory: python docs/river/survey.py
This produces planning artifacts; it does not modify runtime geometry.
"""
import gzip
import hashlib
import importlib.util
import json
import math
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon as PatchPolygon, Rectangle
from pyproj import Transformer
from shapely import make_valid
from shapely.geometry import LineString, Polygon, box

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[1]
SOURCE_HASH = "688c7cf277af904c44ce820c941b83cd23f383b9d5a492040edcf46cfcb6778a"
PROPOSED = (-1500, -1600, 950, 800)
# The original Eriksberg crop, retired 2026-10-25; drawn as the survey's "current" crop.
FORMER = (-700, -600, 950, 800)


def main():
    raw = gzip.decompress((BASE / "osm-survey.json.gz").read_bytes())
    assert hashlib.sha256(raw).hexdigest() == SOURCE_HASH, "Unexpected survey snapshot"
    data = json.loads(raw)
    elements = data["elements"]
    by_id = {(e["type"], e["id"]): e for e in elements}
    # Use the runtime importer's chain joining.
    spec = importlib.util.spec_from_file_location("prepare", ROOT / "tools/prepare_waterfront.py")
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)
    current = box(*FORMER)
    crop = box(*PROPOSED)
    transform = Transformer.from_crs(4326, 3007, always_xy=True)
    inverse = Transformer.from_crs(3007, 4326, always_xy=True)
    ox, oy = transform.transform(11.9089495, 57.6973921)

    def coords(geometry):
        return [(x - ox, y - oy) for p in geometry
                for x, y in [transform.transform(p["lon"], p["lat"])]]

    river = by_id["relation", 3329487]
    chains = sorted(prepare.join_chains([
        coords(m["geometry"]) for m in river["members"] if m.get("geometry")
        and m["role"] == "outer"
    ]), key=len, reverse=True)
    a, b = chains[:2]
    if math.dist(a[-1], b[0]) > math.dist(a[-1], b[-1]):
        b = list(reversed(b))
    closing = [LineString([a[-1], b[0]]), LineString([b[-1], a[0]])]
    assert all(not edge.intersects(crop) for edge in closing), "Artificial bank closure in crop"
    # Any omitted bank fragments must also lie entirely outside the proposed crop.
    assert all(not LineString(c).intersects(crop) for c in chains[2:])
    # Closing clipped banks can create a sliver outside the crop. Repair there,
    # then require every real bank segment inside the crop to remain a boundary.
    water = make_valid(Polygon(a + b)).intersection(crop)
    assert water.geom_type == "Polygon" and not water.interiors, "Unexpected water topology"
    for bank in [a, b]:
        assert water.boundary.buffer(1e-6).covers(LineString(bank).intersection(crop)), \
            "River repair changed a mapped bank inside the crop"

    bridge = by_id["relation", 5776295]
    outline = Polygon(coords(by_id["way", 387711139]["geometry"]))
    assert outline.is_valid and crop.covers(outline)
    # Simulation X points west; convert the existing (-12, 60) spawn to east/north.
    route = LineString([(12, 60), (24, -70), (15, -180), (-250, -500),
                        (-650, -850), (-1100, -950)])
    assert water.buffer(-8).covers(route), "Survey route leaves water clearance envelope"
    crossing = route.intersection(outline)
    assert not crossing.is_empty, "Route misses bridge"

    buildings = []
    for e in elements:
        if e["type"] != "way" or "building" not in e.get("tags", {}):
            continue
        points = coords(e.get("geometry", []))
        if len(points) < 4 or points[0] != points[-1]:
            continue
        shape = Polygon(points)
        if shape.is_valid and shape.intersects(crop):
            buildings.append(shape)

    fig, ax = plt.subplots(figsize=(11, 10))
    fig.patch.set_facecolor("#f5f2eb")
    ax.set_facecolor("#e5e1d7")
    ax.add_patch(PatchPolygon(list(water.exterior.coords), facecolor="#b8dce8",
                             edgecolor="#497b8e", linewidth=1))
    for building in buildings:
        ax.add_patch(PatchPolygon(list(building.exterior.coords), facecolor="#aeb0ad",
                                 edgecolor="none", alpha=.65))
    ax.add_patch(PatchPolygon(list(outline.exterior.coords), facecolor="#625488",
                             edgecolor="#4c3b75", label="Mapped bridge deck"))
    for e in elements:
        if e["type"] == "way" and e.get("tags", {}).get("bridge:support") == "pylon":
            pts = coords(e["geometry"])
            ax.add_patch(PatchPolygon(pts, facecolor="#4c3b75", edgecolor="#4c3b75"))
    for bounds, color, label, style in [
        (current.bounds, "#c97a22", "Current runtime crop", "--"),
        (PROPOSED, "#24775a", "Proposed crop", "-"),
    ]:
        x, y, xx, yy = bounds
        ax.add_patch(Rectangle((x, y), xx-x, yy-y, fill=False, edgecolor=color,
                               linewidth=2, linestyle=style, label=label))
    x, y = route.xy
    ax.plot(x, y, color="#b34835", linewidth=2.5, label="Proposed boat journey")
    ax.scatter([0], [0], marker="*", s=140, color="#b34835", zorder=5)
    for label, point, offset in [
        ("Eriksberg crane", (0, 0), (90, 50)),
        ("Dock spawn", (12, 60), (140, 240)),
        ("Älvsborgsbron", (outline.centroid.x, outline.centroid.y), (240, -300)),
        ("Turn back / look toward crane", (-1100, -950), (-270, -330)),
    ]:
        ax.annotate(label, point, xytext=(point[0]+offset[0], point[1]+offset[1]),
                    fontsize=10, arrowprops={"arrowstyle": "-", "color": "#404b50"})
    ax.set(xlim=(-1600, 1050), ylim=(-1700, 900), aspect="equal",
           xlabel="Metres east of crane (EPSG:3007)", ylabel="Metres north of crane")
    ax.grid(alpha=.15)
    ax.legend(loc="lower right", fontsize=9)
    ax.set_title("Eriksberg → Älvsborgsbron\nProposed navigable river extension", loc="left", pad=16)
    fig.text(.1, .025, "Map data © OpenStreetMap contributors · ODbL 1.0 · Planning only\n"
             "Water route checked in 2D; heights, overhead clearance and camera views remain unverified.",
             fontsize=9, color="#48555b")
    fig.subplots_adjust(bottom=.13, top=.9)
    matplotlib.rcParams["svg.hashsalt"] = "proteus-bridge-survey"
    fig.savefig(BASE / "coverage.png", dpi=150)
    fig.savefig(BASE / "coverage.svg", metadata={"Date": None})
    corners = [inverse.transform(ox+x, oy+y) for x in [PROPOSED[0], PROPOSED[2]]
               for y in [PROPOSED[1], PROPOSED[3]]]
    summary = {
        "source": "osm-survey.json.gz", "source_sha256": SOURCE_HASH,
        "retrieved_utc": data["retrieved_utc"], "origin_epsg3007": [ox, oy],
        "current_crop_wsen": list(current.bounds), "proposed_crop_wsen": list(PROPOSED),
        "geographic_bounds_wsen": [min(p[0] for p in corners), min(p[1] for p in corners),
                                   max(p[0] for p in corners), max(p[1] for p in corners)],
        "bridge_relation": bridge["id"], "bridge_tags_unverified": bridge["tags"],
        "bridge_outline_bounds_wsen": list(outline.bounds),
        "route_east_north": list(route.coords), "route_length_m": round(route.length, 1),
        "route_shore_clearance_m": round(route.distance(water.boundary), 1),
        "route_bridge_crossing_east_north": list(crossing.centroid.coords)[0],
        "water_area_m2": round(water.area), "building_way_outlines": len(buildings),
        "artificial_closures_outside_crop": True,
        "omitted_bank_fragments_outside_crop": True,
        "limitations": ["Building ways are contextual outlines, not deduplicated building counts",
                        "Route checks only mapped water and crop containment in 2D",
                        "OSM elevation, height and clearance tags are not surveyed dimensions"],
    }
    (BASE / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n")
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
