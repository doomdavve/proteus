#include "wake_velocity_local.h"
#include "boat_time_control.h"
#include "raylib.h"

#include <math.h>

// The configuration of a new generation; a time trial's course adapter starts over with it.
static BoatLocalConfig config_of(WakeLocal *local, const BoatSession *session,
    const ObjectStudy *objects, const BoatWorldResources *worlds)
{
    BoatWater water = session->world.water;
    water.objects = NULL; // Each endpoint's fleet owns its pool.
    local->course_active = session->course.enabled;
    BoatLocalConfig config = { .resources = worlds,
        .location = session->world.definition->id,
        .scene = session->world.scene,
        .world = { .water = water,
            .handling = session->handling,
            .wake_life_ticks = objects->wakes.life_ticks,
            .crest_height = objects->wakes.crest_height,
            .spawn = session->boat } };
    if (local->course_active) {
        // A course always runs the speedboat, as boat_session_step does. The local session has
        // one player in slot zero.
        config.world.handling = &boat_handling_speedboat;
        wake_course_init(&local->course, session, 0);
        config.rules = wake_course_rules(&local->course);
    } else {
        wake_course_destroy(&local->course);
    }
    return config;
}

// The client's view of the authority's course after a frame, and the finish the frame settled.
static void course_sync(WakeLocal *local, BoatSession *session)
{
    if (!local->course_active)
        return;
    size_t size;
    uint64_t tick;
    const uint8_t *state = boat_local_state(&local->local, &size, &tick);
    if (state) // Sent only when it changes, so the newest describes the authority's tick.
        wake_course_view(state, size, session);
    wake_course_take(&local->course, &session->runs);
}

bool wake_local_open(WakeLocal *local, const BoatSession *session, const ObjectStudy *objects,
    const BoatWorldResources *worlds, double now)
{
    BoatLocalConfig config = config_of(local, session, objects, worlds);
    local->active = boat_local_start(&local->local, &config, 1, now);
    return local->active;
}

bool wake_local_restart(
    WakeLocal *local, const BoatSession *session, const ObjectStudy *objects, double now)
{
    BoatLocalConfig config = config_of(local, session, objects, local->local.config.resources);
    local->active = boat_local_restart(&local->local, &config, now);
    return local->active;
}

typedef struct {
    WakeLocal *local;
    BoatSession *session;
} Present;

// The client's predicted boat at TICK becomes the session's, and its presentation follows as
// boat_session_step steps it: against WATER, the pool before the tick's emission.
static void present(void *context, const BoatFleet *view, unsigned slot, uint64_t tick,
    const BoatWakeObjects *water)
{
    Present *p = context;
    BoatSession *session = p->session;
    session->boat = view->state.slots[slot].boat;
    boat_session_set_tick(session, tick);
    if (session->presentation_enabled) {
        BoatWorld world = session->world;
        world.water.objects = water;
        boat_presentation_step(
            &session->presentation, &session->boat, &world, boat_tick_seconds(tick));
    }
    if (p->local->traced)
        p->local->traced(p->local->traced_context, session, view);
}

// The scripted drive's helm: demo_input at the tick before TICK, from the predicted boat.
static BoatInput scripted(void *context, const Boat *predicted, uint64_t tick)
{
    const BoatSession *session = context;
    return demo_input(predicted, &session->world.water, true, (int)(tick - 1));
}

// One grant of the player's helm, or of the scripted drive's when SCRIPT.
static bool run(WakeLocal *local, BoatSession *session, ObjectStudy *objects, unsigned grant,
    BoatInput input, float thrust_target, float steering_target, bool script, double now,
    BoatFrameMetrics *metrics)
{
    double started = boat_profile_time(IsWindowReady());
    if (local->course_active && session->runs.practice)
        wake_course_practice(&local->course);
    Present context = { local, session };
    BoatLocalObserver observer = { &context, present };
    BoatLocalScript drive = { session, scripted };
    const BoatLocalScript *source = local->script ? local->script : &drive;
    bool ok = script ? boat_local_frame_scripted(&local->local, now, grant, source, &observer)
                     : boat_local_frame(&local->local, now, grant, input, thrust_target,
                           steering_target, &observer);
    if (ok && grant)
        objects->wakes = boat_local_view(&local->local)->state.wakes;
    if (ok)
        course_sync(local, session);
    float elapsed = (float)((boat_profile_time(IsWindowReady()) - started) * 1000.0);
    if (metrics && grant) {
        metrics->physics_ms += elapsed;
        metrics->physics_max_ms = fmaxf(metrics->physics_max_ms, elapsed);
        metrics->ticks += grant;
    }
    local->active = ok;
    return ok;
}

bool wake_local_frame(WakeLocal *local, BoatSession *session, ObjectStudy *objects,
    double *accumulator, BoatInput input, float thrust_target, float steering_target, double now,
    BoatFrameMetrics *metrics)
{
    unsigned grant = boat_time_grant(accumulator, BOAT_LOCAL_MAX_GRANT);
    return run(
        local, session, objects, grant, input, thrust_target, steering_target, false, now, metrics);
}

bool wake_local_scripted_frame(WakeLocal *local, BoatSession *session, ObjectStudy *objects,
    double *accumulator, double now, BoatFrameMetrics *metrics)
{
    unsigned grant = boat_time_grant(accumulator, BOAT_LOCAL_MAX_GRANT);
    return run(local, session, objects, grant, (BoatInput) { 0 }, NAN, NAN, true, now, metrics);
}

bool wake_local_scripted_drive(
    WakeLocal *local, BoatSession *session, ObjectStudy *objects, uint64_t ticks)
{
    while (local->local.tick < ticks) {
        uint64_t left = ticks - local->local.tick;
        unsigned grant = left < BOAT_LOCAL_MAX_GRANT ? (unsigned)left : BOAT_LOCAL_MAX_GRANT;
        double now = boat_tick_seconds(local->local.tick);
        if (!run(local, session, objects, grant, (BoatInput) { 0 }, NAN, NAN, true, now, NULL))
            return false;
    }
    return true;
}

void wake_local_close(WakeLocal *local)
{
    boat_local_stop(&local->local);
    wake_course_destroy(&local->course);
    local->active = local->course_active = false;
}
