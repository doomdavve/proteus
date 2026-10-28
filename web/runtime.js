// Included inside Emscripten's runtime so FS and callMain need no public JS API.
const playButton = document.getElementById('play');
const statusLine = document.getElementById('status');
const storageLine = document.getElementById('storage');
const canvas = Module['canvas'];
const stage = document.getElementById('stage');
let started = false;
const fullscreenOption = document.getElementById('fullscreen');
try {
  fullscreenOption.checked = localStorage.getItem('wake-velocity-fullscreen') !== 'off';
} catch (_) {}
fullscreenOption.addEventListener('change', () => {
  try { localStorage.setItem('wake-velocity-fullscreen', fullscreenOption.checked ? 'on' : 'off'); } catch (_) {}
});
installBoatMouseControls(Module, canvas, document, window);
installBoatMobileControls(Module, canvas, document, window);
installBoatRtcTransport(Module);
installBoatServerDirectory(Module).refresh();
const canFullscreen = !!stage.requestFullscreen && document.fullscreenEnabled !== false;
fullscreenOption.disabled = !canFullscreen;
const screenButton = document.getElementById('screen-button');
screenButton.hidden = !canFullscreen;
function enterFullscreen() {
  if (canFullscreen) stage.requestFullscreen().catch(() => {});
  canvas.focus();
}
screenButton.addEventListener('click', enterFullscreen);
document.getElementById('iphone-tip').hidden = !(/iPhone|iPod/.test(navigator.userAgent))
  || window.matchMedia('(display-mode: standalone)').matches || navigator.standalone;

Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(() => {
  // The native XDG path resolver will use /saves/proteus. Restore before main reads it.
  ENV['XDG_DATA_HOME'] = '/saves';
  FS.mkdir('/saves');
  FS.mount(IDBFS, { autoPersist: true }, '/saves');
  addRunDependency('saved-games');
  FS.syncfs(true, error => {
    if (error) {
      console.warn('Saved games unavailable:', error);
      storageLine.textContent = 'Browser storage is unavailable. Best times may be lost on reload.';
    }
    removeRunDependency('saved-games');
  });
});

Module['onRuntimeInitialized'] = () => {
  playButton.disabled = false;
  playButton.textContent = 'PLAY';
  statusLine.textContent = '';
};

function fitCanvas() {
  const viewport = window.visualViewport;
  stage.style.width = (viewport ? viewport.width : innerWidth) + 'px';
  stage.style.height = (viewport ? viewport.height : innerHeight) + 'px';
  stage.style.left = (viewport ? viewport.offsetLeft : 0) + 'px';
  stage.style.top = (viewport ? viewport.offsetTop : 0) + 'px';
  const style = getComputedStyle(stage);
  const availableHeight = (viewport ? viewport.height : innerHeight)
    - parseFloat(style.paddingTop) - parseFloat(style.paddingBottom);
  const availableWidth = (viewport ? viewport.width : innerWidth)
    - parseFloat(style.paddingLeft) - parseFloat(style.paddingRight);
  const scale = Math.max(1, Math.floor(Math.min(availableWidth / 320, availableHeight / 200)));
  const mobile = window.matchMedia('(pointer: coarse)').matches;
  // Mobile fills the visible viewport with controls over the game. Stretch the
  // presentation instead of cropping the HUD or reserving space for controls.
  // Desktop retains whole scene-pixel scales for the 320x200 scene.
  const width = mobile ? (viewport ? viewport.width : innerWidth) : 320 * scale;
  const height = mobile ? (viewport ? viewport.height : innerHeight) : 200 * scale;
  canvas.style.width = width + 'px';
  canvas.style.height = height + 'px';
}

playButton.addEventListener('click', () => {
  if (started) return;
  started = true;
  Module['boatGameStarted'] = true;
  playButton.disabled = true;
  document.body.classList.add('playing');
  fitCanvas();
  if (fullscreenOption.checked) enterFullscreen();
  canvas.focus();
  // Audio initialization and PlayMusicStream happen inside this user gesture.
  callMain([]);
  fitCanvas();
});

canvas.addEventListener('click', () => canvas.focus());
canvas.addEventListener('contextmenu', event => event.preventDefault());
canvas.addEventListener('keydown', event => {
  if (['Backspace', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', ' ', 'PageUp', 'PageDown', 'Home', 'End', 'F3'].includes(event.key))
    event.preventDefault();
});
window.addEventListener('resize', fitCanvas);
if (window.visualViewport) {
  window.visualViewport.addEventListener('resize', fitCanvas);
  window.visualViewport.addEventListener('scroll', fitCanvas);
}
document.addEventListener('fullscreenchange', fitCanvas);
Module['onExit'] = code => {
  document.body.classList.remove('playing');
  if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
  statusLine.textContent = code ? 'The game could not start. Check WebGL 2 support and reload.'
    : 'Reload to open the launcher again.';
  if (!code) FS.syncfs(false, () => window.location.reload());
};
fitCanvas();
