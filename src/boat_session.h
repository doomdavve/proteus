#ifndef PROTEUS_BOAT_SESSION_H
#define PROTEUS_BOAT_SESSION_H

#include "boat_replay.h"
#include "boat_voyage.h"

// Single-player owner: one world, one boat with its presentation, the only live clock, course
// progress and the race-session runs. Assignment clones a session whose runs hold no
// recordings; boat_session_destroy() releases recordings.
typedef struct {
    BoatWorld world;
    Boat boat;
    // Cosmetic follower stepped after each physics tick; physics never reads it. Disable it
    // for simulation-only runs; the physical trace is identical either way.
    BoatPresentation presentation;
    bool presentation_enabled;
    uint64_t tick; // Authoritative world clock.
    BoatCourse course; // Swept gate progress; disabled for free boating.
    // The definition's adventure destination and shelters, observed after every step; no
    // route outside the outer location. Restarts begin it again at the start shelter.
    BoatVoyage voyage;
    BoatVoyageEvents voyage_events; // Output of the latest step or recovery.
    // The route that init and restarts begin: the definition's compiled voyage unless the
    // application replaced it (Lua plugin P1). Borrowed; its owner outlives every use.
    const BoatVoyageRoute *voyage_route;
    // Recorded on course ticks while record_runs is set. A finish only updates memory; its
    // save/export waits for boat_session_flush() outside the tick loop.
    BoatReplayRuns runs;
    bool record_runs;
    // The boat's engine class for free boating; boat_session_init() sets the speedboat and
    // restarts keep it. Course ticks always use the speedboat, so runs and bests compare.
    const BoatHandling *handling;
    // PD1: the time trial's start and gates. Borrowed from the selected package revision and
    // outliving the session; NULL selects the compiled course.
    const BoatCourseDefinition *course_definition;
} BoatSession;

// Tick zero, the boat at the definition's spawn, a reset enabled presentation, no course or
// runs, and WATER as the world's initial state. The world borrows RESOURCES, which must be
// ready and outlive the session and every copy of it.
void boat_session_init(BoatSession *session, const BoatWorldResources *resources, BoatWater water);
// Flush first to keep a pending finish.
void boat_session_destroy(BoatSession *session);
// Enable the course and put a reset boat and presentation at its start.
void boat_session_start_course(BoatSession *session);
// Select the course before boat_session_start_course(): the definition is borrowed and must
// outlive every use. NULL restores the compiled S course.
void boat_session_set_course_definition(
    BoatSession *session, const BoatCourseDefinition *definition);
// A new attempt in the same location: flush, reinitialize clock, WATER, boat and presentation
// (at the course start when the course is enabled) and pin the course opponent. Runs and the
// borrowed resources survive.
void boat_session_restart(BoatSession *session, BoatWater water);
// Write a pending course finish. Never called from a tick.
void boat_session_flush(BoatSession *session);
void boat_session_set_tick(BoatSession *session, uint64_t tick);
double boat_session_time(const BoatSession *session);
// The live boat at the current tick with its course progress.
BoatViewState boat_session_view(const BoatSession *session);
// Put the boat back at the definition's spawn and reset its presentation; the clock and world
// water/history continue.
void boat_session_respawn(BoatSession *session);
// Return the boat to the voyage's most recently reached shelter with a reset presentation, or
// respawn it without a voyage. The clock, water and voyage progress continue.
void boat_session_recover(BoatSession *session);
// Replace the route the voyage borrows, keeping its progress, for a ROUTE whose gameplay is the
// current one's (only names differ). Later restarts begin ROUTE.
void boat_session_rebind_voyage(BoatSession *session, const BoatVoyageRoute *route);
// Begin ROUTE's voyage at the start shelter now: progress, timing and recoveries reset while the
// boat, clock, water and weather continue, so ROUTE's exposure must be the current one's. A
// boat already inside an area is observed on the next step. Later restarts begin ROUTE.
void boat_session_restart_voyage(BoatSession *session, const BoatVoyageRoute *route);
// Advance the clock once, step the boat at the new tick, sweep the course gates from its
// previous position, observe the voyage, step its presentation when enabled and record course
// runs when enabled.
// No file I/O. False at the clock limit.
bool boat_session_step(BoatSession *session, BoatInput input);

#endif
