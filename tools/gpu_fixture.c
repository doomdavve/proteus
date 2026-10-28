#include "boat_test_scenery.h"
#include "external/glad.h"
#include "raylib.h"
#include "raymath.h"
#include "renderer_gpu.h"
#include "rlgl.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PROTEUS_ASSET_DIR
#define PROTEUS_ASSET_DIR "assets"
#endif

typedef struct {
    int width;
    int height;
} FixtureSize;

typedef struct {
    FixtureSize size;
    const char *screenshot_path;
    int frames;
    bool visible;
    bool check;
    bool invalid_shader;
    bool city;
    bool lighting;
    bool shadows;
    bool atmosphere;
    bool reflections;
    bool shadow_map_view;
    int shadow_map_size;
    float time;
} FixtureOptions;

static const FixtureSize SUPPORTED_SIZES[] = {
    { 320, 180 },
    { 320, 200 },
    { 320, 240 },
};

static void print_usage(const char *program)
{
    printf("Usage: %s [--check] [--visible] [--frames N] [--screenshot FILE]\n", program);
    printf("       %s [--city --time SECONDS] [--lights on|off] [--shadows on|off] [--atmosphere "
           "on|off] [--reflections on|off]\n",
        program);
    printf("       %s [--city --shadow-map] [--resolution WIDTHxHEIGHT]\n", program);
    printf("Options:\n");
    printf("  --check                 verify depth, orientation, target resizing, capture, and "
           "city sky\n");
    printf("  --visible               present the virtual target in a window\n");
    printf("  --frames N              visible frames to present (default: 1)\n");
    printf("  --resolution WIDTHxHEIGHT (default: 320x200)\n");
    printf("  --screenshot FILE       export the virtual target as a PNG\n");
    printf("  --invalid-shader        verify a shader error fails without a fallback\n");
    printf("  --city                  render the static waterfront GPU scene\n");
    printf("  --time SECONDS          city actor time, fixed dock view (default: 0)\n");
    printf("  --lights on|off         city local-light diagnostic switch (default: on)\n");
    printf("  --shadows on|off        city directional-shadow diagnostic switch (default: on)\n");
    printf("  --atmosphere on|off     city distance-atmosphere diagnostic switch (default: on)\n");
    printf("  --reflections on|off    city water-reflection diagnostic switch (default: on)\n");
    printf("  --shadow-resolution N   directional-shadow map size: 512 or 1024 (default: 1024)\n");
    printf("  --shadow-map            export/present the light-frustum depth-map diagnostic\n");
}

static bool parse_count(const char *text, int *value)
{
    char *end = NULL;
    long parsed = strtol(text, &end, 10);
    if (text[0] == '\0' || *end != '\0' || parsed < 1 || parsed > INT_MAX)
        return false;
    *value = (int)parsed;
    return true;
}

static bool parse_time(const char *text, float *value)
{
    char *end = NULL;
    float parsed = strtof(text, &end);
    if (text[0] == '\0' || *end != '\0' || parsed < 0.0f)
        return false;
    *value = parsed;
    return true;
}

static bool parse_size(const char *text, FixtureSize *size)
{
    for (size_t i = 0; i < sizeof(SUPPORTED_SIZES) / sizeof(SUPPORTED_SIZES[0]); ++i) {
        char candidate[32];
        snprintf(candidate, sizeof(candidate), "%dx%d", SUPPORTED_SIZES[i].width,
            SUPPORTED_SIZES[i].height);
        if (strcmp(candidate, text) == 0) {
            *size = SUPPORTED_SIZES[i];
            return true;
        }
    }
    return false;
}

