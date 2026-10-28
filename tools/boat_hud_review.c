#include "boat_hud.h"
#include "boat_voyage_view.h"
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *background = NULL, *screenshot = NULL;
    int width = 960, height = 600, frames = 1;
    BoatHudTelemetry telemetry = { .speed_mps = 12.6f, .thrust = .50f, .steering = .35f };
    // Optional outer-voyage cue (A3.4), drawn in window pixels as wake_velocity draws it.
    BoatVoyage voyage = { 0 };
    Boat boat = { 0 };
    double voyage_time = 100;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--background") && i + 1 < argc)
            background = argv[++i];
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc)
            screenshot = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--compact")) {
            width = 320;
            height = 200;
        } else if (!strcmp(argv[i], "--large")) {
            width = 1280;
            height = 800;
        } else if (!strcmp(argv[i], "--case") && i + 1 < argc) {
            const char *name = argv[++i];
            if (!strcmp(name, "neutral"))
                telemetry = (BoatHudTelemetry) { 0 };
            else if (!strcmp(name, "reverse"))
                telemetry
                    = (BoatHudTelemetry) { .speed_mps = 3.2f, .thrust = -.20f, .steering = -1 };
            else if (!strcmp(name, "docking"))
                telemetry
                    = (BoatHudTelemetry) { .speed_mps = .7f, .thrust = .05f, .precision = true };
            else if (!strcmp(name, "reverse-docking"))
                telemetry
                    = (BoatHudTelemetry) { .speed_mps = .7f, .thrust = -.05f, .precision = true };
            else if (!strcmp(name, "reset"))
                telemetry = (BoatHudTelemetry) { .reset_progress = .6f, .precision = true };
            else if (!strcmp(name, "full"))
                telemetry = (BoatHudTelemetry) { .speed_mps = 34, .thrust = 1, .steering = 1 };
            else if (strcmp(name, "cruise")) {
                fprintf(stderr, "Unknown HUD case: %s\n", name);
                return 1;
            }
        } else if (!strcmp(argv[i], "--voyage") && i + 1 < argc) {
            const char *name = argv[++i];
            const BoatVoyageRoute *route = &boat_voyage_outer;
            boat_voyage_start(&voyage, route, 0);
            voyage.shelter = 2;
            boat.position = route->shelters[2].position;
            boat.heading = route->shelters[2].heading;
            if (!strcmp(name, "exposed")) {
                boat.position = (Vector2) { 3900, -2600 };
                boat.heading = 3.4f;
                voyage.exposed = true;
                voyage.event = BOAT_VOYAGE_EXPOSED;
                voyage.event_time = voyage_time - 1;
            } else if (!strcmp(name, "slow")) {
                boat.position = (Vector2) { 3640, -3370 };
                boat.heading = -1.57f;
                voyage.too_fast = true;
            } else if (!strcmp(name, "arrived")) {
                boat.position = route->arrival_centre;
                boat.heading = 1.57f;
                voyage.arrived = true;
                voyage.arrival = 412;
                voyage.shelter = route->shelter_count - 1;
                voyage.event = BOAT_VOYAGE_ARRIVED;
                voyage.event_shelter = voyage.shelter;
                voyage.event_time = voyage_time - 1;
            } else if (!strcmp(name, "weather")) {
                boat.position = (Vector2) { 2300, -1600 };
                boat.heading = 1.08f;
                voyage.shelter = 1;
                voyage.weather = true;
                voyage.weather_started = voyage_time - 1;
                voyage.event = BOAT_VOYAGE_WEATHER;
                voyage.event_shelter = 1;
                voyage.event_time = voyage_time - 1;
            } else if (!strcmp(name, "recovered")) {
                voyage.event = BOAT_VOYAGE_RECOVERED;
                voyage.event_shelter = 2;
                voyage.event_time = voyage_time - 1;
            } else if (strcmp(name, "shelter")) {
                fprintf(stderr, "Unknown voyage case: %s\n", name);
                return 1;
            }
        } else {
            fprintf(stderr,
                "Usage: boat_hud_review [--background PNG] [--screenshot PNG] "
                "[--case neutral|docking|reverse-docking|cruise|reverse|full|reset] "
                "[--compact|--large] [--voyage shelter|exposed|slow|arrived|recovered|weather] "
                "[--frames N]\n");
            return 1;
        }
    }
    if (frames < 1)
        return 1;
    SetConfigFlags(screenshot ? FLAG_WINDOW_HIDDEN : 0);
    InitWindow(width, height, "Proteus / marine instruments");
    if (!IsWindowReady())
        return 1;
    SetTargetFPS(60);
    Texture2D scene = { 0 };
    if (background) {
        scene = LoadTexture(background);
        if (!IsTextureValid(scene)) {
            CloseWindow();
            return 1;
        }
        SetTextureFilter(scene, TEXTURE_FILTER_POINT);
    }
    RenderTexture2D native = LoadRenderTexture(320, 200);
    RenderTexture2D target = LoadRenderTexture(width, height);
    if (!IsRenderTextureValid(native) || !IsRenderTextureValid(target)) {
        if (native.id)
            UnloadRenderTexture(native);
        if (target.id)
            UnloadRenderTexture(target);
        if (scene.id)
            UnloadTexture(scene);
        CloseWindow();
        return 1;
    }
    SetTextureFilter(native.texture, TEXTURE_FILTER_POINT);
    int result = 0;
    for (int frame = 0; frame < frames && !WindowShouldClose(); ++frame) {
        BeginTextureMode(native);
        ClearBackground((Color) { 55, 91, 104, 255 });
        if (scene.id)
            DrawTexturePro(scene, (Rectangle) { 0, 0, (float)scene.width, (float)scene.height },
                (Rectangle) { 0, 0, 320, 200 }, (Vector2) { 0 }, 0, WHITE);
        boat_hud_draw(telemetry, 320, 200);
        EndTextureMode();
        BeginTextureMode(target);
        ClearBackground(BLACK);
        DrawTexturePro(native.texture, (Rectangle) { 0, 0, 320, -200 },
            (Rectangle) { 0, 0, (float)width, (float)height }, (Vector2) { 0 }, 0, WHITE);
        boat_voyage_hud(&voyage, &boat, voyage_time, width);
        EndTextureMode();
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTextureRec(target.texture, (Rectangle) { 0, 0, (float)width, (float)-height },
            (Vector2) { 0 }, WHITE);
        EndDrawing();
        if (screenshot && frame == frames - 1) {
            Image capture = LoadImageFromTexture(target.texture);
            ImageFlipVertical(&capture);
            result = ExportImage(capture, screenshot) ? 0 : 1;
            UnloadImage(capture);
        }
    }
    UnloadRenderTexture(native);
    UnloadRenderTexture(target);
    if (scene.id)
        UnloadTexture(scene);
    CloseWindow();
    return result;
}
