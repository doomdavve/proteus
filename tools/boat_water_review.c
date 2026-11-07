// Deterministic boat-level water audit. Uses the playable simulation, not a camera spline.
#include "boat_test_scenery.h"
#include "external/glad.h"

#include "boat_assets.h"
#include "boat_fleet.h"
#include "boat_gpu.h"
#include "boat_map.h"
#include "boat_profile.h"
#include "boat_rearview.h"
#include "boat_screen.h"
#include "boat_test_maps.h"
#include "boat_test_session.h"
#include "frame_pacer.h"
#include "raymath.h"
#include "rlgl.h"
#include "waterfront_traffic.h"

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
// The outer map shares the river's frame: the river route, then out across the estuary, where the
// whole outer crop's terrain and buildings are in view.
static const Stop outer_stops[] = { { "mouth", { -24, -70 } }, { "river", { -15, -180 } },
    { "bridge-approach", { 250, -500 } }, { "under-bridge", { 454, -678 } },
    { "past-bridge", { 650, -850 } }, { "beyond", { 1100, -950 } }, { "estuary", { 1800, -1150 } },
    { "fjord", { 2700, -1500 } } };
typedef struct {
    const char *name; // Scenario and route name in benchmark reports.
    BoatLocationId location;
    const Stop *stops;
    size_t count;
} Route;
static const Route river_route
    = { "river-review", TEST_RIVER, stops, sizeof(stops) / sizeof(*stops) };
static const Route outer_route = { "outer-review", BOAT_LOCATION_OUTER, outer_stops,
    sizeof(outer_stops) / sizeof(*outer_stops) };
#define MAX_TICKS (240 * 120)
#define SAMPLE_TICKS 12 // 10 Hz capture sequence; simulation remains 120 Hz.

static double now(void)
{
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (double)t.tv_sec + t.tv_nsec * 1e-9;
}

static void drive(BoatSession *session, const Route *route, size_t *destination)
{
    const Boat *boat = &session->boat;
    Vector2 delta = Vector2Subtract(route->stops[*destination].position, boat->position);
    if (Vector2Length(delta) < 12 && *destination + 1 < route->count) {
        ++*destination;
        delta = Vector2Subtract(route->stops[*destination].position, boat->position);
    }
    float target = atan2f(delta.x, -delta.y);
    float error = remainderf(target - boat->heading, 2 * PI);
    float wheel = Clamp(1.5f * error - .8f * boat->yaw_velocity, -.65f, .65f);
    float thrust = fabsf(error) > .5f ? .25f : .6f;
    boat_session_step(session,
        (BoatInput) { .steering_rate = Clamp((wheel - boat->wheel) / BOAT_STEP, -1, 1),
            .throttle_rate
            = Clamp((thrust - boat->throttle) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP), -1, 1) });
}

static bool hull_clear(const Boat *boat, BoatLocationId location)
{
    Vector2 forward = { sinf(boat->heading), -cosf(boat->heading) };
    Vector2 right = { cosf(boat->heading), sinf(boat->heading) };
    // Conservative enclosing rectangle, larger than the playable hull footprint.
    for (int end = -1; end <= 1; end += 2)
        for (int side = -1; side <= 1; side += 2) {
            Vector2 p = Vector2Add(boat->position,
                Vector2Add(Vector2Scale(forward, end * 2.5f), Vector2Scale(right, side * 1.2f)));
            if (!boat_map_water(&test_world(location)->map, p, .5f))
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
static bool capture(BoatGpu *gpu, Renderer3D *cpu, BoatCpuView *cpu_view, const Boat *boat,
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
        if (!boat_draw(cpu, cpu_view, pixels, 320, 200, &view, presentation, world, camera))
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
    // Gust patches cross the walk below by design; this probes the exposure field alone.
    waterfront_gpu_scene_set_far_water_enabled(&gpu.scene, false);
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
    session.world.resources = test_world(TEST_RIVER);
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

// Captures one GPU view's frame of SESSION; the caller unloads the image.
static bool gpu_frame(BoatGpu *gpu, const BoatSession *session, Image *image)
{
    boat_gpu_draw(
        gpu, SESSION_VIEW(session), &session->presentation, &session->world, BOAT_CAMERA_DEFAULT);
    *image = gpu->failed ? (Image) { 0 } : LoadImageFromTexture(gpu->target.texture);
    return image->data != NULL;
}

static bool same_image(Image a, Image b)
{
    return a.width == b.width && a.height == b.height && a.format == b.format
        && memcmp(a.data, b.data, (size_t)GetPixelDataSize(a.width, a.height, a.format)) == 0;
}

// A1.4: each GPU view binds its scene to the map it draws. Two views draw the river and the
// synthetic harbor interleaved; destroying or recreating one leaves the other's frame unchanged.
static bool harbor_appearance_owners(WaterfrontGpuScene *a, WaterfrontGpuScene *b)
{
    static BoatSceneDefinition definition = { .version = BOAT_SCENE_VERSION,
        .berths_present = true,
        .berth_count = 2,
        .berths = { { .recipe = BOAT_SCENE_BERTH_RUNABOUT,
                        .appearance = { .length = 5.2f, .name = "SEA-1" } },
            { .recipe = BOAT_SCENE_BERTH_RUNABOUT,
                .appearance = { .length = 6.6f, .name = "SEA-2" } } } };
    WaterfrontHarborFrame frame;
    waterfront_harbor_evaluate_scene(&definition, (BoatWater) { 0 }, 0, NULL, &frame);
    if (!waterfront_gpu_scene_bind_harbor(a, &frame)
        || !waterfront_gpu_scene_bind_harbor(b, &frame))
        return false;
    unsigned av = a->harbor_custom[0].vaoId, bv = b->harbor_custom[0].vaoId;
    unsigned uploads = a->harbor_uploads;
    if (!av || !bv || av == bv || a->harbor_models.boats[0] == b->harbor_models.boats[0]
        || !waterfront_gpu_scene_bind_harbor(a, &frame) || a->harbor_uploads != uploads)
        return false;
    frame.specs[0].hull.r++;
    frame.specs[1].hull.r++;
    a->harbor_upload_limit = 1; // The second upload fails after the first has succeeded.
    bool rejected = !waterfront_gpu_scene_bind_harbor(a, &frame);
    a->harbor_upload_limit = 0;
    if (!rejected || a->harbor_custom[0].vaoId != av || a->harbor_uploads != uploads
        || b->harbor_custom[0].vaoId != bv)
        return false;
    for (unsigned cycle = 0; cycle < 32; ++cycle) {
        frame.berth_count = 2;
        frame.specs[0].hull.r = (unsigned char)cycle;
        if (!waterfront_gpu_scene_bind_harbor(a, &frame) || b->harbor_custom[0].vaoId != bv)
            return false;
        frame.berth_count = cycle % 2;
        if (!waterfront_gpu_scene_bind_harbor(a, &frame) || a->harbor_custom[1].vaoId
            || (frame.berth_count == 0 && a->harbor_custom[0].vaoId))
            return false;
    }
    frame.berth_count = BOAT_SCENE_BERTHS;
    for (unsigned i = 0; i < BOAT_SCENE_BERTHS; ++i) {
        frame.recipes[i] = BOAT_SCENE_BERTH_RUNABOUT;
        frame.specs[i] = frame.specs[0];
        frame.specs[i].hull.r = (unsigned char)i;
    }
    if (!waterfront_gpu_scene_bind_harbor(a, &frame))
        return false;
    size_t vertices = 0;
    for (unsigned i = 0; i < BOAT_SCENE_BERTHS; ++i) {
        if (!a->harbor_custom[i].vaoId)
            return false;
        vertices += (size_t)a->harbor_custom[i].vertexCount;
    }
    uploads = a->harbor_uploads;
    if (!waterfront_gpu_scene_bind_harbor(a, &frame) || a->harbor_uploads != uploads)
        return false;
    printf("gpu_harbor_appearance=ok cycles=32 max_vertices=%zu retained_cpu=%zu uploads=%u\n",
        vertices, waterfront_harbor_models_bytes(&a->harbor_models), uploads);
    return true;
}

static int check_gpu_views(void)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "View binding probe");
    if (!IsWindowReady())
        return 1;
    const BoatWorldDefinition definition = test_harbor_definition(test_location(TEST_RIVER));
    BoatWorldResources harbor = { 0 };
    BoatGpu a = { 0 }, b = { 0 };
    Image first = { 0 }, frame = { 0 }, harbor_frame = { 0 };
    BoatSession river, other;
    int result = 1, harbor_vertices = 0;
    bool sessions = false;
    if (!boat_world_resources_init(&harbor, &definition) || !boat_gpu_init(&a, false)
        || !boat_gpu_init(&b, false))
        goto cleanup;
    boat_session_init(&river, test_world(TEST_RIVER), (BoatWater) { 0 });
    boat_session_init(&other, &harbor, (BoatWater) { 0 });
    sessions = true;
#define EXPECT(test)                                                                               \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
    EXPECT(gpu_frame(&a, &river, &first));
    EXPECT(harbor_appearance_owners(&a.scene, &b.scene));
    EXPECT(a.scene.map == &waterfront_city_river_map && a.scene.river && a.scene.crane.vaoId);
    EXPECT(gpu_frame(&b, &other, &harbor_frame));
    EXPECT(b.scene.map == &test_harbor_map && !b.scene.river && !b.scene.crane.vaoId);
    EXPECT(b.scene.water.vaoId
        && waterfront_gpu_tiled_vertices(&b.scene.corner)
            < waterfront_gpu_tiled_vertices(&a.scene.corner));
    harbor_vertices = waterfront_gpu_tiled_vertices(&b.scene.corner);
    EXPECT(!same_image(first, harbor_frame));
    // B rebinds to the river and back: both frames repeat exactly.
    EXPECT(gpu_frame(&b, &river, &frame) && same_image(first, frame));
    UnloadImage(frame);
    EXPECT(gpu_frame(&b, &other, &frame) && same_image(harbor_frame, frame));
    UnloadImage(frame);
    // Destroy and recreate B with the context alive; A keeps drawing the same river frame.
    boat_gpu_destroy(&b);
    boat_gpu_destroy(&b);
    EXPECT(!b.scene.map && !b.scenery.ready);
    EXPECT(gpu_frame(&a, &river, &frame) && same_image(first, frame));
    UnloadImage(frame);
    EXPECT(boat_gpu_init(&b, false) && gpu_frame(&b, &river, &frame) && same_image(first, frame));
    UnloadImage(frame);
    frame = (Image) { 0 };
#undef EXPECT
    printf("gpu_view_binding=ok harbor_corner_vertices=%d river_corner_vertices=%d\n",
        harbor_vertices, waterfront_gpu_tiled_vertices(&a.scene.corner));
    result = 0;
cleanup:
    if (frame.data)
        UnloadImage(frame);
    if (first.data)
        UnloadImage(first);
    if (harbor_frame.data)
        UnloadImage(harbor_frame);
    if (sessions) {
        boat_session_destroy(&river);
        boat_session_destroy(&other);
    }
    boat_gpu_destroy(&a);
    boat_gpu_destroy(&b);
    boat_world_resources_destroy(&harbor);
    CloseWindow();
    return result;
}

static bool prepare_tiles(WaterfrontScenery *scenery, float tile_size);

// A2.3: per-tile culling removes only geometry outside each pass's volume. At every route stop,
// in four headings and three cameras, both tile sizes draw the same GPU (with waves) and CPU
// (flat water, whose full-map wave surface would dominate the run) pixels with culling on and
// off, and each pass culls some tiles somewhere.
// --no-detail: review and benchmark runs draw the near level of every detail cell.
static bool no_detail;
// --analytic-wakes: shade object wakes with the reference loop instead of the stamped field.
// --stamp-size/--stamp-spacing: another field for measurement (default 1024 texels, 0.25 m).
static bool analytic_wakes;
static int stamp_size = WATERFRONT_WAKE_STAMP_SIZE;
static float stamp_spacing = WATERFRONT_WAKE_STAMP_SPACING;
// --no-water-history: stamp-only water shading, without G3b foam history and ripples.
static bool no_water_history;
// --no-scene-depth: water shading without the G4 copy of opaque depth.
static bool no_scene_depth;
// --no-water-atlas: the hand-written exposure field instead of the G4 map atlas.
static bool no_water_atlas;
// --no-far-water: no G4 gusts, sun glitter or squall water.
static bool no_far_water;
// --light-fallback: the capped uniform light list instead of the G5b clusters.
// --light-reference: every clustered light at every pixel, ignoring the slot lists.
// --stress-lights N: N extra review lights around the shot.
static bool light_fallback, light_reference;
static int stress_lights;
// --no-ambient-occlusion: the map's baked G5c occlusion at zero strength.
// --no-particles: no boat foam or spray cubes, to measure what they cost.
// --no-shadows, --no-reflections: skip the shadow-map or reflection pass, to measure what they
// cost.
static bool no_ambient_occlusion, no_particles, no_shadows, no_reflections;
// --daylight: review poses at a fixed presentation phase (default day).
static float review_daylight = WATERFRONT_DAYLIGHT_DAY;
// --lighting-hour: developer full-day hour; overrides --daylight when set.
static float review_hour;
static bool review_hour_set;
// --resolution and --hdr: the presentation preset of review and benchmark GPU views, sized for
// a 1280x800 window (low: 320x200; high: a 1280x800 scene and 640x480 reflection). HDR follows
// the resolution unless set.
static BoatResolution review_resolution = BOAT_RESOLUTION_LOW;
static int review_hdr = -1;

static bool review_hdr_on(void)
{
    return review_hdr < 0 ? review_resolution == BOAT_RESOLUTION_HIGH : review_hdr != 0;
}

static bool apply_review_preset(BoatGpu *gpu)
{
    BoatScreenLayout layout = boat_screen_layout(review_resolution, 320, 200, 1280, 800);
    return boat_gpu_resize(gpu, layout.scene_width, layout.scene_height)
        && waterfront_gpu_scene_set_reflection_size(
            &gpu->scene, layout.reflection_width, layout.reflection_height)
        && boat_gpu_set_hdr(gpu, review_hdr_on()) == review_hdr_on();
}

// --clip-aa: place-clip supersampling, 4x4 (default) or off.
static bool clip_aa = true;

static bool apply_scene_options(WaterfrontGpuScene *scene)
{
    waterfront_gpu_scene_set_detail_enabled(scene, !no_detail);
    waterfront_gpu_scene_set_wake_stamp_enabled(scene, !analytic_wakes);
    waterfront_gpu_scene_set_water_history_enabled(scene, !no_water_history);
    waterfront_gpu_scene_set_scene_depth_enabled(scene, !no_scene_depth);
    waterfront_gpu_scene_set_water_atlas_enabled(scene, !no_water_atlas);
    waterfront_gpu_scene_set_far_water_enabled(scene, !no_far_water);
    scene->light_clusters_disabled = light_fallback;
    scene->light_reference = light_reference;
    scene->ambient_occlusion_disabled = no_ambient_occlusion;
    scene->particles_disabled = no_particles;
    if (no_shadows)
        waterfront_gpu_scene_set_shadows_enabled(scene, false);
    if (no_reflections)
        waterfront_gpu_scene_set_reflections_enabled(scene, false);
    scene->stress_lights = stress_lights;
    if (stamp_size != WATERFRONT_WAKE_STAMP_SIZE || stamp_spacing != WATERFRONT_WAKE_STAMP_SPACING)
        return waterfront_gpu_scene_set_wake_stamp_size(scene, stamp_size, stamp_spacing);
    return true;
}

// The last frame's G5b light submission: clusters or the capped fallback, and every drop.
static void json_lights(FILE *out, const WaterfrontGpuScene *scene)
{
    const WaterfrontLightStats *l = &scene->light_stats;
    fprintf(out,
        "{\"mode\":\"%s\",\"submitted\":%d,\"kept\":%d,\"dropped\":%d,\"clipped\":%d,"
        "\"overflowed_slots\":%d,\"dropped_assignments\":%d,\"busiest_slot\":%d,"
        "\"indices\":%d,\"upload_rows\":%d,\"fallback_kept\":%d,\"fallback_dropped\":%d}",
        scene->light_clusters_active ? (scene->light_reference ? "reference" : "clusters")
                                     : "fallback",
        l->submitted, scene->light_clusters_active ? scene->light_clusters->light_count : 0,
        l->dropped_lights, l->clipped_lights, l->overflowed_slots, l->dropped_assignments,
        l->busiest_slot, scene->light_clusters_active ? scene->light_clusters->index_count : 0,
        scene->light_rows, scene->light_clusters_active ? 0 : scene->local_light_count,
        scene->fallback_dropped);
}

static int count_detail_changes(const WaterfrontGpuScene *scene, const unsigned char *previous)
{
    int changes = 0;
    for (int i = 0; i < scene->detail_columns * scene->detail_rows; ++i)
        changes += previous[i] != scene->detail_far[0][i];
    return changes;
}

// Two identical draws of one overview pose can differ by 1/255 in one pixel on the M1 Max, with
// culling on for both: repeat-draw noise on a rounding boundary. A wrongly culled tile exceeds
// this.
static bool same_but_for_noise(Image a, Image b)
{
    const Color *p = a.data, *q = b.data;
    int changed = 0;
    for (int i = 0; i < a.width * a.height; ++i) {
        int r = abs(p[i].r - q[i].r), g = abs(p[i].g - q[i].g), bl = abs(p[i].b - q[i].b);
        if (r > 1 || g > 1 || bl > 1)
            return false;
        changed += r || g || bl;
    }
    return changed <= 4;
}

// G2b: drives one GPU view through the detail threshold. Orbiting the boat changes no level, the
// band holds a camera jittering around a switch, repeated round trips end alike, and every cell
// with windows keeps them far.
static int check_detail(void)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "Distance detail probe");
    if (!IsWindowReady())
        return 1;
    BoatGpu gpu = { 0 };
    BoatSession session;
    boat_session_init(&session, test_world(TEST_RIVER), boat_water_gentle());
    int result = 1, cells = 0;
    unsigned char *previous = NULL, *start = NULL, *switched = NULL;
    if (!boat_gpu_init(&gpu, false))
        goto cleanup;
    BoatViewState view = { .boat = session.boat, .time = 12 };
    view.boat.position = (Vector2) { 250, -500 };
    BoatPresentation presentation = session.presentation;
    const BoatCamera camera = BOAT_CAMERA_DEFAULT;
    boat_gpu_draw(&gpu, &view, &presentation, &session.world, camera);
    cells = gpu.scene.detail_columns * gpu.scene.detail_rows;
    previous = malloc((size_t)cells);
    start = malloc((size_t)cells);
    switched = malloc((size_t)cells);
    if (gpu.failed || !cells || !previous || !start || !switched)
        goto cleanup;

    // Every near cell of a detailed class that holds windows has far windows.
    for (int i = 0; i < gpu.scene.windows.count; ++i) {
        bool found = false;
        for (int f = 0; !found && f < gpu.scene.windows_far.count; ++f)
            found = gpu.scene.windows_far.cells[f] == gpu.scene.windows.cells[i];
        if (!found) {
            fprintf(stderr, "detail: cell %d lost its windows\n", gpu.scene.windows.cells[i]);
            goto cleanup;
        }
    }

    // Turning in place.
    memcpy(previous, gpu.scene.detail_far[0], (size_t)cells);
    int turn_changes = 0;
    for (int heading = 0; heading < 64; ++heading) {
        view.boat.heading = (float)heading * PI / 32;
        presentation.camera_heading = view.boat.heading;
        boat_gpu_draw(&gpu, &view, &presentation, &session.world, camera);
        turn_changes += count_detail_changes(&gpu.scene, previous);
        memcpy(previous, gpu.scene.detail_far[0], (size_t)cells);
    }

    // Out along a straight line and back, twice: both round trips end alike, whatever the band
    // kept from the fresh start. A cell that just switched holds under small camera movement.
    view.boat.heading = 0;
    presentation.camera_heading = 0;
    int switches[2] = { 0 }, flickers = 0, frames = 0;
    size_t main_near = 0, main_far = 0;
    for (int trip = 0; trip < 2; ++trip) {
        if (trip == 1)
            memcpy(start, gpu.scene.detail_far[0], (size_t)cells);
        for (int direction = 0; direction < 2; ++direction)
            for (int step = 0; step <= 400; ++step) {
                int at = direction ? 400 - step : step;
                view.boat.position = (Vector2) { 250, -500 + (float)at * 2 };
                boat_gpu_draw(&gpu, &view, &presentation, &session.world, camera);
                int changes = count_detail_changes(&gpu.scene, previous);
                switches[direction] += changes;
                main_far += gpu.scene.stats.far_cells[WATERFRONT_GPU_PASS_MAIN];
                main_near += (size_t)cells - gpu.scene.stats.far_cells[WATERFRONT_GPU_PASS_MAIN];
                ++frames;
                if (changes) {
                    Vector2 held = view.boat.position;
                    memcpy(switched, gpu.scene.detail_far[0], (size_t)cells);
                    for (int jitter = 0; jitter < 6; ++jitter) {
                        view.boat.position
                            = (Vector2) { held.x, held.y + (jitter & 1 ? 1.5f : -1.5f) };
                        boat_gpu_draw(&gpu, &view, &presentation, &session.world, camera);
                        for (int i = 0; i < cells; ++i)
                            flickers += previous[i] != switched[i]
                                && gpu.scene.detail_far[0][i] != switched[i];
                    }
                    view.boat.position = held;
                    boat_gpu_draw(&gpu, &view, &presentation, &session.world, camera);
                }
                memcpy(previous, gpu.scene.detail_far[0], (size_t)cells);
            }
    }
    bool returned = memcmp(start, gpu.scene.detail_far[0], (size_t)cells) == 0;
    if (turn_changes || !switches[0] || !switches[1] || !returned || flickers) {
        fprintf(stderr, "detail: turn=%d forward=%d backward=%d returned=%d flickers=%d\n",
            turn_changes, switches[0], switches[1], returned, flickers);
        goto cleanup;
    }
    printf("detail_check=ok cells=%d turn_changes=0 forward_switches=%d backward_switches=%d "
           "flickers=0 frames=%d mean_far_cells=%.1f mean_near_cells=%.1f\n",
        cells, switches[0], switches[1], frames, (double)main_far / frames,
        (double)main_near / frames);
    result = 0;
