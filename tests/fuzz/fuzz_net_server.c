// H3 harness: a room's server handling arbitrary client messages once a client has joined.
//
// The input is a list of chunks, each a channel byte, a big-endian length and that many bytes,
// sent one per step on a virtual clock. The room is rebuilt for every input, so a failure
// reproduces from the input alone.
#include "fuzz/fuzz_driver.h"
#include "net/boat_net_client.h"
#include "net/boat_net_server.h"
#include "net/boat_net_transport.h"
#include "support/boat_test_world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// What the inputs reached, summed over a run, so a regression run shows the harness is not
// stopping at the decoder.
static uint64_t reached[6];

static void report(void)
{
    fprintf(stderr,
        "fuzz_net_server: accepted=%llu commands=%llu spawns=%llu resyncs=%llu malformed=%llu "
        "refused=%llu\n",
        (unsigned long long)reached[0], (unsigned long long)reached[1],
        (unsigned long long)reached[2], (unsigned long long)reached[3],
        (unsigned long long)reached[4], (unsigned long long)reached[5]);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static BoatRoomDefinition room;
    static BoatNetServer server;
    memset(&room, 0, sizeof(room));
    room.version = BOAT_ROOM_VERSION;
    room.mode = BOAT_ROOM_MODE_FREE;
    room.location = BOAT_LOCATION_BASIN;
    snprintf(room.id, sizeof(room.id), "%s", "fuzz");
    snprintf(room.name, sizeof(room.name), "%s", "Fuzz room");
    BoatNetMemory *network = boat_net_memory_create(1);
    if (!network)
        abort();
    memset(&server, 0, sizeof(server));
    BoatNetServerConfig config = { .token = "room",
        .epoch = 0x5eed,
        .max_players = 2,
        .room = &room,
        .resources = test_world(BOAT_LOCATION_BASIN) };
    if (!boat_net_server_init(&server, boat_net_memory_server(network), &config))
        abort();
    unsigned peer;
    BoatNetTransport transport;
    if (!boat_net_memory_client(network, &peer, &transport))
        abort();
    // A real client joins first, so the fuzzed messages reach the playing-room logic and not
    // only the handshake. After that its transport carries the input.
    static BoatNetClient client;
    memset(&client, 0, sizeof(client));
    (void)test_world(BOAT_LOCATION_BASIN);
    BoatNetClientConfig join
        = { .name = "fuzz", .token = "room", .build = "fuzz", .resources = test_worlds };
    boat_net_client_init(&client, transport, &join, 0);
    double now = 0;
    for (unsigned step = 0; step < 900 && client.phase != BOAT_NET_CLIENT_PLAYING; ++step) {
        now += 1.0 / 60;
        boat_net_memory_set_time(network, now);
        (void)boat_net_server_service(&server, now);
        (void)boat_net_client_service(&client, now, (BoatInput) { 0 }, false);
    }
    for (size_t pos = 0; pos + 3 <= size;) {
        BoatNetChannel channel = (BoatNetChannel)(data[pos] % BOAT_NET_CHANNELS);
        size_t chunk = ((size_t)data[pos + 1] << 8 | data[pos + 2]) % (BOAT_NET_MAX_MESSAGE + 1);
        pos += 3;
        if (chunk > size - pos)
            chunk = size - pos;
        (void)boat_net_send(&transport, 0, channel, data + pos, chunk);
        pos += chunk;
        now += 1.0 / 60;
        boat_net_memory_set_time(network, now);
        (void)boat_net_server_service(&server, now);
        (void)boat_net_client_service(&client, now, (BoatInput) { 0 }, false);
    }
    static bool registered;
    if (!registered) {
        registered = true;
        atexit(report);
    }
    reached[0] += server.stats.accepted;
    reached[1] += server.stats.commands;
    reached[2] += server.stats.spawn_requests;
    reached[3] += server.stats.resyncs;
    reached[4] += server.stats.malformed;
    reached[5] += server.stats.rate_limited + server.stats.forbidden + server.stats.misbehaving;
    boat_net_server_shutdown(&server);
    boat_net_memory_destroy(network);
    return 0;
}

void fuzz_seeds(FuzzAdd add)
{
    // The room's epoch and a sequence well ahead of the joined client's, so the server takes
    // the messages as current rather than stale.
    BoatNetMessage commands[] = {
        { .header = { .type = BOAT_NET_PING }, .clock = 1 },
        { .header = { .type = BOAT_NET_SPAWN_REQUEST }, .spawn = { .request = 1, .life = 1 } },
        { .header = { .type = BOAT_NET_RESYNC_REQUEST }, .resync = { .reason = 1 } },
        { .header = { .type = BOAT_NET_INPUT_BATCH } },
        { .header = { .type = BOAT_NET_READY }, .checkpoint = 0 },
    };
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        commands[i].header.epoch = 0x5eed;
        commands[i].header.sequence = 0x1000 + (uint32_t)i;
    }
    uint8_t seed[1024];
    size_t used = 0;
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        uint8_t bytes[BOAT_NET_MAX_MESSAGE];
        size_t length = boat_net_encode(&commands[i], bytes);
        if (!length || used + 3 + length > sizeof(seed))
            continue;
        seed[used++] = (uint8_t)boat_net_channel(commands[i].header.type);
        seed[used++] = (uint8_t)(length >> 8);
        seed[used++] = (uint8_t)length;
        memcpy(seed + used, bytes, length);
        used += length;
    }
    add(seed, used);
}
