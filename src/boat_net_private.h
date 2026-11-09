#ifndef PROTEUS_BOAT_NET_PRIVATE_H
#define PROTEUS_BOAT_NET_PRIVATE_H

// AR6: the private extension range. Messages a private (in-process) authority and its client
// exchange that v14 cannot express. They use the v14 header layout with a type in a reserved
// range that boat_net_decode() rejects, so a hosted server that received one would count it as
// malformed; hosted bytes and BOAT_NET_VERSION are unchanged. Remote peers never send them.
#include "boat_net_protocol.h"

// A private (in-process, single-thread) session of one authority and one client, set alike on
// both endpoints by their host. Zero for hosted rooms. See docs/ARCHITECTURE_AR1.md section 5:
// the host grants every tick (boat_net_server_grant), connection timeouts and the
// snapshot-silence close are off, and the client's stall rules count granted ticks.
typedef struct {
    bool enabled;
    BoatWater
        water; // The world's water (waves, current, crossing source); objects are the fleet's.
    const BoatHandling *handling; // Every slot's engine class; NULL for the speedboat.
    unsigned wake_life_ticks; // Wake object pool; zero for BOAT_OBJECT_LIFE_TICKS.
    float crest_height; // Zero for BOAT_OBJECT_DEFAULT_HEIGHT.
    Boat spawn; // Authority only: the accepted player's boat, placed before its checkpoint.
} BoatNetPrivateSession;

#define BOAT_NET_PRIVATE_FIRST 0xc0 // First type of the reserved private range.
#define BOAT_NET_PRIVATE_INPUT_BATCH 0xc0 // C->S: an InputBatch with full-precision rates (D4).

// True when DATA carries a v14 header whose type lies in the private range.
bool boat_net_private_message(const uint8_t *data, size_t size);
// The InputBatch BATCH with each command's float rates (BoatNetCommand.steering_rate and
// throttle_rate) instead of the v14 bytes; HEADER's type is ignored. Zero for an invalid batch.
size_t boat_net_private_encode_input(
    const BoatNetHeader *header, const BoatNetInputBatch *batch, uint8_t out[BOAT_NET_MAX_MESSAGE]);
// Decode a private InputBatch: every command is precise. Validates as the v14 batch does, and
// rejects non-finite rates. Never writes the destination on failure.
BoatNetError boat_net_private_decode_input(
    const uint8_t *data, size_t size, BoatNetHeader *header, BoatNetInputBatch *batch);

#endif
