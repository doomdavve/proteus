#include "boat_map.h"
#include "boat_test_session.h"
#include "boat_view.h"
#include "waterfront_harbor.h"
#include "waterfront_route.h"
#include "waterfront_traffic.h"
#include "waterfront_upstream.h"

#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "Traffic check failed at %d: %s\n", __LINE__, #test);                  \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int check_vertical_contacts(void)
{
    BoatWater water = { 0 };
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
        WaterfrontVesselMotion ship = waterfront_traffic_motion(i, 40);
        Vector2 p = { -ship.position.x, ship.position.z };
        CHECK(waterfront_traffic_contact_height(p, 0, 2, 1.6f, .75f, -.4f, 2, water, 40, NULL));
        CHECK(!waterfront_traffic_contact_height(p, 0, 2, 1.6f, .75f, 30, 32, water, 40, NULL));
        CHECK(!waterfront_traffic_contact_height(p, 0, 2, 1.6f, .75f, -30, -28, water, 40, NULL));
    }
    const WaterfrontBerth *berths = waterfront_harbor_berths();
    for (int i = 0; i < WATERFRONT_HARBOR_BOATS; ++i) {
        Vector2 p = { -berths[i].position.x, berths[i].position.z };
        CHECK(waterfront_harbor_contact_height(p, .35f, -.4f, 2, water, 0, NULL));
        CHECK(!waterfront_harbor_contact_height(p, .35f, 30, 32, water, 0, NULL));
        BoatModelSpec spec = waterfront_harbor_spec(i);
        if (spec.mast_height > 5)
            CHECK(waterfront_harbor_contact_height(p, .35f, 4, 5, water, 0, NULL));
    }
    // Quay remains solid above a low pontoon's clearance height.
    CHECK(waterfront_harbor_contact_height((Vector2) { 27.55f, 182 }, .35f, 3, 4, water, 0, NULL));
    CHECK(!waterfront_harbor_contact_height((Vector2) { 20, 182 }, .35f, 3, 4, water, 0, NULL));
    return 0;
}

static bool near(Vector3 a, Vector3 b, float tolerance)
{
    return Vector3Distance(a, b) < tolerance;
}

static bool footprints_overlap(
    Vector2 a, float ah, float al, float ab, Vector2 b, float bh, float bl, float bb)
{
    // Independent projection of the four physical rectangle vertices onto both frames.
    Vector2 points[2][4], axes[4];
    float headings[] = { ah, bh }, lengths[] = { al, bl }, beams[] = { ab, bb };
    Vector2 centers[] = { a, b };
    for (int hull = 0; hull < 2; ++hull) {
        Vector2 right = { cosf(headings[hull]), -sinf(headings[hull]) };
        Vector2 forward = { -sinf(headings[hull]), -cosf(headings[hull]) };
        axes[hull * 2] = right;
        axes[hull * 2 + 1] = forward;
        for (int corner = 0; corner < 4; ++corner)
            points[hull][corner] = Vector2Add(centers[hull],
                Vector2Add(Vector2Scale(right, (corner & 1 ? 1 : -1) * beams[hull] * .5f),
                    Vector2Scale(forward, (corner & 2 ? 1 : -1) * lengths[hull] * .5f)));
    }
    for (int i = 0; i < 4; ++i) {
        float low[2] = { INFINITY, INFINITY }, high[2] = { -INFINITY, -INFINITY };
        for (int hull = 0; hull < 2; ++hull)
            for (int corner = 0; corner < 4; ++corner) {
                float value = Vector2DotProduct(points[hull][corner], axes[i]);
                low[hull] = fminf(low[hull], value);
                high[hull] = fmaxf(high[hull], value);
            }
        if (high[0] <= low[1] || high[1] <= low[0])
            return false;
    }
    return true;
}

static bool hulls_clear(const Boat *boat, double time)
{
    // Player geometry is offset 0.2 m toward its 2.0 m bow / 1.6 m stern.
    Vector2 center = { -boat->position.x - .2f * sinf(boat->heading),
        boat->position.y - .2f * cosf(boat->heading) };
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
        BoatModelSpec s = waterfront_traffic_spec(i);
        WaterfrontVesselMotion m = waterfront_traffic_motion(i, time);
        if (footprints_overlap(center, boat->heading, 3.6f, 1.5f,
                (Vector2) { m.position.x, m.position.z }, m.heading, s.length, s.beam))
            return false;
    }
    return test_river_water(boat->position, .5f);
}

