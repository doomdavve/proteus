#ifndef PROTEUS_WATERFRONT_CITY_H
#define PROTEUS_WATERFRONT_CITY_H

#include "boat_map.h"
#include "geometry3d.h"

#include <stdbool.h>

// Compiled river map crop: its water triangles alone, which collision, the server and the
// multiplayer content digest use. Immutable process-lifetime data, borrowed by the river
// location's definition. Its land, buildings and bridge are presentation data in the river's
// scenery pack (boat_scenery_of), whose leading water is exactly this; the authored
// bridge/approach triangles there use flat materials and have no facade windows.
extern const BoatMapDescription waterfront_city_river_map;

#endif
