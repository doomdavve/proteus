#include "renderer_gpu.h"
#include "boat_clock.h"
#include "boat_water_mesh.h"
#include "waterfront_city.h"
#include "waterfront_corner.h"
#include "waterfront_upstream.h"

#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float *positions;
    float *normals;
    float *texcoords;
    unsigned char *colors;
    int count;
    int capacity;
} MeshBuilder;

static const Color LAMP_LENS_COLOR = { 255, 218, 132, 255 };
static const Color HEADLIGHT_LENS_COLOR = { 255, 246, 202, 255 };
// The generated map uses this colour as its water material identifier.
static const Color WATER_COLOR = { 45, 79, 104, 255 };

static void builder_destroy(MeshBuilder *builder)
{
    free(builder->positions);
    free(builder->normals);
    free(builder->texcoords);
    free(builder->colors);
    *builder = (MeshBuilder) { 0 };
}

static bool builder_reserve(MeshBuilder *builder, int additional)
{
    if (additional <= builder->capacity - builder->count)
        return true;
    int capacity = builder->capacity == 0 ? 256 : builder->capacity;
    while (capacity - builder->count < additional)
        capacity *= 2;
    float *positions = malloc((size_t)capacity * 3 * sizeof(*positions));
    float *normals = malloc((size_t)capacity * 3 * sizeof(*normals));
    float *texcoords = malloc((size_t)capacity * 2 * sizeof(*texcoords));
    unsigned char *colors = malloc((size_t)capacity * 4 * sizeof(*colors));
    if (positions == NULL || normals == NULL || texcoords == NULL || colors == NULL) {
        free(positions);
        free(normals);
        free(texcoords);
        free(colors);
        return false;
    }
    memcpy(positions, builder->positions, (size_t)builder->count * 3 * sizeof(*positions));
    memcpy(normals, builder->normals, (size_t)builder->count * 3 * sizeof(*normals));
    memcpy(texcoords, builder->texcoords, (size_t)builder->count * 2 * sizeof(*texcoords));
    memcpy(colors, builder->colors, (size_t)builder->count * 4 * sizeof(*colors));
    free(builder->positions);
    free(builder->normals);
    free(builder->texcoords);
    free(builder->colors);
    builder->positions = positions;
    builder->normals = normals;
    builder->texcoords = texcoords;
    builder->colors = colors;
    builder->capacity = capacity;
    return true;
}

// Legacy roof mapping. Ground triangles replace UVs with material IDs; facades
// replace them with wall-local metres. Both use filtered procedural finishes.
static Vector2 world_uv(Vector3 point, Vector3 normal)
{
    const float density = .04f;
    if (fabsf(normal.y) > fabsf(normal.x) && fabsf(normal.y) > fabsf(normal.z))
        return (Vector2) { point.x * density, point.z * density };
    if (fabsf(normal.x) > fabsf(normal.z))
        return (Vector2) { point.z * density, -point.y * density };
    return (Vector2) { point.x * density, -point.y * density };
}

static bool builder_triangle(MeshBuilder *builder, Vector3 a, Vector3 b, Vector3 c, Color color)
{
    if (!builder_reserve(builder, 3))
        return false;
    Vector3 normal
        = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
    Vector3 points[] = { a, b, c };
    for (int i = 0; i < 3; ++i) {
        int vertex = builder->count++;
        Vector2 uv = world_uv(points[i], normal);
        builder->positions[vertex * 3] = points[i].x;
        builder->positions[vertex * 3 + 1] = points[i].y;
        builder->positions[vertex * 3 + 2] = points[i].z;
        builder->normals[vertex * 3] = normal.x;
        builder->normals[vertex * 3 + 1] = normal.y;
        builder->normals[vertex * 3 + 2] = normal.z;
        builder->texcoords[vertex * 2] = uv.x;
        builder->texcoords[vertex * 2 + 1] = uv.y;
        builder->colors[vertex * 4] = color.r;
        builder->colors[vertex * 4 + 1] = color.g;
        builder->colors[vertex * 4 + 2] = color.b;
        builder->colors[vertex * 4 + 3] = color.a;
    }
    return true;
}

static bool builder_quad(
    MeshBuilder *builder, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color)
{
    return builder_triangle(builder, a, b, c, color) && builder_triangle(builder, a, c, d, color);
}

static bool builder_box(MeshBuilder *builder, Vector3 a, Vector3 b, float width, float depth)
{
    Vector3 direction = Vector3Normalize(Vector3Subtract(b, a));
    Vector3 reference = fabsf(direction.y) > .9f ? (Vector3) { 1, 0, 0 } : (Vector3) { 0, 1, 0 };
    Vector3 right
        = Vector3Scale(Vector3Normalize(Vector3CrossProduct(reference, direction)), width / 2);
    Vector3 up = Vector3Scale(Vector3Normalize(Vector3CrossProduct(direction, right)), depth / 2);
    Vector3 points[8];
    for (int i = 0; i < 8; ++i) {
        Vector3 center = i < 4 ? a : b;
        int corner = i % 4;
        points[i] = Vector3Add(center,
            Vector3Add(Vector3Scale(right, corner == 0 || corner == 3 ? -1 : 1),
                Vector3Scale(up, corner < 2 ? -1 : 1)));
    }
    static const unsigned int faces[][3]
        = { { 0, 2, 1 }, { 0, 3, 2 }, { 5, 7, 4 }, { 5, 6, 7 }, { 4, 3, 0 }, { 4, 7, 3 },
              { 1, 6, 5 }, { 1, 2, 6 }, { 3, 6, 2 }, { 3, 7, 6 }, { 4, 1, 5 }, { 4, 0, 1 } };
    for (size_t i = 0; i < sizeof(faces) / sizeof(faces[0]); ++i) {
        if (!builder_triangle(builder, points[faces[i][0]], points[faces[i][1]],
                points[faces[i][2]], (Color) { 107, 67, 62, 255 }))
            return false;
    }
    return true;
}

static Mesh builder_finish(MeshBuilder *builder)
{
    Mesh mesh = { .vertexCount = builder->count,
        .triangleCount = builder->count / 3,
        .vertices = builder->positions,
        .normals = builder->normals,
        .texcoords = builder->texcoords,
        .colors = builder->colors };
    builder->positions = NULL;
    builder->normals = NULL;
    builder->texcoords = NULL;
    builder->colors = NULL;
    UploadMesh(&mesh, false);
    *builder = (MeshBuilder) { 0 };
    return mesh;
}

static void unload_mesh(Mesh *mesh)
{
    if (mesh->vaoId != 0)
        UnloadMesh(*mesh);
    *mesh = (Mesh) { 0 };
}

// Uniform locations are resolved once per shader and then by hash. These calls sit in the
// per-frame and per-draw paths; the GL name lookup itself is not repeated there.
#define LOCATION_CACHE_SLOTS 32
#define LOCATION_CACHE_ENTRIES 96
typedef struct {
    unsigned int hash;
    int location;
    char name[48];
} LocationEntry;
typedef struct {
    unsigned int id;
    int count;
    LocationEntry entries[LOCATION_CACHE_ENTRIES];
} LocationCache;
static LocationCache location_caches[LOCATION_CACHE_SLOTS];

static unsigned int name_hash(const char *name)
{
    unsigned int hash = 2166136261u;
    for (; *name; ++name) {
        hash ^= (unsigned char)*name;
        hash *= 16777619u;
    }
    return hash;
}

static LocationCache *location_cache(unsigned int id, bool create)
{
    LocationCache *free_slot = NULL;
    for (int i = 0; i < LOCATION_CACHE_SLOTS; ++i) {
        if (location_caches[i].id == id)
            return &location_caches[i];
        if (!location_caches[i].id && !free_slot)
            free_slot = &location_caches[i];
    }
    if (!create || !free_slot)
        return NULL;
    *free_slot = (LocationCache) { .id = id };
    return free_slot;
}

static void forget_locations(Shader shader)
{
    LocationCache *cache = shader.id ? location_cache(shader.id, false) : NULL;
    if (cache)
        *cache = (LocationCache) { 0 };
}

static int cached_location(Shader shader, const char *name)
{
    if (shader.id == 0)
        return -1;
    LocationCache *cache = location_cache(shader.id, true);
    unsigned int hash = name_hash(name);
    if (cache) {
        for (int i = 0; i < cache->count; ++i)
            if (cache->entries[i].hash == hash && strcmp(cache->entries[i].name, name) == 0)
                return cache->entries[i].location;
    }
    int location = GetShaderLocation(shader, name);
    if (cache && cache->count < LOCATION_CACHE_ENTRIES) {
        LocationEntry *entry = &cache->entries[cache->count++];
        snprintf(entry->name, sizeof(entry->name), "%s", name);
        entry->hash = hash;
        entry->location = location;
    }
    return location;
}

static const char *const known_uniforms[] = { "objectColor", "emissive", "instanced",
    "ghostOpacity", "ghostPixelScale", "groundPass", "facadePass", "reflectionPass", "ambientColor",
    "directionalColor", "directionalDirection", "directionalIntensity", "shadowsEnabled",
    "lightViewProjection", "shadowMap", "farShadowsEnabled", "farLightViewProjection",
    "farShadowMap", "localLightCount", "localLightPositionRadius[0]", "localLightColorIntensity[0]",
    "localLightDirection[0]", "localLightCone[0]", "cameraPosition", "fogColor", "fogStart",
    "fogEnd", "fogOpacity", "atmosphereEnabled", "objectWakeEnabled", "objectWakeLife",
    "wakeFieldEnabled", "wakeGrid", "wakeField", "physicalWaves", "radialOrigin", "radialMotion",
    "stormArea", "stormPhase", "stormMotion", "vesselOrigin", "vesselMotion", "vesselShape",
    "wakeAmplitude", "wakeOffset", "objectWakePlayerCount", "objectWakeTrafficLife",
    "objectWakeData", "playerWakeCount", "playerWakeOrigins[0]", "playerWakeParameters[0]",
    "waterfrontWater", "wavePatch", "waveParameters[0]", "sunDirection", "reflectionViewProjection",
    "crossingWakeTime", "detailRipplePhase", "reflectionRipplePhase", "reflectionsEnabled",
    "reflectionTexture", "surfacePixelScale", "waterPatchPass", "skyForward", "skyRight", "skyUp",
    "skyScale", "skyEye", "squallArea", "squallHeight", NULL };

// Resolve every name the renderer uses for each shader once, while the program is valid.
static void prewarm_locations(Shader shader)
{
    for (const char *const *name = known_uniforms; *name; ++name)
        (void)cached_location(shader, *name);
}

static WaterfrontGpuScene *drawing_scene;

static bool object_batch_create(WaterfrontGpuScene *scene);

// Counter for the pass and group being drawn, set only inside waterfront_gpu_scene_draw on the
// render thread. NULL outside a scene draw, when submissions are not counted.
static WaterfrontGpuStats *counting;
static WaterfrontGpuPass counting_pass;

static void submit(Mesh mesh, Material material, Matrix transform, WaterfrontGpuGroup group)
{
    DrawMesh(mesh, material, transform);
    if (counting) {
        WaterfrontGpuCount *count = &counting->mesh[counting_pass][group];
        ++count->submissions;
        ++count->instances;
        count->triangles += (size_t)mesh.triangleCount;
    }
}

static void draw_map_mesh(Mesh mesh, Material material)
{
    if (mesh.vaoId != 0)
        submit(mesh, material, MatrixIdentity(), WATERFRONT_GPU_GROUP_MAP);
}

static void tiled_destroy(WaterfrontGpuTiled *tiled)
{
    for (int i = 0; i < tiled->count; ++i)
        unload_mesh(&tiled->meshes[i]);
    free(tiled->meshes);
    free(tiled->bounds);
    *tiled = (WaterfrontGpuTiled) { 0 };
}

int waterfront_gpu_tiled_vertices(const WaterfrontGpuTiled *tiled)
{
    int count = 0;
    for (int i = 0; i < tiled->count; ++i)
        count += tiled->meshes[i].vertexCount;
    return count;
}

// True unless all eight corners of BOX lie outside one clip plane of VIEW_PROJECTION, raylib's
// view * projection (also valid for corners behind a perspective camera).
static bool box_in_view(Matrix m, BoundingBox box)
{
    int outside[6] = { 0 };
    for (int i = 0; i < 8; ++i) {
        Vector3 p = { i & 1 ? box.max.x : box.min.x, i & 2 ? box.max.y : box.min.y,
            i & 4 ? box.max.z : box.min.z };
        float x = m.m0 * p.x + m.m4 * p.y + m.m8 * p.z + m.m12;
        float y = m.m1 * p.x + m.m5 * p.y + m.m9 * p.z + m.m13;
        float z = m.m2 * p.x + m.m6 * p.y + m.m10 * p.z + m.m14;
        float w = m.m3 * p.x + m.m7 * p.y + m.m11 * p.z + m.m15;
        outside[0] += x < -w;
        outside[1] += x > w;
        outside[2] += y < -w;
        outside[3] += y > w;
        outside[4] += z < -w;
        outside[5] += z > w;
    }
    for (int i = 0; i < 6; ++i)
        if (outside[i] == 8)
            return false;
    return true;
}

// Draws the tiles of one map class inside the current 3D pass's view volume.
static void draw_tiled(
    const WaterfrontGpuScene *scene, const WaterfrontGpuTiled *tiled, Material material)
{
    Matrix view_projection = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    for (int i = 0; i < tiled->count; ++i) {
        if (scene->culling_disabled || box_in_view(view_projection, tiled->bounds[i]))
            submit(tiled->meshes[i], material, MatrixIdentity(), WATERFRONT_GPU_GROUP_MAP);
        else if (counting)
            ++counting->tiles_culled[counting_pass];
    }
}

// One builder per render tile for a map class.
typedef struct {
    MeshBuilder *tiles;
    size_t count;
} TiledBuilder;

static bool tiled_builder_init(TiledBuilder *builder, size_t tiles)
{
    builder->tiles = calloc(tiles, sizeof(*builder->tiles));
    builder->count = builder->tiles ? tiles : 0;
    return builder->tiles != NULL;
}

static void tiled_builder_destroy(TiledBuilder *builder)
{
    for (size_t i = 0; i < builder->count; ++i)
        builder_destroy(&builder->tiles[i]);
    free(builder->tiles);
    *builder = (TiledBuilder) { 0 };
}

static size_t builder_vertices(const TiledBuilder *builder)
{
    size_t count = 0;
    for (size_t i = 0; i < builder->count; ++i)
        count += (size_t)builder->tiles[i].count;
    return count;
}