// Exercise the boating snapshot as well as the authored paths: both renderers must
// receive the same flock, and moving stimuli must not introduce frame-history dependence.
// Day-long sessions keep exact route periodicity, smooth motion, and contact/render agreement.
static int check_long_session(void)
{
    const double epochs[] = { 3600.3, 86400.3, 30 * 86400.3 };
    WaterfrontTrafficFrame frame;
    WaterfrontSceneFrame gulls = { 0 }, next = { 0 };
    Vector3 far = { 10000, 0, 10000 };
    for (int e = 0; e < 3; ++e) {
        double time = epochs[e];
        waterfront_traffic_evaluate(boat_water_gentle(), time, &frame);
        for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
            double period = waterfront_traffic_period(i);
            WaterfrontVesselMotion m = waterfront_traffic_motion(i, time);
            WaterfrontVesselMotion lap
                = waterfront_traffic_motion(i, time - floor(time / period) * period);
            CHECK(near(m.position, lap.position, .001f) && near(m.velocity, lap.velocity, .001f));
            Vector3 finite_difference
                = Vector3Scale(Vector3Subtract(waterfront_traffic_motion(i, time + .01).position,
                                   waterfront_traffic_motion(i, time - .01).position),
                    50);
            CHECK(near(m.velocity, finite_difference, .03f));
            // Rendered pose, moving contact, and route sample use one time evaluation.
            const WaterfrontVesselState *vessel = &frame.vessels[i];
            CHECK(!memcmp(&vessel->motion, &m, sizeof(m)));
            CHECK(
                vessel->pose.position.x == m.position.x && vessel->pose.position.z == m.position.z);
            WaterfrontTrafficContact contact;
            CHECK(waterfront_traffic_contact((Vector2) { -m.position.x, m.position.z }, m.heading,
                2, 1.6f, .75f, time, &contact));
            if (contact.vessel != i)
                fprintf(stderr, "contact i=%d actual=%d\n", i, contact.vessel);
            CHECK(contact.vessel == i);
        }
        for (int j = 0; j < frame.wake_count; ++j)
            CHECK(test_river_water(
                (Vector2) { -frame.wakes[j].pose.position.x, frame.wakes[j].pose.position.z },
                .5f));
        waterfront_boating_gulls(time, far, (Vector3) { 0 }, &gulls);
        waterfront_boating_gulls(time + 1 / 120.0, far, (Vector3) { 0 }, &next);
        for (int i = 0; i < gulls.gull_count; ++i) {
            float step = Vector3Distance(gulls.gulls[i][0].position, next.gulls[i][0].position);
            // Fastest followers fly about 13 m/s; float time would freeze or jump at 30 days.
            CHECK(step > 1e-4f && step < .25f);
            CHECK(fabsf(gulls.gulls[i][1].rotation.x - next.gulls[i][1].rotation.x) < .2f);
        }
    }
    return 0;
}

static int check_gulls(void)
{
    WaterfrontSceneFrame previous = { 0 }, frame = { 0 }, sought = { 0 };
    Vector3 far = { 10000, 0, 10000 };
    waterfront_boating_gulls(0, far, (Vector3) { 0 }, &previous);
    for (int tick = 0; tick <= 2400; ++tick) {
        float time = tick / 20.0f;
        waterfront_boating_gulls(time, far, (Vector3) { 0 }, &frame);
        waterfront_boating_gulls(120 - time, far, (Vector3) { 0 }, &sought);
        waterfront_boating_gulls(time, far, (Vector3) { 0 }, &sought);
        CHECK(frame.gull_count == WATERFRONT_HARBOR_GULLS);
        for (int i = 0; i < frame.gull_count; ++i) {
            Vector3 p = frame.gulls[i][0].position;
            CHECK(isfinite(p.x) && isfinite(p.y) && isfinite(p.z) && p.y > 1);
            CHECK(Vector3Distance(p, previous.gulls[i][0].position) < 1.5f);
            for (int j = i / 8 * 8; j < i; ++j)
                CHECK(Vector3Distance(p, frame.gulls[j][0].position) > 2);
            for (int part = 0; part < WATERFRONT_GULL_PARTS; ++part) {
                Transform3D a = frame.gulls[i][part], b = sought.gulls[i][part];
                CHECK(near(a.position, b.position, 1e-6f) && near(a.rotation, b.rotation, 1e-6f));
                CHECK(near(a.position, p, 1e-6f));
                CHECK(isfinite(a.rotation.x) && isfinite(a.rotation.y) && isfinite(a.rotation.z));
            }
        }
        previous = frame;
    }
    // A player crosses the nearby flock at speed, testing the avoidance envelope's
    // entry/exit rather than only undisturbed flight.
    for (int tick = 0; tick <= 800; ++tick) {
        float time = tick / 40.0f;
        waterfront_boating_gulls(
            time, (Vector3) { -25 + time * 4, 0, 38 }, (Vector3) { 4, 0, 0 }, &frame);
        if (tick)
            for (int i = 0; i < frame.gull_count; ++i)
                CHECK(
                    Vector3Distance(frame.gulls[i][0].position, previous.gulls[i][0].position) < 1);
        previous = frame;
    }
    waterfront_boating_gulls(3, far, (Vector3) { 0 }, &frame);
    Vector3 stimulus = frame.gulls[0][0].position;
    stimulus.y = 0;
    waterfront_boating_gulls(3, stimulus, (Vector3) { 0 }, &sought);
    CHECK(sought.gulls[0][0].position.y > frame.gulls[0][0].position.y + 2);
    float idle_height = sought.gulls[0][0].position.y;
    waterfront_boating_gulls(3, stimulus, (Vector3) { 8, 0, 0 }, &sought);
    CHECK(sought.gulls[0][0].position.y > idle_height + 1);
    BoatSession boat;
    boat_session_init(&boat, test_world(BOAT_LOCATION_RIVER), (BoatWater) { 0 });
    boat_scene_frame(
        SESSION_VIEW(&boat), &boat.presentation, &boat.world, BOAT_CAMERA_DEFAULT, &frame);
    waterfront_boating_gulls(boat_session_time(&boat), frame.player.hull.position,
        (Vector3) { -boat.boat.velocity.x, 0, boat.boat.velocity.y }, &sought);
    CHECK(frame.gull_count == WATERFRONT_HARBOR_GULLS);
    CHECK(!memcmp(frame.gulls, sought.gulls, sizeof(frame.gulls)));
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat_scene_frame(
        SESSION_VIEW(&boat), &boat.presentation, &boat.world, BOAT_CAMERA_DEFAULT, &frame);
    CHECK(frame.gull_count == 0);
    puts("harbor_gulls=ok birds=32 continuity=120s seeks=ok player_stimuli=ok snapshot=ok");
    return 0;
}

