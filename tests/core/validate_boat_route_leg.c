// Marina M3 motion groundwork: a straight open leg evaluated directly at explicit ticks.
#include "core/boat_route_leg.h"

#include <math.h>
#include <stdio.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static bool near(double a, double b, double tolerance) { return fabs(a - b) <= tolerance; }

static bool same_pose(const BoatRoutePose *a, const BoatRoutePose *b)
{
    return a->x == b->x && a->z == b->z && a->heading == b->heading && a->speed == b->speed;
}

// 50 m north-east-ish leg: 30 m across, 40 m up-river, one second of simulation.
static BoatRouteLeg sample_leg(void)
{
    BoatRouteLeg leg = {
        .start_x = 0.0,
        .start_z = 0.0,
        .end_x = 30.0,
        .end_z = 40.0,
        .duration_ticks = 60,
        .tick_seconds = 1.0 / 60.0,
        .max_speed = 75.0,
        .max_acceleration = 300.0,
    };
    return leg;
}

static BoatRoutePose sentinel_pose(void)
{
    BoatRoutePose pose = { 123.0, -456.0, 7.0, 89.0 };
    return pose;
}

static bool pose_is_sentinel(const BoatRoutePose *pose)
{
    return pose->x == 123.0 && pose->z == -456.0 && pose->heading == 7.0 && pose->speed == 89.0;
}

static int endpoints_and_midpoint(void)
{
    BoatRouteLeg leg = sample_leg();
    BoatRoutePose pose;
    CHECK(boat_route_leg_validate(&leg));

    double heading = atan2(30.0, -40.0);
    CHECK(boat_route_leg_evaluate(&leg, 0, &pose));
    CHECK(pose.x == 0.0 && pose.z == 0.0 && pose.speed == 0.0);
    CHECK(near(pose.heading, heading, 1e-12));

    // The smoothstep reaches halfway at the midpoint; peak speed is 1.5 * distance / duration.
    CHECK(boat_route_leg_evaluate(&leg, 30, &pose));
    CHECK(near(pose.x, 15.0, 1e-12) && near(pose.z, 20.0, 1e-12));
    CHECK(near(pose.speed, 75.0, 1e-12));
    CHECK(near(pose.heading, heading, 1e-12));

    // A quarter tick: blend 0.15625, speed factor 1.125, still one fixed heading.
    CHECK(boat_route_leg_evaluate(&leg, 15, &pose));
    CHECK(near(pose.x, 4.6875, 1e-12) && near(pose.z, 6.25, 1e-12));
    CHECK(near(pose.speed, 56.25, 1e-12));
    CHECK(near(pose.heading, heading, 1e-12));

    CHECK(boat_route_leg_evaluate(&leg, 60, &pose));
    CHECK(near(pose.x, 30.0, 1e-12) && near(pose.z, 40.0, 1e-12));
    CHECK(pose.speed == 0.0);
    CHECK(near(pose.heading, heading, 1e-12));
    return 0;
}

static int explicit_directions(void)
{
    // North: fixed heading zero. South: heading pi.
    BoatRouteLeg north = {
        .start_x = 0.0,
        .start_z = 0.0,
        .end_x = 0.0,
        .end_z = -10.0,
        .duration_ticks = 20,
        .tick_seconds = 0.5,
        .max_speed = 2.0,
        .max_acceleration = 1.0,
    };
    BoatRoutePose pose;
    CHECK(boat_route_leg_validate(&north));
    CHECK(boat_route_leg_evaluate(&north, 10, &pose));
    CHECK(near(pose.x, 0.0, 1e-12) && near(pose.z, -5.0, 1e-12));
    CHECK(near(pose.heading, 0.0, 1e-12));

    BoatRouteLeg south = north;
    south.end_z = 10.0;
    CHECK(boat_route_leg_evaluate(&south, 10, &pose));
    CHECK(near(pose.heading, 3.14159265358979323846, 1e-12));

    // West: atan2(dx, -dz) with dx negative and dz zero gives -pi/2.
    BoatRouteLeg west = {
        .start_x = 5.0,
        .start_z = 5.0,
        .end_x = -5.0,
        .end_z = 5.0,
        .duration_ticks = 20,
        .tick_seconds = 0.5,
        .max_speed = 2.0,
        .max_acceleration = 1.0,
    };
    CHECK(boat_route_leg_evaluate(&west, 10, &pose));
    CHECK(near(pose.x, 0.0, 1e-12) && near(pose.z, 5.0, 1e-12));
    CHECK(near(pose.heading, -1.57079632679489661923, 1e-12));
    return 0;
}

