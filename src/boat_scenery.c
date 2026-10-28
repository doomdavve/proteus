#include "boat_scenery.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// raylib 6.0 builds its DEFLATE decoder (external/sinfl.h) into rcore.c. DecompressData would
// allocate and wipe a 64 MB scratch buffer per call; the pack states its exact size instead.
extern int sinflate(void *out, int cap, const void *in, int size);

enum { PACK_MAGIC = 0x4E435350, PACK_VERSION = 1, PACK_HEADER = 32 };

typedef struct {
    const BoatMapDescription *map; // The compiled description it belongs to; NULL: free slot.
    BoatMapDescription description;
    Mesh3D mesh;
    void *payload; // Vertices, then triangles.
    size_t bytes;
} Scenery;

static Scenery loaded[BOAT_LOCATION_COUNT];

static Scenery *find(const BoatMapDescription *map)
{
    for (size_t i = 0; map && i < BOAT_LOCATION_COUNT; ++i)
        if (loaded[i].map == map)
            return &loaded[i];
    return NULL;
}

const BoatMapDescription *boat_scenery_of(const BoatMapDescription *map)
{
    Scenery *s = find(map);
    return s ? &s->description : NULL;
}

const BoatMapDescription *boat_scenery_drawn(const BoatMapDescription *map)
{
    const BoatMapDescription *scenery = boat_scenery_of(map);
    if (scenery || !map || !map->mesh)
        return scenery;
    return boat_map_leading_water(map->mesh) < map->mesh->triangle_count ? map : NULL;
}

static uint32_t u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool same_vertex(Vector3 a, Vector3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// The pack's leading water must be the compiled map's, triangle for triangle.
static bool water_matches(const Mesh3D *mesh, size_t water, const Mesh3D *compiled)
{
    if (!compiled || compiled->triangle_count != water)
        return false;
    for (size_t i = 0; i < water; ++i) {
        const Triangle3D *a = &mesh->triangles[i], *b = &compiled->triangles[i];
        if (memcmp(&a->color, &b->color, sizeof(a->color)) != 0)
            return false;
        for (int j = 0; j < 3; ++j)
            if (!same_vertex(mesh->vertices[a->vertices[j]], compiled->vertices[b->vertices[j]]))
                return false;
    }
    return true;
}

bool boat_scenery_load_bytes(const BoatMapDescription *map, const void *bytes, size_t size)
{
    if (find(map))
        return true;
    const uint32_t one = 1;
    if (!map || !bytes || size < PACK_HEADER || *(const unsigned char *)&one != 1)
        return false; // The payload is read in place, so the host must be little-endian.
    Scenery *slot = NULL;
    for (size_t i = 0; !slot && i < BOAT_LOCATION_COUNT; ++i)
        slot = loaded[i].map ? NULL : &loaded[i];
    if (!slot)
        return false;
    const unsigned char *h = bytes;
    uint32_t vertices = u32(h + 8), triangles = u32(h + 12), water = u32(h + 16);
    uint32_t bridge = u32(h + 20), payload = u32(h + 24), packed = u32(h + 28);
    uint64_t expected
        = (uint64_t)vertices * sizeof(Vector3) + (uint64_t)triangles * sizeof(Triangle3D);
    if (u32(h) != PACK_MAGIC || u32(h + 4) != PACK_VERSION || payload != expected
        || payload > INT32_MAX || packed > size - PACK_HEADER || water > bridge
        || bridge > triangles || !water)
        return false;
    _Static_assert(sizeof(Vector3) == 12 && sizeof(Triangle3D) == 16, "Pack payload layout");
    unsigned char *out = malloc(payload ? payload : 1);
    if (!out)
        return false;
    if (sinflate(out, (int)payload, h + PACK_HEADER, (int)packed) != (int)payload) {
        free(out);
        return false;
    }
    Mesh3D mesh = { (const Vector3 *)out,
        (const Triangle3D *)(out + (size_t)vertices * sizeof(Vector3)), triangles };
    bool valid = true;
    for (size_t i = 0; valid && i < triangles; ++i)
        for (int j = 0; j < 3; ++j)
            valid = valid && mesh.triangles[i].vertices[j] < vertices;
    if (!valid || boat_map_leading_water(&mesh) != water
        || !water_matches(&mesh, water, map->mesh)) {
        free(out);
        return false;
    }
    *slot = (Scenery) { .map = map, .mesh = mesh, .payload = out, .bytes = payload };
    slot->description = (BoatMapDescription) { &slot->mesh, bridge };
    return true;
}

bool boat_scenery_load(const BoatMapDescription *map, const char *path)
{
    if (find(map))
        return true;
    FILE *file = path ? fopen(path, "rb") : NULL;
    if (!file)
        return false;
    unsigned char *bytes = NULL;
    long size = -1;
    if (fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) > 0 && fseek(file, 0, SEEK_SET) == 0
        && (bytes = malloc((size_t)size)) && fread(bytes, 1, (size_t)size, file) != (size_t)size) {
        free(bytes);
        bytes = NULL;
    }
    fclose(file);
    bool ok = bytes && boat_scenery_load_bytes(map, bytes, (size_t)size);
    free(bytes);
    return ok;
}

bool boat_scenery_path(
    const BoatWorldDefinition *location, const char *assets, char *path, size_t capacity)
{
    if (!location || !location->mapped || !assets || !path)
        return false;
    int n = snprintf(path, capacity, "%sgeometry/%s/scenery.bin", assets, location->name);
    return n > 0 && (size_t)n < capacity;
}

bool boat_scenery_load_location(const BoatWorldDefinition *location, const char *assets)
{
    if (location && !location->mapped)
        return true;
    char path[4096];
    return boat_scenery_path(location, assets, path, sizeof(path))
        && boat_scenery_load(location->map, path);
}

bool boat_scenery_load_all(const char *assets)
{
    for (int i = 0; i < BOAT_LOCATION_COUNT; ++i)
        if (!boat_scenery_load_location(boat_location((BoatLocationId)i), assets))
            return false;
    return true;
}

size_t boat_scenery_bytes(const BoatMapDescription *map)
{
    Scenery *s = find(map);
    return s ? s->bytes : 0;
}

void boat_scenery_unload(void)
{
    for (size_t i = 0; i < BOAT_LOCATION_COUNT; ++i) {
        free(loaded[i].payload);
        loaded[i] = (Scenery) { 0 };
    }
}
