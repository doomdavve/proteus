/* A1.1: explicit map descriptions and caller-owned collision caches. Independent owners
 * initialize, query and destroy without sharing state, and the river answers exactly as the
 * process-global cache did before ownership moved. */
#include "boat_location.h"
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
                bool inside = map ? boat_map_water_contact(map, p, clearances[c], &normal)
                                  : boat_map_legacy_water_contact(p, clearances[c], &normal);
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

static int legacy(void)
{
    int water = 0;
    uint64_t digest = river_digest(NULL, &water);
    printf("river_query_digest=%016" PRIx64 " water=%d\n", digest, water);
    CHECK(digest == RIVER_QUERY_DIGEST && water == RIVER_QUERY_WATER);
    // The compatibility cache is independent of explicit owners and can be rebuilt.
    BoatMap s;
    CHECK(boat_map_init(&s, &square));
    boat_map_legacy_destroy();
    CHECK(boat_map_legacy_water(boat_location(BOAT_LOCATION_RIVER)->spawn, 2));
    if (synthetic(&s))
        return 1;
    boat_map_destroy(&s);
    boat_map_legacy_destroy();
    boat_map_legacy_destroy();
    return 0;
}

int main(void)
{
    if (definitions() || failures() || owners() || legacy())
        return 1;
    puts("boat_map=ok");
    return 0;
}
