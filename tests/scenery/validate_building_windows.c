/* The building window generator (scenery/building_windows.h): seeds are identity, windows follow
 * their facade record and wall exactly, and every migrated facade of the shipped packs expands
 * to windows that fit their wall and face outward. The pinned seeds were computed with Python's
 * zlib.crc32 and murmur3's finalizer, the formulas the facade shader repeats. */
#include "core/waterfront_city.h"
#include "core/waterfront_outer.h"
#include "scenery/building_windows.h"
#include "support/boat_test_scenery.h"

#include "raymath.h"

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

static const BoatSceneryWindowStyle LEGACY
    = { 2.5f, 4, 1.5f, 3.2f, 2.2f, 1.9f, 1.5f, 1.5f, .035f };

static int seeds(void)
{
    // crc32("way/164963584") and the crc32 of its facades, continued, not recomputed.
    CHECK(building_window_facade_seed(0x9df80401u, 0) == 0x6b6a729eu);
    CHECK(building_window_facade_seed(0x9df80401u, 3) == 0xf2632324u);
    CHECK(building_window_facade_seed(0x9df80401u, 17) == 0x6478aff2u);
    CHECK(building_window_facade_seed(0xebba2ed3u, 1) == 0x74d85a01u);
    CHECK(building_window_facade_seed(0x15cba16du, 250) == 0x2e375177u);
    CHECK(building_window_seed(0x6b6a729eu, 0, 0) == 0xd19c0a6fu);
    CHECK(building_window_seed(0xf2632324u, 2, 1) == 0xe5de6477u);
    CHECK(building_window_seed(0x6478aff2u, 5, 4) == 0xd4b1fd71u);
    CHECK(building_window_seed(0x74d85a01u, 0, 2) == 0x3301c0bdu);
    CHECK(building_window_seed(0x2e375177u, 13, 9) == 0xbcce642fu);
    // Glass and occupancy follow the legacy mapping: seed % 7 and the hashed 3 of 8.
    CHECK(0xd19c0a6fu % 7 == 0 && !building_window_occupied(0xd19c0a6fu));
    Color glass = building_window_glass(0xd19c0a6fu);
    CHECK(glass.r == 138 && glass.g == 185 && glass.b == 204 && glass.a == 255);
    CHECK(building_window_occupied(0x3301c0bdu) && building_window_occupied(0xbcce642fu));
    glass = building_window_glass(3);
    CHECK(glass.r == 181 && glass.g == 211 && glass.b == 221);
    glass = building_window_glass(6);
    CHECK(glass.r == 66 && glass.g == 100 && glass.b == 120);
    // A large sample: about three in eight lit, all three glasses used.
    unsigned lit = 0, kinds[3] = { 0 };
    uint32_t facade = building_window_facade_seed(0x12345678u, 9);
    for (unsigned bay = 0; bay < 100; ++bay)
        for (unsigned floor = 0; floor < 80; ++floor) {
            uint32_t seed = building_window_seed(facade, bay, floor);
            lit += building_window_occupied(seed);
            ++kinds[seed % 7 < 3 ? 0 : seed % 7 == 3 ? 1 : 2];
        }
    CHECK(lit > 8000 * 3 / 8 - 300 && lit < 8000 * 3 / 8 + 300);
    CHECK(kinds[0] > 3000 && kinds[1] > 700 && kinds[2] > 3000);
    // Distinct facades of one building and distinct buildings differ.
    CHECK(building_window_facade_seed(0x9df80401u, 4) != building_window_facade_seed(0x9df80401u, 3));
    CHECK(building_window_facade_seed(0x9df80402u, 3) != building_window_facade_seed(0x9df80401u, 3));
    return 0;
}

typedef struct {
    size_t count;
    uint32_t hash; // Order-dependent over the corners and look.
    BuildingWindow first, last;
} Tally;

static void tally(const BuildingWindow *w, void *user)
{
    Tally *t = user;
    if (!t->count)
        t->first = *w;
    t->last = *w;
    ++t->count;
    uint32_t bits[14];
    memcpy(bits, w->corners, sizeof(w->corners)); // 12 floats
    bits[12] = (uint32_t)w->glass.r | (uint32_t)w->glass.g << 8 | (uint32_t)w->glass.b << 16;
    bits[13] = w->occupied | w->bay << 1 | w->floor << 16;
    for (int i = 0; i < 14; ++i)
        t->hash = (t->hash ^ bits[i]) * 16777619u;
}

