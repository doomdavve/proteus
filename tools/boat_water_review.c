// Deterministic boat-level water audit. Uses the playable simulation, not a camera spline.
#include "external/glad.h"

#include "boat_gpu.h"
#include "boat_map.h"
#include "boat_profile.h"
#include "boat_test_session.h"
#include "frame_pacer.h"
#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    const char *name;
    Vector2 position; // Simulation X (west positive), Z (north positive).
} Stop;
static const Stop stops[] = { { "mouth", { -24, -70 } }, { "river", { -15, -180 } },
    { "bridge-approach", { 250, -500 } }, { "under-bridge", { 454, -678 } },
    { "past-bridge", { 650, -850 } }, { "beyond", { 1100, -950 } } };
static const size_t stop_count = sizeof(stops) / sizeof(*stops);
#define MAX_TICKS (240 * 120)
#define SAMPLE_TICKS 12 // 10 Hz capture sequence; simulation remains 120 Hz.

static double now(void)
{
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (double)t.tv_sec + t.tv_nsec * 1e-9;
}

static void drive(BoatSession *session, size_t *destination)
{
    const Boat *boat = &session->boat;
    Vector2 delta = Vector2Subtract(stops[*destination].position, boat->position);
    if (Vector2Length(delta) < 12 && *destination + 1 < stop_count) {
        ++*destination;
        delta = Vector2Subtract(stops[*destination].position, boat->position);
    }
    float target = atan2f(delta.x, -delta.y);
    float error = remainderf(target - boat->heading, 2 * PI);
    float wheel = Clamp(1.5f * error - .8f * boat->yaw_velocity, -.65f, .65f);
    float thrust = fabsf(error) > .5f ? .25f : .6f;
    boat_session_step(session,
        (BoatInput) { Clamp((wheel - boat->wheel) / BOAT_STEP, -1, 1),
            Clamp((thrust - boat->throttle) / (.5f * BOAT_STEP), -1, 1) });
}

static bool hull_clear(const Boat *boat)
{
    Vector2 forward = { sinf(boat->heading), -cosf(boat->heading) };
    Vector2 right = { cosf(boat->heading), sinf(boat->heading) };
    // Conservative enclosing rectangle, larger than the playable hull footprint.
    for (int end = -1; end <= 1; end += 2)
        for (int side = -1; side <= 1; side += 2) {
            Vector2 p = Vector2Add(boat->position,
                Vector2Add(Vector2Scale(forward, end * 2.5f), Vector2Scale(right, side * 1.2f)));
            if (!test_river_water(p, .5f))
                return false;
        }
    return true;
}

static int compare_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static float shore_distance(Vector2 position)
{
    float low = 0, high = 1000;
    for (int i = 0; i < 14; ++i) {
        float mid = (low + high) * .5f;
        if (test_river_water(position, mid))
            low = mid;
        else
            high = mid;
    }
    return low;
}

