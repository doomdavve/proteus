#include "boat_assets.h"
#include "boat_audio.h"
#include "boat_course_view.h"
#include "boat_gpu.h"
#include "boat_hud.h"
#include "boat_minimap.h"
#include "boat_music.h"
#include "boat_profile.h"
#include "boat_rearview.h"
#include "boat_replay.h"
#include "boat_reset_hold.h"
#include "boat_scenery.h"
#include "boat_session.h"
#include "boat_steering.h"
#include "boat_throttle.h"
#include "boat_time_control.h"
#include "boat_ui.h"
#include "boat_voyage_view.h"
#include "wake_velocity_drive.h"
#include "wake_velocity_menu.h"
#ifdef PROTEUS_BOAT_PLUGINS
#include "wake_velocity_plugin.h"
#if !defined(__EMSCRIPTEN__)
#include "wake_velocity_bridge.h"
#endif
#endif
#ifdef PROTEUS_BOAT_NET
#include "boat_server.h"
#if !defined(__EMSCRIPTEN__)
#include "wake_velocity_host.h"
#endif
#include "wake_velocity_net.h"
#endif
#include "boat_title.h"
#include "boat_web_touch.h"
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

EM_JS(int, boat_web_viewport_size, (int axis), {
    const size = Module['boatViewportSize']();
    return axis ? size.height : size.width;
})
EM_JS(void, boat_web_fit_canvas, (void), { Module['boatFitCanvas'](); })
EM_JS(int, boat_web_touch_click, (float *position), {
    const tap = Module['boatMobile']['click']();
    if (Object.is(tap, null))
        return 0;
    if (Object.is(tap, false))
        return -1;
    HEAPF32[position >> 2] = tap.x;
    HEAPF32[(position >> 2) + 1] = tap.y;
    return 1;
})
EM_JS(void, boat_web_mobile_session, (int enabled), { Module['boatMobile']['session'](enabled); })
EM_JS(int, boat_web_mobile_menu, (void), { return Module['boatMobile']['menu'](); })
EM_JS(int, boat_web_mobile_sample, (int active), { return Module['boatMobile']['sample'](active); })
EM_JS(float, boat_web_mobile_target, (void), { return Module['boatMobile']['target'](); })
EM_JS(int, boat_web_mouse_state, (int wanted, int available),
    { return Module['boatMouse']['state'](wanted, available); })
EM_JS(int, boat_web_mouse_toggle, (void), { return Module['boatMouse']['toggle'](); })
EM_JS(float, boat_web_mouse_movement, (void), { return Module['boatMouse']['movement'](); })
EM_JS(float, boat_web_mouse_scroll, (void), { return Module['boatMouse']['scroll'](); })
EM_JS(void, boat_web_scenery_start, (const char *name, const char *path),
    { Module['boatScenery'].start(UTF8ToString(name), UTF8ToString(path)); })
EM_JS(int, boat_web_scenery_state, (const char *name),
    { return Module['boatScenery'].state(UTF8ToString(name)); })
EM_JS(float, boat_web_scenery_progress, (const char *name),
    { return Module['boatScenery'].progress(UTF8ToString(name)); })
#endif

static void usage_line(const char *option, const char *description)
{
    printf("  %-29s %s\n", option, description);
}

