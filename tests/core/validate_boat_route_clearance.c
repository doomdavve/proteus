// Marina M3 motion groundwork: conservative pairwise clearance between two straight open legs.
#include "core/boat_route_clearance.h"

#include <math.h>
#include <stdio.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static BoatRouteLeg make_leg(double start_x, double start_z, double end_x, double end_z,
    uint64_t duration_ticks, double tick_seconds, double max_speed, double max_acceleration)
{
    BoatRouteLeg leg = {
        .start_x = start_x,
        .start_z = start_z,
        .end_x = end_x,
        .end_z = end_z,
        .duration_ticks = duration_ticks,
        .tick_seconds = tick_seconds,
        .max_speed = max_speed,
        .max_acceleration = max_acceleration,
    };
    return leg;
}

static bool same_leg(const BoatRouteLeg *a, const BoatRouteLeg *b)
{
    return a->start_x == b->start_x && a->start_z == b->start_z && a->end_x == b->end_x
        && a->end_z == b->end_z && a->duration_ticks == b->duration_ticks
        && a->tick_seconds == b->tick_seconds && a->max_speed == b->max_speed
        && a->max_acceleration == b->max_acceleration;
}

static int distant_parallel_legs_clear(void)
{
    // Two 100 m legs run in parallel a kilometre apart; the horizon exactly meets the budget.
    BoatRouteLeg a = make_leg(0.0, 0.0, 0.0, -100.0, 100, 0.1, 15.0, 6.0);
    BoatRouteLeg b = make_leg(1000.0, 0.0, 1000.0, -100.0, 100, 0.1, 15.0, 6.0);
    CHECK(boat_route_leg_validate(&a) && boat_route_leg_validate(&b));
    CHECK(boat_route_clearance_check(&a, 2.0, &b, 2.0, 100) == BOAT_ROUTE_CLEARANCE_CLEAR);
    return 0;
}

static int head_on_crossing_between_samples_conflicts(void)
{
    // Opposite 21-tick legs meet at the middle of tick 10.5, never exactly on a sample. Radii are
    // one metre each, so only the Lipschitz margin catches the near miss.
    BoatRouteLeg a = make_leg(-50.0, 0.0, 50.0, 0.0, 21, 0.1, 100.0, 200.0);
    BoatRouteLeg b = make_leg(50.0, 0.0, -50.0, 0.0, 21, 0.1, 100.0, 200.0);
    CHECK(boat_route_leg_validate(&a) && boat_route_leg_validate(&b));

    BoatRoutePose pose_a, pose_b;
    CHECK(boat_route_leg_evaluate(&a, 10, &pose_a));
    CHECK(boat_route_leg_evaluate(&b, 10, &pose_b));
    double separation = hypot(pose_a.x - pose_b.x, pose_a.z - pose_b.z);
    CHECK(separation > 1.0 + 1.0);
    CHECK(separation < 1.0 + 1.0 + (1.5 * 100.0 / 2.1 + 1.5 * 100.0 / 2.1) * 0.1 / 2.0);

    CHECK(boat_route_clearance_check(&a, 1.0, &b, 1.0, 21) == BOAT_ROUTE_CLEARANCE_CONFLICT);
    return 0;
}

static int same_place_different_times_clear(void)
{
    // Perpendicular legs both pass through the origin, but at one second and five seconds. The
    // timing difference leaves ample pairwise clearance.
    BoatRouteLeg a = make_leg(-50.0, 0.0, 50.0, 0.0, 20, 0.1, 100.0, 200.0);
    BoatRouteLeg b = make_leg(0.0, 50.0, 0.0, -50.0, 100, 0.1, 100.0, 200.0);
    CHECK(boat_route_leg_validate(&a) && boat_route_leg_validate(&b));
    CHECK(boat_route_clearance_check(&a, 1.0, &b, 1.0, 100) == BOAT_ROUTE_CLEARANCE_CLEAR);
    return 0;
}

static int stationary_after_completion_conflicts(void)
{
    // The short leg finishes at (0, -10) after one second and holds there; the long leg later
    // sweeps through that endpoint.
    BoatRouteLeg a = make_leg(0.0, 0.0, 0.0, -10.0, 10, 0.1, 100.0, 200.0);
    BoatRouteLeg b = make_leg(0.0, -100.0, 0.0, 0.0, 100, 0.1, 100.0, 200.0);
    CHECK(boat_route_leg_validate(&a) && boat_route_leg_validate(&b));
    CHECK(boat_route_clearance_check(&a, 1.0, &b, 1.0, 100) == BOAT_ROUTE_CLEARANCE_CONFLICT);
    return 0;
}

