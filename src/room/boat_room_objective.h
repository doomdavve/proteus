#ifndef PROTEUS_BOAT_ROOM_OBJECTIVE_H
#define PROTEUS_BOAT_ROOM_OBJECTIVE_H

// Plugin P5b: a free package's room objective on the authority. The server reports crew joins,
// leaves and mark entries through BoatNetRules; this turns them into objective events (kind
// crew.joined, crew.left or mark.reached, with crew = slot + 1 and the mark's id) and runs the
// P3 objective transaction at every 12-tick boundary. A failing callback or a full event queue
// quarantines the objective; the room keeps running. Lua state never leaves the authority.
// P5e: a staged candidate source replaces the running one at the next boundary (see
// boat_room_objective_stage); the marks stay those the room was defined with.
// P5f: a candidate with other marks revises the room definition instead (see
// boat_room_objective_stage_revision) and the objective restarts on it when the server commits.
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
    BoatRoomObjectiveSource revision_source; // Waits for the server to commit its definition.
    bool has_revision;
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
// outright: it has no objective, or its marks differ (that is a definition revision: use
// boat_room_objective_stage_revision). While a revision is waiting for its commit, a candidate
// with the same marks replaces the revision's source instead.
bool boat_room_objective_stage(BoatRoomObjective *objective,
    const BoatRoomObjectiveSource *candidate, BoatPluginDiagnostic *diagnostic);
// P5f: stage DEFINITION (the candidate package's room definition, whose marks CANDIDATE carries)
// as a definition revision on SERVER. The candidate source must start; the server refuses a
// definition it cannot revise to. When the server commits (free rooms: when empty), the objective
// restarts on CANDIDATE with its marks (crews present are announced again, queued mark events
// are dropped), through the rules adapter's definition callback. A newer revision replaces a
// waiting one. False with DIAGNOSTIC leaves everything as it was.
bool boat_room_objective_stage_revision(BoatRoomObjective *objective, BoatNetServer *server,
    const BoatRoomDefinition *definition, const BoatRoomObjectiveSource *candidate,
    BoatPluginDiagnostic *diagnostic);
// Run the update when TICK is a cadence boundary. True when it committed a batch.
bool boat_room_objective_after_tick(BoatRoomObjective *objective, uint64_t tick);
// The adapter for BoatNetServerConfig.rules.
BoatNetRules boat_room_objective_rules(BoatRoomObjective *objective);

#endif
