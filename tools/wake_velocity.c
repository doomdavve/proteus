#if !defined(__EMSCRIPTEN__)
#include "external/glad.h"
#endif

#include "boat_assets.h"
#include "boat_audio.h"
#include "boat_capture.h"
#include "boat_course_view.h"
#include "boat_gpu.h"
#include "boat_gpu_timer.h"
#include "boat_helm.h"
#include "boat_hud.h"
#include "boat_minimap.h"
#include "boat_music.h"
#include "boat_plugin_catalog.h"
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
#include "boat_version.h"
#include "boat_voyage_view.h"
#include "cli_usage.h"
#include "wake_command.h"
#include "wake_console.h"
#include "wake_console_bridge.h"
#include "wake_velocity_app.h"
#include "wake_velocity_controls.h"
#include "wake_velocity_drive.h"
#include "wake_velocity_input.h"
#include "wake_velocity_lighting_review.h"
#include "wake_velocity_menu.h"
#include "wake_velocity_performance.h"
#include "wake_velocity_plugin.h"
#include "wake_velocity_scenery.h"
#include "wake_velocity_servers.h"
#include "wake_velocity_work.h"
#if !defined(__EMSCRIPTEN__)
#include "wake_velocity_bridge.h"
#endif
#ifdef PROTEUS_BOAT_NET
#include "boat_server.h"
#if !defined(__EMSCRIPTEN__)
#include "wake_velocity_host.h"
#include "wake_velocity_local.h"
#endif
#include "wake_velocity_net.h"
#endif
#include "boat_screen.h"
#include "boat_title.h"
#include "frame_pacer.h"
#include "profiler.h"
#include "raymath.h"
#include "rlgl.h"
#include "xm_audio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include "wake_velocity_plugin_web.h"
#include <emscripten/emscripten.h>
EM_JS(int, boat_web_mouse_state, (int wanted, int available),
    { return Module['boatMouse']['state'](wanted, available); })
EM_JS(int, boat_web_mouse_toggle, (void), { return Module['boatMouse']['toggle'](); })
EM_JS(float, boat_web_mouse_movement, (void), { return Module['boatMouse']['movement'](); })
EM_JS(float, boat_web_mouse_scroll, (void), { return Module['boatMouse']['scroll'](); })
#endif

// Draw into the game's low-resolution frame. Publish these exact hit rectangles to the
// browser, which captures each finger and delivers ordinary pedal/steering inputs to C.
static void draw_touch_controls(
    int width, int height, Rectangle destination, bool enabled, int buttons, int button_height)
{
    int state = wake_touch_state();
    if (!(state & 1))
        return;
    int steering_width = button_height > 30 ? button_height : 30;
    int menu_width = button_height > 36 ? button_height : 36;
    Rectangle bounds[8] = {
        { width - 56, height - 2 * button_height - 8, 52, button_height },
        { width - 56, height - button_height - 4, 52, button_height },
        { 4, height - button_height - 4, steering_width, button_height },
        { steering_width + 8, height - button_height - 4, steering_width, button_height },
        { 4, 4, menu_width, button_height }, // Menu, tilt, recenter, fullscreen.
        { menu_width + 8, 4, 56, button_height },
        { menu_width + 68, 4, 50, button_height },
        { menu_width + (state & 2 ? 122 : 68), 4, 66, button_height },
    };
    const char *labels[8] = { "FORWARD", "REVERSE", "LEFT", "RIGHT", "MENU",
        state & 4       ? "WAIT..."
            : state & 2 ? "USE TOUCH"
                        : "TILT",
        "RECENTER", state & 16 ? "EXIT FULL" : "FULLSCREEN" };
    if (!(state & 2))
        bounds[6].width = 0;
    if (!(state & 8))
        bounds[7].width = 0;
    float regions[8][4];
    BoatUiPalette palette = boat_ui_palette();
    for (int i = 0; i < 8; ++i) {
        Rectangle box = bounds[i];
        regions[i][0] = (destination.x + box.x * destination.width / width) / GetScreenWidth();
        regions[i][1] = (destination.y + box.y * destination.height / height) / GetScreenHeight();
        regions[i][2] = box.width * destination.width / width / GetScreenWidth();
        regions[i][3] = box.height * destination.height / height / GetScreenHeight();
        if (!box.width)
            continue;
        bool held = i < 4 && (buttons & (1 << i));
        Color fill = held ? (Color) { 66, 103, 121, 235 } : (Color) { 20, 38, 47, 210 };
        DrawRectangleRec(box, fill);
        DrawRectangleLinesEx(box, 1, palette.edge);
        Color text = i < 4 && !enabled ? palette.hint : palette.text;
        boat_ui_text(labels[i], (int)(box.x + (box.width - boat_ui_width(labels[i], 10)) / 2),
            (int)box.y + (button_height - 10) / 2, 10, text);
    }
    wake_touch_layout(&regions[0][0]);
    char status[128];
    wake_touch_status(status, sizeof(status));
    boat_ui_text(status, 4, button_height + 7, 10, palette.text);
}

static void usage(void)
{
    puts("Usage: wake_velocity [MODE] [OPTIONS]\n"
         "       wake_velocity --serve [boat_server options]\n"
         "\n"
         "Starts the title with no arguments. Explicit options start a session at once.");

    cli_usage_section(stdout, "MODES");
    cli_usage_line(stdout, "(no arguments)", "Open the title screen: single player, multiplayer,");
    cli_usage_line(stdout, "", "settings and credits");
    cli_usage_line(stdout, "--title", "Open the title screen explicitly");
    cli_usage_line(
        stdout, "--title-start PLACE", "Start a PLACE as the title's START does (tests)");
    cli_usage_line(stdout, "--work", "Run the location's working-harbour package (outer)");
    cli_usage_line(stdout, "--course", "Timed S course in the basin");
    cli_usage_line(stdout, "--replay FILE", "Watch a saved course run");
    cli_usage_line(stdout, "--ghost FILE", "Race a compatible saved run");
    cli_usage_line(
        stdout, "--host MODE", "Start a LAN room (free, deathmatch, race, hunt) and join it");
    cli_usage_line(stdout, "--connect HOST:PORT", "Join a boat_server room");
    cli_usage_line(stdout, "--serve", "Run only a room; see --serve --help");

    cli_usage_section(stdout, "WORLD");
    cli_usage_line(stdout, "--location basin|outer", "Water map (default basin; title uses outer)");
    cli_usage_line(
        stdout, "--start X,Z[,DEGREES]", "Place the boat on mapped water (heading 0 faces -Z)");
    cli_usage_line(stdout, "--skin NAME", "sweden|finland|denmark|norway|explorer|cabin|grodan");
    cli_usage_line(stdout, "", "default sweden; explorer and cabin have their own engines");
    cli_usage_line(
        stdout, "--soundtrack NAME", "a-little-boat|acid-wake|off (default a-little-boat)");
    cli_usage_line(stdout, "--mouse-steering", "Start with captured mouse steering");
    cli_usage_line(stdout, "--plugin DIR", "Solo voyage from a Lua plugin");
    cli_usage_line(stdout, "", "Saves reload; F5 reload, F6 undo, F7 apply migration/restart");
    cli_usage_line(
        stdout, "--package ID", "Bundled solo package (title content row selects outer)");
    cli_usage_line(stdout, "--waves", "Gentle crossing waves and a floating hull");

    cli_usage_section(stdout, "WATER AND WAKES");
    cli_usage_line(stdout, "--wake-objects", "Rideable arcade wakes (default in free boating)");
    cli_usage_line(stdout, "--wake-height M", "Crest scale 0.1..3 (default 1.2)");
    cli_usage_line(stdout, "--wake-life-ticks N", "Lifetime at 120 Hz, 240..1920 (default 1920)");
    cli_usage_line(
        stdout, "--wake-objects-crossing", "Basin rival sharing wakes and hull contacts");
    cli_usage_line(stdout, "--no-player-wake", "Visual-only player trail");
    cli_usage_line(stdout, "--current", "Authored current, not measured local conditions");
    cli_usage_line(stdout, "--crossing-wake", "Scripted passing boat and physical wake");
    cli_usage_line(stdout, "--player-wake", "Legacy D3 trail (research)");
    cli_usage_line(stdout, "--wake-field", "Coherent CPU field, native basin only (research)");

    cli_usage_section(stdout, "CAMERA AND DISPLAY");
    cli_usage_line(stdout, "--camera-distance N", "8..48 (default 20; outer 12)");
    cli_usage_line(stdout, "--camera-height N", "4..36 (default 14; outer 6)");
    cli_usage_line(stdout, "--fixed-fov", "Hold the 55-degree view instead of widening with speed");
    cli_usage_line(stdout, "--renderer gpu|cpu", "Renderer (default gpu)");
    cli_usage_line(stdout, "--gpu", "Alias for --renderer gpu");
    cli_usage_line(stdout, "--gpu-aa", "4x supersampling");
    cli_usage_line(stdout, "--resolution low|high",
        "Scene pixels: 320x200 (default) or up to 1280x800; high is GPU only, without --gpu-aa");
    cli_usage_line(stdout, "--hdr on|off",
        "HDR scene with restrained bloom and grade (default: on at high resolution)");
    cli_usage_line(stdout, "--daylight day|sunset|dusk", "Fixed lighting (default day)");
    cli_usage_line(
        stdout, "--lighting-hour H", "Fixed full-day hour override; --time stays drive time");
    cli_usage_line(
        stdout, "--no-far-detail", "Draw distant scenery at full detail, for comparison");
    cli_usage_line(stdout, "--no-vsync", "Pace at exactly 60 Hz");
    cli_usage_line(stdout, "--profiler", "Show the debug profiler at startup");
    cli_usage_line(stdout, "--performance-report PATH",
        "Write a performance bundle at exit (PATH must not exist)");
    cli_usage_line(stdout, "--no-performance-capture", "Disable the rolling live capture");
    cli_usage_line(stdout, "--verbose", "Log graphics/window and resource startup");

    cli_usage_section(stdout, "CAPTURES AND AUTOMATION");
    cli_usage_line(stdout, "--screenshot FILE", "Save a deterministic scripted frame");
    cli_usage_line(stdout, "", "CPU screenshots need no window; GPU ones need a context");
    cli_usage_line(stdout, "--title --screenshot FILE [OPTIONS]", "Capture the title screen");
    cli_usage_line(stdout, "--solo --ask --content NAME --save-mode MODE --data-dir DIR",
        "Title capture options; --save-mode is continue, new or delete");
    cli_usage_line(stdout, "--time SECONDS", "Screenshot only: 0..60, or 0..600 with --replay");
    cli_usage_line(
        stdout, "--time-scale N", "Initial solo live speed 0.25, 0.5, 1, 2, 4 or 8 (default 1)");
    cli_usage_line(stdout, "--frames COUNT", "Stop a smoke run after COUNT frames");
    cli_usage_line(
        stdout, "--frame-capture FILE", "Save the window, HUD included, at the last of --frames");
    cli_usage_line(stdout, "--demo", "Run the scripted screenshot drive in a window");
    cli_usage_line(stdout, "--work-clock-report FILE",
        "Developer review: with --work and --frames, write a live lighting CSV");
    cli_usage_line(stdout, "--work-review-pose swing|deck|approach",
        "Developer review: bounded --work pose for lighting inspection");

    cli_usage_section(stdout, "RECORDINGS");
    cli_usage_line(stdout, "--save-run FILE", "Export each finish");
    cli_usage_line(stdout, "--data-dir DIR", "Override personal-best storage");

    cli_usage_section(stdout, "PLUGIN AUTHORING");
    cli_usage_line(
        stdout, "--plugin-bridge DIR", "With --plugin, let tools/plugin_author.py live-inspect,");
    cli_usage_line(stdout, "", "stage, apply and undo revisions through the private directory");

    cli_usage_section(stdout, "MULTIPLAYER");
    cli_usage_line(stdout, "--name TEXT", "Player name");
    cli_usage_line(stdout, "--token TEXT", "Room token");
    cli_usage_line(stdout, "--port N", "Host UDP port (default 24680)");
    cli_usage_line(
        stdout, "", "--location and --waves configure the hosted room (race and hunt: outer)");
    cli_usage_line(stdout, "", "Leaving closes a room started with --host");

    cli_usage_section(stdout, "CONTROLS");
    cli_usage_line(stdout, "Left/Right, A/D", "Adjust wheel");
    cli_usage_line(stdout, "W/Up, S/Down", "Forward, reverse (release: neutral); Shift: precision");
    cli_usage_line(stdout, "M", "Capture/release mouse steering (move to turn; stop to hold)");
    cli_usage_line(stdout, "Wheel", "Keyboard springs to centre; mouse holds");
    cli_usage_line(stdout, "Arcade jumps", "Automatic aerial stabilization; steering retains yaw");
    cli_usage_line(stdout, "P, Esc", "Menu: steering, controls, leave (solo pauses, rooms run)");
    cli_usage_line(
        stdout, "Backspace (hold 1 s)", "Reset, restart run/passage, or a new multiplayer life");
    cli_usage_line(stdout, "+/-, PageUp/PageDown", "Camera closer/farther, higher/lower");
    cli_usage_line(stdout, "C", "Reset view");
    cli_usage_line(stdout, "F3", "Toggle the debug profiler");
    cli_usage_line(stdout, "F4", "Export the recent performance report");
    cli_usage_line(stdout, "F5 / F6 / F7", "Plugin: reload / undo / apply migration or restart");
    cli_usage_line(stdout, "F2 or `", "Live console: edits apply without a reset; /help, /save");
    puts("");
    cli_usage_line(
        stdout, "Course", "G ghost on/off | L/B last/best replay | Space pause | Enter restart");
    cli_usage_line(
        stdout, "Solo", "[ / ] speed 0.25x..8x | backslash normal time (altered is practice)");
    cli_usage_line(stdout, "Replay", "Left/Right seek 1 s (Shift 5 s) | Home/End | [ / ] speed");
    cli_usage_line(stdout, "", "Click/drag the replay timeline to seek and pause; Space resumes");

    puts("\nSee docs/COMMAND_LINE.md for examples, restrictions and defaults.");
}