cleanup:
    free(previous);
    free(start);
    free(switched);
    boat_gpu_destroy(&gpu);
    boat_session_destroy(&session);
    test_worlds_close();
    CloseWindow();
    return result;
}

static int check_culling(void)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "Tile culling probe");
    if (!IsWindowReady())
        return 1;
    const float sizes[] = { 512, 1024 };
    const BoatCamera cameras[] = { BOAT_CAMERA_DEFAULT, { .distance = 12, .height = 6 },
        { .distance = 1100, .height = 300 } };
    BoatSession session, flat;
    boat_session_init(&session, test_world(TEST_RIVER), boat_water_gentle());
    boat_session_init(&flat, test_world(TEST_RIVER), (BoatWater) { 0 });
    int result = 1, poses = 0;
    unsigned int culled[WATERFRONT_GPU_PASS_COUNT] = { 0 };
    size_t cpu_culled = 0;
    static Color culled_pixels[320 * 200], full_pixels[320 * 200];
    for (size_t s = 0; s < sizeof(sizes) / sizeof(*sizes); ++s) {
        BoatGpu gpu = { 0 };
        Renderer3D cpu = { 0 };
        BoatCpuView cpu_view = { 0 };
        bool ok = boat_gpu_init(&gpu, false) && prepare_tiles(&gpu.scenery, sizes[s])
            && prepare_tiles(&cpu_view.scenery, sizes[s]);
        for (size_t stop = 0; ok && stop < stop_count; ++stop)
            for (int heading = 0; ok && heading < 4; ++heading)
                for (size_t c = 0; ok && c < sizeof(cameras) / sizeof(*cameras); ++c) {
                    BoatViewState view = { .boat = session.boat, .time = 12 };
                    view.boat.position = stops[stop].position;
                    view.boat.heading = heading * PI / 2;
                    BoatPresentation presentation = session.presentation;
                    presentation.camera_heading = view.boat.heading;
                    Image images[2] = { 0 };
                    for (int pass = 0; pass < 2; ++pass) {
                        waterfront_gpu_scene_set_culling_enabled(&gpu.scene, pass == 0);
                        boat_gpu_draw(&gpu, &view, &presentation, &session.world, cameras[c]);
                        images[pass]
                            = gpu.failed ? (Image) { 0 } : LoadImageFromTexture(gpu.target.texture);
                        if (pass == 0)
                            for (int p = 0; p < WATERFRONT_GPU_PASS_COUNT; ++p)
                                culled[p] += gpu.scene.stats.tiles_culled[p];
                        cpu.culling_disabled = pass == 1;
                        ok = ok
                            && boat_draw(&cpu, &cpu_view, pass ? full_pixels : culled_pixels, 320,
                                200, &view, &presentation, &flat.world, cameras[c]);
                        if (pass == 0)
                            cpu_culled += cpu.stats.boxes_culled;
                    }
                    ok = ok && images[0].data && images[1].data
                        && same_but_for_noise(images[0], images[1])
                        && memcmp(culled_pixels, full_pixels, sizeof(full_pixels)) == 0;
                    if (!ok)
                        fprintf(stderr, "culling changed tile=%g stop=%s heading=%d camera=%zu\n",
                            (double)sizes[s], stops[stop].name, heading, c);
                    for (int i = 0; i < 2; ++i)
                        if (images[i].data)
                            UnloadImage(images[i]);
                    ++poses;
                }
        boat_cpu_view_destroy(&cpu_view);
        renderer3d_destroy(&cpu);
        boat_gpu_destroy(&gpu);
        if (!ok)
            goto cleanup;
    }
    for (int p = 0; p < WATERFRONT_GPU_PASS_COUNT; ++p)
        if (!culled[p])
            goto cleanup;
    if (!cpu_culled)
        goto cleanup;
    printf("tile_culling=ok poses=%d gpu_culled shadow=%u reflection=%u main=%u cpu_culled=%zu\n",
        poses, culled[0], culled[1], culled[2], cpu_culled);
    result = 0;
cleanup:
    boat_session_destroy(&session);
    boat_session_destroy(&flat);
    CloseWindow();
    return result;
}

static Color image_pixel(Image image, int x, int y)
{
    return ((const Color *)image.data)[y * image.width + x];
}

// Largest colour channel difference between two RGBA8 images of one size, ignoring alpha (a
// straight-alpha draw into a target leaves its own alpha); -1 when they differ in size.
static int image_distance(Image a, Image b)
{
    if (a.width != b.width || a.height != b.height || a.format != b.format)
        return -1;
    const unsigned char *p = a.data, *q = b.data;
    int largest = 0;
    for (size_t i = 0, n = (size_t)a.width * (size_t)a.height * 4; i < n; ++i) {
        int d = i % 4 == 3 ? 0 : abs((int)p[i] - (int)q[i]);
        largest = d > largest ? d : largest;
    }
    return largest;
}

static bool near_color(Color a, Color b, int tolerance)
{
    return abs(a.r - b.r) <= tolerance && abs(a.g - b.g) <= tolerance && abs(a.b - b.b) <= tolerance
        && abs(a.a - b.a) <= tolerance;
}

// Draw a session frame into GPU at LAYOUT's sizes, with or without HDR.
static bool layout_frame(
    BoatGpu *gpu, const BoatSession *session, BoatScreenLayout layout, bool hdr, Image *image)
{
    if (!boat_gpu_resize(gpu, layout.scene_width, layout.scene_height)
        || !waterfront_gpu_scene_set_reflection_size(
            &gpu->scene, layout.reflection_width, layout.reflection_height)
        || boat_gpu_set_hdr(gpu, hdr) != hdr)
        return false;
    return gpu_frame(gpu, session, image);
}

// G5a: high resolution, HDR and its fallback. The bloom chain spreads only light above 1 and
// clamps it; a neutral grade keeps daytime within one 8-bit step of the 8-bit scene; failed HDR
// allocations present that scene exactly; repeated preset and size changes and a second view
// reproduce their frames; and the HUD overlay composites like drawing over the scene.
static int check_presentation(void)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "Presentation probe");
    if (!IsWindowReady())
        return 1;
    static BoatWorldResources owner;
    static BoatSession session;
    BoatGpu gpu = { 0 }, other = { 0 };
    BoatRearview mirror = { 0 };
    Image low = { 0 }, high = { 0 }, high_ldr = { 0 }, frame = { 0 }, bloom = { 0 };
    RenderTexture2D scene = { 0 }, overlay = { 0 }, direct = { 0 }, composite = { 0 };
    int result = 1, hdr_distance = -1;
    size_t hdr_bytes = 0;
    bool ready = false;
#define EXPECT(test)                                                                               \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
#define RELEASE(image) (UnloadImage(image), (image) = (Image) { 0 })
    EXPECT(boat_world_resources_init(&owner, test_location(TEST_RIVER)));
    boat_session_init(&session, &owner, boat_water_gentle());
    ready = true;
    for (int t = 0; t < 480; ++t)
        boat_session_step(&session, (BoatInput) { .steering_rate = .3f, .throttle_rate = 1 });
    BoatScreenLayout low_layout = boat_screen_layout(BOAT_RESOLUTION_LOW, 320, 200, 960, 600);
    BoatScreenLayout high_layout = boat_screen_layout(BOAT_RESOLUTION_HIGH, 320, 200, 1280, 800);
    EXPECT(boat_gpu_init(&gpu, false));
    if (!waterfront_gpu_scene_float_blending(&gpu.scene)) {
        // The fallback: HDR stays off and the 8-bit frame is presented.
        EXPECT(!boat_gpu_set_hdr(&gpu, true) && !boat_gpu_hdr_active(&gpu));
        printf("presentation=ok hdr=unsupported\n");
        result = 0;
        goto cleanup;
    }
    EXPECT(layout_frame(&gpu, &session, low_layout, false, &low));
    EXPECT(layout_frame(&gpu, &session, high_layout, false, &high_ldr));
    EXPECT(high_ldr.width == 1280 && high_ldr.height == 800);
    EXPECT(gpu.scene.reflection.texture.width == 640);
    EXPECT(layout_frame(&gpu, &session, high_layout, true, &high));
    EXPECT(boat_gpu_hdr_active(&gpu));
    hdr_bytes = boat_gpu_hdr_bytes(&gpu);
    EXPECT(hdr_bytes == (size_t)1280 * 800 * 12 + (640 * 400 + 320 * 200 + 160 * 100) * 8);
    // No daytime light exceeds 1, so HDR differs only by 16-bit storage rounding.
    hdr_distance = image_distance(high, high_ldr);
    EXPECT(hdr_distance >= 0 && hdr_distance <= 1);
    // Repeated preset changes reproduce each preset's frame.
    for (int cycle = 0; cycle < 3; ++cycle) {
        EXPECT(layout_frame(&gpu, &session, low_layout, false, &frame));
        EXPECT(same_image(low, frame) && !boat_gpu_hdr_active(&gpu));
        RELEASE(frame);
        EXPECT(layout_frame(&gpu, &session, high_layout, true, &frame) && same_image(high, frame));
        RELEASE(frame);
        // A window size between presets, then back.
        BoatScreenLayout middle = boat_screen_layout(BOAT_RESOLUTION_HIGH, 320, 200, 1000, 700);
        EXPECT(layout_frame(&gpu, &session, middle, true, &frame) && frame.width == 960);
        RELEASE(frame);
    }
    // A second view and the rear-view mirror leave the HDR view's frame unchanged.
    EXPECT(boat_gpu_init(&other, false) && gpu_frame(&other, &session, &frame));
    EXPECT(same_image(low, frame));
    RELEASE(frame);
    EXPECT(layout_frame(&gpu, &session, high_layout, true, &frame) && same_image(high, frame));
    RELEASE(frame);
    EXPECT(boat_rearview_update(
        &mirror, &gpu, SESSION_VIEW(&session), &session.presentation, &session.world, NULL));
    EXPECT(gpu_frame(&gpu, &session, &frame) && same_image(high, frame));
    RELEASE(frame);
    // A failed HDR allocation presents the 8-bit frame exactly, and a later request recovers.
    gpu.hdr_allocation_failures = 1;
    EXPECT(boat_gpu_resize(&gpu, 960, 600) && !boat_gpu_hdr_active(&gpu));
    EXPECT(boat_gpu_resize(&gpu, 1280, 800) && gpu_frame(&gpu, &session, &frame));
    EXPECT(same_image(high_ldr, frame));
    RELEASE(frame);
    gpu.hdr_allocation_failures = 1;
    EXPECT(!boat_gpu_set_hdr(&gpu, true) && !boat_gpu_hdr_active(&gpu));
    EXPECT(gpu_frame(&gpu, &session, &frame) && same_image(high_ldr, frame));
    RELEASE(frame);
    EXPECT(boat_gpu_set_hdr(&gpu, true) && gpu_frame(&gpu, &session, &frame));
    EXPECT(same_image(high, frame));
    RELEASE(frame);
    EXPECT(!boat_gpu_resize(&gpu, 2049, 800) && boat_gpu_hdr_active(&gpu));
    // Bloom: a 0.5 field with one 2x2 spot of 3.5. The spot clamps, its light spreads a few
    // pixels (about 6/255 three pixels out) and is gone within 16, and the field is unchanged.
    EXPECT(boat_gpu_resize(&gpu, 320, 200) && boat_gpu_hdr_active(&gpu));
    BeginTextureMode(gpu.hdr_target);
    ClearBackground((Color) { 128, 64, 32, 255 });
    rlSetBlendFactors(RL_ONE, RL_ONE, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM);
    for (int i = 0; i < 3; ++i)
        DrawRectangle(160, 100, 2, 2, WHITE);
    EndBlendMode();
    EndTextureMode();
    boat_gpu_present_hdr(&gpu, 0);
    bloom = LoadImageFromTexture(gpu.target.texture);
    EXPECT(bloom.data);
    // The image is bottom-up like the target; rows here are symmetric about the spot.
    Color spot = image_pixel(bloom, 160, 99), corner = image_pixel(bloom, 4, 4);
    Color field = { 128, 64, 32, 255 };
    EXPECT(spot.r == 255 && spot.g == 255 && spot.b == 255);
    EXPECT(near_color(corner, field, 1));
    EXPECT(near_color(image_pixel(bloom, 200, 99), field, 1));
    Color near = image_pixel(bloom, 164, 99), edge = image_pixel(bloom, 176, 99);
    EXPECT(near.r > field.r + 3 && near.g > field.g + 3 && near.b > field.b + 3);
    EXPECT(near_color(edge, field, 1));
    // The HUD overlay: a translucent panel over the scene, drawn directly and composited.
    scene = LoadRenderTexture(64, 40);
    overlay = LoadRenderTexture(16, 10);
    direct = LoadRenderTexture(64, 40);
    composite = LoadRenderTexture(64, 40);
    EXPECT(IsRenderTextureValid(scene) && IsRenderTextureValid(overlay));
    EXPECT(IsRenderTextureValid(direct) && IsRenderTextureValid(composite));
    Color backdrop = { 40, 120, 200, 255 }, panel = { 230, 180, 40, 150 };
    Color text = { 250, 250, 250, 255 };
    BeginTextureMode(scene);
    ClearBackground(backdrop);
    EndTextureMode();
    BeginTextureMode(overlay);
    boat_screen_begin_overlay();
    DrawRectangle(2, 2, 8, 6, panel);
    DrawRectangle(4, 4, 2, 2, panel);
    DrawRectangle(5, 5, 1, 1, text);
    boat_screen_end_overlay();
    EndTextureMode();
    BeginTextureMode(direct);
    ClearBackground(backdrop);
    rlScalef(4, 4, 1);
    DrawRectangle(2, 2, 8, 6, panel);
    DrawRectangle(4, 4, 2, 2, panel);
    DrawRectangle(5, 5, 1, 1, text);
    EndTextureMode();
    BoatScreenLayout hud = boat_screen_layout(BOAT_RESOLUTION_HIGH, 16, 10, 64, 40);
    BeginTextureMode(composite);
    ClearBackground(BLACK);
    boat_screen_present_overlay(&hud, scene.texture, overlay.texture);
    EndTextureMode();
    Image expected = LoadImageFromTexture(direct.texture);
    Image actual = LoadImageFromTexture(composite.texture);
    int hud_distance = image_distance(expected, actual);
    bool hud_drawn = !near_color(image_pixel(actual, 16, 16), backdrop, 8);
    UnloadImage(expected);
    UnloadImage(actual);
    // The composite flips the scene horizontally like every GPU scene; the backdrop is uniform.
    EXPECT(hud_drawn && hud_distance >= 0 && hud_distance <= 1);
#undef RELEASE
#undef EXPECT
    printf("presentation=ok hdr_distance=%d hdr_bytes=%zu hud_distance=%d\n", hdr_distance,
        hdr_bytes, hud_distance);
    result = 0;
cleanup:;
    Image images[] = { low, high, high_ldr, frame, bloom };
    for (size_t i = 0; i < sizeof(images) / sizeof(images[0]); ++i)
        if (images[i].data)
            UnloadImage(images[i]);
    RenderTexture2D targets[] = { scene, overlay, direct, composite };
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i)
        if (targets[i].id)
            UnloadRenderTexture(targets[i]);
    boat_rearview_destroy(&mirror);
    boat_gpu_destroy(&gpu);
    boat_gpu_destroy(&other);
    if (ready) {
        boat_session_destroy(&session);
        boat_world_resources_destroy(&owner);
    }
    CloseWindow();
    return result;
}

// G5b: clustered lights shade exactly as every light tested at every pixel (so no light pops at a
// slot boundary, in the view or its reflection), hundreds of review lights fit without overflow,
// and the capped fallback records what it drops while daytime keeps the clustered frame.
static int check_light_clusters(void)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "Light cluster probe");
    if (!IsWindowReady())
        return 1;
    static BoatWorldResources owner;
    static BoatSession session;
    BoatGpu gpu = { 0 };
    BoatRearview mirror = { 0 };
    Image clustered = { 0 }, frame = { 0 };
    int result = 1, fallback_distance = -1, stress_kept = 0, stress_dropped = 0;
    bool ready = false;
