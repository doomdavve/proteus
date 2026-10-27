"""Land heights for the outer-river bake: a height model from Göteborg's Höjdmodell 2022 grid,
and a land surface that is 20 m inland and 5 m near the shore.

Used by tools/prepare_outer_map.py. Heights are RH 2000 metres, taken as metres above the water
at y = 0; the scanned water surface itself sits near -0.26 m (see the manifest).
"""
import gzip
import json
import math
import sys
from pathlib import Path

import numpy as np
from shapely import prepare
from shapely.geometry import Point, Polygon, box
from shapely.geometry.polygon import orient
from shapely.ops import unary_union

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SOURCE = ROOT / "docs/adventure/sources/terrain-hojdmodell.bin.gz"
# Baskarta spot heights are no longer the model, only an independent check of it.
SPOT_SOURCE = ROOT / "docs/adventure/sources/terrain-baskarta.json.gz"
COARSE = 20.0  # inland cell, metres
FINE = 5.0  # near-shore cell, metres
BAND = 20.0  # cells this close to the shore are fine
TILE = 400.0  # clipping chunk, only for speed
# Land is never lower than this above the water, so waves do not wash over the surface.
MIN_LAND = 1.0
SIDE_EPS = 1e-6


def load_grid(path=SOURCE):
    """(header, heights) of the grid written by tools/prepare_terrain_dem.py."""
    from prepare_terrain_dem import decode

    return decode(Path(path).read_bytes())


def load_spots(path=SPOT_SOURCE):
    data = json.loads(gzip.decompress(Path(path).read_bytes()))
    return np.array(data["points"], dtype=float).reshape(-1, 3)


class GridModel:
    """Bilinear interpolation between cell centres, held at the edge outside the grid."""

    def __init__(self, header, heights):
        self.cell = header["cell_m"]
        self.west, self.north = header["west_m"], header["north_m"]
        self.heights = np.asarray(heights, dtype=float)

    def raw(self, xy):
        xy = np.asarray(xy, dtype=float).reshape(-1, 2)
        rows, columns = self.heights.shape
        u = np.clip((xy[:, 0] - self.west) / self.cell - .5, 0, columns - 1)
        v = np.clip((self.north - xy[:, 1]) / self.cell - .5, 0, rows - 1)
        c0 = np.minimum(np.floor(u).astype(int), columns - 2)
        r0 = np.minimum(np.floor(v).astype(int), rows - 2)
        fu, fv = u - c0, v - r0
        h = self.heights
        top = h[r0, c0] * (1 - fu) + h[r0, c0 + 1] * fu
        bottom = h[r0 + 1, c0] * (1 - fu) + h[r0 + 1, c0 + 1] * fu
        return top * (1 - fv) + bottom * fv

    def __call__(self, xy):
        return np.maximum(self.raw(xy), MIN_LAND)


def spot_check(model, spots, land):
    """Error of the grid model against the Baskarta spot heights that lie on land."""
    from shapely import contains_xy

    keep = contains_xy(land, spots[:, 0], spots[:, 1])
    err = model(spots[keep, :2]) - np.maximum(spots[keep, 2], MIN_LAND)
    return {"samples": int(keep.sum()), "rmse_m": round(float(np.sqrt((err ** 2).mean())), 3),
            "mean_abs_m": round(float(np.abs(err).mean()), 3),
            "median_m": round(float(np.median(err)), 3),
            "p95_abs_m": round(float(np.percentile(np.abs(err), 95)), 3),
            "max_abs_m": round(float(np.abs(err).max()), 3)}


def self_test():
    # A tilted plane sampled at cell centres is reproduced between them and held outside.
    header = {"cell_m": 5.0, "west_m": -10.0, "north_m": 10.0}
    centres_x = -10 + 5 * (np.arange(4) + .5)
    centres_y = 10 - 5 * (np.arange(3) + .5)
    model = GridModel(header, 2 + .1 * centres_x[None, :] + .05 * centres_y[:, None])
    xy = np.array([(-7.5, 7.5), (0.0, 0.0), (6.0, -1.0), (1.3, 4.4)])
    assert np.allclose(model.raw(xy), 2 + .1 * xy[:, 0] + .05 * xy[:, 1])
    assert np.isclose(model.raw([(100.0, 0.0)])[0], model.raw([(7.5, 0.0)])[0])
    # Water and low shore are floored.
    assert GridModel(header, np.full((3, 4), -.26))([(0.0, 0.0)])[0] == MIN_LAND


def clip(geometry, x0, y0, x1, y1):
    """Exact intersection with a rectangle.

    clip_by_rect is faster but returned geometry outside the region for a clipped sliver
    near the first slice's south edge, so it is not used.
    """
    return geometry.intersection(box(x0, y0, x1, y1))


def polygons(geometry):
    if geometry.is_empty:
        return []
    if geometry.geom_type == "Polygon":
        return [geometry]
    if not hasattr(geometry, "geoms"):
        return []
    return [p for g in geometry.geoms for p in polygons(g)]


