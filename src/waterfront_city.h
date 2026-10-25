#ifndef PROTEUS_WATERFRONT_CITY_H
#define PROTEUS_WATERFRONT_CITY_H

#include "geometry3d.h"

#include <stdbool.h>

// Compiled river map crop: water triangles first, then land, buildings and the bridge.
// Static geometry shared by shoreline queries, water meshes and both renderers.
const Mesh3D *waterfront_city_mesh(void);
// Authored bridge/approach triangles use flat materials and have no facade windows.
bool waterfront_city_is_bridge_triangle(size_t index);

#endif
