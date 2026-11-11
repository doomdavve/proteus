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

// L1b: what a private commit changes besides the adapter's own state. L4b: an adapter that cannot
// finish a commit at once sets DEFERRED and applies nothing; its outcome arrives later through
// BoatNetRules.completion.
typedef struct {
    Boat boat;
    bool moved, rescene, deferred;
    const BoatSceneDefinition *scene;
} BoatNetCommitResult;

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
// C->S on the reliable control channel (L1b): ask the rules adapter for its inspection text. No
// payload. Answered by BOAT_NET_PRIVATE_INSPECTION chunks.
#define BOAT_NET_PRIVATE_INSPECT 0xc4
// S->C on the reliable control channel (L1b): one chunk of the inspection text: u16 index, u16
// count, then up to BOAT_NET_PRIVATE_INSPECTION_CHUNK bytes. The text is at most
// BOAT_NET_PRIVATE_INSPECTION_BYTES.
#define BOAT_NET_PRIVATE_INSPECTION 0xc5
#define BOAT_NET_PRIVATE_INSPECTION_CHUNK 1024
#define BOAT_NET_PRIVATE_INSPECTION_BYTES 32768

// L3: the working harbour's private endpoint (src/session/boat_work_local.h). Its payloads are opaque to
// the protocol; the work endpoint's own codec reads them. All four travel on the reliable control
// channel. C->S: the frame's actions, helm targets and crane axes, applied before its first tick.
#define BOAT_NET_PRIVATE_WORK_FRAME 0xc6
// S->C: one committed tick's motion (boat, crate, cranes, clock, trail segments); HEADER.tick.
#define BOAT_NET_PRIVATE_WORK_MOTION 0xc7
// S->C: the job's HUD, notice and waiting text after a chunk, when they changed.
#define BOAT_NET_PRIVATE_WORK_STATUS 0xc8
// S->C: a page of the wake trail, sent with every (re)start's checkpoint.
#define BOAT_NET_PRIVATE_WORK_TRAIL 0xc9

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

// A private inspect request; HEADER's type is ignored. Always BOAT_NET_HEADER_BYTES.
size_t boat_net_private_encode_inspect(
    const BoatNetHeader *header, uint8_t out[BOAT_NET_MAX_MESSAGE]);
BoatNetError boat_net_private_decode_inspect(
    const uint8_t *data, size_t size, BoatNetHeader *header);
// Chunk INDEX of COUNT (1..32) of an inspection text, SIZE (at most
// BOAT_NET_PRIVATE_INSPECTION_CHUNK) bytes of TEXT. Zero for invalid arguments.
size_t boat_net_private_encode_inspection(const BoatNetHeader *header, unsigned index,
    unsigned count, const char *text, size_t size, uint8_t out[BOAT_NET_MAX_MESSAGE]);
// Decode an inspection chunk into INDEX, COUNT, TEXT and *SIZE. Never writes them on failure.
BoatNetError boat_net_private_decode_inspection(const uint8_t *data, size_t size,
    BoatNetHeader *header, unsigned *index, unsigned *count,
    char text[BOAT_NET_PRIVATE_INSPECTION_CHUNK], size_t *text_size);
// A private commit request; HEADER's type is ignored.
size_t boat_net_private_encode_commit(const BoatNetHeader *header, uint32_t revision,
    uint8_t change, uint8_t out[BOAT_NET_MAX_MESSAGE]);
// Decode a private commit request. Never writes the destinations on failure.
BoatNetError boat_net_private_decode_commit(
    const uint8_t *data, size_t size, BoatNetHeader *header, uint32_t *revision, uint8_t *change);

// L3: a private message of TYPE carrying SIZE (at most BOAT_NET_MAX_PAYLOAD) opaque bytes;
// HEADER's type is ignored. Zero for an invalid size or a type outside the private range.
size_t boat_net_private_encode_payload(const BoatNetHeader *header, uint8_t type,
    const uint8_t *payload, size_t size, uint8_t out[BOAT_NET_MAX_MESSAGE]);
// Decode a private message of TYPE into PAYLOAD (BOAT_NET_MAX_PAYLOAD bytes) and *SIZE. Never
// writes them on failure.
BoatNetError boat_net_private_decode_payload(const uint8_t *data, size_t size, uint8_t type,
    BoatNetHeader *header, uint8_t payload[BOAT_NET_MAX_PAYLOAD], size_t *payload_size);

#endif
