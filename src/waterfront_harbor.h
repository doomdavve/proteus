#ifndef PROTEUS_WATERFRONT_HARBOR_H
#define PROTEUS_WATERFRONT_HARBOR_H

#include "boat_model.h"
#include "boat_water.h"

#define WATERFRONT_HARBOR_BOATS 7
#define WATERFRONT_HARBOR_LINES (WATERFRONT_HARBOR_BOATS * 4 + 6)

typedef struct {
    BoatModelKind kind;
    Vector3 position; // Renderer coordinates; fixed mooring centre, Y=0.
    float heading;
} WaterfrontBerth;

typedef struct {
    Transform3D boats[WATERFRONT_HARBOR_BOATS];
    Transform3D pontoon;
    Transform3D gangway;
    Transform3D quay; // Places the quay mesh; identity on the river's 3.5 m ground.
    int line_count;
    Transform3D lines[WATERFRONT_HARBOR_LINES];
} WaterfrontHarborFrame;

const WaterfrontBerth *waterfront_harbor_berths(void);
BoatModelSpec waterfront_harbor_spec(int index);
const Mesh3D *waterfront_harbor_boat_mesh(int index);
const Mesh3D *waterfront_harbor_pontoon_mesh(void);
const Mesh3D *waterfront_harbor_quay_mesh(void);
// Stateless shared pose/rope evaluation. Flat water stays still; waves use boat_water_query.
void waterfront_harbor_evaluate(BoatWater water, double time, WaterfrontHarborFrame *frame);
// The same with the gangway's shore end and the quay gear set on GROUND, the land height under
// renderer X/Z, instead of the river's 3.5 m ground; NULL is waterfront_harbor_evaluate.
void waterfront_harbor_evaluate_ground(
    BoatWater water, double time, float (*ground)(float x, float z), WaterfrontHarborFrame *frame);
Vector3 waterfront_harbor_attachment(Transform3D pose, Vector3 local);
// Stationary conservative water-plane envelopes, including outboards and wave excursion.
// Inputs/normal are simulation X/Z, as in boat_map_water_contact. True means blocked.
bool waterfront_harbor_contact(Vector2 position, float clearance, Vector2 *normal);
bool waterfront_harbor_contact_height(Vector2 position, float clearance, float bottom, float top,
    BoatWater water, double time, Vector2 *normal);
bool waterfront_harbor_camera_clear(Vector3 position, float clearance);

#endif
