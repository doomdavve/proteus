// Virtual-clock canvas input tests, independent of sensor hardware and rendering.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
function element() {
  const events = new Map();
  return { hidden: false,
    addEventListener(type, fn) { if (!events.has(type)) events.set(type, []); events.get(type).push(fn); },
    emit(type, extra = {}) {
      const event = { prevented: false, preventDefault() { this.prevented = true; },
        pointerId: 1, pointerType: 'touch', isPrimary: true, ...extra };
      for (const fn of events.get(type) || []) fn(event);
      return event;
    },
    setPointerCapture() {}, focus() {} };
}
function setup(permission = 'granted', secure = true, sensor = true, coarse = true) {
  let time = 0, requests = 0, fullscreen = 0;
  const document = element(), window = element(), canvas = element();
  Object.assign(canvas, {width: 960, height: 600,
    getBoundingClientRect: () => ({left: 100, top: 50, width: 320, height: 200})});
  window.performance = { now: () => time };
  window.matchMedia = () => ({ matches: coarse });
  window.screen = { orientation: Object.assign(element(), { angle: 0 }) };
  window.isSecureContext = secure;
  if (sensor) window.DeviceOrientationEvent = { requestPermission() {
    requests++; return permission === 'reject' ? Promise.reject(Error()) : Promise.resolve(permission);
  } };
  const context = vm.createContext({ Number, Math, Map, Promise, NaN });
  vm.runInContext(fs.readFileSync(path.join(__dirname, '../web/mobile_controls.js'), 'utf8'), context);
  const module = {boatCanFullscreen: true, boatFullscreen() { fullscreen++; }};
  context.installBoatMobileControls(module, canvas, document, window);
  const mobile = module.boatMobile;
  const sample = () => mobile.sample(true) & 15;
  const boxes = [[264,140,52,26], [264,170,52,26], [4,170,30,26], [38,170,30,26],
    [4,4,36,24], [44,4,56,24], [104,4,50,24], [158,4,66,24]];
  function layout() { mobile.layout(boxes.map(([x,y,w,h]) => [x/320,y/200,w/320,h/200])); }
  function pointer(control, type, id = 1, extra = {}) {
    const [x,y,w,h] = boxes[control];
    return canvas.emit(type, {clientX: 100+x+w/2, clientY: 50+y+h/2, pointerId: id, ...extra});
  }
  function tap(control) { pointer(control, 'pointerdown'); pointer(control, 'pointerup'); }
  mobile.session(true); layout(); sample();
  return { document, window, canvas, mobile, sample, pointer, tap, layout,
    requests: () => requests, fullscreen: () => fullscreen,
    tick(ms) { time += ms; },
    orientation(beta, gamma) { window.emit('deviceorientation', { beta, gamma }); } };
}
const settle = async () => { for (let i = 0; i < 6; i++) await Promise.resolve(); };
(async () => {
  const s = setup();
  assert.equal(s.mobile.click(), null);
  const scene = {clientX: 220, clientY: 130};
  s.canvas.emit('pointerdown', scene);
  assert.equal(s.mobile.click(), false); // Never activate at both down and up.
  s.canvas.emit('pointerup', scene);
  assert.equal(s.mobile.click().x, 360); // CSS coordinates -> backing pixels.
  assert.equal(s.mobile.click(), false); // Each completed tap consumed once.
  s.canvas.emit('pointerdown', scene);
  s.canvas.emit('pointermove', {...scene, clientX: 260});
  s.canvas.emit('pointerup', scene);
  assert.equal(s.mobile.click(), false); // Moving away and back is still a drag.
  s.canvas.emit('pointerdown', scene);
  s.canvas.emit('pointercancel'); s.canvas.emit('pointerup', scene);
  assert.equal(s.mobile.click(), false);
  s.canvas.emit('pointerdown', {pointerType: 'mouse'});
  assert.equal(s.mobile.click(), null);

  assert(s.pointer(0, 'pointerdown').prevented);
  s.pointer(2, 'pointerdown', 2, {isPrimary: false});
  assert.equal(s.sample(), 5);
  s.tick(60000); assert.equal(s.sample(), 5); // Long holds do not time out.
  s.pointer(1, 'pointerdown', 3);
  assert.equal(s.sample(), 7); // C neutralizes opposing pedals.
  s.pointer(2, 'pointercancel', 2); assert.equal(s.sample(), 3);
  s.pointer(0, 'lostpointercapture'); assert.equal(s.sample(), 2);
  s.pointer(1, 'pointerup', 3); assert.equal(s.sample(), 0);
  assert.equal(s.mobile.click(), false); // A control release is never a scene tap.
  s.pointer(0, 'pointerdown');
  s.pointer(0, 'pointermove', 1, {clientX: 200}); assert.equal(s.sample(), 0);
  s.pointer(0, 'pointermove'); assert.equal(s.sample(), 1);
  s.mobile.sample(false); assert.equal(s.sample(), 0);
  // A menu touch survives inactive frames, unlike held driving controls.
  s.canvas.emit('pointerdown', scene); s.mobile.sample(false);
  s.canvas.emit('pointerup', scene); assert.equal(s.mobile.click().x, 360);
  s.pointer(0, 'pointerdown'); s.tap(4);
  assert.equal(s.mobile.menu(), true); assert.equal(s.mobile.menu(), false);
  assert.equal(s.sample(), 0);
  s.tap(7); assert.equal(s.fullscreen(), 1);
  s.tap(5); assert.equal(s.requests(), 1); // Synchronous activation call.
  await settle(); assert(s.mobile.state() & 2);
  s.orientation(45, 0); s.sample(); assert.equal(s.mobile.target(), 0);
  s.tick(200); s.orientation(45, 35); s.sample(); assert(s.mobile.target() > .5);
  s.pointer(2, 'pointerdown'); s.sample(); assert(Number.isNaN(s.mobile.target()));
  s.pointer(2, 'pointerup');
  s.tick(1001); s.sample(); assert(Number.isNaN(s.mobile.target()));
  s.orientation(45, NaN); s.sample(); assert(Number.isNaN(s.mobile.target()));
  s.window.screen.orientation.angle = 90;
  s.window.screen.orientation.emit('change'); s.orientation(0, 45); s.sample();
  assert(Number.isNaN(s.mobile.target()));
  s.tap(6); s.orientation(0, 45); s.sample(); assert.equal(s.mobile.target(), 0);
  s.tick(200); s.orientation(25, 45); s.sample(); assert(s.mobile.target() > .5);
  s.window.screen.orientation.angle = 270; s.window.screen.orientation.emit('change');
  s.tap(6); s.orientation(0, 45); s.tick(200);
  s.orientation(25, 45); s.sample(); assert(s.mobile.target() < -.5);
  s.pointer(0, 'pointerdown'); s.window.emit('blur'); assert.equal(s.sample(), 0);
  s.pointer(0, 'pointerdown'); s.document.hidden = true;
  s.document.emit('visibilitychange'); assert.equal(s.sample(), 0);
  s.document.hidden = false; assert.equal(s.sample(), 0);
  s.pointer(0, 'pointerdown'); s.window.emit('resize'); assert.equal(s.sample(), 0);
  assert.equal(s.mobile.regions().length, 0); s.layout();
  s.mobile.session(false); assert.equal(s.mobile.state() & 1, 0);
  s.mobile.session(true); s.layout(); assert.equal(s.sample(), 0);
  for (const [permission, secure, sensor] of [['denied', true, true], ['reject', true, true],
    ['granted', false, true], ['granted', true, false]]) {
    const denied = setup(permission, secure, sensor);
    denied.tap(5); await settle();
    denied.sample(); assert(Number.isNaN(denied.mobile.target()));
    denied.pointer(3, 'pointerdown'); assert.equal(denied.sample(), 8);
    assert.match(denied.mobile.status(), /denied|unavailable/);
  }
  s.pointer(0, 'pointerdown'); s.mobile.cancel(); assert.equal(s.sample(), 0);
  const implicit = setup(); delete implicit.window.DeviceOrientationEvent.requestPermission;
  implicit.tap(5); await settle(); implicit.tick(2001); implicit.sample();
  assert.match(implicit.mobile.status(), /No tilt readings/);
  implicit.orientation(45, 0); implicit.sample(); assert.equal(implicit.mobile.target(), 0);
  assert.equal(implicit.mobile.status(), 'Tilt steering on');
  implicit.tap(5); implicit.sample(); assert(Number.isNaN(implicit.mobile.target()));
  const desktop = setup('granted', true, true, false);
  desktop.pointer(0, 'pointerdown'); assert.equal(desktop.sample(), 0);
  assert.equal(desktop.mobile.state() & 1, 0);
  assert(s.canvas.emit('touchstart').prevented);
  assert(s.canvas.emit('gesturestart').prevented);
  assert(s.canvas.emit('contextmenu').prevented);
  console.log('web_mobile_controls=ok');
})().catch(error => { console.error(error); process.exitCode = 1; });
