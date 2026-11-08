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
    // Renderer light requested for the CPU mirror. GPU callers select it on their own BoatGpu.
    float daylight, hour;
    bool hour_set;
    // The light actually used by the last sample, so a changed light invalidates the 30 Hz cache.
    float sampled_daylight, sampled_hour;
    bool sampled_hour_set;
    bool enabled; // Session-local V toggle; defaults to hidden.
    bool sampled, gpu;
} BoatRearview;

// Select the light the CPU mirror samples. GPU mirrors read the supplied BoatGpu's selection.
// Only daylight/hour/hour_set are copied into the mirror's own BoatCpuView.
void boat_rearview_set_lighting(BoatRearview *mirror, float daylight, float hour, bool hour_set);

// GPU may be NULL. Reuses its scene resources without changing its main render targets.
// Samples the supplied virtual world time at up to 30 Hz; never steps the simulation.
bool boat_rearview_update(BoatRearview *mirror, BoatGpu *gpu, const BoatViewState *view,
    const BoatPresentation *presentation, const BoatWorld *world, const BoatRivals *rivals);
// Draw inside the scene's render texture, before the normal integer scaling.
void boat_rearview_draw(const BoatRearview *mirror, int width);
void boat_rearview_destroy(BoatRearview *mirror);

#endif
