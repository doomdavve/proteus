#include "boat_net_mux.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "line %d: %s\n", __LINE__, #condition);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
int main(void)
{
    BoatNetMemory *memory[2] = { boat_net_memory_create(1), boat_net_memory_create(2) };
    CHECK(memory[0] && memory[1]);
    BoatNetMux mux;
    boat_net_mux_init(&mux, boat_net_memory_server(memory[0]), boat_net_memory_server(memory[1]));
    BoatNetTransport server = boat_net_mux_transport(&mux), clients[2];
    unsigned local[2], global[2];
    BoatNetEvent event;
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(boat_net_memory_client(memory[i], &local[i], &clients[i]));
        CHECK(local[i] == 0); // Deliberately colliding transport-local peer IDs.
        CHECK(boat_net_poll(&server, &event) && event.kind == BOAT_NET_EVENT_CONNECT);
        global[i] = event.peer;
        CHECK(boat_net_poll(&clients[i], &event) && event.kind == BOAT_NET_EVENT_CONNECT);
    }
    CHECK(global[0] != global[1]);
    for (unsigned channel = 0; channel < BOAT_NET_CHANNELS; ++channel) {
        for (unsigned i = 0; i < 2; ++i) {
            unsigned char value = (unsigned char)(10 * i + channel);
            CHECK(boat_net_send(&clients[i], 0, (BoatNetChannel)channel, &value, 1));
            CHECK(boat_net_poll(&server, &event) && event.kind == BOAT_NET_EVENT_MESSAGE);
            CHECK(event.peer == global[i] && event.channel == channel && event.data[0] == value);
            CHECK(boat_net_send(&server, global[i], event.channel, event.data, event.size));
            CHECK(boat_net_poll(&clients[i], &event) && event.data[0] == value);
            CHECK(!boat_net_poll(&clients[i ^ 1], &event));
        }
    }
    server.disconnect(server.context, global[0]);
    CHECK(boat_net_poll(&server, &event) && event.kind == BOAT_NET_EVENT_DISCONNECT);
    CHECK(event.peer == global[0]);
    CHECK(!boat_net_send(&server, global[0], BOAT_NET_CONTROL, "x", 1));
    // The other transport's identically numbered peer remains connected.
    CHECK(boat_net_send(&server, global[1], BOAT_NET_CONTROL, "y", 1));
    CHECK(boat_net_poll(&clients[1], &event) && event.data[0] == 'y');
    CHECK(server.stats(server.context).received == 2 * BOAT_NET_CHANNELS);
    CHECK(server.stats(server.context).sent == 2 * BOAT_NET_CHANNELS + 1);
    for (unsigned i = 0; i < 2; ++i)
        boat_net_memory_destroy(memory[i]);
    puts("boat_net_mux=ok");
    return 0;
}
