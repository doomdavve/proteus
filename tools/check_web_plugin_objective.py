"""Objective lifecycle checks shared by the browser authoring qualification runner."""
import json


# A deterministic fixture using the example's unchanged voyage geometry. Hidden VM mutations
# must disappear, whereas explicit state, RNG and timers must survive each worker call.
CALLBACKS = '''local hidden = 0
local function hud(c,s)
  return {kind='hud.set',id='count',label='WORKER UPDATES',value=s.updates,generation=c.generation}
end
return {
  init=function(c)
    local s={updates=0,alarms=0,events=0,migrated=0,draw=0}
    return s,{hud(c,s),{kind='timer.set',id='beat',tick=c.tick+12,generation=c.generation}}
  end,
  update=function(c,s)
    hidden=hidden+1; assert(hidden==1, 'hidden VM state persisted')
    s.updates=s.updates+1; s.alarms=s.alarms+#c.timers; s.events=s.events+#c.events
    s.draw=c.random()
    return s,{hud(c,s),{kind='timer.set',id='beat',tick=c.tick+12,generation=c.generation}}
  end,
  migrate=function(c,s)
    s.migrated=s.migrated+1
    return s,{hud(c,s)}
  end,
'''


def state(data):
    """Read canonical integer-only state; Python preserves the full signed integer range."""
    at = 0

    def take(n):
        nonlocal at
        result = bytes(data[at:at+n]); at += n
        assert len(result) == n
        return result

    def value():
        tag = take(1)
        if tag == b't': return True
        if tag == b'f': return False
        if tag == b'i': return int.from_bytes(take(8), 'little', signed=True)
        count = int.from_bytes(take(2), 'little')
        if tag == b's': return take(count).decode()
        if tag == b'a': return [value() for _ in range(count)]
        assert tag == b'r', tag
        result = {}
        for _ in range(count):
            key = take(take(1)[0]).decode()
            result[key] = value()
        return result

    result = value(); assert at == len(data)
    return result


