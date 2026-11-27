// Marina M4: one hull owner per service vessel across harbor frame, traffic frame, contacts and
// camera, at exact ticks around every handoff.
#include "core/boat_clock.h"
#include "core/waterfront_harbor.h"
#include "core/waterfront_service.h"
#include "core/waterfront_traffic.h"

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

static BoatSceneDefinition *scene_with_visitor(void)
{
    static BoatSceneDefinition scene;
    memset(&scene, 0, sizeof(scene));
    scene.version = BOAT_SCENE_VERSION;
    scene.entrance_present = true;
    scene.entrance_width = 10;
    scene.berths_present = true;
    scene.modules_present = true; // Authored (empty): no compiled dock fixtures at the slip.
    scene.berth_count = 2;
    // A static hull first, then the empty slot the visitor uses: slots are not berth indices.
    strcpy(scene.berths[0].id, "fixed");
    scene.berths[0].recipe = BOAT_SCENE_BERTH_SKIFF;
    scene.berths[0].x = 60;
    scene.berths[0].z = 190;
    strcpy(scene.berths[0].appearance.name, "FIXED");
    strcpy(scene.berths[1].id, "slip");
    scene.berths[1].recipe = BOAT_SCENE_BERTH_RUNABOUT;
    scene.berths[1].x = 12;
    scene.berths[1].z = 195;
    scene.berths[1].heading = (float)(M_PI / 2);
    scene.berths[1].empty = true;
    scene.services_present = true;
    scene.vessel_count = 1;
    strcpy(scene.vessels[0].id, "guest");
    scene.vessels[0].recipe = BOAT_SCENE_BERTH_RUNABOUT;
    strcpy(scene.vessels[0].appearance.name, "GUEST");
    BoatRoutePath path = { .count = 4 };
    path.pieces[0] = line(0, 170, 0, 185, 600, 0);
    path.pieces[1] = arc(10, 185, 10, M_PI, -M_PI / 2, 600);
    path.pieces[2] = line(10, 195, 12, 195, 240, 240);
    path.pieces[3] = arc(12, 185, 10, M_PI / 2, -M_PI / 2, 600);
    uint64_t ticks;
    boat_route_path_validate(&path, &ticks);
    WaterfrontRouteActor *a = &scene.traffic.actors[scene.traffic.actor_count++];
    strcpy(a->id, "visit");
    a->shape = WATERFRONT_ROUTE_OPEN_PATH;
    a->recipe = WATERFRONT_CRAFT_RUNABOUT;
    a->retain = 0xff;
    a->speed = 5;
    a->accel = 1;
    a->period = (double)ticks * BOAT_TICK_SECONDS;
    a->open_path = path;
    scene.traffic_present = true;
    scene.service_count = 1;
    strcpy(scene.services[0].id, "stay");
    strcpy(scene.services[0].vessel, "guest");
    strcpy(scene.services[0].berth, "slip");
    strcpy(scene.services[0].route, "visit");
    scene.services[0].kind = BOAT_SERVICE_VISITOR;
    scene.services[0].dock_piece = 2;
    scene.services[0].start_tick = 120;
    return &scene;
}

static int traffic_hits(const BoatSceneDefinition *scene, double time, Vector2 sim_position)
{
    WaterfrontTrafficContact c;
    return waterfront_traffic_contact_height_scene(scene, sim_position, (float)(M_PI / 2), 2.5f,
        2.5f, 1, -INFINITY, INFINITY, (BoatWater) { 0 }, time, &c);
}

