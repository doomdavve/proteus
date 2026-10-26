const assert = require('node:assert/strict');
const { installBoatServerDirectory } = require('../web/server_directory.js');
const manifest = require('../web/servers.json');

(async () => {
  let data = manifest, fail = false, calls = 0;
  const directory = installBoatServerDirectory({}, async (url, options) => {
    ++calls;
    assert.equal(url, 'servers.json');
    assert.equal(options.cache, 'no-store');
    assert.equal(options.credentials, 'omit');
    if (fail) throw new Error('offline');
    return { ok: true, text: async () => JSON.stringify(data) };
  });
  const first = directory.refresh();
  assert.equal(directory.refresh(), first); // Concurrent menu/startup refreshes share one request.
  await first;
  assert.equal(calls, 1);
  assert.equal(directory.server(0).name, 'Winterfell');
  const { protocol, physics } = manifest.servers[0];
  assert.equal(directory.compatible(0, protocol, physics), true);
  assert.equal(directory.compatible(0, protocol, physics + 1), false);
  assert.equal(directory.compatible(0, protocol + 1, physics), false);
  assert.equal(directory.compatible(9, protocol, physics), false);

  // An operator can update the directory without a Wasm rebuild; a transient outage
  // preserves the last successful list instead of blocking a previously available room.
  data = { version: 1, servers: [{ ...manifest.servers[0], name: 'New room' }] };
  await directory.refresh();
  assert.equal(directory.server(0).name, 'New room');
  fail = true;
  await directory.refresh();
  assert.equal(directory.count(), 1);
  assert.match(directory.status(), /UNAVAILABLE/);

  for (const bad of [
    { version: 2, servers: [] },
    { version: 1, servers: Array(9).fill(manifest.servers[0]) },
    { version: 1, servers: [{ ...manifest.servers[0], websocket: 'ws://insecure/room' }] },
    { version: 1, servers: [{ ...manifest.servers[0], websocket: 'wss://user:secret@host/room' }] },
    { version: 1, servers: [{ ...manifest.servers[0], name: 'bad\nname' }] },
    { version: 1, servers: [{ ...manifest.servers[0], websocket: 'wss://host/' + 'x'.repeat(64) }] }
  ]) {
    const invalid = installBoatServerDirectory({}, async () => ({ ok: true, text: async () => JSON.stringify(bad) }));
    await invalid.refresh();
    assert.equal(invalid.count(), 0);
    assert.match(invalid.status(), /UNAVAILABLE/);
  }
  const empty = installBoatServerDirectory({}, async () => ({ ok: true, text: async () => '{"version":1,"servers":[]}' }));
  await empty.refresh();
  assert.match(empty.status(), /NO PUBLIC SERVERS/);
  const remote = installBoatServerDirectory({ boatServerListUrl: 'https://directory.example/servers.json' },
    async url => {
      assert.equal(url, 'https://directory.example/servers.json');
      return { ok: true, text: async () => JSON.stringify(manifest) };
    });
  await remote.refresh();
  assert.equal(remote.count(), 1);
  console.log('web_server_directory=ok');
})().catch(error => { console.error(error); process.exitCode = 1; });
