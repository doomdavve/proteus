#include "boat_test_world.h"
#include "raymath.h"
#include "wake_velocity_drive.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
/* Frozen pre-extraction driver from 2994e0d, P2a regression oracle. W3 ported only its calls
 * to the explicit session/world API; order, expressions and the crossing recovery are unchanged.
 * M2 moved the production crossing boat into a fleet, so the oracle keeps its own copy of the
 * old study state; validate_boat_drive compares the single-boat path only. */
typedef struct {
    BoatWakeObjects wakes;
    BoatWorld other_world;
    Boat other;
    bool crossing;
    unsigned waypoint;
    float recovery_time;
} BaselineStudy;

static void baseline_reset(
    BaselineStudy *study, BoatSession *session, bool crossing, unsigned life, float height)
{
    *study = (BaselineStudy) { .crossing = crossing };
    boat_wake_objects_init(&study->wakes, life, height);
    session->world.water.objects = &study->wakes;
    boat_reset(&study->other);
    study->other.position = (Vector2) { -65, -12 };
    study->other.heading = PI / 2;
    boat_world_init(&study->other_world, test_world(BOAT_LOCATION_BASIN),
        (BoatWater) { .objects = &study->wakes });
}

static void baseline_track(
    BaselineStudy *study, const Boat *boat, const BoatWorld *world, double time, unsigned id)
{
    BoatWakeStern s = boat_wake_stern(boat, world, time);
    if (!boat_wake_objects_track(&study->wakes, id, (Vector2) { (float)s.x, (float)s.z },
            (float)hypot(s.vx, s.vz), (float)s.wet)) {
        fprintf(stderr, "Wake object emission contract failed.\n");
        exit(1);
    }
}

static void baseline_step(BoatSession *session, float steering, float throttle,
    BoatFrameMetrics *metrics, BoatWakePlayer *wake, BaselineStudy *objects)
{
    Boat *boat = &session->boat;
    double started = boat_profile_time(IsWindowReady());
    if (objects)
        boat_wake_objects_advance(&objects->wakes);
    BoatWakeStern before = wake ? boat_wake_stern(boat, &session->world, boat_session_time(session))
                                : (BoatWakeStern) { 0 };
    boat_session_step(session, (BoatInput) { steering, throttle, 0, 0 });
    double now = boat_session_time(session);
    if (objects) {
        baseline_track(objects, boat, &session->world, now, 0);
        if (objects->crossing) {
            const Vector2 route[] = { { 60, -12 }, { 0, -55 }, { -60, -12 }, { 0, 45 } };
            Boat *other = &objects->other;
            Vector2 delta = Vector2Subtract(route[objects->waypoint], other->position);
            if (Vector2Length(delta) < 16)
                objects->waypoint = (objects->waypoint + 1) % 4;
            float target = atan2f(delta.x, -delta.y);
            float error = remainderf(target - other->heading, 2 * PI);
            float wheel = Clamp(2 * error - other->yaw_velocity, -1, 1);
            // Recovery once narrowed the crossing boat's cached time to float for this tick.
            double other_time = now;
            boat_step(other, &objects->other_world, now,
                (BoatInput) { Clamp((wheel - other->wheel) / BOAT_STEP, -1, 1),
                    Clamp(
                        (.75f - other->throttle) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP), -1, 1),
                    0, 0 });
            boat_collide_pair(boat, 1, other, 1.6f);
            if (other->motion_state == BOAT_CAPSIZED) {
                objects->recovery_time += BOAT_STEP;
                Vector2 spawn = { -65, -12 };
                if (objects->recovery_time >= 2 && Vector2Distance(boat->position, spawn) > 10) {
                    other_time = (float)other_time;
                    boat_reset(other);
                    other->position = spawn;
                    other->heading = PI / 2;
                    objects->wakes.emitters[1]
                        = (BoatWakeObjectEmitter) { .next = objects->wakes.emitters[1].next };
                    objects->waypoint = 0;
                    objects->recovery_time = 0;
                }
            }
            baseline_track(objects, other, &objects->other_world, other_time, 1);
        }
    }
    if (wake && !boat_wake_player_step(wake, before, boat_wake_stern(boat, &session->world, now))) {
        fprintf(stderr, "Wake field update failed.\n");
        exit(1);
    }
    float elapsed = (float)((boat_profile_time(IsWindowReady()) - started) * 1000.0);
    if (metrics) {
        metrics->physics_ms += elapsed;
        metrics->physics_max_ms = fmaxf(metrics->physics_max_ms, elapsed);
        ++metrics->ticks;
    }
}

static void baseline_demo(BoatSession *session, int tick, BoatFrameMetrics *metrics,
    BoatWakePlayer *wake, BaselineStudy *objects)
{
    const Boat *boat = &session->boat;
    if (wake || objects || session->world.water.player_wake_enabled) {
        float throttle = tick < 1440 ? 1 : 0;
        float wheel = tick >= 420 && tick < 1440 ? 1 : 0;
        baseline_step(session, Clamp((wheel - boat->wheel) / BOAT_STEP, -1, 1),
            Clamp((throttle - boat->throttle) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP), -1, 1),
            metrics, wake, objects);
        return;
    }
    if (session->world.water.wake_amplitude != 0
        || Vector2LengthSqr(session->world.water.current) > 0) {
        // Let the source become visible, approach its spreading wake, then recover in neutral.
        float target = tick < 720 ? 0 : tick < 1200 ? .65f : 0;
        baseline_step(session, 0,
            Clamp((target - boat->throttle) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP), -1, 1),
            metrics, wake, objects);
        return;
    }
    // Ease through neutral into reverse with the same held steering.
    float target_wheel = tick >= 360 ? 0.65f : 0;
    float wheel_change = fmaxf(-1, fminf(1, (target_wheel - boat->wheel) / BOAT_STEP));
    float throttle_change = tick < 240 ? 1 : tick < 480 ? 0 : tick < 960 ? -1 : 0;
    baseline_step(session, wheel_change, throttle_change, metrics, wake, objects);
}
