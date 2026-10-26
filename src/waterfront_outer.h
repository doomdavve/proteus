#ifndef PROTEUS_WATERFRONT_OUTER_H
#define PROTEUS_WATERFRONT_OUTER_H

#include "boat_map.h"

// Compiled first outer-river slice (the river crop plus the Långedrag run): water triangles
// first, then flat land and quay walls. It has no buildings or bridge, so its bridge range is
// empty. Immutable process-lifetime data in the river's crane-origin frame; where the region
// cuts open water, the water ends at an edge that collision treats as shore.
extern const BoatMapDescription waterfront_outer_map;

#endif