static int touching_limits_conflict(void)
{
    // Equal parallel legs one metre per tick: peaks are 1.5 m/s and the margin is 1.5 m. With
    // two-metre radii the safety limit is 5.5 m, so a constant 5.5 m separation conflicts.
    BoatRouteLeg a = make_leg(0.0, 0.0, 0.0, -100.0, 100, 1.0, 2.0, 1.0);
    BoatRouteLeg b = make_leg(5.5, 0.0, 5.5, -100.0, 100, 1.0, 2.0, 1.0);
    CHECK(boat_route_leg_validate(&a) && boat_route_leg_validate(&b));
    CHECK(boat_route_clearance_check(&a, 2.0, &b, 2.0, 100) == BOAT_ROUTE_CLEARANCE_CONFLICT);

    // Just beyond the inclusive limit is clear.
    b = make_leg(5.6, 0.0, 5.6, -100.0, 100, 1.0, 2.0, 1.0);
    CHECK(boat_route_clearance_check(&a, 2.0, &b, 2.0, 100) == BOAT_ROUTE_CLEARANCE_CLEAR);

    // Overlapping hulls at tick zero conflict regardless of the horizon.
    b = make_leg(0.0, 5.0, 0.0, -100.0, 100, 1.0, 2.0, 1.0);
    CHECK(boat_route_clearance_check(&a, 2.0, &b, 2.0, 100) == BOAT_ROUTE_CLEARANCE_CONFLICT);
    return 0;
}

static int budget_and_huge_duration(void)
{
    BoatRouteLeg a = make_leg(0.0, 0.0, 0.0, -100.0, 100, 1.0, 2.0, 1.0);
    BoatRouteLeg b = make_leg(100.0, 0.0, 100.0, -100.0, 100, 1.0, 2.0, 1.0);
    CHECK(boat_route_leg_validate(&a) && boat_route_leg_validate(&b));

    // One interval short of the shared horizon is rejected as a budget failure.
    CHECK(boat_route_clearance_check(&a, 2.0, &b, 2.0, 99) == BOAT_ROUTE_CLEARANCE_BUDGET);

    // An effectively unbounded duration is rejected before any sampling loop.
    BoatRouteLeg huge = a;
    huge.duration_ticks = UINT64_C(9223372036854775807);
    CHECK(boat_route_leg_validate(&huge));
    CHECK(boat_route_clearance_check(&huge, 2.0, &b, 2.0, 10) == BOAT_ROUTE_CLEARANCE_BUDGET);
    return 0;
}

static int invalid_inputs(void)
{
    BoatRouteLeg a = make_leg(0.0, 0.0, 0.0, -100.0, 100, 1.0, 2.0, 1.0);
    BoatRouteLeg b = make_leg(100.0, 0.0, 100.0, -100.0, 100, 1.0, 2.0, 1.0);
    BoatRouteLeg before_a = a;
    BoatRouteLeg before_b = b;

    CHECK(boat_route_clearance_check(NULL, 2.0, &b, 2.0, 100) == BOAT_ROUTE_CLEARANCE_INVALID);
    CHECK(boat_route_clearance_check(&a, 2.0, NULL, 2.0, 100) == BOAT_ROUTE_CLEARANCE_INVALID);

    BoatRouteLeg bad = a;
    bad.end_x = bad.start_x;
    bad.end_z = bad.start_z;
    CHECK(boat_route_clearance_check(&bad, 2.0, &b, 2.0, 100) == BOAT_ROUTE_CLEARANCE_INVALID);
    bad = a;
    bad.duration_ticks = 0;
    CHECK(boat_route_clearance_check(&bad, 2.0, &b, 2.0, 100) == BOAT_ROUTE_CLEARANCE_INVALID);
    bad = a;
    bad.tick_seconds = 0.0;
    CHECK(boat_route_clearance_check(&bad, 2.0, &b, 2.0, 100) == BOAT_ROUTE_CLEARANCE_INVALID);
    bad = b;
    bad.tick_seconds = 0.2;
    CHECK(boat_route_clearance_check(&a, 2.0, &bad, 2.0, 100) == BOAT_ROUTE_CLEARANCE_INVALID);

    CHECK(boat_route_clearance_check(&a, 0.0, &b, 2.0, 100) == BOAT_ROUTE_CLEARANCE_INVALID);
    CHECK(boat_route_clearance_check(&a, -1.0, &b, 2.0, 100) == BOAT_ROUTE_CLEARANCE_INVALID);
    CHECK(boat_route_clearance_check(&a, NAN, &b, 2.0, 100) == BOAT_ROUTE_CLEARANCE_INVALID);
    CHECK(boat_route_clearance_check(&a, INFINITY, &b, 2.0, 100) == BOAT_ROUTE_CLEARANCE_INVALID);
    CHECK(boat_route_clearance_check(&a, 2.0, &b, 0.0, 100) == BOAT_ROUTE_CLEARANCE_INVALID);
    CHECK(boat_route_clearance_check(&a, 2.0, &b, INFINITY, 100) == BOAT_ROUTE_CLEARANCE_INVALID);

    CHECK(boat_route_clearance_check(&a, 2.0, &b, 2.0, 0) == BOAT_ROUTE_CLEARANCE_INVALID);

    // Finite radii whose sum overflows are a nonfinite intermediate, not a conflict.
    CHECK(boat_route_clearance_check(&a, 1e308, &b, 1e308, 100) == BOAT_ROUTE_CLEARANCE_INVALID);

    // Reads never mutate either leg.
    CHECK(same_leg(&a, &before_a) && same_leg(&b, &before_b));
    return 0;
}

int main(void)
{
    if (distant_parallel_legs_clear() || head_on_crossing_between_samples_conflicts()
        || same_place_different_times_clear() || stationary_after_completion_conflicts()
        || touching_limits_conflict() || budget_and_huge_duration() || invalid_inputs())
        return 1;
    puts("boat_route_clearance: clear, conflict, stationary, touching, budget and invalid input");
    return 0;
}