// Draws BOAT as a free-boating view at world TIME.
static bool capture(BoatGpu *gpu, Renderer3D *cpu, BoatWaterMeshCache *cpu_water, const Boat *boat,
    double time, const BoatPresentation *presentation, const BoatWorld *world, BoatCamera camera,
    bool use_gpu, const char *path)
{
    Image image;
    Color pixels[320 * 200];
    const BoatViewState view = { .boat = *boat, .time = time };
    if (use_gpu) {
        boat_gpu_draw(gpu, &view, presentation, world, camera);
        image = LoadImageFromTexture(gpu->target.texture);
        if (!image.data)
            return false;
        ImageFlipVertical(&image);
        ImageFlipHorizontal(&image);
    } else {
        if (!boat_draw(cpu, cpu_water, pixels, 320, 200, &view, presentation, world, camera))
            return false;
        image = (Image) { pixels, 320, 200, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    }
    bool ok = ExportImage(image, path);
    if (use_gpu)
        UnloadImage(image);
    return ok;
}

// A quiet, reflection-free probe exposes mismatched near/far albedo and field bindings.
static int check_gpu_water(void)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "Water surface probe");
    if (!IsWindowReady())
        return 1;
    BoatGpu gpu = { 0 };
    int result = 1;
    if (!boat_gpu_init(&gpu, false))
        goto cleanup;
    waterfront_gpu_scene_set_reflections_enabled(&gpu.scene, false);
    waterfront_gpu_scene_set_shadows_enabled(&gpu.scene, false);
    BoatWater water = boat_water_gentle();
    water.amplitude = 1e-8f;
    water.crossing[0].amplitude = water.crossing[1].amplitude = 0;
    BoatSession session;
    boat_session_init(&session, test_world(BOAT_LOCATION_BASIN), water);
    Boat *boat = &session.boat;
    boat->position = (Vector2) { 0, 0 };
    const BoatCamera view = { .distance = 48, .height = 36 };
    boat_gpu_draw(&gpu, SESSION_VIEW(&session), &session.presentation, &session.world, view);
    Image image = LoadImageFromTexture(gpu.target.texture);
    if (!image.data)
        goto cleanup;
    ImageFlipVertical(&image);
    ImageFlipHorizontal(&image);
    WaterfrontSceneFrame frame;
    boat_scene_frame(SESSION_VIEW(&session), &session.presentation, &session.world, view, &frame);
    Camera3D camera = { frame.camera.position, frame.camera.target, frame.camera.up,
        frame.camera.vertical_fov * RAD2DEG, CAMERA_PERSPECTIVE };
    const Vector3 samples[] = { { 0, 0, -20 }, { 45, 0, -45 }, { 60, 0, -55 } };
    bool same = true;
    for (size_t i = 0; i < sizeof(samples) / sizeof(*samples); ++i) {
        Vector2 p = GetWorldToScreenEx(samples[i], camera, 320, 200);
        int x = 319 - (int)roundf(p.x), y = (int)roundf(p.y);
        if (x < 0 || x >= 320 || y < 0 || y >= 200) {
            same = false;
            continue;
        }
        Color color = GetImageColor(image, x, y);
        printf("water probe %zu: pixel=%d,%d rgb=%d,%d,%d\n", i, x, y, color.r, color.g, color.b);
        same &= abs(color.r - 33) <= 1 && abs(color.g - 71) <= 1 && abs(color.b - 99) <= 1;
    }
    UnloadImage(image);
    if (!same)
        goto cleanup;
    // In the real waterfront, the authored field must vary the surface even with
    // reflections/shadows disabled, and cross the mouth without an abrupt colour step.
    session.world.resources = test_world(BOAT_LOCATION_RIVER);
    session.world.definition = session.world.resources->definition;
    boat->position.x = -12;
    Color first = { 0 }, previous = { 0 }, last = { 0 };
    for (int z = 60; z >= -260; z -= 10) {
        boat->position.y = (float)z;
        boat_gpu_draw(&gpu, SESSION_VIEW(&session), &session.presentation, &session.world, view);
        image = LoadImageFromTexture(gpu.target.texture);
        if (!image.data)
            goto cleanup;
        ImageFlipVertical(&image);
        ImageFlipHorizontal(&image);
        boat_scene_frame(
            SESSION_VIEW(&session), &session.presentation, &session.world, view, &frame);
        camera.position = frame.camera.position;
        camera.target = frame.camera.target;
        Vector2 p = GetWorldToScreenEx((Vector3) { 12, 0, z - 20 }, camera, 320, 200);
        Color color = GetImageColor(image, 319 - (int)roundf(p.x), (int)roundf(p.y));
        UnloadImage(image);
        if (z == 60)
            first = color;
        else if (abs(color.r - previous.r) > 1 || abs(color.g - previous.g) > 1
            || abs(color.b - previous.b) > 1) {
            fprintf(stderr, "Abrupt water-field transition at Z=%d\n", z);
            goto cleanup;
        }
        previous = last = color;
    }
    if (last.g <= first.g || last.b >= first.b) {
        fprintf(stderr, "Water exposure field did not reach both surface treatments.\n");
        goto cleanup;
    }
    puts("gpu_water_surface=ok");
    result = 0;
cleanup:
    boat_gpu_destroy(&gpu);
    CloseWindow();
    return result;
}

static bool route_finished(const Boat *boat, size_t destination)
{
    return destination == stop_count - 1
        && Vector2Distance(boat->position, stops[destination].position) < 12;
}

// Exact physical state shared by --check and --benchmark for same-build parity.
static void format_final_state(char *out, size_t size, const Boat *boat, int tick)
{
    snprintf(out, size,
        "tick=%d x=%a z=%a vx=%a vz=%a heading=%a yaw_velocity=%a height=%a "
        "vertical_velocity=%a pitch=%a roll=%a throttle=%a wheel=%a landings=%u",
        tick, (double)boat->position.x, (double)boat->position.y, (double)boat->velocity.x,
        (double)boat->velocity.y, (double)boat->heading, (double)boat->yaw_velocity,
        (double)boat->height, (double)boat->vertical_velocity, (double)boat->pitch,
        (double)boat->roll, (double)boat->throttle, (double)boat->wheel, boat->landings);
}

#define BENCH_WARMUP 120
#define BENCH_FRAMES (MAX_TICKS / 2 + 2)
#define BENCH_DEADLINE_MS (1000.0 / 60.0)
#define GPU_QUERIES 8

typedef struct {
    int first_tick, end_tick, wave_rebuilds;
    size_t destination, wake_uploads;
    float x, z, heading;
    float simulation_ms, render_ms, present_ms, frame_ms, interval_ms, gpu_ms;
    bool has_interval, has_gpu;
} BenchFrame;

// Asynchronous GL_TIME_ELAPSED samples. A busy slot skips that frame instead of waiting.
typedef struct {
    GLuint ids[GPU_QUERIES];
    int frame[GPU_QUERIES];
    bool supported;
} GpuTimer;

