#include "boat_map_tiles.h"
#include "raymath.h"

#include <stdlib.h>

typedef enum { LIST_WATER, LIST_SHORE, LIST_LAND, LIST_COUNT } TileList;

static Vector2 point(Vector3 p) { return (Vector2) { p.x, p.z }; }

size_t boat_map_tiles_owner(const BoatMapTiles *tiles, Vector2 p)
{
    int x = (int)floorf((p.x - tiles->origin.x) / tiles->tile_size);
    int z = (int)floorf((p.y - tiles->origin.y) / tiles->tile_size);
    x = x < 0 ? 0 : x >= tiles->columns ? tiles->columns - 1 : x;
    z = z < 0 ? 0 : z >= tiles->rows ? tiles->rows - 1 : z;
    return (size_t)z * (size_t)tiles->columns + (size_t)x;
}

// Visits LIST's items in index order: counts each tile's entries when OUT is NULL, otherwise
// writes them at and advances each tile's CURSOR.
static void assign(
    const BoatMapTiles *tiles, TileList list, unsigned int *cursor, unsigned int *out)
{
    const BoatMap *map = tiles->map;
    const Mesh3D *mesh = tiles->description->mesh;
    size_t water_count = tiles->water_count;
    size_t count = list == LIST_LAND ? mesh->triangle_count - water_count
        : !map                       ? 0
        : list == LIST_WATER         ? water_count
                                     : map->shore_count;
    for (size_t i = 0; i < count; ++i) {
        Vector2 p[3];
        int n = 3;
        if (list == LIST_SHORE) {
            p[0] = point(mesh->vertices[map->shore[i].a]);
            p[1] = point(mesh->vertices[map->shore[i].b]);
            n = 2;
        } else {
            const Triangle3D *t = &mesh->triangles[list == LIST_WATER ? i : water_count + i];
            for (int j = 0; j < 3; ++j)
                p[j] = point(mesh->vertices[t->vertices[j]]);
        }
        unsigned int value = list == LIST_LAND ? (unsigned int)(water_count + i) : (unsigned int)i;
        if (list == LIST_LAND) {
            Vector2 centroid = Vector2Scale(Vector2Add(Vector2Add(p[0], p[1]), p[2]), 1 / 3.0f);
            size_t tile = boat_map_tiles_owner(tiles, centroid);
            if (out)
                out[cursor[tile]++] = value;
            else
                ++cursor[tile];
            continue;
        }
        for (size_t tile = 0; tile < (size_t)tiles->columns * (size_t)tiles->rows; ++tile) {
            const BoatMapTile *t = &tiles->tiles[tile];
            if (!boat_map_polygon_near_rect(p, n, t->min, t->max, BOAT_MAP_MARGIN))
                continue;
            if (out)
                out[cursor[tile]++] = value;
            else
                ++cursor[tile];
        }
    }
}

static unsigned int *first(BoatMapTile *tile, TileList list)
{
    return list == LIST_WATER ? &tile->water_first
        : list == LIST_SHORE  ? &tile->shore_first
                              : &tile->land_first;
}

static unsigned int *count(BoatMapTile *tile, TileList list)
{
    return list == LIST_WATER ? &tile->water_count
        : list == LIST_SHORE  ? &tile->shore_count
                              : &tile->land_count;
}

static bool build_list(BoatMapTiles *tiles, TileList list, unsigned int **out, size_t *entries)
{
    size_t n = (size_t)tiles->columns * (size_t)tiles->rows;
    unsigned int *cursor = calloc(n, sizeof(*cursor));
    if (!cursor)
        return false;
    assign(tiles, list, cursor, NULL);
    size_t total = 0;
    for (size_t i = 0; i < n; ++i) {
        *first(&tiles->tiles[i], list) = (unsigned int)total;
        *count(&tiles->tiles[i], list) = cursor[i];
        total += cursor[i];
        cursor[i] = *first(&tiles->tiles[i], list);
    }
    *out = malloc((total + 1) * sizeof(**out));
    if (*out)
        assign(tiles, list, cursor, *out);
    free(cursor);
    *entries = total;
    return *out != NULL;
}

