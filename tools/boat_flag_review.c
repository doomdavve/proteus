#include "boat_gpu.h"
#include "boat_test_session.h"
#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void advance(BoatSession *session, const char *scenario, int tick)
{
    Boat *boat = &session->boat;
    float t = tick * BOAT_STEP;
    if (strcmp(scenario, "idle") != 0)
        boat->throttle = strcmp(scenario, "reverse") == 0 ? -.65f : 1;
    float steering = strcmp(scenario, "turn") == 0 && t > 3 ? .7f : 0;
    boat_session_step(session,
        (BoatInput) { .steering_rate = Clamp((steering - boat->wheel) / BOAT_STEP, -1, 1) });
}

static View3D camera_for(const Boat *boat, const char *angle)
{
    Vector3 target = { -boat->position.x, boat->height + 1.15f, boat->position.y + .4f };
    Vector3 eye = strcmp(angle, "front") == 0 ? (Vector3) { 4.5f, 2.5f, -5.5f }
        : strcmp(angle, "side") == 0          ? (Vector3) { 6.5f, 1.3f, 0 }
                                              : (Vector3) { 4.5f, 5.5f, 4.5f };
    Vector3 offset = Vector3Transform(eye, MatrixRotateY(boat->heading));
    return (View3D) { .position = Vector3Add(target, offset),
        .target = target,
        .up = { 0, 1, 0 },
        .vertical_fov = 45 * DEG2RAD,
        .near_plane = .1f,
        .far_plane = 1500 };
}