static void gpu_timer_init(GpuTimer *timer)
{
    *timer = (GpuTimer) { .supported = false };
    if (!glGenQueries || !glBeginQuery || !glEndQuery || !glGetQueryiv || !glGetQueryObjectiv
        || !glGetQueryObjectui64v)
        return;
    while (glGetError() != GL_NO_ERROR) { }
    GLint bits = 0;
    glGetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &bits);
    if (bits <= 0)
        return;
    glGenQueries(GPU_QUERIES, timer->ids);
    for (int i = 0; i < GPU_QUERIES; ++i)
        timer->frame[i] = -1;
    timer->supported = glGetError() == GL_NO_ERROR;
}

static bool gpu_timer_begin(GpuTimer *timer, int frame)
{
    int slot = frame % GPU_QUERIES;
    if (!timer->supported || timer->frame[slot] >= 0)
        return false;
    glBeginQuery(GL_TIME_ELAPSED, timer->ids[slot]);
    timer->frame[slot] = frame;
    return true;
}

// Collects finished samples; WAIT is used only after measurement.
static void gpu_timer_poll(GpuTimer *timer, BenchFrame *frames, bool wait)
{
    for (int i = 0; timer->supported && i < GPU_QUERIES; ++i) {
        if (timer->frame[i] < 0)
            continue;
        GLint ready = wait;
        if (!wait)
            glGetQueryObjectiv(timer->ids[i], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready)
            continue;
        GLuint64 elapsed = 0;
        glGetQueryObjectui64v(timer->ids[i], GL_QUERY_RESULT, &elapsed);
        frames[timer->frame[i]].gpu_ms = (float)((double)elapsed / 1e6);
        frames[timer->frame[i]].has_gpu = true;
        timer->frame[i] = -1;
    }
}

static void json_string(FILE *out, const char *text)
{
    fputc('"', out);
    for (; text && *text; ++text) {
        unsigned char c = (unsigned char)*text;
        if (c == '"' || c == '\\')
            fprintf(out, "\\%c", c);
        else if (c < 0x20)
            fprintf(out, "\\u%04x", c);
        else
            fputc(c, out);
    }
    fputc('"', out);
}

// Statistics over measured frames that have a value; null when none has one.
static void json_stats(FILE *out, const char *name, const BenchFrame *frames, int count,
    float *scratch, bool (*select)(const BenchFrame *, float *))
{
    int n = 0;
    for (int i = BENCH_WARMUP; i < count; ++i)
        n += select(&frames[i], &scratch[n]);
    fprintf(out, "  \"%s\":", name);
    if (!n) {
        fputs("null,\n", out);
        return;
    }
    BoatMetricStats s = boat_metric_stats_sort(scratch, n);
    fprintf(out,
        "{\"samples\":%d,\"mean\":%.9g,\"p50\":%.9g,\"p95\":%.9g,\"p99\":%.9g,\"max\":%.9g},\n", n,
        (double)s.mean, (double)s.p50, (double)s.p95, (double)s.p99, (double)s.maximum);
}

#define BENCH_SELECT(name, field, present)                                                         \
    static bool bench_##name(const BenchFrame *frame, float *value)                                \
    {                                                                                              \
        *value = frame->field;                                                                     \
        return present;                                                                            \
    }
BENCH_SELECT(simulation, simulation_ms, true)
BENCH_SELECT(render, render_ms, true)
BENCH_SELECT(present, present_ms, true)
BENCH_SELECT(frame, frame_ms, true)
BENCH_SELECT(interval, interval_ms, frame->has_interval)
BENCH_SELECT(gpu, gpu_ms, frame->has_gpu)

static int bench_fail(const char *message)
{
    fprintf(stderr, "boat_water_review: %s\n", message);
    return 1;
}

