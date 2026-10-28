# Wake Velocity app icon

The editable source is [`web/icon.svg`](../../web/icon.svg). It reduces the daylight
menu's Älvsborgsbron river view to one bridge tower, a level deck, blue water, blocky
clouds and short reflections. Six flat colours and broad shapes keep it legible at small
sizes. There is no lettering or baked-in corner mask; iOS applies its own shape.

Generated outputs:

| File | Use |
| --- | --- |
| `wake-velocity.png` | Opaque 1024×1024 master for packaging |
| `wake-velocity.ico` | Windows: 16, 24, 32, 48, 64, 128 and 256 px |
| `wake-velocity.icns` | macOS: standard and Retina representations through 1024 px |
| `../../web/apple-touch-icon.png` | iOS Home Screen, 180×180 |
| `../../web/icon-192.png`, `../../web/icon-512.png` | Android / web manifest |

The web launcher already consumes the SVG and web PNGs. Desktop containers are ready for
packaging reuse; the Windows executable resource and macOS bundle are not yet wired to them.

Regenerate from the repository root (Python with Pillow and Playwright):

```sh
python3 -m venv /tmp/proteus-icon-env
/tmp/proteus-icon-env/bin/pip install pillow playwright
/tmp/proteus-icon-env/bin/playwright install chromium
/tmp/proteus-icon-env/bin/python tools/generate_app_icons.py
```

Alternatively pass `--executable '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'`
to use installed Chrome on macOS. The generator rasterizes the SVG once at 1024 px, then
exports all sizes with Lanczos downsampling. Normal builds use the checked-in outputs and
need none of these Python dependencies.

[Design, verification and review status](../../docs/TITLE_SCREEN_PLAN.md#app-icon-2026-10-28).