// LOCATION's scenery, loaded now if it is not yet. Acquisition (wake_velocity_scenery.h) is
// immediate in native builds, which read every pack at start; the browser fetches the outer pack
// when a session there first needs it, and this draws its progress until it arrives. The river
// kit's layout pack is bundled with the page. False if it cannot be acquired or loaded.
static bool scenery_ready(const BoatWorldDefinition *location, const char *assets)
{
    if (!location->mapped || boat_scenery_of(location->map))
        return true;
    if (!wake_scenery_request(location, assets))
        return false;
    float progress = 0;
    WakeSceneryStatus status;
    while ((status = wake_scenery_poll(location, &progress)) == WAKE_SCENERY_PENDING
        && !WindowShouldClose()) {
        char text[64];
        snprintf(text, sizeof(text), "LOADING %s %d%%", location->title, (int)(100 * progress));
        int scale = GetScreenHeight() / 200 > 1 ? GetScreenHeight() / 200 : 1;
        BoatUiPalette palette = boat_ui_palette();
        BeginDrawing();
        ClearBackground(palette.panel);
        boat_ui_text(text, (GetScreenWidth() - boat_ui_width(text, 8 * scale)) / 2,
            GetScreenHeight() / 2 - 4 * scale, 8 * scale, palette.text);
        EndDrawing(); // Yields to the page, which receives the pack meanwhile.
    }
    if (status != WAKE_SCENERY_READY)
        return false;
    return boat_scenery_load_location(location, assets);
}

static BoatWater study_water(
    const BoatWorldDefinition *location, bool waves, bool current, bool crossing, bool player)
{
    BoatWater water = boat_world_test_water(location, waves, current, crossing);
    water.player_wake_enabled = player;
    return water; // A solo session's voyage adds its exposure; rooms keep calm water.
}

static void draw_help(bool course, bool voyage, bool minimap, bool deathmatch, bool race,
    bool networked, bool plugin, bool work)
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
        work         ? "Work: TAB crane/helm | F hook | L lash | G moor"
            : voyage ? "Hold Backspace 1 s: back to last shelter"
                     : "Hold Backspace 1 s: reset / restart",
        work ? "Crane: A/D slew | W/S reach | Q/E hoist | 1/2/3 aim" : "",
        work ? "J: next site | Space: brake | menu: return crate" : "",
        plugin ? "F5: reload plugin | F6: undo | F7: apply" : "",
        plugin ? "F2 or `: live console (/help, /save)" : "",
        deathmatch ? "Space: water | E: stern wave | Q: torpedo | Tab: scores"
            : race ? "Hold Tab: race standings"
                   : "",
        deathmatch ? "V: toggle rearview mirror (starts hidden)" : "",
        minimap ? "N: map in corner / large chart / off" : "",
        "+/-: zoom | PgUp/PgDn: height",
        "C: reset camera | F3: profiler | F4: performance report",
        networked ? "" : "[ / ] or , / .: time 0.25x..8x | \\ or 0: 1x",
        "F11: fullscreen",
        course ? "Altered live time: practice, no best/export" : "",
#if defined(__EMSCRIPTEN__)
        "P: menu in fullscreen | Back to title: setup",
#endif
        course ? "G: ghost | L/B: replay last/best" : "",
        course ? "Replay: Space pause | Enter restart" : "",
        course ? "Left/Right: seek | Shift: 5 seconds" : "",
        course ? "Home/End: start/end | [ / ] or , / .: speed" : "",
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
// The latest session launch's diagnostic, for the title's message line.
static char study_failure[192];

// One session with explicit options. Uses the title's window when one is open and then leaves it
// open; the Escape menu's last row ends the session.
static bool console_save(
    void *context, const WakePlugin *plugin, const char *directory, char *message, size_t capacity)
{
    (void)context;
#if defined(__EMSCRIPTEN__)
    (void)directory;
    WakePlugin *mutable_plugin = (WakePlugin *)plugin;
    if (!plugin->authoring && !wake_plugin_promote(mutable_plugin)) {
        snprintf(message, capacity, "cannot export this package");
        return false;
    }
    const char *manifest = NULL, *source = NULL;
    size_t manifest_size = wake_plugin_manifest(plugin, &manifest);
    size_t source_size = wake_plugin_source(plugin, &source);
    if (!manifest_size || !source_size
        || !wake_web_plugin_export((const unsigned char *)manifest, (unsigned)manifest_size,
            (const unsigned char *)source, (unsigned)source_size)) {
        snprintf(message, capacity, "the browser export is unavailable");
        return false;
    }
    snprintf(message, capacity, "EXPORTED voyage.wake-plugin.json");
    return true;
#else
    return wake_plugin_save(plugin, directory, message, capacity);
#endif
}

// The personal best of the session's course, from DATA_DIR or the default data directory.
static void select_course_best(BoatSession *session, const char *data_dir, bool waves)
{
    char directory[1800];
    session->runs.best_path[0] = 0;
    if (!data_dir && !boat_replay_data_directory(directory, sizeof(directory))) {
        boat_replay_destroy(&session->runs.best);
        return;
    }
    if (!boat_replay_runs_select_best(&session->runs, data_dir ? data_dir : directory, waves,
            boat_course_digest(session->course_definition)))
        fprintf(stderr, "%s: %s\n", session->runs.notice, session->runs.best_path);
}

typedef struct {
    BoatCapture *capture;
    WakeLightingReview *lighting;
} PerformanceQueryContext;

static void performance_query_complete(void *context, unsigned long long frame, double milliseconds)
{
    PerformanceQueryContext *query = context;
    if (query->lighting)
        wake_lighting_review_gpu(query->lighting, frame, milliseconds);
    BoatCaptureSample *sample = boat_capture_sample(query->capture, frame);
    if (!sample)
        return; // The sample left the rolling window before its query completed.
    sample->gpu_ms = (float)milliseconds;
    sample->has_gpu = true;
    sample->gpu_pending = false;
}

// Snapshot the retained samples before writing, and never wait for a pending GPU query: the
// export itself is outside the measured window and cannot become the hitch it describes.
static bool export_performance(BoatCapture *capture, const WakePerformanceInfo *info,
    const char *destination, char *notice, size_t notice_size)
{
    char written[4096], error[512];
    bool ok = wake_performance_export(
        capture, info, destination, written, sizeof(written), error, sizeof(error));
    if (ok)
        snprintf(notice, notice_size, "Performance report: %s (F4 again for another)", written);
    else
        snprintf(notice, notice_size, "Performance report failed: %s", error);
    return ok;
}

// Graphics review (Batch B): F8 steps back through the water layers on the GPU renderer.
static const char *const water_layer_names[] = { "WATER: ALL", "WATER: NO FAR LIFE",
    "WATER: G3 ONLY", "WATER: STAMP ONLY", "WATER: ANALYTIC WAKES" };
#define WATER_LAYERS (int)(sizeof(water_layer_names) / sizeof(*water_layer_names))

static void apply_water_layer(BoatGpu *gpu, int layer)
{
    waterfront_gpu_scene_set_far_water_enabled(&gpu->scene, layer < 1);
    waterfront_gpu_scene_set_water_atlas_enabled(&gpu->scene, layer < 2);
    waterfront_gpu_scene_set_scene_depth_enabled(&gpu->scene, layer < 2);
    waterfront_gpu_scene_set_water_history_enabled(&gpu->scene, layer < 3);
    waterfront_gpu_scene_set_wake_stamp_enabled(&gpu->scene, layer < 4);
}

