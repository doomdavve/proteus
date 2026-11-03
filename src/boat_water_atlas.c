#include "boat_water_atlas.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// raylib 6.0 builds its DEFLATE decoder (external/sinfl.h) into rcore.c; see boat_scenery.c.
extern int sinflate(void *out, int cap, const void *in, int size);

typedef struct {
    const BoatMapDescription *map; // NULL: free slot.
    BoatWaterAtlas atlas;
    unsigned char *texels;
} Atlas;

static Atlas loaded[BOAT_LOCATION_COUNT];

static Atlas *find(const BoatMapDescription *map)
{
    for (size_t i = 0; map && i < BOAT_LOCATION_COUNT; ++i)
        if (loaded[i].map == map)
            return &loaded[i];
    return NULL;
}

const BoatWaterAtlas *boat_water_atlas_of(const BoatMapDescription *map)
{
    Atlas *a = find(map);
    return a ? &a->atlas : NULL;
}

static uint32_t u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static float f32(const unsigned char *p)
{
    uint32_t bits = u32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void put32(unsigned char *p, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        p[i] = (unsigned char)(v >> (8 * i));
}

static uint64_t fnv(uint64_t key, const void *bytes, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        key = (key ^ ((const unsigned char *)bytes)[i]) * 1099511628211u;
    return key;
}

uint64_t boat_water_atlas_key(const BoatMapDescription *map)
{
    uint64_t key = 14695981039346656037u;
    if (!map || !map->mesh)
        return key;
    const Mesh3D *mesh = map->mesh;
    size_t water = boat_map_leading_water(mesh);
    unsigned char count[8];
    put32(count, (uint32_t)water);
    put32(count + 4, (uint32_t)((uint64_t)water >> 32));
    key = fnv(key, count, sizeof(count));
    for (size_t i = 0; i < water; ++i) {
        const Triangle3D *t = &mesh->triangles[i];
        for (int j = 0; j < 3; ++j) {
            Vector3 v = mesh->vertices[t->vertices[j]];
            float xyz[] = { v.x, v.y, v.z };
            for (int k = 0; k < 3; ++k) {
                uint32_t bits;
                memcpy(&bits, &xyz[k], sizeof(bits));
                unsigned char little[4];
                put32(little, bits);
                key = fnv(key, little, sizeof(little));
            }
        }
        unsigned char color[] = { t->color.r, t->color.g, t->color.b, t->color.a };
        key = fnv(key, color, sizeof(color));
    }
    return key;
}

void boat_water_atlas_header(unsigned char out[BOAT_WATER_ATLAS_HEADER], int width, int height,
    float origin_x, float origin_z, float spacing, uint64_t key, size_t compressed)
{
    float floats[] = { origin_x, origin_z, spacing };
    uint32_t bits[3];
    memcpy(bits, floats, sizeof(bits));
    uint32_t words[] = { BOAT_WATER_ATLAS_MAGIC, BOAT_WATER_ATLAS_VERSION, (uint32_t)width,
        (uint32_t)height, bits[0], bits[1], bits[2], (uint32_t)key, (uint32_t)(key >> 32),
        (uint32_t)width * (uint32_t)height * 4, (uint32_t)compressed, 0 };
    for (int i = 0; i < 12; ++i)
        put32(out + 4 * i, words[i]);
}

bool boat_water_atlas_load_bytes(const BoatMapDescription *map, const void *bytes, size_t size)
{
    if (find(map))
        return true;
    if (!map || !map->mesh || !bytes || size < BOAT_WATER_ATLAS_HEADER)
        return false;
    Atlas *slot = NULL;
    for (size_t i = 0; !slot && i < BOAT_LOCATION_COUNT; ++i)
        slot = loaded[i].map ? NULL : &loaded[i];
    if (!slot)
        return false;
    const unsigned char *h = bytes;
    uint32_t width = u32(h + 8), height = u32(h + 12), payload = u32(h + 36);
    uint32_t packed = u32(h + 40);
    float origin_x = f32(h + 16), origin_z = f32(h + 20), spacing = f32(h + 24);
    uint64_t key = (uint64_t)u32(h + 28) | (uint64_t)u32(h + 32) << 32;
    // Every bound is checked before anything is allocated or decoded.
    if (u32(h) != BOAT_WATER_ATLAS_MAGIC || u32(h + 4) != BOAT_WATER_ATLAS_VERSION
        || u32(h + 44) != 0 || width < 1 || height < 1 || width > BOAT_WATER_ATLAS_MAX_SIZE
        || height > BOAT_WATER_ATLAS_MAX_SIZE || payload != width * height * 4
        || packed != size - BOAT_WATER_ATLAS_HEADER || !isfinite(origin_x) || !isfinite(origin_z)
        || fabsf(origin_x) > 1e6f || fabsf(origin_z) > 1e6f || !(spacing >= 1 && spacing <= 64)
        || key != boat_water_atlas_key(map))
        return false;
    unsigned char *texels = malloc(payload);
    if (!texels)
        return false;
    if (sinflate(texels, (int)payload, h + BOAT_WATER_ATLAS_HEADER, (int)packed) != (int)payload) {
        free(texels);
        return false;
    }
    // Land texels have no depth: a value there means the file was not baked this way.
    for (uint32_t i = 0; i < width * height; ++i) {
        const unsigned char *t = texels + 4 * i;
        if (t[0] == 0 && t[1]) {
            free(texels);
            return false;
        }
    }
    *slot = (Atlas) { .map = map, .texels = texels };
    slot->atlas = (BoatWaterAtlas) { (int)width, (int)height, origin_x, origin_z, spacing, texels };
    return true;
}

bool boat_water_atlas_load(const BoatMapDescription *map, const char *path)
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
    bool ok = bytes && boat_water_atlas_load_bytes(map, bytes, (size_t)size);
    free(bytes);
    return ok;
}

bool boat_water_atlas_path(
    const BoatWorldDefinition *location, const char *assets, char *path, size_t capacity)
{
    if (!location || !location->mapped || !assets || !path)
        return false;
    int n = snprintf(path, capacity, "%sgeometry/%s/water_atlas.bin", assets, location->name);
    return n > 0 && (size_t)n < capacity;
}

bool boat_water_atlas_load_location(const BoatWorldDefinition *location, const char *assets)
{
    char path[4096];
    return location && location->mapped
        && boat_water_atlas_path(location, assets, path, sizeof(path))
        && boat_water_atlas_load(location->map, path);
}

void boat_water_atlas_unload(void)
{
    for (size_t i = 0; i < BOAT_LOCATION_COUNT; ++i) {
        free(loaded[i].texels);
        loaded[i] = (Atlas) { 0 };
    }
}
