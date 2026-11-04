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
      // The pack, then its optional water atlas (graphics G4) and ambient occlusion (G5c) next to
      // it in the file system.
      const fetchInto = async (file, target, counted) => {
        const url = Module['locateFile'](`geometry/${name}/${file}`, '');
        const response = await fetch(url);
        if (!response.ok)
          throw new Error(`${url}: HTTP ${response.status}`);
        if (counted)
          load.total = Number(response.headers.get('Content-Length')) || 0;
        const reader = response.body.getReader();
        const chunks = [];
        let received = 0;
        for (;;) {
          const { done, value } = await reader.read();
          if (done)
            break;
          chunks.push(value);
          received += value.length;
          if (counted)
            load.received += value.length;
        }
        const bytes = new Uint8Array(received);
        let at = 0;
        for (const chunk of chunks) {
          bytes.set(chunk, at);
          at += chunk.length;
        }
        FS.mkdirTree(target.slice(0, target.lastIndexOf('/')));
        FS.writeFile(target, bytes);
      };
      const directory = path.slice(0, path.lastIndexOf('/') + 1);
      fetchInto('scenery.bin', path, true)
        .then(() => fetchInto('water_atlas.bin', directory + 'water_atlas.bin', false).catch(
          error => console.warn('Water atlas', name, error)))
        .then(() => fetchInto('ambient.bin', directory + 'ambient.bin', false).catch(error =>
          console.warn('Ambient occlusion', name, error)))
        .then(() => {
          load.state = 2;
        })
        .catch(error => {
          console.warn('Scenery pack', name, error);
          load.state = -1;
        });
    },
  };
})();
