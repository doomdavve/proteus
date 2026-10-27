#!/usr/bin/env python3
"""Optional Playwright check against a served Wasm build and WebRTC-enabled native build."""
from playwright.sync_api import sync_playwright
from pathlib import Path
import subprocess, time, socket, argparse
parser = argparse.ArgumentParser(description='Check mixed native/browser play, reset, leave and rejoin.')
parser.add_argument('--url', default='http://127.0.0.1:8000/')
parser.add_argument('--build-dir', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--four-players', action='store_true', help='Fill the room with two native and two browser clients, then reject a fifth.')
parser.add_argument('--mode', choices=['free', 'deathmatch', 'race'], default='free')
parser.add_argument('--executable', default='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)

def port(kind):
    with socket.socket(socket.AF_INET, kind) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]
udp, ws = (port(socket.SOCK_DGRAM), port(socket.SOCK_STREAM))
room_log = args.output / 'server.log'
bot_log = args.output / 'bot.log'
server = subprocess.Popen([str(args.build_dir.resolve() / 'boat_server'), '--bind', '127.0.0.1', '--port', str(udp), '--rtc-port', str(ws), '--no-lan', '--mode', args.mode, '--location', 'river' if args.mode == 'race' else 'basin', '--seconds', '300'], stdout=room_log.open('w'), stderr=subprocess.STDOUT)
for _ in range(100):
    if 'boat_server listening' in room_log.read_text():
        break
    if server.poll() is not None:
        raise RuntimeError(room_log.read_text())
    time.sleep(0.1)
else:
    server.terminate()
    raise RuntimeError('Room did not start')
bot = subprocess.Popen([str(args.build_dir.resolve() / 'boat_bot'), *(['--hunt'] if args.mode == 'deathmatch' else []), '--connect', f'127.0.0.1:{udp}', '--name', 'native', '--predict', '--seconds', '300'], stdout=bot_log.open('w'), stderr=subprocess.STDOUT)
bots = [bot]
if args.four_players:
    bots.append(subprocess.Popen([str(args.build_dir.resolve() / 'boat_bot'), *(['--hunt'] if args.mode == 'deathmatch' else []), '--connect', f'127.0.0.1:{udp}', '--name', 'native2', '--predict', '--seconds', '300'], stdout=(args.output / 'bot2.log').open('w'), stderr=subprocess.STDOUT))
try:
    with sync_playwright() as p:
        b = p.chromium.launch(executable_path=args.executable, headless=True, args=['--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
        page = b.new_page(viewport={'width': 1280, 'height': 900})
        logs = []
        errors = []
        page.on('console', lambda m: (logs.append(m.text), print(m.type, m.text, flush=True)))
        page.on('pageerror', lambda e: errors.append(str(e)))
        page.goto(args.url)
        page.wait_for_function("!document.getElementById('play').disabled", timeout=60000)
        page.locator('#settings').evaluate('(e)=>e.open=true')
        page.uncheck('#fullscreen')
        page.click('#play')
        page.wait_for_timeout(700)

        def key(k):
            page.keyboard.press(k, delay=100)
            page.wait_for_timeout(180)
        key('ArrowDown')
        key('Enter')
        key('ArrowUp')
        key('Enter')
        for i in range(40):
            page.keyboard.press('Backspace', delay=60)
            page.wait_for_timeout(40)
        page.keyboard.type(f'ws://127.0.0.1:{ws}', delay=120)
        page.wait_for_timeout(200)
        key('Enter')
        page.screenshot(path=str(args.output / 'join.png'))
        key('Enter')
        page.wait_for_timeout(4000)
        page.screenshot(path=str(args.output / 'multiplayer.png'))
        print('RTC ERROR', page.evaluate("Module['boatRtc'].failure()"), flush=True)
        assert any(('Joined as slot' in line for line in logs)), logs
        extra_pages = []
        if args.four_players:
            for number in range(1):
                extra = b.new_page(viewport={'width': 960, 'height': 600})
                extra_logs = []
                extra.on('console', lambda message, messages=extra_logs: messages.append(message.text))
                extra.goto(args.url)
                extra.bring_to_front()
                extra.wait_for_function("!document.getElementById('play').disabled", timeout=60000)
                extra.locator('#settings').evaluate('(e)=>e.open=true')
                extra.uncheck('#fullscreen')
                extra.click('#play')
                extra.wait_for_timeout(500)
                for button in ['ArrowDown', 'Enter', 'ArrowUp', 'Enter']:
                    extra.keyboard.press(button, delay=100)
                    extra.wait_for_timeout(180)
                for _ in range(30):
                    extra.keyboard.press('Backspace', delay=250)
                    extra.wait_for_timeout(150)
                extra.keyboard.type(f'ws://127.0.0.1:{ws}', delay=250)
                for _ in range(2):
                    extra.keyboard.press('Enter', delay=100)
                    extra.wait_for_timeout(180)
                extra.wait_for_timeout(3000)
                assert any('Joined as slot' in line for line in extra_logs), extra_logs
                extra_pages.append(extra)
            rejected = subprocess.run([str(args.build_dir.resolve() / 'boat_bot'), *(['--hunt'] if args.mode == 'deathmatch' else []), '--connect', f'127.0.0.1:{udp}', '--name', 'fifth', '--seconds', '2', '--expect-reject'], capture_output=True, text=True, timeout=15)
            (args.output / 'full-room-rejected.log').write_text(rejected.stdout + rejected.stderr)
            assert rejected.returncode == 0, rejected.stdout + rejected.stderr
            assert 'players: 4' in room_log.read_text(), room_log.read_text()
            page.bring_to_front()
            page.screenshot(path=str(args.output / 'four-players.png'))
            for extra in extra_pages:
                extra.close()
            page.bring_to_front()
            page.locator('#canvas').focus()
        page.bring_to_front()
        page.evaluate("""() => {
            window.spawnRequests = 0;
            window.combatCounters = { gun: 0, grenades: new Set(), torpedoes: new Set(), pages: 0 };
            const poll = Module['boatRtc'].poll;
            Module['boatRtc'].poll = () => {
                const event = poll();
                if (event && event.data && event.data[6] === 21) ++combatCounters.pages;
                return event;
            };
            const original = Module['boatRtc'].send;
            Module['boatRtc'].send = (channel, data) => {
                if (channel === 0 && data[6] === 12) ++spawnRequests;
                if (channel === 1 && data[6] === 9) {
                    for (let i = 0; i < data[36]; ++i) {
                        const at = 48 + i * 6;
                        if (data[at + 2] & 2) ++combatCounters.gun;
                        const grenade = (data[at + 3] << 8) | data[at + 4];
                        if (grenade) combatCounters.grenades.add(grenade);
                        if (data[at + 5]) combatCounters.torpedoes.add(data[at + 5]);
                    }
                }
                return original(channel, data);
            };
        }""")
        if args.mode == 'deathmatch':
            page.keyboard.down('Space')
            page.wait_for_timeout(4300)
            page.screenshot(path=str(args.output / 'cannon-overheat.png'))
            page.keyboard.up('Space')
            page.keyboard.press('e', delay=80)
            page.wait_for_timeout(300)
            page.screenshot(path=str(args.output / 'grenade-flight.png'))
            page.keyboard.press('q')
            page.wait_for_timeout(1400)
            page.screenshot(path=str(args.output / 'grenade-burst.png'))
            counters = page.evaluate('({gun:combatCounters.gun,grenades:combatCounters.grenades.size,torpedoes:combatCounters.torpedoes.size,pages:combatCounters.pages})')
            assert counters['gun'] > 0 and counters['grenades'] > 0 and counters['torpedoes'] > 0 and counters['pages'] >= 4, counters
            print('COMBAT', counters, flush=True)
        page.keyboard.down('w')
        page.wait_for_timeout(1500)
        page.keyboard.up('w')
        page.keyboard.down('Backspace')
        if args.mode != 'race':
            page.wait_for_function('spawnRequests === 1', timeout=6000)
        else:
            page.wait_for_timeout(1800)
        page.keyboard.up('Backspace')
        page.wait_for_timeout(1500)
        key('Escape')
        for _ in range(3): key('ArrowDown')
        page.screenshot(path=str(args.output / 'menu-reset.png'))
        key('Enter')
        if args.mode != 'race':
            page.wait_for_function('spawnRequests === 2', timeout=6000)
        page.wait_for_timeout(1500)
        key('Escape')
        page.screenshot(path=str(args.output / 'menu.png'))
        key('ArrowDown')
        key('ArrowDown')
        key('ArrowDown')
        key('ArrowDown')
        key('Enter')
        page.wait_for_timeout(1000)
        page.screenshot(path=str(args.output / 'left.png'))
        key('ArrowDown')
        key('Enter')
        key('Enter')
        page.wait_for_timeout(3000)
        assert sum(('Joined as slot' in line for line in logs)) == 2, logs
        assert not errors, errors
        if args.mode != 'race':
            assert any('resets: 2 completed' in line for line in logs), logs
        b.close()
        print('web_multiplayer=ok', flush=True)
finally:
    for child in [*bots, server]:
        if child.poll() is None:
            child.terminate()
    for child in [*bots, server]:
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