static bool parse_options(int argc, char **argv, FixtureOptions *options)
{
    *options = (FixtureOptions) {
        .size = { 320, 200 },
        .frames = 1,
        .lighting = true,
        .shadows = true,
        .atmosphere = true,
        .reflections = true,
        .shadow_map_size = WATERFRONT_GPU_SHADOW_MAP_SIZE,
    };

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--check") == 0) {
            options->check = true;
        } else if (strcmp(argv[i], "--visible") == 0) {
            options->visible = true;
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            if (!parse_count(argv[++i], &options->frames)) {
                fprintf(stderr, "Invalid frame count: %s\n", argv[i]);
                return false;
            }
        } else if (strcmp(argv[i], "--resolution") == 0 && i + 1 < argc) {
            if (!parse_size(argv[++i], &options->size)) {
                fprintf(stderr, "Unsupported resolution: %s\n", argv[i]);
                return false;
            }
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            options->screenshot_path = argv[++i];
        } else if (strcmp(argv[i], "--invalid-shader") == 0) {
            options->invalid_shader = true;
        } else if (strcmp(argv[i], "--city") == 0) {
            options->city = true;
        } else if (strcmp(argv[i], "--lights") == 0 && i + 1 < argc) {
            const char *lighting = argv[++i];
            if (strcmp(lighting, "on") == 0)
                options->lighting = true;
            else if (strcmp(lighting, "off") == 0)
                options->lighting = false;
            else {
                fprintf(stderr, "Lights must be on or off: %s\n", lighting);
                return false;
            }
        } else if (strcmp(argv[i], "--shadows") == 0 && i + 1 < argc) {
            const char *shadows = argv[++i];
            if (strcmp(shadows, "on") == 0)
                options->shadows = true;
            else if (strcmp(shadows, "off") == 0)
                options->shadows = false;
            else {
                fprintf(stderr, "Shadows must be on or off: %s\n", shadows);
                return false;
            }
        } else if (strcmp(argv[i], "--atmosphere") == 0 && i + 1 < argc) {
            const char *atmosphere = argv[++i];
            if (strcmp(atmosphere, "on") == 0)
                options->atmosphere = true;
            else if (strcmp(atmosphere, "off") == 0)
                options->atmosphere = false;
            else {
                fprintf(stderr, "Atmosphere must be on or off: %s\n", atmosphere);
                return false;
            }
        } else if (strcmp(argv[i], "--reflections") == 0 && i + 1 < argc) {
            const char *reflections = argv[++i];
            if (strcmp(reflections, "on") == 0)
                options->reflections = true;
            else if (strcmp(reflections, "off") == 0)
                options->reflections = false;
            else {
                fprintf(stderr, "Reflections must be on or off: %s\n", reflections);
                return false;
            }
        } else if (strcmp(argv[i], "--shadow-map") == 0) {
            options->shadow_map_view = true;
        } else if (strcmp(argv[i], "--shadow-resolution") == 0 && i + 1 < argc) {
            if (!parse_count(argv[++i], &options->shadow_map_size)
                || (options->shadow_map_size != 512
                    && options->shadow_map_size != WATERFRONT_GPU_SHADOW_MAP_SIZE)) {
                fprintf(stderr, "Shadow resolution must be 512 or 1024: %s\n", argv[i]);
                return false;
            }
        } else if (strcmp(argv[i], "--time") == 0 && i + 1 < argc) {
            if (!parse_time(argv[++i], &options->time)) {
                fprintf(stderr, "Invalid scene time: %s\n", argv[i]);
                return false;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            return false;
        }
    }

    if (options->invalid_shader
        && (options->check || options->visible || options->screenshot_path || options->city)) {
        fprintf(stderr, "--invalid-shader cannot be combined with rendering options\n");
        return false;
    }
    if (options->shadow_map_view && !options->city) {
        fprintf(stderr, "--shadow-map requires --city\n");
        return false;
    }
    if (options->shadow_map_view && !options->shadows) {
        fprintf(stderr, "--shadow-map requires --shadows on\n");
        return false;
    }
    return true;
}

// Checks one tile's facade mesh, adding its triangles per material family to FAMILIES.
static bool verify_facade_mapping(const Mesh *mesh, int *families)
{
    if (mesh->vertexCount % 6 != 0)
        return false;
    for (int i = 0; i < mesh->vertexCount; i += 3) {
        int kind = mesh->colors[i * 4 + 3];
        if (kind < 1 || kind >= WATERFRONT_FACADE_COUNT)
            return false;
        ++families[kind];
        for (int j = i; j < i + 3; ++j) {
            if (mesh->colors[j * 4 + 3] != kind || !isfinite(mesh->texcoords[j * 2])
                || !isfinite(mesh->texcoords[j * 2 + 1]) || mesh->texcoords[j * 2] < -.001f
                || fabsf(mesh->texcoords[j * 2 + 1]
                       - (mesh->vertices[j * 3 + 1] - mesh->vertices[i * 3 + 1]))
                    > .001f) // Rows start at the wall's base corner, vertex zero.
                return false;
            for (int k = i; k < j; ++k) {
                float dx = mesh->vertices[j * 3] - mesh->vertices[k * 3];
                float dz = mesh->vertices[j * 3 + 2] - mesh->vertices[k * 3 + 2];
                if (fabsf(hypotf(dx, dz) - fabsf(mesh->texcoords[j * 2] - mesh->texcoords[k * 2]))
                    > .003f)
                    return false; // One UV metre must remain one metre on angled walls.
            }
        }
        if (i % 6 == 0) {
            // Shared corners of a,c,b / a,d,c must have identical UVs and palette.
            for (int axis = 0; axis < 2; ++axis)
                if (fabsf(mesh->texcoords[i * 2 + axis] - mesh->texcoords[(i + 3) * 2 + axis])
                        > .001f
                    || fabsf(mesh->texcoords[(i + 1) * 2 + axis]
                           - mesh->texcoords[(i + 5) * 2 + axis])
                        > .001f)
                    return false;
            if (memcmp(&mesh->colors[i * 4], &mesh->colors[(i + 3) * 4], 4) != 0)
                return false;
        }
    }
    return true;
}