// Uploads each non-empty tile with its bounds, grown slightly so that rounding never culls a
// visible edge. Consumes the builder, also on failure.
static bool tiled_builder_finish(TiledBuilder *builder, WaterfrontGpuTiled *tiled)
{
    *tiled = (WaterfrontGpuTiled) { 0 };
    int used = 0;
    for (size_t i = 0; i < builder->count; ++i)
        used += builder->tiles[i].count > 0;
    bool ok = true;
    if (used) {
        tiled->meshes = calloc((size_t)used, sizeof(*tiled->meshes));
        tiled->bounds = calloc((size_t)used, sizeof(*tiled->bounds));
        ok = tiled->meshes && tiled->bounds;
    }
    for (size_t i = 0; ok && i < builder->count; ++i) {
        MeshBuilder *tile = &builder->tiles[i];
        if (!tile->count)
            continue;
        Vector3 low = { INFINITY, INFINITY, INFINITY }, high = { -INFINITY, -INFINITY, -INFINITY };
        for (int v = 0; v < tile->count; ++v) {
            Vector3 p = { tile->positions[v * 3], tile->positions[v * 3 + 1],
                tile->positions[v * 3 + 2] };
            low = Vector3Min(low, p);
            high = Vector3Max(high, p);
        }
        const Vector3 grow = { .05f, .05f, .05f };
        tiled->bounds[tiled->count]
            = (BoundingBox) { Vector3Subtract(low, grow), Vector3Add(high, grow) };
        tiled->meshes[tiled->count] = builder_finish(tile);
        ok = tiled->meshes[tiled->count++].vaoId != 0;
    }
    tiled_builder_destroy(builder);
    if (!ok)
        tiled_destroy(tiled);
    return ok;
}

static size_t tile_of(const WaterfrontScenery *scenery, Vector3 a, Vector3 b, Vector3 c)
{
    return boat_map_tiles_owner(
        &scenery->tiles, (Vector2) { (a.x + b.x + c.x) / 3, (a.z + b.z + c.z) / 3 });
}

static bool build_source_mesh(Mesh *mesh, const Mesh3D *source)
{
    MeshBuilder builder = { 0 };
    for (size_t i = 0; i < source->triangle_count; ++i) {
        const Triangle3D *t = &source->triangles[i];
        if (!builder_triangle(&builder, source->vertices[t->vertices[0]],
                source->vertices[t->vertices[1]], source->vertices[t->vertices[2]], t->color)) {
            builder_destroy(&builder);
            return false;
        }
    }
    *mesh = builder_finish(&builder);
    return mesh->vaoId != 0;
}

static void update_flag_mesh(Mesh *mesh, const BoatFlagSurface *surface, unsigned int skin)
{
    BoatFlagMesh storage;
    Mesh3D source = boat_flag_skin_mesh(surface, skin, &storage);
    if (!mesh->vaoId) {
        if (!build_source_mesh(mesh, &source))
            return;
    }
    for (size_t i = 0; i < source.triangle_count; ++i) {
        const Triangle3D *triangle = &source.triangles[i];
        Vector3 a = source.vertices[triangle->vertices[0]];
        Vector3 b = source.vertices[triangle->vertices[1]];
        Vector3 c = source.vertices[triangle->vertices[2]];
        Vector3 normal
            = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        for (int j = 0; j < 3; ++j) {
            size_t v = i * 3 + j;
            memcpy(
                &mesh->vertices[v * 3], &source.vertices[triangle->vertices[j]], sizeof(Vector3));
            memcpy(&mesh->normals[v * 3], &normal, sizeof(Vector3));
            memcpy(&mesh->colors[v * 4], &triangle->color, sizeof(Color));
        }
    }
    UpdateMeshBuffer(*mesh, 0, mesh->vertices, mesh->vertexCount * 3 * (int)sizeof(float), 0);
    UpdateMeshBuffer(*mesh, 2, mesh->normals, mesh->vertexCount * 3 * (int)sizeof(float), 0);
    UpdateMeshBuffer(*mesh, 3, mesh->colors, mesh->vertexCount * 4, 0);
}

typedef enum {
    CITY_SURFACE_FLAT,
    CITY_SURFACE_FACADE,
    CITY_SURFACE_ROOF,
    CITY_SURFACE_QUAY,
} CitySurface;

static bool builder_ground_triangle(MeshBuilder *builder, Vector3 a, Vector3 b, Vector3 c,
    Color color, WaterfrontGroundMaterial material)
{
    if (!builder_triangle(builder, a, b, c, color))
        return false;
    for (int i = builder->count - 3; i < builder->count; ++i) {
        builder->texcoords[i * 2] = (float)material;
        builder->texcoords[i * 2 + 1] = 0;
    }
    return true;
}

