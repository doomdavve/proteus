/* Scenery packs (A3.5i): each mapped location's pack loads once, holds exactly its compiled
 * water before the land, and is what the renderers draw; a missing, malformed or mismatched pack
 * loads nothing, and a compiled map without its pack draws nothing rather than bare water. */
#include "boat_test_scenery.h"
#include "waterfront_city.h"
#include "waterfront_corner.h"
#include "waterfront_outer.h"

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

// Loading COPY (SIZE bytes) as the outer map's scenery fails and leaves it unloaded.
static bool rejected(const unsigned char *copy, size_t size)
{
    return !boat_scenery_load_bytes(&waterfront_outer_map, copy, size)
        && !boat_scenery_of(&waterfront_outer_map);
}

int main(void)
{
    const BoatWorldDefinition *outer = boat_location(BOAT_LOCATION_OUTER);
    const BoatWorldDefinition *river = boat_location(BOAT_LOCATION_RIVER);
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
    put32(copy + 4, 2); // Version.
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
    CHECK(boat_scenery_load_all(PROTEUS_SOURCE_DIR "/assets/"));

    boat_scenery_unload();
    CHECK(!boat_scenery_of(&waterfront_outer_map) && !boat_scenery_of(&waterfront_city_river_map));
    boat_scenery_unload();
    test_scenery_load();
    CHECK(test_scenery(&waterfront_city_river_map) && boat_scenery_bytes(river->map) > 0);
    boat_scenery_unload();
    puts("boat_scenery=ok");
    return 0;
}