// A flat wall along +X from (10, 5, 0) to (30, 5, 0), 12 m high, as (a, c, b), (a, d, c).
static int synthetic(void)
{
    static const Vector3 vertices[] = { { 10, 5, 0 }, { 30, 17, 0 }, { 30, 5, 0 }, { 10, 17, 0 },
        { 10, 5, 40 }, { 40, 5, 40 }, { 40, 20, 40 }, { 10, 20, 40 } };
    static const Triangle3D triangles[] = { { { 0, 1, 2 }, { 1, 2, 3, 255 } },
        { { 0, 3, 1 }, { 1, 2, 3, 255 } }, { { 4, 6, 5 }, { 1, 2, 3, 255 } },
        { { 4, 7, 6 }, { 1, 2, 3, 255 } } };
    Mesh3D mesh = { vertices, triangles, 4 };
    BoatSceneryFacade facade = { 0x9df80401u, 0, 3, 4, 3, 0 };
    BuildingWall wall;
    CHECK(building_wall(&mesh, &facade, &wall));
    CHECK(wall.length == 20 && wall.top == 17 && wall.along.x == 1 && wall.normal.z == -1
        && wall.normal.x == 0 && wall.origin.y == 5 && wall.end.x == 30);
    CHECK(building_window_count(&facade) == 12);
    Tally a = { 0 };
    CHECK(building_windows(&facade, &LEGACY, &wall, tally, &a) == 12 && a.count == 12);
    // First window: ground floor 2.2 x 1.9 at 2.5 m, 1.5 m up, 3.5 cm outside the wall (-Z).
    const BuildingWindow *w = &a.first;
    CHECK(w->corners[0].x == 12.5f && w->corners[0].y == 6.5f && fabsf(w->corners[0].z + .035f) < 1e-6f);
    CHECK(w->corners[1].y == 8.4f && fabsf(w->corners[2].x - 14.7f) < 1e-5f && w->corners[3].y == 6.5f);
    // Last: bay 3, floor 2, 1.5 x 1.5 at 14.5 m along, 11.9 m up from the base.
    w = &a.last;
    CHECK(w->bay == 3 && w->floor == 2 && fabsf(w->corners[0].x - 24.5f) < 1e-5f
        && fabsf(w->corners[0].y - (5 + 1.5f + 6.4f)) < 1e-5f
        && fabsf(w->corners[2].x - 26.0f) < 1e-5f && fabsf(w->corners[2].y - (w->corners[0].y + 1.5f)) < 1e-5f);
    // Each window agrees with the one-window call, and seeds are those of the formulas.
    BuildingWindow one;
    CHECK(building_window(&facade, &LEGACY, &wall, 3, 2, &one) && memcmp(one.corners, a.last.corners, sizeof(one.corners)) == 0
        && one.occupied == a.last.occupied && one.glass.g == a.last.glass.g);
    uint32_t seed = building_window_seed(building_window_facade_seed(0x9df80401u, 3), 3, 2);
    CHECK(one.occupied == building_window_occupied(seed)
        && one.glass.r == building_window_glass(seed).r);
    CHECK(!building_window(&facade, &LEGACY, &wall, 4, 0, &one)
        && !building_window(&facade, &LEGACY, &wall, 0, 3, &one));
    // Deterministic, and independent of the wall's place: the same id and facade number on another
    // wall (a different mesh position and triangle) draws the same glass and lit pattern.
    Tally b = { 0 };
    building_windows(&facade, &LEGACY, &wall, tally, &b);
    CHECK(b.hash == a.hash);
    BoatSceneryFacade moved = facade;
    moved.wall = 2;
    BuildingWall far_wall;
    CHECK(building_wall(&mesh, &moved, &far_wall) && far_wall.length == 30);
    for (unsigned floor = 0; floor < 3; ++floor)
        for (unsigned bay = 0; bay < 4; ++bay) {
            BuildingWindow p, q;
            CHECK(building_window(&facade, &LEGACY, &wall, bay, floor, &p)
                && building_window(&moved, &LEGACY, &far_wall, bay, floor, &q));
            CHECK(p.occupied == q.occupied && p.glass.r == q.glass.r && p.glass.g == q.glass.g);
        }
    // Another facade number of the same building changes the pattern.
    BoatSceneryFacade other = facade;
    other.index = 4;
    unsigned differing = 0;
    for (unsigned floor = 0; floor < 3; ++floor)
        for (unsigned bay = 0; bay < 4; ++bay) {
            BuildingWindow p, q;
            building_window(&facade, &LEGACY, &wall, bay, floor, &p);
            building_window(&other, &LEGACY, &wall, bay, floor, &q);
            differing += p.occupied != q.occupied || p.glass.r != q.glass.r;
        }
    CHECK(differing > 0);
    // A wall that is not the generated pair is refused.
    BoatSceneryFacade bad = facade;
    bad.wall = 1;
    CHECK(!building_wall(&mesh, &bad, &wall));
    bad.wall = 3;
    CHECK(!building_wall(&mesh, &bad, &wall));
    CHECK(!building_wall(NULL, &facade, &wall));
    return 0;
}

