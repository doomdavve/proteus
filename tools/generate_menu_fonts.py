#!/usr/bin/env python3
"""Bake Daniel Linssen's supplied pixel TTFs into native-size BMFont atlases (Pillow)."""
from pathlib import Path

from PIL import Image, ImageFont


def generate(directory, name):
    # Pillow/FreeType sizes the em square; raylib's TTF loader sizes ascent-to-descent,
    # which stretches these 16 px em fonts. Bake once, then use 1:1 bitmap rendering.
    font = ImageFont.truetype(str(directory / f"{name}.ttf"), 16)
    baseline = font.getmetrics()[0]
    columns, cell_width, cell_height = 16, 12, 16
    atlas = Image.new("L", (columns * cell_width, 6 * cell_height))
    lines = [
        f'info face="{name}" size=16 bold=0 italic=0 charset="" unicode=0 stretchH=100 smooth=0 aa=1 padding=1,1,1,1 spacing=0,0',
        f"common lineHeight=16 base={baseline} scaleW={atlas.width} scaleH={atlas.height} pages=1 packed=0",
        f'page id=0 file="{name}.png"',
        "chars count=95",
    ]
    for index, codepoint in enumerate(range(32, 127)):
        character = chr(codepoint)
        mask, origin = font.getmask2(character, mode="L", anchor="ls")
        glyph = Image.frombytes("L", mask.size, bytes(mask)).point(lambda a: 255 if a >= 128 else 0)
        bounds = glyph.getbbox()
        x, y = (index % columns) * cell_width + 1, (index // columns) * cell_height + 1
        if bounds:
            glyph = glyph.crop(bounds)
            width, height = glyph.size
            offset_x, offset_y = origin[0] + bounds[0], baseline + origin[1] + bounds[1]
            if width > cell_width - 2 or height > cell_height - 2:
                raise ValueError(f"{name}: {character!r} exceeds its atlas cell")
            atlas.paste(glyph, (x, y))
        else:
            width, height, offset_x, offset_y = 1, 1, 0, 0
        advance = font.getlength(character)
        if advance != int(advance):
            raise ValueError(f"{name}: {character!r} has a fractional advance")
        lines.append(
            f"char id={codepoint} x={x} y={y} width={width} height={height} "
            f"xoffset={offset_x} yoffset={offset_y} xadvance={int(advance)} page=0 chnl=15"
        )
    atlas.save(directory / f"{name}.png")
    (directory / f"{name}.fnt").write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"{name}: {atlas.width}x{atlas.height}, 95 ASCII glyphs, 16 px em")


def main():
    directory = Path(__file__).resolve().parents[1] / "assets/fonts"
    for name in ("m5x7", "m3x6"):
        generate(directory, name)


if __name__ == "__main__":
    main()
