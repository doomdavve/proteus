/* Procedural-world P2: the generated route graph agrees with the actual C collision map,
 * a new map has no invisible river actors, and its scenery/chart stay independently owned. */
#include "core/boat.h"
#include "core/boat_archipelago.h"
#include "core/boat_clock.h"
#include "core/boat_steering.h"
#include "core/boat_throttle.h"
#include "core/boat_world.h"
#include "core/waterfront_city.h"
#include "core/waterfront_harbor.h"
#include "core/waterfront_outer.h"
#include "raymath.h"
#include "render/boat_archipelago_view.h"
#include "render/boat_minimap.h"
#include "render/boat_view.h"
#include "render/boat_water_mesh.h"
#include "scenery/boat_scenery.h"
#include "scenery/building_windows.h"

#include "../../assets/geometry/archipelago/routes.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int geometry(void)
{
    const BoatWorldDefinition *location = boat_location_find("archipelago");
    CHECK(location && location->id == BOAT_LOCATION_ARCHIPELAGO);
    CHECK(location == boat_location(BOAT_LOCATION_ARCHIPELAGO));
    CHECK(location->mapped && location->map == &boat_archipelago_map && !location->upstream);
    CHECK(!strcmp(location->title, "SKERRY REACH"));
    CHECK(!waterfront_map_has_river_kit(location->map));
    CHECK(waterfront_map_has_river_kit(&waterfront_outer_map));
    CHECK(waterfront_map_has_river_kit(&waterfront_city_river_map));
    CHECK(!waterfront_kit_ground(location->map) && !waterfront_overhead(location->map));
    CHECK(location->spawn.x == 0 && location->spawn.y == 0);
    BoatWorldResources resources;
    CHECK(boat_world_resources_init(&resources, location));
    CHECK(boat_world_resources_ready(&resources));
    CHECK(resources.map.water_count > 100 && resources.map.water_count < 100000);
    CHECK(resources.map.water_count == location->map->mesh->triangle_count);
    CHECK(resources.map.shore_count > 20);
    CHECK(boat_map_bytes(&resources.map) < 32 * 1024 * 1024);
    for (int z = 0; z >= -60; --z)
        CHECK(boat_map_water(&resources.map, (Vector2) { 0, z }, 3));
    CHECK(!boat_map_water(&resources.map, (Vector2) { 10000, 10000 }, 0));
    size_t place_count;
    const BoatArchipelagoPlace *places = boat_archipelago_places(&place_count);
    CHECK(places && place_count >= 4 && place_count <= 64);
    CHECK(!strcmp(places[0].name, "HOME HARBOUR"));
    for (size_t i = 0; i < place_count; ++i) {
        CHECK(places[i].name && *places[i].name);
        CHECK(isfinite(places[i].position.x) && isfinite(places[i].position.y));
        CHECK(isfinite(places[i].radius) && places[i].radius > 0);
        CHECK(boat_map_water(&resources.map, places[i].position, 3));
        CHECK(boat_map_water_contact(&resources.map, places[i].position, 3, NULL)
            == boat_map_water_contact_reference(&resources.map, places[i].position, 3, NULL));
    }
    const size_t route_count
        = sizeof(archipelago_route_offsets) / sizeof(archipelago_route_offsets[0]) - 1;
    CHECK(route_count >= 3);
    size_t checked = 0;
    for (size_t r = 0; r < route_count; ++r) {
        CHECK(archipelago_route_offsets[r + 1] > archipelago_route_offsets[r] + 1);
        for (unsigned i = archipelago_route_offsets[r] + 1; i < archipelago_route_offsets[r + 1];
            ++i) {
            Vector2 a = archipelago_route_points[i - 1], b = archipelago_route_points[i];
            int steps = (int)ceilf(Vector2Distance(a, b) / 4);
            CHECK(steps > 0);
            for (int k = 0; k <= steps; ++k) {
                Vector2 p = Vector2Lerp(a, b, (float)k / steps);
                if (!boat_map_water(&resources.map, p, 3)) {
                    fprintf(stderr, "route=%zu segment=%u sample=%d at=%g,%g\n", r, i, k, p.x, p.y);
                    CHECK(false);
                }
                ++checked;
            }
        }
    }
    printf("archipelago_geometry=ok places=%zu routes=%zu samples=%zu triangles=%zu bytes=%zu\n",
        place_count, route_count, checked, resources.map.water_count,
        boat_map_bytes(&resources.map));
    boat_world_resources_destroy(&resources);
    return 0;
}

