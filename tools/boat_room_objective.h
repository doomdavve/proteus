#ifndef PROTEUS_BOAT_ROOM_OBJECTIVE_H
#define PROTEUS_BOAT_ROOM_OBJECTIVE_H

// Plugin P5b: a free package's room objective on the authority. The server reports crew joins,
// leaves and mark entries through BoatNetRules; this turns them into objective events (kind
// crew.joined, crew.left or mark.reached, with crew = slot + 1 and the mark's id) and runs the
// P3 objective transaction at every 12-tick boundary. A failing callback or a full event queue
// quarantines the objective; the room keeps running. Lua state never leaves the authority.
// P5e: a staged candidate source replaces the running one at the next boundary (see
// boat_room_objective_stage); the marks stay those the room was defined with.
#include "boat_net_server.h"
#include "boat_plugin_state.h"
#include "boat_room_package.h"

#define BOAT_ROOM_OBJECTIVE_CADENCE 12

typedef enum {
    BOAT_ROOM_RELOAD_NONE,
    BOAT_ROOM_RELOAD_MIGRATED, // Accepted; the state went through the candidate's migrate.
    BOAT_ROOM_RELOAD_RESTARTED, // Accepted; the candidate has no migrate, so it started afresh.
    BOAT_ROOM_RELOAD_REJECTED, // The candidate failed; nothing changed.
} BoatRoomReloadKind;

typedef struct {
    BoatRoomReloadKind kind;
    uint64_t tick; // The boundary that applied or rejected the candidate.
    BoatPluginDiagnostic diagnostic; // Why it was rejected.
} BoatRoomReload;

typedef struct {
    // The running source: borrowed from the caller until the first accepted reload, then OWNED.
    const BoatRoomObjectiveSource *source;
    BoatRoomObjectiveSource owned, pending; // Accepted reloads land in OWNED; PENDING is staged.
    bool has_pending;
    bool present[BOAT_FLEET_SLOTS]; // Crews in the room, from the joined and left events.
    BoatRoomReload reload; // The latest reload result.
    uint64_t reloads; // Results so far (accepted or rejected); a caller's change detector.
    unsigned revision; // Source revision: 1 at start, plus one per accepted reload.
    BoatPluginObjective objective; // The committed checkpoint and HUD.
    BoatPluginEvent events[BOAT_PLUGIN_EVENTS]; // Unacknowledged, oldest first.
    unsigned event_count;
    int64_t sequence; // Last event sequence assigned.
    uint64_t hud_changes; // Committed changes of the HUD rows (init and accepted reloads count).
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
// Stage CANDIDATE (copied) to replace the running source at the next cadence boundary, before
// that boundary's update; a newer stage replaces an older pending one. A candidate with a migrate
// callback carries the state over (generation + 1); one without restarts it and re-announces
// every crew present. Any failure leaves the running source, state, quarantine and queued events
// as they were; the result is in RELOAD. False with DIAGNOSTIC when the candidate is refused
// outright: it has no objective, or its marks differ (the room definition cannot change).
bool boat_room_objective_stage(BoatRoomObjective *objective,
    const BoatRoomObjectiveSource *candidate, BoatPluginDiagnostic *diagnostic);
// Run the update when TICK is a cadence boundary. True when it committed a batch.
bool boat_room_objective_after_tick(BoatRoomObjective *objective, uint64_t tick);
// The adapter for BoatNetServerConfig.rules.
BoatNetRules boat_room_objective_rules(BoatRoomObjective *objective);

#endif
