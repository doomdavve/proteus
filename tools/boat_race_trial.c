// R0 fixture: a shortened river route and one independently owned physical ferry wake.
#include "boat_gpu.h"
#include "boat_map.h"
#include "boat_plugin_catalog.h"
#include "boat_race.h"
#include "boat_race_plugin.h"
#include "boat_reset_hold.h"
#include "boat_test_scenery.h"
#include "boat_test_world.h"
#include "boat_throttle.h"
#include "raymath.h"
#include "wake_velocity_drive.h"
#include "waterfront_traffic.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
void boat_vessel_wake_gpu_check(BoatVesselWake wake);
void boat_water_storm_gpu_check(BoatStorm storm);
void boat_water_radial_gpu_check(void);
bool boat_vessel_wake_shadow_read(Texture2D texture, float *pixels);

static RacePlugin plugin;
static const char *plugin_path;
static bool plugin_enabled;

static const BoatRaceCourse *bundled_course(void)
{
    static BoatPluginRevision revision;
    static bool ready;
    if (!ready) {
        const BoatPluginCatalogEntry *entry = boat_plugin_catalog_find("proteus.river-sprint");
        BoatPluginDiagnostic diagnostic = { 0 };
        CHECK(entry && boat_plugin_catalog_decode(entry, &revision, &diagnostic));
        ready = true;
    }
    return &revision.race.course;
}

static const BoatRaceCourse *preview_course(void)
{
    return plugin_enabled ? &boat_plugin_host_active(&plugin.host)->race.course : bundled_course();
}

static const Vector2 route[]
    = { { -180, -250 }, { -40, -360 }, { 220, -510 }, { 454, -678 }, { 600, -800 } };
#define ROUTE_COUNT (sizeof(route) / sizeof(*route))

static BoatVesselWake ferry(void)
{
    Vector2 direction = Vector2Normalize((Vector2) { 110, 140 });
    return (BoatVesselWake) { .origin
        = Vector2Subtract((Vector2) { -30, -385 }, Vector2Scale(direction, 144)),
        .direction = direction,
        .started = -12,
        .speed = 12,
        .height = 3,
        .duration = 24,
        .lifetime = 40,
        .wavelength = 8,
        .spread = 3,
        .half_width = 14 };
}

static float heading_to(Vector2 a, Vector2 b) { return atan2f(b.x - a.x, a.y - b.y); }

static void start(BoatSession *session, ObjectStudy *objects, bool source, double delay)
{
    BoatWater water = { 0 };
    if (source)
        water.vessel = ferry();
    boat_session_init(session, test_world(BOAT_LOCATION_RIVER), water);
    session->boat.position = route[0];
    session->boat.heading = heading_to(route[0], route[1]);
    boat_session_set_tick(session, (uint64_t)llround(delay * BOAT_TICK_HZ));
    object_study_reset(objects, session, false, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT);
    objects->wakes.tick = session->tick;
    objects->traffic_enabled = source;
    if (source) {
        CHECK(waterfront_traffic_wakes_init(&objects->traffic, boat_session_time(session)));
        session->world.water.traffic_objects = &objects->traffic;
    }
    boat_presentation_reset(&session->presentation);
    session->presentation.camera_heading = session->boat.heading;
}

static BoatInput pilot(const Boat *boat, Vector2 target, float thrust)
{
    float error = remainderf(heading_to(boat->position, target) - boat->heading, 2 * PI);
    float wheel = Clamp(1.5f * error - .8f * boat->yaw_velocity, -.6f, .6f);
    return (BoatInput) { .steering_rate = Clamp((wheel - boat->wheel) / BOAT_STEP, -1, 1),
        .throttle_rate = boat_throttle_rate(boat->throttle, thrust) };
}

static void advance_target(const Boat *boat, unsigned *next)
{
    if (*next >= ROUTE_COUNT)
        return;
    Vector2 delta = Vector2Subtract(route[*next], route[*next - 1]);
    // A forward half-plane finish supports a swept crossing without a proximity slowdown.
    if (Vector2DotProduct(Vector2Subtract(boat->position, route[*next]), delta) >= 0)
        ++*next;
}

