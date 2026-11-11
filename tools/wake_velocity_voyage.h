#ifndef WAKE_VELOCITY_VOYAGE_H
#define WAKE_VELOCITY_VOYAGE_H

// L1: voyages and tours on a private local session. WakeVoyageAuthority is the authority's rules
// adapter: it observes each committed tick read-only and steps the voyage, as boat_session_step
// does; reports the weather the world's water carries from the next tick; and places a recovery
// at a frame boundary. Its voyage state reaches the client as private state bytes
// (wake_voyage_view); the route is an immutable resource both sides borrow. No drawing, input or
// files.
//
// L1b: with a plugin revision, the adapter also owns that revision's objective transaction
// (wake_velocity_objective.h): the voyage's events queue after each committed tick and every
// 12-tick boundary runs the update, as wake_plugin_after_tick does on the direct path. Its HUD
// and suspension reach the client as ObjectiveState (rules.state); its generation, the active
// revision, the latest commit's result and the diagnostic text follow the voyage's bytes in the
// private state. Lua state never leaves the authority. A commit request (rules.commit) installs
// a host-managed revision, which the adapter resolves by number and borrows: a rebind, a live
// route edit, a migration of the objective, or a voyage restart.
#include "boat_net_server.h"
#include "boat_session.h"
#include "wake_velocity_objective.h"

#define WAKE_VOYAGE_STATE_BYTES 40
// The objective's tail after the voyage's bytes: generation (4), active revision (4), the latest
// commit's revision (4), commits handled (4), the latest commit's status (1), flags (1, L4b:
// WAKE_VOYAGE_PENDING, WAKE_VOYAGE_INITIALIZED), diagnostic text length (1) and that many bytes
// of text.
#define WAKE_VOYAGE_OBJECTIVE_BYTES 19
#define WAKE_VOYAGE_PENDING 1 // An isolated evaluation (or migration) is in flight.
#define WAKE_VOYAGE_INITIALIZED 2 // The objective's INIT has completed.

// The adapter's commit codes on the wire.
typedef enum {
    WAKE_VOYAGE_CANCEL = 0, // L4b: close the migration in flight to this revision; nothing changes.
    WAKE_VOYAGE_REBIND = 1, // Unchanged or renamed: the same route, the new revision's storage.
    WAKE_VOYAGE_LIVE, // A live route edit: progress kept, the latest shelter found by its id.
    WAKE_VOYAGE_MIGRATE, // The candidate's migrate runs on the objective; it may fail. With a
    // worker (L4b) the commit is deferred until the result arrives.
    WAKE_VOYAGE_RESTART, // The voyage restarts at its start shelter with a new objective.
} WakeVoyageCommit;

// The host-managed revision table: REVISION's storage while it is held or committed. Immutable
// while borrowed; NULL for an unknown number.
typedef const BoatPluginRevision *(*WakeVoyageResolve)(void *context, unsigned revision);

typedef struct {
    unsigned slot; // The player's fleet slot.
    BoatVoyage voyage; // Borrows the session's route.
    BoatVoyageEvents events; // The latest committed tick's or recovery's.
    // L1b: the borrowed active revision (NULL for none) and its objective, when it has one.
    const BoatPluginRevision *revision;
    WakeObjective objective;
    WakeVoyageResolve resolve;
    void *resolve_context;
    unsigned commit, commits; // The latest commit request's revision, requests handled,
    // its result and failure text.
    WakeCommitStatus commit_status;
    char commit_message[sizeof(((BoatPluginDiagnostic *)0)->message)];
    BoatNetObjectiveState reported; // The HUD last reported through rules.state.
    bool has_reported;
    // L4b: the isolated evaluator (borrowed; NULL evaluates natively, at once), its job in flight
    // and the latest service's elapsed time.
    const WakeObjectiveWorker *worker;
    WakeObjectiveJob job;
    double now;
    // L4b: the candidate whose migration runs on the worker (NULL for none), and the settled
    // outcome the server has not collected yet (completion).
    const BoatPluginRevision *migrating;
    BoatNetCommitResult completed;
    bool has_completed;
} WakeVoyageAuthority;

// A new generation from SESSION's voyage (its route and start state). The objective's trace and
// worker are kept (an evaluation in flight is closed); zero VOYAGE to clear them.
void wake_voyage_init(WakeVoyageAuthority *voyage, const BoatSession *session, unsigned slot);
// L1b: REVISION (a voyage revision, borrowed; NULL for none) is the active one, its objective
// (when it has one) starts as generation GENERATION at TICK, and commits resolve their revisions
// through RESOLVE.
void wake_voyage_revision(WakeVoyageAuthority *voyage, const BoatPluginRevision *revision,
    unsigned generation, uint64_t tick, WakeVoyageResolve resolve, void *resolve_context);
// Close the evaluation in flight, if any (the session ends).
void wake_voyage_close(WakeVoyageAuthority *voyage);
// The rules adapter for the local authority: committed ticks, weather, recovery, commits, the
// objective's HUD and private state, and (L4b) the isolated evaluator's service. With a worker,
// INIT and every boundary's UPDATE start a job; its result arrives in a later grant's service
// (INIT before any tick, an UPDATE staged for the tick after its boundary), so the host must not
// grant a tick while the private state reports the job pending.
BoatNetRules wake_voyage_rules(WakeVoyageAuthority *voyage);
// The voyage state codec.
size_t wake_voyage_encode(const BoatVoyage *voyage, uint8_t *out);
// Decode STATE into VOYAGE's progress, keeping its route. False (VOYAGE unchanged) for invalid
// bytes or a shelter outside the route.
bool wake_voyage_decode(const uint8_t *state, size_t size, BoatVoyage *voyage);
// The client's view: decode STATE into SESSION's voyage and the storm its water carries. False
// (SESSION unchanged) for invalid bytes.
bool wake_voyage_view(const uint8_t *state, size_t size, BoatSession *session);
// L1b: the objective tail of STATE into VIEW (generation, revisions, commit result and text) and
// HUD, the newest ObjectiveState (NULL before the first). False (VIEW unchanged) when STATE
// carries no tail or is malformed.
bool wake_voyage_objective_view(
    const uint8_t *state, size_t size, const BoatNetObjectiveState *hud, WakeObjectiveView *view);
// The client's weather reader for BoatLocalConfig.weather; CONTEXT is the borrowed route.
bool wake_voyage_weather(void *context, const uint8_t *state, size_t size, BoatStorm *storm);

#endif
