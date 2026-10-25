#include "boat_banking_fixture.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "Turn entry line %d: %s\n", __LINE__, #c);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static float energy(const Boat *b)
{
    return .5f * Vector2LengthSqr(b->velocity) + 1.1f * b->yaw_velocity * b->yaw_velocity;
}

static int validate(FILE *report)
{
    BoatSession a, b;
    boat_turn_entry_reset(&a);
    b = a;
    int engine_at = -1, heading_at = -1, bank_at = -1;
    float peak_slip = 0;
    for (int tick = 0; tick < BOAT_TURN_ENTRY_TICKS; ++tick) {
        float previous_roll = a.boat.roll;
        boat_turn_entry_step(&a, tick, 1);
        boat_turn_entry_step(&b, tick, -1);
        CHECK(fabsf(a.boat.position.x) + 3 < BOAT_BASIN_HALF_X
            && fabsf(a.boat.position.y) + 3 < BOAT_BASIN_HALF_Z);
        CHECK(fabsf(a.boat.position.x + b.boat.position.x) < .00003f
            && fabsf(a.boat.position.y - b.boat.position.y) < .00003f);
        CHECK(fabsf(a.boat.heading + b.boat.heading) < .00001f
            && fabsf(a.boat.roll + b.boat.roll) < .00002f);
        CHECK(fabsf(a.boat.roll - previous_roll - a.boat.roll_velocity * BOAT_STEP) < .000001f);
        CHECK(fabsf(a.boat.engine_angle + b.boat.engine_angle) < .000001f);
        Vector2 forward = { sinf(a.boat.heading), -cosf(a.boat.heading) },
                right = { cosf(a.boat.heading), sinf(a.boat.heading) };
        float u = Vector2DotProduct(a.boat.velocity, forward),
              v = Vector2DotProduct(a.boat.velocity, right);
        float slip = atan2f(v, u), bow = v + 1.2f * a.boat.yaw_velocity,
              stern = v - 1.6f * a.boat.yaw_velocity;
        peak_slip = fmaxf(peak_slip, fabsf(slip));
        if (engine_at < 0 && a.boat.engine_angle > DEG2RAD)
            engine_at = tick;
        if (heading_at < 0 && a.boat.heading > DEG2RAD)
            heading_at = tick;
        if (bank_at < 0 && a.boat.roll > DEG2RAD)
            bank_at = tick;
        if (report)
            fprintf(report, "%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n", tick,
                (tick + 1) * (double)BOAT_STEP, a.boat.engine_angle, a.boat.heading,
                a.boat.yaw_velocity, slip, a.boat.roll, a.boat.turn_roll_load,
                Vector2Length(a.boat.velocity), bow, stern, energy(&a.boat));
        if (tick == 0) {
            // The aligned stations have no lateral load on this first tick. Only the
            // already-turned outboard's thrust and passive skeg can start yaw/side motion.
            float angle = a.boat.engine_angle;
            float side = -11 * sinf(angle) - .10f * 12 * (12 * sinf(angle)) * cosf(angle);
            CHECK(fabsf(a.boat.velocity.x - side * BOAT_STEP) < .000001f);
            CHECK(fabsf(a.boat.yaw_velocity - (-1.6f * side / 2.2f) * BOAT_STEP) < .000001f);
            CHECK(a.boat.engine_angle > 0 && a.boat.velocity.x < 0 && a.boat.yaw_velocity > 0);
            CHECK(stern < bow && fabsf(a.boat.roll) < .001f * DEG2RAD);
        }
        if (tick == 119) {
            CHECK(a.boat.engine_angle > 30 * DEG2RAD && a.boat.heading > 20 * DEG2RAD
                && a.boat.heading < 35 * DEG2RAD);
            CHECK(fabsf(slip) < 9.5f * DEG2RAD && bow < 0 && stern < 0);
            CHECK(fabsf(bow) < .35f * fabsf(stern));
            CHECK(a.boat.roll > 8 * DEG2RAD && a.boat.roll < 12 * DEG2RAD);
            printf("entry_1s engine_deg=%.6f heading_deg=%.6f slip_deg=%.6f bank_deg=%.6f "
                   "bow_cross=%.6f stern_cross=%.6f speed=%.6f\n",
                a.boat.engine_angle * RAD2DEG, a.boat.heading * RAD2DEG, slip * RAD2DEG,
                a.boat.roll * RAD2DEG, bow, stern, Vector2Length(a.boat.velocity));
        }
    }
    CHECK(engine_at >= 0 && engine_at < heading_at && heading_at < bank_at);
    CHECK(peak_slip < 10 * DEG2RAD);
    CHECK(fabsf(a.boat.roll) < .1f * DEG2RAD && fabsf(a.boat.yaw_velocity) < .1f * DEG2RAD);
    printf("onset_1deg engine_seconds=%.6f heading_seconds=%.6f inward_bank_seconds=%.6f "
           "peak_slip_deg=%.6f\n",
        (engine_at + 1) * BOAT_STEP, (heading_at + 1) * BOAT_STEP, (bank_at + 1) * BOAT_STEP,
        peak_slip * RAD2DEG);
    // Whole-hull lateral and yaw motion must lose energy with no propulsion. Include
    // partial/full plane and both signs of side flow; resistance must remain passive.
    for (int trial = 0; trial < 3; ++trial)
        for (int sign = -1; sign <= 1; sign += 2) {
            boat_session_init(&a, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
            a.boat.position = (Vector2) { 0 };
            a.boat.velocity = (Vector2) { sign * 3, -12 };
            a.boat.yaw_velocity = sign * .7f;
            a.boat.planing = trial * .5f;
            a.boat.wheel = sign * .5f;
            a.boat.engine_angle = sign * .275f;
            for (int tick = 0; tick < 240; ++tick) {
                float previous = energy(&a.boat);
                boat_session_step(&a, (BoatInput) { 0 });
                CHECK(energy(&a.boat) <= previous + .00001f);
            }
        }
    return 0;
}

int main(int argc, char **argv)
{
    test_worlds_open();
    FILE *report = NULL;
    if (argc == 3 && !strcmp(argv[1], "--report")) {
        report = fopen(argv[2], "w");
        if (!report) {
            perror(argv[2]);
            return 1;
        }
        fputs("tick,time,engine_angle,heading,yaw_rate,slip,roll,turn_roll_load,speed,bow_cross,"
              "stern_cross,energy\n",
            report);
    } else if (argc != 1)
        return 2;
    int result = validate(report);
    if (report) {
        bool failed = ferror(report) != 0;
        if (fclose(report) != 0)
            failed = true;
        if (failed)
            result = 1;
    }
    test_worlds_close();
    return result;
}
