#ifndef PROTEUS_WATERFRONT_H
#define PROTEUS_WATERFRONT_H

#include "renderer3d.h"
#include "waterfront_scene.h"

// Static geographic scenery; the boat views draw the boats, actors and camera.
bool waterfront_draw_boating(
    Renderer3D *renderer, Color *pixels, int width, int height, View3D camera, bool omit_water);

void waterfront_draw_traffic(Renderer3D *renderer, const WaterfrontTrafficFrame *frame);
void waterfront_draw_harbor(Renderer3D *renderer, const WaterfrontHarborFrame *frame);

#endif
