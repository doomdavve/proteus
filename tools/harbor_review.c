// Repeatable CPU/GPU berth views; all poses come from the same shared snapshot as playback.
#include "boat_water_mesh.h"
#include "renderer_gpu.h"
#include "waterfront.h"
#include "waterfront_city.h"

#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
    puts("Usage: harbor_review [--view marina|workboat|overview|launch|traffic|ferry] [--renderer "
         "cpu|gpu]\n"
         "       [--waves] [--time SECONDS] [--screenshot FILE] [--frames COUNT]\n"
         "       [--resolution 320x180|320x200|320x240]\n"
         "Default: GPU, marina, interactive. Screenshots render once; --frames bounds playback.\n"
         "GPU uses 4x4 supersampling. CPU screenshots need no graphics context. Esc exits.");
}

// Double world seconds up to 30 days, for long-session water and traffic probes.
static bool number(const char *text, double *value)
{
    char *end;
    *value = strtod(text, &end);
    return end != text && !*end && isfinite(*value) && *value >= 0 && *value <= 2592000;
}

static Mesh patch(BoatWaterMeshCache *water_mesh, Vector3 center)
{
    const Mesh3D *source = boat_water_patch(
        water_mesh, &waterfront_city_river_map, (Vector2) { center.x, center.z });
    Mesh mesh = { 0 };
    if (!source)
        return mesh;
    mesh.triangleCount = (int)source->triangle_count;
    mesh.vertexCount = mesh.triangleCount * 3;
    mesh.vertices = MemAlloc((unsigned int)mesh.vertexCount * 3 * sizeof(float));
    if (!mesh.vertices)
        return (Mesh) { 0 };
    for (int i = 0; i < mesh.vertexCount; ++i) {
        Vector3 v = source->vertices[source->triangles[i / 3].vertices[i % 3]];
        memcpy(&mesh.vertices[i * 3], &v, sizeof(v));
    }
    UploadMesh(&mesh, false);
    return mesh;
}

static View3D follow_vessel(View3D camera, const char *view, double time)
{
    int index = !strcmp(view, "ferry") ? 3 : !strcmp(view, "launch") ? 0 : -1;
    if (index >= 0) {
        WaterfrontVesselMotion m = waterfront_traffic_motion(index, time);
        Vector3 forward = Vector3Normalize(m.velocity);
        Vector3 right = { -forward.z, 0, forward.x };
        camera.position = Vector3Add(m.position,
            Vector3Add(Vector3Add(Vector3Scale(right, index == 3 ? 32 : 13),
                           Vector3Scale(forward, index == 3 ? 12 : 10)),
                (Vector3) { 0, index == 3 ? 10 : 7, 0 }));
        camera.target = Vector3Add(m.position, (Vector3) { 0, index == 3 ? 2 : .9f, 0 });
    }
    return camera;
}

