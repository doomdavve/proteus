#include "wake_velocity_drive.h"
#include "raymath.h"
#include "waterfront_traffic.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
void object_study_reset(
    ObjectStudy *study, BoatSession *session, bool crossing, unsigned life, float height)
{
    *study = (ObjectStudy) { .crossing = crossing };
    boat_wake_objects_init(&study->wakes, life, height);
    session->world.water.objects = &study->wakes;
    if (!crossing)
        return;
    if (!boat_fleet_init(
            &study->fleet, session->world.resources, session->world.water, life, height)) {
        fprintf(stderr, "The crossing boat requires a basin fleet.\n");
        exit(1);
    }
    boat_fleet_place(&study->fleet, 0, session->boat);
    Boat other;
    boat_reset(&other);
    other.position = (Vector2) { -65, -12 };
    other.heading = PI / 2;
    boat_fleet_place(&study->fleet, 1, other);
    study->other_life = study->fleet.state.slots[1].life;
    boat_presentation_reset(&study->other_presentation);
    session->world.water.objects = &study->fleet.state.wakes;
}

const BoatWakeObjects *object_study_wakes(const ObjectStudy *study)
{
    return study->crossing ? &study->fleet.state.wakes : &study->wakes;
}

const Boat *object_study_other(const ObjectStudy *study)
{
    return study->crossing ? &study->fleet.state.slots[1].boat : NULL;
}

static void object_track(
    ObjectStudy *study, const Boat *boat, const BoatWorld *world, double time, unsigned id)
{
    BoatWakeStern s = boat_wake_stern(boat, world, time);
    if (!boat_wake_objects_track(&study->wakes, id, (Vector2) { (float)s.x, (float)s.z },
            (float)hypot(s.vx, s.vz), (float)s.wet)) {
        fprintf(stderr, "Wake object emission contract failed.\n");
        exit(1);
    }
}

// The rival laps four waypoints. Two seconds after a capsize it asks the fleet for a reset,
// which waits for a safe, calm candidate away from every hull.
static BoatInput rival_input(ObjectStudy *study)
{
    static const Vector2 route[] = { { 60, -12 }, { 0, -55 }, { -60, -12 }, { 0, 45 } };
    BoatFleetSlot *slot = &study->fleet.state.slots[1];
    const Boat *other = &slot->boat;
    if (slot->life != study->other_life) {
        study->other_life = slot->life;
        study->waypoint = 0;
        study->recovery_time = 0;
        boat_presentation_reset(&study->other_presentation);
    }
    if (other->motion_state == BOAT_CAPSIZED) {
        study->recovery_time += BOAT_STEP;
        if (study->recovery_time >= 2)
            boat_fleet_request_spawn(&study->fleet, 1);
    }
    Vector2 delta = Vector2Subtract(route[study->waypoint], other->position);
    if (Vector2Length(delta) < 16)
        study->waypoint = (study->waypoint + 1) % 4;
    float target = atan2f(delta.x, -delta.y);
    float error = remainderf(target - other->heading, 2 * PI);
    float wheel = Clamp(2 * error - other->yaw_velocity, -1, 1);
    return (BoatInput) { Clamp((wheel - other->wheel) / BOAT_STEP, -1, 1),
        Clamp((.75f - other->throttle) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP), -1, 1), 0, 0 };
}

// The session keeps the player for R, views and audio; the fleet steps it with the rival.
static void crossing_step(BoatSession *session, BoatInput input, ObjectStudy *study)
{
    BoatFleet *fleet = &study->fleet;
    fleet->state.slots[0].boat = session->boat;
    BoatInput inputs[BOAT_FLEET_SLOTS] = { input, rival_input(study) };
    if (!boat_fleet_step(fleet, inputs) || fleet->stats.emission_failures) {
        fprintf(stderr, "Wake object emission contract failed.\n");
        exit(1);
    }
    session->boat = fleet->state.slots[0].boat;
    boat_session_set_tick(session, fleet->state.tick);
    if (!session->presentation_enabled)
        return;
    double now = boat_fleet_time(fleet);
    BoatWorld world = boat_fleet_world(fleet);
    boat_presentation_step(&session->presentation, &session->boat, &world, now);
    boat_presentation_step(&study->other_presentation, &fleet->state.slots[1].boat, &world, now);
}

void timed_boat_step(BoatSession *session, BoatInput input, BoatFrameMetrics *metrics,
    BoatWakePlayer *wake, ObjectStudy *objects)
{
    Boat *boat = &session->boat;
    double started = boat_profile_time(IsWindowReady());
    if (objects && objects->crossing) {
        crossing_step(session, input, objects);
    } else {
        if (objects)
            boat_wake_objects_advance(&objects->wakes);
        if (objects && objects->traffic_enabled
            && !waterfront_traffic_wakes_step(
                &objects->traffic, boat_session_time(session) + BOAT_TICK_SECONDS)) {
            fprintf(stderr, "Traffic wake emission contract failed.\n");
            exit(1);
        }
        BoatWakeStern before = wake
            ? boat_wake_stern(boat, &session->world, boat_session_time(session))
            : (BoatWakeStern) { 0 };
        boat_session_step(session, input);
        double now = boat_session_time(session);
        if (objects)
            object_track(objects, boat, &session->world, now, 0);
        if (wake
            && !boat_wake_player_step(wake, before, boat_wake_stern(boat, &session->world, now))) {
            fprintf(stderr, "Wake field update failed.\n");
            exit(1);
        }
    }
    float elapsed = (float)((boat_profile_time(IsWindowReady()) - started) * 1000.0);
    if (metrics) {
        metrics->physics_ms += elapsed;
        metrics->physics_max_ms = fmaxf(metrics->physics_max_ms, elapsed);
        ++metrics->ticks;
    }
}

void demo_step(BoatSession *session, int tick, BoatFrameMetrics *metrics, BoatWakePlayer *wake,
    ObjectStudy *objects)
{
    const Boat *boat = &session->boat;
    const BoatWater *water = &session->world.water;
    if (wake || objects || water->player_wake_enabled) {
        float throttle = tick < 1440 ? 1 : 0;
        float wheel = tick >= 420 && tick < 1440 ? 1 : 0;
        timed_boat_step(session,
            (BoatInput) { Clamp((wheel - boat->wheel) / BOAT_STEP, -1, 1),
                Clamp(
                    (throttle - boat->throttle) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP), -1, 1),
                0, 0 },
            metrics, wake, objects);
        return;
    }
    if (water->wake_amplitude != 0 || Vector2LengthSqr(water->current) > 0) {
        // Let the source become visible, approach its spreading wake, then recover in neutral.
        float target = tick < 720 ? 0 : tick < 1200 ? .65f : 0;
        timed_boat_step(session,
            (BoatInput) { 0,
                Clamp((target - boat->throttle) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP), -1, 1),
                0, 0 },
            metrics, wake, objects);
        return;
    }
    // Ease through neutral into reverse with the same held steering.
    float target_wheel = tick >= 360 ? 0.65f : 0;
    float wheel_change = fmaxf(-1, fminf(1, (target_wheel - boat->wheel) / BOAT_STEP));
    float throttle_change = tick < 240 ? 1 : tick < 480 ? 0 : tick < 960 ? -1 : 0;
    timed_boat_step(
        session, (BoatInput) { wheel_change, throttle_change, 0, 0 }, metrics, wake, objects);
}
