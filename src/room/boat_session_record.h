#ifndef PROTEUS_BOAT_SESSION_RECORD_H
#define PROTEUS_BOAT_SESSION_RECORD_H

#include "room/boat_room_record_objective.h"

#define BOAT_SESSION_RECORD_VERSION 1
#define BOAT_SESSION_RECORD_HEADER 32
#define BOAT_SESSION_RECORD_BYTES_MAX (512 * 1024)

typedef enum {
    BOAT_SESSION_RECORD_IDENTITY = 1,
    BOAT_SESSION_RECORD_KEYFRAME,
    BOAT_SESSION_RECORD_INPUT,
    BOAT_SESSION_RECORD_RESULT,
    BOAT_SESSION_RECORD_MUTATION,
    BOAT_SESSION_RECORD_OBJECTIVE_BEFORE,
    BOAT_SESSION_RECORD_OBJECTIVE_AFTER,
    BOAT_SESSION_RECORD_EVENT,
    BOAT_SESSION_RECORD_DEFINITION_BEFORE,
    BOAT_SESSION_RECORD_DEFINITION_AFTER,
    BOAT_SESSION_RECORD_OBJECTIVE_NATIVE, // Suspension outside a callback (event overflow).
    BOAT_SESSION_RECORD_END,
} BoatSessionRecordKind;

// A failed sink permanently stops recording; the application reports the error and closes.
// No files, transport operations or Lua evaluation belong to this codec/recorder.
typedef bool (*BoatSessionRecordSink)(void *context, const uint8_t *bytes, size_t size);
typedef struct {
    BoatSessionRecordSink sink;
    void *context;
    const BoatRoomObjective *objective;
    bool failed, begun, quarantined;
    uint32_t sequence, revision, source_revision;
    uint64_t fleet_hash, race_go, key_tick, objective_hash;
    uint8_t bytes[BOAT_SESSION_RECORD_BYTES_MAX + BOAT_SESSION_RECORD_HEADER];
} BoatSessionRecorder;

bool boat_session_record_begin(BoatSessionRecorder *recorder, const BoatNetServer *server,
    const BoatRoomObjective *objective, BoatSessionRecordSink sink, void *context);
BoatNetRecord boat_session_record_adapter(BoatSessionRecorder *recorder);
bool boat_session_record_end(BoatSessionRecorder *recorder, const BoatNetServer *server);

typedef struct {
    uint32_t kind, sequence;
    uint64_t tick;
    const uint8_t *payload;
    size_t size, bytes;
} BoatSessionRecord;
// Validates one bounded framed record, including its checksum; leaves OUT unchanged on failure.
bool boat_session_record_decode(const uint8_t *bytes, size_t size, BoatSessionRecord *out);

typedef struct {
    BoatFleet fleet;
    BoatRoomDefinition room, callback_room; // Definition handed to a recorded revision callback.
    BoatRoomObjective objective, before;
    BoatNetObjectiveState hud;
    BoatWorldResources resources;
    uint32_t sequence, revision;
    uint64_t race_go, records, keyframes;
    bool identity, ready, ended, waves, awaiting_result, has_before, has_objective;
} BoatSessionPlayback;
// Playback applies recorded objective batches with no Lua. Exact step hashes are checked BEFORE
// any native-rule mutation record is installed. Keyframes verify the preceding reconstruction.
// START_AT_KEYFRAME permits an independently sought keyframe after IDENTITY.
bool boat_session_record_apply(
    BoatSessionPlayback *playback, const BoatSessionRecord *record, bool start_at_keyframe);
void boat_session_record_close(BoatSessionPlayback *playback);

#endif
