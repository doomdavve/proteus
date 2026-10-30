#ifndef PROTEUS_BOAT_SKIN_H
#define PROTEUS_BOAT_SKIN_H

#include "boat_model.h"

// Appearance IDs are local presentation choices,
// not protocol values. The four countries colour room slots. The explorer and cabin choices
// select their own handling profiles in solo free boating.
typedef enum {
    BOAT_SKIN_SWEDEN,
    BOAT_SKIN_FINLAND,
    BOAT_SKIN_DENMARK,
    BOAT_SKIN_NORWAY,
    BOAT_SKIN_COUNTRIES,
    BOAT_SKIN_EXPLORER = BOAT_SKIN_COUNTRIES,
    BOAT_SKIN_CABIN,
    BOAT_SKIN_COUNT
} BoatSkinId;

// ENGINE is one outboard mesh; the cabin boat has its inboard drive on the hull.
typedef enum { BOAT_SKIN_HULL, BOAT_SKIN_COCKPIT, BOAT_SKIN_ENGINE } BoatSkinPart;

typedef struct {
    Color hull, deck, accent, upholstery; // Accent is the livery stripe.
} BoatSkinPalette;

unsigned int boat_skin_index(unsigned int skin);
const char *boat_skin_name(unsigned int skin);
bool boat_skin_parse(const char *name, unsigned int *skin);
BoatSkinPalette boat_skin_palette(unsigned int skin);
// Caller-owned geometry using the existing boat_pose_parts transforms. Palette edits
// never enter Boat, collision bounds, wakes, recordings or the authority simulation.
bool boat_skin_generate(BoatModel *model, BoatSkinPalette palette, BoatSkinPart part);
// Presentation-only twin mounts. Side is -1 (port) or +1 (starboard). Keep each
// mount fixed to the hull while retaining the center engine's steering rotation.
Transform3D boat_skin_outboard_pose(Transform3D hull, Transform3D engine_center, int side);
// Immutable cached presets for CPU and GPU upload; invalid IDs fall back to Sweden.
const Mesh3D *boat_skin_mesh(unsigned int skin, BoatSkinPart part);

#endif
