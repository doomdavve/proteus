#ifndef PROTEUS_BOAT_CARGO_VIEW_H
#define PROTEUS_BOAT_CARGO_VIEW_H

#include "boat_model.h"
#include "boat_water.h"

// Append a vertical landing decal clipped to actual receiver triangles. Start OUT with
// boat_model_begin; do not pass OUT as RECEIVERS. Geometry is caller-owned and bounded.
// REST_WATER optionally displaces only intersecting rest-water triangles with the same
// vertex samples/fade as the GPU near patch. NULL uses receiver vertices exactly (CPU/solids).
// FOCUS is renderer X/Z; fading is irrelevant when REST_WATER is NULL.
// MASK is a diagnostic colour; alpha zero selects darkened receiver albedo instead.
bool boat_cargo_decal(BoatModel *out, const Mesh3D *receivers, Vector3 cargo,
    const BoatWater *rest_water, double now, Vector2 focus, Color mask);

#endif
