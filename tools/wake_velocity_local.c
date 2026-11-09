#include "wake_velocity_local.h"
#include "boat_time_control.h"
#include "raylib.h"

#include <math.h>

static BoatLocalConfig config_of(
    const BoatSession *session, const ObjectStudy *objects, const BoatWorldResources *worlds)
{
    BoatWater water = session->world.water;
    water.objects = NULL; // Each endpoint's fleet owns its pool.
    return (BoatLocalConfig) { .resources = worlds,
        .location = session->world.definition->id,
        .scene = session->world.scene,
        .world = { .water = water,
            .handling = session->handling,
            .wake_life_ticks = objects->wakes.life_ticks,
            .crest_height = objects->wakes.crest_height,
            .spawn = session->boat } };
}

bool wake_local_open(WakeLocal *local, const BoatSession *session, const ObjectStudy *objects,
    const BoatWorldResources *worlds, double now)
{
    BoatLocalConfig config = config_of(session, objects, worlds);
    local->active = boat_local_start(&local->local, &config, 1, now);
    return local->active;
}

bool wake_local_restart(
    WakeLocal *local, const BoatSession *session, const ObjectStudy *objects, double now)
{
    BoatLocalConfig config = config_of(session, objects, local->local.config.resources);
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

bool wake_local_frame(WakeLocal *local, BoatSession *session, ObjectStudy *objects,
    double *accumulator, BoatInput input, float thrust_target, float steering_target, double now,
    BoatFrameMetrics *metrics)
{
    unsigned grant = boat_time_grant(accumulator, BOAT_LOCAL_MAX_GRANT);
    double started = boat_profile_time(IsWindowReady());
    Present context = { local, session };
    BoatLocalObserver observer = { &context, present };
    bool ok = boat_local_frame(
        &local->local, now, grant, input, thrust_target, steering_target, &observer);
    if (ok && grant)
        objects->wakes = boat_local_view(&local->local)->state.wakes;
    float elapsed = (float)((boat_profile_time(IsWindowReady()) - started) * 1000.0);
    if (metrics && grant) {
        metrics->physics_ms += elapsed;
        metrics->physics_max_ms = fmaxf(metrics->physics_max_ms, elapsed);
        metrics->ticks += grant;
    }
    local->active = ok;
    return ok;
}

void wake_local_close(WakeLocal *local)
{
    boat_local_stop(&local->local);
    local->active = false;
}
