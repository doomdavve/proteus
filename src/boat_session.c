#include "boat_session.h"

static void reset(BoatSession *session)
{
    boat_reset_location(&session->boat, session->world.definition);
    boat_presentation_reset(&session->presentation);
    session->presentation_enabled = true;
    session->tick = 0;
    session->course = (BoatCourse) { 0 };
    session->runs = (BoatReplayRuns) { 0 };
    session->record_runs = false;
}

void boat_session_init(BoatSession *session, const BoatWorldResources *resources, BoatWater water)
{
    boat_world_init(&session->world, resources, water);
    reset(session);
}

void boat_session_destroy(BoatSession *session) { boat_replay_runs_destroy(&session->runs); }

void boat_session_start_course(BoatSession *session)
{
    boat_reset(&session->boat);
    session->boat.position = BOAT_COURSE_START;
    boat_presentation_reset(&session->presentation);
    session->course = (BoatCourse) { .enabled = true };
}

void boat_session_restart(BoatSession *session, BoatWater water)
{
    boat_session_flush(session);
    BoatReplayRuns runs = session->runs;
    bool presentation_enabled = session->presentation_enabled;
    bool record_runs = session->record_runs, course = session->course.enabled;
    boat_world_restart(&session->world, water);
    reset(session);
    session->presentation_enabled = presentation_enabled;
    session->runs = runs;
    session->record_runs = record_runs;
    if (course) {
        boat_session_start_course(session);
        boat_replay_runs_reset(&session->runs);
    }
}

void boat_session_flush(BoatSession *session) { boat_replay_runs_flush(&session->runs); }

void boat_session_set_tick(BoatSession *session, uint64_t tick) { session->tick = tick; }

double boat_session_time(const BoatSession *session) { return boat_tick_seconds(session->tick); }

BoatViewState boat_session_view(const BoatSession *session)
{
    return (BoatViewState) {
        .boat = session->boat, .time = boat_session_time(session), .course = session->course
    };
}

void boat_session_respawn(BoatSession *session)
{
    boat_reset_location(&session->boat, session->world.definition);
    boat_presentation_reset(&session->presentation);
}

bool boat_session_step(BoatSession *session, BoatInput input)
{
    if (session->tick == UINT64_MAX)
        return false;
    bool record = session->record_runs && session->course.enabled;
    BoatReplaySample before = record
        ? boat_replay_pose(&session->boat, &session->presentation, boat_session_time(session))
        : (BoatReplaySample) { 0 };
    Vector2 previous = session->boat.position;
    ++session->tick;
    double now = boat_session_time(session);
    boat_step(&session->boat, &session->world, now, input);
    boat_course_step(&session->course, previous, session->boat.position, BOAT_TICK_SECONDS);
    if (session->presentation_enabled)
        boat_presentation_step(&session->presentation, &session->boat, &session->world, now);
    if (record)
        boat_replay_runs_step(&session->runs, before,
            boat_replay_pose(&session->boat, &session->presentation, now), &session->course,
            session->world.water.amplitude != 0);
    return true;
}
