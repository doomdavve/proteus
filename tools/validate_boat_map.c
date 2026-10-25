/* A1.1/A1.2: explicit map descriptions, caller-owned collision caches and the world resources
 * that bind them. Independent owners initialize, query and destroy without sharing state, the
 * river answers exactly as the process-global cache did before ownership moved, and worlds,
 * fleets and their copies only borrow their owner. */
#include "boat.h"
#include "boat_fleet.h"
#include "boat_fleet_bots.h"
#include "boat_map.h"
#include "waterfront_city.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

// Recorded at 413460d from boat_map_water_contact() before the A1.1 ownership change.
#define RIVER_QUERY_DIGEST UINT64_C(0x63225c36181d381d)
#define RIVER_QUERY_WATER 20356

// Synthetic 10 m square of water in map east/Z, with one raised land triangle after it.
static const Vector3 square_vertices[] = {
    { 0, 0, 0 }, { 10, 0, 0 }, { 10, 0, 10 }, { 0, 0, 10 }, { 20, 2, 0 }, { 20, 2, 10 } };
static const Triangle3D square_triangles[] = {
    { { 0, 2, 1 }, WHITE }, { { 0, 3, 2 }, WHITE }, { { 1, 2, 4 }, GREEN } };
static const Mesh3D square_mesh = { square_vertices, square_triangles, 3 };
static const BoatMapDescription square = { &square_mesh, 3 };
// Only raised land: no leading water, so preparation must fail.
static const Mesh3D land_mesh = { square_vertices, square_triangles + 2, 1 };
static const BoatMapDescription land = { &land_mesh, 1 };
static const Mesh3D empty_mesh = { square_vertices, square_triangles, 0 };
static const BoatMapDescription empty = { &empty_mesh, 0 };
static const BoatMapDescription missing_mesh = { NULL, 0 };

static void mix(uint64_t *hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        *hash ^= bytes[i];
        *hash *= UINT64_C(1099511628211);
    }
}

// Clearance/normal answers over the crop's bounds plus a 40 m margin, in simulation X/Z.
static uint64_t river_digest(const BoatMap *map, int *water)
{
    const Mesh3D *mesh = waterfront_city_map()->mesh;
    float lo_x = 1e9f, hi_x = -1e9f, lo_z = 1e9f, hi_z = -1e9f;
    for (size_t i = 0; i < mesh->triangle_count; ++i)
        for (int j = 0; j < 3; ++j) {
            Vector3 v = mesh->vertices[mesh->triangles[i].vertices[j]];
            lo_x = fminf(lo_x, -v.x);
            hi_x = fmaxf(hi_x, -v.x);
            lo_z = fminf(lo_z, v.z);
            hi_z = fmaxf(hi_z, v.z);
        }
    const float clearances[] = { 0, .5f, 3, 12 };
    uint64_t hash = UINT64_C(1469598103934665603);
    *water = 0;
    for (int iz = 0; iz <= 160; ++iz)
        for (int ix = 0; ix <= 160; ++ix)
            for (int c = 0; c < 4; ++c) {
                Vector2 p = { lo_x - 40 + (hi_x - lo_x + 80) * (float)ix / 160.0f,
                    lo_z - 40 + (hi_z - lo_z + 80) * (float)iz / 160.0f };
                Vector2 normal = { 7, 7 };
                bool inside = boat_map_water_contact(map, p, clearances[c], &normal);
                mix(&hash, &inside, sizeof(inside));
                mix(&hash, &normal, sizeof(normal));
                *water += inside;
            }
    return hash;
}

static bool zeroed(const BoatMap *map)
{
    return !map->description && !map->shore && !map->shore_count && !map->water_count;
}

static int definitions(void)
{
    const BoatWorldDefinition *basin = boat_location(BOAT_LOCATION_BASIN);
    const BoatWorldDefinition *river = boat_location(BOAT_LOCATION_RIVER);
    CHECK(!basin->mapped && !basin->map);
    CHECK(river->mapped && river->map == waterfront_city_map());
    CHECK(river->map->mesh == waterfront_city_mesh());
    size_t bridge = river->map->bridge_first_triangle;
    CHECK(bridge > 0 && bridge < river->map->mesh->triangle_count);
    CHECK(!boat_map_is_bridge_triangle(river->map, bridge - 1));
    CHECK(boat_map_is_bridge_triangle(river->map, bridge));
    CHECK(waterfront_city_is_bridge_triangle(bridge) && !waterfront_city_is_bridge_triangle(0));
    return 0;
}