// Drive a full timetable with virtual time: hulls stay in water, stopping remains smooth,
// the raised bridge clears both schooner passages, and contacts are outer-only.
static int check_upstream(void)
{
    const BoatMap *map = &test_world(BOAT_LOCATION_OUTER)->map;
    Vector2 axis = Vector2Normalize(
        Vector2Subtract(WATERFRONT_HISINGSBRON_SOUTH, WATERFRONT_HISINGSBRON_NORTH));
    int passages = 0;
    for (int i = WATERFRONT_TRAFFIC_VESSELS; i < WATERFRONT_TRAFFIC_ALL; ++i) {
        BoatModelSpec spec = waterfront_traffic_spec(i);
        const Mesh3D *mesh = waterfront_traffic_mesh(i);
        CHECK(mesh && mesh->triangle_count > 0);
        WaterfrontVesselMotion previous = waterfront_traffic_motion(i, -1);
        bool stopped = false;
        for (int tick = 0; tick <= (int)WATERFRONT_UPSTREAM_PERIOD; ++tick) {
            double t = tick;
            WaterfrontVesselMotion m = waterfront_traffic_motion(i, t);
            WaterfrontVesselMotion repeat
                = waterfront_traffic_motion(i, t + WATERFRONT_UPSTREAM_PERIOD);
            CHECK(near(m.position, repeat.position, .01f));
            CHECK(near(m.velocity, repeat.velocity, .01f));
            CHECK(Vector3Distance(m.position, previous.position) < 6.1f);
            CHECK(Vector3Distance(m.velocity, previous.velocity) < 1.7f);
            stopped |= Vector3Length(m.velocity) < .001f;
            Transform3D pose
                = { .position = m.position, .rotation = { 0, m.heading, 0 }, .scale = { 1, 1, 1 } };
            for (int corner = 0; corner < 4; ++corner) {
                Vector3 p = boat_model_point(pose,
                    (Vector3) { (corner & 1 ? 1 : -1) * spec.beam * .5f, 0,
                        (corner & 2 ? 1 : -1) * spec.length * .5f });
                CHECK(boat_map_water(map, (Vector2) { -p.x, p.z }, 2));
            }
            if (i == WATERFRONT_TRAFFIC_VESSELS + WATERFRONT_UPSTREAM_SCHOONER) {
                Vector2 d = { m.position.x - WATERFRONT_HISINGSBRON_NORTH.x,
                    m.position.z - WATERFRONT_HISINGSBRON_NORTH.y };
                float across = axis.x * d.y - axis.y * d.x;
                // Include half the hull and the width of the deck in the clearance check.
                if (fabsf(across) < spec.length * .5f + WATERFRONT_LIFT_SPAN_WIDTH * .5f) {
                    CHECK(WATERFRONT_LIFT_UNDERSIDE + waterfront_upstream_lift(t)
                        > spec.mast_height + 1);
                    ++passages;
                }
            }
            previous = m;
        }
        CHECK(stopped == (i < WATERFRONT_TRAFFIC_ALL - 1));
        WaterfrontVesselMotion m = waterfront_traffic_motion(i, 300);
        Vector2 p = { -m.position.x, m.position.z };
        WaterfrontTrafficContact contact;
        CHECK(!waterfront_traffic_contact_height(
            p, 0, 2, 1.6f, .75f, -.4f, 2, (BoatWater) { 0 }, 300, NULL));
        CHECK(waterfront_traffic_contact_height(
            p, 0, 2, 1.6f, .75f, -.4f, 2, (BoatWater) { .upstream = true }, 300, &contact));
        CHECK(contact.vessel == i);
    }
    CHECK(passages > 0);
    WaterfrontTrafficFrame frame;
    waterfront_traffic_evaluate((BoatWater) { .upstream = true }, 300, &frame);
    CHECK(frame.vessel_count == WATERFRONT_TRAFFIC_ALL && frame.lift);
    CHECK(frame.wake_count <= WATERFRONT_TRAFFIC_WAKE_CAPACITY);
    waterfront_traffic_evaluate((BoatWater) { 0 }, 300, &frame);
    CHECK(frame.vessel_count == WATERFRONT_TRAFFIC_VESSELS && !frame.lift);
    puts("upstream=ok timetable=1200s hull_clearance=2m contacts=outer_only lift_clearance=ok");
    return 0;
}

