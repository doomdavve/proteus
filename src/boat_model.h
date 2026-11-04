#ifndef PROTEUS_BOAT_MODEL_H
#define PROTEUS_BOAT_MODEL_H

#include "geometry3d.h"

// Visual recipes in metres, Y up, bow toward -Z, rest waterline at Y=0.
// These do not prescribe performance, physics, prices, or an upgrade progression.
typedef enum {
    BOAT_MODEL_SKIFF,
    BOAT_MODEL_RUNABOUT,
    BOAT_MODEL_CRUISER,
    BOAT_MODEL_SAILBOAT,
    BOAT_MODEL_WORKBOAT,
    BOAT_MODEL_SPEEDBOAT,
    BOAT_MODEL_FERRY,
    BOAT_MODEL_KIND_COUNT
} BoatModelKind;

#define BOAT_MODEL_NAME_BYTES 17 // Optional painted ASCII name, including the terminator.

typedef struct {
    float length, beam, draft, freeboard;
    float bow_width, stern_width; // Fractions of beam; bow .02..0.45, stern .5..1.
    float cabin_length, cabin_height, cabin_offset; // Metres; zero height omits cabin.
    float windshield_rake; // 0..0.6, aft displacement per metre of height.
    float mast_height; // Zero omits mast and furled sail.
    int engines; // 0..2 outboards.
    bool work_deck, fenders, passenger_deck;
    Color hull, deck, trim;
    char name[BOAT_MODEL_NAME_BYTES]; // Empty omits lettering; see boat_model_name_valid.
} BoatModelSpec;

#define BOAT_MODEL_MAX_TRIANGLES 1024
// Caller-owned fixed storage. No allocation or random state; rebuild after changing a recipe.
// Do not copy an initialized model: its Mesh3D view points into its own arrays.
typedef struct {
    Vector3 vertices[BOAT_MODEL_MAX_TRIANGLES * 3];
    Triangle3D triangles[BOAT_MODEL_MAX_TRIANGLES];
    Mesh3D mesh;
    bool overflow;
} BoatModel;

// Shared XYZ pose convention for hulls, attachments, lines, and water-relative dressing.
Vector3 boat_model_rotation(Matrix rotation);
Vector3 boat_model_point(Transform3D pose, Vector3 local);

// Conservative transformed hull/cabin/mast bounds, shared by height-aware contacts.
void boat_model_vertical_bounds(BoatModelSpec spec, Transform3D pose, float *bottom, float *top);
BoatModelSpec boat_model_preset(BoatModelKind kind);
bool boat_model_name_valid(const char name[BOAT_MODEL_NAME_BYTES]);
bool boat_model_spec_valid(BoatModelSpec spec);
bool boat_model_spec_equal(const BoatModelSpec *a, const BoatModelSpec *b);
bool boat_model_generate(BoatModel *model, BoatModelSpec spec);
// The CPU view's existing mirrored camera basis needs mirrored glyph layout (hull unchanged).
bool boat_model_generate_oriented(BoatModel *model, BoatModelSpec spec, bool mirror_lettering);
// Small shared geometry helpers, also used to build the pontoon and quay equipment.
void boat_model_begin(BoatModel *model);
void boat_model_box(BoatModel *model, Vector3 position, Vector3 size, Color color);
void boat_model_beam(BoatModel *model, Vector3 a, Vector3 b, float width, Color color);
// A one-sided panel, counter-clockwise as seen from its front (as the box faces).
void boat_model_quad(BoatModel *model, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color);

#endif
