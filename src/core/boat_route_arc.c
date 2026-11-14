#include "core/boat_route_arc.h"

#include <math.h>

// A coordinate is valid only when it is finite and within the documented absolute bound. For a
// circle, checking |centre| + radius conservatively bounds every point on it.
static bool circle_in_bounds(double centre, double radius)
{
    return isfinite(centre) && fabs(centre) + radius <= BOAT_ROUTE_ARC_COORDINATE_LIMIT;
}

bool boat_route_arc_validate(const BoatRouteArc *arc)
{
    if (arc == NULL)
        return false;
    if (!isfinite(arc->centre_x) || !isfinite(arc->centre_z))
        return false;
    if (!isfinite(arc->radius) || arc->radius <= 0.0)
        return false;
    // Very large angles can absorb a finite sweep during addition, so require a bounded phase.
    if (!isfinite(arc->start_angle) || fabs(arc->start_angle) > 6.28318530717958647692)
        return false;
    if (!isfinite(arc->sweep) || arc->sweep == 0.0)
        return false;
    if (fabs(arc->sweep) > 3.14159265358979323846)
        return false;
    if (arc->duration_ticks == 0 || !isfinite(arc->tick_seconds) || arc->tick_seconds <= 0.0)
        return false;
    if (!isfinite(arc->max_speed) || arc->max_speed <= 0.0)
        return false;
    if (!isfinite(arc->max_acceleration) || arc->max_acceleration <= 0.0)
        return false;

    if (!circle_in_bounds(arc->centre_x, arc->radius)
        || !circle_in_bounds(arc->centre_z, arc->radius))
        return false;

    double duration_seconds = (double)arc->duration_ticks * arc->tick_seconds;
    if (!isfinite(duration_seconds) || duration_seconds <= 0.0)
        return false;

    double arc_length = arc->radius * fabs(arc->sweep);
    double duration_squared = duration_seconds * duration_seconds;
    if (!isfinite(arc_length) || arc_length <= 0.0)
        return false;
    if (!isfinite(duration_squared) || duration_squared <= 0.0)
        return false;

    // The smoothstep blend peaks at 1.5 * arc_length / duration. The total acceleration is
    // conservatively bounded by the triangle inequality: 6 * arc_length / duration^2 tangential
    // plus peak_speed^2 / radius centripetal. Reject nonfinite results before comparing.
    double peak_speed = 1.5 * arc_length / duration_seconds;
    double peak_acceleration
        = 6.0 * arc_length / duration_squared + peak_speed * peak_speed / arc->radius;
    if (!isfinite(peak_speed) || !isfinite(peak_acceleration))
        return false;
    return peak_speed <= arc->max_speed && peak_acceleration <= arc->max_acceleration;
}

bool boat_route_arc_evaluate(const BoatRouteArc *arc, uint64_t tick, BoatRoutePose *pose)
{
    if (pose == NULL || !boat_route_arc_validate(arc))
        return false;

    double duration_ticks = (double)arc->duration_ticks;
    double u = (double)tick / duration_ticks;
    if (u > 1.0)
        u = 1.0;
    if (u < 0.0)
        u = 0.0;

    double blend = u * u * (3.0 - 2.0 * u);
    double theta = arc->start_angle + arc->sweep * blend;
    if (!isfinite(theta))
        return false;
    double cosine = cos(theta);
    double sine = sin(theta);
    double x = arc->centre_x + arc->radius * cosine;
    double z = arc->centre_z + arc->radius * sine;

    // Tangent along the signed sweep direction. The sign is applied to the tangent rather than
    // to the angle so the heading is defined at both zero-speed endpoints as well.
    double direction = arc->sweep > 0.0 ? 1.0 : -1.0;
    double dx_ds = -direction * sine;
    double dz_ds = direction * cosine;
    double heading = atan2(dx_ds, -dz_ds);

    double speed = 0.0;
    if (u > 0.0 && u < 1.0) {
        double duration_seconds = duration_ticks * arc->tick_seconds;
        speed = arc->radius * fabs(arc->sweep) * (6.0 * u - 6.0 * u * u) / duration_seconds;
    }

    BoatRoutePose result = { x, z, heading, speed };
    if (!isfinite(result.x) || !isfinite(result.z) || !isfinite(result.heading)
        || !isfinite(result.speed))
        return false;
    *pose = result;
    return true;
}