// L3.1: the caller-owned definition must reproduce the compiled queries, and its own route
// evaluation must be periodic, finite and structurally bounded.
static int check_route_definition(void)
{
    const WaterfrontRouteDefinition *defaults = waterfront_route_defaults();
    WaterfrontRouteDiagnostic diagnostic;
    CHECK(defaults && defaults->actor_count == WATERFRONT_ROUTE_ACTORS);
    CHECK(waterfront_route_validate(defaults, &diagnostic));
    CHECK(waterfront_route_has_content(defaults) && waterfront_route_has_bridge(defaults));

    const double times[] = { -320.75, -1, 0, .001, 40.3, 300, 1200, 86400.3 };
    for (int i = 0; i < WATERFRONT_ROUTE_ACTORS; ++i) {
        double period = waterfront_traffic_period(i);
        CHECK(waterfront_route_period(defaults, i) == period);
        CHECK(!strcmp(waterfront_route_recipe_name(defaults->actors[i].recipe),
            waterfront_route_recipe_name(defaults->actors[i].recipe)));
        for (unsigned t = 0; t < sizeof(times) / sizeof(times[0]); ++t) {
            WaterfrontVesselMotion a = waterfront_traffic_motion(i, times[t]);
            WaterfrontVesselMotion b = waterfront_traffic_motion_routes(defaults, i, times[t]);
            CHECK(!memcmp(&a, &b, sizeof(a)));
            CHECK(isfinite(b.position.x) && isfinite(b.position.z) && isfinite(b.velocity.x));
            if (period > 0) {
                WaterfrontVesselMotion wrapped
                    = waterfront_traffic_motion(i, times[t] + period);
                CHECK(near(wrapped.position, a.position, .002f));
                CHECK(near(wrapped.velocity, a.velocity, .002f));
            }
        }
        // Direct seek and a stepped evaluation use the same pure time input.
        WaterfrontVesselMotion direct = waterfront_traffic_motion(i, 137.25);
        WaterfrontVesselMotion stepped = direct;
        for (int tick = 0; tick <= 13725; ++tick)
            stepped = waterfront_traffic_motion(i, tick / 100.0);
        CHECK(near(direct.position, stepped.position, .002f)
            && near(direct.velocity, stepped.velocity, .002f));
    }
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
        Vector2 default_stops[8], route_stops[8];
        int a = waterfront_upstream_stops(i, default_stops, 8);
        int b = waterfront_route_stops(defaults, WATERFRONT_TRAFFIC_VESSELS + i, route_stops, 8);
        CHECK(a == b);
        for (int k = 0; k < a; ++k)
            CHECK(Vector2Distance(default_stops[k], route_stops[k]) < 1e-5f);
    }
    for (int step = 0; step <= 1200; ++step)
        CHECK(fabsf(waterfront_route_lift(defaults, step) - waterfront_upstream_lift(step)) < 1e-5f);

    WaterfrontTrafficFrame compiled, selected;
    waterfront_traffic_evaluate((BoatWater) { .upstream = true }, 300, &compiled);
    waterfront_traffic_evaluate_routes(defaults, (BoatWater) { .upstream = true }, 300, &selected);
    CHECK(compiled.vessel_count == selected.vessel_count);
    CHECK(compiled.lift == selected.lift && compiled.wake_count == selected.wake_count);
    CHECK(!memcmp(compiled.vessels, selected.vessels, sizeof(compiled.vessels)));
    CHECK(!memcmp(compiled.wakes, selected.wakes, sizeof(selected.wakes)));
    CHECK(compiled.routes == NULL && selected.routes == defaults);

    // A bounded authored loop: four corners, no stops, period provided without an explicit
    // cruise speed. It must close, and each structural failure must be rejected.
    WaterfrontRouteDefinition custom = { .actor_count = 1 };
    WaterfrontRouteActor *a = &custom.actors[0];
    strcpy(a->id, "passing");
    a->shape = WATERFRONT_ROUTE_POINTS;
    a->recipe = WATERFRONT_CRAFT_RUNABOUT;
    a->period = 40;
    a->point_count = 4;
    a->points[0] = (WaterfrontRoutePoint) { 0, 0, 2 };
    a->points[1] = (WaterfrontRoutePoint) { 40, 0, 2 };
    a->points[2] = (WaterfrontRoutePoint) { 40, 40, 2 };
    a->points[3] = (WaterfrontRoutePoint) { 0, 40, 2 };
    CHECK(waterfront_route_validate(&custom, &diagnostic));
    WaterfrontVesselMotion start = waterfront_route_motion(&custom, 0, 0);
    WaterfrontVesselMotion lap = waterfront_route_motion(&custom, 0, 40);
    CHECK(near(start.position, lap.position, .01f) && near(start.velocity, lap.velocity, .01f));
    CHECK(waterfront_route_period(&custom, 0) == 40);
    CHECK(waterfront_route_mesh(&custom, 0) && waterfront_route_mesh(&custom, 0)->triangle_count);

    custom.actors[1] = custom.actors[0];
    custom.actor_count = 2;
    CHECK(!waterfront_route_validate(&custom, &diagnostic));
    CHECK(!strcmp(diagnostic.field, "scene.traffic.vessels[2].id"));
    custom.actor_count = WATERFRONT_ROUTE_ACTORS + 1;
    CHECK(!waterfront_route_validate(&custom, &diagnostic));
    custom.actor_count = 1;
    a->points[1].radius = 100;
    CHECK(!waterfront_route_validate(&custom, &diagnostic));
    a->points[1].radius = 2;
    a->speed = 4;
    a->period = 30;
    CHECK(!waterfront_route_validate(&custom, &diagnostic));
    a->speed = 0;
    a->period = 40;
    a->stop_count = 1;
    a->stops[0] = (WaterfrontRouteStop) { .id = "pier", .segment = 9, .dwell = 2 };
    CHECK(!waterfront_route_validate(&custom, &diagnostic));
    a->stop_count = 0;
    a->bridge = true;
    CHECK(!waterfront_route_validate(&custom, &diagnostic));
    a->bridge = false;
    CHECK(waterfront_route_validate(&custom, &diagnostic));
    printf("route_definition actors=%u storage=%zu points=%zu stops=%zu\n",
        (unsigned)WATERFRONT_ROUTE_ACTORS, sizeof(WaterfrontRouteDefinition),
        sizeof(WaterfrontRoutePoint) * WATERFRONT_ROUTE_POINT_CAPACITY,
        sizeof(WaterfrontRouteStop) * WATERFRONT_ROUTE_STOPS);
    return 0;
}

