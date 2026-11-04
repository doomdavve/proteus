// Clipboard writes start in the key gesture, rather than the later Wasm frame (Safari/Firefox
// require user activation). Pasting uses the browser's paste event and never requests read access.
function installBoatConsoleControls(module, canvas, document, navigator) {
  let copyAll = false;
  const text = () => (copyAll ? module.boatConsoleTranscript : module.boatConsoleCopy) || '';
  const active = () => module.boatConsoleOpen && document.activeElement === canvas;
  const copied = () => { module.boatConsoleClipboardNotice = copyAll
    ? 'Copied scrollback' : 'Copied latest output'; };
  const failed = () => { module.boatConsoleClipboardNotice = 'Clipboard blocked by browser'; };
  canvas.addEventListener('copy', event => {
    if (!active() || !event.clipboardData) return;
    event.preventDefault();
    event.clipboardData.setData('text/plain', text());
    copied();
  });
  canvas.addEventListener('paste', event => {
    if (!active() || !event.clipboardData) return;
    event.preventDefault();
    // The C editor validates length/ASCII/newlines atomically and waits for Enter to submit.
    module.boatConsolePaste = event.clipboardData.getData('text/plain');
  });
  canvas.addEventListener('keydown', event => {
    if (!active()) return;
    if (event.key === 'Tab') {
      event.preventDefault();
      if (!event.repeat) {
        const tabs = module.boatConsoleTabs ||= [];
        if (tabs.length < 16) tabs.push(event.shiftKey ? -1 : 1);
      }
      return;
    }
    const shortcut = (event.ctrlKey || event.metaKey) && !event.altKey;
    if (shortcut && event.key.toLowerCase() === 'c') {
      event.preventDefault();
      if (event.repeat) return;
      copyAll = !!event.shiftKey;
      const value = text(), all = copyAll;
      if (!value) { module.boatConsoleClipboardNotice = 'Nothing to copy yet'; return; }
      try {
        if (navigator.clipboard && navigator.clipboard.writeText) {
          Promise.resolve(navigator.clipboard.writeText(value)).then(() => {
            module.boatConsoleClipboardNotice = all ? 'Copied scrollback' : 'Copied latest output';
          }, failed);
        } else if (!document.execCommand('copy')) failed();
      } catch (_) { failed(); }
      return;
    }
    // Keep Ctrl/Cmd+V's native paste event and printable keypresses. Suppress page navigation
    // and browser shortcuts for the keys owned by the editor.
    if (['Backspace', 'Delete', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight',
         'PageUp', 'PageDown', 'Home', 'End', 'F2', 'Tab'].includes(event.key)) event.preventDefault();
  });
}
