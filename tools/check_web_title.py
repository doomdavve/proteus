#!/usr/bin/env python3
"""Optional Playwright integration check; requires a served Wasm build."""
from playwright.sync_api import sync_playwright
from pathlib import Path
import json
import statistics
import argparse
parser = argparse.ArgumentParser(description='Check the shared browser title, settings persistence and repeated sessions.')
parser.add_argument('--url', default='http://127.0.0.1:8000/')
parser.add_argument('--executable', default='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
out = args.output
out.mkdir(parents=True, exist_ok=True)
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=args.executable, headless=True, args=['--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
    page = b.new_page(viewport={'width': 1280, 'height': 900})
    errors = []
    workers = []
    page.on('pageerror', lambda e: errors.append(str(e)))
    page.on('worker', lambda w: workers.append(w))
    page.on('console', lambda m: print(m.type, m.text, flush=True) if m.type in ['error', 'warning'] else None)
    page.add_init_script('window.audioContexts=[];const Original=window.AudioContext;window.AudioContext=class extends Original {constructor(...a){super(...a);audioContexts.push(this);}};')
    page.goto(args.url)
    page.wait_for_function("!document.getElementById('play').disabled", timeout=60000)
    page.click('#play')
    page.wait_for_timeout(1600)

    def key(k, count=1):
        for _ in range(count):
            page.keyboard.press(k, delay=90)
            page.wait_for_timeout(200)

    def shot(name):
        page.screenshot(path=str(out / (name + '.png')))
    shot('title')
    key('ArrowDown')
    key('ArrowDown')
    key('Enter')
    shot('settings')
    key('ArrowRight')
    key('ArrowDown')
    key('ArrowLeft')
    key('ArrowUp')
    key('ArrowUp')
    key('ArrowRight')
    shot('settings-changed')
    key('Escape')
    key('ArrowUp')
    key('ArrowUp')
    key('Enter')
    shot('solo-setup')
    key('ArrowUp')
    key('ArrowUp')
    key('ArrowUp')
    key('ArrowLeft')
    key('ArrowDown')
    key('ArrowDown')
    key('ArrowLeft')
    key('ArrowDown')
    key('Enter')
    page.wait_for_timeout(1800)
    shot('basin')
    page.locator('#canvas').click()
    page.keyboard.down('w')
    page.wait_for_timeout(500)
    page.keyboard.up('w')
    key('Escape')
    shot('pause')
    key('ArrowDown')
    key('ArrowDown')
    key('ArrowDown')
    key('ArrowDown')
    key('Enter')
    page.wait_for_timeout(700)
    shot('returned-title')
    key('Enter')
    key('Enter')
    page.wait_for_timeout(1500)
    shot('second-session')
    key('Escape')
    key('ArrowDown')
    key('ArrowDown')
    key('ArrowDown')
    key('ArrowDown')
    key('Enter')
    page.wait_for_timeout(700)
    text = page.evaluate("async()=>{const db=await new Promise((ok,no)=>{const r=indexedDB.open('/saves');r.onsuccess=()=>ok(r.result);r.onerror=()=>no(r.error)});return await new Promise((ok,no)=>{const t=db.transaction('FILE_DATA');const r=t.objectStore('FILE_DATA').get('/saves/proteus/title.txt');r.onsuccess=()=>ok(r.result?new TextDecoder().decode(r.result.contents):null);r.onerror=()=>no(r.error)});}")
    print('SAVED', text, flush=True)
    assert text and 'mouse_steering=on' in text and ('soundtrack=off' in text) and ('location=basin' in text) and ('smooth_edges=off' in text)
    print('AUDIO', page.evaluate('audioContexts.map(a=>({state:a.state,time:a.currentTime}))'), flush=True)
    assert page.evaluate('audioContexts.filter(a=>a.state === "running").length===1 && !audioContexts.some(a=>a.state === "suspended")')
    page.reload()
    page.wait_for_function("!document.getElementById('play').disabled", timeout=60000)
    page.click('#play')
    page.wait_for_timeout(700)
    key('ArrowDown')
    key('ArrowDown')
    key('Enter')
    shot('settings-after-reload')
    key('Escape')
    key('ArrowDown')  # Credits
    key('ArrowDown')  # Back to launcher
    key('Enter')
    page.wait_for_function("!document.getElementById('play').disabled && !document.body.classList.contains('playing')", timeout=20000)

    # PD4: Harbor life is a bundled package in the browser, and the desktop console edits it
    # through a worker. A plain session creates no worker; opening the console is still free.
    assert not workers, f'default sessions created {len(workers)} workers'
    start_before = page.evaluate('performance.now()')
    page.click('#play')
    start_ms = page.evaluate('performance.now()') - start_before
    page.wait_for_timeout(1200)
    key('Enter')          # SINGLE
    page.wait_for_timeout(400)
    key('ArrowUp', 3)     # PLACE
    key('ArrowRight', 2)  # BASIN -> RIVER -> OUTER
    key('ArrowDown')      # CONTENT
    key('ArrowRight')     # HARBOR LIFE
    key('ArrowDown', 3)   # START
    key('Enter')
    page.wait_for_timeout(4000)
    shot('harbor-life')
    assert page.evaluate('Module.boatPluginOwner[0]') == 1
    assert page.evaluate('Module.boatScenery.state("outer")') == 2
    assert page.evaluate('Module.boatConsoleAvailable') is True
    harbor = page.evaluate('({owner: Module.boatPluginOwner.slice(), scenery: Module.boatScenery.state("outer")})')
    page.locator('#canvas').click()
    key('F2')
    page.wait_for_timeout(300)
    assert page.evaluate('Module.boatConsoleOpen') is True
    shot('console-open')
    page.keyboard.type('= def.voyage.arrival.max_speed', delay=30)
    page.wait_for_timeout(300)
    page.keyboard.press('Enter', delay=80)
    page.wait_for_function('Module.boatConsoleLines && Module.boatConsoleLines.length > 0',
        timeout=30000)
    query_lines = page.evaluate('Module.boatConsoleLines.slice()')
    assert any('6' in line for line in query_lines), query_lines
    shot('console-query')
    assert page.evaluate('Module.boatPluginOwner[0]') == 1
    with page.expect_download() as saved:
        page.keyboard.type('/save drafts', delay=30)
        page.wait_for_timeout(300)
        page.keyboard.press('Enter', delay=80)
    exported = out / 'harbor-life.wake-plugin.json'
    saved.value.save_as(exported)
    saved_data = json.loads(exported.read_text())
    assert saved_data['format'] == 'wake-velocity-plugin'
    assert 'scene' in saved_data['source'] and 'Harbor life' in saved_data['manifest']
    page.keyboard.type('def.scene.marina.berths[1].x = 15', delay=30)
    page.wait_for_timeout(300)
    page.keyboard.press('Enter', delay=80)
    page.wait_for_function('Module.boatPluginOwner[0] === 2', timeout=30000)
    page.wait_for_timeout(500)
    shot('console-edited')
    assert page.evaluate('Module.boatPluginStage.jobs.size') == 0
    frame_ms = page.evaluate('''async() => {
      const samples = []; let last = performance.now();
      for (let i = 0; i < 30; ++i) {
        await new Promise(requestAnimationFrame);
        const now = performance.now(); samples.push(now - last); last = now;
      }
      return samples;
    }''')
    memory = page.evaluate('performance.memory ? performance.memory.usedJSHeapSize : null')
    page.close()
    assert not errors, errors
    b.close()
    (out / 'result.json').write_text(json.dumps({'errors': errors, 'settings': text,
        'harbor_life': harbor, 'console_query': query_lines, 'start_ms': round(start_ms, 1), 'frame_ms': {'median': round(statistics.median(frame_ms), 2), 'max': round(max(frame_ms), 2)}, 'used_heap_bytes': memory, 'result': 'ok'}))
    print('web_title=ok', flush=True)
