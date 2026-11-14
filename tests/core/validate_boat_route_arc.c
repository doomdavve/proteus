// Marina M3 motion groundwork: a circular turn evaluated directly at explicit ticks.
#include "core/boat_route_arc.h"
#include "core/boat_route_leg.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define PI 3.14159265358979323846

static bool near(double a, double b, double tolerance) { return fabs(a - b) <= tolerance; }

static bool same_pose(const BoatRoutePose *a, const BoatRoutePose *b)
{
    return a->x == b->x && a->z == b->z && a->heading == b->heading && a->speed == b->speed;
}

// A ten-metre radius quarter turn lasting one second. Limits leave headroom for the sample arc.
static BoatRouteArc sample_arc(void)
{
    BoatRouteArc arc = {
        .centre_x = 0.0,
        .centre_z = 0.0,
        .radius = 10.0,
        .start_angle = PI / 2.0,
        .sweep = PI / 2.0,
        .duration_ticks = 10,
        .tick_seconds = 0.1,
        .max_speed = 30.0,
        .max_acceleration = 200.0,
    };
    return arc;
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

static int quarter_turn_endpoints_and_midpoint(void)
{
    BoatRouteArc ccw = sample_arc();
    BoatRoutePose pose;
    CHECK(boat_route_arc_validate(&ccw));

    // Counterclockwise from the north point (0, 10) to the west point (-10, 0). The tangent
    // heading turns from -pi/2 through -pi/4 to zero.
    CHECK(boat_route_arc_evaluate(&ccw, 0, &pose));
    CHECK(near(pose.x, 0.0, 1e-12) && near(pose.z, 10.0, 1e-12));
    CHECK(pose.speed == 0.0);
    CHECK(near(pose.heading, -PI / 2.0, 1e-12));

    CHECK(boat_route_arc_evaluate(&ccw, 5, &pose));
    double diagonal = 10.0 / sqrt(2.0);
    CHECK(near(pose.x, -diagonal, 1e-12) && near(pose.z, diagonal, 1e-12));
    CHECK(near(pose.heading, -PI / 4.0, 1e-12));
    CHECK(near(pose.speed, 7.5 * PI, 1e-12));

    CHECK(boat_route_arc_evaluate(&ccw, 10, &pose));
    CHECK(near(pose.x, -10.0, 1e-12) && near(pose.z, 0.0, 1e-12));
    CHECK(pose.speed == 0.0);
    CHECK(near(pose.heading, 0.0, 1e-12));

    // Clockwise from the same north point to the east point (10, 0). The heading turns from
    // +pi/2 through +pi/4 to zero, mirroring the counterclockwise case.
    BoatRouteArc cw = ccw;
    cw.sweep = -PI / 2.0;
    CHECK(boat_route_arc_validate(&cw));

    CHECK(boat_route_arc_evaluate(&cw, 0, &pose));
    CHECK(near(pose.x, 0.0, 1e-12) && near(pose.z, 10.0, 1e-12));
    CHECK(pose.speed == 0.0);
    CHECK(near(pose.heading, PI / 2.0, 1e-12));

    CHECK(boat_route_arc_evaluate(&cw, 5, &pose));
    CHECK(near(pose.x, diagonal, 1e-12) && near(pose.z, diagonal, 1e-12));
    CHECK(near(pose.heading, PI / 4.0, 1e-12));
    CHECK(near(pose.speed, 7.5 * PI, 1e-12));

    CHECK(boat_route_arc_evaluate(&cw, 10, &pose));
    CHECK(near(pose.x, 10.0, 1e-12) && near(pose.z, 0.0, 1e-12));
    CHECK(pose.speed == 0.0);
    CHECK(near(pose.heading, 0.0, 1e-12));
    return 0;
}

static int direct_reverse_and_repeated_seeks(void)
{
    BoatRouteArc arc = sample_arc();
    const uint64_t seeks[] = { 10, 0, 7, 7, 5, 2, 5, 0, 10 };
    BoatRoutePose pose;
    for (size_t i = 0; i < sizeof(seeks) / sizeof(seeks[0]); ++i) {
        BoatRoutePose expected;
        CHECK(boat_route_arc_evaluate(&arc, seeks[i], &expected));
        CHECK(boat_route_arc_evaluate(&arc, seeks[i], &pose));
        CHECK(same_pose(&pose, &expected));
    }

    // Repeating an evaluation must not drift.
    BoatRoutePose first;
    CHECK(boat_route_arc_evaluate(&arc, 6, &first));
    for (int i = 0; i < 5; ++i) {
        CHECK(boat_route_arc_evaluate(&arc, 6, &pose));
        CHECK(same_pose(&pose, &first));
    }
    return 0;
}

static int arbitrary_frame_stepping(void)
{
    BoatRouteArc arc = sample_arc();
    const uint64_t steps[] = { 1, 2, 3, 1, 1, 2, 1, 2, 4, 10 };
    uint64_t tick = 0;
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
        tick += steps[i];
        BoatRoutePose direct, stepped;
        CHECK(boat_route_arc_evaluate(&arc, tick, &direct));
        CHECK(boat_route_arc_evaluate(&arc, tick, &stepped));
        CHECK(same_pose(&stepped, &direct));
    }
    CHECK(tick > arc.duration_ticks);
    return 0;
}

