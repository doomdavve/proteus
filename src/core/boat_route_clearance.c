#include "core/boat_route_clearance.h"

#include <math.h>

// Peak speed of the smoothstep blend: 1.5 * distance / duration_seconds. Returns false when the
// leg's geometry or the result is not a finite positive value.
static bool leg_peak_speed(const BoatRouteLeg *leg, double *peak_speed)
{
    double dx = leg->end_x - leg->start_x;
    double dz = leg->end_z - leg->start_z;
    double distance = hypot(dx, dz);
    double duration_seconds = (double)leg->duration_ticks * leg->tick_seconds;
    if (!isfinite(distance) || distance <= 0.0)
        return false;
    if (!isfinite(duration_seconds) || duration_seconds <= 0.0)
        return false;

    double peak = 1.5 * distance / duration_seconds;
    if (!isfinite(peak) || peak <= 0.0)
        return false;
    *peak_speed = peak;
    return true;
}

BoatRouteClearance boat_route_clearance_check(const BoatRouteLeg *a, double radius_a,
    const BoatRouteLeg *b, double radius_b, uint64_t max_intervals)
{
    if (a == NULL || b == NULL)
        return BOAT_ROUTE_CLEARANCE_INVALID;
    if (!boat_route_leg_validate(a) || !boat_route_leg_validate(b))
        return BOAT_ROUTE_CLEARANCE_INVALID;
    if (!isfinite(radius_a) || !isfinite(radius_b) || radius_a <= 0.0 || radius_b <= 0.0)
        return BOAT_ROUTE_CLEARANCE_INVALID;
    if (a->tick_seconds != b->tick_seconds)
        return BOAT_ROUTE_CLEARANCE_INVALID;
    if (max_intervals == 0)
        return BOAT_ROUTE_CLEARANCE_INVALID;

    double peak_a, peak_b;
    if (!leg_peak_speed(a, &peak_a) || !leg_peak_speed(b, &peak_b))
        return BOAT_ROUTE_CLEARANCE_INVALID;

    // The Lipschitz margin covers relative travel during half a tick on either side of a sample.
    double combined_peak = peak_a + peak_b;
    double margin = combined_peak * a->tick_seconds / 2.0;
    double threshold = radius_a + radius_b + margin;
    if (!isfinite(combined_peak) || !isfinite(margin) || !isfinite(threshold))
        return BOAT_ROUTE_CLEARANCE_INVALID;

    uint64_t horizon
        = a->duration_ticks > b->duration_ticks ? a->duration_ticks : b->duration_ticks;
    if (horizon > max_intervals)
        return BOAT_ROUTE_CLEARANCE_BUDGET;

    // Count up to the horizon without an overflow-prone tick <= horizon test.
    for (uint64_t tick = 0;; ++tick) {
        BoatRoutePose pose_a, pose_b;
        if (!boat_route_leg_evaluate(a, tick, &pose_a)
            || !boat_route_leg_evaluate(b, tick, &pose_b))
            return BOAT_ROUTE_CLEARANCE_INVALID;

        double dx = pose_a.x - pose_b.x;
        double dz = pose_a.z - pose_b.z;
        if (!isfinite(dx) || !isfinite(dz))
            return BOAT_ROUTE_CLEARANCE_INVALID;
        double separation = hypot(dx, dz);
        if (!isfinite(separation))
            return BOAT_ROUTE_CLEARANCE_INVALID;
        if (separation <= threshold)
            return BOAT_ROUTE_CLEARANCE_CONFLICT;

        if (tick == horizon)
            break;
    }
    return BOAT_ROUTE_CLEARANCE_CLEAR;
}
