#include "scenery/building_windows.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// crc32 (zlib's, polynomial 0xEDB88320) of TEXT continuing the finished crc CRC, so that
// continue(crc32(id), "/3") equals crc32("<id>/3").
static uint32_t crc32_continue(uint32_t crc, const char *text)
{
    uint32_t c = ~crc;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        c ^= *p;
        for (int k = 0; k < 8; ++k)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

uint32_t building_window_facade_seed(uint32_t id_hash, unsigned facade)
{
    char suffix[16];
    snprintf(suffix, sizeof(suffix), "/%u", facade);
    return crc32_continue(id_hash, suffix);
}

static uint32_t fmix32(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

uint32_t building_window_seed(uint32_t facade_seed, unsigned bay, unsigned floor)
{
    return fmix32(facade_seed + bay * 0x9E3779B1u + floor * 0x85EBCA77u);
}

Color building_window_glass(uint32_t seed)
{
    return seed % 7 < 3 ? (Color) { 138, 185, 204, 255 }
        : seed % 7 == 3 ? (Color) { 181, 211, 221, 255 }
                        : (Color) { 66, 100, 120, 255 };
}

bool building_window_occupied(uint32_t seed)
{
    return ((seed ^ (seed >> 15)) * 2654435761u) >> 29 < 3;
}

bool building_wall(const Mesh3D *mesh, const BoatSceneryFacade *facade, BuildingWall *wall)
{
    if (!mesh || !facade || (size_t)facade->wall + 2 > mesh->triangle_count)
        return false;
    const Triangle3D *t = &mesh->triangles[facade->wall], *u = &mesh->triangles[facade->wall + 1];
    Vector3 a = mesh->vertices[t->vertices[0]], c = mesh->vertices[t->vertices[1]];
    Vector3 b = mesh->vertices[t->vertices[2]];
    float length = hypotf(b.x - a.x, b.z - a.z);
    if (u->vertices[0] != t->vertices[0] || u->vertices[2] != t->vertices[1] || a.y != b.y
        || c.x != b.x || c.z != b.z || !(c.y > a.y) || !(length > 0))
        return false;
    Vector3 along = { (b.x - a.x) / length, 0, (b.z - a.z) / length };
    *wall = (BuildingWall) { .origin = a,
        .end = b,
        .along = along,
        .normal = { along.z, 0, -along.x },
        .length = length,
        .top = c.y };
    return true;
}

size_t building_window_count(const BoatSceneryFacade *facade)
{
    return (size_t)facade->bays * facade->floors;
}

static bool window_at(const BoatSceneryFacade *facade, const BoatSceneryWindowStyle *style,
    const BuildingWall *wall, uint32_t facade_seed, unsigned bay, unsigned floor,
    BuildingWindow *window)
{
    if (bay >= facade->bays || floor >= facade->floors)
        return false;
    float width = floor == 0 ? style->ground_width : style->upper_width;
    float height = floor == 0 ? style->ground_height : style->upper_height;
    float u = style->first_bay + (float)bay * style->bay_pitch;
    float y = wall->origin.y + style->first_floor + (float)floor * style->floor_pitch;
    Vector3 p = { wall->origin.x + wall->along.x * u + wall->normal.x * style->offset, y,
        wall->origin.z + wall->along.z * u + wall->normal.z * style->offset };
    Vector3 q = { p.x + wall->along.x * width, y, p.z + wall->along.z * width };
    uint32_t seed = building_window_seed(facade_seed, bay, floor);
    window->corners[0] = p;
    window->corners[1] = (Vector3) { p.x, y + height, p.z };
    window->corners[2] = (Vector3) { q.x, y + height, q.z };
    window->corners[3] = q;
    window->glass = building_window_glass(seed);
    window->occupied = building_window_occupied(seed);
    window->bay = bay;
    window->floor = floor;
    return true;
}

bool building_window(const BoatSceneryFacade *facade, const BoatSceneryWindowStyle *style,
    const BuildingWall *wall, unsigned bay, unsigned floor, BuildingWindow *window)
{
    return window_at(
        facade, style, wall, building_window_facade_seed(facade->id_hash, facade->index), bay,
        floor, window);
}

size_t building_windows(const BoatSceneryFacade *facade, const BoatSceneryWindowStyle *style,
    const BuildingWall *wall, BuildingWindowFn fn, void *user)
{
    uint32_t facade_seed = building_window_facade_seed(facade->id_hash, facade->index);
    size_t count = 0;
    for (unsigned floor = 0; floor < facade->floors; ++floor)
        for (unsigned bay = 0; bay < facade->bays; ++bay) {
            BuildingWindow window;
            if (window_at(facade, style, wall, facade_seed, bay, floor, &window)) {
                if (fn)
                    fn(&window, user);
                ++count;
            }
        }
    return count;
}
