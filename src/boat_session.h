#ifndef PROTEUS_BOAT_SESSION_H
#define PROTEUS_BOAT_SESSION_H

#include "boat_replay.h"

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
    // Recorded on course ticks while record_runs is set. A finish only updates memory; its
    // save/export waits for boat_session_flush() outside the tick loop.
    BoatReplayRuns runs;
    bool record_runs;
} BoatSession;

// Tick zero, the boat at the definition's spawn, a reset enabled presentation, no course or
// runs, and WATER as the world's initial state. The world borrows RESOURCES, which must be
// ready and outlive the session and every copy of it.
void boat_session_init(BoatSession *session, const BoatWorldResources *resources, BoatWater water);
// Flush first to keep a pending finish.
void boat_session_destroy(BoatSession *session);
// Enable the course and put a reset boat and presentation at its start.
void boat_session_start_course(BoatSession *session);
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
// Advance the clock once, step the boat at the new tick, sweep the course gates from its
// previous position, step its presentation when enabled and record course runs when enabled.
// No file I/O. False at the clock limit.
bool boat_session_step(BoatSession *session, BoatInput input);

#endif