// L3.2: one authored craft flows through the shared scene frame, contact and camera queries.
// Reordering preserves each actor's identity; removing the collection removes every consumer.
static int check_scene_traffic(void)
{
    BoatSceneDefinition scene = { .version = BOAT_SCENE_VERSION, .traffic_present = true };
    CHECK(waterfront_route_preset("marina_pass", &scene.traffic.actors[0]));
    CHECK(waterfront_route_preset("river_runabout", &scene.traffic.actors[1]));
    scene.traffic.actor_count = 2;
    WaterfrontRouteDiagnostic diagnostic;
    CHECK(waterfront_route_validate(&scene.traffic, &diagnostic));
    BoatWater water = { .upstream = true };
    WaterfrontSceneFrame frame, other;
    waterfront_scene_evaluate_scene(&scene, 12, water, &frame);
    CHECK(frame.traffic.routes == &scene.traffic && frame.traffic.vessel_count == 2);
    for (int i = 0; i < 2; ++i) {
        WaterfrontVesselMotion m = waterfront_traffic_motion_routes(&scene.traffic, i, 12);
        CHECK(!memcmp(&frame.traffic.vessels[i].motion, &m, sizeof(m)));
        CHECK(frame.traffic.vessels[i].pose.position.x == m.position.x);
        Vector2 p = { -m.position.x, m.position.z };
        WaterfrontTrafficContact contact;
        CHECK(waterfront_traffic_contact_height_scene(
            &scene, p, m.heading, 2, 1.6f, .75f, -.4f, 2, water, 12, &contact));
        CHECK(contact.vessel == i);
        CHECK(!waterfront_traffic_camera_clear_scene(
            &scene, (Vector3) { m.position.x, 1, m.position.z }, 12, 0));
    }
    Vector3 passing = frame.traffic.vessels[0].pose.position;
    CHECK(waterfront_traffic_camera_clear_scene(
        &scene, (Vector3) { passing.x, 50, passing.z }, 12, 0));
    CHECK(waterfront_traffic_camera_clear((Vector3) { 0, 50, 0 }, 12, 0));
    // Reordering arrays must not change identity or the pose drawn for either actor.
    BoatSceneDefinition reordered = scene;
    reordered.traffic.actors[0] = scene.traffic.actors[1];
    reordered.traffic.actors[1] = scene.traffic.actors[0];
    waterfront_scene_evaluate_scene(&reordered, 12, water, &other);
    for (int i = 0; i < 2; ++i) {
        WaterfrontVesselMotion m = waterfront_traffic_motion_routes(&reordered.traffic, i, 12);
        CHECK(!memcmp(&other.traffic.vessels[i].motion, &m, sizeof(m)));
    }
    // A present empty list disables travelling craft without falling back to compiled actors.
    BoatSceneDefinition removed = scene;
    removed.traffic.actor_count = 0;
    waterfront_scene_evaluate_scene(&removed, 12, water, &other);
    CHECK(other.traffic.vessel_count == 0 && other.traffic.wake_count == 0);
    WaterfrontVesselMotion m = waterfront_traffic_motion_routes(&reordered.traffic, 0, 12);
    CHECK(!waterfront_traffic_contact_height_scene(&removed, (Vector2) { -m.position.x, m.position.z },
        m.heading, 2, 1.6f, .75f, -.4f, 2, water, 12, NULL));
    return 0;
}

