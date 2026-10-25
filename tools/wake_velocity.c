#include "boat_assets.h"
#include "boat_audio.h"
#include "boat_course_view.h"
#include "boat_gpu.h"
#include "boat_hud.h"
#include "boat_music.h"
#include "boat_profile.h"
#include "boat_replay.h"
#include "boat_session.h"
#include "boat_steering.h"
#include "boat_throttle.h"
#include "wake_velocity_drive.h"
#include "wake_velocity_menu.h"
#ifdef PROTEUS_BOAT_NET
#include "boat_server.h"
#include "wake_velocity_host.h"
#include "wake_velocity_net.h"
#endif
#include "boat_title.h"
#include "frame_pacer.h"
#include "profiler.h"
#include "raymath.h"
#include "xm_audio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

EM_JS(int, boat_web_mouse_state, (int wanted, int available),
    { return Module['boatMouse']['state'](wanted, available); })
EM_JS(int, boat_web_mouse_toggle, (void), { return Module['boatMouse']['toggle'](); })
EM_JS(float, boat_web_mouse_movement, (void), { return Module['boatMouse']['movement'](); })
EM_JS(float, boat_web_mouse_scroll, (void), { return Module['boatMouse']['scroll'](); })
#endif

static void usage(void)
{
    puts(
        "Usage: wake_velocity [--demo] [--frames COUNT] [--screenshot FILE [--time SECONDS]]\n"
        "--verbose: log graphics/window backend and resource initialization\n"
        "No options (or --title): Wake Velocity title with multiplayer and settings (name, boat,\n"
        "  mouse steering, soundtrack)\n"
        "  Esc > BACK TO TITLE returns to it | --title --screenshot FILE: capture the title\n"
        "--soundtrack a-little-boat|acid-wake|off: local background music (default a-little-boat)\n"
        "Left/Right or A/D: adjust wheel | Up/Down or W/S: step thrust (4 forward / neutral / 2 "
        "reverse); hold to step every 0.5 s\n"
        "Scroll down/up: more/less thrust (also Up/Down or W/S)\n"
        "M: capture/release mouse steering (move to turn; stop to hold) | --mouse-steering\n"
        "Arcade jumps: steering rolls in the air; centre the wheel to stabilize for landing\n"
        "Keyboard wheel springs to centre; mouse wheel and thrust hold. R: reset | Esc: menu "
        "(steering, controls, leave; solo pauses, rooms keep running)\n"
        "+/-: closer/farther | PageUp/PageDown: raise/lower camera | C: reset view\n"
        "--wake-field: opt-in coherent CPU field, native basin only\n"
        "--wake-objects: bounded arcade jump wakes (default in free boating)\n"
        "--wake-objects-crossing: a scripted second fleet boat sharing wakes and contacts "
        "(basin only)\n"
        "--wake-life-ticks 240..1920 (default 1920) | --wake-height 0.1..3 (default 1.2 m)\n"
        "--player-wake: legacy D3 trail | --no-player-wake: visual-only player trail\n"
        "--current / --crossing-wake: opt-in authored tests in all locations; R restarts passage\n"
        "--skin sweden|finland|denmark|norway: cosmetic speedboat livery (default sweden)\n"
        "--camera-distance UNITS: 8..48 (default 20; river 12)\n"
        "--camera-height UNITS: 4..36 (default 14; river 6)\n"
        "--fixed-fov: keep the 55-degree view instead of widening gently with speed\n"
        "--location basin|river (default basin; river extends to Alvsborgsbron)\n"
        "--waves: gentle crossing waves and a floating hull\n"
        "--course: timed S course in the basin; cross highlighted gates in order | R: retry\n"
        "G: ghost on/off | P/B: replay last/best | Space: pause replay | Enter: restart replay\n"
        "Replay: Left/Right seek 1s (Shift: 5s) | Home/End | [ / ]: speed 0.25x..4x\n"
        "Click/drag the replay timeline to seek and pause; Space resumes.\n"
        "--replay FILE: watch a saved course run (also supports --screenshot --time)\n"
        "--ghost FILE: race a compatible saved run | --save-run FILE: export each finish\n"
        "--data-dir DIR: override personal-best storage directory\n"
        "--renderer gpu|cpu (default gpu) | --gpu: select GPU | --gpu-aa: 4x supersampling\n"
        "F3: toggle debug profiler | --profiler: show it on startup\n"
        "Frames follow the display with vsync (capped just above its refresh rate) |\n"
        "  --no-vsync: pace to exactly 60 Hz instead\n"
        "CPU screenshots need no window; GPU screenshots require a graphics context.\n"
        "--time is screenshot-only (0..60, or 0..600 with --replay); --frames limits a smoke run.\n"
        "--demo drives the screenshot example in a window; R restarts it.\n"
        "--connect HOST:PORT: join a boat_server room (native multiplayer, in progress: the own\n"
        "  boat is predicted and answers at once; R asks for a new life; every boat is drawn in\n"
        "  its slot's country: 1 Sweden, 2 Finland, 3 Denmark, 4 Norway) | --name TEXT |\n"
        "  --token TEXT\n"
        "--host free|deathmatch|race: start a room open to the local network (a second copy of\n"
        "  this program in --serve mode) and join it; --location and --waves set its water,\n"
        "  --port its UDP port (default 24680). The room closes when you leave it.\n"
        "--serve [boat_server options]: run only a room, as boat_server does (--serve --help)");
}

static BoatWater study_water(
    const BoatWorldDefinition *location, bool waves, bool current, bool crossing, bool player)
{
    BoatWater water = boat_world_test_water(location, waves, current, crossing);
    water.player_wake_enabled = player;
    return water;
}

static void draw_help(bool course, bool mapped, bool deathmatch, bool race)
{
    int width = GetScreenWidth(), height = GetScreenHeight();
    const char *lines[] = {
        "BOATING / KEYBOARD",
        "Left/Right or A/D: steer (keyboard mode)",
        "M: toggle mouse / keyboard steering",
        "Mouse: move to turn; stop to hold wheel",
        "In air: steer to roll; centre to stabilize",
        "Esc: menu (steering, controls, leave)",
        "W/S or scroll down/up: step thrust",
        "Release keyboard steering: centre wheel",
        "Thrust: 4 forward / neutral / 2 reverse",
        "Tap: one step | hold: step each 0.5 s",
        "R: reset boat / restart demo",
        deathmatch ? "Hold Tab: deathmatch scoreboard"
            : race ? "Hold Tab: race standings"
                   : "",
        "+/-: zoom | PgUp/PgDn: height",
        "C: reset camera | F3: profiler",
#if defined(__EMSCRIPTEN__)
        "Esc: leave fullscreen | Reload page: new game",
#endif
        course ? "G: ghost | P/B: replay last/best" : "",
        course ? "Replay: Space pause | Enter restart" : "",
        course ? "Left/Right: seek | Shift: 5 seconds" : "",
        course ? "Home/End: start/end | [ / ]: speed" : "",
        course ? "Click/drag timeline: seek and pause" : "",
        "? or Esc: dismiss help",
        mapped ? "Map: OpenStreetMap contributors (ODbL)" : "",
    };
    DrawRectangle(0, 0, width, height, (Color) { 8, 15, 22, 220 });
    int font = height < 300 ? 8 : width < 600 || height < 500 ? 10 : 20;
    int spacing = font + (height < 300 ? 3 : 6);
    int count = (int)(sizeof(lines) / sizeof(lines[0]));
    int visible = 0;
    for (int i = 0; i < count; ++i)
        visible += lines[i][0] != '\0';
    int y = (height - visible * spacing) / 2;
    for (int i = 0; i < count; ++i) {
        if (!lines[i][0])
            continue;
        DrawText(lines[i], (width - MeasureText(lines[i], font)) / 2, y, font, RAYWHITE);
        y += spacing;
    }
}

static Rectangle replay_timeline(int width)
{
    return (Rectangle) { 16, 160, fminf(416, width - 32), 12 };
}