// Follow both sides of the loop with ordinary helm/throttle inputs and the real integrator.
// This is a navigation smoke check, not an artistic or human handling acceptance test.
static int drive(void)
{
    const unsigned edges[] = { 0, 1, 2, 4, 7, 9, 6, 5, 3, 0 };
    const bool reverse[] = { false, false, false, false, false, true, true, true, true, true };
    Vector2 path[64];
    size_t count = 0;
    for (size_t e = 0; e < sizeof(edges) / sizeof(edges[0]); ++e) {
        unsigned start = archipelago_route_offsets[edges[e]];
        unsigned end = archipelago_route_offsets[edges[e] + 1];
        for (unsigned j = 0; j < end - start; ++j) {
            Vector2 p = archipelago_route_points[reverse[e] ? end - j - 1 : start + j];
            if (count && Vector2Distance(path[count - 1], p) < .01f)
                continue;
            CHECK(count < sizeof(path) / sizeof(path[0]));
            path[count++] = p;
        }
    }
    CHECK(count >= 20);
    BoatWorldResources resources;
    CHECK(boat_world_resources_init(&resources, boat_location(BOAT_LOCATION_ARCHIPELAGO)));
    BoatWorld world;
    boat_world_init(&world, &resources, boat_water_gentle());
    Boat boat;
    boat_reset_location(&boat, resources.definition);
    size_t next = 1;
    float travelled = 0, maximum_error = 0;
    uint64_t tick;
    for (tick = 1; tick <= 240000 && next < count; ++tick) {
        Vector2 delta = Vector2Subtract(path[next], boat.position);
        float distance = Vector2Length(delta);
        if (distance < 18) {
            ++next;
            if (next == count)
                break;
            delta = Vector2Subtract(path[next], boat.position);
            distance = Vector2Length(delta);
        }
        float desired = atan2f(delta.x, -delta.y);
        float error = remainderf(desired - boat.heading, 2 * PI);
        float wheel = Clamp(error * 1.2f - boat.yaw_velocity * 1.4f, -.7f, .7f);
        float speed = Vector2Length(boat.velocity);
        float target_speed = fabsf(error) > .35f ? 5 : distance < 70 ? 6 : 10;
        float throttle = Clamp(.2f + (target_speed - speed) * .08f, .03f, .6f);
        Vector2 before = boat.position;
        boat_step_handling(&boat, &world, boat_tick_seconds(tick),
            (BoatInput) { .steering_rate = boat_steering_rate(boat.wheel, wheel),
                .throttle_rate = boat_throttle_rate(boat.throttle, throttle) },
            &boat_handling_explorer);
        CHECK(isfinite(boat.position.x) && isfinite(boat.position.y));
        CHECK(boat_map_water(&resources.map, boat.position, 3));
        CHECK(boat.motion_state != BOAT_CAPSIZED);
        Vector2 leg = Vector2Subtract(path[next], path[next - 1]);
        float along = Clamp(Vector2DotProduct(Vector2Subtract(boat.position, path[next - 1]), leg)
                / Vector2LengthSqr(leg),
            0, 1);
        float departure
            = Vector2Distance(boat.position, Vector2Add(path[next - 1], Vector2Scale(leg, along)));
        maximum_error = fmaxf(maximum_error, departure);
        if (departure >= 40) {
            fprintf(stderr, "drive departure=%g next=%zu tick=%llu position=%g,%g\n", departure,
                next, (unsigned long long)tick, boat.position.x, boat.position.y);
            CHECK(false);
        }
        travelled += Vector2Distance(before, boat.position);
    }
    if (next != count)
        fprintf(stderr, "drive timed out next=%zu/%zu position=%g,%g speed=%g\n", next, count,
            boat.position.x, boat.position.y, Vector2Length(boat.velocity));
    CHECK(next == count);
    CHECK(Vector2Distance(boat.position, resources.definition->spawn) < 18);
    CHECK(travelled > 6000);
    printf("archipelago_drive=ok points=%zu metres=%.1f simulated_seconds=%.2f "
           "maximum_departure=%.2f\n",
        count, travelled, boat_tick_seconds(tick), maximum_error);
    boat_world_resources_destroy(&resources);
    return 0;
}

