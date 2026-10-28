#ifndef PROTEUS_WATERFRONT_H
#define PROTEUS_WATERFRONT_H

#include "renderer3d.h"
#include "waterfront_corner.h"
#include "waterfront_scene.h"

// Static geographic scenery of SCENERY's map under a sky with SQUALL; the boat views draw the
// boats, actors and camera. Fails for an unready or basin scenery.
bool waterfront_draw_boating(Renderer3D *renderer, const WaterfrontScenery *scenery, Color *pixels,
    int width, int height, View3D camera, bool omit_water, WaterfrontSquall squall);

void waterfront_draw_traffic(Renderer3D *renderer, const WaterfrontTrafficFrame *frame);
void waterfront_draw_harbor(Renderer3D *renderer, const WaterfrontHarborFrame *frame);

#endif