static int direct_reverse_and_repeated_seeks(void)
{
    BoatRouteLeg leg = sample_leg();
    // Backwards, repeated and endpoint seeks must equal a fresh seek at the same tick.
    const uint64_t seeks[] = { 60, 0, 45, 45, 30, 15, 30, 0, 60 };
    BoatRoutePose pose;
    for (size_t i = 0; i < sizeof(seeks) / sizeof(seeks[0]); ++i) {
        BoatRoutePose expected;
        CHECK(boat_route_leg_evaluate(&leg, seeks[i], &expected));
        CHECK(boat_route_leg_evaluate(&leg, seeks[i], &pose));
        CHECK(same_pose(&pose, &expected));
    }

    // Repeating an evaluation must not drift.
    BoatRoutePose first;
    CHECK(boat_route_leg_evaluate(&leg, 37, &first));
    for (int i = 0; i < 5; ++i) {
        CHECK(boat_route_leg_evaluate(&leg, 37, &pose));
        CHECK(same_pose(&pose, &first));
    }
    return 0;
}

static int arbitrary_frame_stepping(void)
{
    BoatRouteLeg leg = sample_leg();
    const uint64_t steps[] = { 1, 2, 5, 3, 11, 7, 13, 5, 13, 20, 200 };
    uint64_t tick = 0;
    BoatRoutePose stepped;
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
        tick += steps[i];
        BoatRoutePose direct;
        CHECK(boat_route_leg_evaluate(&leg, tick, &direct));
        CHECK(boat_route_leg_evaluate(&leg, tick, &stepped));
        CHECK(same_pose(&stepped, &direct));
    }
    CHECK(tick > leg.duration_ticks);
    return 0;
}

static int peak_limits(void)
{
    // distance 10 m over two seconds: peak speed 7.5 m/s, peak acceleration 15 m/s^2.
    BoatRouteLeg leg = {
        .start_x = 0.0,
        .start_z = 0.0,
        .end_x = 10.0,
        .end_z = 0.0,
        .duration_ticks = 120,
        .tick_seconds = 1.0 / 60.0,
        .max_speed = 7.5,
        .max_acceleration = 15.0,
    };
    CHECK(boat_route_leg_validate(&leg));

    BoatRouteLeg limited = leg;
    limited.max_speed = 7.499999;
    CHECK(!boat_route_leg_validate(&limited));

    limited = leg;
    limited.max_acceleration = 14.999999;
    CHECK(!boat_route_leg_validate(&limited));

    // Each limit is independent: a generous acceleration does not excuse excess speed, and a
    // generous speed does not excuse excess acceleration.
    limited = leg;
    limited.max_speed = 7.499999;
    limited.max_acceleration = 1000.0;
    CHECK(!boat_route_leg_validate(&limited));

    limited = leg;
    limited.max_speed = 1000.0;
    limited.max_acceleration = 14.999999;
    CHECK(!boat_route_leg_validate(&limited));
    return 0;
}