// Count actual positive local maxima across one side of a fully developed packet.
// This catches extra small crests as well as a reversed centre/outer height taper.
static void crest_profile(BoatVesselWake w)
{
    Vector2 side = { -w.direction.y, w.direction.x };
    float emitted = w.duration * .5f, age = w.lifetime * .5f;
    double time = w.started + emitted + age;
    float peaks[4] = { 0 }, previous = 0, before = 0;
    unsigned count = 0;
    for (int i = 0; i <= 4000; ++i) {
        float q = -w.half_width - .1f + (2 * w.half_width + .2f) * i / 4000;
        float radius = w.spread * age + q;
        CHECK(radius > 2);
        float across = sqrtf(radius * radius - 4);
        Vector2 p = Vector2Add(w.origin,
            Vector2Add(Vector2Scale(w.direction, w.speed * emitted), Vector2Scale(side, across)));
        float height = boat_vessel_wake_query(w, p, time).height;
        if (previous > 0 && previous > before && previous >= height) {
            CHECK(count < 4);
            peaks[count++] = previous;
        }
        before = previous;
        previous = height;
    }
    CHECK(count == 3);
    CHECK(peaks[1] > peaks[0] * 2 && peaks[1] > peaks[2] * 2);
    CHECK(peaks[0] > peaks[1] * .15f && peaks[2] > peaks[1] * .15f);
    CHECK(fabsf(peaks[0] - peaks[2]) < .001f);
    printf("ferry_crest_profile count=%u heights=%.3f,%.3f,%.3f\n", count, peaks[0], peaks[1],
        peaks[2]);
}

static void contracts(void)
{
    BoatVesselWake w = ferry();
    CHECK(boat_vessel_wake_valid(w));
    crest_profile(w);
    crest_profile(bundled_course()->source);
    CHECK(!boat_vessel_wake_valid((BoatVesselWake) { 0 }));
    BoatVesselWake invalid = w;
    invalid.direction.x = NAN;
    CHECK(!boat_vessel_wake_valid(invalid));
    // Finite differences verify both spatial derivatives and surface velocity. Include
    // smooth support boundaries, a rotated track and a month-long world-clock translation.
    unsigned nonzero = 0;
    for (int ti = -13; ti <= 53; ti += 2)
        for (int x = -270; x < -30; x += 13)
            for (int z = -520; z < -180; z += 17) {
                Vector2 p = { x + .17f, z + .31f };
                BoatVesselWakeSample q = boat_vessel_wake_query(w, p, ti);
                float e = .02f;
                float dx = (boat_vessel_wake_query(w, (Vector2) { p.x + e, p.y }, ti).height
                               - boat_vessel_wake_query(w, (Vector2) { p.x - e, p.y }, ti).height)
                    / (2 * e);
                float dz = (boat_vessel_wake_query(w, (Vector2) { p.x, p.y + e }, ti).height
                               - boat_vessel_wake_query(w, (Vector2) { p.x, p.y - e }, ti).height)
                    / (2 * e);
                float dt = (boat_vessel_wake_query(w, p, ti + .001).height
                               - boat_vessel_wake_query(w, p, ti - .001).height)
                    / .002f;
                CHECK(isfinite(q.height) && fabsf(q.height) <= w.height);
                CHECK(fabsf(dx - q.gradient.x) < .004f);
                CHECK(fabsf(dz - q.gradient.y) < .004f);
                CHECK(fabsf(dt - q.vertical_velocity) < .004f);
                BoatVesselWake shifted = w;
                shifted.started += 2592000;
                BoatVesselWakeSample later = boat_vessel_wake_query(shifted, p, ti + 2592000.0);
                CHECK(q.height == later.height && q.vertical_velocity == later.vertical_velocity);
                if (fabsf(q.height) > .1f)
                    ++nonzero;
            }
    CHECK(nonzero > 100);
    // Restore the real fleet across the encounter, including all four ordinary player wakes.
    BoatFleet a, b;
    CHECK(boat_fleet_init(&a, test_world(BOAT_LOCATION_RIVER), (BoatWater) { .vessel = w },
        BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT));
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        Boat boat;
        boat_reset(&boat);
        boat.position = Vector2Add(route[0], (Vector2) { 0, (float)i * 12 });
        boat.heading = heading_to(route[0], route[1]);
        boat_fleet_place(&a, i, boat);
    }
    BoatFleetState checkpoint;
    for (int t = 0; t < 900; ++t) {
        BoatInput inputs[BOAT_FLEET_SLOTS];
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            inputs[i] = pilot(&a.state.slots[i].boat, route[1], .8f);
        CHECK(boat_fleet_step(&a, inputs) && !a.stats.emission_failures);
    }
    boat_fleet_save(&a, &checkpoint);
    CHECK(boat_fleet_init(
        &b, a.world.resources, a.world.water, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT));
    boat_fleet_restore(&b, &checkpoint);
    for (int t = 0; t < 900; ++t) {
        BoatInput inputs[BOAT_FLEET_SLOTS];
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            inputs[i] = pilot(&a.state.slots[i].boat, route[1], .8f);
        CHECK(boat_fleet_step(&a, inputs) && boat_fleet_step(&b, inputs));
        CHECK(!a.stats.emission_failures && !b.stats.emission_failures);
        CHECK(boat_fleet_hash(&a.state) == boat_fleet_hash(&b.state));
    }
    CHECK(a.state.wakes.emitted > 0);
    // Source hull contact is physical, including vertical separation.
    Vector2 ship;
    CHECK(boat_vessel_wake_position(w, 0, &ship));
    WaterfrontTrafficContact contact;
    CHECK(waterfront_traffic_contact_height(
        ship, 0, 2, 2, 1, -1, 1, (BoatWater) { .vessel = w }, 0, &contact));
    CHECK(contact.vessel == WATERFRONT_TRAFFIC_TRIAL);
    CHECK(!waterfront_traffic_contact_height(
        ship, 0, 2, 2, 1, 30, 32, (BoatWater) { .vessel = w }, 0, &contact));
}

