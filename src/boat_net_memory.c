#include "boat_net_transport.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    double due;
    uint64_t order; // Ties keep send order.
    BoatNetEventKind kind;
    BoatNetChannel channel;
    uint32_t sequence; // Per-channel send order, for sequenced delivery.
    uint16_t size;
    uint8_t data[BOAT_NET_MAX_MESSAGE];
} Flight;

typedef struct {
    Flight queue[BOAT_NET_MEMORY_QUEUE];
    unsigned count, max_count;
    BoatNetImpairment impairment;
    double reliable_due[BOAT_NET_CHANNELS]; // Reliable delivery stays ordered.
    uint32_t next_sequence[BOAT_NET_CHANNELS], delivered[BOAT_NET_CHANNELS];
    bool delivered_any[BOAT_NET_CHANNELS];
} Direction;

typedef struct {
    BoatNetMemory *network;
    unsigned client; // BOAT_NET_MAX_PEERS for the server endpoint.
} Endpoint;

typedef struct {
    bool used, connected, blackout; // A closed link is not reused.
    Direction up, down; // Client to server, server to client.
    Endpoint endpoint;
} Link;

struct BoatNetMemory {
    double now;
    uint64_t random, order;
    Link links[BOAT_NET_MAX_PEERS];
    Endpoint server;
    BoatNetTransportStats server_stats, client_stats[BOAT_NET_MAX_PEERS];
};

static double uniform(BoatNetMemory *n)
{
    n->random = n->random * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return (double)(n->random >> 11) / 9007199254740992.0;
}

static bool reliable(BoatNetChannel channel)
{
    return channel == BOAT_NET_CONTROL || channel == BOAT_NET_BOOTSTRAP;
}

static bool sequenced(BoatNetChannel channel)
{
    return channel == BOAT_NET_STATE || channel == BOAT_NET_BIRTHS;
}

static void close_link(BoatNetMemory *n, unsigned client);

static bool enqueue(BoatNetMemory *n, Direction *d, BoatNetEventKind kind, BoatNetChannel channel,
    const void *data, size_t size, double due, uint32_t sequence, bool event)
{
    // The last place is kept for the disconnect event.
    if (d->count == BOAT_NET_MEMORY_QUEUE - (event ? 0 : 1))
        return false;
    Flight *f = &d->queue[d->count++];
    *f = (Flight) { .due = due,
        .order = n->order++,
        .kind = kind,
        .channel = channel,
        .sequence = sequence,
        .size = (uint16_t)size };
    if (size)
        memcpy(f->data, data, size);
    if (d->count > d->max_count)
        d->max_count = d->count;
    return true;
}

static double transit(BoatNetMemory *n, const BoatNetImpairment *i)
{
    double delay = i->delay + (i->jitter > 0 ? (2 * uniform(n) - 1) * i->jitter : 0);
    return delay > 0 ? delay : 0;
}

static bool transmit(BoatNetMemory *n, unsigned client, bool down, BoatNetChannel channel,
    const void *data, size_t size, BoatNetTransportStats *stats)
{
    Link *link = &n->links[client];
    Direction *d = down ? &link->down : &link->up;
    if (!link->connected || size == 0 || size > BOAT_NET_MAX_MESSAGE
        || channel >= BOAT_NET_CHANNELS) {
        ++stats->dropped;
        return false;
    }
    ++stats->sent;
    stats->sent_bytes += size;
    if (size > stats->max_message)
        stats->max_message = size;
    if (link->blackout) {
        // A blackout models an outage: reliable messages are lost too, so neither end hears
        // anything until it lifts.
        ++stats->dropped;
        return true;
    }
    uint32_t sequence = d->next_sequence[channel]++;
    const BoatNetImpairment *i = &d->impairment;
    if (reliable(channel)) {
        double due = n->now + transit(n, i);
        if (due < d->reliable_due[channel])
            due = d->reliable_due[channel];
        d->reliable_due[channel] = due;
        if (!enqueue(n, d, BOAT_NET_EVENT_MESSAGE, channel, data, size, due, sequence, false)) {
            close_link(n, client); // A reliable backlog beyond the bound ends the connection.
            return false;
        }
        return true;
    }
    if (uniform(n) < i->loss)
        return true; // Lost on the way; the sender cannot tell.
    unsigned copies = uniform(n) < i->duplicate ? 2 : 1;
    for (unsigned c = 0; c < copies; ++c) {
        double due = n->now + transit(n, i) + (uniform(n) < i->reorder ? i->reorder_delay : 0);
        if (!enqueue(n, d, BOAT_NET_EVENT_MESSAGE, channel, data, size, due, sequence, false))
            ++stats->dropped;
    }
    return true;
}

