// Browser descriptor authoring. Transport and bundle functions are also exercised under Node.
(function (root) {
  'use strict';
  const encoder = new TextEncoder();
  const FORMAT = 'wake-velocity-plugin';
  const STORAGE = 'wake-velocity-plugin-v1';
  function bundle(manifest, source) {
    if (typeof manifest !== 'string' || typeof source !== 'string') throw Error('Expected source text');
    if (encoder.encode(manifest).length > 4096 || encoder.encode(source).length > 65536)
      throw Error('Package exceeds the 4 KiB manifest or 64 KiB source limit');
    return {format: FORMAT, version: 1, manifest, source};
  }
  function decode(text) {
    if (typeof text !== 'string' || encoder.encode(text).length > 200000)
      throw Error('Bundle exceeds the 200 KB import limit');
    const data = JSON.parse(text);
    if (!data || data.format !== FORMAT || data.version !== 1
        || Object.keys(data).sort().join(',') !== 'format,manifest,source,version')
      throw Error('Expected a Wake Velocity version 1 plugin bundle');
    return bundle(data.manifest, data.source);
  }
  function frame(data) {
    const m = encoder.encode(data.manifest), s = encoder.encode(data.source);
    const result = new Uint8Array(8 + m.length + s.length), view = new DataView(result.buffer);
    view.setUint32(0, m.length); view.setUint32(4, s.length);
    result.set(m, 8); result.set(s, 8 + m.length); return result;
  }
  function equal(a, b) { return a.length === b.length && a.every((v, i) => v === b[i]); }
  function sameBase(a, b) { return equal(a, b); }

  class Transport {
    constructor({makeWorker, timer = (fn, ms) => setTimeout(fn, ms), clear = id => clearTimeout(id)}) {
      this.makeWorker = makeWorker; this.timer = timer; this.clear = clear;
      this.jobs = new Map(); this.serial = 0; this.prepared = null;
    }
    start(bytes, base) {
      if (bytes.length > 8 + 4096 + 65536 || this.jobs.size >= 2 || this.serial === 0xffffffff) return 0;
      const id = ++this.serial;
      const job = {bytes: bytes.slice(), base: base.slice(), state: 0, response: null};
      job.promise = new Promise(resolve => { job.resolve = resolve; });
      this.jobs.set(id, job);
      if (this.prepared && equal(bytes, this.prepared.bytes)) {
        const prepared = this.prepared; this.prepared = null; // One receipt, one request.
        this.finish(id, sameBase(base, prepared.base) ? prepared.result : null);
        return id;
      }
      try {
        job.worker = this.makeWorker();
        job.worker.onmessage = event => this.finish(id, event.data);
        job.worker.onerror = () => this.finish(id, null);
        job.deadline = this.timer(() => this.finish(id, null), 5000);
        job.worker.postMessage({bytes: job.bytes});
      } catch (_) { this.finish(id, null); }
      return id;
    }
    finish(id, result) {
      const job = this.jobs.get(id);
      if (!job || job.state) return; // Late results from canceled/expired workers are inert.
      if (job.worker) job.worker.terminate();
      if (job.deadline !== undefined) this.clear(job.deadline);
      if (result && (result.exit === 0 || result.exit === 1)
          && result.bytes instanceof Uint8Array && result.bytes.length >= 8 && result.bytes.length <= 4096
          && new DataView(result.bytes.buffer, result.bytes.byteOffset).getUint32(0) === 0x50535432
          && (new DataView(result.bytes.buffer, result.bytes.byteOffset).getUint32(4) === 0) === (result.exit === 0)) {
        job.response = result.bytes.slice(); job.state = result.exit === 0 ? 1 : 2;
      } else job.state = -1;
      job.resolve({state: job.state, bytes: job.response, base: job.base});
    }
    status(id) { return this.jobs.get(id)?.state ?? -1; }
    read(id) { return this.jobs.get(id)?.response; }
    wait(id) { return this.jobs.get(id)?.promise ?? Promise.resolve({state: -1}); }
    close(id) { this.finish(id, null); this.jobs.delete(id); }
    prepare(bytes, base, result) {
      this.prepared = {bytes: bytes.slice(), base: base.slice(),
        result: {exit: 0, bytes: result.slice()}};
    }
  }

  // This is diagnostics display only; C checks exact framing, native bounds and worker status.
  function message(bytes) {
    try {
      const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
      if (view.getUint32(0) !== 0x50535432) return 'Invalid worker reply';
      let at = 8;
      const string = () => {
        const length = view.getUint32(at); at += 4;
        if (length > 160 || at + length > bytes.length) throw Error();
        const text = new TextDecoder().decode(bytes.slice(at, at + length)); at += length;
        return text;
      };
      const file = string(), line = view.getUint32(at); at += 4;
      const field = string(), detail = string();
      return [file + (line ? ':' + line : ''), field, detail].filter(Boolean).join(' ')
        || 'Descriptor validated';
    } catch (_) { return 'Invalid worker reply'; }
  }

  function install(Module, files, getFS, launch, document, window) {
    const get = id => document.getElementById(id);
    const storage = get('plugin-storage');
    const status = get('plugin-status'), manifest = get('plugin-manifest'), source = get('plugin-source');
    const stage = get('plugin-stage'), play = get('plugin-play');
    const transport = new Transport({makeWorker: () => {
      const worker = new Worker(files['plugin_worker.js'], {type: 'module'});
      // Pin every runtime component to this package's content-addressed map.
      const post = worker.postMessage.bind(worker);
      worker.postMessage = data => post({...data,
        runtime: new URL(files['plugin_stage.js'], window.location.href).href,
        wasm: new URL(files['plugin_stage.wasm'], window.location.href).href});
      return worker;
    }});
    Module.boatPluginStage = transport;
    let sequence = 0, pending = 0, accepted = null, ready = false;
    const base = () => (Module.boatPluginOwner || [0, 0, 0]).slice();
    const current = () => bundle(manifest.value, source.value);
    const show = text => { status.textContent = text; };
    const set = data => { manifest.value = data.manifest; source.value = data.source; };
    function cancel() {
      ++sequence;
      if (pending) transport.close(pending);
      pending = 0; stage.disabled = !ready;
    }
    Module.boatPluginNotice = show;
    Module.boatPluginAuthorReady = () => { ready = true; stage.disabled = false; };
    get('plugin-cancel').onclick = () => {
      cancel(); Module.boatPluginActions = (Module.boatPluginActions || 0) | 8;
      show('Staging canceled. Current voyage kept.');
    };
    get('plugin-tools').addEventListener('toggle', () => {
      Module.boatPluginEditing = get('plugin-tools').open && Module.boatGameStarted;
    });
    get('plugin-panel').addEventListener('keydown', event => {
      event.stopPropagation();
      if (['F5', 'F6', 'F7'].includes(event.key)) event.preventDefault();
    });
    get('plugin-apply').onclick = () => { Module.boatPluginActions = (Module.boatPluginActions || 0) | 4; };
    get('plugin-undo').onclick = () => {
      const wasPending = !!pending;
      cancel(); Module.boatPluginActions = (Module.boatPluginActions || 0) | (wasPending ? 8 : 2);
      if (wasPending) show('Staging canceled. Current voyage kept.');
    };
    stage.onclick = async () => {
      cancel(); const request = sequence, pin = base();
      let id = 0;
      try {
        const data = current(), bytes = frame(data);
        id = pending = transport.start(bytes, pin);
        stage.disabled = true; show('Validating voyage…');
        const result = await transport.wait(id);
        if (request !== sequence) return;
        if (!sameBase(pin, base())) throw Error('Voyage changed during validation; stage again');
        if (result.state !== 1) throw Error(result.bytes ? message(result.bytes)
          : 'Worker failed or reached its five-second deadline');
        transport.prepare(bytes, pin, result.bytes);
        accepted = data;
        const fs = getFS();
        fs.mkdirTree('/plugins/browser');
        fs.writeFile('/plugins/browser/plugin.json', encoder.encode(data.manifest));
        fs.writeFile('/plugins/browser/main.lua', encoder.encode(data.source));
        if (Module.boatGameStarted) Module.boatPluginActions = (Module.boatPluginActions || 0) | 1;
        else play.disabled = false;
        show(Module.boatGameStarted ? 'Validated; offering revision to the live voyage…'
          : 'Voyage staged. Play the staged voyage, or export the source bundle.');
        try { window.localStorage.setItem(STORAGE, JSON.stringify(data)); storage.textContent = ''; }
        catch (_) { storage.textContent = 'Browser storage unavailable; export to keep your source.'; }
      } catch (error) { if (request === sequence) show('Rejected: ' + error.message + '. Previous voyage kept.'); }
      finally {
        if (id) transport.close(id);
        if (request === sequence) { pending = 0; stage.disabled = !ready; }
      }
    };
    play.onclick = () => {
      if (!accepted || Module.boatGameStarted) return;
      play.disabled = true;
      document.body.classList.add('plugin-session');
      get('plugin-tools').open = false;
      launch(['--plugin', '/plugins/browser']);
    };
    get('plugin-export').onclick = () => {
      try {
        const url = URL.createObjectURL(new Blob([JSON.stringify(current(), null, 2) + '\n'],
          {type: 'application/json'}));
        const link = document.createElement('a'); link.href = url;
        link.download = 'voyage.wake-plugin.json'; link.click(); URL.revokeObjectURL(url);
      } catch (error) { show(error.message); }
    };
    get('plugin-import').onchange = async event => {
      cancel(); const request = sequence;
      try {
        const file = event.target.files[0];
        if (!file) return;
        if (file.size > 200000) throw Error('Bundle exceeds the 200 KB import limit');
        const data = decode(await file.text());
        if (request !== sequence) return;
        set(data); show('Imported draft. Stage it to validate before play.');
      } catch (error) { if (request === sequence) show(error.message); }
      event.target.value = '';
    };
    for (const editor of [manifest, source]) editor.addEventListener('input', cancel);
    // Restoration is source-only and never bypasses validation. A storage failure is nonfatal.
    let restored = false;
    try {
      const saved = window.localStorage.getItem(STORAGE);
      if (saved) { set(decode(saved)); restored = true; show('Restored source. Stage to validate it.'); }
    } catch (_) { storage.textContent = 'Saved source unavailable or invalid. Import a bundle or use the example.'; }
    get('plugin-example').onclick = async () => {
      cancel(); const request = sequence;
      try {
        const response = await fetch(files['plugin_example.json']);
        if (!response.ok) throw Error('Example unavailable');
        const data = decode(await response.text());
        if (request === sequence) { set(data); show('Example loaded. Stage to validate it.'); }
      } catch (error) { if (request === sequence) show(error.message); }
    };
    if (!restored) get('plugin-tools').addEventListener('toggle', () => {
      if (get('plugin-tools').open && !manifest.value && !source.value) get('plugin-example').onclick();
    });
    window.addEventListener('pagehide', () => {
      cancel(); for (const id of transport.jobs.keys()) transport.close(id);
    });
    return transport;
  }
  root.installBoatPluginAuthor = install;
  if (typeof module === 'object') module.exports = {bundle, decode, frame, Transport, message, install};
})(globalThis);
