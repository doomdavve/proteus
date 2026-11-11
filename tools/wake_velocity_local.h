#ifndef WAKE_VELOCITY_LOCAL_H
#define WAKE_VELOCITY_LOCAL_H

// AR6 (ledger L0): the game's glue for private free boating as a local authority and client
// (src/boat_local.h). The game keeps its BoatSession for views, audio and HUD; this glue fills
// its boat, clock, presentation and arcade wake pool from the client's prediction, never from
// the authority. Native and (L4a) browser builds.
#include "boat_local.h"
#include "boat_profile.h"
#include "wake_velocity_course.h"
#include "wake_velocity_drive.h"
#include "wake_velocity_voyage.h"

// Zero-initialize.
typedef struct {
    BoatLocal local;
    bool active;
    // L2: a time trial (the session's course is enabled) runs its course on the authority.
    bool course_active;
    WakeCourseAuthority course;
    // L1: a voyage or tour (the session has a voyage route) runs its voyage on the authority.
    bool voyage_active;
    WakeVoyageAuthority voyage;
    // L1b: the active plugin revision whose objective the authority runs (borrowed; NULL for
    // none) and the generation the next open or restart starts it as. Set by the game.
    const BoatPluginRevision *revision;
    unsigned generation;
    // L1b: the host's revision table, through which the authority resolves a commit's revision.
    WakeVoyageResolve resolve;
    void *resolve_context;
    // L1b: the client's view of the authority's objective after each frame.
    WakeObjectiveView objective;
    // L4b: the authority's isolated objective evaluator (the browser's worker; NULL natively,
    // where the authority evaluates at once). Set by the game before opening.
    const WakeObjectiveWorker *worker;
    // L4b: a migration request is on its way to the worker; the next frame grants nothing.
    bool hold;
    // Test seam: called after each predicted tick has filled the session (NULL in the game).
    void (*traced)(void *context, const BoatSession *session, const BoatFleet *view);
    void *traced_context;
    // Test seam: scripted frames take this command source instead of the scripted drive.
    const BoatLocalScript *script;
} WakeLocal;

// Start a session from SESSION's location, water, scene, handling and boat and OBJECTS' pool
// recipe. WORLDS is the game's location table. A session with its course enabled is a time trial
// (L2): speedboat handling and the course adapter on the authority, recording when SESSION
// records runs, against its best. A session with a voyage route runs the voyage adapter (L1).
// False with LOCAL's failure text.
bool wake_local_open(WakeLocal *local, const BoatSession *session, const ObjectStudy *objects,
    const BoatWorldResources *worlds, double now);
// After the game restarted SESSION and OBJECTS: a new generation from their reset state.
bool wake_local_restart(
    WakeLocal *local, const BoatSession *session, const ObjectStudy *objects, double now);
// L4b: whether the host must hold the clock: with a worker, until the client has seen the
// authority's objective after a start, while it reports an evaluation pending, and in the frame
// that carries a migration request. Read from the client's view only.
bool wake_local_waiting(const WakeLocal *local);
// Grant the ticks ACCUMULATOR holds (boat_time_grant, at most BOAT_LOCAL_MAX_GRANT; any excess
// stays for the next frame) and run them. L4b: with a worker and an objective, a grant ends at
// the next update boundary, and a waiting frame grants nothing and empties ACCUMULATOR (as the
// direct path's worker gate does) while the authority still services its evaluator; then copy the predicted pool into OBJECTS. Presentation
// steps per predicted tick against the pool the boat read. A time trial first latches SESSION's
// practice on the authority; afterwards SESSION's course shows the authority's state and a
// finish is settled into its runs for boat_session_flush. False on an endpoint failure (D10).
bool wake_local_frame(WakeLocal *local, BoatSession *session, ObjectStudy *objects,
    double *accumulator, BoatInput input, float thrust_target, float steering_target, double now,
    BoatFrameMetrics *metrics);
// L6: wake_local_frame with each tick's helm from the scripted drive (demo_input at the
// session's tick, from the predicted boat) instead of the player's.
bool wake_local_scripted_frame(WakeLocal *local, BoatSession *session, ObjectStudy *objects,
    double *accumulator, double now, BoatFrameMetrics *metrics);
// L6: the scripted drive's first TICKS ticks for a capture, granted in frames of at most
// BOAT_LOCAL_MAX_GRANT on virtual time (each grant at its starting tick's time).
bool wake_local_scripted_drive(
    WakeLocal *local, BoatSession *session, ObjectStudy *objects, uint64_t ticks);
// L1: between frames, recover the boat to the voyage's latest shelter, as boat_session_recover
// does: SESSION shows the recovered boat, presentation and voyage at once, and the authority
// applies the same recovery before the next frame's first tick. False on an endpoint failure.
bool wake_local_recover(WakeLocal *local, BoatSession *session, ObjectStudy *objects);
// L1b: between frames, after the host committed REVISION with CHANGE into SESSION (its view: the
// voyage rebound or restarted, the boat at the tour start when MOVED), the authority installs
// the same revision before the next frame's first tick. A migration's outcome arrives in
// LOCAL's objective view after that frame (its commit and commit_status); until then SESSION's
// revision must not change. L4b: with a worker the migration runs there, the clock is held from
// the request until the outcome, which can take several frames. False on an endpoint failure.
bool wake_local_commit(WakeLocal *local, BoatSession *session, ObjectStudy *objects,
    const BoatPluginRevision *revision, BoatPluginChange change, bool moved);
// L4b: between frames, cancel the migration to REVISION the authority's worker is running (the
// host discarded its candidate); the authority closes it at the next frame's start, keeping its
// revision and objective. False on an endpoint failure.
bool wake_local_cancel(WakeLocal *local, const BoatSession *session, unsigned revision);
// L1b: ask the authority for its objective's inspection lines; they arrive within the next frame
// (boat_local_inspection). False on an endpoint failure.
bool wake_local_inspect(WakeLocal *local);
void wake_local_close(WakeLocal *local);

#endif
