# Title graphics

`wake-velocity.png` is the native-size geometric bitmap wordmark. Its source is
`tools/generate_title_wordmark.py`: every glyph shares a 24 px cap height, 6 px stems/bars,
3 px letter spacing and a 10 px word space. Diagonals are rasterized directly at the final
pixel grid. A one-pixel cyan edge and two-pixel coral depth complete the 256×26 RGBA image.
The warm build remaps its three inks to cream, brown and terracotta at load; the cyan build
uses the original inks. Shapes, dimensions and placement are identical. The title draws it 1:1; the window's final integer scaling enlarges the whole screen.

Regenerate the wordmark using a Python environment with Pillow installed:

```sh
python3 tools/generate_title_wordmark.py
```

`river-backdrop.png` is a fresh, scenery-only game render. It retains the original title
sample's bridge-lookback composition: local boat/camera anchor `(641.115, -842.196)`,
heading `0.850 + pi`, horizontal distance/height `32/4` m, fixed 62° vertical field of view,
scene time 73.350 s. No player layer or player wake is drawn. The native GPU renderer
renders at 1280×800 and downsamples to 320×200 using the ordinary game shader. The title
uses a warm daylight grade by default. `-DPROTEUS_WARM_MENU=OFF` retains the original dusk
grade and occupied-window pixels. Both load the same source without moving the backdrop.

Regenerate from the repository root with a working desktop graphics context:

```sh
cmake --build build --target title_backdrop
./build/title_backdrop assets/title/river-backdrop.png
cmake --build build --target wake_velocity
./build/wake_velocity --title --screenshot build/title.png
```

The second build copies the freshly rendered source into the executable's asset directory.
The original moving-boat capture and coarse title alphabets are historical; they are no
longer the current title assets. The owning design and review status is in
[the title plan](../../docs/TITLE_SCREEN_PLAN.md).
