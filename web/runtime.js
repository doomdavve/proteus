// Included inside Emscripten's runtime so FS and callMain need no public JS API.
const playButton = document.getElementById('play');
const statusLine = document.getElementById('status');
const storageLine = document.getElementById('storage');
const canvas = Module['canvas'];
const stage = document.getElementById('stage');
let started = false;
installBoatMouseControls(Module, canvas, document, window);
installBoatMobileControls(Module, canvas, document, window);
installBoatRtcTransport(Module);
installBoatServerDirectory(Module).refresh();
const canFullscreen = !!stage.requestFullscreen && document.fullscreenEnabled !== false;
const screenButton = document.getElementById('screen-button');
screenButton.hidden = !canFullscreen;
function enterFullscreen() {
  if (canFullscreen) {
    try {
      const request = document.fullscreenElement
        ? document.exitFullscreen() : stage.requestFullscreen({ navigationUI: 'hide' });
      Promise.resolve(request).catch(() => {});
    } catch (_) {} // Fullscreen refusal leaves touch play available in the tab.
  }
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
  if (Module.boatPluginAuthorReady) Module.boatPluginAuthorReady();
};

let viewportSize = {width: 320, height: 200};
let candidateSize = viewportSize, candidateSince = 0;
Module['boatViewportSize'] = () => {
  // Avoid reallocating GPU targets at every animation frame of browser-bar movement.
  if (performance.now() - candidateSince >= 150) viewportSize = candidateSize;
  return viewportSize;
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
  const viewportWidth = viewport ? viewport.width : innerWidth;
  const viewportHeight = viewport ? viewport.height : innerHeight;
  // Keep at least 320x200 logical pixels so menus and instruments fit naturally.
  // Cap the long side to bound memory and supersampling cost on mobile GPUs.
  const aspect = Math.max(1 / 3, Math.min(3, viewportWidth / viewportHeight));
  const next = mobile ? (aspect >= 1.6
    ? {width: Math.round(200 * aspect), height: 200}
    : {width: 320, height: Math.round(320 / aspect)}) : {width: 320, height: 200};
  if (next.width !== candidateSize.width || next.height !== candidateSize.height) {
    candidateSize = next;
    candidateSince = performance.now();
  }
  // Preserve proportions even while a resize is settling, and on the fixed-size title.
  const mobileScale = Math.min(viewportWidth / canvas.width, viewportHeight / canvas.height);
  const width = mobile ? canvas.width * mobileScale : 320 * scale;
  const height = mobile ? canvas.height * mobileScale : 200 * scale;
  canvas.style.width = width + 'px';
  canvas.style.height = height + 'px';
}

function startSession(args) {
  if (started) return;
  started = true;
  Module['boatGameStarted'] = true;
  playButton.disabled = true;
  document.body.classList.add('playing');
  fitCanvas();
  canvas.focus();
  // Audio initialization and PlayMusicStream happen inside this user gesture.
  callMain(args);
  fitCanvas();
}
playButton.addEventListener('click', () => startSession([]));
installBoatPluginAuthor(Module, Module.boatWebFiles, () => FS, startSession, document, window);

canvas.addEventListener('click', () => canvas.focus());
canvas.addEventListener('contextmenu', event => event.preventDefault());
canvas.addEventListener('keydown', event => {
  // The live console reads text through GetCharPressed; preventDefault on keydown suppresses the
  // keypress that carries a space or another printable character.
  if (event.key === 'F11') {
    event.preventDefault();
    if (!event.repeat) enterFullscreen();
    return;
  }
  if (Module.boatConsoleOpen) return;
  if (['Backspace', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', ' ', 'PageUp', 'PageDown', 'Home', 'End', 'F3', 'F5', 'F6', 'F7'].includes(event.key))
    event.preventDefault();
});
Module['boatFitCanvas'] = fitCanvas;
window.addEventListener('resize', fitCanvas);
if (window.visualViewport) {
  window.visualViewport.addEventListener('resize', fitCanvas);
  window.visualViewport.addEventListener('scroll', fitCanvas);
}
document.addEventListener('fullscreenchange', () => {
  screenButton.textContent = document.fullscreenElement ? 'Exit fullscreen' : 'Fullscreen';
  Module['boatMobile']['cancel']();
  fitCanvas();
});
Module['onExit'] = code => {
  document.body.classList.remove('playing');
  if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
  statusLine.textContent = code ? 'The game could not start. Check WebGL 2 support and reload.'
    : 'Reload to open the launcher again.';
  if (!code) FS.syncfs(false, () => window.location.reload());
};
fitCanvas();
