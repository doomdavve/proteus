#include "boat_net_web.h"
#include <emscripten.h>
#include <stddef.h>
#include <stdio.h>

EM_JS(
    int, connect_room, (const char *url), { return Module['boatRtc'].connect(UTF8ToString(url)); })
EM_JS(void, close_room, (void), { Module['boatRtc'].close(); })
EM_JS(void, leave_room, (void), { Module['boatRtc'].leave(); })
EM_JS(void, failure_text, (char *out, int size),
    { stringToUTF8(Module['boatRtc'].failure(), out, size); })
EM_JS(int, next_event, (int *kind, int *channel, unsigned char *data), {
    const event = Module['boatRtc'].poll();
    if (!event)
        return -1;
    HEAP32[kind >> 2] = event.kind;
    HEAP32[channel >> 2] = event.channel || 0;
    if (event.data)
        HEAPU8.set(event.data, data);
    return event.data ? event.data.length : 0;
})
EM_JS(int, send_bytes, (int channel, const void *data, int size),
    { return Module['boatRtc'].send(channel, HEAPU8.slice(data, data + size)); })
static BoatNetTransportStats stats;
static bool poll_event(void *context, BoatNetEvent *event)
{
    (void)context;
    int kind, channel;
    int size = next_event(&kind, &channel, event->data);
    if (size < 0)
        return false;
    event->kind = (BoatNetEventKind)kind;
    event->channel = (BoatNetChannel)channel;
    event->peer = 0;
    event->size = (size_t)size;
    if (kind == BOAT_NET_EVENT_MESSAGE) {
        ++stats.received;
        stats.received_bytes += size;
    }
    return true;
}
static bool send_message(
    void *context, unsigned peer, BoatNetChannel channel, const void *data, size_t size)
{
    (void)context;
    if (peer || channel >= BOAT_NET_CHANNELS || !size || size > BOAT_NET_MAX_MESSAGE)
        return false;
    if (!send_bytes(channel, data, (int)size))
        return false;
    ++stats.sent;
    stats.sent_bytes += size;
    if (size > stats.max_message)
        stats.max_message = size;
    return true;
}
static void disconnect_peer(void *context, unsigned peer)
{
    (void)context;
    (void)peer;
    leave_room();
}
static BoatNetTransportStats statistics(void *context)
{
    (void)context;
    return stats;
}
bool boat_net_web_connect(const char *url, char *error, size_t size)
{
    stats = (BoatNetTransportStats) { 0 };
    if (connect_room(url))
        return true;
    snprintf(error, size, "%s", boat_net_web_failure());
    return false;
}
BoatNetTransport boat_net_web_transport(void)
{
    return (BoatNetTransport) { NULL, poll_event, send_message, disconnect_peer, statistics };
}
void boat_net_web_close(void)
{
    emscripten_sleep(50); // Allow the reliable leave message to reach the authority.
    close_room();
}
const char *boat_net_web_failure(void)
{
    static char error[160];
    failure_text(error, sizeof(error));
    return error;
}
double boat_net_clock(void) { return emscripten_get_now() / 1000; }