static Mesh material_probe_plane(void)
{
    Mesh mesh = { .vertexCount = 6, .triangleCount = 2 };
    mesh.vertices = MemAlloc(18 * sizeof(float));
    mesh.normals = MemAlloc(18 * sizeof(float));
    mesh.texcoords = MemAlloc(12 * sizeof(float));
    mesh.colors = MemAlloc(24);
    if (!mesh.vertices || !mesh.normals || !mesh.texcoords || !mesh.colors) {
        UnloadMesh(mesh);
        return (Mesh) { 0 };
    }
    static const Vector3 corners[]
        = { { -256, 0, -256 }, { 256, 0, -256 }, { 256, 0, 256 }, { -256, 0, 256 } };
    static const int order[] = { 0, 2, 1, 0, 3, 2 };
    for (int i = 0; i < 6; ++i) {
        Vector3 p = corners[order[i]];
        memcpy(&mesh.vertices[i * 3], &p, sizeof(p));
        mesh.normals[i * 3] = mesh.normals[i * 3 + 2] = 0;
        mesh.normals[i * 3 + 1] = 1;
        mesh.texcoords[i * 2] = mesh.texcoords[i * 2 + 1] = 0;
    }
    memset(mesh.colors, 255, 24);
    UploadMesh(&mesh, true);
    return mesh;
}

static bool verify_surface_materials(WaterfrontGpuScene *scene, bool facade)
{
    int triangles[WATERFRONT_GROUND_COUNT] = { 0 };
    for (int t = 0; t < scene->quay.count; ++t) {
        const Mesh *quay = &scene->quay.meshes[t];
        for (int i = 0; i < quay->vertexCount; i += 3) {
            float encoded = quay->texcoords[i * 2];
            if (!isfinite(encoded) || encoded < 1 || encoded >= WATERFRONT_GROUND_COUNT
                || encoded != floorf(encoded))
                return false;
            ++triangles[(int)encoded];
            for (int v = i; v < i + 3; ++v)
                if (quay->texcoords[v * 2] != encoded || quay->normals[v * 3 + 1] < .95f)
                    return false;
        }
    }
    for (int i = 1; i < WATERFRONT_GROUND_COUNT; ++i)
        if (triangles[i] < 20)
            return false;
    // Each tile's facade mesh keeps its wall pairs together.
    if (facade) {
        int families[WATERFRONT_FACADE_COUNT] = { 0 };
        for (int t = 0; t < scene->facades.count; ++t)
            if (!verify_facade_mapping(&scene->facades.meshes[t], families))
                return false;
        for (int i = 1; i < WATERFRONT_FACADE_COUNT; ++i) {
            printf("facade_family=%d triangles=%d\n", i, families[i]);
            if (families[i] < 20)
                return false;
        }
    }
    int pass_location = GetShaderLocation(scene->shader, facade ? "facadePass" : "groundPass");
    if (pass_location < 0)
        return false;

    // Exercise the actual material shader with identical neutral base colours.
    // Near probes must retain detail; far probes must tolerate a tiny camera shift.
    RenderTexture2D target = LoadRenderTexture(64, 64);
    Mesh plane = material_probe_plane();
    bool ok = IsRenderTextureValid(target) && plane.vaoId != 0;
    if (!ok)
        goto cleanup;
    int zero = 0, one = 1;
    float none = 0, white[] = { 1, 1, 1 }, neutral[] = { .7f, .7f, .7f, 1 };
    const char *off[] = { "emissive", "shadowsEnabled", "atmosphereEnabled", "localLightCount" };
    for (size_t i = 0; i < sizeof(off) / sizeof(off[0]); ++i)
        SetShaderValue(
            scene->shader, GetShaderLocation(scene->shader, off[i]), &zero, SHADER_UNIFORM_INT);
    SetShaderValue(scene->shader, pass_location, &one, SHADER_UNIFORM_INT);
    SetShaderValue(scene->shader, GetShaderLocation(scene->shader, "ambientColor"), white,
        SHADER_UNIFORM_VEC3);
    SetShaderValue(scene->shader, GetShaderLocation(scene->shader, "directionalDirection"), white,
        SHADER_UNIFORM_VEC3);
    SetShaderValue(scene->shader, GetShaderLocation(scene->shader, "directionalIntensity"), &none,
        SHADER_UNIFORM_FLOAT);
    SetShaderValue(scene->shader, GetShaderLocation(scene->shader, "objectColor"), neutral,
        SHADER_UNIFORM_VEC4);
    float means[WATERFRONT_GROUND_COUNT] = { 0 };
    int kind_count = facade ? WATERFRONT_FACADE_COUNT : WATERFRONT_GROUND_COUNT;
    for (int kind = 1; kind < kind_count; ++kind) {
        for (int v = 0; v < plane.vertexCount; ++v) {
            plane.texcoords[v * 2] = facade ? plane.vertices[v * 3] : (float)kind;
            plane.texcoords[v * 2 + 1] = facade ? plane.vertices[v * 3 + 2] + 512 : 0;
            plane.colors[v * 4 + 3] = facade ? (unsigned char)kind : 255;
        }
        UpdateMeshBuffer(plane, 1, plane.texcoords, plane.vertexCount * 2 * (int)sizeof(float), 0);
        UpdateMeshBuffer(plane, 3, plane.colors, plane.vertexCount * 4, 0);
        unsigned char previous[64 * 64];
        int drift = 0, near_range = 0;
        for (int sample = 0; sample < 3; ++sample) {
            float offset = sample == 2 ? .02f : 0;
            Camera3D camera = { .position = { offset, 20, 0 },
                .target = { offset, 0, 0 },
                .up = { 0, 0, -1 },
                .fovy = sample == 0 ? 4 : 256,
                .projection = CAMERA_ORTHOGRAPHIC };
            BeginTextureMode(target);
            ClearBackground(BLACK);
            BeginMode3D(camera);
            DrawMesh(plane, scene->city_material, MatrixIdentity());
            EndMode3D();
            EndTextureMode();
            Image image = LoadImageFromTexture(target.texture);
            if (!image.data) {
                ok = false;
                break;
            }
            int low = 255, high = 0;
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) {
                    Color pixel = GetImageColor(image, x, y);
                    int value = pixel.r;
                    if (pixel.a != 255)
                        ok = false; // Facade alpha is metadata, never output transparency.
                    if (value < low)
                        low = value;
                    if (value > high)
                        high = value;
                    if (sample == 0)
                        means[kind] += value / 4096.0f;
                    if (sample == 1)
                        previous[y * 64 + x] = (unsigned char)value;
                    if (sample == 2) {
                        int delta = abs(value - previous[y * 64 + x]);
                        if (delta > drift)
                            drift = delta;
                    }
                }
            if (sample == 0)
                near_range = high - low;
            if (low < 100 || high > 230)
                ok = false;
            UnloadImage(image);
        }
        printf("%s_material=%d mean=%.2f near_range=%d far_shift_max=%d\n",
            facade ? "facade" : "ground", kind, means[kind], near_range, drift);
        if (near_range < 3 || drift > 2)
            ok = false;
    }
    if (!facade && means[WATERFRONT_GROUND_ASPHALT] + 3 >= means[WATERFRONT_GROUND_CONCRETE])
        ok = false;
    if (facade
        && (means[WATERFRONT_FACADE_BRICK] + 2 >= means[WATERFRONT_FACADE_PLASTER]
            || fabsf(means[WATERFRONT_FACADE_PANEL] - means[WATERFRONT_FACADE_PLASTER]) < .5f))
        ok = false;
    SetShaderValue(scene->shader, pass_location, &zero, SHADER_UNIFORM_INT);
