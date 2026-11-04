#include "boat_ambient.h"
#include "boat_map.h"
#include "boat_scenery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// raylib 6.0 builds its DEFLATE decoder (external/sinfl.h) into rcore.c; see boat_scenery.c.
extern int sinflate(void *out, int cap, const void *in, int size);

typedef struct {
    const BoatMapDescription *map; // NULL: free slot.
    unsigned char *corners;
} Ambient;

static Ambient loaded[BOAT_LOCATION_COUNT];

static Ambient *find(const BoatMapDescription *map)
{
    for (size_t i = 0; map && i < BOAT_LOCATION_COUNT; ++i)
        if (loaded[i].map == map)
            return &loaded[i];
    return NULL;
}

const unsigned char *boat_ambient_of(const BoatMapDescription *map)
{
    Ambient *a = find(map);
    return a ? a->corners : NULL;
}

static uint32_t u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(unsigned char *p, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        p[i] = (unsigned char)(v >> (8 * i));
}

// One FNV-1a step per 32-bit word rather than per byte: the outer scenery is 3.5 M words.
static uint64_t mix(uint64_t key, uint32_t word) { return (key ^ word) * 1099511628211u; }

static uint32_t bits(float value)
{
    uint32_t word;
    memcpy(&word, &value, sizeof(word));
    return word;
}

uint64_t boat_ambient_key(const Mesh3D *mesh)
{
    uint64_t key = 14695981039346656037u;
    if (!mesh)
        return key;
    size_t vertices = 0;
    for (size_t i = 0; i < mesh->triangle_count; ++i)
        for (int j = 0; j < 3; ++j)
            if (mesh->triangles[i].vertices[j] >= vertices)
                vertices = mesh->triangles[i].vertices[j] + 1;
    key = mix(mix(key, (uint32_t)vertices), (uint32_t)mesh->triangle_count);
    for (size_t i = 0; i < vertices; ++i)
        key = mix(mix(mix(key, bits(mesh->vertices[i].x)), bits(mesh->vertices[i].y)),
            bits(mesh->vertices[i].z));
    for (size_t i = 0; i < mesh->triangle_count; ++i) {
        const Triangle3D *t = &mesh->triangles[i];
        for (int j = 0; j < 3; ++j)
            key = mix(key, t->vertices[j]);
        key = mix(key,
            (uint32_t)t->color.r | (uint32_t)t->color.g << 8 | (uint32_t)t->color.b << 16
                | (uint32_t)t->color.a << 24);
    }
    return key;
}

void boat_ambient_header(
    unsigned char out[BOAT_AMBIENT_HEADER], size_t triangles, uint64_t key, size_t compressed)
{
    uint32_t words[] = { BOAT_AMBIENT_MAGIC, BOAT_AMBIENT_VERSION, (uint32_t)triangles,
        (uint32_t)key, (uint32_t)(key >> 32), (uint32_t)triangles * 3, (uint32_t)compressed, 0 };
    for (int i = 0; i < 8; ++i)
        put32(out + 4 * i, words[i]);
}

bool boat_ambient_load_bytes(const BoatMapDescription *map, const void *bytes, size_t size)
{
    if (find(map))
        return true;
    const BoatMapDescription *scenery = boat_scenery_of(map);
    if (!scenery || !bytes || size < BOAT_AMBIENT_HEADER)
        return false;
    Ambient *slot = NULL;
    for (size_t i = 0; !slot && i < BOAT_LOCATION_COUNT; ++i)
        slot = loaded[i].map ? NULL : &loaded[i];
    if (!slot)
        return false;
    const Mesh3D *mesh = scenery->mesh;
    const unsigned char *h = bytes;
    uint32_t triangles = u32(h + 8), payload = u32(h + 20), packed = u32(h + 24);
    uint64_t key = (uint64_t)u32(h + 12) | (uint64_t)u32(h + 16) << 32;
    // Every bound is checked before anything is allocated or decoded.
    if (u32(h) != BOAT_AMBIENT_MAGIC || u32(h + 4) != BOAT_AMBIENT_VERSION || u32(h + 28) != 0
        || triangles != mesh->triangle_count || payload != triangles * 3 || payload > INT32_MAX
        || packed != size - BOAT_AMBIENT_HEADER || key != boat_ambient_key(mesh))
        return false;
    unsigned char *corners = malloc(payload ? payload : 1);
    if (!corners)
        return false;
    if (sinflate(corners, (int)payload, h + BOAT_AMBIENT_HEADER, (int)packed) != (int)payload) {
        free(corners);
        return false;
    }
    // Water never carries occlusion: a value there means the file was not baked this way.
    size_t water = boat_map_leading_water(mesh);
    for (size_t i = 0; i < water * 3; ++i)
        if (corners[i]) {
            free(corners);
            return false;
        }
    *slot = (Ambient) { .map = map, .corners = corners };
    return true;
}

bool boat_ambient_load(const BoatMapDescription *map, const char *path)
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
    bool ok = bytes && boat_ambient_load_bytes(map, bytes, (size_t)size);
    free(bytes);
    return ok;
}

bool boat_ambient_path(
    const BoatWorldDefinition *location, const char *assets, char *path, size_t capacity)
{
    if (!location || !location->mapped || !assets || !path)
        return false;
    int n = snprintf(path, capacity, "%sgeometry/%s/ambient.bin", assets, location->name);
    return n > 0 && (size_t)n < capacity;
}

bool boat_ambient_load_location(const BoatWorldDefinition *location, const char *assets)
{
    char path[4096];
    return location && location->mapped && boat_ambient_path(location, assets, path, sizeof(path))
        && boat_ambient_load(location->map, path);
}

void boat_ambient_unload(void)
{
    for (size_t i = 0; i < BOAT_LOCATION_COUNT; ++i) {
        free(loaded[i].corners);
        loaded[i] = (Ambient) { 0 };
    }
}