def densify_side(ring, sides, step):
    """Insert points every STEP metres (on the step grid) along ring edges lying on SIDES.

    SIDES holds ("x", value) or ("y", value) lines. Other edges are returned unchanged.
    """
    out = []
    for p, q in zip(ring[:-1], ring[1:]):
        out.append(p)
        for axis, value in sides:
            i = 0 if axis == "x" else 1
            if abs(p[i] - value) < SIDE_EPS and abs(q[i] - value) < SIDE_EPS:
                j = 1 - i
                lo, hi = sorted((p[j], q[j]))
                first = math.floor(lo / step + 1e-9) + 1
                ticks = [k * step for k in range(first, math.ceil(hi / step - 1e-9))]
                if p[j] > q[j]:
                    ticks.reverse()
                for t in ticks:
                    out.append((value, t) if axis == "x" else (t, value))
                break
    out.append(ring[-1])
    return out


def on_cell_side(p, q, cell):
    x0, y0, x1, y1 = cell
    for i, values in ((0, (x0, x1)), (1, (y0, y1))):
        for v in values:
            if abs(p[i] - v) < SIDE_EPS and abs(q[i] - v) < SIDE_EPS:
                return True
    return False


def cell_pieces(land, region, triangulate_polygon, layers=None):
    """Triangle lists and shore edges of the land surface.

    LAYERS optionally partitions the land into [(geometry, tag)], each cut into the same cells,
    so neighbouring layers meet at the same vertices. Returns (triangles, shore_edges, tags).
    Triangles are three (x, z) points oriented clockwise in X/Z, each with its layer's tag
    (None without layers); shore edges are (p, q) pairs of ring edges on neither a cell side
    nor a boundary between layers.
    """
    layers = layers or [(land, None)]
    between = None
    if len(layers) > 1:
        between = land.boundary.buffer(1e-3, quad_segs=1)
        prepare(between)
    shore = land.boundary.difference(region.boundary.buffer(.05))
    near = shore.buffer(BAND, quad_segs=2)
    prepare(near)
    fine_cache = {}

    def is_fine(i, j):
        key = (i, j)
        if key not in fine_cache:
            fine_cache[key] = bool(near.intersects(
                box(i * COARSE, j * COARSE, (i + 1) * COARSE, (j + 1) * COARSE)))
        return fine_cache[key]

    triangles, shore_edges, tags = [], [], []

    def add(poly, cell, tag):
        for part in polygons(poly):
            # Only degenerate pieces are skipped: a corner sliver of one layer must stay, or
            # its neighbour in the other layer has an open edge.
            if part.area < 1e-7:
                continue
            # Counter-clockwise exteriors, so wall edges run with land on their left.
            part = orient(part, sign=1)
            for tri in triangulate_polygon(part):
                triangles.append(tri)
                tags.append(tag)
            for ring in [part.exterior, *part.interiors]:
                pts = list(ring.coords)
                for p, q in zip(pts[:-1], pts[1:]):
                    if on_cell_side(p, q, cell):
                        continue
                    if between is not None and not between.contains(
                            Point((p[0] + q[0]) / 2, (p[1] + q[1]) / 2)):
                        continue
                    shore_edges.append((p, q))

    minx, miny, maxx, maxy = land.bounds
    tx0, ty0 = math.floor(minx / TILE), math.floor(miny / TILE)
    tx1, ty1 = math.ceil(maxx / TILE), math.ceil(maxy / TILE)
    per = int(TILE / COARSE)
    for layer, tag in layers:
        for ti in range(tx0, tx1):
            for tj in range(ty0, ty1):
                chunk = clip(layer, ti * TILE, tj * TILE, (ti + 1) * TILE, (tj + 1) * TILE)
                if chunk.is_empty:
                    continue
                for ci in range(ti * per, (ti + 1) * per):
                    for cj in range(tj * per, (tj + 1) * per):
                        x0, y0 = ci * COARSE, cj * COARSE
                        cell = (x0, y0, x0 + COARSE, y0 + COARSE)
                        piece = clip(chunk, *cell)
                        if piece.is_empty:
                            continue
                        if is_fine(ci, cj):
                            n = int(COARSE / FINE)
                            for a in range(n):
                                for b in range(n):
                                    sub = (x0 + a * FINE, y0 + b * FINE,
                                           x0 + (a + 1) * FINE, y0 + (b + 1) * FINE)
                                    add(clip(piece, *sub), sub, tag)
                            continue
                        # A coarse cell bordering fine cells carries their 5 m vertices on
                        # that side, so both surfaces use the same heights along it.
                        sides = [axis_value for axis_value, neighbour in (
                            (("x", x0), (ci - 1, cj)), (("x", x0 + COARSE), (ci + 1, cj)),
                            (("y", y0), (ci, cj - 1)), (("y", y0 + COARSE), (ci, cj + 1)))
                            if is_fine(*neighbour)]
                        for part in polygons(piece):
                            if sides:
                                part = Polygon(
                                    densify_side(list(part.exterior.coords), sides, FINE),
                                    [densify_side(list(r.coords), sides, FINE)
                                     for r in part.interiors])
                            add(part, cell, tag)
    return triangles, shore_edges, tags
