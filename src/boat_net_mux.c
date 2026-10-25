#include "boat_net_mux.h"
#include <string.h>
static bool poll_event(void *context, BoatNetEvent *event)
{
    BoatNetMux *mux = context;
    // Bound rejection/drain work even if a transport continually supplies stale events.
    for (unsigned attempt = 0; attempt < 2 * BOAT_NET_MAX_PEERS + 2; ++attempt) {
        unsigned owner = mux->cursor++ % 2;
        BoatNetTransport *transport = &mux->transports[owner];
        if (!boat_net_poll(transport, event)) {
            transport = &mux->transports[owner ^ 1];
            if (!boat_net_poll(transport, event))
                return false;
            owner ^= 1;
        }
        unsigned global = BOAT_NET_MAX_PEERS, free_slot = BOAT_NET_MAX_PEERS;
        for (unsigned i = 0; i < BOAT_NET_MAX_PEERS; ++i) {
            if (!mux->occupied[i])
                free_slot = i;
            else if (mux->owner[i] == owner && mux->local[i] == event->peer)
                global = i;
        }
        if (event->kind == BOAT_NET_EVENT_CONNECT && global == BOAT_NET_MAX_PEERS) {
            if (free_slot == BOAT_NET_MAX_PEERS) {
                transport->disconnect(transport->context, event->peer);
                continue;
            }
            global = free_slot;
            mux->occupied[global] = true;
            mux->owner[global] = owner;
            mux->local[global] = event->peer;
        }
        if (global == BOAT_NET_MAX_PEERS)
            continue;
        if (event->kind == BOAT_NET_EVENT_DISCONNECT)
            mux->occupied[global] = false;
        event->peer = global;
        return true;
    }
    return false;
}
static bool send_message(
    void *context, unsigned peer, BoatNetChannel channel, const void *data, size_t size)
{
    BoatNetMux *mux = context;
    if (peer >= BOAT_NET_MAX_PEERS || !mux->occupied[peer])
        return false;
    BoatNetTransport *transport = &mux->transports[mux->owner[peer]];
    return boat_net_send(transport, mux->local[peer], channel, data, size);
}
static void disconnect_peer(void *context, unsigned peer)
{
    BoatNetMux *mux = context;
    if (peer >= BOAT_NET_MAX_PEERS || !mux->occupied[peer])
        return;
    BoatNetTransport *transport = &mux->transports[mux->owner[peer]];
    transport->disconnect(transport->context, mux->local[peer]);
}
static BoatNetTransportStats statistics(void *context)
{
    BoatNetMux *mux = context;
    BoatNetTransportStats a = mux->transports[0].stats(mux->transports[0].context);
    BoatNetTransportStats b = mux->transports[1].stats(mux->transports[1].context);
    a.sent += b.sent;
    a.sent_bytes += b.sent_bytes;
    a.received += b.received;
    a.received_bytes += b.received_bytes;
    a.dropped += b.dropped;
    a.queued += b.queued;
    a.max_queued += b.max_queued;
    if (b.max_message > a.max_message)
        a.max_message = b.max_message;
    if (b.max_datagram > a.max_datagram)
        a.max_datagram = b.max_datagram;
    return a;
}
void boat_net_mux_init(BoatNetMux *mux, BoatNetTransport first, BoatNetTransport second)
{
    memset(mux, 0, sizeof(*mux));
    mux->transports[0] = first;
    mux->transports[1] = second;
}
BoatNetTransport boat_net_mux_transport(BoatNetMux *mux)
{
    return (BoatNetTransport) { mux, poll_event, send_message, disconnect_peer, statistics };
}