static int isolation(void)
{
    // A plain water map crossing the old ferry position must not inherit that ferry's contacts.
    static const Vector3 vertices[]
        = { { -800, 0, -800 }, { 800, 0, -800 }, { 800, 0, 800 }, { -800, 0, 800 } };
    static const Triangle3D triangles[]
        = { { { 0, 2, 1 }, { 45, 79, 104, 255 } }, { { 0, 3, 2 }, { 45, 79, 104, 255 } } };
    static const Mesh3D mesh = { vertices, triangles, 2 };
    static const BoatMapDescription description = { &mesh, 2 };
    BoatWorldDefinition location = *boat_location(BOAT_LOCATION_ARCHIPELAGO);
    location.map = &description;
    location.spawn = (Vector2) { -237, -376 };
    BoatWorldResources resources;
    CHECK(boat_world_resources_init(&resources, &location));
    BoatWorld world;
    boat_world_init(&world, &resources, (BoatWater) { 0 });
    CHECK(waterfront_harbor_contact_height_scene(
        NULL, location.spawn, .1f, -1, 2, world.water, 0, NULL));
    Boat boat;
    boat_reset_location(&boat, &location);
    boat.velocity = (Vector2) { 0, -8 };
    boat.throttle = .5f;
    for (int tick = 1; tick <= 120; ++tick)
        boat_step(&boat, &world, tick * BOAT_STEP, (BoatInput) { 0 });
    CHECK(boat.position.y < location.spawn.y - 5);
    BoatSceneDefinition scene = { .version = BOAT_SCENE_VERSION };
    CHECK(boat_scene_clear(&boat, &world, &scene, 0, .1f));
    scene.berths_present = true;
    scene.berth_count = 1;
    CHECK(!boat_scene_clear(&boat, &world, &scene, 0, .1f));
    BoatPresentation presentation = { 0 };
    BoatViewState view = { .boat = boat };
    WaterfrontSceneFrame frame;
    boat_scene_frame(&view, &presentation, &world, BOAT_CAMERA_DEFAULT, &frame);
    CHECK(frame.map_only && !frame.boating_basin && frame.player_active);
    CHECK(!frame.harbor.berth_count && !frame.harbor.line_count && !frame.harbor.module_count);
    CHECK(!frame.traffic.vessel_count && !frame.traffic.wake_count && !frame.traffic.lift);
    CHECK(!frame.gull_count && !frame.lamp_count);
    CHECK(frame.bridge.scale.x == 0 && frame.harbor.ferry.scale.x == 0);
    CHECK(world.water.upstream == false);
    boat_world_resources_destroy(&resources);
    puts("archipelago_isolation=ok");
    return 0;
}

