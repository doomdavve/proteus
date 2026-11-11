#ifndef PROTEUS_RENDERER_GPU_H
#define PROTEUS_RENDERER_GPU_H

#include "render/waterfront_corner.h"
#include "render/waterfront_harbor_view.h"
#include "render/waterfront_lights.h"
#include "render/waterfront_scene.h"
#include "render/waterfront_wake_stamp.h"
#include "render/waterfront_water_history.h"

#include "raylib.h"

#include <stdbool.h>

#define WATERFRONT_GPU_MAX_LOCAL_LIGHTS 32
#define WATERFRONT_GPU_SHADOW_MAP_SIZE 1024
#define WATERFRONT_GPU_FAR_SHADOW_MAP_SIZE 512
#define WATERFRONT_GPU_REFLECTION_WIDTH 160
#define WATERFRONT_GPU_REFLECTION_HEIGHT 120
// How much of the ambient term a fully closed baked corner (boat_ambient.h) removes.
#define WATERFRONT_GPU_AMBIENT_OCCLUSION .6f

// Ground batch stores one constant material ID per triangle in texcoord.x.
// Keep these values in sync with waterfront_city.fs.
typedef enum {
    WATERFRONT_GROUND_ASPHALT = 1,
    WATERFRONT_GROUND_PAVING,
    WATERFRONT_GROUND_CONCRETE,
    WATERFRONT_GROUND_PLANTING,
    WATERFRONT_GROUND_COUNT
} WaterfrontGroundMaterial;

// Facade UVs are metres along the wall / above quay level. Vertex RGB is the
// building palette; alpha stores this opaque material ID, not transparency.
typedef enum {
    WATERFRONT_FACADE_BRICK = 1,
    WATERFRONT_FACADE_PLASTER,
    WATERFRONT_FACADE_PANEL,
    WATERFRONT_FACADE_COUNT
} WaterfrontFacadeMaterial;

typedef struct {
    Vector3 position;
    Vector3 direction;
    Color color;
    float radius;
    float intensity;
    float inner_cone_cos;
    float outer_cone_cos;
} WaterfrontGpuLight;

typedef enum {
    WATERFRONT_GPU_PASS_SHADOW,
    WATERFRONT_GPU_PASS_REFLECTION,
    WATERFRONT_GPU_PASS_MAIN,
    WATERFRONT_GPU_PASS_COUNT
} WaterfrontGpuPass;

typedef enum {
    WATERFRONT_GPU_GROUP_MAP, // Bound map meshes: city, facades, roofs, ground, windows, crane.
    WATERFRONT_GPU_GROUP_WATER, // Map water and the borrowed wave patch.
    WATERFRONT_GPU_GROUP_OBJECTS, // Transformed meshes: boats, actors, gulls, bridge, basin.
    WATERFRONT_GPU_GROUP_COUNT
} WaterfrontGpuGroup;

typedef struct {
    unsigned int submissions;
    size_t triangles; // Multiplied by instances: the work the GPU rasterizes.
    unsigned int instances;
} WaterfrontGpuCount;

// One map surface class split by render tile (the scenery's catalogue): mesh i covers bounds[i],
// its triangles in source order. Tiles without geometry of the class have no mesh.
typedef struct {
    Mesh *meshes;
    BoundingBox *bounds;
    int *cells; // The grid cell of each mesh, which selects a detailed class's level.
    int count;
} WaterfrontGpuTiled;

// Storage of the static map meshes of the bound map, reported by waterfront_gpu_scene_geometry.
// Source vertices are three per triangle, what the previous unindexed storage uploaded.
typedef struct {
    int meshes;
    size_t triangles, source_vertices, vertices;
    size_t vertex_bytes, index_bytes; // Uploaded to the GPU.
    size_t far_triangles, far_vertex_bytes, far_index_bytes; // Distance detail, also GPU memory.
    size_t retained_cpu_bytes; // Zero unless the review-only retain_map_sources is set.
    float normal_error; // Worst decoded normal component error of packing.
    size_t occluded_vertices; // Uploaded map vertices with a baked occlusion above zero.
} WaterfrontGpuGeometry;

