#ifndef PROTEUS_WATERFRONT_OUTER_H
#define PROTEUS_WATERFRONT_OUTER_H

#include "boat_map.h"

// Compiled first outer-river slice (the river crop plus the Långedrag run): water triangles
// first, then terrain land (with the river's roads and gardens painted on it inside the crop)
// and quay walls, then the river's buildings and trees moved onto the terrain, and last the
// river's bridge, which forms the bridge range. Immutable process-lifetime data in the river's
// crane-origin frame; where the region cuts open water, the water ends at an edge that
// collision treats as shore.
extern const BoatMapDescription waterfront_outer_map;

// The baked land height under renderer X/Z over the river crop (bilinear on a 4 m lattice);
// 3.5 m, the river's ground, outside it.
float waterfront_outer_ground(float x, float z);

typedef float (*WaterfrontGround)(float x, float z);
// The land under the river kit (crane, bridge deck, cafe, street furniture, frontages, quay
// fittings, street life and harbour) on MAP: waterfront_outer_ground for the outer map. NULL for
// any other map: the river keeps the kit's own flat 3.5 m ground, and other maps have no kit.
WaterfrontGround waterfront_kit_ground(const BoatMapDescription *map);

#endif