static int life(void)
{
    const BoatSceneDefinition *scene = boat_archipelago_life();
    WaterfrontRouteDiagnostic route_diagnostic;
    if (!waterfront_route_validate(&scene->traffic, &route_diagnostic)) {
        fprintf(stderr, "%s: %s\n", route_diagnostic.field, route_diagnostic.message);
        CHECK(false);
    }
    WaterfrontFlockDiagnostic flock_diagnostic;
    CHECK(waterfront_flock_validate(&scene->gulls, &flock_diagnostic));
    BoatWorldResources resources;
    CHECK(boat_world_resources_init(&resources, boat_location(BOAT_LOCATION_ARCHIPELAGO)));
    BoatWorld world;
    boat_world_init(&world, &resources, (BoatWater) { 0 });
    BoatViewState view = { 0 };
    boat_reset_location(&view.boat, resources.definition);
    BoatPresentation presentation = { 0 };
    WaterfrontSceneFrame frame;
    boat_scene_frame(&view, &presentation, &world, BOAT_CAMERA_DEFAULT, &frame);
    CHECK(frame.traffic.vessel_count == 4 && !frame.traffic.lift);
    CHECK(frame.traffic.routes == &scene->traffic);
    CHECK(frame.gull_count == 26 && !frame.lamp_count);
    CHECK(frame.harbor.berth_count == 4 && frame.harbor.omit_ferry);
    CHECK(frame.harbor.line_count == 8);
    CHECK(frame.harbor.ferry.scale.x == 0);
    size_t moving = 0, stopped = 0;
    for (unsigned i = 0; i < scene->traffic.actor_count; ++i) {
        BoatModelSpec spec = waterfront_traffic_spec_routes(&scene->traffic, (int)i);
        double period = waterfront_route_period(&scene->traffic, (int)i);
        CHECK(period > 0 && period <= 1400);
        for (double t = -period; t <= period; t += 2) {
            WaterfrontVesselMotion motion = waterfront_route_motion(&scene->traffic, (int)i, t);
            Vector2 p = { -motion.position.x, motion.position.z };
            float clearance = hypotf(spec.length * .55f, spec.beam * .55f) + .5f;
            if (!boat_map_water(&resources.map, p, clearance)) {
                fprintf(stderr, "traffic=%s time=%g x=%g z=%g\n", scene->traffic.actors[i].id, t,
                    p.x, p.y);
                CHECK(false);
            }
            CHECK(isfinite(motion.heading) && isfinite(motion.yaw_rate));
            float speed = Vector3Length(motion.velocity);
            moving += speed > .1f;
            stopped += speed < .01f;
            WaterfrontTrafficContact contact;
            CHECK(waterfront_traffic_contact_height_scene(scene, p, motion.heading, 1.8f, 1.8f,
                .75f, -.2f, 1, (BoatWater) { 0 }, t, &contact));
            CHECK(contact.vessel == (int)i);
            CHECK(!waterfront_traffic_contact_height_scene(scene, p, motion.heading, 1.8f, 1.8f,
                .75f, 100, 110, (BoatWater) { 0 }, t, &contact));
        }
    }
    CHECK(moving > 1000 && stopped > 20);
    size_t count;
    const BoatArchipelagoResident *residents = boat_archipelago_residents(&count);
    CHECK(count >= 4 && count <= 16);
    for (size_t i = 0; i < count; ++i) {
        const BoatArchipelagoResident *r = &residents[i];
        CHECK(r->period > 0 && Vector3Distance(r->start, r->end) > 2);
        for (int j = 0; j <= 16; ++j) {
            Vector3 p = Vector3Lerp(r->start, r->end, j / 16.0f);
            CHECK(!boat_map_water(&resources.map, (Vector2) { p.x, p.z }, 0));
            CHECK(isfinite(p.y) && p.y > 0);
        }
    }
    WaterfrontSceneFrame a = { 0 }, b = { 0 }, other = { 0 };
    Vector3 player = { 0 };
    boat_archipelago_evaluate((BoatWater) { 0 }, 73, player, player, &a);
    boat_archipelago_evaluate((BoatWater) { 0 }, 5000, player, player, &other);
    boat_archipelago_evaluate((BoatWater) { 0 }, 73, player, player, &b);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0); // Pause or seeking never accumulates actor state.
    CHECK(a.scenery_count > 0 && a.gull_count == 26 && a.traffic.wake_count > 0);
    CHECK(a.scenery_count == b.scenery_count);
    CHECK(memcmp(a.gulls, other.gulls, sizeof(a.gulls)) != 0);
    for (int i = 0; i < a.gull_count; ++i) {
        Vector3 p = a.gulls[i][0].position;
        CHECK(p.y >= boat_archipelago_ground((Vector2) { -p.x, p.z }) + 3.99f);
    }
    boat_world_resources_destroy(&resources);
    printf("archipelago_life=ok vessels=%u gulls=%d residents=%zu moving=%zu stopped=%zu\n",
        scene->traffic.actor_count, a.gull_count, count, moving, stopped);
    return 0;
}

