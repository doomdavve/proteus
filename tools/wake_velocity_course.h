#ifndef WAKE_VELOCITY_COURSE_H
#define WAKE_VELOCITY_COURSE_H

// L2: the time trial on a private local session. WakeCourseAuthority is the authority's rules
// adapter (application code: it records replays, which proteus_boat_net may not): it observes
// each committed tick read-only, steps the course, records the run with its own camera-follow
// copy, settles a finish (best selection and practice) at its tick and queues one finish request
// with the copied run. Its course state reaches the client as private state bytes
// (wake_course_view). The host takes the finish request after the frame into the session's runs,
// whose existing flush is the storage adapter. No drawing, input or files.
#include "boat_net_server.h"
#include "boat_session.h"

#define WAKE_COURSE_STATE_BYTES 74

typedef struct {
    // Configuration, set by the host for one session generation.
    const BoatCourseDefinition *definition;
    unsigned slot; // The player's fleet slot.
    bool waves; // The world's water is displaced (the recording's water).
    bool record; // Record and settle runs (false for captures).
    bool follow; // The presentation follows the heading (its camera-follow copy).
    bool has_best;
    double best; // The best run's time; a faster finish replaces it.
    // State.
    BoatCourse course;
    Boat previous; // The committed boat before the next tick.
    float camera_heading; // The recorder's copy of the presentation's camera follow.
    BoatReplay recording;
    bool practice, finished;
    // The pending finish request: one run, moved to the host by wake_course_take.
    bool pending, pending_faster, pending_practice;
    BoatReplay run;
} WakeCourseAuthority;

// A new generation at the course start: SESSION's course definition, boat (the spawn), water,
// presentation and runs (its best and record_runs). Releases any earlier recording.
void wake_course_init(WakeCourseAuthority *course, const BoatSession *session, unsigned slot);
void wake_course_destroy(WakeCourseAuthority *course);
// The rules adapter for the local authority: committed ticks and private state.
BoatNetRules wake_course_rules(WakeCourseAuthority *course);
// Host management before a frame's ticks: latch practice for the attempt (no effect once
// finished), as altered time or console use does.
void wake_course_practice(WakeCourseAuthority *course);
// Persistence capture after a frame: settle a pending finish into RUNS (last, best, notice and
// the save/export requests boat_replay_runs_flush performs). False when none was pending.
bool wake_course_take(WakeCourseAuthority *course, BoatReplayRuns *runs);
// The course state codec.
size_t wake_course_encode(const WakeCourseAuthority *course, uint8_t *out);
// The client's view: decode STATE into SESSION's course progress and practice latch, and report a
// stopped recording. False (SESSION unchanged) for invalid bytes.
bool wake_course_view(const uint8_t *state, size_t size, BoatSession *session);

#endif