#undef EXPECT
#undef RELEASE
#define EXPECT(test)                                                                               \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
#define RELEASE(image) (UnloadImage(image), (image) = (Image) { 0 })
    EXPECT(boat_world_resources_init(&owner, test_location(TEST_RIVER)));
    boat_session_init(&session, &owner, boat_water_gentle());
    ready = true;
    for (int t = 0; t < 480; ++t)
        boat_session_step(&session, (BoatInput) { .steering_rate = .3f, .throttle_rate = 1 });
    EXPECT(boat_gpu_init(&gpu, false));
    WaterfrontGpuScene *scene = &gpu.scene;
    // Daytime: every authored light fits the capped list, so the fallback draws the same frame.
    EXPECT(gpu_frame(&gpu, &session, &clustered) && scene->light_clusters_active);
    EXPECT(scene->light_stats.submitted > 0 && scene->light_stats.dropped_lights == 0);
    scene->light_clusters_disabled = true;
    EXPECT(gpu_frame(&gpu, &session, &frame) && !scene->light_clusters_active);
    // Lights behind the camera are left out too; they reach nothing it sees.
    EXPECT(scene->local_light_count + scene->fallback_dropped == scene->light_stats.submitted);
    fallback_distance = image_distance(clustered, frame);
    EXPECT(fallback_distance >= 0 && fallback_distance <= 1);
    RELEASE(frame);
    RELEASE(clustered);
    scene->light_clusters_disabled = false;
    // Dusk, then dusk with hundreds of review lights, at both presets: the slot lists draw
    // exactly the reference that shades every light everywhere.
    gpu.daylight = WATERFRONT_DAYLIGHT_DUSK;
    gpu.hour_set = false; // This internal dusk parity check does not use the review hour.
    BoatScreenLayout layouts[] = { boat_screen_layout(BOAT_RESOLUTION_LOW, 320, 200, 960, 600),
        boat_screen_layout(BOAT_RESOLUTION_HIGH, 320, 200, 1280, 800) };
    for (int stress = 0; stress < 2; ++stress) {
        scene->stress_lights = stress ? 600 : 0;
        for (int preset = 0; preset < 2; ++preset) {
            bool hdr = preset == 1 && waterfront_gpu_scene_float_blending(scene);
            EXPECT(layout_frame(&gpu, &session, layouts[preset], hdr, &clustered));
            EXPECT(scene->light_clusters_active && scene->light_stats.overflowed_slots == 0);
            EXPECT(
                scene->light_stats.clipped_lights == 0 && scene->light_stats.dropped_lights == 0);
            EXPECT(scene->light_clusters->light_count == scene->light_stats.submitted);
            scene->light_reference = true;
            EXPECT(gpu_frame(&gpu, &session, &frame) && same_image(clustered, frame));
            RELEASE(frame);
            scene->light_reference = false;
            // The rear-view mirror rebuilds the lists from its own frame; the view's frame
            // returns unchanged.
            EXPECT(boat_rearview_update(&mirror, &gpu, SESSION_VIEW(&session),
                &session.presentation, &session.world, NULL));
            EXPECT(gpu_frame(&gpu, &session, &frame) && same_image(clustered, frame));
            RELEASE(frame);
            RELEASE(clustered);
        }
    }
    stress_kept = scene->light_clusters->light_count;
    EXPECT(stress_kept >= 600 && scene->light_stats.busiest_slot <= WATERFRONT_LIGHT_CELL_LIGHTS);
    // The capped fallback keeps at most 32 (headlight places stay empty in play) and records
    // every other light.
    scene->light_clusters_disabled = true;
    EXPECT(gpu_frame(&gpu, &session, &frame) && !scene->light_clusters_active);
    stress_dropped = scene->fallback_dropped;
    EXPECT(scene->local_light_count <= WATERFRONT_GPU_MAX_LOCAL_LIGHTS);
    EXPECT(scene->light_stats.submitted == stress_kept);
    EXPECT(stress_dropped == stress_kept - scene->local_light_count);
    RELEASE(frame);
    printf("light_clusters=ok fallback_distance=%d stress_lights=%d fallback_dropped=%d\n",
        fallback_distance, stress_kept, stress_dropped);
    result = 0;
cleanup:
    if (clustered.data)
        UnloadImage(clustered);
    if (frame.data)
        UnloadImage(frame);
    boat_rearview_destroy(&mirror);
    boat_gpu_destroy(&gpu);
    if (ready) {
        boat_session_destroy(&session);
        boat_world_resources_destroy(&owner);
    }
    CloseWindow();
    return result;
#undef RELEASE
#undef EXPECT
}

// A1.5: GPU views with private owners and waves, so both patch caches build. Two river owners,
// the harbor and the basin render in fresh views as references; then shared views reproduce
// them interleaved, after the first owner and its view are destroyed, and across repeated
// view recreation with the context alive.
static int check_gpu_lifecycle(void)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "View lifecycle probe");
    if (!IsWindowReady())
        return 1;
    enum { VIEWS = 4, CYCLES = 3 };
    const BoatWorldDefinition *river = test_location(TEST_RIVER);
    const BoatWorldDefinition harbor = test_harbor_definition(river);
    const BoatWorldDefinition *definitions[VIEWS]
        = { river, river, &harbor, test_location(BOAT_LOCATION_BASIN) };
    static BoatWorldResources owners[VIEWS];
    static BoatSession sessions[VIEWS];
    static BoatWakeObjects arcade;
    BoatGpu a = { 0 }, b = { 0 };
    Image reference[VIEWS] = { 0 }, frame = { 0 }, resize_reference = { 0 };
    int result = 1, ready = 0, arcade_patch = 0, patch = 0;
#define EXPECT(test)                                                                               \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
#define MATCHES(gpu, i)                                                                            \
    (gpu_frame(gpu, &sessions[i], &frame) && same_image(reference[i], frame)                       \
        && (UnloadImage(frame), frame = (Image) { 0 }, true))
    EXPECT(boat_wake_objects_init(&arcade, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT));
    for (; ready < VIEWS; ++ready) {
        EXPECT(boat_world_resources_init(&owners[ready], definitions[ready]));
        boat_session_init(&sessions[ready], &owners[ready], boat_water_gentle());
    }
    // Owner B's water carries an (empty) arcade world, so its views use the fine patch.
    sessions[1].world.water.objects = &arcade;
    for (int i = 0; i < VIEWS; ++i)
        for (int t = 0; t < 240 * (i + 1); ++t)
            boat_session_step(&sessions[i],
                (BoatInput) { .steering_rate = i % 2 ? -.3f : .3f, .throttle_rate = 1 });
    for (int i = 0; i < VIEWS; ++i) {
        BoatGpu own = { 0 };
        bool drawn = boat_gpu_init(&own, false) && gpu_frame(&own, &sessions[i], &reference[i]);
        boat_gpu_destroy(&own);
        EXPECT(drawn);
        for (int j = 0; j < i; ++j)
            EXPECT(!same_image(reference[i], reference[j]));
    }
    EXPECT(boat_gpu_init(&a, false) && boat_gpu_init(&b, false));
    EXPECT(MATCHES(&a, 0) && MATCHES(&b, 1) && MATCHES(&b, 2) && MATCHES(&a, 1));
    EXPECT(a.water_mesh.patches[0].count && a.water_mesh.patches[1].count);
    patch = (int)a.water_mesh.patches[0].count;
    arcade_patch = (int)a.water_mesh.patches[1].count;
    EXPECT(MATCHES(&b, 3) && MATCHES(&a, 0));
    // Release owner A with the view that last drew it; B's owner keeps rendering.
    boat_gpu_destroy(&a);
    boat_session_destroy(&sessions[0]);
    boat_world_resources_destroy(&owners[0]);
    EXPECT(MATCHES(&b, 1) && MATCHES(&b, 2) && MATCHES(&b, 1));
    for (int cycle = 0; cycle < CYCLES; ++cycle) {
        EXPECT(boat_gpu_init(&a, false));
        EXPECT(MATCHES(&a, 3) && MATCHES(&a, 1) && MATCHES(&b, 3) && MATCHES(&a, 2));
        boat_gpu_destroy(&a);
        boat_gpu_destroy(&a);
        EXPECT(!a.scene.map && !a.scenery.ready && !a.water_mesh.patches[0].count);
    }
    EXPECT(MATCHES(&b, 1));
    // A phone rotates without recreating shaders or scenery. Returning to the original
    // viewport must reproduce exactly the same frame, with and without supersampling.
    for (int aa = 0; aa < 2; ++aa) {
        EXPECT(boat_gpu_init(&a, aa != 0) && gpu_frame(&a, &sessions[3], &resize_reference));
        const int sizes[][2] = { { 320, 693 }, { 444, 200 }, { 320, 200 } };
        for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
            EXPECT(boat_gpu_resize(&a, sizes[i][0], sizes[i][1]));
            EXPECT(!boat_gpu_resize(&a, 0, 200) && !boat_gpu_resize(&a, 320, 2049));
            EXPECT(gpu_frame(&a, &sessions[3], &frame));
            EXPECT(frame.width == sizes[i][0] && frame.height == sizes[i][1]);
            if (i == 2)
                EXPECT(same_image(resize_reference, frame));
            UnloadImage(frame);
            frame = (Image) { 0 };
        }
        UnloadImage(resize_reference);
        resize_reference = (Image) { 0 };
        boat_gpu_destroy(&a);
    }
    // G2a: a failed map upload leaves nothing bound, and the same view then recovers exactly.
    EXPECT(boat_gpu_init(&a, false));
    a.scene.map_upload_limit = 5;
    EXPECT(!gpu_frame(&a, &sessions[1], &frame));
    EXPECT(!a.scene.map && a.scene.geometry.meshes == 0);
    a.scene.map_upload_limit = 0;
    EXPECT(MATCHES(&a, 1));
    EXPECT(a.scene.geometry.normal_error > 0 && a.scene.geometry.normal_error <= 2.0f / 255.0f);
    // G5c: the river's baked ambient occlusion reaches its uploaded vertices.
    EXPECT(a.scene.geometry.occluded_vertices > 0);
    boat_gpu_destroy(&a);
#undef MATCHES
#undef EXPECT
    printf("gpu_lifecycle=ok views=river-a,river-b-arcade,harbor,basin cycles=%d "
           "patch_triangles=%d arcade_triangles=%d\n",
        CYCLES, patch, arcade_patch);
    result = 0;
cleanup:
    if (resize_reference.data)
        UnloadImage(resize_reference);
    if (frame.data)
        UnloadImage(frame);
    for (int i = 0; i < VIEWS; ++i)
        if (reference[i].data)
            UnloadImage(reference[i]);
    boat_gpu_destroy(&a);
    boat_gpu_destroy(&b);
    for (int i = 0; i < ready; ++i) {
        boat_session_destroy(&sessions[i]);
        boat_world_resources_destroy(&owners[i]);
    }
    CloseWindow();
    return result;
}

static bool route_finished(const Boat *boat, const Route *route, size_t destination)
{
    return destination == route->count - 1
        && Vector2Distance(boat->position, route->stops[destination].position) < 12;
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

// G0 four-boat stress: four fleet boats on the open basin, each circling its own centre so all
// four wake emitters fill, plus a saturated scenery traffic pool. The basin keeps the fixture
// collision-safe and independent of shoreline variation; the ordinary river route remains the
// submission/triangle measurement. Production physics is unchanged; only the scripted inputs
// and the review camera are fixtures.
#define STRESS_PREROLL_TICKS 2400
#define STRESS_WARMUP_FRAMES 120
#define STRESS_MEASURED_FRAMES 900
#define STRESS_FRAMES (STRESS_WARMUP_FRAMES + STRESS_MEASURED_FRAMES)
#define STRESS_TICKS_PER_FRAME 2

typedef struct {
    BoatFleet fleet;
    BoatPresentation presentations[BOAT_FLEET_SLOTS];
    BoatWakeObjects traffic; // The scenery traffic pool; the player shader samples both pools.
    bool traffic_ready;
    uint64_t ticks; // Fixture tick, including the pre-roll.
} StressFixture;

static const Vector2 stress_centres[BOAT_FLEET_SLOTS]
    = { { -60, -40 }, { 60, -40 }, { -60, 40 }, { 60, 40 } };
#define STRESS_CIRCLE_RADIUS 20.0f

static BoatInput stress_input(const Boat *boat, unsigned slot, uint64_t tick)
{
    float phase = (float)tick * .0028f + (float)slot * (2 * PI / BOAT_FLEET_SLOTS);
    Vector2 target = Vector2Add(stress_centres[slot],
        (Vector2) { STRESS_CIRCLE_RADIUS * cosf(phase), STRESS_CIRCLE_RADIUS * sinf(phase) });
    Vector2 delta = Vector2Subtract(target, boat->position);
    float desired = atan2f(delta.x, -delta.y);
    float error = remainderf(desired - boat->heading, 2 * PI);
    float wheel = Clamp(1.6f * error - .8f * boat->yaw_velocity, -.8f, .8f);
    float thrust = .72f;
    return (BoatInput) { .steering_rate = Clamp((wheel - boat->wheel) / BOAT_STEP, -1, 1),
        .throttle_rate
        = Clamp((thrust - boat->throttle) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP), -1, 1) };
}

static bool stress_init(StressFixture *fixture, bool waves)
{
    *fixture = (StressFixture) { 0 };
    const BoatWorldDefinition *basin = test_location(BOAT_LOCATION_BASIN);
    if (!boat_fleet_init(&fixture->fleet, test_world(BOAT_LOCATION_BASIN),
            boat_world_test_water(basin, waves, false, false), BOAT_OBJECT_LIFE_TICKS,
            BOAT_OBJECT_DEFAULT_HEIGHT))
        return false;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        boat_fleet_place(&fixture->fleet, i,
            (Boat) { .position = stress_centres[i],
                .heading = (float)i * (2 * PI / BOAT_FLEET_SLOTS),
                .throttle = .55f });
        boat_presentation_reset(&fixture->presentations[i]);
    }
    fixture->traffic_ready = waterfront_traffic_wakes_init(&fixture->traffic, 0);
    return fixture->traffic_ready;
}

// One fixed tick for every boat, then the traffic pool and cosmetic followers. The pool holds
// each emitter's thirty-two live pieces, so a completed pre-roll proves saturated counts.
static bool stress_step(StressFixture *fixture)
{
    BoatInput inputs[BOAT_FLEET_SLOTS];
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        inputs[i] = stress_input(&fixture->fleet.state.slots[i].boat, i, fixture->fleet.state.tick);
    if (!boat_fleet_step(&fixture->fleet, inputs) || fixture->fleet.stats.emission_failures)
        return false;
    double now = boat_fleet_time(&fixture->fleet);
    if (!waterfront_traffic_wakes_step(&fixture->traffic, now))
        return false;
    BoatWorld world = boat_fleet_world(&fixture->fleet);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        boat_presentation_step(
            &fixture->presentations[i], &fixture->fleet.state.slots[i].boat, &world, now);
    ++fixture->ticks;
    return true;
}

static bool stress_advance(StressFixture *fixture, unsigned ticks)
{
    for (unsigned i = 0; i < ticks; ++i)
        if (!stress_step(fixture))
            return false;
    return true;
}

// The player view of slot 0 and the other three slots as rivals; WORLD borrows both pools.
static BoatWorld stress_world(const StressFixture *fixture)
{
    BoatWorld world = boat_fleet_world(&fixture->fleet);
    world.water.traffic_objects = &fixture->traffic;
    return world;
}

static BoatViewState stress_view(const StressFixture *fixture)
{
    return (BoatViewState) { .boat = fixture->fleet.state.slots[0].boat,
        .time = boat_fleet_time(&fixture->fleet) };
}

static BoatRivals stress_rivals(const StressFixture *fixture)
{
    BoatRivals rivals = { 0 };
    for (unsigned i = 1; i < BOAT_FLEET_SLOTS; ++i) {
        rivals.boats[rivals.count++]
            = (BoatRival) { .view = { .boat = fixture->fleet.state.slots[i].boat,
                                .time = boat_fleet_time(&fixture->fleet),
                                .skin = i },
                  .presentation = &fixture->presentations[i] };
    }
    return rivals;
}

// Exact fleet digest and per-slot pose. The wake counts prove the stress load reached capacity.
static void format_stress_state(char *out, size_t size, const StressFixture *fixture)
{
    int used = snprintf(out, size,
        "tick=%llu hash=%016llx wake_live=%u wake_peak=%u "
        "wake_emitted=%llu wake_expired=%llu traffic_live=%u traffic_peak=%u traffic_emitted=%llu",
        (unsigned long long)fixture->fleet.state.tick,
        (unsigned long long)boat_fleet_hash(&fixture->fleet.state), fixture->fleet.state.wakes.live,
        fixture->fleet.state.wakes.peak_live,
        (unsigned long long)fixture->fleet.state.wakes.emitted,
        (unsigned long long)fixture->fleet.state.wakes.expired, fixture->traffic.live,
        fixture->traffic.peak_live, (unsigned long long)fixture->traffic.emitted);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS && used > 0 && (size_t)used < size; ++i) {
        const Boat *boat = &fixture->fleet.state.slots[i].boat;
        used += snprintf(out + used, size - (size_t)used, " slot%u=%a,%a,%a,%a", i,
            (double)boat->position.x, (double)boat->position.y, (double)boat->heading,
            (double)boat->velocity.x);
    }
}

// Display-free fixture state for the benchmark contract: the same pre-roll and measured ticks.
static int stress_state(void)
{
    StressFixture fixture;
    char state[1024];
    // The benchmark draws frame zero without stepping, then two ticks per later frame.
    if (!stress_init(&fixture, true) || !stress_advance(&fixture, STRESS_PREROLL_TICKS)
        || !stress_advance(&fixture, (STRESS_FRAMES - 1) * STRESS_TICKS_PER_FRAME))
        return 1;
    format_stress_state(state, sizeof(state), &fixture);
    printf("stress_state %s\n", state);
    test_worlds_close();
    return 0;
}

#define BENCH_WARMUP 120
#define BENCH_FRAMES (MAX_TICKS / 2 + 2)
#define BENCH_DEADLINE_MS (1000.0 / 60.0)
#define GPU_QUERIES 8

