#ifndef PROTEUS_WATERFRONT_SERVICE_H
#define PROTEUS_WATERFRONT_SERVICE_H

// Marina M4: the single evaluation of a service vessel's hull at a time. Harbor frames (drawing
// and ropes), traffic contacts and camera exclusion all call this, so a vessel has one owner,
// one pose and one spec at every instant and is never drawn or contacted twice. Core only.
#include "core/boat_model.h"
#include "core/boat_scene_definition.h"
#include "core/boat_service.h"

typedef struct {
    bool present; // False while absent: no hull, contact, camera exclusion or wake.
    BoatOwner owner; // MOORED at the service berth, or TRAVELLING its route.
    unsigned service; // Index of the controlling service.
    Vector3 position, velocity; // Renderer coordinates; rest waterline Y=0.
    float heading, yaw_rate;
    BoatModelSpec spec; // The vessel's resolved appearance.
    unsigned char recipe; // BoatSceneBerthRecipe, for the registered mesh.
} WaterfrontServiceHull;

// Simulation tick the evaluators use for TIME seconds. Same rounding as authored path routes.
uint64_t waterfront_service_tick(double time);
// Vessel INDEX of SCENE at TIME. False for a bad index or while absent; HULL is then not written.
bool waterfront_service_hull(
    const BoatSceneDefinition *scene, unsigned index, double time, WaterfrontServiceHull *hull);
// Index of the service whose route is traffic actor ACTOR, or -1. Such an actor only provides a
// path: traffic frames neither draw it nor give it contacts or wakes.
int waterfront_service_for_route(const BoatSceneDefinition *scene, unsigned actor);
// Berths holding static hulls (not empty), plus vessels: the harbor frame's hull slot count.
unsigned waterfront_service_hull_slots(const BoatSceneDefinition *scene);

#endif
