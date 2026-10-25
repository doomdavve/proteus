#ifndef PROTEUS_WATERFRONT_CITY_H
#define PROTEUS_WATERFRONT_CITY_H

#include "boat_map.h"
#include "geometry3d.h"

#include <stdbool.h>

// Compiled river map crop: water triangles first, then land, buildings and the bridge.
// Immutable process-lifetime data, borrowed by the river location's definition. Shoreline
// queries, water meshes and both renderers reach it only through an explicit owner or binding;
// its authored bridge/approach triangles use flat materials and have no facade windows.
extern const BoatMapDescription waterfront_city_river_map;

#endif
