#ifndef PROTEUS_BOAT_PLUGIN_STATE_H
#define PROTEUS_BOAT_PLUGIN_STATE_H

#include "boat_plugin_objective.h"

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
// With TIMERS_ENABLED, due timers are delivered separately in context.timers (tick, id order).
// Due consumption commits with the batch; migration preserves them, INIT/RESET retires them.
BoatPluginStatus boat_plugin_objective_step(BoatPluginObjective *objective, const char *source,
    size_t size, BoatPluginTransition transition, int64_t tick, const BoatPluginEvent *events,
    unsigned event_count, const BoatPluginLimits *limits, BoatPluginDiagnostic *diagnostic);
// Candidate-only migration: success replaces generation/schema/grant atomically. Removing
// the timer grant retires its timers; retained resources can be removed by migration commands.
BoatPluginStatus boat_plugin_objective_migrate(BoatPluginObjective *objective, const char *source,
    size_t size, unsigned version, unsigned generation, bool timers, int64_t tick,
    BoatPluginDiagnostic *diagnostic);
// Deterministic HUD composition in caller-selected owner order. Local IDs are namespaced by
// OWNER; duplicate owners or insufficient capacity reject without writing output/count.
typedef struct {
    const char *owner;
    const BoatPluginObjective *objective;
} BoatPluginHudOwner;
typedef struct {
    char owner[64];
    BoatPluginHud row;
} BoatPluginHudRow;
bool boat_plugin_hud_compose(const BoatPluginHudOwner *owners, unsigned owner_count,
    BoatPluginHudRow *rows, unsigned capacity, unsigned *count);
// Canonical hash of the complete committed checkpoint, excluding quarantine/struct padding.
uint64_t boat_plugin_objective_digest(const BoatPluginObjective *objective);

#endif