// Continuous river route at two ticks per rendered frame. Timed work is simulation, scene
// submission and presentation; route checks, GPU polling and report writing are outside it.
// Visible pacing: "cap" is raylib's SetTargetFPS(60) (the A0.4 baseline), "deadline" the game's
// 60 Hz pacer without vsync and "vsync" the game default (vsync with the pacer as a backstop).
static int benchmark(const char *prefix, bool waves, bool aa, bool visible, const char *pacing)
{
    char csv_path[4096], json_path[4096], state[512];
    snprintf(csv_path, sizeof(csv_path), "%s.csv", prefix);
    snprintf(json_path, sizeof(json_path), "%s.json", prefix);
    // Fail on an unusable prefix before the voyage; both reports are rewritten at the end.
    FILE *csv = fopen(csv_path, "w"), *json = csv ? fopen(json_path, "w") : NULL;
    if (!csv || !json) {
        if (csv) {
            fclose(csv);
            remove(csv_path);
        }
        return bench_fail("cannot create report (parent directory must exist)");
    }
    BenchFrame *frames = calloc(BENCH_FRAMES, sizeof(*frames));
    float *scratch = calloc(BENCH_FRAMES, sizeof(*scratch));
    const BoatWorldDefinition *location = boat_location(BOAT_LOCATION_RIVER);
    BoatSession session;
    boat_session_init(
        &session, test_world(BOAT_LOCATION_RIVER), waves ? boat_water_gentle() : (BoatWater) { 0 });
    const Boat *boat = &session.boat;
    const BoatCamera camera
        = { .distance = location->camera_distance, .height = location->camera_height };
    BoatGpu gpu = { 0 };
    GpuTimer timer = { 0 };
    const char *error = NULL;
    int count = 0, tick = 0, refresh = 0, render_width = 0, render_height = 0;
    size_t destination = 0;
    bool finished = false, ok = true;
    char renderer_name[256] = "", gl_version[256] = "", pacing_name[64] = "unpaced";
    bool vsync = visible && strcmp(pacing, "vsync") == 0;
    FramePacer pacer;
    frame_pacer_init(&pacer, 0);
    if (!frames || !scratch) {
        error = "allocation failed";
        goto cleanup;
    }
    SetConfigFlags(!visible ? FLAG_WINDOW_HIDDEN : vsync ? FLAG_VSYNC_HINT : 0);
    InitWindow(960, 600, "Proteus / boat water benchmark");
    if (!IsWindowReady() || !boat_gpu_init(&gpu, aa)) {
        error = "graphics initialization failed";
        goto cleanup;
    }
    if (visible && strcmp(pacing, "cap") == 0) {
        SetTargetFPS(60);
        snprintf(pacing_name, sizeof(pacing_name), "SetTargetFPS(60)");
    } else if (visible) {
        double hz = frame_pacer_init_window(&pacer);
        snprintf(pacing_name, sizeof(pacing_name), "%s %.6g Hz",
            vsync ? "vsync, deadline backstop" : "deadline", hz);
    }
    refresh = GetMonitorRefreshRate(GetCurrentMonitor());
    render_width = GetRenderWidth();
    render_height = GetRenderHeight();
    snprintf(renderer_name, sizeof(renderer_name), "%s", (const char *)glGetString(GL_RENDERER));
    snprintf(gl_version, sizeof(gl_version), "%s", (const char *)glGetString(GL_VERSION));
    gpu_timer_init(&timer);
    double previous_start = 0;
    for (;;) {
        BenchFrame *frame = &frames[count];
        frame->first_tick = tick;
        double start = boat_profile_time(false);
        if (count > 0)
            do {
                drive(&session, &destination);
                ++tick;
                finished = route_finished(boat, destination);
            } while (tick % 2 && !finished);
        double simulated = boat_profile_time(false);
        int rebuilds = gpu.wave_rebuilds;
        size_t uploads = gpu.wake_uploads;
        bool timed = gpu_timer_begin(&timer, count);
        boat_gpu_draw(&gpu, SESSION_VIEW(&session), &session.presentation, &session.world, camera);
        rlDrawRenderBatchActive();
        if (timed)
            glEndQuery(GL_TIME_ELAPSED);
        double submitted = boat_profile_time(false);
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(gpu.target.texture, (Rectangle) { 0, 0, -320, -200 },
            (Rectangle) { 0, 0, 960, 600 }, (Vector2) { 0 }, 0, WHITE);
        frame_pacer_wait(&pacer);
        EndDrawing();
        double presented = boat_profile_time(false);
        frame->end_tick = tick;
        frame->destination = destination;
        frame->x = boat->position.x;
        frame->z = boat->position.y;
        frame->heading = boat->heading;
        frame->simulation_ms = (float)((simulated - start) * 1000);
        frame->render_ms = (float)((submitted - simulated) * 1000);
        frame->present_ms = (float)((presented - submitted) * 1000);
        frame->frame_ms = (float)((presented - start) * 1000);
        frame->has_interval = count > 0;
        frame->interval_ms = count > 0 ? (float)((start - previous_start) * 1000) : 0;
        frame->wave_rebuilds = gpu.wave_rebuilds - rebuilds;
        frame->wake_uploads = gpu.wake_uploads - uploads;
        previous_start = start;
        gpu_timer_poll(&timer, frames, false);
        ++count;
        // Frame poses only; --check covers every tick and the report's final state proves
        // that this run followed the same trajectory.
        if (!hull_clear(boat)) {
            error = "route too close to shore";
            goto cleanup;
        }
        if (finished)
            break;
        if (tick >= MAX_TICKS || count == BENCH_FRAMES) {
            error = "route did not finish";
            goto cleanup;
        }
        if (visible && WindowShouldClose()) {
            error = "window closed before the route finished";
            goto cleanup;
        }
    }
    gpu_timer_poll(&timer, frames, true);
    if (count <= BENCH_WARMUP) {
        error = "route finished during warmup";
        goto cleanup;
    }
    int gpu_samples = 0, frame_misses = 0, interval_misses = 0, measured_rebuilds = 0;
    for (int i = BENCH_WARMUP; i < count; ++i) {
        gpu_samples += frames[i].has_gpu;
        frame_misses += frames[i].frame_ms > BENCH_DEADLINE_MS;
        interval_misses += frames[i].has_interval && frames[i].interval_ms > BENCH_DEADLINE_MS;
        measured_rebuilds += frames[i].wave_rebuilds;
    }
    fputs("frame,warmup,first_tick,end_tick,ticks,destination,x,z,heading,simulation_ms,"
          "render_submit_ms,present_ms,frame_ms,frame_interval_ms,gpu_ms,"
          "wave_patch_rebuilds,wake_uploads\n",
        csv);
    for (int i = 0; i < count; ++i) {
        const BenchFrame *f = &frames[i];
        char interval[32] = "", gpu_ms[32] = "";
        if (f->has_interval)
            snprintf(interval, sizeof(interval), "%.9g", (double)f->interval_ms);
        if (f->has_gpu)
            snprintf(gpu_ms, sizeof(gpu_ms), "%.9g", (double)f->gpu_ms);
        fprintf(csv, "%d,%d,%d,%d,%d,%zu,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%s,%s,%d,%zu\n", i,
            i < BENCH_WARMUP, f->first_tick, f->end_tick, f->end_tick - f->first_tick,
            f->destination, (double)f->x, (double)f->z, (double)f->heading,
            (double)f->simulation_ms, (double)f->render_ms, (double)f->present_ms,
            (double)f->frame_ms, interval, gpu_ms, f->wave_rebuilds, f->wake_uploads);
    }
    format_final_state(state, sizeof(state), boat, tick);
    fprintf(json,
        "{\n  \"schema_revision\":1,\"tool\":\"boat_water_review\",\"mode\":\"benchmark\",\n"
        "  \"route\":\"river-review\",\"route_revision\":1,\"physics_revision\":%d,\n"
        "  \"build\":\"%s\",\"platform\":\"%s\",\n  \"gl_renderer\":",
        BOAT_PHYSICS_REVISION, BOAT_BENCH_BUILD, BOAT_BENCH_PLATFORM);
    json_string(json, renderer_name);
    fputs(",\"gl_version\":", json);
    json_string(json, gl_version);
    fprintf(json,
        ",\n  \"renderer\":\"gpu\",\"aa\":\"%s\",\"preset\":\"%s\",\"window\":\"%s\",\n"
        "  \"window_size\":[960,600],\"framebuffer_size\":[%d,%d],"
        "\"virtual_resolution\":[320,200],\"internal_resolution\":[%d,%d],\n"
        "  \"pacing\":\"%s\",\"vsync_hint\":%s,\"monitor_refresh_hz\":",
        aa ? "4x4" : "off", waves ? "waves" : "flat", visible ? "visible" : "hidden", render_width,
        render_height, aa ? 1280 : 320, aa ? 800 : 200, pacing_name, vsync ? "true" : "false");
    if (refresh > 0)
        fprintf(json, "%d", refresh);
    else
        fputs("null", json);
    fprintf(json,
        ",\n  \"clock\":\"boat_profile_time(false) platform monotonic\","
        "\"tick_hz\":120,\"ticks_per_frame\":2,\n"
        "  \"frames\":%d,\"warmup_frames\":%d,\"measured_frames\":%d,\"total_ticks\":%d,"
        "\"warmup_end_tick\":%d,\n"
        "  \"route_completed\":true,\"destination\":%zu,\"final_state\":\"%s\",\n"
        "  \"deadline_ms\":%.9g,\"deadline_rule\":\"value > deadline_ms\","
        "\"frame_deadline_misses\":%d,\"interval_deadline_misses\":%d,\n",
        count, BENCH_WARMUP, count - BENCH_WARMUP, tick, frames[BENCH_WARMUP - 1].end_tick,
        destination, state, BENCH_DEADLINE_MS, frame_misses, interval_misses);
    json_stats(json, "simulation_ms", frames, count, scratch, bench_simulation);
    json_stats(json, "render_submit_ms", frames, count, scratch, bench_render);
    json_stats(json, "present_ms", frames, count, scratch, bench_present);
    json_stats(json, "frame_ms", frames, count, scratch, bench_frame);
    json_stats(json, "frame_interval_ms", frames, count, scratch, bench_interval);
    json_stats(json, "gpu_ms", frames, count, scratch, bench_gpu);
    fprintf(json,
        "  \"gpu_timer\":%s,\"gpu_samples\":%d,\"gpu_samples_skipped\":%d,\n"
        "  \"wave_patch_rebuilds\":%d,\"wave_patch_rebuilds_measured\":%d,"
        "\"wave_vertex_capacity\":%d,\"wave_peak_triangles\":%d,\n"
        "  \"app_submissions\":null,\"driver_draw_calls\":null,\"scene_triangles\":null\n}\n",
        timer.supported ? "\"GL_TIME_ELAPSED around boat_gpu_draw, asynchronous\"" : "null",
        gpu_samples, timer.supported ? count - BENCH_WARMUP - gpu_samples : 0, gpu.wave_rebuilds,
        measured_rebuilds, gpu.wave_capacity, gpu.wave_peak_triangles);
    ok = !ferror(csv) && !ferror(json);
    printf("benchmark aa=%s preset=%s window=%s frames=%d warmup=%d ticks=%d gpu_samples=%d\n",
        aa ? "4x4" : "off", waves ? "waves" : "flat", visible ? "visible" : "hidden", count,
        BENCH_WARMUP, tick, gpu_samples);
    printf("final_state %s\n", state);
cleanup:
    if (error)
        ok = false;
    if (fclose(csv))
        ok = false;
    if (fclose(json))
        ok = false;
    if (!ok) {
        remove(csv_path);
        remove(json_path);
    }
    if (IsWindowReady()) {
        if (timer.supported)
            glDeleteQueries(GPU_QUERIES, timer.ids);
        boat_gpu_destroy(&gpu);
        CloseWindow();
    }
    free(frames);
    free(scratch);
    return ok ? 0 : bench_fail(error ? error : "cannot write report");
}

