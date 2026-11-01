// L4.3 first-playable harbor qualification fixture. It loads the real package, drives the
// validated marina approach/idle/departure inputs, checks the combined actors, and measures a
// CPU frame distribution for the compiled and populated scenes at one camera/time sweep.
#include "boat_map.h"
#include "boat_plugin_host.h"
#include "boat_plugin_io.h"
#include "boat_session.h"
#include "boat_test_scenery.h"
#include "boat_test_session.h"
#include "boat_view.h"
#include "raymath.h"
#include "waterfront_scene.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "Harbor-life check failed at %d: %s\n", __LINE__, #test);              \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static double now_ms(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC); // C11 portable; no POSIX clock feature macro needed.
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
    int index = (int)(fraction * (count - 1) + .5);
    return values[index];
}

static bool player_clear(const BoatSession *session)
{
    const Boat *boat = &session->boat;
    if (!boat_map_water(&session->world.resources->map, boat->position, .45f))
        return false;
    return !waterfront_harbor_contact_height_scene(session->world.scene, boat->position,
               boat->heading, -.4f, 2, session->world.water, boat_session_time(session), NULL)
        && !waterfront_traffic_contact_height_scene(session->world.scene, boat->position,
            boat->heading, 2, 1.6f, .75f, -.4f, 2, session->world.water, boat_session_time(session),
            NULL);
}

static int drive_approach(BoatSession *session)
{
    session->boat.position = (Vector2) { 4, 132 };
    session->boat.heading = (float)PI; // Forward +z, north into the marina.
    for (int tick = 0; tick < 480; ++tick) {
        BoatInput input = { .throttle_rate = tick < 300 ? .5f : 0 };
        boat_session_step(session, input);
        CHECK(player_clear(session));
        CHECK(isfinite(session->boat.position.x) && isfinite(session->boat.position.y));
    }
    CHECK(session->boat.position.y > 150);
    return 0;
}

static int drive_departure(BoatSession *session)
{
    session->boat.heading = 0; // Forward -z, back out through the entrance.
    float start = session->boat.position.y;
    for (int tick = 0; tick < 720; ++tick) {
        boat_session_step(session, (BoatInput) { .throttle_rate = .45f });
        CHECK(player_clear(session));
    }
    CHECK(start - session->boat.position.y > 5);
    return 0;
}

static int measure_render(const BoatSceneDefinition *scene, const BoatSession *source,
    const char *label, double *p50, double *p95, WaterfrontSceneFrame *snapshot)
{
    enum { WIDTH = 320, HEIGHT = 200, FRAMES = 180 };
    static Color pixels[WIDTH * HEIGHT];
    static double samples[FRAMES];
    Renderer3D renderer = { 0 };
    BoatCpuView cpu = { 0 };
    BoatWorld world = source->world;
    BoatViewState view = boat_session_view(source);
    view.scene = scene;
    BoatPresentation presentation = { .camera_heading = 1.5707964f };
    view.boat.position = (Vector2) { 2, 161 };
    view.boat.heading = 1.5707964f;
    view.boat.velocity = (Vector2) { 0 };
    view.boat.yaw_velocity = 0;
    for (int frame = 0; frame < FRAMES; ++frame) {
        view.time = frame / 60.0;
        double start = now_ms();
        if (!boat_draw_scene(&renderer, &cpu, pixels, WIDTH, HEIGHT, &view, &presentation, &world,
                BOAT_CAMERA_DEFAULT, NULL, NULL, NULL, 0)) {
            boat_cpu_view_destroy(&cpu);
            return 1;
        }
        samples[frame] = now_ms() - start;
    }
    *p50 = percentile(samples, FRAMES, .50);
    *p95 = percentile(samples, FRAMES, .95);
    boat_scene_frame(&view, &presentation, &world, BOAT_CAMERA_DEFAULT, snapshot);
    boat_cpu_view_destroy(&cpu);
    printf("render %s p50_ms=%.3f p95_ms=%.3f frames=%d\n", label, *p50, *p95, FRAMES);
    return 0;
}