// Application submissions of the last waterfront_gpu_scene_draw. Each mesh submission is one GL
// draw: a raylib DrawMesh, or one instanced run of contiguous objects sharing a mesh; sky_quads
// counts the batched sky quad, flushed as one more draw per pass. These are not driver-reported
// draw calls.
// tiles_culled counts the map tile meshes each pass skipped outside its view volume.
typedef struct {
    WaterfrontGpuCount mesh[WATERFRONT_GPU_PASS_COUNT][WATERFRONT_GPU_GROUP_COUNT];
    unsigned int tiles_culled[WATERFRONT_GPU_PASS_COUNT];
    unsigned int far_cells[WATERFRONT_GPU_PASS_COUNT]; // Detail cells drawn far, per map drawn.
    unsigned int sky_quads;
    unsigned int wake_pieces, wake_outside; // Object wake pieces and those shaded analytically.
    size_t wake_texels; // Texels the stamped pieces cover; zero when the field is reused.
    unsigned history_substeps; // Fixed water history substeps run this frame.
    unsigned history_resets; // 1 when the history reset and pre-rolled this frame.
} WaterfrontGpuStats;

// GPU-owned waterfront resources. They must be created, bound and destroyed while a
// raylib/OpenGL context is alive. Init builds the map-independent shared resources (shaders,
// sky, unit, boat, gull and harbor/traffic meshes); waterfront_gpu_scene_bind builds the
// map-derived meshes below from one scenery owner.
typedef struct {
    const BoatMapDescription *map; // Bound map; NULL: unbound, which draws only basin frames.
    bool river; // The bound map carries the authored river crane, bridge deck and fixtures.
    float cafe_lift; // The kit ground at the cafe less the river's 3.5 m, for its light.
    // Per-tile map classes, each culled against every pass's view volume.
    WaterfrontGpuTiled city;
    WaterfrontGpuTiled facades;
    WaterfrontGpuTiled roofs;
    WaterfrontGpuTiled quay; // All ground materials, including authored pavement tops.
    Mesh water;
    Mesh near_water; // Borrowed boat wave patch; owned by BoatGpu, never unloaded here.
    Mesh near_water_extra; // Optional second indexed candidate strip, same lifetime.
    Texture2D wake_field; // Borrowed, immutable for the complete draw; owned by BoatGpu.
    float wake_spacing;
    Texture2D object_wakes; // Owned compact object records; not a sampled water height field.
    // Records of the frame being drawn: player and traffic pieces, and, in a third row, the
    // indices of the pieces the stamped field cannot shade.
    float *object_records;
    int object_count, object_players, object_outside;
    float object_life, object_traffic_life;
    WaterfrontWakeStamp wake_stamp; // Sampled wake shading; analytic when unsupported.
    bool wake_stamp_disabled; // Review: shade every piece analytically, the reference.
    WaterfrontWaterHistory water_history; // Foam history and ripples; none when unsupported.
    bool water_history_disabled; // Review: stamp-only water, the G3a comparison.
    WaterfrontGpuTiled windows;
    WaterfrontGpuTiled corner;
    WaterfrontGpuTiled corner_emissive;
    // Far levels of the two detailed classes, on the same fine grid. Each view chooses per cell.
    WaterfrontGpuTiled windows_far;
    WaterfrontGpuTiled corner_far;
    unsigned char *detail_far[2]; // Presentation-only per-cell level of main and reflection.
    Vector2 detail_origin;
    float detail_cell;
    int detail_columns, detail_rows;
    int detail_source; // Which level or view's selection the pass being drawn uses.
    float detail_scale; // Render pixels per presented pixel, for projected size; 0 means 1.
    Mesh crane;
    Mesh bridge;
    Mesh cube;
    Mesh player_hull[BOAT_SKIN_COUNT], player_cabin[BOAT_SKIN_COUNT],
        player_engine[BOAT_SKIN_COUNT];
    Mesh player_flag, ghost_flag; // Dynamic cloth, uploaded once before all scene passes.
    Mesh rival_flags[WATERFRONT_RIVALS];
    Mesh traffic_boats[WATERFRONT_TRAFFIC_MESH_SLOTS], lift_span;
    Mesh harbor_boats[BOAT_SCENE_BERTH_RECIPES], harbor_pontoon, harbor_quay, harbor_ferry;
    Mesh harbor_custom[BOAT_SCENE_BERTHS];
    WaterfrontHarborModels harbor_models;
    unsigned harbor_uploads;
    int harbor_upload_limit; // Review: fail after this many uploads per bind (0: no limit).
    Mesh plane;
    Mesh gull[WATERFRONT_GULL_PARTS];
    Shader shader;
    Shader shadow_shader;
    Shader water_shader;
    Shader sky_shader;
    Texture2D sky_mask; // Static cloud coverage/rim/terrace mask, uploaded once per scene.
    RenderTexture2D shadow_map;
    RenderTexture2D far_shadow_map;
    // G4: a copy of the main pass's opaque depth taken just before water draws, for shore and
    // quay shading. Never the attachment being written; absent outside a render texture.
    RenderTexture2D scene_depth;
    bool scene_depth_valid; // Copied for the water now being drawn.
    bool scene_depth_disabled; // Review: water shading without the depth copy.
    // G4: the bound map's water atlas (shore, depth, shelter, chop) as a bilinear RGBA8 texture;
    // id 0 without one, and the water keeps its hand-written exposure.
    Texture2D water_atlas;
    float water_atlas_rect[4]; // Origin X/Z, 1/extent X/Z, renderer metres.
    bool water_atlas_disabled; // Review: the hand-written exposure even with an atlas.
    bool far_water_disabled; // Review: no G4 gusts, sun glitter or squall water.
    RenderTexture2D reflection;
    Matrix light_view_projection;
    Matrix far_light_view_projection;
    Matrix reflection_view_projection;
    int shadow_map_size;
    Material city_material;
    Material roof_material;
    Material life_material;
    Material shadow_material;
    Material water_material;
    Texture2D roof_texture;
    // G5b clustered local lights (waterfront_lights.h), uploaded as an RGBA32F texture. Without
    // the clusters or their texture, the capped uniform list below is the fallback: the closest
    // lights to the shot, by class, and no lamplight on the water.
    WaterfrontLightClusters *light_clusters;
    float *light_texels;
    Texture2D light_texture;
    bool light_clusters_active; // This frame shades from the clusters.
    bool light_clusters_disabled; // Review: the capped uniform fallback.
    bool light_reference; // Review: shade every clustered light, ignoring the slot lists.
    bool ambient_occlusion_disabled; // Review: draw the baked occlusion at zero strength.
    bool particles_disabled; // Review: draw no boat foam or spray cubes (their cost).
    int stress_lights; // Review: extra synthetic lights around the shot.
    WaterfrontLightStats light_stats; // This frame's submissions, drops and overflow.
    int light_rows; // Texture rows uploaded this frame.
    int fallback_dropped; // Lights the capped fallback left out this frame (or behind the eye).
    bool emissive_hdr; // The target keeps light above 1: dusk lenses and windows exceed it.
    WaterfrontGpuLight local_lights[WATERFRONT_GPU_MAX_LOCAL_LIGHTS];
    int local_light_ids[WATERFRONT_GPU_MAX_LOCAL_LIGHTS];
    int local_light_count;
    bool lighting_enabled;
    bool shadows_enabled;
    bool far_shadows_active; // Gameplay only; title keeps its existing single map.
    bool atmosphere_enabled;
    bool reflections_enabled;
    bool culling_disabled; // Review only: draw every map tile and object in every pass.
    bool detail_disabled; // Review only: draw the near level of every detail cell.
    bool retain_map_sources; // Review only: keep each map mesh's unindexed arrays after upload.
    int map_upload_limit; // Review only: fail map uploads after this many meshes (0: no limit).
    WaterfrontGpuGeometry geometry;
    bool instancing_disabled; // Review only: the scalar reference for instanced object draws.
    int instance_limit; // Review only: replay a batch sooner than its capacity (0: capacity).
    void *bounds_cache; // Private local bounds of transformed object meshes, keyed by VAO.
    void *object_batch; // Private instance storage; NULL falls back to the scalar path.
    bool ready;
    WaterfrontGpuStats stats; // Reset by each draw.
} WaterfrontGpuScene;

