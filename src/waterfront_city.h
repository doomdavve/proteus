#ifndef PROTEUS_WATERFRONT_CITY_H
#define PROTEUS_WATERFRONT_CITY_H

#include "boat_map.h"
#include "geometry3d.h"

#include <stdbool.h>

// Compiled Eriksberg river crop: its water triangles alone. Immutable process-lifetime data. It is
// not a location (the outer map holds this water in the same frame): its scenery pack
// (boat_scenery_of), whose leading water is exactly this, lays out the river kit on the outer
// map, and validators drive it as a small mapped fixture. The authored bridge/approach
// triangles there use flat materials and have no facade windows.
extern const BoatMapDescription waterfront_city_river_map;

#endif
