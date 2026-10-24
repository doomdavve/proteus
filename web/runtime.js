// Included inside Emscripten's runtime so FS and callMain need no public JS API.
const playButton = document.getElementById('play');
const statusLine = document.getElementById('status');
const storageLine = document.getElementById('storage');
const canvas = Module['canvas'];
const stage = document.getElementById('stage');
let started = false;
installBoatMouseControls(Module, canvas, document, window);
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
  const location = document.getElementById('location').value;
  const args = location === 'course' ? ['--course'] : ['--location', location];
  args.push('--soundtrack', document.getElementById('soundtrack').value);
  if (document.getElementById('waves').checked) args.push('--waves');
  if (document.getElementById('aa').checked) args.push('--gpu-aa');
  for (const id of ['location', 'soundtrack', 'waves', 'aa']) document.getElementById(id).disabled = true;
  canvas.focus();
  // Audio initialization and PlayMusicStream happen inside this user gesture.
  callMain(args);
  fitCanvas();
});

canvas.addEventListener('click', () => canvas.focus());
canvas.addEventListener('contextmenu', event => event.preventDefault());
canvas.addEventListener('keydown', event => {
  if (['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', ' ', 'PageUp', 'PageDown', 'Home', 'End', 'F3'].includes(event.key))
    event.preventDefault();
});
window.addEventListener('resize', fitCanvas);
document.addEventListener('fullscreenchange', fitCanvas);
Module['onExit'] = code => {
  document.body.classList.remove('playing');
  if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
  statusLine.textContent = code ? 'The game could not start. Check WebGL 2 support and reload.'
    : 'Game closed. Reload the page to play again.';
};
fitCanvas();
