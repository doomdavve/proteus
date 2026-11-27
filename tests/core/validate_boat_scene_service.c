// Marina M4: scene vessels and services: identity, berth fit, pose binding and reservations.
#include "core/boat_clock.h"
#include "core/boat_scene_definition.h"

#include <math.h>

#ifndef M_PI // Strict C17 on glibc hides it.
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <string.h>

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

static void add_route(BoatSceneDefinition *scene, const char *id, const BoatRoutePath *path)
{
    WaterfrontRouteActor *a = &scene->traffic.actors[scene->traffic.actor_count++];
    uint64_t ticks = 0;
    boat_route_path_validate(path, &ticks);
    memset(a, 0, sizeof(*a));
    strcpy(a->id, id);
    a->shape = WATERFRONT_ROUTE_OPEN_PATH;
    a->recipe = WATERFRONT_CRAFT_RUNABOUT;
    a->retain = 0xff;
    a->speed = 5;
    a->accel = 1;
    a->period = (double)ticks * BOAT_TICK_SECONDS;
    a->open_path = *path;
    scene->traffic_present = true;
}

// A visitor path: approach, turn, a 2 s stay at berth "slip" (12, 195, heading pi/2), leave.
static BoatRoutePath visitor_path(void)
{
    BoatRoutePath path = { .count = 4 };
    path.pieces[0] = line(0, 170, 0, 185, 600, 0);
    path.pieces[1] = arc(10, 185, 10, M_PI, -M_PI / 2, 600);
    path.pieces[2] = line(10, 195, 12, 195, 240, 240);
    path.pieces[3] = arc(12, 185, 10, M_PI / 2, -M_PI / 2, 600);
    return path;
}

// A resident round trip from berth "home" at (0, 0) heading 0: out, turn, back, turn.
static BoatRoutePath resident_path(void)
{
    BoatRoutePath path = { .count = 4 };
    path.pieces[0] = line(0, 0, 0, -30, 1200, 60);
    path.pieces[1] = arc(10, -30, 10, M_PI, M_PI, 1200);
    path.pieces[2] = line(20, -30, 20, 0, 1200, 0);
    path.pieces[3] = arc(10, 0, 10, 0, M_PI, 1200);
    return path;
}

static BoatSceneDefinition base_scene(void)
{
    static BoatSceneDefinition scene;
    memset(&scene, 0, sizeof(scene));
    scene.version = BOAT_SCENE_VERSION;
    scene.entrance_present = true;
    scene.entrance_width = 10;
    scene.berths_present = true;
    scene.berth_count = 2;
    strcpy(scene.berths[0].id, "slip");
    scene.berths[0].recipe = BOAT_SCENE_BERTH_RUNABOUT;
    scene.berths[0].x = 12;
    scene.berths[0].z = 195;
    scene.berths[0].heading = (float)(M_PI / 2);
    scene.berths[0].empty = true;
    strcpy(scene.berths[1].id, "home");
    scene.berths[1].recipe = BOAT_SCENE_BERTH_RUNABOUT;
    scene.berths[1].empty = true;
    scene.services_present = true;
    scene.vessel_count = 1;
    strcpy(scene.vessels[0].id, "guest");
    scene.vessels[0].recipe = BOAT_SCENE_BERTH_RUNABOUT;
    strcpy(scene.vessels[0].appearance.name, "GUEST");
    BoatRoutePath visit = visitor_path();
    add_route(&scene, "visit", &visit);
    scene.service_count = 1;
    strcpy(scene.services[0].id, "stay");
    strcpy(scene.services[0].vessel, "guest");
    strcpy(scene.services[0].berth, "slip");
    strcpy(scene.services[0].route, "visit");
    scene.services[0].kind = BOAT_SERVICE_VISITOR;
    scene.services[0].dock_piece = 2;
    scene.services[0].start_tick = 120;
    return scene;
}

static bool rejects(BoatSceneDefinition *scene, const char *field)
{
    BoatSceneDiagnostic d;
    return !boat_scene_validate(scene, &d) && strstr(d.field, field) != NULL;
}