cleanup:
    if (plane.vaoId != 0)
        UnloadMesh(plane);
    if (target.id != 0)
        UnloadRenderTexture(target);
    if (!ok)
        fprintf(stderr, "GPU %s material classification/filtering check failed\n",
            facade ? "facade" : "ground");
    return ok;
}

static bool verify_sky_targets(WaterfrontGpuScene *scene)
{
    const FixtureSize sizes[] = { { 320, 200 }, { 320, 240 }, { 1280, 800 }, { 1280, 960 } };
    // Look entirely above the city: every pixel must come from the same panorama as the CPU.
    // The large window deliberately differs from both native and supersampled render targets.
    int window_width = GetScreenWidth(), window_height = GetScreenHeight();
    SetWindowSize(960, 800);
    BeginDrawing();
    ClearBackground(BLACK);
    EndDrawing();
    bool ok = true;
    for (size_t size = 0; ok && size < sizeof(sizes) / sizeof(*sizes); ++size) {
        int width = sizes[size].width, height = sizes[size].height;
        RenderTexture2D target = LoadRenderTexture(width, height);
        Color *reference = malloc((size_t)width * height * sizeof(*reference));
        if (!IsRenderTextureValid(target) || !reference) {
            if (target.id)
                UnloadRenderTexture(target);
            free(reference);
            ok = false;
            break;
        }
        WaterfrontSceneFrame frame;
        waterfront_scene_evaluate(0, (BoatWater) { 0 }, &frame);
        frame.camera = (View3D) { .position = { 0, 2000, 0 },
            .target = { 80, 2100, -100 },
            .up = { 0, 1, 0 },
            .vertical_fov = 55 * DEG2RAD,
            .near_plane = .3f,
            .far_plane = 25 };
        // The plain panorama, then an A3.5 squall's cloud and rain ahead, high above the map.
        for (int squall = 0; ok && squall < 2; ++squall) {
            if (squall) {
                frame.camera.target = (Vector3) { 60, 2012, -75 };
                frame.squall = (WaterfrontSquall) { .centre = { 1300, -1600 },
                    .radius = 800,
                    .base = 1960,
                    .top = 2400,
                    .grown = .8f };
            }
            waterfront_sky_draw_squall(reference, width, height, frame.camera, frame.squall);
            if (squall) {
                // The case must show the squall: compare with the plain panorama.
                Color *plain = malloc((size_t)width * height * sizeof(*plain));
                int changed = 0;
                if (plain) {
                    waterfront_sky_draw(plain, width, height, frame.camera);
                    for (int i = 0; i < width * height; ++i)
                        changed += abs(plain[i].r - reference[i].r) > 12;
                    free(plain);
                }
                if (changed < width * height / 5) {
                    fprintf(stderr, "Sky squall case shows too little cloud: %d of %d\n", changed,
                        width * height);
                    ok = false;
                    break;
                }
            }
            for (int passes = 0; ok && passes < 4; ++passes) {
                waterfront_gpu_scene_set_shadows_enabled(scene, (passes & 1) != 0);
                waterfront_gpu_scene_set_reflections_enabled(scene, (passes & 2) != 0);
                // A second frame catches state inherited from the preceding presentation.
                for (int tick = 0; ok && tick < 2; ++tick) {
                    BeginTextureMode(target);
                    waterfront_gpu_scene_draw(scene, &frame, width, height);
                    // A 2D overlay must also retain the caller's projection after the scene draw.
                    DrawRectangle(width - 8, height - 8, 8, 8, MAGENTA);
                    EndTextureMode();
                    Image image = LoadImageFromTexture(target.texture);
                    if (!image.data) {
                        ok = false;
                        break;
                    }
                    ImageFlipVertical(&image);
                    Color marker = GetImageColor(image, width - 4, height - 4);
                    if (marker.r != 255 || marker.g != 0 || marker.b != 255)
                        ok = false;
                    ImageFlipHorizontal(&image);
                    int probes = 0, mismatches = 0;
                    for (int y = 8; y < height - 8; y += 7)
                        for (int x = 8; x < width - 8; x += 7) {
                            Color expected = reference[y * width + x];
                            Color actual = GetImageColor(image, x, y);
                            // Allow a few panorama-texel boundary differences from float precision.
                            if (abs(actual.r - expected.r) > 3 || abs(actual.g - expected.g) > 3
                                || abs(actual.b - expected.b) > 3 || actual.a != 255)
                                ++mismatches;
                            ++probes;
                        }
                    if (mismatches > probes / 100)
                        ok = false;
                    if (!ok)
                        fprintf(stderr,
                            "Sky target check failed: %dx%d squall=%d passes=%d frame=%d "
                            "mismatches=%d/%d overlay=%u,%u,%u\n",
                            width, height, squall, passes, tick, mismatches, probes, marker.r,
                            marker.g, marker.b);
                    UnloadImage(image);
                    BeginDrawing();
                    ClearBackground(BLACK);
                    EndDrawing();
                }
            }
        }
        free(reference);
        UnloadRenderTexture(target);
    }
    SetWindowSize(window_width, window_height);
    printf("sky_targets=%s\n", ok ? "ok" : "failed");
    return ok;
}

