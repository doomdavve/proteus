#ifndef PROTEUS_WATERFRONT_H
#define PROTEUS_WATERFRONT_H

#include "renderer3d.h"
#include "waterfront_corner.h"
#include "waterfront_harbor_view.h"
#include "waterfront_scene.h"

// Static geographic scenery of SCENERY's map under a sky with SQUALL, in LIGHTING, which stays
// set on RENDERER for the boats, actors and camera the boat views draw next. Fails for an
// unready or basin scenery.
bool waterfront_draw_boating(Renderer3D *renderer, const WaterfrontScenery *scenery, Color *pixels,
    int width, int height, View3D camera, bool omit_water, WaterfrontSquall squall,
    const WaterfrontLighting *lighting, double time);

void waterfront_draw_traffic(Renderer3D *renderer, const WaterfrontTrafficFrame *frame);
bool waterfront_draw_harbor(
    Renderer3D *renderer, WaterfrontHarborModels *models, const WaterfrontHarborFrame *frame);

#endif