typedef struct {
    int first_tick, end_tick, wave_rebuilds;
    size_t destination, wake_uploads;
    unsigned wake_live, wake_peak_live, wake_emitters, traffic_wake_live;
    float x, z, heading;
    float simulation_ms, render_ms, present_ms, frame_ms, interval_ms, gpu_ms;
    bool has_interval, has_gpu;
    WaterfrontGpuCount total, pass[WATERFRONT_GPU_PASS_COUNT], group[WATERFRONT_GPU_GROUP_COUNT];
    unsigned int culled[WATERFRONT_GPU_PASS_COUNT];
    unsigned int stamp_pieces, stamp_outside;
    size_t stamp_texels;
    unsigned int history_substeps;
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
BENCH_SELECT(submissions, total.submissions, true)
BENCH_SELECT(instances, total.instances, true)
BENCH_SELECT(triangles, total.triangles, true)
BENCH_SELECT(map_submissions, group[WATERFRONT_GPU_GROUP_MAP].submissions, true)
BENCH_SELECT(map_triangles, group[WATERFRONT_GPU_GROUP_MAP].triangles, true)
BENCH_SELECT(stamp_pieces, stamp_pieces, true)
BENCH_SELECT(stamp_outside, stamp_outside, true)
BENCH_SELECT(stamp_texels, stamp_texels, true)
BENCH_SELECT(history_substeps, history_substeps, true)

static void bench_count(BenchFrame *frame, const WaterfrontGpuStats *stats)
{
    frame->total = waterfront_gpu_stats_total(stats);
    frame->stamp_pieces = stats->wake_pieces;
    frame->stamp_outside = stats->wake_outside;
    frame->stamp_texels = stats->wake_texels;
    frame->history_substeps = stats->history_substeps;
    for (int pass = 0; pass < WATERFRONT_GPU_PASS_COUNT; ++pass)
        frame->culled[pass] = stats->tiles_culled[pass];
    for (int pass = 0; pass < WATERFRONT_GPU_PASS_COUNT; ++pass)
        for (int group = 0; group < WATERFRONT_GPU_GROUP_COUNT; ++group) {
            WaterfrontGpuCount count = stats->mesh[pass][group];
            frame->pass[pass].submissions += count.submissions;
            frame->pass[pass].triangles += count.triangles;
            frame->group[group].submissions += count.submissions;
            frame->group[group].triangles += count.triangles;
        }
}

static int bench_fail(const char *message)
{
    fprintf(stderr, "boat_water_review: %s\n", message);
    return 1;
}

// Continuous river route at two ticks per rendered frame. Timed work is simulation, scene
// submission and presentation; route checks, GPU polling and report writing are outside it.
// Visible pacing: "cap" is raylib's SetTargetFPS(60) (the A0.4 baseline), "deadline" the game's
// 60 Hz pacer without vsync and "vsync" the game default (vsync with the pacer as a backstop).
// Prepares a view's scenery at TILE_SIZE before its first draw; drawing keeps a ready scenery
// of the same map. Zero keeps the default size.
static bool prepare_location_tiles(
    WaterfrontScenery *scenery, BoatLocationId location, float tile_size)
{
    return !(tile_size > 0)
        || waterfront_scenery_init_tiled(scenery, test_location(location)->map, tile_size);
}

static bool prepare_tiles(WaterfrontScenery *scenery, float tile_size)
{
    return prepare_location_tiles(scenery, TEST_RIVER, tile_size);
}

static int benchmark(const char *prefix, bool waves, bool aa, bool visible, const char *pacing,
    float tile_size, const Route *route)
{
    // No route: the four-boat stress fixture.
    bool stress = !route;
    char csv_path[4096], json_path[4096], state[1024];
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
    const BoatWorldDefinition *location
        = test_location(stress ? BOAT_LOCATION_BASIN : route->location);
    BoatSession session;
    StressFixture stress_fixture;
    if (!stress)
        boat_session_init(
            &session, test_world(route->location), waves ? boat_water_gentle() : (BoatWater) { 0 });
    const Boat *boat = stress ? &stress_fixture.fleet.state.slots[0].boat : &session.boat;
    const BoatCamera camera = stress
        ? (BoatCamera) { .distance = 26, .height = 16 }
        : (BoatCamera) { .distance = location->camera_distance, .height = location->camera_height };
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
    if (!IsWindowReady() || !boat_gpu_init(&gpu, aa)
        || (!stress && !prepare_location_tiles(&gpu.scenery, route->location, tile_size))) {
        error = "graphics initialization failed";
        goto cleanup;
    }
    if (!apply_scene_options(&gpu.scene)) {
        error = "wake stamp field unavailable";
        goto cleanup;
    }
    if (!apply_review_preset(&gpu)) {
        error = "presentation preset unavailable";
        goto cleanup;
    }
    gpu.daylight = review_daylight;
    gpu.hour = review_hour;
    gpu.hour_set = review_hour_set;
    if (stress
        && (!stress_init(&stress_fixture, waves)
            || !stress_advance(&stress_fixture, STRESS_PREROLL_TICKS))) {
        error = "stress fixture failed";
        goto cleanup;
    }
    if (stress)
        tick = (int)stress_fixture.fleet.state.tick;
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
        if (stress) {
            if (count > 0 && !stress_advance(&stress_fixture, STRESS_TICKS_PER_FRAME)) {
                error = "stress fixture failed";
                goto cleanup;
            }
            tick = (int)stress_fixture.fleet.state.tick;
            finished = count + 1 >= STRESS_FRAMES;
        } else if (count > 0) {
            do {
                drive(&session, route, &destination);
                ++tick;
                finished = route_finished(boat, route, destination);
            } while (tick % 2 && !finished);
        }
        double simulated = boat_profile_time(false);
        int rebuilds = gpu.wave_rebuilds;
        size_t uploads = gpu.wake_uploads;
        bool timed = gpu_timer_begin(&timer, count);
        if (stress) {
            BoatWorld world = stress_world(&stress_fixture);
            BoatViewState view = stress_view(&stress_fixture);
            BoatRivals rivals = stress_rivals(&stress_fixture);
            boat_gpu_draw_scene(&gpu, &view, &stress_fixture.presentations[0], &world, camera,
                &rivals, NULL, NULL, 0);
        } else {
            boat_gpu_draw(
                &gpu, SESSION_VIEW(&session), &session.presentation, &session.world, camera);
        }
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
        if (stress) {
            frame->wake_live = stress_fixture.fleet.state.wakes.live;
            frame->wake_peak_live = stress_fixture.fleet.state.wakes.peak_live;
            frame->wake_emitters = BOAT_FLEET_SLOTS;
            frame->traffic_wake_live = stress_fixture.traffic.live;
        }
        bench_count(frame, &gpu.scene.stats);
        previous_start = start;
        gpu_timer_poll(&timer, frames, false);
        ++count;
        if (stress) {
            if (finished)
                break;
            if (count == BENCH_FRAMES) {
                error = "stress run exceeded its frame capacity";
                goto cleanup;
            }
        } else {
            // Frame poses only; --check covers every tick and the report's final state proves
            // that this run followed the same trajectory.
            if (!hull_clear(boat, route->location)) {
                error = "route too close to shore";
                goto cleanup;
            }
            if (finished)
                break;
            if (tick >= MAX_TICKS || count == BENCH_FRAMES) {
                error = "route did not finish";
                goto cleanup;
            }
        }
        if (visible && WindowShouldClose()) {
            error = "window closed before the run finished";
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
          "wave_patch_rebuilds,wake_uploads,app_submissions,app_instances,scene_triangles,"
          "shadow_submissions,shadow_triangles,reflection_submissions,reflection_triangles,"
          "main_submissions,main_triangles,map_submissions,map_triangles,"
          "water_submissions,water_triangles,object_submissions,object_triangles,"
          "shadow_tiles_culled,reflection_tiles_culled,main_tiles_culled,"
          "wake_live,wake_peak_live,wake_emitters,traffic_wake_live,"
          "wake_stamp_pieces,wake_stamp_outside,wake_stamp_texels,water_history_substeps\n",
        csv);
    for (int i = 0; i < count; ++i) {
        const BenchFrame *f = &frames[i];
        char interval[32] = "", gpu_ms[32] = "";
        if (f->has_interval)
            snprintf(interval, sizeof(interval), "%.9g", (double)f->interval_ms);
        if (f->has_gpu)
            snprintf(gpu_ms, sizeof(gpu_ms), "%.9g", (double)f->gpu_ms);
        fprintf(csv, "%d,%d,%d,%d,%d,%zu,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%s,%s,%d,%zu,%u,%u,%zu",
            i, i < BENCH_WARMUP, f->first_tick, f->end_tick, f->end_tick - f->first_tick,
            f->destination, (double)f->x, (double)f->z, (double)f->heading,
            (double)f->simulation_ms, (double)f->render_ms, (double)f->present_ms,
            (double)f->frame_ms, interval, gpu_ms, f->wave_rebuilds, f->wake_uploads,
            f->total.submissions, f->total.instances, f->total.triangles);
        for (int pass = 0; pass < WATERFRONT_GPU_PASS_COUNT; ++pass)
            fprintf(csv, ",%u,%zu", f->pass[pass].submissions, f->pass[pass].triangles);
        for (int group = 0; group < WATERFRONT_GPU_GROUP_COUNT; ++group)
            fprintf(csv, ",%u,%zu", f->group[group].submissions, f->group[group].triangles);
        for (int pass = 0; pass < WATERFRONT_GPU_PASS_COUNT; ++pass)
            fprintf(csv, ",%u", f->culled[pass]);
        fprintf(csv, ",%u,%u,%u,%u,%u,%u,%zu,%u\n", f->wake_live, f->wake_peak_live,
            f->wake_emitters, f->traffic_wake_live, f->stamp_pieces, f->stamp_outside,
            f->stamp_texels, f->history_substeps);
    }
    unsigned traffic_peak = 0;
    for (int i = 0; i < count; ++i)
        traffic_peak = frames[i].traffic_wake_live > traffic_peak ? frames[i].traffic_wake_live
                                                                  : traffic_peak;
    if (stress)
        format_stress_state(state, sizeof(state), &stress_fixture);
    else
        format_final_state(state, sizeof(state), boat, tick);
    // CPU cost of one cluster build and pack, repeated over the final frame's lights.
    double light_build_us = 0;
    if (gpu.scene.light_clusters_active) {
        static WaterfrontLightClusters rebuilt;
        static float texels[WATERFRONT_LIGHT_TEXELS * 4];
        const WaterfrontLightClusters *source = gpu.scene.light_clusters;
        enum { REBUILDS = 50 };
        double begin = boat_profile_time(false);
        for (int r = 0; r < REBUILDS; ++r) {
            waterfront_lights_begin(&rebuilt);
            for (int i = 0; i < source->light_count; ++i)
                waterfront_lights_add(&rebuilt, source->lights[i]);
            waterfront_lights_build(&rebuilt);
            waterfront_lights_pack(&rebuilt, texels);
        }
        light_build_us = (boat_profile_time(false) - begin) * 1e6 / REBUILDS;
    }
    fprintf(json,
        "{\n  \"schema_revision\":7,\"tool\":\"boat_water_review\",\"mode\":\"benchmark\",\n"
        "  \"scenario\":\"%s\",\"route\":\"%s\",\"route_revision\":1,\"physics_revision\":%d,\n"
        "  \"build\":\"%s\",\"platform\":\"%s\",\n  \"gl_renderer\":",
        stress ? "stress-basin" : route->name, stress ? "stress-basin" : route->name,
        BOAT_PHYSICS_REVISION, BOAT_BENCH_BUILD, BOAT_BENCH_PLATFORM);
    json_string(json, renderer_name);
    fputs(",\"gl_version\":", json);
    json_string(json, gl_version);
    fprintf(json,
        ",\n  \"lighting_mode\":\"%s\",\"daylight\":%.6g,\"lighting_hour\":%.6g,"
        "\"stress_lights\":%d,\"light_build_us\":%.6g,\"lights\":",
        review_hour_set ? "hour" : "daylight", (double)review_daylight, (double)review_hour,
        gpu.scene.stress_lights, light_build_us);
    json_lights(json, &gpu.scene);
    fprintf(json,
        ",\n  \"renderer\":\"gpu\",\"aa\":\"%s\",\"preset\":\"%s\",\"window\":\"%s\","
        "\"tile_size\":%.9g,\n"
        "  \"resolution\":\"%s\",\"hdr\":%s,\"hdr_bytes\":%zu,\"reflection_size\":[%d,%d],\n"
        "  \"window_size\":[960,600],\"framebuffer_size\":[%d,%d],"
        "\"virtual_resolution\":[320,200],\"internal_resolution\":[%d,%d],\n"
        "  \"pacing\":\"%s\",\"vsync_hint\":%s,\"monitor_refresh_hz\":",
        aa ? "4x4" : "off", waves ? "waves" : "flat", visible ? "visible" : "hidden",
        (double)gpu.scenery.tile_size, boat_resolution_name(review_resolution),
        boat_gpu_hdr_active(&gpu) ? "true" : "false", boat_gpu_hdr_bytes(&gpu),
        gpu.scene.reflection.texture.width, gpu.scene.reflection.texture.height, render_width,
        render_height, aa ? 1280 : gpu.target.texture.width, aa ? 800 : gpu.target.texture.height,
        pacing_name, vsync ? "true" : "false");
    if (refresh > 0)
        fprintf(json, "%d", refresh);
    else
        fputs("null", json);
    fprintf(json,
        ",\n  \"clock\":\"boat_profile_time(false) platform monotonic\","
        "\"tick_hz\":120,\"ticks_per_frame\":2,\n"
        "  \"frames\":%d,\"warmup_frames\":%d,\"measured_frames\":%d,\"total_ticks\":%d,"
        "\"warmup_end_tick\":%d,\n"
        "  \"run_completed\":true,\"route_completed\":%s,\"destination\":%zu,"
        "\"final_state\":\"%s\",\n"
        "  \"deadline_ms\":%.9g,\"deadline_rule\":\"value > deadline_ms\","
        "\"frame_deadline_misses\":%d,\"interval_deadline_misses\":%d,\n",
        count, BENCH_WARMUP, count - BENCH_WARMUP, tick, frames[BENCH_WARMUP - 1].end_tick,
        stress ? "false" : "true", destination, state, BENCH_DEADLINE_MS, frame_misses,
        interval_misses);
    json_stats(json, "simulation_ms", frames, count, scratch, bench_simulation);
    json_stats(json, "render_submit_ms", frames, count, scratch, bench_render);
    json_stats(json, "present_ms", frames, count, scratch, bench_present);
    json_stats(json, "frame_ms", frames, count, scratch, bench_frame);
    json_stats(json, "frame_interval_ms", frames, count, scratch, bench_interval);
    json_stats(json, "gpu_ms", frames, count, scratch, bench_gpu);
    json_stats(json, "app_submissions", frames, count, scratch, bench_submissions);
    json_stats(json, "app_instances", frames, count, scratch, bench_instances);
    json_stats(json, "scene_triangles", frames, count, scratch, bench_triangles);
    json_stats(json, "map_submissions", frames, count, scratch, bench_map_submissions);
    json_stats(json, "map_triangles", frames, count, scratch, bench_map_triangles);
    const WaterfrontWakeStamp *stamp = &gpu.scene.wake_stamp;
    bool stamped = stamp->supported && !gpu.scene.wake_stamp_disabled;
    fprintf(json,
        "  \"wake_shading\":{\"mode\":\"%s\",\"field_size\":%d,\"spacing_m\":%.9g,"
        "\"field_bytes\":%zu,\"builds\":%llu},\n",
        stamped ? "stamped" : "analytic", stamp->size, (double)stamp->spacing,
        waterfront_wake_stamp_bytes(stamp), stamp->builds);
    json_stats(json, "wake_stamp_pieces", frames, count, scratch, bench_stamp_pieces);
    json_stats(json, "wake_stamp_outside", frames, count, scratch, bench_stamp_outside);
    json_stats(json, "wake_stamp_texels", frames, count, scratch, bench_stamp_texels);
    const WaterfrontWaterHistory *history = &gpu.scene.water_history;
    bool historic = history->supported && !gpu.scene.water_history_disabled;
    fprintf(json,
        "  \"water_history\":{\"mode\":\"%s\",\"field_size\":%d,\"spacing_m\":%.9g,"
        "\"field_bytes\":%zu,\"substeps\":%llu,\"resets\":%llu},\n",
        historic ? "history" : "off", WATERFRONT_WATER_HISTORY_SIZE,
        (double)WATERFRONT_WATER_HISTORY_SPACING, waterfront_water_history_bytes(history),
        history->substeps, history->resets);
    json_stats(json, "water_history_substeps", frames, count, scratch, bench_history_substeps);
    if (stress) {
        fprintf(json,
            "  \"wake_objects\":{\"emitters\":%u,\"boat_pool_capacity\":%d,\"live\":%u,"
            "\"peak_live\":%u,\"emitted\":%llu,\"expired\":%llu,"
            "\"traffic_live\":%u,\"traffic_peak_live\":%u,\"traffic_emitted\":%llu},\n"
            "  \"wake_objects_note\":\"Four fleet emitters share one 128-slot boat pool; the "
            "traffic pool adds a second 128-record texture row. Counts are live records, not "
            "draw calls.\",\n",
            BOAT_FLEET_SLOTS, BOAT_OBJECT_CAPACITY, stress_fixture.fleet.state.wakes.live,
            stress_fixture.fleet.state.wakes.peak_live,
            (unsigned long long)stress_fixture.fleet.state.wakes.emitted,
            (unsigned long long)stress_fixture.fleet.state.wakes.expired,
            stress_fixture.traffic.live, traffic_peak,
            (unsigned long long)stress_fixture.traffic.emitted);
    } else {
        fputs("  \"wake_objects\":null,\n", json);
    }
    fprintf(json,
        "  \"gpu_timer\":%s,\"gpu_samples\":%d,\"gpu_samples_skipped\":%d,\n"
        "  \"wave_patch_rebuilds\":%d,\"wave_patch_rebuilds_measured\":%d,"
        "\"wave_vertex_capacity\":%d,\"wave_peak_triangles\":%d,\n"
        "  \"submission_counter\":\"waterfront_gpu_scene_draw DrawMesh calls plus batched sky "
        "quads; excludes the AA downsample and presentation\",\"driver_draw_calls\":null\n}\n",
        timer.supported ? "\"GL_TIME_ELAPSED around the scene draw, asynchronous\"" : "null",
        gpu_samples, timer.supported ? count - BENCH_WARMUP - gpu_samples : 0, gpu.wave_rebuilds,
        measured_rebuilds, gpu.wave_capacity, gpu.wave_peak_triangles);
    ok = !ferror(csv) && !ferror(json);
    printf("benchmark scenario=%s aa=%s preset=%s resolution=%s hdr=%s window=%s frames=%d "
           "warmup=%d ticks=%d gpu_samples=%d\n",
        stress ? "stress-basin" : route->name, aa ? "4x4" : "off", waves ? "waves" : "flat",
        boat_resolution_name(review_resolution), boat_gpu_hdr_active(&gpu) ? "on" : "off",
        visible ? "visible" : "hidden", count, BENCH_WARMUP, tick, gpu_samples);
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

// G0 review poses: fixed map, boat pose and camera, with explicit ticks. Motion cases advance the
// pose between captures so the sequence shows movement rather than a reused still.
typedef struct {
    const char *name, *kind;
    BoatLocationId location;
    Vector2 position;
    float heading, camera_heading, distance, height;
    unsigned tick, frames, step_ticks;
    Vector2 step_position; // Added once per captured frame, renderer X/Z.
    float step_heading; // Added once per captured frame.
} ReviewPose;

static const ReviewPose review_poses[] = {
    { "basin-hull", "static", BOAT_LOCATION_BASIN, { 0, 10 }, 0, 0, 8, 4, 360, 1, 0, { 0 }, 0 },
    { "basin-wake", "static", BOAT_LOCATION_BASIN, { 0, -20 }, 0, 0, 16, 9, 1080, 1, 0, { 0 }, 0 },
    { "river-quay", "static", TEST_RIVER, { -24, -70 }, -.3f, -.3f, 12, 6, 600, 1, 0, { 0 }, 0 },
    { "river-bridge", "static", TEST_RIVER, { 250, -500 }, .85f, .85f, 14, 6, 900, 1, 0, { 0 }, 0 },
    { "river-skyline", "static", TEST_RIVER, { 650, -850 }, 1.0f, 1.0f + PI, 44, 22, 1200, 1, 0,
        { 0 }, 0 },
    { "outer-dock", "static", BOAT_LOCATION_OUTER, { -12, 60 }, 0, 0, 12, 6, 600, 1, 0, { 0 }, 0 },
    { "outer-coast", "static", BOAT_LOCATION_OUTER, { -24, -70 }, -.4f, -.4f, 36, 18, 1200, 1, 0,
        { 0 }, 0 },
    { "river-tile-boundary", "tile-boundary", TEST_RIVER, { 1100, -950 }, -1.79f, -1.79f, 30, 14,
        1500, 4, 30, { -40, 9 }, 0 },
    { "river-fast-turn", "fast-turn", TEST_RIVER, { 250, -500 }, -.4f, -.4f, 22, 10, 300, 4, 30,
        { 3, 2 }, .45f },
    { "river-stop", "stop-restart", TEST_RIVER, { -15, -180 }, 2.2f, 2.2f, 16, 8, 180, 4, 360,
        { 0 }, 0 },
    { "river-camera-turn", "camera-turn", TEST_RIVER, { -15, -180 }, 2.2f, 2.2f, 20, 10, 180, 4, 30,
        { 0 }, 0 },
    { "river-map-reload", "map-reload", TEST_RIVER, { -12, 60 }, 0, 0, 20, 10, 2100, 4, 120, { 0 },
        0 },
    { "outer-map-reload", "map-reload", BOAT_LOCATION_OUTER, { -12, 60 }, 0, 0, 20, 10, 2100, 4,
        120, { 0 }, 0 },
};

// One rendered frame's parity evidence: the composed image, the mirrored reflection source and both
// shadow depth maps, so a pass cannot hide a difference the final image happens not to show.
typedef struct {
    Image main, reflection;
    float *near_depth, *far_depth;
    WaterfrontGpuCount total;
} ParityFrame;

static float *parity_depth(Texture2D depth, int size)
{
    float *values = malloc((size_t)size * (size_t)size * sizeof(*values));
    if (!values)
        return NULL;
    glBindTexture(GL_TEXTURE_2D, depth.id);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, values);
    glBindTexture(GL_TEXTURE_2D, 0);
    return values;
}

static bool parity_capture(const BoatGpu *gpu, ParityFrame *frame)
{
    const WaterfrontGpuScene *scene = &gpu->scene;
    frame->main = LoadImageFromTexture(gpu->target.texture);
    frame->reflection = LoadImageFromTexture(scene->reflection.texture);
    frame->near_depth = parity_depth(scene->shadow_map.depth, scene->shadow_map_size);
    frame->far_depth
        = parity_depth(scene->far_shadow_map.depth, WATERFRONT_GPU_FAR_SHADOW_MAP_SIZE);
    frame->total = waterfront_gpu_stats_total(&scene->stats);
    return !gpu->failed && frame->main.data && frame->reflection.data && frame->near_depth
        && frame->far_depth;
}

static void parity_release(ParityFrame *frame)
{
    if (frame->main.data)
        UnloadImage(frame->main);
    if (frame->reflection.data)
        UnloadImage(frame->reflection);
    free(frame->near_depth);
    free(frame->far_depth);
    *frame = (ParityFrame) { 0 };
}

static size_t parity_differences(const void *a, const void *b, size_t size, size_t element)
{
    size_t different = 0;
    for (size_t i = 0; i < size / element; ++i)
        different
            += memcmp((const char *)a + i * element, (const char *)b + i * element, element) != 0;
    return different;
}

typedef struct {
    int frames;
    unsigned scalar_submissions, instanced_submissions;
    size_t instances;
} ParityTotals;

// Fails on any changed byte in any pass, or on a change of the work drawn.
static bool parity_compare(const BoatGpu *gpu, const ParityFrame *scalar,
    const ParityFrame *instanced, const char *name, unsigned frame, bool aa, ParityTotals *totals)
{
    int near_size = gpu->scene.shadow_map_size, far_size = WATERFRONT_GPU_FAR_SHADOW_MAP_SIZE;
    size_t differences[4] = {
        parity_differences(scalar->main.data, instanced->main.data,
            (size_t)GetPixelDataSize(scalar->main.width, scalar->main.height, scalar->main.format),
            4),
        parity_differences(scalar->reflection.data, instanced->reflection.data,
            (size_t)GetPixelDataSize(
                scalar->reflection.width, scalar->reflection.height, scalar->reflection.format),
            4),
        parity_differences(scalar->near_depth, instanced->near_depth,
            (size_t)near_size * (size_t)near_size * sizeof(float), sizeof(float)),
        parity_differences(scalar->far_depth, instanced->far_depth,
            (size_t)far_size * (size_t)far_size * sizeof(float), sizeof(float)),
    };
    totals->scalar_submissions += scalar->total.submissions;
    totals->instanced_submissions += instanced->total.submissions;
    totals->instances += instanced->total.instances;
    ++totals->frames;
    bool same_work = scalar->total.triangles == instanced->total.triangles
        && scalar->total.instances == instanced->total.instances;
    if (!differences[0] && !differences[1] && !differences[2] && !differences[3] && same_work)
        return true;
    fprintf(stderr,
        "instancing changed %s-%u aa=%d main=%zu reflection=%zu near_shadow=%zu far_shadow=%zu "
        "triangles=%zu/%zu instances=%u/%u\n",
        name, frame, aa, differences[0], differences[1], differences[2], differences[3],
        scalar->total.triangles, instanced->total.triangles, scalar->total.instances,
        instanced->total.instances);
    return false;
}

typedef struct {
    const BoatViewState *view;
    const BoatPresentation *presentation;
    const BoatWorld *world;
    const BoatRivals *rivals;
    BoatCamera camera;
} ParityScene;

// Draws one scene scalar, instanced, then instanced with a replay every few objects (the batch
// capacity boundary), from the same state. The tolerance is zero.
static bool parity_frame(BoatGpu *gpu, const ParityScene *scene, const char *name, unsigned frame,
    bool aa, ParityTotals *totals)
{
    ParityFrame frames[3] = { 0 };
    bool ok = true;
    for (int mode = 0; ok && mode < 3; ++mode) {
        waterfront_gpu_scene_set_instancing_enabled(&gpu->scene, mode != 0);
        waterfront_gpu_scene_set_instance_limit(&gpu->scene, mode == 2 ? 3 : 0);
        boat_gpu_draw_scene(gpu, scene->view, scene->presentation, scene->world, scene->camera,
            scene->rivals, NULL, NULL, 0);
        ok = parity_capture(gpu, &frames[mode]);
    }
    waterfront_gpu_scene_set_instance_limit(&gpu->scene, 0);
    ParityTotals boundary = { 0 };
    ok = ok && parity_compare(gpu, &frames[0], &frames[1], name, frame, aa, totals)
        && parity_compare(gpu, &frames[0], &frames[2], name, frame, aa, &boundary);
    for (int mode = 0; mode < 3; ++mode)
        parity_release(&frames[mode]);
    return ok;
}

// G1: every G0 review pose and the four-boat stress scene, on the same build and platform, with AA
// off and 4x4.
// G3a stamped wake shading against the analytic pieces, on the saturated four-boat fixture.
// Predeclared bounds (docs/BOATING_PERFORMANCE.md): fixed world samples inside the field, and
// presented captures of the stamped and analytic paths. Reading the field back is test-only.
#define STAMP_SAMPLES 200000
#define STAMP_BOUND_HEIGHT_RMS .004
#define STAMP_BOUND_HEIGHT_MAX .05
#define STAMP_BOUND_SLOPE_RMS .012
#define STAMP_BOUND_SLOPE_MAX .15
#define STAMP_BOUND_FOAM_RMS .012
#define STAMP_BOUND_FOAM_MAX .2
#define STAMP_BOUND_IMAGE_MEAN .5 // Per channel, /255.
#define STAMP_BOUND_IMAGE_P999 32 // 99.9th percentile channel difference, /255.

typedef struct {
    double height_sq, slope_sq, foam_sq, height_max, slope_max, foam_max;
    int samples;
} StampError;

// The shader's height, slope and crest foam of both pools at shader world X/Z. Shader X is
// physics -X, so its slope along X changes sign.
static Vector4 stamp_reference(
    const BoatWakeObjects *player, const BoatWakeObjects *traffic, float x, float z)
{
    Vector2 p = { -x, z };
    BoatWakeObjectSample a = boat_wake_objects_query(player, p);
    BoatWakeObjectSample b = boat_wake_objects_query(traffic, p);
    return (Vector4) { a.height + b.height, -(a.gradient.x + b.gradient.x),
        a.gradient.y + b.gradient.y, a.crest_foam + b.crest_foam };
}

static Vector4 stamp_bilinear(const float *texels, int size, float u, float v)
{
    float tx = u * size - .5f, tz = v * size - .5f;
    int x = (int)floorf(tx), z = (int)floorf(tz);
    float fx = tx - x, fz = tz - z;
    float out[4] = { 0 };
    for (int c = 0; c < 4; ++c) {
        const float *t = texels + c;
        float bottom = t[4 * (z * size + x)] * (1 - fx) + t[4 * (z * size + x + 1)] * fx;
        float top = t[4 * ((z + 1) * size + x)] * (1 - fx) + t[4 * ((z + 1) * size + x + 1)] * fx;
        out[c] = bottom * (1 - fz) + top * fz;
    }
    return (Vector4) { out[0], out[1], out[2], out[3] };
}

static bool stamp_read(const WaterfrontWakeStamp *stamp, float *texels)
{
    glBindTexture(GL_TEXTURE_2D, stamp->target.texture.id);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, texels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return glGetError() == GL_NO_ERROR;
}

// Sample the read-back field at fixed world points inside its guard, where either side has wake.
static StampError stamp_errors(const WaterfrontWakeStamp *stamp, const float *texels,
    const BoatWakeObjects *player, const BoatWakeObjects *traffic)
{
    StampError e = { 0 };
    float rect[4];
    waterfront_wake_stamp_rect(stamp, rect);
    unsigned seed = 12345;
    for (int i = 0; i < STAMP_SAMPLES; ++i) {
        float uv[2];
        for (int k = 0; k < 2; ++k) {
            seed = seed * 1664525u + 1013904223u;
            uv[k] = rect[3] + (1 - 2 * rect[3]) * (float)(seed >> 8) / 16777216.0f;
        }
        float x = rect[0] + uv[0] / rect[2], z = rect[1] + uv[1] / rect[2];
        Vector4 r = stamp_reference(player, traffic, x, z);
        Vector4 g = stamp_bilinear(texels, stamp->size, uv[0], uv[1]);
        if (r.x == 0 && r.y == 0 && r.z == 0 && r.w == 0 && g.x == 0 && g.y == 0 && g.w == 0)
            continue;
        double dh = fabs(g.x - r.x), ds = hypot(g.y - r.y, g.z - r.z);
        double df = fabs(fminf(fmaxf(g.w, 0), 1) - fminf(fmaxf(r.w, 0), 1));
        e.height_sq += dh * dh;
        e.slope_sq += ds * ds;
        e.foam_sq += df * df;
        e.height_max = fmax(e.height_max, dh);
        e.slope_max = fmax(e.slope_max, ds);
        e.foam_max = fmax(e.foam_max, df);
        ++e.samples;
    }
    return e;
}

static bool stamp_within(StampError e, const char *label)
{
    double n = e.samples ? e.samples : 1;
    double hr = sqrt(e.height_sq / n), sr = sqrt(e.slope_sq / n), fr = sqrt(e.foam_sq / n);
    printf("wake_stamp %s samples=%d height_rms=%.5f height_max=%.4f slope_rms=%.5f "
           "slope_max=%.4f foam_rms=%.5f foam_max=%.4f\n",
        label, e.samples, hr, e.height_max, sr, e.slope_max, fr, e.foam_max);
    return e.samples > 1000 && hr <= STAMP_BOUND_HEIGHT_RMS
        && e.height_max <= STAMP_BOUND_HEIGHT_MAX && sr <= STAMP_BOUND_SLOPE_RMS
        && e.slope_max <= STAMP_BOUND_SLOPE_MAX && fr <= STAMP_BOUND_FOAM_RMS
        && e.foam_max <= STAMP_BOUND_FOAM_MAX;
}

// Keep only the packed records whose indices are listed: player pool first, then traffic, in
// boat_wake_objects_pack order.
static void stamp_keep(BoatWakeObjects *pool, int first, const float *listed, int count)
{
    int index = first;
    for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i) {
        if (!pool->objects[i].amplitude)
            continue;
        bool keep = false;
        for (int k = 0; k < count; ++k)
            keep |= (int)listed[4 * k] == index;
        if (!keep)
            pool->objects[i].amplitude = 0;
        ++index;
    }
}

