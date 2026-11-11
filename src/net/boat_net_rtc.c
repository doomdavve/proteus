#include "boat_net_rtc.h"
#include "boat_net_enet.h" // Monotonic scheduling, shared with the native authority.
#include <rtc/rtc.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SIGNAL_BYTES = 16384, QUEUE_BYTES = 65536, ACCEPTS = 8 };
typedef struct {
    int ws, pc, channels[BOAT_NET_CHANNELS];
    atomic_int state;
    atomic_bool gathered;
    bool answered, connected;
    double deadline, close_after;
} Peer;
struct BoatNetRtc {
    int listener;
    atomic_flag lock;
    int accepted[ACCEPTS];
    unsigned count, cursor;
    Peer peers[BOAT_NET_MAX_PEERS];
    BoatNetTransportStats stats;
    char ice[512];
};

static bool reliable(unsigned channel)
{
    return channel == BOAT_NET_CONTROL || channel == BOAT_NET_BOOTSTRAP;
}
static void state_changed(int pc, rtcState state, void *ptr)
{
    (void)pc;
    Peer *peer = ptr;
    atomic_store(&peer->state, state);
}
static void gathered(int pc, rtcGatheringState state, void *ptr)
{
    (void)pc;
    Peer *peer = ptr;
    if (state == RTC_GATHERING_COMPLETE)
        atomic_store(&peer->gathered, true);
}
static void accepted(int server, int ws, void *ptr)
{
    (void)server;
    BoatNetRtc *rtc = ptr;
    if (rtc) {
        while (atomic_flag_test_and_set(&rtc->lock)) { }
        bool room = rtc->count < ACCEPTS;
        if (room)
            rtc->accepted[rtc->count++] = ws;
        atomic_flag_clear(&rtc->lock);
        if (room)
            return;
    }
    rtcDeleteWebSocket(ws);
}
static void clear_peer(Peer *peer)
{
    // Deletion waits for callbacks; the stable Peer storage outlives all handles.
    for (unsigned i = 0; i < BOAT_NET_CHANNELS; ++i)
        if (peer->channels[i] >= 0)
            rtcDeleteDataChannel(peer->channels[i]);
    if (peer->pc >= 0)
        rtcDeletePeerConnection(peer->pc);
    if (peer->ws >= 0)
        rtcDeleteWebSocket(peer->ws);
    peer->ws = peer->pc = -1;
    for (unsigned i = 0; i < BOAT_NET_CHANNELS; ++i)
        peer->channels[i] = -1;
    peer->connected = peer->answered = false;
    peer->close_after = 0;
    atomic_store(&peer->state, RTC_NEW);
    atomic_store(&peer->gathered, false);
}
static bool start_peer(BoatNetRtc *rtc, Peer *peer, const char *offer)
{
    const char *ice[] = { rtc->ice };
    rtcConfiguration config = { .iceServers = ice,
        .iceServersCount = rtc->ice[0] ? 1 : 0,
        .disableAutoNegotiation = true,
        .maxMessageSize = BOAT_NET_MAX_MESSAGE };
    peer->pc = rtcCreatePeerConnection(&config);
    if (peer->pc < 0)
        return false;
    rtcSetUserPointer(peer->pc, peer);
    rtcSetStateChangeCallback(peer->pc, state_changed);
    rtcSetGatheringStateChangeCallback(peer->pc, gathered);
    for (unsigned i = 0; i < BOAT_NET_CHANNELS; ++i) {
        char label[16];
        snprintf(label, sizeof(label), "boat-%u", i);
        rtcDataChannelInit init = { .negotiated = true,
            .manualStream = true,
            .stream = i,
            .reliability
            = { .unordered = !reliable(i), .unreliable = !reliable(i), .maxRetransmits = 0 } };
        peer->channels[i] = rtcCreateDataChannelEx(peer->pc, label, &init);
        if (peer->channels[i] < 0)
            return false;
    }
    return rtcSetRemoteDescription(peer->pc, offer, "offer") >= 0
        && rtcSetLocalDescription(peer->pc, "answer") >= 0;
}
static bool service_peer(BoatNetRtc *rtc, Peer *peer)
{
    if (peer->close_after && boat_net_clock() >= peer->close_after)
        return false;
    if (peer->pc < 0) {
        char offer[SIGNAL_BYTES];
        int size = sizeof(offer);
        int result = rtcReceiveMessage(peer->ws, offer, &size);
        if (result == RTC_ERR_SUCCESS) {
            if (size >= 0 || !start_peer(rtc, peer, offer))
                return false;
        } else if (result != RTC_ERR_NOT_AVAIL || rtcIsClosed(peer->ws))
            return false;
    }
    if (peer->pc >= 0 && !peer->answered && atomic_load(&peer->gathered)) {
        char answer[SIGNAL_BYTES];
        if (rtcGetLocalDescription(peer->pc, answer, sizeof(answer)) < 0
            || rtcSendMessage(peer->ws, answer, -1) < 0)
            return false;
        peer->answered = true;
    }
    int state = atomic_load(&peer->state);
    if (state == RTC_FAILED || state == RTC_CLOSED || state == RTC_DISCONNECTED)
        return false;
    return peer->connected || boat_net_clock() < peer->deadline;
}
static void accept_pending(BoatNetRtc *rtc)
{
    int pending[ACCEPTS];
    while (atomic_flag_test_and_set(&rtc->lock)) { }
    unsigned count = rtc->count;
    memcpy(pending, rtc->accepted, count * sizeof(int));
    rtc->count = 0;
    atomic_flag_clear(&rtc->lock);
    for (unsigned i = 0; i < count; ++i) {
        Peer *free_peer = NULL;
        for (unsigned j = 0; j < BOAT_NET_MAX_PEERS; ++j)
            if (rtc->peers[j].ws < 0) {
                free_peer = &rtc->peers[j];
                break;
            }
        if (free_peer) {
            free_peer->ws = pending[i];
            free_peer->deadline = boat_net_clock() + 15;
        } else
            rtcDeleteWebSocket(pending[i]);
    }
}
static bool poll_event(void *context, BoatNetEvent *event)
{
    BoatNetRtc *rtc = context;
    accept_pending(rtc);
    for (unsigned turn = 0; turn < BOAT_NET_MAX_PEERS; ++turn) {
        unsigned slot = rtc->cursor++ % BOAT_NET_MAX_PEERS;
        Peer *peer = &rtc->peers[slot];
        if (peer->ws < 0)
            continue;
        *event = (BoatNetEvent) { .peer = slot };
        if (!service_peer(rtc, peer)) {
            bool connected = peer->connected;
            clear_peer(peer);
            if (connected) {
                event->kind = BOAT_NET_EVENT_DISCONNECT;
                return true;
            }
            continue;
        }
        if (!peer->connected) {
            bool open = peer->answered;
            for (unsigned i = 0; i < BOAT_NET_CHANNELS; ++i)
                open = open && peer->channels[i] >= 0 && rtcIsOpen(peer->channels[i]);
            if (!open)
                continue;
            peer->connected = true;
            event->kind = BOAT_NET_EVENT_CONNECT;
            return true;
        }
        for (unsigned i = 0; i < BOAT_NET_CHANNELS; ++i) {
            int dc = peer->channels[i], bytes = sizeof(event->data);
            if (rtcGetAvailableAmount(dc) > QUEUE_BYTES) {
                clear_peer(peer);
                event->kind = BOAT_NET_EVENT_DISCONNECT;
                ++rtc->stats.dropped;
                return true;
            }
            int result = rtcReceiveMessage(dc, (char *)event->data, &bytes);
            if (result == RTC_ERR_NOT_AVAIL)
                continue;
            if (result < 0 || bytes <= 0) {
                clear_peer(peer);
                event->kind = BOAT_NET_EVENT_DISCONNECT;
                ++rtc->stats.dropped;
                return true;
            }
            event->kind = BOAT_NET_EVENT_MESSAGE;
            event->channel = (BoatNetChannel)i;
            event->size = (size_t)bytes;
            ++rtc->stats.received;
            rtc->stats.received_bytes += event->size;
            return true;
        }
    }
    return false;
}
static bool send_message(
    void *context, unsigned slot, BoatNetChannel channel, const void *data, size_t size)
{
    BoatNetRtc *rtc = context;
    if (slot >= BOAT_NET_MAX_PEERS || channel >= BOAT_NET_CHANNELS || !size
        || size > BOAT_NET_MAX_MESSAGE)
        return false;
    Peer *peer = &rtc->peers[slot];
    int dc = peer->channels[channel];
    if (!peer->connected || dc < 0)
        return false;
    if (rtcGetBufferedAmount(dc) > QUEUE_BYTES) {
        ++rtc->stats.dropped;
        // Disposable updates may be skipped; a stalled reliable stream ends the connection.
        if (reliable(channel)) {
            rtcClosePeerConnection(peer->pc);
            return false;
        }
        return true;
    }
    if (rtcSendMessage(dc, data, (int)size) < 0)
        return false;
    ++rtc->stats.sent;
    rtc->stats.sent_bytes += size;
    if (size > rtc->stats.max_message)
        rtc->stats.max_message = size;
    return true;
}
static void disconnect_peer(void *context, unsigned slot)
{
    BoatNetRtc *rtc = context;
    if (slot < BOAT_NET_MAX_PEERS && rtc->peers[slot].pc >= 0)
        rtc->peers[slot].close_after = boat_net_clock() + .25; // Flush rejection/leave messages.
}
static BoatNetTransportStats statistics(void *context) { return ((BoatNetRtc *)context)->stats; }
BoatNetTransport boat_net_rtc_transport(BoatNetRtc *rtc)
{
    return (BoatNetTransport) { rtc, poll_event, send_message, disconnect_peer, statistics };
}
BoatNetRtc *boat_net_rtc_listen(
    const char *bind, uint16_t port, const char *ice_server, char *error, size_t size)
{
    BoatNetRtc *rtc = calloc(1, sizeof(*rtc));
    if (!rtc) {
        snprintf(error, size, "Cannot allocate the WebRTC listener.");
        return NULL;
    }
    atomic_flag_clear(&rtc->lock);
    rtc->listener = -1;
    for (unsigned i = 0; i < BOAT_NET_MAX_PEERS; ++i) {
        Peer *peer = &rtc->peers[i];
        peer->pc = peer->ws = -1;
        for (unsigned j = 0; j < BOAT_NET_CHANNELS; ++j)
            peer->channels[j] = -1;
        atomic_init(&peer->state, RTC_NEW);
        atomic_init(&peer->gathered, false);
    }
    if (ice_server && strlen(ice_server) >= sizeof(rtc->ice)) {
        snprintf(error, size, "ICE server URL is too long.");
        boat_net_rtc_close(rtc);
        return NULL;
    }
    snprintf(rtc->ice, sizeof(rtc->ice), "%s", ice_server ? ice_server : "");
    rtcInitLogger(RTC_LOG_WARNING, NULL);
    rtcWsServerConfiguration config = { .port = port,
        .bindAddress = bind,
        .connectionTimeoutMs = 10000,
        .maxMessageSize = SIGNAL_BYTES - 1 };
    rtc->listener = rtcCreateWebSocketServer(&config, accepted);
    if (rtc->listener < 0) {
        snprintf(error, size, "Cannot listen for WebRTC signaling on %s:%u", bind, port);
        boat_net_rtc_close(rtc);
        return NULL;
    }
    rtcSetUserPointer(rtc->listener, rtc);
    return rtc;
}
void boat_net_rtc_close(BoatNetRtc *rtc)
{
    if (!rtc)
        return;
    if (rtc->listener >= 0)
        rtcDeleteWebSocketServer(rtc->listener);
    for (unsigned i = 0; i < rtc->count; ++i)
        rtcDeleteWebSocket(rtc->accepted[i]);
    for (unsigned i = 0; i < BOAT_NET_MAX_PEERS; ++i)
        clear_peer(&rtc->peers[i]);
    free(rtc);
}