static void course_runs_hud(
    const BoatReplayRuns *runs, const BoatViewState *view, const BoatViewState *ghost, int width)
{
    DrawRectangle(8, 72, width > 460 ? 440 : width - 16, runs->playing ? 120 : 84,
        (Color) { 14, 23, 32, 245 });
    if (runs->best.complete)
        DrawText(TextFormat("BEST %.2fs", runs->best.splits[BOAT_COURSE_GATES - 1]), 16, 76, 16,
            (Color) { 90, 240, 255, 255 });
    if (!runs->playing && runs->opponent.complete && view->course.next_gate > 1) {
        int gate = view->course.next_gate - 1;
        DrawText(
            TextFormat("Split %+.2fs", view->course.splits[gate] - runs->opponent.splits[gate]), 16,
            96, 16, RAYWHITE);
    }
    if (runs->playing) {
        double duration = runs->playing->splits[BOAT_COURSE_GATES - 1];
        DrawText(TextFormat("REPLAY %s %.2gx  %.2f / %.2fs", runs->paused ? "PAUSED" : "",
                     boat_replay_speed(runs), runs->replay_time, duration),
            16, 116, 16, (Color) { 90, 240, 255, 255 });
        Rectangle bar = replay_timeline(width);
        DrawRectangleRec(bar, (Color) { 40, 58, 68, 255 });
        float playhead = bar.x + bar.width * (float)(runs->replay_time / duration);
        DrawRectangleRec((Rectangle) { bar.x, bar.y, playhead - bar.x, bar.height },
            (Color) { 46, 130, 146, 255 });
        for (int i = 0; i < BOAT_COURSE_GATES; ++i) {
            int x = (int)(bar.x + bar.width * (float)(runs->playing->splits[i] / duration));
            DrawLine(x, (int)bar.y - 2, x, (int)(bar.y + bar.height), LIGHTGRAY);
            DrawText(i == 0                      ? "S"
                    : i == BOAT_COURSE_GATES - 1 ? "F"
                                                 : TextFormat("%d", i),
                x - 3, (int)(bar.y + bar.height + 3), 10, LIGHTGRAY);
        }
        DrawCircle((int)playhead, (int)(bar.y + bar.height / 2), 3, (Color) { 90, 240, 255, 255 });
    } else {
        if (runs->notice)
            DrawText(runs->notice, 16, 138, 12, RAYWHITE);
    }
    if (ghost && width >= 368) {
        int left = width - 128;
        int x = left + 60 + (int)fmaxf(-54, fminf(54, ghost->boat.position.x));
        int y = 38 + (int)fmaxf(0, fminf(112, ghost->boat.position.y + 60));
        DrawCircleLines(x, y, 4, (Color) { 90, 240, 255, 255 });
    }
}

#ifdef PROTEUS_BOAT_NET
static NetStudy net_study; // Outlives a session so the title can show why it ended.
static NetHost net_host; // The room this session started with --host, if any.
#endif

// The steering mode a session ended in, so the title remembers changes made in game.
static bool study_mouse_steering;

// One session with explicit options. Uses the title's window when one is open and then leaves it
// open; the Escape menu's last row ends the session.
static int study(int argc, char **argv)
{
    const char *screenshot = NULL;
    unsigned int skin = BOAT_SKIN_SWEDEN;
    unsigned soundtrack = BOAT_MUSIC_LITTLE_BOAT;
    bool skin_set = false;
    const char *replay_path = NULL, *ghost_path = NULL, *data_dir = NULL, *export_path = NULL;
    float time = 0;
    BoatCamera camera_settings = BOAT_CAMERA_DEFAULT;
    bool camera_distance_set = false, camera_height_set = false;
    bool time_set = false, demo = false, mouse_steering = false;
    const BoatWorldDefinition *location = boat_location(BOAT_LOCATION_BASIN);
    bool course = false, waves = false;
    bool current = false, crossing_wake = false, player_wake = true;
    bool player_wake_set = false, wake_field = false;
    bool wake_objects = false, object_crossing = false, object_tuning = false;
    unsigned object_life = BOAT_OBJECT_LIFE_TICKS;
    float object_height = BOAT_OBJECT_DEFAULT_HEIGHT;
    bool gpu_enabled = true, gpu_aa = false;
    bool profiler_visible = false;
    bool vsync = true;
    bool verbose = false;
    int frame_limit = 0;
    const char *connect_to = NULL, *net_name = "player", *net_token = "";
    bool networked = false; // Joined a room with --connect (native builds only).
    const char *host_mode = NULL; // --host: the room this session starts and joins.
    bool location_set = false;
    long host_port = 0; // --port: the hosted room's UDP port.
#ifdef PROTEUS_BOAT_NET
    NetStudy *net = NULL;
#endif
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--soundtrack") == 0 && i + 1 < argc) {
            if (!boat_music_parse(argv[++i], &soundtrack))
                goto invalid;
        } else if (strcmp(argv[i], "--skin") == 0 && i + 1 < argc) {
            if (!boat_skin_parse(argv[++i], &skin))
                goto invalid;
            skin_set = true;
        } else if (strcmp(argv[i], "--location") == 0 && i + 1 < argc) {
            location = boat_location_find(argv[++i]);
            if (!location)
                goto invalid;
            location_set = true;
        } else if (strcmp(argv[i], "--renderer") == 0 && i + 1 < argc) {
            const char *name = argv[++i];
            if (strcmp(name, "gpu") == 0)
                gpu_enabled = true;
            else if (strcmp(name, "cpu") == 0)
                gpu_enabled = false;
            else
                goto invalid;
        } else if (strcmp(argv[i], "--gpu") == 0) {
            gpu_enabled = true;
        } else if (strcmp(argv[i], "--gpu-aa") == 0) {
            gpu_aa = true;
        } else if (strcmp(argv[i], "--profiler") == 0) {
            profiler_visible = true;
        } else if (strcmp(argv[i], "--no-vsync") == 0) {
            vsync = false;
        } else if (strcmp(argv[i], "--wake-field") == 0) {
            wake_field = true;
        } else if (strcmp(argv[i], "--wake-objects") == 0) {
            wake_objects = true;
        } else if (strcmp(argv[i], "--wake-objects-crossing") == 0) {
            wake_objects = object_crossing = true;
        } else if (strcmp(argv[i], "--wake-life-ticks") == 0 && i + 1 < argc) {
            char *end;
            const char *value = argv[++i];
            long parsed = strtol(value, &end, 10);
            if (end == value || *end || parsed < 240 || parsed > BOAT_OBJECT_LIFE_TICKS)
                goto invalid;
            object_life = (unsigned)parsed;
            object_tuning = true;
        } else if (strcmp(argv[i], "--wake-height") == 0 && i + 1 < argc) {
            char *end;
            const char *value = argv[++i];
            float parsed = strtof(value, &end);
            if (end == value || *end || !isfinite(parsed) || parsed < .1f || parsed > 3)
                goto invalid;
            object_height = parsed;
            object_tuning = true;
        } else if (strcmp(argv[i], "--player-wake") == 0) {
            player_wake = true;
            player_wake_set = true;
        } else if (strcmp(argv[i], "--no-player-wake") == 0) {
            player_wake = false;
            player_wake_set = true;
        } else if (strcmp(argv[i], "--current") == 0) {
            current = true;
        } else if (strcmp(argv[i], "--crossing-wake") == 0) {
            crossing_wake = true;
        } else if (strcmp(argv[i], "--waves") == 0) {
            waves = true;
        } else if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            replay_path = argv[++i];
            course = true;
        } else if (strcmp(argv[i], "--ghost") == 0 && i + 1 < argc) {
            ghost_path = argv[++i];
            course = true;
        } else if (strcmp(argv[i], "--save-run") == 0 && i + 1 < argc) {
            export_path = argv[++i];
        } else if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (strcmp(argv[i], "--course") == 0) {
            course = true;
        } else if (strcmp(argv[i], "--mouse-steering") == 0) {
            mouse_steering = true;
        } else if (strcmp(argv[i], "--demo") == 0) {
            demo = true;
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (strcmp(argv[i], "--time") == 0 && i + 1 < argc) {
            char *end;
            const char *value = argv[++i];
            time = strtof(value, &end);
            if (end == value || *end || !isfinite(time) || time < 0
                || time > BOAT_REPLAY_MAX_SECONDS)
                goto invalid;
            time_set = true;
        } else if (strcmp(argv[i], "--fixed-fov") == 0) {
            camera_settings.fixed_fov = true;
        } else if ((strcmp(argv[i], "--camera-distance") == 0
                       || strcmp(argv[i], "--camera-height") == 0)
            && i + 1 < argc) {
            bool distance = strcmp(argv[i], "--camera-distance") == 0;
            char *end;
            const char *value = argv[++i];
            float parsed = strtof(value, &end);
            if (end == value || *end || !isfinite(parsed) || parsed < (distance ? 8 : 4)
                || parsed > (distance ? 48 : 36))
                goto invalid;
            if (distance) {
                camera_settings.distance = parsed;
                camera_distance_set = true;
            } else {
                camera_settings.height = parsed;
                camera_height_set = true;
            }
        } else if (strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            connect_to = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            char *end;
            const char *value = argv[++i];
            host_port = strtol(value, &end, 10);
            if (end == value || *end || host_port < 1 || host_port > 65535)
                goto invalid;
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host_mode = argv[++i];
            if (strcmp(host_mode, "free") != 0 && strcmp(host_mode, "deathmatch") != 0
                && strcmp(host_mode, "race") != 0)
                goto invalid;
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            net_name = argv[++i];
        } else if (strcmp(argv[i], "--token") == 0 && i + 1 < argc) {
            net_token = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            char *end;
            const char *value = argv[++i];
            long count = strtol(value, &end, 10);
            if (end == value || *end || count <= 0 || count > 10000000)
                goto invalid;
            frame_limit = (int)count;
        } else {
            goto invalid;
        }
    }
    if ((time_set && !screenshot) || (screenshot && frame_limit) || (!replay_path && time > 60))
        goto invalid;