static int exact_and_exceeding_limits(void)
{
    BoatRouteArc arc = sample_arc();
    double arc_length = arc.radius * fabs(arc.sweep);
    double duration_seconds = (double)arc.duration_ticks * arc.tick_seconds;
    double duration_squared = duration_seconds * duration_seconds;
    double peak_speed = 1.5 * arc_length / duration_seconds;
    double peak_acceleration
        = 6.0 * arc_length / duration_squared + peak_speed * peak_speed / arc.radius;

    arc.max_speed = peak_speed;
    arc.max_acceleration = peak_acceleration;
    CHECK(boat_route_arc_validate(&arc));

    // Each bound is inclusive at its exact value and rejects the next representable value below.
    BoatRouteArc limited = arc;
    limited.max_speed = nextafter(peak_speed, 0.0);
    CHECK(!boat_route_arc_validate(&limited));
    limited = arc;
    limited.max_acceleration = nextafter(peak_acceleration, 0.0);
    CHECK(!boat_route_arc_validate(&limited));

    // Each limit is independent: generous headroom elsewhere does not excuse an excess.
    limited = arc;
    limited.max_speed = nextafter(peak_speed, 0.0);
    limited.max_acceleration = 1e9;
    CHECK(!boat_route_arc_validate(&limited));
    limited = arc;
    limited.max_speed = 1e9;
    limited.max_acceleration = nextafter(peak_acceleration, 0.0);
    CHECK(!boat_route_arc_validate(&limited));
    return 0;
}

static int invalid_inputs(void)
{
    BoatRouteArc arc = sample_arc();
    BoatRoutePose pose = sentinel_pose();

    CHECK(!boat_route_arc_validate(NULL));
    CHECK(!boat_route_arc_evaluate(NULL, 0, &pose) && pose_is_sentinel(&pose));
    CHECK(!boat_route_arc_evaluate(&arc, 0, NULL));

    BoatRouteArc bad = arc;
    bad.centre_x = NAN;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.centre_x = INFINITY;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.centre_z = -INFINITY;
    CHECK(!boat_route_arc_validate(&bad));

    bad = arc;
    bad.radius = 0.0;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.radius = -1.0;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.radius = NAN;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.radius = INFINITY;
    CHECK(!boat_route_arc_validate(&bad));

    bad = arc;
    bad.start_angle = NAN;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.start_angle = INFINITY;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.start_angle = 1e100;
    CHECK(!boat_route_arc_validate(&bad));

    bad = arc;
    bad.sweep = 0.0;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.sweep = NAN;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.sweep = INFINITY;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.sweep = PI + 1e-9;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.sweep = -(PI + 1e-9);
    CHECK(!boat_route_arc_validate(&bad));

    // A full half turn is allowed because the bound is inclusive at pi.
    bad = arc;
    bad.sweep = PI;
    bad.max_speed = 100.0;
    bad.max_acceleration = 1000.0;
    CHECK(boat_route_arc_validate(&bad));

    bad = arc;
    bad.duration_ticks = 0;
    CHECK(!boat_route_arc_validate(&bad));

    bad = arc;
    bad.tick_seconds = 0.0;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.tick_seconds = -0.1;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.tick_seconds = NAN;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.tick_seconds = INFINITY;
    CHECK(!boat_route_arc_validate(&bad));

    bad = arc;
    bad.max_speed = 0.0;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.max_speed = -1.0;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.max_speed = NAN;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.max_speed = INFINITY;
    CHECK(!boat_route_arc_validate(&bad));

    bad = arc;
    bad.max_acceleration = 0.0;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.max_acceleration = -1.0;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.max_acceleration = NAN;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.max_acceleration = INFINITY;
    CHECK(!boat_route_arc_validate(&bad));

    // Nonfinite intermediates: an overflowing duration and an underflowing duration squared.
    bad = arc;
    bad.tick_seconds = 1e300;
    CHECK(!boat_route_arc_validate(&bad));
    bad = arc;
    bad.tick_seconds = 1e-300;
    CHECK(!boat_route_arc_validate(&bad));

    // Failed evaluations leave the caller's pose untouched.
    bad = arc;
    bad.duration_ticks = 0;
    CHECK(!boat_route_arc_evaluate(&bad, 0, &pose));
    CHECK(pose_is_sentinel(&pose));
    bad = arc;
    bad.radius = 20001.0;
    CHECK(!boat_route_arc_evaluate(&bad, 5, &pose));
    CHECK(pose_is_sentinel(&pose));
    return 0;
}