// Pieces not listed as outside must vanish outside the guard: there, the listed pieces alone
// equal both pools. Samples run along a ring just beyond the sampled area.
static bool stamp_outside_exact(const WaterfrontGpuScene *scene, const BoatWakeObjects *player,
    const BoatWakeObjects *traffic, int *checked)
{
    static BoatWakeObjects kept_player, kept_traffic;
    kept_player = *player;
    kept_traffic = *traffic;
    // The stress fixture has both pools: three rows of 256 vec4s.
    const float *listed = scene->object_records + 8 * BOAT_OBJECT_CAPACITY * 2;
    stamp_keep(&kept_player, 0, listed, scene->object_outside);
    stamp_keep(&kept_traffic, scene->object_players, listed, scene->object_outside);
    float rect[4];
    waterfront_wake_stamp_rect(&scene->wake_stamp, rect);
    float extent = 1 / rect[2];
    *checked = 0;
    for (int i = 0; i < 4000; ++i) {
        float t = i / 4000.0f, margin = .1f + 30 * (float)(i % 7) / 6;
        float along = -margin + (extent + 2 * margin) * t;
        float x, z;
        switch (i % 4) {
        case 0:
            x = rect[0] + along, z = rect[1] - margin;
            break;
        case 1:
            x = rect[0] + along, z = rect[1] + extent + margin;
            break;
        case 2:
            x = rect[0] - margin, z = rect[1] + along;
            break;
        default:
            x = rect[0] + extent + margin, z = rect[1] + along;
            break;
        }
        Vector4 all = stamp_reference(player, traffic, x, z);
        Vector4 kept = stamp_reference(&kept_player, &kept_traffic, x, z);
        *checked += all.x != 0 || all.w != 0;
        if (fabsf(all.x - kept.x) > 1e-5f || fabsf(all.y - kept.y) > 1e-5f
            || fabsf(all.z - kept.z) > 1e-5f || fabsf(all.w - kept.w) > 1e-5f)
            return false;
    }
    return true;
}

static void stamp_draw(BoatGpu *gpu, const StressFixture *fixture, BoatCamera camera)
{
    BoatWorld world = stress_world(fixture);
    BoatViewState view = stress_view(fixture);
    BoatRivals rivals = stress_rivals(fixture);
    boat_gpu_draw_scene(
        gpu, &view, &fixture->presentations[0], &world, camera, &rivals, NULL, NULL, 0);
}

static bool stamp_images(
    BoatGpu *gpu, const StressFixture *fixture, BoatCamera camera, double *mean, int *p999)
{
    Image images[2];
    for (int k = 0; k < 2; ++k) {
        waterfront_gpu_scene_set_wake_stamp_enabled(&gpu->scene, k == 0);
        stamp_draw(gpu, fixture, camera);
        images[k] = LoadImageFromTexture(gpu->target.texture);
    }
    waterfront_gpu_scene_set_wake_stamp_enabled(&gpu->scene, true);
    bool ok = images[0].data && images[1].data;
    int histogram[256] = { 0 }, values = images[0].width * images[0].height * 3;
    double sum = 0;
    for (int i = 0; ok && i < images[0].width * images[0].height; ++i)
        for (int c = 0; c < 3; ++c) {
            int d = abs(((unsigned char *)images[0].data)[4 * i + c]
                - ((unsigned char *)images[1].data)[4 * i + c]);
            ++histogram[d];
            sum += d;
        }
    *mean = sum / values;
    int below = 0;
    *p999 = 0;
    while (*p999 < 255 && (below += histogram[*p999]) < values * .999)
        ++*p999;
    UnloadImage(images[0]);
    UnloadImage(images[1]);
    return ok;
}

static int check_wake_stamp(void)
{
#define EXPECT(test)                                                                               \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "Wake stamp probe");
    if (!IsWindowReady())
        return 1;
    int result = 1;
    BoatGpu gpu = { 0 };
    static StressFixture fixture;
    float *texels
        = malloc(sizeof(float) * 4 * WATERFRONT_WAKE_STAMP_SIZE * WATERFRONT_WAKE_STAMP_SIZE);
    const BoatCamera camera = { .distance = 26, .height = 16 };
    EXPECT(texels && boat_gpu_init(&gpu, true) && boat_gpu_resize(&gpu, 320, 200));
    EXPECT(gpu.scene.wake_stamp.supported);
    EXPECT(stress_init(&fixture, true) && stress_advance(&fixture, STRESS_PREROLL_TICKS));
    // Three moments of the saturated fixture: sampled values, the outside list and the image.
    for (int moment = 0; moment < 3; ++moment) {
        stamp_draw(&gpu, &fixture, camera);
        const BoatWakeObjects *player = &fixture.fleet.state.wakes;
        EXPECT(gpu.scene.object_count >= 200 && gpu.scene.stats.wake_texels > 0);
        EXPECT(stamp_read(&gpu.scene.wake_stamp, texels));
        char label[32];
        snprintf(label, sizeof(label), "moment%d", moment);
        EXPECT(stamp_within(
            stamp_errors(&gpu.scene.wake_stamp, texels, player, &fixture.traffic), label));
        int checked;
        EXPECT(stamp_outside_exact(&gpu.scene, player, &fixture.traffic, &checked));
        printf("wake_stamp %s outside=%d of %d ring_samples_with_wake=%d\n", label,
            gpu.scene.object_outside, gpu.scene.object_count, checked);
        double mean;
        int p999;
        EXPECT(stamp_images(&gpu, &fixture, camera, &mean, &p999));
        printf("wake_stamp %s image_mean=%.4f image_p999=%d\n", label, mean, p999);
        EXPECT(mean <= STAMP_BOUND_IMAGE_MEAN && p999 <= STAMP_BOUND_IMAGE_P999);
        EXPECT(stress_advance(&fixture, 360));
    }
    // An identical redraw reuses the field; new records rebuild it, and so does a moved origin.
    unsigned long long builds = gpu.scene.wake_stamp.builds;
    stamp_draw(&gpu, &fixture, camera);
    EXPECT(gpu.scene.wake_stamp.builds == builds + 1);
    stamp_draw(&gpu, &fixture, camera);
    EXPECT(gpu.scene.wake_stamp.builds == builds + 1 && gpu.scene.stats.wake_texels == 0);
    int origin_x = gpu.scene.wake_stamp.origin_x;
    Boat moved = fixture.fleet.state.slots[0].boat;
    fixture.fleet.state.slots[0].boat.position.x += 300;
    stamp_draw(&gpu, &fixture, camera);
    fixture.fleet.state.slots[0].boat = moved;
    EXPECT(gpu.scene.wake_stamp.builds == builds + 2 && gpu.scene.wake_stamp.origin_x != origin_x);
    EXPECT(stamp_read(&gpu.scene.wake_stamp, texels));
    EXPECT(stamp_within(
        stamp_errors(&gpu.scene.wake_stamp, texels, &fixture.fleet.state.wakes, &fixture.traffic),
        "teleport"));
    // Rewinding to an earlier tick rebuilds from those records, with nothing left from later.
    EXPECT(stress_init(&fixture, true) && stress_advance(&fixture, STRESS_PREROLL_TICKS));
    stamp_draw(&gpu, &fixture, camera);
    EXPECT(stamp_read(&gpu.scene.wake_stamp, texels));
    EXPECT(stamp_within(
        stamp_errors(&gpu.scene.wake_stamp, texels, &fixture.fleet.state.wakes, &fixture.traffic),
        "rewind"));
    // The reference switch draws no field.
    builds = gpu.scene.wake_stamp.builds;
    EXPECT(stress_advance(&fixture, 2));
    waterfront_gpu_scene_set_wake_stamp_enabled(&gpu.scene, false);
    stamp_draw(&gpu, &fixture, camera);
    waterfront_gpu_scene_set_wake_stamp_enabled(&gpu.scene, true);
    EXPECT(gpu.scene.wake_stamp.builds == builds && gpu.scene.stats.wake_texels == 0);
    // A new context's resources probe and stamp again.
    boat_gpu_destroy(&gpu);
    EXPECT(boat_gpu_init(&gpu, true) && boat_gpu_resize(&gpu, 320, 200));
    stamp_draw(&gpu, &fixture, camera);
    EXPECT(gpu.scene.wake_stamp.builds == 1 && stamp_read(&gpu.scene.wake_stamp, texels));
    EXPECT(stamp_within(
        stamp_errors(&gpu.scene.wake_stamp, texels, &fixture.fleet.state.wakes, &fixture.traffic),
        "recreated"));
    puts("wake_stamp=ok");
    result = 0;
cleanup:
    free(texels);
    boat_gpu_destroy(&gpu);
    CloseWindow();
    return result;
}
#undef EXPECT

// G3b: foam history and ripples, driven directly with fixtures and through the stress scene.
// Every advance takes an explicit simulated time; nothing waits for the wall clock.
#define HISTORY_TEXELS (WATERFRONT_WATER_HISTORY_SIZE * WATERFRONT_WATER_HISTORY_SIZE)

static bool history_read(const WaterfrontWaterHistory *history, float *texels)
{
    glBindTexture(GL_TEXTURE_2D, waterfront_water_history_texture(history).id);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, texels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return glGetError() == GL_NO_ERROR;
}

static bool history_read_mask(const WaterfrontWaterHistory *history, unsigned char *texels)
{
    glBindTexture(GL_TEXTURE_2D, history->mask.texture.id);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE, texels);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    return glGetError() == GL_NO_ERROR;
}

// The history as an image: foam in red, ripple height around mid-grey in green and blue.
static bool history_export(const WaterfrontWaterHistory *history, const char *path)
{
    float *texels = malloc(sizeof(float) * 4 * HISTORY_TEXELS);
    bool ok = texels && history_read(history, texels);
    if (ok) {
        Image field
            = GenImageColor(WATERFRONT_WATER_HISTORY_SIZE, WATERFRONT_WATER_HISTORY_SIZE, BLACK);
        Color *pixels = field.data;
        for (int i = 0; i < HISTORY_TEXELS; ++i) {
            float h = Clamp(.5f + texels[4 * i + 1] * 25, 0, 1);
            pixels[i] = (Color) { (unsigned char)(Clamp(texels[4 * i], 0, 1) * 255),
                (unsigned char)(h * 255), (unsigned char)(h * 255), 255 };
        }
        ImageFlipVertical(&field);
        ok = ExportImage(field, path);
        UnloadImage(field);
    }
    free(texels);
    return ok;
}

// The texel holding world X/Z, or -1 outside the field.
static int history_texel(const WaterfrontWaterHistory *history, float x, float z)
{
    int i = (int)floorf(x / WATERFRONT_WATER_HISTORY_SPACING) - history->origin_x;
    int j = (int)floorf(z / WATERFRONT_WATER_HISTORY_SPACING) - history->origin_z;
    int size = WATERFRONT_WATER_HISTORY_SIZE;
    return i < 0 || j < 0 || i >= size || j >= size ? -1 : j * size + i;
}

typedef struct {
    double foam, energy, peak; // Sum of foam, sum of squared heights, largest |height|.
    bool finite;
} HistoryTotals;