#ifdef PROTEUS_BOAT_NET
    NetHostOptions host_options = { 0 };
    char host_endpoint[32];
#endif
    if (host_mode) {
#ifdef PROTEUS_BOAT_NET
        if (connect_to || net_token[0]) {
            fprintf(stderr, "--host joins its own room; it cannot take --connect or --token.\n");
            goto invalid;
        }
        if (!strcmp(host_mode, "race") && location_set && location->id != BOAT_LOCATION_RIVER) {
            fprintf(stderr, "--host race runs on the river location.\n");
            goto invalid;
        }
        // The room's water: what follows joins it like --connect.
        host_options = (NetHostOptions) { .mode = host_mode,
            .location = location->name,
            .waves = waves,
            .port = (uint16_t)(host_port ? host_port : BOAT_NET_PORT) };
        snprintf(host_endpoint, sizeof(host_endpoint), "127.0.0.1:%u", (unsigned)host_options.port);
        connect_to = host_endpoint;
        location = boat_location(BOAT_LOCATION_BASIN);
        waves = false;
#else
        fprintf(stderr, "--host is only available in native builds.\n");
        goto invalid;
#endif
    }
    if (host_port && !host_mode) {
        fprintf(stderr, "--port sets the room --host starts.\n");
        goto invalid;
    }
    if (!connect_to && (strcmp(net_name, "player") != 0 || net_token[0])) {
        fprintf(stderr, "--name and --token require --connect or --host.\n");
        goto invalid;
    }
    if (connect_to) {
#ifdef PROTEUS_BOAT_NET
        // Any option that the room's own settings replace.
        bool other_mode = course || ghost_path || replay_path || wake_field || wake_objects
            || object_tuning || player_wake_set || current || crossing_wake || waves || demo
            || screenshot || location->id != BOAT_LOCATION_BASIN;
        if (other_mode) {
            fprintf(stderr,
                "--connect takes its water, location and wakes from the room; it cannot be "
                "combined with course, replay, wake, water, location, demo or screenshot "
                "options.\n");
            goto invalid;
        }
        if (skin_set) {
            fprintf(stderr,
                "--skin does not apply with --connect: each room slot has its own country "
                "(1 Sweden, 2 Finland, 3 Denmark, 4 Norway) on every client.\n");
            goto invalid;
        }
        if (strlen(net_name) > BOAT_NET_NAME_BYTES || strlen(net_token) > BOAT_NET_TOKEN_BYTES) {
            fprintf(stderr, "--name is at most %d and --token at most %d characters.\n",
                BOAT_NET_NAME_BYTES, BOAT_NET_TOKEN_BYTES);
            goto invalid;
        }
        player_wake = false;
        player_wake_set = true; // No local wake mode: the room's pool is drawn instead.
#else
        fprintf(stderr, "--connect is only available in native builds.\n");
        goto invalid;
#endif
    }
    if ((!course && (data_dir || export_path)) || (replay_path && ghost_path))
        goto invalid;
    if (course && (location->mapped || demo)) {
        fprintf(stderr, "--course requires the basin and cannot be combined with --demo.\n");
        goto invalid;
    }
    if ((current || crossing_wake) && (course || ghost_path)) {
        fprintf(stderr,
            "--current and --crossing-wake require free boating, without course/replay/ghost "
            "modes.\n");
        goto invalid;
    }
    if (player_wake_set && player_wake && (course || ghost_path)) {
        fprintf(
            stderr, "--player-wake requires free boating, without course/replay/ghost modes.\n");
        goto invalid;
    }
    // Choose the free-boating default after parsing so explicit alternatives and
    // recorded course/replay water remain independent of argument order.
    if (!wake_objects && !wake_field && !player_wake_set && !course && !ghost_path)
        wake_objects = true;
    if ((object_tuning && !wake_objects)
        || (wake_objects
            && (wake_field || course || ghost_path || (player_wake_set && player_wake)
                || (object_crossing && location->mapped)))) {
        fprintf(stderr,
            "Wake objects require free boating, without another player-wake mode; "
            "the scripted crossing boat requires the basin.\n");
        goto invalid;
    }
    if (wake_objects)
        player_wake = false;
    if (wake_field
        && (location->mapped || course || ghost_path || current || crossing_wake
            || (player_wake_set && !player_wake))) {
        fprintf(stderr,
            "--wake-field requires still-water free boating in the basin; no current, crossing "
            "source, course or replay.\n");
        goto invalid;
    }
#if defined(__EMSCRIPTEN__)
    if (wake_field) {
        fprintf(stderr, "--wake-field is a native desktop review candidate.\n");
        goto invalid;
    }