// Each box belongs to the tile owning its centre.
static bool build_corner(const WaterfrontScenery *scenery, WaterfrontGpuTiled *opaque,
    WaterfrontGpuTiled *emissive, TiledBuilder *ground)
{
    size_t count = scenery->count;
    const WaterfrontCornerBox *boxes = scenery->boxes;
    static const unsigned char faces[][3]
        = { { 0, 2, 1 }, { 0, 3, 2 }, { 5, 7, 4 }, { 5, 6, 7 }, { 4, 3, 0 }, { 4, 7, 3 },
              { 1, 6, 5 }, { 1, 2, 6 }, { 3, 6, 2 }, { 3, 7, 6 }, { 4, 1, 5 }, { 4, 0, 1 } };
    TiledBuilder builders[2] = { 0 };
    if (!tiled_builder_init(&builders[0], ground->count)
        || !tiled_builder_init(&builders[1], ground->count)) {
        tiled_builder_destroy(&builders[0]);
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        Vector3 center = { 0 };
        for (int v = 0; v < 8; ++v)
            center = Vector3Add(center, Vector3Scale(boxes[i].vertices[v], 1 / 8.0f));
        size_t tile = boat_map_tiles_owner(&scenery->tiles, (Vector2) { center.x, center.z });
        for (size_t f = 0; f < sizeof(faces) / sizeof(faces[0]); ++f) {
            Vector3 a = boxes[i].vertices[faces[f][0]], b = boxes[i].vertices[faces[f][1]];
            Vector3 c = boxes[i].vertices[faces[f][2]];
            Color color = boxes[i].color;
            Vector3 normal = Vector3Normalize(
                Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
            int finish = 0;
            // The static kit's surface palette is semantic, like the retained map's.
            // Only tops at ground level: never texture benches, tree crowns, or kerb sides.
            if (!boxes[i].emissive && normal.y > .95f && a.y < 3.9f) {
                if (ColorIsEqual(color, (Color) { 171, 171, 158, 255 }))
                    finish = WATERFRONT_GROUND_PAVING;
                else if (ColorIsEqual(color, (Color) { 76, 109, 73, 255 }))
                    finish = WATERFRONT_GROUND_PLANTING;
                else if (ColorIsEqual(color, (Color) { 98, 106, 112, 255 }))
                    finish = WATERFRONT_GROUND_ASPHALT;
            }
            bool ok = finish
                ? builder_ground_triangle(&ground->tiles[tile], a, b, c, color, finish)
                : builder_triangle(&builders[boxes[i].emissive].tiles[tile], a, b, c, color);
            if (!ok) {
                tiled_builder_destroy(&builders[0]);
                tiled_builder_destroy(&builders[1]);
                return false;
            }
        }
    }
    bool opaque_ok = tiled_builder_finish(&builders[0], opaque);
    bool emissive_ok = tiled_builder_finish(&builders[1], emissive);
    if (!opaque_ok || !emissive_ok) {
        tiled_destroy(opaque);
        tiled_destroy(emissive);
        return false;
    }
    return true;
}

// Only imported buildings take roof and facade materials, by colour and measured from their own
// base, so terrain and shore walls at any height stay flat.
static CitySurface city_surface(Vector3 a, Vector3 b, Vector3 c, Color color)
{
    Vector3 normal
        = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
    float top = fmaxf(a.y, fmaxf(b.y, c.y));
    float bottom = fminf(a.y, fminf(b.y, c.y));
    if (normal.y > .95f)
        return top > 4.0f && waterfront_building_roof(color) ? CITY_SURFACE_ROOF
                                                             : CITY_SURFACE_FLAT;
    if (fabsf(normal.y) < .15f && waterfront_building_wall(color) && top - bottom >= 5.5f)
        return CITY_SURFACE_FACADE;
    return CITY_SURFACE_FLAT;
}

static bool is_water_triangle(const Triangle3D *triangle)
{
    return triangle->color.r == WATER_COLOR.r && triangle->color.g == WATER_COLOR.g
        && triangle->color.b == WATER_COLOR.b;
}

static bool builder_facade_triangle(
    MeshBuilder *builder, Vector3 a, Vector3 b, Vector3 c, Color source)
{
    // Generated wall pairs are a,c,b and a,d,c: vertex zero is the same lower
    // corner, and the last vertex reaches the other end in both triangles. Texture rows start
    // at the wall's base, which is 3.5 m in the river.
    Vector3 edge = { c.x - a.x, 0, c.z - a.z };
    float length = Vector3Length(edge);
    if (length < .001f)
        return false;
    Vector3 along = Vector3Scale(edge, 1 / length);
    // The retained palette is stable per building (including its mapped parts).
    // Artistic material assignments, not claims about surveyed facade finishes.
    Color color = { 208, 198, 175, WATERFRONT_FACADE_PLASTER };
    if (source.r == 185)
        color = (Color) { 170, 104, 77, WATERFRONT_FACADE_BRICK };
    else if (source.r == 190)
        color = (Color) { 183, 154, 108, WATERFRONT_FACADE_BRICK };
    else if (source.r == 154)
        color = (Color) { 147, 165, 174, WATERFRONT_FACADE_PANEL };
    if (!builder_triangle(builder, a, b, c, color))
        return false;
    Vector3 points[] = { a, b, c };
    for (int i = 0; i < 3; ++i) {
        int vertex = builder->count - 3 + i;
        builder->texcoords[vertex * 2] = Vector3DotProduct(Vector3Subtract(points[i], a), along);
        builder->texcoords[vertex * 2 + 1] = points[i].y - a.y;
    }
    return true;
}

// Land triangles go to the tile owning their centroid, the catalogue's rule. The second
// triangle of each generated wall pair joins the first's tile, so that every facade mesh keeps
// its a,c,b / a,d,c pairs adjacent.
static bool build_city(const WaterfrontScenery *scenery, WaterfrontGpuTiled *city_mesh,
    WaterfrontGpuTiled *facades, WaterfrontGpuTiled *roofs, WaterfrontGpuTiled *quay, Mesh *water,
    WaterfrontGpuTiled *corner, WaterfrontGpuTiled *corner_emissive)
{
    const Mesh3D *city = scenery->full->mesh;
    size_t tiles = boat_map_tiles_count(&scenery->tiles);
    TiledBuilder builders[4] = { 0 };
    MeshBuilder water_builder = { 0 };
    size_t facade_count = 0, facade_tile = 0;
    for (size_t i = 0; i < sizeof(builders) / sizeof(builders[0]); ++i)
        if (!tiled_builder_init(&builders[i], tiles))
            goto failed;
    for (size_t i = 0; i < city->triangle_count; ++i) {
        const Triangle3D *triangle = &city->triangles[i];
        Vector3 a = city->vertices[triangle->vertices[0]];
        Vector3 b = city->vertices[triangle->vertices[1]];
        Vector3 c = city->vertices[triangle->vertices[2]];
        if (is_water_triangle(triangle)) {
            if (!builder_triangle(&water_builder, a, b, c, (Color) { 67, 151, 180, 255 }))
                goto failed;
            continue;
        }
        bool planting = (triangle->color.r == 72 && triangle->color.g == 105)
            || (triangle->color.r == 61 && triangle->color.g == 91);
        // Authored bridge beams retain their flat steel/concrete colors.
        bool bridge = boat_map_is_bridge_triangle(scenery->full, i);
        CitySurface surface
            = planting || bridge ? CITY_SURFACE_FLAT : city_surface(a, b, c, triangle->color);
        Vector3 normal
            = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        bool road = ColorIsEqual(triangle->color, (Color) { 83, 89, 99, 255 });
        bool land = ColorIsEqual(triangle->color, (Color) { 135, 139, 144, 255 });
        size_t tile = tile_of(scenery, a, b, c);
        // Roads and planting painted on terrain (the outer map) keep their finish at any
        // height; the bridge deck's road colour stays out of it.
        float top = fmaxf(a.y, fmaxf(b.y, c.y));
        if (normal.y > .95f
            && (((road || land || planting) && top < 4.3f) || ((road || planting) && !bridge))) {
            int finish = planting ? WATERFRONT_GROUND_PLANTING : WATERFRONT_GROUND_CONCRETE;
            if (road)
                finish = WATERFRONT_GROUND_ASPHALT;
            if (!builder_ground_triangle(
                    &builders[CITY_SURFACE_QUAY].tiles[tile], a, b, c, triangle->color, finish))
                goto failed;
            continue;
        }
        if (surface == CITY_SURFACE_FACADE) {
            if (facade_count++ % 2)
                tile = facade_tile;
            facade_tile = tile;
        }
        MeshBuilder *builder = &builders[surface].tiles[tile];
        bool ok = surface == CITY_SURFACE_FACADE
            ? builder_facade_triangle(builder, a, b, c, triangle->color)
            : builder_triangle(builder, a, b, c, triangle->color);
        if (!ok)
            goto failed;
    }
    // The compiled river has every surface class; another map needs only its water.
    if (water_builder.count == 0
        || (scenery->map == &waterfront_city_river_map
            && (!builder_vertices(&builders[CITY_SURFACE_FLAT])
                || !builder_vertices(&builders[CITY_SURFACE_FACADE])
                || !builder_vertices(&builders[CITY_SURFACE_ROOF])
                || !builder_vertices(&builders[CITY_SURFACE_QUAY]))))
        goto failed;
    if (!build_corner(scenery, corner, corner_emissive, &builders[CITY_SURFACE_QUAY]))
        goto failed;
    bool ok = tiled_builder_finish(&builders[CITY_SURFACE_FLAT], city_mesh);
    ok = tiled_builder_finish(&builders[CITY_SURFACE_FACADE], facades) && ok;
    ok = tiled_builder_finish(&builders[CITY_SURFACE_ROOF], roofs) && ok;
    ok = tiled_builder_finish(&builders[CITY_SURFACE_QUAY], quay) && ok;
    *water = builder_finish(&water_builder);
    return ok && water->vaoId != 0;
failed:
    for (size_t i = 0; i < sizeof(builders) / sizeof(builders[0]); ++i)
        tiled_builder_destroy(&builders[i]);
    builder_destroy(&water_builder);
    return false;
}

// Windows join the tile of the wall triangle they are derived from.
static bool build_windows(const WaterfrontScenery *scenery, WaterfrontGpuTiled *mesh)
{
    const BoatMapDescription *map = scenery->full;
    const Mesh3D *city = map->mesh;
    TiledBuilder tiled = { 0 };
    if (!tiled_builder_init(&tiled, boat_map_tiles_count(&scenery->tiles)))
        return false;
    for (size_t i = 0; i < city->triangle_count; ++i) {
        if (boat_map_is_bridge_triangle(map, i))
            continue;
        const Triangle3D *triangle = &city->triangles[i];
        Vector3 a = city->vertices[triangle->vertices[0]];
        Vector3 c = city->vertices[triangle->vertices[1]];
        Vector3 b = city->vertices[triangle->vertices[2]];
        if (!waterfront_window_wall(triangle->color, a, c, b))
            continue;
        Vector3 edge = Vector3Subtract(b, a);
        float length = Vector3Length(edge);
        Vector3 along = Vector3Scale(edge, 1 / length);
        Vector3 normal = { along.z, 0, -along.x };
        MeshBuilder *builder = &tiled.tiles[tile_of(scenery, a, c, b)];
        for (int floor = 0; a.y + 2.5f + floor * 3.2f < c.y - 1; ++floor) {
            for (int column = 0; 2.5f + column * 4 < length - 1.5f; ++column) {
                unsigned int seed = (unsigned int)i * 1664525u + (unsigned int)column * 1013904223u
                    + (unsigned int)floor * 374761393u;
                Color color = seed % 7 < 3 ? (Color) { 138, 185, 204, 255 }
                    : seed % 7 == 3        ? (Color) { 181, 211, 221, 255 }
                                           : (Color) { 66, 100, 120, 255 };
                Vector3 p = Vector3Add(Vector3Add(a, Vector3Scale(along, 2.5f + column * 4)),
                    Vector3Scale(normal, .035f));
                p.y = a.y + 1.5f + floor * 3.2f;
                Vector3 q = Vector3Add(p, Vector3Scale(along, floor == 0 ? 2.2f : 1.5f));
                Vector3 up = { 0, floor == 0 ? 1.9f : 1.5f, 0 };
                if (!builder_quad(builder, p, Vector3Add(p, up), Vector3Add(q, up), q, color)) {
                    tiled_builder_destroy(&tiled);
                    return false;
                }
            }
        }
    }
    return tiled_builder_finish(&tiled, mesh);
}

static Vector3 crane_point(float x, float y, float z)
{
    const float angle = .056f;
    return (Vector3) { x * cosf(angle) - z * sinf(angle), y, x * sinf(angle) + z * cosf(angle) };
}

// Each foot stands on the kit's ground; the gantry keeps its authored height.
static bool build_crane(Mesh *mesh, const WaterfrontScenery *scenery)
{
    MeshBuilder builder = { 0 };
    for (int side = -1; side <= 1; side += 2) {
        float sill = 1.5f;
        for (int face = -1; face <= 1; face += 2) {
            Vector3 foot = crane_point(side * 49.0f, 0, face * 5.0f);
            foot.y = waterfront_scenery_ground(scenery, foot.x, foot.z);
            sill += foot.y * .5f;
            if (!builder_box(&builder, foot, crane_point(side * 45, 73, face * 3), 3, 3))
                goto failed;
        }
        if (!builder_box(
                &builder, crane_point(side * 49, sill, -5), crane_point(side * 49, sill, 5), 4, 4)
            || !builder_box(
                &builder, crane_point(side * 47, 35, -4), crane_point(side * 47, 35, 4), 2, 2))
            goto failed;
    }
    if (!builder_box(&builder, crane_point(-55, 76, 0), crane_point(55, 76, 0), 8, 6)
        || !builder_box(&builder, crane_point(-15, 81, 0), crane_point(3, 81, 0), 8, 4)
        || !builder_box(&builder, crane_point(5, 75, 0), crane_point(5, 48, 0), .4f, .4f)
        || !builder_box(&builder, crane_point(2, 48, 0), crane_point(8, 48, 0), 1.5f, 1.5f))
        goto failed;
    *mesh = builder_finish(&builder);
    return mesh->vaoId != 0;
failed:
    builder_destroy(&builder);
    return false;
}

static bool build_unit_cube(Mesh *mesh)
{
    static const Vector3 points[] = {
        { -.5f, -.5f, -.5f },
        { .5f, -.5f, -.5f },
        { .5f, .5f, -.5f },
        { -.5f, .5f, -.5f },
        { -.5f, -.5f, .5f },
        { .5f, -.5f, .5f },
        { .5f, .5f, .5f },
        { -.5f, .5f, .5f },
    };
    static const unsigned char faces[][3] = {
        { 0, 2, 1 },
        { 0, 3, 2 },
        { 5, 7, 4 },
        { 5, 6, 7 },
        { 4, 3, 0 },
        { 4, 7, 3 },
        { 1, 6, 5 },
        { 1, 2, 6 },
        { 3, 6, 2 },
        { 3, 7, 6 },
        { 4, 1, 5 },
        { 4, 0, 1 },
    };
    MeshBuilder builder = { 0 };
    for (size_t i = 0; i < sizeof(faces) / sizeof(faces[0]); ++i) {
        if (!builder_triangle(
                &builder, points[faces[i][0]], points[faces[i][1]], points[faces[i][2]], WHITE)) {
            builder_destroy(&builder);
            return false;
        }
    }
    *mesh = builder_finish(&builder);
    return mesh->vaoId != 0;
}

static bool build_unit_plane(Mesh *mesh)
{
    MeshBuilder builder = { 0 };
    if (!builder_quad(&builder, (Vector3) { -.5f, 0, -.5f }, (Vector3) { -.5f, 0, .5f },
            (Vector3) { .5f, 0, .5f }, (Vector3) { .5f, 0, -.5f }, WHITE)) {
        builder_destroy(&builder);
        return false;
    }
    *mesh = builder_finish(&builder);
    return mesh->vaoId != 0;
}

static bool load_detail_texture(
    Texture2D *texture, const char *fragment_shader_path, const char *asset_name)
{
    const char *slash = strrchr(fragment_shader_path, '/');
    if (slash == NULL || (size_t)(slash - fragment_shader_path) > 900)
        return false;
    char texture_path[1024];
    snprintf(texture_path, sizeof(texture_path), "%.*s/../textures/%s",
        (int)(slash - fragment_shader_path), fragment_shader_path, asset_name);
    *texture = LoadTexture(texture_path);
    if (texture->id == 0)
        return false;
    SetTextureFilter(*texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(*texture, TEXTURE_WRAP_REPEAT);
    return true;
}

static bool init_detail_material(Material *material, Shader shader, Texture2D texture)
{
    *material = LoadMaterialDefault();
    if (material->maps == NULL)
        return false;
    material->shader = shader;
    material->maps[MATERIAL_MAP_DIFFUSE].texture = texture;
    return true;
}

static Shader load_city_shader(const char *fragment_shader_path)
{
    const char *slash = strrchr(fragment_shader_path, '/');
    if (slash == NULL || (size_t)(slash - fragment_shader_path) > 900)
        return (Shader) { 0 };
    char vertex_shader_path[1024];
    snprintf(vertex_shader_path, sizeof(vertex_shader_path), "%.*s/waterfront_city.vs",
        (int)(slash - fragment_shader_path), fragment_shader_path);
    return LoadShader(vertex_shader_path, fragment_shader_path);
}

static Shader load_shadow_shader(const char *fragment_shader_path)
{
    const char *slash = strrchr(fragment_shader_path, '/');
    if (slash == NULL || (size_t)(slash - fragment_shader_path) > 900)
        return (Shader) { 0 };
    char vertex_shader_path[1024];
    char fragment_path[1024];
    snprintf(vertex_shader_path, sizeof(vertex_shader_path), "%.*s/waterfront_shadow.vs",
        (int)(slash - fragment_shader_path), fragment_shader_path);
    snprintf(fragment_path, sizeof(fragment_path), "%.*s/waterfront_shadow.fs",
        (int)(slash - fragment_shader_path), fragment_shader_path);
    return LoadShader(vertex_shader_path, fragment_path);
}

static Shader load_water_shader(const char *fragment_shader_path)
{
    const char *slash = strrchr(fragment_shader_path, '/');
    if (slash == NULL || (size_t)(slash - fragment_shader_path) > 900)
        return (Shader) { 0 };
    char vertex_shader_path[1024];
    char fragment_path[1024];
    snprintf(vertex_shader_path, sizeof(vertex_shader_path), "%.*s/waterfront_water.vs",
        (int)(slash - fragment_shader_path), fragment_shader_path);
    snprintf(fragment_path, sizeof(fragment_path), "%.*s/waterfront_water.fs",
        (int)(slash - fragment_shader_path), fragment_shader_path);
    return LoadShader(vertex_shader_path, fragment_path);
}

static Shader load_sky_shader(const char *fragment_shader_path)
{
    const char *slash = strrchr(fragment_shader_path, '/');
    if (slash == NULL || (size_t)(slash - fragment_shader_path) > 900)
        return (Shader) { 0 };
    char path[1024];
    snprintf(path, sizeof(path), "%.*s/waterfront_sky.fs", (int)(slash - fragment_shader_path),
        fragment_shader_path);
    return LoadShader(NULL, path);
}

static bool create_shadow_map(RenderTexture2D *target, int size)
{
    *target = (RenderTexture2D) { 0 };
    target->id = rlLoadFramebuffer();
    if (target->id == 0)
        return false;
    target->texture.width = size;
    target->texture.height = size;
    target->depth.id = rlLoadTextureDepth(size, size, false);
    target->depth.width = size;
    target->depth.height = size;
    target->depth.mipmaps = 1;
    if (target->depth.id == 0) {
        rlUnloadFramebuffer(target->id);
        *target = (RenderTexture2D) { 0 };
        return false;
    }
    rlEnableFramebuffer(target->id);
    rlFramebufferAttach(
        target->id, target->depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    bool complete = rlFramebufferComplete(target->id);
    rlDisableFramebuffer();
    if (!complete) {
        rlUnloadFramebuffer(target->id);
        *target = (RenderTexture2D) { 0 };
        return false;
    }
    SetTextureFilter(target->depth, TEXTURE_FILTER_POINT);
    return true;
}

static void unbind_map(WaterfrontGpuScene *scene)
{
    tiled_destroy(&scene->city);
    tiled_destroy(&scene->facades);
    tiled_destroy(&scene->roofs);
    tiled_destroy(&scene->quay);
    unload_mesh(&scene->water);
    tiled_destroy(&scene->windows);
    tiled_destroy(&scene->corner);
    tiled_destroy(&scene->corner_emissive);
    unload_mesh(&scene->crane);
    scene->map = NULL;
    scene->river = false;
    scene->cafe_lift = 0;
}

bool waterfront_gpu_scene_bind(WaterfrontGpuScene *scene, const WaterfrontScenery *scenery)
{
    unbind_map(scene);
    if (!scenery->ready)
        return false;
    if (!scenery->map)
        return true;
    if (!build_city(scenery, &scene->city, &scene->facades, &scene->roofs, &scene->quay,
            &scene->water, &scene->corner, &scene->corner_emissive)
        || !build_windows(scenery, &scene->windows)
        || (scenery->river && !build_crane(&scene->crane, scenery))) {
        unbind_map(scene);
        return false;
    }
    scene->map = scenery->map;
    scene->river = scenery->river;
    Vector3 cafe = waterfront_corner_position(-2.8f, 0, -6);
    scene->cafe_lift = waterfront_scenery_ground(scenery, cafe.x, cafe.z) - 3.5f;
    return true;
}

bool waterfront_gpu_scene_init(WaterfrontGpuScene *scene, const char *fragment_shader_path)
{
    *scene = (WaterfrontGpuScene) { 0 };
    scene->shader = load_city_shader(fragment_shader_path);
    scene->shadow_shader = load_shadow_shader(fragment_shader_path);
    scene->water_shader = load_water_shader(fragment_shader_path);
    scene->sky_shader = load_sky_shader(fragment_shader_path);
    if (!IsShaderValid(scene->shader) || scene->shader.id == rlGetShaderIdDefault()
        || !IsShaderValid(scene->shadow_shader) || scene->shadow_shader.id == rlGetShaderIdDefault()
        || !IsShaderValid(scene->water_shader) || scene->water_shader.id == rlGetShaderIdDefault()
        || !IsShaderValid(scene->sky_shader) || scene->sky_shader.id == rlGetShaderIdDefault())
        goto failed;
    scene->sky_texture = LoadTextureFromImage(waterfront_sky_image());
    if (!IsTextureValid(scene->sky_texture))
        goto failed;
    SetTextureFilter(scene->sky_texture, TEXTURE_FILTER_POINT);
    scene->city_material = LoadMaterialDefault();
    scene->life_material = LoadMaterialDefault();
    scene->shadow_material = LoadMaterialDefault();
    scene->water_material = LoadMaterialDefault();
    if (scene->city_material.maps == NULL || scene->life_material.maps == NULL
        || scene->shadow_material.maps == NULL || scene->water_material.maps == NULL)
        goto failed;
    scene->city_material.shader = scene->shader;
    scene->life_material.shader = scene->shader;
    scene->shadow_material.shader = scene->shadow_shader;
    scene->water_material.shader = scene->water_shader;
    if (!load_detail_texture(&scene->roof_texture, fragment_shader_path, "roof.png")
        || !init_detail_material(&scene->roof_material, scene->shader, scene->roof_texture))
        goto failed;
    for (int part = 0; part < WATERFRONT_GULL_PARTS; ++part)
        if (!build_source_mesh(&scene->gull[part], waterfront_gull_mesh(part)))
            goto failed;
    for (unsigned int skin = 0; skin < BOAT_SKIN_COUNT; ++skin)
        if (!build_source_mesh(&scene->player_hull[skin], boat_skin_mesh(skin, BOAT_SKIN_HULL))
            || !build_source_mesh(
                &scene->player_cabin[skin], boat_skin_mesh(skin, BOAT_SKIN_COCKPIT))
            || !build_source_mesh(
                &scene->player_engine[skin], boat_skin_mesh(skin, BOAT_SKIN_ENGINE)))
            goto failed;
    for (int i = 0; i < WATERFRONT_TRAFFIC_MESH_SLOTS; ++i)
        if (!build_source_mesh(&scene->traffic_boats[i], waterfront_traffic_mesh_slot(i)))
            goto failed;
    if (!build_source_mesh(&scene->lift_span, waterfront_upstream_span_mesh()))
        goto failed;
    for (int i = 0; i < WATERFRONT_HARBOR_BOATS; ++i)
        if (!build_source_mesh(&scene->harbor_boats[i], waterfront_harbor_boat_mesh(i)))
            goto failed;
    if (!build_source_mesh(&scene->harbor_pontoon, waterfront_harbor_pontoon_mesh())
        || !build_source_mesh(&scene->harbor_small_pontoon,
            waterfront_harbor_module_mesh(BOAT_SCENE_MODULE_PONTOON_SMALL))
        || !build_source_mesh(&scene->harbor_quay, waterfront_harbor_quay_mesh())
        || !build_source_mesh(&scene->harbor_ferry, waterfront_harbor_ferry_mesh()))
        goto failed;
    if (!build_unit_cube(&scene->bridge) || !build_unit_cube(&scene->cube)
        || !build_unit_plane(&scene->plane))
        goto failed;
    (void)object_batch_create(scene); // Scalar drawing remains valid if this fails.
    scene->shadow_map_size = WATERFRONT_GPU_SHADOW_MAP_SIZE;
    if (!create_shadow_map(&scene->shadow_map, scene->shadow_map_size)
        || !create_shadow_map(&scene->far_shadow_map, WATERFRONT_GPU_FAR_SHADOW_MAP_SIZE))
        goto failed;
    scene->reflection
        = LoadRenderTexture(WATERFRONT_GPU_REFLECTION_WIDTH, WATERFRONT_GPU_REFLECTION_HEIGHT);
    if (!IsRenderTextureValid(scene->reflection))
        goto failed;
    SetTextureFilter(scene->reflection.texture, TEXTURE_FILTER_POINT);
    scene->lighting_enabled = true;
    scene->shadows_enabled = true;
    scene->atmosphere_enabled = true;
    scene->reflections_enabled = true;
    prewarm_locations(scene->shader);
    prewarm_locations(scene->shadow_shader);
    prewarm_locations(scene->water_shader);
    prewarm_locations(scene->sky_shader);
    scene->ready = true;
    return true;
failed:
    waterfront_gpu_scene_destroy(scene);
    return false;
}

static Matrix transform_matrix(Transform3D transform)
{
    // MatrixMultiply(A, B) composes B before A in raymath. This matches the CPU renderer's
    // explicit order: scale a mesh vertex, rotate it, then add its world position.
    return MatrixMultiply(
        MatrixMultiply(MatrixScale(transform.scale.x, transform.scale.y, transform.scale.z),
            MatrixRotateXYZ(transform.rotation)),
        MatrixTranslate(transform.position.x, transform.position.y, transform.position.z));
}

static void set_object_color(Shader shader, Color color)
{
    float values[] = {
        (float)color.r / 255.0f,
        (float)color.g / 255.0f,
        (float)color.b / 255.0f,
        (float)color.a / 255.0f,
    };
    int location = cached_location(shader, "objectColor");
    if (location >= 0)
        SetShaderValue(shader, location, values, SHADER_UNIFORM_VEC4);
}

static void set_emissive(Shader shader, bool emissive)
{
    int location = cached_location(shader, "emissive");
    if (location >= 0) {
        int value = emissive ? 1 : 0;
        SetShaderValue(shader, location, &value, SHADER_UNIFORM_INT);
    }
}

// Local bounds of transformed object meshes, computed once per VAO. Culling uses each pass's own
// view volume; the scalar draw order is unchanged for everything that remains visible.
typedef struct {
    unsigned int vao;
    BoundingBox local;
} ObjectBounds;

typedef struct {
    ObjectBounds *items;
    int count, capacity;
} ObjectBoundsCache;

static bool object_is_culled(WaterfrontGpuScene *scene, const Mesh *mesh, Transform3D transform)
{
    if (scene->culling_disabled || !mesh->vertices || mesh->vertexCount <= 0)
        return false;
    // Cloth vertices change every frame; their first-frame bounds are not valid.
    if (mesh->vaoId == scene->player_flag.vaoId || mesh->vaoId == scene->ghost_flag.vaoId)
        return false;
    for (int i = 0; i < WATERFRONT_RIVALS; ++i)
        if (mesh->vaoId == scene->rival_flags[i].vaoId)
            return false;
    ObjectBoundsCache *cache = scene->bounds_cache;
    if (!cache) {
        cache = calloc(1, sizeof(*cache));
        if (!cache)
            return false;
        scene->bounds_cache = cache;
    }
    ObjectBounds *bounds = NULL;
    for (int i = 0; i < cache->count; ++i)
        if (cache->items[i].vao == mesh->vaoId) {
            bounds = &cache->items[i];
            break;
        }
    if (!bounds) {
        if (cache->count == cache->capacity) {
            int capacity = cache->capacity ? cache->capacity * 2 : 32;
            ObjectBounds *items = realloc(cache->items, (size_t)capacity * sizeof(*items));
            if (!items)
                return false;
            cache->items = items;
            cache->capacity = capacity;
        }
        bounds = &cache->items[cache->count++];
        bounds->vao = mesh->vaoId;
        Vector3 low = { INFINITY, INFINITY, INFINITY }, high = { -INFINITY, -INFINITY, -INFINITY };
        for (int i = 0; i < mesh->vertexCount; ++i) {
            Vector3 point
                = { mesh->vertices[i * 3], mesh->vertices[i * 3 + 1], mesh->vertices[i * 3 + 2] };
            low = Vector3Min(low, point);
            high = Vector3Max(high, point);
        }
        const Vector3 grow = { .05f, .05f, .05f };
        bounds->local = (BoundingBox) { Vector3Subtract(low, grow), Vector3Add(high, grow) };
    }
    Matrix model = transform_matrix(transform);
    Vector3 low = { INFINITY, INFINITY, INFINITY }, high = { -INFINITY, -INFINITY, -INFINITY };
    for (int i = 0; i < 8; ++i) {
        Vector3 point = { i & 1 ? bounds->local.max.x : bounds->local.min.x,
            i & 2 ? bounds->local.max.y : bounds->local.min.y,
            i & 4 ? bounds->local.max.z : bounds->local.min.z };
        point = Vector3Transform(point, model);
        low = Vector3Min(low, point);
        high = Vector3Max(high, point);
    }
    Matrix view_projection = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    return !box_in_view(view_projection, (BoundingBox) { low, high });
}

// Per-instance attributes, laid out for the fixed locations in waterfront_city.vs and
// waterfront_shadow.vs. Values are the exact floats the scalar path computes for its uniforms.
typedef struct {
    float mvp[16];
    float model_rows[12];
    float normal[9];
    float color[4];
    float emissive;
} ObjectInstance;

enum {
    OBJECT_INSTANCE_CAPACITY = 8192,
    OBJECT_RUN_CAPACITY = 1024,
    INSTANCE_LOCATION_MVP = 4,
    INSTANCE_LOCATION_MODEL = 8,
    INSTANCE_LOCATION_NORMAL = 11,
    INSTANCE_LOCATION_COLOR = 14,
    INSTANCE_LOCATION_EMISSIVE = 15,
    INSTANCE_LOCATION_END = 16,
};

// One run draws contiguous submissions of one mesh, so the scalar draw order is unchanged.
typedef struct {
    unsigned int vao;
    int vertex_count, triangle_count;
    bool indexed;
    int first, count;
} ObjectRun;

// Objects recorded between object_batch_begin and object_batch_end. A batch uses one material and
// spans no other state change. Storage is fixed, and a full batch replays and continues in order.
typedef struct {
    ObjectInstance *instances;
    ObjectRun *runs;
    unsigned int *prepared_vaos;
    int instance_count, run_count, prepared_count, prepared_capacity;
    unsigned int buffer;
    int buffer_cursor;
    bool recording;
    Material material;
    Matrix view, projection;
} ObjectBatch;

static bool object_batch_create(WaterfrontGpuScene *scene)
{
    // Instanced programs must resolve to the shared VAO layout; otherwise stay scalar.
    if (rlGetLocationAttrib(scene->shader.id, "instanceMvp") != INSTANCE_LOCATION_MVP
        || rlGetLocationAttrib(scene->shadow_shader.id, "instanceMvp") != INSTANCE_LOCATION_MVP)
        return false;
    ObjectBatch *batch = calloc(1, sizeof(*batch));
    if (!batch)
        return false;
    batch->instances = malloc(OBJECT_INSTANCE_CAPACITY * sizeof(*batch->instances));
    batch->runs = malloc(OBJECT_RUN_CAPACITY * sizeof(*batch->runs));
    batch->buffer = batch->instances && batch->runs
        ? rlLoadVertexBuffer(NULL, OBJECT_INSTANCE_CAPACITY * sizeof(ObjectInstance), true)
        : 0;
    if (!batch->buffer) {
        free(batch->instances);
        free(batch->runs);
        free(batch);
        return false;
    }
    scene->object_batch = batch;
    return true;
}

static void object_batch_destroy(WaterfrontGpuScene *scene)
{
    ObjectBatch *batch = scene->object_batch;
    if (!batch)
        return;
    rlUnloadVertexBuffer(batch->buffer);
    free(batch->instances);
    free(batch->runs);
    free(batch->prepared_vaos);
    free(batch);
    scene->object_batch = NULL;
}

// Points VAO's instance attributes at FIRST in the shared buffer. The VAO keeps the layout, and
// scalar draws ignore those attributes while `instanced` is zero.
static void bind_run_attributes(ObjectBatch *batch, unsigned int vao, int first)
{
    const int stride = (int)sizeof(ObjectInstance);
    const int base = first * stride;
    for (int i = 0; i < 4; ++i)
        rlSetVertexAttribute(INSTANCE_LOCATION_MVP + i, 4, RL_FLOAT, false, stride,
            base + (int)offsetof(ObjectInstance, mvp) + i * 4 * (int)sizeof(float));
    for (int i = 0; i < 3; ++i) {
        rlSetVertexAttribute(INSTANCE_LOCATION_MODEL + i, 4, RL_FLOAT, false, stride,
            base + (int)offsetof(ObjectInstance, model_rows) + i * 4 * (int)sizeof(float));
        rlSetVertexAttribute(INSTANCE_LOCATION_NORMAL + i, 3, RL_FLOAT, false, stride,
            base + (int)offsetof(ObjectInstance, normal) + i * 3 * (int)sizeof(float));
    }
    rlSetVertexAttribute(INSTANCE_LOCATION_COLOR, 4, RL_FLOAT, false, stride,
        base + (int)offsetof(ObjectInstance, color));
    rlSetVertexAttribute(INSTANCE_LOCATION_EMISSIVE, 1, RL_FLOAT, false, stride,
        base + (int)offsetof(ObjectInstance, emissive));
    for (int i = 0; i < batch->prepared_count; ++i)
        if (batch->prepared_vaos[i] == vao)
            return;
    for (int location = INSTANCE_LOCATION_MVP; location < INSTANCE_LOCATION_END; ++location) {
        rlEnableVertexAttribute(location);
        rlSetVertexAttributeDivisor(location, 1);
    }
    if (batch->prepared_count == batch->prepared_capacity) {
        int capacity = batch->prepared_capacity ? batch->prepared_capacity * 2 : 32;
        unsigned int *vaos = realloc(batch->prepared_vaos, (size_t)capacity * sizeof(*vaos));
        if (!vaos)
            return; // Prepared again next time; the VAO state above is already valid.
        batch->prepared_vaos = vaos;
        batch->prepared_capacity = capacity;
    }
    batch->prepared_vaos[batch->prepared_count++] = vao;
}

static void object_batch_replay(ObjectBatch *batch)
{
    if (batch->instance_count == 0)
        return;
    int bytes = batch->instance_count * (int)sizeof(ObjectInstance);
    if (batch->buffer_cursor + batch->instance_count > OBJECT_INSTANCE_CAPACITY)
        batch->buffer_cursor = 0;
    rlUpdateVertexBuffer(
        batch->buffer, batch->instances, bytes, batch->buffer_cursor * (int)sizeof(ObjectInstance));
    Shader shader = batch->material.shader;
    int instanced = cached_location(shader, "instanced");
    const int enabled = 1, disabled = 0, slot = 0;
    rlEnableShader(shader.id);
    rlSetUniform(instanced, &enabled, SHADER_UNIFORM_INT, 1);
    rlActiveTextureSlot(0);
    rlEnableTexture(batch->material.maps[MATERIAL_MAP_DIFFUSE].texture.id);
    rlSetUniform(shader.locs[SHADER_LOC_MAP_DIFFUSE], &slot, SHADER_UNIFORM_INT, 1);
    for (int i = 0; i < batch->run_count; ++i) {
        const ObjectRun *run = &batch->runs[i];
        rlEnableVertexArray(run->vao);
        rlEnableVertexBuffer(batch->buffer);
        bind_run_attributes(batch, run->vao, batch->buffer_cursor + run->first);
        if (run->indexed)
            rlDrawVertexArrayElementsInstanced(0, run->triangle_count * 3, 0, run->count);
        else
            rlDrawVertexArrayInstanced(0, run->vertex_count, run->count);
        if (counting) {
            WaterfrontGpuCount *count
                = &counting->mesh[counting_pass][WATERFRONT_GPU_GROUP_OBJECTS];
            ++count->submissions;
            count->instances += (unsigned int)run->count;
            count->triangles += (size_t)run->triangle_count * (size_t)run->count;
        }
    }
    rlDisableVertexArray();
    rlDisableVertexBuffer();
    rlDisableTexture();
    // Later draws see the uniforms the scalar path would have left behind.
    const ObjectInstance *last = &batch->instances[batch->instance_count - 1];
    int emissive = cached_location(shader, "emissive");
    int color = cached_location(shader, "objectColor");
    int emissive_value = (int)last->emissive;
    if (emissive >= 0)
        rlSetUniform(emissive, &emissive_value, SHADER_UNIFORM_INT, 1);
    if (color >= 0)
        rlSetUniform(color, last->color, SHADER_UNIFORM_VEC4, 1);
    rlSetUniform(instanced, &disabled, SHADER_UNIFORM_INT, 1);
    rlDisableShader();
    batch->buffer_cursor += batch->instance_count;
    batch->instance_count = 0;
    batch->run_count = 0;
}

static void object_batch_begin(const WaterfrontGpuScene *scene, Material material)
{
    ObjectBatch *batch = scene->object_batch;
    if (!batch || scene->instancing_disabled)
        return;
    batch->recording = true;
    batch->material = material;
    batch->view = rlGetMatrixModelview();
    batch->projection = rlGetMatrixProjection();
}

static void object_batch_end(const WaterfrontGpuScene *scene)
{
    ObjectBatch *batch = scene->object_batch;
    if (!batch || !batch->recording)
        return;
    object_batch_replay(batch);
    batch->recording = false;
}

static void object_batch_add(ObjectBatch *batch, int instance_limit, Mesh mesh,
    Transform3D transform, Color color, bool emissive, bool shaded)
{
    if (batch->instance_count == (instance_limit ? instance_limit : OBJECT_INSTANCE_CAPACITY)
        || (batch->run_count == OBJECT_RUN_CAPACITY
            && batch->runs[batch->run_count - 1].vao != mesh.vaoId))
        object_batch_replay(batch);
    ObjectRun *run = batch->run_count ? &batch->runs[batch->run_count - 1] : NULL;
    if (!run || run->vao != mesh.vaoId) {
        run = &batch->runs[batch->run_count++];
        *run = (ObjectRun) { .vao = mesh.vaoId,
            .vertex_count = mesh.vertexCount,
            .triangle_count = mesh.triangleCount,
            .indexed = mesh.indices != NULL,
            .first = batch->instance_count };
    }
    ++run->count;
    ObjectInstance *instance = &batch->instances[batch->instance_count++];
    // The same matrix products, in the same order, as DrawMesh computes for its uniforms.
    Matrix model = MatrixMultiply(transform_matrix(transform), rlGetMatrixTransform());
    Matrix mvp = MatrixMultiply(MatrixMultiply(model, batch->view), batch->projection);
    memcpy(instance->mvp, MatrixToFloatV(mvp).v, sizeof(instance->mvp));
    instance->color[0] = (float)color.r / 255.0f;
    instance->color[1] = (float)color.g / 255.0f;
    instance->color[2] = (float)color.b / 255.0f;
    instance->color[3] = (float)color.a / 255.0f;
    instance->emissive = emissive ? 1.0f : 0.0f;
    if (!shaded)
        return; // The shadow program reads only the MVP; the skipped fields are never used.
    const float rows[12] = { model.m0, model.m4, model.m8, model.m12, model.m1, model.m5, model.m9,
        model.m13, model.m2, model.m6, model.m10, model.m14 };
    memcpy(instance->model_rows, rows, sizeof(rows));
    Matrix normal = MatrixTranspose(MatrixInvert(model));
    const float columns[9] = { normal.m0, normal.m1, normal.m2, normal.m4, normal.m5, normal.m6,
        normal.m8, normal.m9, normal.m10 };
    memcpy(instance->normal, columns, sizeof(columns));
}

static void draw_colored_mesh(
    Mesh mesh, Material material, Transform3D transform, Color color, bool emissive)
{
    if (drawing_scene && object_is_culled(drawing_scene, &mesh, transform))
        return;
    ObjectBatch *batch = drawing_scene ? drawing_scene->object_batch : NULL;
    if (batch && batch->recording) {
        object_batch_add(batch, drawing_scene->instance_limit, mesh, transform, color, emissive,
            material.shader.id != drawing_scene->shadow_shader.id);
        return;
    }
    set_object_color(material.shader, color);
    set_emissive(material.shader, emissive);
    material.maps[MATERIAL_MAP_DIFFUSE].color = color;
    submit(mesh, material, transform_matrix(transform), WATERFRONT_GPU_GROUP_OBJECTS);
}

typedef enum {
    LIGHT_CRANE,
    LIGHT_SHOP,
    LIGHT_LAMP,
    LIGHT_HEADLIGHT,
} LightKind;

typedef struct {
    int id;
    WaterfrontGpuLight light;
    float score;
    LightKind kind;
} LightCandidate;

static bool light_was_selected(const WaterfrontGpuScene *scene, int id)
{
    for (int i = 0; i < scene->local_light_count; ++i) {
        if (scene->local_light_ids[i] == id)
            return true;
    }
    return false;
}

static void add_light_candidate(
    LightCandidate *candidates, int *count, int id, WaterfrontGpuLight light, LightKind kind)
{
    if (*count >= 72)
        return;
    candidates[*count] = (LightCandidate) { .id = id, .light = light, .kind = kind };
    ++*count;
}

static int select_light_class(const WaterfrontGpuScene *scene, const LightCandidate *candidates,
    int candidate_count, const WaterfrontSceneFrame *frame, LightKind kind,
    LightCandidate *selected, int capacity)
{
    Vector3 view_direction
        = Vector3Normalize(Vector3Subtract(frame->camera.target, frame->camera.position));
    int selected_count = 0;
    for (int candidate = 0; candidate < candidate_count; ++candidate) {
        LightCandidate current = candidates[candidate];
        if (current.kind != kind)
            continue;
        Vector3 from_camera = Vector3Subtract(current.light.position, frame->camera.position);
        float view_depth = Vector3DotProduct(from_camera, view_direction);
        if (view_depth < -current.light.radius)
            continue;
        // The target is the authored shot focus, unlike the eye position which may sit far away
        // or above the street. It keeps the selected lights relevant to the visible composition.
        current.score = Vector3DistanceSqr(current.light.position, frame->camera.target);
        if (light_was_selected(scene, current.id))
            current.score *= .35f;
        int slot = selected_count;
        while (slot > 0 && current.score < selected[slot - 1].score) {
            if (slot < capacity)
                selected[slot] = selected[slot - 1];
            --slot;
        }
        if (slot < capacity) {
            selected[slot] = current;
            if (selected_count < capacity)
                ++selected_count;
        }
    }
    return selected_count;
}

// Every authored lamp stays in the forward-light list, so its pool cannot pop off while the camera
// approaches. Reserve the cafe light, three crane lights, and nearby moving headlights.
static void select_local_lights(WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame)
{
    if (frame->boating_basin) {
        scene->local_light_count = 0;
        return;
    }
    LightCandidate candidates[72];
    int candidate_count = 0;
    // The cafe and crane fixtures are authored for the river crop.
    if (scene->river) {
        add_light_candidate(candidates, &candidate_count, 40,
            (WaterfrontGpuLight) {
                .position = waterfront_corner_position(-2.8f, 3.1f + scene->cafe_lift, -6),
                .direction = { 0, -1, 0 },
                .color = { 255, 197, 119, 255 },
                .radius = 12,
                .intensity = 1.5f,
                .inner_cone_cos = -1,
                .outer_cone_cos = -1 },
            LIGHT_SHOP);
        add_light_candidate(candidates, &candidate_count, 10,
            (WaterfrontGpuLight) { .position = crane_point(-42, 75, -5),
                .direction = Vector3Normalize(crane_point(.2f, -1, .3f)),
                .color = { 255, 92, 48, 255 },
                .radius = 34,
                .intensity = 1.45f,
                .inner_cone_cos = .78f,
                .outer_cone_cos = .48f },
            LIGHT_CRANE);
        add_light_candidate(candidates, &candidate_count, 11,
            (WaterfrontGpuLight) { .position = crane_point(5, 75, -5),
                .direction = Vector3Normalize(crane_point(0, -1, .15f)),
                .color = { 106, 135, 255, 255 },
                .radius = 26,
                .intensity = 1.35f,
                .inner_cone_cos = .82f,
                .outer_cone_cos = .55f },
            LIGHT_CRANE);
        add_light_candidate(candidates, &candidate_count, 12,
            (WaterfrontGpuLight) { .position = crane_point(40, 75, -5),
                .direction = Vector3Normalize(crane_point(-.2f, -1, .3f)),
                .color = { 255, 78, 40, 255 },
                .radius = 31,
                .intensity = 1.3f,
                .inner_cone_cos = .78f,
                .outer_cone_cos = .48f },
            LIGHT_CRANE);
    }
    for (int street_index = 0; street_index < 2; ++street_index) {
        for (int i = 0; i < frame->lamp_count; ++i) {
            const WaterfrontLampState *lamp = &frame->lamps[i];
            if (lamp->street != street_index)
                continue;
            add_light_candidate(candidates, &candidate_count, lamp->id,
                (WaterfrontGpuLight) { .position = lamp->lens,
                    .direction = { 0, -1, 0 },
                    .color = { 255, 202, 116, 255 },
                    .radius = 18,
                    .intensity = 1.0f,
                    .inner_cone_cos = -1,
                    .outer_cone_cos = -1 },
                LIGHT_LAMP);
        }
        for (int car = 0; !frame->player_active && car < WATERFRONT_CAR_COUNT; ++car) {
            const WaterfrontCarState *state = &frame->cars[car];
            if (state->street != street_index)
                continue;
            for (int side = 0; side < 2; ++side) {
                add_light_candidate(candidates, &candidate_count, state->id + side,
                    (WaterfrontGpuLight) { .position = state->lenses[1][side],
                        .direction = state->headlight_direction,
                        .color = { 255, 233, 174, 255 },
                        .radius = 18,
                        .intensity = 1.25f,
                        .inner_cone_cos = .90f,
                        .outer_cone_cos = .72f },
                    LIGHT_HEADLIGHT);
            }
        }
    }

    LightCandidate selected[WATERFRONT_GPU_MAX_LOCAL_LIGHTS];
    int selected_count
        = select_light_class(scene, candidates, candidate_count, frame, LIGHT_CRANE, selected, 3);
    selected_count += select_light_class(
        scene, candidates, candidate_count, frame, LIGHT_SHOP, &selected[selected_count], 1);
    selected_count += select_light_class(scene, candidates, candidate_count, frame, LIGHT_LAMP,
        &selected[selected_count], WATERFRONT_GPU_MAX_LOCAL_LIGHTS - selected_count - 3);
    selected_count += select_light_class(scene, candidates, candidate_count, frame, LIGHT_HEADLIGHT,
        &selected[selected_count], WATERFRONT_GPU_MAX_LOCAL_LIGHTS - selected_count);
    scene->local_light_count = selected_count;
    for (int i = 0; i < selected_count; ++i) {
        scene->local_light_ids[i] = selected[i].id;
        scene->local_lights[i] = selected[i].light;
    }
}

// Slot 15 is free in both city and water materials (WebGL2 guarantees 16 fragment slots).
static void set_far_shadow_uniforms(const WaterfrontGpuScene *scene, Shader shader)
{
    int enabled = scene->shadows_enabled && scene->far_shadows_active;
    SetShaderValue(
        shader, cached_location(shader, "farShadowsEnabled"), &enabled, SHADER_UNIFORM_INT);
    if (!enabled)
        return;
    SetShaderValueMatrix(shader, cached_location(shader, "farLightViewProjection"),
        scene->far_light_view_projection);
    int slot = 15;
    rlEnableShader(shader.id);
    rlActiveTextureSlot(slot);
    rlEnableTexture(scene->far_shadow_map.depth.id);
    rlSetUniform(cached_location(shader, "farShadowMap"), &slot, SHADER_UNIFORM_INT, 1);
    rlActiveTextureSlot(0);
}

static void set_lighting_uniforms(WaterfrontGpuScene *scene)
{
    static const float ambient_color[] = { .48f, .57f, .65f };
    static const float directional_color[] = { 1.0f, .92f, .74f };
    Vector3 sun = waterfront_sun_direction();
    float directional_direction[] = { sun.x, sun.y, sun.z };
    int location = cached_location(scene->shader, "ambientColor");
    if (location >= 0)
        SetShaderValue(scene->shader, location, ambient_color, SHADER_UNIFORM_VEC3);
    location = cached_location(scene->shader, "directionalColor");
    if (location >= 0)
        SetShaderValue(scene->shader, location, directional_color, SHADER_UNIFORM_VEC3);
    location = cached_location(scene->shader, "directionalDirection");
    if (location >= 0)
        SetShaderValue(scene->shader, location, directional_direction, SHADER_UNIFORM_VEC3);
    location = cached_location(scene->shader, "directionalIntensity");
    if (location >= 0) {
        float intensity = .65f;
        SetShaderValue(scene->shader, location, &intensity, SHADER_UNIFORM_FLOAT);
    }
    int shadows_enabled = scene->shadows_enabled && scene->shadow_map.depth.id != 0;
    location = cached_location(scene->shader, "shadowsEnabled");
    if (location >= 0)
        SetShaderValue(scene->shader, location, &shadows_enabled, SHADER_UNIFORM_INT);
    if (shadows_enabled) {
        location = cached_location(scene->shader, "lightViewProjection");
        if (location >= 0)
            SetShaderValueMatrix(scene->shader, location, scene->light_view_projection);
        location = cached_location(scene->shader, "shadowMap");
        if (location >= 0) {
            int texture_slot = 10;
            rlEnableShader(scene->shader.id);
            rlActiveTextureSlot(texture_slot);
            rlEnableTexture(scene->shadow_map.depth.id);
            rlSetUniform(location, &texture_slot, SHADER_UNIFORM_INT, 1);
            rlActiveTextureSlot(0);
        }
    }
    set_far_shadow_uniforms(scene, scene->shader);
    int light_count = scene->lighting_enabled ? scene->local_light_count : 0;
    location = cached_location(scene->shader, "localLightCount");
    if (location >= 0)
        SetShaderValue(scene->shader, location, &light_count, SHADER_UNIFORM_INT);
    if (light_count == 0)
        return;

    float positions[WATERFRONT_GPU_MAX_LOCAL_LIGHTS * 4];
    float colors[WATERFRONT_GPU_MAX_LOCAL_LIGHTS * 4];
    float directions[WATERFRONT_GPU_MAX_LOCAL_LIGHTS * 4];
    float cones[WATERFRONT_GPU_MAX_LOCAL_LIGHTS * 4];
    for (int i = 0; i < light_count; ++i) {
        WaterfrontGpuLight light = scene->local_lights[i];
        float *position = &positions[i * 4];
        float *color = &colors[i * 4];
        float *direction = &directions[i * 4];
        float *cone = &cones[i * 4];
        position[0] = light.position.x;
        position[1] = light.position.y;
        position[2] = light.position.z;
        position[3] = light.radius;
        color[0] = (float)light.color.r / 255.0f;
        color[1] = (float)light.color.g / 255.0f;
        color[2] = (float)light.color.b / 255.0f;
        color[3] = light.intensity;
        direction[0] = light.direction.x;
        direction[1] = light.direction.y;
        direction[2] = light.direction.z;
        direction[3] = 0;
        cone[0] = light.inner_cone_cos;
        cone[1] = light.outer_cone_cos;
        cone[2] = 0;
        cone[3] = 0;
    }
    location = cached_location(scene->shader, "localLightPositionRadius[0]");
    if (location >= 0)
        SetShaderValueV(scene->shader, location, positions, SHADER_UNIFORM_VEC4, light_count);
    location = cached_location(scene->shader, "localLightColorIntensity[0]");
    if (location >= 0)
        SetShaderValueV(scene->shader, location, colors, SHADER_UNIFORM_VEC4, light_count);
    location = cached_location(scene->shader, "localLightDirection[0]");
    if (location >= 0)
        SetShaderValueV(scene->shader, location, directions, SHADER_UNIFORM_VEC4, light_count);
    location = cached_location(scene->shader, "localLightCone[0]");
    if (location >= 0)
        SetShaderValueV(scene->shader, location, cones, SHADER_UNIFORM_VEC4, light_count);
}

static void set_atmosphere_uniforms(WaterfrontGpuScene *scene, Vector3 camera_position)
{
    static const float fog_color[] = { 186.0f / 255.0f, 229.0f / 255.0f, 244.0f / 255.0f };
    float camera[] = { camera_position.x, camera_position.y, camera_position.z };
    float fog_start = 145.0f;
    float fog_end = 620.0f;
    float fog_opacity = .38f;
    int atmosphere_enabled = scene->atmosphere_enabled;
    int location = cached_location(scene->shader, "cameraPosition");
    if (location >= 0)
        SetShaderValue(scene->shader, location, camera, SHADER_UNIFORM_VEC3);
    location = cached_location(scene->shader, "fogColor");
    if (location >= 0)
        SetShaderValue(scene->shader, location, fog_color, SHADER_UNIFORM_VEC3);
    location = cached_location(scene->shader, "fogStart");
    if (location >= 0)
        SetShaderValue(scene->shader, location, &fog_start, SHADER_UNIFORM_FLOAT);
    location = cached_location(scene->shader, "fogEnd");
    if (location >= 0)
        SetShaderValue(scene->shader, location, &fog_end, SHADER_UNIFORM_FLOAT);
    location = cached_location(scene->shader, "fogOpacity");
    if (location >= 0)
        SetShaderValue(scene->shader, location, &fog_opacity, SHADER_UNIFORM_FLOAT);
    location = cached_location(scene->shader, "atmosphereEnabled");
    if (location >= 0)
        SetShaderValue(scene->shader, location, &atmosphere_enabled, SHADER_UNIFORM_INT);
}

static void set_water_uniforms(WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame)
{
    BoatWater water = frame->player_water;
    int objects = water.objects != NULL || water.traffic_objects != NULL;
    float object_life = water.objects ? water.objects->life_ticks / 120.0f : 16;
    SetShaderValue(scene->water_shader, cached_location(scene->water_shader, "objectWakeEnabled"),
        &objects, SHADER_UNIFORM_INT);
    SetShaderValue(scene->water_shader, cached_location(scene->water_shader, "objectWakeLife"),
        &object_life, SHADER_UNIFORM_FLOAT);
    int field_enabled = scene->wake_field.id != 0;
    SetShaderValue(scene->water_shader, cached_location(scene->water_shader, "wakeFieldEnabled"),
        &field_enabled, SHADER_UNIFORM_INT);
    if (field_enabled) {
        float grid[] = { (float)scene->wake_field.width, (float)scene->wake_field.height,
            scene->wake_spacing };
        SetShaderValue(scene->water_shader, cached_location(scene->water_shader, "wakeGrid"), grid,
            SHADER_UNIFORM_VEC3);
        int slot = 13;
        rlEnableShader(scene->water_shader.id);
        rlActiveTextureSlot(slot);
        rlEnableTexture(scene->wake_field.id);
        rlSetUniform(
            cached_location(scene->water_shader, "wakeField"), &slot, SHADER_UNIFORM_INT, 1);
        rlActiveTextureSlot(0);
    }
    int physical_waves = frame->player_active && (field_enabled || boat_water_displaced(water));
    SetShaderValue(scene->water_shader, cached_location(scene->water_shader, "physicalWaves"),
        &physical_waves, SHADER_UNIFORM_INT);
    float wake_amplitude = frame->player_active ? water.wake_amplitude : 0;
    float radial[32], origins[16], motions[16];
    boat_water_radial_uniforms(water.radial, frame->time, radial);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            origins[4 * i + j] = radial[8 * i + j];
            motions[4 * i + j] = radial[8 * i + 4 + j];
        }
    SetShaderValueV(scene->water_shader, cached_location(scene->water_shader, "radialOrigin"),
        origins, SHADER_UNIFORM_VEC4, 4);
    SetShaderValueV(scene->water_shader, cached_location(scene->water_shader, "radialMotion"),
        motions, SHADER_UNIFORM_VEC4, 4);
    float storm[12];
    boat_water_storm_uniforms(
        frame->player_active ? water.storm : (BoatStorm) { 0 }, frame->time, storm);
    SetShaderValue(scene->water_shader, cached_location(scene->water_shader, "stormArea"), storm,
        SHADER_UNIFORM_VEC4);
    SetShaderValue(scene->water_shader, cached_location(scene->water_shader, "stormPhase"),
        storm + 4, SHADER_UNIFORM_VEC4);
    SetShaderValue(scene->water_shader, cached_location(scene->water_shader, "stormMotion"),
        storm + 8, SHADER_UNIFORM_VEC4);
    float vessel[12];
    boat_vessel_wake_uniforms(
        frame->player_active ? water.vessel : (BoatVesselWake) { 0 }, frame->time, vessel);
    const char *vessel_uniforms[] = { "vesselOrigin", "vesselMotion", "vesselShape" };
    for (int i = 0; i < 3; ++i)
        SetShaderValue(scene->water_shader,
            cached_location(scene->water_shader, vessel_uniforms[i]), vessel + 4 * i,
            SHADER_UNIFORM_VEC4);
    SetShaderValue(scene->water_shader, cached_location(scene->water_shader, "wakeAmplitude"),
        &wake_amplitude, SHADER_UNIFORM_FLOAT);
    float wake_offset[] = { -water.wake_offset.x, water.wake_offset.y };
    SetShaderValue(scene->water_shader, cached_location(scene->water_shader, "wakeOffset"),
        wake_offset, SHADER_UNIFORM_VEC2);
    float wake_origins[BOAT_WAKE_SEGMENTS * 4], wake_parameters[BOAT_WAKE_SEGMENTS * 4];
    int wake_count = frame->player_active
        ? boat_water_wake_uniforms(water, frame->time, wake_origins, wake_parameters)
        : 0;
    if (objects && frame->player_active) {
        float records[BOAT_OBJECT_CAPACITY * 16] = { 0 };
        int capacity = BOAT_OBJECT_CAPACITY * (water.traffic_objects ? 2 : 1);
        float *parameters = records + capacity * 4;
        int players
            = water.objects ? boat_wake_objects_pack(water.objects, records, parameters) : 0;
        wake_count = players;
        if (water.traffic_objects)
            wake_count += boat_wake_objects_pack(
                water.traffic_objects, records + players * 4, parameters + players * 4);
        float traffic_life = water.traffic_objects ? water.traffic_objects->life_ticks / 120.0f : 0;
        SetShaderValue(scene->water_shader,
            cached_location(scene->water_shader, "objectWakePlayerCount"), &players,
            SHADER_UNIFORM_INT);
        SetShaderValue(scene->water_shader,
            cached_location(scene->water_shader, "objectWakeTrafficLife"), &traffic_life,
            SHADER_UNIFORM_FLOAT);
        if (scene->object_wakes.id && scene->object_wakes.width != capacity) {
            UnloadTexture(scene->object_wakes);
            scene->object_wakes = (Texture2D) { 0 };
        }
        if (!scene->object_wakes.id) {
            Image image = { records, capacity, 2, 1, PIXELFORMAT_UNCOMPRESSED_R32G32B32A32 };
            scene->object_wakes = LoadTextureFromImage(image);
            SetTextureFilter(scene->object_wakes, TEXTURE_FILTER_POINT);
        } else {
            UpdateTexture(scene->object_wakes, records);
        }
        int slot = 14;
        rlEnableShader(scene->water_shader.id);
        rlActiveTextureSlot(slot);
        rlEnableTexture(scene->object_wakes.id);
        rlSetUniform(
            cached_location(scene->water_shader, "objectWakeData"), &slot, SHADER_UNIFORM_INT, 1);
        rlActiveTextureSlot(0);
    }
    SetShaderValue(scene->water_shader, cached_location(scene->water_shader, "playerWakeCount"),
        &wake_count, SHADER_UNIFORM_INT);
    if (wake_count && !objects) {
        SetShaderValueV(scene->water_shader,
            cached_location(scene->water_shader, "playerWakeOrigins[0]"), wake_origins,
            SHADER_UNIFORM_VEC4, wake_count);
        SetShaderValueV(scene->water_shader,
            cached_location(scene->water_shader, "playerWakeParameters[0]"), wake_parameters,
            SHADER_UNIFORM_VEC4, wake_count);
    }
    int waterfront_water = frame->player_active && !frame->boating_basin;
    SetShaderValue(scene->water_shader, cached_location(scene->water_shader, "waterfrontWater"),
        &waterfront_water, SHADER_UNIFORM_INT);
    float patch[] = { frame->player.hull.position.x, frame->player.hull.position.z,
        BOAT_WATER_PATCH_FULL, BOAT_WATER_PATCH_RADIUS };
    SetShaderValue(scene->water_shader, cached_location(scene->water_shader, "wavePatch"), patch,
        SHADER_UNIFORM_VEC4);
    if (physical_waves) {
        float parameters[12];
        boat_water_wave_uniforms(water, frame->time, parameters);
        SetShaderValueV(scene->water_shader,
            cached_location(scene->water_shader, "waveParameters[0]"), parameters,
            SHADER_UNIFORM_VEC4, 3);
    }
    int shadows = scene->shadows_enabled && frame->player_active;
    Vector3 sun = waterfront_sun_direction();
    float sun_direction[] = { sun.x, sun.y, sun.z };
    SetShaderValue(scene->water_shader, cached_location(scene->water_shader, "sunDirection"),
        sun_direction, SHADER_UNIFORM_VEC3);
    int shadow_location = cached_location(scene->water_shader, "shadowsEnabled");
    SetShaderValue(scene->water_shader, shadow_location, &shadows, SHADER_UNIFORM_INT);
    SetShaderValueMatrix(scene->water_shader,
        cached_location(scene->water_shader, "lightViewProjection"), scene->light_view_projection);
    if (shadows) {
        int slot = 12;
        rlEnableShader(scene->water_shader.id);
        rlActiveTextureSlot(slot);
        rlEnableTexture(scene->shadow_map.depth.id);
        rlSetUniform(
            cached_location(scene->water_shader, "shadowMap"), &slot, SHADER_UNIFORM_INT, 1);
        rlActiveTextureSlot(0);
    }
    set_far_shadow_uniforms(scene, scene->water_shader);
    float camera[]
        = { frame->camera.position.x, frame->camera.position.y, frame->camera.position.z };
    int reflections_enabled = scene->reflections_enabled;
    int location = cached_location(scene->water_shader, "reflectionViewProjection");
    if (location >= 0)
        SetShaderValueMatrix(scene->water_shader, location, scene->reflection_view_projection);
    location = cached_location(scene->water_shader, "cameraPosition");
    if (location >= 0)
        SetShaderValue(scene->water_shader, location, camera, SHADER_UNIFORM_VEC3);
    // Double frame time never reaches a float uniform; upload bounded, reduced payloads.
    float crossing_clock = boat_water_crossing_clock(water, frame->time);
    location = cached_location(scene->water_shader, "crossingWakeTime");
    if (location >= 0)
        SetShaderValue(scene->water_shader, location, &crossing_clock, SHADER_UNIFORM_FLOAT);
    float detail_phase[] = { boat_phase(1.5, frame->time), boat_phase(2.1, frame->time) };
    location = cached_location(scene->water_shader, "detailRipplePhase");
    if (location >= 0)
        SetShaderValue(scene->water_shader, location, detail_phase, SHADER_UNIFORM_VEC2);
    float reflection_phase[] = { boat_phase(.80, frame->time), boat_phase(.55, frame->time),
        boat_phase(.65, frame->time), boat_phase(.35, frame->time) };
    location = cached_location(scene->water_shader, "reflectionRipplePhase");
    if (location >= 0)
        SetShaderValue(scene->water_shader, location, reflection_phase, SHADER_UNIFORM_VEC4);
    location = cached_location(scene->water_shader, "reflectionsEnabled");
    if (location >= 0)
        SetShaderValue(scene->water_shader, location, &reflections_enabled, SHADER_UNIFORM_INT);
    location = cached_location(scene->water_shader, "reflectionTexture");
    if (location >= 0) {
        int texture_slot = 11;
        rlEnableShader(scene->water_shader.id);
        rlActiveTextureSlot(texture_slot);
        rlEnableTexture(scene->reflection.texture.id);
        rlSetUniform(location, &texture_slot, SHADER_UNIFORM_INT, 1);
        rlActiveTextureSlot(0);
    }
}

// Called only in the main colour pass; ghosts never enter reflection or shadow passes.
static void draw_ghost(const WaterfrontGpuScene *scene, Material material,
    const WaterfrontSceneFrame *frame, int width)
{
    if (frame->ghost_opacity <= 0)
        return;
    int opacity = cached_location(material.shader, "ghostOpacity");
    int pixel_scale = cached_location(material.shader, "ghostPixelScale");
    float scale = (float)width / 320;
    SetShaderValue(material.shader, pixel_scale, &scale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(material.shader, opacity, &frame->ghost_opacity, SHADER_UNIFORM_FLOAT);
    draw_colored_mesh(scene->player_hull[boat_skin_index(frame->ghost_skin)], material,
        frame->ghost_hull, WHITE, true);
    draw_colored_mesh(scene->player_cabin[boat_skin_index(frame->ghost_skin)], material,
        frame->ghost_cabin, WHITE, true);
    for (int side = -1; frame->ghost_skin != BOAT_SKIN_CABIN && side <= 1; side += 2)
        draw_colored_mesh(scene->player_engine[boat_skin_index(frame->ghost_skin)], material,
            boat_skin_outboard_pose(frame->ghost_hull, frame->ghost_engine, side), WHITE, true);
    if (scene->ghost_flag.vaoId)
        draw_colored_mesh(
            scene->ghost_flag, material, (Transform3D) { .scale = { 1, 1, 1 } }, WHITE, true);
    float zero = 0;
    SetShaderValue(material.shader, opacity, &zero, SHADER_UNIFORM_FLOAT);
}

static void draw_harbor(
    const WaterfrontGpuScene *scene, Material material, const WaterfrontSceneFrame *frame)
{
    const WaterfrontHarborFrame *harbor = &frame->harbor;
    if (harbor->modules_authored) {
        for (int i = 0; i < harbor->module_count; ++i) {
            const WaterfrontModuleFrame *m = &harbor->modules[i];
            if (m->kind == BOAT_SCENE_MODULE_GANGWAY)
                draw_colored_mesh(
                    scene->cube, material, m->pose, (Color) { 154, 123, 84, 255 }, false);
            else if (m->kind == BOAT_SCENE_MODULE_PONTOON)
                draw_colored_mesh(scene->harbor_pontoon, material, m->pose, WHITE, false);
            else if (m->kind == BOAT_SCENE_MODULE_PONTOON_SMALL)
                draw_colored_mesh(scene->harbor_small_pontoon, material, m->pose, WHITE, false);
            else
                draw_colored_mesh(scene->harbor_quay, material, m->pose, WHITE, false);
        }
    } else {
        draw_colored_mesh(scene->harbor_quay, material, harbor->quay, WHITE, false);
        draw_colored_mesh(scene->harbor_pontoon, material, harbor->pontoon, WHITE, false);
        draw_colored_mesh(
            scene->cube, material, harbor->gangway, (Color) { 154, 123, 84, 255 }, false);
    }
    draw_colored_mesh(scene->harbor_ferry, material, harbor->ferry, WHITE, false);
    for (int i = 0; i < harbor->berth_count; ++i)
        draw_colored_mesh(
            scene->harbor_boats[harbor->recipes[i]], material, harbor->boats[i], WHITE, false);
    for (int i = 0; i < harbor->line_count; ++i)
        draw_colored_mesh(
            scene->cube, material, harbor->lines[i], (Color) { 169, 153, 113, 255 }, false);
}

static void draw_traffic(const WaterfrontGpuScene *scene, Material material,
    const WaterfrontSceneFrame *frame, bool foam)
{
    const WaterfrontTrafficFrame *traffic = &frame->traffic;
    bool lights = material.shader.id != scene->shadow_shader.id;
    const Color colors[] = { { 222, 70, 52, 255 }, { 72, 191, 139, 255 }, { 235, 223, 188, 255 } };
    // Hulls first and then every light cube, so the cubes form one instanced run.
    for (int i = 0; i < traffic->vessel_count; ++i)
        draw_colored_mesh(scene->traffic_boats[traffic->mesh[i]], material,
            traffic->vessels[i].pose, WHITE, false);
    for (int i = 0; i < traffic->vessel_count; ++i) {
        const WaterfrontVesselState *vessel = &traffic->vessels[i];
        float size = i >= 3 ? .32f : .18f;
        for (int light = 0; lights && light < WATERFRONT_TRAFFIC_LIGHTS; ++light)
            draw_colored_mesh(scene->cube, material,
                (Transform3D) { .position = vessel->lights[light], .scale = { size, size, size } },
                colors[light], true);
    }
    if (traffic->lift)
        draw_colored_mesh(scene->lift_span, material, traffic->lift_pose, WHITE, false);
    for (int i = 0; foam && i < traffic->wake_count; ++i)
        draw_colored_mesh(
            scene->plane, material, traffic->wakes[i].pose, traffic->wakes[i].color, false);
}

typedef struct {
    Transform3D hull, cabin, engine;
    unsigned int skin;
    Mesh flag;
    const WaterfrontPlayerParticle *particles;
    int particle_count;
} BoatDrawParts;

static void draw_boat(
    const WaterfrontGpuScene *scene, Material material, BoatDrawParts b, bool emissive)
{
    unsigned int index = boat_skin_index(b.skin);
    draw_colored_mesh(scene->player_hull[index], material, b.hull, WHITE, false);
    draw_colored_mesh(scene->player_cabin[index], material, b.cabin, WHITE, false);
    for (int side = -1; index != BOAT_SKIN_CABIN && side <= 1; side += 2)
        draw_colored_mesh(scene->player_engine[index], material,
            boat_skin_outboard_pose(b.hull, b.engine, side), WHITE, false);
    Mesh flag = b.flag;
    const WaterfrontPlayerParticle *particles = b.particles;
    int particle_count = b.particle_count;
    if (flag.vaoId)
        draw_colored_mesh(flag, material, (Transform3D) { .scale = { 1, 1, 1 } }, WHITE, false);
    for (int i = 0; i < particle_count; ++i) {
        // Surface foam receives shadows/depth but does not cast a solid slab shadow.
        // Airborne spray participates in both the reflection and shadow passes.
        if (!emissive && !particles[i].spray)
            continue;
        draw_colored_mesh(scene->cube, material, particles[i].transform, particles[i].color, false);
    }
}

static void add_scenery(
    const WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame, Material material)
{
    for (int i = 0; i < frame->scenery_count; ++i)
        draw_colored_mesh(
            scene->cube, material, frame->scenery[i].transform, frame->scenery[i].color, false);
}

static void draw_scenery(
    const WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame, Material material)
{
    object_batch_begin(scene, material);
    add_scenery(scene, frame, material);
    object_batch_end(scene);
}

static void add_life(const WaterfrontGpuScene *scene, Material material,
    const WaterfrontSceneFrame *frame, bool emissive)
{
    if (!frame->boating_basin) {
        add_scenery(scene, frame, material);
        draw_harbor(scene, material, frame);
        draw_traffic(scene, material, frame, emissive);
    }
    // By part, so each part's flock forms one instanced run.
    for (int part = 0; part < WATERFRONT_GULL_PARTS; ++part)
        for (int i = 0; i < frame->gull_count; ++i)
            draw_colored_mesh(scene->gull[part], material, frame->gulls[i][part], WHITE, true);
    if (frame->player_active) {
        for (int i = 0; i < frame->lamp_count; ++i) {
            const WaterfrontLampState *lamp = &frame->lamps[i];
            draw_colored_mesh(
                scene->cube, material, lamp->pole, (Color) { 58, 66, 79, 255 }, false);
            if (emissive)
                draw_colored_mesh(scene->cube, material,
                    (Transform3D) { .position = lamp->lens, .scale = { .85f, .38f, .85f } },
                    LAMP_LENS_COLOR, true);
        }
        const WaterfrontPlayerState *p = &frame->player;
        draw_boat(scene, material,
            (BoatDrawParts) { p->hull, p->cabin, p->engine, p->skin, scene->player_flag,
                p->particles, p->particle_count },
            emissive);
        for (int i = 0; i < frame->rival_count; ++i) {
            const WaterfrontRivalState *r = &frame->rivals[i];
            draw_boat(scene, material,
                (BoatDrawParts) { r->hull, r->cabin, r->engine, r->skin, scene->rival_flags[i],
                    r->particles, r->particle_count },
                emissive);
        }
        return;
    }
    for (int street_index = 0; street_index < 2; ++street_index) {
        for (int i = 0; i < frame->lamp_count; ++i) {
            const WaterfrontLampState *lamp = &frame->lamps[i];
            if (lamp->street != street_index)
                continue;
            draw_colored_mesh(
                scene->cube, material, lamp->pole, (Color) { 58, 66, 79, 255 }, false);
            if (emissive)
                draw_colored_mesh(scene->cube, material,
                    (Transform3D) { .position = lamp->lens, .scale = { .85f, .38f, .85f } },
                    LAMP_LENS_COLOR, true);
        }
        for (int car = 0; car < WATERFRONT_CAR_COUNT; ++car) {
            const WaterfrontCarState *state = &frame->cars[car];
            if (state->street != street_index)
                continue;
            draw_colored_mesh(scene->cube, material, state->body, state->color, false);
            draw_colored_mesh(
                scene->cube, material, state->cabin, (Color) { 49, 65, 81, 255 }, false);
            if (emissive) {
                // Source lenses are always emissive. The bounded forward-light selection only
                // decides which nearby headlights affect surrounding surfaces this frame->
                for (int end = -1; end <= 1; end += 2) {
                    for (int side = -1; side <= 1; side += 2) {
                        draw_colored_mesh(scene->cube, material,
                            (Transform3D) { .position = state->lenses[end > 0][side > 0],
                                .scale = { .48f, .34f, .48f } },
                            end > 0 ? HEADLIGHT_LENS_COLOR : (Color) { 240, 56, 34, 255 }, true);
                    }
                }
            }
        }
    }
}

static void draw_life(const WaterfrontGpuScene *scene, Material material,
    const WaterfrontSceneFrame *frame, bool emissive)
{
    object_batch_begin(scene, material);
    add_life(scene, material, frame, emissive);
    object_batch_end(scene);
}

static void draw_crane_lights(const WaterfrontGpuScene *scene, Material material)
{
    // Source fixtures stay intentionally chunky: at virtual resolution they read as the coloured
    // work lights in the reference rather than as a row of sub-pixel bulbs.
    draw_colored_mesh(scene->cube, material,
        (Transform3D) { .position = crane_point(-42, 75, -5), .scale = { 1.3f, .45f, .7f } },
        (Color) { 255, 92, 48, 255 }, true);
    draw_colored_mesh(scene->cube, material,
        (Transform3D) { .position = crane_point(5, 75, -5), .scale = { .85f, .85f, .85f } },
        (Color) { 122, 145, 255, 255 }, true);
    draw_colored_mesh(scene->cube, material,
        (Transform3D) { .position = crane_point(40, 75, -5), .scale = { 1.15f, .4f, .65f } },
        (Color) { 255, 86, 44, 255 }, true);
}

static Camera3D reflection_camera(const WaterfrontSceneFrame *frame)
{
    return (Camera3D) { .position
        = { frame->camera.position.x, -frame->camera.position.y, frame->camera.position.z },
        .target = { frame->camera.target.x, -frame->camera.target.y, frame->camera.target.z },
        .up = { 0, -1, 0 },
        .fovy = frame->camera.vertical_fov * RAD2DEG,
        .projection = CAMERA_PERSPECTIVE };
}

typedef struct {
    unsigned int framebuffer;
    int width, height;
    Matrix projection, modelview;
} RenderTargetState;

static RenderTargetState render_target_state(void)
{
    return (RenderTargetState) { .framebuffer = rlGetActiveFramebuffer(),
        .width = rlGetFramebufferWidth(),
        .height = rlGetFramebufferHeight(),
        .projection = rlGetMatrixProjection(),
        .modelview = rlGetMatrixModelview() };
}

static void restore_render_target(RenderTargetState state)
{
    // raylib's EndTextureMode returns to the window, including its 2D matrices. Nested
    // shadow/reflection passes must restore the caller's matrices as well as its framebuffer:
    // the sky and subsequent overlays otherwise draw at the window's size inside that target.
    if (state.framebuffer != 0)
        rlEnableFramebuffer(state.framebuffer);
    else
        rlDisableFramebuffer();
    rlViewport(0, 0, state.width, state.height);
    rlSetFramebufferWidth(state.width);
    rlSetFramebufferHeight(state.height);
    rlSetMatrixProjection(state.projection);
    rlSetMatrixModelview(state.modelview);
}

static void draw_sky(const WaterfrontGpuScene *scene, View3D camera, WaterfrontSquall squall,
    int width, int height, float aspect)
{
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Vector3 up = Vector3CrossProduct(right, forward);
    float scale[] = { tanf(camera.vertical_fov * .5f) * aspect, tanf(camera.vertical_fov * .5f) };
    Shader shader = scene->sky_shader;
    SetShaderValue(shader, cached_location(shader, "skyForward"), &forward, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, cached_location(shader, "skyRight"), &right, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, cached_location(shader, "skyUp"), &up, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, cached_location(shader, "skyScale"), scale, SHADER_UNIFORM_VEC2);
    SetShaderValue(
        shader, cached_location(shader, "skyEye"), &camera.position, SHADER_UNIFORM_VEC3);
    float area[] = { squall.centre.x, squall.centre.y, squall.radius, squall.grown };
    float heights[] = { squall.base, squall.top };
    SetShaderValue(shader, cached_location(shader, "squallArea"), area, SHADER_UNIFORM_VEC4);
    SetShaderValue(shader, cached_location(shader, "squallHeight"), heights, SHADER_UNIFORM_VEC2);
    if (counting)
        ++counting->sky_quads;
    BeginShaderMode(shader);
    DrawTexturePro(scene->sky_texture,
        (Rectangle) { 0, 0, scene->sky_texture.width, scene->sky_texture.height },
        (Rectangle) { 0, 0, width, height }, (Vector2) { 0 }, 0, WHITE);
    EndShaderMode();
}

static void draw_ground(const WaterfrontGpuScene *scene, Material material)
{
    int location = cached_location(scene->shader, "groundPass");
    int enabled = 1;
    SetShaderValue(scene->shader, location, &enabled, SHADER_UNIFORM_INT);
    draw_tiled(scene, &scene->quay, material);
    enabled = 0;
    SetShaderValue(scene->shader, location, &enabled, SHADER_UNIFORM_INT);
}

static void draw_facades(const WaterfrontGpuScene *scene, Material material)
{
    int location = cached_location(scene->shader, "facadePass");
    int enabled = 1;
    SetShaderValue(scene->shader, location, &enabled, SHADER_UNIFORM_INT);
    draw_tiled(scene, &scene->facades, material);
    enabled = 0;
    SetShaderValue(scene->shader, location, &enabled, SHADER_UNIFORM_INT);
}

static void draw_reflected_scene(const WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame,
    Material material, Material life)
{
    if (frame->boating_basin) {
        draw_scenery(scene, frame, material);
        draw_life(scene, life, frame, false);
        return;
    }
    set_object_color(scene->shader, WHITE);
    set_emissive(scene->shader, false);
    draw_tiled(scene, &scene->city, material);
    draw_facades(scene, material);
    draw_tiled(scene, &scene->roofs, scene->roof_material);
    draw_ground(scene, material);
    draw_tiled(scene, &scene->corner, material);
    set_emissive(scene->shader, true);
    draw_tiled(scene, &scene->windows, material);
    draw_tiled(scene, &scene->corner_emissive, material);
    if (scene->river)
        draw_colored_mesh(scene->bridge, life, frame->bridge, frame->bridge_color, false);
    // The reflection source deliberately excludes the water mesh and its wake, preventing a
    // feedback surface while retaining vehicles, boat hull, lamp poles, and the city silhouette.
    draw_life(scene, life, frame, false);
    set_emissive(scene->shader, false);
    draw_map_mesh(scene->crane, material);
}

static void render_reflection(
    WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame, float aspect)
{
    Camera3D camera = reflection_camera(frame);
    // Cover the output aspect plus 20% overscan for ripples and displaced wave vertices.
    camera.fovy = 2 * atanf(tanf(frame->camera.vertical_fov * .5f) * 1.2f) * RAD2DEG;
    RenderTargetState previous = render_target_state();
    BeginTextureMode(scene->reflection);
    ClearBackground(BLACK);
    View3D sky_camera = frame->camera;
    sky_camera.position = camera.position;
    sky_camera.target = camera.target;
    sky_camera.up = camera.up;
    sky_camera.vertical_fov = camera.fovy * DEG2RAD;
    // The fixed-size texture stores this wider frustum, independent of its pixel aspect.
    draw_sky(scene, sky_camera, frame->squall, scene->reflection.texture.width,
        scene->reflection.texture.height, aspect);
    BeginMode3D(camera);
    rlSetMatrixProjection(MatrixPerspective(
        camera.fovy * DEG2RAD, aspect, frame->camera.near_plane, frame->camera.far_plane));
    Matrix view = rlGetMatrixModelview();
    Matrix projection = rlGetMatrixProjection();
    // A reflected view reverses the projected winding. Disable culling for this pass so the
    // separate camera does not require mirrored geometry or per-mesh winding changes.
    rlDisableBackfaceCulling();
    int reflection_pass = 1;
    int clip_location = cached_location(scene->shader, "reflectionPass");
    SetShaderValue(scene->shader, clip_location, &reflection_pass, SHADER_UNIFORM_INT);
    draw_reflected_scene(scene, frame, scene->city_material, scene->life_material);
    reflection_pass = 0;
    SetShaderValue(scene->shader, clip_location, &reflection_pass, SHADER_UNIFORM_INT);
    rlEnableBackfaceCulling();
    EndMode3D();
    EndTextureMode();
    restore_render_target(previous);
    scene->reflection_view_projection = MatrixMultiply(view, projection);
}

static Camera3D shadow_camera(
    const WaterfrontSceneFrame *frame, float coverage, int map_size, float light_distance)
{
    const float texel_size = coverage / map_size;
    Vector3 light_direction = waterfront_sun_direction();
    Vector3 right = Vector3Normalize(Vector3CrossProduct((Vector3) { 0, 1, 0 }, light_direction));
    Vector3 up = Vector3CrossProduct(light_direction, right);
    Vector3 target = frame->camera.target;
    float right_offset = roundf(Vector3DotProduct(target, right) / texel_size) * texel_size
        - Vector3DotProduct(target, right);
    float up_offset = roundf(Vector3DotProduct(target, up) / texel_size) * texel_size
        - Vector3DotProduct(target, up);
    target = Vector3Add(
        target, Vector3Add(Vector3Scale(right, right_offset), Vector3Scale(up, up_offset)));
    Vector3 position = Vector3Add(target, Vector3Scale(light_direction, light_distance));
    return (Camera3D) { .position = position,
        .target = target,
        .up = up,
        .fovy = coverage,
        .projection = CAMERA_ORTHOGRAPHIC };
}

static void draw_shadow_casters(
    const WaterfrontGpuScene *scene, Material material, const WaterfrontSceneFrame *frame)
{
    if (frame->boating_basin) {
        // Explicit: inheriting the context's cull state made the first frame differ from the rest.
        rlDisableBackfaceCulling();
        draw_scenery(scene, frame, material);
        draw_life(scene, material, frame, false);
        return;
    }
    set_object_color(scene->shadow_shader, WHITE);
    set_emissive(scene->shadow_shader, false);
    rlEnableBackfaceCulling();
    draw_tiled(scene, &scene->city, material);
    draw_tiled(scene, &scene->facades, material);
    draw_tiled(scene, &scene->roofs, material);
    draw_tiled(scene, &scene->quay, material);
    draw_tiled(scene, &scene->corner, material);
    if (scene->river)
        draw_colored_mesh(scene->bridge, material, frame->bridge, frame->bridge_color, false);
    // Cars, boats, and lamp poles matter to the road shadow, but their lenses and wake do not.
    rlDisableBackfaceCulling();
    draw_life(scene, material, frame, false);
    rlEnableBackfaceCulling();
    draw_map_mesh(scene->crane, material);
}

static Matrix render_shadow_map(WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame,
    RenderTexture2D target, float coverage, int map_size, float light_distance)
{
    Camera3D camera = shadow_camera(frame, coverage, map_size, light_distance);
    RenderTargetState previous = render_target_state();
    BeginTextureMode(target);
    ClearBackground(WHITE);
    BeginMode3D(camera);
    float half_coverage = camera.fovy * .5f;
    // A wider map reaches farther along the light axis, too; retain room for its casters.
    rlSetMatrixProjection(MatrixOrtho(-half_coverage, half_coverage, -half_coverage, half_coverage,
        .25, light_distance * 2 + 200));
    Matrix view = rlGetMatrixModelview();
    Matrix projection = rlGetMatrixProjection();
    draw_shadow_casters(scene, scene->shadow_material, frame);
    EndMode3D();
    EndTextureMode();
    restore_render_target(previous);
    // raylib composes matrices in this order for its shaders; retain the pinned example's
    // convention so this matrix maps world positions into the depth texture's clip space.
    return MatrixMultiply(view, projection);
}

static void draw_water(WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame, int width)
{
    set_object_color(scene->water_shader, WHITE);
    set_water_uniforms(scene, frame);
    float pixel_scale = fmaxf(1, width / 320.0f);
    SetShaderValue(scene->water_shader, cached_location(scene->water_shader, "surfacePixelScale"),
        &pixel_scale, SHADER_UNIFORM_FLOAT);
    int pass = scene->near_water.vaoId ? 1 : 0;
    int location = cached_location(scene->water_shader, "waterPatchPass");
    SetShaderValue(scene->water_shader, location, &pass, SHADER_UNIFORM_INT);
    if (scene->water.vaoId)
        submit(scene->water, scene->water_material, MatrixIdentity(), WATERFRONT_GPU_GROUP_WATER);
    if (pass) {
        pass = 2;
        set_object_color(scene->water_shader, WHITE);
        SetShaderValue(scene->water_shader, location, &pass, SHADER_UNIFORM_INT);
        submit(
            scene->near_water, scene->water_material, MatrixIdentity(), WATERFRONT_GPU_GROUP_WATER);
        if (scene->near_water_extra.vaoId)
            submit(scene->near_water_extra, scene->water_material, MatrixIdentity(),
                WATERFRONT_GPU_GROUP_WATER);
    }
}

static void draw_scene(
    WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame, int width, int height);

void waterfront_gpu_scene_draw(
    WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame, int width, int height)
{
    scene->stats = (WaterfrontGpuStats) { 0 };
    if (!frame->boating_basin && !scene->map) {
        TraceLog(LOG_WARNING, "GPU waterfront frame drawn without a bound map");
        return;
    }
    counting = &scene->stats;
    drawing_scene = scene;
    draw_scene(scene, frame, width, height);
    drawing_scene = NULL;
    counting = NULL;
}

WaterfrontGpuCount waterfront_gpu_stats_total(const WaterfrontGpuStats *stats)
{
    WaterfrontGpuCount total = { stats->sky_quads, 2 * (size_t)stats->sky_quads, stats->sky_quads };
    for (int pass = 0; pass < WATERFRONT_GPU_PASS_COUNT; ++pass)
        for (int group = 0; group < WATERFRONT_GPU_GROUP_COUNT; ++group) {
            total.submissions += stats->mesh[pass][group].submissions;
            total.triangles += stats->mesh[pass][group].triangles;
            total.instances += stats->mesh[pass][group].instances;
        }
    return total;
}

static void draw_scene(
    WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame, int width, int height)
{
    if (frame->player_active)
        update_flag_mesh(&scene->player_flag, &frame->player.flag, frame->player.skin);
    if (frame->ghost_opacity > 0)
        update_flag_mesh(&scene->ghost_flag, &frame->ghost_flag, frame->ghost_skin);
    for (int i = 0; frame->player_active && i < frame->rival_count; ++i)
        update_flag_mesh(&scene->rival_flags[i], &frame->rivals[i].flag, frame->rivals[i].skin);
    Camera3D camera = { .position = frame->camera.position,
        .target = frame->camera.target,
        .up = frame->camera.up,
        .fovy = frame->camera.vertical_fov * RAD2DEG,
        .projection = CAMERA_PERSPECTIVE };
    Material material = scene->city_material;
    Material life_material = scene->life_material;
    counting_pass = WATERFRONT_GPU_PASS_SHADOW;
    scene->far_shadows_active = frame->player_active;
    if (scene->shadows_enabled) {
        scene->light_view_projection = render_shadow_map(scene, frame, scene->shadow_map,
            frame->player_active ? 96.0f : 500.0f, scene->shadow_map_size, 600);
        if (scene->far_shadows_active)
            scene->far_light_view_projection = render_shadow_map(scene, frame,
                scene->far_shadow_map, 768.0f, WATERFRONT_GPU_FAR_SHADOW_MAP_SIZE, 1000);
    }
    select_local_lights(scene, frame);
    set_lighting_uniforms(scene);
    counting_pass = WATERFRONT_GPU_PASS_REFLECTION;
    if (scene->reflections_enabled) {
        set_atmosphere_uniforms(scene, reflection_camera(frame).position);
        render_reflection(scene, frame, (float)width / height);
    }
    counting_pass = WATERFRONT_GPU_PASS_MAIN;
    set_atmosphere_uniforms(scene, frame->camera.position);
    ClearBackground(BLACK);
    draw_sky(scene, frame->camera, frame->squall, width, height, (float)width / height);
    BeginMode3D(camera);
    rlSetMatrixProjection(MatrixPerspective(frame->camera.vertical_fov,
        (float)width / (float)height, frame->camera.near_plane, frame->camera.far_plane));
    rlEnableBackfaceCulling();
    if (frame->boating_basin) {
        draw_scenery(scene, frame, material);
        draw_water(scene, frame, width);
        rlDisableBackfaceCulling();
        draw_life(scene, life_material, frame, true);
        draw_ghost(scene, life_material, frame, width);
        EndMode3D();
        return;
    }
    set_object_color(scene->shader, WHITE);
    set_emissive(scene->shader, false);
    draw_tiled(scene, &scene->city, material);
    draw_facades(scene, material);
    draw_tiled(scene, &scene->roofs, scene->roof_material);
    draw_ground(scene, material);
    draw_tiled(scene, &scene->corner, material);
    draw_water(scene, frame, width);
    set_emissive(scene->shader, true);
    draw_tiled(scene, &scene->windows, material);
    draw_tiled(scene, &scene->corner_emissive, material);
    if (scene->river)
        draw_colored_mesh(scene->bridge, life_material, frame->bridge, frame->bridge_color, false);
    // These small closed meshes use reusable, non-indexed buffers. Keep them two-sided while
    // retaining depth testing, so their visibility does not depend on backend cull-state defaults.
    rlDisableBackfaceCulling();
    draw_life(scene, life_material, frame, true);
    rlEnableBackfaceCulling();
    set_object_color(scene->shader, WHITE);
    set_emissive(scene->shader, false);
    draw_map_mesh(scene->crane, material);
    rlDisableBackfaceCulling();
    if (scene->river)
        draw_crane_lights(scene, life_material);
    rlEnableBackfaceCulling();
    EndMode3D();
}

void waterfront_gpu_scene_set_culling_enabled(WaterfrontGpuScene *scene, bool enabled)
{
    scene->culling_disabled = !enabled;
}

void waterfront_gpu_scene_set_instancing_enabled(WaterfrontGpuScene *scene, bool enabled)
{
    scene->instancing_disabled = !enabled;
}

void waterfront_gpu_scene_set_instance_limit(WaterfrontGpuScene *scene, int limit)
{
    scene->instance_limit = limit;
}

void waterfront_gpu_scene_set_lighting_enabled(WaterfrontGpuScene *scene, bool enabled)
{
    scene->lighting_enabled = enabled;
}

void waterfront_gpu_scene_set_shadows_enabled(WaterfrontGpuScene *scene, bool enabled)
{
    scene->shadows_enabled = enabled;
}

void waterfront_gpu_scene_set_atmosphere_enabled(WaterfrontGpuScene *scene, bool enabled)
{
    scene->atmosphere_enabled = enabled;
}

void waterfront_gpu_scene_set_reflections_enabled(WaterfrontGpuScene *scene, bool enabled)
{
    scene->reflections_enabled = enabled;
}

bool waterfront_gpu_scene_set_shadow_map_size(WaterfrontGpuScene *scene, int size)
{
    if (size != 512 && size != WATERFRONT_GPU_SHADOW_MAP_SIZE)
        return false;
    if (size == scene->shadow_map_size)
        return true;
    RenderTexture2D replacement;
    if (!create_shadow_map(&replacement, size))
        return false;
    if (scene->shadow_map.id != 0)
        rlUnloadFramebuffer(scene->shadow_map.id);
    scene->shadow_map = replacement;
    scene->shadow_map_size = size;
    return true;
}

Texture2D waterfront_gpu_scene_shadow_map(const WaterfrontGpuScene *scene)
{
    return scene->shadow_map.depth;
}

void waterfront_gpu_scene_destroy(WaterfrontGpuScene *scene)
{
    unbind_map(scene);
    if (scene->bridge.vaoId != 0)
        UnloadMesh(scene->bridge);
    for (int part = 0; part < WATERFRONT_GULL_PARTS; ++part)
        if (scene->gull[part].vaoId != 0)
            UnloadMesh(scene->gull[part]);
    if (scene->cube.vaoId != 0)
        UnloadMesh(scene->cube);
    for (int i = 0; i < WATERFRONT_TRAFFIC_MESH_SLOTS; ++i)
        if (scene->traffic_boats[i].vaoId)
            UnloadMesh(scene->traffic_boats[i]);
    if (scene->lift_span.vaoId)
        UnloadMesh(scene->lift_span);
    for (int i = 0; i < WATERFRONT_HARBOR_BOATS; ++i)
        if (scene->harbor_boats[i].vaoId)
            UnloadMesh(scene->harbor_boats[i]);
    if (scene->harbor_pontoon.vaoId)
        UnloadMesh(scene->harbor_pontoon);
    if (scene->harbor_small_pontoon.vaoId)
        UnloadMesh(scene->harbor_small_pontoon);
    if (scene->harbor_quay.vaoId)
        UnloadMesh(scene->harbor_quay);
    if (scene->harbor_ferry.vaoId)
        UnloadMesh(scene->harbor_ferry);
    if (scene->player_flag.vaoId)
        UnloadMesh(scene->player_flag);
    if (scene->ghost_flag.vaoId)
        UnloadMesh(scene->ghost_flag);
    for (int i = 0; i < WATERFRONT_RIVALS; ++i)
        if (scene->rival_flags[i].vaoId)
            UnloadMesh(scene->rival_flags[i]);
    for (int skin = 0; skin < BOAT_SKIN_COUNT; ++skin)
        if (scene->player_hull[skin].vaoId != 0)
            UnloadMesh(scene->player_hull[skin]);
    for (int skin = 0; skin < BOAT_SKIN_COUNT; ++skin)
        if (scene->player_cabin[skin].vaoId != 0)
            UnloadMesh(scene->player_cabin[skin]);
    for (int skin = 0; skin < BOAT_SKIN_COUNT; ++skin)
        if (scene->player_engine[skin].vaoId != 0)
            UnloadMesh(scene->player_engine[skin]);
    if (scene->plane.vaoId != 0)
        UnloadMesh(scene->plane);
    object_batch_destroy(scene);
    if (scene->bounds_cache) {
        ObjectBoundsCache *cache = scene->bounds_cache;
        free(cache->items);
        free(cache);
        scene->bounds_cache = NULL;
    }
    if (scene->city_material.maps != NULL) {
        scene->city_material.shader.id = rlGetShaderIdDefault();
        UnloadMaterial(scene->city_material);
    }
    if (scene->roof_material.maps != NULL) {
        scene->roof_material.shader.id = rlGetShaderIdDefault();
        UnloadMaterial(scene->roof_material);
    }
    if (scene->life_material.maps != NULL) {
        scene->life_material.shader.id = rlGetShaderIdDefault();
        UnloadMaterial(scene->life_material);
    }
    if (scene->shadow_material.maps != NULL) {
        scene->shadow_material.shader.id = rlGetShaderIdDefault();
        UnloadMaterial(scene->shadow_material);
    }
    if (scene->water_material.maps != NULL) {
        scene->water_material.shader.id = rlGetShaderIdDefault();
        UnloadMaterial(scene->water_material);
    }
    if (scene->sky_texture.id != 0)
        UnloadTexture(scene->sky_texture);
    if (scene->object_wakes.id != 0)
        UnloadTexture(scene->object_wakes);
    if (scene->sky_shader.id != 0 && scene->sky_shader.id != rlGetShaderIdDefault()) {
        forget_locations(scene->sky_shader);
        UnloadShader(scene->sky_shader);
    }
    if (scene->roof_texture.id != 0)
        UnloadTexture(scene->roof_texture);
    if (scene->shadow_map.id != 0)
        rlUnloadFramebuffer(scene->shadow_map.id);
    if (scene->far_shadow_map.id != 0)
        rlUnloadFramebuffer(scene->far_shadow_map.id);
    if (scene->reflection.id != 0)
        UnloadRenderTexture(scene->reflection);
    if (scene->shader.id != 0 && scene->shader.id != rlGetShaderIdDefault()) {
        forget_locations(scene->shader);
        UnloadShader(scene->shader);
    }
    if (scene->shadow_shader.id != 0 && scene->shadow_shader.id != rlGetShaderIdDefault()) {
        forget_locations(scene->shadow_shader);
        UnloadShader(scene->shadow_shader);
    }
    if (scene->water_shader.id != 0 && scene->water_shader.id != rlGetShaderIdDefault()) {
        forget_locations(scene->water_shader);
        UnloadShader(scene->water_shader);
    }
    *scene = (WaterfrontGpuScene) { 0 };
}