static HistoryTotals history_totals(const float *texels)
{
    HistoryTotals totals = { .finite = true };
    for (int i = 0; i < HISTORY_TEXELS; ++i) {
        const float *t = texels + 4 * i;
        totals.finite &= isfinite(t[0]) && isfinite(t[1]) && isfinite(t[2]);
        totals.foam += t[0];
        totals.energy += (double)t[1] * t[1];
        totals.peak = fmax(totals.peak, fabsf(t[1]));
    }
    return totals;
}

// A flat water rectangle over world X/Z, for the shore mask.
static Mesh history_water_mesh(float x0, float z0, float x1, float z1)
{
    Mesh mesh = { .vertexCount = 6, .triangleCount = 2 };
    mesh.vertices = MemAlloc(6 * 3 * sizeof(float));
    float corners[6][2]
        = { { x0, z0 }, { x0, z1 }, { x1, z1 }, { x0, z0 }, { x1, z1 }, { x1, z0 } };
    for (int i = 0; i < 6; ++i) {
        mesh.vertices[3 * i] = corners[i][0];
        mesh.vertices[3 * i + 2] = corners[i][1];
    }
    UploadMesh(&mesh, false);
    return mesh;
}

// A stand-in stamped field: one crest-foam disc of RADIUS metres around world X/Z.
static Texture2D history_stamp(float x, float z, float radius, float rect[4])
{
    enum { SIZE = 256 };
    const float spacing = .5f;
    static float texels[SIZE * SIZE * 4];
    rect[0] = x - SIZE * spacing / 2;
    rect[1] = z - SIZE * spacing / 2;
    rect[2] = 1 / (SIZE * spacing);
    rect[3] = .5f / SIZE;
    for (int j = 0; j < SIZE; ++j)
        for (int i = 0; i < SIZE; ++i) {
            float wx = rect[0] + (i + .5f) * spacing, wz = rect[1] + (j + .5f) * spacing;
            bool inside = hypotf(wx - x, wz - z) < radius;
            float *t = texels + 4 * (j * SIZE + i);
            t[0] = inside ? .3f : 0;
            t[1] = t[2] = 0;
            t[3] = inside ? 1 : 0;
        }
    Image image = { texels, SIZE, SIZE, 1, PIXELFORMAT_UNCOMPRESSED_R32G32B32A32 };
    Texture2D texture = LoadTextureFromImage(image);
    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    return texture;
}

// Irregular frame times: 60, 30, about 45 and 20 Hz frames in turn.
static double history_frame(int frame)
{
    static const double steps[] = { 1.0 / 60, 1.0 / 30, 1.0 / 45, 1.0 / 20 };
    double time = 100;
    for (int i = 0; i < frame; ++i)
        time += steps[i % 4];
    return time;
}

// The fixed drive both replays use: a hull crossing the field, a bobbing contact, rain.
static WaterfrontWaterHistoryInput history_drive(int frame, const Mesh *water)
{
    double time = history_frame(frame);
    float run = (float)(time - 100);
    WaterfrontWaterHistoryInput input = { .time = time,
        .content = 1,
        .centre = { 0, 0 },
        .hull_count = 1,
        .contact_count = 1,
        .water = water };
    input.hulls[0] = (Vector3) { -20 + 5 * run, 3, 1.3f };
    input.contacts[0] = (Vector2) { 12, -9 };
    input.rain = (Vector4) { -10, 15, 18, 1 };
    return input;
}

// G4: the water reads a copy of opaque depth only inside a render texture. At the outer dock the
// copy finds the quay walls below the surface and lightens a band along them; nothing else in
// the frame changes, and drawing to the default framebuffer keeps the fallback.
static int check_scene_depth(void)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "Scene depth");
    if (!IsWindowReady())
        return 1;
    int result = 1;
    BoatGpu gpu = { 0 };
    Image with = { 0 }, without = { 0 };
    if (!boat_gpu_init(&gpu, true))
        goto cleanup;
    BoatSession session;
    boat_session_init(&session, test_world(BOAT_LOCATION_OUTER), boat_water_gentle());
    BoatViewState view = { .boat = session.boat, .time = 600 * (double)BOAT_STEP };
    view.boat.position = (Vector2) { -12, 60 };
    view.boat.heading = 0;
    session.presentation.camera_heading = 0;
    const BoatCamera camera = { .distance = 12, .height = 6 };
    boat_gpu_draw(&gpu, &view, &session.presentation, &session.world, camera);
    bool copied = gpu.scene.scene_depth_valid;
    bool atlas = gpu.scene.water_atlas.id != 0; // The outer map binds its baked water atlas.
    with = LoadImageFromTexture(gpu.target.texture);
    waterfront_gpu_scene_set_scene_depth_enabled(&gpu.scene, false);
    boat_gpu_draw(&gpu, &view, &session.presentation, &session.world, camera);
    bool disabled = !gpu.scene.scene_depth_valid;
    without = LoadImageFromTexture(gpu.target.texture);
    waterfront_gpu_scene_set_scene_depth_enabled(&gpu.scene, true);
    WaterfrontSceneFrame frame;
    boat_scene_frame(&view, &session.presentation, &session.world, camera, &frame);
    BeginDrawing();
    waterfront_gpu_scene_draw(&gpu.scene, &frame, 320, 200);
    EndDrawing();
    bool fallback = !gpu.scene.scene_depth_valid;
    if (!with.data || !without.data || with.width != without.width || with.height != without.height)
        goto cleanup;
    int changed = 0, darker = 0, largest = 0;
    for (int y = 0; y < with.height; ++y)
        for (int x = 0; x < with.width; ++x) {
            Color a = GetImageColor(with, x, y), b = GetImageColor(without, x, y);
            int delta = (a.r - b.r) + (a.g - b.g) + (a.b - b.b);
            changed += delta != 0;
            darker += delta < -3;
            largest = abs(delta) > largest ? abs(delta) : largest;
        }
    printf("scene_depth copied=%d disabled=%d fallback=%d atlas=%d changed=%d darker=%d "
           "largest=%d\n",
        copied, disabled, fallback, atlas, changed, darker, largest);
    // The band is a few pixels along each quay: present, bounded, and a lightening.
    if (!copied || !disabled || !fallback || !atlas || changed < 100 || changed > 320 * 200 / 20
        || darker > changed / 20 || largest > 300) {
        fprintf(stderr, "Scene depth shading out of bounds.\n");
        goto cleanup;
    }
    puts("scene_depth=ok");
    result = 0;
cleanup:
    UnloadImage(with);
    UnloadImage(without);
    boat_gpu_destroy(&gpu);
    CloseWindow();
    return result;
}

static int check_water_history(void)
{
#define EXPECT(test)                                                                               \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "Water history");
    if (!IsWindowReady())
        return 1;
    int result = 1;
    WaterfrontWaterHistory history = { 0 }, replay = { 0 }, fallback = { 0 };
    BoatGpu gpu = { 0 };
    static StressFixture fixture;
    float *texels = malloc(sizeof(float) * 4 * HISTORY_TEXELS);
    float *other = malloc(sizeof(float) * 4 * HISTORY_TEXELS);
    unsigned char *mask = malloc(HISTORY_TEXELS);
    // Water covers X in [-40, 40] and Z in [-30, 50] metres; a quay closes the rest.
    Mesh water = history_water_mesh(-40, -30, 40, 50);
    float stamp_rect[4];
    Texture2D stamp = history_stamp(-25, -10, 3, stamp_rect);
    char shaders[4096];
    EXPECT(texels && other && mask && water.vaoId && stamp.id);
    EXPECT(boat_asset_path(shaders, sizeof(shaders), "shaders/waterfront_water_history.fs"));
    *strrchr(shaders, '/') = 0;
    EXPECT(waterfront_water_history_init(&history, shaders, true) && history.supported);
    EXPECT(waterfront_water_history_init(&replay, shaders, true) && replay.supported);

    // Fixed replay: the same inputs at the same times give the same state, bit for bit.
    int peak_substeps = 0;
    for (int frame = 0; frame < 240; ++frame) {
        WaterfrontWaterHistoryInput input = history_drive(frame, &water);
        int a = waterfront_water_history_advance(&history, &input);
        int b = waterfront_water_history_advance(&replay, &input);
        EXPECT(a == b
            && a <= (frame ? WATERFRONT_WATER_HISTORY_CATCH_UP : WATERFRONT_WATER_HISTORY_PREROLL));
        peak_substeps = frame && a > peak_substeps ? a : peak_substeps;
    }
    EXPECT(history.resets == 1 && history_read(&history, texels) && history_read(&replay, other));
    EXPECT(memcmp(texels, other, sizeof(float) * 4 * HISTORY_TEXELS) == 0);
    HistoryTotals drive = history_totals(texels);
    printf("water_history replay substeps=%llu peak_per_draw=%d energy=%.6f peak=%.4f\n",
        history.substeps, peak_substeps, drive.energy, drive.peak);
    EXPECT(drive.finite && drive.energy > 0);

    // Pause: the same time runs nothing and keeps the state.
    WaterfrontWaterHistoryInput paused = history_drive(239, &water);
    for (int i = 0; i < 3; ++i)
        EXPECT(waterfront_water_history_advance(&history, &paused) == 0);
    EXPECT(history_read(&history, other));
    EXPECT(memcmp(texels, other, sizeof(float) * 4 * HISTORY_TEXELS) == 0);

    // The shore mask: open over the water rectangle, closed on the quay, and no water there.
    EXPECT(history_read_mask(&history, mask));
    int water_texel = history_texel(&history, 30, 40), quay_texel = history_texel(&history, 45, 40);
    int north_quay = history_texel(&history, 30, 55), south_quay = history_texel(&history, 30, -35);
    EXPECT(water_texel >= 0 && quay_texel >= 0 && north_quay >= 0 && south_quay >= 0);
    EXPECT(mask[water_texel] > 128 && mask[quay_texel] < 128);
    EXPECT(mask[north_quay] < 128 && mask[south_quay] < 128);
    int closed_wet = 0;
    for (int i = 0; i < HISTORY_TEXELS; ++i)
        closed_wet += mask[i] < 128 && (texels[4 * i] != 0 || texels[4 * i + 1] != 0);
    EXPECT(closed_wet == 0);
    // Inside the hull disc there is no water either.
    WaterfrontWaterHistoryInput last = history_drive(239, &water);
    int hull = history_texel(&history, last.hulls[0].x, last.hulls[0].y);
    EXPECT(hull >= 0 && texels[4 * hull] == 0 && texels[4 * hull + 1] == 0);

    // Catch-up is bounded: a quarter-second gap runs CATCH_UP substeps, a larger one resets.
    paused.time += .25;
    EXPECT(
        waterfront_water_history_advance(&history, &paused) == WATERFRONT_WATER_HISTORY_CATCH_UP);
    unsigned long long resets = history.resets;
    paused.time += WATERFRONT_WATER_HISTORY_JUMP + 1;
    EXPECT(waterfront_water_history_advance(&history, &paused) == WATERFRONT_WATER_HISTORY_PREROLL
        && history.resets == resets + 1);
    // Rewinding, teleporting and changing content reset as well.
    paused.time -= 0.5;
    EXPECT(waterfront_water_history_advance(&history, &paused) == WATERFRONT_WATER_HISTORY_PREROLL
        && history.resets == resets + 2);
    paused.time += 1.0 / 60;
    paused.centre.x += WATERFRONT_WATER_HISTORY_TELEPORT + 1;
    EXPECT(waterfront_water_history_advance(&history, &paused) == WATERFRONT_WATER_HISTORY_PREROLL
        && history.resets == resets + 3);
    paused.time += 1.0 / 60;
    paused.content = 2;
    EXPECT(waterfront_water_history_advance(&history, &paused) == WATERFRONT_WATER_HISTORY_PREROLL
        && history.resets == resets + 4);
    // A reset forgets what came before: the same input after a reset gives the same state.
    waterfront_water_history_invalidate(&replay);
    EXPECT(waterfront_water_history_advance(&replay, &paused) == WATERFRONT_WATER_HISTORY_PREROLL);
    EXPECT(history_read(&history, texels) && history_read(&replay, other));
    EXPECT(memcmp(texels, other, sizeof(float) * 4 * HISTORY_TEXELS) == 0);

    // Foam: seeded from the stamped crest, drifting and decaying; no carpet once it stops.
    WaterfrontWaterHistoryTuning tuning = history.tuning;
    history.tuning.ripple_speed = 0;
    history.tuning.rain_rate = 0;
    WaterfrontWaterHistoryInput seed
        = { .time = 200, .content = 3, .water = &water, .stamp = stamp.id };
    memcpy(seed.stamp_rect, stamp_rect, sizeof(stamp_rect));
    waterfront_water_history_advance(&history, &seed);
    for (int i = 1; i <= 30; ++i) {
        seed.time = 200 + i / 60.0;
        waterfront_water_history_advance(&history, &seed);
    }
    EXPECT(history_read(&history, texels));
    HistoryTotals seeded = history_totals(texels);
    int centre = history_texel(&history, -25, -10);
    EXPECT(centre >= 0 && texels[4 * centre] > .9f && texels[4 * centre] <= 1);
    seed.stamp = 0;
    double half_life = 0;
    for (int i = 1; i <= 5 * 60; ++i) {
        seed.time = 200.5 + i / 60.0;
        waterfront_water_history_advance(&history, &seed);
        if (i % 6 == 0 && !half_life) {
            EXPECT(history_read(&history, texels));
            if (history_totals(texels).foam <= .5 * seeded.foam)
                half_life = i / 60.0;
        }
    }
    EXPECT(history_read(&history, texels));
    HistoryTotals faded = history_totals(texels);
    printf("water_history foam seeded=%.1f half_life=%.2fs after_5s=%.4f\n", seeded.foam, half_life,
        faded.foam / seeded.foam);
    EXPECT(half_life > 0 && half_life <= 1.5 * tuning.foam_half_life);
    EXPECT(faded.foam <= .05 * seeded.foam);

    // Scrolling: one block of origin movement carries the foam with the world; the exposed
    // strip starts empty. Foam neither decays nor drifts here, so values must match exactly.
    history.tuning.foam_half_life = 1e9f;
    history.tuning.foam_drift = (Vector2) { 0 };
    seed.stamp = stamp.id;
    seed.time = 300;
    waterfront_water_history_advance(&history, &seed);
    seed.stamp = 0;
    seed.time += 1.0 / 60;
    waterfront_water_history_advance(&history, &seed);
    EXPECT(history_read(&history, other));
    int before = history_texel(&history, -25, -10), origin_x = history.origin_x;
    float block = WATERFRONT_WATER_HISTORY_BLOCK * WATERFRONT_WATER_HISTORY_SPACING;
    seed.centre.x += block;
    seed.time += 1.0 / 60;
    EXPECT(waterfront_water_history_advance(&history, &seed) == 1);
    EXPECT(history.origin_x == origin_x + WATERFRONT_WATER_HISTORY_BLOCK);
    EXPECT(history_read(&history, texels));
    int after = history_texel(&history, -25, -10);
    EXPECT(after == before - WATERFRONT_WATER_HISTORY_BLOCK && other[4 * before] > .9f);
    int moved = 0, exposed = 0;
    for (int j = 0; j < WATERFRONT_WATER_HISTORY_SIZE; ++j)
        for (int i = 0; i < WATERFRONT_WATER_HISTORY_SIZE; ++i) {
            int k = j * WATERFRONT_WATER_HISTORY_SIZE + i;
            if (i >= WATERFRONT_WATER_HISTORY_SIZE - WATERFRONT_WATER_HISTORY_BLOCK)
                exposed += texels[4 * k] != 0;
            else
                moved += texels[4 * k] != other[4 * (k + WATERFRONT_WATER_HISTORY_BLOCK)];
        }
    printf("water_history scroll moved_mismatch=%d exposed_nonzero=%d\n", moved, exposed);
    EXPECT(moved == 0 && exposed == 0);
    history.tuning = tuning;

    // Stability: a minute of rain, a bobbing contact and a hull circling at speed, against the
    // quay, stays finite and bounded, with energy that levels off instead of growing.
    WaterfrontWaterHistoryInput storm = {
        .content = 4, .water = &water, .hull_count = 1, .contact_count = 1, .rain = { 0, 10, 40, 1 }
    };
    storm.contacts[0] = (Vector2) { 25, 30 };
    double window[2] = { 0 };
    float limit = history.tuning.ripple_limit;
    for (int frame = 0; frame <= 3600; ++frame) {
        storm.time = 400 + frame / 60.0;
        float angle = (float)frame / 60 * .5f;
        storm.hulls[0] = (Vector3) { 22 * cosf(angle), 10 + 22 * sinf(angle), 1.3f };
        waterfront_water_history_advance(&history, &storm);
        if (frame % 300 == 0 && frame >= 1800) {
            EXPECT(history_read(&history, texels));
            HistoryTotals t = history_totals(texels);
            EXPECT(t.finite && t.peak <= limit + 1e-3f);
            window[frame >= 2700] = fmax(window[frame >= 2700], t.energy);
        }
    }
    if (getenv("HISTORY_DUMP"))
        history_export(&history, getenv("HISTORY_DUMP"));
    printf("water_history stability energy_30_45s=%.4f energy_45_60s=%.4f limit=%.3f\n", window[0],
        window[1], limit);
    EXPECT(window[0] > 0 && window[1] <= 1.5 * window[0]);

    // Without float targets there is no history and nothing runs.
    EXPECT(waterfront_water_history_init(&fallback, shaders, false));
    EXPECT(!fallback.supported && waterfront_water_history_advance(&fallback, &storm) == 0
        && waterfront_water_history_bytes(&fallback) == 0);

    // The scene: stepping draws advance it, a repeated frame (pause) runs nothing, the
    // history-off switch runs nothing, and drawing never touches the simulation.
    const BoatCamera camera = { .distance = 26, .height = 16 };
    EXPECT(boat_gpu_init(&gpu, true) && boat_gpu_resize(&gpu, 320, 200));
    EXPECT(gpu.scene.water_history.supported);
    EXPECT(stress_init(&fixture, true) && stress_advance(&fixture, STRESS_PREROLL_TICKS));
    char drawn[4096], undrawn[4096];
    stamp_draw(&gpu, &fixture, camera);
    EXPECT(gpu.scene.stats.history_resets == 1
        && gpu.scene.stats.history_substeps == WATERFRONT_WATER_HISTORY_PREROLL);
    for (int frame = 0; frame < 30; ++frame) {
        EXPECT(stress_advance(&fixture, 2));
        stamp_draw(&gpu, &fixture, camera);
        EXPECT(gpu.scene.stats.history_resets == 0 && gpu.scene.stats.history_substeps >= 1);
    }
    stamp_draw(&gpu, &fixture, camera);
    EXPECT(gpu.scene.stats.history_substeps == 0);
    EXPECT(history_read(&gpu.scene.water_history, texels));
    HistoryTotals scene = history_totals(texels);
    printf("water_history scene foam=%.1f energy=%.6f peak=%.4f bytes=%zu\n", scene.foam,
        scene.energy, scene.peak, waterfront_water_history_bytes(&gpu.scene.water_history));
    EXPECT(scene.finite && scene.foam > 0 && scene.energy > 0);
    Image images[2];
    for (int k = 0; k < 2; ++k) {
        waterfront_gpu_scene_set_water_history_enabled(&gpu.scene, k == 0);
        stamp_draw(&gpu, &fixture, camera);
        images[k] = LoadImageFromTexture(gpu.target.texture);
    }
    EXPECT(gpu.scene.stats.history_substeps == 0);
    waterfront_gpu_scene_set_water_history_enabled(&gpu.scene, true);
    bool differs = images[0].data && images[1].data
        && memcmp(images[0].data, images[1].data, (size_t)images[0].width * images[0].height * 4);
    UnloadImage(images[0]);
    UnloadImage(images[1]);
    EXPECT(differs);
    format_stress_state(drawn, sizeof(drawn), &fixture);
    static StressFixture plain;
    EXPECT(stress_init(&plain, true) && stress_advance(&plain, STRESS_PREROLL_TICKS + 60));
    format_stress_state(undrawn, sizeof(undrawn), &plain);
    EXPECT(strcmp(drawn, undrawn) == 0);
    puts("water_history=ok");
    result = 0;
cleanup:
    free(texels);
    free(other);
    free(mask);
    UnloadMesh(water);
    if (stamp.id)
        UnloadTexture(stamp);
    waterfront_water_history_destroy(&history);
    waterfront_water_history_destroy(&replay);
    waterfront_water_history_destroy(&fallback);
    boat_gpu_destroy(&gpu);
    CloseWindow();
    return result;
}
#undef EXPECT