static bool build(BoatMapTiles *tiles, const BoatMapDescription *description, const BoatMap *map,
    size_t water_count, float tile_size)
{
    const Mesh3D *mesh = description->mesh;
    Vector2 lo = { INFINITY, INFINITY }, hi = { -INFINITY, -INFINITY };
    for (size_t i = 0; i < mesh->triangle_count; ++i)
        for (int j = 0; j < 3; ++j) {
            Vector2 p = point(mesh->vertices[mesh->triangles[i].vertices[j]]);
            lo = Vector2Min(lo, p);
            hi = Vector2Max(hi, p);
        }
    tiles->description = description;
    tiles->map = map;
    tiles->water_count = water_count;
    tiles->tile_size = tile_size;
    tiles->origin
        = (Vector2) { floorf(lo.x / tile_size) * tile_size, floorf(lo.y / tile_size) * tile_size };
    // A vertex exactly on the far seam still lies inside the last tile.
    tiles->columns = (int)floorf((hi.x - tiles->origin.x) / tile_size) + 1;
    tiles->rows = (int)floorf((hi.y - tiles->origin.y) / tile_size) + 1;
    size_t n = (size_t)tiles->columns * (size_t)tiles->rows;
    tiles->tiles = calloc(n, sizeof(*tiles->tiles));
    if (!tiles->tiles) {
        *tiles = (BoatMapTiles) { 0 };
        return false;
    }
    for (int z = 0; z < tiles->rows; ++z)
        for (int x = 0; x < tiles->columns; ++x) {
            BoatMapTile *t = &tiles->tiles[(size_t)z * (size_t)tiles->columns + (size_t)x];
            t->min = (Vector2) { tiles->origin.x + x * tile_size, tiles->origin.y + z * tile_size };
            t->max = (Vector2) { t->min.x + tile_size, t->min.y + tile_size };
        }
    if (!build_list(tiles, LIST_WATER, &tiles->water, &tiles->water_entries)
        || !build_list(tiles, LIST_SHORE, &tiles->shore, &tiles->shore_entries)
        || !build_list(tiles, LIST_LAND, &tiles->land, &tiles->land_entries)) {
        boat_map_tiles_destroy(tiles);
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        BoatMapTile *t = &tiles->tiles[i];
        t->land_min = (Vector3) { INFINITY, INFINITY, INFINITY };
        t->land_max = (Vector3) { -INFINITY, -INFINITY, -INFINITY };
        for (unsigned int k = 0; k < t->land_count; ++k) {
            const Triangle3D *triangle = &mesh->triangles[tiles->land[t->land_first + k]];
            for (int j = 0; j < 3; ++j) {
                t->land_min = Vector3Min(t->land_min, mesh->vertices[triangle->vertices[j]]);
                t->land_max = Vector3Max(t->land_max, mesh->vertices[triangle->vertices[j]]);
            }
        }
    }
    return true;
}

bool boat_map_tiles_init(BoatMapTiles *tiles, const BoatMap *map, float tile_size)
{
    *tiles = (BoatMapTiles) { 0 };
    if (!boat_map_ready(map) || !(tile_size > 0))
        return false;
    return build(tiles, map->description, map, map->water_count, tile_size);
}

bool boat_map_tiles_init_land(
    BoatMapTiles *tiles, const BoatMapDescription *description, float tile_size)
{
    *tiles = (BoatMapTiles) { 0 };
    if (!description || !description->mesh || !description->mesh->triangle_count
        || !(tile_size > 0))
        return false;
    size_t water_count = boat_map_leading_water(description->mesh);
    if (!water_count)
        return false;
    return build(tiles, description, NULL, water_count, tile_size);
}

size_t boat_map_tiles_count(const BoatMapTiles *tiles)
{
    return (size_t)tiles->columns * (size_t)tiles->rows;
}

void boat_map_tiles_destroy(BoatMapTiles *tiles)
{
    free(tiles->tiles);
    free(tiles->water);
    free(tiles->shore);
    free(tiles->land);
    *tiles = (BoatMapTiles) { 0 };
}

const BoatMapTile *boat_map_tiles_at(const BoatMapTiles *tiles, Vector2 p)
{
    if (!tiles->tiles)
        return NULL;
    float x = floorf((p.x - tiles->origin.x) / tiles->tile_size);
    float z = floorf((p.y - tiles->origin.y) / tiles->tile_size);
    if (!(x >= 0 && z >= 0 && x < tiles->columns && z < tiles->rows))
        return NULL;
    return &tiles->tiles[(size_t)z * (size_t)tiles->columns + (size_t)x];
}

bool boat_map_tile_empty(const BoatMapTile *tile)
{
    return !tile->water_count && !tile->shore_count && !tile->land_count;
}

size_t boat_map_tiles_bytes(const BoatMapTiles *tiles)
{
    if (!tiles->tiles)
        return 0;
    return (size_t)tiles->columns * (size_t)tiles->rows * sizeof(*tiles->tiles)
        + (tiles->water_entries + tiles->shore_entries + tiles->land_entries + 3)
        * sizeof(*tiles->water);
}