static void traffic_contracts(void)
{
    BoatWakeObjects traffic, restored;
    CHECK(waterfront_traffic_wakes_init(&traffic, 0));
    restored = traffic;
    for (int t = 1; t <= 120; ++t) {
        CHECK(waterfront_traffic_wakes_step(&traffic, t / 120.0));
        CHECK(waterfront_traffic_wakes_step(&restored, t / 120.0));
        CHECK(memcmp(&traffic, &restored, sizeof(traffic)) == 0);
    }
    for (unsigned i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
        float peak = 0;
        for (unsigned j = i * BOAT_OBJECT_PER_BOAT; j < (i + 1) * BOAT_OBJECT_PER_BOAT; ++j) {
            const BoatWakeObject *o = &traffic.objects[j];
            if (!o->amplitude)
                continue;
            float age = (traffic.tick - o->born) / 120.0f;
            Vector2 normal = { -o->direction.y, o->direction.x };
            Vector2 p = Vector2Add(o->position, Vector2Scale(normal, .9f + 2.4f * age));
            BoatWakeObjectSample q = boat_wake_objects_query_emitter(&traffic, i, p);
            peak = fmaxf(peak, fabsf(q.height));
            BoatWater water = { .traffic_objects = &traffic };
            BoatWaterSample sample = boat_water_query(water, p, 1);
            BoatWakeObjectSample all = boat_wake_objects_query(&traffic, p);
            CHECK(fabsf(sample.height - all.height) < .000001f);
            CHECK(fabsf(sample.velocity.y - all.vertical_velocity) < .000001f);
        }
        printf("scenery_emitter=%u peak_sampled_height=%.4f\n", i, peak);
        CHECK(peak > .01f); // Includes the 2.2 m/s passing runabout, below player wake threshold.
    }
    CHECK(traffic.live > 0 && traffic.live <= BOAT_OBJECT_CAPACITY);
    WaterfrontTrafficFrame ordinary, trial;
    waterfront_traffic_evaluate((BoatWater) { 0 }, 1, &ordinary);
    waterfront_traffic_evaluate(
        (BoatWater) { .traffic_objects = &traffic, .vessel = ferry() }, 1, &trial);
    CHECK(ordinary.wake_count == WATERFRONT_TRAFFIC_WAKES);
    CHECK(trial.wake_count > ordinary.wake_count
        && trial.wake_count <= WATERFRONT_TRAFFIC_WAKE_CAPACITY);
    CHECK(trial.wakes[0].pose.scale.x > ordinary.wakes[0].pose.scale.x);
}

