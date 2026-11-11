// Fixed shelter-hunt presentation fixture (P5d): the CPU scene with the room's marks as rings of
// buoys, the objective panel and the minimap marks. Offline capture only; never joins a room.
#include "core/boat_location.h"
#include "render/boat_minimap.h"
#include "render/boat_ui.h"
#include "render/boat_view.h"
#include "support/boat_test_scenery.h"
#include "wake_velocity/wake_velocity_net.h"
#include <stdio.h>
#include <string.h>

#define WIDTH 480
#define HEIGHT 300

int main(int argc, char **argv)
{
    const char *path = "build/hunt.png";
    if (argc == 3 && !strcmp(argv[1], "--screenshot"))
        path = argv[2];
    else if (argc != 1)
        return 2;
    test_scenery_load();
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(WIDTH * 2, HEIGHT * 2, "Shelter hunt presentation fixture");
    const BoatWorldDefinition *outer = boat_location(BOAT_LOCATION_OUTER);
    BoatWorldResources resources;
    if (!boat_world_resources_init(&resources, outer)) {
        fprintf(stderr, "boat_hunt_review: cannot load the outer map\n");
        return 1;
    }
    static WakeRoom net;
    BoatMarkSet *marks = &net.client.room.marks;
    net.client.room_valid = true;
    *marks = (BoatMarkSet) { .count = 2,
        .items = { { "upper_bend", "UPPER BEND", -350, 0, 25 },
            { "north_reach", "NORTH REACH", -500, 300, 25 } } };
    net.client.objective_valid = true;
    net.client.objective = (BoatNetObjectiveState) { .revision = 3,
        .row_count = 3,
        .rows = { { "hunt", "HUNT", 1 }, { "crew_1", "CREW 1", 1 }, { "crew_2", "CREW 2", 0 } } };
    Boat boat;
    boat_reset(&boat);
    boat.position = (Vector2) { -350, 40 };
    boat.heading = 0; // Forward is (sin, -cos): towards the mark.
    BoatWorld world;
    boat_world_init(&world, &resources, (BoatWater) { 0 });
    BoatPresentation presentation = { 0 };
    boat_presentation_step(&presentation, &boat, &world, 3);
    BoatViewState view = { .boat = boat, .time = 3 };
    wake_room_marks_view(&net, &view);
    Renderer3D cpu = { 0 };
    BoatCpuView cpu_view = { .hour = 14, .hour_set = true };
    static Color pixels[WIDTH * HEIGHT];
    if (!boat_draw_scene(&cpu, &cpu_view, pixels, WIDTH, HEIGHT, &view, &presentation, &world,
            (BoatCamera) { .distance = 26, .height = 20 }, NULL, NULL, NULL, 0)) {
        fprintf(stderr, "boat_hunt_review: the scene did not draw\n");
        return 1;
    }
    Texture2D scene = LoadTextureFromImage(
        (Image) { pixels, WIDTH, HEIGHT, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 });
    BoatMinimap minimap = { 0 };
    if (!boat_minimap_init(&minimap, outer)) {
        fprintf(stderr, "boat_hunt_review: the outer chart did not bake\n");
        return 1;
    }
    minimap.room_marks = view.marks;
    RenderTexture2D target = LoadRenderTexture(WIDTH, HEIGHT);
    BeginTextureMode(target);
    DrawTexture(scene, 0, 0, WHITE);
    // The corner chart as the game places it, then the objective panel under the roster's place.
    const Image *chart
        = boat_minimap_compose(&minimap, BOAT_MINIMAP_CORNER, NULL, boat.position, boat.heading);
    Rectangle bounds = boat_minimap_bounds(&minimap, BOAT_MINIMAP_CORNER, WIDTH, HEIGHT);
    Texture2D chart_texture = LoadTextureFromImage(*chart);
    DrawTexture(chart_texture, (int)bounds.x, (int)bounds.y, WHITE);
    wake_room_draw_objective(&net, WIDTH - 12, (int)(bounds.y + bounds.height) + 6);
    // The labelled chart, for judging the marks' circles and names.
    chart = boat_minimap_compose(&minimap, BOAT_MINIMAP_CHART, NULL, boat.position, boat.heading);
    Rectangle large = boat_minimap_bounds(&minimap, BOAT_MINIMAP_CHART, WIDTH, HEIGHT);
    Texture2D large_texture = LoadTextureFromImage(*chart);
    DrawTexture(large_texture, 8, (int)large.y > 0 ? (int)large.y : 8, WHITE);
    EndTextureMode();
    Image out = LoadImageFromTexture(target.texture);
    ImageFlipVertical(&out);
    ImageResizeNN(&out, WIDTH * 2, HEIGHT * 2); // Nearest-neighbour, so each pixel stays a block.
    bool ok = ExportImage(out, path);
    UnloadImage(out);
    UnloadTexture(chart_texture);
    UnloadTexture(large_texture);
    UnloadTexture(scene);
    UnloadRenderTexture(target);
    boat_minimap_destroy(&minimap);
    boat_cpu_view_destroy(&cpu_view);
    renderer3d_destroy(&cpu);
    boat_world_resources_destroy(&resources);
    CloseWindow();
    return ok ? 0 : 1;
}
