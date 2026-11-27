// Marina M4: vessel ownership at exact ticks around every handoff, seeks and berth checks.
#include "core/boat_clock.h"
#include "core/boat_service.h"

#include <math.h>

#ifndef M_PI // Strict C17 on glibc hides it.
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static BoatRoutePathPiece line(
    double x0, double z0, double x1, double z1, uint64_t ticks, uint64_t dwell)
{
    BoatRoutePathPiece p = { .kind = BOAT_ROUTE_PATH_LINE, .dwell_ticks = dwell };
    p.shape.line = (BoatRouteLeg) { x0, z0, x1, z1, ticks, BOAT_TICK_SECONDS, 5, 7 };
    return p;
}

static BoatRoutePathPiece arc(
    double cx, double cz, double r, double start, double sweep, uint64_t ticks)
{
    BoatRoutePathPiece p = { .kind = BOAT_ROUTE_PATH_ARC };
    p.shape.arc = (BoatRouteArc) { cx, cz, r, start, sweep, ticks, BOAT_TICK_SECONDS, 5, 7 };
    return p;
}

// The marina-route-demo visitor: approach, turn, slow dock (dwell 240), departure.
static BoatRoutePath visitor_path(void)
{
    BoatRoutePath path = { .count = 4 };
    path.pieces[0] = line(0, 170, 0, 185, 600, 0);
    path.pieces[1] = arc(10, 185, 10, M_PI, -M_PI / 2, 600);
    path.pieces[2] = line(10, 195, 12, 195, 240, 240);
    path.pieces[3] = arc(12, 185, 10, M_PI / 2, -M_PI / 2, 600);
    return path;
}

