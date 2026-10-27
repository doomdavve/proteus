// Regenerate the title's fixed river view without a player boat or player wake.
#include "boat_gpu.h"
#include "boat_world.h"
#include "raymath.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: title_backdrop OUTPUT.png (requires desktop graphics)\n");
        return 2;
    }
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "Wake Velocity / title backdrop");
    BoatGpu gpu = { 0 };
    BoatWorldResources resources = { 0 };
    Image image = { 0 };
    int result = 1;
    if (!IsWindowReady()
        || !boat_world_resources_init(&resources, boat_location(BOAT_LOCATION_RIVER))
        || !boat_gpu_init(&gpu, true)
        || !waterfront_scenery_bind(&gpu.scenery, resources.definition->map)
        || !waterfront_gpu_scene_bind(&gpu.scene, &gpu.scenery))
        goto cleanup;
    BoatWorld world;
    boat_world_init(&world, &resources, boat_water_gentle());
    const BoatViewState view
        = { .boat = { .position = { 641.115f, -842.196f }, .heading = .850f }, .time = 73.350 };
    const BoatPresentation presentation = { .camera_heading = .850f + PI };
    static WaterfrontSceneFrame frame;
    boat_scene_frame(&view, &presentation, &world,
        (BoatCamera) { .distance = 32, .height = 4, .fixed_fov = true }, &frame);
    // Match the original moving capture's fully widened lens without simulating a boat.
    frame.camera.vertical_fov = 62 * DEG2RAD;
    // The camera is already evaluated. Draw ordinary scenery/water with no player layer.
    frame.player_active = false;
    RenderTexture2D target = gpu.supersampled;
    BeginTextureMode(target);
    waterfront_gpu_scene_draw(&gpu.scene, &frame, target.texture.width, target.texture.height);
    EndTextureMode();
    BeginTextureMode(gpu.target);
    ClearBackground(BLACK);
    BeginShaderMode(gpu.downsample);
    DrawTexturePro(target.texture, (Rectangle) { 0, 0, 1280, -800 }, (Rectangle) { 0, 0, 320, 200 },
        (Vector2) { 0 }, 0, WHITE);
    EndShaderMode();
    EndTextureMode();
    image = LoadImageFromTexture(gpu.target.texture);
    if (!IsImageValid(image))
        goto cleanup;
    ImageFlipVertical(&image);
    ImageFlipHorizontal(&image);
    result = ExportImage(image, argv[1]) ? 0 : 1;
cleanup:
    if (IsImageValid(image))
        UnloadImage(image);
    boat_gpu_destroy(&gpu);
    boat_world_resources_destroy(&resources);
    CloseWindow();
    return result;
}
