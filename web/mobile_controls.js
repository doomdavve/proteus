// Browser-only controls. C consumes ordinary pedals and a wheel target, never sensor data.
function installBoatMobileControls(module, canvas, document, window) {
  // Regions come from the same C rectangles used to draw the controls, normalized
  // to the canvas backing store. No DOM controls or duplicate layout rules.
  const pointers = new Map();
  let regions = [], tap = null, touchInput = false;
  let status = 'Touch steering';
  let session = false, active = false, menu = false, tilt = false, pending = false;
  let rotationBlocked = false;
  let center = null, angle = null, target = 0, last = null, receivedAt = null, startedAt = 0;
  const now = () => window.performance.now();
  const mobile = () => window.matchMedia('(pointer: coarse)').matches;
  function clear() { pointers.clear(); target = 0; last = null; }
  function message(text) { status = text; }
  function stopTilt(text) {
    tilt = false; center = null; clear();
    message(text);
  }
  function calibrate() {
    clear(); center = null; angle = null; receivedAt = null; startedAt = now();
    message('Hold comfortably; waiting for tilt…');
  }
  function toggleTilt() {
    if (pending) return;
    if (tilt) { stopTilt('Touch steering'); return; }
    if (!window.isSecureContext || !window.DeviceOrientationEvent) {
      stopTilt('Tilt unavailable. Use touch steering (HTTPS required).'); return;
    }
    pending = true;
    // Invoke permission synchronously in this tap; do not share PLAY's activation.
    let permission;
    try {
      permission = typeof window.DeviceOrientationEvent.requestPermission === 'function'
        ? window.DeviceOrientationEvent.requestPermission() : 'granted';
    } catch (_) { permission = 'denied'; }
    Promise.resolve(permission).then(value => {
      if (value !== 'granted') { stopTilt('Tilt permission denied. Use touch steering.'); return; }
      rotationBlocked = false; tilt = true; calibrate();
    }).catch(() => stopTilt('Tilt unavailable. Use touch steering.'))
      .finally(() => { pending = false; });
  }
  function position(event) {
    const bounds = canvas.getBoundingClientRect();
    return {x: (event.clientX - bounds.left) / bounds.width,
      y: (event.clientY - bounds.top) / bounds.height};
  }
  function hit(point) {
    if (!session || !mobile()) return -1;
    return regions.findIndex(([x, y, w, h]) => w > 0 && h > 0
      && point.x >= x && point.x < x + w && point.y >= y && point.y < y + h);
  }
  function action(control) {
    if (control === 4) { cancel(); menu = true; }
    else if (control === 5) toggleTilt();
    else if (control === 6 && tilt) { rotationBlocked = false; calibrate(); }
    else if (control === 7 && module['boatFullscreen']) module['boatFullscreen']();
  }
  canvas.addEventListener('pointerdown', event => {
    touchInput = event.pointerType === 'touch';
    if (!touchInput) return;
    event.preventDefault(); canvas.focus({preventScroll: true});
    if (document.hidden) return;
    const control = hit(position(event));
    // Disabled driving regions still consume the touch, so it cannot select a menu row.
    pointers.set(event.pointerId, {control, held: active && control >= 0 && control < 4,
      x: event.clientX, y: event.clientY, primary: event.isPrimary, dragged: false});
    canvas.setPointerCapture(event.pointerId);
  });
  canvas.addEventListener('pointermove', event => {
    const pointer = pointers.get(event.pointerId);
    if (!pointer) return;
    event.preventDefault();
    pointer.dragged ||= Math.hypot(event.clientX - pointer.x, event.clientY - pointer.y) >= 12;
    // Sliding off releases a pedal; only that original pedal can be re-entered.
    pointer.held = active && pointer.control >= 0 && pointer.control < 4
      && hit(position(event)) === pointer.control;
  });
  canvas.addEventListener('pointerup', event => {
    if (event.pointerType !== 'touch') return;
    event.preventDefault();
    const pointer = pointers.get(event.pointerId);
    pointers.delete(event.pointerId);
    if (!pointer) return;
    const dragged = pointer.dragged || Math.hypot(event.clientX - pointer.x, event.clientY - pointer.y) >= 12;
    if (pointer.control >= 0) {
      // Permission/fullscreen calls must run inside this real user gesture, not a C frame.
      if (!dragged && hit(position(event)) === pointer.control) action(pointer.control);
    } else if (pointer.primary && !dragged) {
      const point = position(event);
      tap = {x: point.x * canvas.width, y: point.y * canvas.height};
    }
  });
  for (const type of ['pointercancel', 'lostpointercapture'])
    canvas.addEventListener(type, event => { pointers.delete(event.pointerId); });
  canvas.addEventListener('contextmenu', event => event.preventDefault());
  // Also suppress Safari's long-press callout/gesture handling on the game surface.
  for (const type of ['touchstart', 'touchmove', 'gesturestart', 'gesturechange'])
    canvas.addEventListener(type, event => event.preventDefault(), {passive: false});
  window.addEventListener('deviceorientation', event => {
    if (!tilt || rotationBlocked || !Number.isFinite(event.beta) || !Number.isFinite(event.gamma)) return;
    receivedAt = now();
    const screenAngle = Number.isFinite(window.screen.orientation?.angle)
      ? window.screen.orientation.angle : (window.orientation || 0);
    const radians = screenAngle * Math.PI / 180;
    // Project gravity onto the screen's horizontal and vertical axes. Unlike raw
    // beta/gamma selection this remains continuous across Euler angle wrapping.
    const beta = event.beta * Math.PI / 180, gamma = event.gamma * Math.PI / 180;
    const x = Math.cos(beta) * Math.sin(gamma), y = Math.sin(beta);
    const horizontal = x * Math.cos(radians) + y * Math.sin(radians);
    const vertical = y * Math.cos(radians) - x * Math.sin(radians);
    if (Math.hypot(horizontal, vertical) < 0.15) {
      angle = null; target = 0; message('Raise phone slightly for tilt steering.'); return;
    }
    angle = Math.atan2(horizontal, Math.abs(vertical)) * 180 / Math.PI;
    if (center === null) center = angle;
    message('Tilt steering on');
    const delta = angle - center;
    const requested = Math.sign(delta) * Math.min(1, Math.max(0, Math.abs(delta) - 3) / 22);
    const time = now();
    const blend = last === null ? 1 : 1 - Math.exp(-Math.max(0, time - last) / 100);
    target += (requested - target) * blend;
    last = time;
  });
  function rotate() {
    clear(); if (tilt) { center = null; angle = null; message('Rotation changed. Tap Recenter.'); }
    // Calibration stays blocked until a deliberate Recenter gesture.
    rotationBlocked = tilt;
  }
  if (window.screen.orientation) window.screen.orientation.addEventListener('change', rotate);
  else window.addEventListener('orientationchange', rotate);
  function cancel() { clear(); tap = null; }
  window.addEventListener('blur', cancel);
  window.addEventListener('resize', () => { cancel(); regions = []; });
  document.addEventListener('visibilitychange', () => { if (document.hidden) cancel(); });
  module['boatMobile'] = {
    'cancel': cancel,
    'layout': value => { regions = value; },
    'regions': () => regions,
    'state': () => (session && mobile() ? 1 : 0) | (tilt ? 2 : 0) | (pending ? 4 : 0)
      | (module['boatCanFullscreen'] ? 8 : 0) | (document.fullscreenElement ? 16 : 0),
    'status': () => status,
    'click': () => {
      const value = tap; tap = null;
      return value || (touchInput ? false : null);
    },
    'session': value => {
      session = !!value; active = false; menu = false; tap = null; clear();
      regions = [];
    },
    'menu': () => { const value = menu; menu = false; return value; },
    'sample': enabled => {
      active = !!enabled && session && !document.hidden;
      if (!active) {
        for (const [id, pointer] of pointers)
          if (pointer.control >= 0 && pointer.control < 4) pointers.delete(id);
        target = 0; last = null;
      } else tap = null;
      if (tilt && !rotationBlocked && active && now() - (receivedAt ?? startedAt) > 2000)
        message('No tilt readings. Use touch or check motion settings.');
      const held = control => active && [...pointers.values()].some(p => p.control === control && p.held);
      const fresh = tilt && !rotationBlocked && angle !== null && last !== null && now() - last < 1000;
      module['boatMobileTarget'] = active && !held(2) && !held(3) && fresh ? target : NaN;
      // Bits: forward, reverse, left, right, mobile presentation owns steering.
      return (session && mobile() ? 16 : 0) | (held(0) ? 1 : 0) | (held(1) ? 2 : 0)
        | (held(2) ? 4 : 0) | (held(3) ? 8 : 0);
    },
    'target': () => module['boatMobileTarget']
  };
}