// The performance capture's preset: default, gpu-aa, high (with HDR), high-ldr or hdr.
static const char *presentation_preset(bool aa, BoatResolution resolution, const BoatGpu *gpu)
{
    bool hdr = boat_gpu_hdr_active(gpu);
    return aa               ? "gpu-aa"
        : resolution && hdr ? "high"
        : resolution        ? "high-ldr"
        : hdr               ? "hdr"
                            : "default";
}

// Size the GPU scene and its reflection for LAYOUT; failure keeps the previous targets.
static bool apply_screen_layout(BoatGpu *gpu, BoatScreenLayout layout)
{
    return boat_gpu_resize(gpu, layout.scene_width, layout.scene_height)
        && waterfront_gpu_scene_set_reflection_size(
            &gpu->scene, layout.reflection_width, layout.reflection_height);
}

static int study(int argc, char **argv)
{
    WakeLightingReview lighting_review = { 0 };
    const char *screenshot = NULL;
    unsigned int skin = BOAT_SKIN_SWEDEN;
    unsigned soundtrack = BOAT_MUSIC_LITTLE_BOAT;
    bool skin_set = false;
    const char *replay_path = NULL, *ghost_path = NULL, *data_dir = NULL, *export_path = NULL;
    float time = 0;
    BoatCamera camera_settings = BOAT_CAMERA_DEFAULT;
    bool camera_distance_set = false, camera_height_set = false;
    bool time_set = false, demo = false, mouse_steering = false;
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
    // D8: a developer-only switch back to the direct free-boating path (Debug builds only, so
    // never in a release); AR7 removes it with that path.
    bool direct_session = false;
#endif
    const BoatWorldDefinition *location = boat_location(BOAT_LOCATION_BASIN);
    bool course = false, waves = false;
    bool current = false, crossing_wake = false, player_wake = true;
    bool player_wake_set = false, wake_field = false;
    bool wake_objects = false, object_crossing = false, object_tuning = false;
    unsigned object_life = BOAT_OBJECT_LIFE_TICKS;
    float object_height = BOAT_OBJECT_DEFAULT_HEIGHT;
    bool gpu_enabled = true, gpu_aa = false, far_detail = true;
    BoatResolution resolution = BOAT_RESOLUTION_LOW;
    int hdr_option = -1; // --hdr; unset follows the resolution.
    float daylight_phase = WATERFRONT_DAYLIGHT_DAY;
    float lighting_hour = 0;
    bool lighting_hour_set = false;
    bool daylight_set = false;
    int time_speed_index = BOAT_TIME_NORMAL; // Solo live play; --time-scale sets its start.
    bool time_scale_set = false;
    bool profiler_visible = false;
    const char *performance_report = NULL;
    bool performance_capture = true;
    bool vsync = true;
    bool verbose = false;
    int frame_limit = 0;
    const char *frame_capture = NULL; // Saved from the window at the last of --frames.
    const char *connect_to = NULL, *net_name = "player", *net_token = "";
    bool networked = false; // Joined a room with --connect (native builds only).
    const char *host_mode = NULL; // --host: the room this session starts and joins.
    bool location_set = false;
    bool start_set = false;
    Vector3 start = { 0 }; // --start: simulation X, Z and heading in degrees.
    long host_port = 0; // --port: the hosted room's UDP port.
    const char *plugin_path = NULL; // --plugin: the solo voyage's Lua package.
    const char *package_id = NULL; // --package: a bundled catalog entry in runtime mode.
    const char *bridge_path = NULL; // --plugin-bridge: the live tool bridge's mailbox.
    bool work_mode = false; // --work: the location's working-harbour session (SV1f).
    const char *save_path = NULL; // --save: the work session's file (default: the data directory).
    bool fresh_work = false, save_now = false; // --new ignores a save; --save-now (tests).
    const char *work_clock_report = NULL; // --work-clock-report: developer lighting CSV.
    const char *work_review_pose_name = NULL; // --work-review-pose: developer bounded fixture.
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
            if (!location && !strcmp(argv[i], "river")) {
                fprintf(
                    stderr, "The river location is retired; --location outer holds its water.\n");
                return 1;
            }
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
        } else if (strcmp(argv[i], "--resolution") == 0 && i + 1 < argc) {
            if (!boat_resolution_parse(argv[++i], &resolution))
                goto invalid;
        } else if (strcmp(argv[i], "--hdr") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(value, "on") && strcmp(value, "off"))
                goto invalid;
            hdr_option = !strcmp(value, "on");
        } else if (strcmp(argv[i], "--daylight") == 0 && i + 1 < argc) {
            if (!waterfront_daylight_parse(argv[++i], &daylight_phase))
                goto invalid;
            daylight_set = true;
        } else if (strcmp(argv[i], "--lighting-hour") == 0 && i + 1 < argc) {
            if (!waterfront_hour_parse(argv[++i], &lighting_hour))
                goto invalid;
            lighting_hour_set = true;
        } else if (strcmp(argv[i], "--no-far-detail") == 0) {
            far_detail = false;
        } else if (strcmp(argv[i], "--profiler") == 0) {
            profiler_visible = true;
        } else if (strcmp(argv[i], "--performance-report") == 0 && i + 1 < argc) {
            performance_report = argv[++i];
            if (!*performance_report)
                goto invalid;
        } else if (strcmp(argv[i], "--no-performance-capture") == 0) {
            performance_capture = false;
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
        } else if (strcmp(argv[i], "--package") == 0 && i + 1 < argc) {
            package_id = argv[++i];
        } else if (strcmp(argv[i], "--plugin-bridge") == 0 && i + 1 < argc) {
            bridge_path = argv[++i];
        } else if (strcmp(argv[i], "--work") == 0) {
            work_mode = true;
        } else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) {
            save_path = argv[++i];
        } else if (strcmp(argv[i], "--new") == 0) {
            fresh_work = true;
        } else if (strcmp(argv[i], "--save-now") == 0) {
            save_now = true;
        } else if (strcmp(argv[i], "--work-clock-report") == 0 && i + 1 < argc) {
            work_clock_report = argv[++i];
            if (!work_clock_report[0])
                goto invalid;
        } else if (strcmp(argv[i], "--work-review-pose") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(value, "swing") && strcmp(value, "deck") && strcmp(value, "approach"))
                goto invalid;
            work_review_pose_name = value;
        } else if (strcmp(argv[i], "--demo") == 0) {
            demo = true;
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__) && !defined(NDEBUG)
        } else if (strcmp(argv[i], "--direct-session") == 0) {
            direct_session = true;
#endif
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
        } else if (strcmp(argv[i], "--time-scale") == 0 && i + 1 < argc) {
            char *end;
            const char *value = argv[++i];
            double parsed = strtod(value, &end);
            if (end == value || *end || !isfinite(parsed))
                goto invalid;
            int matched = -1;
            for (int index = 0; index <= BOAT_TIME_MAX_INDEX; ++index)
                if (parsed == (double)boat_time_rate(index))
                    matched = index;
            if (matched < 0)
                goto invalid;
            time_speed_index = matched;
            time_scale_set = true;
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
                && strcmp(host_mode, "race") != 0 && strcmp(host_mode, "hunt") != 0)
                goto invalid;
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            net_name = argv[++i];
        } else if (strcmp(argv[i], "--token") == 0 && i + 1 < argc) {
            net_token = argv[++i];
        } else if (strcmp(argv[i], "--frame-capture") == 0 && i + 1 < argc) {
            frame_capture = argv[++i];
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
    if ((time_set && !screenshot && !save_now) || (screenshot && frame_limit)
        || (!replay_path && time > 60))
        goto invalid;
    if (time_scale_set && (connect_to || host_mode)) {
        fprintf(stderr, "--time-scale is for solo live play; a room keeps its own time.\n");
        goto invalid;
    }
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
        bool fixed = !strcmp(host_mode, "race") || !strcmp(host_mode, "hunt");
        if (fixed && location_set && location->id != BOAT_LOCATION_OUTER) {
            fprintf(stderr, "--host %s runs on the outer location.\n", host_mode);
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
    if ((!course && ((data_dir && !work_mode) || export_path)) || (replay_path && ghost_path))
        goto invalid;
#if defined(__EMSCRIPTEN__)
    if (bridge_path) {
        fprintf(stderr, "--plugin-bridge is only available in native builds.\n");
        goto invalid;
    }
    if (work_clock_report) {
        fprintf(stderr, "--work-clock-report is only available in native builds.\n");
        goto invalid;
    }
#endif
    if (bridge_path && (!plugin_path || screenshot)) {
        fprintf(stderr, "--plugin-bridge needs --plugin and a window (no --screenshot).\n");
        goto invalid;
    }
    if ((work_clock_report || work_review_pose_name) && !work_mode) {
        fprintf(stderr, "--work-clock-report and --work-review-pose belong to --work.\n");
        goto invalid;
    }
    if (work_clock_report && (!frame_limit || screenshot || performance_report)) {
        fprintf(stderr,
            "--work-clock-report needs --work and --frames, with no --screenshot or "
            "--performance-report.\n");
        goto invalid;
    }
    if (work_review_pose_name && !screenshot && !frame_limit) {
        fprintf(stderr, "--work-review-pose needs a bounded --screenshot or --frames run.\n");
        goto invalid;
    }
    if ((save_path || fresh_work || save_now) && !work_mode) {
        fprintf(stderr, "--save, --new and --save-now belong to --work.\n");
        goto invalid;
    }
    if (save_now && (!(save_path || data_dir) || screenshot || frame_limit || demo)) {
        fprintf(stderr, "--save-now needs --save or --data-dir and no screenshot or demo.\n");
        goto invalid;
    }
    if (work_mode
        && (plugin_path || package_id || connect_to || host_mode || course || replay_path
            || ghost_path || wake_field || wake_objects || player_wake_set || current
            || crossing_wake || waves || start_set || skin_set || bridge_path)) {
        fprintf(stderr,
            "--work runs the location's working-harbour package; it cannot be combined with "
            "package, room, course, replay, wake, water, start, skin or bridge options.\n");
        goto invalid;
    }
    if (plugin_path && package_id) {
        fprintf(stderr, "--plugin is the file-based authoring path; it cannot take --package.\n");
        goto invalid;
    }
    if (package_id && connect_to) {
        fprintf(stderr, "--package applies to solo sessions; rooms use their own package.\n");
        goto invalid;
    }
    if (plugin_path) {
        // Rooms carry no package revisions, and recordings pin the standard course.
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
    }
    // Every solo session starts from a build-time catalog entry. Explicit --package IDs must
    // name this location's kind. The browser builds the same catalog into the game Wasm.
    const BoatPluginCatalogEntry *catalog = NULL;
    if (!plugin_path && !connect_to && !work_mode) {
        BoatPluginKind kind = course              ? BOAT_PLUGIN_KIND_COURSE
            : location->id == BOAT_LOCATION_OUTER ? BOAT_PLUGIN_KIND_VOYAGE
                                                  : BOAT_PLUGIN_KIND_FREE;
        catalog = package_id ? boat_plugin_catalog_find(package_id)
                             : boat_plugin_catalog_default(kind, location->name);
        if (!catalog) {
            fprintf(stderr,
                package_id ? "--package %s is not a bundled package.\n"
                           : "No bundled package is available for %s %s.\n",
                package_id ? package_id : boat_plugin_kind_name(kind), location->name);
            return 1;
        }
        BoatPluginKind found = boat_plugin_kind_parse(catalog->kind);
        if (strcmp(catalog->location, location->name)) {
            fprintf(
                stderr, "--package %s belongs to --location %s.\n", catalog->id, catalog->location);
            return 1;
        }
        if (found != kind) {
            fprintf(stderr, "--package %s is a %s package; this session needs %s.\n", catalog->id,
                catalog->kind, boat_plugin_kind_name(kind));
            return 1;
        }
    }
    if (start_set && (!location->mapped || course || replay_path || ghost_path || connect_to)) {
        fprintf(stderr, "--start places the boat in a mapped location's free boating.\n");
        goto invalid;
    }
    if (frame_capture && (!frame_limit || screenshot)) {
        fprintf(stderr, "--frame-capture needs --frames and no --screenshot.\n");
        goto invalid;
    }
    if (resolution == BOAT_RESOLUTION_HIGH && (!gpu_enabled || gpu_aa)) {
        fprintf(stderr, "--resolution high needs the GPU renderer and replaces --gpu-aa.\n");
        goto invalid;
    }
    bool hdr = hdr_option < 0 ? resolution == BOAT_RESOLUTION_HIGH : hdr_option;
    if (hdr && (!gpu_enabled || gpu_aa)) {
        fprintf(stderr, "--hdr on needs the GPU renderer without --gpu-aa.\n");
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
    if (!wake_objects && !wake_field && !player_wake_set && !course && !ghost_path && !work_mode)
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
    if (skin == BOAT_SKIN_GRODAN && !connect_to) {
        // The 15 m Grodan needs the camera clear of its stern and high enough to see over it.
        camera_default.distance += 12;
        camera_default.height += 4;
    }
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
    if (!boat_asset_path(assets, sizeof(assets), "") || !wake_scenery_startup(assets)) {
        fprintf(stderr, "Cannot load the map scenery from %sgeometry/.\n", assets);
        for (int i = 0; i < BOAT_LOCATION_COUNT; ++i)
            boat_world_resources_destroy(&worlds[i]);
        free(pixels);
        return 1;
    }
    Renderer3D renderer = { 0 };
    BoatCpuView cpu_view = { 0 }; // The GPU view owns its own water mesh and scenery.
    cpu_view.daylight = daylight_phase;
    cpu_view.hour = lighting_hour;
    cpu_view.hour_set = lighting_hour_set;
    BoatGpu gpu = { 0 };
    int water_layer = 0, review_label_frames = 0; // F8 and F9; label frames left.
    const char *review_label = NULL;
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
    if (start_set) {
        session.boat.position = (Vector2) { start.x, start.y };
        session.boat.heading = start.z * DEG2RAD;
    }
    session.runs.ghost_visible = true;
    session.runs.export_path = export_path;
    BoatWakePlayer *wake = NULL;
    ObjectStudy object_world = { 0 };
    ObjectStudy *objects = wake_objects ? &object_world : NULL;
    int result = 1;
    bool plugin_active = false;
    study_failure[0] = 0;
    // Owns the revisions the session borrows, so it outlives the session (static, like WORLDS).
    static WakePlugin plugin;
    static WakeConsole console;
    static WakeCommand command;
    wake_console_init(&console);
    wake_command_init(&command, wake_console_print, wake_console_clear, console_save, &console);
    if (plugin_path) {
        if (!wake_plugin_open(&plugin, plugin_path, &session, wake_app_now())) {
            snprintf(study_failure, sizeof(study_failure), "%s", plugin.host.diagnostic.message);
            goto cleanup;
        }
        plugin_active = true;
    } else if (catalog) {
        if (!wake_plugin_open_catalog(&plugin, catalog, &session)) {
            snprintf(study_failure, sizeof(study_failure), "%s", plugin.host.diagnostic.message);
            goto cleanup;
        }
        plugin_active = true;
    }
    static WakeWork work;
    if (work_mode
        && !wake_work_open(
            &work, &session, &worlds[location->id], location, study_failure, sizeof(study_failure)))
        goto cleanup;
    if (work_mode) {
        // The session clock keeps running and saving; only presentation yields to an explicit
        // fixed daylight or hour option.
        work.lighting_cycle = !daylight_set && !lighting_hour_set;
        // Only interactive play reads or writes the game's data directory unasked; scripted runs
        // use a save only when told which one (--save or --data-dir).
        bool plain = !screenshot && !demo && !frame_limit && !save_now;
        wake_work_save_open(&work, &session, save_path, data_dir, plain, fresh_work);
    }
    if (work_review_pose_name && !wake_work_review_pose(&work, &session, work_review_pose_name)) {
        fprintf(stderr, "Could not prepare the %s work review pose.\n", work_review_pose_name);
        goto cleanup;
    }
    if (save_now) {
        // Test fixture: the scripted drive, then one save and exit.
        bool ok = wake_work_script(&work, &session, (uint64_t)round(time / BOAT_TICK_SECONDS))
            && wake_work_save_now(&work);
        result = ok ? 0 : 1;
        goto cleanup;
    }
#if !defined(__EMSCRIPTEN__)
    static char stage_executable[2048];
    snprintf(stage_executable, sizeof(stage_executable), "%s%s", GetApplicationDirectory(),
        GetFileName(argv[0]));
    if (plugin_active)
        plugin.stage_executable = stage_executable;
    static WakeBridge bridge;
    if (bridge_path && !wake_bridge_open(&bridge, bridge_path, GetTime()))
        goto cleanup;
    if (bridge_path)
        wake_bridge_commands(&bridge, &command);
#endif
    // An imported course package is still a time trial: use the same start, best-run path and
    // practice latch as the bundled course.
    if (plugin_active && plugin.host.kind == BOAT_PLUGIN_KIND_COURSE)
        course = true;
    if (course)
        boat_session_start_course(&session);
    else if (!connect_to && skin == BOAT_SKIN_EXPLORER)
        session.handling = &boat_handling_explorer;
    else if (!connect_to && skin == BOAT_SKIN_CABIN)
        session.handling = &boat_handling_cabin;
    else if (!connect_to && skin == BOAT_SKIN_GRODAN)
        session.handling = &boat_handling_grodan;
    if (objects)
        object_study_reset(objects, &session, object_crossing, object_life, object_height);
    // A replay or ghost is recorded against one exact course; only the standard course
    // may be replayed while other course packages remain playable.
    if ((replay_path || ghost_path)
        && (!session.course_definition
            || boat_course_digest(session.course_definition) != BOAT_COURSE_STANDARD_DIGEST)) {
        fprintf(stderr, "--replay and --ghost need the standard time-trial course.\n");
        goto cleanup;
    }
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
        select_course_best(&session, data_dir, waves);
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
        fprintf(stderr, "Map spawn is not navigable.\n");
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
    } else if (screenshot && work_mode) {
        // The scripted work drive: idle at the berth, and the landing delivery after 10 s.
        if (!wake_work_script(&work, &session, (uint64_t)round(time / BOAT_TICK_SECONDS))) {
            fprintf(stderr, "The scripted landing delivery did not complete.\n");
            goto cleanup;
        }
    } else if (screenshot) {
        for (int tick = 0; tick < (int)round(time / BOAT_TICK_SECONDS); ++tick) {
            if (plugin_active)
                wake_plugin_before_tick(&plugin);
            demo_step(&session, tick, NULL, wake, objects);
            if (plugin_active)
                wake_plugin_after_tick(&plugin, &session);
        }
    }
    live_view = boat_session_view(&session);
    live_view.skin = skin;
    if (work_mode)
        live_view.props = wake_work_props(&work);
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
    // The browser's mapped scenery registry is filled by scenery_ready() below, after the
    // window exists (wake_scenery_deferred). Defer the initial CPU draw and its screenshot
    // export until then.
    bool scenery_deferred = wake_scenery_deferred();
    bool initial_cpu_draw_pending = !gpu_enabled && scenery_deferred;
    if (!gpu_enabled && !scenery_deferred
        && !boat_draw_with_ghost(&renderer, &cpu_view, pixels, width, height, view,
            view_presentation, view_world, camera_settings, ghost_view, ghost_view_flag,
            ghost_opacity))
        goto cleanup;
    Image image = { pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    if (screenshot && !gpu_enabled && !scenery_deferred) {
        result = ExportImage(image, screenshot) ? 0 : 1;
        goto cleanup;
    }

    if (!IsWindowReady()) {
        SetConfigFlags(wake_app_window_flags(screenshot || performance_report));
        int window_scale = boat_resolution_window_scale(resolution);
        InitWindow(width * window_scale, height * window_scale, "Wake Velocity");
        if (!IsWindowReady())
            goto cleanup;
        window_owned = true;
        SetWindowMinSize(width, height);
    }
    window_ready = true;
    wake_app_session_canvas(resolution, width, height);
    if (!boat_ui_load())
        goto cleanup;
    wake_app_session_open(vsync);
    if (scenery_deferred && !scenery_ready(location, assets)) {
        fprintf(stderr, "Cannot load the %s scenery.\n", location->name);
        goto cleanup;
    }
    if (initial_cpu_draw_pending) {
        if (!boat_draw_with_ghost(&renderer, &cpu_view, pixels, width, height, view,
                view_presentation, view_world, camera_settings, ghost_view, ghost_view_flag,
                ghost_opacity))
            goto cleanup;
        if (screenshot) {
            result = ExportImage(image, screenshot) ? 0 : 1;
            goto cleanup;
        }
    }
    FramePacer pacer;
    frame_pacer_init_window(&pacer);
    if (gpu_enabled) {
        if (!boat_gpu_init(&gpu, gpu_aa)) {
            fprintf(stderr, "Could not initialize GPU boating resources.\n");
            goto cleanup;
        }
        waterfront_gpu_scene_set_detail_enabled(&gpu.scene, far_detail);
        gpu.daylight = daylight_phase;
        gpu.hour = lighting_hour;
        gpu.hour_set = lighting_hour_set;
        if (!apply_screen_layout(&gpu,
                boat_screen_layout(
                    resolution, width, height, GetScreenWidth(), GetScreenHeight()))) {
            fprintf(stderr, "Could not allocate the %s-resolution scene.\n",
                boat_resolution_name(resolution));
            goto cleanup;
        }
        if (hdr && !boat_gpu_set_hdr(&gpu, true))
            fprintf(stderr, "HDR is unavailable here; presenting the 8-bit scene.\n");
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
    bool work_was_active = false;
    bool net_ready_sent = !networked;
    int frames = 0, demo_tick = 0;
    bool plugin_reset_pending = false;
    bool replay_dragging = false;
    bool help_visible = false;
    BoatMenu menu = { .can_leave = true,
        .return_label = work_mode ? "RETURN CRATE" : NULL,
        .reset_hint = work_mode ? "STARTS THE JOB OVER; THE SAVE FOLLOWS" : NULL,
        .reset_label = work_mode   ? "RESTART JOB"
            : networked            ? "REQUEST RESET"
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
#endif
    wake_touch_session(true);
    SetExitKey(KEY_NULL);
    Profiler profiler = { 0 };
    BoatProfile boat_profile = { 0 };
    BoatProfileSnapshot boat_snapshot = { 0 };
    ProfilerSnapshot snapshot = { 0 };
    VirtualSize size = { width, height };
    double frame_started_at = boat_profile_time(true);
    double next_snapshot_time = 0;
    // G0w: live capture is independent of the F3 overlay. Its ring, GPU queries and the export
    // path belong to the application, not the simulation or network.
    // Static, like the other large application state: its ~370 KB ring would otherwise sit in
    // this frame, which must fit Windows' 1 MB main-thread stack in MSVC Debug builds. study()
    // runs again from the title, so clear it here.
    static BoatCapture capture;
    memset(&capture, 0, sizeof(capture));
    BoatGpuTimer performance_timer = { 0 };
    unsigned long long performance_serial = 0;
    double previous_performance_start = -1;
    double performance_notice_until = 0;
    int performance_segment_map = -1;
    char performance_notice[512] = "";
    PerformanceQueryContext performance_query
        = { .capture = &capture, .lighting = &lighting_review };
    if (performance_capture && gpu_enabled)
        boat_gpu_timer_init(&performance_timer);
    if (work_clock_report) {
        WaterfrontGpuScene *scene = gpu_enabled ? &gpu.scene : NULL;
        if (!wake_lighting_review_begin(&lighting_review, work_clock_report, scene)) {
            fprintf(stderr, "Could not start the work clock report %s.\n", work_clock_report);
            result = 1;
            goto cleanup;
        }
    }
    result = 0;
    if (work_mode)
        wake_work_autosave(&work, true);
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
    // AR6 (L0): plain free boating runs as a local authority and client. Voyages, objectives,
    // courses, the working harbour, rooms, demos and the developer studies keep the direct path
    // until their ledger rows migrate them.
    static WakeLocal local;
#define LOCAL_ELIGIBLE()                                                                           \
    (!direct_session && !networked && !work_mode && !course && !demo && objects                    \
        && !objects->crossing && !wake && !session.voyage_route && !session.runs.playing           \
        && plugin_active && !plugin.authoring && plugin.host.kind == BOAT_PLUGIN_KIND_FREE         \
        && !plugin.objective.initialized)
    if (LOCAL_ELIGIBLE() && !wake_local_open(&local, &session, objects, worlds, GetTime())) {
        snprintf(study_failure, sizeof(study_failure), "Single player could not start: %s.",
            local.local.failure);
        fprintf(stderr, "%s\n", study_failure);
        result = 1;
        goto cleanup;
    }
#endif
    while (!WindowShouldClose() && (!frame_limit || frames < frame_limit)) {
        // The browser supplies a bounded low-resolution viewport after resizing settles.
        int next_width = width, next_height = height;
        if (wake_app_viewport(&next_width, &next_height) && gpu_enabled
            && (next_width != width || next_height != height)) {
            RenderTexture2D next_frame = LoadRenderTexture(next_width, next_height);
            int window_scale = wake_app_canvas_scale(resolution, next_width, next_height);
            BoatScreenLayout next_layout = boat_screen_layout(resolution, next_width, next_height,
                next_width * window_scale, next_height * window_scale);
            if (!IsRenderTextureValid(next_frame) || !apply_screen_layout(&gpu, next_layout)) {
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
            SetWindowSize(width * window_scale, height * window_scale);
            wake_app_canvas_fit();
        }
        double current_frame_started_at = boat_profile_time(true);
        frame_started_at = current_frame_started_at;
        BoatFrameMetrics metrics = { 0 }; // Reset once before any tick in this frame.
        BoatAppInput app_input = boat_app_input_sample();
        int character;
        bool help_pressed = false;
        // Bracket and backslash sit behind AltGr on many layouts, so time keys also match the
        // typed character, and , . 0 serve as layout-independent alternatives.
        int time_direction = 0;
        bool time_normal = false;
        bool plugin_restart = false;
        char console_line[WAKE_CONSOLE_INPUT] = { 0 };
        // PC: the live console owns typed input while it is open. It is unavailable in rooms
        // (no package), replays, demos and screenshots.
        bool console_was_open = false;
        bool console_just_opened = false;
        console_was_open = console.open;
        bool console_available = false;
        // Desktop browsers with a physical keyboard get the same console; touch-only phones do
        // not, because the panel would cover the game and has no text-entry affordance.
        console_available = plugin_active && !networked && !demo && !screenshot
            && !session.runs.playing && !menu.open && !help_visible
            && wake_console_bridge_text_entry();
        bool console_open_key
            = IsKeyPressed(KEY_GRAVE) || IsKeyPressed(KEY_F2) || IsKeyPressed(161);
        if (console_available && !console.open && console_open_key && IsWindowFocused()) {
            wake_console_open(&console, PROTEUS_VERSION);
            console_was_open = true; // Consume the opening key's own character this frame.
            console_just_opened = true;
            plugin_restart |= wake_command_frame(&command, &plugin, &session, wake_app_now());
        }
        wake_console_bridge_state(console.open, console_available, IsWindowFocused());
        if (console.open && !console_just_opened
            && wake_console_update(&console, width, height, console_line, sizeof(console_line))) {
            wake_console_begin_command(&console, console_line);
            if (course)
                session.runs.practice = true;
            plugin_restart
                |= wake_command_submit(&command, &plugin, &session, console_line, wake_app_now());
        }
        while ((character = GetCharPressed()) != 0) {
            if (console.open || console_was_open)
                continue;
            help_pressed |= character == '?';
            time_direction
                += (character == ']' || character == '.') - (character == '[' || character == ',');
            time_normal |= character == '\\' || character == '0';
        }
        if (!console.open) {
            time_direction += (int)app_input.bracket_right - (int)app_input.bracket_left;
            time_normal |= app_input.backslash;
            time_direction = time_direction > 0 ? 1 : time_direction < 0 ? -1 : 0;
            if (help_pressed)
                help_visible = !help_visible;
        }
        // Menu shortcuts release capture without changing steering mode. P preserves fullscreen.
        bool reset_requested = false;
        bool menu_was_open = menu.open;
        bool menu_pressed
            = !console.open && !console_was_open && (app_input.back || app_input.pause);
#if !defined(__EMSCRIPTEN__)
        if (IsKeyPressed(KEY_F11))
            ToggleBorderlessWindowed();
#endif
        menu_pressed |= wake_touch_menu_pressed();
        if (menu_pressed) {
            if (help_visible)
                help_visible = false;
            else if (menu.open)
                menu.open = false;
            else
                boat_menu_open(&menu);
        }
        bool menu_gate = boat_menu_gate(menu_was_open, menu.open, help_visible, IsWindowFocused());
        if (menu_gate)
            boat_app_input_poll_touch(&app_input);
        BoatScreenLayout layout
            = boat_screen_layout(resolution, width, height, GetScreenWidth(), GetScreenHeight());
        int scale = layout.scale;
        Rectangle destination = layout.destination;
        // A failed high-resolution reallocation keeps drawing at the previous size.
        if (gpu_enabled && (resolution != BOAT_RESOLUTION_LOW || hdr)
            && apply_screen_layout(&gpu, layout))
            texture = gpu.target.texture;
        Camera2D menu_camera = { .offset = { destination.x + (width - 320) * scale / 2,
                                     destination.y + (height - 200) * scale / 2 },
            .zoom = scale };
        if (menu_gate) {
            BoatMenuChoice choice
                = boat_menu_update(&menu, &app_input, menu_camera, GetFrameTime());
            if (choice == BOAT_MENU_RESUME)
                menu.open = false;
            else if (choice == BOAT_MENU_CONTROLS)
                help_visible = true;
            else if (choice == BOAT_MENU_RETURN) {
                wake_work_return_crate(&work);
                menu.open = false;
            } else if (choice == BOAT_MENU_RESET) {
                reset_requested = true;
                menu.open = false;
            } else if (choice == BOAT_MENU_LEAVE)
                break;
            else if (choice == BOAT_MENU_FULLSCREEN)
                ToggleBorderlessWindowed();
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
        if (console.open || console_was_open)
            active = false;
        if (plugin_path && wake_plugin_editing())
            active = false;
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
        int mobile_button_height = 26;
        mobile_buttons = wake_touch_buttons(active && !demo && !session.runs.playing);
        mobile_target = wake_touch_target();
        mobile_button_height = wake_touch_button_height(width);
        bool keyboard_steering = app_input.helm_right || app_input.helm_left;
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
        float mouse_delta = app_input.pointer_delta.x;
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
            ? (float)((int)app_input.helm_right - (int)app_input.helm_left)
            : (float)(((mobile_buttons & 8) != 0) - ((mobile_buttons & 4) != 0));
        // The open console takes typed input, but the world keeps running with a neutral helm.
        bool console_simulating = false;
        console_simulating = console.open && !networked && !demo && !session.runs.playing;
        if (console_simulating) {
            steering_target = 0;
            steering_rate = 0;
        }
        if (active && IsKeyPressed(KEY_F3)) {
            profiler_visible = !profiler_visible;
            next_snapshot_time = 0;
        }
        if (active && gpu_enabled && IsKeyPressed(KEY_F8)) {
            water_layer = (water_layer + 1) % WATER_LAYERS;
            apply_water_layer(&gpu, water_layer);
            review_label = water_layer_names[water_layer];
            review_label_frames = 180;
        }
        if (active && gpu_enabled && IsKeyPressed(KEY_F9)) {
            // Graphics review (Batch C): the map's baked ambient occlusion on or off.
            gpu.scene.ambient_occlusion_disabled = !gpu.scene.ambient_occlusion_disabled;
            review_label = gpu.scene.ambient_occlusion_disabled ? "AMBIENT OCCLUSION: OFF"
                                                                : "AMBIENT OCCLUSION: ON";
            review_label_frames = 180;
        }
        bool performance_request = active && IsKeyPressed(KEY_F4);
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
        bool precision = active && !demo && !session.runs.playing && app_input.precision;
        thrust_target = active && !demo && !session.runs.playing
            ? boat_pedal_target(app_input.throttle_up || (mobile_buttons & 1),
                  app_input.throttle_down || (mobile_buttons & 2), precision)
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
                if (time_direction)
                    boat_replay_speed_step(&session.runs, time_direction);
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
                // On the web a consumed touch tap may have overwritten the app's pointer values,
                // but only while the menu is open, when this replay block is inactive.
                if (app_input.pointer_pressed && CheckCollisionPointRec(app_input.pointer, hit))
                    replay_dragging = true;
                if (replay_dragging) {
                    boat_replay_seek(
                        &session.runs, (app_input.pointer.x - bar.x) / bar.width * duration);
                    session.runs.paused = true;
                    replay_adjusted = true;
                    if (!app_input.pointer_down)
                        replay_dragging = false;
                }
            }
        }
        if (active && !networked) {
            if (session.runs.playing) {
                if (time_normal)
                    session.runs.replay_speed_index = BOAT_TIME_NORMAL;
            } else {
                time_speed_index = boat_time_speed_step(time_speed_index, time_direction);
                if (time_normal)
                    time_speed_index = BOAT_TIME_NORMAL;
            }
        }
        metrics.course_replay_ms
            += (float)((boat_profile_time(true) - course_input_started_at) * 1000.0);
        reset_requested |= boat_reset_hold_update(&reset_hold, app_input.backspace_down,
            active && !session.runs.playing && !work_mode, GetFrameTime());
        // A plugin revision changes between this frame's ticks; a new exposure restarts below.
#if !defined(__EMSCRIPTEN__)
        // Tool requests arrive whether or not the window has focus.
        if (bridge_path)
            plugin_restart |= wake_bridge_frame(&bridge, &plugin, &session, GetTime());
#endif
        if (plugin_active && plugin.authoring)
            plugin_restart |= wake_plugin_frame(&plugin, &session, active && IsKeyPressed(KEY_F5),
                active && IsKeyPressed(KEY_F6), active && IsKeyPressed(KEY_F7), wake_app_now());
        if (plugin_active)
            plugin_restart |= wake_command_frame(&command, &plugin, &session, wake_app_now());
        if (plugin_active && wake_plugin_waiting(&plugin))
            plugin_reset_pending |= reset_requested;
        else if (plugin_reset_pending) {
            reset_requested = true;
            plugin_reset_pending = false;
        }
#ifdef __EMSCRIPTEN__
        if (plugin_active && plugin.authoring)
            wake_web_plugin_controls(plugin_reset_pending, menu.open);
#endif
        if (reset_requested) {
            thrust_target = 0;
            mouse_wheel = 0;
            steering_target = mouse_driving ? 0 : NAN;
        }
        double simulation_started_at = boat_profile_time(true);
        bool work_active = active || console_simulating;
        // The work session's key actions run once per frame and its crane axes drive each tick.
        WakeWorkDrive work_drive = { 0 };
        if (work_mode)
            work_drive = wake_work_frame_input(&work, active && !demo);
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
        } else if (plugin_active && wake_plugin_waiting(&plugin) && !plugin_restart) {
            // Keep drawing and polling the worker, but never advance past callback boundary T.
            accumulator = 0;
        } else if (reset_requested && session.voyage.route && !plugin_restart) {
            // The outing continues: back to the last shelter, clock, water and wakes running.
            thrust_target = 0;
            boat_session_recover(&session);
            if (plugin_active)
                wake_plugin_after_tick(&plugin, &session);
            accumulator = 0;
        } else if (reset_requested || plugin_restart) {
            thrust_target = 0;
            boat_session_restart(
                &session, study_water(location, waves, current, crossing_wake, player_wake));
            if (course && plugin_restart) {
                // An applied course edit is another course: its runs and bests are its own.
                boat_replay_destroy(&session.runs.last);
                boat_replay_destroy(&session.runs.reference);
                select_course_best(&session, data_dir, waves);
                boat_replay_runs_reset(&session.runs);
            }
            if (work_mode)
                wake_work_reset(&work, &session);
            if (wake) {
                boat_wake_player_reset(wake);
                session.world.wake_field = boat_wake_player_field(wake);
            }
            if (objects)
                object_study_reset(objects, &session, object_crossing, object_life, object_height);
            accumulator = 0;
            if (plugin_active)
                wake_plugin_reset_objective(&plugin, &session);
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
            // A restart recreates both endpoints with a new generation; a revision that is no
            // longer plain free boating continues on the direct path from the reset session.
            if (local.active && !LOCAL_ELIGIBLE())
                wake_local_close(&local);
            else if (local.active && !wake_local_restart(&local, &session, objects, GetTime())) {
                snprintf(study_failure, sizeof(study_failure), "Single player stopped: %s.",
                    local.local.failure);
                fprintf(stderr, "%s\n", study_failure);
                result = 1;
                break;
            }
#endif
            demo_tick = 0;
            replay_dragging = false;
        } else if (session.runs.playing) {
            accumulator = 0;
            double course_started_at = boat_profile_time(true);
            boat_replay_advance(&session.runs, replay_adjusted ? 0 : GetFrameTime(), active);
            metrics.course_replay_ms
                += (float)((boat_profile_time(true) - course_started_at) * 1000.0);
        } else if (work_active) {
            BoatInput input = {
                .steering_rate = steering_rate,
            };
            double raw_active_delta = GetFrameTime();
            metrics.discarded_active_ms
                = boat_profile_discarded_seconds(raw_active_delta, true) * 1000.0f;
            if (course && time_speed_index != BOAT_TIME_NORMAL && !session.runs.finished)
                session.runs.practice = true;
            accumulator += work_mode
                ? boat_time_work_frame_seconds(raw_active_delta, time_speed_index, work_was_active)
                : boat_time_frame_seconds(raw_active_delta, time_speed_index);
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
            // The grant is the same accumulator arithmetic; the client derives each tick's rates
            // from its predicted boat as the loop below does from the session's.
            if (local.active
                && !wake_local_frame(&local, &session, objects, &accumulator, input, thrust_target,
                    steering_target, GetTime(), &metrics)) {
                // D10: no save applies to free boating; back to the title with the reason.
                snprintf(study_failure, sizeof(study_failure), "Single player stopped: %s.",
                    local.local.failure);
                fprintf(stderr, "%s\n", study_failure);
                result = 1;
                break;
            }
#endif
            while (accumulator >= BOAT_TICK_SECONDS) {
                if (plugin_active)
                    wake_plugin_before_tick(&plugin);
                if (work_mode) {
                    // The helm is the game's own (throttle, wheel, mouse); the crane view and a
                    // brake take it over.
                    const BoatWorkSession *w = &work.session;
                    BoatInput helm = { 0 };
                    if (!w->crane_view && !demo) {
                        helm.steering_rate = isfinite(steering_target)
                            ? boat_steering_rate(w->boat.wheel, steering_target)
                            : steering_rate;
                        helm.throttle_rate
                            = w->braking ? 0 : boat_throttle_rate(w->boat.throttle, thrust_target);
                    }
                    wake_work_tick(
                        &work, &session, work_drive.slew, work_drive.reach, work_drive.hoist, helm);
                } else if (demo)
                    demo_step(&session, demo_tick++, &metrics, wake, objects);
                else
                    timed_boat_step(&session,
                        boat_helm_input(&session.boat, input, thrust_target, steering_target),
                        &metrics, wake, objects);
                if (plugin_active)
                    wake_plugin_after_tick(&plugin, &session);
                accumulator -= BOAT_TICK_SECONDS;
                if (plugin_active && wake_plugin_waiting(&plugin)) {
                    accumulator = 0;
                    break;
                }
            }
        } else {
            accumulator = 0;
        }
        if (work_mode)
            work_was_active = work_active;
        metrics.paused
            = (!active && !console_simulating) || (session.runs.playing && session.runs.paused);
        metrics.simulation_ms = (float)((boat_profile_time(true) - simulation_started_at) * 1000.0);
        // Course saves wait until the frame's ticks are done.
        double flush_started_at = boat_profile_time(true);
        if (work_mode) {
            wake_work_sync(&work, &session);
            wake_work_save_frame(&work, menu.open || help_visible, IsWindowFocused());
        }
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
        if (work_mode)
            live_view.props = wake_work_props(&work);
#ifdef PROTEUS_BOAT_NET
        if (net) {
            live_view.skin = net_study_skin(net->client.slot); // Every client agrees.
            net_study_race_view(net, &live_view);
            net_study_marks_view(net, &live_view);
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
        boat_audio_update(&audio, audio_input,
            (active || console_simulating) && (!session.runs.playing || !session.runs.paused));
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
        unsigned long long performance_frame = performance_serial++;
        bool performance_timed = performance_capture && gpu_enabled
            && boat_gpu_timer_begin(&performance_timer, performance_frame);
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
        if (performance_timed) {
            rlDrawRenderBatchActive();
            boat_gpu_timer_end();
        }
        if (!gpu_enabled)
            UpdateTexture(texture, pixels);
        if (mirror_visible) {
            boat_rearview_set_lighting(&mirror, daylight_phase, lighting_hour, lighting_hour_set);
            if (!boat_rearview_update(&mirror, gpu_enabled ? &gpu : NULL, view, view_presentation,
                    view_world, &rivals)) {
                result = 1;
                break;
            }
        }
        metrics.snapshot_render_ms = (float)((boat_profile_time(true) - scene_started_at) * 1000.0);
        if (profiler_visible && current_frame_started_at >= next_snapshot_time) {
            snapshot = profiler_snapshot(&profiler, (float)live_view.time, size, destination);
            boat_snapshot = boat_profile_snapshot(&boat_profile);
            snapshot.scene_label = location->name;
            snapshot.renderer_label = !gpu_enabled ? "CPU"
                : gpu_aa                           ? "GPU 4x SSAA"
                : boat_gpu_hdr_active(&gpu)        ? (resolution ? "GPU high HDR" : "GPU HDR")
                : resolution                       ? "GPU high"
                                                   : "GPU";
            snapshot.texture_upload_kib = gpu_enabled ? (wake ? 384
                                                                : objects ? 4
                                                                          : 0)
                                                      : snapshot.framebuffer_kib;
            next_snapshot_time = current_frame_started_at + 0.25;
        }
        double present_started_at = boat_profile_time(true);
        // High resolution draws the HUD over a transparent frame, accumulating premultiplied
        // alpha, and composites it over the full-size scene; low draws the scene into the frame.
        bool hud_overlay = gpu_enabled && resolution != BOAT_RESOLUTION_LOW;
        BeginTextureMode(frame_target);
        if (hud_overlay) {
            boat_screen_begin_overlay();
        } else {
            ClearBackground((Color) { 14, 23, 32, 255 });
            DrawTexturePro(texture,
                (Rectangle) { 0, 0, gpu_enabled ? -width : width, gpu_enabled ? -height : height },
                (Rectangle) { 0, 0, width, height }, (Vector2) { 0 }, 0, WHITE);
        }
        boat_hud_draw(
            (BoatHudTelemetry) { .speed_mps = hypotf(view->boat.velocity.x, view->boat.velocity.y),
                .thrust = view->boat.throttle,
                .steering = view->boat.wheel,
                .precision = precision,
                .reset_progress = reset_hold.seconds / BOAT_RESET_HOLD_SECONDS },
            width,
            height > width ? height - ((mobile_buttons & 16) ? 2 * mobile_button_height + 24 : 80)
                           : height);
        if (work_mode)
            wake_work_hud(&work,
                &(WakeWorkProjection) { .view = view,
                    .presentation = view_presentation,
                    .world = view_world,
                    .camera = camera_settings },
                width, height);
        minimap.room_marks = live_view.marks;
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
        if (!(mobile_buttons & 16) && mouse_driving && !menu.open)
            boat_ui_text(mouse_captured ? "MOUSE / P: MENU" : "MOUSE / CLICK TO CAPTURE", 4,
                hint_top, 10, (Color) { 218, 223, 202, 255 });
        else if (!(mobile_buttons & 16) && !menu.open)
            boat_ui_text("P: MENU", 4, hint_top, 10, (Color) { 218, 223, 202, 255 });
#if defined(__EMSCRIPTEN__)
        if (mouse_unavailable)
            boat_ui_text("MOUSE UNAVAILABLE / A/D / P: MENU", 4, hint_top, 10,
                (Color) { 218, 223, 202, 255 });
#endif
        if (review_label_frames > 0 && review_label) {
            --review_label_frames;
            boat_ui_text(review_label, 4, height - 76, 10, (Color) { 233, 180, 104, 255 });
        }
        if (!networked && !session.runs.playing
            && (time_speed_index != BOAT_TIME_NORMAL || session.runs.practice)) {
            const char *time_label = TextFormat("TIME %.2gx%s", boat_time_rate(time_speed_index),
                session.runs.practice ? " / PRACTICE" : "");
            boat_ui_text(time_label, 4, height - 64, 10, (Color) { 233, 180, 104, 255 });
        }
        draw_touch_controls(width, height, destination, active && !demo && !session.runs.playing,
            mobile_buttons, mobile_button_height);
        if (hud_overlay)
            boat_screen_end_overlay();
        EndTextureMode();
        BeginDrawing();
        ClearBackground((Color) { 14, 23, 32, 255 });
        if (hud_overlay)
            boat_screen_present_overlay(&layout, texture, frame_target.texture);
        else
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
        if (plugin_active && plugin.authoring)
            wake_plugin_draw(&plugin, 12, (int)destination.y + (height - 48) * scale - 20);
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
            int objective_bottom
                = net_study_draw_objective(net, GetScreenWidth() - 12, feed_top + 8);
            feed_top = objective_bottom > feed_top + 8 ? objective_bottom : feed_top;
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
        if (performance_notice[0] && GetTime() < performance_notice_until)
            boat_ui_text(performance_notice, 12, 40, 10, RAYWHITE);
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
                deathmatch, race, networked, plugin_active && plugin.authoring, work_mode);
        }
        if (console.open) {
            // Console stays above the HUD, in the same integer-scaled pixel grid as the frame.
            // High resolution's frame holds only the HUD, so there is no low-resolution scene
            // to blur beneath the panel.
            Camera2D console_camera = { .offset = { destination.x, destination.y }, .zoom = scale };
            BeginMode2D(console_camera);
            wake_console_draw(&console, width, height,
                gpu_enabled && !hud_overlay ? &frame_target.texture : NULL);
            EndMode2D();
        }
        if (frame_capture && frames + 1 == frame_limit) {
            rlDrawRenderBatchActive(); // Draw what is batched before reading it back.
            Image window = LoadImageFromScreen(); // The presented window, HUD included.
            if (!ExportImage(window, frame_capture))
                result = 1;
            UnloadImage(window);
        }
        frame_pacer_wait(&pacer);
        EndDrawing();
        metrics.present_ms = (float)((boat_profile_time(true) - present_started_at) * 1000.0);
        metrics.frame_ms = (float)((boat_profile_time(true) - frame_started_at) * 1000.0);
        if (performance_capture) {
            if (performance_segment_map < 0)
                performance_segment_map = (int)view_world->definition->id;
            else if (performance_segment_map != (int)view_world->definition->id) {
                performance_segment_map = (int)view_world->definition->id;
                boat_capture_segment(&capture);
            }
            BoatCaptureSample sample = {
                .time = boat_profile_time(true),
                .serial = performance_frame,
                .interval_ms = previous_performance_start >= 0
                    ? (float)((current_frame_started_at - previous_performance_start) * 1000.0)
                    : 0,
                .frame_ms = metrics.frame_ms,
                .simulation_ms = metrics.simulation_ms,
                .physics_ms = metrics.physics_ms,
                .audio_ms = metrics.audio_ms,
                .render_ms = metrics.snapshot_render_ms,
                .present_ms = metrics.present_ms,
                .discarded_ms = metrics.discarded_active_ms,
                .ticks = metrics.ticks,
                .map = view_world->definition->id,
                .preset = gpu_aa                         ? 1
                    : resolution == BOAT_RESOLUTION_HIGH ? 2
                                                         : 0,
                .segment = capture.segment,
                .x = session.boat.position.x,
                .z = session.boat.position.y,
                .heading = session.boat.heading,
                .camera_heading = session.presentation.camera_heading,
                .speed = Vector2Length(session.boat.velocity),
                .focused = IsWindowFocused(),
                .active = active,
                .paused = !active,
                .gpu_pending = performance_timed,
            };
            if (gpu_enabled) {
                WaterfrontGpuCount total = waterfront_gpu_stats_total(&gpu.scene.stats);
                sample.app_submissions = total.submissions;
                sample.scene_triangles = total.triangles;
                for (int pass = 0; pass < WATERFRONT_GPU_PASS_COUNT && pass < BOAT_CAPTURE_PASSES;
                    ++pass)
                    for (int group = 0;
                        group < WATERFRONT_GPU_GROUP_COUNT && group < BOAT_CAPTURE_GROUPS;
                        ++group) {
                        WaterfrontGpuCount count = gpu.scene.stats.mesh[pass][group];
                        sample.pass_submissions[pass] += count.submissions;
                        sample.pass_triangles[pass] += count.triangles;
                        sample.group_submissions[group] += count.submissions;
                        sample.group_triangles[group] += count.triangles;
                    }
            }
            const BoatWakeObjects *pool = objects ? object_study_wakes(objects) : NULL;
            if (pool) {
                sample.wake_live = pool->live;
                sample.wake_peak_live = pool->peak_live;
                sample.wake_emitters = BOAT_OBJECT_BOATS;
            }
            sample.traffic_wake_live
                = objects && objects->traffic_enabled ? objects->traffic.live : 0;
            boat_capture_add(&capture, &sample);
            if (work_clock_report)
                wake_lighting_review_frame(&lighting_review, &sample, work.session.tick,
                    work.session.lighting_hours, work.session.lighting_cycle_seconds,
                    boat_time_rate(time_speed_index), work.lighting_cycle,
                    gpu_enabled ? NULL : &renderer);
            previous_performance_start = current_frame_started_at;
            boat_gpu_timer_poll(
                &performance_timer, performance_query_complete, &performance_query, false);
            if (performance_request) {
                char directory[4096], destination[4096];
                if (!wake_performance_default_directory(directory, sizeof(directory)))
                    snprintf(directory, sizeof(directory), ".");
                wake_performance_default_name(directory, destination, sizeof(destination), true);
                WakePerformanceInfo info = {
                    .capture_kind = demo || screenshot || frame_limit ? "scripted" : "live",
                    .map_name = view_world->definition->name,
                    .preset_name = presentation_preset(gpu_aa, resolution, &gpu),
                    .vsync = vsync,
                    .aa = gpu_aa,
                    .fullscreen = IsWindowFullscreen(),
                    .waves = waves,
                    .wake_objects = objects != NULL,
                    .lighting = gpu_enabled,
                    .shadows = gpu_enabled,
                    .reflections = gpu_enabled,
                    .window_width = GetScreenWidth(),
                    .window_height = GetScreenHeight(),
                    .render_width = GetRenderWidth(),
                    .render_height = GetRenderHeight(),
                    .scene_width = gpu_enabled ? gpu.target.texture.width : width,
                    .scene_height = gpu_enabled ? gpu.target.texture.height : height,
                    .reflection_width = gpu_enabled ? gpu.scene.reflection.texture.width : 0,
                    .reflection_height = gpu_enabled ? gpu.scene.reflection.texture.height : 0,
                    .monitor_refresh_hz = GetMonitorRefreshRate(GetCurrentMonitor()),
                };
                export_performance(
                    &capture, &info, destination, performance_notice, sizeof(performance_notice));
                performance_notice_until = GetTime() + 10;
            }
        } else if (performance_request) {
            snprintf(performance_notice, sizeof(performance_notice),
                "Performance capture is disabled (--no-performance-capture)");
            performance_notice_until = GetTime() + 6;
        }
        boat_profile_add(&boat_profile, metrics);
        profiler_add(&profiler, metrics.frame_ms, metrics.snapshot_render_ms, metrics.present_ms);
        ++frames;
#ifdef PROTEUS_BOAT_NET
        // Let the room spawn the boat only after the first frame has warmed up the renderer.
        // READY makes the authority queue reliable spawn and roster traffic; it must not land
        // during that first frame's shader and presentation work.
        if (networked && !net_ready_sent) {
            if (!net_study_ready(net)) {
                result = 1;
                break;
            }
            net_ready_sent = true;
        }
#endif
    }
    if (work_mode)
        wake_work_save_leave(&work); // Quitting, closing the window or returning to the title.
    if (performance_report) {
        if (!performance_capture || !capture.count) {
            fprintf(stderr, "No performance samples were captured; the report was not written.\n");
            result = 1;
        } else {
            WakePerformanceInfo info = {
                .capture_kind = demo || frame_limit ? "scripted" : "live",
                .map_name = view_world->definition->name,
                .preset_name = presentation_preset(gpu_aa, resolution, &gpu),
                .vsync = vsync,
                .aa = gpu_aa,
                .fullscreen = IsWindowFullscreen(),
                .waves = waves,
                .wake_objects = objects != NULL,
                .lighting = gpu_enabled,
                .shadows = gpu_enabled,
                .reflections = gpu_enabled,
                .window_width = GetScreenWidth(),
                .window_height = GetScreenHeight(),
                .render_width = GetRenderWidth(),
                .render_height = GetRenderHeight(),
                .scene_width = gpu_enabled ? gpu.target.texture.width : width,
                .scene_height = gpu_enabled ? gpu.target.texture.height : height,
                .reflection_width = gpu_enabled ? gpu.scene.reflection.texture.width : 0,
                .reflection_height = gpu_enabled ? gpu.scene.reflection.texture.height : 0,
                .monitor_refresh_hz = GetMonitorRefreshRate(GetCurrentMonitor()),
            };
            char written[4096], error[512];
            if (wake_performance_export(&capture, &info, performance_report, written,
                    sizeof(written), error, sizeof(error))) {
                printf("performance_report %s\n", written);
            } else {
                fprintf(stderr, "Performance report failed: %s\n", error);
                result = 1;
            }
        }
    }
cleanup:
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
    if (local.local.stats.frames)
        fprintf(stdout,
            "Local session: generation %u, %llu ticks in %llu chunks (at most %u per frame), "
            "%llu restarts\n",
            (unsigned)local.local.generation, (unsigned long long)local.local.stats.ticks,
            (unsigned long long)local.local.stats.chunks, local.local.stats.max_chunks,
            (unsigned long long)local.local.stats.restarts);
    wake_local_close(&local);
    local.local.stats = (BoatLocalStats) { 0 };
#endif
    if (lighting_review.state) {
        if (performance_timer.supported)
            boat_gpu_timer_poll(
                &performance_timer, performance_query_complete, &performance_query, true);
        if (!wake_lighting_review_end(&lighting_review))
            result = 1;
    }
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
#if !defined(__EMSCRIPTEN__)
    if (bridge_path)
        wake_bridge_close(&bridge);
#endif
    if (plugin_active)
        wake_plugin_close(&plugin, &session);
    boat_session_flush(&session);
    boat_session_destroy(&session);
    if (performance_timer.supported)
        boat_gpu_timer_destroy(&performance_timer);
    if (window_ready) {
        wake_console_unload();
        boat_ui_unload();
#if defined(__EMSCRIPTEN__)
        boat_web_mouse_state(0, 0);
        wake_touch_session(false);
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

// Single player from the title: free boating, as the macOS app used to start, or the basin trial,
// which has no current or crossing wake. EXTRA options follow the title's own.
static int title_single(const BoatTitleSettings *settings, int extra_count, char **extra)
{
    char *arguments[32] = { "wake_velocity" };
    int count = 1;
    // WORKING HARBOUR runs its own --work session, which refuses the free-ride options.
    bool working_harbour = settings->location == BOAT_LOCATION_OUTER
        && settings->outer_content == BOAT_TITLE_OUTER_WORKING_HARBOUR;
    if (working_harbour) {
        // Starting New over an unavailable save sets it aside instead of overwriting it.
        if (settings->work_new) {
            BoatTitleSave slot;
            wake_work_title_scan(settings->data_dir, &slot);
            if (slot.present && !slot.available)
                wake_work_title_set_aside(&slot);
        }
        arguments[count++] = "--location";
        arguments[count++] = "outer";
        arguments[count++] = "--work";
        if (settings->work_new)
            arguments[count++] = "--new";
        if (settings->data_dir) {
            arguments[count++] = "--data-dir";
            arguments[count++] = (char *)settings->data_dir;
        }
    } else if (settings->location == BOAT_LOCATION_COUNT)
        arguments[count++] = "--course";
    else {
        arguments[count++] = "--current";
        arguments[count++] = "--crossing-wake";
        arguments[count++] = "--location";
        arguments[count++] = (char *)boat_location(settings->location)->name;
    }
    bool grodan = false;
    if (!working_harbour && settings->location == BOAT_LOCATION_OUTER && settings->outer_content) {
        arguments[count++] = "--package";
        arguments[count++] = (char *)boat_title_outer_package(settings->outer_content);
        grodan = settings->outer_content == BOAT_TITLE_OUTER_GRODAN;
    }
    if (!working_harbour && settings->waves)
        arguments[count++] = "--waves";
    if (settings->high_resolution) {
        arguments[count++] = "--resolution";
        arguments[count++] = "high";
    } else if (settings->smooth_edges)
        arguments[count++] = "--gpu-aa";
    if (!working_harbour) {
        // GRODAN TOUR is the Grodan's, whatever livery the player keeps; CANAL TOUR keeps it.
        arguments[count++] = "--skin";
        arguments[count++] = (char *)boat_skin_name(grodan ? BOAT_SKIN_GRODAN : settings->skin);
    }
    arguments[count++] = "--soundtrack";
    arguments[count++] = (char *)boat_music(settings->soundtrack)->id;
    const char *lighting_option, *lighting_value;
    // WORKING HARBOUR keeps its session clock's default cycle; a saved title preset must not
    // become an explicit fixed override there. Other modes still inject the saved choice.
    if (!working_harbour
        && boat_title_lighting_cli(
            settings->lighting, extra_count, extra, &lighting_option, &lighting_value)) {
        arguments[count++] = (char *)lighting_option;
        arguments[count++] = (char *)lighting_value;
    }
    if (settings->mouse_steering)
        arguments[count++] = "--mouse-steering";
    for (int i = 0; i < extra_count && count < 31; ++i)
        arguments[count++] = extra[i];
    arguments[count] = NULL;
    study_mouse_steering = settings->mouse_steering;
    return study(count, arguments);
}

// --title-start PLACE [OPTIONS]: the title's START with default settings and no menu, so a test
// proves that every PLACE the panel offers opens. PLACE is basin, outer, outer-standard,
// outer-harbor-life, outer-grodan, outer-canal-tour, outer-working-harbour or basin-trial.
static int title_start(int argc, char **argv)
{
    BoatTitleSettings settings;
    boat_title_defaults(&settings);
    const char *place = argv[2];
    const BoatWorldDefinition *location = boat_location_find(place);
    bool known = location != NULL;
    if (location)
        settings.location = location->id;
    else if (!strcmp(place, "basin-trial")) {
        settings.location = BOAT_LOCATION_COUNT;
        known = true;
    } else if (!strncmp(place, "outer-", 6))
        for (unsigned i = 0; i < BOAT_TITLE_OUTER_CONTENTS; ++i)
            if (!strcmp(place + 6, boat_title_outer_content(i))) {
                settings.location = BOAT_LOCATION_OUTER;
                settings.outer_content = i;
                known = true;
            }
    if (!known) {
        fprintf(stderr, "--title-start: unknown place %s.\n", place);
        return 1;
    }
    return title_single(&settings, argc - 3, argv + 3);
}

#ifdef PROTEUS_BOAT_NET
// Join the address, or (HOST) start a room with the title's host settings and join that.
static int title_join(BoatTitleSettings *settings, bool host)
{
    char *arguments[16] = { "wake_velocity", "--name", settings->name, "--soundtrack",
        (char *)boat_music(settings->soundtrack)->id };
    int count = 5;
    const char *lighting_option, *lighting_value;
    if (boat_title_lighting_cli(settings->lighting, 0, NULL, &lighting_option, &lighting_value)) {
        arguments[count++] = (char *)lighting_option;
        arguments[count++] = (char *)lighting_value;
    }
    if (host) {
        arguments[count++] = "--host";
        arguments[count++] = (char *)boat_title_host_mode(settings->host_mode);
        if (settings->host_mode != BOAT_TITLE_HOST_RACE
            && settings->host_mode != BOAT_TITLE_HOST_HUNT) {
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
    if (settings->high_resolution) {
        arguments[count++] = "--resolution";
        arguments[count++] = "high";
    } else if (settings->smooth_edges)
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
#if !defined(__EMSCRIPTEN__)
    if (argc == 2 && !strcmp(argv[1], "--plugin-stage-worker"))
        return boat_plugin_stage_worker();
    if (argc == 2 && !strcmp(argv[1], "--plugin-query-worker"))
        return boat_plugin_query_worker();
#endif
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
    if (argc > 1 && !strcmp(argv[1], "--serve"))
        return boat_server_main(argc - 1, argv + 1); // A hosted room: no window, audio or assets.
#endif
    if (argc > 2 && !strcmp(argv[1], "--title-start"))
        return title_start(argc, argv);
    bool capture = argc >= 4 && !strcmp(argv[1], "--title") && !strcmp(argv[2], "--screenshot");
    if (argc != 1 && !(argc == 2 && !strcmp(argv[1], "--title")) && !capture)
        return study(argc, argv);
    BoatTitleSettings settings;
    boat_title_load(&settings);
    BoatTitleCapture capture_options = { false, false, 0 };
    if (capture) {
        for (int i = 4; i < argc; ++i) {
            if (!strcmp(argv[i], "--solo"))
                capture_options.solo = true;
            else if (!strcmp(argv[i], "--ask")) {
                capture_options.ask = true;
                capture_options.solo = true;
            } else if (!strcmp(argv[i], "--save-mode") && i + 1 < argc) {
                const char *mode = argv[++i];
                if (!strcmp(mode, "continue"))
                    capture_options.save_mode = 0;
                else if (!strcmp(mode, "new"))
                    capture_options.save_mode = 1;
                else if (!strcmp(mode, "delete"))
                    capture_options.save_mode = 2;
                else {
                    fprintf(stderr, "--title --screenshot: unknown save mode %s.\n", mode);
                    return 1;
                }
            } else if (!strcmp(argv[i], "--content") && i + 1 < argc) {
                const char *name = argv[++i];
                bool found = false;
                for (unsigned content = 0; content < BOAT_TITLE_OUTER_CONTENTS; ++content)
                    if (!strcmp(name, boat_title_outer_content(content))) {
                        settings.location = BOAT_LOCATION_OUTER;
                        settings.outer_content = content;
                        found = true;
                        break;
                    }
                if (!found) {
                    fprintf(stderr, "--title --screenshot: unknown content %s.\n", name);
                    return 1;
                }
                capture_options.solo = true;
            } else if (!strcmp(argv[i], "--data-dir") && i + 1 < argc)
                settings.data_dir = argv[++i];
            else {
                fprintf(stderr, "--title --screenshot: unknown option %s.\n", argv[i]);
                return 1;
            }
        }
    }
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(wake_app_window_flags(capture));
    InitWindow(320 * 3, 200 * 3, "Wake Velocity");
    if (!IsWindowReady())
        return 1;
    if (capture) {
        bool ok = argc > 4 ? boat_title_screenshot_panel(&settings, 2.5, argv[3], &capture_options)
                           : boat_title_screenshot(&settings, 2.5, argv[3]);
        CloseWindow();
        return ok ? 0 : 1;
    }
    wake_app_title_open();
    wake_servers_start();
    SetWindowMinSize(320, 200);
    SetExitKey(KEY_NULL);
    for (BoatTitleChoice choice; (choice = boat_title_run(&settings)) != BOAT_TITLE_EXIT;) {
        boat_title_save(&settings);
        if (choice == BOAT_TITLE_SINGLE && title_single(&settings, 0, NULL))
            snprintf(settings.message, sizeof(settings.message), "%s",
                study_failure[0] ? study_failure : "Single player stopped.");
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
    wake_servers_stop();
    wake_app_title_close();
    CloseWindow();
    return 0;
}
