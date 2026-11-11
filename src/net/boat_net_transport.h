#ifndef PROTEUS_BOAT_NET_TRANSPORT_H
#define PROTEUS_BOAT_NET_TRANSPORT_H

#include "boat_net_protocol.h"

// The only boundary between the protocol sessions and a delivery mechanism: byte messages,
// delivery classes and small peer numbers. ENet (boat_net_enet.c) and the in-memory
// impairment adapter below implement it; their types never reach the codec or simulation.
typedef enum {
    BOAT_NET_EVENT_NONE,
    BOAT_NET_EVENT_CONNECT,
    BOAT_NET_EVENT_DISCONNECT,
    BOAT_NET_EVENT_MESSAGE
} BoatNetEventKind;

typedef struct {
    BoatNetEventKind kind;
    unsigned peer; // Below BOAT_NET_MAX_PEERS; a client's single peer is 0.
    BoatNetChannel channel;
    size_t size;
    uint8_t data[BOAT_NET_MAX_MESSAGE];
} BoatNetEvent;

typedef struct {
    uint64_t sent, sent_bytes, received, received_bytes;
    uint64_t dropped; // Refused or discarded locally: oversize, queue full, stale.
    size_t max_message; // Largest application message sent or received.
    size_t max_datagram; // Largest UDP datagram received (ENet only; zero otherwise).
    size_t queued, max_queued; // Messages waiting in the adapter (memory adapter).
} BoatNetTransportStats;

typedef struct BoatNetTransport {
    void *context;
    // Next event without blocking; false when none is waiting.
    bool (*poll)(void *context, BoatNetEvent *event);
    // Queue SIZE bytes (at most BOAT_NET_MAX_MESSAGE) for PEER; false if refused.
    bool (*send)(
        void *context, unsigned peer, BoatNetChannel channel, const void *data, size_t size);
    void (*disconnect)(void *context, unsigned peer);
    BoatNetTransportStats (*stats)(void *context);
} BoatNetTransport;

static inline bool boat_net_poll(BoatNetTransport *t, BoatNetEvent *event)
{
    return t->poll(t->context, event);
}

static inline bool boat_net_send(
    BoatNetTransport *t, unsigned peer, BoatNetChannel channel, const void *data, size_t size)
{
    return t->send(t->context, peer, channel, data, size);
}

// In-memory network: one server endpoint and up to BOAT_NET_MAX_PEERS client endpoints with
// virtual time. Unreliable channels can be delayed, jittered, lost, duplicated and reordered;
// sequenced channels then discard anything older than the newest delivery, as ENet does.
// Reliable channels are delayed but ordered and lossless. Every queue is bounded: unreliable
// overflow drops the message, reliable overflow disconnects the pair.
typedef struct {
    double delay, jitter; // Seconds one way; jitter is uniform +-.
    double loss, duplicate, reorder; // Probabilities for unreliable messages.
    double reorder_delay; // Extra delay of a reordered message.
} BoatNetImpairment;

#define BOAT_NET_MEMORY_QUEUE 256 // Messages in flight per direction.

typedef struct BoatNetMemory BoatNetMemory;

BoatNetMemory *boat_net_memory_create(uint64_t seed);
void boat_net_memory_destroy(BoatNetMemory *network);
void boat_net_memory_set_time(BoatNetMemory *network, double now);
// Impairment for messages sent from now on, server-to-client (DOWN) and client-to-server.
void boat_net_memory_impair(
    BoatNetMemory *network, unsigned client, BoatNetImpairment up, BoatNetImpairment down);
// Drop every message in both directions while set (an outage); the connection survives.
void boat_net_memory_blackout(BoatNetMemory *network, unsigned client, bool blackout);
BoatNetTransport boat_net_memory_server(BoatNetMemory *network);
// A new client endpoint (connected after one delay); its index is the server's peer number.
bool boat_net_memory_client(BoatNetMemory *network, unsigned *client, BoatNetTransport *t);

#endif
