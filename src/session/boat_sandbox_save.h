#ifndef PROTEUS_BOAT_SANDBOX_SAVE_H
#define PROTEUS_BOAT_SANDBOX_SAVE_H

// SV1b: the byte format of a sandbox save and its compatibility verdict. Byte buffers only: no
// files, no Lua, no drawing. Files and automatic saving are SV1c/SV1d.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/boat_cargo.h"
#include "core/boat_scene_definition.h"
#include "plugins/boat_plugin_objective.h"

#define BOAT_SANDBOX_SAVE_FORMAT 2
#define BOAT_SANDBOX_SAVE_MAX 32768
#define BOAT_SANDBOX_SAVE_HEADER_MAX 512

// Presentation clock shared by the save codec and session resume. Hours are unwrapped
// game hours (a cycle count survives a save so deterministic glitter continues), bounded to keep
// the clock finite over a very long session. The default cycle is 900 active scaled seconds.
#define BOAT_SANDBOX_LIGHTING_HOURS_DEFAULT 12.0
#define BOAT_SANDBOX_LIGHTING_PERIOD_DEFAULT 900.0
#define BOAT_SANDBOX_LIGHTING_HOURS_MAX 1.0e9
#define BOAT_SANDBOX_LIGHTING_PERIOD_MIN 1.0
#define BOAT_SANDBOX_LIGHTING_PERIOD_MAX 86400.0

typedef struct {
    char package[64], build[64];
    uint64_t work_digest, source_hash; // SOURCE_HASH and BUILD: messages only, never compared.
    unsigned state_version, physics_revision, tick_hz;
    bool migrates; // The current package has a migrate callback; never saved or compared.
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
    // Required presentation clock (format 2+). Format 1 records decode to noon / 900 seconds.
    double lighting_hours, lighting_cycle_seconds;
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

// True when a presentation clock is finite and in bounds: hours 0..1e9, cycle 1..86400 seconds.
bool boat_sandbox_lighting_valid(double hours, double period);

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

#define BOAT_AUTOSAVE_PERIOD 3600 /* simulation ticks of dirty play between periodic writes */
typedef struct {
    bool dirty, milestone;
    uint64_t last_write;
} BoatAutosave;
/* Record what one frame changed. CHANGED marks the session dirty; MILESTONE also requests a write
this frame. The first change after a clean state starts the periodic clock at TICK. */
void boat_autosave_note(BoatAutosave *autosave, uint64_t tick, bool changed, bool milestone);
/* True when a write is due now. LEAVING (pause, focus loss, return to title, quit) writes whenever
dirty. A clean session never writes; a milestone writes at once; otherwise write once
BOAT_AUTOSAVE_PERIOD ticks have passed since last_write (use tick >= last_write guard against
underflow). */
bool boat_autosave_due(const BoatAutosave *autosave, uint64_t tick, bool leaving);
/* After a write attempt at TICK. Success clears dirty and milestone. Failure keeps dirty, clears
milestone, and restarts the periodic clock at TICK, so it retries at the next milestone, leave or
period. Both set last_write = tick. */
void boat_autosave_written(BoatAutosave *autosave, uint64_t tick, bool ok);

#endif