// Review clip: the saturated stress fixture at 60 Hz from a close follow camera, with history
// (left) and stamp-only (right) side by side, written as numbered PNG frames into DIRECTORY.
static int history_clip(const char *directory, int frames)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "Water history clip");
    if (!IsWindowReady())
        return 1;
    int result = 1;
    BoatGpu gpu = { 0 };
    static StressFixture fixture;
    const BoatCamera camera = { .distance = 22, .height = 13 };
    const int width = 640, height = 400;
    Image strip = GenImageColor(2 * width, height, BLACK);
    if (!boat_gpu_init(&gpu, true) || !boat_gpu_resize(&gpu, width, height)
        || !stress_init(&fixture, true) || !stress_advance(&fixture, STRESS_PREROLL_TICKS))
        goto cleanup;
    for (int frame = 0; frame < frames; ++frame) {
        if (!stress_advance(&fixture, 2))
            goto cleanup;
        for (int k = 0; k < 2; ++k) {
            waterfront_gpu_scene_set_water_history_enabled(&gpu.scene, k == 0);
            stamp_draw(&gpu, &fixture, camera);
            Image image = LoadImageFromTexture(gpu.target.texture);
            ImageFlipVertical(&image);
            ImageDraw(&strip, image, (Rectangle) { 0, 0, (float)width, (float)height },
                (Rectangle) { (float)(k * width), 0, (float)width, (float)height }, WHITE);
            UnloadImage(image);
        }
        char path[4096];
        snprintf(path, sizeof(path), "%s/frame%04d.png", directory, frame);
        if (!ExportImage(strip, path))
            goto cleanup;
    }
    char path[4096];
    snprintf(path, sizeof(path), "%s/history.png", directory);
    history_export(&gpu.scene.water_history, path);
    printf("history_clip frames=%d water_mesh=%u masks=%llu\n", frames, gpu.scene.water.vaoId,
        gpu.scene.water_history.masks);
    result = 0;
cleanup:
    UnloadImage(strip);
    boat_gpu_destroy(&gpu);
    CloseWindow();
    return result;
}

// G4 playback: a boat crossing the outer harbour toward the sun at 6 m/s while the camera swings
// 35 degrees either side, all G4 layers left and the G3b water (no atlas, depth copy or far
// water) right. SQUALL adds a weather squall approaching from 900 m ahead.
static int place_clip(const char *directory, int frames, bool squall)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "Water place clip");
    if (!IsWindowReady())
        return 1;
    int result = 1;
    BoatGpu gpu = { 0 };
    const int width = 320, height = 200; // The default resolution.
    Image strip = GenImageColor(2 * width, height, BLACK);
    BoatSession session;
    boat_session_init(&session, test_world(BOAT_LOCATION_OUTER), boat_water_gentle());
    if (squall)
        session.world.water.storm = (BoatStorm) { .centre = { 24, -970 },
            .velocity = { 0, 9 },
            .radius = 450,
            .height = .5f,
            .started = 0,
            .build = 10 };
    const BoatCamera camera = { .distance = 22, .height = 9 };
    if (!boat_gpu_init(&gpu, clip_aa) || !boat_gpu_resize(&gpu, width, height))
        goto cleanup;
    gpu.daylight = review_daylight;
    gpu.hour = review_hour;
    gpu.hour_set = review_hour_set;
    for (int frame = 0; frame < frames; ++frame) {
        double time = frame / 60.0;
        BoatViewState view = { .boat = session.boat, .time = time };
        view.boat.position = (Vector2) { -24, -70 - 6 * (float)time };
        view.boat.heading = 0;
        session.presentation.camera_heading = .6f * sinf((float)time * .35f);
        for (int k = 0; k < 2; ++k) {
            waterfront_gpu_scene_set_water_atlas_enabled(&gpu.scene, k == 0);
            waterfront_gpu_scene_set_scene_depth_enabled(&gpu.scene, k == 0);
            waterfront_gpu_scene_set_far_water_enabled(&gpu.scene, k == 0);
            boat_gpu_draw(&gpu, &view, &session.presentation, &session.world, camera);
            Image image = LoadImageFromTexture(gpu.target.texture);
            ImageFlipVertical(&image);
            ImageFlipHorizontal(&image);
            ImageDraw(&strip, image, (Rectangle) { 0, 0, (float)width, (float)height },
                (Rectangle) { (float)(k * width), 0, (float)width, (float)height }, WHITE);
            UnloadImage(image);
        }
        char path[4096];
        snprintf(path, sizeof(path), "%s/frame%04d.png", directory, frame);
        if (!ExportImage(strip, path))
            goto cleanup;
    }
    printf("place_clip frames=%d squall=%d atlas=%u\n", frames, squall, gpu.scene.water_atlas.id);
    result = 0;
cleanup:
    UnloadImage(strip);
    boat_gpu_destroy(&gpu);
    boat_session_destroy(&session);
    CloseWindow();
    return result;
}

static int check_instancing(void)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "Instancing parity probe");
    if (!IsWindowReady())
        return 1;
    int result = 1;
    ParityTotals totals = { 0 };
    for (int aa = 0; aa < 2; ++aa) {
        BoatGpu gpu = { 0 };
        bool ok = boat_gpu_init(&gpu, aa != 0) && gpu.scene.object_batch;
        for (size_t p = 0; ok && p < sizeof(review_poses) / sizeof(*review_poses); ++p) {
            const ReviewPose *pose = &review_poses[p];
            BoatSession session;
            boat_session_init(&session, test_world(pose->location), boat_water_gentle());
            BoatPresentation shown = session.presentation;
            Boat boat = session.boat;
            boat.position = pose->position;
            boat.heading = pose->heading;
            for (unsigned frame = 0; ok && frame < pose->frames; ++frame) {
                shown.camera_heading = pose->camera_heading + frame * pose->step_heading;
                const BoatViewState view = { .boat = boat,
                    .time = (pose->tick + frame * pose->step_ticks) * (double)BOAT_STEP };
                const ParityScene scene = { &view, &shown, &session.world, NULL,
                    { .distance = pose->distance, .height = pose->height } };
                ok = parity_frame(&gpu, &scene, pose->name, frame, aa, &totals);
                boat.position = Vector2Add(boat.position, pose->step_position);
                boat.heading += pose->step_heading;
            }
            boat_session_destroy(&session);
        }
        StressFixture *stress = ok ? malloc(sizeof(*stress)) : NULL;
        ok = ok && stress && stress_init(stress, true)
            && stress_advance(stress, STRESS_PREROLL_TICKS);
        for (unsigned frame = 0; ok && frame < 6; ++frame) {
            BoatWorld world = stress_world(stress);
            BoatViewState view = stress_view(stress);
            BoatRivals rivals = stress_rivals(stress);
            const ParityScene scene = { &view, &stress->presentations[0], &world, &rivals,
                { .distance = 26, .height = 16 } };
            ok = parity_frame(&gpu, &scene, "stress", frame, aa, &totals)
                && stress_advance(stress, 30);
        }
        free(stress); // Like the other stress callers, the fleet lives until the tool exits.
        boat_gpu_destroy(&gpu);
        if (!ok)
            goto cleanup;
    }
    if (totals.instanced_submissions >= totals.scalar_submissions)
        goto cleanup;
    printf("instancing_parity=ok frames=%d scalar_submissions=%u instanced_submissions=%u "
           "instances=%zu\n",
        totals.frames, totals.scalar_submissions, totals.instanced_submissions, totals.instances);
    result = 0;
cleanup:
    test_worlds_close();
    CloseWindow();
    return result;
}

// G2a: the static map storage of each location, against the unindexed storage it replaced, which
// uploaded and also retained 36 bytes for each of three vertices per triangle.
static int report_geometry(void)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "Geometry report");
    BoatGpu gpu = { 0 };
    int result = !IsWindowReady() || !boat_gpu_init(&gpu, false);
    const BoatLocationId locations[] = { BOAT_LOCATION_BASIN, TEST_RIVER, BOAT_LOCATION_OUTER };
    for (size_t i = 0; !result && i < sizeof(locations) / sizeof(*locations); ++i) {
        const BoatWorldResources *resources = test_world(locations[i]);
        BoatSession session;
        boat_session_init(&session, resources, boat_water_gentle());
        boat_gpu_draw(&gpu, SESSION_VIEW(&session), &session.presentation, &session.world,
            BOAT_CAMERA_DEFAULT);
        result = gpu.failed;
        WaterfrontGpuGeometry geometry = waterfront_gpu_scene_geometry(&gpu.scene);
        size_t before = geometry.source_vertices * 36;
        size_t after = geometry.vertex_bytes + geometry.index_bytes;
        printf("geometry location=%s meshes=%d triangles=%zu vertices=%zu source_vertices=%zu "
               "vertex_bytes=%zu index_bytes=%zu far_triangles=%zu far_vertex_bytes=%zu "
               "far_index_bytes=%zu retained_cpu_bytes=%zu normal_error=%.5f "
               "occluded_vertices=%zu before_gpu_bytes=%zu "
               "before_cpu_bytes=%zu vertex_change=%.1f%% total_change=%.1f%%\n",
            resources->definition->name, geometry.meshes, geometry.triangles, geometry.vertices,
            geometry.source_vertices, geometry.vertex_bytes, geometry.index_bytes,
            geometry.far_triangles, geometry.far_vertex_bytes, geometry.far_index_bytes,
            geometry.retained_cpu_bytes, geometry.normal_error, geometry.occluded_vertices, before,
            before, before ? 100.0 * ((double)geometry.vertex_bytes / (double)before - 1) : 0,
            before ? 100.0 * ((double)after / (double)before - 1) : 0);
        const struct {
            const char *name;
            const WaterfrontGpuTiled *tiled;
        } classes[] = { { "city", &gpu.scene.city }, { "facades", &gpu.scene.facades },
            { "roofs", &gpu.scene.roofs }, { "quay", &gpu.scene.quay },
            { "corner", &gpu.scene.corner }, { "corner_far", &gpu.scene.corner_far },
            { "windows", &gpu.scene.windows }, { "windows_far", &gpu.scene.windows_far },
            { "corner_emissive", &gpu.scene.corner_emissive } };
        for (size_t c = 0; c < sizeof(classes) / sizeof(*classes); ++c) {
            size_t triangles = 0;
            for (int m = 0; m < classes[c].tiled->count; ++m)
                triangles += (size_t)classes[c].tiled->meshes[m].triangleCount;
            printf("geometry_class location=%s class=%s meshes=%d triangles=%zu\n",
                resources->definition->name, classes[c].name, classes[c].tiled->count, triangles);
        }
        boat_session_destroy(&session);
    }
    boat_gpu_destroy(&gpu);
    test_worlds_close();
    CloseWindow();
    return result;
}

static bool review_pose_clear(const BoatWorldResources *resources, Vector2 position)
{
    if (resources->definition->mapped)
        return boat_map_water(&resources->map, position, 2.5f);
    return fabsf(position.x) < BOAT_BASIN_HALF_X - 3 && fabsf(position.y) < BOAT_BASIN_HALF_Z - 3;
}

