#!/usr/bin/env python3
"""Opt-in Playwright check against a built/served wake_velocity WebAssembly game."""
import argparse
import json
from pathlib import Path

from playwright.sync_api import sync_playwright


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://127.0.0.1:8000/')
    parser.add_argument('--browser', choices=['chromium', 'firefox', 'webkit'], default='chromium')
    parser.add_argument('--executable')
    parser.add_argument('--headed', action='store_true', help='Use a visible browser window')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    errors = []
    with sync_playwright() as playwright:
        options = {'headless': not args.headed}
        if args.executable:
            options['executable_path'] = args.executable
        if args.browser == 'chromium':
            options['args'] = ['--use-angle=swiftshader', '--enable-unsafe-swiftshader']
        browser = getattr(playwright, args.browser).launch(**options)
        page = browser.new_page(viewport={'width': 1280, 'height': 900})
        page.on('console', lambda message: print(message.type, message.text, flush=True)
                if message.type in ('warning', 'error') else None)
        page.on('pageerror', lambda error: errors.append(str(error)))
        page.on('console', lambda message: errors.append(message.text) if message.type == 'error'
                and 'favicon.ico' not in message.text else None)
        page.goto(args.url)
        page.wait_for_function("!document.getElementById('play').disabled", timeout=60000)
        page.click('#play')
        page.wait_for_function("Module['boatMouse'] && document.activeElement === Module.canvas")
        # Shared title -> single-player setup -> start (default river).
        page.wait_for_timeout(500)
        page.keyboard.press('Enter', delay=100)
        page.wait_for_timeout(200)
        page.keyboard.press('Enter', delay=100)
        page.wait_for_timeout(1000)
        # Observe the actual C bridge consuming input without changing the returned values.
        page.evaluate('''() => {
          window.controlAudit = { movement: [], scroll: [], wanted: 0 };
          const mouse = Module['boatMouse'];
          for (const name of ['movement', 'scroll']) {
            const original = mouse[name];
            mouse[name] = () => { const value = original();
              if (value) controlAudit[name].push(value); return value; };
          }
          const state = mouse.state;
          mouse.state = (wanted, available) => {
            controlAudit.wanted = wanted; return state(wanted, available);
          };
        }''')
        page.bring_to_front()
        page.locator('#canvas').click()
        print('checking capture', flush=True)
        page.keyboard.press('m', delay=150)
        page.wait_for_timeout(200)
        print('capture state', page.evaluate('({focus:document.hasFocus(), locked:document.pointerLockElement === Module.canvas, audit:controlAudit})'), flush=True)
        if not page.evaluate('document.pointerLockElement === Module.canvas'):
            page.wait_for_function('controlAudit.wanted === 0')
            page.evaluate("Module.canvas.dispatchEvent(new WheelEvent('wheel', {deltaY:-100, cancelable:true}))")
            page.wait_for_function('controlAudit.scroll.includes(1)')
            page.keyboard.press('w', delay=150)
            page.keyboard.down('Backspace')
            page.wait_for_timeout(1250)
            page.keyboard.up('Backspace')
            page.screenshot(path=str(args.output / 'keyboard-fallback.png'))
            result = {'browser': args.browser, 'pointer_lock': 'unavailable-or-denied',
                      'audit': page.evaluate('controlAudit'), 'errors': errors}
            (args.output / 'results.json').write_text(json.dumps(result, indent=2))
            assert not errors, errors
            browser.close()
            print(f'web_controls_fallback=ok ({args.browser}; captured steering unavailable)')
            return
        page.wait_for_function('document.pointerLockElement === Module.canvas', timeout=10000)
        page.wait_for_timeout(150)  # First capture frame deliberately discards motion.
        page.mouse.move(650, 300)
        page.mouse.move(710, 300)
        page.evaluate('''() => {
          Module['boatMouse'].movement();
          controlAudit.movement = [];
          for (const movementX of [12, 7, -4])
            document.dispatchEvent(new MouseEvent('mousemove', {movementX}));
        }''')
        print('checking accumulated movement', page.evaluate('controlAudit'), flush=True)
        page.wait_for_function('controlAudit.movement.includes(15)')
        page.wait_for_timeout(700)
        page.screenshot(path=str(args.output / 'steering-held.png'))
        page.evaluate('''() => {
          controlAudit.scroll = [];
          for (const deltaY of [-25, -75]) Module.canvas.dispatchEvent(
            new WheelEvent('wheel', {deltaY, deltaMode: 0, cancelable: true}));
        }''')
        page.wait_for_function('controlAudit.scroll.includes(1)')
        page.evaluate('''() => Module.canvas.dispatchEvent(
          new WheelEvent('wheel', {deltaY: 3, deltaMode: 1, cancelable: true}))''')
        page.wait_for_function('controlAudit.scroll.includes(-1)')
        print('checking help and release', flush=True)
        page.keyboard.press('?', delay=150)
        page.wait_for_function('document.pointerLockElement === null')
        page.keyboard.press('?', delay=150)
        page.wait_for_timeout(200)
        assert page.evaluate('document.pointerLockElement === null')
        page.locator('#canvas').click()
        page.wait_for_function('document.pointerLockElement === Module.canvas')
        # Escape can be handled by the browser without sending a key event to the game.
        page.evaluate('document.exitPointerLock()')
        page.wait_for_function('controlAudit.wanted === 0')
        page.wait_for_timeout(200)
        assert page.evaluate('document.pointerLockElement === null')
        # External unlock opens the Escape menu and preserves mouse steering.
        page.keyboard.press('Enter', delay=150)
        page.wait_for_timeout(200)
        page.locator('#canvas').click()
        page.wait_for_function('document.pointerLockElement === Module.canvas')
        page.keyboard.press('Escape', delay=150)
        page.wait_for_function('document.pointerLockElement === null')
        page.wait_for_function('controlAudit.wanted === 0')
        # Unsupported/denied capture must fall back to working keyboard mode.
        page.evaluate('''() => { Module.canvas.requestPointerLock = () => {
          throw new Error('test denial'); }; }''')
        page.keyboard.press('Enter', delay=150)
        page.wait_for_timeout(200)
        page.locator('#canvas').click()
        page.wait_for_function('controlAudit.wanted === 0')
        page.keyboard.press('w')
        page.keyboard.down('Backspace')
        page.wait_for_timeout(1250)
        page.keyboard.up('Backspace')
        page.screenshot(path=str(args.output / 'keyboard-fallback.png'))
        result = {'browser': args.browser, 'pointer_lock': 'verified', 'audit': page.evaluate('controlAudit'), 'errors': errors}
        (args.output / 'results.json').write_text(json.dumps(result, indent=2))
        assert not errors, errors
        browser.close()
    print(f'web_controls_browser=ok ({args.browser})')


if __name__ == '__main__':
    main()