int main(void)
{
    BoatSceneDefinition *scene = scene_with_visitor();
    BoatSceneDiagnostic d;
    if (!boat_scene_validate(scene, &d)) {
        fprintf(stderr, "scene: %s %s\n", d.field, d.message);
        return 1;
    }
    CHECK(waterfront_service_hull_slots(scene) == 2); // The static hull and the vessel.
    CHECK(waterfront_service_for_route(scene, 0) == 0);
    CHECK(waterfront_service_tick(1.0 + 1e-12) == 120 && waterfront_service_tick(-3) == 0);

    const double dock_start = (120 + 1440) * BOAT_TICK_SECONDS; // First moored tick.
    const double before = dock_start - BOAT_TICK_SECONDS, after = dock_start;
    const double leave = (120 + 1680) * BOAT_TICK_SECONDS;
    const double gone = (120 + 2280) * BOAT_TICK_SECONDS;
    const struct {
        double time;
        BoatOwner owner;
        bool present;
    } expect[] = { { 119 * BOAT_TICK_SECONDS, BOAT_OWNER_ABSENT, false },
        { 120 * BOAT_TICK_SECONDS, BOAT_OWNER_TRAVELLING, true },
        { before, BOAT_OWNER_TRAVELLING, true }, { after, BOAT_OWNER_MOORED, true },
        { leave - BOAT_TICK_SECONDS, BOAT_OWNER_MOORED, true },
        { leave, BOAT_OWNER_TRAVELLING, true },
        { gone - BOAT_TICK_SECONDS, BOAT_OWNER_TRAVELLING, true },
        { gone, BOAT_OWNER_ABSENT, false } };
    for (unsigned i = 0; i < sizeof(expect) / sizeof(expect[0]); ++i) {
        double t = expect[i].time;
        WaterfrontServiceHull hull;
        bool present = waterfront_service_hull(scene, 0, t, &hull);
        CHECK(present == expect[i].present);
        CHECK(!present || hull.owner == expect[i].owner);

        // Exactly one drawn hull for the vessel, in the compact slot after the static hull.
        WaterfrontHarborFrame harbor;
        waterfront_harbor_evaluate_scene(scene, (BoatWater) { 0 }, t, NULL, &harbor);
        CHECK(harbor.berth_count == 2 && harbor.visible[0] && harbor.visible[1] == present);

        // The serviced actor is not drawn as traffic; its hull still has contacts and camera
        // exclusion exactly while the vessel is present.
        WaterfrontTrafficFrame traffic;
        waterfront_traffic_evaluate_scene(scene, (BoatWater) { 0 }, t, &traffic);
        CHECK(traffic.vessel_count == 1 && traffic.hidden[0]);
        if (present) {
            Vector2 sim = { -hull.position.x, hull.position.z };
            CHECK(traffic_hits(scene, t, sim));
            Vector3 p = { hull.position.x, 1, hull.position.z };
            CHECK(!waterfront_traffic_camera_clear_scene(scene, p, t, .3f));
        } else {
            CHECK(!traffic_hits(scene, t, (Vector2) { 12, 195 }));
            CHECK(!traffic_hits(scene, t, (Vector2) { 0, 170 }));
            CHECK(waterfront_traffic_camera_clear_scene(scene, (Vector3) { -12, 1, 195 }, t, .3f));
        }
    }

    // History is emitted by the service hull, not the hidden traffic actor, and survives
    // docking and visitor disappearance before expiring after the finite 5.6-second window.
    WaterfrontTrafficFrame trails, repeat;
    waterfront_traffic_evaluate_scene(scene, (BoatWater) { 0 }, 0, &trails);
    CHECK(trails.wake_count == 0);
    waterfront_traffic_evaluate_scene(scene, (BoatWater) { 0 }, 4, &trails);
    CHECK(trails.wake_count > 0 && trails.wake_count == trails.service_wake_count[0]);
    CHECK(trails.wake_count <= WATERFRONT_TRAFFIC_WAKE_SEGMENTS * 2);
    waterfront_traffic_evaluate_scene(scene, (BoatWater) { 0 }, after, &trails);
    CHECK(trails.service_wake_count[0] > 0);
    waterfront_traffic_evaluate_scene(scene, (BoatWater) { 0 }, gone, &trails);
    CHECK(trails.service_wake_count[0] > 0);
    waterfront_traffic_evaluate_scene(scene, (BoatWater) { 0 }, gone + 5.7, &trails);
    CHECK(trails.wake_count == 0);
    waterfront_traffic_evaluate_scene(scene, (BoatWater) { 0 }, 4, &trails);
    waterfront_traffic_evaluate_scene(scene, (BoatWater) { 0 }, 4, &repeat);
    CHECK(trails.wake_count == repeat.wake_count);
    for (int i = 0; i < trails.wake_count; ++i) {
        CHECK(trails.wakes[i].pose.position.x == repeat.wakes[i].pose.position.x);
        CHECK(trails.wakes[i].pose.position.z == repeat.wakes[i].pose.position.z);
        CHECK(trails.wakes[i].pose.rotation.y == repeat.wakes[i].pose.rotation.y);
        CHECK(trails.wakes[i].pose.scale.z == repeat.wakes[i].pose.scale.z);
    }

    // Every historical interval intersecting even a one-tick dwell is rejected. The long
    // dock dwell also emits nothing once all earlier travelling history has expired.
    BoatSceneDefinition stopped = *scene;
    stopped.traffic.actors[0].open_path.pieces[0].dwell_ticks = 1;
    waterfront_traffic_evaluate_scene(&stopped, (BoatWater) { 0 }, 6.3, &trails);
    waterfront_traffic_evaluate_scene(scene, (BoatWater) { 0 }, 6.3, &repeat);
    CHECK(trails.wake_count < repeat.wake_count);
    stopped.traffic.actors[0].open_path.pieces[2].dwell_ticks = 1200;
    waterfront_traffic_evaluate_scene(&stopped, (BoatWater) { 0 }, after + 6, &trails);
    CHECK(trails.wake_count == 0);

    // Four service vessels plus four ordinary actors fit the unchanged shared quad pool.
    BoatSceneDefinition maximum = *scene;
    maximum.vessel_count = maximum.service_count = 4;
    maximum.berth_count = 5;
    maximum.traffic.actor_count = 8;
    for (unsigned i = 1; i < 4; ++i) {
        maximum.vessels[i] = scene->vessels[0];
        maximum.services[i] = scene->services[0];
        maximum.berths[i + 1] = scene->berths[1];
        maximum.traffic.actors[i] = scene->traffic.actors[0];
        snprintf(maximum.vessels[i].id, BOAT_SCENE_TEXT, "guest%u", i);
        snprintf(maximum.services[i].id, BOAT_SCENE_TEXT, "stay%u", i);
        snprintf(maximum.services[i].vessel, BOAT_SCENE_TEXT, "guest%u", i);
        snprintf(maximum.services[i].route, BOAT_SCENE_TEXT, "visit%u", i);
        snprintf(maximum.services[i].berth, BOAT_SCENE_TEXT, "slip%u", i);
        snprintf(maximum.berths[i + 1].id, BOAT_SCENE_TEXT, "slip%u", i);
        snprintf(maximum.traffic.actors[i].id, BOAT_SCENE_TEXT, "visit%u", i);
    }
    for (unsigned i = 4; i < 8; ++i) {
        maximum.traffic.actors[i] = scene->traffic.actors[0];
        snprintf(maximum.traffic.actors[i].id, BOAT_SCENE_TEXT, "ordinary%u", i);
    }
    CHECK(boat_scene_validate(&maximum, &d));
    waterfront_traffic_evaluate_scene(&maximum, (BoatWater) { 0 }, 4, &trails);
    CHECK(trails.wake_count <= WATERFRONT_TRAFFIC_WAKE_CAPACITY);
    WaterfrontRouteDefinition ordinary = maximum.traffic;
    ordinary.actor_count = 4;
    for (unsigned i = 0; i < 4; ++i)
        ordinary.actors[i] = maximum.traffic.actors[i + 4];
    waterfront_traffic_evaluate_routes(&ordinary, (BoatWater) { 0 }, 4, &repeat);
    CHECK(trails.wake_count == 4 * trails.service_wake_count[0] + repeat.wake_count);
    for (unsigned i = 0; i < 4; ++i)
        CHECK(trails.service_wake_count[i] > 0 && trails.hidden[i]);
    for (int i = 0; i < trails.wake_count; ++i)
        CHECK(isfinite(trails.wakes[i].pose.position.x) && isfinite(trails.wakes[i].pose.scale.z));

    // Stern geometry uses the vessel's appearance, even if its path actor uses another recipe.
    BoatSceneDefinition wider = *scene;
    wider.vessels[0].appearance.beam = 3;
    waterfront_traffic_evaluate_scene(scene, (BoatWater) { 0 }, 4, &trails);
    waterfront_traffic_evaluate_scene(&wider, (BoatWater) { 0 }, 4, &repeat);
    CHECK(trails.wake_count == repeat.wake_count && trails.wake_count > 0);
    CHECK(trails.wakes[0].pose.position.x != repeat.wakes[0].pose.position.x);

    // The handoff does not move the hull: the last travelling tick sits within a few millimetres
    // of the berth pose that the moored tick reports exactly.
    WaterfrontServiceHull a, b;
    CHECK(waterfront_service_hull(scene, 0, before, &a));
    CHECK(waterfront_service_hull(scene, 0, after, &b));
    CHECK(hypotf(a.position.x - b.position.x, a.position.z - b.position.z) < .05f);
    CHECK(fabsf(b.position.x + 12) < .001f && fabsf(b.position.z - 195) < .001f);
    CHECK(b.velocity.x == 0 && b.velocity.z == 0 && b.yaw_rate == 0);

    // Direct seeks equal stepped evaluation, in both directions.
    for (int i = 0; i < 40; ++i) {
        double t = 3 + i * 0.37, u = 3 + (39 - i) * 0.37;
        WaterfrontServiceHull h1, h2, h3;
        bool p1 = waterfront_service_hull(scene, 0, t, &h1);
        waterfront_service_hull(scene, 0, u, &h3); // Seek elsewhere and come back.
        bool p2 = waterfront_service_hull(scene, 0, t, &h2);
        CHECK(p1 == p2 && (!p1 || (h1.position.x == h2.position.x && h1.heading == h2.heading)));
    }

    // The empty berth owns no static contact and no camera exclusion while the vessel is away.
    Vector2 slip = { 12, 195 };
    CHECK(!waterfront_harbor_contact_height_scene(
        scene, slip, .3f, -INFINITY, INFINITY, (BoatWater) { 0 }, 0, NULL));
    // Marking the same berth as a static hull makes it a contact: emptiness is what frees it.
    BoatSceneDefinition filled = *scene;
    filled.berths[1].empty = false;
    CHECK(waterfront_harbor_contact_height_scene(
        &filled, slip, .3f, -INFINITY, INFINITY, (BoatWater) { 0 }, 0, NULL));
    return 0;
}
