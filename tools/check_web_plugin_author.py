#!/usr/bin/env python3
"""Optional browser qualification of plugin bundles, isolated objectives and live transactions."""
import argparse
import json
import statistics
import time
from pathlib import Path

from playwright.sync_api import sync_playwright
from check_web_plugin_objective import check_objectives

# Each engine needs WebGL in headless mode; Chromium uses SwiftShader, the others their defaults.
ENGINES = {'chromium': ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
           'firefox': [], 'webkit': []}
# Firefox otherwise blocklists software WebGL on a GPU-less machine.
LAUNCH = {'firefox': {'firefox_user_prefs': {'webgl.force-enabled': True}}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://127.0.0.1:8000/')
    parser.add_argument('--browser', choices=sorted(ENGINES), default='chromium')
    parser.add_argument('--executable', help='Chromium-family binary instead of Playwright\'s')
    parser.add_argument('--cycles', type=int, default=20, help='repeated worker validations')
    parser.add_argument('--sessions', type=int, default=3, help='repeated page sessions')
    parser.add_argument('--headed', action='store_true',
                        help='show the window, e.g. Firefox under xvfb-run where headless lacks GL')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.executable and args.browser != 'chromium':
        parser.error('--executable applies only to --browser chromium')
    args.output.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parent.parent
    errors, logs, checks, evidence = [], [], [], {}
    chromium = args.browser == 'chromium'
    current = {}
    try:
        with sync_playwright() as p:
            browser = getattr(p, args.browser).launch(executable_path=args.executable, headless=not args.headed,
                args=ENGINES[args.browser], **LAUNCH.get(args.browser, {}))
            evidence['browser'] = {'engine': args.browser, 'version': browser.version}
            context = browser.new_context(viewport={'width': 1100, 'height': 900})
            page = context.new_page()
            workers = set()

            def watch(page):
                current['page'] = page
                page.on('pageerror', lambda e: errors.append(str(e)))
                page.on('console', lambda m: logs.append(m.text))
                page.on('worker', lambda w: (workers.add(w), w.on('close', lambda c: workers.discard(c))))

            watch(page)

            def ready():
                page.wait_for_function("!document.getElementById('plugin-stage').disabled", timeout=60000)
                page.locator('#plugin-tools').evaluate('(e) => e.open = true')
                page.wait_for_function("document.getElementById('plugin-source').value.length > 0")
                # Record each worker-validated job's duration in page time; results are evidence only.
                page.evaluate('''() => {
                  const t = Module.boatPluginStage, start = t.start.bind(t), finish = t.finish.bind(t);
                  const begun = new Map(); window.stageTimes = [];
                  t.start = (bytes, base) => {
                    const at = performance.now(), id = start(bytes, base);
                    if (t.jobs.get(id)?.worker) begun.set(id, at);
                    return id;
                  };
                  t.finish = (id, result) => {
                    if (begun.has(id) && !t.jobs.get(id)?.state)
                      stageTimes.push({ms: performance.now() - begun.get(id), exit: result?.exit ?? -1});
                    begun.delete(id); finish(id, result);
                  };
                }''')

            def stage():
                page.click('#plugin-stage')
                page.wait_for_function("!document.getElementById('plugin-stage').disabled")
                return page.locator('#plugin-status').inner_text()

            def owner(revision):
                page.wait_for_function('(r) => Module.boatPluginOwner && Module.boatPluginOwner[0] === r',
                    arg=revision)
                return page.evaluate('Module.boatPluginOwner')

            def shot(name):
                page.screenshot(path=str(args.output / (name + '.png')))

            def drained(what):
                # Terminated workers report close asynchronously; allow a short delivery deadline.
                assert page.evaluate('Module.boatPluginStage.jobs.size') == 0, what
                deadline = time.monotonic() + 5
                while workers and time.monotonic() < deadline:
                    page.wait_for_timeout(50)
                assert not workers, f'{what}: {len(workers)} validation workers still alive'

            def heap():
                if not chromium:
                    return None
                cdp.send('HeapProfiler.collectGarbage')
                return cdp.send('Runtime.getHeapUsage')['usedSize']

            page.goto(args.url)
            ready()
            if chromium:
                cdp = context.new_cdp_session(page)
            original = {'manifest': page.input_value('#plugin-manifest'), 'source': page.input_value('#plugin-source')}
            assert 'Voyage staged' in stage()
            cold = page.evaluate('stageTimes[0]')
            with page.expect_download() as download:
                page.click('#plugin-export')
            exported = args.output / 'voyage.wake-plugin.json'
            download.value.save_as(exported)
            data = json.loads(exported.read_text())
            assert data == {'format': 'wake-velocity-plugin', 'version': 1, **original}
            page.fill('#plugin-source', '')
            page.set_input_files('#plugin-import', exported)
            page.wait_for_function("document.getElementById('plugin-status').textContent.includes('Imported draft')")
            assert page.input_value('#plugin-source') == original['source']
            assert 'Voyage staged' in stage()
            checks.append('bundle export/import')
            for example in ('river-sprint',):
                manifest = (root / 'plugins' / example / 'plugin.json').read_text()
                source = (root / 'plugins' / example / 'main.lua').read_text()
                page.fill('#plugin-manifest', manifest); page.fill('#plugin-source', source)
                rejected = stage()
                assert 'Rejected:' in rejected and 'room content' in rejected, rejected
            checks.append('capability rejection')

            # Repeated validations: each uses and retires its own worker; distinct names avoid the
            # one-use prepared receipt so every cycle evaluates in a fresh worker.
            renamed = json.loads(original['manifest'])
            page.fill('#plugin-source', original['source'])
            drained('before validation cycles')
            before = heap()
            page.evaluate('stageTimes.length = 0')
            for cycle in range(args.cycles):
                renamed['name'] = f'Cycle {cycle}'
                page.fill('#plugin-manifest', json.dumps(renamed))
                assert 'Voyage staged' in stage()
            drained('after validation cycles')
            after = heap()
            times = [t['ms'] for t in page.evaluate('stageTimes')]
            assert len(times) == args.cycles, times
            evidence['validation_ms'] = {'cold': round(cold['ms'], 1), 'cycles': len(times),
                'median': round(statistics.median(times), 1), 'max': round(max(times), 1)}
            if before is not None:
                evidence['main_heap_bytes'] = {'before_cycles': before, 'after_cycles': after}
            checks.append(f'{args.cycles} repeated worker validations retired')

            page.fill('#plugin-manifest', original['manifest']); page.fill('#plugin-source', original['source'])
            assert 'Voyage staged' in stage()
            shot('staged')
            page.locator('#settings').evaluate('(e) => e.open = true')
            page.uncheck('#fullscreen')
            page.evaluate('''() => {
              window.authorFrames = 0;
              const state = Module.boatMouse.state;
              Module.boatMouse.state = (...args) => { ++authorFrames; return state(...args); };
            }''')
            page.click('#plugin-play')
            owner(1)
            page.wait_for_function('authorFrames >= 3')
            assert page.evaluate('Module.boatScenery.state("outer")') == 2
            shot('voyage')
            checks.append('worker launch')
            page.locator('#plugin-tools').evaluate('(e) => e.open = true')
            renamed = json.loads(original['manifest']); renamed['name'] = 'Browser test voyage'
            page.fill('#plugin-manifest', json.dumps(renamed))
            stage(); first = owner(2)
            assert first[1:] == [1, 0], first
            checks.append('live rename')
            page.fill('#plugin-source', 'return !!!')
            assert 'Rejected:' in stage()
            assert page.evaluate('Module.boatPluginOwner') == first
            checks.append('syntax rejection')
            page.fill('#plugin-source', original['source'].replace('weather_range = 2500', 'weather_range = 2400'))
            stage()
            page.wait_for_function("document.getElementById('plugin-status').textContent.includes('CHANGES THE EXPOSED WATER')")
            assert page.evaluate('Module.boatPluginOwner') == first
            shot('held-water-edit')
            page.click('#plugin-apply'); second = owner(3)
            assert second[1:] == [2, 0], second
            page.click('#plugin-undo')
            page.wait_for_function("document.getElementById('plugin-status').textContent.includes('(UNDO)')")
            page.click('#plugin-apply'); third = owner(2)
            assert third[1:] == [3, 0], third
            checks.append('held water Apply/Undo')

            # Hold a real worker result while Undo changes the owner's epoch; stale output is rejected.
            page.evaluate('''() => {
              const transport = Module.boatPluginStage, finish = transport.finish.bind(transport);
              window.holdAuthorResult = true; window.authorResult = null;
              transport.finish = (id, result) => {
                if (window.holdAuthorResult && result && result.exit === 0) {
                  window.authorResult = () => finish(id, result); return;
                }
                finish(id, result);
              };
            }''')
            renamed['name'] = 'Stale proposed name'
            page.fill('#plugin-manifest', json.dumps(renamed))
            page.fill('#plugin-source', original['source'])
            page.click('#plugin-stage')
            page.wait_for_function('window.authorResult !== null')
            page.evaluate('Module.boatPluginActions = 2')  # Native Undo without canceling the UI request.
            fourth = owner(1)
            page.evaluate('holdAuthorResult = false; authorResult()')
            page.wait_for_function("document.getElementById('plugin-status').textContent.includes('Voyage changed during validation')")
            assert page.evaluate('Module.boatPluginOwner') == fourth
            assert page.evaluate('Module.boatPluginStage.jobs.size') == 0
            shot('stale-rejected')
            checks.append('stale epoch rejection')

            # Cancel a held real result; a late completion must neither apply nor trigger Undo.
            page.evaluate('holdAuthorResult = true; authorResult = null')
            renamed['name'] = 'Canceled proposed name'
            page.fill('#plugin-manifest', json.dumps(renamed))
            page.click('#plugin-stage')
            page.wait_for_function('window.authorResult !== null')
            page.click('#plugin-undo')
            page.evaluate('holdAuthorResult = false; authorResult()')
            page.wait_for_function('Module.boatPluginActions === 0')
            assert page.evaluate('Module.boatPluginOwner') == fourth
            drained('after cancellation')
            checks.append('cancellation/late completion')

            if chromium:
                # Freeze the tab mid-validation, as a background-tab discard policy would. Either the
                # result or the five-second deadline may win on resume; both must keep a consistent
                # owner, retire the worker and let the game keep running.
                renamed['name'] = 'Frozen proposed name'
                page.fill('#plugin-manifest', json.dumps(renamed))
                page.click('#plugin-stage')
                cdp.send('Page.setWebLifecycleState', {'state': 'frozen'})
                time.sleep(1.5)
                cdp.send('Page.setWebLifecycleState', {'state': 'active'})
                page.wait_for_function("!document.getElementById('plugin-stage').disabled")
                status = page.locator('#plugin-status').inner_text()
                revision = page.evaluate('Module.boatPluginOwner')
                if 'Rejected:' in status:
                    assert revision == fourth, (status, revision)
                else:  # Revision numbers keep counting after Undo; the rename must become active.
                    page.wait_for_function('(r) => Module.boatPluginOwner[0] !== r', arg=fourth[0])
                    revision = page.evaluate('Module.boatPluginOwner')
                frames = page.evaluate('authorFrames')
                page.wait_for_function('(n) => authorFrames >= n + 3', arg=frames)
                drained('after freeze/resume')
                evidence['freeze_resume'] = {'frozen_s': 1.5, 'status': status, 'owner': revision}

                # A held result outlives a freeze past the deadline: resume delivers the overdue
                # deadline first, and the late result is inert.
                page.evaluate('holdAuthorResult = true; authorResult = null')
                renamed['name'] = 'Expired proposed name'
                page.fill('#plugin-manifest', json.dumps(renamed))
                page.click('#plugin-stage')
                page.wait_for_function('window.authorResult !== null')
                cdp.send('Page.setWebLifecycleState', {'state': 'frozen'})
                time.sleep(6)
                cdp.send('Page.setWebLifecycleState', {'state': 'active'})
                page.wait_for_function("document.getElementById('plugin-status').textContent.includes('five-second deadline')")
                page.evaluate('holdAuthorResult = false; authorResult()')
                assert page.evaluate('Module.boatPluginOwner') == revision
                drained('after deadline across freeze')
                checks.append('tab freeze/resume during validation; deadline across a freeze')
            else:
                evidence['freeze_resume'] = 'not exercised: Page lifecycle control is Chromium-only'

            # Leaving the page mid-validation retires the request through pagehide. Each new session
            # restores source only and must revalidate before it can launch.
            renamed['name'] = 'Abandoned proposed name'
            page.fill('#plugin-manifest', json.dumps(renamed))
            page.click('#plugin-stage')
            for session in range(args.sessions):
                page.reload(); ready()
                assert page.locator('#plugin-play').is_disabled()
                assert 'Voyage staged' in stage()
                page.click('#plugin-play'); owner(1)
                # Revision activation precedes lazy scenery loading. A restored session must
                # actually play before the next reload; WebKit can interrupt an overlapping
                # startup navigation while that old session still owns fetches.
                page.wait_for_function('Module.boatPluginCheckpoint?.nativeTick[0] > 0')
                drained(f'session {session + 2}')
            checks.append(f'navigation during validation; {args.sessions} restored sessions revalidated')
            page.close(); context.close()

            evidence['objectives'] = check_objectives(browser, args, root, errors, logs)
            checks.append('isolated objective init/update/timers, gated ticks, migration/Undo/restart, quarantine, timeout and session cleanup')

            # Storage denial preserves import/validation/play and visibly recommends export.
            denied = browser.new_context(viewport={'width': 1100, 'height': 900})
            denied.add_init_script('''Storage.prototype.getItem = function(){throw Error('denied')};
              Storage.prototype.setItem = function(){throw Error('denied')};''')
            page = denied.new_page()
            watch(page)
            page.goto(args.url); ready()
            assert 'Voyage staged' in stage()
            assert 'unavailable' in page.locator('#plugin-storage').inner_text()
            shot('storage-denied')
            with page.expect_download() as denied_download:
                page.click('#plugin-export')
            denied_export = args.output / 'denied.wake-plugin.json'
            denied_download.value.save_as(denied_export)
            assert json.loads(denied_export.read_text()) == data
            page.click('#plugin-play'); owner(1)
            checks.append('storage denial')
            page.close(); denied.close(); browser.close()
    except BaseException:
        # Keep the evidence of a failed run: the page as it stood and its console.
        try:
            current['page'].screenshot(path=str(args.output / 'failure.png'))
        except Exception:
            pass
        (args.output / 'results.json').write_text(json.dumps({'errors': errors, 'logs': logs,
            'checks': checks, 'evidence': evidence, 'failed': True}, indent=2))
        print('\n'.join(logs[-40:]))
        raise
    assert not errors, errors
    (args.output / 'results.json').write_text(json.dumps({'errors': errors, 'logs': logs,
        'checks': checks, 'evidence': evidence}, indent=2))
    print(f'web_plugin_author_browser=ok engine={args.browser} '
          f'evidence={json.dumps(evidence, separators=(",", ":"))}')


if __name__ == '__main__':
    main()
