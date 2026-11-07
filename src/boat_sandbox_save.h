#ifndef PROTEUS_BOAT_SANDBOX_SAVE_H
#define PROTEUS_BOAT_SANDBOX_SAVE_H

// SV1b: the byte format of a sandbox save and its compatibility verdict. Byte buffers only: no
// files, no Lua, no drawing. Files and automatic saving are SV1c/SV1d.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boat_cargo.h"
#include "boat_plugin_state.h"
#include "boat_scene_definition.h"

#define BOAT_SANDBOX_SAVE_FORMAT 1
#define BOAT_SANDBOX_SAVE_MAX 32768
#define BOAT_SANDBOX_SAVE_HEADER_MAX 512

typedef struct {
    char package[64], build[64];
    uint64_t work_digest, source_hash; // SOURCE_HASH and BUILD: messages only, never compared.
    unsigned state_version, physics_revision, tick_hz;
} BoatSandboxIdentity;

typedef struct {
    BoatSandboxIdentity identity;
    char name[64], location[16];
    int64_t saved_at; // Unix seconds from the application; display only.
    uint64_t tick;
    unsigned load, built, sites;
    char progress_label[BOAT_PLUGIN_TEXT];
    int64_t progress_value;
    bool has_progress;
} BoatSandboxSummary;

typedef struct {
    BoatSandboxSummary summary;
    char site_ids[BOAT_SCENE_WORK_SITES][BOAT_PLUGIN_TEXT]; // Index order of the snapshot.
    BoatCargoSnapshot cargo;
    unsigned guide, target; // Site index, or cargo.crane_count for none.
    bool has_objective;
    BoatPluginObjective objective;
    BoatPluginEvent events[BOAT_PLUGIN_EVENTS];
    unsigned event_count, generation;
    int64_t sequence;
} BoatSandboxSave;

typedef enum {
    BOAT_SANDBOX_SAVE_OK,
    BOAT_SANDBOX_SAVE_TRUNCATED,
    BOAT_SANDBOX_SAVE_MAGIC,
    BOAT_SANDBOX_SAVE_NEWER,
    BOAT_SANDBOX_SAVE_CHECKSUM,
    BOAT_SANDBOX_SAVE_MALFORMED
} BoatSandboxSaveStatus;

typedef enum { BOAT_SANDBOX_RESUME, BOAT_SANDBOX_MIGRATE, BOAT_SANDBOX_REJECT } BoatSandboxVerdict;

// Encode SAVE (little-endian, field by field). Fails when a field is out of bounds or the
// capacity is too small. A successful encode never exceeds BOAT_SANDBOX_SAVE_MAX bytes.
bool boat_sandbox_save_encode(
    const BoatSandboxSave *save, unsigned char *out, size_t capacity, size_t *size);
// Decode and validate a whole file. Leaves OUT untouched unless the status is OK.
BoatSandboxSaveStatus boat_sandbox_save_decode(
    const unsigned char *bytes, size_t size, BoatSandboxSave *out);
// Header only, for discovery: needs at most BOAT_SANDBOX_SAVE_HEADER_MAX leading bytes.
BoatSandboxSaveStatus boat_sandbox_save_peek(
    const unsigned char *bytes, size_t size, BoatSandboxSummary *out);
// Compare the saved identity with the current one. REASON gets a short upper-case phrase when
// the verdict is REJECT. Only the package, physics revision, tick rate, work digest and state
// version are compared; SOURCE_HASH and BUILD never are.
BoatSandboxVerdict boat_sandbox_save_check(const BoatSandboxIdentity *saved,
    const BoatSandboxIdentity *current, bool current_migrates, char *reason, size_t capacity);
// FNV-1a 64 over a package's entry source, for messages.
uint64_t boat_sandbox_source_hash(const char *source, size_t size);

#endif
