#!/usr/bin/env node
'use strict';
const assert = require('node:assert/strict');
const {bundle, decode, encode, frame, Transport, message, scenes, WIRE_MAGIC, packImage, unpackImage,
  imageIs, sourceBytes, fromWasm, editModules, writePackage, restoreSaved, moduleNameValid,
  moduleFile} = require('../../web/plugin_author.js');
const {ObjectiveTransport} = require('../../web/plugin_objective.js');
const fs = require('node:fs');
const path = require('node:path');

function response(status = 0) {
  const bytes = new Uint8Array(8), view = new DataView(bytes.buffer);
  view.setUint32(0, WIRE_MAGIC); view.setUint32(4, status);
  return {exit: status ? 1 : 0, bytes};
}
function fixture() {
  const workers = [], timers = new Map(); let serial = 0;
  const transport = new Transport({
    makeWorker() {
      const worker = {terminated: false, sent: null,
        terminate() { this.terminated = true; }, postMessage(value) { this.sent = value; }};
      workers.push(worker); return worker;
    },
    timer(callback) { timers.set(++serial, callback); return serial; },
    clear(id) { timers.delete(id); }
  });
  return {transport, workers, timers};
}
const decoder = new TextDecoder(), encoder = new TextEncoder();
const hex = bytes => Buffer.from(bytes).toString('hex');
function moduleTests(root) {
  const fixture = path.join(root, 'tests/support/fixtures');
  // Golden vector: the C packer (validate_boat_plugin_modules), tools/plugin_author.py and this
  // file pack the same three files to the same bytes.
  const golden = fs.readFileSync(path.join(fixture, 'plugin_image_golden.hex'), 'utf8').trim();
  const files = [['main.lua', 'return {}\n'], ['marina', "return 'G\u00f6teborg'\n"],
    ['marina.berths', 'return {}\n']].map(([name, text]) => ({name, data: encoder.encode(text)}));
  const image = packImage(files);
  assert.equal(hex(image), golden);
  assert(imageIs(image)); assert(!imageIs(encoder.encode('return {}')));
  assert.deepEqual(unpackImage(image).map(f => [f.name, decoder.decode(f.data)]),
    files.map(f => [f.name, decoder.decode(f.data)]));
  const patch = (at, value) => { const copy = image.slice(); copy[at] = value; return copy; };
  for (const bad of [image.slice(0, -1), Uint8Array.of(...image, 0), patch(0, 1), patch(5, 0),
    patch(5, 10), patch(6, 0), patch(6, 33)])
    assert.throws(() => unpackImage(bad), /malformed package source image/);
  assert.throws(() => packImage([{name: 'a', data: new Uint8Array()}, {name: 'a', data: new Uint8Array()}]));

  const dir = path.join(fixture, 'plugins/modules-route');
  const manifest = fs.readFileSync(path.join(dir, 'plugin.json'), 'utf8');
  const main = fs.readFileSync(path.join(dir, 'main.lua'), 'utf8');
  const modules = {route: fs.readFileSync(path.join(dir, 'modules/route.lua'), 'utf8'),
    'route.arrival': fs.readFileSync(path.join(dir, 'modules/route/arrival.lua'), 'utf8')};
  const data = bundle(manifest, main, modules);
  assert.equal(data.version, 2); assert.deepEqual(Object.keys(data.modules), ['route', 'route.arrival']);
  assert.deepEqual(decode(encode(data)), data);
  // The source the worker receives is the image in the manifest's order, modules after the entry.
  const framed = frame(data), view = new DataView(framed.buffer);
  const source = framed.slice(8 + view.getUint32(0));
  assert(imageIs(source)); assert.equal(source.length, view.getUint32(4));
  assert.deepEqual(unpackImage(source).map(f => f.name), ['main.lua', 'route', 'route.arrival']);
  assert.deepEqual(sourceBytes(manifest, main, modules), source);
  // Export reads the image back, the same bundle (/save passes manifest and image bytes).
  assert.deepEqual(fromWasm(encoder.encode(manifest), source), data);
  // Bundle key order does not matter; the manifest's order does.
  const swapped = {...JSON.parse(encode(data)), modules: {'route.arrival': modules['route.arrival'], route: modules.route}};
  assert.deepEqual(Object.keys(decode(JSON.stringify(swapped)).modules), ['route', 'route.arrival']);
  assert.deepEqual(frame(decode(JSON.stringify(swapped))), framed);
  // Consistency errors name the module file or the manifest field.
  assert.throws(() => bundle(manifest, main, {route: ''}), /declares module "route.arrival" but modules\/route\/arrival.lua is missing/);
  assert.throws(() => bundle(manifest, main, {...modules, extra: ''}), /holds "extra", which plugin.json does not declare/);
  assert.throws(() => bundle(manifest, main, 'x'), /Expected module text/);
  assert.throws(() => bundle(manifest, main, {route: 1, 'route.arrival': ''}), /Expected module text/);
  assert.throws(() => bundle('{', main, {route: ''}), /not valid JSON/);
  assert.throws(() => fromWasm(encoder.encode(manifest), encoder.encode(main)), /declares 2 modules but the package source is not a module image/);
  assert.throws(() => fromWasm(encoder.encode(manifest.replace(/,\s*"modules".*\]/s, '')), source), /declares no modules but the package source is a module image/);
  assert.throws(() => fromWasm(encoder.encode(manifest.replace('["route", "route.arrival"]', '["route.arrival", "route"]')), source), /names and order must agree/);
  const withModules = list => JSON.stringify({...JSON.parse(manifest), modules: list});
  for (const [list, message] of [[['a', 'a'], /modules\[2\] repeats modules\[1\]/], [['Bad'], /modules\[1\] must be 1-3 dot-separated/],
    [['main'], /must not be "main"/], [['a.b.c.d'], /dot-separated/], [['a'.repeat(33)], /at most 32 bytes/],
    [Array.from({length: 9}, (_, i) => 'm' + i), /at most 8 module names/], ['x', /must be an array/]])
    assert.throws(() => bundle(withModules(list), main, {}), message);
  const eight = Array.from({length: 8}, (_, i) => 'm' + i);
  assert.equal(Object.keys(bundle(withModules(eight), main, Object.fromEntries(eight.map(n => [n, 'return 1'])))
    .modules).length, 8);
  // Budgets: entry, modules and framing share 64 KiB, as boat_plugin_read counts them.
  const m1 = withModules(['m']);
  const framing = 6 + (1 + 8 + 4) + (1 + 1 + 4), edge = 65536 - framing - main.length;
  assert.equal(sourceBytes(m1, main, {m: 'x'.repeat(edge)}).length, 65536);
  assert.throws(() => sourceBytes(m1, main, {m: 'x'.repeat(edge + 1)}),
    new RegExp(`modules/m.lua is larger than the ${edge} bytes left of the 65536-byte source budget`));
  assert.throws(() => sourceBytes(m1, 'x'.repeat(65515), {m: ''}), /modules\/m.lua is larger than the 0 bytes/);
  assert.throws(() => sourceBytes(m1, 'x'.repeat(65536), {m: ''}), /main.lua is larger than/);
  assert.throws(() => bundle(manifest, main, {route: 'é'.repeat(32768), 'route.arrival': 'é'.repeat(32768)}), /larger than/);
  // Names and files.
  assert(moduleNameValid('a.b_2.c')); assert(!moduleNameValid('a.b.c.d')); assert(!moduleNameValid('1a'));
  assert.equal(moduleFile('a.b'), 'modules/a/b.lua');
  // plugin.json edits made by the module controls.
  const added = JSON.parse(editModules(manifest, 'extra', true));
  assert.deepEqual(added.modules, ['route', 'route.arrival', 'extra']);
  assert.deepEqual(JSON.parse(editModules(JSON.stringify(added), 'route', false)).modules, ['route.arrival', 'extra']);
  assert.equal('modules' in JSON.parse(editModules(editModules('{"a": 1}', 'x', true), 'x', false)), false);
  assert.throws(() => editModules('{', 'x', true), /not valid JSON/);
  // Staging writes the package, replacing the modules of an earlier staging.
  const tree = new Map([['/plugins/browser/modules/stale/old.lua', 'x']]), dirs = new Set(['/plugins/browser/modules', '/plugins/browser/modules/stale']);
  const fakeFs = {
    analyzePath: p => ({exists: dirs.has(p)}),
    readdir: p => ['.', '..', ...[...tree.keys(), ...dirs].filter(k => k.startsWith(p + '/') && !k.slice(p.length + 1).includes('/')).map(k => k.slice(p.length + 1))],
    stat: p => ({mode: dirs.has(p) ? 1 : 0}), isDir: mode => mode === 1,
    unlink: p => tree.delete(p), rmdir: p => dirs.delete(p),
    mkdirTree: p => { let at = ''; for (const part of p.split('/').slice(1)) dirs.add(at += '/' + part); },
    writeFile: (p, bytes) => tree.set(p, decoder.decode(bytes))};
  writePackage(fakeFs, data);
  assert.deepEqual([...tree.keys()].sort(), ['/plugins/browser/main.lua', '/plugins/browser/modules/route.lua',
    '/plugins/browser/modules/route/arrival.lua', '/plugins/browser/plugin.json']);
  assert.equal(tree.get('/plugins/browser/modules/route/arrival.lua'), modules['route.arrival']);
  writePackage(fakeFs, bundle(original(), main, {}));
  assert.deepEqual([...tree.keys()].sort(), ['/plugins/browser/main.lua', '/plugins/browser/plugin.json']);
  // A saved draft restores with its modules; a stale version 1 save is refused by name, not thrown.
  assert.deepEqual(restoreSaved(encode(data)).data, data);
  const v1 = JSON.stringify({format: 'wake-velocity-plugin', version: 1, manifest, source: main});
  assert.deepEqual(restoreSaved(v1), {data: null, error: 'version 1 bundles are not supported; re-export'});
  assert.equal(restoreSaved('{').data, null); assert.ok(restoreSaved('{').error);
  assert.deepEqual(restoreSaved(null), {data: null, error: null});
  editorTests(manifest, main, modules, data);
  function original() { return manifest.replace(/,\s*"modules".*\]/s, ''); }
}
// The editor wiring over a stub document: restore, module selection, add and remove.
function editorTests(manifest, main, modules, data) {
  const {install} = require('../../web/plugin_author.js');
  const elements = {};
  const element = () => ({value: '', _text: '', get textContent() { return this._text; },
    set textContent(text) { this._text = text; this.children.length = 0; }, children: [], handlers: {}, disabled: false,
    addEventListener(name, fn) { this.handlers[name] = fn; }, appendChild(child) { this.children.push(child); },
    });
  const document = {getElementById: id => (elements[id] ||= element()), body: {classList: {add() {}}},
    createElement: () => element()};
  const run = saved => {
    for (const key of Object.keys(elements)) delete elements[key];
    const window = {localStorage: {getItem: () => saved, setItem() {}}, addEventListener() {}, location: {href: 'http://x/'}};
    globalThis.BoatPluginObjectiveTransport = require('../../web/plugin_objective.js').ObjectiveTransport;
    install({}, {}, () => null, () => {}, document, window);
    return elements;
  };
  let ui = run(encode(data));
  assert.equal(ui['plugin-manifest'].value, manifest); assert.equal(ui['plugin-source'].value, main);
  assert.deepEqual(ui['plugin-module'].children.map(o => o.value), ['route', 'route.arrival']);
  assert.equal(ui['plugin-module-source'].value, modules.route);
  assert.equal(ui['plugin-module-file'].textContent, 'modules/route.lua');
  ui['plugin-module'].value = 'route.arrival'; ui['plugin-module'].onchange();
  assert.equal(ui['plugin-module-source'].value, modules['route.arrival']);
  ui['plugin-module-source'].value = '-- changed\nreturn {}\n';
  ui['plugin-module-name'].value = 'extra'; ui['plugin-module-add'].onclick();
  assert.deepEqual(JSON.parse(ui['plugin-manifest'].value).modules, ['route', 'route.arrival', 'extra']);
  assert.deepEqual(ui['plugin-module'].children.map(o => o.value), ['route', 'route.arrival', 'extra']);
  ui['plugin-module-name'].value = 'Bad'; ui['plugin-module-add'].onclick();
  assert.match(ui['plugin-status'].textContent, /dot-separated/);
  ui['plugin-module-name'].value = 'route'; ui['plugin-module-add'].onclick();
  assert.match(ui['plugin-status'].textContent, /already exists/);
  ui['plugin-module'].value = 'extra'; ui['plugin-module'].onchange();
  ui['plugin-module-remove'].onclick();
  assert.deepEqual(JSON.parse(ui['plugin-manifest'].value).modules, ['route', 'route.arrival']);
  // The edit to route.arrival was kept across selections and removals.
  ui['plugin-module'].value = 'route.arrival'; ui['plugin-module'].onchange();
  assert.equal(ui['plugin-module-source'].value, '-- changed\nreturn {}\n');
  const v1 = JSON.stringify({format: 'wake-velocity-plugin', version: 1, manifest, source: main});
  ui = run(v1);
  assert.match(ui['plugin-storage'].textContent, /version 1 bundles are not supported; re-export/);
  assert.equal(ui['plugin-manifest'].value, '');
}
async function main() {
  const root = path.resolve(__dirname, '../..');
  const original = bundle(fs.readFileSync(path.join(root, 'plugins/langedrag/plugin.json'), 'utf8'),
    fs.readFileSync(path.join(root, 'plugins/langedrag/main.lua'), 'utf8'));
  assert.deepEqual(decode(encode(original)), original);
  // Imported scenes and solo kinds share the worker wire; room kinds are rejected up front.
  const sceneManifest = original.manifest.replace('["voyage.define"]',
    '["voyage.define", "scene.define"]');
  assert.equal(scenes(sceneManifest), true);
  assert.ok(bundle(sceneManifest, original.source));
  assert.deepEqual(decode(encode({...original, manifest: sceneManifest})).manifest, sceneManifest);
  assert.throws(() => bundle(original.manifest.replace('"kind": "voyage"', '"kind": "race"'),
    original.source), /room content/);
  assert.throws(() => bundle(original.manifest.replace('"kind": "voyage"', '"kind": "match"'),
    original.source), /room content/);
  assert.throws(() => encode(bundle('', '\u0001'.repeat(65536))));
  const framed = frame(original), view = new DataView(framed.buffer);
  assert.equal(view.getUint32(0), Buffer.byteLength(original.manifest));
  assert.equal(view.getUint32(4), Buffer.byteLength(original.source));
  assert.equal(new TextDecoder().decode(framed.slice(8, 8 + view.getUint32(0))), original.manifest);
  for (const invalid of ['null', '[]', '{}', '{', JSON.stringify({...original, version: 3}),
    JSON.stringify({...original, version: 1}), JSON.stringify((({modules, ...rest}) => rest)(original)),
    JSON.stringify({...original, extra: 'value'}), 'x'.repeat(200001)]) {
    assert.throws(() => decode(invalid));
  }
  assert.equal(original.version, 2); assert.deepEqual(original.modules, {});
  assert.throws(() => decode(JSON.stringify({format: 'wake-velocity-plugin', version: 1,
    manifest: original.manifest, source: original.source})), /version 1 bundles are not supported; re-export/);
  moduleTests(root);
  assert.throws(() => bundle('x'.repeat(4097), ''));
  assert.throws(() => bundle('', 'é'.repeat(32769)));
  assert.equal(bundle('x'.repeat(4096), 'x'.repeat(65536)).source.length, 65536);
  assert.deepEqual(decode(JSON.stringify(bundle('é', '\u0000\n\\"'))), bundle('é', '\u0000\n\\"'));

  const {transport: t, workers, timers} = fixture(), base = [2, 3, 0];
  const id = t.start(framed, base);
  const sent = workers[0].sent.bytes;
  assert.notEqual(sent, framed); assert.deepEqual(sent, framed);
  assert.equal(t.status(id), 0); assert.equal(timers.size, 1);
  workers[0].onmessage({data: response()});
  assert.equal((await t.wait(id)).state, 1);
  assert.equal(timers.size, 0); assert(workers[0].terminated);
  t.prepare(framed, base, t.read(id)); t.close(id);
  const cached = t.start(framed, base);
  assert.equal(t.status(cached), 1); assert.equal(workers.length, 1);
  t.close(cached);
  const again = t.start(framed, base); // Receipt was consumed, so a fresh request is required.
  assert.equal(t.status(again), 0); assert.equal(workers.length, 2);
  const late = workers[1].onmessage;
  t.close(again); late({data: response()});
  assert.equal(t.status(again), -1); assert.equal(t.jobs.size, 0); assert(workers[1].terminated);

  t.prepare(framed, base, response().bytes);
  const stale = t.start(framed, [2, 4, 0]); // Same revision, new epoch rejects.
  assert.equal(t.status(stale), -1); assert.equal(workers.length, 2); t.close(stale);
  const deadline = t.start(framed, base);
  const expire = [...timers.values()][0]; expire(); // Virtual deadline; no clock/sleep.
  assert.equal((await t.wait(deadline)).state, -1); assert(workers[2].terminated);
  workers[2].onmessage({data: response()}); assert.equal(t.status(deadline), -1); t.close(deadline);
  for (const bad of [{exit: 0, bytes: new Uint8Array(4097)}, {exit: 0, bytes: new Uint8Array(8)},
    {exit: 7, bytes: response().bytes}, {exit: 1, bytes: response().bytes}, null]) {
    const request = t.start(framed, base);
    workers.at(-1).onmessage({data: bad}); assert.equal(t.status(request), -1); t.close(request);
  }
  const rejected = t.start(framed, base);
  workers.at(-1).onmessage({data: response(3)}); assert.equal(t.status(rejected), 2); t.close(rejected);
  const wrongWire = response(), wrongView = new DataView(wrongWire.bytes.buffer);
  wrongView.setUint32(0, 0x50535436);
  const unrecognised = t.start(framed, base);
  workers.at(-1).onmessage({data: wrongWire});
  assert.equal(t.status(unrecognised), -1);
  assert.match((await t.wait(unrecognised)).failure, /unrecognised result/);
  t.close(unrecognised);
  const cap1 = t.start(framed, base), cap2 = t.start(framed, base);
  assert.equal(t.start(framed, base), 0); t.close(cap1); t.close(cap2);
  const failed = t.start(framed, base);
  workers.at(-1).onerror(); assert.equal(t.status(failed), -1); t.close(failed);
  assert.equal(t.jobs.size, 0); assert.equal(timers.size, 0);
  assert.equal(message(new Uint8Array()), 'Worker replied with an unrecognised wire version');
  assert.equal(t.start(new Uint8Array(131073), base), 0);
  // M2 maximum scenes retain full source in the reply; the old 16 KiB cap rejected them.
  for (const size of [16385, 163840, 163841]) {
    const request = t.start(framed, base);
    const bytes = new Uint8Array(size);
    bytes.set(response().bytes);
    workers.at(-1).onmessage({data: {exit: 0, bytes}});
    assert.equal(t.status(request), size <= 163840 ? 1 : -1);
    t.close(request);
  }
  // Objective transitions reuse Wasm, but never share an outstanding callback or its result.
  const ow = [], otimers = new Map(); let onow = 0, timerId = 0;
  const o = new ObjectiveTransport({now: () => onow,
    makeWorker: () => { const w = {terminate() { this.dead = true; },
      postMessage(data) { this.data = data; }}; ow.push(w); return w; },
    timer: fn => { otimers.set(++timerId, fn); return timerId; }, clear: id => otimers.delete(id)});
  const ob = new Uint8Array(8); new DataView(ob.buffer).setUint32(0, 0x50455231);
  const payload = new Uint8Array(65536);
  const first = o.start(payload);
  assert.notEqual(ow[0].data.bytes, payload);
  assert.equal(o.start(payload), 0);
  const retiredTimer = [...otimers.values()][0];
  onow = 20;
  ow[0].onmessage({data: {id: first, bytes: ob, wasmBytes: 16777216}});
  assert.equal(o.status(first), 1); assert.equal(o.wasmBytes, 16777216);
  o.close(first); assert(!ow[0].dead); assert.equal(otimers.size, 0);
  const second = o.start(payload); assert.equal(ow.length, 1);
  retiredTimer(); assert.equal(o.status(second), 0);
  ow[0].onmessage({data: {id: second, bytes: ob}}); o.close(second);
  assert.equal(o.completed, 2);
  const timed = o.start(payload), oldMessage = ow[0].onmessage;
  onow += 5000; // Held response after tab suspension must expire even before timer delivery.
  oldMessage({data: {id: timed, bytes: ob}});
  assert.equal(o.status(timed), -1); assert(ow[0].dead); o.close(timed);
  const next = o.start(payload); oldMessage({data: {id: timed, bytes: ob}});
  assert.equal(o.status(next), 0); assert.equal(ow.length, 2);
  o.close(next); assert(ow[1].dead);
  const wrong = o.start(payload);
  ow[2].onmessage({data: {id: wrong + 1, bytes: ob}});
  assert.equal(o.status(wrong), -1); assert(ow[2].dead);
  const malformed = o.start(payload);
  ow[3].onmessage({data: {id: malformed, bytes: new Uint8Array(49153)}});
  assert.equal(o.status(malformed), -1); o.close(malformed);
  const crash = o.start(payload); ow[4].onerror();
  assert.equal(o.status(crash), -1); o.close(crash);
  const cancel = o.start(payload); o.reset();
  assert.equal(o.status(cancel), -1); assert.equal(otimers.size, 0);
  assert.equal(o.start(new Uint8Array(131073)), 0);
  console.log('web_plugin_author=ok');
}
main().catch(error => { console.error(error); process.exitCode = 1; });