static bool draw_city(RenderTexture2D target, WaterfrontGpuScene *scene, FixtureOptions options)
{
    char shader_path[1024];
    snprintf(shader_path, sizeof(shader_path), "%s/shaders/waterfront_city.fs", PROTEUS_ASSET_DIR);
    if (!waterfront_gpu_scene_init(scene, shader_path)) {
        fprintf(stderr, "GPU fixture: waterfront city resources failed: %s\n", shader_path);
        return false;
    }
    // The scene keeps only its uploaded meshes; the scenery is needed for the bind alone.
    WaterfrontScenery scenery = { 0 };
    bool bound = waterfront_scenery_init(&scenery, &waterfront_city_river_map)
        && waterfront_gpu_scene_bind(scene, &scenery);
    waterfront_scenery_destroy(&scenery);
    if (!bound) {
        fprintf(stderr, "GPU fixture: river scenery could not be bound\n");
        return false;
    }
    if (options.check
        && (!verify_surface_materials(scene, false) || !verify_surface_materials(scene, true)
            || !verify_sky_targets(scene)))
        return false;
    waterfront_gpu_scene_set_lighting_enabled(scene, options.lighting);
    waterfront_gpu_scene_set_shadows_enabled(scene, options.shadows);
    waterfront_gpu_scene_set_atmosphere_enabled(scene, options.atmosphere);
    waterfront_gpu_scene_set_reflections_enabled(scene, options.reflections);
    if (!waterfront_gpu_scene_set_shadow_map_size(scene, options.shadow_map_size)) {
        fprintf(stderr, "GPU fixture: unable to select %d shadow map\n", options.shadow_map_size);
        waterfront_gpu_scene_destroy(scene);
        return false;
    }
    WaterfrontSceneFrame frame;
    waterfront_scene_evaluate(options.time, (BoatWater) { 0 }, &frame);
    // Over the dock towards the crane, the old storyboard's opening view.
    frame.camera = (View3D) { .position = { -85, 17.5f, 170 },
        .target = { -78, 5, 100 },
        .up = { 0, 1, 0 },
        .vertical_fov = 62 * DEG2RAD,
        .near_plane = .25f,
        .far_plane = 1400 };
    BeginTextureMode(target);
    waterfront_gpu_scene_draw(scene, &frame, target.texture.width, target.texture.height);
    EndTextureMode();
    if (options.shadow_map_view) {
        Texture2D shadow_map = waterfront_gpu_scene_shadow_map(scene);
        BeginTextureMode(target);
        ClearBackground(BLACK);
        DrawTexturePro(shadow_map,
            (Rectangle) { 0, 0, (float)shadow_map.width, (float)-shadow_map.height },
            (Rectangle) { 0, 0, (float)target.texture.width, (float)target.texture.height },
            (Vector2) { 0 }, 0, WHITE);
        EndTextureMode();
    }
    return true;
}

