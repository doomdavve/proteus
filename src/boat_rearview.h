#ifndef PROTEUS_BOAT_REARVIEW_H
#define PROTEUS_BOAT_REARVIEW_H

#include "boat_gpu.h"

#define BOAT_REARVIEW_WIDTH 112
#define BOAT_REARVIEW_HEIGHT 40
#define BOAT_REARVIEW_TOP 4
#define BOAT_REARVIEW_BOTTOM (BOAT_REARVIEW_TOP + BOAT_REARVIEW_HEIGHT + 2)

// Presentation only. Zero-initialize; resources are allocated when first shown.
typedef struct {
    Renderer3D renderer;
    BoatCpuView cpu;
    Color pixels[BOAT_REARVIEW_WIDTH * BOAT_REARVIEW_HEIGHT];
    Texture2D texture;
    RenderTexture2D target;
    const BoatWorldDefinition *location;
    double sampled_time;
    bool enabled; // Session-local V toggle; defaults to hidden.
    bool sampled, gpu;
} BoatRearview;

// GPU may be NULL. Reuses its scene resources without changing its main render targets.
// Samples the supplied virtual world time at up to 30 Hz; never steps the simulation.
bool boat_rearview_update(BoatRearview *mirror, BoatGpu *gpu, const BoatViewState *view,
    const BoatPresentation *presentation, const BoatWorld *world, const BoatRivals *rivals);
// Draw inside the scene's render texture, before the normal integer scaling.
void boat_rearview_draw(const BoatRearview *mirror, int width);
void boat_rearview_destroy(BoatRearview *mirror);

#endif