static void usage(void)
{
    puts("Usage: wake_velocity [MODE] [OPTIONS]\n"
         "       wake_velocity --serve [boat_server options]\n"
         "\n"
         "Starts the title with no arguments. Explicit options start a session at once.");

    puts("\nMODES");
    usage_line("(none), --title", "Title: multiplayer and settings");
    usage_line("--course", "Timed S course in the basin");
    usage_line("--replay FILE", "Watch a saved course run");
    usage_line("--ghost FILE", "Race a compatible saved run");
    usage_line("--host free|deathmatch|race", "Start a LAN room and join it");
    usage_line("--connect HOST:PORT", "Join a boat_server room");
    usage_line("--serve", "Run only a room; see --serve --help");

    puts("\nWORLD");
    usage_line("--location basin|river|outer", "Water map (default basin; title uses river)");
    usage_line("--start X,Z[,DEGREES]", "Place the boat on mapped water (heading 0 faces -Z)");
    usage_line("--skin NAME", "sweden|finland|denmark|norway|explorer|cabin");
    usage_line("", "default sweden; explorer and cabin have their own engines");
    usage_line("--soundtrack NAME", "a-little-boat|acid-wake|off (default a-little-boat)");
    usage_line("--mouse-steering", "Start with captured mouse steering");
    usage_line("--plugin DIR", "Solo voyage from a Lua plugin");
    usage_line("", "Saves reload; F5 reload, F6 undo, F7 apply migration/restart");
    usage_line("--waves", "Gentle crossing waves and a floating hull");

    puts("\nWATER AND WAKES");
    usage_line("--wake-objects", "Rideable arcade wakes (default in free boating)");
    usage_line("--wake-height M", "Crest scale 0.1..3 (default 1.2)");
    usage_line("--wake-life-ticks N", "Lifetime at 120 Hz, 240..1920 (default 1920)");
    usage_line("--wake-objects-crossing", "Basin rival sharing wakes and hull contacts");
    usage_line("--no-player-wake", "Visual-only player trail");
    usage_line("--current", "Authored current, not measured local conditions");
    usage_line("--crossing-wake", "Scripted passing boat and physical wake");
    usage_line("--player-wake", "Legacy D3 trail (research)");
    usage_line("--wake-field", "Coherent CPU field, native basin only (research)");

    puts("\nCAMERA AND DISPLAY");
    usage_line("--camera-distance N", "8..48 (default 20; river 12)");
    usage_line("--camera-height N", "4..36 (default 14; river 6)");
    usage_line("--fixed-fov", "Hold the 55-degree view instead of widening with speed");
    usage_line("--renderer gpu|cpu", "Renderer (default gpu)");
    usage_line("--gpu", "Alias for --renderer gpu");
    usage_line("--gpu-aa", "4x supersampling");
    usage_line("--no-vsync", "Pace at exactly 60 Hz");
    usage_line("--profiler", "Show the debug profiler at startup");
    usage_line("--verbose", "Log graphics/window and resource startup");

    puts("\nCAPTURES AND AUTOMATION");
    usage_line("--screenshot FILE", "Save a deterministic scripted frame");
    usage_line("", "CPU screenshots need no window; GPU ones need a context");
    usage_line("--title --screenshot FILE", "Capture the title screen");
    usage_line("--time SECONDS", "Screenshot only: 0..60, or 0..600 with --replay");
    usage_line("--frames COUNT", "Stop a smoke run after COUNT frames");
    usage_line("--demo", "Run the scripted screenshot drive in a window");

    puts("\nRECORDINGS");
    usage_line("--save-run FILE", "Export each finish");
    usage_line("--data-dir DIR", "Override personal-best storage");

    puts("\nPLUGIN AUTHORING");
    usage_line("--plugin-bridge DIR", "With --plugin, let tools/plugin_author.py live-inspect,");
    usage_line("", "stage, apply and undo revisions through the private directory");

    puts("\nMULTIPLAYER");
    usage_line("--name TEXT", "Player name");
    usage_line("--token TEXT", "Room token");
    usage_line("--port N", "Host UDP port (default 24680)");
    usage_line("", "--location and --waves configure the hosted room");
    usage_line("", "Leaving closes a room started with --host");

    puts("\nCONTROLS");
    usage_line("Left/Right, A/D", "Adjust wheel");
    usage_line("W/Up, S/Down", "Forward, reverse (release: neutral); Shift: precision");
    usage_line("M", "Capture/release mouse steering (move to turn; stop to hold)");
    usage_line("Wheel", "Keyboard springs to centre; mouse holds");
    usage_line("Arcade jumps", "Automatic aerial stabilization; steering retains yaw");
    usage_line("P, Esc", "Menu: steering, controls, leave (solo pauses, rooms run)");
    usage_line("Backspace (hold 1 s)", "Reset, restart run/passage, or a new multiplayer life");
    usage_line("+/-, PageUp/PageDown", "Camera closer/farther, higher/lower");
    usage_line("C", "Reset view");
    usage_line("F3", "Toggle the debug profiler");
    usage_line("F5 / F6 / F7", "Plugin: reload / undo / apply migration or restart");
    puts("");
    usage_line("Course", "G ghost on/off | L/B last/best replay | Space pause | Enter restart");
    usage_line("Solo", "[ / ] speed 0.25x..8x | backslash normal time (altered is practice)");
    usage_line("Replay", "Left/Right seek 1 s (Shift 5 s) | Home/End | [ / ] speed");
    usage_line("", "Click/drag the replay timeline to seek and pause; Space resumes");

    puts("\nSee docs/COMMAND_LINE.md for examples, restrictions and defaults.");
}

// Native builds load every mapped location's scenery pack at start. The browser loads only the
// river's, bundled with the page; the outer pack is fetched when a session there needs it.
static bool load_scenery(const char *assets)
{
#if defined(__EMSCRIPTEN__)
    return boat_scenery_load_location(boat_location(BOAT_LOCATION_RIVER), assets);
#else
    return boat_scenery_load_all(assets);
#endif
}

// LOCATION's scenery, loaded now if it is not yet. The browser first fetches the pack, drawing
// its progress until it arrives; false if it cannot be fetched or loaded.
static bool scenery_ready(const BoatWorldDefinition *location, const char *assets)
{
    if (!location->mapped || boat_scenery_of(location->map))
        return true;
#if defined(__EMSCRIPTEN__)
    char path[4096];
    if (!boat_scenery_path(location, assets, path, sizeof(path)))
        return false;
    boat_web_scenery_start(location->name, path);
    while (boat_web_scenery_state(location->name) == 1 && !WindowShouldClose()) {
        char text[64];
        snprintf(text, sizeof(text), "LOADING %s %d%%",
            location->id == BOAT_LOCATION_OUTER ? "OUTER RIVER" : "MAP",
            (int)(100 * boat_web_scenery_progress(location->name)));
        int scale = GetScreenHeight() / 200 > 1 ? GetScreenHeight() / 200 : 1;
        BoatUiPalette palette = boat_ui_palette();
        BeginDrawing();
        ClearBackground(palette.panel);
        boat_ui_text(text, (GetScreenWidth() - boat_ui_width(text, 8 * scale)) / 2,
            GetScreenHeight() / 2 - 4 * scale, 8 * scale, palette.text);
        EndDrawing(); // Yields to the page, which receives the pack meanwhile.
    }
    if (boat_web_scenery_state(location->name) != 2)
        return false;
#endif
    return boat_scenery_load_location(location, assets);
}

static BoatWater study_water(
    const BoatWorldDefinition *location, bool waves, bool current, bool crossing, bool player)
{
    BoatWater water = boat_world_test_water(location, waves, current, crossing);
    water.player_wake_enabled = player;
    return water; // A solo session's voyage adds its exposure; rooms keep calm water.
}

