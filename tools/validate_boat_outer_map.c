/* A3.1: the baked first outer-river slice. Every waypoint and sampled leg of the A0.3 slice route
 * (dock to the Långedrag old harbour) must be water with the survey's 8 m envelope, the indexed
 * queries must equal the exhaustive scan, and points outside the region or over land must not be
 * water. The route is copied from docs/adventure/survey.py (SLICE_ROUTE) in map east/north metres,
 * which the queries take as simulation X = -east, Z = north. */
#include "boat_map.h"
#include "boat_world.h"
#include "waterfront_outer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define ENVELOPE 8.0f
#define WATER_TRIANGLES 2738 // The manifest's water_triangles.
#define BRIDGE_TRIANGLES 4388 // The manifest's river_overlay.bridge_triangles.
#define DRESSING_TRIANGLES 65612 // The manifest's dressing.triangles.
static const Vector2 route[] = { { 12, 60 }, { 24, -70 }, { 15, -180 }, { -250, -500 },
    { -650, -850 }, { -1100, -960 }, { -3300, -1550 }, { -3850, -2400 }, { -3850, -3150 },
    { -3700, -3310 }, { -3620, -3380 } };
#define ROUTE_POINTS ((int)(sizeof(route) / sizeof(route[0])))

static Vector2 sim(Vector2 east_north) { return (Vector2) { -east_north.x, east_north.y }; }

static float unit(void) { return (float)rand() / (float)RAND_MAX; }

int main(void)
{
    BoatMap map;
    CHECK(boat_map_init(&map, &waterfront_outer_map));
    CHECK(boat_map_leading_water(waterfront_outer_map.mesh) == WATER_TRIANGLES);
    CHECK(map.water_count == WATER_TRIANGLES);
    // A3.3: the river's bridge triangles start the authored range, which the A3.5a dressing
    // (buildings, cranes, marks and trees beyond the crop) completes.
    CHECK(waterfront_outer_map.bridge_first_triangle < waterfront_outer_map.mesh->triangle_count);
    CHECK(waterfront_outer_map.mesh->triangle_count - waterfront_outer_map.bridge_first_triangle
        == BRIDGE_TRIANGLES + DRESSING_TRIANGLES);
    CHECK(!boat_map_is_bridge_triangle(
        &waterfront_outer_map, waterfront_outer_map.bridge_first_triangle - 1));
    CHECK(boat_map_is_bridge_triangle(
        &waterfront_outer_map, waterfront_outer_map.bridge_first_triangle));

    // A3.2: the `outer` location binds this map; its spawn and fleet start share the river's
    // frame and lie in navigable water, and a world prepared from it answers collision queries.
    const BoatWorldDefinition *outer = boat_location_find("outer");
    CHECK(outer && outer == boat_location(BOAT_LOCATION_OUTER));
    CHECK(outer->mapped && outer->map == &waterfront_outer_map);
    CHECK(boat_map_water(&map, outer->spawn, 2.5f));
    CHECK(boat_map_water(&map, outer->fleet_spawn, 2.5f));
    BoatWorldResources world;
    CHECK(boat_world_resources_init(&world, outer));
    CHECK(boat_world_resources_ready(&world));
    CHECK(boat_map_water(&world.map, outer->spawn, 2.5f));
    boat_world_resources_destroy(&world);

    // The land is Baskarta terrain (A3.1b) outside the river crop: real relief above the water,
    // never below the 1 m floor on its top surface, with hills that rise tens of metres. The
    // river's bridge (A3.3) reaches its 107.6 m tower tops and the container cranes (A3.5a)
    // their raised booms at the mapped 118 m above the quay. Water stays at 0.
    const Mesh3D *mesh = waterfront_outer_map.mesh;
    float highest = 0, lowest_top = INFINITY;
    for (size_t i = WATER_TRIANGLES; i < mesh->triangle_count; ++i) {
        const Triangle3D *t = &mesh->triangles[i];
        float y[3] = { mesh->vertices[t->vertices[0]].y, mesh->vertices[t->vertices[1]].y,
            mesh->vertices[t->vertices[2]].y };
        float flo = fminf(y[0], fminf(y[1], y[2])), fhi = fmaxf(y[0], fmaxf(y[1], y[2]));
        highest = fmaxf(highest, fhi);
        if (flo >= .99f) // Land top: every corner at or above the floor.
            lowest_top = fminf(lowest_top, flo);
    }
    CHECK(highest > 118.0f && highest < 125.0f);
    CHECK(lowest_top >= .99f);
    CHECK(mesh->triangle_count > 200000);

    size_t legs = 0;
    for (int i = 0; i < ROUTE_POINTS; ++i) {
        CHECK(boat_map_water(&map, sim(route[i]), ENVELOPE));
        if (i == 0)
            continue;
        Vector2 a = route[i - 1], b = route[i];
        int steps = (int)ceilf(hypotf(b.x - a.x, b.y - a.y) / 10.0f);
        for (int s = 1; s < steps; ++s, ++legs) {
            float t = (float)s / (float)steps;
            Vector2 p = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
            CHECK(boat_map_water(&map, sim(p), ENVELOPE));
        }
    }

    // Indexed queries equal the exhaustive scan over the region and its surroundings.
    static const float clearances[3] = { 0, 3, 12 };
    srand(3);
    size_t wet = 0, dry = 0;
    for (int i = 0; i < 20000; ++i) {
        Vector2 p = { -6500.0f + 8000.0f * unit(), -4500.0f + 5500.0f * unit() };
        float clearance = clearances[i % 3];
        Vector2 na = { 0, 0 }, nb = { 0, 0 };
        bool a = boat_map_water_contact(&map, p, clearance, &na);
        bool b = boat_map_water_contact_reference(&map, p, clearance, &nb);
        CHECK(a == b);
        CHECK(a || (na.x == nb.x && na.y == nb.y));
        a ? ++wet : ++dry;
    }
    CHECK(wet > 100 && dry > 1000);

    // Beyond the region's south-west and north-east corners there is no water, and the quay
    // north of the dock is land.
    CHECK(!boat_map_water(&map, sim((Vector2) { -9000, -6000 }), 0));
    CHECK(!boat_map_water(&map, sim((Vector2) { 3000, 3000 }), 0));
    CHECK(!boat_map_water(&map, sim((Vector2) { 12, 60 + 400 }), 0));

    printf("boat_outer_map=ok water=%zu edges=%zu leg_samples=%zu wet=%zu dry=%zu bytes=%zu\n",
        map.water_count, map.shore_count, legs, wet, dry, boat_map_bytes(&map));
    boat_map_destroy(&map);
    return 0;
}
