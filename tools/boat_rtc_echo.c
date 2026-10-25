// Transport feasibility gate: echo only validated protocol bytes, before involving gameplay.
#include "boat_net_enet.h"
#include "boat_net_rtc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--fixture")) {
        BoatNetMessage message
            = { .header = { .type = BOAT_NET_PING, .sequence = 7 }, .clock = 123456 };
        uint8_t bytes[BOAT_NET_MAX_MESSAGE];
        size_t size = boat_net_encode(&message, bytes);
        FILE *file = fopen(argv[2], "wb");
        if (!file || !size)
            return 1;
        bool ok = fwrite(bytes, 1, size, file) == size;
        fclose(file);
        return ok ? 0 : 1;
    }
    char error[160];
    uint16_t port = 24682;
    if (argc > 1 && !boat_net_parse_port(argv[1], &port))
        return 2;
    BoatNetRtc *rtc = boat_net_rtc_listen("127.0.0.1", port, NULL, error, sizeof(error));
    if (!rtc) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    BoatNetTransport transport = boat_net_rtc_transport(rtc);
    double end = boat_net_clock() + 120;
    unsigned messages = 0;
    while (boat_net_clock() < end) {
        BoatNetEvent event;
        while (boat_net_poll(&transport, &event)) {
            if (event.kind != BOAT_NET_EVENT_MESSAGE)
                continue;
            BoatNetMessage message;
            if (boat_net_decode(event.data, event.size, &message) != BOAT_NET_OK)
                return 1;
            if (!boat_net_send(&transport, event.peer, event.channel, event.data, event.size))
                return 1;
            ++messages;
            printf("echo channel=%u bytes=%zu messages=%u\n", event.channel, event.size, messages);
            fflush(stdout);
        }
        boat_net_sleep(.001);
    }
    boat_net_rtc_close(rtc);
    return messages ? 0 : 1;
}
