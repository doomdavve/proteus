#include "boat_session.h"

#include <assert.h>

static void reset(BoatSession *session)
{
    boat_reset_location(&session->boat, session->world.definition);
    boat_presentation_reset(&session->presentation);
    session->presentation_enabled = true;
    session->tick = 0;
    session->voyage_events = (BoatVoyageEvents) { 0 };
    session->course = (BoatCourse) { 0 };
    boat_voyage_start(&session->voyage, session->voyage_route, 0);
    if (session->voyage.route)
        session->world.water.storm = boat_voyage_weather(&session->voyage);
    session->runs = (BoatReplayRuns) { 0 };
    session->record_runs = false;
}

void boat_session_init(BoatSession *session, const BoatWorldResources *resources, BoatWater water)
{
    boat_world_init(&session->world, resources, water);
    session->voyage_route = session->world.definition->voyage;
    session->course_definition = NULL;
    reset(session);
    session->handling = &boat_handling_speedboat;
}

void boat_session_destroy(BoatSession *session) { boat_replay_runs_destroy(&session->runs); }

void boat_session_set_course_definition(
    BoatSession *session, const BoatCourseDefinition *definition)
{
    session->course_definition = definition;
}

void boat_session_start_course(BoatSession *session)
{
    assert(session->course_definition);
    const BoatCourseDefinition *definition = session->course_definition;
    boat_reset(&session->boat);
    session->boat.position = definition->start;
    session->boat.heading = definition->heading;
    boat_presentation_reset(&session->presentation);
    session->course = (BoatCourse) { .enabled = true, .definition = definition };
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
    return (BoatViewState) { .boat = session->boat,
        .time = boat_session_time(session),
        .course = session->course,
        .scene = session->world.scene };
}

void boat_session_respawn(BoatSession *session)
{
    boat_reset_location(&session->boat, session->world.definition);
    boat_presentation_reset(&session->presentation);
}

void boat_session_recover(BoatSession *session)
{
    if (!session->voyage.route) {
        boat_session_respawn(session);
        return;
    }
    boat_voyage_recover(&session->voyage, &session->boat, boat_session_time(session));
    session->voyage_events
        = (BoatVoyageEvents) { .items = { { BOAT_VOYAGE_RECOVERED, session->voyage.shelter } },
              .count = 1 };
    boat_presentation_reset(&session->presentation);
}

void boat_session_rebind_voyage(BoatSession *session, const BoatVoyageRoute *route)
{
    session->voyage_route = route;
    if (session->voyage.route)
        session->voyage.route = route;
}

void boat_session_rebind_voyage_live(BoatSession *session, const BoatVoyageRoute *route,
    const char *old_id, const char *const *new_ids, unsigned new_count)
{
    session->voyage_route = route;
    boat_voyage_rebind(&session->voyage, route, old_id, new_ids, new_count);
    if (route)
        session->world.water.storm = boat_voyage_weather(&session->voyage);
}

void boat_session_restart_voyage(BoatSession *session, const BoatVoyageRoute *route)
{
    session->voyage_route = route;
    boat_voyage_restart(&session->voyage, route, boat_session_time(session));
    if (route)
        session->world.water.storm = boat_voyage_weather(&session->voyage);
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
    boat_step_handling(&session->boat, &session->world, now, input,
        session->course.enabled ? &boat_handling_speedboat : session->handling);
    boat_course_step(&session->course, previous, session->boat.position, BOAT_TICK_SECONDS);
    boat_voyage_step_events(&session->voyage, session->boat.position, session->boat.velocity, now,
        &session->voyage_events);
    if (session->voyage.route)
        session->world.water.storm = boat_voyage_weather(&session->voyage);
    if (session->presentation_enabled)
        boat_presentation_step(&session->presentation, &session->boat, &session->world, now);
    if (record)
        boat_replay_runs_step(&session->runs, before,
            boat_replay_pose(&session->boat, &session->presentation, now), &session->course,
            session->world.water.amplitude != 0);
    return true;
}
