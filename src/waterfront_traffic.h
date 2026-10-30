#ifndef PROTEUS_WATERFRONT_TRAFFIC_H
#define PROTEUS_WATERFRONT_TRAFFIC_H

#include "boat_model.h"
#include "boat_water.h"

#define WATERFRONT_TRAFFIC_VESSELS 4 // The river's circuits, on every mapped location.
// A3.5j: the outer map's upstream ferries and schooner (waterfront_upstream.h) follow them at
// indices WATERFRONT_TRAFFIC_VESSELS and up, active only where BoatWater.upstream is set.
#define WATERFRONT_TRAFFIC_ALL (WATERFRONT_TRAFFIC_VESSELS + 4)
#define WATERFRONT_TRAFFIC_TRIAL WATERFRONT_TRAFFIC_ALL // Contact index of the race trial ferry.
#define WATERFRONT_TRAFFIC_WAKE_SEGMENTS 8
#define WATERFRONT_TRAFFIC_WAKES (WATERFRONT_TRAFFIC_VESSELS * WATERFRONT_TRAFFIC_WAKE_SEGMENTS * 2)
#define WATERFRONT_TRAFFIC_WAKE_CAPACITY (WATERFRONT_TRAFFIC_ALL * 16 * 2 + 32)
#define WATERFRONT_TRAFFIC_LIGHTS 3

typedef struct {
    Vector3 position, velocity; // Renderer/world X/Z, rest waterline Y=0.
    float heading, yaw_rate; // Model bow -Z; same heading as the reflected-X simulation.
} WaterfrontVesselMotion;

typedef struct {
    int id;
    Transform3D pose;
    WaterfrontVesselMotion motion;
    Vector3 lights[WATERFRONT_TRAFFIC_LIGHTS]; // port, starboard, stern
} WaterfrontVesselState;

typedef struct {
    Transform3D pose; // Shared unit plane, X across the trail, Z along it.
    Color color;
} WaterfrontTrafficWake;

struct WaterfrontRouteDefinition;

typedef struct {
    int vessel_count; // WATERFRONT_TRAFFIC_VESSELS, or WATERFRONT_TRAFFIC_ALL upstream.
    WaterfrontVesselState vessels[WATERFRONT_TRAFFIC_ALL];
    bool lift; // Upstream: Hisingsbron's lift span, at LIFT_POSE (waterfront_upstream.h).
    Transform3D lift_pose;
    int wake_count;
    WaterfrontTrafficWake wakes[WATERFRONT_TRAFFIC_WAKE_CAPACITY];
    // L3.1: borrowed accepted route definition that produced this snapshot; NULL is compiled.
    const struct WaterfrontRouteDefinition *routes;
} WaterfrontTrafficFrame;

typedef struct {
    int vessel;
    Vector2 normal, velocity, local; // Simulation X/Z; contact point local to player hull.
    float penetration;
} WaterfrontTrafficContact;

// INDEX runs to WATERFRONT_TRAFFIC_ALL; the upstream vessels' entries are valid everywhere,
// but only upstream worlds evaluate, draw and collide with them.
BoatModelSpec waterfront_traffic_spec(int index);
const Mesh3D *waterfront_traffic_mesh(int index);
// L3.1: the same queries against a caller-owned definition; NULL selects compiled defaults.
BoatModelSpec waterfront_traffic_spec_routes(const struct WaterfrontRouteDefinition *routes, int index);
const Mesh3D *waterfront_traffic_mesh_routes(const struct WaterfrontRouteDefinition *routes, int index);
// Closed authored routes; negative time supports wakes across the start and lap boundary.
WaterfrontVesselMotion waterfront_traffic_motion(int index, double time);
WaterfrontVesselMotion waterfront_traffic_motion_routes(
    const struct WaterfrontRouteDefinition *routes, int index, double time);
double waterfront_traffic_period(int index); // Exact route period in seconds.
double waterfront_traffic_period_routes(const struct WaterfrontRouteDefinition *routes, int index);
void waterfront_traffic_evaluate(BoatWater water, double time, WaterfrontTrafficFrame *frame);
void waterfront_traffic_evaluate_routes(const struct WaterfrontRouteDefinition *routes,
    BoatWater water, double time, WaterfrontTrafficFrame *frame);
int waterfront_traffic_actor_count(const struct WaterfrontRouteDefinition *routes);
// Opt-in race fixture: an independent four-emitter pool seeded from 16 seconds of actual
// scenery motion. Advance once per fixed tick before boat physics; ordinary traffic is unchanged.
bool waterfront_traffic_wakes_init(BoatWakeObjects *wakes, double time);
bool waterfront_traffic_wakes_step(BoatWakeObjects *wakes, double time);
// Full rectangle SAT against moving, padded traffic hulls. Traffic follows its route;
// only the player responds. No basin traffic or traffic AI. The height-aware variant also
// checks WATER's opt-in authored ferry hull (vessel WATERFRONT_TRAFFIC_TRIAL) and, where
// WATER.upstream is set, the upstream vessels.
bool waterfront_traffic_contact(Vector2 position, float heading, float bow, float stern,
    float half_beam, double time, WaterfrontTrafficContact *contact);
bool waterfront_traffic_contact_height(Vector2 position, float heading, float bow, float stern,
    float half_beam, float bottom, float top, BoatWater water, double time,
    WaterfrontTrafficContact *contact);
bool waterfront_traffic_camera_clear(Vector3 position, double time, float clearance);

#endif
