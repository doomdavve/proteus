#ifndef PROTEUS_BOAT_PLUGIN_STATE_H
#define PROTEUS_BOAT_PLUGIN_STATE_H

#include "boat_plugin.h"

// P3.1 transaction engine; P3.2 grants objective.hud for the native solo voyage adapter.
#define BOAT_PLUGIN_STATE_BYTES 16384
#define BOAT_PLUGIN_STATE_DEPTH 16
#define BOAT_PLUGIN_STATE_TABLES 256
#define BOAT_PLUGIN_EVENTS 64
#define BOAT_PLUGIN_HUD 8
#define BOAT_PLUGIN_COMMANDS 32

typedef struct {
    unsigned size;
    unsigned char bytes[BOAT_PLUGIN_STATE_BYTES];
} BoatPluginState;

typedef struct {
    int64_t tick, sequence;
    char kind[48], id[BOAT_PLUGIN_TEXT];
} BoatPluginEvent;

typedef struct {
    char id[BOAT_PLUGIN_TEXT], label[BOAT_PLUGIN_TEXT];
    int64_t value;
} BoatPluginHud;

// Caller-owned checkpoint. No Lua pointers; assignment copies the entire committed result.
// One owner, generation fixed by host. Failed calls retain all fields except quarantine.
typedef struct {
    BoatPluginState state;
    BoatPluginHud hud[BOAT_PLUGIN_HUD];
    unsigned hud_count;
    uint32_t rng;
    int64_t acknowledged, tick;
    unsigned generation;
    bool initialized, quarantined;
} BoatPluginObjective;

typedef enum {
    BOAT_PLUGIN_OBJECTIVE_INIT,
    BOAT_PLUGIN_OBJECTIVE_UPDATE,
    BOAT_PLUGIN_OBJECTIVE_MIGRATE,
    BOAT_PLUGIN_OBJECTIVE_RESET,
} BoatPluginTransition;

// Immutable accepted source is supplied by the review harness, not acquired from disk here.
// INIT/RESET call init(context); UPDATE calls update(context, state); MIGRATE calls
// migrate(context, state). Return state, commands. A candidate migration failure leaves even
// quarantine unchanged. Other failures suspend callbacks; RESET is the explicit recovery path.
// UPDATE dispatches only at ticks divisible by 12, strictly after the last committed tick.
// Events must be ordered by tick/sequence, newer than ACKNOWLEDGED and no later than TICK.
BoatPluginStatus boat_plugin_objective_step(BoatPluginObjective *objective, const char *source,
    size_t size, BoatPluginTransition transition, int64_t tick, const BoatPluginEvent *events,
    unsigned event_count, const BoatPluginLimits *limits, BoatPluginDiagnostic *diagnostic);
// Canonical hash of the complete committed checkpoint, excluding quarantine/struct padding.
uint64_t boat_plugin_objective_digest(const BoatPluginObjective *objective);

#endif