#endif
    if (wake_field) {
        player_wake = false;
#ifndef NDEBUG
        fprintf(stderr,
            "Wake field: this is a slow Debug build. For play review, build RelWithDebInfo "
            "in build/review and run ./build/review/wake_velocity --wake-field --profiler.\n");
#endif
        if (gpu_aa)
            fprintf(stderr,
                "Wake field: --gpu-aa renders 16 times as many pixels; start review without "
                "AA to check frame pacing.\n");
    }
    if (course || ghost_path)
        player_wake = false; // Existing course/replay comparisons retain their original water.
    BoatCamera camera_default
        = { .distance = location->camera_distance, .height = location->camera_height };
    camera_default.fixed_fov = camera_settings.fixed_fov;
    if (!camera_distance_set)
        camera_settings.distance = camera_default.distance;
    if (!camera_height_set)
        camera_settings.height = camera_default.height;

    SetTraceLogLevel(verbose ? LOG_INFO : LOG_WARNING);
    enum { WIDTH = 320, HEIGHT = 200 };
    Color *pixels = malloc(WIDTH * HEIGHT * sizeof(*pixels));
    if (!pixels)
        return 1;
    // Every location's physical resources: a joined room picks its own. Sessions, fleets and
    // replay views borrow them until shutdown.
    static BoatWorldResources worlds[BOAT_LOCATION_COUNT];
    for (int i = 0; i < BOAT_LOCATION_COUNT; ++i)
        if (!boat_world_resources_init(&worlds[i], boat_location((BoatLocationId)i))) {
            fprintf(
                stderr, "Cannot prepare the %s world.\n", boat_location((BoatLocationId)i)->name);
            while (i--)
                boat_world_resources_destroy(&worlds[i]);
            free(pixels);
            return 1;
        }
    Renderer3D renderer = { 0 };
    BoatCpuView cpu_view = { 0 }; // The GPU view owns its own water mesh and scenery.
    BoatGpu gpu = { 0 };
    Texture2D texture = { 0 };
    RenderTexture2D frame_target = { 0 };
    bool window_ready = false, window_owned = false, mouse_captured = false;
    BoatAudio audio = { 0 };
    Music music = { 0 };
    bool music_ready = false;
    int thrust_step = BOAT_THROTTLE_NEUTRAL;
    float throttle_scroll = 0, throttle_held = 0;
    BoatSession session;
    boat_session_init(&session, &worlds[location->id],
        study_water(location, waves, current, crossing_wake, player_wake));
    if (course)
        boat_session_start_course(&session);
    session.runs.ghost_visible = true;
    session.runs.export_path = export_path;
    BoatWakePlayer *wake = NULL;
    ObjectStudy object_world = { 0 };
    ObjectStudy *objects = wake_objects ? &object_world : NULL;
    if (objects)
        object_study_reset(objects, &session, object_crossing, object_life, object_height);
    int result = 1;
    BoatReplayFlag playback_flag = { 0 }, ghost_flag = { 0 };
#ifdef PROTEUS_BOAT_NET
    if (host_mode
        && !net_host_start(
            &net_host, &host_options, net_study.failure, sizeof(net_study.failure))) {
        fprintf(stderr, "%s\n", net_study.failure);
        goto cleanup;
    }
    if (connect_to) {
        if (!net_study_connect(&net_study, connect_to, net_name, net_token, worlds, &session))
            goto cleanup;
        net = &net_study;
        networked = true;
        location = session.world.definition;
        waves = net_study.client.accept.waves;
        camera_default.distance = location->camera_distance;
        camera_default.height = location->camera_height;
        if (!camera_distance_set)
            camera_settings.distance = camera_default.distance;
        if (!camera_height_set)
            camera_settings.height = camera_default.height;
    }
