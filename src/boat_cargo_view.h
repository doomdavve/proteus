#ifndef PROTEUS_BOAT_CARGO_VIEW_H
#define PROTEUS_BOAT_CARGO_VIEW_H

#include "boat_cargo.h"
#include "boat_model.h"
#include "boat_water.h"
#include "boat_world.h"
#include "renderer3d.h"
#include "waterfront.h"

// Append a vertical landing decal clipped to actual receiver triangles. Start OUT with
// boat_model_begin; do not pass OUT as RECEIVERS. Geometry is caller-owned and bounded.
// REST_WATER optionally displaces only intersecting rest-water triangles with the same
// vertex samples/fade as the GPU near patch. NULL uses receiver vertices exactly (CPU/solids).
// FOCUS is renderer X/Z; fading is irrelevant when REST_WATER is NULL.
// MASK is a diagnostic colour; alpha zero selects darkened receiver albedo instead.
bool boat_cargo_decal(BoatModel *out, const Mesh3D *receivers, Vector3 cargo,
    const BoatWater *rest_water, double now, Vector2 focus, Color mask);

// What the cargo view reads of a work session; borrowed for one call.
typedef struct {
    const BoatCargoSandbox *sandbox;
    const Boat *boat;
    const BoatWorld *world;
    uint64_t tick;
    bool crane_view; // false: the camera follows the boat (helm).
} BoatCargoView;

// One evaluated mesh for both renderers (sites, boat boxes, crate, straps, berth outlines),
// relative to the first site's berth and returned with the origin added back. The result is the
// number of leading triangles that receive the landing decal (everything before the crate).
size_t boat_cargo_view_scene(const BoatCargoView *v, BoatModel *m);
// The crane or helm camera; mapped locations pull the eye in until the line of sight is clear.
View3D boat_cargo_view_camera(const BoatCargoView *v, const WaterfrontScenery *scenery);
// A small finite water patch around the boat for the non-GPU harness views.
void boat_cargo_view_water(const BoatCargoView *v, BoatModel *m);

#endif
