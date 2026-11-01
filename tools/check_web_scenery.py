#!/usr/bin/env python3
"""Optional Playwright check of on-demand scenery (A3.5i); requires a served Wasm build.

The page must not request the outer scenery pack until a single-player session there starts;
the session then fetches it once, draws the outer map, and a second outer session reuses it.
"""
import argparse
import json
from pathlib import Path

from playwright.sync_api import sync_playwright

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--url', default='http://127.0.0.1:8000/')
parser.add_argument('--executable', default='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--throttle-kbps', type=int, default=0,
                    help='Limit downloads after the page has loaded, to capture the loading line')
args = parser.parse_args()
out = args.output
out.mkdir(parents=True, exist_ok=True)
with sync_playwright() as p:
    browser = p.chromium.launch(executable_path=args.executable, headless=True,
                                args=['--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
    page = browser.new_page(viewport={'width': 1280, 'height': 900})
    errors, scenery = [], []
    page.on('pageerror', lambda e: errors.append(str(e)))
    page.on('console', lambda m: print(m.type, m.text, flush=True) if m.type in ['error', 'warning'] else None)
    page.on('request', lambda r: scenery.append(r.url) if '/scenery-' in r.url else None)
    page.goto(args.url)
    page.wait_for_function("!document.getElementById('play').disabled", timeout=60000)
    page.click('#play')
    page.wait_for_timeout(1600)

    def key(k):
        page.keyboard.press(k, delay=90)
        page.wait_for_timeout(200)

    def shot(name):
        page.screenshot(path=str(out / (name + '.png')))

    shot('title')
    assert not scenery, f'outer pack requested before any session: {scenery}'
    key('Enter')  # SINGLE PLAYER
    for _ in range(3):
        key('ArrowUp')  # START -> PLACE
    key('ArrowRight')  # River -> Outer river
    shot('solo-outer')
    if args.throttle_kbps:
        cdp = page.context.new_cdp_session(page)
        cdp.send('Network.emulateNetworkConditions', {
            'offline': False, 'latency': 50, 'uploadThroughput': -1,
            'downloadThroughput': args.throttle_kbps * 1024})
    for _ in range(3):
        key('ArrowDown')
    key('Enter')  # START
    page.wait_for_timeout(1500 if args.throttle_kbps else 150)
    shot('loading')
    page.wait_for_function(
        "performance.getEntriesByType('resource').some(e => e.name.includes('/scenery-'))",
        timeout=120000)
    page.wait_for_timeout(4000)
    shot('outer')
    assert len(scenery) == 1, f'expected one outer pack request, saw {scenery}'
    sizes = page.evaluate("performance.getEntriesByType('resource').filter(e => e.name.includes('/scenery-'))"
                          ".map(e => ({name: e.name, bytes: e.transferSize, ms: e.duration}))")
    print('FETCH', sizes, flush=True)
    key('Escape')
    for _ in range(4):
        key('ArrowDown')
    key('Enter')  # BACK TO TITLE
    page.wait_for_timeout(700)
    key('Enter')  # SINGLE PLAYER, remembered Outer river
    key('Enter')  # START
    page.wait_for_timeout(3000)
    shot('outer-again')
    assert len(scenery) == 1, f'the second outer session fetched again: {scenery}'
    assert not errors, errors
    (out / 'result.json').write_text(json.dumps({'errors': errors, 'requests': scenery,
                                                 'fetch': sizes, 'result': 'ok'}))
    print('web_scenery=ok', flush=True)
    browser.close()
