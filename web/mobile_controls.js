// Browser-only controls. C consumes ordinary pedals and a wheel target, never sensor data.
function installBoatMobileControls(module, canvas, document, window) {
  const root = document.getElementById('mobile-controls');
  const status = document.getElementById('tilt-status');
  const tiltButton = document.getElementById('tilt');
  const recenter = document.getElementById('recenter');
  const pointers = new Map();
  let tap = null, touchStart = null, touchInput = false;
  canvas.addEventListener('pointerdown', event => {
    touchInput = event.pointerType === 'touch';
    if (touchInput && event.isPrimary) touchStart = {x: event.clientX, y: event.clientY};
  });
  canvas.addEventListener('pointerup', event => {
    if (event.pointerType !== 'touch' || !event.isPrimary || !touchStart) return;
    if (Math.hypot(event.clientX - touchStart.x, event.clientY - touchStart.y) < 12) {
      const bounds = canvas.getBoundingClientRect();
      tap = {x: (event.clientX - bounds.left) * canvas.width / bounds.width,
        y: (event.clientY - bounds.top) * canvas.height / bounds.height};
    }
    touchStart = null;
  });
  canvas.addEventListener('pointercancel', () => { touchStart = tap = null; });
  let session = false, active = false, menu = false, tilt = false, pending = false;
  let rotationBlocked = false;
  let center = null, angle = null, target = 0, last = null;
  const now = () => window.performance.now();
  const mobile = () => window.matchMedia('(pointer: coarse)').matches;
  function clear() { pointers.clear(); target = 0; last = null; }
  function message(text) { status.textContent = text; }
  function stopTilt(text) {
    tilt = false; center = null; clear();
    tiltButton.textContent = 'Enable tilt'; recenter.hidden = true; message(text);
  }
  function calibrate() {
    clear(); center = null; angle = null;
    message('Hold comfortably; waiting for tilt…');
  }
  tiltButton.addEventListener('click', () => {
    canvas.focus();
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
      rotationBlocked = false; tilt = true; tiltButton.textContent = 'Use touch'; recenter.hidden = false; calibrate();
    }).catch(() => stopTilt('Tilt unavailable. Use touch steering.'))
      .finally(() => { pending = false; });
  });
  recenter.addEventListener('click', () => { canvas.focus(); rotationBlocked = false; calibrate(); });
  root.querySelectorAll('[data-hold]').forEach(button => {
    button.addEventListener('pointerdown', event => {
      event.preventDefault(); canvas.focus();
      if (!session || !active) return;
      button.setPointerCapture(event.pointerId);
      pointers.set(event.pointerId, button.dataset.hold);
    });
    for (const type of ['pointerup', 'pointercancel', 'lostpointercapture'])
      button.addEventListener(type, event => { pointers.delete(event.pointerId); });
  });
  document.getElementById('mobile-menu').addEventListener('click', () => {
    canvas.focus(); clear(); if (session) menu = true;
  });
  window.addEventListener('deviceorientation', event => {
    if (!tilt || rotationBlocked || !Number.isFinite(event.beta) || !Number.isFinite(event.gamma)) return;
    const screenAngle = window.screen.orientation ? window.screen.orientation.angle
      : (window.orientation || 0);
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
    if (center === null) { center = angle; message('Tilt steering on'); }
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
  function cancel() { clear(); tap = touchStart = null; }
  window.addEventListener('blur', cancel);
  document.addEventListener('visibilitychange', () => { if (document.hidden) cancel(); });
  module['boatMobile'] = {
    'click': () => {
      const value = tap; tap = null;
      return value || (touchInput ? false : null);
    },
    'session': value => {
      session = !!value; active = false; menu = false; tap = null; clear();
      root.hidden = !session || !mobile();
    },
    'menu': () => { const value = menu; menu = false; return value; },
    'sample': enabled => {
      active = !!enabled && session && !document.hidden;
      if (!active) clear();
      else tap = null;
      root.querySelectorAll('[data-hold]').forEach(button => { button.disabled = !active; });
      const held = name => active && [...pointers.values()].includes(name);
      const fresh = tilt && !rotationBlocked && angle !== null && last !== null && now() - last < 1000;
      module['boatMobileTarget'] = active && !held('left') && !held('right') && fresh ? target : NaN;
      // Bits: forward, reverse, left, right, mobile presentation owns steering.
      return (session && mobile() ? 16 : 0) | (held('forward') ? 1 : 0) | (held('reverse') ? 2 : 0)
        | (held('left') ? 4 : 0) | (held('right') ? 8 : 0);
    },
    'target': () => module['boatMobileTarget']
  };
}