static int marina_and_clock(void)
{
    BoatWorldResources resources;
    CHECK(boat_world_resources_init(&resources, boat_location(BOAT_LOCATION_ARCHIPELAGO)));
    BoatWorld world;
    boat_world_init(&world, &resources, (BoatWater) { 0 });
    const BoatSceneDefinition *scene = boat_archipelago_life();
    BoatModelSpec fishing = waterfront_route_spec(&scene->traffic, 1);
    CHECK(fishing.mast_height > 6 && fishing.cabin_offset > 0);
    CHECK(boat_model_spec_valid(fishing));
    const Mesh3D *mesh = waterfront_route_mesh(&scene->traffic, 1);
    CHECK(mesh && mesh->triangle_count > 0 && mesh->triangle_count <= BOAT_MODEL_MAX_TRIANGLES);
    Transform3D rest = { .scale = { 1, 1, 1 } };
    float low, high;
    boat_model_vertical_bounds(fishing, rest, &low, &high);
    for (size_t i = 0; i < mesh->triangle_count * 3; ++i) {
        Vector3 v = mesh->vertices[i];
        CHECK(isfinite(v.x) && isfinite(v.y) && isfinite(v.z));
        CHECK(v.y >= low && v.y <= high);
        CHECK(fabsf(v.x) <= fishing.beam * .5f + .4f);
        CHECK(fabsf(v.z) <= fishing.length * .5f + .8f);
    }
    WaterfrontVesselMotion motion = waterfront_route_motion(&scene->traffic, 1, 0);
    printf("archipelago_fishing position=%g,%g heading=%g triangles=%zu\n", -motion.position.x,
        motion.position.z, motion.heading * RAD2DEG, mesh->triangle_count);
    CHECK(scene->berths_present && scene->berth_count == 4);
    WaterfrontSceneFrame frame = { 0 };
    boat_archipelago_evaluate(world.water, 0, (Vector3) { 0 }, (Vector3) { 0 }, &frame);
    for (unsigned i = 0; i < scene->berth_count; ++i) {
        const BoatSceneBerth *berth = &scene->berths[i];
        BoatModelSpec spec = boat_scene_berth_resolve(berth);
        CHECK(frame.harbor.visible[i]);
        CHECK(fabsf(frame.harbor.boats[i].position.x + berth->x) < .001f);
        CHECK(fabsf(frame.harbor.boats[i].position.z - berth->z) < .001f);
        CHECK(waterfront_harbor_berths_contact_height_scene(
            scene, (Vector2) { berth->x, berth->z }, 0, -1, 2, world.water, 0, NULL));
        CHECK(!waterfront_harbor_berths_contact_height_scene(
            scene, (Vector2) { berth->x, berth->z }, 0, 20, 22, world.water, 0, NULL));
        // Sample the full conservative contact rectangle, including wave excursion.
        for (int x = -1; x <= 1; ++x)
            for (int z = -1; z <= 1; ++z) {
                Vector3 p = boat_model_point(frame.harbor.boats[i],
                    (Vector3) { x * (spec.beam * .5f + .45f), 0, z * (spec.length * .5f + .9f) });
                if (!boat_map_water(&resources.map, (Vector2) { -p.x, p.z }, .1f))
                    fprintf(stderr, "marina %u corner %d,%d at %g,%g\n", i, x, z, -p.x, p.z);
                CHECK(boat_map_water(&resources.map, (Vector2) { -p.x, p.z }, .1f));
            }
    }
    for (int z = 10; z >= -80; --z)
        CHECK(!waterfront_harbor_berths_contact_height_scene(
            scene, (Vector2) { 0, z }, 12, -1, 3, world.water, 0, NULL));
    // No imported ferry collision in this local marina.
    CHECK(!waterfront_harbor_berths_contact_height_scene(
        scene, (Vector2) { -237, -376 }, 1, -1, 3, world.water, 0, NULL));
    BoatViewState view = { 0 };
    const double times[] = { 0, 225, 450, 675, 900, 112.5, -225, 225 };
    const float hours[] = { 12, 18, 0, 6, 12, 15, 6, 18 };
    for (unsigned i = 0; i < sizeof(times) / sizeof(times[0]); ++i) {
        BoatViewProps props = boat_archipelago_lighting(times[i]);
        view.props = &props;
        float hour;
        double glitter, expected;
        CHECK(boat_view_lighting_clock(&view, &hour, &glitter));
        CHECK(hour == hours[i]);
        CHECK(waterfront_glitter_clock(12 + times[i] * 24 / 900, 900, &expected));
        CHECK(fabs(glitter - expected) < 1e-9);
    }
    BoatViewProps invalid = boat_archipelago_lighting(NAN);
    view.props = &invalid;
    float hour;
    double glitter;
    CHECK(!boat_view_lighting_clock(&view, &hour, &glitter));
    boat_world_resources_destroy(&resources);
    puts("archipelago_marina_and_clock=ok");
    return 0;
}

