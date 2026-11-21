/* Scenery packs (A3.5i): each mapped location's pack loads once, holds exactly its compiled
 * water before the land, and is what the renderers draw; a missing, malformed or mismatched pack
 * loads nothing, and a compiled map without its pack draws nothing rather than bare water. */
#include "core/waterfront_city.h"
#include "core/waterfront_outer.h"
#include "render/waterfront_corner.h"
#include "scenery/boat_ambient.h"
#include "scenery/boat_water_atlas.h"
#include "scenery/building_windows.h"
#include "support/boat_test_scenery.h"

#include "raymath.h"

#include <math.h>
#include <stdint.h>
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

#define OUTER_PACK PROTEUS_SOURCE_DIR "/assets/geometry/outer/scenery.bin"
#define RIVER_PACK PROTEUS_SOURCE_DIR "/assets/geometry/river/scenery.bin"
#define RIVER_ATLAS PROTEUS_SOURCE_DIR "/assets/geometry/river/water_atlas.bin"
#define RIVER_AMBIENT PROTEUS_SOURCE_DIR "/assets/geometry/river/ambient.bin"

static unsigned char *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    fseek(file, 0, SEEK_END);
    long n = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char *bytes = n > 0 ? malloc((size_t)n) : NULL;
    if (bytes && fread(bytes, 1, (size_t)n, file) != (size_t)n) {
        free(bytes);
        bytes = NULL;
    }
    fclose(file);
    *size = bytes ? (size_t)n : 0;
    return bytes;
}

static void put32(unsigned char *p, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        p[i] = (unsigned char)(v >> (8 * i));
}

// Loading COPY (SIZE bytes) as the river's water atlas fails and leaves it unloaded.
static bool atlas_rejected(const unsigned char *copy, size_t size)
{
    return !boat_water_atlas_load_bytes(&waterfront_city_river_map, copy, size)
        && !boat_water_atlas_of(&waterfront_city_river_map);
}

// Loading COPY (SIZE bytes) as the river's ambient occlusion fails and leaves it unloaded.
static bool ambient_rejected(const unsigned char *copy, size_t size)
{
    return !boat_ambient_load_bytes(&waterfront_city_river_map, copy, size)
        && !boat_ambient_of(&waterfront_city_river_map);
}

// A sidecar file of CORNERS (three per triangle of MESH), or NULL.
static unsigned char *ambient_file(const Mesh3D *mesh, const unsigned char *corners, size_t *size)
{
    int packed = 0;
    unsigned char *stream = CompressData(corners, (int)(mesh->triangle_count * 3), &packed);
    unsigned char *file = stream ? malloc(BOAT_AMBIENT_HEADER + (size_t)packed) : NULL;
    if (file) {
        boat_ambient_header(file, mesh->triangle_count, boat_ambient_key(mesh), (size_t)packed);
        memcpy(file + BOAT_AMBIENT_HEADER, stream, (size_t)packed);
        *size = BOAT_AMBIENT_HEADER + (size_t)packed;
    }
    MemFree(stream);
    return file;
}

// Loading COPY (SIZE bytes) as the outer map's scenery fails and leaves it unloaded.
static bool rejected(const unsigned char *copy, size_t size)
{
    return !boat_scenery_load_bytes(&waterfront_outer_map, copy, size)
        && !boat_scenery_of(&waterfront_outer_map);
}

