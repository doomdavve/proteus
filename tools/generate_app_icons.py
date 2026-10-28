#!/usr/bin/env python3
"""Export the shared SVG icon to web PNGs and reusable desktop icon containers.

Requires Pillow, Playwright and its Chromium browser (or --executable for Chrome).
Run from any directory; the SVG is the sole artwork source.
"""

import argparse
from io import BytesIO
from pathlib import Path

from PIL import Image
from playwright.sync_api import sync_playwright


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", help="Use an installed Chromium/Chrome executable")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    desktop = root / "assets/icons"
    desktop.mkdir(exist_ok=True)
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(executable_path=args.executable)
        page = browser.new_page(viewport={"width": 1024, "height": 1024},
                                device_scale_factor=1)
        svg = (root / "web/icon.svg").read_text()
        page.set_content('<style>html,body{margin:0}svg{display:block;width:1024px;'
                         'height:1024px}</style>' + svg)
        master = Image.open(BytesIO(page.locator("svg").screenshot())).convert("RGB")
        browser.close()
    master.save(desktop / "wake-velocity.png")
    for name, size in (("apple-touch-icon.png", 180), ("icon-192.png", 192),
                       ("icon-512.png", 512)):
        master.resize((size, size), Image.Resampling.LANCZOS).save(root / "web" / name)
    master.save(desktop / "wake-velocity.ico", sizes=[(n, n) for n in (16, 24, 32, 48, 64, 128, 256)])
    master.save(desktop / "wake-velocity.icns")
    print("Updated web icons and assets/icons/wake-velocity.{png,ico,icns}")


if __name__ == "__main__":
    main()