// Keep the physical water identical and remove only the ferry's render pieces. An image
// difference in all three passes guards the mapped-scenery bug, which recipe parity missed.
static void ferry_visibility(void)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "Mapped ferry visibility regression");
    CHECK(IsWindowReady());
    BoatSession session;
    ObjectStudy objects;
    start(&session, &objects, true, 0);
    Vector2 ship;
    CHECK(boat_vessel_wake_position(ferry(), 0, &ship));
    session.boat.position = Vector2Add(ship, (Vector2) { -35, 40 });
    session.boat.heading = heading_to(session.boat.position, ship);
    session.presentation.camera_heading = session.boat.heading;
    BoatViewState view = boat_session_view(&session);
    BoatCamera camera = { .distance = 24, .height = 10 };
    BoatGpu gpu = { 0 };
    CHECK(boat_gpu_init(&gpu, false));
    boat_gpu_draw(&gpu, &view, &session.presentation, &session.world, camera);
    CHECK(!gpu.failed);
    WaterfrontSceneFrame frame;
    boat_scene_frame(&view, &session.presentation, &session.world, camera, &frame);
    CHECK(!frame.boating_basin && frame.scenery_count > 0);
    gpu.scene.near_water = gpu.wave_mesh;
    Image present[2];
    Texture2D depth = waterfront_gpu_scene_shadow_map(&gpu.scene);
    size_t shadow_count = (size_t)depth.width * depth.height;
    float *shadows = malloc(2 * shadow_count * sizeof(float));
    CHECK(depth.id && shadows);
    for (int pass = 0; pass < 2; ++pass) {
        if (pass)
            frame.scenery_count = 0;
        BeginTextureMode(gpu.target);
        waterfront_gpu_scene_draw(&gpu.scene, &frame, 320, 200);
        EndTextureMode();
        CHECK(boat_vessel_wake_shadow_read(depth, shadows + pass * shadow_count));
        Texture2D textures[] = { gpu.target.texture, gpu.scene.reflection.texture };
        for (unsigned i = 0; i < 2; ++i) {
            Image image = LoadImageFromTexture(textures[i]);
            CHECK(image.data);
            if (!pass)
                present[i] = image;
            else {
                CHECK(image.width == present[i].width && image.height == present[i].height);
                Color *with = LoadImageColors(present[i]), *without = LoadImageColors(image);
                CHECK(with && without);
                unsigned changed = 0;
                for (int p = 0; p < image.width * image.height; ++p)
                    if (memcmp(&with[p], &without[p], sizeof(Color)))
                        ++changed;
                printf("ferry_visibility_pass=%u changed_pixels=%u\n", i, changed);
                CHECK(changed > 5);
                UnloadImageColors(with);
                UnloadImageColors(without);
                UnloadImage(present[i]);
                UnloadImage(image);
            }
        }
    }
    unsigned shadow_changed = 0;
    for (size_t i = 0; i < shadow_count; ++i)
        if (fabsf(shadows[i] - shadows[i + shadow_count]) > .0000001f)
            ++shadow_changed;
    printf("ferry_visibility_pass=shadow changed_pixels=%u\n", shadow_changed);
    CHECK(shadow_changed > 5);
    free(shadows);
    gpu.scene.near_water = (Mesh) { 0 };
    boat_gpu_destroy(&gpu);
    boat_session_destroy(&session);
    CloseWindow();
    puts("ferry_visibility=ok");
}

typedef struct {
    double seconds, flight;
    float distance, min_clearance, max_height, min_source_distance, end_speed;
    unsigned jumps, landings, capsizes;
    float encounter_min_speed;
    bool finished;
} Report;

static Report measure(bool source, double delay, float thrust, float offset, bool player_wakes)
{
    BoatSession session;
    ObjectStudy objects;
    start(&session, &objects, source, delay);
    session.presentation_enabled = false;
    if (!player_wakes) {
        session.world.water.objects = NULL;
        session.world.water.traffic_objects = NULL;
    }
    session.boat.position.y += offset;
    unsigned next = 1;
    Report r = { .min_clearance = 1000, .min_source_distance = 1000, .encounter_min_speed = 1000 };
    BoatMotionState previous = session.boat.motion_state;
    for (unsigned t = 0; t < 90 * BOAT_TICK_HZ && next < ROUTE_COUNT; ++t) {
        Boat before = session.boat;
        timed_boat_step(&session, pilot(&session.boat, route[next], thrust), NULL, NULL,
            player_wakes ? &objects : NULL);
        advance_target(&session.boat, &next);
        r.seconds += BOAT_TICK_SECONDS;
        r.distance += Vector2Distance(before.position, session.boat.position);
        r.max_height = fmaxf(r.max_height, session.boat.height);
        if (r.seconds >= 8 && r.seconds <= 30)
            r.encounter_min_speed
                = fminf(r.encounter_min_speed, Vector2Length(session.boat.velocity));
        bool clear = boat_hull_clear(&session.boat, &session.world, boat_session_time(&session));
        if (!clear)
            r.min_clearance = 0;
        Vector2 ship;
        if (source && boat_vessel_wake_position(ferry(), boat_session_time(&session), &ship))
            r.min_source_distance
                = fminf(r.min_source_distance, Vector2Distance(ship, session.boat.position));
        if (session.boat.motion_state == BOAT_AIRBORNE) {
            r.flight += BOAT_TICK_SECONDS;
            if (previous != BOAT_AIRBORNE)
                ++r.jumps;
        }
        if (session.boat.motion_state == BOAT_CAPSIZED && previous != BOAT_CAPSIZED)
            ++r.capsizes;
        previous = session.boat.motion_state;
    }
    r.finished = next == ROUTE_COUNT;
    r.landings = session.boat.landings;
    r.end_speed = Vector2Length(session.boat.velocity);
    boat_session_destroy(&session);
    return r;
}