int main(int argc, char **argv)
{
    const char *scenario = "idle", *screenshot = NULL;
    bool gpu = false;
    unsigned int skin = BOAT_SKIN_SWEDEN;
    const char *angle = "rear";
    int frames = 0, width = 960, height = 600;
    float time = 5;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc)
            scenario = argv[++i];
        else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            screenshot = argv[++i];
        else if (strcmp(argv[i], "--skin") == 0 && i + 1 < argc) {
            if (!boat_skin_parse(argv[++i], &skin))
                return 2;
        } else if (strcmp(argv[i], "--angle") == 0 && i + 1 < argc)
            angle = argv[++i];
        else if (strcmp(argv[i], "--time") == 0 && i + 1 < argc)
            time = strtof(argv[++i], NULL);
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--renderer") == 0 && i + 1 < argc) {
            const char *renderer = argv[++i];
            if (strcmp(renderer, "cpu") && strcmp(renderer, "gpu"))
                return 2;
            gpu = strcmp(renderer, "gpu") == 0;
        } else if (strcmp(argv[i], "--low-resolution") == 0) {
            width = 320;
            height = 200;
        } else {
            fprintf(stderr,
                "Usage: boat_flag_review [--scenario idle|cruise|turn|reverse] "
                "[--renderer cpu|gpu] [--time 0..60] [--screenshot FILE] "
                "[--frames COUNT] [--low-resolution] "
                "[--skin sweden|finland|denmark|norway|explorer] [--angle front|side|rear]\n");
            return 2;
        }
    }
    if ((strcmp(angle, "front") && strcmp(angle, "side") && strcmp(angle, "rear"))
        || (!screenshot && frames <= 0) || !isfinite(time) || time < 0 || time > 60
        || (strcmp(scenario, "idle") && strcmp(scenario, "cruise") && strcmp(scenario, "turn")
            && strcmp(scenario, "reverse")))
        return 2;
    SetTraceLogLevel(LOG_WARNING);
    BoatSession boat;
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    for (int tick = 0; tick < (int)roundf(time / BOAT_STEP); ++tick)
        advance(&boat, scenario, tick);
    Renderer3D cpu = { 0 };
    Color *pixels = calloc((size_t)width * height, sizeof(*pixels));
    if (!pixels)
        return 1;
    BoatGpu graphics = { 0 };
    RenderTexture2D target = { 0 };
    if (gpu || !screenshot) {
        SetConfigFlags(screenshot ? FLAG_WINDOW_HIDDEN : 0);
        InitWindow(width, height, "Nordic speedboats: ensign / apparent wind review");
        if (!IsWindowReady())
            return 1;
        SetTargetFPS(60);
        if (gpu) {
            if (!boat_gpu_init(&graphics, false))
                return 1;
            target = LoadRenderTexture(width, height);
            graphics.basin_water = GenMeshPlane(2 * BOAT_BASIN_HALF_X, 2 * BOAT_BASIN_HALF_Z, 1, 1);
        }
    }
    int count = screenshot ? 1 : frames;
    for (int f = 0; f < count && (screenshot || !WindowShouldClose()); ++f) {
        WaterfrontPlayerState player = { 0 };
        BoatViewState view = boat_session_view(&boat);
        view.skin = skin;
        boat_player_pose(&view, &boat.presentation.flag, &player);
        View3D camera = camera_for(&boat.boat, angle);
        if (gpu) {
            WaterfrontSceneFrame frame;
            boat_scene_frame(&view, &boat.presentation, &boat.world, BOAT_CAMERA_DEFAULT, &frame);
            frame.camera = camera;
            frame.scenery_count = 0;
            frame.gull_count = 0;
            Mesh original_water = graphics.scene.water;
            graphics.scene.water = graphics.basin_water;
            BeginTextureMode(target);
            waterfront_gpu_scene_draw(&graphics.scene, &frame, width, height);
            EndTextureMode();
            graphics.scene.water = original_water;
        } else {
            for (int i = 0; i < width * height; ++i)
                pixels[i] = (Color) { 56, 78, 91, 255 };
            if (!renderer3d_begin(&cpu, pixels, width, height, camera, (Vector3) { -1, 3, 2 }))
                return 1;
            renderer3d_draw_mesh(&cpu, boat_skin_mesh(skin, BOAT_SKIN_HULL), player.hull);
            renderer3d_draw_mesh(&cpu, boat_skin_mesh(skin, BOAT_SKIN_COCKPIT), player.cabin);
            for (int side = -1; side <= 1; side += 2)
                renderer3d_draw_mesh(&cpu, boat_skin_mesh(skin, BOAT_SKIN_ENGINE),
                    boat_skin_outboard_pose(player.hull, player.engine, side));
            BoatFlagMesh flag_storage;
            Mesh3D flag = boat_flag_skin_mesh(&player.flag, skin, &flag_storage);
            renderer3d_draw_mesh(&cpu, &flag, (Transform3D) { .scale = { 1, 1, 1 } });
        }
        if (screenshot) {
            Image image = gpu
                ? LoadImageFromTexture(target.texture)
                : (Image) { pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
            if (gpu) {
                ImageFlipVertical(&image);
                ImageFlipHorizontal(&image); // Match the CPU camera's mirrored basis.
            }
            bool saved = ExportImage(image, screenshot);
            if (gpu)
                UnloadImage(image);
            if (!saved)
                return 1;
        } else {
            Texture2D texture = gpu ? target.texture
                                    : LoadTextureFromImage((Image) { pixels, width, height, 1,
                                          PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 });
            BeginDrawing();
            ClearBackground(BLACK);
            DrawTexturePro(texture,
                (Rectangle) { 0, 0, gpu ? -width : width, gpu ? -height : height },
                (Rectangle) { 0, 0, width, height }, (Vector2) { 0 }, 0, WHITE);
            EndDrawing();
            if (!gpu)
                UnloadTexture(texture);
            for (int step = 0; step < 2; ++step)
                advance(&boat, scenario, (int)roundf(boat_session_time(&boat) / BOAT_STEP));
        }
    }
    if (gpu) {
        UnloadRenderTexture(target);
        boat_gpu_destroy(&graphics);
    }
    if (gpu || !screenshot)
        CloseWindow();
    renderer3d_destroy(&cpu);
    free(pixels);
    test_worlds_close();
    return 0;
}