#endif
    if (wake_field) {
        wake = boat_wake_player_create(1.0 / 120);
        if (!wake) {
            fprintf(stderr, "Cannot allocate wake review world.\n");
            goto cleanup;
        }
        session.world.wake_field = boat_wake_player_field(wake);
    }
    if (replay_path) {
        if (!boat_replay_load(&session.runs.last, replay_path)
            || (waves && !session.runs.last.waves)) {
            fprintf(stderr, "Invalid or incompatible course replay: %s\n", replay_path);
            goto cleanup;
        }
        waves = session.runs.last.waves;
        session.world.water = study_water(location, waves, current, crossing_wake, player_wake);
    }
    if (course) {
        char directory[1800];
        if (data_dir || boat_replay_data_directory(directory, sizeof(directory))) {
            int n = snprintf(session.runs.best_path, sizeof(session.runs.best_path),
                "%s/course-%s-physics%d.run", data_dir ? data_dir : directory,
                waves ? "waves" : "calm", BOAT_PHYSICS_REVISION);
            if (n < 0 || (size_t)n >= sizeof(session.runs.best_path))
                session.runs.best_path[0] = 0;
        }
        if (session.runs.best_path[0] && FileExists(session.runs.best_path)
            && (!boat_replay_load(&session.runs.best, session.runs.best_path)
                || session.runs.best.waves != waves)) {
            boat_replay_destroy(&session.runs.best);
            session.runs.notice = "Saved best is incompatible or damaged; ignored";
            fprintf(stderr, "%s: %s\n", session.runs.notice, session.runs.best_path);
        }
        if (ghost_path
            && (!boat_replay_load(&session.runs.reference, ghost_path)
                || session.runs.reference.waves != waves)) {
            fprintf(stderr, "Invalid or incompatible course ghost: %s\n", ghost_path);
            goto cleanup;
        }
        boat_replay_runs_reset(&session.runs);
        if (replay_path)
            boat_replay_play(&session.runs, &session.runs.last);
    }
    if (location->mapped
        && !boat_map_water(&session.world.resources->map, session.boat.position, 2.5f)) {
        fprintf(stderr, "River spawn is not navigable.\n");
        goto cleanup;
    }
    // A replay is viewed in its own recorded water, never by writing into the live session.
    BoatViewState live_view, playback;
    BoatPresentation playback_presentation;
    BoatWorld playback_world;
    const BoatViewState *view = &live_view;
    const BoatPresentation *view_presentation = &session.presentation;
    const BoatWorld *view_world = &session.world;
    if (screenshot && session.runs.playing) {
        boat_replay_sample(session.runs.playing, time, &playback, &playback_presentation);
        boat_replay_animate_flag(
            session.runs.playing, &playback, &playback_flag, &playback_presentation.flag);
        boat_world_init(
            &playback_world, session.world.resources, boat_replay_water(session.runs.playing));
        view = &playback;
        view_presentation = &playback_presentation;
        view_world = &playback_world;
    } else if (screenshot) {
        for (int tick = 0; tick < (int)round(time / BOAT_TICK_SECONDS); ++tick)
            demo_step(&session, tick, NULL, wake, objects);
    }
    live_view = boat_session_view(&session);
    live_view.skin = skin;
    if (view == &playback)
        playback.skin = skin;
    // Course attempts record from here on; screenshots above never save a run.
    session.record_runs = course;
    BoatViewState ghost, other_view;
    BoatFlag ghost_cloth;
    const BoatViewState *ghost_view = NULL;
    const BoatFlag *ghost_view_flag = NULL;
    float ghost_opacity = 0;
    if (objects && objects->crossing) {
        other_view
            = (BoatViewState) { .boat = *object_study_other(objects), .time = live_view.time };
        ghost_view = &other_view;
        ghost_view_flag = &objects->other_presentation.flag;
        ghost_opacity = 1;
    }
    if (course && !session.runs.playing && session.runs.opponent.complete
        && boat_replay_sample(&session.runs.opponent, view->course.elapsed, &ghost, NULL)) {
        boat_replay_animate_flag(&session.runs.opponent, &ghost, &ghost_flag, &ghost_cloth);
        ghost_view = &ghost;
        ghost_view_flag = &ghost_cloth;
        ghost_opacity = .5f;
    }
    if (!gpu_enabled
        && !boat_draw_with_ghost(&renderer, &cpu_view, pixels, WIDTH, HEIGHT, view,
            view_presentation, view_world, camera_settings, ghost_view, ghost_view_flag,
            ghost_opacity))
        goto cleanup;
    Image image = { pixels, WIDTH, HEIGHT, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    if (screenshot && !gpu_enabled) {
        result = ExportImage(image, screenshot) ? 0 : 1;
        goto cleanup;
    }

    if (!IsWindowReady()) {
#if defined(__EMSCRIPTEN__)
        // The page scales a fixed 8:5 canvas; raylib's web resize fills the entire viewport.
        SetConfigFlags(0);
#else
        SetConfigFlags(screenshot ? FLAG_WINDOW_HIDDEN : FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
#endif
        InitWindow(WIDTH * 3, HEIGHT * 3, "Wake Velocity");
        if (!IsWindowReady())
            goto cleanup;
        window_owned = true;
        SetWindowMinSize(WIDTH, HEIGHT);
    }
    window_ready = true;
#if defined(__EMSCRIPTEN__)
    SetTargetFPS(60);
#else
    if (!vsync)
        ClearWindowState(FLAG_VSYNC_HINT);
#endif
    FramePacer pacer;
    frame_pacer_init_window(&pacer);
    if (gpu_enabled) {
        if (!boat_gpu_init(&gpu, gpu_aa)) {
            fprintf(stderr, "Could not initialize GPU boating resources.\n");
            goto cleanup;
        }
        texture = gpu.target.texture;
        if (screenshot) {
            boat_gpu_draw_with_ghost(&gpu, view, view_presentation, view_world, camera_settings,
                ghost_view, ghost_view_flag, ghost_opacity);
            if (gpu.failed) {
                fprintf(stderr, "Wake render allocation failed.\n");
                goto cleanup;
            }
            Image capture = LoadImageFromTexture(texture);
            // CPU camera basis is horizontally mirrored relative to native OpenGL.
            // Normalize X here (and in presentation) to preserve steering and map alignment.
            ImageFlipVertical(&capture);
            ImageFlipHorizontal(&capture);
            result = ExportImage(capture, screenshot) ? 0 : 1;
            UnloadImage(capture);
            goto cleanup;
        }
    } else {
        texture = LoadTextureFromImage(image);
        if (!IsTextureValid(texture))
            goto cleanup;
    }
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    frame_target = LoadRenderTexture(WIDTH, HEIGHT);
    if (!IsRenderTextureValid(frame_target))
        goto cleanup;
    SetTextureFilter(frame_target.texture, TEXTURE_FILTER_POINT);
    boat_audio_init(&audio, BOAT_ENGINE_DEFAULT);
    char music_path[4096];
    const BoatMusic *track = boat_music(soundtrack);
    if (track->asset && !boat_asset_path(music_path, sizeof(music_path), track->asset))
        goto cleanup;
    if (IsAudioDeviceReady() && track->asset) {
        music = LoadMusicStream(music_path);
        music_ready = IsMusicValid(music);
        if (music_ready && !xm_music_start_at(&music, music_path, 0)) {
            UnloadMusicStream(music);
            music_ready = false;
        }
        if (music_ready) {
            music.looping = true;
            SetMusicVolume(music, track->volume);
            PlayMusicStream(music);
        } else {
            TraceLog(LOG_WARNING, "Boating background music could not be loaded");
        }
    }
    double accumulator = 0;
    int frames = 0, demo_tick = 0;
    bool replay_dragging = false;
    bool help_visible = false;
    BoatMenu menu = { .can_leave = true,
        .leave_label = host_mode ? "CLOSE ROOM"
            : networked          ? "LEAVE ROOM"
            : window_owned       ? "QUIT"
                                 : "BACK TO TITLE",
        .room_hint = host_mode ? "YOU HOST: CLOSING ENDS THE ROOM" : NULL };
#if defined(__EMSCRIPTEN__)
    menu.can_leave = false;
#endif
    float mouse_wheel = session.boat.wheel;
#if defined(__EMSCRIPTEN__)
    bool mouse_unavailable = false;
#endif
    SetExitKey(KEY_NULL);
    Profiler profiler = { 0 };
    BoatProfile boat_profile = { 0 };
    BoatProfileSnapshot boat_snapshot = { 0 };
    ProfilerSnapshot snapshot = { 0 };
    VirtualSize size = { WIDTH, HEIGHT };
    double frame_started_at = boat_profile_time(true);
    double next_snapshot_time = 0;
    result = 0;
    while (!WindowShouldClose() && (!frame_limit || frames < frame_limit)) {
        double current_frame_started_at = boat_profile_time(true);
        frame_started_at = current_frame_started_at;
        BoatFrameMetrics metrics = { 0 }; // Reset once before any tick in this frame.
        int character;
        bool help_pressed = false;
        while ((character = GetCharPressed()) != 0)
            help_pressed |= character == '?';
        if (help_pressed)
            help_visible = !help_visible;
        // Escape never changes the steering mode: it opens the menu, which releases capture.
        bool menu_was_open = menu.open;
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (help_visible)
                help_visible = false;
            else if (menu.open)
                menu.open = false;
            else
                boat_menu_open(&menu);
        }
        int scale = GetScreenWidth() / WIDTH < GetScreenHeight() / HEIGHT
            ? GetScreenWidth() / WIDTH
            : GetScreenHeight() / HEIGHT;
        if (scale < 1)
            scale = 1;
        Rectangle destination = { (GetScreenWidth() - WIDTH * scale) / 2,
            (GetScreenHeight() - HEIGHT * scale) / 2, WIDTH * scale, HEIGHT * scale };
        Camera2D menu_camera = { .offset = { destination.x, destination.y }, .zoom = scale };
        if (menu.open && menu_was_open && !help_visible) {
            BoatMenuChoice choice = boat_menu_update(&menu, menu_camera, GetFrameTime());
            if (choice == BOAT_MENU_RESUME)
                menu.open = false;
            else if (choice == BOAT_MENU_CONTROLS)
                help_visible = true;
            else if (choice == BOAT_MENU_LEAVE)
                break;
            else if (choice == BOAT_MENU_STEERING) {
#if defined(__EMSCRIPTEN__)
                mouse_unavailable = false;
#endif
                mouse_steering = !mouse_steering;
                if (mouse_steering)
                    mouse_wheel = session.boat.wheel;
            }
        }
        // A click that resumes play is not also a replay-timeline click.
        bool active = IsWindowFocused() && !help_visible && !menu.open && !menu_was_open;
#if defined(__EMSCRIPTEN__)
        bool mouse_toggle_pressed = boat_web_mouse_toggle() != 0;
#else
        bool mouse_toggle_pressed = IsKeyPressed(KEY_M);
#endif
        bool toggle_mouse = active && !demo && !session.runs.playing && mouse_toggle_pressed;
        if (toggle_mouse) {
#if defined(__EMSCRIPTEN__)
            mouse_unavailable = false;
#endif
            mouse_steering = !mouse_steering;
            if (mouse_steering)
                mouse_wheel = session.boat.wheel;
        }
        bool mouse_driving = mouse_steering && !demo && !session.runs.playing;
#if defined(__EMSCRIPTEN__)
        int browser_mouse = boat_web_mouse_state(
            active && mouse_driving, active && !demo && !session.runs.playing);
        if (browser_mouse & 4) {
            // Denied or missing capture falls back to the keyboard.
            mouse_unavailable = true;
            mouse_steering = mouse_driving = false;
        } else if (browser_mouse & 2) {
            // The browser's own Escape (or focus loss) unlocked: open the menu, keep mouse mode.
            if (!menu.open)
                boat_menu_open(&menu);
            active = false;
        }
        if (browser_mouse & 6)
            browser_mouse = boat_web_mouse_state(0, active && !demo && !session.runs.playing);
        bool capture_mouse = (browser_mouse & 1) != 0;
        bool capture_changed = capture_mouse != mouse_captured;
        mouse_captured = capture_mouse;
        float mouse_delta = boat_web_mouse_movement();
        float scroll_delta = boat_web_mouse_scroll();
#else
        bool capture_mouse = active && mouse_driving;
        bool capture_changed = capture_mouse != mouse_captured;
        if (capture_changed) {
            if (capture_mouse)
                DisableCursor();
            else
                EnableCursor();
            mouse_captured = capture_mouse;
        }
        float mouse_delta = GetMouseDelta().x;
        float scroll_delta = GetMouseWheelMoveV().y;
#endif
        // Capture warps the native cursor; discard that frame's delta. Stop moving to
        // hold this target, rather than deriving steering from a screen position.
        if (active && IsKeyPressed(KEY_R))
            mouse_wheel = 0;
        else if (mouse_captured && !capture_changed)
            mouse_wheel = boat_mouse_wheel(mouse_wheel, mouse_delta);
        if (!active && networked)
            mouse_wheel = 0; // The authority neutralizes inactive controls too.
        float steering_target = mouse_driving ? mouse_wheel : NAN;
        if (active && IsKeyPressed(KEY_F3)) {
            profiler_visible = !profiler_visible;
            next_snapshot_time = 0;
        }
        if (active) {
            float dt = fminf(GetFrameTime(), 0.1f);
            int zoom = (IsKeyDown(KEY_MINUS) || IsKeyDown(KEY_KP_SUBTRACT))
                - (IsKeyDown(KEY_EQUAL) || IsKeyDown(KEY_KP_ADD));
            int elevation = IsKeyDown(KEY_PAGE_UP) - IsKeyDown(KEY_PAGE_DOWN);
            camera_settings.distance
                = fmaxf(8, fminf(48, camera_settings.distance + zoom * 12 * dt));
            camera_settings.height
                = fmaxf(4, fminf(36, camera_settings.height + elevation * 8 * dt));
            if (IsKeyPressed(KEY_C))
                camera_settings = camera_default;
        }
        if (active && !demo && !session.runs.playing) {
            int pressed = (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W))
                - (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S));
            int held = (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))
                - (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S));
            thrust_step = boat_throttle_hold(
                thrust_step, pressed, held, fminf(GetFrameTime(), 0.1f), &throttle_held);
            // Scrolling down (toward the player) opens the throttle.
            thrust_step = boat_throttle_scroll(thrust_step, -scroll_delta, &throttle_scroll);
        } else {
            throttle_scroll = throttle_held = 0;
        }
        bool replay_adjusted = false;
        if (!active)
            replay_dragging = false;
        double course_input_started_at = boat_profile_time(true);
        if (course && active) {
            if (IsKeyPressed(KEY_G))
                session.runs.ghost_visible = !session.runs.ghost_visible;
            if (session.course.next_gate == 0 || session.course.next_gate == BOAT_COURSE_GATES
                || session.runs.playing) {
                const BoatReplay *selected = IsKeyPressed(KEY_P) ? &session.runs.last
                    : IsKeyPressed(KEY_B)                        ? &session.runs.best
                                                                 : NULL;
                if (selected && boat_replay_play(&session.runs, selected)) {
                    accumulator = 0;
                    replay_dragging = false;
                    replay_adjusted = true;
                }
            }
            if (session.runs.playing) {
                double duration = session.runs.playing->splits[BOAT_COURSE_GATES - 1];
                if (IsKeyPressed(KEY_SPACE)) {
                    if (session.runs.replay_time == duration)
                        boat_replay_seek(&session.runs, 0);
                    session.runs.paused = !session.runs.paused;
                }
                if (IsKeyPressed(KEY_ENTER)) {
                    boat_replay_seek(&session.runs, 0);
                    session.runs.paused = false;
                    replay_adjusted = true;
                }
                if (IsKeyPressed(KEY_LEFT_BRACKET))
                    boat_replay_speed_step(&session.runs, -1);
                if (IsKeyPressed(KEY_RIGHT_BRACKET))
                    boat_replay_speed_step(&session.runs, 1);
                int seek = (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT))
                    - (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT));
                bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
                if (seek) {
                    boat_replay_seek(
                        &session.runs, session.runs.replay_time + seek * (shift ? 5 : 1));
                    replay_adjusted = true;
                }
                if (IsKeyPressed(KEY_HOME) || IsKeyPressed(KEY_END)) {
                    boat_replay_seek(&session.runs, IsKeyPressed(KEY_END) ? duration : 0);
                    replay_adjusted = true;
                }
                Rectangle bar = replay_timeline(GetScreenWidth());
                Rectangle hit = { bar.x - 4, bar.y - 6, bar.width + 8, bar.height + 12 };
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
                    && CheckCollisionPointRec(GetMousePosition(), hit))
                    replay_dragging = true;
                if (replay_dragging) {
                    boat_replay_seek(&session.runs, (GetMouseX() - bar.x) / bar.width * duration);
                    session.runs.paused = true;
                    replay_adjusted = true;
                    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                        replay_dragging = false;
                }
            }
        }
        metrics.course_replay_ms
            += (float)((boat_profile_time(true) - course_input_started_at) * 1000.0);
        double simulation_started_at = boat_profile_time(true);
        if (networked) {
#ifdef PROTEUS_BOAT_NET
            BoatInput input = { 0 };
            if (active)
                input = (BoatInput) {
                    .steering_rate = (float)((IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))
                        - (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))),
                };
            // No multiplayer pause: the room keeps running while help or focus is away.
            uint32_t previous_life = net->own_life;
            if (!active || IsKeyPressed(KEY_R)) {
                thrust_step = BOAT_THROTTLE_NEUTRAL;
                throttle_scroll = 0;
            }
            if (!net_study_frame(net, &session, input, boat_throttle_setting(thrust_step),
                    steering_target, active, active && IsKeyPressed(KEY_R))) {
                result = 1;
                break;
            }
            if (net_host.running && !net_host_poll(&net_host, net->failure, sizeof(net->failure))) {
                fprintf(stderr, "%s\n", net->failure);
                result = 1;
                break;
            }
            if (net->own_life != previous_life) {
                thrust_step = BOAT_THROTTLE_NEUTRAL;
                throttle_scroll = 0;
                mouse_wheel = 0;
            }