static int failures(void)
{
    BoatMap map = { 0 };
    Vector2 normal = { 7, 7 };
    boat_map_destroy(&map);
    boat_map_destroy(&map);
    CHECK(zeroed(&map) && !boat_map_ready(&map) && !boat_map_ready(NULL));
    CHECK(!boat_map_water(&map, (Vector2) { -5, 5 }, 0));
    CHECK(!boat_map_water_contact(NULL, (Vector2) { -5, 5 }, 0, &normal));
    CHECK(normal.x == 7 && normal.y == 7);
    const BoatMapDescription *invalid[] = { NULL, &missing_mesh, &empty, &land };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        // A stale prior value must not survive a failed preparation.
        map = (BoatMap) { &square, NULL, 9, 9 };
        CHECK(!boat_map_init(&map, invalid[i]));
        CHECK(zeroed(&map));
        CHECK(!boat_map_water_contact(&map, (Vector2) { -5, 5 }, 0, &normal));
        boat_map_destroy(&map);
    }
    return 0;
}

static int synthetic(const BoatMap *map)
{
    CHECK(boat_map_ready(map) && map->description == &square);
    CHECK(map->water_count == 2 && map->shore_count == 4);
    // Simulation X is the reflected map east coordinate.
    CHECK(boat_map_water(map, (Vector2) { -5, 5 }, 0));
    CHECK(boat_map_water(map, (Vector2) { -5, 5 }, 4.9f));
    CHECK(!boat_map_water(map, (Vector2) { -5, 5 }, 5.1f));
    CHECK(!boat_map_water(map, (Vector2) { 5, 5 }, 0));
    CHECK(!boat_map_water(map, (Vector2) { -15, 5 }, 0)); // Raised land is not water.
    Vector2 normal = { 0 };
    CHECK(!boat_map_water_contact(map, (Vector2) { -5, 1 }, 2, &normal));
    CHECK(normal.x == 0 && normal.y == 1);
    CHECK(!boat_map_water_contact(map, (Vector2) { -1, 5 }, 2, &normal));
    CHECK(normal.x == -1 && normal.y == 0);
    return 0;
}

static int owners(void)
{
    const BoatWorldDefinition *river = boat_location(BOAT_LOCATION_RIVER);
    BoatMap a, b, s;
    CHECK(boat_map_init(&a, river->map));
    CHECK(boat_map_init(&s, &square));
    CHECK(boat_map_init(&b, river->map));
    CHECK(a.shore != b.shore && a.water_count == b.water_count && a.shore_count == b.shore_count);
    CHECK(memcmp(a.shore, b.shore, a.shore_count * sizeof(*a.shore)) == 0);
    CHECK(a.water_count != s.water_count);
    // Each owner answers from its own description.
    CHECK(boat_map_water(&a, river->spawn, 2) && !boat_map_water(&s, river->spawn, 0));
    if (synthetic(&s))
        return 1;

    boat_map_destroy(&a);
    CHECK(zeroed(&a) && !boat_map_water(&a, river->spawn, 2));
    int water = 0;
    CHECK(river_digest(&b, &water) == RIVER_QUERY_DIGEST && water == RIVER_QUERY_WATER);
    if (synthetic(&s))
        return 1;

    CHECK(boat_map_init(&a, river->map));
    CHECK(a.shore_count == b.shore_count);
    CHECK(memcmp(a.shore, b.shore, a.shore_count * sizeof(*a.shore)) == 0);
    boat_map_destroy(&s);
    boat_map_destroy(&b);
    CHECK(river_digest(&a, &water) == RIVER_QUERY_DIGEST && water == RIVER_QUERY_WATER);
    CHECK(boat_map_init(&s, &square));
    if (synthetic(&s))
        return 1;
    boat_map_destroy(&a);
    boat_map_destroy(&s);
    return 0;
}

