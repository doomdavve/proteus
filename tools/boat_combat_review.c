// Fixed combat presentation fixture. Offline captures and a visible animation use the
// production CPU/GPU scene, water, HUD and audio; never joins or modifies a room.
#include "boat_audio.h"
#include "boat_fleet.h"
#include "boat_gpu.h"
#include "boat_hud.h"
#include "wake_velocity_net.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    bool gpu_enabled = false, play = false, close = false;
    const char *path = "build/combat.png";
    double at = 2.3;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--gpu"))
            gpu_enabled = true;
        else if (!strcmp(argv[i], "--play"))
            play = true;
        else if (!strcmp(argv[i], "--close"))
            close = true;
        else if (!strcmp(argv[i], "--time") && i + 1 < argc)
            at = atof(argv[++i]);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc)
            path = argv[++i];
        else
            return 2;
    }
    if (gpu_enabled || play) {
        if (!play)
            SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(960, 600, "Deathmatch weapons presentation fixture");
        SetTargetFPS(60);
    }
    BoatWorldResources resources;
    if (!boat_world_resources_init(&resources, boat_location(BOAT_LOCATION_BASIN)))
        return 1;
    static NetStudy net;
    BoatFleet *f = &net.client.fleet;
    if (!boat_fleet_init(f, &resources, (BoatWater) { 0 }, BOAT_OBJECT_LIFE_TICKS, 1.8f))
        return 1;
    boat_combat_enable(f);
    net.client.accept.mode = BOAT_NET_MODE_DEATHMATCH;
    net.client.slot = 0;
    Boat b;
    boat_reset(&b);
    b.position = (Vector2) { 0, 15 };
    boat_fleet_place(f, 0, b);
    Boat rival = b;
    rival.position = (Vector2) { 7, 0 };
    boat_fleet_place(f, 1, rival);
    BoatGpu gpu = { 0 };
    Renderer3D cpu = { 0 };
    BoatCpuView cpu_view = { 0 };
    if (gpu_enabled && !boat_gpu_init(&gpu, false))
        return 1;
    BoatPresentation presentation = { 0 };
    BoatAudio audio = { 0 };
    if (play)
        boat_audio_init(&audio, BOAT_ENGINE_DEFAULT);
    RenderTexture2D target = { 0 };
    if (gpu_enabled || play)
        target = LoadRenderTexture(320, 200);
    Color pixels[320 * 200] = { 0 };
    Texture2D texture = { 0 };
    if (gpu_enabled || play)
        texture = LoadTextureFromImage(
            (Image) { pixels, 320, 200, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 });
    for (unsigned frame = 0;
        frame < (play ? 480u : 1u) && ((!gpu_enabled && !play) || !WindowShouldClose()); ++frame) {
        double time = play ? (double)frame / 60 : at;
        uint64_t tick = (uint64_t)llround(time * BOAT_TICK_HZ);
        f->state.tick = f->state.wakes.tick = tick;
        BoatCombat *c = &f->state.combat;
        c->waves[0] = (BoatRadialWave) {
            .active = true, .born = 180, .height = 2, .speed = 10, .range = 30, .centre = { 0, 0 }
        };
        c->grenades[1] = (BoatCombatObject) { .active = tick >= 240 && tick < 420,
            .born = 240,
            .position = { 7, -9 },
            .direction = { 0, -1 },
            .height = .6f };
        // The production launch cadence and parabola, sampled without a hidden visual clock.
        Vector3 muzzle = boat_combat_cannon_muzzle(&b);
        for (unsigned k = 0; k < 6; ++k) {
            uint64_t age_ticks = tick % c->rules.fire_ticks + k * c->rules.fire_ticks;
            float age = (float)age_ticks * BOAT_STEP;
            float height = muzzle.y + boat_combat_cannon_rise(age);
            c->bullets[k]
                = (BoatCombatObject) { .active = tick >= age_ticks && height > BOAT_CANNON_RADIUS,
                      .born = tick >= age_ticks ? tick - age_ticks : 0,
                      .position = { -muzzle.x, muzzle.z - c->rules.bullet_speed * age },
                      .height = height,
                      .direction = { 0, -1 } };
        }
        c->torpedoes[0] = (BoatCombatObject) { .active = true,
            .born = tick > 30 ? tick - 30 : 0,
            .position = { 3, 3 },
            .height = .2f,
            .direction = { 0, -1 } };
        c->players[0].damage[1] = 40;
        c->players[0].damage_tick[1] = tick > 20 ? tick - 20 : 0;
        c->players[0].damage_life[1] = f->state.slots[1].life;
        c->players[0].health = 68;
        c->players[0].heat = (float)fmod(time / 2, 1);
        c->players[0].next_grenade = tick + 360;
        BoatViewState view = { .boat = b, .time = time };
        BoatWorld world = boat_fleet_world(f);
        boat_presentation_step(&presentation, &b, &world, time);
        BoatRivals rivals = { .count = 1 };
        rivals.boats[0].view = (BoatViewState) { .boat = b, .time = time, .skin = 1 };
        rivals.boats[0].view.boat.position = (Vector2) { 7, 0 };
        if (time > 4)
            rivals.boats[0].view.boat.height -= fminf(4, (float)(time - 4) * 1.5f);
        BoatCamera camera = close ? (BoatCamera) { .distance = 10, .height = 7 }
                                  : (BoatCamera) { .distance = 26, .height = 20 };
        if (gpu_enabled)
            boat_gpu_draw_scene(&gpu, &view, &presentation, &world, camera, &rivals, NULL, NULL, 0);
        else {
            if (!boat_draw_scene(&cpu, &cpu_view, pixels, 320, 200, &view, &presentation, &world,
                    camera, &rivals, NULL, NULL, 0))
                return 1;
            if (!play) {
                Image image = { pixels, 320, 200, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
                bool ok = ExportImage(image, path);
                boat_cpu_view_destroy(&cpu_view);
                renderer3d_destroy(&cpu);
                boat_world_resources_destroy(&resources);
                return ok ? 0 : 1;
            }
            UpdateTexture(texture, pixels);
        }
        BeginTextureMode(target);
        DrawTexturePro(gpu_enabled ? gpu.target.texture : texture,
            (Rectangle) { 0, 0, gpu_enabled ? -320 : 320, gpu_enabled ? -200 : 200 },
            (Rectangle) { 0, 0, 320, 200 }, (Vector2) { 0 }, 0, WHITE);
        boat_hud_draw((BoatHudTelemetry) { .speed_mps = 12, .thrust = .5f }, 320, 200);
        net_study_combat_hud(&net, 320, 200, &view, &presentation, &world, camera);
        EndTextureMode();
        if (play) {
            boat_audio_update(&audio,
                (BoatAudioInput) {
                    .gun_tick = (tick / 15) * 15, .grenade_tick = tick >= 180 ? 180 : 0 },
                true);
            BeginDrawing();
            ClearBackground(BLACK);
            DrawTexturePro(target.texture, (Rectangle) { 0, 0, 320, -200 },
                (Rectangle) { 0, 0, 960, 600 }, (Vector2) { 0 }, 0, WHITE);
            EndDrawing();
        }
    }
    Image image = LoadImageFromTexture(target.texture);
    ImageFlipVertical(&image);
    bool ok = ExportImage(image, path);
    UnloadImage(image);
    if (play)
        boat_audio_destroy(&audio);
    UnloadTexture(texture);
    UnloadRenderTexture(target);
    if (gpu_enabled)
        boat_gpu_destroy(&gpu);
    boat_cpu_view_destroy(&cpu_view);
    renderer3d_destroy(&cpu);
    boat_world_resources_destroy(&resources);
    CloseWindow();
    return ok ? 0 : 1;
}
