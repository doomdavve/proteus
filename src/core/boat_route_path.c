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
            if (!piece(p, tick, &result, &duration))
                return false;
            *pose = result;
            if (yaw_rate)
                *yaw_rate = p->kind == BOAT_ROUTE_PATH_ARC
                    ? copysign(result.speed / p->shape.arc.radius, p->shape.arc.sweep)
                    : 0;
            return true;
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