int main(int argc, char **argv)
{
    const char *view = "marina", *screenshot = NULL;
    bool gpu = true, waves = false;
    double time = 0;
    int frames = 0, height = 200;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help")) {
            usage();
            return 0;
        } else if (!strcmp(argv[i], "--view") && i + 1 < argc) {
            view = argv[++i];
            if (strcmp(view, "marina") && strcmp(view, "workboat") && strcmp(view, "overview")
                && strcmp(view, "ferry") && strcmp(view, "launch") && strcmp(view, "traffic"))
                goto invalid;
        } else if (!strcmp(argv[i], "--renderer") && i + 1 < argc) {
            const char *name = argv[++i];
            if (strcmp(name, "cpu") && strcmp(name, "gpu"))
                goto invalid;
            gpu = !strcmp(name, "gpu");
        } else if (!strcmp(argv[i], "--waves")) {
            waves = true;
        } else if (!strcmp(argv[i], "--time") && i + 1 < argc) {
            if (!number(argv[++i], &time))
                goto invalid;
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            char *end;
            long n = strtol(argv[++i], &end, 10);
            if (*end || end == argv[i] || n < 1 || n > 36000)
                goto invalid;
            frames = (int)n;
        } else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (!strcmp(argv[i], "--resolution") && i + 1 < argc) {
            const char *size = argv[++i];
            if (!strcmp(size, "320x180"))
                height = 180;
            else if (!strcmp(size, "320x200"))
                height = 200;
            else if (!strcmp(size, "320x240"))
                height = 240;
            else
                goto invalid;
        } else {
            goto invalid;
        }
    }
    SetTraceLogLevel(LOG_WARNING);
    View3D camera = { .position = { 12, 8, 157 },
        .target = { -15, 2, 182 },
        .up = { 0, 1, 0 },
        .vertical_fov = 58 * DEG2RAD,
        .near_plane = .25f,
        .far_plane = 1400 };
    if (!strcmp(view, "workboat")) {
        camera.position = (Vector3) { 20, 6.5f, 18 };
        camera.target = (Vector3) { 42, 1.8f, 35 };
    } else if (!strcmp(view, "overview")) {
        camera.position = (Vector3) { 145, 145, 85 };
        camera.target = (Vector3) { 4, 0, 125 };
    }
    if (!strcmp(view, "traffic")) {
        camera.position = (Vector3) { 220, 65, -80 };
        camera.target = (Vector3) { 50, 0, -290 };
    }
    camera = follow_vessel(camera, view, time);
    Vector3 patch_center = camera.target;
    bool window = gpu || !screenshot;
    if (window) {
        if (screenshot)
            SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(960, height * 3, "Proteus / harbor review");
        if (!IsWindowReady())
            return 1;
        SetTargetFPS(60);
    }
    int result = 0, count = 0;
    Color *pixels = NULL;
    Renderer3D cpu = { 0 };
    BoatWaterMeshCache water_mesh = { 0 };
    WaterfrontScenery scenery = { 0 };
    WaterfrontGpuScene scene = { 0 };
    RenderTexture2D target = { 0 }, resolved = { 0 };
    Shader downsample = { 0 };
    Texture2D texture = { 0 };
    Mesh near = { 0 };
    BoatWater water = waves ? boat_water_gentle() : (BoatWater) { 0 };
    if (!waterfront_scenery_init(&scenery, &waterfront_city_river_map)) {
        result = 1;
        goto cleanup;
    }
    if (gpu) {
        char path[1024];
        int n = snprintf(
            path, sizeof(path), "%sassets/shaders/waterfront_city.fs", GetApplicationDirectory());
        if (n < 0 || (size_t)n >= sizeof(path) || !waterfront_gpu_scene_init(&scene, path)
            || !waterfront_gpu_scene_bind(&scene, &scenery)) {
            result = 1;
            goto cleanup;
        }
        target = LoadRenderTexture(1280, height * 4);
        resolved = LoadRenderTexture(320, height);
        n = snprintf(
            path, sizeof(path), "%sassets/shaders/gpu_downsample.fs", GetApplicationDirectory());
        if (n < 0 || (size_t)n >= sizeof(path)) {
            result = 1;
            goto cleanup;
        }
        downsample = LoadShader(NULL, path);
        if (!IsRenderTextureValid(target) || !IsRenderTextureValid(resolved)
            || !IsShaderValid(downsample) || downsample.id == rlGetShaderIdDefault()) {
            result = 1;
            goto cleanup;
        }
        SetTextureFilter(resolved.texture, TEXTURE_FILTER_POINT);
        if (waves) {
            near = patch(&water_mesh, camera.target);
            if (!near.vaoId) {
                result = 1;
                goto cleanup;
            }
            scene.near_water = near;
        }
    } else {
        pixels = malloc((size_t)320 * height * sizeof(*pixels));
        if (!pixels) {
            result = 1;
            goto cleanup;
        }
    }
    double sum = 0, peak = 0;
    do {
        WaterfrontSceneFrame frame;
        double local_time = time + count / 60.0;
        waterfront_scene_evaluate(local_time, water, &frame);
        frame.camera = follow_vessel(camera, view, local_time);
        frame.player_active = true;
        frame.player_water = water;
        // Keep the player out of the scene; its X/Z still centres the shared GPU wave patch.
        frame.player.hull = frame.player.cabin = frame.player.engine
            = (Transform3D) { .position = { frame.camera.target.x, -100, frame.camera.target.z },
                  .scale = { 1, 1, 1 } };
        double start = window ? GetTime() : 0;
        if (gpu) {
            if (waves && Vector3Distance(frame.camera.target, patch_center) > 12) {
                UnloadMesh(near);
                near = patch(&water_mesh, frame.camera.target);
                if (!near.vaoId) {
                    result = 1;
                    break;
                }
                scene.near_water = near;
                patch_center = frame.camera.target;
            }
            BeginTextureMode(target);
            waterfront_gpu_scene_draw(&scene, &frame, 1280, height * 4);
            EndTextureMode();
            BeginTextureMode(resolved);
            ClearBackground(BLACK);
            BeginShaderMode(downsample);
            DrawTexturePro(target.texture, (Rectangle) { 0, 0, 1280, -height * 4.0f },
                (Rectangle) { 0, 0, 320, (float)height }, (Vector2) { 0 }, 0, WHITE);
            EndShaderMode();
            EndTextureMode();
        } else {
            if (!waterfront_draw_boating(
                    &cpu, &scenery, pixels, 320, height, frame.camera, waves)) {
                result = 1;
                break;
            }
            if (waves) {
                const Mesh3D *surface = boat_water_surface(
                    &water_mesh, &waterfront_city_river_map, water, frame.time);
                if (!surface) {
                    result = 1;
                    break;
                }
                renderer3d_draw_emissive_mesh(
                    &cpu, surface, (Transform3D) { .scale = { 1, 1, 1 } });
            }
            waterfront_draw_traffic(&cpu, &frame.traffic);
            waterfront_draw_harbor(&cpu, &frame.harbor);
        }
        if (window) {
            double elapsed = (GetTime() - start) * 1000;
            sum += elapsed;
            peak = fmax(peak, elapsed);
        }
        if (screenshot) {
            Image image;
            if (gpu) {
                image = LoadImageFromTexture(resolved.texture);
                ImageFlipVertical(&image);
            } else {
                image = (Image) { pixels, 320, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
            }
            result = ExportImage(image, screenshot) ? 0 : 1;
            if (gpu)
                UnloadImage(image);
            break;
        }
        if (!gpu) {
            Image image = { pixels, 320, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
            if (!texture.id) {
                texture = LoadTextureFromImage(image);
                SetTextureFilter(texture, TEXTURE_FILTER_POINT);
            } else {
                UpdateTexture(texture, pixels);
            }
        }
        BeginDrawing();
        ClearBackground(BLACK);
        Texture2D shown = gpu ? resolved.texture : texture;
        DrawTexturePro(shown,
            (Rectangle) { 0, 0, (float)shown.width, (gpu ? -1.0f : 1.0f) * shown.height },
            (Rectangle) { 0, 0, 960, height * 3.0f }, (Vector2) { 0 }, 0, WHITE);
        EndDrawing();
        ++count;
    } while ((!frames || count < frames) && !WindowShouldClose());
    if (count && window)
        printf("harbor_view=%s frames=%d render_ms_mean=%.3f render_ms_max=%.3f scope=%s\n", view,
            count, sum / count, peak, gpu ? "CPU_submission_at_4x" : "CPU_rendering");
cleanup:
    if (texture.id)
        UnloadTexture(texture);
    if (near.vaoId)
        UnloadMesh(near);
    if (gpu)
        waterfront_gpu_scene_destroy(&scene);
    if (target.id)
        UnloadRenderTexture(target);
    if (resolved.id)
        UnloadRenderTexture(resolved);
    if (downsample.id)
        UnloadShader(downsample);
    renderer3d_destroy(&cpu);
    free(pixels);
    boat_water_mesh_cache_destroy(&water_mesh);
    waterfront_scenery_destroy(&scenery);
    if (window)
        CloseWindow();
    return result;
invalid:
    usage();
    return 2;
}
