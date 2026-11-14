#include "core/boat_route_path.h"

#include "core/boat_clock.h"

#include <math.h>

static bool piece(
    const BoatRoutePathPiece *p, uint64_t tick, BoatRoutePose *pose, uint64_t *duration)
{
    if (p->kind == BOAT_ROUTE_PATH_LINE) {
        if (p->shape.line.tick_seconds != BOAT_TICK_SECONDS)
            return false;
        *duration = p->shape.line.duration_ticks;
        return boat_route_leg_evaluate(&p->shape.line, tick, pose);
    }
    if (p->kind == BOAT_ROUTE_PATH_ARC) {
        if (p->shape.arc.tick_seconds != BOAT_TICK_SECONDS)
            return false;
        *duration = p->shape.arc.duration_ticks;
        return boat_route_arc_evaluate(&p->shape.arc, tick, pose);
    }
    return false;
}

static double mean_speed(const BoatRoutePathPiece *p);

bool boat_route_path_validate(const BoatRoutePath *path, uint64_t *total_ticks)
{
    if (!path || !total_ticks || !path->count || path->count > BOAT_ROUTE_PATH_PIECES)
        return false;
    uint64_t total = 0;
    BoatRoutePose end = { 0 };
    for (unsigned i = 0; i < path->count; ++i) {
        const BoatRoutePathPiece *p = &path->pieces[i];
        BoatRoutePose start;
        uint64_t duration;
        if (!piece(p, 0, &start, &duration))
            return false;
        double mean = mean_speed(p);
        if (!isfinite(mean) || mean <= 0)
            return false;
        if (i
            && (hypot(end.x - start.x, end.z - start.z) > .001
                || fabs(remainder(end.heading - start.heading, BOAT_TWO_PI)) > .001))
            return false;
        if (duration > UINT64_MAX - total)
            return false;
        total += duration;
        if (p->dwell_ticks > UINT64_MAX - total)
            return false;
        total += p->dwell_ticks;
        if (!piece(p, duration, &end, &duration))
            return false;
    }
    *total_ticks = total;
    return true;
}

static double mean_speed(const BoatRoutePathPiece *p)
{
    if (p->kind == BOAT_ROUTE_PATH_LINE) {
        const BoatRouteLeg *l = &p->shape.line;
        return hypot(l->end_x - l->start_x, l->end_z - l->start_z)
            / ((double)l->duration_ticks * BOAT_TICK_SECONDS);
    }
    const BoatRouteArc *a = &p->shape.arc;
    return a->radius * fabs(a->sweep) / ((double)a->duration_ticks * BOAT_TICK_SECONDS);
}

// Cubic Hermite distance with endpoint speeds no greater than either adjoining piece's mean
// speed. Both normalized endpoint slopes lie in [0,1], so speed is at most 1.5 times mean and
// tangential acceleration at most 6 times length / duration^2, as validated by each primitive.
static bool moving_piece(
    const BoatRoutePath *path, unsigned i, uint64_t tick, BoatRoutePose *pose, double *yaw_rate)
{
    const BoatRoutePathPiece *p = &path->pieces[i];
    uint64_t ticks = p->kind == BOAT_ROUTE_PATH_LINE ? p->shape.line.duration_ticks
                                                     : p->shape.arc.duration_ticks;
    double mean = mean_speed(p);
    double start
        = i && !path->pieces[i - 1].dwell_ticks ? fmin(mean, mean_speed(&path->pieces[i - 1])) : 0;
    double end
        = i + 1 < path->count && !p->dwell_ticks ? fmin(mean, mean_speed(&path->pieces[i + 1])) : 0;
    double m0 = start / mean, m1 = end / mean;
    double u = (double)tick / (double)ticks;
    double u2 = u * u, u3 = u2 * u;
    double progress = 3 * u2 - 2 * u3 + (u3 - 2 * u2 + u) * m0 + (u3 - u2) * m1;
    double speed = mean * (6 * u * (1 - u) + (3 * u2 - 4 * u + 1) * m0 + (3 * u2 - 2 * u) * m1);
    BoatRoutePose out;
    double yaw = 0;
    if (p->kind == BOAT_ROUTE_PATH_LINE) {
        const BoatRouteLeg *l = &p->shape.line;
        double dx = l->end_x - l->start_x, dz = l->end_z - l->start_z;
        out = (BoatRoutePose) { l->start_x + dx * progress, l->start_z + dz * progress,
            atan2(dx, -dz), speed };
    } else {
        const BoatRouteArc *a = &p->shape.arc;
        double angle = a->start_angle + a->sweep * progress;
        double sign = a->sweep > 0 ? 1 : -1;
        out = (BoatRoutePose) { a->centre_x + a->radius * cos(angle),
            a->centre_z + a->radius * sin(angle), atan2(-sign * sin(angle), -sign * cos(angle)),
            speed };
        yaw = sign * speed / a->radius;
    }
    if (!isfinite(out.x) || !isfinite(out.z) || !isfinite(out.heading) || !isfinite(out.speed)
        || !isfinite(yaw))
        return false;
    *pose = out;
    if (yaw_rate)
        *yaw_rate = yaw;
    return true;
}

bool boat_route_path_evaluate(
    const BoatRoutePath *path, uint64_t tick, BoatRoutePose *pose, double *yaw_rate)
{
    uint64_t total;
    if (!pose || !boat_route_path_validate(path, &total))
        return false;
    BoatRoutePose result;
    for (unsigned i = 0; i < path->count; ++i) {
        const BoatRoutePathPiece *p = &path->pieces[i];
        uint64_t duration;
        if (!piece(p, 0, &result, &duration))
            return false;
        if (tick < duration) {
            return moving_piece(path, i, tick, pose, yaw_rate);
        }
        tick -= duration;
        if (i + 1 == path->count || tick < p->dwell_ticks) {
            if (!piece(p, duration, &result, &duration))
                return false;
            *pose = result;
            if (yaw_rate)
                *yaw_rate = 0;
            return true;
        }
        tick -= p->dwell_ticks;
    }
    return false;
}
