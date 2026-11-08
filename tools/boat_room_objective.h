#ifndef PROTEUS_BOAT_ROOM_OBJECTIVE_H
#define PROTEUS_BOAT_ROOM_OBJECTIVE_H

// Plugin P5b: a free package's room objective on the authority. The server reports crew joins,
// leaves and mark entries through BoatNetRules; this turns them into objective events (kind
// crew.joined, crew.left or mark.reached, with crew = slot + 1 and the mark's id) and runs the
// P3 objective transaction at every 12-tick boundary. A failing callback or a full event queue
// quarantines the objective; the room keeps running. Lua state never leaves the authority.
#include "boat_net_server.h"
#include "boat_plugin_state.h"
#include "boat_room_package.h"

#define BOAT_ROOM_OBJECTIVE_CADENCE 12

typedef struct {
    const BoatRoomObjectiveSource *source; // Borrowed for the objective's lifetime.
    BoatPluginObjective objective; // The committed checkpoint and HUD.
    BoatPluginEvent events[BOAT_PLUGIN_EVENTS]; // Unacknowledged, oldest first.
    unsigned event_count;
    int64_t sequence; // Last event sequence assigned.
    uint64_t hud_changes; // Committed batches that changed the HUD rows (init counts).
    bool hud_changed; // The latest boundary changed the HUD; cleared by the caller.
    uint64_t reported_hud_changes; // What the rules adapter's state callback last reported.
    bool reported_quarantined;
    BoatPluginDiagnostic diagnostic; // Why the objective was quarantined.
} BoatRoomObjective;

// Initialise at tick zero with generation 1. False when init fails: the objective is then
// quarantined and every later call is a no-op.
bool boat_room_objective_init(BoatRoomObjective *objective, const BoatRoomObjectiveSource *source);
void boat_room_objective_event(
    BoatRoomObjective *objective, uint64_t tick, const BoatNetRuleEvent *event);
// Run the update when TICK is a cadence boundary. True when it committed a batch.
bool boat_room_objective_after_tick(BoatRoomObjective *objective, uint64_t tick);
// The adapter for BoatNetServerConfig.rules.
BoatNetRules boat_room_objective_rules(BoatRoomObjective *objective);

#endif
