// Event-contract tests; real browser integration is checked separately against the Wasm build.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const context = vm.createContext({ Number });
vm.runInContext(fs.readFileSync(path.join(__dirname, '../../web/mouse_controls.js'), 'utf8'), context);
function target() {
  const listeners = new Map();
  return {
    addEventListener(type, handler) {
      if (!listeners.has(type)) listeners.set(type, []);
      listeners.get(type).push(handler);
    },
    emit(type, values = {}) {
      const event = { preventDefault() { this.prevented = true; }, ...values };
      for (const handler of listeners.get(type) || []) handler(event);
      return event;
    }
  };
}
function setup(request = 'promise') {
  const canvas = target(), document = target(), window = target();
  const module = { boatGameStarted: true };
  let requests = 0;
  document.activeElement = canvas;
  document.pointerLockElement = null;
  document.exitPointerLock = () => {
    document.pointerLockElement = null;
    document.emit('pointerlockchange');
  };
  if (request !== 'missing') canvas.requestPointerLock = () => {
    ++requests;
    if (request === 'reject') return Promise.reject(new Error('denied'));
    return request === 'promise' ? Promise.resolve() : undefined;
  };
  context.installBoatMouseControls(module, canvas, document, window);
  const mouse = module.boatMouse;
  mouse.state(0, 1);
  return { canvas, document, window, module, mouse, requests: () => requests,
    lock() { document.pointerLockElement = canvas; document.emit('pointerlockchange'); } };
}
(async () => {
  for (const api of ['promise', 'void']) {
    const s = setup(api);
    s.canvas.emit('keydown', { code: 'KeyM' });
    assert.equal(s.requests(), 1);
    assert.equal(s.mouse.toggle(), true);
    assert.equal(s.mouse.toggle(), false);
    assert.equal(s.mouse.state(1, 1), 0); // Request is not successful capture yet.
    s.canvas.emit('click');
    assert.equal(s.requests(), 1); // No duplicate while pending.
    s.lock();
    assert.equal(s.mouse.state(1, 1), 1);
    s.document.emit('mousemove', { movementX: 12 });
    s.document.emit('mousemove', { movementX: 7 });
    s.document.emit('mousemove', { movementX: -4 });
    assert.equal(s.mouse.movement(), 15); // Preserve every event in a frame.
    assert.equal(s.mouse.movement(), 0);
    s.document.exitPointerLock(); // Browser's Escape may bypass game key events.
    assert.equal(s.mouse.state(1, 1), 2);
    s.mouse.state(0, 1);
    assert.equal(s.requests(), 1); // Never automatically relock after Escape.
    s.canvas.emit('keydown', { code: 'KeyM' });
    s.lock();
    s.mouse.state(1, 1);
    s.mouse.state(0, 0); // Help releases capture intentionally.
    assert.equal(s.mouse.state(1, 1), 0); // A fresh gesture is needed to resume.
    s.canvas.emit('click');
    assert.equal(s.requests(), 3);
    s.lock();
    s.window.emit('blur');
    assert.equal(s.mouse.state(0, 0), 2);
  }
  // Escape can cancel a granted lock before its acquisition callback is delivered.
  const cancelled = setup();
  cancelled.canvas.emit('keydown', { code: 'KeyM' });
  cancelled.document.emit('pointerlockchange');
  assert.equal(cancelled.mouse.state(1, 1), 2);
  for (const api of ['missing', 'reject']) {
    const s = setup(api);
    s.canvas.emit('keydown', { code: 'KeyM' });
    await Promise.resolve();
    assert.equal(s.mouse.state(1, 1), 6); // Fall back to keyboard on unsupported/denied lock.
  }
  const s = setup();
  function wheel(deltaY, deltaMode = 0, extra = {}) {
    return s.canvas.emit('wheel', { deltaY, deltaMode, ...extra });
  }
  assert.equal(wheel(-25).prevented, true);
  wheel(-75);
  assert.equal(s.mouse.scroll(), 1);
  wheel(3, 1);
  assert.equal(s.mouse.scroll(), -1);
  wheel(-1, 2);
  assert.equal(s.mouse.scroll(), 1);
  wheel(0, 0, { deltaX: 100 });
  wheel(100, 0, { ctrlKey: true });
  wheel(NaN);
  assert.equal(s.mouse.scroll(), 0);
  wheel(-100);
  s.window.emit('blur');
  assert.equal(s.mouse.scroll(), 0);
  s.document.activeElement = null;
  assert.equal(wheel(-100).prevented, undefined);
  assert.equal(s.mouse.scroll(), 0);
  console.log('web_mouse_controls=ok');
})().catch(error => { console.error(error); process.exitCode = 1; });
