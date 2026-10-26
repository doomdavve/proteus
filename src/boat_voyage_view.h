#ifndef PROTEUS_BOAT_VOYAGE_VIEW_H
#define PROTEUS_BOAT_VOYAGE_VIEW_H

#include "boat_voyage.h"

// Screen-space destination cue centred at the top of a WIDTH-pixel window: a bearing arrow,
// the distance or arrival time, and the latest voyage notice, the exposure or the shelter that
// recovery returns to. Draws nothing without a route.
void boat_voyage_hud(const BoatVoyage *voyage, const Boat *boat, double time, int width);

#endif