// G2a's predeclared capture bounds: every review image in A against its namesake in B.
static int compare_captures(const char *a, const char *b)
{
    FilePathList files = LoadDirectoryFilesEx(a, ".png", false);
    int failures = files.count ? 0 : 1, largest = 0;
    double worst_mean = 0;
    for (unsigned int i = 0; i < files.count; ++i) {
        const char *name = GetFileName(files.paths[i]);
        Image left = LoadImage(files.paths[i]);
        Image right = LoadImage(TextFormat("%s/%s", b, name));
        bool same_size
            = left.data && right.data && left.width == right.width && left.height == right.height;
        if (!same_size) {
            printf("compare %s missing or resized\n", name);
            ++failures;
        } else {
            ImageFormat(&left, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
            ImageFormat(&right, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
            const unsigned char *p = left.data, *q = right.data;
            const size_t channels = (size_t)left.width * (size_t)left.height * 3;
            size_t total = 0, changed = 0;
            int max = 0;
            for (size_t c = 0; c < channels; ++c) {
                int difference = abs((int)p[c] - (int)q[c]);
                total += (size_t)difference;
                changed += difference != 0;
                if (difference > max)
                    max = difference;
            }
            double mean = (double)total / (double)channels;
            printf("compare %s max=%d mean=%.4f changed=%.2f%%\n", name, max, mean,
                100.0 * (double)changed / (double)channels);
            if (max > 2 || mean > 0.25)
                ++failures;
            largest = max > largest ? max : largest;
            worst_mean = mean > worst_mean ? mean : worst_mean;
        }
        UnloadImage(left);
        UnloadImage(right);
    }
    printf("compare images=%u max=%d worst_mean=%.4f over_bound=%d\n", files.count, largest,
        worst_mean, failures);
    UnloadDirectoryFiles(files);
    return failures ? 1 : 0;
}

// Capture the G0 review manifest and named pose images. The manifest records exactly the
// location, world pose, camera, ticks, water and resolution used, plus the reproducible command.
static int review(const char *directory, bool use_gpu)
{
    char path[2048], manifest_path[2048];
    if (!DirectoryExists(directory) && MakeDirectory(directory) != 0)
        return bench_fail("cannot create the review directory (its parent must exist)");
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", directory);
    FILE *manifest = fopen(manifest_path, "w");
    if (!manifest)
        return bench_fail("cannot create the review manifest");
    BoatGpu gpu = { 0 };
    Renderer3D cpu = { 0 };
    BoatCpuView cpu_view = { 0 };
    int result = 1;
    if (use_gpu) {
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(320, 200, "Proteus / graphics review poses");
        bool aa = review_resolution == BOAT_RESOLUTION_LOW && !review_hdr_on();
        if (!IsWindowReady() || !boat_gpu_init(&gpu, aa) || !apply_scene_options(&gpu.scene)
            || !apply_review_preset(&gpu))
            goto cleanup;
    } else if (!prepare_tiles(&cpu_view.scenery, 0)) {
        goto cleanup;
    }
    gpu.daylight = cpu_view.daylight = review_daylight;
    gpu.hour = cpu_view.hour = review_hour;
    gpu.hour_set = cpu_view.hour_set = review_hour_set;
    fprintf(manifest,
        "{\n  \"schema_revision\":2,\"tool\":\"boat_water_review\",\"mode\":\"review\",\n"
        "  \"lighting_mode\":\"%s\",\"daylight\":%.6g,\"lighting_hour\":%.6g,\n"
        "  \"renderer\":\"%s\",\"resolution\":[%d,%d],\"gpu_aa\":%s,\"hdr\":%s,"
        "\"physics_revision\":%d,\n"
        "  \"clock\":\"explicit pose tick * BOAT_STEP; presentation only\",\n"
        "  \"check_command\":\"boat_water_review --check\",\n"
        "  \"capture_command\":\"boat_water_review --review DIR --renderer %s\",\n"
        "  \"poses\":[\n",
        review_hour_set ? "hour" : "daylight", (double)review_daylight, (double)review_hour,
        use_gpu ? "gpu" : "cpu", use_gpu ? gpu.target.texture.width : 320,
        use_gpu ? gpu.target.texture.height : 200,
        use_gpu && gpu.supersampled.id ? "true" : "false",
        boat_gpu_hdr_active(&gpu) ? "true" : "false", BOAT_PHYSICS_REVISION,
        use_gpu ? "gpu" : "cpu");
    for (size_t p = 0; p < sizeof(review_poses) / sizeof(*review_poses); ++p) {
        const ReviewPose *pose = &review_poses[p];
        const BoatWorldResources *resources = test_world(pose->location);
        BoatSession session;
        boat_session_init(&session, resources, boat_water_gentle());
        BoatPresentation shown = session.presentation;
        Boat inspection = session.boat;
        inspection.position = pose->position;
        inspection.heading = pose->heading;
        shown.camera_heading = pose->camera_heading;
        printf("review %s location=%s kind=%s frames=%u\n", pose->name, resources->definition->name,
            pose->kind, pose->frames);
        fprintf(manifest,
            "%s    {\"name\":\"%s\",\"kind\":\"%s\",\"location\":\"%s\","
            "\"position\":[%.6g,%.6g],\"heading\":%.9g,\"camera\":{"
            "\"distance\":%.6g,\"height\":%.6g,\"heading\":%.9g},"
            "\"tick\":%u,\"frames\":%u,\"step_ticks\":%u,\"step_position\":"
            "[%.6g,%.6g],\"step_heading\":%.9g,\"waves\":true,"
            "\"resolution\":[%d,%d],\"captures\":[",
            p ? ",\n" : "", pose->name, pose->kind, resources->definition->name,
            (double)pose->position.x, (double)pose->position.y, (double)pose->heading,
            (double)pose->distance, (double)pose->height, (double)pose->camera_heading, pose->tick,
            pose->frames, pose->step_ticks, (double)pose->step_position.x,
            (double)pose->step_position.y, (double)pose->step_heading,
            use_gpu ? gpu.target.texture.width : 320, use_gpu ? gpu.target.texture.height : 200);
        for (unsigned frame = 0; frame < pose->frames; ++frame) {
            if (!review_pose_clear(resources, inspection.position)) {
                fputs("\n  ],\n  \"error\":\"pose is not clear of land\"\n}\n", manifest);
                boat_session_destroy(&session);
                goto cleanup;
            }
            shown.camera_heading = pose->camera_heading + frame * pose->step_heading;
            const BoatCamera camera = { .distance = pose->distance, .height = pose->height };
            double time = (pose->tick + frame * pose->step_ticks) * (double)BOAT_STEP;
            snprintf(path, sizeof(path), "%s/%s-%u.png", directory, pose->name, frame);
            if (!capture(&gpu, &cpu, &cpu_view, &inspection, time, &shown, &session.world, camera,
                    use_gpu, path)) {
                fputs("\n  ],\n  \"error\":\"capture failed\"\n}\n", manifest);
                boat_session_destroy(&session);
                goto cleanup;
            }
            fprintf(manifest, "%s\"%s-%u.png\"", frame ? "," : "", pose->name, frame);
            inspection.position = Vector2Add(inspection.position, pose->step_position);
            inspection.heading += pose->step_heading;
        }
        fprintf(manifest, "]");
        if (use_gpu) {
            fputs(",\"lights\":", manifest);
            json_lights(manifest, &gpu.scene);
        }
        fprintf(manifest, "}");
        boat_session_destroy(&session);
    }
    fputs("\n  ]\n}\n", manifest);
    result = 0;
cleanup:
    if (fclose(manifest))
        result = 1;
    boat_cpu_view_destroy(&cpu_view);
    renderer3d_destroy(&cpu);
    if (use_gpu && IsWindowReady()) {
        boat_gpu_destroy(&gpu);
        CloseWindow();
    }
    test_worlds_close();
    if (result)
        remove(manifest_path);
    return result ? bench_fail("review capture failed") : 0;
}

int main(int argc, char **argv)
{
    test_scenery_load();
    const char *output = NULL, *bench = NULL, *aa_name = NULL, *pacing = NULL;
    const char *scenario = NULL, *review_dir = NULL;
    float tile_size = 0;
    bool no_cull = false;
    bool use_gpu = true, waves = false, check = false, sequence = false, visible = false;
    bool instancing = false, geometry = false;
    bool gpu_check = false, gpu_views = false, gpu_lifecycle = false, culling = false;
    bool presentation_check = false, light_clusters_check = false;
    bool detail = false, wake_stamp = false, water_history = false, scene_depth = false;
    const char *clip_dir = NULL, *place_dir = NULL;
    bool place_squall = false;
    int clip_frames = 0;
    int sequence_ticks = SAMPLE_TICKS;
    bool stress_state_only = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc)
            output = argv[++i];
        else if (strcmp(argv[i], "--benchmark") == 0 && i + 1 < argc)
            bench = argv[++i];
        else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc)
            scenario = argv[++i];
        else if (strcmp(argv[i], "--stress-state") == 0)
            stress_state_only = true;
        else if (strcmp(argv[i], "--review") == 0 && i + 1 < argc)
            review_dir = argv[++i];
        else if (strcmp(argv[i], "--aa") == 0 && i + 1 < argc)
            aa_name = argv[++i];
        else if (strcmp(argv[i], "--waves") == 0)
            waves = true;
        else if (strcmp(argv[i], "--check-gpu") == 0)
            gpu_check = true;
        else if (strcmp(argv[i], "--check-gpu-views") == 0)
            gpu_views = true;
        else if (strcmp(argv[i], "--check-gpu-lifecycle") == 0)
            gpu_lifecycle = true;
        else if (strcmp(argv[i], "--check-culling") == 0)
            culling = true;
        else if (strcmp(argv[i], "--check-presentation") == 0)
            presentation_check = true;
        else if (strcmp(argv[i], "--check-light-clusters") == 0)
            light_clusters_check = true;
        else if (strcmp(argv[i], "--check-instancing") == 0)
            instancing = true;
        else if (strcmp(argv[i], "--geometry") == 0)
            geometry = true;
        else if (strcmp(argv[i], "--compare") == 0 && i + 2 < argc)
            return compare_captures(argv[i + 1], argv[i + 2]);
        else if (strcmp(argv[i], "--check") == 0)
            check = true;
        else if (strcmp(argv[i], "--sequence") == 0)
            sequence = true;
        else if (strcmp(argv[i], "--sequence-ticks") == 0 && i + 1 < argc)
            sequence_ticks = atoi(argv[++i]);
        else if (strcmp(argv[i], "--visible") == 0)
            visible = true;
        else if (strcmp(argv[i], "--pacing") == 0 && i + 1 < argc)
            pacing = argv[++i];
        else if (strcmp(argv[i], "--no-detail") == 0)
            no_detail = true;
        else if (strcmp(argv[i], "--analytic-wakes") == 0)
            analytic_wakes = true;
        else if (strcmp(argv[i], "--stamp-size") == 0 && i + 1 < argc)
            stamp_size = atoi(argv[++i]);
        else if (strcmp(argv[i], "--stamp-spacing") == 0 && i + 1 < argc)
            stamp_spacing = (float)atof(argv[++i]);
        else if (strcmp(argv[i], "--check-wake-stamp") == 0)
            wake_stamp = true;
        else if (strcmp(argv[i], "--check-water-history") == 0)
            water_history = true;
        else if (strcmp(argv[i], "--no-water-history") == 0)
            no_water_history = true;
        else if (strcmp(argv[i], "--no-scene-depth") == 0)
            no_scene_depth = true;
        else if (strcmp(argv[i], "--no-water-atlas") == 0)
            no_water_atlas = true;
        else if (strcmp(argv[i], "--no-far-water") == 0)
            no_far_water = true;
        else if (strcmp(argv[i], "--light-fallback") == 0)
            light_fallback = true;
        else if (strcmp(argv[i], "--light-reference") == 0)
            light_reference = true;
        else if (strcmp(argv[i], "--no-ambient-occlusion") == 0)
            no_ambient_occlusion = true;
        else if (strcmp(argv[i], "--no-particles") == 0)
            no_particles = true;
        else if (strcmp(argv[i], "--no-shadows") == 0)
            no_shadows = true;
        else if (strcmp(argv[i], "--no-reflections") == 0)
            no_reflections = true;
        else if (strcmp(argv[i], "--stress-lights") == 0 && i + 1 < argc) {
            stress_lights = atoi(argv[++i]);
            if (stress_lights < 0 || stress_lights > WATERFRONT_LIGHTS_MAX) {
                fprintf(stderr, "--stress-lights takes 0..%d.\n", WATERFRONT_LIGHTS_MAX);
                return 2;
            }
        } else if (strcmp(argv[i], "--clip-aa") == 0 && i + 1 < argc) {
            const char *name = argv[++i];
            if (strcmp(name, "off") && strcmp(name, "4x4")) {
                fprintf(stderr, "--clip-aa takes off or 4x4.\n");
                return 2;
            }
            clip_aa = strcmp(name, "4x4") == 0;
        } else if (strcmp(argv[i], "--resolution") == 0 && i + 1 < argc) {
            if (!boat_resolution_parse(argv[++i], &review_resolution)) {
                fprintf(stderr, "--resolution takes low or high.\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--hdr") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(value, "on") && strcmp(value, "off")) {
                fprintf(stderr, "--hdr takes on or off.\n");
                return 2;
            }
            review_hdr = strcmp(value, "on") == 0;
        } else if (strcmp(argv[i], "--daylight") == 0 && i + 1 < argc) {
            if (!waterfront_daylight_parse(argv[++i], &review_daylight)) {
                fprintf(stderr, "--daylight takes day, sunset, dusk or 0..1.\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--lighting-hour") == 0 && i + 1 < argc) {
            if (!waterfront_hour_parse(argv[++i], &review_hour)) {
                fprintf(stderr, "--lighting-hour takes a finite hour (wrapped into 0..24).\n");
                return 2;
            }
            review_hour_set = true;
        } else if (strcmp(argv[i], "--check-scene-depth") == 0)
            scene_depth = true;
        else if (strcmp(argv[i], "--history-clip") == 0 && i + 2 < argc) {
            clip_dir = argv[++i];
            clip_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--place-clip") == 0 && i + 3 < argc) {
            place_dir = argv[++i];
            clip_frames = atoi(argv[++i]);
            place_squall = strcmp(argv[++i], "squall") == 0;
        } else if (strcmp(argv[i], "--check-detail") == 0)
            detail = true;
        else if (strcmp(argv[i], "--no-cull") == 0)
            no_cull = true;
        else if (strcmp(argv[i], "--tile-size") == 0 && i + 1 < argc) {
            char *end = NULL;
            tile_size = strtof(argv[++i], &end);
            if (!*argv[i] || *end || !(tile_size >= 64 && tile_size <= 65536))
                return 2;
        } else if (strcmp(argv[i], "--renderer") == 0 && i + 1 < argc) {
            const char *name = argv[++i];
            if (strcmp(name, "cpu") != 0 && strcmp(name, "gpu") != 0)
                return 2;
            use_gpu = strcmp(name, "gpu") == 0;
        } else {
            puts("Usage: boat_water_review --check [--waves] | --check-gpu | --check-gpu-views\n"
                 "       boat_water_review --check-gpu-lifecycle | --check-culling\n"
                 "       boat_water_review --check-presentation | --check-light-clusters\n"
                 "       boat_water_review --check-instancing | --geometry | --compare A B\n"
                 "       boat_water_review --stress-state\n"
                 "       boat_water_review --output DIR [--renderer cpu|gpu] [--waves]\n"
                 "                         [--sequence] [--sequence-ticks N] [--visible]\n"
                 "                         [--tile-size M] [--no-cull] [--no-detail]\n"
                 "       Any GPU run: [--analytic-wakes] [--stamp-size N] [--stamp-spacing M]\n"
                 "                    [--no-water-history] [--no-scene-depth] [--no-water-atlas]\n"
                 "                    [--no-far-water] [--light-fallback] [--light-reference]\n"
                 "                    [--stress-lights N] [--no-ambient-occlusion]\n"
                 "                    [--no-particles] [--no-shadows] [--no-reflections]\n"
                 "                    [--daylight ...] [--lighting-hour H]\n"
                 "       boat_water_review --check-wake-stamp | --check-water-history\n"
                 "                         | --check-scene-depth\n"
                 "       boat_water_review --history-clip DIR FRAMES\n"
                 "       boat_water_review --place-clip DIR FRAMES calm|squall\n"
                 "                         [--clip-aa off|4x4] [--daylight ...]\n"
                 "       boat_water_review --review DIR [--renderer cpu|gpu]\n"
                 "                         [--daylight day|sunset|dusk|0..1]\n"
                 "                         [--resolution low|high] [--hdr on|off]\n"
                 "       boat_water_review --benchmark PREFIX [--renderer gpu] --aa off|4x4\n"
                 "                         [--resolution low|high] [--hdr on|off] (with --aa off)\n"
                 "                         [--daylight day|sunset|dusk|0..1]\n"
                 "                         [--scenario river|outer|stress] [--waves]\n"
                 "                         [--visible [--pacing cap|deadline|vsync]]\n"
                 "                         [--tile-size M]\n");
            puts("--tile-size: render tile edge, 64..65536 m (default 2048).\n"
                 "--analytic-wakes shades object wakes with the reference per-piece loop; by\n"
                 "  default they sample the stamped field (1024 texels of 0.25 m).\n"
                 "--no-water-history shades stamp-only water, without foam history and ripples.\n"
                 "--no-scene-depth shades water without the copy of opaque depth.\n"
                 "--no-water-atlas shades water from the hand-written dock exposure, not the\n"
                 "  map's baked atlas.\n"
                 "--no-far-water draws water without gusts, sun glitter or squall water.\n"
                 "--light-fallback draws the capped uniform light list instead of the clustered\n"
                 "  lights; --light-reference shades every clustered light at every pixel;\n"
                 "  --stress-lights adds N (0..1024) review lights on a 12 m lattice.\n"
                 "--no-ambient-occlusion draws the map without its baked ambient occlusion.\n"
                 "--no-particles draws no boat foam or spray cubes (to measure their cost).\n"
                 "--no-shadows and --no-reflections skip that pass (to measure its cost).\n"
                 "--history-clip writes FRAMES (1..3600) 60 Hz stress frames to DIR, history\n"
                 "  left and stamp-only right, plus the final history field as history.png.\n"
                 "--place-clip writes FRAMES (1..3600) 60 Hz outer-harbour frames to DIR, all\n"
                 "  G4 water left and G3b water right; squall brings a weather squall in.\n"
                 "  --clip-aa off draws it without supersampling; --daylight sets the light.\n"
                 "--lighting-hour H selects a developer full-day hour and overrides --daylight\n"
                 "  regardless of argument order; pose ticks and clip frames still drive waves.\n"
                 "--scenario river: the existing river route; outer: the same route on the outer\n"
                 "  map, continued across the estuary; stress: four basin boats with\n"
                 "  saturated boat and traffic wake pools (deterministic pre-roll).\n"
                 "--stress-state prints the stress fixture's final physical state without a\n"
                 "  window; --benchmark --scenario stress must report the same state.\n"
                 "Visible benchmark pacing: cap = SetTargetFPS(60) (default), deadline = 60 Hz\n"
                 "deadline chain, vsync = the game's vsync with a backstop above the refresh.\n"
                 "Benchmark writes PREFIX.json and PREFIX.csv: two ticks per frame, 120 warmup.\n"
                 "GPU uses existing 4x AA. GPU sequence: 10 Hz; CPU samples: five seconds.\n"
                 "Visible GPU playback uses 60 Hz; simulation remains 120 Hz.\n"
                 "Named captures use both default and close follow cameras.\n"
                 "--review writes manifest.json and the named pose captures.\n"
                 "--compare checks each PNG in A against B: channel difference at most 2/255\n"
                 "  and mean difference at most 0.25/255.\n"
                 "Map data (c) OpenStreetMap contributors, ODbL 1.0.");
            return strcmp(argv[i], "--help") == 0 ? 0 : 2;
        }
    }
    if (stress_state_only)
        return argc > 2 ? 2 : stress_state();
    // --check drives the river or outer route display-free; captures follow only the river.
    bool route_check = check && !bench && !aa_name && !pacing && scenario
        && (strcmp(scenario, "river") == 0 || strcmp(scenario, "outer") == 0);
    if (bench || aa_name || pacing || (scenario && !route_check)) {
        const char *problem = !bench
            ? "--aa, --pacing and --scenario require --benchmark (--check: river or outer)"
            : !aa_name                                         ? "--benchmark requires --aa off|4x4"
            : strcmp(aa_name, "off") && strcmp(aa_name, "4x4") ? "--aa must be off or 4x4"
            : scenario && strcmp(scenario, "river") && strcmp(scenario, "outer")
                && strcmp(scenario, "stress")
            ? "--scenario must be river, outer or stress"
            : check || gpu_check || gpu_views || gpu_lifecycle || output || sequence || review_dir
            ? "--benchmark excludes --check, --check-gpu*, --output, --review and --sequence"
            : !use_gpu ? "--benchmark supports only --renderer gpu"
            : !*bench || strlen(bench) > 4000 || bench[strlen(bench) - 1] == '/'
            ? "--benchmark needs a file prefix of 1..4000 bytes"
            : review_hdr_on() && strcmp(aa_name, "off")
            ? "--resolution high and --hdr need --aa off"
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
    if (review_dir
        && (check || output || sequence || visible || aa_name || pacing || scenario || gpu_check
            || gpu_views || gpu_lifecycle || culling || instancing))
        return 2;
    if ((review_resolution != BOAT_RESOLUTION_LOW || review_hdr >= 0)
        && (!use_gpu || !(review_dir || bench))) {
        fprintf(stderr, "--resolution and --hdr apply to GPU --review and --benchmark runs.\n");
        return 2;
    }
    if (gpu_check || gpu_views || gpu_lifecycle || culling || detail || instancing || geometry
        || presentation_check || light_clusters_check || wake_stamp || water_history || scene_depth
        || clip_dir || place_dir) {
        SetTraceLogLevel(LOG_WARNING);
        if (place_dir)
            return clip_frames > 0 && clip_frames <= 3600
                ? place_clip(place_dir, clip_frames, place_squall)
                : 2;
        if (clip_dir)
            return clip_frames > 0 && clip_frames <= 3600 ? history_clip(clip_dir, clip_frames) : 2;
        return presentation_check  ? check_presentation()
            : light_clusters_check ? check_light_clusters()
            : scene_depth          ? check_scene_depth()
            : water_history        ? check_water_history()
            : wake_stamp           ? check_wake_stamp()
            : geometry             ? report_geometry()
            : instancing           ? check_instancing()
            : culling              ? check_culling()
            : detail               ? check_detail()
            : gpu_lifecycle        ? check_gpu_lifecycle()
            : gpu_views            ? check_gpu_views()
                                   : check_gpu_water();
    }
    if (!check && !output && !bench && !review_dir)
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
        return benchmark(bench, waves, strcmp(aa_name, "4x4") == 0, visible,
            pacing ? pacing : "cap", tile_size,
            !scenario || strcmp(scenario, "river") == 0 ? &river_route
                : strcmp(scenario, "outer") == 0        ? &outer_route
                                                        : NULL);
    if (review_dir)
        return review(review_dir, use_gpu);
    const Route *route = scenario && strcmp(scenario, "outer") == 0 ? &outer_route : &river_route;
    const BoatWorldDefinition *location = test_location(route->location);
    BoatSession session;
    boat_session_init(
        &session, test_world(route->location), waves ? boat_water_gentle() : (BoatWater) { 0 });
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
    BoatCpuView cpu_view = { 0 };
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
            if (!IsWindowReady() || !boat_gpu_init(&gpu, true)
                || !prepare_tiles(&gpu.scenery, tile_size))
                goto cleanup;
            waterfront_gpu_scene_set_culling_enabled(&gpu.scene, !no_cull);
            if (!apply_scene_options(&gpu.scene))
                goto cleanup;
            if (visible)
                SetTargetFPS(60);
        }
    }
    if (!use_gpu && !prepare_tiles(&cpu_view.scenery, tile_size))
        goto cleanup;
    cpu.culling_disabled = no_cull;
    size_t destination = 0, captured_destination = 0;
    const BoatCamera default_camera
        = { .distance = location->camera_distance, .height = location->camera_height };
    const BoatCamera review_camera = default_camera;
    printf("sequence_camera=%.1f/%.1f\n", review_camera.distance, review_camera.height);
    int frames = 0, ticks = 0, timing_count = 0;
    double timings[MAX_TICKS / 2 + 1], total = 0;
    // Sampled-frame work: GPU scene submissions and triangles, or CPU meshes, submitted and
    // rasterized triangles.
    size_t work_total[3] = { 0 }, work_max[3] = { 0 };
    for (ticks = 0; ticks <= MAX_TICKS; ++ticks) {
        if (!hull_clear(boat, route->location)) {
            fprintf(stderr, "Route too close to shore at tick %d: %.3f %.3f\n", ticks,
                boat->position.x, boat->position.y);
            goto cleanup;
        }
        bool arrival = destination != captured_destination;
        bool finished = route_finished(boat, route, destination);
        const char *name = ticks == 0 ? "dock"
            : finished                ? route->stops[route->count - 1].name
            : arrival                 ? route->stops[destination - 1].name
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
                    if (!capture(&gpu, &cpu, &cpu_view, boat, boat_session_time(&session),
                            presentation, world, cameras[view], use_gpu, path))
                        goto cleanup;
                }
                if (finished) {
                    shown = *presentation;
                    shown.camera_heading += PI;
                    snprintf(path, sizeof(path), "%s/beyond-lookback.png", output);
                    if (!capture(&gpu, &cpu, &cpu_view, boat, boat_session_time(&session), &shown,
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
                        if (!hull_clear(&inspection, TEST_RIVER))
                            goto cleanup;
                        snprintf(path, sizeof(path), "%s/%s.png", output, landfalls[bank].name);
                        if (!capture(&gpu, &cpu, &cpu_view, &inspection,
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
                    if (!capture(&gpu, &cpu, &cpu_view, &overview, boat_session_time(&session),
                            &shown, world, (BoatCamera) { .distance = 1100, .height = 300 },
                            use_gpu, path))
                        goto cleanup;
                }
            }
        }
        // CPU wave tessellation is deliberately expensive: sample five-second intervals
        // plus every named stop. GPU captures the continuous ten-Hz review sequence.
        int sample_ticks = use_gpu ? (visible ? 2 : sequence_ticks) : 600;
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
                if (!boat_draw(&cpu, &cpu_view, pixels, 320, 200, SESSION_VIEW(&session),
                        presentation, world, review_camera))
                    goto cleanup;
            }
            double ms = (now() - start) * 1000;
            WaterfrontGpuCount scene_work = waterfront_gpu_stats_total(&gpu.scene.stats);
            const size_t work[3] = { use_gpu ? scene_work.submissions : cpu.stats.meshes,
                use_gpu ? scene_work.triangles : cpu.stats.triangles,
                use_gpu ? 0 : cpu.stats.rasterized };
            for (int i = 0; i < 3; ++i) {
                work_total[i] += work[i];
                work_max[i] = work[i] > work_max[i] ? work[i] : work_max[i];
            }
            int warmup = use_gpu ? 120 : 2;
            if (frames >= warmup) {
                timings[timing_count++] = ms;
                total += ms;
            }
            if (sequence) {
                snprintf(path, sizeof(path), "%s/frame-%05d.png", output, frames);
                if (!capture(&gpu, &cpu, &cpu_view, boat, boat_session_time(&session), presentation,
                        world, review_camera, use_gpu, path))
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
        drive(&session, route, &destination);
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
        if (use_gpu)
            printf("scene_work frames=%d submissions_mean=%.1f submissions_max=%zu "
                   "triangles_mean=%.0f triangles_max=%zu\n",
                frames, (double)work_total[0] / frames, work_max[0], (double)work_total[1] / frames,
                work_max[1]);
        else
            printf("scene_work frames=%d meshes_mean=%.1f meshes_max=%zu triangles_mean=%.0f "
                   "triangles_max=%zu rasterized_mean=%.0f rasterized_max=%zu\n",
                frames, (double)work_total[0] / frames, work_max[0], (double)work_total[1] / frames,
                work_max[1], (double)work_total[2] / frames, work_max[2]);
        const Mesh3D *surface = use_gpu ? NULL
                                        : boat_water_surface(&cpu_view.water_mesh,
                                              world->definition->map, (BoatWater) { 0 }, 0);
        const Mesh3D *city = world->definition->map->mesh;
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
    boat_cpu_view_destroy(&cpu_view);
    renderer3d_destroy(&cpu);
    if (!check && use_gpu && IsWindowReady()) {
        boat_gpu_destroy(&gpu);
        CloseWindow();
    }
    test_worlds_close();
    return result;
}
