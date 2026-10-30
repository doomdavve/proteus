#!/usr/bin/env node
'use strict';
const assert = require('node:assert/strict');
const {bundle, decode, encode, frame, Transport, message} = require('../web/plugin_author.js');
const {ObjectiveTransport} = require('../web/plugin_objective.js');
const fs = require('node:fs');
const path = require('node:path');

function response(status = 0) {
  const bytes = new Uint8Array(8), view = new DataView(bytes.buffer);
  view.setUint32(0, 0x50535432); view.setUint32(4, status);
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
async function main() {
  const root = path.resolve(__dirname, '..');
  const original = bundle(fs.readFileSync(path.join(root, 'plugins/langedrag/plugin.json'), 'utf8'),
    fs.readFileSync(path.join(root, 'plugins/langedrag/main.lua'), 'utf8'));
  assert.deepEqual(decode(encode(original)), original);
  assert.throws(() => encode(bundle('', '\u0001'.repeat(65536))));
  const framed = frame(original), view = new DataView(framed.buffer);
  assert.equal(view.getUint32(0), Buffer.byteLength(original.manifest));
  assert.equal(view.getUint32(4), Buffer.byteLength(original.source));
  assert.equal(new TextDecoder().decode(framed.slice(8, 8 + view.getUint32(0))), original.manifest);
  for (const invalid of ['null', '[]', '{}', '{', JSON.stringify({...original, version: 2}),
    JSON.stringify({...original, extra: 'value'}), 'x'.repeat(200001)]) {
    assert.throws(() => decode(invalid));
  }
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
  const cap1 = t.start(framed, base), cap2 = t.start(framed, base);
  assert.equal(t.start(framed, base), 0); t.close(cap1); t.close(cap2);
  const failed = t.start(framed, base);
  workers.at(-1).onerror(); assert.equal(t.status(failed), -1); t.close(failed);
  assert.equal(t.jobs.size, 0); assert.equal(timers.size, 0);
  assert.equal(message(new Uint8Array()), 'Invalid worker reply');
  assert.equal(t.start(new Uint8Array(8 + 4096 + 65537), base), 0);
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
