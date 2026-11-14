// H3 harness: the byte codecs that read untrusted network input, with no server state.
// A message the decoder accepts must re-encode and decode again (the codec's own contract).
#include "fuzz/fuzz_driver.h"
#include "net/boat_net_protocol.h"

#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static BoatNetMessage message, again;
    BoatNetHeader header;
    (void)boat_net_decode_header(data, size, &header);
    if (boat_net_decode(data, size, &message) == BOAT_NET_OK) {
        uint8_t out[BOAT_NET_MAX_MESSAGE];
        size_t length = boat_net_encode(&message, out);
        if (length == 0 || boat_net_decode(out, length, &again) != BOAT_NET_OK)
            abort(); // Accepted bytes must survive a round trip.
    }
    static BoatFleetState state;
    BoatNetSlotInput inputs[BOAT_FLEET_SLOTS];
    uint64_t race_go;
    (void)boat_net_checkpoint_decode(data, size, &state, inputs, &race_go);
    BoatNetLanRoom room;
    (void)boat_net_lan_query_decode(data, size);
    if (boat_net_lan_room_decode(data, size, &room)) {
        uint8_t out[BOAT_NET_DISCOVERY_BYTES];
        (void)boat_net_lan_room_encode(&room, out);
    }
    return 0;
}

void fuzz_seeds(FuzzAdd add)
{
    for (unsigned type = 1; type < BOAT_NET_TYPE_LIMIT; ++type) {
        BoatNetMessage message = { .header = { .type = (uint8_t)type } };
        if (type == BOAT_NET_HELLO)
            message.hello = (BoatNetHello) { .physics_revision = BOAT_PHYSICS_REVISION,
                .content_digest = boat_net_content_digest(),
                .name = "fuzz",
                .token = "room" };
        uint8_t bytes[BOAT_NET_MAX_MESSAGE];
        size_t length = boat_net_encode(&message, bytes);
        if (length) {
            add(bytes, length);
        } else { // A bare header still reaches the type's length check.
            memset(bytes, 0, 32);
            memcpy(bytes,
                (uint8_t[]) { 0x50, 0x42, 0x4e, 0x54, 0, BOAT_NET_VERSION, (uint8_t)type }, 7);
            add(bytes, 32);
        }
    }
    BoatNetLanRoom room = { .version = BOAT_NET_VERSION,
        .physics_revision = BOAT_PHYSICS_REVISION,
        .content_digest = boat_net_content_digest(),
        .epoch = 1,
        .port = 24680,
        .players = 1,
        .max_players = 4,
        .name = "fuzz" };
    uint8_t lan[BOAT_NET_DISCOVERY_BYTES];
    size_t length = boat_net_lan_room_encode(&room, lan);
    if (length)
        add(lan, length);
}
