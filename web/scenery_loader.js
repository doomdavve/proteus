// Scenery packs the page does not preload (A3.5i): the outer map's is fetched the first time a
// session needs it and written to the in-memory file system where the game reads its packs.
Module['boatScenery'] = (() => {
  const loads = {};
  return {
    // 0 not started, 1 loading, 2 ready, -1 failed (a later start retries).
    state: name => (loads[name] ? loads[name].state : 0),
    progress: name => {
      const load = loads[name];
      return load && load.total ? Math.min(1, load.received / load.total) : 0;
    },
    start(name, path) {
      if (loads[name] && loads[name].state !== -1)
        return;
      const load = (loads[name] = { state: 1, received: 0, total: 0 });
      const url = Module['locateFile'](`geometry/${name}/scenery.bin`, '');
      fetch(url)
        .then(async response => {
          if (!response.ok)
            throw new Error(`${url}: HTTP ${response.status}`);
          load.total = Number(response.headers.get('Content-Length')) || 0;
          const reader = response.body.getReader();
          const chunks = [];
          for (;;) {
            const { done, value } = await reader.read();
            if (done)
              break;
            chunks.push(value);
            load.received += value.length;
          }
          const bytes = new Uint8Array(load.received);
          let at = 0;
          for (const chunk of chunks) {
            bytes.set(chunk, at);
            at += chunk.length;
          }
          FS.mkdirTree(path.slice(0, path.lastIndexOf('/')));
          FS.writeFile(path, bytes);
          load.state = 2;
        })
        .catch(error => {
          console.warn('Scenery pack', name, error);
          load.state = -1;
        });
    },
  };
})();