// L3.3: named stops sit stationary through a dwell, depart on the native acceleration profile,
// wrap at the period and update one departure without rebuilding the whole timetable.
static int check_ferry_schedule(void)
{
    BoatSceneDefinition scene = { .version = BOAT_SCENE_VERSION, .traffic_present = true };
    CHECK(waterfront_route_preset("alvsnabben", &scene.traffic.actors[0]));
    scene.traffic.actor_count = 1;
    WaterfrontRouteActor *ferry = &scene.traffic.actors[0];
    WaterfrontRouteDiagnostic diagnostic;
    CHECK(waterfront_route_validate(&scene.traffic, &diagnostic));
    // One explicit dwell; the remaining two stops share the rest of the 1,200 s period.
    ferry->stops[0].dwell = 35;
    CHECK(waterfront_route_validate(&scene.traffic, &diagnostic));
    double arrive[3], depart[3];
    for (int k = 0; k < 3; ++k)
        CHECK(waterfront_route_stop_times(&scene.traffic, 0, k, &arrive[k], &depart[k]));
    CHECK(depart[0] - arrive[0] == 35);
    CHECK(arrive[1] > depart[0] && depart[1] >= arrive[1]);
    CHECK(arrive[2] > depart[1]);
    WaterfrontVesselMotion before = waterfront_traffic_motion_routes(
        &scene.traffic, 0, arrive[0] - 2);
    WaterfrontVesselMotion stopped
        = waterfront_traffic_motion_routes(&scene.traffic, 0, arrive[0]);
    WaterfrontVesselMotion dwell_end
        = waterfront_traffic_motion_routes(&scene.traffic, 0, depart[0] - .001);
    WaterfrontVesselMotion leaving
        = waterfront_traffic_motion_routes(&scene.traffic, 0, depart[0] + 2);
    CHECK(Vector3Length(before.velocity) > .05f);
    CHECK(Vector3Length(stopped.velocity) < .01f && Vector3Length(dwell_end.velocity) < .01f);
    CHECK(Vector3Length(leaving.velocity) > 0 && Vector3Length(leaving.velocity) < 3);
    CHECK(Vector3Distance(stopped.position, dwell_end.position) < .01f);
    Vector2 stop_position;
    CHECK(waterfront_route_stops(&scene.traffic, 0, &stop_position, 1) == 3);
    CHECK(hypotf(stopped.position.x - stop_position.x, stopped.position.z - stop_position.y)
        < .01f);
    WaterfrontVesselMotion lap
        = waterfront_traffic_motion_routes(&scene.traffic, 0, 1200);
    CHECK(Vector3Distance(lap.position, waterfront_traffic_motion_routes(&scene.traffic, 0, 0)
                                            .position) < .01f);
    BoatWater water = { .upstream = true };
    WaterfrontTrafficContact contact;
    CHECK(waterfront_traffic_contact_height_scene(&scene,
        (Vector2) { -stopped.position.x, stopped.position.z }, stopped.heading, 2, 1.6f, .75f, -.4f,
        2, water, arrive[0], &contact));
    CHECK(contact.vessel == 0);
    // Stationary trail segments fade out; the same explicit time reloads one identical frame.
    WaterfrontTrafficFrame at_stop, at_leg, repeated;
    waterfront_traffic_evaluate_routes(&scene.traffic, water, arrive[0], &at_stop);
    waterfront_traffic_evaluate_routes(&scene.traffic, water, depart[0] + 30, &at_leg);
    waterfront_traffic_evaluate_routes(&scene.traffic, water, arrive[0], &repeated);
    CHECK(at_stop.vessel_count == 1 && !memcmp(&at_stop, &repeated, sizeof(at_stop)));
    CHECK(at_stop.wake_count < at_leg.wake_count || at_leg.wake_count == 0);
    // A too-short period is rejected rather than truncating a leg or a dwell.
    ferry->period = 60;
    CHECK(!waterfront_route_validate(&scene.traffic, &diagnostic));
    ferry->period = 1200;
    CHECK(waterfront_route_validate(&scene.traffic, &diagnostic));
    return 0;
}

