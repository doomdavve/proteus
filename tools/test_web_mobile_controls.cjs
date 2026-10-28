// Virtual-clock tests of the browser input contract, independent of sensor hardware.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
function element() {
  const events = new Map();
  return { hidden: false, disabled: false, dataset: {}, textContent: '',
    addEventListener(type, fn) { if (!events.has(type)) events.set(type, []); events.get(type).push(fn); },
    emit(type, extra = {}) {
      const event = { preventDefault() {}, pointerId: 1, ...extra };
      for (const fn of events.get(type) || []) fn(event);
    },
    setPointerCapture() {}, focus() {} };
}
function setup(permission = 'granted', secure = true, sensor = true) {
  let time = 0, requests = 0;
  const elements = Object.fromEntries(['mobile-controls', 'tilt-status', 'tilt', 'recenter',
    'mobile-menu'].map(id => [id, element()]));
  const buttons = Object.fromEntries(['forward', 'reverse', 'left', 'right'].map(name => {
    const button = element(); button.dataset.hold = name; return [name, button];
  }));
  elements['mobile-controls'].querySelectorAll = () => Object.values(buttons);
  const document = element(), window = element(), canvas = element();
  document.getElementById = id => elements[id];
  window.performance = { now: () => time };
  window.matchMedia = () => ({ matches: true });
  window.screen = { orientation: Object.assign(element(), { angle: 0 }) };
  window.isSecureContext = secure;
  if (sensor) window.DeviceOrientationEvent = { requestPermission() {
    requests++; return permission === 'reject' ? Promise.reject(Error()) : Promise.resolve(permission);
  } };
  const context = vm.createContext({ Number, Math, Map, Promise, NaN });
  vm.runInContext(fs.readFileSync(path.join(__dirname, '../web/mobile_controls.js'), 'utf8'), context);
  const module = {};
  context.installBoatMobileControls(module, canvas, document, window);
  const mobile = module.boatMobile;
  const sample = () => mobile.sample(true) & 15;
  mobile.session(true); sample();
  return { elements, buttons, document, window, canvas, mobile, sample, requests: () => requests,
    tick(ms) { time += ms; },
    orientation(beta, gamma) { window.emit('deviceorientation', { beta, gamma }); } };
}
const settle = async () => { for (let i = 0; i < 6; i++) await Promise.resolve(); };
(async () => {
  const s = setup();
  Object.assign(s.canvas, {width: 960, height: 600,
    getBoundingClientRect: () => ({left: 100, top: 50, width: 320, height: 200})});
  assert.equal(s.mobile.click(), null);
  s.canvas.emit('pointerdown', {pointerType: 'touch', isPrimary: true, clientX: 120, clientY: 70});
  assert.equal(s.mobile.click(), false); // Never activate at both touch-down and touch-up.
  s.canvas.emit('pointerup', {pointerType: 'touch', isPrimary: true, clientX: 120, clientY: 70});
  assert.equal(s.mobile.click().x, 60);
  assert.equal(s.mobile.click(), false); // Each completed tap is consumed once.
  s.canvas.emit('pointerdown', {pointerType: 'touch', isPrimary: true, clientX: 120, clientY: 70});
  s.canvas.emit('pointerup', {pointerType: 'touch', isPrimary: true, clientX: 200, clientY: 70});
  assert.equal(s.mobile.click(), false); // A drag is not a tap.
  s.canvas.emit('pointerdown', {pointerType: 'mouse'});
  assert.equal(s.mobile.click(), null);

  s.buttons.forward.emit('pointerdown', { pointerId: 1 });
  s.buttons.left.emit('pointerdown', { pointerId: 2 });
  assert.equal(s.sample(), 5);
  s.buttons.reverse.emit('pointerdown', { pointerId: 3 });
  assert.equal(s.sample(), 7); // C's pedal mapping neutralizes opposing pedals.
  s.buttons.left.emit('pointercancel', { pointerId: 2 });
  assert.equal(s.sample(), 3);
  s.buttons.forward.emit('lostpointercapture', { pointerId: 1 });
  assert.equal(s.sample(), 2);
  s.mobile.sample(false); assert.equal(s.sample(), 0);
  s.elements['mobile-menu'].emit('click');
  assert.equal(s.mobile.menu(), true); assert.equal(s.mobile.menu(), false);
  s.elements.tilt.emit('click');
  assert.equal(s.requests(), 1); await settle();
  s.orientation(45, 0); s.sample(); assert.equal(s.mobile.target(), 0);
  s.tick(200); s.orientation(45, 35); s.sample(); assert(s.mobile.target() > .5);
  s.buttons.left.emit('pointerdown'); s.sample(); assert(Number.isNaN(s.mobile.target()));
  s.buttons.left.emit('pointerup');
  s.tick(1001); s.sample(); assert(Number.isNaN(s.mobile.target()));
  s.orientation(45, NaN); s.sample(); assert(Number.isNaN(s.mobile.target()));
  s.window.screen.orientation.angle = 90;
  s.window.screen.orientation.emit('change'); s.orientation(0, 45); s.sample();
  assert(Number.isNaN(s.mobile.target()));
  s.elements.recenter.emit('click'); s.orientation(0, 45); s.sample();
  assert.equal(s.mobile.target(), 0);
  s.tick(200); s.orientation(25, 45); s.sample(); assert(s.mobile.target() > .5);
  s.window.screen.orientation.angle = 270; s.window.screen.orientation.emit('change');
  s.elements.recenter.emit('click'); s.orientation(0, 45); s.tick(200);
  s.orientation(25, 45); s.sample(); assert(s.mobile.target() < -.5);
  s.buttons.forward.emit('pointerdown'); s.window.emit('blur'); assert.equal(s.sample(), 0);
  s.buttons.forward.emit('pointerdown'); s.document.hidden = true;
  s.document.emit('visibilitychange'); assert.equal(s.sample(), 0);
  s.document.hidden = false; assert.equal(s.sample(), 0);
  s.mobile.session(false); assert.equal(s.elements['mobile-controls'].hidden, true);
  s.mobile.session(true); assert.equal(s.sample(), 0);
  for (const [permission, secure, sensor] of [['denied', true, true], ['reject', true, true],
    ['granted', false, true], ['granted', true, false]]) {
    const denied = setup(permission, secure, sensor);
    denied.elements.tilt.emit('click'); await settle();
    denied.sample(); assert(Number.isNaN(denied.mobile.target()));
    denied.buttons.right.emit('pointerdown'); assert.equal(denied.sample(), 8);
    assert.match(denied.elements['tilt-status'].textContent, /denied|unavailable/);
  }
  const implicit = setup(); delete implicit.window.DeviceOrientationEvent.requestPermission;
  implicit.elements.tilt.emit('click'); await settle();
  implicit.orientation(45, 0); implicit.sample(); assert.equal(implicit.mobile.target(), 0);
  implicit.elements.tilt.emit('click'); implicit.sample(); assert(Number.isNaN(implicit.mobile.target()));
  console.log('web_mobile_controls=ok');
})().catch(error => { console.error(error); process.exitCode = 1; });