// The migrated buildings of a loaded pack: records in range after the water, one role per
// triangle, no baked window triangles, attached detail after walls and roof, and facade records
// on wall pairs of their building.
static int check_migrated(const BoatMapDescription *map)
{
    const BoatMapDescription *full = boat_scenery_of(map);
    size_t count = 0, facades = 0, styles = 0;
    const BoatSceneryBuilding *list = boat_scenery_buildings(map, &count);
    const BoatSceneryFacade *facade = boat_scenery_facades(map, &facades);
    const BoatSceneryWindowStyle *style = boat_scenery_window_styles(map, &styles);
    const unsigned char *roles = boat_scenery_roles(map);
    CHECK(full && list && roles && facade && style && count > 0 && facades > 0 && styles == 8);
    size_t water = boat_map_leading_water(full->mesh), listed = 0;
    for (size_t b = 0; b < count; ++b) {
        CHECK(list[b].first >= water && list[b].end <= full->mesh->triangle_count);
        CHECK(list[b].min.y <= list[b].max.y); // A roof alone (every wall buried) is flat.
        // Windows are generated, not baked: attached detail (balconies, trim) follows walls and roof.
        CHECK(list[b].detail_first >= list[b].first && list[b].detail_first <= list[b].end);
        for (size_t i = list[b].detail_first; i < list[b].end; ++i)
            CHECK(!boat_scenery_role_is_wall(roles[i]) && roles[i] != BOAT_SCENERY_ROLE_ROOF);
        for (size_t i = list[b].first; i < list[b].end; ++i)
            CHECK(roles[i] != BOAT_SCENERY_ROLE_NONE);
        for (size_t f = 0; f < list[b].facade_count; ++f, ++listed) {
            const BoatSceneryFacade *w = &facade[list[b].facade_first + f];
            CHECK(w->id_hash == list[b].id_hash && w->bays > 0 && w->floors > 0);
            CHECK(w->wall >= list[b].first && w->wall + 2 <= list[b].end);
            CHECK(boat_scenery_role_is_wall(roles[w->wall])
                && roles[w->wall + 1] == roles[w->wall]);
        }
    }
    CHECK(listed == facades);
    return 0;
}

static bool in_triangle_xz(Vector3 p, Vector3 a, Vector3 b, Vector3 c)
{
    float d1 = (p.x - b.x) * (a.z - b.z) - (a.x - b.x) * (p.z - b.z);
    float d2 = (p.x - c.x) * (b.z - c.z) - (b.x - c.x) * (p.z - c.z);
    float d3 = (p.x - a.x) * (c.z - a.z) - (c.x - a.x) * (p.z - a.z);
    bool negative = d1 < -1e-4f || d2 < -1e-4f || d3 < -1e-4f;
    bool positive = d1 > 1e-4f || d2 > 1e-4f || d3 > 1e-4f;
    return !(negative && positive);
}

// The authored compositions of a loaded pack: every role is present, each building's bounds hold
// every one of its triangles (projecting balconies, cornices and canopies included, so culling
// by building or tile bounds never drops them), and attached architecture stays out of the water
// and inside its building's height range.
static int check_composition(const BoatMapDescription *map)
{
    const BoatMapDescription *full = boat_scenery_of(map);
    size_t count = 0;
    const BoatSceneryBuilding *list = boat_scenery_buildings(map, &count);
    const unsigned char *roles = boat_scenery_roles(map);
    CHECK(full && list && roles);
    const Mesh3D *mesh = full->mesh;
    size_t water = boat_map_leading_water(mesh), seen[BOAT_SCENERY_ROLE_COUNT] = { 0 };
    for (size_t b = 0; b < count; ++b)
        for (size_t i = list[b].first; i < list[b].end; ++i) {
            ++seen[roles[i]];
            for (int k = 0; k < 3; ++k) {
                Vector3 v = mesh->vertices[mesh->triangles[i].vertices[k]];
                CHECK(v.x >= list[b].min.x && v.x <= list[b].max.x && v.y >= list[b].min.y
                    && v.y <= list[b].max.y && v.z >= list[b].min.z && v.z <= list[b].max.z);
                if (roles[i] != BOAT_SCENERY_ROLE_BALCONY && roles[i] != BOAT_SCENERY_ROLE_CANOPY)
                    continue;
                CHECK(v.y > list[b].min.y + .5f);
                for (size_t w = 0; w < water; ++w) {
                    const Triangle3D *t = &mesh->triangles[w];
                    CHECK(!in_triangle_xz(v, mesh->vertices[t->vertices[0]],
                        mesh->vertices[t->vertices[1]], mesh->vertices[t->vertices[2]]));
                }
            }
        }
    return seen[BOAT_SCENERY_ROLE_WALL_PLASTER] && seen[BOAT_SCENERY_ROLE_WALL_BRICK]
            && seen[BOAT_SCENERY_ROLE_WALL_PANEL] && seen[BOAT_SCENERY_ROLE_BALCONY]
            && seen[BOAT_SCENERY_ROLE_CANOPY] && seen[BOAT_SCENERY_ROLE_TRIM]
            && seen[BOAT_SCENERY_ROLE_ROOF]
        ? 0
        : 1;
}

