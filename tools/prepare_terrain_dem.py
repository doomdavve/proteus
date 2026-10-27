#!/usr/bin/env python3
"""Resample Göteborg's Höjdmodell 2022 to a 5 m height grid over the first outer-river slice.

Requires numpy, scipy, tifffile and imagecodecs (see docs/adventure/requirements.txt). Run
from anywhere:

    python -I tools/prepare_terrain_dem.py --self-test
    python -I tools/prepare_terrain_dem.py ZIP [ZIP ...]   # hojdmodell_2022_639_13.zip etc.

Writes docs/adventure/sources/terrain-hojdmodell.bin.gz: one JSON header line, then the grid
as little-endian int16 centimetres, rows from north to south. Each cell is the mean of the
land pixels inside it: the 0.5 m pixels above WATER_MAX. The model flattens water to the
surface on the scan days (about -0.26 m), so leaving water out keeps a quay edge at the
quay's height instead of averaging it with the river. Cells with no land pixels (water, or
no data) take the nearest land cell, so the grid is a land surface extended over the water.
The grid covers the Baskarta window (tools/prepare_terrain_sources.py): the river crop and
the first slice.
Source: Göteborgs stad, Stadsbyggnadsförvaltningen, Höjdmodell 2022 (ground model from
airborne laser scanning, May 2022; 0.5 m; EPSG:3007; RH 2000), released under CC0 1.0.
Reads the tiles straight from the downloaded zips, records their SHA-256 hashes, and never
fetches. The zips (about 1 GB for the two that cover the slice) are not committed.
"""
import argparse
import gzip
import hashlib
import io
import json
import math
import re
import sys
import zipfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare_terrain_sources import ORIGIN, WINDOW, origin_3007  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs/adventure/sources/terrain-hojdmodell.bin.gz"
CELL = 5.0  # metres; ten source pixels
PIXEL = 0.5
NODATA = -9999.0
# Pixels at or below this are water (flattened to -0.34..-0.18 m in the slice) or shore that
# the bake floors at 1 m anyway.
WATER_MAX = 0.2
TILE_NAME = re.compile(r"(?:^|/)(\d{4})_(\d{3})\.tif$")
EPSG_KEY = 3072  # ProjectedCSTypeGeoKey


def grid_frame(ox, oy, window=WINDOW, cell=CELL):
    """(west, north, columns, rows) of a CELL-aligned EPSG:3007 grid covering the window."""
    west = math.floor((ox + window[0]) / cell) * cell
    south = math.floor((oy + window[1]) / cell) * cell
    east = math.ceil((ox + window[2]) / cell) * cell
    north = math.ceil((oy + window[3]) / cell) * cell
    return west, north, round((east - west) / cell), round((north - south) / cell)


def tile_geometry(page):
    """(x0, y0) of the tile's top-left corner in EPSG:3007; checks CRS and pixel size."""
    keys = page.tags["GeoKeyDirectoryTag"].value
    entries = {keys[i]: keys[i + 3] for i in range(4, 4 + 4 * keys[3], 4)}
    assert entries.get(EPSG_KEY) == 3007, f"not EPSG:3007: {entries.get(EPSG_KEY)}"
    sx, sy = page.tags["ModelPixelScaleTag"].value[:2]
    assert abs(sx - PIXEL) < 1e-6 and abs(sy - PIXEL) < 1e-6, (sx, sy)
    tie = page.tags["ModelTiepointTag"].value
    assert tie[0] == 0 and tie[1] == 0, tie
    return round(tie[3] / PIXEL) * PIXEL, round(tie[4] / PIXEL) * PIXEL


def accumulate(sums, counts, frame, heights, x0, y0, cell=CELL):
    """Add one tile's land pixels to the per-cell sums and counts."""
    west, north, columns, rows = frame
    h, w = heights.shape
    cols = np.floor((x0 + (np.arange(w) + .5) * PIXEL - west) / cell).astype(np.int64)
    rws = np.floor((north - (y0 - (np.arange(h) + .5) * PIXEL)) / cell).astype(np.int64)
    c, r = np.meshgrid(cols, rws)
    keep = (heights != NODATA) & (heights > WATER_MAX) & (c >= 0) & (c < columns) & (r >= 0) & (r < rows)
    index = r[keep] * columns + c[keep]
    sums += np.bincount(index, heights[keep].astype(np.float64), rows * columns)
    counts += np.bincount(index, minlength=rows * columns)


def fill_missing(grid, missing):
    """Give each missing cell the value of its nearest valid cell."""
    if not missing.any():
        return grid
    from scipy.ndimage import distance_transform_edt

    _, (ri, ci) = distance_transform_edt(missing, return_indices=True)
    return grid[ri, ci]


def resample(tiles, frame, cell=CELL):
    """tiles: iterable of (name, tifffile page source). Returns (heights_m, missing, used)."""
    import tifffile

    west, north, columns, rows = frame
    sums = np.zeros(rows * columns)
    counts = np.zeros(rows * columns, dtype=np.int64)
    used = []
    for name, source in tiles:
        with tifffile.TiffFile(source) as tif:
            page = tif.pages[0]
            x0, y0 = tile_geometry(page)
            h, w = page.shape
            if (x0 >= west + columns * cell or x0 + w * PIXEL <= west or
                    y0 <= north - rows * cell or y0 - h * PIXEL >= north):
                continue
            accumulate(sums, counts, frame, page.asarray(), x0, y0, cell)
        used.append(name)
    missing = (counts == 0).reshape(rows, columns)
    grid = (sums / np.maximum(counts, 1)).reshape(rows, columns)
    return fill_missing(grid, missing), missing, sorted(used)