// L3.4: hull height, span station, lift transition and occupancy are one dependency group.
static int check_bridge_safety(void)
{
    const WaterfrontRouteDefinition *defaults = waterfront_route_defaults();
    WaterfrontRouteDiagnostic diagnostic;
    CHECK(waterfront_route_validate_bridge(defaults, WATERFRONT_LIFT_UNDERSIDE,
        WATERFRONT_LIFT_HEIGHT, &diagnostic));
    CHECK(!waterfront_route_validate_bridge(defaults, WATERFRONT_LIFT_UNDERSIDE, 4, &diagnostic));
    CHECK(strstr(diagnostic.message, "insufficient"));

    WaterfrontBridgeWindow windows[WATERFRONT_ROUTE_ACTORS * 2];
    int count = waterfront_route_bridge_windows(defaults, windows, WATERFRONT_ROUTE_ACTORS * 2);
    CHECK(count == 2);
    for (int i = 0; i < count; ++i) {
        WaterfrontBridgeWindow w = windows[i];
        CHECK(w.leave > w.entry && w.crossing > w.entry && w.crossing < w.leave);
        // Fully raised at entry and leave, ramping before/after the hold.
        CHECK(fabsf(waterfront_route_lift(defaults, w.entry) - WATERFRONT_LIFT_HEIGHT) < .1f);
        CHECK(fabsf(waterfront_route_lift(defaults, w.leave) - WATERFRONT_LIFT_HEIGHT) < .1f);
        CHECK(waterfront_route_lift(defaults, w.crossing - 50 - 40 - .1) == 0);
        CHECK(waterfront_route_lift(defaults, w.crossing + 30 + 40 + .1) == 0);
        float partial = waterfront_route_lift(defaults, w.crossing - 50 - 20);
        CHECK(partial > 0 && partial < WATERFRONT_LIFT_HEIGHT);
        // The rendered span pose and the hull contact use the same crossing time.
        Transform3D pose = waterfront_upstream_span_pose(w.crossing);
        CHECK(fabsf(pose.position.y - WATERFRONT_LIFT_HEIGHT) < .1f);
        WaterfrontVesselMotion m = waterfront_traffic_motion(
            WATERFRONT_TRAFFIC_VESSELS + WATERFRONT_UPSTREAM_SCHOONER, w.crossing);
        WaterfrontTrafficContact contact;
        CHECK(waterfront_traffic_contact_height((Vector2) { -m.position.x, m.position.z },
            m.heading, 2, 1.6f, .75f, -.4f, 2, (BoatWater) { .upstream = true }, w.crossing,
            &contact));
    }

    // A slow bridge actor would enter before the span has finished rising.
    WaterfrontRouteDefinition single = { .actor_count = 1 };
    CHECK(waterfront_route_preset("schooner", &single.actors[0]));
    strcpy(single.actors[0].id, "slow_schooner");
    single.actors[0].speed = 0;
    single.actors[0].period = 12000;
    CHECK(!waterfront_route_validate(&single, &diagnostic));
    CHECK(strstr(diagnostic.message, "before the lift is fully raised"));
    // Two tall vessels sharing one passage window are rejected atomically.
    WaterfrontRouteDefinition two = { .actor_count = 2 };
    CHECK(waterfront_route_preset("schooner", &two.actors[0]));
    CHECK(waterfront_route_preset("schooner", &two.actors[1]));
    strcpy(two.actors[0].id, "schooner_one");
    strcpy(two.actors[1].id, "schooner_two");
    CHECK(!waterfront_route_validate(&two, &diagnostic));
    CHECK(strstr(diagnostic.message, "overlapping"));
    // A route that never reaches the bridge cannot claim a lift dependency.
    WaterfrontRouteDefinition malformed = { .actor_count = 1 };
    CHECK(waterfront_route_preset("marina_pass", &malformed.actors[0]));
    strcpy(malformed.actors[0].id, "not_bridge");
    malformed.actors[0].bridge = true;
    CHECK(!waterfront_route_validate(&malformed, &diagnostic));
    CHECK(strstr(diagnostic.message, "does not cross"));
    return 0;
}

