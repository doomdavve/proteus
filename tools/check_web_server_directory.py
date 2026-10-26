#!/usr/bin/env python3
"""Opt-in browser check: fetch the public directory and join without entering an address."""
import argparse
from pathlib import Path
from playwright.sync_api import sync_playwright

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--url', required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--executable', default='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
parser.add_argument('--unavailable', action='store_true', help='Fail the directory and verify manual joining still works.')
parser.add_argument('--incompatible', action='store_true', help='Expect the listed room to require a different client version.')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)

with sync_playwright() as p:
    browser = p.chromium.launch(executable_path=args.executable, headless=True,
                               args=['--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
    try:
        page = browser.new_page(viewport={'width': 1280, 'height': 900})
        logs, errors, requests = [], [], []
        page.on('console', lambda message: logs.append(message.text))
        page.on('pageerror', lambda error: errors.append(str(error)))
        page.on('request', lambda request: requests.append(request.url))
        if args.unavailable:
            page.route('**/servers.json', lambda route: route.fulfill(status=503, body='offline'))
        page.goto(args.url)
        page.wait_for_function("!document.getElementById('play').disabled", timeout=60000)
        page.wait_for_function("Module['boatServers'].status().includes('UNAVAILABLE')" if args.unavailable
                               else "Module['boatServers'].count() > 0")
        page.locator('#settings').evaluate('(element)=>element.open=true')
        page.uncheck('#fullscreen')
        page.click('#play')
        page.wait_for_timeout(700)

        def key(button):
            page.keyboard.press(button, delay=350)
            page.wait_for_timeout(300)

        key('ArrowDown')
        key('Enter')
        page.wait_for_timeout(1000)
        page.screenshot(path=str(args.output / 'servers.png'))
        assert any(url.endswith('/servers.json') for url in requests), requests
        if args.unavailable:
            assert page.evaluate("Module['boatServers'].count()") == 0
            key('ArrowDown')
            key('Enter')
            for _ in range(40):
                page.keyboard.press('Backspace', delay=150)
                page.wait_for_timeout(100)
            page.keyboard.type('wss://winterfell.davve.net:24443/room', delay=150)
            key('Enter')
            key('Enter')
        else:
            assert page.evaluate("Module['boatServers'].server(0).name") == 'Winterfell'
            key('Enter')  # The default named server; no URL editing or typing.
        if args.incompatible:
            page.wait_for_timeout(1000)
            assert not any('Connecting to' in line or 'Joined as slot' in line for line in logs), logs
            page.screenshot(path=str(args.output / 'incompatible.png'))
            assert not errors, errors
            print('web_server_directory_incompatible=ok')
            raise SystemExit(0)
        page.wait_for_timeout(8000)
        page.screenshot(path=str(args.output / 'joined.png'))
        assert any('Joined as slot' in line for line in logs), logs
        page.keyboard.down('w')
        page.wait_for_timeout(1500)
        page.keyboard.up('w')
        page.screenshot(path=str(args.output / 'driving.png'))
        assert not errors, errors
        (args.output / 'console.log').write_text('\n'.join(logs))
        print('web_server_directory_join=ok')
    finally:
        browser.close()