// A local straight approach isolates speed/angle from the route controller. No player
// emitter is installed in the first half; the second half includes a crossing player trail.
static void approaches(void)
{
    puts("approach_speed,angle,player_overlap,jumps,landings,flight_s,capsizes,min_hull_clear");
    BoatVesselWake w = ferry();
    Vector2 centre = { -30, -385 };
    Vector2 forward = Vector2Normalize(Vector2Subtract(route[1], route[0]));
    for (int speed = 12; speed <= 24; speed += 6)
        for (int angle = -20; angle <= 20; angle += 20)
            for (int overlap = 0; overlap <= 1; ++overlap) {
                BoatSession session;
                BoatWater water = { .vessel = w };
                BoatWakeObjects objects;
                CHECK(boat_wake_objects_init(
                    &objects, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT));
                // Seed a real, separately owned player-emitter trail crossing the ferry's crests.
                for (unsigned tick = 0; tick <= 600; ++tick) {
                    if (tick)
                        boat_wake_objects_advance(&objects);
                    CHECK(boat_wake_objects_track(
                        &objects, 3, (Vector2) { -170 + tick * .2f, -350 }, 24, 1));
                }
                if (overlap)
                    water.objects = &objects;
                boat_session_init(&session, test_world(BOAT_LOCATION_RIVER), water);
                session.presentation_enabled = false;
                boat_session_set_tick(&session, 600);
                Vector2 direction = Vector2Rotate(forward, angle * DEG2RAD);
                session.boat.position = Vector2Subtract(centre, Vector2Scale(direction, 80));
                session.boat.heading = atan2f(direction.x, -direction.y);
                session.boat.velocity = Vector2Scale(direction, (float)speed);
                session.boat.throttle = session.boat.engine_response = 1;
                BoatMotionState previous = session.boat.motion_state;
                unsigned jumps = 0, capsizes = 0;
                double flight = 0;
                bool clear = true;
                for (unsigned t = 0; t < 7 * BOAT_TICK_HZ; ++t) {
                    if (overlap)
                        boat_wake_objects_advance(&objects);
                    CHECK(boat_session_step(&session, (BoatInput) { 0 }));
                    if (session.boat.motion_state == BOAT_AIRBORNE) {
                        flight += BOAT_TICK_SECONDS;
                        if (previous != BOAT_AIRBORNE)
                            ++jumps;
                    }
                    if (session.boat.motion_state == BOAT_CAPSIZED && previous != BOAT_CAPSIZED)
                        ++capsizes;
                    previous = session.boat.motion_state;
                    clear &= boat_hull_clear(
                        &session.boat, &session.world, boat_session_time(&session));
                }
                printf("%d,%d,%d,%u,%u,%.3f,%u,%d\n", speed, angle, overlap, jumps,
                    session.boat.landings, flight, capsizes, clear);
                CHECK(jumps > 0 && session.boat.landings > 0 && !capsizes && clear);
                boat_session_destroy(&session);
            }
    Report alone = measure(true, 0, 1, 0, false);
    printf("source_without_player_wakes seconds=%.3f jumps=%u landings=%u\n", alone.seconds,
        alone.jumps, alone.landings);
    CHECK(alone.finished && alone.jumps > 0 && !alone.capsizes);
}

static void source_bounds(void)
{
    BoatVesselWake w = ferry();
    Vector2 side = { -w.direction.y, w.direction.x };
    float shore = 1000;
    for (int tick = 0; tick <= 24 * BOAT_TICK_HZ; ++tick) {
        Vector2 centre
            = Vector2Add(w.origin, Vector2Scale(w.direction, w.speed * tick / BOAT_TICK_HZ));
        // Full ferry footprint plus one metre of clearance to the shore and bridge legs.
        for (int end = -1; end <= 1; end += 2)
            for (int edge = -1; edge <= 1; edge += 2) {
                Vector2 p = Vector2Add(centre,
                    Vector2Add(Vector2Scale(w.direction, end * BOAT_VESSEL_LENGTH * .5f),
                        Vector2Scale(side, edge * BOAT_VESSEL_BEAM * .5f)));
                CHECK(test_river_water(p, 1));
            }
        float lo = 0, hi = 200;
        for (int i = 0; i < 12; ++i) {
            float mid = (lo + hi) * .5f;
            if (test_river_water(centre, mid))
                lo = mid;
            else
                hi = mid;
        }
        shore = fminf(shore, lo);
    }
    printf("source_centre_min_shore_m=%.2f source_bytes=%zu source_count=1 history_records=0\n",
        shore, sizeof(w));
    clock_t begin = clock();
    double sum = 0;
    for (int i = 0; i < 100000; ++i)
        sum += boat_vessel_wake_query(w, (Vector2) { -160 + (i % 150), -350 }, 10).height;
    printf("source_query_ns=%.1f checksum=%.6f\n", (double)(clock() - begin) / CLOCKS_PER_SEC * 1e4,
        sum);
}