static void close_link(BoatNetMemory *n, unsigned client)
{
    Link *link = &n->links[client];
    if (!link->connected)
        return;
    link->connected = false;
    // Messages already sent still arrive; each end then learns of the close, after the last
    // reliable message in its direction (graceful, like enet_peer_disconnect_later).
    Direction *directions[] = { &link->up, &link->down };
    for (unsigned k = 0; k < 2; ++k) {
        Direction *d = directions[k];
        double due = n->now + d->impairment.delay;
        for (unsigned c = 0; c < BOAT_NET_CHANNELS; ++c)
            if (d->reliable_due[c] > due)
                due = d->reliable_due[c];
        enqueue(n, d, BOAT_NET_EVENT_DISCONNECT, BOAT_NET_CONTROL, NULL, 0, due, 0, true);
    }
}

// Earliest due message in D (stable by send order), removed into EVENT.
static bool receive(BoatNetMemory *n, Direction *d, unsigned peer, BoatNetEvent *event,
    BoatNetTransportStats *stats)
{
    for (;;) {
        unsigned best = d->count;
        for (unsigned i = 0; i < d->count; ++i)
            if (d->queue[i].due <= n->now
                && (best == d->count || d->queue[i].due < d->queue[best].due
                    || (d->queue[i].due == d->queue[best].due
                        && d->queue[i].order < d->queue[best].order)))
                best = i;
        if (best == d->count)
            return false;
        Flight f = d->queue[best];
        d->queue[best] = d->queue[--d->count];
        if (f.kind == BOAT_NET_EVENT_MESSAGE && sequenced(f.channel)) {
            if (d->delivered_any[f.channel]
                && !boat_net_sequence_newer(f.sequence, d->delivered[f.channel])) {
                ++stats->dropped; // Older than the newest delivery: discarded like ENet.
                continue;
            }
            d->delivered[f.channel] = f.sequence;
            d->delivered_any[f.channel] = true;
        }
        event->kind = f.kind;
        event->peer = peer;
        event->channel = f.channel;
        event->size = f.size;
        if (f.size)
            memcpy(event->data, f.data, f.size);
        if (f.kind == BOAT_NET_EVENT_MESSAGE) {
            ++stats->received;
            stats->received_bytes += f.size;
            if (f.size > stats->max_message)
                stats->max_message = f.size;
        }
        return true;
    }
}

static bool server_poll(void *context, BoatNetEvent *event)
{
    Endpoint *e = context;
    BoatNetMemory *n = e->network;
    for (unsigned c = 0; c < BOAT_NET_MAX_PEERS; ++c) {
        Link *link = &n->links[c];
        if (link->used && receive(n, &link->up, c, event, &n->server_stats))
            return true;
    }
    return false;
}

static bool server_send(
    void *context, unsigned peer, BoatNetChannel channel, const void *data, size_t size)
{
    Endpoint *e = context;
    if (peer >= BOAT_NET_MAX_PEERS) {
        ++e->network->server_stats.dropped;
        return false;
    }
    return transmit(e->network, peer, true, channel, data, size, &e->network->server_stats);
}

