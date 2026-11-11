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
// S->C on the reliable control channel (L2): the private rules adapter's state after a grant, as
// opaque bytes the adapter's own codec reads; HEADER.tick is the authority's tick.
#define BOAT_NET_PRIVATE_STATE 0xc1
#define BOAT_NET_PRIVATE_STATE_BYTES 256 // The largest private state.
// C->S on the reliable control channel (L1): recover the client's boat to the rules adapter's
// recovery pose at a frame boundary; HEADER.tick is the authority's tick it applies after. No
// payload.
#define BOAT_NET_PRIVATE_RECOVER 0xc2
// C->S on the reliable control channel (L1b): install a host-managed definition revision at a
// frame boundary; HEADER.tick is the authority's tick it applies after. Payload: the revision
// number (u32) and a change code (u8), both opaque to the protocol; the rules adapter resolves
// and interprets them.
#define BOAT_NET_PRIVATE_COMMIT 0xc3
#define BOAT_NET_PRIVATE_COMMIT_BYTES 5

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

// A private state message of SIZE (1..BOAT_NET_PRIVATE_STATE_BYTES) bytes of STATE; HEADER's type
// is ignored. Zero for an invalid size.
size_t boat_net_private_encode_state(const BoatNetHeader *header, const uint8_t *state, size_t size,
    uint8_t out[BOAT_NET_MAX_MESSAGE]);
// Decode a private state message into STATE and *STATE_SIZE. Never writes them on failure.
BoatNetError boat_net_private_decode_state(const uint8_t *data, size_t size, BoatNetHeader *header,
    uint8_t state[BOAT_NET_PRIVATE_STATE_BYTES], size_t *state_size);

// A private recover request; HEADER's type is ignored. Always BOAT_NET_HEADER_BYTES.
size_t boat_net_private_encode_recover(
    const BoatNetHeader *header, uint8_t out[BOAT_NET_MAX_MESSAGE]);
// The private type of DATA (an encoded private message), or zero when it carries none.
uint8_t boat_net_private_type(const uint8_t *data, size_t size);
// Decode a private recover request. Never writes HEADER on failure.
BoatNetError boat_net_private_decode_recover(
    const uint8_t *data, size_t size, BoatNetHeader *header);

// A private commit request; HEADER's type is ignored.
size_t boat_net_private_encode_commit(const BoatNetHeader *header, uint32_t revision,
    uint8_t change, uint8_t out[BOAT_NET_MAX_MESSAGE]);
// Decode a private commit request. Never writes the destinations on failure.
BoatNetError boat_net_private_decode_commit(
    const uint8_t *data, size_t size, BoatNetHeader *header, uint32_t *revision, uint8_t *change);

#endif