int main(void)
{
    const BoatWorldDefinition *outer = boat_location(BOAT_LOCATION_OUTER);
    const BoatWorldDefinition *river = test_location(TEST_RIVER); // The crop fixture.
    const BoatWorldDefinition *basin = boat_location(BOAT_LOCATION_BASIN);

    // Nothing is loaded at start: compiled maps draw nothing until their pack is in.
    CHECK(!boat_scenery_of(&waterfront_outer_map) && !boat_scenery_bytes(&waterfront_outer_map));
    CHECK(!boat_scenery_drawn(&waterfront_outer_map) && !boat_scenery_drawn(NULL));
    WaterfrontScenery scenery = { 0 };
    CHECK(!waterfront_scenery_init(&scenery, &waterfront_city_river_map) && !scenery.ready);
    // A description that carries its own land (a test map) is drawn as it is.
    static const Vector3 vertices[]
        = { { 0, 0, 0 }, { 0, 0, 10 }, { 10, 0, 0 }, { 0, 2, 0 }, { 0, 2, 10 }, { 10, 2, 0 } };
    static const Triangle3D triangles[]
        = { { { 0, 1, 2 }, { 40, 60, 80, 255 } }, { { 3, 4, 5 }, { 120, 120, 120, 255 } } };
    static const Mesh3D test_mesh = { vertices, triangles, 2 };
    static const BoatMapDescription test_map = { &test_mesh, 2 };
    static const Mesh3D water_mesh = { vertices, triangles, 1 };
    static const BoatMapDescription water_only = { &water_mesh, 1 };
    CHECK(boat_scenery_drawn(&test_map) == &test_map && !boat_scenery_drawn(&water_only));

    // Paths are relative to the asset directory; the basin has no pack.
    char path[64];
    CHECK(boat_scenery_path(outer, "assets/", path, sizeof(path))
        && strcmp(path, "assets/geometry/outer/scenery.bin") == 0);
    CHECK(
        !boat_scenery_path(outer, "assets/", path, 12) && !boat_scenery_path(basin, "", path, 64));
    CHECK(boat_scenery_load_location(basin, "/nonexistent/"));
    CHECK(!boat_scenery_load_location(outer, "/nonexistent/") && !boat_scenery_of(outer->map));

    // Malformed and mismatched packs load nothing.
    size_t size = 0;
    unsigned char *pack = read_file(OUTER_PACK, &size);
    CHECK(pack && size > 32);
    unsigned char *copy = malloc(size);
    CHECK(copy);
    CHECK(rejected(pack, 31) && rejected(pack, size - 64)); // Header cut, record stream cut.
    memcpy(copy, pack, size);
    copy[0] ^= 1; // Magic.
    CHECK(rejected(copy, size));
    memcpy(copy, pack, size);
    put32(copy + 4, 5); // Unknown version.
    CHECK(rejected(copy, size));
    memcpy(copy, pack, size);
    put32(copy + 8, 1); // Vertex count no longer matches the payload size.
    CHECK(rejected(copy, size));
    memcpy(copy, pack, size);
    put32(copy + 16, 2737); // One water triangle short of the compiled map's.
    CHECK(rejected(copy, size));
    memcpy(copy, pack, size);
    put32(copy + 20, 2737); // Bridge range before the water ends.
    CHECK(rejected(copy, size));
    size_t river_size = 0;
    unsigned char *river_pack = read_file(RIVER_PACK, &river_size);
    CHECK(river_pack && rejected(river_pack, river_size)); // Another map's water.
    free(river_pack);

    // Version 3: the same pack with a trailing building-record block (tools/map_pack.py). The
    // shipped pack is version 3; the synthetic cases reuse only its vertices and triangles, and a
    // real wall pair of it (a, c, b), (a, d, c) as the facade's wall.
    size_t stream = 32 + (size_t)pack[28] + 256u * pack[29] + 65536u * pack[30];
    CHECK(stream < size);
    uint32_t water_count = (uint32_t)waterfront_outer_map.mesh->triangle_count;
    uint32_t wall = 0;
    {
        CHECK(boat_scenery_load_bytes(&waterfront_outer_map, pack, size));
        const Mesh3D *mesh = boat_scenery_of(&waterfront_outer_map)->mesh;
        for (uint32_t i = water_count + 1; !wall && i + 20 < mesh->triangle_count; ++i) {
            const Triangle3D *t = &mesh->triangles[i], *u = &mesh->triangles[i + 1];
            Vector3 a = mesh->vertices[t->vertices[0]], c = mesh->vertices[t->vertices[1]];
            Vector3 e = mesh->vertices[t->vertices[2]];
            if (u->vertices[0] == t->vertices[0] && u->vertices[2] == t->vertices[1] && a.y == e.y
                && c.x == e.x && c.z == e.z && c.y > a.y)
                wall = i;
        }
        boat_scenery_unload();
        CHECK(wall > water_count);
    }
    free(copy);
    copy = malloc(stream + 4096);
    CHECK(copy);
    enum { HEAD = 5, B0 = HEAD, B1 = B0 + 14, RUNS = B1 + 14, FACADE = RUNS + 6, STYLE = FACADE + 3,
        WORDS = STYLE + 9 };
    float tiny[9] = { 0, 1, 0, 1, .01f, .01f, .01f, .01f, 0 }; // Fits any wall.
    uint32_t words0[WORDS] = { 0x444C4250u, 2, 2, 1, 1 };
    uint32_t b0[14] = { 11, wall, wall + 8, wall + 6, 0, 2, 0, 1 };
    uint32_t b1[14] = { 22, wall + 12, wall + 15, wall + 15, 2, 0, 1, 0 };
    float box0[6] = { 0, 3, 0, 1, 9, 1 }, box1[6] = { 0, 0, 0, 1, 1, 1 };
    memcpy(b0 + 8, box0, sizeof(box0));
    memcpy(b1 + 8, box1, sizeof(box1));
    memcpy(words0 + B0, b0, sizeof(b0));
    memcpy(words0 + B1, b1, sizeof(b1));
    uint32_t r[6] = { wall, wall + 4, 1 | 7 << 8 | 4096u << 16, wall + 6, wall + 8, 3 | 255 << 8 };
    memcpy(words0 + RUNS, r, sizeof(r));
    words0[FACADE] = wall;
    words0[FACADE + 1] = 3 | 1u << 16; // Facade number 3, one bay.
    words0[FACADE + 2] = 1; // One floor, style 0, profile 0.
    memcpy(words0 + STYLE, tiny, sizeof(tiny));
    struct {
        const char *what;
        int field; // Index into the words above; -1: no change.
        uint32_t value;
    } cases[] = { { "ok", -1, 0 }, { "magic", 0, 1 }, { "building count", 1, 3 },
        { "run count", 2, 1 }, { "facade count", 3, 2 }, { "style count", 4, 0 },
        { "first in water", B0 + 1, 1 }, { "overlap", B1 + 1, wall + 7 },
        { "empty", B0 + 2, wall }, { "past mesh", B0 + 2, 0xFFFFFFF0u },
        { "detail past end", B0 + 3, wall + 9 }, { "duplicate id", B1, 11 },
        { "run mismatch", B0 + 4, 1 }, { "facade mismatch", B0 + 6, 1 },
        { "bad role", RUNS + 2, BOAT_SCENERY_ROLE_COUNT }, { "role none", RUNS + 5, 0 }, { "run outside", RUNS + 3, wall + 12 },
        { "run order", RUNS + 3, 0 }, { "wall outside building", FACADE, wall + 7 },
        { "wall not a wall", FACADE, wall + 6 }, { "wall unpaired", FACADE, wall + 1 },
        { "no bays", FACADE + 1, 3 }, { "no floors", FACADE + 2, 0 },
        { "no such style", FACADE + 2, 1 | 1u << 16 }, { "no such profile", FACADE + 2, 1 | (unsigned)BUILDING_PROFILE_COUNT << 24 },
        { "grid larger than the wall", FACADE + 1, 3 | 60000u << 16 },
        { "style not finite", STYLE, 0x7FC00000u }, { "style pitch", STYLE + 1, 0 },
        { "style size", STYLE + 4, 0 },
        { "facade on a balcony run", RUNS + 2, BOAT_SCENERY_ROLE_BALCONY | 7 << 8 | 4096u << 16 } };
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        memcpy(copy, pack, stream);
        put32(copy + 4, 3);
        uint32_t words[WORDS];
        memcpy(words, words0, sizeof(words));
        if (cases[c].field >= 0)
            words[cases[c].field] = cases[c].value;
        for (size_t i = 0; i < WORDS; ++i)
            put32(copy + stream + 4 * i, words[i]);
        size_t total = stream + WORDS * 4;
        if (c == 0) {
            CHECK(boat_scenery_load_bytes(&waterfront_outer_map, copy, total));
            size_t count = 0, runs = 0, facades = 0, styles = 0;
            const BoatSceneryBuilding *list = boat_scenery_buildings(&waterfront_outer_map, &count);
            const BoatSceneryRun *run = boat_scenery_runs(&waterfront_outer_map, &runs);
            const BoatSceneryFacade *fa = boat_scenery_facades(&waterfront_outer_map, &facades);
            const BoatSceneryWindowStyle *st
                = boat_scenery_window_styles(&waterfront_outer_map, &styles);
            const BoatMapDescription *v3 = boat_scenery_of(&waterfront_outer_map);
            CHECK(list && run && fa && st && count == 2 && runs == 2 && facades == 1 && styles == 1);
            CHECK(list[1].id_hash == 22 && list[0].detail_first == wall + 6 && list[0].max.y == 9
                && list[0].facade_count == 1 && list[1].facade_first == 1
                && run[0].facade == 7 && run[0].heading == 4096 && run[1].facade == 255);
            CHECK(fa[0].id_hash == 11 && fa[0].wall == wall && fa[0].index == 3 && fa[0].bays == 1
                && fa[0].floors == 1 && fa[0].style == 0 && st[0].bay_pitch == 1
                && st[0].ground_width == .01f);
            CHECK(boat_scenery_buildings(v3, NULL) == list);
            const unsigned char *roles = boat_scenery_roles(v3);
            CHECK(roles && roles[wall - 1] == BOAT_SCENERY_ROLE_NONE
                && roles[wall] == BOAT_SCENERY_ROLE_WALL
                && roles[wall + 5] == BOAT_SCENERY_ROLE_DETAIL
                && roles[wall + 6] == BOAT_SCENERY_ROLE_DETAIL
                && roles[wall + 8] == BOAT_SCENERY_ROLE_NONE
                && roles[wall + 12] == BOAT_SCENERY_ROLE_DETAIL);
            boat_scenery_unload();
            CHECK(!boat_scenery_buildings(&waterfront_outer_map, &count) && count == 0
                && !boat_scenery_facades(&waterfront_outer_map, &facades) && facades == 0);
        } else {
            if (!rejected(copy, total))
                fprintf(stderr, "v3 case accepted: %s\n", cases[c].what);
            CHECK(rejected(copy, total));
        }
    }
    // The authored wall materials and attached roles load, and a facade may stand on any wall role.
    {
        static const unsigned char walls[] = { BOAT_SCENERY_ROLE_WALL_PLASTER,
            BOAT_SCENERY_ROLE_WALL_BRICK, BOAT_SCENERY_ROLE_WALL_PANEL };
        for (size_t k = 0; k < sizeof(walls); ++k) {
            memcpy(copy, pack, stream);
            put32(copy + 4, 3);
            uint32_t words[WORDS];
            memcpy(words, words0, sizeof(words));
            words[RUNS + 2] = walls[k] | 7 << 8 | 4096u << 16;
            words[RUNS + 5] = BOAT_SCENERY_ROLE_CANOPY | 255 << 8;
            for (size_t i = 0; i < WORDS; ++i)
                put32(copy + stream + 4 * i, words[i]);
            CHECK(boat_scenery_load_bytes(&waterfront_outer_map, copy, stream + WORDS * 4));
            const unsigned char *roles = boat_scenery_roles(boat_scenery_of(&waterfront_outer_map));
            CHECK(roles && roles[wall] == walls[k] && boat_scenery_role_is_wall(roles[wall + 1])
                && roles[wall + 6] == BOAT_SCENERY_ROLE_CANOPY);
            boat_scenery_unload();
        }
        CHECK(!boat_scenery_role_is_wall(BOAT_SCENERY_ROLE_BALCONY)
            && !boat_scenery_role_is_wall(BOAT_SCENERY_ROLE_TRIM)
            && !boat_scenery_role_is_wall(BOAT_SCENERY_ROLE_ROOF)
            && !boat_scenery_role_is_wall(BOAT_SCENERY_ROLE_NONE));
    }
    memcpy(copy, pack, stream);
    put32(copy + 4, 3);
    memset(copy + stream, 0, 20);
    CHECK(rejected(copy, stream + 20) && rejected(copy, stream + 3) && rejected(copy, stream));
    memcpy(copy, pack, stream);
    put32(copy + 4, 2); // Version 2 (baked windows) was never shipped and is not read.
    CHECK(rejected(copy, stream + 20));
    memcpy(copy, pack, stream);
    put32(copy + 4, 1);
    CHECK(boat_scenery_load_bytes(&waterfront_outer_map, copy, stream)); // Version 1 still loads.
    size_t none = 7;
    CHECK(!boat_scenery_buildings(&waterfront_outer_map, &none) && none == 0
        && !boat_scenery_roles(&waterfront_outer_map));
    boat_scenery_unload();
    free(copy);
    copy = malloc(size);
    CHECK(copy);

    // A well-formed pack loads once; its water is the compiled map's, triangle for triangle.
    CHECK(boat_scenery_load_bytes(&waterfront_outer_map, pack, size));
    const BoatMapDescription *full = boat_scenery_of(&waterfront_outer_map);
    CHECK(full && boat_scenery_drawn(&waterfront_outer_map) == full);
    CHECK(boat_scenery_load(&waterfront_outer_map, "/nonexistent")
        && boat_scenery_of(outer->map) == full); // Already loaded.
    const Mesh3D *water = waterfront_outer_map.mesh;
    CHECK(boat_map_leading_water(full->mesh) == water->triangle_count);
    for (size_t i = 0; i < water->triangle_count; ++i)
        for (int j = 0; j < 3; ++j) {
            Vector3 a = full->mesh->vertices[full->mesh->triangles[i].vertices[j]];
            Vector3 b = water->vertices[water->triangles[i].vertices[j]];
            CHECK(a.x == b.x && a.y == b.y && a.z == b.z);
        }
    CHECK(full->bridge_first_triangle > water->triangle_count
        && full->bridge_first_triangle < full->mesh->triangle_count);
    // The payload is the vertices and triangles, nothing else.
    size_t triangle_bytes = full->mesh->triangle_count * sizeof(Triangle3D);
    size_t bytes = boat_scenery_bytes(&waterfront_outer_map);
    CHECK(bytes > triangle_bytes && (bytes - triangle_bytes) % sizeof(Vector3) == 0);
    free(copy);
    free(pack);

    // The outer kit is laid out from the river's land, so the outer scenery needs both packs.
    CHECK(!waterfront_scenery_init(&scenery, &waterfront_outer_map) && !scenery.ready);
    CHECK(boat_scenery_load_location(river, PROTEUS_SOURCE_DIR "/assets/"));
    CHECK(waterfront_scenery_init(&scenery, &waterfront_outer_map) && scenery.ready
        && scenery.full == full && scenery.map == &waterfront_outer_map && scenery.river);
    waterfront_scenery_destroy(&scenery);
    // Loading the outer location loads the river crop's pack it lays the kit out from, but not
    // the crop's sidecars: the river is not a location.
    boat_scenery_unload();
    CHECK(boat_scenery_load_all(PROTEUS_SOURCE_DIR "/assets/"));
    CHECK(boat_scenery_of(river->map) && !boat_water_atlas_of(river->map)
        && !boat_ambient_of(river->map));
    CHECK(boat_scenery_load_location(river, PROTEUS_SOURCE_DIR "/assets/"));

    // G4 water atlases: each map's sidecar loads with its pack, sized and keyed to it.
    const BoatWaterAtlas *river_atlas = boat_water_atlas_of(river->map);
    const BoatWaterAtlas *outer_atlas = boat_water_atlas_of(outer->map);
    CHECK(river_atlas && outer_atlas && !boat_water_atlas_of(basin->map));
    CHECK(river_atlas->spacing == 4 && outer_atlas->spacing == 8);
    CHECK(boat_water_atlas_key(river->map) != boat_water_atlas_key(outer->map));
    // Land texels have no shore distance or depth; water texels have a shore distance.
    int land = 0, wet = 0;
    for (int i = 0; i < river_atlas->width * river_atlas->height; ++i) {
        const unsigned char *t = river_atlas->texels + 4 * i;
        land += t[0] == 0 && t[1] == 0;
        wet += t[0] > 0;
    }
    CHECK(land > 0 && wet > 0 && land + wet == river_atlas->width * river_atlas->height);
    boat_scenery_unload();
    CHECK(!boat_water_atlas_of(river->map) && !boat_water_atlas_of(outer->map));
    CHECK(!boat_water_atlas_load_location(river, "/nonexistent/")
        && !boat_water_atlas_of(river->map));
    CHECK(!boat_water_atlas_load_location(basin, PROTEUS_SOURCE_DIR "/assets/"));
    // Malformed, stale and mismatched atlases load nothing.
    size_t atlas_size = 0;
    unsigned char *atlas = read_file(RIVER_ATLAS, &atlas_size);
    CHECK(atlas && atlas_size > BOAT_WATER_ATLAS_HEADER);
    unsigned char *bad = malloc(atlas_size + 1);
    CHECK(bad);
    CHECK(atlas_rejected(atlas, BOAT_WATER_ATLAS_HEADER - 1)
        && atlas_rejected(atlas, atlas_size - 1));
    memcpy(bad, atlas, atlas_size);
    bad[atlas_size] = 0;
    CHECK(atlas_rejected(bad, atlas_size + 1)); // Trailing bytes.
    const size_t fields[]
        = { 0, 4, 8, 12, 24, 28, 36, 44 }; // Magic, version, sizes, spacing, key, payload, zero.
    const uint32_t values[] = { 0, 2, 4097, 1, 0, 1, 4, 1 };
    for (size_t f = 0; f < sizeof(fields) / sizeof(fields[0]); ++f) {
        memcpy(bad, atlas, atlas_size);
        put32(bad + fields[f], values[f]);
        CHECK(atlas_rejected(bad, atlas_size));
    }
    CHECK(!boat_water_atlas_load_bytes(outer->map, atlas, atlas_size)); // Another map's water.
    CHECK(boat_water_atlas_load_bytes(river->map, atlas, atlas_size)
        && boat_water_atlas_of(river->map));
    boat_water_atlas_unload();
    free(bad);
    free(atlas);

    // G5c ambient occlusion: each map's sidecar loads with its pack, keyed to its scenery.
    // Water corners are open; land has both open and closed-off corners.
    CHECK(boat_scenery_load_all(PROTEUS_SOURCE_DIR "/assets/")
        && boat_scenery_load_location(river, PROTEUS_SOURCE_DIR "/assets/"));
    CHECK(check_migrated(river->map) == 0 && check_migrated(outer->map) == 0);
    CHECK(check_composition(river->map) == 0 && check_composition(outer->map) == 0);
    const unsigned char *river_ambient = boat_ambient_of(river->map);
    CHECK(river_ambient && boat_ambient_of(outer->map) && !boat_ambient_of(basin->map));
    const Mesh3D *river_mesh = boat_scenery_of(river->map)->mesh;
    CHECK(boat_ambient_key(river_mesh) != boat_ambient_key(boat_scenery_of(outer->map)->mesh));
    size_t wet_corners = boat_map_leading_water(river_mesh) * 3, open = 0, closed = 0;
    for (size_t i = 0; i < river_mesh->triangle_count * 3; ++i) {
        CHECK(i >= wet_corners || river_ambient[i] == 0);
        open += i >= wet_corners && river_ambient[i] == 0;
        closed += river_ambient[i] > 0;
    }
    CHECK(open > 0 && closed > 0);
    // Malformed, stale and mismatched sidecars load nothing; nor does any without its scenery.
    size_t ambient_size = 0;
    unsigned char *ambient = read_file(RIVER_AMBIENT, &ambient_size);
    CHECK(ambient && ambient_size > BOAT_AMBIENT_HEADER);
    boat_ambient_unload();
    CHECK(!boat_ambient_of(river->map));
    bad = malloc(ambient_size + 1);
    CHECK(bad);
    CHECK(ambient_rejected(ambient, BOAT_AMBIENT_HEADER - 1)
        && ambient_rejected(ambient, ambient_size - 1));
    memcpy(bad, ambient, ambient_size);
    bad[ambient_size] = 0;
    CHECK(ambient_rejected(bad, ambient_size + 1)); // Trailing bytes.
    // Magic, version, triangle count, key, payload, compressed bytes and the zero word.
    const size_t ambient_fields[] = { 0, 4, 8, 12, 20, 24, 28 };
    const uint32_t ambient_values[] = { 0, 2, 1, 1, 3, 1, 1 };
    for (size_t f = 0; f < sizeof(ambient_fields) / sizeof(ambient_fields[0]); ++f) {
        memcpy(bad, ambient, ambient_size);
        put32(bad + ambient_fields[f], ambient_values[f]);
        CHECK(ambient_rejected(bad, ambient_size));
    }
    // A value on a water corner means another bake.
    unsigned char *corners = malloc(river_mesh->triangle_count * 3);
    CHECK(corners);
    CHECK(boat_ambient_load_bytes(river->map, ambient, ambient_size));
    memcpy(corners, boat_ambient_of(river->map), river_mesh->triangle_count * 3);
    boat_ambient_unload();
    size_t edited_size = 0;
    corners[0] = 1;
    unsigned char *edited = ambient_file(river_mesh, corners, &edited_size);
    CHECK(edited && ambient_rejected(edited, edited_size));
    free(edited);
    corners[0] = 0; // Rewritten with open water, it loads.
    edited = ambient_file(river_mesh, corners, &edited_size);
    CHECK(edited && boat_ambient_load_bytes(river->map, edited, edited_size));
    boat_ambient_unload();
    free(edited);
    free(corners);
    CHECK(!boat_ambient_load_bytes(outer->map, ambient, ambient_size)); // Another map's scenery.
    boat_scenery_unload();
    CHECK(!boat_ambient_of(river->map)
        && !boat_ambient_load_bytes(river->map, ambient, ambient_size));
    CHECK(!boat_ambient_load_location(basin, PROTEUS_SOURCE_DIR "/assets/"));
    free(bad);
    free(ambient);

    boat_scenery_unload();
    CHECK(!boat_scenery_of(&waterfront_outer_map) && !boat_scenery_of(&waterfront_city_river_map));
    boat_scenery_unload();
    test_scenery_load();
    CHECK(test_scenery(&waterfront_city_river_map) && boat_scenery_bytes(river->map) > 0);
    boat_scenery_unload();
    puts("boat_scenery=ok");
    return 0;
}