int main(int argc, char **argv)
{
    test_worlds_open();
    const char *output = NULL, *bench = NULL, *aa_name = NULL, *pacing = NULL;
    bool use_gpu = true, waves = false, check = false, sequence = false, visible = false;
    bool gpu_check = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc)
            output = argv[++i];
        else if (strcmp(argv[i], "--benchmark") == 0 && i + 1 < argc)
            bench = argv[++i];
        else if (strcmp(argv[i], "--aa") == 0 && i + 1 < argc)
            aa_name = argv[++i];
        else if (strcmp(argv[i], "--waves") == 0)
            waves = true;
        else if (strcmp(argv[i], "--check-gpu") == 0)
            gpu_check = true;
        else if (strcmp(argv[i], "--check") == 0)
            check = true;
        else if (strcmp(argv[i], "--sequence") == 0)
            sequence = true;
        else if (strcmp(argv[i], "--visible") == 0)
            visible = true;
        else if (strcmp(argv[i], "--pacing") == 0 && i + 1 < argc)
            pacing = argv[++i];
        else if (strcmp(argv[i], "--renderer") == 0 && i + 1 < argc) {
            const char *name = argv[++i];
            if (strcmp(name, "cpu") != 0 && strcmp(name, "gpu") != 0)
                return 2;
            use_gpu = strcmp(name, "gpu") == 0;
        } else {
            puts("Usage: boat_water_review --check [--waves] | --check-gpu\n"
                 "       boat_water_review --output DIR [--renderer cpu|gpu] [--waves]\n"
                 "                         [--sequence] [--visible]\n"
                 "       boat_water_review --benchmark PREFIX [--renderer gpu] --aa off|4x4\n"
                 "                         [--waves] [--visible [--pacing cap|deadline|vsync]]\n"
                 "Visible benchmark pacing: cap = SetTargetFPS(60) (default), deadline = 60 Hz\n"
                 "deadline chain, vsync = the game's vsync with a backstop above the refresh.\n"
                 "Benchmark writes PREFIX.json and PREFIX.csv: two ticks per frame, 120 warmup.\n"
                 "GPU uses existing 4x AA. GPU sequence: 10 Hz; CPU samples: five seconds.\n"
                 "Visible GPU playback uses 60 Hz; simulation remains 120 Hz.\n"
                 "Named captures use both default and close follow cameras.\n"
                 "Map data (c) OpenStreetMap contributors, ODbL 1.0.");
            return strcmp(argv[i], "--help") == 0 ? 0 : 2;
        }
    }
    if (bench || aa_name || pacing) {
        const char *problem = !bench ? "--aa and --pacing require --benchmark"
            : !aa_name               ? "--benchmark requires --aa off|4x4"
            : strcmp(aa_name, "off") && strcmp(aa_name, "4x4") ? "--aa must be off or 4x4"
            : check || gpu_check || output || sequence
            ? "--benchmark excludes --check, --check-gpu, --output and --sequence"
            : !use_gpu ? "--benchmark supports only --renderer gpu"
            : !*bench || strlen(bench) > 4000 || bench[strlen(bench) - 1] == '/'
            ? "--benchmark needs a file prefix of 1..4000 bytes"
            : pacing && !visible ? "--pacing requires --visible"
            : pacing && strcmp(pacing, "cap") && strcmp(pacing, "deadline")
                && strcmp(pacing, "vsync")
            ? "--pacing must be cap, deadline or vsync"
            : NULL;
        if (problem) {
            fprintf(stderr, "boat_water_review: %s\n", problem);
            return 2;
        }
    }
    if (gpu_check) {
        SetTraceLogLevel(LOG_WARNING);
        return check_gpu_water();
    }
    if (!check && !output && !bench)
        return 2;
    SetTraceLogLevel(LOG_WARNING);
    // These samples distinguish navigable space under the deck from solid legs.
    if (!test_river_water((Vector2) { 454, -678 }, 8)
        || !test_river_water((Vector2) { 1100, -950 }, 8)
        || test_river_water((Vector2) { 517.2f, -536.8f }, 0)
        || test_river_water((Vector2) { 486.7f, -527.6f }, 0)
        || test_river_water((Vector2) { 381.5f, -949.2f }, 0)
        || test_river_water((Vector2) { 349.1f, -938.7f }, 0)
        || test_river_water((Vector2) { 2000, -950 }, 0)) {
        fprintf(stderr, "River support/water/crop checks failed.\n");
        return 1;
    }
    if (bench)
        return benchmark(
            bench, waves, strcmp(aa_name, "4x4") == 0, visible, pacing ? pacing : "cap");
    const BoatWorldDefinition *location = boat_location(BOAT_LOCATION_RIVER);
    BoatSession session;
    boat_session_init(
        &session, test_world(BOAT_LOCATION_RIVER), waves ? boat_water_gentle() : (BoatWater) { 0 });
    const BoatWorld *world = &session.world;
    Boat *boat = &session.boat;
    const BoatPresentation *presentation = &session.presentation;
    // Re-aimed review views copy the presentation; the live one keeps following the route.
    static BoatPresentation shown;
    BoatWater water = world->water;
    const BoatWave components[]
        = { { water.amplitude, water.wavelength, water.angular_speed, water.direction },
              water.crossing[0], water.crossing[1] };
    printf("preset=%s cameras=%s resolution=320x200 gpu_aa=4x4\n", waves ? "waves" : "flat",
        "12/6,12/6");
    for (int i = 0; i < 3; ++i)
        printf("wave[%d] amplitude=%.6f wavelength=%.6f angular_speed=%.6f direction=%.6f,%.6f\n",
            i, components[i].amplitude, components[i].wavelength, components[i].angular_speed,
            components[i].direction.x, components[i].direction.y);
    BoatGpu gpu = { 0 };
    Renderer3D cpu = { 0 };
    BoatWaterMeshCache cpu_water = { 0 };
    FILE *csv = NULL;
    char path[2048];
    int result = 1;
    if (!check) {
        if (!DirectoryExists(output) && MakeDirectory(output) != 0)
            goto cleanup;
        snprintf(path, sizeof(path), "%s/route.csv", output);
        csv = fopen(path, "w");
        if (!csv)
            goto cleanup;
        fputs("tick,time,x,z,heading,camera_heading,speed,height,pitch,roll,shore_distance,"
              "destination\n",
            csv);
        if (use_gpu) {
            SetConfigFlags(visible ? 0 : FLAG_WINDOW_HIDDEN);
            InitWindow(960, 600, "Proteus / boat water review");
            if (!IsWindowReady() || !boat_gpu_init(&gpu, true))
                goto cleanup;
            if (visible)
                SetTargetFPS(60);
        }
    }
    size_t destination = 0, captured_destination = 0;
    const BoatCamera default_camera
        = { .distance = location->camera_distance, .height = location->camera_height };
    const BoatCamera review_camera = default_camera;
    printf("sequence_camera=%.1f/%.1f\n", review_camera.distance, review_camera.height);
    int frames = 0, ticks = 0, timing_count = 0;
    double timings[MAX_TICKS / 2 + 1], total = 0;
    for (ticks = 0; ticks <= MAX_TICKS; ++ticks) {
        if (!hull_clear(boat)) {
            fprintf(stderr, "Route too close to shore at tick %d: %.3f %.3f\n", ticks,
                boat->position.x, boat->position.y);
            goto cleanup;
        }
        bool arrival = destination != captured_destination;
        bool finished = route_finished(boat, destination);
        const char *name = ticks == 0 ? "dock"
            : finished                ? "beyond"
            : arrival                 ? stops[destination - 1].name
                                      : NULL;
        if (name) {
            printf("%s tick=%d time=%.3f x=%.3f z=%.3f heading=%.3f\n", name, ticks,
                ticks * (double)BOAT_STEP, boat->position.x, boat->position.y, boat->heading);
            captured_destination = destination;
            if (!check) {
                const BoatCamera cameras[] = { default_camera, { .distance = 12, .height = 6 } };
                for (int view = 0; view < 2; ++view) {
                    snprintf(path, sizeof(path), "%s/%s-%s.png", output, name,
                        view ? "close" : "default");
                    if (!capture(&gpu, &cpu, &cpu_water, boat, boat_session_time(&session),
                            presentation, world, cameras[view], use_gpu, path))
                        goto cleanup;
                }
                if (finished) {
                    shown = *presentation;
                    shown.camera_heading += PI;
                    snprintf(path, sizeof(path), "%s/beyond-lookback.png", output);
                    if (!capture(&gpu, &cpu, &cpu_water, boat, boat_session_time(&session), &shown,
                            world, default_camera, use_gpu, path))
                        goto cleanup;
                    // Fixed inspection views of both landfalls, separate from route poses.
                    const Stop landfalls[] = { { "north-landfall", { 750, -850 } },
                        { "south-landfall", { 500, -700 } } };
                    const Vector2 targets[] = { { 650, -180 }, { 280, -1300 } };
                    for (int bank = 0; bank < 2; ++bank) {
                        Boat inspection = *boat;
                        inspection.position = landfalls[bank].position;
                        shown = *presentation;
                        shown.flag.initialized = false;
                        Vector2 delta = Vector2Subtract(targets[bank], inspection.position);
                        shown.camera_heading = inspection.heading = atan2f(delta.x, -delta.y);
                        if (!hull_clear(&inspection))
                            goto cleanup;
                        snprintf(path, sizeof(path), "%s/%s.png", output, landfalls[bank].name);
                        if (!capture(&gpu, &cpu, &cpu_water, &inspection,
                                boat_session_time(&session), &shown, world, default_camera, use_gpu,
                                path))
                            goto cleanup;
                    }
                    Boat overview = *boat;
                    overview.position = (Vector2) { 425, -762 };
                    shown = *presentation;
                    shown.flag.initialized = false;
                    shown.camera_heading = overview.heading = -PI / 2;
                    snprintf(path, sizeof(path), "%s/connections-overview.png", output);
                    if (!capture(&gpu, &cpu, &cpu_water, &overview, boat_session_time(&session),
                            &shown, world, (BoatCamera) { .distance = 1100, .height = 300 },
                            use_gpu, path))
                        goto cleanup;
                }
            }
        }
        // CPU wave tessellation is deliberately expensive: sample five-second intervals
        // plus every named stop. GPU captures the continuous ten-Hz review sequence.
        int sample_ticks = use_gpu ? (visible ? 2 : SAMPLE_TICKS) : 600;
        if (!check && (ticks % sample_ticks == 0 || finished)) {
            fprintf(csv, "%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.3f,%zu\n", ticks,
                ticks * (double)BOAT_STEP, boat->position.x, boat->position.y, boat->heading,
                presentation->camera_heading, Vector2Length(boat->velocity), boat->height,
                boat->pitch, boat->roll, shore_distance(boat->position), destination);
            double start = now();
            if (use_gpu) {
                boat_gpu_draw(&gpu, SESSION_VIEW(&session), presentation, world, review_camera);
                rlDrawRenderBatchActive();
            } else {
                Color pixels[320 * 200];
                if (!boat_draw(&cpu, &cpu_water, pixels, 320, 200, SESSION_VIEW(&session),
                        presentation, world, review_camera))
                    goto cleanup;
            }
            double ms = (now() - start) * 1000;
            int warmup = use_gpu ? 120 : 2;
            if (frames >= warmup) {
                timings[timing_count++] = ms;
                total += ms;
            }
            if (sequence) {
                snprintf(path, sizeof(path), "%s/frame-%05d.png", output, frames);
                if (!capture(&gpu, &cpu, &cpu_water, boat, boat_session_time(&session),
                        presentation, world, review_camera, use_gpu, path))
                    goto cleanup;
            }
            if (visible && use_gpu) {
                if (WindowShouldClose())
                    goto cleanup;
                BeginDrawing();
                ClearBackground(BLACK);
                DrawTexturePro(gpu.target.texture, (Rectangle) { 0, 0, -320, -200 },
                    (Rectangle) { 0, 0, 960, 600 }, (Vector2) { 0 }, 0, WHITE);
                EndDrawing();
            }
            ++frames;
        }
        if (finished)
            break;
        drive(&session, &destination);
    }
    if (ticks > MAX_TICKS) {
        fprintf(stderr, "Route did not finish; destination=%zu position=%.3f %.3f\n", destination,
            boat->position.x, boat->position.y);
        goto cleanup;
    }
    char state[512];
    format_final_state(state, sizeof(state), boat, ticks);
    printf("final_state %s\n", state);
    if (!check) {
        qsort(timings, (size_t)timing_count, sizeof(*timings), compare_double);
        printf("renderer=%s preset=%s frames=%d warmup=%d mean_ms=%.3f p95_ms=%.3f max_ms=%.3f\n",
            use_gpu ? "gpu-aa submission" : "cpu", waves ? "waves" : "flat", frames,
            use_gpu ? 120 : 2, timing_count ? total / timing_count : 0,
            timing_count ? timings[(timing_count - 1) * 95 / 100] : 0,
            timing_count ? timings[timing_count - 1] : 0);
        const Mesh3D *surface = use_gpu
            ? NULL
            : boat_water_surface(&cpu_water, world->definition->map, (BoatWater) { 0 }, 0);
        const Mesh3D *city = waterfront_city_mesh();
        size_t water_count = 0;
        while (water_count < city->triangle_count) {
            const Triangle3D *t = &city->triangles[water_count];
            if (city->vertices[t->vertices[0]].y != 0 || city->vertices[t->vertices[1]].y != 0
                || city->vertices[t->vertices[2]].y != 0)
                break;
            ++water_count;
        }
        printf("water_base_triangles=%zu wave_triangles=%zu gpu_wave_vertex_bytes=%zu\n",
            water_count,
            use_gpu       ? (size_t)gpu.wave_peak_triangles
                : surface ? surface->triangle_count
                          : 0,
            use_gpu ? (size_t)gpu.wave_capacity * 3 * sizeof(float) : 0);
        if (use_gpu)
            printf("wave_patch_rebuilds=%d vertex_capacity=%d\n", gpu.wave_rebuilds,
                gpu.wave_capacity);
    }
    result = 0;
cleanup:
    if (csv)
        fclose(csv);
    boat_water_mesh_cache_destroy(&cpu_water);
    renderer3d_destroy(&cpu);
    if (!check && use_gpu && IsWindowReady()) {
        boat_gpu_destroy(&gpu);
        CloseWindow();
    }
    test_worlds_close();
    return result;
}
