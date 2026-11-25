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
#include "render/boat_minimap.h"
#include "render/boat_view.h"
#include "scenery/boat_scenery.h"

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
            (BoatInput) {
                .steering_rate = boat_steering_rate(boat.wheel, wheel), .throttle_rate = boat_throttle_rate(boat.throttle, throttle) },
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
    CHECK(!boat_scenery_of(&waterfront_outer_map));
    CHECK(!boat_scenery_of(&waterfront_city_river_map));
    WaterfrontScenery scenery;
    CHECK(waterfront_scenery_init(&scenery, location->map));
    CHECK(!scenery.river && !scenery.ground);
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
    CHECK(scenery() == 0);
    puts("boat_archipelago=ok");
    return 0;
}