static int coordinate_bounds(void)
{
    // The conservative whole-circle bound is inclusive at 20000 on both axes.
    BoatRouteArc boundary = {
        .centre_x = 19995.0,
        .centre_z = -19995.0,
        .radius = 5.0,
        .start_angle = 0.0,
        .sweep = PI / 2.0,
        .duration_ticks = 10,
        .tick_seconds = 0.1,
        .max_speed = 100.0,
        .max_acceleration = 1000.0,
    };
    CHECK(boat_route_arc_validate(&boundary));

    BoatRouteArc bad = boundary;
    bad.centre_x = 19995.5;
    CHECK(!boat_route_arc_validate(&bad));
    bad = boundary;
    bad.centre_z = 19995.5;
    CHECK(!boat_route_arc_validate(&bad));
    bad = boundary;
    bad.centre_x = -19995.5;
    CHECK(!boat_route_arc_validate(&bad));
    bad = boundary;
    bad.centre_z = -19995.5;
    CHECK(!boat_route_arc_validate(&bad));

    // A circle touching the bound from the origin is acceptable; a larger one is not.
    BoatRouteArc large = sample_arc();
    large.centre_x = 0.0;
    large.centre_z = 0.0;
    large.radius = BOAT_ROUTE_ARC_COORDINATE_LIMIT;
    large.max_speed = 1e6;
    large.max_acceleration = 1e6;
    CHECK(boat_route_arc_validate(&large));
    large.radius = BOAT_ROUTE_ARC_COORDINATE_LIMIT + 0.5;
    CHECK(!boat_route_arc_validate(&large));
    return 0;
}

static int huge_tick_clamps(void)
{
    BoatRouteArc arc = sample_arc();
    BoatRoutePose pose;
    CHECK(boat_route_arc_evaluate(&arc, UINT64_MAX, &pose));
    CHECK(near(pose.x, -10.0, 1e-12) && near(pose.z, 0.0, 1e-12));
    CHECK(pose.speed == 0.0);
    CHECK(near(pose.heading, 0.0, 1e-12));

    // One tick past the duration is the same clamped endpoint.
    BoatRoutePose past;
    CHECK(boat_route_arc_evaluate(&arc, arc.duration_ticks + 1, &past));
    CHECK(same_pose(&pose, &past));
    return 0;
}

int main(void)
{
    if (quarter_turn_endpoints_and_midpoint() || direct_reverse_and_repeated_seeks()
        || arbitrary_frame_stepping() || exact_and_exceeding_limits() || invalid_inputs()
        || coordinate_bounds() || huge_tick_clamps())
        return 1;
    puts("boat_route_arc: quarter turns, seeks, frame steps, limits, bounds, clamps and invalid "
         "input");
    return 0;
}
