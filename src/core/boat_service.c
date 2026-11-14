#include "core/boat_service.h"

#include "core/boat_clock.h"

#include <math.h>

static bool piece_ticks(const BoatRoutePathPiece *p, uint64_t *ticks)
{
    if (p->kind == BOAT_ROUTE_PATH_LINE)
        *ticks = p->shape.line.duration_ticks;
    else if (p->kind == BOAT_ROUTE_PATH_ARC)
        *ticks = p->shape.arc.duration_ticks;
    else
        return false;
    return true;
}

bool boat_service_dock_interval(
    const BoatRoutePath *path, unsigned piece, uint64_t *begin, uint64_t *end)
{
    uint64_t total;
    if (!path || !begin || !end || !boat_route_path_validate(path, &total) || piece >= path->count
        || !path->pieces[piece].dwell_ticks)
        return false;
    uint64_t at = 0;
    for (unsigned i = 0; i <= piece; ++i) {
        uint64_t ticks;
        if (!piece_ticks(&path->pieces[i], &ticks))
            return false;
        at += ticks;
        if (i == piece) {
            *begin = at;
            *end = at + path->pieces[i].dwell_ticks;
            return true;
        }
        at += path->pieces[i].dwell_ticks;
    }
    return false;
}

static bool usable(const BoatServiceDefinition *s, const BoatRoutePath *path, uint64_t *total)
{
    return s && path && s->kind < BOAT_SERVICE_KINDS && !path->loop
        && boat_route_path_validate(path, total) && *total <= UINT64_MAX - s->start_tick;
}

bool boat_service_state(const BoatServiceDefinition *service, const BoatRoutePath *path,
    uint64_t tick, BoatServiceState *state)
{
    uint64_t total, begin = 0, end = 0;
    if (!state || !usable(service, path, &total))
        return false;
    bool visitor = service->kind == BOAT_SERVICE_VISITOR;
    if (visitor && !boat_service_dock_interval(path, service->dock_piece, &begin, &end))
        return false;
    BoatServiceState out = { 0 };
    if (tick < service->start_tick) {
        out.owner = visitor ? BOAT_OWNER_ABSENT : BOAT_OWNER_MOORED;
    } else if (tick - service->start_tick >= total) {
        out.owner = visitor ? BOAT_OWNER_ABSENT : BOAT_OWNER_MOORED;
        out.path_tick = total;
    } else {
        out.path_tick = tick - service->start_tick;
        out.owner = visitor && out.path_tick >= begin && out.path_tick < end
            ? BOAT_OWNER_MOORED
            : BOAT_OWNER_TRAVELLING;
    }
    *state = out;
    return true;
}

bool boat_service_pose(const BoatServiceDefinition *service, const BoatRoutePath *path,
    uint64_t tick, BoatRoutePose *pose, double *yaw_rate)
{
    BoatServiceState state;
    if (!boat_service_state(service, path, tick, &state) || state.owner == BOAT_OWNER_ABSENT)
        return false;
    // Before its window a resident rests at the path start (tick 0); after it, at the end.
    return boat_route_path_evaluate(path, state.path_tick, pose, yaw_rate);
}

static bool near_pose(const BoatRoutePose *p, BoatServiceBerthPose b)
{
    return hypot(p->x - b.x, p->z - b.z) <= BOAT_SERVICE_POSE_TOLERANCE
        && fabs(remainder(p->heading - b.heading, BOAT_TWO_PI)) <= BOAT_SERVICE_POSE_TOLERANCE;
}

BoatServiceFault boat_service_validate(const BoatServiceDefinition *service,
    const BoatRoutePath *path, BoatServiceBerthPose berth, BoatRoutePose *got)
{
    uint64_t total;
    if (!usable(service, path, &total))
        return BOAT_SERVICE_FAULT_PATH;
    BoatRoutePose pose = { 0 };
    BoatServiceFault fault = BOAT_SERVICE_OK;
    if (service->kind == BOAT_SERVICE_RESIDENT) {
        if (!boat_route_path_evaluate(path, 0, &pose, NULL) || !near_pose(&pose, berth))
            fault = BOAT_SERVICE_FAULT_START;
        else if (!boat_route_path_evaluate(path, total, &pose, NULL) || !near_pose(&pose, berth))
            fault = BOAT_SERVICE_FAULT_END;
    } else {
        uint64_t begin, end;
        if (!boat_service_dock_interval(path, service->dock_piece, &begin, &end))
            return BOAT_SERVICE_FAULT_DWELL;
        if (!boat_route_path_evaluate(path, begin, &pose, NULL) || !near_pose(&pose, berth))
            fault = BOAT_SERVICE_FAULT_DOCK;
    }
    if (fault && got)
        *got = pose;
    return fault;
}

bool boat_service_reservation(
    const BoatServiceDefinition *service, const BoatRoutePath *path, uint64_t *begin, uint64_t *end)
{
    uint64_t total;
    if (!begin || !end || !usable(service, path, &total))
        return false;
    if (service->kind == BOAT_SERVICE_RESIDENT) {
        *begin = 0;
        *end = UINT64_MAX;
    } else {
        *begin = service->start_tick;
        *end = service->start_tick + total;
    }
    return true;
}

bool boat_service_intervals_overlap(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1)
{
    return a0 < b1 && b0 < a1;
}
