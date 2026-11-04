#!/usr/bin/env node
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const context = vm.createContext({Promise});
vm.runInContext(fs.readFileSync(path.join(__dirname, '../web/console_controls.js'), 'utf8'), context);
function fixture(clipboard = {writeText: async value => { writes.push(value); }}) {
  const listeners = {}, canvas = {addEventListener: (name, cb) => { listeners[name] = cb; }};
  const module = {boatConsoleOpen: true, boatConsoleCopy: '> /status\nR1\n',
    boatConsoleTranscript: 'welcome\n> /status\nR1\n'};
  const document = {activeElement: canvas, execCommand() {
    emit('copy', {clipboardData: {setData: (_, value) => writes.push(value)}}); return true;
  }};
  function emit(name, values) {
    const event = {preventDefault() { this.prevented = true; }, ...values};
    listeners[name](event); return event;
  }
  context.installBoatConsoleControls(module, canvas, document, {clipboard});
  return {module, document, canvas, emit};
}
const writes = [];
(async () => {
  const f = fixture();
  assert.ok(f.emit('keydown', {key: 'c', ctrlKey: true}).prevented);
  await Promise.resolve();
  assert.equal(writes.pop(), f.module.boatConsoleCopy);
  assert.equal(f.module.boatConsoleClipboardNotice, 'Copied latest output');
  f.emit('keydown', {key: 'C', metaKey: true, shiftKey: true});
  await Promise.resolve();
  assert.equal(writes.pop(), f.module.boatConsoleTranscript);
  assert.equal(f.module.boatConsoleClipboardNotice, 'Copied scrollback');
  const v = f.emit('keydown', {key: 'v', metaKey: true});
  assert.ok(!v.prevented, 'native paste event must remain available');
  assert.ok(f.emit('paste', {clipboardData: {getData: () => '/status\n/restart'}}).prevented);
  assert.equal(f.module.boatConsolePaste, '/status\n/restart', 'C rejects multiline paste atomically');
  for (const key of ['ArrowLeft', 'Home', 'End', 'Backspace', 'PageDown', 'F2', 'Tab'])
    assert.ok(f.emit('keydown', {key}).prevented);
  assert.deepEqual(Array.from(f.module.boatConsoleTabs), [1]);
  f.emit('keydown', {key: 'Tab', shiftKey: true});
  f.emit('keydown', {key: 'Tab', repeat: true});
  assert.deepEqual(Array.from(f.module.boatConsoleTabs), [1, -1],
    'rapid Tab presses retain order and each event modifier; repeats do not flood the queue');
  for (const key of [' ', 'a', '/', '=']) assert.ok(!f.emit('keydown', {key}).prevented);
  f.document.activeElement = null;
  assert.ok(!f.emit('paste', {clipboardData: {getData() { throw Error('unfocused'); }}}).prevented);
  f.document.activeElement = f.canvas;
  f.module.boatConsoleOpen = false;
  assert.ok(!f.emit('keydown', {key: 'Home'}).prevented);
  f.emit('keydown', {key: 'c', ctrlKey: true});
  assert.equal(writes.length, 0, 'closed console must not own clipboard shortcuts');
  const fallback = fixture(null);
  fallback.emit('keydown', {key: 'c', ctrlKey: true});
  assert.equal(writes.pop(), fallback.module.boatConsoleCopy);
  const denied = fixture({writeText: () => Promise.reject(Error('denied'))});
  denied.emit('keydown', {key: 'c', ctrlKey: true});
  await Promise.resolve();
  assert.equal(denied.module.boatConsoleClipboardNotice, 'Clipboard blocked by browser');
  denied.module.boatConsoleCopy = '';
  denied.emit('keydown', {key: 'c', ctrlKey: true});
  assert.equal(denied.module.boatConsoleClipboardNotice, 'Nothing to copy yet');
  console.log('web_console_controls=ok');
})().catch(error => { console.error(error); process.exitCode = 1; });