static void server_disconnect(void *context, unsigned peer)
{
    Endpoint *e = context;
    if (peer < BOAT_NET_MAX_PEERS)
        close_link(e->network, peer);
}

static BoatNetTransportStats stats_with_queue(
    BoatNetTransportStats stats, unsigned queued, unsigned max_queued)
{
    stats.queued = queued;
    stats.max_queued = max_queued;
    return stats;
}

static BoatNetTransportStats server_stats(void *context)
{
    Endpoint *e = context;
    unsigned queued = 0, max_queued = 0;
    for (unsigned c = 0; c < BOAT_NET_MAX_PEERS; ++c) {
        queued += e->network->links[c].up.count;
        if (e->network->links[c].up.max_count > max_queued)
            max_queued = e->network->links[c].up.max_count;
    }
    return stats_with_queue(e->network->server_stats, queued, max_queued);
}

static bool client_poll(void *context, BoatNetEvent *event)
{
    Endpoint *e = context;
    Link *link = &e->network->links[e->client];
    return receive(e->network, &link->down, 0, event, &e->network->client_stats[e->client]);
}

static bool client_send(
    void *context, unsigned peer, BoatNetChannel channel, const void *data, size_t size)
{
    Endpoint *e = context;
    if (peer != 0)
        return false;
    return transmit(
        e->network, e->client, false, channel, data, size, &e->network->client_stats[e->client]);
}

static void client_disconnect(void *context, unsigned peer)
{
    Endpoint *e = context;
    (void)peer;
    close_link(e->network, e->client);
}

static BoatNetTransportStats client_stats(void *context)
{
    Endpoint *e = context;
    const Link *link = &e->network->links[e->client];
    return stats_with_queue(
        e->network->client_stats[e->client], link->down.count, link->down.max_count);
}

BoatNetMemory *boat_net_memory_create(uint64_t seed)
{
    BoatNetMemory *n = calloc(1, sizeof(*n));
    if (!n)
        return NULL;
    n->random = seed * UINT64_C(0x9e3779b97f4a7c15) + 1;
    n->server = (Endpoint) { n, BOAT_NET_MAX_PEERS };
    return n;
}

void boat_net_memory_destroy(BoatNetMemory *network) { free(network); }

void boat_net_memory_set_time(BoatNetMemory *network, double now) { network->now = now; }

void boat_net_memory_impair(
    BoatNetMemory *network, unsigned client, BoatNetImpairment up, BoatNetImpairment down)
{
    if (client < BOAT_NET_MAX_PEERS) {
        network->links[client].up.impairment = up;
        network->links[client].down.impairment = down;
    }
}

void boat_net_memory_blackout(BoatNetMemory *network, unsigned client, bool blackout)
{
    if (client < BOAT_NET_MAX_PEERS)
        network->links[client].blackout = blackout;
}

BoatNetTransport boat_net_memory_server(BoatNetMemory *network)
{
    return (BoatNetTransport) { &network->server, server_poll, server_send, server_disconnect,
        server_stats };
}

bool boat_net_memory_client(BoatNetMemory *network, unsigned *client, BoatNetTransport *t)
{
    for (unsigned c = 0; c < BOAT_NET_MAX_PEERS; ++c) {
        Link *link = &network->links[c];
        if (link->used)
            continue;
        *link = (Link) { .used = true, .connected = true, .endpoint = { network, c } };
        enqueue(network, &link->up, BOAT_NET_EVENT_CONNECT, BOAT_NET_CONTROL, NULL, 0, network->now,
            0, true);
        enqueue(network, &link->down, BOAT_NET_EVENT_CONNECT, BOAT_NET_CONTROL, NULL, 0,
            network->now, 0, true);
        *client = c;
        *t = (BoatNetTransport) { &link->endpoint, client_poll, client_send, client_disconnect,
            client_stats };
        return true;
    }
    return false;
}
