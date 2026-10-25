#include "boat_map.h"
#include "boat_steering.h"
#include "boat_test_session.h"
#include "boat_throttle.h"
#include "boat_view.h"
#include "raymath.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "Boat check failed at line %d: %s\n", __LINE__, #condition);           \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int validate_flight_outcomes(void)
{
    // Both axes can invert in flight without magical righting or immediate failure.
    for (int axis = 0; axis < 2; ++axis) {
        BoatSession boat;
        boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
        boat.boat.height = 20;
        if (axis == 0)
            boat.boat.pitch = PI;
        else
            boat.boat.roll = 80 * DEG2RAD;
        float angle = axis == 0 ? boat.boat.pitch : boat.boat.roll;
        for (int tick = 0; tick < 60; ++tick)
            boat_session_step(&boat, (BoatInput) { 0 });
        CHECK(boat.boat.motion_state == BOAT_AIRBORNE);
        CHECK(fabsf((axis == 0 ? boat.boat.pitch : boat.boat.roll) - angle) < .00001f);
        CHECK(boat.boat.inverted_contact_time == 0);
    }
    BoatSession flying;
    boat_session_init(&flying, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    flying.boat.height = 20;
    flying.boat.velocity = (Vector2) { 3, -7 };
    flying.boat.throttle = flying.boat.engine_response = flying.boat.wheel = 1;
    for (int tick = 0; tick < 60; ++tick)
        boat_session_step(&flying, (BoatInput) { 1, 0 });
    CHECK(flying.boat.velocity.x == 3 && flying.boat.velocity.y == -7);
    CHECK(flying.boat.heading > 0 && flying.boat.heading < .05f);
    CHECK(flying.boat.roll == 0 && flying.boat.pitch == 0);
    CHECK(fabsf(flying.boat.height - (20 - .5f * 9.81f * .5f * (.5f + BOAT_STEP))) < .001f);
    // Inversion is an orientation, not an Euler-angle range. Test odd/even full turns.
    for (int axis = 0; axis < 2; ++axis) {
        for (int turns = -3; turns <= 3; ++turns) {
            BoatSession boat;
            boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
            float angle = turns * PI;
            if (axis == 0)
                boat.boat.pitch = angle;
            else
                boat.boat.roll = angle;
            for (int tick = 0; tick < 120; ++tick)
                boat_session_step(&boat, (BoatInput) { 0, 1 });
            CHECK((boat.boat.motion_state == BOAT_CAPSIZED) == (abs(turns) % 2 == 1));
            if (boat.boat.motion_state == BOAT_CAPSIZED) {
                CHECK(boat.boat.throttle == 0 && boat.boat.engine_response == 0);
                boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
                CHECK(boat.boat.motion_state == BOAT_WATERBORNE);
                CHECK(boat.boat.inverted_contact_time == 0 && boat.boat.landings == 0);
            }
        }
    }
    BoatSession boat;
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat.boat.height = 2;
    bool landing = false;
    for (int tick = 0; tick < 600; ++tick) {
        boat_session_step(&boat, (BoatInput) { 0 });
        landing |= boat.boat.motion_state == BOAT_LANDING;
    }
    CHECK(landing && boat.boat.landings == 1 && boat.boat.motion_state == BOAT_WATERBORNE);
    CHECK(fabsf(boat.boat.height) < .001f);
    return 0;
}

static int validate_dynamic_pair(void)
{
    BoatSession a, b;
    boat_session_init(&a, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat_session_init(&b, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    a.boat.position = (Vector2) { -.7f, 0 };
    b.boat.position = (Vector2) { .7f, 0 };
    a.boat.velocity.x = 6;
    Vector2 initial_a = a.boat.position, initial_b = b.boat.position;
    b.boat.height = 10;
    CHECK(!boat_collide_pair(&a.boat, 1, &b.boat, 1.6f));
    CHECK(a.boat.velocity.x == 6 && b.boat.velocity.x == 0);
    b.boat.height = 0;
    CHECK(boat_collide_pair(&a.boat, 1, &b.boat, 1.6f));
    CHECK(b.boat.velocity.x > 0 && a.boat.velocity.x < 6);
    CHECK(fabsf(a.boat.velocity.x + 1.6f * b.boat.velocity.x - 6) < .0001f);
    CHECK(.5f * a.boat.velocity.x * a.boat.velocity.x + .8f * b.boat.velocity.x * b.boat.velocity.x
        < 18);
    CHECK(
        Vector2Distance(a.boat.position, initial_a) > Vector2Distance(b.boat.position, initial_b));
    CHECK(!boat_collide_pair(&a.boat, 1, &b.boat, 1.6f));
    // Offset impact transfers angular momentum as well as linear momentum.
    a.boat.yaw_velocity = b.boat.yaw_velocity = 0;
    a.boat.position = (Vector2) { -.7f, 1 };
    b.boat.position = (Vector2) { .7f, 0 };
    a.boat.velocity = (Vector2) { 6, 0 };
    b.boat.velocity = (Vector2) { 0 };
    CHECK(boat_collide_pair(&a.boat, 1, &b.boat, 1.6f));
    CHECK(fabsf(a.boat.yaw_velocity) > .1f && fabsf(b.boat.yaw_velocity) > .1f);
    return 0;
}

static int particles(const BoatPresentation *view, bool spray)
{
    int count = 0;
    for (int i = 0; i < BOAT_WATER_PARTICLES; ++i)
        if (view->water[i].lifetime > 0 && view->water[i].spray == spray)
            ++count;
    return count;
}

// Stern variation must not change the accepted bow spray or any handling state.
static int validate_wake_independence(void)
{
    BoatSession a, b;
    boat_session_init(&a, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat_session_init(&b, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    b.presentation.wake_emission = 931;
    for (int tick = 0; tick < 720; ++tick) {
        a.boat.throttle = b.boat.throttle = tick < 480 ? 1 : -1;
        boat_session_step(&a, (BoatInput) { tick > 360 ? .4f : 0, 0 });
        boat_session_step(&b, (BoatInput) { tick > 360 ? .4f : 0, 0 });
        CHECK(a.boat.position.x == b.boat.position.x && a.boat.position.y == b.boat.position.y);
        CHECK(a.boat.heading == b.boat.heading && a.presentation.emission == b.presentation.emission
            && a.boat.turn_roll_load == b.boat.turn_roll_load);
        CHECK(particles(&a.presentation, true) == particles(&b.presentation, true));
        for (int i = 0; i < BOAT_WATER_PARTICLES; ++i) {
            const BoatWaterParticle *p = &a.presentation.water[i];
            if (!p->spray || p->lifetime <= 0)
                continue;
            bool found = false;
            for (int j = 0; j < BOAT_WATER_PARTICLES; ++j) {
                const BoatWaterParticle *q = &b.presentation.water[j];
                if (q->spray && q->lifetime > 0 && p->age == q->age
                    && p->position.x == q->position.x && p->position.y == q->position.y
                    && p->height == q->height && p->size == q->size && p->lift == q->lift) {
                    found = true;
                    break;
                }
            }
            CHECK(found);
        }
    }
    // Even a saturated pool must not let a foam birth overwrite live airborne spray.
    boat_session_init(&a, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    a.boat.throttle = a.boat.engine_response = 1;
    a.presentation.propeller_time = .075f;
    for (int i = 0; i < BOAT_WATER_PARTICLES; ++i)
        a.presentation.water[i]
            = (BoatWaterParticle) { .height = 100, .lifetime = 100, .spray = true };
    boat_session_step(&a, (BoatInput) { 0 });
    CHECK(particles(&a.presentation, true) == BOAT_WATER_PARTICLES);
    CHECK(particles(&a.presentation, false) == 0);
    return 0;
}

static float kinetic_energy(const Boat *boat)
{
    // Normalized mass 1 and yaw inertia 2.2; total energy includes rotation.
    return 0.5f * (boat->velocity.x * boat->velocity.x + boat->velocity.y * boat->velocity.y)
        + 1.1f * boat->yaw_velocity * boat->yaw_velocity;
}

static int validate_countersteering(void)
{
    for (int direction = -1; direction <= 1; direction += 2) {
        for (int coast = 0; coast <= 1; ++coast) {
            BoatSession boat = basin_session((Boat) { .velocity = { 0, -12 },
                .throttle = coast ? 0 : 1,
                .engine_response = coast ? 0 : 1,
                .planing = 1 });
            // Establish a powered or unpowered turn, then ask to catch it.
            for (int tick = 0; tick < 120; ++tick)
                boat_session_step(&boat, (BoatInput) { direction, 0 });
            CHECK(fabsf(boat.boat.wheel - direction) < .00001f
                && boat.boat.yaw_velocity * direction > 0);
            float heading = boat.boat.heading;
            float overshoot = 0;
            int reversed = 0;
            for (int tick = 1; tick <= 120; ++tick) {
                float energy = kinetic_energy(&boat.boat);
                boat_session_step(&boat, (BoatInput) { -direction, 0 });
                if (coast)
                    CHECK(kinetic_energy(&boat.boat) <= energy + .00001f);
                if (tick == 42)
                    CHECK(
                        boat.boat.wheel * direction < 0 && boat.boat.engine_angle * direction <= 0);
                if (!reversed && boat.boat.yaw_velocity * direction <= 0) {
                    reversed = tick;
                    overshoot = direction * remainderf(boat.boat.heading - heading, 2 * PI);
                }
            }
            // Baseline needed ~1.0s powered / 1.13s coasting and overshot by 40 / 16 degrees.
            CHECK(reversed > 0 && reversed <= 84);
            CHECK(overshoot > 0 && overshoot < (coast ? 10 : 24) * DEG2RAD);
        }
    }
    // Tiny corrections that cross centre use the ordinary rate for the rest of that tick.
    BoatSession small = basin_session((Boat) { .wheel = .001f });
    boat_session_step(&small, (BoatInput) { -1, 0 });
    CHECK(small.boat.wheel < 0 && small.boat.wheel >= -BOAT_STEP);
    BoatSession partial = basin_session((Boat) { .wheel = 1 });
    BoatSession clamped = partial, full = partial;
    for (int tick = 0; tick < 40; ++tick) {
        boat_session_step(&partial, (BoatInput) { -.5f, 0 });
        boat_session_step(&clamped, (BoatInput) { -8, 0 });
        boat_session_step(&full, (BoatInput) { -1, 0 });
    }
    CHECK(fabsf(partial.boat.wheel - .5f) < .0001f);
    CHECK(fabsf(full.boat.wheel) < .0001f);
    CHECK(memcmp(&clamped.boat, &full.boat, sizeof(Boat)) == 0);
    return 0;
}

static int validate_speed_view(void)
{
    BoatSession boat;
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    WaterfrontSceneFrame frame;
    float previous = 55 * DEG2RAD;
    for (int step = 0; step <= 240; ++step) {
        boat.boat.velocity.y = -(float)step * .1f;
        boat_scene_frame(
            SESSION_VIEW(&boat), &boat.presentation, &boat.world, BOAT_CAMERA_DEFAULT, &frame);
        float fov = frame.camera.vertical_fov;
        CHECK(fov >= previous && fov <= 62 * DEG2RAD + .000001f);
        CHECK(fov - previous < .08f * DEG2RAD);
        if (step <= 40)
            CHECK(fov == 55 * DEG2RAD);
        if (step >= 180)
            CHECK(fabsf(fov - 62 * DEG2RAD) < .000001f);
        CHECK(frame.camera.up.x == 0 && frame.camera.up.y == 1 && frame.camera.up.z == 0);
        CHECK(frame.camera.position.y == BOAT_CAMERA_DEFAULT.height);
        previous = fov;
    }
    BoatCamera fixed = BOAT_CAMERA_DEFAULT;
    fixed.fixed_fov = true;
    boat_scene_frame(SESSION_VIEW(&boat), &boat.presentation, &boat.world, fixed, &frame);
    CHECK(frame.camera.vertical_fov == 55 * DEG2RAD);
    // Reverse, sideways drift and rest do not open the lens; it uses ground travel, not flow.
    boat.boat.velocity = (Vector2) { 0, 20 };
    boat_scene_frame(
        SESSION_VIEW(&boat), &boat.presentation, &boat.world, BOAT_CAMERA_DEFAULT, &frame);
    CHECK(frame.camera.vertical_fov == 55 * DEG2RAD);
    boat.boat.velocity = (Vector2) { 20, 0 };
    boat_scene_frame(
        SESSION_VIEW(&boat), &boat.presentation, &boat.world, BOAT_CAMERA_DEFAULT, &frame);
    CHECK(frame.camera.vertical_fov == 55 * DEG2RAD);
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat.world.water.current = (Vector2) { 0, -20 };
    boat_scene_frame(
        SESSION_VIEW(&boat), &boat.presentation, &boat.world, BOAT_CAMERA_DEFAULT, &frame);
    CHECK(frame.camera.vertical_fov == 55 * DEG2RAD);
    return 0;
}

static int validate_coasting(void)
{
    BoatSession fast
        = basin_session((Boat) { .velocity = { 0, -10 }, .wheel = 0.6f, .engine_angle = 0.33f });
    BoatSession slow
        = basin_session((Boat) { .velocity = { 0, -2 }, .wheel = 0.6f, .engine_angle = 0.33f });
    BoatSession left
        = basin_session((Boat) { .velocity = { 0, -10 }, .wheel = -0.6f, .engine_angle = -0.33f });
    BoatSession straight = basin_session((Boat) { .velocity = { 0, -10 } });
    float energy = kinetic_energy(&fast.boat);
    boat_session_step(&fast, (BoatInput) { 0 });
    boat_session_step(&slow, (BoatInput) { 0 });
    boat_session_step(&left, (BoatInput) { 0 });
    boat_session_step(&straight, (BoatInput) { 0 });
    CHECK(fast.boat.throttle == 0 && fast.boat.yaw_velocity > 0);
    CHECK(fast.boat.yaw_velocity > slow.boat.yaw_velocity * 10);
    CHECK(fabsf(left.boat.yaw_velocity + fast.boat.yaw_velocity) < 0.0001f);
    CHECK(straight.boat.yaw_velocity == 0 && straight.boat.velocity.x == 0);
    CHECK(kinetic_energy(&fast.boat) < energy);
    // A sustained unpowered turn changes heading while always losing total kinetic energy.
    for (int i = 0; i < 120; ++i) {
        energy = kinetic_energy(&fast.boat);
        boat_session_step(&fast, (BoatInput) { 0 });
        CHECK(kinetic_energy(&fast.boat) <= energy + 0.00001f);
    }
    CHECK(fast.boat.heading > 0.1f);
    CHECK(fabsf(fast.boat.position.x) < BOAT_LIMIT_X && fabsf(fast.boat.position.y) < BOAT_LIMIT_Z);

    // Broad drift, backwards motion, and spin must not turn passive resistance into propulsion.
    for (int direction = -1; direction <= 1; direction += 2) {
        for (int wheel = -1; wheel <= 1; ++wheel) {
            BoatSession drift = basin_session((Boat) { .velocity = { 7, direction * 8 },
                .yaw_velocity = direction * 2,
                .wheel = wheel,
                .engine_angle = wheel * 0.55f });
            for (int i = 0; i < 600; ++i) {
                energy = kinetic_energy(&drift.boat);
                boat_session_step(&drift, (BoatInput) { 0 });
                CHECK(kinetic_energy(&drift.boat) <= energy + 0.00001f);
            }
        }
    }
    // Straight-line drag grows more than linearly with speed.
    fast = basin_session((Boat) { .velocity = { 0, -10 } });
    slow = basin_session((Boat) { .velocity = { 0, -2 } });
    boat_session_step(&fast, (BoatInput) { 0 });
    boat_session_step(&slow, (BoatInput) { 0 });
    CHECK((10 + fast.boat.velocity.y) / 10 > (2 + slow.boat.velocity.y) / 2);
    return 0;
}

static int validate_signed_propulsion(void)
{
    BoatSession forward, reverse, forward_turn, reverse_turn;
    boat_session_init(&forward, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat_session_init(&reverse, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    forward.boat.throttle = 1;
    reverse.boat.throttle = -1;
    for (int i = 0; i < 240; ++i) {
        boat_session_step(&forward, (BoatInput) { 0 });
        boat_session_step(&reverse, (BoatInput) { 0 });
    }
    CHECK(forward.boat.engine_response > .99f && reverse.boat.engine_response < -.99f);
    CHECK(forward.boat.velocity.y < -1 && reverse.boat.velocity.y > 1);

    boat_session_init(&forward_turn, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat_session_init(&reverse_turn, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    forward_turn.boat.throttle = 1;
    reverse_turn.boat.throttle = -1;
    forward_turn.boat.wheel = reverse_turn.boat.wheel = .6f;
    for (int i = 0; i < 240; ++i) {
        boat_session_step(&forward_turn, (BoatInput) { 1, 0 });
        boat_session_step(&reverse_turn, (BoatInput) { 1, 0 });
    }
    CHECK(forward_turn.boat.yaw_velocity > 0 && reverse_turn.boat.yaw_velocity < 0);

    BoatSession saturated = basin_session((Boat) { .throttle = 4 });
    boat_session_step(&saturated, (BoatInput) { 0 });
    CHECK(saturated.boat.throttle == 1 && saturated.boat.engine_response > 0);
    saturated.boat.throttle = -4;
    boat_session_step(&saturated, (BoatInput) { 0 });
    CHECK(saturated.boat.throttle == -1 && saturated.boat.engine_response <= 0);

    BoatSession deterministic_a = basin_session((Boat) { .throttle = -.8f });
    BoatSession deterministic_b = deterministic_a;
    for (int i = 0; i < 720; ++i) {
        boat_session_step(&deterministic_a, (BoatInput) { 0 });
        boat_session_step(&deterministic_b, (BoatInput) { 0 });
    }
    CHECK(memcmp(&deterministic_a.boat, &deterministic_b.boat, sizeof(Boat)) == 0);

    BoatSession sustained = basin_session((Boat) { .throttle = -1 });
    for (int i = 0; i < 120 * 120; ++i) {
        boat_session_step(&sustained, (BoatInput) { 0 });
        CHECK(isfinite(sustained.boat.engine_response));
        CHECK(isfinite(sustained.boat.velocity.x) && isfinite(sustained.boat.velocity.y));
        CHECK(sustained.boat.engine_response >= -1 && sustained.boat.engine_response <= 1);
        CHECK(hypotf(sustained.boat.velocity.x, sustained.boat.velocity.y) < 21);
    }
    return 0;
}

static int validate_signed_throttle(void)
{
    BoatSession boat;
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    for (int i = 0; i < 120; ++i)
        boat_session_step(&boat, (BoatInput) { 0, 1 });
    CHECK(boat.boat.throttle > .49f);
    for (int i = 0; i < 240; ++i)
        boat_session_step(&boat, (BoatInput) { 0, -1 });
    CHECK(boat.boat.throttle < -.49f && boat.boat.engine_response < 0);
    for (int i = 0; i < 240; ++i)
        boat_session_step(&boat, (BoatInput) { 0, -1 });
    CHECK(boat.boat.throttle == -1);
    for (int i = 0; i < 1200; ++i)
        boat_session_step(&boat, (BoatInput) { 0, 1 });
    CHECK(boat.boat.throttle == 1);
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    CHECK(fabsf(boat.boat.throttle) < .001f);
    return 0;
}

static int validate_planing(void)
{
    BoatSession boat;
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat.boat.velocity.y = -7;
    boat.boat.throttle = 1;
    boat.boat.engine_response = 1;
    float previous_speed = 7;
    float previous_acceleration = 0;
    for (int i = 0; i < 300; ++i) {
        boat_session_step(&boat, (BoatInput) { 0 });
        float speed = hypotf(boat.boat.velocity.x, boat.boat.velocity.y);
        float acceleration = speed - previous_speed;
        // The rate-limited blend cannot produce a gear-shift-like velocity jump.
        if (i > 0)
            CHECK(fabsf(acceleration - previous_acceleration) < .003f);
        CHECK(boat.boat.planing >= 0 && boat.boat.planing <= 1);
        previous_speed = speed;
        previous_acceleration = acceleration;
    }
    CHECK(boat.boat.planing > .9f);

    // With the engine at neutral, planing drains away and cannot create kinetic energy.
    boat.boat.throttle = 0;
    while (boat.boat.engine_response > 0)
        boat_session_step(&boat, (BoatInput) { 0 });
    for (int i = 0; i < 1200; ++i) {
        float energy = kinetic_energy(&boat.boat);
        boat_session_step(&boat, (BoatInput) { 0 });
        CHECK(kinetic_energy(&boat.boat) <= energy + .00001f);
    }
    CHECK(boat.boat.planing < .01f);

    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat.boat.velocity.y = 9;
    boat.boat.throttle = -1;
    for (int i = 0; i < 480; ++i) {
        boat_session_step(&boat, (BoatInput) { 0 });
        CHECK(boat.boat.planing == 0);
    }
    return 0;
}

static int validate_planing_attitude(void)
{
    BoatSession boat;
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat.boat.throttle = 1;
    float peak = 0;
    for (int tick = 0; tick < 360; ++tick) {
        float previous = boat.boat.pitch;
        boat_session_step(&boat, (BoatInput) { 0 });
        CHECK(isfinite(boat.boat.pitch));
        CHECK(fabsf(boat.boat.pitch - previous) < .008f);
        peak = fmaxf(peak, boat.boat.pitch);
    }
    CHECK(peak > 5 * DEG2RAD);
    CHECK(boat.boat.pitch > 2 * DEG2RAD && boat.boat.pitch < 4 * DEG2RAD);
    CHECK(particles(&boat.presentation, true) > 0);

    // Validate rigid attachments and local bow rise across headings, including Euler singularities.
    WaterfrontSceneFrame frame;
    for (int angle = 0; angle < 16; ++angle) {
        boat.boat.heading = angle * PI / 8;
        boat.boat.engine_angle = .4f;
        for (int attitude = 0; attitude < 2; ++attitude) {
            boat.boat.pitch = attitude * .15f;
            boat_scene_frame(
                SESSION_VIEW(&boat), &boat.presentation, &boat.world, BOAT_CAMERA_DEFAULT, &frame);
            Matrix hull = MatrixRotateXYZ(frame.player.hull.rotation);
            Matrix expected
                = MatrixMultiply(MatrixRotateX(boat.boat.pitch), MatrixRotateY(boat.boat.heading));
            Vector3 bow = Vector3Transform((Vector3) { 0, 0, -1 }, hull);
            CHECK(fabsf(bow.y - sinf(boat.boat.pitch)) < .0001f);
            Vector3 cabin = Vector3Add(
                frame.player.hull.position, Vector3Transform((Vector3) { 0, 1.1f, 0 }, expected));
            CHECK(Vector3Distance(cabin, frame.player.cabin.position) < .0001f);
            Vector3 stern = Vector3Add(
                frame.player.hull.position, Vector3Transform((Vector3) { 0, .4f, 1.6f }, expected));
            CHECK(Vector3Distance(stern, frame.player.engine.position) < .0001f);
            Matrix engine = MatrixRotateXYZ(frame.player.engine.rotation);
            Matrix expected_engine
                = MatrixMultiply(MatrixRotateY(-boat.boat.engine_angle), expected);
            for (int axis = 0; axis < 3; ++axis) {
                Vector3 basis = { axis == 0, axis == 1, axis == 2 };
                CHECK(Vector3Distance(
                          Vector3Transform(basis, engine), Vector3Transform(basis, expected_engine))
                    < .0001f);
            }
            CHECK(frame.camera.up.x == 0 && frame.camera.up.y == 1 && frame.camera.up.z == 0);
            CHECK(frame.camera.position.y == BOAT_CAMERA_DEFAULT.height);
        }
    }
    // Presentation state must never feed back into horizontal handling or the camera.
    BoatSession level = boat;
    level.boat.pitch = 0;
    boat_session_step(&boat, (BoatInput) { 0 });
    boat_session_step(&level, (BoatInput) { 0 });
    CHECK(Vector2Distance(boat.boat.velocity, level.boat.velocity) == 0);
    CHECK(boat.boat.heading == level.boat.heading
        && boat.presentation.camera_heading == level.presentation.camera_heading);
    boat.boat.throttle = 0;
    for (int tick = 0; tick < 2400; ++tick) {
        float previous = boat.boat.pitch;
        boat_session_step(&boat, (BoatInput) { 0 });
        CHECK(fabsf(boat.boat.pitch - previous) < .008f);
    }
    CHECK(boat.boat.pitch < .0001f && boat.boat.planing == 0);
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    CHECK(boat.boat.pitch == 0);
    return 0;
}

static bool footprint_in_water(const BoatSession *session)
{
    const Boat *boat = &session->boat;
    const Vector2 samples[]
        = { { 0, 2 }, { -.75f, 2 }, { .75f, 2 }, { 0, -1.6f }, { -.75f, -1.6f }, { .75f, -1.6f } };
    for (size_t i = 0; i < sizeof(samples) / sizeof(*samples); ++i) {
        Vector2 p = { boat->position.x
                + (sinf(boat->heading) * samples[i].y + cosf(boat->heading) * samples[i].x),
            boat->position.y
                + (-cosf(boat->heading) * samples[i].y + sinf(boat->heading) * samples[i].x) };
        if (session->world.definition->mapped
                ? !test_river_water(p, .35f)
                : fabsf(p.x) > BOAT_BASIN_HALF_X - .35f || fabsf(p.y) > BOAT_BASIN_HALF_Z - .35f)
            return false;
    }
    return true;
}

// Contact trajectories diverge with float rounding; exercise nearby headings on every platform.
static int validate_basin_stress(float heading)
{
    BoatSession boat = basin_session(
        (Boat) { .position = { 116, 76 }, .velocity = { 8, 8 }, .heading = heading });
    bool corner_contact = false;
    for (int i = 0; i < 120; ++i) {
        boat_session_step(&boat, (BoatInput) { 0 });
        CHECK(boat.boat.position.x < BOAT_BASIN_HALF_X && boat.boat.position.y < BOAT_BASIN_HALF_Z);
        if (boat.boat.velocity.x < 0 || boat.boat.velocity.y < 0) {
            corner_contact = true;
            break;
        }
    }
    CHECK(corner_contact);
    for (int i = 0; i < 120 * 120; ++i) {
        boat_session_step(&boat, (BoatInput) { sinf(i * 0.003f), 1 });
        CHECK(isfinite(boat.boat.heading) && isfinite(boat.boat.velocity.x)
            && isfinite(boat.boat.velocity.y));
        CHECK(isfinite(boat.boat.yaw_velocity));
        // Wall impulses can exchange translation for rotation. Bound their combined energy
        // using the old speed/yaw budget rather than imposing a free-turn yaw limit on impacts.
        float energy_limit = .5f * 23 * 23 + 1.1f * 4 * 4;
        if (!(kinetic_energy(&boat.boat) < energy_limit))
            fprintf(stderr, "Basin stress: heading=%g tick=%d yaw=%g speed=%g energy=%g\n", heading,
                i, boat.boat.yaw_velocity, hypotf(boat.boat.velocity.x, boat.boat.velocity.y),
                kinetic_energy(&boat.boat));
        CHECK(kinetic_energy(&boat.boat) < energy_limit);
        CHECK(isfinite(boat.presentation.camera_heading));
        CHECK(fabsf(boat.boat.position.x) < BOAT_BASIN_HALF_X);
        CHECK(fabsf(boat.boat.position.y) < BOAT_BASIN_HALF_Z);
        CHECK(hypotf(boat.boat.velocity.x, boat.boat.velocity.y) < 23);
    }
    return 0;
}

static int validate_hull_contacts(void)
{
    // A starboard-bow glancing impact should rebound, scrub speed, and turn the hull away.
    BoatSession glancing
        = basin_session((Boat) { .position = { 118.5f, 10 }, .velocity = { 7, -3 } });
    float energy = kinetic_energy(&glancing.boat);
    bool contacted = false;
    for (int tick = 0; tick < 180; ++tick) {
        boat_session_step(&glancing, (BoatInput) { 0 });
        CHECK(fabsf(glancing.boat.position.x) < BOAT_BASIN_HALF_X);
        CHECK(fabsf(glancing.boat.position.y) < BOAT_BASIN_HALF_Z);
        if (glancing.boat.velocity.x < 0) {
            contacted = true;
            break;
        }
    }
    CHECK(contacted);
    CHECK(glancing.boat.heading < 0);
    CHECK(kinetic_energy(&glancing.boat) < energy);

    // A faster beam impact legitimately exceeds the old 4 rad/s stress-test limit.
    // It must exchange translation for rotation without creating kinetic energy.
    BoatSession fast = basin_session((Boat) { .position = { 118.5f, 10 }, .velocity = { 19, -3 } });
    float peak_yaw = 0;
    for (int tick = 0; tick < 180; ++tick) {
        float before = kinetic_energy(&fast.boat);
        boat_session_step(&fast, (BoatInput) { 0 });
        CHECK(footprint_in_water(&fast));
        CHECK(kinetic_energy(&fast.boat) <= before + .00001f);
        peak_yaw = fmaxf(peak_yaw, fabsf(fast.boat.yaw_velocity));
    }
    CHECK(peak_yaw > 4);

    // A shallow scrape retains useful forward motion while the contact redirects the hull.
    BoatSession sliding
        = basin_session((Boat) { .position = { 118.89f, 10 }, .velocity = { .3f, -6 } });
    float sliding_energy = kinetic_energy(&sliding.boat);
    for (int tick = 0; tick < 60; ++tick) {
        boat_session_step(&sliding, (BoatInput) { 0 });
        CHECK(footprint_in_water(&sliding));
    }
    CHECK(sliding.boat.position.y < 8.5f);
    CHECK(kinetic_energy(&sliding.boat) > sliding_energy * .45f);
    CHECK(kinetic_energy(&sliding.boat) < sliding_energy);

    // Rotation into a wall used to leave an overlapping hull and block every later move.
    BoatSession rotating = basin_session((Boat) { .position = { 118.89f, 10 }, .yaw_velocity = 1 });
    for (int tick = 0; tick < 120; ++tick) {
        boat_session_step(&rotating, (BoatInput) { 0 });
        CHECK(footprint_in_water(&rotating));
    }

    // Bow, stern, and both beams stay contained across representative headings.
    for (int heading = 0; heading < 8; ++heading) {
        BoatSession hull = basin_session((Boat) { .position = { 0, 0 },
            .heading = heading * PI / 4,
            .velocity = { 13 * sinf(heading * PI / 4), -13 * cosf(heading * PI / 4) } });
        for (int tick = 0; tick < 3600; ++tick) {
            boat_session_step(&hull, (BoatInput) { 0 });
            CHECK(fabsf(hull.boat.position.x) < BOAT_BASIN_HALF_X);
            CHECK(fabsf(hull.boat.position.y) < BOAT_BASIN_HALF_Z);
            CHECK(isfinite(hull.boat.heading) && isfinite(hull.boat.yaw_velocity));
        }
    }
    return 0;
}

static int validate_course(void)
{
    BoatSession boat;
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat_session_start_course(&boat);
    CHECK(boat.course.enabled && boat.course.next_gate == 0);
    CHECK(boat.boat.position.x == 0 && boat.boat.position.y > boat_course_gates[0].centre.y);
    for (int i = 0; i < 120; ++i)
        boat_session_step(&boat, (BoatInput) { 0 });
    CHECK(boat.course.elapsed == 0);
    BoatCourse *course = &boat.course;
    // Wrong direction, outside the posts, and later gates cannot start the clock.
    boat_course_step(course, (Vector2) { 0, 34 }, (Vector2) { 0, 36 }, 1);
    boat_course_step(course, (Vector2) { 20, 36 }, (Vector2) { 20, 34 }, 1);
    boat_course_step(course, (Vector2) { 22, 8 }, (Vector2) { 22, 6 }, 1);
    CHECK(course->next_gate == 0 && course->elapsed == 0);
    // Interpolate both the crossing time and lateral position, even for a large step.
    boat_course_step(course, (Vector2) { -20, 36 }, (Vector2) { 20, 34 }, 1);
    CHECK(course->next_gate == 1 && fabs(course->elapsed - .5) < .00001);
    boat_course_step(course, (Vector2) { 0, -52 }, (Vector2) { 0, -54 }, 1);
    CHECK(course->next_gate == 1 && fabs(course->elapsed - 1.5) < .00001);
    boat_course_step(course, (Vector2) { 0, 8 }, (Vector2) { 0, 6 }, 1);
    CHECK(course->next_gate == 1); // Short-cut misses the first bend.
    boat_course_step(course, (Vector2) { 22, 8 }, (Vector2) { 22, 6 }, 1);
    CHECK(course->next_gate == 2);
    boat_course_step(course, (Vector2) { -22, -24 }, (Vector2) { -22, -22 }, 1);
    CHECK(course->next_gate == 2); // Reverse crossing does not advance.
    boat_course_step(course, (Vector2) { -22, -22 }, (Vector2) { -22, -24 }, 1);
    CHECK(course->next_gate == 3);
    double before_finish = course->elapsed;
    boat_course_step(course, (Vector2) { 0, -52 }, (Vector2) { 0, -56 }, 1);
    CHECK(course->next_gate == BOAT_COURSE_GATES);
    CHECK(fabs(course->elapsed - before_finish - .25) < .00001);
    double finished = course->elapsed;
    boat_course_step(course, (Vector2) { 0, 50 }, (Vector2) { 0, -60 }, 10);
    CHECK(course->elapsed == finished);
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat_session_start_course(&boat);
    CHECK(course->elapsed == 0 && course->next_gate == 0);
    // Integration: powered movement starts the timer on the fixed simulation ticks.
    boat.boat.throttle = 1;
    for (int i = 0; i < 360; ++i)
        boat_session_step(&boat, (BoatInput) { 0 });
    CHECK(course->next_gate == 1 && course->elapsed > 0);
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    CHECK(!course->enabled && course->elapsed == 0);
    return 0;
}

// Potential of the same unilateral springs and gravity, relative to calm-water equilibrium.
static float floating_energy(const Boat *boat)
{
    float energy = .5f * boat->vertical_velocity * boat->vertical_velocity
        + boat->pitch_velocity * boat->pitch_velocity
        + .225f * boat->roll_velocity * boat->roll_velocity + 9.81f * boat->height;
    float draft = 9.81f / 64;
    for (int end = -1; end <= 1; ++end) {
        for (int side = -1; side <= 1; side += 2) {
            float height = boat->height + side * .65f * sinf(boat->roll) * cosf(boat->pitch)
                + end * 1.4f * sinf(boat->pitch);
            float immersion = fmaxf(0, draft - height);
            energy += .5f * (64.0f / 6) * (immersion * immersion - draft * draft);
        }
    }
    return energy;
}

static int validate_buoyancy(void)
{
    BoatSession boat;
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    for (int tick = 0; tick < 1200; ++tick)
        boat_session_step(&boat, (BoatInput) { 0 });
    CHECK(fabsf(boat.boat.height) < .000001f && fabsf(boat.boat.vertical_velocity) < .000001f);
    CHECK(fabsf(boat.boat.pitch) < .000001f && fabsf(boat.boat.roll) < .000001f);
    // Drop, push under water, and disturb both angular modes with and without initial velocity.
    for (int trial = 0; trial < 4; ++trial) {
        boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
        boat.boat.height = trial == 0 ? 1.2f : trial == 1 ? -.35f : .1f;
        boat.boat.pitch = trial % 2 ? -.16f : .12f;
        boat.boat.roll = trial % 2 ? .14f : -.1f;
        boat.boat.vertical_velocity = trial == 2 ? -1 : 0;
        boat.boat.pitch_velocity = trial == 3 ? .4f : 0;
        boat.boat.roll_velocity = trial == 3 ? -.3f : 0;
        float initial_energy = floating_energy(&boat.boat);
        for (int tick = 0; tick < 1200; ++tick) {
            boat_session_step(&boat, (BoatInput) { 0 });
            CHECK(isfinite(boat.boat.height) && isfinite(boat.boat.pitch)
                && isfinite(boat.boat.roll));
            CHECK(floating_energy(&boat.boat) < initial_energy * 1.01f + .00001f);
        }
        CHECK(fabsf(boat.boat.height) < .0001f && fabsf(boat.boat.pitch) < .0001f);
        CHECK(fabsf(boat.boat.roll) < .0001f && floating_energy(&boat.boat) < .00001f);
    }
    // A static sloping wave must lift the correct bow and side (simulation X is mirrored).
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat.boat.position = (Vector2) { 0 };
    boat.world.water = boat_water_gentle();
    boat.world.water.angular_speed = 0;
    boat.world.water.crossing[0] = boat.world.water.crossing[1] = (BoatWave) { 0 };
    for (int tick = 0; tick < 720; ++tick)
        boat_session_step(&boat, (BoatInput) { 0 });
    CHECK(boat.boat.pitch > .01f && boat.boat.roll < -.01f);
    // The full rigid pose must carry every attachment through heave and roll as well as pitch.
    for (int angle = 0; angle < 16; ++angle) {
        boat.boat.heading = angle * PI / 8;
        boat.boat.height = .25f;
        boat.boat.pitch = .12f;
        boat.boat.roll = -.09f;
        boat.boat.engine_angle = .3f;
        WaterfrontSceneFrame frame;
        boat_scene_frame(
            SESSION_VIEW(&boat), &boat.presentation, &boat.world, BOAT_CAMERA_DEFAULT, &frame);
        Matrix expected = MatrixMultiply(
            MatrixMultiply(MatrixRotateZ(boat.boat.roll), MatrixRotateX(boat.boat.pitch)),
            MatrixRotateY(boat.boat.heading));
        Matrix actual = MatrixRotateXYZ(frame.player.hull.rotation);
        for (int axis = 0; axis < 3; ++axis) {
            Vector3 basis = { axis == 0, axis == 1, axis == 2 };
            CHECK(
                Vector3Distance(Vector3Transform(basis, actual), Vector3Transform(basis, expected))
                < .0001f);
        }
        Vector3 cabin = Vector3Add(
            frame.player.hull.position, Vector3Transform((Vector3) { 0, 1.1f, 0 }, expected));
        Vector3 engine = Vector3Add(
            frame.player.hull.position, Vector3Transform((Vector3) { 0, .4f, 1.6f }, expected));
        CHECK(Vector3Distance(cabin, frame.player.cabin.position) < .0001f);
        CHECK(Vector3Distance(engine, frame.player.engine.position) < .0001f);
        CHECK(frame.player.hull.position.y == boat.boat.height);
        CHECK(frame.camera.position.y == BOAT_CAMERA_DEFAULT.height);
        CHECK(frame.camera.up.x == 0 && frame.camera.up.y == 1 && frame.camera.up.z == 0);
    }
    // Repeatable rest, head-on, beam and following sampling at low and cruising speeds.
    for (int encounter = 0; encounter < 4; ++encounter) {
        for (int speed = 0; speed <= 10; speed += 5) {
            boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
            boat.boat.position = (Vector2) { 0 };
            boat.world.water = boat_water_gentle();
            boat.boat.heading = atan2f(.6f, .8f) + encounter * PI / 2;
            BoatSession repeat = boat;
            float motion = 0;
            for (int tick = 0; tick < 960; ++tick) {
                boat.boat.velocity = repeat.boat.velocity
                    = (Vector2) { speed * sinf(boat.boat.heading),
                          -speed * cosf(boat.boat.heading) };
                boat_session_step(&boat, (BoatInput) { 0 });
                boat_session_step(&repeat, (BoatInput) { 0 });
                CHECK(fabsf(boat.boat.height) < .4f && fabsf(boat.boat.pitch) < .35f
                    && fabsf(boat.boat.roll) < .35f);
                motion = fmaxf(motion, fabsf(boat.boat.height));
                for (int i = 0; i < BOAT_WATER_PARTICLES; ++i) {
                    BoatWaterParticle p = boat.presentation.water[i];
                    if (p.lifetime <= 0)
                        continue;
                    float surface
                        = boat_water_query(boat.world.water, p.position, boat_session_time(&boat))
                              .height;
                    if (!p.spray)
                        CHECK(fabsf(p.height - surface - .015f) < .00001f);
                    else
                        CHECK(p.height >= surface);
                }
            }
            CHECK(motion > .02f);
            CHECK(memcmp(&boat.boat, &repeat.boat, sizeof(Boat)) == 0);
        }
    }
    // Longer powered/reversing run, then remove the forcing and recover in calm water.
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat.world.water = boat_water_gentle();
    float combined_peak_roll = 0;
    int peak_tick = 0;
    BoatSession peak_state = basin_session((Boat) { 0 });
    for (int tick = 0; tick < 36000; ++tick) {
        boat_session_step(&boat, (BoatInput) { sinf(tick * .005f), sinf(tick * .001f) });
        CHECK(isfinite(boat.boat.height) && fabsf(boat.boat.height) < .5f);
        CHECK(isfinite(boat.boat.pitch) && fabsf(boat.boat.pitch) < .4f);
        CHECK(isfinite(boat.boat.roll));
        // T3 tuned the passive guard against this coupled stress drive; no angle clamp.
        CHECK(fabsf(boat.boat.roll) < 30 * DEG2RAD);
        if (fabsf(boat.boat.roll) > combined_peak_roll) {
            combined_peak_roll = fabsf(boat.boat.roll);
            peak_tick = tick;
            peak_state = boat;
        }
    }
    printf("combined_300s_peak_roll_deg=%.9g time=%.6f load=%.9g rate=%.9g speed=%.9g\n",
        combined_peak_roll * RAD2DEG, (peak_tick + 1) * BOAT_STEP, peak_state.boat.turn_roll_load,
        peak_state.boat.roll_velocity, Vector2Length(peak_state.boat.velocity));
    boat.world.water = (BoatWater) { 0 };
    boat.boat.throttle = boat.boat.engine_response = 0;
    for (int tick = 0; tick < 2400; ++tick)
        boat_session_step(&boat, (BoatInput) { 0 });
    CHECK(fabsf(boat.boat.height) < .0001f && fabsf(boat.boat.pitch) < .0001f);
    CHECK(fabsf(boat.boat.roll) < .0001f);
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    CHECK(boat.boat.height == 0 && boat.boat.vertical_velocity == 0);
    CHECK(boat.boat.pitch == 0 && boat.boat.pitch_velocity == 0 && boat.boat.roll == 0
        && boat.boat.roll_velocity == 0 && boat.boat.turn_roll_load == 0);
    return 0;
}

static int validate_wave_resistance(void)
{
    // Test resistance in isolation: its work is nonpositive relative to local moving water.
    for (int i = 0; i < 2000; ++i) {
        BoatWaterSample water = boat_water_query(
            boat_water_gentle(), (Vector2) { i * .13f, sinf(i * .07f) * 20 }, i * .021f);
        water.velocity.x = sinf(i * .11f) * .4f;
        water.velocity.z = cosf(i * .05f) * .3f;
        Vector3 velocity = { sinf(i * .07f) * 15, cosf(i * .17f) * 3, cosf(i * .03f) * 12 };
        Vector3 relative = Vector3Subtract(velocity, water.velocity);
        float support = .2f + (i % 13) * .4f;
        Vector3 force = boat_water_resistance(water, velocity, 3, support);
        CHECK(Vector3DotProduct(force, relative) <= .00001f);
        CHECK(support + force.y >= -.00001f);
        CHECK(Vector3Length(Vector3CrossProduct(force, water.normal)) < .00001f);
        Vector3 next = Vector3Add(relative, Vector3Scale(force, BOAT_STEP));
        CHECK(Vector3LengthSqr(next) <= Vector3LengthSqr(relative) + .0001f);
        CHECK(Vector3LengthSqr(boat_water_resistance(water, velocity, 3, 0)) == 0);
        CHECK(Vector3LengthSqr(boat_water_resistance(water, water.velocity, 3, support)) == 0);
    }
    // Moving water can supply ground-frame energy, without supplying relative-frame energy.
    BoatWaterSample rising = { .normal = { 0, 1, 0 }, .velocity = { 0, .5f, 0 } };
    Vector3 force = boat_water_resistance(rising, (Vector3) { 0 }, 3, 2);
    CHECK(force.y > 0);
    CHECK(Vector3DotProduct(force, Vector3Negate(rising.velocity)) < 0);
    rising.velocity.y = 0;
    CHECK(boat_water_resistance(rising, (Vector3) { 0, 50, 0 }, 3, 2).y == -2);
    return 0;
}

typedef struct {
    float angular_motion, vertical_acceleration, spray_load;
    BoatSession final;
} Encounter;

static Encounter wave_encounter(float heading, float speed)
{
    Encounter result = { 0 };
    boat_session_init(&result.final, test_world(BOAT_LOCATION_BASIN), boat_water_gentle());
    Boat *boat = &result.final.boat;
    boat->position = (Vector2) { 0 };
    for (int tick = 0; tick < 960; ++tick) {
        // Controlled straight tow isolates heading and speed; it supplies horizontal work.
        boat->heading = heading;
        boat->yaw_velocity = 0;
        boat->velocity = (Vector2) { speed * sinf(heading), -speed * cosf(heading) };
        float previous = boat->vertical_velocity;
        boat_session_step(&result.final, (BoatInput) { 0 });
        if (tick >= 240) {
            float acceleration = (boat->vertical_velocity - previous) / BOAT_STEP;
            result.angular_motion += boat->pitch_velocity * boat->pitch_velocity
                + boat->roll_velocity * boat->roll_velocity;
            result.vertical_acceleration += acceleration * acceleration;
            result.spray_load += boat->wave_spray;
        }
    }
    result.angular_motion = sqrtf(result.angular_motion / 720);
    result.vertical_acceleration = sqrtf(result.vertical_acceleration / 720);
    result.spray_load /= 720;
    return result;
}

static int validate_gentle_encounters(void)
{
    float following = atan2f(.6f, .8f);
    Encounter fast[3];
    for (int angle = 0; angle < 3; ++angle) {
        float heading = following + (angle == 0 ? PI : angle == 1 ? PI / 2 : 0);
        Encounter slow = wave_encounter(heading, 2);
        fast[angle] = wave_encounter(heading, 12);
        Encounter repeat = wave_encounter(heading, 12);
        CHECK(memcmp(&fast[angle].final.boat, &repeat.final.boat, sizeof(Boat)) == 0);
        CHECK(memcmp(&fast[angle].final.presentation, &repeat.final.presentation,
                  sizeof(BoatPresentation))
            == 0);
        CHECK(slow.angular_motion < fast[angle].angular_motion * .8f);
        CHECK(slow.vertical_acceleration < fast[angle].vertical_acceleration);
        CHECK(slow.spray_load < fast[angle].spray_load);
        CHECK(fast[angle].angular_motion < .4f && fast[angle].vertical_acceleration < 5);
        CHECK(fast[angle].spray_load < .4f);
        WaterfrontSceneFrame frame;
        boat_scene_frame(SESSION_VIEW(&fast[angle].final), &fast[angle].final.presentation,
            &fast[angle].final.world, BOAT_CAMERA_DEFAULT, &frame);
        CHECK(frame.camera.up.y == 1 && frame.camera.position.y == BOAT_CAMERA_DEFAULT.height);
        CHECK(frame.player.particle_count > 0);
        int foam_count = 0;
        for (int i = 0; i < frame.player.particle_count; ++i) {
            WaterfrontPlayerParticle p = frame.player.particles[i];
            Vector2 position = { -p.transform.position.x, p.transform.position.z };
            BoatWaterSample water = boat_water_query(
                fast[angle].final.world.water, position, boat_session_time(&fast[angle].final));
            if (p.spray)
                CHECK(p.transform.position.y >= water.height);
            else {
                Vector3 up = Vector3Transform(
                    (Vector3) { 0, 1, 0 }, MatrixRotateXYZ(p.transform.rotation));
                Vector3 normal = { -water.normal.x, water.normal.y, water.normal.z };
                CHECK(Vector3Distance(up, normal) < .0001f);
                CHECK(fabsf(p.transform.position.y - water.height - .015f) < .00001f);
                ++foam_count;
            }
        }
        CHECK(foam_count > 0);
    }
    CHECK(fast[0].vertical_acceleration > fast[2].vertical_acceleration * 2);
    CHECK(fast[1].angular_motion > fast[2].angular_motion);
    // Wave forces cannot bypass the existing hull/shore containment.
    for (int angle = 0; angle < 4; ++angle) {
        BoatSession boat;
        boat_session_init(&boat, test_world(BOAT_LOCATION_RIVER), (BoatWater) { 0 });
        boat.world.water = boat_water_gentle();
        boat.boat.heading = angle * PI / 2;
        boat.boat.throttle = 1;
        for (int tick = 0; tick < 1200; ++tick) {
            boat_session_step(&boat, (BoatInput) { 0 });
            CHECK(test_river_water(boat.boat.position, .3f));
            CHECK(isfinite(boat.boat.heading) && fabsf(boat.boat.yaw_velocity) < 4);
        }
    }
    // Unpowered waves may transfer energy, but must not create a runaway neutral drift or turn.
    BoatSession neutral;
    boat_session_init(&neutral, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    neutral.world.water = boat_water_gentle();
    for (int tick = 0; tick < 14400; ++tick) {
        boat_session_step(&neutral, (BoatInput) { 0 });
        CHECK(Vector2Length(neutral.boat.velocity) < .5f);
        CHECK(fabsf(neutral.boat.yaw_velocity) < .2f);
        CHECK(fabsf(neutral.boat.height) < .4f && fabsf(neutral.boat.pitch) < .3f
            && fabsf(neutral.boat.roll) < .3f);
    }
    Boat reset = fast[0].final.boat;
    boat_reset(&reset);
    CHECK(reset.wave_spray == 0);
    return 0;
}

static int validate_calm_reference(void)
{
    // Straight sample retains the accepted C2 baseline. Turning/coasting samples are
    // measured H3 trajectories. Revision 5 updates only the final coast after dry-stern
    // skeg disengagement; the powered straight/turn references remain unchanged.
    const float expected[3][10] = { { 0, 3.60687423f, 0, -8.85409927f, 0, 0, .00596522959f,
                                        .0537321381f, 0, .276249886f },
        { 23.757431f, -49.1584587f, 15.6087561f, 1.88263535f, 1.85332334f, 1.05519712f,
            .0500000007f, .053120777f, .211947575f, 1 },
        { 39.4533844f, -33.0364151f, 9.76989174f, -13.2869892f, -2.50756764f, 0, 0, 0, 0, 0 } };
    BoatSession boat;
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    int sample = 0;
    for (int tick = 0; tick < 1800; ++tick) {
        float wheel = tick >= 360 && tick < 900 ? .35f : 0;
        float thrust = tick < 240 ? 1 : tick < 900 ? 0 : tick < 1380 ? -1 : 0;
        boat_session_step(&boat, (BoatInput) { wheel, thrust });
        CHECK(boat.boat.wave_spray == 0);
        if (tick == 239 || tick == 719 || tick == 1799) {
            const float actual[]
                = { boat.boat.position.x, boat.boat.position.y, boat.boat.velocity.x,
                      boat.boat.velocity.y, boat.boat.heading, boat.boat.yaw_velocity,
                      boat.boat.height, boat.boat.pitch, boat.boat.roll, boat.boat.planing };
            for (int i = 0; i < 10; ++i)
                CHECK(fabsf(actual[i] - expected[sample][i]) < (i < 4 ? .002f : .00001f));
            if (sample == 1) {
                printf("banked_calm_reference height=%.9g pitch=%.9g roll=%.9g load=%.9g\n",
                    boat.boat.height, boat.boat.pitch, boat.boat.roll, boat.boat.turn_roll_load);
                // Measured H3 attitude, backed by independent force/filter/tow/entry checks.
                CHECK(fabsf(boat.boat.height - .0500000007f) < .00001f);
                CHECK(fabsf(boat.boat.pitch - .053120777f) < .00001f);
                CHECK(fabsf(boat.boat.roll - .211947575f) < .00001f);
                CHECK(boat.boat.roll > 5 * DEG2RAD && boat.boat.roll < 25 * DEG2RAD);
                CHECK(fabsf(boat.boat.height) < .2f && fabsf(boat.boat.pitch) < .15f);
                CHECK(boat.boat.turn_roll_load > 0);
            }
            ++sample;
        }
    }
    return 0;
}

static int shared_water(void)
{
    BoatWater water = boat_water_gentle();
    water.crossing[0] = water.crossing[1] = (BoatWave) { 0 };
    BoatWaterSample q = boat_water_query((BoatWater) { 0 }, (Vector2) { 83, -51 }, 120);
    CHECK(q.height == 0 && q.normal.x == 0 && q.normal.y == 1 && q.normal.z == 0);
    CHECK(Vector3LengthSqr(q.velocity) == 0);
    q = boat_water_query(water, (Vector2) { 0 }, 0);
    CHECK(q.height == 0 && q.normal.x < 0 && q.normal.z > 0);
    CHECK(fabsf(q.velocity.y + water.amplitude * water.angular_speed) < .00001f);
    Vector2 crest = Vector2Scale(water.direction, water.wavelength / 4);
    q = boat_water_query(water, crest, 0);
    CHECK(fabsf(q.height - water.amplitude) < .00001f);
    CHECK(fabsf(q.normal.y - 1) < .00001f && fabsf(q.velocity.y) < .00001f);
    BoatWaterSample moved = boat_water_query(water,
        Vector2Scale(water.direction,
            water.wavelength / 4 + water.angular_speed / (2 * PI / water.wavelength)),
        1);
    CHECK(fabsf(moved.height - q.height) < .00001f);
    water = boat_water_gentle();
    for (int i = 0; i < 10000; ++i) {
        float time = i * .13f;
        Vector2 point = { sinf(time) * 250, cosf(time) * 300 };
        q = boat_water_query(water, point, time);
        CHECK(isfinite(q.height)
            && fabsf(q.height) <= water.amplitude + water.crossing[0].amplitude
                    + water.crossing[1].amplitude + .00001f);
        CHECK(fabsf(Vector3Length(q.normal) - 1) < .00001f);
        float dt = .002f;
        if (time < 20) {
            float derivative = (boat_water_query(water, point, time + dt).height
                                   - boat_water_query(water, point, time - dt).height)
                / (2 * dt);
            CHECK(fabsf(derivative - q.velocity.y) < .001f);
            float ds = .02f;
            Vector2 dx = { point.x + ds, point.y }, dz = { point.x, point.y + ds };
            float sx = (boat_water_query(water, dx, time).height - q.height) / ds;
            float sz = (boat_water_query(water, dz, time).height - q.height) / ds;
            CHECK(fabsf(sx + q.normal.x / q.normal.y) < .001f);
            CHECK(fabsf(sz + q.normal.z / q.normal.y) < .001f);
        }
    }
    BoatWaterMeshCache cache = { 0 };
    for (int location = 0; location < 2; ++location) {
        const Mesh3D *mesh = boat_water_surface(&cache, boat_location(location)->map, water, .7f);
        CHECK(mesh && mesh->triangle_count > 100);
        for (size_t i = 0; i < mesh->triangle_count; ++i) {
            Vector3 centre = { 0 };
            float height = 0;
            for (int j = 0; j < 3; ++j) {
                Vector3 v = mesh->vertices[mesh->triangles[i].vertices[j]];
                q = boat_water_query(water, (Vector2) { -v.x, v.z }, .7f);
                CHECK(fabsf(v.y - q.height) < .000001f);
                centre = Vector3Add(centre, Vector3Scale(v, 1.0f / 3));
                height += v.y / 3;
            }
            q = boat_water_query(water, (Vector2) { -centre.x, centre.z }, .7f);
            CHECK(fabsf(height - q.height) < .035f); // Bounded interpolation error.
        }
    }
    BoatSession flat, waves;
    boat_session_init(&flat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    waves = flat;
    waves.world.water = water;
    for (int i = 0; i < 2400; ++i) {
        boat_session_step(&flat, (BoatInput) { sinf(i * .01f), i < 300 ? 1 : 0 });
        boat_session_step(&waves, (BoatInput) { sinf(i * .01f), i < 300 ? 1 : 0 });
    }
    CHECK(Vector2Distance(flat.boat.position, waves.boat.position) > .01f);
    CHECK(Vector2Distance(flat.boat.velocity, waves.boat.velocity) > .001f);
    CHECK(fabsf(flat.boat.pitch - waves.boat.pitch) + fabsf(flat.boat.height - waves.boat.height)
        > .001f);
    boat_session_init(&waves, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    CHECK(waves.world.water.amplitude == 0 && boat_session_time(&waves) == 0);
    boat_water_mesh_cache_destroy(&cache);
    return 0;
}

static bool patch_contains(const Mesh3D *mesh, Vector2 p)
{
    for (size_t i = 0; i < mesh->triangle_count; ++i) {
        Vector3 a = mesh->vertices[mesh->triangles[i].vertices[0]];
        Vector3 b = mesh->vertices[mesh->triangles[i].vertices[1]];
        Vector3 c = mesh->vertices[mesh->triangles[i].vertices[2]];
        if (p.x < fminf(a.x, fminf(b.x, c.x)) || p.x > fmaxf(a.x, fmaxf(b.x, c.x))
            || p.y < fminf(a.z, fminf(b.z, c.z)) || p.y > fmaxf(a.z, fmaxf(b.z, c.z)))
            continue;
        float ab = (b.x - a.x) * (p.y - a.z) - (b.z - a.z) * (p.x - a.x);
        float bc = (c.x - b.x) * (p.y - b.z) - (c.z - b.z) * (p.x - b.x);
        float ca = (a.x - c.x) * (p.y - c.z) - (a.z - c.z) * (p.x - c.x);
        if ((ab >= -1e-5f && bc >= -1e-5f && ca >= -1e-5f)
            || (ab <= 1e-5f && bc <= 1e-5f && ca <= 1e-5f))
            return true;
    }
    return false;
}

static uint64_t patch_overlap(const Mesh3D *mesh)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < mesh->triangle_count; ++i) {
        bool inside = true;
        for (int j = 0; j < 3; ++j) {
            Vector3 p = mesh->vertices[mesh->triangles[i].vertices[j]];
            inside &= fabsf(p.x) < 40 && p.z > -280 && p.z < -220;
        }
        if (!inside)
            continue;
        for (int j = 0; j < 3; ++j) {
            Vector3 p = mesh->vertices[mesh->triangles[i].vertices[j]];
            hash = (hash ^ (uint32_t)lrintf(p.x * 1000)) * UINT64_C(1099511628211);
            hash = (hash ^ (uint32_t)lrintf(p.z * 1000)) * UINT64_C(1099511628211);
        }
    }
    return hash;
}

static int validate_water_patch(void)
{
    BoatWater water = boat_water_gentle();
    BoatWaterMeshCache cache = { 0 };
    const Vector2 centers[]
        = { { 12, 60 }, { 12, -125 }, { 170, -250 }, { -80, -455 }, { -60, -175 }, { 940, 100 } };
    for (int detail = 0; detail < 2; ++detail)
        for (int location = 0; location < 2; ++location)
            for (size_t k = 0; k < sizeof(centers) / sizeof(*centers); ++k) {
                if (!location && k > 1)
                    break;
                Vector2 center = location ? centers[k] : (Vector2) { k ? 100 : 0, 0 };
                const BoatMapDescription *map = boat_location(location)->map;
                const Mesh3D *mesh = detail ? boat_water_patch_arcade(&cache, map, center)
                                            : boat_water_patch(&cache, map, center);
                CHECK(mesh && mesh->triangle_count > 0
                    && mesh->triangle_count < (detail ? 80000 : 50000));
                for (size_t i = 0; i < mesh->triangle_count; ++i) {
                    Vector3 centroid = { 0 };
                    float height = 0;
                    for (int j = 0; j < 3; ++j) {
                        Vector3 p = mesh->vertices[mesh->triangles[i].vertices[j]];
                        Vector3 q = mesh->vertices[mesh->triangles[i].vertices[(j + 1) % 3]];
                        CHECK(p.y == 0 && isfinite(p.x) && isfinite(p.z));
                        CHECK(Vector3Distance(p, q) <= 2.122f);
                        Vector2 snapped
                            = { roundf(center.x / 12) * 12, roundf(center.y / 12) * 12 };
                        if (detail && fabsf(p.x - snapped.x) < 23 && fabsf(q.x - snapped.x) < 23
                            && fabsf(p.z - snapped.y) < 23 && fabsf(q.z - snapped.y) < 23)
                            CHECK(Vector3Distance(p, q) <= 1.061f);
                        centroid = Vector3Add(centroid, Vector3Scale(p, 1.0f / 3));
                        height += boat_water_query(water, (Vector2) { -p.x, p.z }, .7f).height / 3;
                    }
                    float exact
                        = boat_water_query(water, (Vector2) { -centroid.x, centroid.z }, .7f)
                              .height;
                    CHECK(fabsf(height - exact) < .035f);
                }
                // Sample the entire visible disk against the original water, including land,
                // narrow dock edges, the southern bank, and the artificial map crop.
                for (int z = -78; z <= 78; z += 6)
                    for (int x = -78; x <= 78; x += 6) {
                        if (x * x + z * z > 78 * 78)
                            continue;
                        Vector2 p = { center.x + x + .17f, center.y + z + .23f };
                        bool expected = location ? test_river_water((Vector2) { -p.x, p.y }, 0)
                                                 : fabsf(p.x) <= 120 && fabsf(p.y) <= 80;
                        CHECK(patch_contains(mesh, p) == expected);
                    }
            }
    const BoatMapDescription *river = boat_location(BOAT_LOCATION_RIVER)->map;
    const Mesh3D *mesh = boat_water_patch(&cache, river, (Vector2) { 5.99f, -250 });
    CHECK(mesh);
    uint64_t before = patch_overlap(mesh);
    mesh = boat_water_patch(&cache, river, (Vector2) { 6.01f, -250 });
    CHECK(mesh && before == patch_overlap(mesh));
    // Recentring changes only cells beyond the rendered disk, not overlapping world geometry.
    boat_water_mesh_cache_destroy(&cache);
    return 0;
}

static uint64_t mesh_digest(const Mesh3D *mesh)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < mesh->triangle_count; ++i) {
        unsigned char bytes[3 * sizeof(Vector3) + sizeof(Color)];
        for (int j = 0; j < 3; ++j)
            memcpy(bytes + j * sizeof(Vector3), &mesh->vertices[mesh->triangles[i].vertices[j]],
                sizeof(Vector3));
        memcpy(bytes + 3 * sizeof(Vector3), &mesh->triangles[i].color, sizeof(Color));
        for (size_t j = 0; j < sizeof(bytes); ++j)
            hash = (hash ^ bytes[j]) * UINT64_C(1099511628211);
    }
    return hash ^ mesh->triangle_count;
}

// Synthetic 10 m pond in map east/Z, followed by one raised land triangle.
static const Vector3 pond_vertices[]
    = { { 0, 0, 0 }, { 10, 0, 0 }, { 10, 0, 10 }, { 0, 0, 10 }, { 20, 2, 0 } };
static const Triangle3D pond_triangles[]
    = { { { 0, 2, 1 }, WHITE }, { { 0, 3, 2 }, WHITE }, { { 1, 2, 4 }, GREEN } };
static const Mesh3D pond_mesh = { pond_vertices, pond_triangles, 3 };
static const BoatMapDescription pond = { &pond_mesh, 3 };

// Patch digest built alone in a fresh cache: the reference for interleaved caches.
static uint64_t patch_alone(const BoatMapDescription *map, Vector2 center, bool fine)
{
    BoatWaterMeshCache cache = { 0 };
    const Mesh3D *mesh = fine ? boat_water_patch_arcade(&cache, map, center)
                              : boat_water_patch(&cache, map, center);
    uint64_t digest = mesh ? mesh_digest(mesh) : 0;
    boat_water_mesh_cache_destroy(&cache);
    return digest;
}

static int validate_water_mesh_caches(void)
{
    const BoatMapDescription *river = boat_location(BOAT_LOCATION_RIVER)->map;
    const Vector2 a_center = { 12, -125 }, b_center = { 170, -250 }, basin_center = { 100, 0 };
    uint64_t a_ref = patch_alone(river, a_center, false);
    uint64_t b_ref = patch_alone(river, b_center, true);
    uint64_t basin_ref = patch_alone(NULL, basin_center, false);
    uint64_t pond_ref = patch_alone(&pond, (Vector2) { 5, 5 }, false);
    CHECK(a_ref && b_ref && basin_ref && pond_ref && a_ref != b_ref);

    // Two river views at different centres and a basin view hold their meshes together.
    BoatWaterMeshCache a = { 0 }, b = { 0 }, basin = { 0 };
    const Mesh3D *a_mesh = boat_water_patch(&a, river, a_center);
    const Mesh3D *b_mesh = boat_water_patch_arcade(&b, river, b_center);
    const Mesh3D *basin_mesh = boat_water_patch(&basin, NULL, basin_center);
    CHECK(a_mesh && b_mesh && basin_mesh && a.map == river && b.map == river && !basin.map);
    CHECK(mesh_digest(a_mesh) == a_ref && mesh_digest(b_mesh) == b_ref);
    CHECK(mesh_digest(basin_mesh) == basin_ref);
    // The snap-boundary pair through B leaves A's and the basin's borrowed meshes intact.
    const Mesh3D *snap = boat_water_patch(&b, river, (Vector2) { 5.99f, -250 });
    CHECK(snap);
    uint64_t overlap = patch_overlap(snap);
    snap = boat_water_patch(&b, river, (Vector2) { 6.01f, -250 });
    CHECK(snap && patch_overlap(snap) == overlap);
    CHECK(mesh_digest(a_mesh) == a_ref && mesh_digest(basin_mesh) == basin_ref);

    // Destroy and rebuild B; the survivors keep their storage and values.
    const Vector3 *a_vertices = a_mesh->vertices;
    boat_water_mesh_cache_destroy(&b);
    CHECK(!b.map && !b.patches[0].vertices && !b.patches[1].vertices && !b.surface.vertices);
    boat_water_mesh_cache_destroy(&b); // Idempotent.
    CHECK(mesh_digest(a_mesh) == a_ref && a_mesh->vertices == a_vertices);
    CHECK(mesh_digest(basin_mesh) == basin_ref);
    b_mesh = boat_water_patch_arcade(&b, river, b_center);
    CHECK(b_mesh && mesh_digest(b_mesh) == b_ref && mesh_digest(a_mesh) == a_ref);

    // A cache rebinds to whichever description it is asked for; nothing leaks between maps.
    BoatWaterMeshCache c = { 0 };
    const Mesh3D *mesh = boat_water_patch(&c, &pond, (Vector2) { 5, 5 });
    CHECK(mesh && c.map == &pond && mesh_digest(mesh) == pond_ref);
    for (size_t i = 0; i < mesh->triangle_count; ++i)
        for (int j = 0; j < 3; ++j) {
            Vector3 v = mesh->vertices[mesh->triangles[i].vertices[j]];
            CHECK(v.y == 0 && v.x >= 0 && v.x <= 10 && v.z >= 0 && v.z <= 10);
        }
    mesh = boat_water_patch(&c, river, a_center);
    CHECK(mesh && c.map == river && mesh_digest(mesh) == a_ref);
    mesh = boat_water_patch(&c, &pond, (Vector2) { 5, 5 });
    CHECK(mesh && mesh_digest(mesh) == pond_ref);

    // CPU surfaces: per-cache and per-map, unaffected by other caches' rebuilds.
    BoatWater water = boat_water_gentle();
    const Mesh3D *pond_surface = boat_water_surface(&c, &pond, water, .7);
    const Mesh3D *basin_surface = boat_water_surface(&basin, NULL, water, .7);
    CHECK(pond_surface && basin_surface && pond_surface->triangle_count > 2);
    CHECK(pond_surface->triangle_count < basin_surface->triangle_count);
    uint64_t pond_surface_ref = mesh_digest(pond_surface);
    uint64_t basin_surface_ref = mesh_digest(basin_surface);
    boat_water_mesh_cache_destroy(&a);
    mesh = boat_water_surface(&a, NULL, water, .7);
    CHECK(mesh && mesh_digest(mesh) == basin_surface_ref);
    CHECK(mesh_digest(pond_surface) == pond_surface_ref);
    CHECK(mesh_digest(basin_surface) == basin_surface_ref);
    CHECK(mesh_digest(basin_mesh) == basin_ref && mesh_digest(b_mesh) == b_ref);

    boat_water_mesh_cache_destroy(&a);
    boat_water_mesh_cache_destroy(&b);
    boat_water_mesh_cache_destroy(&c);
    boat_water_mesh_cache_destroy(&basin);
    return 0;
}

static int validate_throttle_detents(void)
{
    // Exercise every transition through the real integrator, including crossing neutral.
    for (int from = 0; from < BOAT_THROTTLE_COUNT; ++from) {
        for (int to = 0; to < BOAT_THROTTLE_COUNT; ++to) {
            BoatSession session = basin_session((Boat) { .throttle = boat_throttle_setting(from) });
            float target = boat_throttle_setting(to);
            for (int tick = 0; tick < 360; ++tick) {
                float previous = session.boat.throttle;
                boat_session_step(
                    &session, (BoatInput) { 0, boat_throttle_rate(previous, target) });
                CHECK(session.boat.throttle >= fminf(previous, target) - .000001f);
                CHECK(session.boat.throttle <= fmaxf(previous, target) + .000001f);
            }
            CHECK(fabsf(session.boat.throttle - target) < .000001f);
            CHECK(boat_throttle_rate(session.boat.throttle, target) == 0);
        }
    }
    int step = BOAT_THROTTLE_NEUTRAL;
    for (int i = 0; i < 10; ++i)
        step = boat_throttle_step(step, 1);
    CHECK(step == 6);
    for (int i = 0; i < 10; ++i)
        step = boat_throttle_step(step, -1);
    CHECK(step == 0);
    float waited = 0;
    step = boat_throttle_hold(BOAT_THROTTLE_NEUTRAL, 1, 1, .1f, &waited);
    CHECK(step == 3 && waited == 0); // A press steps at once.
    for (int frame = 0; frame < 4; ++frame)
        step = boat_throttle_hold(step, 0, 1, .1f, &waited);
    CHECK(step == 3); // 0.4 s held: still waiting.
    step = boat_throttle_hold(step, 0, 1, .1f, &waited);
    CHECK(step == 4); // 0.5 s held: the next detent.
    step = boat_throttle_hold(step, 1, 1, .1f, &waited);
    CHECK(step == 5 && waited == 0); // A quick re-press steps without waiting.
    step = boat_throttle_hold(step, 0, 0, .1f, &waited);
    CHECK(step == 5 && waited == 0); // Release holds the detent.
    step = boat_throttle_hold(step, -1, -1, .1f, &waited);
    for (int frame = 0; frame < 10; ++frame)
        step = boat_throttle_hold(step, 0, -1, .1f, &waited);
    CHECK(step == 2); // One press plus two half-second repeats in reverse.
    for (int frame = 0; frame < 100; ++frame)
        step = boat_throttle_hold(step, 0, -1, .1f, &waited);
    CHECK(step == 0);
    CHECK(boat_throttle_hold(step, 0, 1, NAN, &waited) == 0 && waited == 0);
    float remainder = 0;
    step = boat_throttle_scroll(2, .25f, &remainder);
    CHECK(step == 2 && remainder == .25f);
    step = boat_throttle_scroll(step, .75f, &remainder);
    CHECK(step == 3 && remainder == 0);
    step = boat_throttle_scroll(step, 2, &remainder);
    CHECK(step == 5);
    step = boat_throttle_scroll(step, -4, &remainder);
    CHECK(step == 1); // Cross neutral using the same forward/reverse ordering.
    step = boat_throttle_scroll(step, -100, &remainder);
    CHECK(step == 0 && remainder == 0);
    step = boat_throttle_scroll(step, 1, &remainder);
    CHECK(step == 1);
    step = boat_throttle_scroll(step, 100, &remainder);
    CHECK(step == 6 && remainder == 0);
    step = boat_throttle_scroll(step, -1, &remainder);
    CHECK(step == 5);
    CHECK(boat_throttle_scroll(step, NAN, &remainder) == 5);
    return 0;
}

static int validate_mouse_steering(void)
{
    CHECK(boat_mouse_wheel(.4f, 0) == .4f); // Stationary mouse holds its wheel state.
    CHECK(boat_mouse_wheel(0, 300) == 1 && boat_mouse_wheel(0, -300) == -1);
    CHECK(boat_mouse_wheel(1, 900) == 1 && boat_mouse_wheel(-1, -900) == -1);
    CHECK(fabsf(boat_mouse_wheel(1, -30) - .9f) < .000001f); // No full-lock windup.
    CHECK(fabsf(boat_mouse_wheel(.4f, -120)) < .000001f);
    CHECK(boat_mouse_wheel(.4f, NAN) == .4f);
    CHECK(boat_mouse_wheel(NAN, 0) == 0);
    float wheel_60 = 0, wheel_144 = 0;
    for (int i = 0; i < 60; ++i)
        wheel_60 = boat_mouse_wheel(wheel_60, 120.0f / 60);
    for (int i = 0; i < 144; ++i)
        wheel_144 = boat_mouse_wheel(wheel_144, 120.0f / 144);
    CHECK(fabsf(wheel_60 - wheel_144) < .00001f); // Movement, not frame rate.
    const float targets[] = { -1, -.7f, -.01f, 0, .01f, .7f, 1 };
    for (int quantized = 0; quantized < 2; ++quantized) {
        for (int from = 0; from < 7; ++from) {
            for (int to = 0; to < 7; ++to) {
                BoatSession session = basin_session((Boat) { .wheel = targets[from] });
                for (int tick = 0; tick < 600; ++tick) {
                    float rate = boat_steering_rate(session.boat.wheel, targets[to]);
                    if (quantized)
                        rate = roundf(rate * 127) / 127;
                    boat_session_step(&session, (BoatInput) { rate, 0 });
                    if (tick >= 360)
                        CHECK(fabsf(session.boat.wheel - targets[to]) < .0002f);
                }
            }
        }
    }
    return 0;
}

int main(void)
{
    test_worlds_open();
    CHECK(validate_throttle_detents() == 0);
    CHECK(validate_mouse_steering() == 0);
    CHECK(validate_flight_outcomes() == 0);
    CHECK(validate_dynamic_pair() == 0);
    CHECK(validate_water_patch() == 0);
    CHECK(validate_water_mesh_caches() == 0);
    CHECK(shared_water() == 0);
    CHECK(validate_buoyancy() == 0);
    CHECK(validate_wave_resistance() == 0);
    CHECK(validate_gentle_encounters() == 0);
    CHECK(validate_calm_reference() == 0);
    // The GPU consumes the same held engine angle and mirrored world coordinates as CPU.
    BoatSession player
        = basin_session((Boat) { .position = { 4, 9 }, .heading = .7f, .engine_angle = .2f });
    boat_session_set_tick(&player, 360);
    player.presentation.water[0]
        = (BoatWaterParticle) { .position = { 2, 8 }, .height = .1f, .lifetime = 2, .size = .4f };
    player.presentation.water[1] = (BoatWaterParticle) {
        .position = { 3, 8 }, .height = 1, .lifetime = 1, .size = .2f, .spray = true
    };
    WaterfrontSceneFrame frame;
    boat_scene_frame(
        SESSION_VIEW(&player), &player.presentation, &player.world, BOAT_CAMERA_DEFAULT, &frame);
    CHECK(frame.player_active && frame.time == 3);
    CHECK(frame.player.hull.position.x == -4 && frame.player.hull.position.z == 9);
    CHECK(fabsf(frame.player.engine.rotation.y - .5f) < .0001f);
    CHECK(frame.player.particle_count == 2);
    CHECK(!frame.player.particles[0].spray && frame.player.particles[1].spray);
    CHECK(frame.player.particles[0].transform.position.x == -2);
    CHECK(frame.player.particles[1].transform.position.y == 1);
    boat_session_init(&player, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat_scene_frame(
        SESSION_VIEW(&player), &player.presentation, &player.world, BOAT_CAMERA_DEFAULT, &frame);
    CHECK(frame.time == 0 && frame.player.particle_count == 0);
    waterfront_scene_evaluate(0, (BoatWater) { 0 }, &frame);
    CHECK(!frame.player_active);
    BoatSession mapped;
    boat_session_init(&mapped, test_world(BOAT_LOCATION_RIVER), (BoatWater) { 0 });
    CHECK(mapped.world.definition->mapped && test_river_water(mapped.boat.position, .3f));
    CHECK(!test_river_water((Vector2) { 10000, 0 }, 0));
    CHECK(!test_river_water((Vector2) { -100, 60 }, 0));
    // Drive into both dock quays. Hull clearance must remain in water and impacts rebound.
    for (int side = -1; side <= 1; side += 2) {
        boat_session_init(&mapped, test_world(BOAT_LOCATION_RIVER), (BoatWater) { 0 });
        mapped.boat.heading = mapped.presentation.camera_heading = side * 1.5707963f;
        mapped.boat.throttle = 1;
        bool bumped = false;
        for (int tick = 0; tick < 1200; ++tick) {
            boat_session_step(&mapped, (BoatInput) { 0 });
            CHECK(footprint_in_water(&mapped));
            if (mapped.boat.velocity.x * side < -0.01f)
                bumped = true;
        }
        CHECK(bumped);
    }
    // The turn can leave the boat moving forward when reverse thrust is selected. Measure
    // actual backward travel: braking and returning near the initial position is not a jam.
    // Small heading perturbations cover contact trajectories sensitive to float rounding.
    const float heading_offsets[] = { 0, -.00001f, .00001f };
    for (size_t i = 0; i < sizeof(heading_offsets) / sizeof(*heading_offsets); ++i)
        for (int side = -1; side <= 1; side += 2) {
            boat_session_init(&mapped, test_world(BOAT_LOCATION_RIVER), (BoatWater) { 0 });
            mapped.boat.heading = mapped.presentation.camera_heading
                = side * PI / 2 + heading_offsets[i];
            mapped.boat.throttle = 1;
            for (int tick = 0; tick < 900; ++tick) {
                boat_session_step(&mapped, (BoatInput) { tick > 500 ? side : 0, 0 });
                CHECK(footprint_in_water(&mapped));
            }
            float reverse_travel = 0;
            mapped.boat.throttle = -1;
            for (int tick = 0; tick < 360; ++tick) {
                Vector2 previous = mapped.boat.position;
                Vector2 forward = { sinf(mapped.boat.heading), -cosf(mapped.boat.heading) };
                boat_session_step(&mapped, (BoatInput) { 0 });
                CHECK(footprint_in_water(&mapped));
                reverse_travel += fmaxf(0,
                    -Vector2DotProduct(Vector2Subtract(mapped.boat.position, previous), forward));
            }
            if (reverse_travel <= 3)
                fprintf(stderr, "Mapped reverse: side=%d heading_offset=%g backward_travel=%g\n",
                    side, heading_offsets[i], reverse_travel);
            CHECK(reverse_travel > 3);
        }
    // The channel remains navigable through the crane towards the river.
    boat_session_init(&mapped, test_world(BOAT_LOCATION_RIVER), (BoatWater) { 0 });
    mapped.boat.throttle = 1;
    for (int tick = 0; tick < 1200; ++tick) {
        boat_session_step(&mapped, (BoatInput) { 0 });
        CHECK(test_river_water(mapped.boat.position, .3f));
    }
    CHECK(mapped.boat.position.y < -50);
    boat_session_init(&mapped, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    CHECK(!mapped.world.definition->mapped && mapped.boat.position.y == 10);
    CHECK(validate_course() == 0);
    CHECK(validate_coasting() == 0);
    CHECK(validate_countersteering() == 0);
    CHECK(validate_speed_view() == 0);
    CHECK(validate_wake_independence() == 0);
    CHECK(validate_signed_propulsion() == 0);
    CHECK(validate_signed_throttle() == 0);
    CHECK(validate_planing() == 0);
    CHECK(validate_planing_attitude() == 0);
    CHECK(validate_hull_contacts() == 0);
    BoatSession boat;
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    // Follow the short arc across the +/-pi boundary, without a full camera orbit.
    boat.boat.heading = -3.13f;
    boat.presentation.camera_heading = 3.13f;
    boat_session_step(&boat, (BoatInput) { 0 });
    float camera_turn = remainderf(boat.presentation.camera_heading - 3.13f, 6.2831853f);
    CHECK(camera_turn > 0 && camera_turn < 0.01f);
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    CHECK(boat.presentation.camera_heading == 0);
    // Wheel adjustments are bounded and reversible; release springs back symmetrically.
    for (int i = 0; i < 60; ++i)
        boat_session_step(&boat, (BoatInput) { 1, 0 });
    CHECK(fabsf(boat.boat.wheel - 0.5f) < 0.001f);
    BoatSession mirrored = boat;
    mirrored.boat.wheel = -boat.boat.wheel;
    mirrored.boat.engine_angle = -boat.boat.engine_angle;
    for (int i = 0; i < 360; ++i) {
        float previous = boat.boat.wheel;
        boat_session_step(&boat, (BoatInput) { 0 });
        boat_session_step(&mirrored, (BoatInput) { 0 });
        CHECK(boat.boat.wheel >= 0 && boat.boat.wheel <= previous);
        CHECK(fabsf(boat.boat.wheel + mirrored.boat.wheel) < 0.000001f);
        CHECK(fabsf(boat.boat.engine_angle + mirrored.boat.engine_angle) < 0.000001f);
        if (i == 119)
            CHECK(boat.boat.wheel > 0.01f && boat.boat.wheel < 0.05f);
    }
    CHECK(boat.boat.wheel == 0 && boat.boat.engine_angle == 0);
    CHECK(mirrored.boat.wheel == 0 && mirrored.boat.engine_angle == 0);
    boat_session_step(&boat, (BoatInput) { 0 });
    CHECK(boat.boat.wheel == 0 && boat.boat.engine_angle == 0);
    for (int i = 0; i < 600; ++i)
        boat_session_step(&boat, (BoatInput) { 1, 0 });
    CHECK(boat.boat.wheel == 1);
    for (int i = 0; i < 600; ++i)
        boat_session_step(&boat, (BoatInput) { -1, 0 });
    CHECK(boat.boat.wheel == -1);
    for (int i = 0; i < 40; ++i)
        boat_session_step(&boat, (BoatInput) { 1, 0 });
    CHECK(fabsf(boat.boat.wheel) < 0.001f);
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    CHECK(boat.boat.wheel == 0 && boat.boat.engine_angle == 0);
    // Turning the engine at idle cannot rotate a stationary boat.
    for (int i = 0; i < 120; ++i)
        boat_session_step(&boat, (BoatInput) { 1, 0 });
    CHECK(boat.boat.heading == 0 && boat.boat.yaw_velocity == 0);
    CHECK(boat.boat.position.x == 0 && boat.boat.position.y == 10);
    CHECK(boat.boat.engine_angle > 0 && boat.boat.engine_angle < 0.6f);
    CHECK(particles(&boat.presentation, false) == 0);
    // The first powered impulse pushes the stern left while yawing the bow right.
    boat.boat.throttle = 1;
    boat_session_step(&boat, (BoatInput) { 1, 0 });
    CHECK(boat.boat.velocity.x < 0 || boat.boat.velocity.y < 0);
    CHECK(boat.boat.heading > 0 && boat.boat.yaw_velocity > 0);
    for (int i = 0; i < 10; ++i)
        boat_session_step(&boat, (BoatInput) { 1, 0 });
    CHECK(particles(&boat.presentation, false) > 0);
    CHECK(boat.presentation.water[0].velocity.x > 0 && boat.presentation.water[0].velocity.y > 0);
    // Cutting the command lets the small engine-response state spool down first.
    float response = boat.boat.engine_response;
    boat.boat.throttle = 0;
    boat_session_step(&boat, (BoatInput) { 0 });
    CHECK(boat.boat.engine_response > 0 && boat.boat.engine_response < response);
    while (boat.boat.engine_response > 0)
        boat_session_step(&boat, (BoatInput) { 0 });
    // Once neutral, angular momentum and passive steering remain while energy decays.
    float energy = kinetic_energy(&boat.boat);
    boat_session_step(&boat, (BoatInput) { 0 });
    CHECK(boat.boat.yaw_velocity > 0 && kinetic_energy(&boat.boat) < energy);
    for (int i = 0; i < 2400; ++i)
        boat_session_step(&boat, (BoatInput) { 0 });
    CHECK(boat.boat.wheel == 0 && boat.boat.engine_angle == 0
        && fabsf(boat.boat.yaw_velocity) < 0.001f);
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    for (int i = 0; i < 120; ++i)
        boat_session_step(&boat, (BoatInput) { 0, 1 });
    CHECK(fabsf(boat.boat.throttle - 0.5f) < 0.001f);
    float held = boat.boat.throttle;
    for (int i = 0; i < 60; ++i)
        boat_session_step(&boat, (BoatInput) { 0 });
    CHECK(boat.boat.throttle == held);
    for (int i = 0; i < 600; ++i)
        boat_session_step(&boat, (BoatInput) { 0, 1 });
    CHECK(boat.boat.throttle == 1);
    for (int i = 0; i < 240; ++i)
        boat_session_step(&boat, (BoatInput) { 0, -1 });
    CHECK(fabsf(boat.boat.throttle) < .001f);
    boat.boat.throttle = 0.75f;
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    CHECK(boat.boat.throttle == 0);
    boat.boat.throttle = 1;
    for (int i = 0; i < 120; ++i)
        boat_session_step(&boat, (BoatInput) { 0 });
    CHECK(boat.boat.position.y < 10 && boat.boat.velocity.y < -5);
    CHECK(fabsf(boat.boat.position.x) < 0.001f);
    CHECK(particles(&boat.presentation, false) > 0);
    CHECK(particles(&boat.presentation, true) > 0); // Forward planing now throws symmetric spray.
    boat.boat.throttle = 0;
    while (boat.boat.engine_response > 0)
        boat_session_step(&boat, (BoatInput) { 0 });
    float speed = hypotf(boat.boat.velocity.x, boat.boat.velocity.y);
    for (int i = 0; i < 120; ++i)
        boat_session_step(&boat, (BoatInput) { 0 });
    // A boat still on plane coasts farther than the displacement-only model, but slows.
    CHECK(hypotf(boat.boat.velocity.x, boat.boat.velocity.y) < speed * 0.85f);
    CHECK(hypotf(boat.boat.velocity.x, boat.boat.velocity.y) > speed * 0.4f);

    BoatSession left, right;
    boat_session_init(&left, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat_session_init(&right, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    left.boat.throttle = right.boat.throttle = 1;
    left.boat.wheel = -0.5f;
    right.boat.wheel = 0.5f;
    for (int i = 0; i < 180; ++i) {
        boat_session_step(&left, (BoatInput) { -1, 0 });
        boat_session_step(&right, (BoatInput) { 1, 0 });
    }
    CHECK(right.boat.position.x > 1 && left.boat.position.x < -1);
    CHECK(fabsf(left.boat.position.x + right.boat.position.x) < 0.001f);
    CHECK(fabsf(left.boat.position.y - right.boat.position.y) < 0.001f);
    CHECK(particles(&right.presentation, true) > 0);
    CHECK(particles(&left.presentation, true) == particles(&right.presentation, true));
    // Stopping leaves existing water effects to fade; steering at idle produces no new wash.
    right.boat.velocity = (Vector2) { 0 };
    right.boat.throttle = 0;
    for (int i = 0; i < 600; ++i)
        boat_session_step(&right, (BoatInput) { 1, 0 });
    CHECK(particles(&right.presentation, false) == 0 && particles(&right.presentation, true) == 0);
    // A powered turn retains sideways momentum.
    CHECK(fabsf(left.boat.velocity.x * cosf(left.boat.heading)
              + left.boat.velocity.y * sinf(left.boat.heading))
        > 0.1f);

    CHECK(validate_basin_stress(-.00001f) == 0);
    CHECK(validate_basin_stress(0) == 0);
    CHECK(validate_basin_stress(.00001f) == 0);
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    CHECK(boat.boat.position.x == 0 && boat.boat.position.y == 10);
    CHECK(boat.boat.heading == 0 && boat.boat.velocity.x == 0 && boat.boat.velocity.y == 0);
    CHECK(boat.boat.wheel == 0 && boat.boat.engine_angle == 0 && boat.boat.yaw_velocity == 0);
    CHECK(boat.boat.throttle == 0 && boat.boat.engine_response == 0);
    CHECK(boat.boat.planing == 0);
    CHECK(particles(&boat.presentation, false) == 0 && particles(&boat.presentation, true) == 0);
    puts("boat_handling=ok");
    test_worlds_close();
    return 0;
}