#endif
        } else if (active && IsKeyPressed(KEY_R)) {
            thrust_step = BOAT_THROTTLE_NEUTRAL;
            throttle_scroll = 0;
            boat_session_restart(
                &session, study_water(location, waves, current, crossing_wake, player_wake));
            if (wake) {
                boat_wake_player_reset(wake);
                session.world.wake_field = boat_wake_player_field(wake);
            }
            if (objects)
                object_study_reset(objects, &session, object_crossing, object_life, object_height);
            accumulator = 0;
            demo_tick = 0;
            replay_dragging = false;
        } else if (session.runs.playing) {
            accumulator = 0;
            double course_started_at = boat_profile_time(true);
            boat_replay_advance(&session.runs, replay_adjusted ? 0 : GetFrameTime(), active);
            metrics.course_replay_ms
                += (float)((boat_profile_time(true) - course_started_at) * 1000.0);
        } else if (active) {
            BoatInput input = {
                .steering_rate = (float)((IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))
                    - (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))),
            };
            double raw_active_delta = GetFrameTime();
            metrics.discarded_active_ms
                = boat_profile_discarded_seconds(raw_active_delta, true) * 1000.0f;
            accumulator += fmin(raw_active_delta, 0.1);
            while (accumulator >= BOAT_TICK_SECONDS) {
                if (demo)
                    demo_step(&session, demo_tick++, &metrics, wake, objects);
                else {
                    if (mouse_driving)
                        input.steering_rate
                            = boat_steering_rate(session.boat.wheel, steering_target);
                    input.throttle_rate = boat_throttle_rate(
                        session.boat.throttle, boat_throttle_setting(thrust_step));
                    timed_boat_step(&session, input, &metrics, wake, objects);
                }
                accumulator -= BOAT_TICK_SECONDS;
            }
        } else {
            accumulator = 0;
        }
        metrics.paused = !active || (session.runs.playing && session.runs.paused);
        metrics.simulation_ms = (float)((boat_profile_time(true) - simulation_started_at) * 1000.0);
        // Course saves wait until the frame's ticks are done.
        double flush_started_at = boat_profile_time(true);
        boat_session_flush(&session);
        metrics.course_replay_ms += (float)((boat_profile_time(true) - flush_started_at) * 1000.0);
        double audio_started_at = boat_profile_time(true);
        live_view = boat_session_view(&session);
        view = &live_view;
        view_presentation = &session.presentation;
        view_world = &session.world;
        if (session.runs.playing
            && boat_replay_sample(session.runs.playing, session.runs.replay_time, &playback,
                &playback_presentation)) {
            boat_replay_animate_flag(
                session.runs.playing, &playback, &playback_flag, &playback_presentation.flag);
            boat_world_init(
                &playback_world, session.world.resources, boat_replay_water(session.runs.playing));
            view = &playback;
            view_presentation = &playback_presentation;
            view_world = &playback_world;
        }
        live_view.skin = skin;
