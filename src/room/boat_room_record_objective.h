#ifndef PROTEUS_BOAT_ROOM_RECORD_OBJECTIVE_H
#define PROTEUS_BOAT_ROOM_RECORD_OBJECTIVE_H

#include "room/boat_room_objective.h"

// Version 1, explicit little-endian fields. Includes the host's pending events and reloads,
// command inspection batch and diagnostics, in addition to the committed objective codec.
// The running source is supplied separately by the recording's pinned source record. Pending
// candidates are included so a restored pre-callback checkpoint can execute that exact callback.
#define BOAT_ROOM_RECORD_OBJECTIVE_BYTES_MAX (2 * BOAT_PLUGIN_SOURCE_MAX + 32768)

// Zero on invalid input or insufficient capacity. Neither function evaluates Lua or uses files.
size_t boat_room_record_objective_encode(
    const BoatRoomObjective *objective, uint8_t *out, size_t capacity);
// Failure leaves OUT unchanged. Success owns a copy of RUNNING_SOURCE and binds its pointer;
// callers moving OUT afterwards must rebind out->source = &out->owned.
bool boat_room_record_objective_decode(const uint8_t *bytes, size_t size,
    const BoatRoomObjectiveSource *running_source, BoatRoomObjective *out);

#endif
