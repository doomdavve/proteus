#!/usr/bin/env python3
"""Rasterize the title's geometric lettering on a native pixel grid (requires Pillow)."""
from pathlib import Path

from PIL import Image

# Every letter has the same 24 px cap height and 6 px stems/bars. Coordinates mark
# pixel boundaries, not pixel centers; widths include no inter-letter padding.
GLYPHS = {
    "W": (26, [(0, 0), (6, 0), (6, 18), (10, 18), (10, 0), (16, 0),
               (16, 18), (20, 18), (20, 0), (26, 0), (26, 24), (0, 24)], []),
    "A": (18, [(0, 0), (18, 0), (18, 24), (12, 24), (12, 18), (6, 18),
               (6, 24), (0, 24)], [[(6, 6), (12, 6), (12, 12), (6, 12)]]),
    "K": (18, [(0, 0), (6, 0), (6, 9), (12, 3), (12, 0), (18, 0),
               (18, 6), (12, 12), (18, 18), (18, 24), (12, 24), (12, 21),
               (6, 15), (6, 24), (0, 24)], []),
    "E": (18, [(0, 0), (18, 0), (18, 6), (6, 6), (6, 9), (16, 9),
               (16, 15), (6, 15), (6, 18), (18, 18), (18, 24), (0, 24)], []),
    "V": (20, [(0, 0), (6, 0), (6, 14), (10, 18), (14, 14), (14, 0),
               (20, 0), (20, 17), (13, 24), (7, 24), (0, 17)], []),
    "L": (16, [(0, 0), (6, 0), (6, 18), (16, 18), (16, 24), (0, 24)], []),
    "O": (18, [(0, 0), (18, 0), (18, 24), (0, 24)],
               [[(6, 6), (12, 6), (12, 18), (6, 18)]]),
    "C": (18, [(0, 0), (18, 0), (18, 6), (6, 6), (6, 18), (18, 18),
               (18, 24), (0, 24)], []),
    "I": (6, [(0, 0), (6, 0), (6, 24), (0, 24)], []),
    "T": (18, [(0, 0), (18, 0), (18, 6), (12, 6), (12, 24), (6, 24),
               (6, 6), (0, 6)], []),
    "Y": (20, [(0, 0), (6, 0), (6, 7), (10, 11), (14, 7), (14, 0),
               (20, 0), (20, 10), (13, 17), (13, 24), (7, 24), (7, 17),
               (0, 10)], []),
}


def inside(x, y, polygon):
    result = False
    for a, b in zip(polygon, polygon[1:] + polygon[:1]):
        if (a[1] > y) != (b[1] > y):
            crossing = a[0] + (y - a[1]) * (b[0] - a[0]) / (b[1] - a[1])
            if x < crossing:
                result = not result
    return result


def main():
    mask = set()
    cursor = 0
    for letter in "WAKE VELOCITY":
        if letter == " ":
            cursor += 7  # Replaces the normal 3 px gap with a 10 px word space.
            continue
        width, outline, holes = GLYPHS[letter]
        for y in range(24):
            for x in range(width):
                if inside(x + .5, y + .5, outline) and not any(
                        inside(x + .5, y + .5, hole) for hole in holes):
                    mask.add((cursor + x, y))
        cursor += width + 3
    width = cursor - 3
    image = Image.new("RGBA", (width + 2, 26))
    for x, y in mask:
        image.putpixel((x + 2, y + 2), (231, 128, 105, 255))
    for x, y in mask:
        interior = all((x + dx, y + dy) in mask for dx, dy in
                       ((-1, 0), (1, 0), (0, -1), (0, 1)))
        image.putpixel((x, y), (12, 45, 72, 255) if interior else (174, 245, 238, 255))
    path = Path(__file__).resolve().parents[1] / "assets/title/wake-velocity.png"
    image.save(path)
    print(f"{path.name}: {image.width}x{image.height}, 24 px caps, 6 px strokes")


if __name__ == "__main__":
    main()