static bool zeroed_resources(const BoatWorldResources *r)
{
    return !r->definition && zeroed(&r->map);
}

static int resources(void)
{
    const BoatWorldDefinition *basin = boat_location(BOAT_LOCATION_BASIN);
    const BoatWorldDefinition *river = boat_location(BOAT_LOCATION_RIVER);
    BoatWorldResources r = { 0 };
    boat_world_resources_destroy(&r);
    CHECK(zeroed_resources(&r) && !boat_world_resources_ready(&r));
    CHECK(!boat_world_resources_ready(NULL));
    // The basin is a valid empty-map resource.
    CHECK(boat_world_resources_init(&r, basin) && boat_world_resources_ready(&r));
    CHECK(r.definition == basin && zeroed(&r.map));
    boat_world_resources_destroy(&r);
    boat_world_resources_destroy(&r);
    CHECK(zeroed_resources(&r));
    // A mapped flag that disagrees with the description, or a map that cannot be prepared,
    // fails at initialization and leaves no stale fields.
    BoatWorldDefinition unmapped_river = *river, mapped_basin = *basin, land_river = *river;
    unmapped_river.mapped = false;
    mapped_basin.mapped = true;
    land_river.map = &land;
    const BoatWorldDefinition *invalid[] = { NULL, &unmapped_river, &mapped_basin, &land_river };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        r = (BoatWorldResources) { river, { &square, NULL, 9, 9 } };
        CHECK(!boat_world_resources_init(&r, invalid[i]));
        CHECK(zeroed_resources(&r) && !boat_world_resources_ready(&r));
        boat_world_resources_destroy(&r);
    }
    // A mapped owner is ready only with its own description prepared.
    CHECK(boat_world_resources_init(&r, river) && boat_world_resources_ready(&r));
    CHECK(r.map.description == river->map);
    int water = 0;
    uint64_t digest = river_digest(&r.map, &water);
    printf("river_query_digest=%016" PRIx64 " water=%d\n", digest, water);
    CHECK(digest == RIVER_QUERY_DIGEST && water == RIVER_QUERY_WATER);
    BoatWorldResources stale = r;
    stale.map.description = &square;
    CHECK(!boat_world_resources_ready(&stale));
    boat_world_resources_destroy(&r);
    CHECK(zeroed_resources(&r));
    return 0;
}

typedef struct {
    BoatWorld world;
    Boat boat;
} Run;

// Full throttle at POSITION, heading east (simulation +X), weaving gently.
static void run_start(Run *run, const BoatWorldResources *resources, Vector2 position)
{
    *run = (Run) { 0 };
    boat_world_init(&run->world, resources, (BoatWater) { 0 });
    boat_reset_location(&run->boat, resources->definition);
    run->boat.position = position;
    run->boat.heading = PI / 2;
}

static void run_step(Run *run, uint64_t tick)
{
    double now = boat_tick_seconds(tick + 1);
    boat_step(&run->boat, &run->world, now, (BoatInput) { tick % 240 < 120 ? .3f : -.3f, 1 });
}

static uint64_t boat_digest(const Boat *boat)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    mix(&hash, boat, sizeof(*boat));
    return hash;
}