static bool color_matches(Color actual, Color expected)
{
    return actual.r == expected.r && actual.g == expected.g && actual.b == expected.b
        && actual.a == expected.a;
}

static void print_context_details(FixtureSize size)
{
    const GLubyte *vendor = glGetString(GL_VENDOR);
    const GLubyte *renderer = glGetString(GL_RENDERER);
    const GLubyte *version = glGetString(GL_VERSION);
    const GLubyte *glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);
    printf("opengl_vendor=%s\n", vendor != NULL ? (const char *)vendor : "unavailable");
    printf("opengl_renderer=%s\n", renderer != NULL ? (const char *)renderer : "unavailable");
    printf("opengl_version=%s\n", version != NULL ? (const char *)version : "unavailable");
    printf("glsl_version=%s\n", glsl != NULL ? (const char *)glsl : "unavailable");
    printf("render_target=%dx%d\n", size.width, size.height);
}

static bool replace_target(RenderTexture2D *target, FixtureSize size)
{
    RenderTexture2D replacement = LoadRenderTexture(size.width, size.height);
    if (!IsRenderTextureValid(replacement)) {
        fprintf(stderr, "GPU fixture: incomplete %dx%d framebuffer\n", size.width, size.height);
        if (replacement.id != 0)
            UnloadRenderTexture(replacement);
        return false;
    }

    SetTextureFilter(replacement.texture, TEXTURE_FILTER_POINT);
    if (target->id != 0)
        UnloadRenderTexture(*target);
    *target = replacement;
    return true;
}

static void draw_fixture(RenderTexture2D target, Shader shader)
{
    Camera3D camera = {
        .position = { 0.0f, 0.0f, 8.0f },
        .target = { 0.0f, 0.0f, 0.0f },
        .up = { 0.0f, 1.0f, 0.0f },
        .fovy = 2.5f,
        .projection = CAMERA_ORTHOGRAPHIC,
    };

    BeginTextureMode(target);
    ClearBackground((Color) { 20, 28, 45, 255 });
    BeginMode3D(camera);
    BeginShaderMode(shader);

    // Draw the nearer blue cube first. The later red cube intersects it but is farther away.
    DrawCubeV((Vector3) { 0.20f, 0.0f, 0.35f }, (Vector3) { 1.5f, 1.5f, 1.5f }, BLUE);
    DrawCubeV((Vector3) { -0.20f, 0.0f, 0.0f }, (Vector3) { 1.5f, 1.5f, 1.5f }, RED);

    EndShaderMode();
    EndMode3D();

    // Four unequal corner colors make vertical flips and horizontal mirroring unambiguous.
    DrawRectangle(0, 0, 8, 8, RED);
    DrawRectangle(target.texture.width - 8, 0, 8, 8, GREEN);
    DrawRectangle(0, target.texture.height - 8, 8, 8, BLUE);
    DrawRectangle(target.texture.width - 8, target.texture.height - 8, 8, 8, YELLOW);
    EndTextureMode();
}

