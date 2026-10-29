#ifndef PROTEUS_BOAT_PLUGIN_HOST_H
#define PROTEUS_BOAT_PLUGIN_HOST_H

// Lua plugin P1 (docs/PLUGIN_PLAN.md): the application's owner of one voyage or race plugin's
// revisions. It stages a candidate from immutable bytes, classifies it against the active revision,
// holds it until the application commits it between ticks, and keeps prior revisions for Undo.
// Nothing here reads files or the clock: the application acquires bytes and passes the time.
#include "boat_plugin.h"
#include "boat_plugin_io.h"
#include "boat_plugin_state.h"

#define BOAT_PLUGIN_UNDO 8 // Prior revisions that Undo can reinstall.
#define BOAT_PLUGIN_POLL 0.25 // Seconds between save polls.
#define BOAT_PLUGIN_SETTLE 0.3 // A save must stay unchanged this long before it is staged.

typedef struct {
    BoatPluginVoyage voyage;
    BoatPluginRace race; // Used instead of VOYAGE by a river race host.
    char source[BOAT_PLUGIN_SOURCE_MAX];
    size_t source_size;
    unsigned number; // 1 for the launch revision, then one more for each staged definition.
} BoatPluginRevision;

typedef struct {
    bool racing; // One descriptor kind for the lifetime of this host.
    const BoatWorldDefinition *location;
    const BoatMap *map; // LOCATION's compiled water, for staging checks.
    // Revisions never move: the session borrows the active one's route until the next commit,
    // and a slot is reused only after a later commit has rebound the session.
    BoatPluginRevision slots[BOAT_PLUGIN_UNDO + 2];
    unsigned history[BOAT_PLUGIN_UNDO + 1]; // Slot indices, oldest first; the last is active.
    unsigned depth;
    uint64_t epoch; // Monotonic commit identity, including Undo (prevents stale-base ABA).
    unsigned numbered; // The latest revision number given.
    // A staged definition waiting for its commit. CHANGE compares it with the revision active
    // when it was staged (BASE); an Undo candidate is the previous revision's own slot.
    bool held, undo;
    unsigned held_slot, base;
    BoatPluginChange change;
    // The latest staging failure, or BOAT_PLUGIN_OK.
    BoatPluginDiagnostic diagnostic;
} BoatPluginHost;

// Fixed native reservation includes all ten revision slots, whether live or retired. Payload
// counts only reachable history and a distinct held candidate; Undo never counts a slot twice.
typedef struct {
    size_t reserved_bytes, retained_source_bytes;
    unsigned retained_revisions;
} BoatPluginHostMemory;
BoatPluginHostMemory boat_plugin_host_memory(const BoatPluginHost *host);

// Load, check and activate BUNDLE as revision 1 for LOCATION. False with DIAGNOSTIC filled.
bool boat_plugin_host_start(BoatPluginHost *host, const BoatWorldDefinition *location,
    const BoatMap *map, const BoatPluginBundle *bundle);
const BoatPluginRevision *boat_plugin_host_active(const BoatPluginHost *host);
const BoatPluginRevision *boat_plugin_host_held(const BoatPluginHost *host);
// Stage BUNDLE in place of any held candidate. A failure fills DIAGNOSTIC and drops the held
// candidate; the active revision is never touched. Success holds a renamed or restarting
// candidate; an unchanged or incompatible one is not held. On success CHANGE says how it compares.
bool boat_plugin_host_stage(BoatPluginHost *host, const BoatPluginBundle *bundle);
// Internal isolated-worker completion only: checked decoded data, pinned to this commit epoch.
// Callers must not treat external bytes/digests as this trusted receipt. No Lua is evaluated.
bool boat_plugin_host_stage_decoded(
    BoatPluginHost *host, const BoatPluginRevision *candidate, unsigned base, uint64_t epoch);
// Hold the previous revision for reinstallation through the same rules. False without one.
bool boat_plugin_host_undo(BoatPluginHost *host);
void boat_plugin_host_discard(BoatPluginHost *host);
// Make the held candidate active and return it; NULL without one or when the revision it was
// staged against is no longer active. The caller rebinds every borrower of the old route now.
const BoatPluginRevision *boat_plugin_host_commit(BoatPluginHost *host);

// Native save detection: a poll every BOAT_PLUGIN_POLL seconds reads the package and keys its
// bytes (or its read failure). A new key that stays unchanged for BOAT_PLUGIN_SETTLE is staged
// once; a manifest and entry are therefore only staged together after both stop changing.
typedef struct {
    double next_poll, changed;
    uint64_t seen, staged;
} BoatPluginWatch;

// Start watching at NOW with the package already staged as KEY.
void boat_plugin_watch_start(BoatPluginWatch *watch, uint64_t key, double now);
// True when a poll is due at NOW; schedules the next.
bool boat_plugin_watch_due(BoatPluginWatch *watch, double now);
// Observe the package as KEY at NOW. True exactly once per settled change.
bool boat_plugin_watch_settled(BoatPluginWatch *watch, uint64_t key, double now);
// Mark KEY as staged by an explicit reload, so the watcher does not stage it again.
void boat_plugin_watch_staged(BoatPluginWatch *watch, uint64_t key);
// The key of a read: its bytes when STATUS is OK, otherwise its failure.
uint64_t boat_plugin_read_key(
    BoatPluginStatus status, const BoatPluginFiles *files, const BoatPluginDiagnostic *diagnostic);

#endif
