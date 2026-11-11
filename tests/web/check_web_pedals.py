#!/usr/bin/env python3
"""Optional Playwright pedal/reset check using pixels from the real instrument panel."""
import argparse
import base64
from pathlib import Path
from playwright.sync_api import sync_playwright

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--url', default='http://127.0.0.1:8000/')
parser.add_argument('--executable', default='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
with sync_playwright() as playwright:
    browser = playwright.chromium.launch(executable_path=args.executable, headless=True,
        args=['--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
    page = browser.new_page(viewport={'width': 1280, 'height': 900})
    errors = []
    page.on('pageerror', lambda error: errors.append(str(error)))
    page.on('console', lambda message: print(message.type, message.text, flush=True)
        if message.type in ['error', 'warning'] else None)
    page.goto(args.url)
    page.wait_for_function("!document.getElementById('play').disabled", timeout=60000)
    page.click('#play')
    page.wait_for_timeout(700)

    def key(value):
        page.keyboard.press(value, delay=100)
        page.wait_for_timeout(160)

    key('Enter')
    for _ in range(3):
        key('ArrowUp')
    key('ArrowLeft')  # Basin.
    key('ArrowDown')
    key('ArrowLeft')  # Waves off.
    key('ArrowDown')
    key('ArrowLeft')  # Smoothing off for software-renderer input checks.
    key('ArrowDown')
    key('Enter')
    page.wait_for_timeout(1000)

    def instrument(name):
        png = page.locator('#canvas').screenshot()
        (args.output / (name + '.png')).write_bytes(png)
        return page.evaluate('''async encoded => {
            const image = new Image();
            image.src = 'data:image/png;base64,' + encoded;
            await image.decode();
            const canvas = document.createElement('canvas');
            canvas.width = image.width; canvas.height = image.height;
            const ctx = canvas.getContext('2d', {willReadFrequently:true}); ctx.drawImage(image, 0, 0);
            const k = image.width / 320;
            function color(x,y) {
                return Array.from(ctx.getImageData(Math.floor((x+.5)*k), Math.floor((y+.5)*k),1,1).data).slice(0,3).join(',');
            }
            const needles = [];
            for (let y=165;y<194;++y)
                if (['118,196,178','233,180,104'].includes(color(104,y))) needles.push(y);
            const steering = [];
            for (let y=163;y<193;++y) for(let x=190;x<233;++x)
                if(color(x,y)==='118,196,178') steering.push([x,y]);
            return {needles,steering};
        }''', base64.b64encode(png).decode())

    def thrust(name, expected):
        reading = instrument(name)
        print(name, reading['needles'], flush=True)
        assert reading['needles'] == [expected], reading

    thrust('neutral', 179)
    page.keyboard.down('w'); page.wait_for_timeout(650)
    thrust('forward', 168)
    page.keyboard.down('s'); page.wait_for_timeout(500)
    thrust('opposing-pedals-neutral', 179)
    page.keyboard.up('s'); page.keyboard.up('w'); page.wait_for_timeout(500)
    thrust('released-neutral', 179)
    page.keyboard.down('Shift'); page.keyboard.down('w'); page.wait_for_timeout(500)
    thrust('precision-forward', 176)
    page.keyboard.up('w'); page.keyboard.down('s'); page.wait_for_timeout(500)
    thrust('precision-reverse', 185)
    page.keyboard.up('Shift'); page.wait_for_timeout(500)
    thrust('reverse', 190)
    page.keyboard.up('s'); page.wait_for_timeout(500)
    page.mouse.wheel(0, 600); page.wait_for_timeout(500)
    thrust('scroll-stays-neutral', 179)
    key('m'); page.locator('#canvas').click()
    page.wait_for_function('document.pointerLockElement === Module.canvas', timeout=10000)
    page.wait_for_timeout(200)
    page.evaluate("document.dispatchEvent(new MouseEvent('mousemove', {movementX:120}))")
    page.wait_for_timeout(800)
    before = instrument('mouse-held-before-r')['steering']
    key('r'); page.wait_for_timeout(400)
    assert instrument('mouse-held-after-r')['steering'] == before
    key('Space'); page.wait_for_timeout(400)
    assert instrument('space-reserved')['steering'] == before
    page.keyboard.down('Backspace'); page.wait_for_timeout(400)
    instrument('reset-progress')
    page.keyboard.up('Backspace'); page.wait_for_timeout(200)
    assert instrument('reset-cancelled')['steering'] == before
    page.keyboard.down('Backspace'); page.wait_for_timeout(1400)
    instrument('reset-completed')
    page.keyboard.up('Backspace'); page.wait_for_timeout(1200)
    assert instrument('reset-centered')['steering'] != before
    key('Escape'); instrument('menu')
    for _ in range(3): key('ArrowDown')
    instrument('menu-reset-selected')
    key('Enter'); page.wait_for_timeout(500)
    thrust('menu-reset-neutral', 179)
    assert not errors, errors
    browser.close()
print('web_pedals=ok')
