#include "dance.h"
#include "xm_audio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
    puts("Usage: dance_study [--time SECONDS] [--screenshot FILE] [--side] [--silent]\n"
         "                   [--slow] [--frames COUNT] [--choreography FILE | --classic]\n"
         "House study at 125 BPM; default choreography is assets/dance/house.dance.\n"
         "--classic selects the earlier cross-and-reach study.\n"
         "Space: pause | R: restart | V: side view | S: half speed (also lowers audio pitch)\n"
         "L: reload choreography and restart | Left/Right: pause and step one beat | Esc: close\n"
         "Screenshots are 320x200 and need no window or audio device.\n"
         "--frames closes playback after COUNT frames, for a manual smoke check.");
}

static bool seek_music(Music *music, float time, bool paused, float speed)
{
    StopMusicStream(*music);
    if (!xm_music_start_at(music, PROTEUS_ASSET_DIR "/music/starter.xm", time))
        return false;
    SetMusicPitch(*music, speed);
    PlayMusicStream(*music);
    UpdateMusicStream(*music);
    if (paused)
        PauseMusicStream(*music);
    return true;
}

int main(int argc, char **argv)
{
    const char *screenshot = NULL;
    const char *choreography_path = NULL;
    float time = 0, speed = 1;
    bool side = false, silent = false, classic = false;
    int frame_limit = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (strcmp(argv[i], "--time") == 0 && i + 1 < argc) {
            char *end;
            const char *value = argv[++i];
            time = strtof(value, &end);
            if (end == value || *end || !isfinite(time) || time < 0)
                goto invalid;
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            char *end;
            const char *value = argv[++i];
            long count = strtol(value, &end, 10);
            if (end == value || *end || count <= 0 || count > 10000000)
                goto invalid;
            frame_limit = (int)count;
        } else if (strcmp(argv[i], "--choreography") == 0 && i + 1 < argc) {
            choreography_path = argv[++i];
        } else if (strcmp(argv[i], "--classic") == 0) {
            classic = true;
        } else if (strcmp(argv[i], "--side") == 0) {
            side = true;
        } else if (strcmp(argv[i], "--silent") == 0) {
            silent = true;
        } else if (strcmp(argv[i], "--slow") == 0) {
            speed = .5f;
        } else {
            goto invalid;
        }
    }

    if (classic && choreography_path)
        goto invalid;
    SetTraceLogLevel(LOG_WARNING);
    char error[512];
    if (!classic) {
        if (!choreography_path)
            choreography_path = FileExists("assets/dance/house.dance") ? "assets/dance/house.dance"
                                                                       : PROTEUS_ASSET_DIR
                "/dance/house.dance";
        if (!dance_load_choreography(choreography_path, error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        printf("choreography=%s\nduration=%.2f\n", choreography_path, dance_duration());
    }
    enum { WIDTH = 320, HEIGHT = 200 };
    Color *pixels = malloc(WIDTH * HEIGHT * sizeof(Color));
    if (!pixels)
        return 1;
    Renderer3D renderer = { 0 };
    int result = 1;
    time = fmodf(time, dance_duration());
    if (!dance_draw(&renderer, pixels, WIDTH, HEIGHT, time, side))
        goto cleanup;
    Image image = { pixels, WIDTH, HEIGHT, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    if (screenshot) {
        result = ExportImage(image, screenshot) ? 0 : 1;
        goto cleanup;
    }

    InitWindow(WIDTH * 3, HEIGHT * 3 + 64, "Proteus / dance study");
    if (!IsWindowReady())
        goto cleanup;
    SetTargetFPS(60);
    Texture2D texture = LoadTextureFromImage(image);
    if (!IsTextureValid(texture)) {
        CloseWindow();
        goto cleanup;
    }
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    Music music = { 0 };
    bool audio_ready = false, music_ready = false;
    if (!silent) {
        InitAudioDevice();
        audio_ready = IsAudioDeviceReady();
        if (audio_ready) {
            music = LoadMusicStream(PROTEUS_ASSET_DIR "/music/starter.xm");
            music_ready = IsMusicValid(music);
            if (music_ready && !seek_music(&music, time, false, speed)) {
                UnloadMusicStream(music);
                music_ready = false;
            }
        }
        if (!music_ready)
            fprintf(stderr, "Music unavailable; continuing the dance study silently.\n");
    }
    bool paused = false, reload_failed = false;
    double previous = GetTime();
    int frames = 0, loops = 0;
    result = 0;
    while (!WindowShouldClose() && (frame_limit == 0 || frames < frame_limit)) {
        double now = GetTime();
        bool seek = false;
        if (!paused)
            time += (float)(now - previous) * speed;
        previous = now;
        if (time >= dance_duration()) {
            time = fmodf(time, dance_duration());
            seek = true;
            ++loops;
        }
        if (IsKeyPressed(KEY_SPACE)) {
            paused = !paused;
            if (music_ready) {
                if (paused)
                    PauseMusicStream(music);
                else
                    ResumeMusicStream(music);
            }
        }
        if (IsKeyPressed(KEY_R)) {
            time = 0;
            seek = true;
        }
        if (IsKeyPressed(KEY_L) && choreography_path) {
            if (dance_load_choreography(choreography_path, error, sizeof(error))) {
                time = 0;
                seek = true;
                reload_failed = false;
            } else {
                fprintf(stderr, "%s\n", error);
                reload_failed = true;
            }
        }
        if (IsKeyPressed(KEY_V))
            side = !side;
        if (IsKeyPressed(KEY_S)) {
            speed = speed == 1 ? .5f : 1;
            seek = true;
        }
        int direction = IsKeyPressed(KEY_RIGHT) ? 1 : IsKeyPressed(KEY_LEFT) ? -1 : 0;
        if (direction) {
            time = fmodf(time + direction * 60.0f / 125 + dance_duration(), dance_duration());
            paused = true;
            seek = true;
        }
        if (music_ready) {
            if (seek) {
                if (!seek_music(&music, time, paused, speed)) {
                    result = 1;
                    break;
                }
                previous = GetTime();
            }
            if (!paused)
                UpdateMusicStream(music);
        }
        if (!dance_draw(&renderer, pixels, WIDTH, HEIGHT, time, side)) {
            result = 1;
            break;
        }
        UpdateTexture(texture, pixels);
        DancePose pose;
        dance_evaluate(time, &pose);
        float bar_beat = fmodf(pose.beat, 4);
        BeginDrawing();
        ClearBackground((Color) { 17, 23, 34, 255 });
        DrawTextureEx(texture, (Vector2) { 0 }, 0, 3, WHITE);
        DrawText(TextFormat("%s | %s | BAR %d/%d  BEAT %d/4 | %s%.1fx", pose.phrase, pose.action,
                     (int)pose.beat / 4 + 1, (int)roundf(dance_duration() * 125 / 240),
                     (int)bar_beat + 1, paused ? "PAUSED " : "", speed),
            16, HEIGHT * 3 + 8, 18, RAYWHITE);
        DrawText(reload_failed ? "Reload failed: see terminal; previous choreography retained."
                               : "Space pause   R restart   L reload   V side   S slow   "
                                 "Left/Right step   Esc close",
            16, HEIGHT * 3 + 36, 16, GRAY);
        if (!music_ready)
            DrawText("SILENT", WIDTH * 3 - 90, HEIGHT * 3 + 8, 16, ORANGE);
        EndDrawing();
        ++frames;
    }
    printf("dance_study_frames=%d\nloops=%d\nmusic=%s\n", frames, loops,
        music_ready ? "playing" : "silent");
    if (music_ready)
        UnloadMusicStream(music);
    if (audio_ready)
        CloseAudioDevice();
    UnloadTexture(texture);
    CloseWindow();
cleanup:
    renderer3d_destroy(&renderer);
    free(pixels);
    return result;
invalid:
    usage();
    return 2;
}