static int lanterns(void)
{
    size_t count;
    const BoatArchipelagoLantern *lamps = boat_archipelago_lanterns(&count);
    CHECK(count >= 2 && count <= 8);
    for (size_t i = 0; i < count; ++i) {
        CHECK(lamps[i].width > 2 && lamps[i].width < 6);
        CHECK(lamps[i].position.y > 15);
    }
    const float hours[] = { 12, 17.99f, 18, 23, 0, 5.99f, 6, 12 };
    for (size_t h = 0; h < sizeof(hours) / sizeof(hours[0]); ++h) {
        WaterfrontSceneFrame frame = { 0 };
        CHECK(waterfront_lighting_hour(hours[h], &frame.lighting));
        boat_archipelago_lanterns_evaluate(0, &frame);
        bool night = hours[h] >= 18 || hours[h] < 6;
        CHECK(frame.scenery_count == (night ? (int)count * 4 : 0));
        for (int i = 0; i < frame.scenery_count; ++i)
            CHECK(frame.scenery[i].emissive);
    }
    WaterfrontSceneFrame first = { 0 }, later = { 0 }, repeat = { 0 };
    first.lighting = later.lighting = repeat.lighting
        = waterfront_lighting(WATERFRONT_DAYLIGHT_DUSK);
    boat_archipelago_lanterns_evaluate(0, &first);
    boat_archipelago_lanterns_evaluate(2, &later);
    boat_archipelago_lanterns_evaluate(8, &repeat);
    CHECK(first.scenery_count == (int)count * 4);
    CHECK(first.scenery[0].color.r > later.scenery[0].color.r);
    CHECK(first.scenery[1].color.r < later.scenery[1].color.r);
    CHECK(memcmp(first.scenery, repeat.scenery, sizeof(first.scenery)) == 0);
    repeat.scenery_count = 0;
    boat_archipelago_lanterns_evaluate(NAN, &repeat);
    CHECK(repeat.scenery_count == 0);
    puts("archipelago_lanterns=ok dusk_dawn=ok rotating_lens=ok seek=ok");
    return 0;
}

static bool surface_contains(const Mesh3D *mesh, Vector3 p)
{
    for (size_t i = 0; i < mesh->triangle_count; ++i) {
        const Triangle3D *t = &mesh->triangles[i];
        Vector3 a = mesh->vertices[t->vertices[0]], b = mesh->vertices[t->vertices[1]],
                c = mesh->vertices[t->vertices[2]];
        if (CheckCollisionPointTriangle((Vector2) { p.x, p.z }, (Vector2) { a.x, a.z },
                (Vector2) { b.x, b.z }, (Vector2) { c.x, c.z }))
            return true;
    }
    return false;
}

static int pier_surface(void)
{
    const BoatMapDescription *surface = boat_water_render_map(&boat_archipelago_map);
    CHECK(surface == &boat_archipelago_water_surface);
    CHECK(surface->mesh->triangle_count > boat_archipelago_map.mesh->triangle_count);
    CHECK(boat_water_render_map(NULL) == NULL);
    CHECK(boat_water_render_map(&waterfront_outer_map) == &waterfront_outer_map);
    size_t samples = 0;
    for (size_t i = boat_archipelago_map.mesh->triangle_count; i < surface->mesh->triangle_count;
        ++i) {
        const Triangle3D *t = &surface->mesh->triangles[i];
        Vector3 a = surface->mesh->vertices[t->vertices[0]],
                b = surface->mesh->vertices[t->vertices[1]],
                c = surface->mesh->vertices[t->vertices[2]];
        if (Vector3Length(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a))) < .1f)
            continue;
        Vector3 p = Vector3Scale(Vector3Add(Vector3Add(a, b), c), 1.0f / 3);
        CHECK(!surface_contains(boat_archipelago_map.mesh, p));
        BoatWaterMeshCache cache = { 0 };
        const Mesh3D *patch
            = boat_water_patch(&cache, &boat_archipelago_map, (Vector2) { p.x, p.z });
        CHECK(patch && surface_contains(patch, p));
        patch = boat_water_patch_arcade(&cache, &boat_archipelago_map, (Vector2) { p.x, p.z });
        CHECK(patch && surface_contains(patch, p));
        boat_water_mesh_cache_destroy(&cache);
        ++samples;
    }
    CHECK(samples > 10);
    puts("archipelago_pier_surface=ok collision=solid gpu_patches=covered");
    return 0;
}