int main(void)
{
    CHECK(check_vertical_contacts() == 0);
    CHECK(check_route_definition() == 0);
    CHECK(check_scene_traffic() == 0);
    CHECK(check_ferry_schedule() == 0);
    CHECK(check_bridge_safety() == 0);
    CHECK(check_upstream() == 0);
    if (check_gulls() || check_long_session())
        return 1;
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
        BoatModelSpec spec = waterfront_traffic_spec(i);
        const Mesh3D *mesh = waterfront_traffic_mesh(i);
        CHECK(mesh && mesh->triangle_count > 0 && mesh->triangle_count <= BOAT_MODEL_MAX_TRIANGLES);
        float period = waterfront_traffic_period(i);
        for (int tick = 0; tick <= (int)ceilf(period * 12); ++tick) {
            float time = tick / 12.0f;
            WaterfrontVesselMotion m = waterfront_traffic_motion(i, time);
            float speed = Vector3Length(m.velocity);
            CHECK(speed > .5f && speed < 6.2f && fabsf(m.yaw_rate) < .2f);
            Transform3D pose
                = { .position = m.position, .rotation = { 0, m.heading, 0 }, .scale = { 1, 1, 1 } };
            for (int corner = 0; corner < 4; ++corner) {
                Vector3 p = boat_model_point(pose,
                    (Vector3) { (corner & 1 ? 1 : -1) * (spec.beam * .5f + 1.5f), 0,
                        (corner & 2 ? 1 : -1) * (spec.length * .5f + 1.5f) });
                CHECK(test_river_water((Vector2) { -p.x, p.z }, .5f));
                CHECK(!waterfront_harbor_contact((Vector2) { -p.x, p.z }, .5f, NULL));
            }
        }
    }
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
        float period = waterfront_traffic_period(i);
        WaterfrontVesselMotion start = waterfront_traffic_motion(i, 0);
        WaterfrontVesselMotion end = waterfront_traffic_motion(i, period);
        CHECK(near(start.position, end.position, .002f));
        CHECK(near(start.velocity, end.velocity, .002f));
        CHECK(near(waterfront_traffic_motion(i, -.001f).position,
            waterfront_traffic_motion(i, .001f).position, .02f));
        // Velocity and heading follow the actual curve rather than interpolated key rotations.
        for (int tick = 0; tick < (int)period * 4; ++tick) {
            float t = tick / 4.0f;
            WaterfrontVesselMotion m = waterfront_traffic_motion(i, t);
            Vector3 finite_difference
                = Vector3Scale(Vector3Subtract(waterfront_traffic_motion(i, t + .01f).position,
                                   waterfront_traffic_motion(i, t - .01f).position),
                    50);
            CHECK(near(m.velocity, finite_difference, .03f));
            Vector3 bow = Vector3Transform((Vector3) { 0, 0, -1 }, MatrixRotateY(m.heading));
            CHECK(Vector3DotProduct(bow, Vector3Normalize(m.velocity)) > .9999f);
        }
    }
    // The two river periods repeat together after 4,160 s; all four corridors stay separated.
    for (int tick = 0; tick <= 8320; ++tick) {
        float time = tick * .5f;
        for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i)
            for (int j = i + 1; j < WATERFRONT_TRAFFIC_VESSELS; ++j) {
                BoatModelSpec a = waterfront_traffic_spec(i), b = waterfront_traffic_spec(j);
                WaterfrontVesselMotion ma = waterfront_traffic_motion(i, time);
                WaterfrontVesselMotion mb = waterfront_traffic_motion(j, time);
                CHECK(!footprints_overlap((Vector2) { ma.position.x, ma.position.z }, ma.heading,
                    a.length + 1, a.beam + .5f, (Vector2) { mb.position.x, mb.position.z },
                    mb.heading, b.length + 1, b.beam + .5f));
            }
    }
    WaterfrontTrafficFrame flat, waves, sought;
    for (int t = 0; t <= 320; ++t) {
        waterfront_traffic_evaluate((BoatWater) { 0 }, (float)t, &flat);
        waterfront_traffic_evaluate(boat_water_gentle(), (float)t, &waves);
        waterfront_traffic_evaluate(boat_water_gentle(), 320 - (float)t, &sought);
        waterfront_traffic_evaluate(boat_water_gentle(), (float)t, &sought);
        CHECK(!memcmp(&sought, &waves, sizeof(waves)));
        CHECK(waves.wake_count == WATERFRONT_TRAFFIC_WAKES);
        for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
            CHECK(waves.vessels[i].id == 3000 + i);
            CHECK(fabsf(waves.vessels[i].pose.position.y) < .22f);
            CHECK(flat.vessels[i].pose.position.y == 0);
            CHECK(near(flat.vessels[i].motion.position, waves.vessels[i].motion.position, 1e-6f));
        }
        for (int j = 0; j < waves.wake_count; ++j) {
            const WaterfrontTrafficWake *wake = &waves.wakes[j];
            CHECK(isfinite(wake->pose.rotation.x) && isfinite(wake->pose.rotation.z));
            CHECK(wake->pose.scale.z > 0 && wake->pose.scale.z < 5);
            CHECK(
                test_river_water((Vector2) { -wake->pose.position.x, wake->pose.position.z }, .5f));
            CHECK(wake->color.a == 255);
        }
    }
    // A moving vessel catches a neutral boat; the player is separated and can power away.
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
        WaterfrontVesselMotion ship = waterfront_traffic_motion(i, 60);
        BoatModelSpec spec = waterfront_traffic_spec(i);
        BoatSession boat;
        boat_session_init(&boat, test_world(BOAT_LOCATION_RIVER), (BoatWater) { 0 });
        boat_session_set_tick(&boat, 7200);
        boat.boat.heading = boat.presentation.camera_heading = ship.heading;
        Vector3 p = Vector3Add(
            ship.position, Vector3Scale(Vector3Normalize(ship.velocity), spec.length * .5f + 2.7f));
        boat.boat.position = (Vector2) { -p.x, p.z };
        Vector2 initial = boat.boat.position;
        for (int tick = 0; tick < 120 * 8; ++tick) {
            boat_session_step(&boat, (BoatInput) { 0 });
            CHECK(hulls_clear(&boat.boat, boat_session_time(&boat)));
            CHECK(Vector2Length(boat.boat.velocity) < 12 && fabsf(boat.boat.yaw_velocity) < 5);
        }
        CHECK(Vector2Distance(initial, boat.boat.position) > .5f);
        boat.boat.throttle = 1;
        for (int tick = 0; tick < 120 * 6; ++tick) {
            boat_session_step(&boat, (BoatInput) { 0 });
            CHECK(hulls_clear(&boat.boat, boat_session_time(&boat)));
        }
        // A deliberate beam-on crossing checks the full SAT hull and moving contact torque.
        ship = waterfront_traffic_motion(i, 40);
        Vector2 right = { cosf(ship.heading), sinf(ship.heading) };
        boat_session_init(&boat, test_world(BOAT_LOCATION_RIVER), (BoatWater) { 0 });
        boat_session_set_tick(&boat, 4800);
        boat.boat.position = Vector2Add((Vector2) { -ship.position.x, ship.position.z },
            Vector2Scale(right, spec.beam * .5f + 3));
        boat.boat.heading = ship.heading - PI / 2;
        boat.presentation.camera_heading = boat.boat.heading;
        boat.boat.velocity
            = (Vector2) { sinf(boat.boat.heading) * 8, -cosf(boat.boat.heading) * 8 };
        for (int tick = 0; tick < 120 * 5; ++tick) {
            boat_session_step(&boat, (BoatInput) { 0 });
            CHECK(hulls_clear(&boat.boat, boat_session_time(&boat)));
            CHECK(Vector2Length(boat.boat.velocity) < 16 && fabsf(boat.boat.yaw_velocity) < 6);
        }
    }
    CHECK(waterfront_traffic_mesh(-1) == NULL);
    CHECK(waterfront_traffic_period(-1) == 0);
    puts("harbor_traffic=ok routes=4 pairwise_schedule=4160s visual_wakes=64 moving_contacts=ok "
         "long_session=30d");
    test_worlds_close();
    return 0;
}
