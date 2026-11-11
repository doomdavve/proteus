#ifndef WAKE_VELOCITY_OBJECTIVE_H
#define WAKE_VELOCITY_OBJECTIVE_H

// A voyage plugin's objective transaction, owned by whichever side runs it: the direct path's
// WakePlugin (the browser and the review tools) or the local authority's voyage adapter (L1b).
// It queues the voyage's events after each tick, runs the update at every 12-tick boundary,
// stages its batch for the next tick and commits it before that tick; a failing callback or a
// full queue quarantines it. The revision is borrowed and never written. No drawing, input or
// files; the native functions run Lua (the browser evaluates in its isolated worker instead).
#include "core/boat_voyage.h"
#include "plugins/boat_plugin_eval.h"
#include "plugins/boat_plugin_revision.h"
#include "plugins/boat_plugin_state.h"

#define WAKE_PLUGIN_TRACE 16
#define WAKE_OBJECTIVE_CADENCE 12

typedef struct {
    int64_t tick;
    unsigned revision, events;
    BoatPluginTransition transition;
    BoatPluginStatus status;
    uint64_t before, after;
    BoatPluginCommand commands[BOAT_PLUGIN_COMMANDS];
    unsigned command_count;
} WakePluginTrace;

typedef struct {
    BoatPluginObjective objective, staged_objective;
    BoatPluginEvent events[BOAT_PLUGIN_EVENTS]; // Unacknowledged, oldest first.
    unsigned event_count, generation;
    int64_t sequence; // Last event sequence assigned.
    bool objective_pending; // STAGED_OBJECTIVE commits before the next tick.
    BoatPluginDiagnostic objective_diagnostic;
    WakePluginTrace trace[WAKE_PLUGIN_TRACE]; // Oldest first; bounded inspection history.
    unsigned trace_count;
} WakeObjective;

// The view of an objective that runs elsewhere (the local authority's, L1b): what its client
// received after the latest frame.
typedef enum {
    WAKE_COMMIT_NONE,
    WAKE_COMMIT_APPLIED,
    WAKE_COMMIT_FAILED, // A migration failed; the authority kept its revision.
    WAKE_COMMIT_CANCELED, // L4b: the host canceled a migration in flight; nothing changed.
} WakeCommitStatus;

typedef struct {
    bool valid; // A HUD and the private state's objective tail have arrived.
    unsigned generation, revision; // The objective's generation and the authority's revision.
    unsigned commit, commits; // The latest commit request's revision number, requests handled,
    // and the latest one's result.
    WakeCommitStatus commit_status;
    bool pending; // L4b: an isolated evaluation is in flight; the host must hold the clock.
    bool initialized; // L4b: the objective's INIT has completed.
    bool suspended;
    // The suspension's, or a failed migration's, diagnostic text.
    char message[sizeof(((BoatPluginDiagnostic *)0)->message)];
    BoatPluginHud hud[BOAT_PLUGIN_HUD];
    unsigned hud_count;
} WakeObjectiveView;

// Append a trace entry for REVISION's ACTION from digest BEFORE to AFTER.
void wake_objective_trace(WakeObjective *run, unsigned revision, BoatPluginTransition action,
    uint64_t before, const BoatPluginObjective *after, unsigned events);
// A new generation (the counter advances) with nothing queued. True when REVISION (a voyage
// revision, or NULL for none) has an objective whose INIT must now run at TICK; false when it has
// none, or when the clock or generation is exhausted (then it is quarantined).
bool wake_objective_clear(WakeObjective *run, const BoatPluginRevision *revision, uint64_t tick);
// Commit the batch staged for this tick and retire the events it acknowledged.
void wake_objective_before_tick(WakeObjective *run);
// Queue the voyage's EVENTS of TICK (REVISION names their shelters). True when TICK is an update
// boundary the objective must now evaluate; false otherwise, or after quarantine.
bool wake_objective_queue(WakeObjective *run, const BoatPluginRevision *revision, uint64_t tick,
    const BoatVoyageEvents *events);
