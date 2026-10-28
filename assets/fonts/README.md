# Compact menu fonts

Fonts by **Daniel Linssen** (managore), supplied by the user from the author's downloads:

- [m5x7](https://managore.itch.io/m5x7), `m5x7.ttf`: menu labels, values and the Proteus credit.
  The author lists CC0 and appreciates attribution.
- [m3x6](https://managore.itch.io/m3x6), `m3x6.ttf`: hints, version and map credit.
  The author permits use with attribution. The title's Credits screen credits Daniel Linssen.

Source pages checked 2026-10-27. The `.fnt` metrics and `.png` atlases are generated from
these original TTFs with `tools/generate_menu_fonts.py`. Regenerate from the repository root
in a Python environment with Pillow installed:

```sh
python3 -m pip install Pillow
python3 tools/generate_menu_fonts.py
```

The author recommends a 16 px em size. Pillow/FreeType preserves the fonts' original
pixel grid at that size; raylib's runtime TTF loader instead sizes ascent-to-descent,
which stretches the glyphs. The game loads the baked BMFont assets and draws them 1:1
with point filtering. Visible capitals are 5×7 (m5x7) or 3×6 (m3x6), not 16 px tall;
menu placement aligns their capital tops. Do not shrink the atlas to a nominal 6 or 7 px.
The title wordmark is separately authored bitmap lettering. Native and browser packages
include this directory, its attribution and the original TTFs.
