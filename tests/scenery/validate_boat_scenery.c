/* Scenery packs (A3.5i): each mapped location's pack loads once, holds exactly its compiled
 * water before the land, and is what the renderers draw; a missing, malformed or mismatched pack
 * loads nothing, and a compiled map without its pack draws nothing rather than bare water. */
#include "core/waterfront_city.h"
#include "core/waterfront_outer.h"
#include "render/waterfront_corner.h"
#include "scenery/boat_ambient.h"
#include "scenery/boat_water_atlas.h"
#include "support/boat_test_scenery.h"

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
    CHECK(rejected(pack, 31) && rejected(pack, size - 1)); // Header cut, stream cut.
    memcpy(copy, pack, size);
    copy[0] ^= 1; // Magic.
    CHECK(rejected(copy, size));
    memcpy(copy, pack, size);
    put32(copy + 4, 3); // Unknown version.
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

    // Version 2: the same pack with a trailing building-record block (tools/map_pack.py).
    uint32_t water_count = (uint32_t)waterfront_outer_map.mesh->triangle_count;
    CHECK(water_count + 20 < pack[12] + 256u * pack[13] + 65536u * pack[14]);
    free(copy);
    copy = malloc(size + 4096);
    CHECK(copy);
    struct {
        uint32_t id, first, end, detail, run_first, run_count;
        float box[6];
    } b[2] = { { 11, water_count + 1, water_count + 9, water_count + 7, 0, 2,
                   { 0, 3, 0, 1, 9, 1 } },
        { 22, water_count + 12, water_count + 15, water_count + 15, 2, 0, { 0, 0, 0, 1, 1, 1 } } };
    uint32_t r[2][3] = { { water_count + 1, water_count + 5, 1 | 7 << 8 | 4096u << 16 },
        { water_count + 7, water_count + 9, 3 | 255 << 8 } };
    struct {
        const char *what;
        int field; // Index into the 4 head, 24 building and 6 run words; -1: no change.
        uint32_t value;
    } cases[] = { { "ok", -1, 0 }, { "magic", 0, 1 }, { "building count", 1, 3 },
        { "run count", 2, 1 }, { "reserved", 3, 5 }, { "first in water", 4 + 1, 1 },
        { "overlap", 4 + 12 + 1, water_count + 8 }, { "empty", 4 + 2, water_count + 1 },
        { "past mesh", 4 + 2, 0xFFFFFFF0u }, { "detail past end", 4 + 3, water_count + 10 },
        { "duplicate id", 4 + 12, 11 }, { "run mismatch", 4 + 4, 1 }, { "bad role", 4 + 24 + 2, 9 },
        { "role none", 4 + 24 + 5, 0 }, { "run outside", 4 + 24 + 4, water_count + 12 },
        { "run order", 4 + 24 + 3, 0 } };
    size_t block_words = 4 + 12 + 12 + 6;
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        memcpy(copy, pack, size);
        put32(copy + 4, 2);
        unsigned char *at = copy + size;
        uint32_t words[4 + 12 + 12 + 6] = { 0x444C4250u, 2, 2, 0 };
        memcpy(words + 4, b, sizeof(b));
        memcpy(words + 4 + 24, r, sizeof(r));
        if (cases[c].field >= 0)
            words[cases[c].field] = cases[c].value;
        for (size_t i = 0; i < block_words; ++i)
            put32(at + 4 * i, words[i]);
        size_t total = size + block_words * 4;
        if (c == 0) {
            CHECK(boat_scenery_load_bytes(&waterfront_outer_map, copy, total));
            size_t count = 0, runs = 0;
            const BoatSceneryBuilding *list = boat_scenery_buildings(&waterfront_outer_map, &count);
            const BoatSceneryRun *run = boat_scenery_runs(&waterfront_outer_map, &runs);
            const BoatMapDescription *v2 = boat_scenery_of(&waterfront_outer_map);
            CHECK(list && run && count == 2 && runs == 2 && list[1].id_hash == 22
                && list[0].detail_first == water_count + 7 && list[0].max.y == 9
                && run[0].facade == 7 && run[0].heading == 4096 && run[1].facade == 255);
            CHECK(boat_scenery_buildings(v2, NULL) == list);
            const unsigned char *roles = boat_scenery_roles(v2);
            CHECK(roles && roles[water_count] == BOAT_SCENERY_ROLE_NONE
                && roles[water_count + 1] == BOAT_SCENERY_ROLE_WALL
                && roles[water_count + 5] == BOAT_SCENERY_ROLE_DETAIL
                && roles[water_count + 7] == BOAT_SCENERY_ROLE_WINDOW
                && roles[water_count + 9] == BOAT_SCENERY_ROLE_NONE
                && roles[water_count + 12] == BOAT_SCENERY_ROLE_DETAIL);
            boat_scenery_unload();
            CHECK(!boat_scenery_buildings(&waterfront_outer_map, &count) && count == 0);
        } else {
            if (!rejected(copy, total))
                fprintf(stderr, "v2 case accepted: %s\n", cases[c].what);
            CHECK(rejected(copy, total));
        }
    }
    memcpy(copy, pack, size);
    put32(copy + 4, 2);
    memset(copy + size, 0, 16);
    CHECK(rejected(copy, size + 16) && rejected(copy, size + 3) && rejected(copy, size));
    memcpy(copy, pack, size);
    CHECK(boat_scenery_load_bytes(&waterfront_outer_map, copy, size)); // Version 1 still loads.
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