static bool normalize_and_check_image(
    Image *image, FixtureSize size, bool check, bool flip_horizontal)
{
    if (image->data == NULL) {
        fprintf(stderr, "GPU fixture: render-target readback failed\n");
        return false;
    }
    if (image->width != size.width || image->height != size.height) {
        fprintf(stderr, "GPU fixture: readback dimensions %dx%d, expected %dx%d\n", image->width,
            image->height, size.width, size.height);
        return false;
    }

    ImageFlipVertical(image);
    if (flip_horizontal)
        ImageFlipHorizontal(image);
    if (!check)
        return true;

    Color top_left = GetImageColor(*image, 2, 2);
    Color top_right = GetImageColor(*image, size.width - 3, 2);
    Color bottom_left = GetImageColor(*image, 2, size.height - 3);
    Color bottom_right = GetImageColor(*image, size.width - 3, size.height - 3);
    Color center = GetImageColor(*image, size.width / 2, size.height / 2);
    if (!color_matches(top_left, RED) || !color_matches(top_right, GREEN)
        || !color_matches(bottom_left, BLUE) || !color_matches(bottom_right, YELLOW)) {
        fprintf(stderr, "GPU fixture: orientation marker check failed\n");
        return false;
    }
    if (!color_matches(center, BLUE)) {
        fprintf(stderr, "GPU fixture: depth probe is %u,%u,%u,%u, expected blue\n", center.r,
            center.g, center.b, center.a);
        return false;
    }
    return true;
}

static bool verify_shader_rejection(void)
{
    static const char INVALID_FRAGMENT_SHADER[]
        = "#version 330\n"
          "out vec4 finalColor;\n"
          "void main() { finalColor = vec4(this is invalid); }\n";
    Shader shader = LoadShaderFromMemory(NULL, INVALID_FRAGMENT_SHADER);
    bool rejected = shader.id == 0 || shader.id == rlGetShaderIdDefault();
    if (shader.id != 0 && shader.id != rlGetShaderIdDefault())
        UnloadShader(shader);
    if (!rejected)
        fprintf(stderr, "GPU fixture: invalid shader unexpectedly linked\n");
    return rejected;
}

static bool verify_winding_and_normals(FixtureSize size)
{
    static const char NORMAL_VERTEX_SHADER[]
        = "#version 330\n"
          "in vec3 vertexPosition;\n"
          "in vec3 vertexNormal;\n"
          "uniform mat4 mvp;\n"
          "out vec3 normal;\n"
          "void main() { normal = vertexNormal; gl_Position = mvp * vec4(vertexPosition, 1.0); }\n";
    static const char NORMAL_FRAGMENT_SHADER[]
        = "#version 330\n"
          "in vec3 normal;\n"
          "out vec4 finalColor;\n"
          "void main() { finalColor = vec4(normal * .5 + .5, 1.0); }\n";
    Camera3D camera = {
        .position = { 0.0f, 0.0f, 8.0f },
        .target = { 0.0f, 0.0f, 0.0f },
        .up = { 0.0f, 1.0f, 0.0f },
        .fovy = 4.0f,
        .projection = CAMERA_ORTHOGRAPHIC,
    };
    RenderTexture2D target = LoadRenderTexture(size.width, size.height);
    if (!IsRenderTextureValid(target)) {
        fprintf(stderr, "GPU fixture: winding/normal framebuffer is incomplete\n");
        if (target.id != 0)
            UnloadRenderTexture(target);
        return false;
    }
    float *vertices = malloc(9 * sizeof(*vertices));
    float *normals = malloc(9 * sizeof(*normals));
    if (vertices == NULL || normals == NULL) {
        free(normals);
        free(vertices);
        UnloadRenderTexture(target);
        return false;
    }
    float positions[] = { -.3f, -.8f, 0, .3f, -.8f, 0, 0, .2f, 0 };
    float normal_data[] = { 0, 1, 0, 0, 1, 0, 0, 1, 0 };
    memcpy(vertices, positions, sizeof(positions));
    memcpy(normals, normal_data, sizeof(normal_data));
    Mesh normal_mesh
        = { .vertexCount = 3, .triangleCount = 1, .vertices = vertices, .normals = normals };
    UploadMesh(&normal_mesh, false);
    Shader normal_shader = LoadShaderFromMemory(NORMAL_VERTEX_SHADER, NORMAL_FRAGMENT_SHADER);
    if (normal_mesh.vaoId == 0 || !IsShaderValid(normal_shader)
        || normal_shader.id == rlGetShaderIdDefault()) {
        fprintf(stderr, "GPU fixture: normal submission shader failed\n");
        if (normal_mesh.vaoId != 0)
            UnloadMesh(normal_mesh);
        if (normal_shader.id != 0 && normal_shader.id != rlGetShaderIdDefault())
            UnloadShader(normal_shader);
        UnloadRenderTexture(target);
        return false;
    }
    Material material = LoadMaterialDefault();
    material.shader = normal_shader;

    BeginTextureMode(target);
    ClearBackground(BLACK);
    BeginMode3D(camera);
    rlEnableBackfaceCulling();
    DrawTriangle3D((Vector3) { -1.8f, -1.0f, 0 }, (Vector3) { -.6f, -1.0f, 0 },
        (Vector3) { -1.2f, 1.0f, 0 }, GREEN);
    // raylib batches immediate triangles, so flush while culling is enabled before changing it.
    rlDrawRenderBatchActive();
    DrawTriangle3D((Vector3) { .6f, -1.0f, 0 }, (Vector3) { 1.2f, 1.0f, 0 },
        (Vector3) { 1.8f, -1.0f, 0 }, RED);
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    DrawMesh(normal_mesh, material, MatrixIdentity());
    EndMode3D();
    EndTextureMode();

    Image image = LoadImageFromTexture(target.texture);
    bool valid = image.data != NULL;
    if (valid)
        ImageFlipVertical(&image);
    Color front = valid ? GetImageColor(image, size.width / 3, size.height / 2) : BLACK;
    Color back = valid ? GetImageColor(image, size.width * 2 / 3, size.height / 2) : BLACK;
    Color normal = valid ? GetImageColor(image, size.width / 2, size.height / 2) : BLACK;
    if (!valid || !color_matches(front, GREEN) || !color_matches(back, BLACK) || normal.g < 220
        || normal.r < 100 || normal.r > 155 || normal.b < 100 || normal.b > 155) {
        fprintf(stderr,
            "GPU fixture: winding/normal check failed (front=%u,%u,%u back=%u,%u,%u "
            "normal=%u,%u,%u)\n",
            front.r, front.g, front.b, back.r, back.g, back.b, normal.r, normal.g, normal.b);
        valid = false;
    }
    UnloadImage(image);
    material.shader.id = rlGetShaderIdDefault();
    UnloadMaterial(material);
    UnloadShader(normal_shader);
    UnloadMesh(normal_mesh);
    UnloadRenderTexture(target);
    return valid;
}