bool waterfront_gpu_scene_init(WaterfrontGpuScene *scene, const char *fragment_shader_path);
// Replace the map-derived meshes with those of SCENERY (borrowed only during the call). A basin
// scenery unbinds. Fails, leaving the scene unbound, for an unready scenery or upload failure.
// A non-basin frame drawn on an unbound scene is refused with a warning.
bool waterfront_gpu_scene_bind(WaterfrontGpuScene *scene, const WaterfrontScenery *scenery);
void waterfront_gpu_scene_set_lighting_enabled(WaterfrontGpuScene *scene, bool enabled);
void waterfront_gpu_scene_set_shadows_enabled(WaterfrontGpuScene *scene, bool enabled);
void waterfront_gpu_scene_set_atmosphere_enabled(WaterfrontGpuScene *scene, bool enabled);
void waterfront_gpu_scene_set_reflections_enabled(WaterfrontGpuScene *scene, bool enabled);
bool waterfront_gpu_scene_set_shadow_map_size(WaterfrontGpuScene *scene, int size);
// Whether float render targets accept blending (the wake stamp's probe).
bool waterfront_gpu_scene_float_blending(const WaterfrontGpuScene *scene);
// Reallocate the water reflection target (initially 160x120, nearest-filtered). Failure keeps the
// existing target. Dimensions are bounded to 1..2048.
bool waterfront_gpu_scene_set_reflection_size(WaterfrontGpuScene *scene, int width, int height);
// Review only: drawing every tile isolates culling from the per-tile draw order.
void waterfront_gpu_scene_set_culling_enabled(WaterfrontGpuScene *scene, bool enabled);
void waterfront_gpu_scene_set_instancing_enabled(WaterfrontGpuScene *scene, bool enabled);
void waterfront_gpu_scene_set_instance_limit(WaterfrontGpuScene *scene, int limit);
// Review: shade object wakes from the stamped field (default when supported) or analytically.
void waterfront_gpu_scene_set_wake_stamp_enabled(WaterfrontGpuScene *scene, bool enabled);
// Review: replace the stamped field with one of SIZE texels per side at SPACING metres.
bool waterfront_gpu_scene_set_wake_stamp_size(WaterfrontGpuScene *scene, int size, float spacing);
// Review: draw water without (or again with) G4 gusts, sun glitter and squall water.
void waterfront_gpu_scene_set_far_water_enabled(WaterfrontGpuScene *scene, bool enabled);
// Review: shade water from the hand-written exposure instead of (or again from) the map atlas.
void waterfront_gpu_scene_set_water_atlas_enabled(WaterfrontGpuScene *scene, bool enabled);
// Review: shade water without (or again with) the copy of opaque depth.
void waterfront_gpu_scene_set_scene_depth_enabled(WaterfrontGpuScene *scene, bool enabled);
// Review: turn the foam history and ripples off (stamp-only shading) or back on.
void waterfront_gpu_scene_set_water_history_enabled(WaterfrontGpuScene *scene, bool enabled);
Texture2D waterfront_gpu_scene_shadow_map(const WaterfrontGpuScene *scene);
// Transactional appearance refresh; failure retains every old CPU/GPU hull resource.
bool waterfront_gpu_scene_bind_harbor(
    WaterfrontGpuScene *scene, const WaterfrontHarborFrame *frame);
bool waterfront_gpu_scene_draw(
    WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame, int width, int height);
void waterfront_gpu_scene_destroy(WaterfrontGpuScene *scene);
WaterfrontGpuCount waterfront_gpu_stats_total(const WaterfrontGpuStats *stats);
// Review only: whether distant cells use the far level.
void waterfront_gpu_scene_set_detail_enabled(WaterfrontGpuScene *scene, bool enabled);
// Vertices across all tiles of one class.
int waterfront_gpu_tiled_vertices(const WaterfrontGpuTiled *tiled);
WaterfrontGpuGeometry waterfront_gpu_scene_geometry(const WaterfrontGpuScene *scene);

#endif
