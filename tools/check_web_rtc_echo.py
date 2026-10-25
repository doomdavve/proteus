#!/usr/bin/env python3
"""Optional browser/native protocol-byte gate, without starting a gameplay session."""
import argparse
import socket
import subprocess
import time
from pathlib import Path
from playwright.sync_api import sync_playwright

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--url', default='http://127.0.0.1:8000/')
parser.add_argument('--build-dir', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--executable', default='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
echo = args.build_dir.resolve() / 'boat_rtc_echo'
fixture = args.output / 'ping.bin'
subprocess.run([str(echo), '--fixture', str(fixture)], check=True)
with socket.socket() as sock:
    sock.bind(('127.0.0.1', 0))
    port = sock.getsockname()[1]
with (args.output / 'echo.log').open('w') as log:
    server = subprocess.Popen([str(echo), str(port)], stdout=log, stderr=subprocess.STDOUT)
    try:
        time.sleep(.5)
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(executable_path=args.executable, headless=True)
            page = browser.new_page()
            page.goto(args.url)
            page.add_script_tag(path=str(Path(__file__).resolve().parents[1] / 'web/rtc_transport.js'))
            page.evaluate('''url => {
                window.rtc = installBoatRtcTransport({});
                window.received = [];
                rtc.connect(url);
                window.drain = setInterval(() => {
                    let event;
                    while (event = rtc.poll()) received.push({ ...event,
                        data: event.data ? Array.from(event.data) : null });
                }, 10);
            }''', f'ws://127.0.0.1:{port}')
            page.wait_for_function('received.some(e => e.kind === 1)', timeout=20000)
            data = list(fixture.read_bytes())
            assert page.evaluate('bytes => [0,1,2,3,4].every(c => rtc.send(c, new Uint8Array(bytes)))', data)
            page.wait_for_function('received.filter(e => e.kind === 3).length === 5', timeout=10000)
            assert page.evaluate('bytes => received.filter(e => e.kind === 3).every(e => JSON.stringify(e.data) === JSON.stringify(bytes))', data)
            page.evaluate('received = []')
            # The protocol's big-endian sequence field distinguishes reliable messages.
            assert page.evaluate('''bytes => {
                for (const c of [0,4]) for (let n = 0; n < 32; ++n) {
                    const packet = new Uint8Array(bytes);
                    new DataView(packet.buffer).setUint32(12, n, false);
                    if (!rtc.send(c, packet)) return false;
                }
                return true;
            }''', data)
            page.wait_for_function('received.filter(e => e.kind === 3).length === 64', timeout=10000)
            assert page.evaluate('''() => [0,4].every(c => received.filter(e => e.channel === c)
                .every((e,n) => new DataView(new Uint8Array(e.data).buffer).getUint32(12, false) === n))''')
            page.evaluate('clearInterval(drain); rtc.close()')
            browser.close()
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
print('web_rtc_echo=ok')
