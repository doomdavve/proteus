#!/usr/bin/env python3
"""Optional Playwright check of G5a's high resolution and HDR fallback against a served build.

Single player on the river with PICTURE set to HIGH RES must render a 1280x800 scene, a
640x480 reflection and (with float render targets) the 640x400 bloom level; it must survive a
mobile rotation and return to the title. With EXT_color_buffer_float hidden, the same session
presents 8-bit (no bloom viewports) without errors. The saved settings keep resolution=high.
"""
import argparse
from pathlib import Path

from playwright.sync_api import sync_playwright

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--url', default='http://127.0.0.1:8000/')
parser.add_argument('--executable', default='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
parser.add_argument('--angle', default='swiftshader', help='ANGLE backend, e.g. swiftshader or metal')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)

VIEWPORTS = '''window.renderSizes = [];
    const original = WebGL2RenderingContext.prototype.viewport;
    WebGL2RenderingContext.prototype.viewport = function(x, y, w, h) {
        const size = `${w}x${h}`;
        if (!window.renderSizes.includes(size)) window.renderSizes.push(size);
        return original.call(this, x, y, w, h);
    };'''
NO_FLOAT = '''const getExtension = WebGL2RenderingContext.prototype.getExtension;
    WebGL2RenderingContext.prototype.getExtension = function(name) {
        return /color_buffer_(half_)?float|float_blend/i.test(name) ? null : getExtension.call(this, name);
    };
    const supported = WebGL2RenderingContext.prototype.getSupportedExtensions;
    WebGL2RenderingContext.prototype.getSupportedExtensions = function() {
        return supported.call(this).filter(n => !/color_buffer_(half_)?float|float_blend/i.test(n));
    };'''
SAVED = """async()=>{const db=await new Promise((ok,no)=>{const r=indexedDB.open('/saves');r.onsuccess=()=>ok(r.result);r.onerror=()=>no(r.error)});return await new Promise((ok,no)=>{const t=db.transaction('FILE_DATA');const r=t.objectStore('FILE_DATA').get('/saves/proteus/title.txt');r.onsuccess=()=>ok(r.result?new TextDecoder().decode(r.result.contents):null);r.onerror=()=>no(r.error)});}"""


def session(browser, name, float_targets, mobile):
    options = {'viewport': {'width': 1280, 'height': 900}}
    if mobile:
        options = {'viewport': {'width': 844, 'height': 390}, 'is_mobile': True, 'has_touch': True,
                   'device_scale_factor': 2}
    context = browser.new_context(**options)
    page = context.new_page()
    errors, logs = [], []
    page.on('console', lambda m: logs.append(m.text))
    page.on('pageerror', lambda e: errors.append(str(e)))
    # The game's stderr reaches the console as errors; real failures name GL, shaders or WebGL.
    page.on('console', lambda m: errors.append(m.text) if m.type == 'error'
            and any(w in m.text for w in ('WebGL', 'GL_', 'SHADER', 'shader', 'abort')) else None)
    page.on('console', lambda m: print(name, m.type, m.text, flush=True) if m.type in ['error', 'warning'] else None)
    page.add_init_script(VIEWPORTS + ('' if float_targets else NO_FLOAT))
    page.goto(args.url)
    page.wait_for_function("!document.getElementById('play').disabled", timeout=60000)
    page.click('#play')
    page.wait_for_timeout(1600)

    def key(k):
        page.keyboard.press(k, delay=90)
        page.wait_for_timeout(200)

    key('Enter')  # SINGLE PLAYER, on START with the river selected
    key('ArrowUp')  # PICTURE
    key('ArrowRight')  # SMOOTH -> HIGH RES
    page.screenshot(path=str(args.output / f'{name}-setup.png'))
    key('ArrowDown')
    key('Enter')  # START
    page.wait_for_timeout(5000)
    page.screenshot(path=str(args.output / f'{name}-river.png'))
    sizes = page.evaluate('window.renderSizes')
    if mobile:
        page.set_viewport_size({'width': 390, 'height': 844})  # Rotate to portrait.
        page.wait_for_timeout(2500)
        page.screenshot(path=str(args.output / f'{name}-portrait.png'))
        page.set_viewport_size({'width': 844, 'height': 390})
        page.wait_for_timeout(2500)
        page.screenshot(path=str(args.output / f'{name}-landscape.png'))
        sizes = page.evaluate('window.renderSizes')
    key('Escape')
    for _ in range(4):
        key('ArrowDown')
    key('Enter')  # BACK TO TITLE
    page.wait_for_timeout(800)
    page.screenshot(path=str(args.output / f'{name}-title.png'))
    saved = page.evaluate(SAVED)
    context.close()
    print(name, 'render viewports', sizes, flush=True)
    assert not errors, errors
    fallback = any('HDR is unavailable' in text for text in logs)
    assert fallback == (not float_targets), logs
    assert saved and 'resolution=high' in saved, saved
    if not mobile:
        assert '1280x800' in sizes and '640x480' in sizes, sizes
        assert ('640x400' in sizes) == float_targets, sizes
    return sizes


with sync_playwright() as p:
    browser = p.chromium.launch(executable_path=args.executable, headless=True,
                                args=[f'--use-angle={args.angle}', '--enable-unsafe-swiftshader',
                                      '--host-resolver-rules=MAP wake.davve.net ~NOTFOUND'])
    try:
        session(browser, 'hdr', True, False)
        session(browser, 'fallback', False, False)
        session(browser, 'mobile', True, True)
    finally:
        browser.close()
print('web resolution check passed', flush=True)