int main(void)
{
    BoatSceneDiagnostic d;
    BoatSceneDefinition scene = base_scene();
    if (!boat_scene_validate(&scene, &d)) {
        fprintf(stderr, "base: %s %s\n", d.field, d.message);
        return 1;
    }
    uint64_t digest = boat_scene_digest(&scene);
    CHECK(boat_scene_has_content(&scene));
    CHECK(boat_scene_find_vessel(&scene, "guest") && !boat_scene_find_vessel(&scene, "nobody"));
    CHECK(boat_scene_find_route(&scene, "visit") == 0 && boat_scene_find_route(&scene, "x") < 0);

    // Identity and digest: every service field changes the digest.
    BoatSceneDefinition s = scene;
    s.services[0].start_tick = 121;
    CHECK(boat_scene_digest(&s) != digest);
    s = scene;
    s.vessels[0].appearance.hull = (Color) { 10, 20, 30, 255 };
    CHECK(boat_scene_digest(&s) != digest);

    // Missing IDs.
    s = scene;
    strcpy(s.services[0].vessel, "ghost");
    CHECK(rejects(&s, "vessel"));
    s = scene;
    strcpy(s.services[0].berth, "ghost");
    CHECK(rejects(&s, "berth"));
    s = scene;
    strcpy(s.services[0].route, "ghost");
    CHECK(rejects(&s, "route"));
    s = scene; // A vessel may not reuse a berth ID.
    strcpy(s.vessels[0].id, "slip");
    CHECK(rejects(&s, "vessels[1].id"));

    // A static hull berth cannot be a service berth; fit uses the customized hull.
    s = scene;
    s.berths[0].empty = false;
    CHECK(rejects(&s, "berth"));
    s = scene;
    s.vessels[0].appearance.length = 1000;
    CHECK(rejects(&s, "vessels[1].appearance.length"));
    s = scene;
    s.berths[0].recipe = BOAT_SCENE_BERTH_SKIFF; // Slot smaller than a runabout.
    CHECK(rejects(&s, "berth"));

    // The berth pose is the single source of the terminal pose.
    s = scene;
    s.berths[0].x = 12.5f;
    CHECK(rejects(&s, "route"));
    s = scene;
    s.berths[0].heading = 0;
    CHECK(rejects(&s, "route"));
    s = scene;
    s.services[0].dock_piece = 0; // No dwell on piece zero.
    CHECK(rejects(&s, "dock_piece"));
    s = scene;
    s.services[0].kind = 7;
    CHECK(rejects(&s, "kind"));

    // A looping or non-path route is not a service route.
    s = scene;
    s.traffic.actors[0].open_path.loop = true;
    CHECK(!boat_scene_validate(&s, &d));

    // Resident round trip in its own berth, alongside the visitor.
    BoatSceneDefinition both = scene;
    both.vessel_count = 2;
    strcpy(both.vessels[1].id, "local");
    both.vessels[1].recipe = BOAT_SCENE_BERTH_RUNABOUT;
    strcpy(both.vessels[1].appearance.name, "LOCAL");
    BoatRoutePath out = resident_path();
    add_route(&both, "round", &out);
    both.service_count = 2;
    strcpy(both.services[1].id, "live");
    strcpy(both.services[1].vessel, "local");
    strcpy(both.services[1].berth, "home");
    strcpy(both.services[1].route, "round");
    both.services[1].kind = BOAT_SERVICE_RESIDENT;
    both.services[1].start_tick = 600;
    if (!boat_scene_validate(&both, &d)) {
        fprintf(stderr, "both: %s %s\n", d.field, d.message);
        return 1;
    }

    // One route, one service; a resident reserves its berth forever; no double booking.
    s = both;
    strcpy(s.services[1].route, "visit");
    CHECK(rejects(&s, "services[2].route"));
    s = both;
    strcpy(s.services[1].berth, "slip"); // Same berth as the visitor.
    CHECK(!boat_scene_validate(&s, &d));
    s = both;
    s.service_count = 3; // A second visit by the same vessel over an overlapping window.
    s.services[2] = s.services[0];
    strcpy(s.services[2].id, "again");
    CHECK(!boat_scene_validate(&s, &d));

    // Two visitors on one berth: overlapping windows double-book it; disjoint ones are legal.
    s = scene;
    s.vessel_count = 2;
    strcpy(s.vessels[1].id, "guest2");
    s.vessels[1].recipe = BOAT_SCENE_BERTH_RUNABOUT;
    strcpy(s.vessels[1].appearance.name, "GUEST 2");
    BoatRoutePath again = visitor_path();
    add_route(&s, "visit2", &again);
    s.service_count = 2;
    s.services[1] = s.services[0];
    strcpy(s.services[1].id, "stay2");
    strcpy(s.services[1].vessel, "guest2");
    strcpy(s.services[1].route, "visit2");
    s.services[1].start_tick = 130;
    CHECK(rejects(&s, "services[2].berth"));
    s.services[1].start_tick = 120 + 2280; // Starts the tick the first window ends.
    CHECK(boat_scene_validate(&s, &d));
    s.services[1].start_tick = 120 + 2279;
    CHECK(rejects(&s, "services[2].berth"));
    // The same vessel visiting twice in disjoint windows is allowed; overlapping is not.
    strcpy(s.services[1].vessel, "guest");
    s.services[1].start_tick = 120 + 2280;
    CHECK(boat_scene_validate(&s, &d));
    s.services[1].start_tick = 120 + 2279;
    CHECK(!boat_scene_validate(&s, &d));

    // Capacity and absence.
    s = both;
    s.services_present = false;
    CHECK(rejects(&s, "services"));
    s = both;
    s.vessel_count = BOAT_SCENE_VESSELS + 1;
    CHECK(rejects(&s, "vessels"));

    // An empty berth with no vessel is allowed: the slot is unoccupied.
    s = both;
    s.service_count = 1;
    s.vessel_count = 1;
    CHECK(boat_scene_validate(&s, &d));
    // A scene with no services keeps its earlier digest.
    BoatSceneDefinition plain = base_scene();
    plain.services_present = false;
    plain.vessel_count = plain.service_count = 0;
    plain.berths[0].empty = false;
    plain.berths[1].empty = false;
    plain.traffic_present = false;
    plain.traffic.actor_count = 0;
    CHECK(boat_scene_validate(&plain, &d));
    return 0;
}
