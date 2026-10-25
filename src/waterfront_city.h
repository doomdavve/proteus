#ifndef PROTEUS_WATERFRONT_CITY_H
#define PROTEUS_WATERFRONT_CITY_H

#include "boat_map.h"
#include "geometry3d.h"

#include <stdbool.h>

// Compiled river map crop: water triangles first, then land, buildings and the bridge.
// Static geometry shared by shoreline queries, water meshes and both renderers.
extern const BoatMapDescription waterfront_city_river_map;

const BoatMapDescription *waterfront_city_map(void);
// A1.1 compatibility accessors for the river description; A1.4 binds renderers explicitly.
const Mesh3D *waterfront_city_mesh(void);
// Authored bridge/approach triangles use flat materials and have no facade windows.
bool waterfront_city_is_bridge_triangle(size_t index);

#endif