def check_objectives(browser, args, root, errors, logs):
    context = browser.new_context(viewport={'width': 1100, 'height': 900})
    page = context.new_page()
    workers = set()
    page.on('worker', lambda w: (workers.add(w), w.on('close', lambda c: workers.discard(c))))
    page.on('pageerror', lambda e: errors.append(str(e)))
    page.on('console', lambda m: logs.append(m.text))
    evidence = {}
    original = (root / 'plugins/shelter-tour/main.lua').read_text()
    source = CALLBACKS + '  voyage = {' + original.split('  voyage = {', 1)[1]
    manifest = json.loads((root / 'plugins/shelter-tour/plugin.json').read_text())
    manifest['capabilities'].append('objective.timers')

    def ready():
        page.wait_for_function("!document.getElementById('plugin-stage').disabled", timeout=60000)
        page.locator('#plugin-tools').evaluate('(e) => e.open = true')

    def stage(m, s):
        page.fill('#plugin-manifest', json.dumps(m)); page.fill('#plugin-source', s)
        page.click('#plugin-stage')
        page.wait_for_function("!document.getElementById('plugin-stage').disabled")
        text = page.locator('#plugin-status').inner_text()
        assert 'Rejected:' not in text, text

    def snapshot():
        return page.evaluate('Module.boatPluginCheckpoint')

    def pause():
        page.locator('#plugin-tools').evaluate('(e) => e.open = true')
        page.wait_for_function('Module.boatPluginCheckpoint?.initialized && !Module.boatPluginCheckpoint.waiting')
        # A successful UPDATE remains pending until its T+1 publication; opening the panel
        # pauses physics, so inspection remains stable throughout the following transactions.

    def resume():
        page.locator('#plugin-tools').evaluate('(e) => e.open = false')
        page.click('canvas')

    def held():
        page.wait_for_function("document.getElementById('plugin-status').textContent.includes('OBJECTIVE')")

    def apply_revision(revision):
        generation = snapshot()['generation']
        page.click('#plugin-apply')
        page.wait_for_function('([r,g]) => Module.boatPluginOwner[0] === r && Module.boatPluginCheckpoint.generation > g && !Module.boatPluginCheckpoint.waiting', arg=[revision, generation])

    def hold_next():
        page.evaluate('''() => {
          const t=Module.boatPluginObjective;
          if (!t.originalFinish) {
            t.originalFinish=t.finish.bind(t);
            t.finish = result => {
              if (result?.bytes && window.holdObjective) window.heldObjective=()=>t.originalFinish(result);
              else t.originalFinish(result);
            };
          }
          window.holdObjective=true; window.heldObjective=null;
        }''')

    def release():
        page.evaluate('holdObjective=false; heldObjective(); heldObjective=null')

    try:
        page.goto(args.url + ('&' if '?' in args.url else '?') + 'author'); ready(); stage(manifest, source)
        page.click('#plugin-play')
        page.wait_for_function('Module.boatPluginCheckpoint?.initialized')
        cold = page.evaluate('({n:Module.boatPluginObjective.completed, ms:Module.boatPluginObjective.totalMs, evalMs:Module.boatPluginObjective.evalTotalMs})')
        page.evaluate('Module.boatPluginObjective.maxMs = Module.boatPluginObjective.evalMaxMs = 0')
        page.wait_for_function('Module.boatPluginObjective.completed >= 21')
        evidence['normal_callbacks'] = page.evaluate('''(cold) => {
          const t=Module.boatPluginObjective;
          return {count:t.completed-cold.n, mean_ms:(t.totalMs-cold.ms)/(t.completed-cold.n),
            max_ms:t.maxMs, initial_delivery_ms:cold.ms,
            evaluator_mean_ms:(t.evalTotalMs-cold.evalMs)/(t.completed-cold.n), evaluator_max_ms:t.evalMaxMs};
        }''', cold)
        pause()
        before = snapshot(); initial = state(before['state'])
        assert initial['updates'] >= 2 and initial['alarms'] == initial['updates'], initial
        assert not before['quarantined']
        evidence['initial'] = initial
        page.screenshot(path=str(args.output / 'objective-running.png'))
        evidence['worker_wasm_bytes'] = page.evaluate('Module.boatPluginObjective.wasmBytes')
        assert evidence['worker_wasm_bytes'] > 0

        # Hold a real result: native tick and state must remain pinned, while frames render.
        hold_next(); resume()
        page.wait_for_function('window.heldObjective !== null && Module.boatPluginCheckpoint.waiting')
        frozen = snapshot()
        # Use the existing menu's immediate recovery action. The one-second Backspace hold
        # accumulates capped frame deltas and can outlive the callback deadline on slow renderers.
        page.keyboard.down('p')
        page.wait_for_function('Module.boatPluginMenuOpen', timeout=2000)
        page.keyboard.up('p')
        box = page.locator('canvas').bounding_box()
        height = page.evaluate('Module.boatViewportSize().height')
        # CLI-launched plugin sessions own their window: four menu rows, recovery at y=123
        # in the centered 320x200 menu coordinates.
        page.mouse.move(box['x'] + box['width'] / 2,
                        box['y'] + box['height'] * (0.5 + 23 / height))
        page.mouse.down()
        page.wait_for_function('Module.boatPluginResetPending', timeout=2000)
        page.mouse.up()
        assert snapshot() == frozen, (frozen, snapshot())
        release()
        page.wait_for_function('Module.boatPluginCheckpoint.events > 0 || Module.boatPluginCheckpoint.tick[0] > 240')
        page.wait_for_function('!Module.boatPluginCheckpoint.waiting')
        # Advance until a later batch has consumed the recovery notice exactly once.
        page.wait_for_function('(tick) => Module.boatPluginCheckpoint.tick[0] >= tick + 24', arg=frozen['tick'][0])
        pause()
        assert state(snapshot()['state'])['events'] == 1
        assert not snapshot()['quarantined']

        # Migrate current state, then Undo code against that current state (no old-state rewind).
        revision2 = dict(manifest, state_version=2)
        stage(revision2, source); held()
        kept_state = snapshot()['state']; kept_generation = snapshot()['generation']
        hold_next(); page.click('#plugin-apply')
        page.wait_for_function('window.heldObjective !== null')
        page.click('#plugin-undo')
        page.wait_for_function("document.getElementById('plugin-status').textContent.includes('MIGRATION CANCELED')")
        release()
        page.wait_for_function('!Module.boatPluginCheckpoint.waiting')
        assert snapshot()['state'] == kept_state and snapshot()['generation'] == kept_generation
        assert page.evaluate('Module.boatPluginOwner[0]') == 1
        stage(revision2, source); held()
        # Staging numbers are monotonic even for a discarded candidate.
        apply_revision(3)
        migrated = state(snapshot()['state'])
        assert migrated['migrated'] == 1 and migrated['updates'] >= initial['updates'], migrated
        page.click('#plugin-undo'); held(); apply_revision(1)
        undone = state(snapshot()['state'])
        assert undone['migrated'] == 2 and undone['updates'] == migrated['updates'], undone

        # Failed live migration retains source, state, revision, generation and pending output.
        failing = source.replace('s.migrated=s.migrated+1', "error('migration refusal')")
        stage(revision2, failing); held()
        kept = snapshot()
        page.click('#plugin-apply')
        page.wait_for_function("document.getElementById('plugin-status').textContent.includes('MIGRATION FAILED')")
        assert page.evaluate('Module.boatPluginOwner[0]') == 1
        assert snapshot() == kept, (kept, snapshot())
        page.click('#plugin-undo')  # Discard failed candidate.
        page.wait_for_function("document.getElementById('plugin-status').textContent.includes('DISCARDED')")

        # Restart initializes asynchronously without running source on the game thread.
        # Exposed water is a session restart; arrival edits are live now, and an objective
        # package would be forced through a state migration instead of this restart path.
        route_source = source.replace('weather_range = 2500', 'weather_range = 2400')
        assert route_source != source
        stage(manifest, route_source)
        page.wait_for_function("document.getElementById('plugin-status').textContent.includes('CHANGES THE EXPOSED WATER')")
        previous = page.evaluate('Module.boatPluginOwner[0]')
        generation = snapshot()['generation']
        page.click('#plugin-apply')
        page.wait_for_function('([r,g]) => Module.boatPluginOwner[0] !== r && Module.boatPluginCheckpoint.generation > g && Module.boatPluginCheckpoint.initialized && !Module.boatPluginCheckpoint.waiting', arg=[previous, generation])
        reset = state(snapshot()['state'])
        assert reset['updates'] == 0 and reset['migrated'] == 0, reset
        evidence['restart_initialized'] = True
        source = route_source
        # Reload a callback that fails on update: last committed state survives quarantine.
        broken = source.replace('hidden=hidden+1;', "error('update refusal');")
        stage(revision2, broken); held()
        next_revision = page.evaluate('Module.boatPluginOwner[0]')
        page.click('#plugin-apply')
        page.wait_for_function('(r) => Module.boatPluginOwner[0] !== r', arg=next_revision)
        pause(); last_good = snapshot()['state']
        resume()
        page.wait_for_function('Module.boatPluginCheckpoint.quarantined')
        assert snapshot()['state'] == last_good
        pause()
        page.screenshot(path=str(args.output / 'objective-quarantined.png'))

        # Recover through a valid candidate; timeout also preserves the committed checkpoint.
        stage(manifest, source); held()
        previous = page.evaluate('Module.boatPluginOwner[0]')
        page.click('#plugin-apply')
        page.wait_for_function('(r) => Module.boatPluginOwner[0] !== r', arg=previous)
        pause(); hold_next(); resume()
        page.wait_for_function('window.heldObjective !== null')
        timed = snapshot()
        if args.browser == 'chromium':
            cdp = context.new_cdp_session(page)
            cdp.send('Page.setWebLifecycleState', {'state': 'frozen'})
            import time
            time.sleep(6)
            cdp.send('Page.setWebLifecycleState', {'state': 'active'})
            evidence['deadline_across_freeze'] = True
        page.wait_for_function('Module.boatPluginCheckpoint.quarantined', timeout=10000)
        assert snapshot()['state'] == timed['state']
        release()
        assert snapshot()['quarantined']
        evidence['timeout_retained_state'] = True
        evidence['completed_including_fault_checks'] = page.evaluate('Module.boatPluginObjective.completed')
        page.screenshot(path=str(args.output / 'objective-timeout.png'))
        # Pagehide terminates the warm worker and invalidates late callbacks.
        page.reload(); ready()
        assert page.locator('#plugin-play').is_disabled()
        assert not workers
        for _ in range(args.sessions):
            stage(manifest, source); page.click('#plugin-play')
            page.wait_for_function('Module.boatPluginCheckpoint?.initialized')
            page.reload(); ready()
            assert not workers
        evidence['sessions_cleaned'] = args.sessions + 1

        # GLFW rebases its clock during InitWindow, after INIT has been dispatched. Give
        # the page clock a constant offset to expose any mixing of those two origins without
        # waiting for wall time before launch. Durations and native simulation remain unchanged.
        page.add_init_script('''(() => {
          const now=performance.now.bind(performance);
          Object.defineProperty(performance, 'now', {value:()=>now()+60000});
        })()''')
        page.reload(); ready(); stage(manifest, source)
        page.evaluate('''() => {
          const t=Module.boatPluginObjective, status=t.status.bind(t);
          window.cachedObjectiveResult=false;
          t.status=id => {
            const s=status(id);
            if (s===1) {
              window.cachedObjectiveResult=true;
              if (t.now()-t.job.started < 6000) return 0;
            }
            return s;
          };
        }''')
        page.click('#plugin-play')
        page.wait_for_function('Module.boatPluginCheckpoint?.initialized || Module.boatPluginCheckpoint?.quarantined', timeout=10000)
        assert page.evaluate('window.cachedObjectiveResult')
        assert snapshot()['quarantined'] and not snapshot()['initialized'], snapshot()
        assert not snapshot()['state']
        assert page.evaluate('Module.boatPluginObjective.completed') == 1
        evidence['cached_init_deadline'] = True
    except BaseException:
        logs.append('Objective transport: ' + str(page.evaluate('Module.boatPluginObjective?.lastError')))
        logs.append('Objective checkpoint: ' + str(snapshot()))
        page.screenshot(path=str(args.output / 'objective-failure.png'))
        raise
    finally:
        page.close(); context.close()
    return evidence
