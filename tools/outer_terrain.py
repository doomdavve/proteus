"""Land heights for the outer-river bake: a height model from Baskarta contours and spot
heights, and a land surface that is 20 m inland and 5 m near the shore.

Used by tools/prepare_outer_map.py. Heights are metres above the water at y = 0; the datum of
the source is assumed to be RH 2000 and was not confirmed (see the manifest).
"""
import gzip
import json
import math
from pathlib import Path

import numpy as np
from scipy.interpolate import LinearNDInterpolator, NearestNDInterpolator
from shapely import prepare
from shapely.geometry import box
from shapely.geometry.polygon import orient
from shapely.ops import unary_union

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "docs/adventure/sources/terrain-baskarta.json.gz"
COARSE = 20.0  # inland cell, metres
FINE = 5.0  # near-shore cell, metres
BAND = 20.0  # cells this close to the shore are fine
TILE = 400.0  # clipping chunk, only for speed
# Land is never lower than this above the water, so waves do not wash over the surface.
MIN_LAND = 1.0
SIDE_EPS = 1e-6


def load_samples(path=SOURCE):
    data = json.loads(gzip.decompress(Path(path).read_bytes()))
    contours = np.array([(x, y, z) for z, flat in data["contours"]
                         for x, y in zip(flat[0::2], flat[1::2])], dtype=float)
    spots = np.array(data["points"], dtype=float).reshape(-1, 3)
    return data, contours, spots


class HeightModel:
    """Linear interpolation over the contour and spot-height cloud, nearest outside its hull."""

    def __init__(self, samples):
        self.linear = LinearNDInterpolator(samples[:, :2], samples[:, 2])
        self.nearest = NearestNDInterpolator(samples[:, :2], samples[:, 2])

    def __call__(self, xy):
        xy = np.asarray(xy, dtype=float).reshape(-1, 2)
        h = self.linear(xy)
        missing = np.isnan(h)
        if missing.any():
            h[missing] = self.nearest(xy[missing])
        return np.maximum(h, MIN_LAND)


def holdout_error(contours, spots, land):
    """Error of a contour-only model against the spot heights that lie on land."""
    from shapely import contains_xy

    keep = contains_xy(land, spots[:, 0], spots[:, 1])
    model = HeightModel(contours)
    raw = model(spots[keep, :2])
    truth = np.maximum(spots[keep, 2], MIN_LAND)
    err = raw - truth
    return {"samples": int(keep.sum()), "rmse_m": round(float(np.sqrt((err ** 2).mean())), 3),
            "mean_abs_m": round(float(np.abs(err).mean()), 3),
            "p95_abs_m": round(float(np.percentile(np.abs(err), 95)), 3),
            "max_abs_m": round(float(np.abs(err).max()), 3)}


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


def cell_pieces(land, region, triangulate_polygon):
    """Triangle lists and shore edges of the land surface.

    Returns (triangles, shore_edges). Triangles are three (x, z) points oriented clockwise in
    X/Z; shore edges are (p, q) pairs of ring edges not lying on a cell side.
    """
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

    triangles, shore_edges = [], []

    def add(poly, cell):
        for part in polygons(poly):
            if part.area < 1e-4:
                continue
            # Counter-clockwise exteriors, so wall edges run with land on their left.
            part = orient(part, sign=1)
            for tri in triangulate_polygon(part):
                triangles.append(tri)
            for ring in [part.exterior, *part.interiors]:
                pts = list(ring.coords)
                for p, q in zip(pts[:-1], pts[1:]):
                    if not on_cell_side(p, q, cell):
                        shore_edges.append((p, q))

    minx, miny, maxx, maxy = land.bounds
    tx0, ty0 = math.floor(minx / TILE), math.floor(miny / TILE)
    tx1, ty1 = math.ceil(maxx / TILE), math.ceil(maxy / TILE)
    per = int(TILE / COARSE)
    for ti in range(tx0, tx1):
        for tj in range(ty0, ty1):
            chunk = clip(land, ti * TILE, tj * TILE, (ti + 1) * TILE, (tj + 1) * TILE)
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
                                add(clip(piece, *sub), sub)
                        continue
                    # A coarse cell bordering fine cells carries their 5 m vertices on that
                    # side, so both surfaces use the same heights along it.
                    sides = [axis_value for axis_value, neighbour in (
                        (("x", x0), (ci - 1, cj)), (("x", x0 + COARSE), (ci + 1, cj)),
                        (("y", y0), (ci, cj - 1)), (("y", y0 + COARSE), (ci, cj + 1)))
                        if is_fine(*neighbour)]
                    for part in polygons(piece):
                        if sides:
                            from shapely.geometry import Polygon

                            part = Polygon(densify_side(list(part.exterior.coords), sides, FINE),
                                           [densify_side(list(r.coords), sides, FINE)
                                            for r in part.interiors])
                        add(part, cell)
    return triangles, shore_edges