// Two river owners and a synthetic one step interleaved: each world answers from its own
// borrowed map, and the river runs match a run made alone, bit for bit.
static int worlds(void)
{
    enum { TICKS = 3600 };
    const BoatWorldDefinition *river = boat_location(BOAT_LOCATION_RIVER);
    BoatWorldDefinition pond_definition = *river;
    pond_definition.map = &square;
    pond_definition.spawn = (Vector2) { -5, 5 };
    BoatWorldResources first, second, pond;
    CHECK(boat_world_resources_init(&first, river));
    CHECK(boat_world_resources_init(&second, river));
    CHECK(boat_world_resources_init(&pond, &pond_definition));
    CHECK(first.map.shore != second.map.shore);

    // Start 25 m short of the east bank from the spawn.
    Vector2 start = river->spawn;
    while (boat_map_water(&first.map, (Vector2) { start.x + 25, start.y }, 0))
        start.x += 1;
    Run alone;
    run_start(&alone, &first, start);
    for (uint64_t t = 0; t < TICKS; ++t)
        run_step(&alone, t);
    // 30 s at full throttle ends held at the bank, so the map shapes the trace.
    CHECK(alone.boat.position.x < start.x + 25);

    Run a, b, shared, small;
    run_start(&a, &first, start);
    run_start(&b, &second, start);
    run_start(&shared, &first, start);
    run_start(&small, &pond, pond_definition.spawn);
    CHECK(a.world.resources == &first && b.world.resources == &second);
    Run copy = { 0 };
    for (uint64_t t = 0; t < TICKS; ++t) {
        run_step(&small, t);
        run_step(&b, t);
        run_step(&a, t);
        run_step(&shared, t);
        if (t == TICKS / 2)
            copy = a; // A value copy borrows the same owner.
        if (t > TICKS / 2)
            run_step(&copy, t);
    }
    CHECK(copy.world.resources == &first);
    uint64_t expected = boat_digest(&alone.boat);
    CHECK(boat_digest(&a.boat) == expected && boat_digest(&b.boat) == expected);
    CHECK(boat_digest(&shared.boat) == expected && boat_digest(&copy.boat) == expected);
    // The pond's own 10 m shore holds its hull.
    CHECK(fabsf(small.boat.position.x) < 20 && fabsf(small.boat.position.y) < 20);
    printf("interleaved_worlds=ok river_end=%.3f,%.3f pond_end=%.3f,%.3f\n", alone.boat.position.x,
        alone.boat.position.y, small.boat.position.x, small.boat.position.y);

    // Fleets: a checkpoint restored into a fleet on another owner continues identically, and
    // neither the copy nor the restore touches either owner.
    static BoatFleet fleet, other, copied;
    static BoatFleetState checkpoint;
    CHECK(boat_fleet_init(&fleet, &first, (BoatWater) { 0 }, BOAT_OBJECT_LIFE_TICKS, 1.8f));
    CHECK(boat_fleet_init(&other, &second, (BoatWater) { 0 }, BOAT_OBJECT_LIFE_TICKS, 1.8f));
    CHECK(!boat_fleet_init(&copied, NULL, (BoatWater) { 0 }, BOAT_OBJECT_LIFE_TICKS, 1.8f));
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        CHECK(boat_fleet_request_spawn(&fleet, i));
    BoatFleetBots bots = { 0 }, other_bots, copied_bots;
    BoatInput inputs[BOAT_FLEET_SLOTS];
    for (int t = 0; t < 1200; ++t) {
        boat_fleet_bots_drive(&bots, &fleet, inputs);
        CHECK(boat_fleet_step(&fleet, inputs));
    }
    boat_fleet_save(&fleet, &checkpoint);
    boat_fleet_restore(&other, &checkpoint);
    copied = fleet;
    other_bots = copied_bots = bots;
    for (int t = 0; t < 1200; ++t) {
        boat_fleet_bots_drive(&bots, &fleet, inputs);
        CHECK(boat_fleet_step(&fleet, inputs));
        boat_fleet_bots_drive(&other_bots, &other, inputs);
        CHECK(boat_fleet_step(&other, inputs));
        boat_fleet_bots_drive(&copied_bots, &copied, inputs);
        CHECK(boat_fleet_step(&copied, inputs));
    }
    CHECK(other.world.resources == &second && copied.world.resources == &first);
    uint64_t fleet_hash = boat_fleet_hash(&fleet.state);
    CHECK(boat_fleet_hash(&other.state) == fleet_hash);
    CHECK(boat_fleet_hash(&copied.state) == fleet_hash);
    CHECK(boat_world_resources_ready(&first) && boat_world_resources_ready(&second));

    boat_world_resources_destroy(&pond);
    boat_world_resources_destroy(&first);
    boat_world_resources_destroy(&second);
    return 0;
}

int main(void)
{
    if (definitions() || failures() || owners() || resources() || worlds())
        return 1;
    puts("boat_map=ok");
    return 0;
}
