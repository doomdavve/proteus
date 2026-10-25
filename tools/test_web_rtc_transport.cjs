const assert = require('node:assert/strict');
const { installBoatRtcTransport } = require('../web/rtc_transport.js');
let peer;
class Peer {
  constructor() { peer = this; this.channels = []; }
  createDataChannel(label, options) {
    const dc = { label, ...options, readyState: 'open', bufferedAmount: 0, send() { this.sent = true; } };
    this.channels.push(dc);
    return dc;
  }
  close() {}
}
class Socket { static OPEN = 1; close() {} }
global.RTCPeerConnection = Peer;
global.WebSocket = Socket;
const rtc = installBoatRtcTransport({});
assert.equal(rtc.connect('127.0.0.1:1234'), false);
assert.equal(rtc.poll().kind, 2);
function connect() {
  assert.equal(rtc.connect('ws://localhost:24682'), true);
  peer.channels[0].onopen();
  assert.equal(rtc.poll().kind, 1);
}
connect();
assert.deepEqual(peer.channels.map(c => c.ordered), [true, false, false, false, true]);
assert.deepEqual(peer.channels.map(c => c.maxRetransmits), [undefined, 0, 0, 0, undefined]);
// Disposable overflow is bounded; reliable overflow produces a clean disconnect.
for (let i = 0; i < 300; ++i) peer.channels[1].onmessage({ data: new Uint8Array([i]).buffer });
let count = 0; while (rtc.poll()) ++count;
assert.equal(count, 256);
for (let i = 0; i < 257; ++i) peer.channels[0].onmessage({ data: new Uint8Array([i]).buffer });
assert.equal(rtc.poll().kind, 2);
assert.match(rtc.failure(), /queue is full/);
connect();
const staleChannel = peer.channels[1];
peer.channels[1].bufferedAmount = 65537;
assert.equal(rtc.send(1, new Uint8Array([1])), true);
assert.equal(peer.channels[1].sent, undefined);
peer.channels[0].bufferedAmount = 65537;
assert.equal(rtc.send(0, new Uint8Array([1])), false);
assert.equal(rtc.poll().kind, 2);
connect();
staleChannel.onmessage({ data: new Uint8Array([1]).buffer });
assert.equal(rtc.poll(), undefined);
peer.channels[1].onmessage({ data: new ArrayBuffer(1101) });
assert.equal(rtc.poll().kind, 2);
assert.match(rtc.failure(), /Invalid/);
connect();
assert.equal(rtc.send(0, new Uint8Array(1101)), false);
rtc.close();
assert.equal(rtc.poll(), undefined);
console.log('web_rtc_transport=ok');
