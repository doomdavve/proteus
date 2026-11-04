#include "boat_gpu.h"
#include "boat_assets.h"
#include "raymath.h"
#include "rlgl.h"
#include "waterfront_grade.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum { POST_MODE, POST_TEXEL, POST_BLOOM, POST_GRADE, POST_LIFT, POST_GAIN, POST_BLOOM_TEXTURE };
enum { POST_BLOOM_SLOT = 1 };

static RenderTexture2D load_float_target(int width, int height, bool depth)
{
    RenderTexture2D target = { .id = rlLoadFramebuffer() };
    if (target.id == 0)
        return target;
    int format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    target.texture
        = (Texture2D) { .width = width, .height = height, .mipmaps = 1, .format = format };
    target.texture.id = rlLoadTexture(NULL, width, height, format, 1);
    if (target.texture.id != 0) {
        rlFramebufferAttach(
            target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        // A renderbuffer like LoadRenderTexture's, so the water's depth copy can blit it.
        if (depth) {
            target.depth = (Texture2D) { .id = rlLoadTextureDepth(width, height, true),
                .width = width,
                .height = height,
                .format = 19 }; // raylib's DEPTH_COMPONENT_24BIT marker.
            if (target.depth.id != 0)
                rlFramebufferAttach(
                    target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
        }
        if ((!depth || target.depth.id != 0) && rlFramebufferComplete(target.id)) {
            SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(target.texture, TEXTURE_WRAP_CLAMP);
            return target;
        }
    }
    UnloadRenderTexture(target);
    return (RenderTexture2D) { 0 };
}

static void unload_hdr_targets(RenderTexture2D *hdr, RenderTexture2D *bloom)
{
    if (hdr->id)
        UnloadRenderTexture(*hdr);
    *hdr = (RenderTexture2D) { 0 };
    for (int i = 0; i < BOAT_GPU_BLOOM_LEVELS; ++i) {
        if (bloom[i].id)
            UnloadRenderTexture(bloom[i]);
        bloom[i] = (RenderTexture2D) { 0 };
    }
}

// Replace the HDR targets with WIDTH x HEIGHT ones, or release them all on failure.
static bool allocate_hdr(BoatGpu *gpu, int width, int height)
{
    RenderTexture2D hdr = { 0 }, bloom[BOAT_GPU_BLOOM_LEVELS] = { 0 };
    bool ok = gpu->hdr_allocation_failures <= 0;
    if (!ok)
        --gpu->hdr_allocation_failures;
    if (ok) {
        hdr = load_float_target(width, height, true);
        ok = hdr.id != 0;
        // A point-sampled scene composites exactly at 1:1 and feeds the bright pass texels.
        if (ok)
            SetTextureFilter(hdr.texture, TEXTURE_FILTER_POINT);
    }
    for (int i = 0; ok && i < BOAT_GPU_BLOOM_LEVELS; ++i) {
        int w = width >> (i + 1), h = height >> (i + 1);
        bloom[i] = load_float_target(w > 0 ? w : 1, h > 0 ? h : 1, false);
        ok = bloom[i].id != 0;
    }
    unload_hdr_targets(&gpu->hdr_target, gpu->bloom);
    if (!ok) {
        unload_hdr_targets(&hdr, bloom);
        TraceLog(LOG_WARNING, "HDR targets of %dx%d unavailable; presenting 8-bit", width, height);
        return false;
    }
    gpu->hdr_target = hdr;
    memcpy(gpu->bloom, bloom, sizeof(bloom));
    return true;
}

bool boat_gpu_hdr_active(const BoatGpu *gpu) { return gpu->hdr_target.id != 0; }

size_t boat_gpu_hdr_bytes(const BoatGpu *gpu)
{
    if (!gpu->hdr_target.id)
        return 0;
    size_t pixels = (size_t)gpu->hdr_target.texture.width * (size_t)gpu->hdr_target.texture.height;
    size_t bytes = pixels * (8 + 4); // RGBA16F colour and a 24/8 depth-stencil buffer.
    for (int i = 0; i < BOAT_GPU_BLOOM_LEVELS; ++i)
        bytes += (size_t)gpu->bloom[i].texture.width * (size_t)gpu->bloom[i].texture.height * 8;
    return bytes;
}

bool boat_gpu_set_hdr(BoatGpu *gpu, bool enabled)
{
    gpu->hdr_requested = enabled;
    if (!enabled) {
        unload_hdr_targets(&gpu->hdr_target, gpu->bloom);
        return false;
    }
    if (!gpu->post.id) {
        char path[1024];
        if (!boat_asset_path(path, sizeof(path), "shaders/gpu_post.fs"))
            return false;
        Shader post = LoadShader(NULL, path);
        if (!IsShaderValid(post) || post.id == rlGetShaderIdDefault()) {
            TraceLog(LOG_WARNING, "HDR post shader unavailable; presenting 8-bit");
            return false;
        }
        gpu->post = post;
        const char *names[] = { "postMode", "sourceTexel", "bloomParameters", "gradeParameters",
            "gradeLift", "gradeGain", "bloomTexture" };
        for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); ++i)
            gpu->post_locations[i] = GetShaderLocation(post, names[i]);
        int slot = POST_BLOOM_SLOT;
        SetShaderValue(post, gpu->post_locations[POST_BLOOM_TEXTURE], &slot, SHADER_UNIFORM_INT);
    }
    if (!waterfront_gpu_scene_float_blending(&gpu->scene)) {
        TraceLog(LOG_INFO, "HDR: float blending unavailable; presenting 8-bit");
        return false;
    }
    if (gpu->hdr_target.id && gpu->hdr_target.texture.width == gpu->target.texture.width
        && gpu->hdr_target.texture.height == gpu->target.texture.height)
        return true;
    if (!allocate_hdr(gpu, gpu->target.texture.width, gpu->target.texture.height))
        return false;
    TraceLog(LOG_INFO, "HDR: %dx%d RGBA16F scene, %d bloom levels", gpu->target.texture.width,
        gpu->target.texture.height, BOAT_GPU_BLOOM_LEVELS);
    return true;
}

