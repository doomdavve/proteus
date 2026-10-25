#ifndef PROTEUS_BOAT_GPU_H
#define PROTEUS_BOAT_GPU_H
#include "boat_view.h"
#include "boat_water_mesh.h"
#include "renderer_gpu.h"

typedef struct {
    WaterfrontGpuScene scene;
    RenderTexture2D target, supersampled;
    Shader downsample;
    Mesh wave_mesh;
    Mesh wave_mesh_extra; // Second indexed basin strip (16-bit vertex indices).
    Mesh basin_water;
    Vector2 wave_center;
    const BoatMapDescription *wave_map; // Map whose patch wave_mesh holds (NULL: basin).
    BoatWaterMeshCache water_mesh; // This view's own patches; never shared with CPU views.
    int wave_capacity, wave_peak_triangles, wave_rebuilds;
    Texture2D wake_texture;
    float *wake_upload;
    double wake_upload_seconds;
    size_t wake_uploads;
    bool failed;

} BoatGpu;

bool boat_gpu_init(BoatGpu *gpu, bool aa);
void boat_gpu_draw(BoatGpu *gpu, const BoatViewState *view, const BoatPresentation *presentation,
    const BoatWorld *world, BoatCamera camera);
void boat_gpu_destroy(BoatGpu *gpu);
void boat_gpu_draw_with_ghost(BoatGpu *gpu, const BoatViewState *view,
    const BoatPresentation *presentation, const BoatWorld *world, BoatCamera camera,
    const BoatViewState *ghost, const BoatFlag *ghost_flag, float opacity);
// The complete scene: the player, RIVALS (may be NULL) as ordinary hulls in every pass, and an
// optional dithered GHOST in the colour pass.
void boat_gpu_draw_scene(BoatGpu *gpu, const BoatViewState *view,
    const BoatPresentation *presentation, const BoatWorld *world, BoatCamera camera,
    const BoatRivals *rivals, const BoatViewState *ghost, const BoatFlag *ghost_flag,
    float opacity);

#endif
