// Browser voyage authoring. Transport and bundle functions are also exercised under Node.
// A bundle is the version 2 `wake-velocity-plugin` form: {format, version, manifest, source, modules}.
(function (root) {
  'use strict';
  const encoder = new TextEncoder();
  const FORMAT = 'wake-velocity-plugin';
  const STORAGE = 'wake-velocity-plugin-v1';
  // Must equal BOAT_PLUGIN_WIRE_VERSION in src/plugins/boat_plugin_wire.h; web_plugin_wire checks it.
  const WIRE_MAGIC = 0x50535443;
  // Matches the Wasm host read buffer: source plus bounded decoded scenes and diagnostics.
  const RESULT_LIMIT = 163840;
  function scenes(manifest) {
    try {
      const data = JSON.parse(manifest);
      return Array.isArray(data.capabilities) && data.capabilities.includes('scene.define');
    } catch (_) { return false; }
  }
  function kind(manifest) {
    try { return JSON.parse(manifest).kind; } catch (_) { return null; }
  }
  const decoder = new TextDecoder('utf-8', {fatal: true});
  // The source image of src/plugins/boat_plugin_image.h (byte-identical; see the golden vector in
  // tests/support/fixtures/plugin_image_golden.hex): "\0PMI" + version 1, a file count, then per
  // file a u8 name length, the name, a u32 little-endian length and the bytes. File 0 is the
  // entry; modules follow in the manifest's order.
  const IMAGE_MAGIC = [0, 0x50, 0x4d, 0x49, 1];
  const ENTRY = 'main.lua', MANIFEST_MAX = 4096, SOURCE_MAX = 65536;
  const MODULES_MAX = 8, NAME_MAX = 32;
  const NAME = /^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*){0,2}$/;
  const NAME_RULE = "must be 1-3 dot-separated names of a-z, 0-9 and '_', each starting with a "
    + `letter, in at most ${NAME_MAX} bytes`;
  const V1_MESSAGE = 'version 1 bundles are not supported; re-export';
  const moduleNameValid = name => typeof name === 'string' && name.length <= NAME_MAX && NAME.test(name);
  const moduleFile = name => 'modules/' + name.replace(/\./g, '/') + '.lua';
  function imageIs(bytes) {
    return bytes.length >= IMAGE_MAGIC.length && IMAGE_MAGIC.every((v, i) => bytes[i] === v);
  }
  // files: [{name, data: Uint8Array}], entry first. Throws like the native packer refuses.
  function packImage(files) {
    if (files.length < 1 || files.length > MODULES_MAX + 1) throw Error('Package image needs 1-9 files');
    let total = IMAGE_MAGIC.length + 1;
    const names = new Set();
    for (const f of files) {
      const name = encoder.encode(f.name);
      if (name.length < 1 || name.length > NAME_MAX || names.has(f.name) || f.data.length > SOURCE_MAX)
        throw Error('Package image file ' + f.name + ' cannot be packed');
      names.add(f.name); total += 1 + name.length + 4 + f.data.length;
      if (total > SOURCE_MAX) throw Error('Package image exceeds the 64 KiB source budget');
    }
    const out = new Uint8Array(total), view = new DataView(out.buffer);
    out.set(IMAGE_MAGIC); out[IMAGE_MAGIC.length] = files.length;
    let at = IMAGE_MAGIC.length + 1;
    for (const f of files) {
      const name = encoder.encode(f.name);
      out[at++] = name.length; out.set(name, at); at += name.length;
      view.setUint32(at, f.data.length, true); at += 4;
      out.set(f.data, at); at += f.data.length;
    }
    return out;
  }
  // The inverse; refuses anything packImage could not have written, like the native unpacker.
  function unpackImage(bytes) {
    const bad = () => Error('malformed package source image');
    if (!imageIs(bytes) || bytes.length > SOURCE_MAX || bytes.length < IMAGE_MAGIC.length + 1) throw bad();
    const count = bytes[IMAGE_MAGIC.length];
    if (count < 1 || count > MODULES_MAX + 1) throw bad();
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const files = [], seen = new Set();
    let at = IMAGE_MAGIC.length + 1;
    for (let i = 0; i < count; ++i) {
      if (at >= bytes.length) throw bad();
      const n = bytes[at++];
      if (n < 1 || n > NAME_MAX || bytes.length - at < n + 4) throw bad();
      let name;
      try { name = decoder.decode(bytes.slice(at, at + n)); } catch (_) { throw bad(); }
      at += n;
      const size = view.getUint32(at, true); at += 4;
      if (size > bytes.length - at || (i > 0 && !moduleNameValid(name)) || seen.has(name)) throw bad();
      seen.add(name); files.push({name, data: bytes.slice(at, at + size)}); at += size;
    }
    if (at !== bytes.length) throw bad();
    return files;
  }
  // The module names plugin.json declares, in order; null when the manifest is not JSON (the
  // native parser stays the authority and reports that), and the same refusals as the native one.
  function declared(manifest) {
    let data;
    try { data = JSON.parse(manifest); } catch (_) { return null; }
    if (!data || typeof data !== 'object' || Array.isArray(data) || !('modules' in data)) return [];
    const list = data.modules;
    if (!Array.isArray(list) || list.length > MODULES_MAX)
      throw Error(`plugin.json modules must be an array of at most ${MODULES_MAX} module names`);
    list.forEach((name, i) => {
      if (!moduleNameValid(name)) throw Error(`plugin.json modules[${i + 1}] ${NAME_RULE}`);
      if (name === 'main')
        throw Error(`plugin.json modules[${i + 1}] must not be "main", the entry's own name`);
      const first = list.indexOf(name);
      if (first !== i) throw Error(`plugin.json modules[${i + 1}] repeats modules[${first + 1}]`);
    });
    return list;
  }
  // The source the native side holds: the entry bytes, or an image when modules are declared.
  // Per-file and total limits mirror boat_plugin_read: entry, modules and framing share 64 KiB.
  function sourceBytes(manifest, source, modules) {
    const names = declared(manifest) || [];
    const given = Object.keys(modules);
    for (const name of given)
      if (!names.includes(name))
        throw Error(`bundle modules holds "${name}", which plugin.json does not declare`);
    for (const name of names)
      if (!given.includes(name))
        throw Error(`plugin.json declares module "${name}" but ${moduleFile(name)} is missing`);
    const entry = encoder.encode(source);
    if (!names.length) {
      if (entry.length > SOURCE_MAX) throw Error('Package exceeds the 4 KiB manifest or 64 KiB source limit');
      return entry;
    }
    const files = [{name: ENTRY, data: entry}, ...names.map(name => ({name, data: encoder.encode(modules[name])}))];
    const budget = file => `${file} is larger than the %ROOM% bytes left of the ${SOURCE_MAX}-byte `
      + 'source budget (entry, modules and framing share it)';
    let used = IMAGE_MAGIC.length + 1;
    for (const f of files) {
      const label = f.name === ENTRY ? ENTRY : moduleFile(f.name);
      const framing = 1 + f.name.length + 4;
      const room = Math.max(0, SOURCE_MAX - used - framing);
      if (used + framing > SOURCE_MAX || f.data.length > room) throw Error(budget(label).replace('%ROOM%', room));
      used += framing + f.data.length;
    }
    return packImage(files);
  }
  // The inverse for a source read back from the Wasm side: {entry, modules} as text, checked
  // against the manifest's declared names and order.
  function unpackSource(manifest, bytes) {
    const names = declared(manifest) || [];
    if (!imageIs(bytes)) {
      if (names.length) throw Error(`plugin.json declares ${names.length} module${names.length === 1 ? '' : 's'}`
        + ' but the package source is not a module image');
      return {entry: decoder.decode(bytes), modules: {}};
    }
    const files = unpackImage(bytes);
    if (!names.length) throw Error('plugin.json declares no modules but the package source is a module image');
    if (files[0].name !== ENTRY) throw Error('plugin.json entry is not the entry file the module image carries');
    if (files.length - 1 !== names.length || names.some((n, i) => files[i + 1].name !== n))
      throw Error('plugin.json modules do not match the modules in the package source image'
        + ' (names and order must agree)');
    const modules = {};
    try {
      for (const f of files.slice(1)) modules[f.name] = decoder.decode(f.data);
      return {entry: decoder.decode(files[0].data), modules};
    } catch (_) { throw Error('package source image is not UTF-8 text'); }
  }
  function bundle(manifest, source, modules = {}) {
    if (typeof manifest !== 'string' || typeof source !== 'string') throw Error('Expected source text');
    if (!modules || typeof modules !== 'object' || Array.isArray(modules)
        || Object.values(modules).some(text => typeof text !== 'string'))
      throw Error('Expected module text');
    if (encoder.encode(manifest).length > MANIFEST_MAX)
      throw Error('Package exceeds the 4 KiB manifest or 64 KiB source limit');
    if (declared(manifest) === null && Object.keys(modules).length)
      throw Error('plugin.json is not valid JSON, so its modules cannot be matched');
    sourceBytes(manifest, source, modules); // Limits and consistency; the bytes are rebuilt on use.
    // Native scene authoring is not part of the browser subset; reject before the worker starts.
    // Imported scenes and all solo kinds share the worker's bounded stage wire.
    const type = kind(manifest);
    if (type === 'race' || type === 'match')
      throw Error(`${type} packages are room content; import them through a room`);
    const ordered = {};
    for (const name of declared(manifest) || []) ordered[name] = modules[name];
    return {format: FORMAT, version: 2, manifest, source, modules: ordered};
  }
  function decode(text) {
    if (typeof text !== 'string' || encoder.encode(text).length > 200000)
      throw Error('Bundle exceeds the 200 KB import limit');
    const data = JSON.parse(text);
    if (data && data.format === FORMAT && data.version === 1) throw Error(V1_MESSAGE);
    if (!data || data.format !== FORMAT || data.version !== 2
        || Object.keys(data).sort().join(',') !== 'format,manifest,modules,source,version')
      throw Error('Expected a Wake Velocity version 2 plugin bundle');
    return bundle(data.manifest, data.source, data.modules);
  }
  function encode(data) {
    const text = JSON.stringify(data, null, 2) + '\n';
    if (encoder.encode(text).length > 200000) throw Error('Bundle exceeds the 200 KB import limit');
    return text;
  }
  function frame(data) {
    const m = encoder.encode(data.manifest), s = sourceBytes(data.manifest, data.source, data.modules);
    const result = new Uint8Array(8 + m.length + s.length), view = new DataView(result.buffer);
    view.setUint32(0, m.length); view.setUint32(4, s.length);
    result.set(m, 8); result.set(s, 8 + m.length); return result;
  }
  // A bundle from the (manifest, source) bytes the Wasm side holds, as the console /save exports.
  function fromWasm(manifestBytes, source) {
    const manifest = decoder.decode(manifestBytes), {entry, modules} = unpackSource(manifest, source);
    return bundle(manifest, entry, modules);
  }
  // plugin.json with NAME added to, or removed from, its modules array (reformatted JSON).
  function editModules(manifest, name, add) {
    let data;
    try { data = JSON.parse(manifest); } catch (_) { throw Error('plugin.json is not valid JSON'); }
    if (!data || typeof data !== 'object' || Array.isArray(data)) throw Error('plugin.json is not a JSON object');
    const list = Array.isArray(data.modules) ? data.modules.filter(n => n !== name) : [];
    if (add) list.push(name);
    if (list.length) data.modules = list; else delete data.modules;
    return JSON.stringify(data, null, 2) + '\n';
  }
  // Rewrite /plugins/browser (FS is Emscripten's FS) to hold exactly this package: stale module
  // files from an earlier staging must not stay readable by the native directory read.
  function writePackage(fs, data, base = '/plugins/browser') {
    const clear = dir => {
      if (!fs.analyzePath(dir).exists) return;
      for (const name of fs.readdir(dir)) {
        if (name === '.' || name === '..') continue;
        const path = dir + '/' + name;
        if (fs.isDir(fs.stat(path).mode)) clear(path); else fs.unlink(path);
      }
      fs.rmdir(dir);
    };
    fs.mkdirTree(base);
    clear(base + '/modules');
    fs.writeFile(base + '/plugin.json', encoder.encode(data.manifest));
    fs.writeFile(base + '/' + ENTRY, encoder.encode(data.source));
    for (const [name, text] of Object.entries(data.modules)) {
      const path = base + '/' + moduleFile(name);
      fs.mkdirTree(path.slice(0, path.lastIndexOf('/')));
      fs.writeFile(path, encoder.encode(text));
    }
  }
  // The saved draft, or why it cannot be restored (a version 1 save names the re-export).
  function restoreSaved(saved) {
    if (!saved) return {data: null, error: null};
    try { return {data: decode(saved), error: null}; }
    catch (error) { return {data: null, error: error.message}; }
  }
  function equal(a, b) { return a.length === b.length && a.every((v, i) => v === b[i]); }
  function sameBase(a, b) { return equal(a, b); }

  class Transport {
    constructor({makeWorker, timer = (fn, ms) => setTimeout(fn, ms), clear = id => clearTimeout(id)}) {
      this.makeWorker = makeWorker; this.timer = timer; this.clear = clear;
      this.jobs = new Map(); this.serial = 0; this.prepared = null;
    }
    start(bytes, base, mode = 'stage') {
      if (bytes.length > 131072 || this.jobs.size >= 2 || this.serial === 0xffffffff) return 0;
      const id = ++this.serial;
      const job = {bytes: bytes.slice(), base: base.slice(), state: 0, response: null, mode};
      job.promise = new Promise(resolve => { job.resolve = resolve; });
      this.jobs.set(id, job);
      if (mode === 'stage' && this.prepared && equal(bytes, this.prepared.bytes)) {
        const prepared = this.prepared; this.prepared = null; // One receipt, one request.
        this.finish(id, sameBase(base, prepared.base) ? prepared.result : null);
        return id;
      }
      try {
        job.worker = this.makeWorker();
        job.worker.onmessage = event => this.finish(id, event.data);
        job.worker.onerror = () => this.finish(id, null);
        job.deadline = this.timer(() => this.finish(id, null), 15000);
        job.worker.postMessage({bytes: job.bytes, mode});
      } catch (_) { this.finish(id, null); }
      return id;
    }
    query(bytes, base) { return this.start(bytes, base, 'query'); }
    finish(id, result) {
      const job = this.jobs.get(id);
      if (!job || job.state) return; // Late results from canceled/expired workers are inert.
      if (job.worker) job.worker.terminate();
      if (job.deadline !== undefined) this.clear(job.deadline);
      if (result && (result.exit === 0 || result.exit === 1)
          && result.bytes instanceof Uint8Array && result.bytes.length >= 8 && result.bytes.length <= RESULT_LIMIT
          && new DataView(result.bytes.buffer, result.bytes.byteOffset).getUint32(0) === WIRE_MAGIC
          && (new DataView(result.bytes.buffer, result.bytes.byteOffset).getUint32(4) === 0) === (result.exit === 0)) {
        job.response = result.bytes.slice(); job.state = result.exit === 0 ? 1 : 2;
      } else {
        job.state = -1;
        // An unrecognised reply is a protocol mismatch, not a missed deadline. Keep the two
        // apart so the panel does not send the next wire bump looking for a slow worker.
        job.failure = result && result.bytes instanceof Uint8Array
          ? 'Worker replied with an unrecognised result'
          : result && typeof result.error === 'string' ? result.error
          : 'Worker failed or reached its deadline';
      }
      job.resolve({state: job.state, bytes: job.response, base: job.base, failure: job.failure});
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
      if (view.getUint32(0) !== WIRE_MAGIC) return 'Worker replied with an unrecognised wire version';
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
    } catch (_) { return 'Worker replied with an unrecognised wire version'; }
  }

  function install(Module, files, getFS, launch, document, window) {
    const get = id => document.getElementById(id);
    const storage = get('plugin-storage');
    const status = get('plugin-status'), manifest = get('plugin-manifest'), source = get('plugin-source');
    const moduleList = get('plugin-module'), moduleText = get('plugin-module-source');
    const moduleName = get('plugin-module-name'), moduleFileLabel = get('plugin-module-file');
    // Module texts by name. The selector shows them; plugin.json's modules array fixes the
    // names and order the package uses, and a mismatch is reported when staging or exporting.
    const texts = {};
    let selected = '';
    const stage = get('plugin-stage'), play = get('plugin-play');
    const makeWorker = () => {
      const worker = new Worker(files['plugin_worker.js'], {type: 'module'});
      // Pin every runtime component to this package's content-addressed map.
      const post = worker.postMessage.bind(worker);
      worker.postMessage = data => post({...data,
        runtime: new URL(files['plugin_stage.js'], window.location.href).href,
        wasm: new URL(files['plugin_stage.wasm'], window.location.href).href});
      return worker;
    };
    const transport = new Transport({makeWorker});
    const objective = new root.BoatPluginObjectiveTransport({makeWorker});
    Module.boatPluginObjective = objective;
    Module.boatPluginStage = transport;
    let sequence = 0, pending = 0, accepted = null, ready = false;
    const base = () => (Module.boatPluginOwner || [0, 0, 0]).slice();
    const current = () => { keep(); return bundle(manifest.value, source.value, {...texts}); };
    const show = text => { status.textContent = text; };
    function keep() { if (selected in texts) texts[selected] = moduleText.value; }
    function listModules() {
      let listed = [];
      try { listed = declared(manifest.value) || []; } catch (_) { /* reported when staging */ }
      const names = Object.keys(texts);
      moduleList.textContent = '';
      for (const name of names) {
        const option = document.createElement('option');
        option.value = name;
        option.textContent = listed.includes(name) ? name : name + ' (not in plugin.json)';
        moduleList.appendChild(option);
      }
      if (!(selected in texts)) selected = names[0] || '';
      moduleList.value = selected;
      moduleText.value = selected ? texts[selected] : '';
      moduleText.disabled = !selected;
      moduleFileLabel.textContent = selected ? moduleFile(selected) : 'no modules';
      get('plugin-module-remove').disabled = !selected;
    }
    const set = data => {
      manifest.value = data.manifest; source.value = data.source;
      for (const name of Object.keys(texts)) delete texts[name];
      Object.assign(texts, data.modules); selected = ''; listModules();
    };
    moduleList.onchange = () => { keep(); selected = moduleList.value; listModules(); };
    get('plugin-module-add').onclick = () => {
      cancel(); keep(); const name = moduleName.value.trim();
      try {
        if (!moduleNameValid(name)) throw Error(`Module name ${NAME_RULE}`);
        if (name === 'main' || name in texts) throw Error(`Module "${name}" already exists`);
        if (Object.keys(texts).length >= MODULES_MAX) throw Error(`A package holds at most ${MODULES_MAX} modules`);
        manifest.value = editModules(manifest.value, name, true);
        texts[name] = '-- ' + moduleFile(name) + '\nreturn {}\n'; selected = name; moduleName.value = '';
        listModules(); show('Added ' + moduleFile(name) + '. Stage to validate it.');
      } catch (error) { show(error.message); }
    };
    get('plugin-module-remove').onclick = () => {
      cancel(); if (!selected) return;
      const name = selected;
      try {
        manifest.value = editModules(manifest.value, name, false);
        delete texts[name]; selected = ''; listModules(); show('Removed ' + moduleFile(name) + '.');
      } catch (error) { show(error.message); }
    };
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
        const data = current(), encoded = encode(data), bytes = frame(data);
        id = pending = transport.start(bytes, pin);
        stage.disabled = true; show('Validating voyage…');
        const result = await transport.wait(id);
        if (request !== sequence) return;
        if (!sameBase(pin, base())) throw Error('Voyage changed during validation; stage again');
        if (result.state !== 1) throw Error(result.bytes ? message(result.bytes)
          : result.failure || 'Worker failed or reached its five-second deadline');
        transport.prepare(bytes, pin, result.bytes);
        accepted = data;
        writePackage(getFS(), data);
        if (Module.boatGameStarted) Module.boatPluginActions = (Module.boatPluginActions || 0) | 1;
        else play.disabled = false;
        show(Module.boatGameStarted ? 'Validated; offering revision to the live voyage…'
          : 'Voyage staged. Play the staged voyage, or export the source bundle.');
        try { window.localStorage.setItem(STORAGE, encoded); storage.textContent = ''; }
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
        const data = current();
        downloadBundle(data.manifest, data.source, data.modules);
      } catch (error) { show(error.message); }
    };
    function downloadBundle(manifestText, sourceText, modules) {
      const url = URL.createObjectURL(new Blob([encode(bundle(manifestText, sourceText, modules))],
        {type: 'application/json'}));
      const link = document.createElement('a'); link.href = url;
      link.download = 'voyage.wake-plugin.json'; link.click(); URL.revokeObjectURL(url);
    }
    // The native console's /save reaches this export path instead of writing a directory.
    // The Wasm side holds the source as the native side does: an image when modules exist.
    Module.boatPluginExport = (manifestBytes, sourceBytes) => {
      try {
        const data = fromWasm(manifestBytes, sourceBytes);
        downloadBundle(data.manifest, data.source, data.modules);
      } catch (error) { show('Export failed: ' + error.message); }
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
    for (const editor of [manifest, source, moduleText]) editor.addEventListener('input', cancel);
    manifest.addEventListener('change', () => { keep(); listModules(); });
    // Restoration is source-only and never bypasses validation. A storage failure is nonfatal.
    let restored = false;
    try {
      const {data, error} = restoreSaved(window.localStorage.getItem(STORAGE));
      if (data) { set(data); restored = true; show('Restored source. Stage to validate it.'); }
      else if (error) storage.textContent = 'Saved source not restored: ' + error
        + '. Import a bundle or use the example.';
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
      objective.reset();
    });
    return transport;
  }
  root.installBoatPluginAuthor = install;
  if (typeof module === 'object') module.exports = {bundle, decode, encode, frame, Transport, message, install, scenes, WIRE_MAGIC,
    packImage, unpackImage, imageIs, sourceBytes, unpackSource, fromWasm, editModules, writePackage,
    restoreSaved, moduleNameValid, moduleFile};
})(globalThis);