static void checks(void)
{
    contracts();
    traffic_contracts();
    source_bounds();
    approaches();
    float length = 0;
    for (unsigned i = 1; i < ROUTE_COUNT; ++i) {
        length += Vector2Distance(route[i - 1], route[i]);
        for (int j = 0; j <= 100; ++j) {
            Vector2 p = Vector2Lerp(route[i - 1], route[i], j / 100.0f);
            CHECK(test_river_water(p, 12));
        }
    }
    printf("prototype_polyline_m=%.2f required_average_55s=%.2f\n", length, length / 55);
    puts("source,delay,thrust,offset,finished,seconds,metres,mean_mps,jumps,landings,flight_s,max_"
         "height,capsizes,hull_clear,min_source_m,end_mps,encounter_min_mps");
    for (int source = 0; source <= 1; ++source)
        for (int arrival = 0; arrival <= (source ? 2 : 0); ++arrival)
            for (int speed = 0; speed <= 1; ++speed)
                for (int line = -1; line <= 1; ++line) {
                    Report r = measure(source, arrival * 3, speed ? 1 : .75f, line * 10, true);
                    printf("%d,%d,%.2f,%d,%d,%.3f,%.2f,%.2f,%u,%u,%.3f,%.3f,%u,%d,%.1f,%.2f,%.2f\n",
                        source, arrival * 3, speed ? 1.0 : .75, line * 10, r.finished, r.seconds,
                        r.distance, r.distance / r.seconds, r.jumps, r.landings, r.flight,
                        r.max_height, r.capsizes, r.min_clearance > 0, r.min_source_distance,
                        r.end_speed, r.encounter_min_speed);
                    CHECK(r.finished && !r.capsizes && r.min_clearance > 0);
                    CHECK(!source
                        || (r.min_source_distance > 30 && r.jumps > 0 && r.landings == r.jumps
                            && r.seconds >= 45 && r.seconds <= 60));
                }
    puts("boat_race_trial=ok");
}

// Keep the historical R0 checks intact; opt into the current race layout for visual play.
static void preview_start(BoatSession *session, ObjectStudy *objects, bool source, bool skill)
{
    start(session, objects, source, 0);
    if (skill) {
        objects->traffic_enabled = false;
        session->world.water.traffic_objects = NULL;
        session->world.water.vessel = source ? preview_course()->source : (BoatVesselWake) { 0 };
        session->world.water.storm = source ? preview_course()->storm : (BoatStorm) { 0 };
        // Start behind the buoy row at an authored grid slot, as a race entrant does.
        session->boat.position = preview_course()->grid[0];
        Vector2 direction = preview_course()->start.direction;
        session->boat.heading = atan2f(direction.x, -direction.y);
        boat_presentation_reset(&session->presentation);
        session->presentation.camera_heading = session->boat.heading;
    }
}

static Vector2 preview_target(unsigned next, bool skill)
{
    const BoatRaceCourse *course = preview_course();
    if (skill)
        return course->gates[next <= course->gate_count ? next - 1 : course->gate_count - 1].centre;
    return route[next < ROUTE_COUNT ? next : ROUTE_COUNT - 1];
}

static void preview_advance(const Boat *boat, Vector2 previous, unsigned *next, bool skill)
{
    if (!skill) {
        advance_target(boat, next);
        return;
    }
    const BoatRaceCourse *course = preview_course();
    while (*next <= course->gate_count
        && boat_race_cross(&course->gates[*next - 1], previous, boat->position) >= 0)
        ++*next;
}