// Every facade of a loaded pack expands inside its wall and building, facing outward, once.
static int pack(const BoatMapDescription *map, size_t expect_facades, size_t expect_windows)
{
    const BoatMapDescription *full = boat_scenery_of(map);
    size_t buildings = 0, facades = 0, styles = 0;
    const BoatSceneryBuilding *b = boat_scenery_buildings(map, &buildings);
    const BoatSceneryFacade *f = boat_scenery_facades(map, &facades);
    const BoatSceneryWindowStyle *style = boat_scenery_window_styles(map, &styles);
    CHECK(full && b && f && style && styles == 6 && facades == expect_facades);
    size_t windows = 0;
    uint32_t forward = 0, backward = 0;
    for (size_t k = 0; k < buildings; ++k) {
        for (size_t i = 0; i < b[k].facade_count; ++i) {
            const BoatSceneryFacade *w = &f[b[k].facade_first + i];
            const BoatSceneryWindowStyle *s = &style[w->style];
            BuildingWall wall;
            CHECK(building_wall(full->mesh, w, &wall));
            CHECK(wall.top > wall.origin.y + 2.9f); // Walls under 3 m take no windows.
            // No two facades share a wall (no window set twice).
            for (size_t j = 0; j < i; ++j)
                CHECK(f[b[k].facade_first + j].wall != w->wall);
            Tally t = { 0 };
            CHECK(building_windows(w, s, &wall, tally, &t) == building_window_count(w));
            windows += t.count;
            forward += t.hash;
            // Corners: on the wall's outside, inside its extent and the building's bounds.
            for (unsigned floor = 0; floor < w->floors; floor += w->floors > 1 ? w->floors - 1 : 1)
                for (unsigned bay = 0; bay < w->bays; bay += w->bays > 1 ? w->bays - 1 : 1) {
                    BuildingWindow q;
                    CHECK(building_window(w, s, &wall, bay, floor, &q));
                    Vector3 edge = Vector3Subtract(q.corners[2], q.corners[0]);
                    Vector3 up = Vector3Subtract(q.corners[1], q.corners[0]);
                    Vector3 n = Vector3CrossProduct(up, edge);
                    CHECK(Vector3DotProduct(n, wall.normal) > 0 && fabsf(n.y) < 1e-3f * Vector3Length(n));
                    for (int c = 0; c < 4; ++c) {
                        Vector3 d = Vector3Subtract(q.corners[c], wall.origin);
                        float along = Vector3DotProduct(d, wall.along);
                        float out = Vector3DotProduct(d, wall.normal);
                        CHECK(along >= -1e-3f && along <= wall.length + 1e-3f);
                        CHECK(fabsf(out - s->offset) < 1e-3f);
                        CHECK(q.corners[c].y >= wall.origin.y && q.corners[c].y <= wall.top);
                        CHECK(q.corners[c].x > b[k].min.x - .1f && q.corners[c].x < b[k].max.x + .1f
                            && q.corners[c].z > b[k].min.z - .1f && q.corners[c].z < b[k].max.z + .1f);
                    }
                }
        }
        // Reversed order gives the same set (identity is the building, not the position).
        for (size_t i = b[k].facade_count; i-- > 0;) {
            const BoatSceneryFacade *w = &f[b[k].facade_first + i];
            BuildingWall wall;
            Tally t = { 0 };
            CHECK(building_wall(full->mesh, w, &wall));
            building_windows(w, &style[w->style], &wall, tally, &t);
            backward += t.hash;
        }
    }
    CHECK(forward == backward);
    CHECK(windows == expect_windows);
    return 0;
}

int main(void)
{
    CHECK(seeds() == 0);
    CHECK(synthetic() == 0);
    const BoatWorldDefinition *outer = boat_location(BOAT_LOCATION_OUTER);
    CHECK(boat_scenery_load_all(PROTEUS_SOURCE_DIR "/assets/"));
    CHECK(pack(&waterfront_city_river_map, 157, 1570) == 0);
    CHECK(pack(outer->map, 195, 1954) == 0);
    boat_scenery_unload();
    printf("building_windows=ok\n");
    return 0;
}