static void draw_help(
    bool course, bool voyage, bool minimap, bool deathmatch, bool race, bool networked, bool plugin)
{
    int width = GetScreenWidth(), height = GetScreenHeight();
    const char *lines[] = {
        "BOATING / KEYBOARD",
        "Left/Right or A/D: steer (keyboard mode)",
        "M: toggle mouse / keyboard steering",
        "Mouse: move to turn; stop to hold wheel",
        "In air: automatic upright assistance",
        "P/Esc: menu (steering, controls, leave)",
        "Hold W/Up: forward | S/Down: reverse",
        "Release keyboard steering: centre wheel",
        "Release thrust keys: neutral / coast",
        "Hold Shift: low-power maneuvering",
        voyage ? "Hold Backspace 1 s: back to last shelter" : "Hold Backspace 1 s: reset / restart",
        plugin ? "F5: reload plugin | F6: undo | F7: apply" : "",
        deathmatch ? "Space: water | E: stern wave | Q: torpedo | Tab: scores"
            : race ? "Hold Tab: race standings"
                   : "",
        deathmatch ? "V: toggle rearview mirror (starts hidden)" : "",
        minimap ? "N: map in corner / large chart / off" : "",
        "+/-: zoom | PgUp/PgDn: height",
        "C: reset camera | F3: profiler",
        networked ? "" : "[ / ]: time 0.25x..8x | backslash: 1x",
        course ? "Altered live time: practice, no best/export" : "",
#if defined(__EMSCRIPTEN__)
        "P: menu in fullscreen | Back to title: setup",
#endif
        course ? "G: ghost | L/B: replay last/best" : "",
        course ? "Replay: Space pause | Enter restart" : "",
        course ? "Left/Right: seek | Shift: 5 seconds" : "",
        course ? "Home/End: start/end | [ / ]: speed" : "",
        course ? "Click/drag timeline: seek and pause" : "",
        "?, P or Esc: dismiss help",
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
        boat_ui_text(lines[i], (width - boat_ui_width(lines[i], font)) / 2, y, font, RAYWHITE);
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
        boat_ui_text(TextFormat("BEST %.2fs", runs->best.splits[BOAT_COURSE_GATES - 1]), 16, 76, 16,
            (Color) { 90, 240, 255, 255 });
    if (!runs->playing && runs->opponent.complete && view->course.next_gate > 1) {
        int gate = view->course.next_gate - 1;
        boat_ui_text(
            TextFormat("Split %+.2fs", view->course.splits[gate] - runs->opponent.splits[gate]), 16,
            96, 16, RAYWHITE);
    }
    if (runs->playing) {
        double duration = runs->playing->splits[BOAT_COURSE_GATES - 1];
        boat_ui_text(TextFormat("REPLAY %s %.2gx  %.2f / %.2fs", runs->paused ? "PAUSED" : "",
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
            boat_ui_text(i == 0                  ? "S"
                    : i == BOAT_COURSE_GATES - 1 ? "F"
                                                 : TextFormat("%d", i),
                x - 3, (int)(bar.y + bar.height + 3), 10, LIGHTGRAY);
        }
        DrawCircle((int)playhead, (int)(bar.y + bar.height / 2), 3, (Color) { 90, 240, 255, 255 });
    } else {
        if (runs->notice)
            boat_ui_text(runs->notice, 16, 138, 12, RAYWHITE);
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
#if !defined(__EMSCRIPTEN__)
static NetHost net_host; // The room this session started with --host, if any.
#endif
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
    bool start_set = false;
    Vector3 start = { 0 }; // --start: simulation X, Z and heading in degrees.
    long host_port = 0; // --port: the hosted room's UDP port.
    const char *plugin_path = NULL; // --plugin: the solo voyage's Lua package.
    const char *bridge_path = NULL; // --plugin-bridge: the live tool bridge's mailbox.
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
        } else if (strcmp(argv[i], "--start") == 0 && i + 1 < argc) {
            float values[3] = { 0 };
            int count = sscanf(argv[++i], "%f,%f,%f", &values[0], &values[1], &values[2]);
            if (count < 2 || !isfinite(values[0]) || !isfinite(values[1]) || !isfinite(values[2]))
                goto invalid;
            start = (Vector3) { values[0], values[1], values[2] };
            start_set = true;
        } else if (strcmp(argv[i], "--plugin") == 0 && i + 1 < argc) {
            plugin_path = argv[++i];
        } else if (strcmp(argv[i], "--plugin-bridge") == 0 && i + 1 < argc) {
            bridge_path = argv[++i];
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
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
    NetHostOptions host_options = { 0 };
    char host_endpoint[32];
#endif
    if (host_mode) {
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
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
#if defined(__EMSCRIPTEN__)
    if (bridge_path) {
        fprintf(stderr, "--plugin-bridge is only available in native builds.\n");
        goto invalid;
    }
#endif
    if (bridge_path && (!plugin_path || screenshot)) {
        fprintf(stderr, "--plugin-bridge needs --plugin and a window (no --screenshot).\n");
        goto invalid;
    }
    if (plugin_path) {
#ifdef PROTEUS_BOAT_PLUGINS
        // Rooms and course recordings keep compiled definitions until they can carry revisions.
        if (connect_to || course || replay_path || ghost_path) {
            fprintf(stderr,
                "--plugin is solo free boating; it cannot join or host a room or use the "
                "course, replay or ghost.\n");
            goto invalid;
        }
        const BoatWorldDefinition *named = wake_plugin_location(plugin_path);
        if (!named)
            return 1;
        if (location_set && named != location) {
            fprintf(stderr, "--plugin %s belongs to --location %s.\n", plugin_path, named->name);
            goto invalid;
        }
        location = named;
#else
        fprintf(stderr, "--plugin is only available in native builds.\n");
        goto invalid;
#endif
    }
    if (start_set && (!location->mapped || course || replay_path || ghost_path || connect_to)) {
        fprintf(stderr, "--start places the boat in a mapped location's free boating.\n");
        goto invalid;
    }
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
    (void)vsync;
    (void)location_set;
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
    int width = 320, height = 200;
    Color *pixels = malloc(width * height * sizeof(*pixels));
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
    // The mapped locations' scenery packs, which every view draws (the core uses only their
    // compiled water). The browser fetches the outer pack the first time it is needed.
    char assets[4096];
    if (!boat_asset_path(assets, sizeof(assets), "") || !load_scenery(assets)) {
        fprintf(stderr, "Cannot load the map scenery from %sgeometry/.\n", assets);
        for (int i = 0; i < BOAT_LOCATION_COUNT; ++i)
            boat_world_resources_destroy(&worlds[i]);
        free(pixels);
        return 1;
    }
    Renderer3D renderer = { 0 };
    BoatCpuView cpu_view = { 0 }; // The GPU view owns its own water mesh and scenery.
    BoatGpu gpu = { 0 };
    BoatRearview mirror = { 0 };
    // Every mapped location, solo or room; hidden until N.
    BoatMinimap minimap = { 0 };
    Texture2D minimap_textures[BOAT_MINIMAP_MODES] = { 0 };
    BoatMinimapMode minimap_mode = BOAT_MINIMAP_HIDDEN;
    Texture2D texture = { 0 };
    RenderTexture2D frame_target = { 0 };
    bool window_ready = false, window_owned = false, mouse_captured = false;
    BoatAudio audio = { 0 };
    Music music = { 0 };
    bool music_ready = false;
    float thrust_target = 0;
    BoatResetHold reset_hold = { 0 };
    BoatSession session;
    boat_session_init(&session, &worlds[location->id],
        study_water(location, waves, current, crossing_wake, player_wake));
    if (course)
        boat_session_start_course(&session);
    else if (!connect_to && skin == BOAT_SKIN_EXPLORER)
        session.handling = &boat_handling_explorer;
    else if (!connect_to && skin == BOAT_SKIN_CABIN)
        session.handling = &boat_handling_cabin;
    if (start_set) {
        session.boat.position = (Vector2) { start.x, start.y };
        session.boat.heading = start.z * DEG2RAD;
    }
    session.runs.ghost_visible = true;
    session.runs.export_path = export_path;
    BoatWakePlayer *wake = NULL;
    ObjectStudy object_world = { 0 };
    ObjectStudy *objects = wake_objects ? &object_world : NULL;
    if (objects)
        object_study_reset(objects, &session, object_crossing, object_life, object_height);
    int result = 1;
#ifdef PROTEUS_BOAT_PLUGINS
    // Owns the revisions the session borrows, so it outlives the session (static, like WORLDS).
    static WakePlugin plugin;
    if (plugin_path && !wake_plugin_open(&plugin, plugin_path, &session, GetTime()))
        goto cleanup;
#if !defined(__EMSCRIPTEN__)
    static char stage_executable[2048];
    snprintf(stage_executable, sizeof(stage_executable), "%s%s", GetApplicationDirectory(),
        GetFileName(argv[0]));
    plugin.stage_executable = stage_executable;
    static WakeBridge bridge;
    if (bridge_path && !wake_bridge_open(&bridge, bridge_path, GetTime()))
        goto cleanup;
#endif
#endif
    BoatReplayFlag playback_flag = { 0 }, ghost_flag = { 0 };
#ifdef PROTEUS_BOAT_NET
#if !defined(__EMSCRIPTEN__)
    if (host_mode
        && !net_host_start(
            &net_host, &host_options, net_study.failure, sizeof(net_study.failure))) {
        fprintf(stderr, "%s\n", net_study.failure);
        goto cleanup;
    }
#endif
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
        for (int tick = 0; tick < (int)round(time / BOAT_TICK_SECONDS); ++tick) {
            if (plugin_path)
                wake_plugin_before_tick(&plugin);
            demo_step(&session, tick, NULL, wake, objects);
            if (plugin_path)
                wake_plugin_after_tick(&plugin, &session);
        }
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
        && !boat_draw_with_ghost(&renderer, &cpu_view, pixels, width, height, view,
            view_presentation, view_world, camera_settings, ghost_view, ghost_view_flag,
            ghost_opacity))
        goto cleanup;
    Image image = { pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    if (screenshot && !gpu_enabled) {
        result = ExportImage(image, screenshot) ? 0 : 1;
        goto cleanup;
    }

    if (!IsWindowReady()) {
#if defined(__EMSCRIPTEN__)
        // CSS sizing belongs to the page; mobile gameplay adapts the drawing buffer below.
        SetConfigFlags(0);
#else
        SetConfigFlags(screenshot ? FLAG_WINDOW_HIDDEN : FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
#endif
        InitWindow(width * 3, height * 3, "Wake Velocity");
        if (!IsWindowReady())
            goto cleanup;
        window_owned = true;
        SetWindowMinSize(width, height);
    }
    window_ready = true;
    if (!boat_ui_load())
        goto cleanup;
#if defined(__EMSCRIPTEN__)
    SetTargetFPS(60);
    if (!scenery_ready(location, assets)) {
        fprintf(stderr, "Cannot load the %s scenery.\n", location->name);
        goto cleanup;
    }
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
    frame_target = LoadRenderTexture(width, height);
    if (!IsRenderTextureValid(frame_target))
        goto cleanup;
    SetTextureFilter(frame_target.texture, TEXTURE_FILTER_POINT);
    if (boat_minimap_init(&minimap, location)) {
        for (int mode = BOAT_MINIMAP_CORNER; mode < BOAT_MINIMAP_MODES; ++mode) {
            minimap_textures[mode] = LoadTextureFromImage(minimap.layers[mode].frame);
            if (!IsTextureValid(minimap_textures[mode]))
                goto cleanup;
            SetTextureFilter(minimap_textures[mode], TEXTURE_FILTER_POINT);
        }
    }
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
    int time_speed_index = BOAT_TIME_NORMAL;
    int frames = 0, demo_tick = 0;
    bool replay_dragging = false;
    bool help_visible = false;
    BoatMenu menu = { .can_leave = true,
        .reset_label = networked   ? "REQUEST RESET"
            : course               ? "RESTART RUN"
            : session.voyage.route ? "RECOVER BOAT"
                                   : "RESET BOAT",
        .leave_label = host_mode ? "CLOSE ROOM"
            : networked          ? "LEAVE ROOM"
            : window_owned       ? "QUIT"
                                 : "BACK TO TITLE",
        .room_hint = host_mode ? "YOU HOST: CLOSING ENDS THE ROOM" : NULL };
#if defined(__EMSCRIPTEN__)
    menu.can_leave = !window_owned;
#endif
    float mouse_wheel = session.boat.wheel;
#if defined(__EMSCRIPTEN__)
    bool mouse_unavailable = false;
    boat_web_mobile_session(1);
#endif
    SetExitKey(KEY_NULL);
    Profiler profiler = { 0 };
    BoatProfile boat_profile = { 0 };
    BoatProfileSnapshot boat_snapshot = { 0 };
    ProfilerSnapshot snapshot = { 0 };
    VirtualSize size = { width, height };
    double frame_started_at = boat_profile_time(true);
    double next_snapshot_time = 0;
    result = 0;
    while (!WindowShouldClose() && (!frame_limit || frames < frame_limit)) {
#if defined(__EMSCRIPTEN__)
        // The browser supplies a bounded low-resolution viewport after resizing settles.
        int next_width = boat_web_viewport_size(0);
        int next_height = boat_web_viewport_size(1);
        if (gpu_enabled && (next_width != width || next_height != height)) {
            RenderTexture2D next_frame = LoadRenderTexture(next_width, next_height);
            if (!IsRenderTextureValid(next_frame)
                || !boat_gpu_resize(&gpu, next_width, next_height)) {
                if (next_frame.id)
                    UnloadRenderTexture(next_frame);
                result = 1;
                break;
            }
            UnloadRenderTexture(frame_target);
            frame_target = next_frame;
            SetTextureFilter(frame_target.texture, TEXTURE_FILTER_POINT);
            width = next_width;
            height = next_height;
            texture = gpu.target.texture;
            size = (VirtualSize) { width, height };
            SetWindowSize(width * 3, height * 3);
            boat_web_fit_canvas();
        }
#endif
        double current_frame_started_at = boat_profile_time(true);
        frame_started_at = current_frame_started_at;
        BoatFrameMetrics metrics = { 0 }; // Reset once before any tick in this frame.
        int character;
        bool help_pressed = false;
        while ((character = GetCharPressed()) != 0)
            help_pressed |= character == '?';
        if (help_pressed)
            help_visible = !help_visible;
        // Menu shortcuts release capture without changing steering mode. P preserves fullscreen.
        bool reset_requested = false;
        bool menu_was_open = menu.open;
        bool menu_pressed = IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P);
#if defined(__EMSCRIPTEN__)
        menu_pressed |= boat_web_mobile_menu() != 0;
#endif
        if (menu_pressed) {
            if (help_visible)
                help_visible = false;
            else if (menu.open)
                menu.open = false;
            else
                boat_menu_open(&menu);
        }
        int scale = GetScreenWidth() / width < GetScreenHeight() / height
            ? GetScreenWidth() / width
            : GetScreenHeight() / height;
        if (scale < 1)
            scale = 1;
        Rectangle destination = { (GetScreenWidth() - width * scale) / 2,
            (GetScreenHeight() - height * scale) / 2, width * scale, height * scale };
        Camera2D menu_camera = { .offset = { destination.x + (width - 320) * scale / 2,
                                     destination.y + (height - 200) * scale / 2 },
            .zoom = scale };
        if (menu.open && menu_was_open && !help_visible && IsWindowFocused()) {
            BoatMenuChoice choice = boat_menu_update(&menu, menu_camera, GetFrameTime());
            if (choice == BOAT_MENU_RESUME)
                menu.open = false;
            else if (choice == BOAT_MENU_CONTROLS)
                help_visible = true;
            else if (choice == BOAT_MENU_RESET) {
                reset_requested = true;
                menu.open = false;
            } else if (choice == BOAT_MENU_LEAVE)
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
#ifdef PROTEUS_BOAT_PLUGINS
        if (plugin_path && wake_plugin_editing())
            active = false;
#endif
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
        float mobile_target = NAN;
        int mobile_buttons = 0;
#if defined(__EMSCRIPTEN__)
        mobile_buttons = boat_web_mobile_sample(active && !demo && !session.runs.playing);
        mobile_target = boat_web_mobile_target();
#endif
        bool keyboard_steering
            = IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D) || IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A);
        bool mobile_driving = (mobile_buttons & 16) || isfinite(mobile_target);
        bool mouse_driving = mouse_steering && !demo && !session.runs.playing && !mobile_driving;
#if defined(__EMSCRIPTEN__)
        int browser_mouse = boat_web_mouse_state(
            active && mouse_driving, active && !demo && !session.runs.playing && !mobile_driving);
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
        (void)boat_web_mouse_scroll(); // Drain browser input; scrolling no longer applies thrust.
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
#endif
        // Capture warps the native cursor; discard that frame's delta. Stop moving to
        // hold this target, rather than deriving steering from a screen position.
        if (mouse_captured && !capture_changed)
            mouse_wheel = boat_mouse_wheel(mouse_wheel, mouse_delta);
        if (!active && networked)
            mouse_wheel = 0; // The authority neutralizes inactive controls too.
        float steering_target = mouse_driving ? mouse_wheel
            : !keyboard_steering              ? mobile_target
                                              : NAN;
        float steering_rate = keyboard_steering
            ? (float)((IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))
                  - (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)))
            : (float)(((mobile_buttons & 8) != 0) - ((mobile_buttons & 4) != 0));
        if (active && IsKeyPressed(KEY_F3)) {
            profiler_visible = !profiler_visible;
            next_snapshot_time = 0;
        }
        if (active && boat_minimap_ready(&minimap) && IsKeyPressed(KEY_N))
            minimap_mode = boat_minimap_next(minimap_mode);
#ifdef PROTEUS_BOAT_NET
        if (active && net && net_study_deathmatch(net) && IsKeyPressed(KEY_V))
            mirror.enabled = !mirror.enabled;
#endif
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
        bool precision = active && !demo && !session.runs.playing
            && (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
        thrust_target = active && !demo && !session.runs.playing
            ? boat_pedal_target(IsKeyDown(KEY_UP) || IsKeyDown(KEY_W) || (mobile_buttons & 1),
                  IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S) || (mobile_buttons & 2), precision)
            : 0;
        bool replay_adjusted = false;
        if (!active)
            replay_dragging = false;
        double course_input_started_at = boat_profile_time(true);
        if (course && active) {
            if (IsKeyPressed(KEY_G))
                session.runs.ghost_visible = !session.runs.ghost_visible;
            if (session.course.next_gate == 0 || session.course.next_gate == BOAT_COURSE_GATES
                || session.runs.playing) {
                const BoatReplay *selected = IsKeyPressed(KEY_L) ? &session.runs.last
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
        if (active && !networked) {
            if (session.runs.playing) {
                if (IsKeyPressed(KEY_BACKSLASH))
                    session.runs.replay_speed_index = BOAT_TIME_NORMAL;
            } else {
                int direction = IsKeyPressed(KEY_RIGHT_BRACKET) - IsKeyPressed(KEY_LEFT_BRACKET);
                time_speed_index = boat_time_speed_step(time_speed_index, direction);
                if (IsKeyPressed(KEY_BACKSLASH))
                    time_speed_index = BOAT_TIME_NORMAL;
            }
        }
        metrics.course_replay_ms
            += (float)((boat_profile_time(true) - course_input_started_at) * 1000.0);
        reset_requested |= boat_reset_hold_update(
            &reset_hold, IsKeyDown(KEY_BACKSPACE), active && !session.runs.playing, GetFrameTime());
        // A plugin revision changes between this frame's ticks; a new exposure restarts below.
        bool plugin_restart = false;
#ifdef PROTEUS_BOAT_PLUGINS
#if !defined(__EMSCRIPTEN__)
        // Tool requests arrive whether or not the window has focus.
        if (bridge_path)
            plugin_restart = wake_bridge_frame(&bridge, &plugin, &session, GetTime());
#endif
        if (plugin_path)
            plugin_restart |= wake_plugin_frame(&plugin, &session, active && IsKeyPressed(KEY_F5),
                active && IsKeyPressed(KEY_F6), active && IsKeyPressed(KEY_F7), GetTime());
#endif
        if (reset_requested) {
            thrust_target = 0;
            mouse_wheel = 0;
            steering_target = mouse_driving ? 0 : NAN;
        }
        double simulation_started_at = boat_profile_time(true);
        if (networked) {
#ifdef PROTEUS_BOAT_NET
            BoatInput input = { 0 };
            if (active)
                input = (BoatInput) {
                    .steering_rate = steering_rate,
                };
            // No multiplayer pause: the room keeps running while help or focus is away.
            uint32_t previous_life = net->own_life;
            if (!net_study_frame(net, &session, input, thrust_target, steering_target, active,
                    reset_requested)) {
                result = 1;
                break;
            }
#if !defined(__EMSCRIPTEN__)
            if (net_host.running && !net_host_poll(&net_host, net->failure, sizeof(net->failure))) {
                fprintf(stderr, "%s\n", net->failure);
                result = 1;
                break;
            }
#endif
            if (net->own_life != previous_life) {
                thrust_target = 0;
                mouse_wheel = 0;
            }
#endif
        } else if (reset_requested && session.voyage.route && !plugin_restart) {
            // The outing continues: back to the last shelter, clock, water and wakes running.
            thrust_target = 0;
            boat_session_recover(&session);
            if (plugin_path)
                wake_plugin_after_tick(&plugin, &session);
            accumulator = 0;
        } else if (reset_requested || plugin_restart) {
            thrust_target = 0;
            boat_session_restart(
                &session, study_water(location, waves, current, crossing_wake, player_wake));
            if (wake) {
                boat_wake_player_reset(wake);
                session.world.wake_field = boat_wake_player_field(wake);
            }
            if (objects)
                object_study_reset(objects, &session, object_crossing, object_life, object_height);
            accumulator = 0;
            if (plugin_path)
                wake_plugin_reset_objective(&plugin, &session);
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
                .steering_rate = steering_rate,
            };
            double raw_active_delta = GetFrameTime();
            metrics.discarded_active_ms
                = boat_profile_discarded_seconds(raw_active_delta, true) * 1000.0f;
            if (course && time_speed_index != BOAT_TIME_NORMAL && !session.runs.finished)
                session.runs.practice = true;
            accumulator += boat_time_frame_seconds(raw_active_delta, time_speed_index);
            while (accumulator >= BOAT_TICK_SECONDS) {
                if (plugin_path)
                    wake_plugin_before_tick(&plugin);
                if (demo)
                    demo_step(&session, demo_tick++, &metrics, wake, objects);
                else {
                    if (isfinite(steering_target))
                        input.steering_rate
                            = boat_steering_rate(session.boat.wheel, steering_target);
                    input.throttle_rate = boat_throttle_rate(session.boat.throttle, thrust_target);
                    timed_boat_step(&session, input, &metrics, wake, objects);
                }
                if (plugin_path)
                    wake_plugin_after_tick(&plugin, &session);
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
#ifdef PROTEUS_BOAT_NET
        if (net && net_study_deathmatch(net)) {
            const BoatFleet *f = boat_net_client_view(&net->client);
            const BoatCombat *c = &f->state.combat;
            for (unsigned i = 0; i < BOAT_COMBAT_BULLETS; ++i) {
                const BoatCombatObject *b = &c->bullets[i];
                if (b->owner == net->client.slot && b->born > audio_input.gun_tick)
                    audio_input.gun_tick = b->born;
            }
            for (unsigned i = 0; i < BOAT_COMBAT_TORPEDOES; ++i) {
                const BoatCombatObject *t = &c->torpedoes[i];
                if (t->born > audio_input.grenade_tick)
                    audio_input.grenade_tick = t->born;
                if (t->ended > audio_input.grenade_tick)
                    audio_input.grenade_tick = t->ended;
            }
            for (unsigned i = 0; i < BOAT_COMBAT_WAVES; ++i)
                if (c->waves[i].born > audio_input.grenade_tick)
                    audio_input.grenade_tick = c->waves[i].born;
        }
#endif
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
        bool mirror_visible = false;
#ifdef PROTEUS_BOAT_NET
        if (net) {
            net_study_rivals(net, live_view.time, &rivals);
            // The large chart takes the mirror's place while it is open.
            mirror_visible = mirror.enabled && net_study_deathmatch(net) && net->own_visible
                && minimap_mode != BOAT_MINIMAP_CHART;
        }
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
        // A joined room may select a location whose pack the browser has not fetched yet.
        if (!scenery_ready(view_world->definition, assets)) {
            fprintf(stderr, "Cannot load the %s scenery.\n", view_world->definition->name);
            result = 1;
            break;
        }
        if (gpu_enabled) {
            boat_gpu_draw_scene(&gpu, view, view_presentation, view_world, camera_settings, &rivals,
                ghost_view, ghost_view_flag, ghost_opacity);
            if (gpu.failed) {
                result = 1;
                break;
            }
        } else if (!boat_draw_scene(&renderer, &cpu_view, pixels, width, height, view,
                       view_presentation, view_world, camera_settings, &rivals, ghost_view,
                       ghost_view_flag, ghost_opacity)) {
            result = 1;
            break;
        }
        if (!gpu_enabled)
            UpdateTexture(texture, pixels);
        if (mirror_visible
            && !boat_rearview_update(
                &mirror, gpu_enabled ? &gpu : NULL, view, view_presentation, view_world, &rivals)) {
            result = 1;
            break;
        }
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
            (Rectangle) { 0, 0, gpu_enabled ? -width : width, gpu_enabled ? -height : height },
            (Rectangle) { 0, 0, width, height }, (Vector2) { 0 }, 0, WHITE);
        boat_hud_draw(
            (BoatHudTelemetry) { .speed_mps = hypotf(view->boat.velocity.x, view->boat.velocity.y),
                .thrust = view->boat.throttle,
                .steering = view->boat.wheel,
                .precision = precision,
                .reset_progress = reset_hold.seconds / BOAT_RESET_HOLD_SECONDS },
            width, height > width ? height - 80 : height);
        const Image *chart = boat_minimap_compose(&minimap, minimap_mode,
            networked ? NULL : &session.voyage, view->boat.position, view->boat.heading);
        Rectangle minimap_bounds = boat_minimap_bounds(&minimap, minimap_mode, width, height);
        if (chart) {
            Rectangle bounds = minimap_bounds;
            UpdateTexture(minimap_textures[minimap_mode], chart->data);
            DrawTexture(minimap_textures[minimap_mode], (int)bounds.x, (int)bounds.y, WHITE);
        }
#ifdef PROTEUS_BOAT_NET
        if (net)
            net_study_combat_hud(
                net, width, height, view, view_presentation, view_world, camera_settings);
#endif
        if (mirror_visible)
            boat_rearview_draw(&mirror, width);
        int hint_top = mirror_visible ? BOAT_REARVIEW_BOTTOM + 12 : 4;
        if (mouse_driving && !menu.open)
            boat_ui_text(mouse_captured ? "MOUSE / P: MENU" : "MOUSE / CLICK TO CAPTURE", 4,
                hint_top, 10, (Color) { 218, 223, 202, 255 });
        else if (!menu.open)
            boat_ui_text("P: MENU", 4, hint_top, 10, (Color) { 218, 223, 202, 255 });
#if defined(__EMSCRIPTEN__)
        if (mouse_unavailable)
            boat_ui_text("MOUSE UNAVAILABLE / A/D / P: MENU", 4, hint_top, 10,
                (Color) { 218, 223, 202, 255 });
#endif
        if (!networked && !session.runs.playing
            && (time_speed_index != BOAT_TIME_NORMAL || session.runs.practice)) {
            const char *time_label = TextFormat("TIME %.2gx%s", boat_time_rate(time_speed_index),
                session.runs.practice ? " / PRACTICE" : "");
            boat_ui_text(time_label, 4, height - 64, 10, (Color) { 233, 180, 104, 255 });
        }
        EndTextureMode();
        BeginDrawing();
        ClearBackground((Color) { 14, 23, 32, 255 });
        DrawTexturePro(frame_target.texture, (Rectangle) { 0, 0, width, -height }, destination,
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
        if (!networked)
            boat_voyage_hud(&session.voyage, &session.boat, live_view.time, GetScreenWidth());
#ifdef PROTEUS_BOAT_PLUGINS
        if (plugin_path)
            wake_plugin_draw(&plugin, 12, (int)destination.y + (height - 48) * scale - 20);
#endif
        if (notice)
            boat_ui_text(notice, 12, 36, 20, ORANGE);
        else if (view->boat.motion_state == BOAT_CAPSIZED)
            boat_ui_text("CAPSIZED / HOLD BACKSPACE 1s", 12, 36, 20, ORANGE);
        else if (view->boat.motion_state == BOAT_AIRBORNE)
            boat_ui_text("AIRBORNE", 12, 36, 16, SKYBLUE);
        else if (view->boat.motion_state == BOAT_LANDING)
            boat_ui_text("LANDING", 12, 36, 16, SKYBLUE);
        if (wake)
            boat_ui_text(TextFormat("WAKE FIELD / REVIEW %d", BOAT_WAKE_SOURCE_REVISION), 12,
                (int)destination.y + (height - 48) * scale - 16, 12, SKYBLUE);
        if (course)
            course_runs_hud(&session.runs, view, ghost_view, GetScreenWidth());
        if (networked) {
#ifdef PROTEUS_BOAT_NET
            boat_ui_text(net_study_status(net), 12, (int)destination.y + (height - 48) * scale - 16,
                12, SKYBLUE);
            int match_top
                = mirror_visible ? (int)destination.y + (BOAT_REARVIEW_BOTTOM + 2) * scale : 12;
            int roster_top = mirror_visible ? match_top + 28 : 12;
            if (minimap_mode == BOAT_MINIMAP_CORNER) // The roster continues below the map.
                roster_top = (int)fmaxf((float)roster_top,
                    destination.y + (minimap_bounds.y + minimap_bounds.height + 2) * scale);
            int feed_top = net_study_draw_roster(net, GetScreenWidth() - 12, roster_top);
            net_study_draw_match(net, GetScreenWidth(), GetScreenHeight(), match_top, feed_top + 8,
                active && IsKeyDown(KEY_TAB));
            net_study_draw_race(net, GetScreenWidth(), GetScreenHeight(), feed_top + 8,
                active && IsKeyDown(KEY_TAB));
#endif
        } else if (!IsWindowFocused() && !menu.open)
            boat_ui_text("PAUSED", 12, 12, 20, RAYWHITE);
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
            draw_help(course, !networked && session.voyage.route, boat_minimap_ready(&minimap),
                deathmatch, race, networked, plugin_path != NULL);
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
#if !defined(__EMSCRIPTEN__)
    net_host_stop(&net_host); // After leaving, so the room says goodbye to the others only.
#endif
#endif
#ifdef PROTEUS_BOAT_PLUGINS
#if !defined(__EMSCRIPTEN__)
    if (bridge_path)
        wake_bridge_close(&bridge);
#endif
    if (plugin_path)
        wake_plugin_close(&plugin);
#endif
    boat_session_flush(&session);
    boat_session_destroy(&session);
    if (window_ready) {
        boat_ui_unload();
#if defined(__EMSCRIPTEN__)
        boat_web_mouse_state(0, 0);
        boat_web_mobile_session(0);
#else
        if (mouse_captured)
            EnableCursor();
#endif
        boat_rearview_destroy(&mirror);
        for (int mode = 0; mode < BOAT_MINIMAP_MODES; ++mode)
            if (minimap_textures[mode].id)
                UnloadTexture(minimap_textures[mode]);
        if (frame_target.id)
            UnloadRenderTexture(frame_target);
        if (gpu_enabled)
            boat_gpu_destroy(&gpu);
        else if (texture.id)
            UnloadTexture(texture);
        if (window_owned)
            CloseWindow();
    }
    boat_minimap_destroy(&minimap);
    boat_cpu_view_destroy(&cpu_view);
    for (int i = 0; i < BOAT_LOCATION_COUNT; ++i)
        boat_world_resources_destroy(&worlds[i]);
    boat_scenery_unload(); // After every view that borrowed it.
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
    char *arguments[20] = { "wake_velocity", "--current", "--crossing-wake" };
    int count = 3;
    if (settings->location == BOAT_LOCATION_COUNT)
        arguments[count++] = "--course";
    else {
        arguments[count++] = "--location";
        arguments[count++] = (char *)boat_location(settings->location)->name;
    }
    if (settings->waves)
        arguments[count++] = "--waves";
    if (settings->smooth_edges)
        arguments[count++] = "--gpu-aa";
    arguments[count++] = "--skin";
    arguments[count++] = (char *)boat_skin_name(settings->skin);
    arguments[count++] = "--soundtrack";
    arguments[count++] = (char *)boat_music(settings->soundtrack)->id;
    if (settings->mouse_steering)
        arguments[count++] = "--mouse-steering";
    arguments[count] = NULL;
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
    if (settings->smooth_edges)
        arguments[count++] = "--gpu-aa";
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
#if defined(PROTEUS_BOAT_PLUGINS) && !defined(__EMSCRIPTEN__)
    if (argc == 2 && !strcmp(argv[1], "--plugin-stage-worker"))
        return boat_plugin_stage_worker();
#endif
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
    if (argc > 1 && !strcmp(argv[1], "--serve"))
        return boat_server_main(argc - 1, argv + 1); // A hosted room: no window, audio or assets.
#endif
    bool capture = argc == 4 && !strcmp(argv[1], "--title") && !strcmp(argv[2], "--screenshot");
    if (argc != 1 && !(argc == 2 && !strcmp(argv[1], "--title")) && !capture)
        return study(argc, argv);
    BoatTitleSettings settings;
    boat_title_load(&settings);
    SetTraceLogLevel(LOG_WARNING);
#if defined(__EMSCRIPTEN__)
    SetConfigFlags(0);
#else
    SetConfigFlags(capture ? FLAG_WINDOW_HIDDEN : FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
#endif
    InitWindow(320 * 3, 200 * 3, "Wake Velocity");
    if (!IsWindowReady())
        return 1;
    if (capture) {
        bool ok = boat_title_screenshot(&settings, 2.5, argv[3]);
        CloseWindow();
        return ok ? 0 : 1;
    }
#if defined(__EMSCRIPTEN__)
    SetTargetFPS(60);
    InitAudioDevice(); // Start inside the page's Play gesture, before the title yields.
#endif
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
    boat_title_directory_start();
#endif
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
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
    boat_title_directory_stop();
#endif
#if defined(__EMSCRIPTEN__)
    CloseAudioDevice();
#endif
    CloseWindow();
    return 0;
}