int main(void)
{
    BoatRoutePath path = visitor_path();
    uint64_t total;
    CHECK(boat_route_path_validate(&path, &total) && total == 2280);

    uint64_t begin, end;
    CHECK(boat_service_dock_interval(&path, 2, &begin, &end));
    CHECK(begin == 600 + 600 + 240 && end == begin + 240);
    CHECK(!boat_service_dock_interval(&path, 0, &begin, &end)); // No dwell.
    CHECK(!boat_service_dock_interval(&path, 4, &begin, &end)); // No such piece.

    BoatServiceDefinition visitor = { BOAT_SERVICE_VISITOR, 1000, 2 };
    BoatServiceState s;
    CHECK(boat_service_state(&visitor, &path, 999, &s) && s.owner == BOAT_OWNER_ABSENT);
    CHECK(boat_service_state(&visitor, &path, 1000, &s) && s.owner == BOAT_OWNER_TRAVELLING
        && s.path_tick == 0);
    uint64_t dock = 1000 + 1440;
    CHECK(boat_service_state(&visitor, &path, dock - 1, &s) && s.owner == BOAT_OWNER_TRAVELLING);
    CHECK(boat_service_state(&visitor, &path, dock, &s) && s.owner == BOAT_OWNER_MOORED);
    CHECK(boat_service_state(&visitor, &path, dock + 239, &s) && s.owner == BOAT_OWNER_MOORED);
    CHECK(boat_service_state(&visitor, &path, dock + 240, &s) && s.owner == BOAT_OWNER_TRAVELLING);
    CHECK(boat_service_state(&visitor, &path, 1000 + total - 1, &s)
        && s.owner == BOAT_OWNER_TRAVELLING);
    CHECK(boat_service_state(&visitor, &path, 1000 + total, &s) && s.owner == BOAT_OWNER_ABSENT);
    CHECK(boat_service_state(&visitor, &path, UINT64_MAX, &s) && s.owner == BOAT_OWNER_ABSENT);

    // Berth pose: the dock piece end, (12, 195) heading pi/2.
    BoatServiceBerthPose berth = { 12, 195, M_PI / 2 };
    CHECK(boat_service_validate(&visitor, &path, berth, NULL) == BOAT_SERVICE_OK);
    BoatServiceBerthPose off = { 12.01, 195, M_PI / 2 };
    BoatRoutePose got;
    CHECK(boat_service_validate(&visitor, &path, off, &got) == BOAT_SERVICE_FAULT_DOCK);
    CHECK(fabs(got.x - 12) < 1e-9);
    BoatServiceBerthPose turned = { 12, 195, 0 };
    CHECK(boat_service_validate(&visitor, &path, turned, NULL) == BOAT_SERVICE_FAULT_DOCK);
    BoatServiceDefinition no_dwell = { BOAT_SERVICE_VISITOR, 0, 0 };
    CHECK(boat_service_validate(&no_dwell, &path, berth, NULL) == BOAT_SERVICE_FAULT_DWELL);

    // Moored pose is the berth pose for the whole stay; direct seeks equal stepped evaluation.
    BoatRoutePose pose;
    for (uint64_t t = dock; t < dock + 240; t += 17) {
        CHECK(boat_service_pose(&visitor, &path, t, &pose, NULL));
        CHECK(fabs(pose.x - 12) < 1e-9 && fabs(pose.z - 195) < 1e-9 && pose.speed == 0);
    }
    CHECK(!boat_service_pose(&visitor, &path, 0, &pose, NULL)); // Absent: no pose.
    BoatRoutePose a, b;
    for (uint64_t t = 1000; t < 1000 + total; t += 7) {
        CHECK(boat_service_pose(&visitor, &path, t, &a, NULL));
        CHECK(boat_service_pose(&visitor, &path, t, &b, NULL));
        CHECK(a.x == b.x && a.z == b.z && a.heading == b.heading);
    }

    // Resident: a round trip from and back to one berth.
    BoatRoutePath round = { .count = 2 };
    round.pieces[0] = line(0, 0, 0, -30, 600, 0);
    round.pieces[1] = line(0, -30, 0, 0, 600, 0);
    // Both legs head differently; the join is not tangent, so the path itself is rejected.
    CHECK(!boat_route_path_validate(&round, &total));
    BoatRoutePath out = { .count = 3 };
    out.pieces[0] = line(0, 0, 0, -30, 1200, 60);
    out.pieces[1] = arc(10, -30, 10, M_PI, M_PI, 1200);
    out.pieces[2] = line(20, -30, 20, 0, 1200, 0);
    CHECK(boat_route_path_validate(&out, &total));
    BoatServiceDefinition resident = { BOAT_SERVICE_RESIDENT, 500, 0 };
    BoatServiceBerthPose start = { 0, 0, 0 };
    // Ends at (20, 0), not the start: a resident must return to the same berth.
    CHECK(boat_service_validate(&resident, &out, start, NULL) == BOAT_SERVICE_FAULT_END);
    CHECK(boat_service_state(&resident, &out, 499, &s) && s.owner == BOAT_OWNER_MOORED);
    CHECK(boat_service_state(&resident, &out, 500, &s) && s.owner == BOAT_OWNER_TRAVELLING);
    CHECK(boat_service_state(&resident, &out, 500 + total - 1, &s)
        && s.owner == BOAT_OWNER_TRAVELLING);
    CHECK(boat_service_state(&resident, &out, 500 + total, &s) && s.owner == BOAT_OWNER_MOORED);

    // A round trip that returns to its berth pose validates.
    BoatRoutePath loop4 = { .count = 4 };
    loop4.pieces[0] = line(0, 0, 0, -30, 1200, 60);
    loop4.pieces[1] = arc(10, -30, 10, M_PI, M_PI, 1200);
    loop4.pieces[2] = line(20, -30, 20, 0, 1200, 0);
    loop4.pieces[3] = arc(10, 0, 10, 0, M_PI, 1200);
    CHECK(boat_route_path_validate(&loop4, &total));
    CHECK(boat_service_validate(&resident, &loop4, start, NULL) == BOAT_SERVICE_OK);
    CHECK(boat_service_pose(&resident, &loop4, 0, &pose, NULL) && pose.z == 0);
    CHECK(boat_service_pose(&resident, &loop4, 500 + total + 99, &pose, NULL) && fabs(pose.x) < 1e-9
        && fabs(pose.z) < 1e-9);

    // Reservations: a resident reserves forever; visitors reserve their window; boundaries are
    // half-open so back-to-back windows do not conflict.
    uint64_t r0, r1, v0, v1;
    CHECK(boat_service_reservation(&resident, &out, &r0, &r1) && r0 == 0 && r1 == UINT64_MAX);
    CHECK(boat_service_reservation(&visitor, &path, &v0, &v1) && v0 == 1000 && v1 == 1000 + 2280);
    CHECK(boat_service_intervals_overlap(r0, r1, v0, v1));
    CHECK(!boat_service_intervals_overlap(0, 10, 10, 20));
    CHECK(boat_service_intervals_overlap(0, 11, 10, 20));

    // Loops and invalid kinds are rejected.
    BoatRoutePath looped = path;
    looped.loop = true;
    CHECK(!boat_service_state(&visitor, &looped, 0, &s));
    BoatServiceDefinition bad = { BOAT_SERVICE_KINDS, 0, 0 };
    CHECK(!boat_service_state(&bad, &path, 0, &s));
    return 0;
}
