#ifndef PROTEUS_WATERFRONT_TRAFFIC_H
#define PROTEUS_WATERFRONT_TRAFFIC_H

#include "boat_model.h"
#include "boat_water.h"

#define WATERFRONT_TRAFFIC_VESSELS 4
#define WATERFRONT_TRAFFIC_WAKE_SEGMENTS 8
#define WATERFRONT_TRAFFIC_WAKES (WATERFRONT_TRAFFIC_VESSELS * WATERFRONT_TRAFFIC_WAKE_SEGMENTS * 2)
#define WATERFRONT_TRAFFIC_WAKE_CAPACITY (WATERFRONT_TRAFFIC_VESSELS * 16 * 2 + 32)
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

typedef struct {
    WaterfrontVesselState vessels[WATERFRONT_TRAFFIC_VESSELS];
    int wake_count;
    WaterfrontTrafficWake wakes[WATERFRONT_TRAFFIC_WAKE_CAPACITY];
} WaterfrontTrafficFrame;

typedef struct {
    int vessel;
    Vector2 normal, velocity, local; // Simulation X/Z; contact point local to player hull.
    float penetration;
} WaterfrontTrafficContact;

BoatModelSpec waterfront_traffic_spec(int index);
const Mesh3D *waterfront_traffic_mesh(int index);
// Closed authored routes; negative time supports wakes across the start and lap boundary.
WaterfrontVesselMotion waterfront_traffic_motion(int index, double time);
double waterfront_traffic_period(int index); // Exact route period in seconds.
void waterfront_traffic_evaluate(BoatWater water, double time, WaterfrontTrafficFrame *frame);
// Opt-in race fixture: an independent four-emitter pool seeded from 16 seconds of actual
// scenery motion. Advance once per fixed tick before boat physics; ordinary traffic is unchanged.
bool waterfront_traffic_wakes_init(BoatWakeObjects *wakes, double time);
bool waterfront_traffic_wakes_step(BoatWakeObjects *wakes, double time);
// Full rectangle SAT against moving, padded traffic hulls. Traffic follows its route;
// only the player responds. No basin traffic or traffic AI. The height-aware variant also
// checks WATER's opt-in authored ferry hull.
bool waterfront_traffic_contact(Vector2 position, float heading, float bow, float stern,
    float half_beam, double time, WaterfrontTrafficContact *contact);
bool waterfront_traffic_contact_height(Vector2 position, float heading, float bow, float stern,
    float half_beam, float bottom, float top, BoatWater water, double time,
    WaterfrontTrafficContact *contact);
bool waterfront_traffic_camera_clear(Vector3 position, double time, float clearance);

#endif