int main(void)
{
    test_scenery_load();
    BoatPluginFiles files;
    BoatPluginDiagnostic diagnostic;
    CHECK(boat_plugin_read("plugins/harbor-life", NULL, &files, &diagnostic) == BOAT_PLUGIN_OK);
    const BoatWorldDefinition *location = boat_location_find("outer");
    BoatWorldResources resources;
    CHECK(boat_world_resources_init(&resources, location));
    BoatPluginBundle bundle = boat_plugin_bundle(&files);
    BoatPluginHost host;
    CHECK(boat_plugin_host_start(&host, resources.definition, &resources.map, &bundle));
    const BoatPluginRevision *active = boat_plugin_host_active(&host);
    CHECK(active->voyage.has_scene);
    const BoatSceneDefinition *scene = &active->voyage.scene;
    CHECK(scene->berth_count == 7 && scene->module_count == 3);
    CHECK(scene->traffic.actor_count == 4 && scene->gulls.flock_count == 1);

    BoatSession session;
    physics_session(&session, BOAT_LOCATION_OUTER);
    session.world.scene = scene;
    CHECK(drive_approach(&session) == 0);
    CHECK(drive_departure(&session) == 0);
    CHECK(waterfront_flock_has_content(&scene->gulls)
        && waterfront_route_has_bridge(&scene->traffic));

    // Inspection values from the same accepted scene and explicit time.
    BoatViewState view = boat_session_view(&session);
    view.scene = scene;
    view.boat.position = (Vector2) { 2, 161 };
    view.boat.heading = 1.5707964f;
    view.time = 9;
    BoatPresentation presentation = { .camera_heading = 1.5707964f };
    WaterfrontSceneFrame frame;
    boat_scene_frame(&view, &presentation, &session.world, BOAT_CAMERA_DEFAULT, &frame);
    CHECK(frame.harbor.berth_count == 7 && frame.harbor.module_count == 3);
    CHECK(frame.traffic.vessel_count == 4 && frame.gull_count == 8);
    CHECK(frame.traffic.wake_count > 0);

    // Feeding at 3 s is lower than the alarm climb at 9 s for the authored flock.
    WaterfrontSceneFrame feeding, alarm;
    waterfront_boating_gulls_scene(scene, 3, frame.player.hull.position, (Vector3) { 0 }, &feeding);
    waterfront_boating_gulls_scene(scene, 9, frame.player.hull.position, (Vector3) { 0 }, &alarm);
    CHECK(feeding.gull_count == 8 && alarm.gull_count == 8);
    CHECK(alarm.gulls[0][0].position.y > feeding.gulls[0][0].position.y + 1);

    // One complete ferry dwell/departure from the authored stopped route.
    double arrive, depart;
    CHECK(waterfront_route_stop_times(&scene->traffic, 1, 0, &arrive, &depart));
    WaterfrontVesselMotion at_stop = waterfront_traffic_motion_routes(&scene->traffic, 1, arrive);
    WaterfrontVesselMotion after = waterfront_traffic_motion_routes(&scene->traffic, 1, depart + 2);
    CHECK(Vector3Length(at_stop.velocity) < .01f && Vector3Length(after.velocity) > .1f);
    for (int t = 0; t <= 1200; t += 5) {
        WaterfrontVesselMotion m = waterfront_traffic_motion_routes(&scene->traffic, 1, t);
        CHECK(isfinite(m.position.x) && isfinite(m.position.z));
    }

    double baseline50, baseline95, populated50, populated95;
    WaterfrontSceneFrame compiled_frame, populated_frame;
    BoatSession compiled_session;
    physics_session(&compiled_session, BOAT_LOCATION_OUTER);
    CHECK(measure_render(
              NULL, &compiled_session, "compiled", &baseline50, &baseline95, &compiled_frame)
        == 0);
    CHECK(measure_render(scene, &session, "populated", &populated50, &populated95, &populated_frame)
        == 0);
    // The same camera/input at the supported maxima exercises the bounded worst case without
    // changing the installed package: 7 berths, 4 modules, 8 craft and 4 x 8 birds.
    BoatSceneDefinition maximum = *scene;
    while (maximum.berth_count < BOAT_SCENE_BERTHS) {
        unsigned next = maximum.berth_count;
        maximum.berths[next] = maximum.berths[next % 2];
        maximum.berth_count = next + 1;
    }
    maximum.modules_present = true;
    maximum.module_count = BOAT_SCENE_MODULES;
    static const unsigned char module_kinds[BOAT_SCENE_MODULES] = { BOAT_SCENE_MODULE_PONTOON,
        BOAT_SCENE_MODULE_PONTOON_SMALL, BOAT_SCENE_MODULE_QUAY, BOAT_SCENE_MODULE_PONTOON };
    for (unsigned i = 0; i < BOAT_SCENE_MODULES; ++i) {
        maximum.modules[i] = scene->modules[i % scene->module_count];
        maximum.modules[i].kind = module_kinds[i];
        snprintf(maximum.modules[i].id, sizeof(maximum.modules[i].id), "m%u", i);
    }
    maximum.traffic.actor_count = WATERFRONT_ROUTE_ACTORS;
    for (unsigned i = scene->traffic.actor_count; i < WATERFRONT_ROUTE_ACTORS; ++i) {
        maximum.traffic.actors[i] = scene->traffic.actors[i % scene->traffic.actor_count];
        snprintf(maximum.traffic.actors[i].id, sizeof(maximum.traffic.actors[i].id), "v%u", i);
    }
    maximum.gulls.flock_count = WATERFRONT_FLOCKS;
    for (unsigned i = 1; i < WATERFRONT_FLOCKS; ++i) {
        maximum.gulls.flocks[i] = scene->gulls.flocks[0];
        snprintf(maximum.gulls.flocks[i].id, sizeof(maximum.gulls.flocks[i].id), "f%u", i);
    }
    double max50, max95;
    WaterfrontSceneFrame max_frame;
    CHECK(measure_render(&maximum, &session, "max", &max50, &max95, &max_frame) == 0);
    BoatPluginHostMemory memory = boat_plugin_host_memory(&host);
    unsigned meshes = (unsigned)(frame.harbor.berth_count + frame.harbor.module_count
        + frame.traffic.vessel_count + frame.gull_count * WATERFRONT_GULL_PARTS);
    unsigned lights
        = (unsigned)(frame.lamp_count + frame.traffic.vessel_count * WATERFRONT_TRAFFIC_LIGHTS);
    printf("harbor_life=ok berths=%d modules=%d vessels=%d flocks=%u birds=%d wakes=%d "
           "meshes=%u lights=%u cars=%d host_revisions=%u host_bytes=%zu retained_source=%zu "
           "baseline_p50=%.3f baseline_p95=%.3f populated_p50=%.3f populated_p95=%.3f "
           "max_p50=%.3f max_p95=%.3f max_bytes=%zu\n",
        frame.harbor.berth_count, frame.harbor.module_count, frame.traffic.vessel_count,
        scene->gulls.flock_count, frame.gull_count, frame.traffic.wake_count, meshes, lights,
        WATERFRONT_CAR_COUNT, memory.retained_revisions, memory.reserved_bytes,
        memory.retained_source_bytes, baseline50, baseline95, populated50, populated95, max50,
        max95, sizeof(maximum));
    boat_session_destroy(&session);
    boat_session_destroy(&compiled_session);
    boat_world_resources_destroy(&resources);
    return 0;
}