def encode(header, heights):
    centimetres = np.round(heights * 100).astype("<i2")
    assert np.abs(heights * 100 - centimetres).max() <= .5 + 1e-6, "height outside int16 cm"
    head = json.dumps(header, separators=(",", ":"), ensure_ascii=False).encode()
    return gzip.compress(head + b"\n" + centimetres.tobytes(), 9, mtime=0)


def decode(blob):
    """(header, heights in metres as float32, rows from north to south)."""
    raw = gzip.decompress(blob)
    split = raw.index(b"\n")
    header = json.loads(raw[:split])
    cm = np.frombuffer(raw[split + 1:], dtype="<i2")
    return header, (cm.astype(np.float32) / 100).reshape(header["rows"], header["columns"])


def zip_tiles(paths, frame):
    """Yield (member name, bytes) for the tiles in the zips that can touch the grid."""
    west, north, columns, rows = frame
    east, south = west + columns * CELL, north - rows * CELL
    for path in paths:
        with zipfile.ZipFile(path) as archive:
            for name in sorted(archive.namelist()):
                m = TILE_NAME.search(name)
                if not m:
                    continue
                n_km, e_km = int(m.group(1)), int(m.group(2))
                if (e_km * 1000 >= east or (e_km + 1) * 1000 <= west or
                        n_km * 1000 >= north or (n_km + 1) * 1000 <= south):
                    continue
                yield name, io.BytesIO(archive.read(name))


def self_test():
    import tifffile

    def tile(x0, y0, data):
        """An uncompressed GeoTIFF of DATA with its top-left corner at (x0, y0), EPSG:3007."""
        out = io.BytesIO()
        keys = (1, 1, 0, 1, EPSG_KEY, 0, 1, 3007)
        tifffile.imwrite(out, data.astype(np.float32), extratags=[
            (33550, "d", 3, (PIXEL, PIXEL, 0.0)), (33922, "d", 6, (0, 0, 0, x0, y0, 0)),
            (34735, "H", len(keys), keys)])
        out.seek(0)
        return out

    # Two 10 m x 10 m tiles side by side on a 5 m grid: cells are means of their land pixels,
    # water pixels are left out, a block of no data or water takes its nearest neighbour, and
    # a tile outside the grid is skipped.
    left = np.arange(1, 401, dtype=np.float32).reshape(20, 20)
    left[10:, 10:] = NODATA
    right = np.full((20, 20), 7.25, np.float32)
    right[:10, :5] = -.26  # half of the north-west cell is water
    right[10:, 10:] = -.26  # all of the south-east cell is
    frame = (100.0, 220.0, 4, 2)
    heights, missing, used = resample([("a", tile(100, 220, left)), ("b", tile(110, 220, right)),
                                       ("far", tile(500, 220, right))], frame)
    assert used == ["a", "b"], used
    assert missing.tolist() == [[False] * 4, [False, True, False, True]]
    assert heights[0, 0] == left[:10, :10].mean() and heights[0, 1] == left[:10, 10:].mean()
    assert heights[1, 1] in (heights[0, 1], heights[1, 0], 7.25) and heights[1, 2] == 7.25
    assert heights[0, 2] == 7.25 and heights[1, 3] == 7.25
    # The grid frame snaps outward to whole cells, and the encoding round-trips to 1 cm.
    assert grid_frame(1.0, 2.0, (-3.0, -4.0, 8.0, 9.0)) == (-5.0, 15.0, 3, 4)
    header = {"columns": 4, "rows": 2}
    back_header, back = decode(encode(header, heights))
    assert back_header == header and np.abs(back - heights).max() <= .005


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("zips", nargs="*", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    self_test()
    if args.self_test or not args.zips:
        print("terrain grid fixtures OK")
        return
    ox, oy = origin_3007()
    frame = grid_frame(ox, oy)
    west, north, columns, rows = frame
    zips = sorted(args.zips, key=lambda p: p.name)
    heights, missing, used = resample(zip_tiles(zips, frame), frame)
    assert used, "no tile touches the grid"
    header = {
        "schema": 1,
        "generator": "tools/prepare_terrain_dem.py",
        "source": "Göteborgs stad, Höjdmodell 2022 (markmodell, 0.5 m)",
        "licence": "CC0 1.0",
        "survey": "airborne laser scanning, 9, 15 and 16 May 2022",
        "crs": "EPSG:3007 offsets from the crane origin; heights in metres, RH 2000",
        "origin_epsg4326": list(ORIGIN),
        "window_wsen": list(WINDOW),
        "cell_m": CELL,
        # Edges of the grid relative to the crane origin; row 0 is the northernmost.
        "west_m": round(west - ox, 4), "north_m": round(north - oy, 4),
        "columns": columns, "rows": rows,
        "encoding": "int16 little-endian centimetres after this line, rows north to south",
        "cell_value": f"mean of the 0.5 m pixels above {WATER_MAX} m in the cell; cells "
                      "without any (water, no data) take the nearest such cell",
        "water_max_m": WATER_MAX,
        "filled_cells": int(missing.sum()),
        "tiles": used,
        "source_files": {p.name: {"sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                         for p in zips},
    }
    blob = encode(header, heights)
    OUTPUT.write_bytes(blob)
    print(f"{columns} x {rows} cells from {len(used)} tiles, {int(missing.sum())} filled, "
          f"heights {heights.min():.2f} to {heights.max():.2f} m, "
          f"{len(blob) / 1e6:.1f} MB gz")


if __name__ == "__main__":
    sys.exit(main())
