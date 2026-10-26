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
installBoatRtcTransport(Module);
installBoatServerDirectory(Module).refresh();
document.getElementById('fullscreen').disabled = !stage.requestFullscreen;

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
  const availableHeight = innerHeight;
  const scale = Math.max(1, Math.floor(Math.min(innerWidth / 320, availableHeight / 200)));
  // The fixed 960x600 buffer contains a 320x200 scene. Use whole scene-pixel scales.
  const width = 320 * scale;
  const height = 200 * scale;
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
  if (document.getElementById('fullscreen').checked && stage.requestFullscreen) {
    stage.requestFullscreen().catch(() => canvas.focus());
  }
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
document.addEventListener('fullscreenchange', fitCanvas);
Module['onExit'] = code => {
  document.body.classList.remove('playing');
  if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
  statusLine.textContent = code ? 'The game could not start. Check WebGL 2 support and reload.'
    : 'Reload to open the launcher again.';
  if (!code) FS.syncfs(false, () => window.location.reload());
};
fitCanvas();