int main(int argc, char **argv)
{
    test_scenery_load();
    bool check = false, gpu_check = false, gpu_enabled = true, play = false, source = true;
    bool skill = false;
    const char *output = NULL;
    double seconds = 12;
    float wave_scale = 1;
    int frames = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--check"))
            check = true;
        else if (!strcmp(argv[i], "--gpu-check"))
            gpu_check = true;
        else if (!strcmp(argv[i], "--plugin") && i + 1 < argc) {
            plugin_path = argv[++i];
            skill = true;
        } else if (!strcmp(argv[i], "--skill-course"))
            skill = true;
        else if (!strcmp(argv[i], "--play"))
            play = true;
        else if (!strcmp(argv[i], "--no-source"))
            source = false;
        else if (!strcmp(argv[i], "--wave-scale") && i + 1 < argc) {
            char *end;
            wave_scale = strtof(argv[++i], &end);
            if (*end || !isfinite(wave_scale) || wave_scale < 0 || wave_scale > 4)
                return 2;
        } else if (!strcmp(argv[i], "--renderer") && i + 1 < argc) {
            const char *name = argv[++i];
            if (strcmp(name, "gpu") && strcmp(name, "cpu"))
                return 2;
            gpu_enabled = !strcmp(name, "gpu");
        } else if (!strcmp(argv[i], "--output") && i + 1 < argc)
            output = argv[++i];
        else if (!strcmp(argv[i], "--time") && i + 1 < argc) {
            char *end;
            seconds = strtod(argv[++i], &end);
            if (*end || !isfinite(seconds) || seconds < 0 || seconds > 90)
                return 2;
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            char *end;
            long value = strtol(argv[++i], &end, 10);
            if (*end || value < 1 || value > 100000)
                return 2;
            frames = (int)value;
        } else {
            fprintf(stderr,
                "Usage: boat_race_trial [--check] [--play] [--skill-course] [--plugin DIR] "
                "[--no-source] "
                "[--wave-scale 0..4] [--renderer cpu|gpu] [--output FILE --time SECONDS] [--frames "
                "COUNT]\n");
            return 2;
        }
    }
    SetTraceLogLevel(LOG_WARNING);
    if (plugin_path && (check || gpu_check)) {
        fprintf(stderr,
            "--plugin is for local preview; use validate_boat_plugin_race for headless checks.\n");
        return 2;
    }
    if (plugin_path
        && !race_plugin_open(&plugin, plugin_path, test_world(BOAT_LOCATION_RIVER), GetTime())) {
        test_worlds_close();
        return 1;
    }
    plugin_enabled = plugin_path != NULL;
    if (gpu_check) {
        boat_water_radial_gpu_check();
        boat_vessel_wake_gpu_check(ferry());
        boat_water_storm_gpu_check(preview_course()->storm);
        ferry_visibility();
        return 0;
    }
    if (check) {
        checks();
        return 0;
    }
    if (output && play)
        return 2;
    BoatSession session;
    ObjectStudy objects;
    preview_start(&session, &objects, source, skill);
    if (!output) {
        session.world.water.storm.height *= wave_scale;
        session.world.water.vessel.height *= wave_scale;
    }
    unsigned next = 1;
    unsigned targets = skill ? preview_course()->gate_count : ROUTE_COUNT - 1;
    if (output)
        for (unsigned t = 0; t < (unsigned)llround(seconds * BOAT_TICK_HZ); ++t) {
            Vector2 previous = session.boat.position;
            timed_boat_step(&session,
                pilot(&session.boat, preview_target(next, skill), skill && next > targets ? 0 : 1),
                NULL, NULL, &objects);
            preview_advance(&session.boat, previous, &next, skill);
        }
    // Captures keep the default driven path/camera so height comparisons share a view.
    if (output) {
        session.world.water.storm.height *= wave_scale;
        session.world.water.vessel.height *= wave_scale;
    }
    bool window = gpu_enabled || !output;
    if (window) {
        SetConfigFlags(output ? FLAG_WINDOW_HIDDEN : FLAG_WINDOW_RESIZABLE);
        InitWindow(960, 600,
            skill ? "River sprint - skill course"
                  : "River sprint R0 - shortened route / ferry trial");
        CHECK(IsWindowReady());
        SetTargetFPS(60);
    }
    BoatGpu gpu = { 0 };
    Renderer3D cpu = { 0 };
    BoatCpuView cpu_view = { 0 };
    Color pixels[320 * 200];
    Texture2D texture = { 0 };
    if (gpu_enabled)
        CHECK(boat_gpu_init(&gpu, false));
    if (!gpu_enabled && !output) {
        Image blank = GenImageColor(320, 200, BLACK);
        texture = LoadTextureFromImage(blank);
        UnloadImage(blank);
    }
    double accumulator = 0;
    unsigned frame = 0;
    bool staged_previous_frame = false;
    float target_thrust = 0;
    BoatResetHold reset_hold = { 0 };
    while (output || !WindowShouldClose()) {
        if (!output) {
            bool plugin_restart = plugin_enabled
                && race_plugin_frame(&plugin, IsKeyPressed(KEY_F5), IsKeyPressed(KEY_F6),
                    IsKeyPressed(KEY_F7), GetTime());
            if (plugin_restart
                || boat_reset_hold_update(
                    &reset_hold, IsKeyDown(KEY_BACKSPACE), IsWindowFocused(), GetFrameTime())) {
                boat_session_destroy(&session);
                preview_start(&session, &objects, source, skill);
                session.world.water.storm.height *= wave_scale;
                session.world.water.vessel.height *= wave_scale;
                targets = skill ? preview_course()->gate_count : ROUTE_COUNT - 1;
                next = 1;
                target_thrust = 0;
                accumulator = 0;
            }
            target_thrust = IsWindowFocused()
                ? boat_pedal_target(IsKeyDown(KEY_W) || IsKeyDown(KEY_UP),
                      IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN),
                      IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
                : 0;
            if (plugin_enabled && (plugin.staged || staged_previous_frame))
                accumulator = 0;
            else
                accumulator += fmin(GetFrameTime(), .1);
            staged_previous_frame = plugin_enabled && plugin.staged;
            while (accumulator >= BOAT_TICK_SECONDS) {
                BoatInput input = play ? (BoatInput) { .steering_rate
                    = (float)(IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT))
                        - (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)),
                    .throttle_rate = boat_throttle_rate(session.boat.throttle, target_thrust) }
                                       : pilot(&session.boat, preview_target(next, skill),
                                             next <= targets ? 1 : 0);
                Vector2 previous = session.boat.position;
                timed_boat_step(&session, input, NULL, NULL, &objects);
                preview_advance(&session.boat, previous, &next, skill);
                accumulator -= BOAT_TICK_SECONDS;
            }
        }
        BoatViewState view = boat_session_view(&session);
        view.race.course = preview_course();
        view.race.enabled = true; // The race room's start, checkpoint and finish lines.
        if (skill)
            view.race.next_gate = (uint8_t)(next - 1);
        BoatCamera camera = { .distance = 40, .height = 14 };
        if (skill)
            camera = (BoatCamera) { .distance = session.world.definition->camera_distance,
                .height = session.world.definition->camera_height };
        if (gpu_enabled) {
            boat_gpu_draw(&gpu, &view, &session.presentation, &session.world, camera);
            CHECK(!gpu.failed);
        } else
            CHECK(boat_draw(&cpu, &cpu_view, pixels, 320, 200, &view, &session.presentation,
                &session.world, camera));
        if (output) {
            Image image = gpu_enabled
                ? LoadImageFromTexture(gpu.target.texture)
                : (Image) { pixels, 320, 200, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
            CHECK(image.data);
            if (gpu_enabled) {
                ImageFlipVertical(&image);
                ImageFlipHorizontal(&image);
            }
            CHECK(ExportImage(image, output));
            if (gpu_enabled)
                UnloadImage(image);
            break;
        }
        if (!gpu_enabled)
            UpdateTexture(texture, pixels);
        BeginDrawing();
        ClearBackground(BLACK);
        int scale = GetScreenWidth() / 320;
        if (GetScreenHeight() / 200 < scale)
            scale = GetScreenHeight() / 200;
        if (scale < 1)
            scale = 1;
        Rectangle dest = { (GetScreenWidth() - 320 * scale) / 2.0f,
            (GetScreenHeight() - 200 * scale) / 2.0f, 320 * scale, 200 * scale };
        DrawTexturePro(gpu_enabled ? gpu.target.texture : texture,
            gpu_enabled ? (Rectangle) { 320, 200, -320, -200 } : (Rectangle) { 0, 0, 320, 200 },
            dest, (Vector2) { 0 }, 0, WHITE);
        DrawRectangle(0, 0, GetScreenWidth(), plugin_enabled ? 84 : 58, (Color) { 0, 0, 0, 180 });
        DrawText(
            TextFormat("%s  %.2f s   %u landings   %.1f m/s   target %u/%u",
                skill ? "SKILL COURSE" : "R0 PROTOTYPE", view.time, session.boat.landings,
                Vector2Length(session.boat.velocity), next <= targets ? next : targets, targets),
            12, 8, 20, WHITE);
        DrawText(play ? "Hold W/S: forward/reverse | Shift: precision | A/D steer | Esc exit"
                      : "Scripted route | Hold Backspace 1 s: restart | Esc exit",
            12, 34, 18, WHITE);
        if (plugin_enabled)
            DrawText(TextFormat("%.110s", plugin.notice), 12, 64, 16, YELLOW);
        if (play && (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)))
            DrawText("PRECISION", 12, plugin_enabled ? 88 : 64, 14, (Color) { 118, 196, 178, 255 });
        if (reset_hold.seconds > 0) {
            DrawText("HOLD BACKSPACE: RESTART", 12, GetScreenHeight() - 32, 16, YELLOW);
            DrawRectangle(12, GetScreenHeight() - 12,
                (int)(180 * reset_hold.seconds / BOAT_RESET_HOLD_SECONDS), 4, YELLOW);
        }
        EndDrawing();
        if (frames && ++frame >= (unsigned)frames)
            break;
    }
    if (gpu_enabled)
        boat_gpu_destroy(&gpu);
    if (texture.id)
        UnloadTexture(texture);
    boat_cpu_view_destroy(&cpu_view);
    renderer3d_destroy(&cpu);
    boat_session_destroy(&session);
    if (window)
        CloseWindow();
    test_worlds_close();
    return 0;
}
