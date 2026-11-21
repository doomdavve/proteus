// Manual measurement, not a test (Marina M5 W6): the cost of a marina package, in the form the
// #131 harbor-life qualification used. Reads PACKAGE, stages it, then renders 180 CPU frames at
// 320x200 from one camera while simulated time advances one 60 Hz frame at a time. Wall-clock
// readings are printed for a report; nothing here asserts on them and CTest does not run it.
//
//   measure_marina_package PACKAGE [--time SECONDS] [--camera X,Z,DEGREES] [--frames N]
//                                   [--stage-samples N] [--worker PATH]
//
// Defaults reproduce the #131 sweep: camera (2, 161) at 90 degrees, time 0..3 s, 180 frames.
#include "core/boat_map.h"
#include "plugins/boat_plugin_host.h"
#include "plugins/boat_plugin_io.h"
#include "plugins/boat_plugin_load.h"
#include "plugins/boat_plugin_stage.h"
#include "raylib.h"
#include "render/boat_view.h"
#include "render/waterfront_scene.h"
#include "session/boat_session.h"
#include "support/boat_test_scenery.h"
#include "support/boat_test_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WIDTH 320
#define HEIGHT 200
#define MAX_SAMPLES 1000

static double now_ms(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int compare_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double percentile(double *values, int count, double fraction)
{
    qsort(values, (size_t)count, sizeof(*values), compare_double);
    return values[(int)(fraction * (count - 1) + .5)];
}

static void summary(const char *name, double *samples, int count)
{
    printf("%s samples=%d p50_ms=%.3f p95_ms=%.3f max_ms=%.3f\n", name, count,
        percentile(samples, count, .5), percentile(samples, count, .95), samples[count - 1]);
}

static int render(const BoatSceneDefinition *scene, const BoatSession *source, const char *label,
    Vector2 camera, float heading, double start, int frames, WaterfrontSceneFrame *snapshot)
{
    static Color pixels[WIDTH * HEIGHT];
    static double samples[MAX_SAMPLES];
    Renderer3D renderer = { 0 };
    BoatCpuView cpu = { 0 };
    BoatWorld world = source->world;
    BoatViewState view = boat_session_view(source);
    view.scene = scene;
    BoatPresentation presentation = { .camera_heading = heading };
    view.boat.position = camera;
    view.boat.heading = heading;
    view.boat.velocity = (Vector2) { 0 };
    view.boat.yaw_velocity = 0;
    for (int frame = 0; frame < frames; ++frame) {
        view.time = start + frame / 60.0;
        double begin = now_ms();
        if (!boat_draw_scene(&renderer, &cpu, pixels, WIDTH, HEIGHT, &view, &presentation, &world,
                BOAT_CAMERA_DEFAULT, NULL, NULL, NULL, 0)) {
            boat_cpu_view_destroy(&cpu);
            return 1;
        }
        samples[frame] = now_ms() - begin;
    }
    boat_scene_frame(&view, &presentation, &world, BOAT_CAMERA_DEFAULT, snapshot);
    printf("render %s hull_cpu_bytes=%zu hull_builds=%u berths=%d modules=%d vessels=%d wakes=%d "
           "gulls=%d\n",
        label, waterfront_harbor_models_bytes(&cpu.harbor), cpu.harbor.builds,
        snapshot->harbor.berth_count, snapshot->harbor.module_count, snapshot->traffic.vessel_count,
        snapshot->traffic.wake_count, snapshot->gull_count);
    boat_cpu_view_destroy(&cpu);
    char name[96];
    snprintf(name, sizeof(name), "render_%s", label);
    summary(name, samples, frames);
    return 0;
}

int main(int argc, char **argv)
{
    const char *package = NULL, *worker = "build/boat_plugin_worker";
    double start = 0;
    Vector2 camera = { 2, 161 };
    float heading = 1.5707964f;
    int frames = 180, stage_samples = 20;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--time") && i + 1 < argc) {
            start = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--camera") && i + 1 < argc) {
            double x, z, degrees;
            if (sscanf(argv[++i], "%lf,%lf,%lf", &x, &z, &degrees) != 3)
                goto usage;
            camera = (Vector2) { (float)x, (float)z };
            heading = (float)(degrees * 3.14159265358979323846 / 180.0);
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frames = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--stage-samples") && i + 1 < argc) {
            stage_samples = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--worker") && i + 1 < argc) {
            worker = argv[++i];
        } else if (argv[i][0] != '-' && !package) {
            package = argv[i];
        } else {
            goto usage;
        }
    }
    if (!package || frames < 1 || frames > MAX_SAMPLES || stage_samples < 1
        || stage_samples > MAX_SAMPLES)
        goto usage;

    test_scenery_load();
    static BoatPluginFiles files;
    BoatPluginDiagnostic d;
    if (boat_plugin_read(package, NULL, &files, &d) != BOAT_PLUGIN_OK) {
        fprintf(stderr, "cannot read %s: %s: %s\n", package, d.file, d.message);
        return 1;
    }
    const BoatWorldDefinition *location = boat_location_find("outer");
    BoatWorldResources resources;
    if (!boat_world_resources_init(&resources, location))
        return 1;
    BoatPluginBundle bundle = boat_plugin_bundle(&files);
    static BoatPluginHost host;
    double begin = now_ms();
    if (!boat_plugin_host_start(&host, resources.definition, &resources.map, &bundle)) {
        fprintf(stderr, "stage failed: %s: %s\n", host.diagnostic.file, host.diagnostic.message);
        return 1;
    }
    printf("package=%s source_bytes=%zu manifest_bytes=%zu\n", package, files.source_size,
        files.manifest_size);
    printf("initial_stage_ms=%.3f\n", now_ms() - begin);
    // One evaluation's cost, as the stage worker reports it: instructions, fields and Lua heap peak.
    static BoatPluginVoyage voyage;
    if (boat_plugin_load_voyage(&bundle, NULL, &voyage, &d) != BOAT_PLUGIN_OK)
        return 1;
    BoatPluginCost cost = d.cost;
    printf("lua_cost peak_bytes=%zu instructions=%llu entry_instructions=%llu fields=%llu "
           "leaked=%zu\n",
        cost.peak_bytes, (unsigned long long)cost.instructions,
        (unsigned long long)cost.entry_instructions, (unsigned long long)cost.fields,
        cost.leaked_bytes);

    // Repeated stages of the unchanged package: the in-process path, then the worker process.
    static double samples[MAX_SAMPLES];
    for (int i = 0; i < stage_samples; ++i) {
        begin = now_ms();
        if (!boat_plugin_host_stage(&host, &bundle))
            return 1;
        samples[i] = now_ms() - begin;
        if (host.held)
            boat_plugin_host_discard(&host);
    }
    summary("stage_in_process", samples, stage_samples);
    for (int i = 0; i < stage_samples; ++i) {
        begin = now_ms();
        BoatPluginStageJob *job = boat_plugin_stage_start(worker, &host, &files, 0);
        if (!job) {
            fprintf(stderr, "cannot start %s\n", worker);
            return 1;
        }
        int result;
        while (!(result = boat_plugin_stage_poll(job, &host, .25))) { }
        boat_plugin_stage_close(job);
        samples[i] = now_ms() - begin;
        if (result != 1)
            return 1;
        if (host.held)
            boat_plugin_host_discard(&host);
    }
    summary("stage_worker_process", samples, stage_samples);
    BoatPluginHostMemory memory = boat_plugin_host_memory(&host);
    printf("host_reserved=%zu retained_revisions=%u retained_source=%zu sizeof_host=%zu "
           "sizeof_scene=%zu\n",
        memory.reserved_bytes, memory.retained_revisions, memory.retained_source_bytes,
        sizeof(BoatPluginHost), sizeof(BoatSceneDefinition));

    const BoatSceneDefinition *scene = &boat_plugin_host_active(&host)->voyage.scene;
    printf("scene berths=%u modules=%u traffic=%u vessels=%u services=%u flocks=%u\n",
        scene->berth_count, scene->module_count, scene->traffic.actor_count, scene->vessel_count,
        scene->service_count, scene->gulls.flock_count);
    BoatSession session, compiled_session;
    physics_session(&session, BOAT_LOCATION_OUTER);
    session.world.scene = scene;
    physics_session(&compiled_session, BOAT_LOCATION_OUTER);
    printf("camera=%.1f,%.1f heading=%.4f time=%.3f..%.3f frames=%d size=%dx%d\n", camera.x,
        camera.y, heading, start, start + (frames - 1) / 60.0, frames, WIDTH, HEIGHT);
    WaterfrontSceneFrame frame;
    if (render(NULL, &compiled_session, "compiled", camera, heading, start, frames, &frame)
        || render(scene, &session, "package", camera, heading, start, frames, &frame))
        return 1;
    boat_session_destroy(&session);
    boat_session_destroy(&compiled_session);
    boat_world_resources_destroy(&resources);
    return 0;
usage:
    fprintf(stderr,
        "Usage: measure_marina_package PACKAGE [--time SECONDS] [--camera X,Z,DEGREES]\n"
        "       [--frames N] [--stage-samples N] [--worker PATH]\n");
    return 2;
}
