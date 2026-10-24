#ifndef PROTEUS_WATERFRONT_CITY_H
#define PROTEUS_WATERFRONT_CITY_H

#include "geometry3d.h"

#include <stdbool.h>

// Compiled map crops: water triangles first, then land, buildings and the river bridge.
// Static geometry shared by shoreline queries, water meshes and both renderers.
const Mesh3D *waterfront_city_mesh(void);
// Read either immutable crop without changing the process's selected map.
const Mesh3D *waterfront_city_mesh_crop(bool river);
// Select once before initializing boat map/water caches or GPU resources.
// The default remains the original Eriksberg crop used by the main demo.
void waterfront_city_select_river(bool enabled);
// Authored bridge/approach triangles use flat materials and have no facade windows.
bool waterfront_city_is_bridge_triangle(size_t index);

#endif
