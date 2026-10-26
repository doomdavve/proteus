// Operator-owned static JSON, fetched independently of Wasm startup and gameplay.
function installBoatServerDirectory(module, fetchList = globalThis.fetch) {
  let servers = [], state = 'loading', pending = null;
  function validate(data) {
    if (data?.version !== 1 || !Array.isArray(data.servers) || data.servers.length > 8)
      throw new Error('Invalid server list.');
    const text = (value, limit) => typeof value === 'string' && value.length > 0
      && value.length <= limit && /^[\x20-\x7e]+$/.test(value);
    return data.servers.map(server => {
      if (!text(server.name, 24) || !text(server.description, 64)
          || !text(server.websocket, 63) || !Number.isInteger(server.protocol)
          || !Number.isInteger(server.physics)) throw new Error('Invalid server entry.');
      const url = new URL(server.websocket);
      if (url.protocol !== 'wss:' || url.username || url.password || url.hash)
        throw new Error('Public rooms require secure WebSocket URLs.');
      return server;
    });
  }
  function refresh() {
    if (pending) return pending;
    state = 'loading';
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 5000);
    pending = (async () => {
      try {
        const response = await fetchList(module['boatServerListUrl'] || 'servers.json', {
          cache: 'no-store', credentials: 'omit', signal: controller.signal
        });
        if (!response.ok) throw new Error('Server list unavailable.');
        const body = await response.text();
        if (body.length > 16384) throw new Error('Server list too large.');
        servers = validate(JSON.parse(body));
        state = 'ready';
      } catch (_) {
        state = 'unavailable'; // A previously fetched list stays usable during an outage.
      } finally {
        clearTimeout(timer);
        pending = null;
      }
    })();
    return pending;
  }
  const directory = {
    refresh,
    count: () => servers.length,
    server: index => servers[index],
    compatible: (index, protocol, physics) => servers[index]?.protocol === protocol
      && servers[index]?.physics === physics,
    status: () => state === 'loading' ? 'LOADING SERVER LIST...'
      : state === 'unavailable' ? 'LIST UNAVAILABLE; MANUAL ADDRESS STILL WORKS'
      : servers.length ? 'CHOOSE A SERVER; ENTER TO JOIN' : 'NO PUBLIC SERVERS LISTED'
  };
  module['boatServers'] = directory;
  return directory;
}
if (typeof module !== 'undefined' && module.exports) module.exports = { installBoatServerDirectory };
