// One module instance and one bounded request per disposable Web Worker.
self.onmessage = async ({data}) => {
  try {
    if (!(data.bytes instanceof Uint8Array) || data.bytes.length > 8 + 4096 + 65536)
      throw Error('Invalid package frame');
    const create = (await import(data.runtime)).default;
    let offset = 0;
    const output = [];
    const runtime = await create({
      locateFile: () => data.wasm,
      stdin: () => offset < data.bytes.length ? data.bytes[offset++] : null,
      stdout: byte => { if (output.length >= 4096) throw Error('Worker output exceeds limit'); output.push(byte); },
      stderr: () => {},
      print: () => {}, printErr: () => {}
    });
    const exit = runtime._main();
    self.postMessage({exit, bytes: new Uint8Array(output)});
  } catch (error) { self.postMessage({error: 'Worker initialization failed: ' + error.message}); }
};