#ifndef __EMSCRIPTEN__
// Native evaluation (Lua). INIT after wake_objective_clear returned true.
void wake_objective_init(WakeObjective *run, const BoatPluginRevision *revision, uint64_t tick);
// The update after wake_objective_queue returned true.
void wake_objective_update(WakeObjective *run, const BoatPluginRevision *revision, uint64_t tick);
// Run CANDIDATE's migrate on the committed checkpoint at TICK into MIGRATED (the next
// generation). False (with the diagnostic and a trace under ACTIVE) when it fails. Nothing
// else changes either way; a pending batch stays pending.
bool wake_objective_migrate(WakeObjective *run, const BoatPluginRevision *candidate,
    unsigned active, uint64_t tick, BoatPluginObjective *migrated);
#endif
// L4b: an isolated evaluator: the browser's objective worker (wake_velocity_plugin_web.h), or a
// validator's model of it. Each job evaluates one encoded request; see those functions.
typedef struct {
    unsigned (*start)(const unsigned char *bytes, unsigned size); // 0 when unavailable.
    int (*status)(unsigned job); // 0 pending, 1 replied, negative failed.
    int (*read)(unsigned job, unsigned char *bytes); // The reply's size, or -1.
    void (*close)(unsigned job); // Late replies are inert.
} WakeObjectiveWorker;

#define WAKE_OBJECTIVE_DEADLINE 5.0 // Seconds of elapsed time a job may take.

// One evaluation in flight on an isolated evaluator (zero for none).
typedef struct {
    unsigned job;
    double deadline;
    BoatPluginEvalRequest *request; // Owned while the job is open.
} WakeObjectiveJob;

// Start ACTION (INIT after wake_objective_clear returned true, UPDATE after wake_objective_queue
// did, or MIGRATE to candidate REVISION) of REVISION at TICK on WORKER at NOW; ACTIVE is the
// active revision's number (REVISION's own but for a migration) and EPOCH identifies the run.
// NOW may be NaN: the deadline then counts from the first poll. False when no job could start:
// then an INIT or UPDATE quarantines the objective, as a failed callback would; a MIGRATE keeps
// it, with its diagnostic and trace as wake_objective_migrate's failure leaves them.
bool wake_objective_begin(WakeObjective *run, WakeObjectiveJob *job,
    const WakeObjectiveWorker *worker, BoatPluginTransition action,
    const BoatPluginRevision *revision, unsigned active, uint64_t epoch, uint64_t tick, double now);
// Collect an INIT or UPDATE JOB's reply at NOW. True when it resolved: an INIT's result becomes
// the objective, an UPDATE's is staged for the next tick (as wake_objective_init and
// wake_objective_update leave them), with the same trace; a failed callback, an invalid or stale
// reply or the deadline quarantines it. False while the job is pending (or is a migration).
bool wake_objective_poll(
    WakeObjective *run, WakeObjectiveJob *job, const WakeObjectiveWorker *worker, double now);
// L4b: collect a MIGRATE JOB's reply at NOW: 1 with MIGRATED (the next generation, for
// wake_objective_adopt), -1 when it failed (the callback, an invalid or stale reply or the
// deadline; diagnostic and trace as wake_objective_migrate's failure), 0 while pending. Nothing
// else changes; a pending batch stays pending.
int wake_objective_poll_migration(WakeObjective *run, WakeObjectiveJob *job,
    const WakeObjectiveWorker *worker, double now, BoatPluginObjective *migrated);
// A tick committed while JOB was open: close it. An INIT or UPDATE quarantines the objective
// ("objective evaluation overrun"); a migration fails and keeps it ("migration overrun").
void wake_objective_overrun(
    WakeObjective *run, WakeObjectiveJob *job, const WakeObjectiveWorker *worker);
// Close JOB (if open) without applying anything.
void wake_objective_cancel(WakeObjectiveJob *job, const WakeObjectiveWorker *worker);
// The objective's inspection text under REVISION (the active revision's number) into OUT (at most
// CAPACITY bytes, terminated): its revision line, then (from *FIRST, the revision line's length)
// its rng, pending batch, HUD, timers, state bytes, queued events and trace. Returns the length
// written; a text that does not fit ends early at a line boundary.
size_t wake_objective_inspect(
    const WakeObjective *run, unsigned revision, char *out, size_t capacity, size_t *first);
// Commit MIGRATED under the new revision number REVISION: it becomes the objective, its
// generation the counter's, and any pending batch is dropped. Queued events stay for its next
// update.
void wake_objective_adopt(
    WakeObjective *run, unsigned revision, const BoatPluginObjective *migrated);

#endif