// Draw SOURCE over all of DESTINATION with the post shader in MODE. Texture coordinates follow
// the targets' own (bottom-up) rows, so no pass flips the image.
static void post_pass(BoatGpu *gpu, RenderTexture2D destination, Texture2D source, int mode,
    unsigned int bloom, bool add)
{
    int width = destination.texture.width, height = destination.texture.height;
    BeginTextureMode(destination);
    rlSetMatrixProjection(MatrixOrtho(0, width, 0, height, -1, 1));
    rlSetMatrixModelview(MatrixIdentity());
    if (add) {
        rlSetBlendFactors(RL_ONE, RL_ONE, RL_FUNC_ADD);
        BeginBlendMode(BLEND_CUSTOM);
    } else {
        rlDisableColorBlend();
    }
    BeginShaderMode(gpu->post);
    SetShaderValue(gpu->post, gpu->post_locations[POST_MODE], &mode, SHADER_UNIFORM_INT);
    Vector2 texel = { 1.0f / (float)source.width, 1.0f / (float)source.height };
    SetShaderValue(gpu->post, gpu->post_locations[POST_TEXEL], &texel, SHADER_UNIFORM_VEC2);
    // Every declared sampler gets a texture, even in modes that do not read it.
    rlActiveTextureSlot(POST_BLOOM_SLOT);
    rlEnableTexture(bloom ? bloom : rlGetTextureIdDefault());
    rlActiveTextureSlot(0);
    rlSetTexture(source.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    rlTexCoord2f(0, 0);
    rlVertex2f(0, 0);
    rlTexCoord2f(1, 0);
    rlVertex2f((float)width, 0);
    rlTexCoord2f(1, 1);
    rlVertex2f((float)width, (float)height);
    rlTexCoord2f(0, 1);
    rlVertex2f(0, (float)height);
    rlEnd();
    rlDrawRenderBatchActive();
    rlSetTexture(0);
    EndShaderMode();
    rlActiveTextureSlot(POST_BLOOM_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(0);
    if (add)
        EndBlendMode();
    else
        rlEnableColorBlend();
    EndTextureMode();
}

void boat_gpu_present_hdr(BoatGpu *gpu, float phase)
{
    WaterfrontGrade grade = waterfront_grade(phase);
    Vector3 bloom = { grade.bloom_threshold, grade.bloom_knee, grade.bloom_strength };
    Vector3 look = { grade.exposure, grade.contrast, grade.saturation };
    SetShaderValue(gpu->post, gpu->post_locations[POST_BLOOM], &bloom, SHADER_UNIFORM_VEC3);
    SetShaderValue(gpu->post, gpu->post_locations[POST_GRADE], &look, SHADER_UNIFORM_VEC3);
    SetShaderValue(gpu->post, gpu->post_locations[POST_LIFT], &grade.lift, SHADER_UNIFORM_VEC3);
    SetShaderValue(gpu->post, gpu->post_locations[POST_GAIN], &grade.gain, SHADER_UNIFORM_VEC3);
    unsigned int blurred = 0;
    if (grade.bloom_strength > 0) {
        post_pass(gpu, gpu->bloom[0], gpu->hdr_target.texture, 0, 0, false);
        for (int i = 1; i < BOAT_GPU_BLOOM_LEVELS; ++i)
            post_pass(gpu, gpu->bloom[i], gpu->bloom[i - 1].texture, 1, 0, false);
        for (int i = BOAT_GPU_BLOOM_LEVELS - 1; i > 0; --i)
            post_pass(gpu, gpu->bloom[i - 1], gpu->bloom[i].texture, 2, 0, true);
        blurred = gpu->bloom[0].texture.id;
    }
    post_pass(gpu, gpu->target, gpu->hdr_target.texture, 3, blurred, false);
}

void boat_gpu_destroy(BoatGpu *gpu)
{
    unload_hdr_targets(&gpu->hdr_target, gpu->bloom);
    if (gpu->post.id)
        UnloadShader(gpu->post);
    if (gpu->wake_texture.id)
        UnloadTexture(gpu->wake_texture);
    MemFree(gpu->wake_upload);
    if (gpu->basin_water.vaoId)
        UnloadMesh(gpu->basin_water);
    if (gpu->wave_mesh.vaoId)
        UnloadMesh(gpu->wave_mesh);
    if (gpu->wave_mesh_extra.vaoId)
        UnloadMesh(gpu->wave_mesh_extra);
    boat_water_mesh_cache_destroy(&gpu->water_mesh);
    waterfront_gpu_scene_destroy(&gpu->scene);
    waterfront_scenery_destroy(&gpu->scenery);
    if (gpu->downsample.id)
        UnloadShader(gpu->downsample);
    if (gpu->supersampled.id)
        UnloadRenderTexture(gpu->supersampled);
    if (gpu->target.id)
        UnloadRenderTexture(gpu->target);
    *gpu = (BoatGpu) { 0 };
}

bool boat_gpu_resize(BoatGpu *gpu, int width, int height)
{
    if (width < 1 || height < 1 || width > 2048 || height > 2048)
        return false;
    if (gpu->target.texture.width == width && gpu->target.texture.height == height)
        return true;
    RenderTexture2D target = LoadRenderTexture(width, height);
    RenderTexture2D supersampled = { 0 };
    if (!IsRenderTextureValid(target)) {
        if (target.id)
            UnloadRenderTexture(target);
        return false;
    }
    SetTextureFilter(target.texture, TEXTURE_FILTER_POINT);
    if (gpu->downsample.id) {
        supersampled = LoadRenderTexture(width * 4, height * 4);
        if (!IsRenderTextureValid(supersampled)) {
            if (supersampled.id)
                UnloadRenderTexture(supersampled);
            UnloadRenderTexture(target);
            return false;
        }
        SetTextureFilter(supersampled.texture, TEXTURE_FILTER_POINT);
    }
    if (gpu->target.id)
        UnloadRenderTexture(gpu->target);
    if (gpu->supersampled.id)
        UnloadRenderTexture(gpu->supersampled);
    gpu->target = target;
    gpu->supersampled = supersampled;
    // The HDR chain follows the target; failing that, presentation stays 8-bit.
    if (gpu->hdr_requested && gpu->hdr_target.id)
        allocate_hdr(gpu, width, height);
    return true;
}

bool boat_gpu_init(BoatGpu *gpu, bool aa)
{
    char shader_path[1024];
    if (!boat_asset_path(shader_path, sizeof(shader_path), "shaders/waterfront_city.fs")) {
        TraceLog(LOG_ERROR, "Boat shader path is too long");
        goto failed;
    }
    if (!waterfront_gpu_scene_init(&gpu->scene, shader_path))
        goto failed;
    if (aa) {
        if (!boat_asset_path(shader_path, sizeof(shader_path), "shaders/gpu_downsample.fs"))
            goto failed;
        gpu->downsample = LoadShader(NULL, shader_path);
        if (!IsShaderValid(gpu->downsample) || gpu->downsample.id == rlGetShaderIdDefault())
            goto failed;
    }
    if (!boat_gpu_resize(gpu, 320, 200))
        goto failed;
    return true;
failed:
    boat_gpu_destroy(gpu);
    return false;
}

void boat_gpu_draw(BoatGpu *gpu, const BoatViewState *view, const BoatPresentation *presentation,
    const BoatWorld *world, BoatCamera camera)
{
    boat_gpu_draw_with_ghost(gpu, view, presentation, world, camera, NULL, NULL, 0);
}

static bool candidate_strip(Mesh *m, int first, int rows)
{
    m->vertexCount = 322 * (rows + 1);
    m->triangleCount = 321 * rows * 2;
    m->vertices = MemAlloc((unsigned int)m->vertexCount * 3 * sizeof(float));
    m->indices = MemAlloc((unsigned int)m->triangleCount * 3 * sizeof(unsigned short));
    if (!m->vertices || !m->indices) {
        UnloadMesh(*m);
        *m = (Mesh) { 0 };
        return false;
    }
    for (int z = 0; z <= rows; ++z)
        for (int x = 0; x < 322; ++x) {
            int row = first + z;
            Vector3 v = { x == 0 ? -120
                    : x == 321   ? 120
                                 : -119.625f + (x - 1) * .75f,
                0,
                row == 0         ? -80
                    : row == 215 ? 80
                                 : -79.875f + (row - 1) * .75f };
            memcpy(m->vertices + 3 * (z * 322 + x), &v, sizeof(v));
        }
    for (int z = 0; z < rows; ++z)
        for (int x = 0; x < 321; ++x) {
            unsigned short a = (unsigned short)(z * 322 + x), b = (unsigned short)(a + 322);
            unsigned short indices[] = { a, b, (unsigned short)(b + 1), a, (unsigned short)(b + 1),
                (unsigned short)(a + 1) };
            memcpy(m->indices + 6 * (z * 321 + x), indices, sizeof(indices));
        }
    UploadMesh(m, false);
    return m->vaoId != 0;
}

void boat_gpu_draw_with_ghost(BoatGpu *gpu, const BoatViewState *view,
    const BoatPresentation *presentation, const BoatWorld *world, BoatCamera camera,
    const BoatViewState *ghost, const BoatFlag *ghost_flag, float opacity)
{
    boat_gpu_draw_scene(gpu, view, presentation, world, camera, NULL, ghost, ghost_flag, opacity);
}

void boat_gpu_draw_scene(BoatGpu *gpu, const BoatViewState *view,
    const BoatPresentation *presentation, const BoatWorld *world, BoatCamera camera,
    const BoatRivals *rivals, const BoatViewState *ghost, const BoatFlag *ghost_flag, float opacity)
{
    const Boat *boat = &view->boat;
    gpu->failed = false;
    bool mapped = world->definition->mapped;
    const BoatMapDescription *map = world->definition->map;
    if (mapped && gpu->scene.map != map
        && (!waterfront_scenery_bind(&gpu->scenery, map)
            || !waterfront_gpu_scene_bind(&gpu->scene, &gpu->scenery))) {
        gpu->failed = true;
        return;
    }
    static WaterfrontSceneFrame frame; // Large; drawing is single-threaded.
    boat_scene_frame(view, presentation, world, camera, &frame);
    frame.lighting = waterfront_lighting(gpu->daylight);
    boat_scene_rivals(rivals, world, view->time, &frame);
    if (ghost && opacity > 0) {
        WaterfrontPlayerState pose = { 0 };
        boat_player_pose(ghost, ghost_flag, &pose);
        frame.ghost_opacity = opacity;
        frame.ghost_skin = pose.skin;
        frame.ghost_hull = pose.hull;
        frame.ghost_cabin = pose.cabin;
        frame.ghost_engine = pose.engine;
        frame.ghost_flag = pose.flag;
    }
    // HDR applies to the target it was allocated for; the mirror swaps in a smaller one.
    bool hdr = gpu->hdr_target.id && !gpu->supersampled.id
        && gpu->hdr_target.texture.width == gpu->target.texture.width
        && gpu->hdr_target.texture.height == gpu->target.texture.height;
    RenderTexture2D target = hdr ? gpu->hdr_target
        : gpu->supersampled.id   ? gpu->supersampled
                                 : gpu->target;
    gpu->scene.detail_scale = gpu->supersampled.id ? 4 : 1;
    gpu->scene.emissive_hdr = hdr;
    Mesh original_water = gpu->scene.water;
    if (!mapped) {
        if (!gpu->basin_water.vaoId)
            gpu->basin_water = GenMeshPlane(
                2 * world->definition->half_extent.x, 2 * world->definition->half_extent.y, 1, 1);
        gpu->scene.water = gpu->basin_water;
    }
    gpu->scene.wake_field = (Texture2D) { 0 };
    if (world->wake_field) {
        clock_t begin = clock();
        BoatWakeConfig c = boat_wake_config(world->wake_field);
        if (!gpu->wake_upload) {
            gpu->wake_upload = MemAlloc((unsigned int)(c.width * c.height * sizeof(float)));
            if (!gpu->wake_upload)
                goto restore_water;
            boat_wake_copy_heights(world->wake_field, gpu->wake_upload);
            Image image = { gpu->wake_upload, (int)c.width, (int)c.height, 1,
                PIXELFORMAT_UNCOMPRESSED_R32 };
            gpu->wake_texture = LoadTextureFromImage(image);
            SetTextureFilter(gpu->wake_texture, TEXTURE_FILTER_POINT);
            SetTextureWrap(gpu->wake_texture, TEXTURE_WRAP_CLAMP);
        } else {
            boat_wake_copy_heights(world->wake_field, gpu->wake_upload);
            UpdateTexture(gpu->wake_texture, gpu->wake_upload);
        }
        if (!gpu->wake_texture.id)
            goto restore_water;
        gpu->scene.wake_field = gpu->wake_texture;
        gpu->scene.wake_spacing = (float)c.spacing;
        if (!gpu->wave_mesh.vaoId || !gpu->wave_mesh_extra.vaoId) {
            if ((!gpu->wave_mesh.vaoId && !candidate_strip(&gpu->wave_mesh, 0, 128))
                || (!gpu->wave_mesh_extra.vaoId
                    && !candidate_strip(&gpu->wave_mesh_extra, 128, 87)))
                goto restore_water;
            gpu->wave_capacity = gpu->wave_mesh.vertexCount + gpu->wave_mesh_extra.vertexCount;
        }
        gpu->scene.near_water = gpu->wave_mesh;
        gpu->scene.near_water_extra = gpu->wave_mesh_extra;
        gpu->wake_upload_seconds += (double)(clock() - begin) / CLOCKS_PER_SEC;
        ++gpu->wake_uploads;
    } else if (boat_water_displaced(world->water)) {
        Vector2 center
            = { roundf(-boat->position.x / 12) * 12, roundf(boat->position.y / 12) * 12 };
        Mesh *mesh = &gpu->wave_mesh;
        if (!mesh->vaoId || !Vector2Equals(center, gpu->wave_center) || map != gpu->wave_map) {
            Vector2 world_center = { -boat->position.x, boat->position.y };
            const Mesh3D *surface = (world->water.objects || world->water.traffic_objects)
                ? boat_water_patch_arcade(&gpu->water_mesh, map, world_center)
                : boat_water_patch(&gpu->water_mesh, map, world_center);
            if (!surface)
                goto restore_water;
            int count = (int)surface->triangle_count * 3;
            if (count > gpu->wave_capacity || !mesh->vaoId) {
                if (mesh->vaoId)
                    UnloadMesh(*mesh);
                *mesh = (Mesh) { 0 };
                gpu->wave_capacity = 3072;
                while (gpu->wave_capacity < count)
                    gpu->wave_capacity *= 2;
                mesh->vertexCount = gpu->wave_capacity;
                mesh->triangleCount = gpu->wave_capacity / 3;
                mesh->vertices = MemAlloc((unsigned int)gpu->wave_capacity * 3 * sizeof(float));
                if (!mesh->vertices) {
                    UnloadMesh(*mesh);
                    *mesh = (Mesh) { 0 };
                    goto restore_water;
                }
                memset(mesh->vertices, 0, (size_t)gpu->wave_capacity * 3 * sizeof(float));
                UploadMesh(mesh, true);
            }
            for (int i = 0; i < count; ++i) {
                Vector3 v = surface->vertices[surface->triangles[i / 3].vertices[i % 3]];
                memcpy(&mesh->vertices[i * 3], &v, sizeof(v));
            }
            UpdateMeshBuffer(*mesh, 0, mesh->vertices, count * 3 * (int)sizeof(float), 0);
            mesh->vertexCount = count;
            mesh->triangleCount = count / 3;
            gpu->wave_center = center;
            gpu->wave_map = map;
            gpu->wave_peak_triangles
                = count / 3 > gpu->wave_peak_triangles ? count / 3 : gpu->wave_peak_triangles;
            ++gpu->wave_rebuilds;
        }
        gpu->scene.near_water = *mesh;
    }
    BeginTextureMode(target);
    if (!waterfront_gpu_scene_draw(
            &gpu->scene, &frame, target.texture.width, target.texture.height))
        gpu->failed = true;
    float overcast = boat_view_overcast(view, world);
    if (overcast > 0) {
        Color tint = BOAT_VIEW_STORM_LIGHT;
        tint.a = (unsigned char)(overcast * 255 + .5f);
        DrawRectangle(0, 0, target.texture.width, target.texture.height, tint);
    }
    EndTextureMode();
restore_water:
    if ((world->water.objects || world->water.traffic_objects) && !gpu->scene.object_wakes.id)
        gpu->failed = true;
    if (world->wake_field
        && (!gpu->wake_texture.id || !gpu->wave_mesh.vaoId || !gpu->wave_mesh_extra.vaoId))
        gpu->failed = true;
    gpu->scene.water = original_water;
    gpu->scene.near_water = (Mesh) { 0 };
    gpu->scene.near_water_extra = (Mesh) { 0 };
    if (hdr)
        boat_gpu_present_hdr(gpu, frame.lighting.phase);
    if (gpu->supersampled.id) {
        BeginTextureMode(gpu->target);
        ClearBackground(BLACK);
        BeginShaderMode(gpu->downsample);
        DrawTexturePro(target.texture,
            (Rectangle) { 0, 0, target.texture.width, -target.texture.height },
            (Rectangle) { 0, 0, gpu->target.texture.width, gpu->target.texture.height },
            (Vector2) { 0 }, 0, WHITE);
        EndShaderMode();
        EndTextureMode();
    }
}
