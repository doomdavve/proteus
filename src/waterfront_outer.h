#ifndef PROTEUS_WATERFRONT_OUTER_H
#define PROTEUS_WATERFRONT_OUTER_H

#include "boat_map.h"

// Compiled first outer-river slice (the river crop plus the Långedrag run): its water triangles
// alone, for collision, the server and the content digest. Immutable process-lifetime data in
// the river's crane-origin frame; where the region cuts open water, the water ends at an edge
// that collision treats as shore. The rest of the bake is presentation data in the outer
// scenery pack (boat_scenery_of), after this same water: terrain land (with the river's roads
// and gardens painted on it inside the crop) and quay walls, the river's buildings and trees
// moved onto the terrain, the river's bridge, which starts the bridge range, and the A3.5a
// dressing.
extern const BoatMapDescription waterfront_outer_map;

// The baked land height under renderer X/Z over the river crop (bilinear on a 4 m lattice);
// 3.5 m, the river's ground, outside it.
float waterfront_outer_ground(float x, float z);

typedef float (*WaterfrontGround)(float x, float z);
// The land under the river kit (crane, bridge deck, cafe, street furniture, frontages, quay
// fittings, street life and harbour) on MAP: waterfront_outer_ground for the outer map. NULL for
// any other map: the river keeps the kit's own flat 3.5 m ground, and other maps have no kit.
WaterfrontGround waterfront_kit_ground(const BoatMapDescription *map);

// GH2: a canal bridge deck in renderer X/Z (east, north): the span runs along the unit axis UX/UZ
// from station S0 to S1 about the centre, over water from WET0 to WET1, and HALF_WIDTH either
// side. The underside stands UNDER above the water at the middle of the water and SPRING at its
// quays (a parabola between, equal for a beam); TOP is the deck's walking surface.
typedef struct {
    float x, z, ux, uz, s0, s1, wet0, wet1, half_width, under, spring, top;
} WaterfrontBridge;

// The lowest bridge underside over renderer X/Z, or INFINITY under open sky. The heights are
// estimates (docs/GOTHENBURG_HARBOUR_PLAN.md GH2).
float waterfront_outer_overhead(float x, float z);

typedef float (*WaterfrontOverhead)(float x, float z);
// The bridge undersides over MAP: waterfront_outer_overhead for the outer map, NULL otherwise.
WaterfrontOverhead waterfront_overhead(const BoatMapDescription *map);

#endif