#ifdef PROTEUS_BOAT_NET
        if (net) {
            live_view.skin = net_study_skin(net->client.slot); // Every client agrees.
            net_study_race_view(net, &live_view);
        }
#endif
        if (view == &playback)
            playback.skin = skin;
        Vector2 flow = boat_water_current(view_world->water, view->boat.position);
        float water_speed = hypotf(view->boat.velocity.x - flow.x, view->boat.velocity.y - flow.y);
        BoatAudioInput audio_input = { .thrust = view->boat.engine_response,
            .speed = water_speed,
            .planing = view->boat.planing,
            .wave_entry = view->boat.wave_spray,
            .landing = view_presentation->landing_strength,
            .landing_serial = view_presentation->landing_serial };
        boat_audio_update(
            &audio, audio_input, active && (!session.runs.playing || !session.runs.paused));
        if (music_ready) {
            if (active && (!session.runs.playing || !session.runs.paused))
                ResumeMusicStream(music);
            else
                PauseMusicStream(music);
            UpdateMusicStream(music);
        }
        metrics.audio_ms = (float)((boat_profile_time(true) - audio_started_at) * 1000.0);
        double ghost_started_at = boat_profile_time(true);
        ghost_view = NULL;
        ghost_view_flag = NULL;
        ghost_opacity = 0;
        if (objects && objects->crossing) {
            other_view
                = (BoatViewState) { .boat = *object_study_other(objects), .time = live_view.time };
            ghost_view = &other_view;
            ghost_view_flag = &objects->other_presentation.flag;
            ghost_opacity = 1;
        }
        BoatRivals rivals = { 0 };
#ifdef PROTEUS_BOAT_NET
        if (net)
            net_study_rivals(net, live_view.time, &rivals);
#endif
        if (course && !session.runs.playing && session.runs.ghost_visible
            && session.runs.opponent.complete) {
            double fade = 1
                - fmax(0,
                    session.course.elapsed - session.runs.opponent.splits[BOAT_COURSE_GATES - 1]);
            if (fade > 0
                && boat_replay_sample(
                    &session.runs.opponent, session.course.elapsed, &ghost, NULL)) {
                boat_replay_animate_flag(&session.runs.opponent, &ghost, &ghost_flag, &ghost_cloth);
                ghost_view = &ghost;
                ghost_view_flag = &ghost_cloth;
                ghost_opacity = .5f * (float)fade;
            }
        }
        metrics.course_replay_ms += (float)((boat_profile_time(true) - ghost_started_at) * 1000.0);
        double scene_started_at = boat_profile_time(true);
        if (gpu_enabled) {
            boat_gpu_draw_scene(&gpu, view, view_presentation, view_world, camera_settings, &rivals,
                ghost_view, ghost_view_flag, ghost_opacity);
            if (gpu.failed) {
                result = 1;
                break;
            }
        } else if (!boat_draw_scene(&renderer, &cpu_view, pixels, WIDTH, HEIGHT, view,
                       view_presentation, view_world, camera_settings, &rivals, ghost_view,
                       ghost_view_flag, ghost_opacity)) {
            result = 1;
            break;
        }
        if (!gpu_enabled)
            UpdateTexture(texture, pixels);
        metrics.snapshot_render_ms = (float)((boat_profile_time(true) - scene_started_at) * 1000.0);
        if (profiler_visible && current_frame_started_at >= next_snapshot_time) {
            snapshot = profiler_snapshot(&profiler, (float)live_view.time, size, destination);
            boat_snapshot = boat_profile_snapshot(&boat_profile);
            snapshot.scene_label = location->name;
            snapshot.renderer_label = gpu_enabled ? (gpu_aa ? "GPU 4x SSAA" : "GPU") : "CPU";
            snapshot.texture_upload_kib = gpu_enabled ? (wake ? 384
                                                                : objects ? 4
                                                                          : 0)
                                                      : snapshot.framebuffer_kib;
            next_snapshot_time = current_frame_started_at + 0.25;
        }
        double present_started_at = boat_profile_time(true);
        BeginTextureMode(frame_target);
        ClearBackground((Color) { 14, 23, 32, 255 });
        DrawTexturePro(texture,
            (Rectangle) { 0, 0, gpu_enabled ? -WIDTH : WIDTH, gpu_enabled ? -HEIGHT : HEIGHT },
            (Rectangle) { 0, 0, WIDTH, HEIGHT }, (Vector2) { 0 }, 0, WHITE);
        boat_hud_draw(
            (BoatHudTelemetry) { .speed_mps = hypotf(view->boat.velocity.x, view->boat.velocity.y),
                .thrust = view->boat.throttle,
                .steering = view->boat.wheel },
            WIDTH, HEIGHT);
        if (mouse_driving && !menu.open)
            DrawText(mouse_captured ? "MOUSE / ESC: MENU" : "MOUSE / CLICK TO CAPTURE", 4, 4, 10,
                (Color) { 218, 223, 202, 255 });
#if defined(__EMSCRIPTEN__)
        if (mouse_unavailable)
            DrawText("MOUSE UNAVAILABLE / USE A/D", 4, 4, 10, (Color) { 218, 223, 202, 255 });
#endif
        EndTextureMode();
        BeginDrawing();
        ClearBackground((Color) { 14, 23, 32, 255 });
        DrawTexturePro(frame_target.texture, (Rectangle) { 0, 0, WIDTH, -HEIGHT }, destination,
            (Vector2) { 0 }, 0, WHITE);
        const char *notice = NULL;
#ifdef PROTEUS_BOAT_NET
        if (networked) {
            float fade = net_study_fade(net);
            if (fade > 0)
                DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                    Fade((Color) { 14, 23, 32, 255 }, fade));
            notice = net_study_notice(net);
        }
#endif
        boat_course_hud(&view->course, view->boat.position, GetScreenWidth());
        if (notice)
            DrawText(notice, 12, 36, 20, ORANGE);
        else if (view->boat.motion_state == BOAT_CAPSIZED)
            DrawText("CAPSIZED / R TO RESTART", 12, 36, 20, ORANGE);
        else if (view->boat.motion_state == BOAT_AIRBORNE)
            DrawText("AIRBORNE", 12, 36, 16, SKYBLUE);
        else if (view->boat.motion_state == BOAT_LANDING)
            DrawText("LANDING", 12, 36, 16, SKYBLUE);
        if (wake)
            DrawText(TextFormat("WAKE FIELD / REVIEW %d", BOAT_WAKE_SOURCE_REVISION), 12,
                (int)destination.y + (HEIGHT - 48) * scale - 16, 12, SKYBLUE);
        if (course)
            course_runs_hud(&session.runs, view, ghost_view, GetScreenWidth());
        if (networked) {
#ifdef PROTEUS_BOAT_NET
            DrawText(net_study_status(net), 12, (int)destination.y + (HEIGHT - 48) * scale - 16, 12,
                SKYBLUE);
            int feed_top = net_study_draw_roster(net, GetScreenWidth() - 12, 12);
            net_study_draw_match(net, GetScreenWidth(), GetScreenHeight(), feed_top + 8,
                active && IsKeyDown(KEY_TAB));
            net_study_draw_race(net, GetScreenWidth(), GetScreenHeight(), feed_top + 8,
                active && IsKeyDown(KEY_TAB));
#endif
        } else if (!IsWindowFocused() && !menu.open)
            DrawText("PAUSED", 12, 12, 20, RAYWHITE);
        if (profiler_visible)
            profiler_draw(&profiler, &snapshot, size);
        if (profiler_visible)
            boat_profile_draw(&boat_snapshot, 12, 355);
        if (menu.open) {
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), (Color) { 8, 15, 22, 120 });
            boat_menu_draw(&menu, menu_camera, mouse_steering, networked);
        }
        if (help_visible) {
            bool deathmatch = false, race = false;
#ifdef PROTEUS_BOAT_NET
            deathmatch = networked && net_study_deathmatch(net);
            race = networked && net_study_race(net);
#endif
            draw_help(course, location->mapped, deathmatch, race);
        }
        frame_pacer_wait(&pacer);
        EndDrawing();
        metrics.present_ms = (float)((boat_profile_time(true) - present_started_at) * 1000.0);
        metrics.frame_ms = (float)((boat_profile_time(true) - frame_started_at) * 1000.0);
        boat_profile_add(&boat_profile, metrics);
        profiler_add(&profiler, metrics.frame_ms, metrics.snapshot_render_ms, metrics.present_ms);
        ++frames;
    }
