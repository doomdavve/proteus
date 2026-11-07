#ifndef PROTEUS_BOAT_GPU_H
#define PROTEUS_BOAT_GPU_H
#include "boat_view.h"
#include "boat_water_mesh.h"
#include "renderer_gpu.h"

#define BOAT_GPU_BLOOM_LEVELS 3 // Half, quarter and eighth size.

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
    WaterfrontScenery scenery; // Scenery the scene's map meshes were built from.
    int wave_capacity, wave_peak_triangles, wave_rebuilds;
    Texture2D wake_texture;
    float *wake_upload;
    double wake_upload_seconds;
    size_t wake_uploads;
    float daylight; // Presentation phase, 0 (day; init resets it) .. 1 (dusk).
    float hour; // Full-day hour used when hour_set; never aliases daylight.
    bool hour_set; // Zero-init false keeps the legacy daylight phase.
    // HDR presentation, off unless requested: the colour pass renders into hdr_target (RGBA16F,
    // with depth) and the post shader blooms, grades and clamps it into target. The rear-view
    // mirror and supersampling keep drawing 8-bit directly.
    RenderTexture2D hdr_target, bloom[BOAT_GPU_BLOOM_LEVELS];
    Shader post;
    int post_locations[7];
    bool hdr_requested;
    int hdr_allocation_failures; // Tests: fail this many upcoming HDR allocations.
    bool failed;

} BoatGpu;

// Map meshes are built on the first draw of a mapped world and rebuilt when its map changes.
bool boat_gpu_init(BoatGpu *gpu, bool aa);
// Reallocate only the render targets, retaining scenery, shaders and water caches.
// Failure leaves the existing targets valid. Dimensions are bounded to 1..2048.
bool boat_gpu_resize(BoatGpu *gpu, int width, int height);
void boat_gpu_draw(BoatGpu *gpu, const BoatViewState *view, const BoatPresentation *presentation,
    const BoatWorld *world, BoatCamera camera);
void boat_gpu_destroy(BoatGpu *gpu);
// Request (or release) HDR presentation at the target's size and return whether it is active.
// It needs float render targets with blending; otherwise the 8-bit presentation remains, as it
// does after a failed allocation here or in a later resize.
bool boat_gpu_set_hdr(BoatGpu *gpu, bool enabled);
bool boat_gpu_hdr_active(const BoatGpu *gpu);
// Legacy bloom, grade (for daylight PHASE) and clamp of hdr_target into target. Kept for existing
// callers and tests. boat_gpu_draw_scene uses the full-day hour grade when hour_set and otherwise
// this wrapper for the legacy daylight phase; tests call it on a hand-filled hdr_target.
void boat_gpu_present_hdr(BoatGpu *gpu, float phase);
// Bytes held by the HDR scene, its depth and the bloom chain.
size_t boat_gpu_hdr_bytes(const BoatGpu *gpu);
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
