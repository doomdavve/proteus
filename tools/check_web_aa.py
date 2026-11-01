#!/usr/bin/env python3
"""Optional Playwright check of multiplayer AA against a served build and test room."""
import argparse
from pathlib import Path

from playwright.sync_api import sync_playwright

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--url', default='http://127.0.0.1:8000/')
parser.add_argument('--room-url', required=True, help='WebSocket URL of a compatible test room')
parser.add_argument('--executable', default='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)

with sync_playwright() as p:
    browser = p.chromium.launch(executable_path=args.executable, headless=True,
                               args=['--use-angle=swiftshader', '--enable-unsafe-swiftshader',
                                     '--host-resolver-rules=MAP wake.davve.net ~NOTFOUND'])
    try:
        for smooth in (True, False):
            context = browser.new_context(viewport={'width': 1280, 'height': 900})
            page = context.new_page()
            logs, errors = [], []
            page.on('console', lambda message: logs.append(message.text))
            page.on('pageerror', lambda error: errors.append(str(error)))
            # Observe actual scene render viewports, including the supersampled pass.
            page.add_init_script('''window.renderSizes = [];
                const original = WebGL2RenderingContext.prototype.viewport;
                WebGL2RenderingContext.prototype.viewport = function(x, y, w, h) {
                    const size = `${w}x${h}`;
                    if (!window.renderSizes.includes(size)) window.renderSizes.push(size);
                    return original.call(this, x, y, w, h);
                };''')
            page.goto(args.url)
            page.wait_for_function("!document.getElementById('play').disabled", timeout=60000)
            # Supply a test room to the named-server UI without using a public room.
            page.evaluate('''url => {
                Module.boatServers.count = () => 1;
                Module.boatServers.compatible = () => true;
                Module.boatServers.server = () => ({name: 'AA TEST', description: 'Local room', websocket: url});
            }''', args.room_url)
            page.locator('#settings').evaluate('(element) => element.open = true')
            page.uncheck('#fullscreen')
            page.click('#play')
            page.wait_for_timeout(1000)

            def key(button):
                page.keyboard.press(button, delay=150)
                page.wait_for_timeout(250)

            key('ArrowDown')
            key('ArrowDown')
            key('Enter')
            for _ in range(2):  # STEERING to SMOOTHING
                key('ArrowDown')
            if not smooth:
                key('ArrowRight')
            label = 'on' if smooth else 'off'
            page.screenshot(path=str(args.output / f'settings-{label}.png'))
            key('Escape')
            key('ArrowUp')  # MULTIPLAYER
            key('Enter')
            page.evaluate('window.renderSizes = []')
            key('Enter')
            page.wait_for_function("window.renderSizes.includes('320x200')", timeout=30000)
            for _ in range(120):
                if any('Joined as slot' in line for line in logs):
                    break
                page.wait_for_timeout(250)
            page.wait_for_timeout(1000)
            assert any('Joined as slot' in line for line in logs), logs
            sizes = page.evaluate('window.renderSizes')
            assert ('1280x800' in sizes) == smooth, sizes
            page.keyboard.down('w')
            page.wait_for_timeout(600)
            page.keyboard.up('w')
            page.screenshot(path=str(args.output / f'multiplayer-{label}.png'))
            assert not errors, errors
            assert not any('wake.davve.net' in line for line in logs), logs
            assert not any('Failed to compile' in line or 'Failed to link' in line for line in logs), logs
            (args.output / f'console-{label}.log').write_text('\n'.join(logs))
            print(f'multiplayer smoothing={label}: render viewports {sizes}', flush=True)
            context.close()
    finally:
        browser.close()
