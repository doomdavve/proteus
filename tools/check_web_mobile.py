#!/usr/bin/env python3
"""Optional touch/Wasm integration and visual review; physical iPhone review is separate."""
import argparse
import json
from pathlib import Path
from playwright.sync_api import sync_playwright

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--url', default='http://127.0.0.1:8765/')
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--android', action='store_true', help='Emulate a recent Android phone and implicit sensor permission.')
parser.add_argument('--room-url', help='Optional matching local WebRTC room (ws://localhost is allowed).')
parser.add_argument('--executable', default='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
with sync_playwright() as p:
    browser = p.chromium.launch(executable_path=args.executable,
                               args=['--use-gl=angle', '--use-angle=swiftshader'])
    phone = {'width': 915, 'height': 412} if args.android else {'width': 844, 'height': 390}
    options = dict(viewport=phone, has_touch=True, is_mobile=True, device_scale_factor=1)
    if args.android:
        options['user_agent'] = ('Mozilla/5.0 (Linux; Android 15; Pixel 9) AppleWebKit/537.36 '
                                 '(KHTML, like Gecko) Chrome/140.0.0.0 Mobile Safari/537.36')
    page = browser.new_page(**options)
    errors = []
    page.on('pageerror', lambda error: errors.append(str(error)))
    page.on('console', lambda msg: print(msg.text, flush=True) if 'Joined as slot' in msg.text or msg.type == 'error' else None)
    page.add_init_script('''
      window.testAudioContexts = [];
      const OriginalAudioContext = window.AudioContext;
      window.AudioContext = class extends OriginalAudioContext {
        constructor(...args) { super(...args); testAudioContexts.push(this); }
      };
      window.DeviceOrientationEvent = class extends Event {
        static requestPermission() { return Promise.resolve('granted'); }
      };
    ''')
    if args.android:
        page.add_init_script('delete window.DeviceOrientationEvent.requestPermission;')
    page.goto(args.url)
    page.wait_for_function("!document.getElementById('play').disabled", timeout=60000)
    page.locator('#settings').evaluate('(e) => e.open = true')
    if not args.android:
        page.uncheck('#fullscreen')
    page.locator('#play').tap()
    page.wait_for_timeout(1600)
    if args.android:
        page.wait_for_function('document.fullscreenElement !== null')
        page.wait_for_function('testAudioContexts.some(c => c.state === "running")')
        page.evaluate('document.exitFullscreen()')
        page.wait_for_function('document.fullscreenElement === null')
        page.wait_for_timeout(300)

    def tap_scene(x, y):
        box = page.locator('#canvas').bounding_box()
        buffer = page.locator('#canvas').evaluate('(c) => ({width: c.width, height: c.height})')
        # Menus remain a centred 320x200 layout inside the adaptive game viewport.
        page.touchscreen.tap(box['x'] + box['width'] / 2 + (x - 160) * box['width'] / (buffer['width'] / 3),
                             box['y'] + box['height'] / 2 + (y - 100) * box['height'] / (buffer['height'] / 3))
        page.wait_for_timeout(450)

    tap_scene(120, 115)  # SINGLE PLAYER
    page.screenshot(path=str(args.output / 'solo-setup.png'))
    tap_scene(160, 140)  # START
    page.wait_for_function("!document.getElementById('mobile-controls').hidden", timeout=30000)
    page.wait_for_timeout(1500)
    page.evaluate('''() => {
      window.mobileAudit = {buttons: 0, target: null, frames: 0};
      const original = Module.boatMobile.sample;
      Module.boatMobile.sample = active => {
        const result = original(active);
        mobileAudit = {buttons: result, target: Module.boatMobile.target(), frames: mobileAudit.frames + 1};
        return result;
      };
    }''')
    page.wait_for_function('mobileAudit.frames > 2')
    def assert_overlay_layout():
        page.wait_for_function("""() => {
          const box = document.getElementById('canvas').getBoundingClientRect();
          const viewport = window.visualViewport;
          return Math.abs(box.width - viewport.width) < 1
            && Math.abs(box.height - viewport.height) < 1;
        }""")
        canvas_box = page.locator('#canvas').bounding_box()
        buffer = page.locator('#canvas').evaluate('(c) => ({width: c.width, height: c.height})')
        assert abs(canvas_box['width'] / buffer['width'] - canvas_box['height'] / buffer['height']) < .001
        forward = page.locator('[data-hold=forward]').bounding_box()
        reverse = page.locator('[data-hold=reverse]').bounding_box()
        assert forward['y'] + forward['height'] <= reverse['y']
        assert abs(forward['x'] - reverse['x']) < 1
        for selector in ['#mobile-menu', '[data-hold=left]', '[data-hold=forward]']:
            button = page.locator(selector).bounding_box()
            assert button['x'] >= canvas_box['x'] - 1
            assert button['y'] >= canvas_box['y'] - 1
            assert button['x'] + button['width'] <= canvas_box['x'] + canvas_box['width'] + 1
            assert button['y'] + button['height'] <= canvas_box['y'] + canvas_box['height'] + 1

    assert_overlay_layout()
    page.screenshot(path=str(args.output / 'landscape.png'))
    # CDP sends real simultaneous touch contacts through browser pointer dispatch.
    cdp = page.context.new_cdp_session(page)
    def point(selector, identifier):
        box = page.locator(selector).bounding_box()
        return {'x': box['x'] + box['width'] / 2,
                'y': box['y'] + box['height'] / 2, 'id': identifier}
    forward = point('[data-hold=forward]', 1)
    left = point('[data-hold=left]', 2)
    cdp.send('Input.dispatchTouchEvent', {'type': 'touchStart', 'touchPoints': [forward]})
    cdp.send('Input.dispatchTouchEvent', {'type': 'touchStart', 'touchPoints': [forward, left]})
    page.wait_for_function('(mobileAudit.buttons & 15) === 5')
    cdp.send('Input.dispatchTouchEvent', {'type': 'touchEnd', 'touchPoints': []})
    page.wait_for_function('(mobileAudit.buttons & 15) === 0')
    if args.android:
        # Android normally exposes sensors without the iPhone permission method.
        assert page.evaluate('typeof DeviceOrientationEvent.requestPermission') == 'undefined'
        page.locator('#screen-button').tap()
        page.wait_for_function('document.fullscreenElement !== null')
        assert_overlay_layout()
        cdp.send('Input.dispatchTouchEvent', {'type': 'touchStart',
                                             'touchPoints': [point('[data-hold=forward]', 1)]})
        page.wait_for_function('(mobileAudit.buttons & 1) !== 0')
        page.evaluate('document.exitFullscreen()')  # System Back/escape releases held controls.
        page.wait_for_function('document.fullscreenElement === null && (mobileAudit.buttons & 15) === 0')
        cdp.send('Input.dispatchTouchEvent', {'type': 'touchCancel', 'touchPoints': []})
        assert_overlay_layout()
        page.screenshot(path=str(args.output / 'fullscreen-return.png'))
    page.locator('#tilt').tap()
    page.wait_for_function("document.getElementById('tilt').textContent === 'Use touch'")
    def orientation(beta, gamma):
        page.evaluate('''([beta, gamma]) => {
          const event = new Event('deviceorientation');
          Object.assign(event, {beta, gamma}); window.dispatchEvent(event);
        }''', [beta, gamma])
    # Emulated screen orientation is portrait-primary despite landscape viewport.
    orientation(45, 0)
    page.wait_for_timeout(150)
    orientation(45, 35)
    page.wait_for_function('Number.isFinite(mobileAudit.target) && Math.abs(mobileAudit.target) > .3')
    page.locator('#mobile-menu').tap()
    page.wait_for_function("document.querySelector('[data-hold=forward]').disabled")
    page.screenshot(path=str(args.output / 'menu.png'))
    tap_scene(160, 115)  # RESET BOAT, row 3
    page.wait_for_function("!document.querySelector('[data-hold=forward]').disabled")
    page.set_viewport_size({'width': phone['height'], 'height': phone['width']})
    page.wait_for_timeout(500)
    assert_overlay_layout()
    page.screenshot(path=str(args.output / 'portrait.png'))
    assert page.locator('[data-hold=forward]').bounding_box()['x'] >= 0
    page.locator('#mobile-menu').tap()
    page.wait_for_function("document.querySelector('[data-hold=forward]').disabled")
    tap_scene(160, 132)  # BACK TO TITLE
    page.wait_for_function("document.getElementById('mobile-controls').hidden")
    page.screenshot(path=str(args.output / 'returned-title.png'))
    if args.room_url:
        page.set_viewport_size({'width': 844, 'height': 390})
        page.wait_for_timeout(700)
        page.evaluate("""url => {
          // Use the named-room path without relying on a live public directory.
          Module.boatServers.count = () => 1;
          Module.boatServers.server = () => ({name: 'Local mobile test', description: 'Local basin', websocket: url});
          Module.boatServers.compatible = () => true;
          window.wireAudit = {forward: false, steering: false};
          const send = Module.boatRtc.send;
          Module.boatRtc.send = (channel, data) => {
            const bytes = new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
            if (bytes[6] === 9 && bytes.length >= 54) {
              const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
              const last = 48 + (bytes[36] - 1) * 6;
              wireAudit.forward ||= view.getInt8(last + 1) > 0;
              wireAudit.steering ||= view.getInt8(last) !== 0;
            }
            return send(channel, data);
          };
        }""", args.room_url)
        tap_scene(120, 129)  # MULTIPLAYER
        tap_scene(160, 100)  # Named room
        page.wait_for_function("!document.getElementById('mobile-controls').hidden", timeout=30000)
        page.wait_for_timeout(1500)
        page.locator('#recenter').tap()
        orientation(45, 0)
        page.wait_for_timeout(150)
        orientation(45, 35)
        cdp.send('Input.dispatchTouchEvent', {'type': 'touchStart',
                                             'touchPoints': [point('[data-hold=forward]', 1)]})
        page.wait_for_function('wireAudit.forward && wireAudit.steering', timeout=10000)
        cdp.send('Input.dispatchTouchEvent', {'type': 'touchEnd', 'touchPoints': []})
        page.screenshot(path=str(args.output / 'multiplayer.png'))
        page.locator('#mobile-menu').tap()
        page.wait_for_function("document.querySelector('[data-hold=forward]').disabled")
        tap_scene(160, 132)  # LEAVE ROOM
        page.wait_for_function("document.getElementById('mobile-controls').hidden")
    assert not errors, errors
    (args.output / 'result.json').write_text(json.dumps({'result': 'ok', 'errors': errors}))
    print('web_mobile=ok')
    browser.close()
