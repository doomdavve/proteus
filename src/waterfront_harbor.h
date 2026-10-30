#ifndef PROTEUS_WATERFRONT_HARBOR_H
#define PROTEUS_WATERFRONT_HARBOR_H

#include "boat_model.h"
#include "boat_scene_definition.h"
#include "boat_water.h"

#define WATERFRONT_HARBOR_BOATS 7
#define WATERFRONT_FERRY_LINES 4
#define WATERFRONT_HARBOR_LINES                                                                    \
    (WATERFRONT_HARBOR_BOATS * 4 + BOAT_SCENE_MODULES * 6 + WATERFRONT_FERRY_LINES * 2)

typedef struct {
    char id[BOAT_SCENE_TEXT]; // Compiled IDs are for diagnostics and parity; authored IDs are
                              // stable.
    BoatModelKind kind;
    unsigned char recipe; // BoatSceneBerthRecipe; selects the registered generated mesh.
    unsigned char mooring; // BoatSceneMooring.
    Vector3 position; // Renderer coordinates; fixed mooring centre, Y=0.
    float heading;
} WaterfrontBerth;

// The docked Gothenburg-Frederikshavn ro-pax ferry across the river from the Eriksberg crane:
// an authored berth (renderer coordinates; heading as WaterfrontBerth) at Stena Danica's size.
typedef struct {
    Vector3 position;
    float heading, length, beam, draft, height; // Draft below and height above the waterline.
} WaterfrontFerry;

typedef struct {
    unsigned char kind; // BoatSceneModuleKind
    Transform3D pose; // Pontoon/quay mesh or gangway box.
} WaterfrontModuleFrame;

typedef struct {
    float length, beam, height;
} WaterfrontModuleSpec;

typedef struct {
    Transform3D boats[WATERFRONT_HARBOR_BOATS];
    unsigned char recipes[WATERFRONT_HARBOR_BOATS]; // Mesh recipe per drawn boat.
    int berth_count;
    Transform3D ferry;
    // Compiled dock furniture (NULL/length-zero authored-module scenes use modules instead).
    Transform3D pontoon;
    Transform3D gangway;
    Transform3D quay; // Places the quay mesh; identity on the river's 3.5 m ground.
    // L2.3 authored modules. module_count > 0 means the compiled dock parts are not drawn.
    int module_count;
    WaterfrontModuleFrame modules[BOAT_SCENE_MODULES];
    int line_count;
    Transform3D lines[WATERFRONT_HARBOR_LINES];
} WaterfrontHarborFrame;

const WaterfrontBerth *waterfront_harbor_berths(void);
unsigned waterfront_harbor_berth_count(const BoatSceneDefinition *scene);
// Convert one accepted scene berth to renderer coordinates. NULL SCENE uses the compiled list.
// INDEX beyond the selected count returns a zeroed berth with an empty ID.
WaterfrontBerth waterfront_harbor_berth_at(const BoatSceneDefinition *scene, unsigned index);
BoatModelSpec waterfront_harbor_berth_spec(const WaterfrontBerth *berth);
BoatModelSpec waterfront_harbor_spec(int index); // Compiled index wrapper.
const Mesh3D *waterfront_harbor_recipe_mesh(unsigned recipe);
const Mesh3D *waterfront_harbor_boat_mesh(int index); // Compiled index wrapper.
const Mesh3D *waterfront_harbor_pontoon_mesh(void);
const Mesh3D *waterfront_harbor_quay_mesh(void);
// L2.3 registered module recipes.
WaterfrontModuleSpec waterfront_harbor_module_spec(unsigned kind);
const Mesh3D *waterfront_harbor_module_mesh(unsigned kind);
const WaterfrontFerry *waterfront_harbor_ferry(void);
const Mesh3D *waterfront_harbor_ferry_mesh(void);
// Stateless shared pose/rope evaluation. Flat water stays still; waves use boat_water_query.
void waterfront_harbor_evaluate(BoatWater water, double time, WaterfrontHarborFrame *frame);
// The same with the gangway's shore end and the quay gear set on GROUND, the land height under
// renderer X/Z, instead of the river's 3.5 m ground; NULL is waterfront_harbor_evaluate.
void waterfront_harbor_evaluate_ground(
    BoatWater water, double time, float (*ground)(float x, float z), WaterfrontHarborFrame *frame);
// L2.1: evaluate the caller-owned berth list. NULL SCENE keeps the compiled seven hulls; an
// authored berths subsystem replaces the whole compiled hull collection.
void waterfront_harbor_evaluate_scene(const BoatSceneDefinition *scene, BoatWater water,
    double time, float (*ground)(float x, float z), WaterfrontHarborFrame *frame);
Vector3 waterfront_harbor_attachment(Transform3D pose, Vector3 local);
// Stationary conservative water-plane envelopes, including outboards and wave excursion.
// Inputs/normal are simulation X/Z, as in boat_map_water_contact. True means blocked.
bool waterfront_harbor_contact(Vector2 position, float clearance, Vector2 *normal);
bool waterfront_harbor_contact_height(Vector2 position, float clearance, float bottom, float top,
    BoatWater water, double time, Vector2 *normal);
bool waterfront_harbor_contact_height_scene(const BoatSceneDefinition *scene, Vector2 position,
    float clearance, float bottom, float top, BoatWater water, double time, Vector2 *normal);
bool waterfront_harbor_camera_clear(Vector3 position, float clearance);
bool waterfront_harbor_camera_clear_scene(
    const BoatSceneDefinition *scene, Vector3 position, float clearance);

#endif