static void present_target(RenderTexture2D target, FixtureOptions options)
{
    Rectangle source = { options.city ? (float)target.texture.width : 0, 0,
        options.city ? (float)-target.texture.width : (float)target.texture.width,
        (float)-target.texture.height };
    Rectangle destination = { 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() };
    for (int frame = 0; frame < options.frames && !WindowShouldClose(); ++frame) {
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(target.texture, source, destination, (Vector2) { 0 }, 0.0f, WHITE);
        EndDrawing();
    }
}

int main(int argc, char **argv)
{
    test_scenery_load();
    FixtureOptions options;
    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return 2;
    }

    SetConfigFlags(options.visible ? 0 : FLAG_WINDOW_HIDDEN);
    InitWindow(options.size.width, options.size.height, "Proteus GPU fixture");
    if (!IsWindowReady()) {
        fprintf(stderr, "GPU fixture: unable to create a raylib window/context\n");
        return 1;
    }
    SetTraceLogLevel(LOG_WARNING);
    print_context_details(options.size);

    int result = 1;
    WaterfrontGpuScene city = { 0 };
    RenderTexture2D target = { 0 };
    if (options.invalid_shader) {
        result = verify_shader_rejection() ? 1 : 2;
        CloseWindow();
        return result;
    }

    Shader shader = { 0 };
    if (!options.city) {
        char shader_path[1024];
        snprintf(shader_path, sizeof(shader_path), "%s/shaders/gpu_fixture.fs", PROTEUS_ASSET_DIR);
        shader = LoadShader(NULL, shader_path);
        if (!IsShaderValid(shader) || shader.id == rlGetShaderIdDefault()) {
            fprintf(stderr, "GPU fixture: custom fragment shader failed: %s\n", shader_path);
            if (shader.id != 0 && shader.id != rlGetShaderIdDefault())
                UnloadShader(shader);
            CloseWindow();
            return 1;
        }
    }
    if (options.check && !verify_shader_rejection())
        goto cleanup;

    FixtureSize intermediate = { options.size.width / 2, options.size.height / 2 };
    if (!replace_target(&target, intermediate) || !replace_target(&target, options.size))
        goto cleanup;

    if (options.city) {
        if (!draw_city(target, &city, options))
            goto cleanup;
    } else {
        draw_fixture(target, shader);
    }
    Image image = LoadImageFromTexture(target.texture);
    if (!normalize_and_check_image(&image, options.size, options.check && !options.city,
            options.city && !options.shadow_map_view)) {
        UnloadImage(image);
        goto cleanup;
    }
    if (options.check && !options.city && !verify_winding_and_normals(options.size)) {
        UnloadImage(image);
        goto cleanup;
    }
    if (options.screenshot_path != NULL && !ExportImage(image, options.screenshot_path)) {
        fprintf(stderr, "GPU fixture: unable to export screenshot: %s\n", options.screenshot_path);
        UnloadImage(image);
        goto cleanup;
    }
    UnloadImage(image);

    if (options.visible)
        present_target(target, options);
    printf("gpu_fixture=ok\n");
    if (options.screenshot_path != NULL)
        printf("screenshot=%s\n", options.screenshot_path);
    result = 0;

cleanup:
    waterfront_gpu_scene_destroy(&city);
    if (target.id != 0)
        UnloadRenderTexture(target);
    if (shader.id != 0)
        UnloadShader(shader);
    CloseWindow();
    return result;
}
