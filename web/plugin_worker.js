// Disposable descriptor workers; a warm objective worker executes one bounded request at a time.
let objectiveRuntime = null, busy = false;
self.onmessage = async ({data}) => {
  if (busy) { self.postMessage({error: "Worker busy", id: data.id}); return; }
  busy = true;
  try {
    if (data.mode === 'objective') {
      if (!(data.bytes instanceof Uint8Array) || data.bytes.length > 131072)
        throw Error('Invalid objective frame');
      if (!objectiveRuntime) {
        const create = (await import(data.runtime)).default;
        objectiveRuntime = await create({locateFile: () => data.wasm,
          print: () => {}, printErr: () => {}});
      }
      const runtime = objectiveRuntime;
      const input = runtime._boat_plugin_eval_input();
      runtime.boatPluginHeap.set(data.bytes, input);
      const begun = performance.now();
      const size = runtime._boat_plugin_eval_run(data.bytes.length);
      const evalMs = performance.now() - begun;
      if (!size || size > 49152) throw Error('Invalid objective output');
      const at = runtime._boat_plugin_eval_output();
      self.postMessage({id: data.id, evalMs, bytes: runtime.boatPluginHeap.slice(at, at + size),
        wasmBytes: runtime.boatPluginHeap.buffer.byteLength});
      return;
    }
    if (data.mode === 'query') {
      if (!(data.bytes instanceof Uint8Array) || data.bytes.length > 131072)
        throw Error('Invalid query frame');
      const create = (await import(data.runtime)).default;
      let offset = 0;
      const output = [];
      const runtime = await create({
        locateFile: () => data.wasm,
        stdin: () => offset < data.bytes.length ? data.bytes[offset++] : null,
        stdout: byte => { if (output.length >= 16384) throw Error('Worker output exceeds limit'); output.push(byte); },
        stderr: () => {},
        print: () => {}, printErr: () => {}
      });
      const exit = runtime._boat_plugin_query_worker();
      self.postMessage({exit, bytes: new Uint8Array(output)});
      return;
    }
    if (!(data.bytes instanceof Uint8Array) || data.bytes.length > 8 + 4096 + 65536)
      throw Error('Invalid package frame');
    const view = new DataView(data.bytes.buffer, data.bytes.byteOffset, data.bytes.byteLength);
    const manifestSize = view.getUint32(0);
    if (manifestSize > 4096)
      throw Error('Invalid package manifest size');
    const manifest = JSON.parse(new TextDecoder().decode(
      data.bytes.slice(8, 8 + manifestSize)));
    if (manifest && (manifest.kind === 'race' || manifest.kind === 'match'))
      throw Error(`${manifest.kind} packages are room content; import them through a room`);
    const create = (await import(data.runtime)).default;
    let offset = 0;
    const output = [];
    const runtime = await create({
      locateFile: () => data.wasm,
      stdin: () => offset < data.bytes.length ? data.bytes[offset++] : null,
      stdout: byte => { if (output.length >= 16384) throw Error('Worker output exceeds limit'); output.push(byte); },
      stderr: () => {},
      print: () => {}, printErr: () => {}
    });
    const exit = runtime._main();
    self.postMessage({exit, bytes: new Uint8Array(output)});
  } catch (error) { self.postMessage({id: data.id, error: 'Worker evaluation failed: ' + error.message}); }
  finally { busy = false; }
};