static int invalid_inputs(void)
{
    BoatRouteLeg leg = sample_leg();
    BoatRoutePose pose = sentinel_pose();

    CHECK(!boat_route_leg_validate(NULL));
    CHECK(!boat_route_leg_evaluate(NULL, 0, &pose) && pose_is_sentinel(&pose));
    CHECK(!boat_route_leg_evaluate(&leg, 0, NULL));

    BoatRouteLeg bad = leg;
    bad.start_x = NAN;
    CHECK(!boat_route_leg_validate(&bad));
    bad = leg;
    bad.start_z = INFINITY;
    CHECK(!boat_route_leg_validate(&bad));
    bad = leg;
    bad.end_x = -INFINITY;
    CHECK(!boat_route_leg_validate(&bad));

    // The coordinate bound is inclusive at 20000 and exclusive beyond it.
    BoatRouteLeg boundary = {
        .start_x = 20000.0,
        .start_z = 20000.0,
        .end_x = 20000.0,
        .end_z = 19999.0,
        .duration_ticks = 10,
        .tick_seconds = 0.5,
        .max_speed = 20000.0,
        .max_acceleration = 10000.0,
    };
    CHECK(boat_route_leg_validate(&boundary));
    boundary.end_z = -20000.0;
    CHECK(boat_route_leg_validate(&boundary));
    boundary.end_z = 20000.5;
    CHECK(!boat_route_leg_validate(&boundary));
    boundary = leg;
    boundary.start_x = -20000.5;
    CHECK(!boat_route_leg_validate(&boundary));

    bad = leg;
    bad.end_x = bad.start_x;
    bad.end_z = bad.start_z;
    CHECK(!boat_route_leg_validate(&bad));

    bad = leg;
    bad.duration_ticks = 0;
    CHECK(!boat_route_leg_validate(&bad));

    bad = leg;
    bad.tick_seconds = 0.0;
    CHECK(!boat_route_leg_validate(&bad));
    bad = leg;
    bad.tick_seconds = -0.5;
    CHECK(!boat_route_leg_validate(&bad));
    bad = leg;
    bad.tick_seconds = NAN;
    CHECK(!boat_route_leg_validate(&bad));
    bad = leg;
    bad.tick_seconds = INFINITY;
    CHECK(!boat_route_leg_validate(&bad));

    bad = leg;
    bad.max_speed = 0.0;
    CHECK(!boat_route_leg_validate(&bad));
    bad = leg;
    bad.max_speed = -1.0;
    CHECK(!boat_route_leg_validate(&bad));
    bad = leg;
    bad.max_speed = NAN;
    CHECK(!boat_route_leg_validate(&bad));
    bad = leg;
    bad.max_speed = INFINITY;
    CHECK(!boat_route_leg_validate(&bad));

    bad = leg;
    bad.max_acceleration = 0.0;
    CHECK(!boat_route_leg_validate(&bad));
    bad = leg;
    bad.max_acceleration = -1.0;
    CHECK(!boat_route_leg_validate(&bad));
    bad = leg;
    bad.max_acceleration = NAN;
    CHECK(!boat_route_leg_validate(&bad));
    bad = leg;
    bad.max_acceleration = INFINITY;
    CHECK(!boat_route_leg_validate(&bad));

    // Nonfinite intermediates: an overflowing duration and an acceleration denominator that
    // underflows are both rejected.
    bad = leg;
    bad.tick_seconds = 1e300;
    CHECK(!boat_route_leg_validate(&bad));
    bad = leg;
    bad.tick_seconds = 1e-300;
    CHECK(!boat_route_leg_validate(&bad));

    // Failed evaluations leave the caller's pose untouched.
    bad = leg;
    bad.duration_ticks = 0;
    CHECK(!boat_route_leg_evaluate(&bad, 0, &pose));
    CHECK(pose_is_sentinel(&pose));
    bad = leg;
    bad.end_x = 20001.0;
    CHECK(!boat_route_leg_evaluate(&bad, 30, &pose));
    CHECK(pose_is_sentinel(&pose));
    return 0;
}

static int huge_tick_clamps(void)
{
    BoatRouteLeg leg = sample_leg();
    BoatRoutePose pose;
    CHECK(boat_route_leg_evaluate(&leg, UINT64_MAX, &pose));
    CHECK(near(pose.x, leg.end_x, 1e-12) && near(pose.z, leg.end_z, 1e-12));
    CHECK(pose.speed == 0.0);
    CHECK(near(pose.heading, atan2(30.0, -40.0), 1e-12));

    // One tick past the duration is the same clamped endpoint.
    BoatRoutePose past;
    CHECK(boat_route_leg_evaluate(&leg, leg.duration_ticks + 1, &past));
    CHECK(same_pose(&pose, &past));
    return 0;
}

int main(void)
{
    if (endpoints_and_midpoint() || explicit_directions() || direct_reverse_and_repeated_seeks()
        || arbitrary_frame_stepping() || peak_limits() || invalid_inputs() || huge_tick_clamps())
        return 1;
    puts("boat_route_leg: endpoints, directions, seeks, frame steps, peaks, clamps and invalid "
         "input");
    return 0;
}
