#include "core/boat_route_leg.h"

#include <math.h>

// A coordinate is valid only when it is finite and within the documented absolute bound.
static bool coordinate_in_bounds(double value)
{
    return isfinite(value) && fabs(value) <= BOAT_ROUTE_LEG_COORDINATE_LIMIT;
}

bool boat_route_leg_validate(const BoatRouteLeg *leg)
{
    if (leg == NULL)
        return false;
    if (!coordinate_in_bounds(leg->start_x) || !coordinate_in_bounds(leg->start_z)
        || !coordinate_in_bounds(leg->end_x) || !coordinate_in_bounds(leg->end_z))
        return false;
    if (leg->duration_ticks == 0 || !isfinite(leg->tick_seconds) || leg->tick_seconds <= 0.0)
        return false;
    if (!isfinite(leg->max_speed) || leg->max_speed <= 0.0)
        return false;
    if (!isfinite(leg->max_acceleration) || leg->max_acceleration <= 0.0)
        return false;

    double dx = leg->end_x - leg->start_x;
    double dz = leg->end_z - leg->start_z;
    double distance = hypot(dx, dz);
    double duration_seconds = (double)leg->duration_ticks * leg->tick_seconds;
    if (!isfinite(distance) || distance <= 0.0)
        return false;
    if (!isfinite(duration_seconds) || duration_seconds <= 0.0)
        return false;

    // The smoothstep blend peaks at 1.5 * distance / duration; its acceleration peaks at
    // 6 * distance / duration^2. Reject nonfinite results before comparing with the limits.
    double duration_squared = duration_seconds * duration_seconds;
    if (!isfinite(duration_squared))
        return false;
    double peak_speed = 1.5 * distance / duration_seconds;
    double peak_acceleration = 6.0 * distance / duration_squared;
    if (!isfinite(peak_speed) || !isfinite(peak_acceleration))
        return false;
    return peak_speed <= leg->max_speed && peak_acceleration <= leg->max_acceleration;
}

bool boat_route_leg_evaluate(const BoatRouteLeg *leg, uint64_t tick, BoatRoutePose *pose)
{
    if (pose == NULL || !boat_route_leg_validate(leg))
        return false;

    double duration_ticks = (double)leg->duration_ticks;
    double u = (double)tick / duration_ticks;
    if (u > 1.0)
        u = 1.0;
    if (u < 0.0)
        u = 0.0;

    double dx = leg->end_x - leg->start_x;
    double dz = leg->end_z - leg->start_z;
    double blend = u * u * (3.0 - 2.0 * u);
    double x = leg->start_x + dx * blend;
    double z = leg->start_z + dz * blend;

    // Endpoint ticks have exactly zero speed; the interior expression returns to zero there
    // only approximately.
    double speed = 0.0;
    if (u > 0.0 && u < 1.0) {
        double duration_seconds = duration_ticks * leg->tick_seconds;
        double distance = hypot(dx, dz);
        speed = distance * (6.0 * u - 6.0 * u * u) / duration_seconds;
    }

    BoatRoutePose result = { x, z, atan2(dx, -dz), speed };
    if (!isfinite(result.x) || !isfinite(result.z) || !isfinite(result.heading)
        || !isfinite(result.speed))
        return false;
    *pose = result;
    return true;
}
