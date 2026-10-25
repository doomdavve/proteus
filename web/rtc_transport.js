// The Wasm bridge and the standalone connectivity gate share this browser transport.
// Five negotiated channels preserve the native protocol's delivery classes.
function installBoatRtcTransport(module) {
  const limit = 256, maxBytes = 1100, bufferedLimit = 65536;
  let pc = null, signal = null, channels = [], events = [], failure = '', generation = 0;
  let timer = null, connected = false;
  function close() {
    ++generation;
    clearTimeout(timer);
    timer = null;
    if (signal) signal.close();
    if (pc) pc.close();
    signal = pc = null;
    channels = [];
    events = [];
    connected = false;
  }
  function fail(message) {
    failure = message;
    close();
    events.push({ kind: 2 }); // BOAT_NET_EVENT_DISCONNECT
  }
  function reliable(channel) { return channel === 0 || channel === 4; }
  function enqueue(event) {
    if (events.length >= limit) {
      if (reliable(event.channel)) fail('Browser network receive queue is full.');
      return;
    }
    events.push(event);
  }
  function connect(url) {
    close();
    failure = '';
    const current = generation;
    try {
      const address = new URL(url);
      if (!['ws:', 'wss:'].includes(address.protocol)) throw new Error('Use a ws:// or wss:// room URL.');
      pc = new RTCPeerConnection({ iceServers: module['boatIceServers'] || [] });
      const peer = pc;
      signal = new WebSocket(url);
      const socket = signal;
      timer = setTimeout(() => { if (current === generation) fail('WebRTC connection timed out.'); }, 15000);
      for (let i = 0; i < 5; ++i) {
        const options = { negotiated: true, id: i, ordered: reliable(i) };
        if (!reliable(i)) options.maxRetransmits = 0;
        const channel = peer.createDataChannel('boat-' + i, options);
        channels.push(channel);
        channel.binaryType = 'arraybuffer';
        channel.onopen = () => {
          if (current !== generation || connected || !channels.every(c => c.readyState === 'open')) return;
          connected = true;
          clearTimeout(timer);
          enqueue({ kind: 1 });
        };
        channel.onmessage = event => {
          if (current !== generation) return;
          if (!(event.data instanceof ArrayBuffer) || !event.data.byteLength || event.data.byteLength > maxBytes) {
            fail('Invalid WebRTC game message.');
            return;
          }
          enqueue({ kind: 3, channel: i, data: new Uint8Array(event.data) });
        };
        channel.onclose = () => { if (current === generation) fail('The room closed the connection.'); };
        channel.onerror = () => { if (current === generation) fail('WebRTC data channel failed.'); };
      }
      let offerSent = false;
      function sendOffer() {
        if (current !== generation || offerSent || peer.iceGatheringState !== 'complete' || socket.readyState !== WebSocket.OPEN) return;
        offerSent = true;
        socket.send(peer.localDescription.sdp);
      }
      peer.onicegatheringstatechange = sendOffer;
      socket.onopen = async () => {
        try {
          await peer.setLocalDescription(await peer.createOffer());
          sendOffer();
        } catch (error) { if (current === generation) fail(error.message); }
      };
      socket.onmessage = async event => {
        if (current !== generation) return;
        try {
          if (typeof event.data !== 'string' || event.data.length > 16384) throw new Error('Invalid room signaling response.');
          await peer.setRemoteDescription({ type: 'answer', sdp: event.data });
        } catch (error) { if (current === generation) fail(error.message); }
      };
      socket.onerror = () => { if (current === generation) fail('Cannot reach the room signaling server.'); };
      socket.onclose = () => { if (current === generation && !connected) fail('Room signaling closed before connection.'); };
      peer.onconnectionstatechange = () => {
        if (current === generation && ['failed', 'closed', 'disconnected'].includes(peer.connectionState))
          fail('WebRTC connection lost.');
      };
      return true;
    } catch (error) {
      fail(error.message);
      return false;
    }
  }
  module['boatRtc'] = {
    connect, close,
    leave: () => { const current = generation; setTimeout(() => { if (current === generation) close(); }, 40); },
    failure: () => failure,
    poll: () => events.shift(),
    send: (channel, data) => {
      const dc = channels[channel];
      if (!dc || dc.readyState !== 'open' || !data.byteLength || data.byteLength > maxBytes) return false;
      if (dc.bufferedAmount > bufferedLimit) {
        if (reliable(channel)) { fail('Browser network send queue is full.'); return false; }
        return true; // Skip disposable updates while congested.
      }
      try { dc.send(data); return true; }
      catch (error) { fail(error.message); return false; }
    }
  };
  return module['boatRtc'];
}
if (typeof module !== 'undefined' && module.exports) module.exports = { installBoatRtcTransport };