static int house_windows(void)
{
    size_t count;
    const BoatArchipelagoWindow *windows = boat_archipelago_windows(&count);
    CHECK(count > 40);
    int evening = 0, late = 0;
    const float hours[] = { 12, 18, 21, 3, 6, 12 };
    for (size_t h = 0; h < sizeof(hours) / sizeof(hours[0]); ++h) {
        WaterfrontSceneFrame frame = { 0 };
        CHECK(waterfront_lighting_hour(hours[h], &frame.lighting));
        boat_archipelago_windows_evaluate(&frame);
        int emitted = 0;
        for (size_t i = 0; i < count; ++i) {
            const BoatArchipelagoWindow *w = &windows[i];
            float dx = w->position.x, dz = w->position.z;
            if (dx * dx + dz * dz > BUILDING_WINDOW_NEAR_RANGE * BUILDING_WINDOW_NEAR_RANGE)
                continue;
            bool lit = building_window_lit(
                BUILDING_PROFILE_RESIDENTIAL, w->seed, waterfront_window_minute(&frame.lighting));
            Color expected = waterfront_window_tint(
                building_window_glass(w->seed), lit ? 1.0f : 0.0f, &frame.lighting);
            CHECK(emitted + 3 <= frame.scenery_count);
            CHECK(Vector3Equals(w->position, frame.scenery[emitted].transform.position));
            CHECK(ColorIsEqual(expected, frame.scenery[emitted].color));
            CHECK(frame.scenery[emitted].emissive);
            emitted += 3;
            if (hours[h] == 21 && lit)
                ++evening;
            if (hours[h] == 3 && lit)
                ++late;
        }
        CHECK(emitted == frame.scenery_count);
    }
    CHECK(evening > 0 && late < evening);
    printf(
        "archipelago_house_windows=ok outer_schedule=shared evening=%d late=%d\n", evening, late);
    return 0;
}

static int scenery(void)
{
    const BoatWorldDefinition *location = boat_location(BOAT_LOCATION_ARCHIPELAGO);
    boat_scenery_unload();
    CHECK(!boat_scenery_of(&waterfront_outer_map));
    CHECK(boat_scenery_load_location(location, PROTEUS_SOURCE_DIR "/assets/"));
    const BoatMapDescription *full = boat_scenery_of(location->map);
    CHECK(full && full->mesh && full != location->map);
    CHECK(full->mesh->triangle_count > location->map->mesh->triangle_count);
    CHECK(boat_map_leading_water(full->mesh) == location->map->mesh->triangle_count);
    for (size_t i = location->map->mesh->triangle_count; i < full->bridge_first_triangle; ++i)
        for (int j = 0; j < 3; ++j) {
            Vector3 p = full->mesh->vertices[full->mesh->triangles[i].vertices[j]];
            CHECK(boat_archipelago_ground((Vector2) { -p.x, p.z }) >= p.y - .02f);
        }
    CHECK(!boat_scenery_of(&waterfront_outer_map));
    CHECK(!boat_scenery_of(&waterfront_city_river_map));
    WaterfrontScenery scenery;
    CHECK(waterfront_scenery_init(&scenery, location->map));
    CHECK(!scenery.river && !scenery.ground);
    CHECK(scenery.ready && scenery.tiles.tiles);
    CHECK(!scenery.count && !scenery.boxes && !scenery.bounds);
    BoatMinimap minimap;
    CHECK(boat_minimap_init(&minimap, location));
    const Image *chart
        = boat_minimap_compose(&minimap, BOAT_MINIMAP_CHART, NULL, location->spawn, 0);
    CHECK(chart && chart->data && chart->width > 0 && chart->height > 0);
    CHECK(boat_minimap_compose(&minimap, BOAT_MINIMAP_CORNER, NULL, location->spawn, 0));
    boat_minimap_destroy(&minimap);
    waterfront_scenery_destroy(&scenery);
    printf("archipelago_scenery=ok triangles=%zu bytes=%zu\n", full->mesh->triangle_count,
        boat_scenery_bytes(location->map));
    boat_scenery_unload();
    return 0;
}

int main(void)
{
    SetTraceLogLevel(LOG_WARNING);
    CHECK(geometry() == 0);
    CHECK(drive() == 0);
    CHECK(isolation() == 0);
    CHECK(life() == 0);
    CHECK(marina_and_clock() == 0);
    CHECK(lanterns() == 0);
    CHECK(pier_surface() == 0);
    CHECK(house_windows() == 0);
    CHECK(scenery() == 0);
    puts("boat_archipelago=ok");
    return 0;
}