cleanup:
    study_mouse_steering = mouse_steering;
    if (objects) {
        const BoatWakeObjects *pool = object_study_wakes(objects);
        fprintf(stdout,
            "Wake objects: %llu ticks; %u live (peak %u / %d); %llu emitted, "
            "%llu expired; %zu bytes; lifetime %u ticks, crest %.2f m\n",
            (unsigned long long)pool->tick, pool->live, pool->peak_live, BOAT_OBJECT_CAPACITY,
            (unsigned long long)pool->emitted, (unsigned long long)pool->expired, sizeof(*pool),
            object_life, object_height);
    }
    if (music_ready)
        UnloadMusicStream(music);
    boat_audio_destroy(&audio);
    if (wake) {
        BoatWakePlayerReport r = boat_wake_player_report(wake);
        fprintf(stdout,
            "Wake candidate r%d (%.0f kg): %zu ticks, %zu owned bytes; source %.3f, update %.3f, "
            "query/audit "
            "%.3f ms/tick; upload %.3f ms/frame\n",
            BOAT_WAKE_SOURCE_REVISION, BOAT_WAKE_MASS, r.steps, boat_wake_player_storage(wake),
            r.source_seconds * 1000 / fmax(1, r.steps), r.update_seconds * 1000 / fmax(1, r.steps),
            r.query_audit_seconds * 1000 / fmax(1, r.steps),
            gpu.wake_upload_seconds * 1000 / fmax(1, gpu.wake_uploads));
        fprintf(stdout,
            "Wake joules: signed %.6g (+%.6g / %.6g), retained %.6g, absorbed %.6g, retired %.6g, "
            "residual %.6g; available drag %.6g\n",
            r.energy.injected, r.energy.positive_work, r.energy.negative_work, r.energy.retained,
            r.energy.absorbed, r.energy.retired, r.energy.numerical_residual, r.resistance_work);
        fprintf(
            stdout, "Wake nodal peaks: height %.6g m, slope %.6g\n", r.peak_height, r.peak_slope);
    }
    boat_wake_player_destroy(wake);
#ifdef PROTEUS_BOAT_NET
    if (net)
        net_study_close(net);
    net_host_stop(&net_host); // After leaving, so the room says goodbye to the others only.
#endif
    boat_session_flush(&session);
    boat_session_destroy(&session);
    if (window_ready) {
#if defined(__EMSCRIPTEN__)
        boat_web_mouse_state(0, 0);
#else
        if (mouse_captured)
            EnableCursor();
#endif
        if (frame_target.id)
            UnloadRenderTexture(frame_target);
        if (gpu_enabled)
            boat_gpu_destroy(&gpu);
        else if (texture.id)
            UnloadTexture(texture);
        if (window_owned)
            CloseWindow();
    }
    boat_cpu_view_destroy(&cpu_view);
    for (int i = 0; i < BOAT_LOCATION_COUNT; ++i)
        boat_world_resources_destroy(&worlds[i]);
    renderer3d_destroy(&renderer);
    free(pixels);
    return result;
invalid:
    usage();
    return 1;
}

// Single player from the title: free boating on the river, as the macOS app used to start.
static int title_single(const BoatTitleSettings *settings)
{
    char *arguments[] = { "wake_velocity", "--current", "--crossing-wake", "--location", "river",
        "--waves", "--gpu-aa", "--skin", (char *)boat_skin_name(settings->skin), "--soundtrack",
        (char *)boat_music(settings->soundtrack)->id,
        settings->mouse_steering ? "--mouse-steering" : NULL, NULL };
    int count = (int)(sizeof(arguments) / sizeof(arguments[0])) - 1 - !settings->mouse_steering;
    study_mouse_steering = settings->mouse_steering;
    return study(count, arguments);
}

#ifdef PROTEUS_BOAT_NET
// Join the address, or (HOST) start a room with the title's host settings and join that.
static int title_join(BoatTitleSettings *settings, bool host)
{
    char *arguments[16] = { "wake_velocity", "--name", settings->name, "--soundtrack",
        (char *)boat_music(settings->soundtrack)->id };
    int count = 5;
    if (host) {
        arguments[count++] = "--host";
        arguments[count++] = (char *)boat_title_host_mode(settings->host_mode);
        if (settings->host_mode != BOAT_TITLE_HOST_RACE) {
            arguments[count++] = "--location";
            arguments[count++] = (char *)boat_location(settings->host_location)->name;
        }
        if (settings->host_waves)
            arguments[count++] = "--waves";
    } else {
        arguments[count++] = "--connect";
        arguments[count++] = settings->address;
    }
    if (settings->mouse_steering)
        arguments[count++] = "--mouse-steering";
    net_study.failure[0] = 0;
    study_mouse_steering = settings->mouse_steering;
    int result = study(count, arguments);
    if (net_study.failure[0])
        snprintf(settings->message, sizeof(settings->message), "%s", net_study.failure);
    else if (result)
        snprintf(settings->message, sizeof(settings->message),
            host ? "Could not host a room." : "Could not join that address.");
    return result;
}
#endif

// A launch without arguments (including Finder) opens the title; it returns there after each
// session. Explicit options enter their session directly, keeping scripted checks unchanged.
int main(int argc, char **argv)
{
#if defined(__EMSCRIPTEN__)
    return study(argc, argv);
#else
#ifdef PROTEUS_BOAT_NET
    if (argc > 1 && !strcmp(argv[1], "--serve"))
        return boat_server_main(argc - 1, argv + 1); // A hosted room: no window, audio or assets.
#endif
    bool capture = argc == 4 && !strcmp(argv[1], "--title") && !strcmp(argv[2], "--screenshot");
    if (argc != 1 && !(argc == 2 && !strcmp(argv[1], "--title")) && !capture)
        return study(argc, argv);
    BoatTitleSettings settings;
    boat_title_load(&settings);
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(capture ? FLAG_WINDOW_HIDDEN : FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(320 * 3, 200 * 3, "Wake Velocity");
    if (!IsWindowReady())
        return 1;
    if (capture) {
        bool ok = boat_title_screenshot(&settings, 2.5, argv[3]);
        CloseWindow();
        return ok ? 0 : 1;
    }
    SetWindowMinSize(320, 200);
    SetExitKey(KEY_NULL);
    for (BoatTitleChoice choice; (choice = boat_title_run(&settings)) != BOAT_TITLE_EXIT;) {
        boat_title_save(&settings);
        if (choice == BOAT_TITLE_SINGLE && title_single(&settings))
            snprintf(settings.message, sizeof(settings.message), "Single player stopped.");
#ifdef PROTEUS_BOAT_NET
        if (choice == BOAT_TITLE_JOIN || choice == BOAT_TITLE_HOST)
            title_join(&settings, choice == BOAT_TITLE_HOST);
#else
        if (choice == BOAT_TITLE_JOIN || choice == BOAT_TITLE_HOST)
            snprintf(settings.message, sizeof(settings.message), "No multiplayer in this build.");
#endif
        settings.mouse_steering = study_mouse_steering; // Esc menu or M changes are remembered.
        if (WindowShouldClose())
            break;
    }
    boat_title_save(&settings); // Also keep settings changed just before Exit.
    CloseWindow();
    return 0;
#endif
}
