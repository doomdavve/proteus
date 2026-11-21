/* R0 source-off propagation and coupled encounter baselines, using production queries/physics. */
#include "raylib.h"
#include "raymath.h"
#include "support/boat_test_session.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "encounter baseline failed: %s\n", #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static BoatWakeObjects seed(unsigned age)
{
    BoatWakeObjects w;
    CHECK(boat_wake_objects_init(&w, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT));
    CHECK(boat_wake_objects_track(&w, 0, (Vector2) { -3.75f, 0 }, 15, 1));
    for (unsigned t = 1; t <= 60; ++t) {
        boat_wake_objects_advance(&w);
        CHECK(boat_wake_objects_track(&w, 0, (Vector2) { -3.75f + t / 8.0f, 0 }, 15, 1));
    }
    for (unsigned t = 0; t < age; ++t)
        boat_wake_objects_advance(&w);
    CHECK(w.live == 1);
    return w;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: wake_encounters OUTPUT_PREFIX\n");
        return 2;
    }
    char name[4096];
    CHECK(snprintf(name, sizeof(name), "%s-field.csv", argv[1]) < (int)sizeof(name));
    FILE *out = fopen(name, "w");
    CHECK(out);
    fputs("age_ticks,x,z,single_height,crossed_height,cancelled_height,gradient_x,gradient_z,"
          "vertical_velocity,gradient_error,time_error\n",
        out);
    BoatWakeObjects w = seed(0);
    float worst_sum = 0, worst_gradient = 0, worst_time = 0;
    for (unsigned age = 1; age < 2040; ++age) {
        boat_wake_objects_advance(&w);
        BoatWakeObjects crossed = w, cancelled = w, perpendicular = w;
        perpendicular.objects[0].direction = (Vector2) { 0, 1 };
        crossed.objects[BOAT_OBJECT_PER_BOAT] = perpendicular.objects[0];
        cancelled.objects[BOAT_OBJECT_PER_BOAT] = w.objects[0];
        cancelled.objects[BOAT_OBJECT_PER_BOAT].amplitude *= -1;
        for (unsigned point = 0; point < 3; ++point) {
            Vector2 p = { .25f + point * .7f, 4 + point * 6 };
            BoatWakeObjectSample a = boat_wake_objects_query(&w, p);
            BoatWakeObjectSample b = boat_wake_objects_query(&perpendicular, p);
            BoatWakeObjectSample c = boat_wake_objects_query(&crossed, p);
            BoatWakeObjectSample d = boat_wake_objects_query(&cancelled, p);
            float residual = fmaxf(fabsf(c.height - a.height - b.height), fabsf(d.height));
            residual = fmaxf(residual, fabsf(c.gradient.x - a.gradient.x - b.gradient.x));
            residual = fmaxf(residual, fabsf(c.gradient.y - a.gradient.y - b.gradient.y));
            residual = fmaxf(
                residual, fabsf(c.vertical_velocity - a.vertical_velocity - b.vertical_velocity));
            residual = fmaxf(residual, fabsf(d.vertical_velocity));
            residual = fmaxf(residual, fmaxf(fabsf(d.gradient.x), fabsf(d.gradient.y)));
            worst_sum = fmaxf(worst_sum, residual);
            float gradient_error = 0;
            for (unsigned axis = 0; axis < 2; ++axis) {
                Vector2 low = p, high = p;
                if (axis) {
                    low.y -= .002f;
                    high.y += .002f;
                } else {
                    low.x -= .002f;
                    high.x += .002f;
                }
                float fd = (boat_wake_objects_query(&w, high).height
                               - boat_wake_objects_query(&w, low).height)
                    / .004f;
                gradient_error
                    = fmaxf(gradient_error, fabsf(fd - (axis ? a.gradient.y : a.gradient.x)));
            }
            BoatWakeObjects low = w, high = w;
            --low.tick;
            ++high.tick;
            float fd = (boat_wake_objects_query(&high, p).height
                           - boat_wake_objects_query(&low, p).height)
                * 60;
            float time_error = fabsf(fd - a.vertical_velocity);
            worst_gradient = fmaxf(worst_gradient, gradient_error);
            worst_time = fmaxf(worst_time, time_error);
            fprintf(out, "%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n", age, (double)p.x,
                (double)p.y, (double)a.height, (double)c.height, (double)d.height,
                (double)a.gradient.x, (double)a.gradient.y, (double)a.vertical_velocity,
                (double)gradient_error, (double)time_error);
        }
    }
    CHECK(fclose(out) == 0);
    CHECK(worst_sum < 1e-5f && worst_gradient < .003f && worst_time < .02f);
    CHECK(w.live == 0);
    CHECK(snprintf(name, sizeof(name), "%s-hulls.csv", argv[1]) < (int)sizeof(name));
    out = fopen(name, "w");
    CHECK(out);
    fputs("angle_degrees,speed,initial_age_seconds,peak_heave_above_calm,peak_roll,peak_pitch,peak_"
          "vertical_acceleration,peak_launch_velocity,landing_delta_v_proxy,landings,capsized_ever,"
          "final_state,first_wet_stern_probe_speed,first_wet_stern_probe_age\n",
        out);
    const unsigned ages[] = { 120, 720, 1440 };
    const float speeds[] = { 8, 16, 24 }, angles[] = { 0, 30, 60 };
    for (unsigned ai = 0; ai < 3; ++ai)
        for (unsigned si = 0; si < 3; ++si)
            for (unsigned ti = 0; ti < 3; ++ti) {
                BoatWakeObjects wave = seed(ages[ti]), flat;
                CHECK(boat_wake_objects_init(
                    &flat, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT));
                BoatSession boat;
                physics_session(&boat, BOAT_LOCATION_BASIN);
                float angle = angles[ai] * DEG2RAD, radius = .9f + 2.4f * ages[ti] / 120;
                boat.boat.position = (Vector2) { -12 * sinf(angle), radius + 12 * cosf(angle) };
                boat.boat.heading = angle;
                boat.boat.velocity
                    = (Vector2) { speeds[si] * sinf(angle), -speeds[si] * cosf(angle) };
                boat.boat.throttle = boat.boat.engine_response = .7f;
                boat.boat.planing = 1;
                boat.world.water.objects = &wave;
                boat_session_set_tick(&boat, wave.tick);
                BoatSession calm = boat;
                calm.world.water.objects = &flat;
                float heave = 0, roll = 0, pitch = 0, acceleration = 0, launch = 0, landing = 0;
                bool capsized = false, encountered = false;
                float encounter_speed = 0, encounter_age = 0;
                for (unsigned t = 0; t < 480; ++t) {
                    float before = boat.boat.vertical_velocity;
                    unsigned landed = boat.boat.landings;
                    boat_wake_objects_advance(&wave);
                    boat_wake_objects_advance(&flat);
                    boat_session_step(&boat, (BoatInput) { 0 });
                    boat_session_step(&calm, (BoatInput) { 0 });
                    BoatWakeStern stern
                        = boat_wake_stern(&boat.boat, &boat.world, boat_session_time(&boat));
                    BoatWakeObjectSample surface = boat_wake_objects_query(
                        &wave, (Vector2) { (float)stern.x, (float)stern.z });
                    if (!encountered && stern.wet > .05 && fabsf(surface.height) > .05f) {
                        encountered = true;
                        encounter_speed = (float)hypot(stern.vx, stern.vz);
                        encounter_age = (wave.tick - 60) / 120.0f;
                    }
                    float dv = fabsf(boat.boat.vertical_velocity - before);
                    CHECK(isfinite(boat.boat.height) && isfinite(dv));
                    heave = fmaxf(heave, boat.boat.height - calm.boat.height);
                    roll = fmaxf(roll, fabsf(boat.boat.roll));
                    pitch = fmaxf(pitch, fabsf(boat.boat.pitch));
                    acceleration = fmaxf(acceleration, dv * 120);
                    launch = fmaxf(launch, boat.boat.vertical_velocity);
                    if (boat.boat.landings > landed)
                        landing = fmaxf(landing, dv);
                    capsized |= boat.boat.motion_state == BOAT_CAPSIZED;
                }
                fprintf(out, "%.9g,%.9g,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u,%d,%d,",
                    (double)angles[ai], (double)speeds[si], ages[ti] / 120, (double)heave,
                    (double)roll, (double)pitch, (double)acceleration, (double)launch,
                    (double)landing, boat.boat.landings, capsized, boat.boat.motion_state);
                if (encountered)
                    fprintf(out, "%.9g,%.9g", (double)encounter_speed, (double)encounter_age);
                else
                    fputc(',', out);
                fputc('\n', out);
            }
    CHECK(fclose(out) == 0);
    test_worlds_close();
    printf("encounters=27 signed_residual=%.9g gradient_error=%.9g time_error=%.9g\n",
        (double)worst_sum, (double)worst_gradient, (double)worst_time);
    return 0;
}
