#include "core/waterfront_service.h"

#include "core/boat_clock.h"

#include <math.h>
#include <string.h>

uint64_t waterfront_service_tick(double time)
{
    if (!(time > 0))
        return 0;
    double scaled = time * (double)BOAT_TICK_HZ;
    if (!isfinite(scaled) || scaled >= 1.8e19)
        return UINT64_MAX;
    // Below 2^52, tick/120 can round one ULP below its integer on multiplication.
    double whole = floor(fabs(scaled) < 0x1p52 ? nextafter(scaled, INFINITY) : scaled);
    return (uint64_t)whole;
}

int waterfront_service_for_route(const BoatSceneDefinition *scene, unsigned actor)
{
    if (!scene || !scene->services_present || actor >= scene->traffic.actor_count)
        return -1;
    for (unsigned i = 0; i < scene->service_count && i < BOAT_SCENE_SERVICES; ++i)
        if (boat_scene_find_route(scene, scene->services[i].route) == (int)actor)
            return (int)i;
    return -1;
}

unsigned waterfront_service_hull_slots(const BoatSceneDefinition *scene)
{
    if (!scene || !scene->berths_present)
        return 0;
    unsigned n = 0;
    for (unsigned i = 0; i < scene->berth_count && i < BOAT_SCENE_BERTHS; ++i)
        n += !scene->berths[i].empty;
    return scene->services_present ? n + scene->vessel_count : n;
}

bool waterfront_service_hull(
    const BoatSceneDefinition *scene, unsigned index, double time, WaterfrontServiceHull *hull)
{
    if (!scene || !hull || !scene->services_present || index >= scene->vessel_count
        || index >= BOAT_SCENE_VESSELS)
        return false;
    const BoatSceneVessel *vessel = &scene->vessels[index];
    uint64_t tick = waterfront_service_tick(time);
    for (unsigned i = 0; i < scene->service_count && i < BOAT_SCENE_SERVICES; ++i) {
        const BoatSceneService *s = &scene->services[i];
        int route = boat_scene_find_route(scene, s->route);
        const BoatSceneBerth *berth = boat_scene_find_berth(scene, s->berth);
        if (strncmp(s->vessel, vessel->id, BOAT_SCENE_TEXT) || route < 0 || !berth)
            continue;
        const BoatRoutePath *path = &scene->traffic.actors[route].open_path;
        BoatServiceDefinition def = { (BoatServiceKind)s->kind, s->start_tick, s->dock_piece };
        BoatServiceState state;
        if (!boat_service_state(&def, path, tick, &state) || state.owner == BOAT_OWNER_ABSENT)
            continue;
        BoatRoutePose pose;
        double yaw = 0;
        if (!boat_service_pose(&def, path, tick, &pose, &yaw))
            continue;
        float heading = (float)pose.heading, speed = (float)pose.speed;
        *hull = (WaterfrontServiceHull) { .present = true,
            .owner = state.owner,
            .service = i,
            .position = { (float)-pose.x, 0, (float)pose.z },
            .velocity = { -sinf(heading) * speed, 0, -cosf(heading) * speed },
            .heading = heading,
            .yaw_rate = (float)yaw,
            .spec = boat_scene_vessel_resolve(vessel),
            .recipe = vessel->recipe };
        if (state.owner == BOAT_OWNER_MOORED) // Exactly the berth pose; no residual motion.
            hull->velocity = (Vector3) { 0 }, hull->yaw_rate = 0;
        return true;
    }
    return false;
}
