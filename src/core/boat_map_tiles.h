#ifndef PROTEUS_BOAT_MAP_TILES_H
#define PROTEUS_BOAT_MAP_TILES_H

#include "boat_map.h"

// One square tile of a map, in map east/Z. Seams are bookkeeping only: they add no shore edges.
// Water triangles and shore edges are listed in every tile they overlap, so queries near a seam
// see the same geometry from either side. Each land triangle (everything after the water,
// bridge included) belongs to exactly one tile, the one holding its centroid, so drawing every
// tile draws it once; land_min/land_max bound those triangles, which may cross the seam.
typedef struct {
    Vector2 min, max; // The tile square.
    Vector3 land_min, land_max; // Bounds of the owned land triangles; min > max when none.
    unsigned int water_first, water_count; // Range in BoatMapTiles.water.
    unsigned int shore_first, shore_count; // Range in BoatMapTiles.shore.
    unsigned int land_first, land_count; // Range in BoatMapTiles.land.
} BoatMapTile;

// Caller-owned, immutable once built. Borrows the description, and the prepared map when one was
// given, which must outlive it. Tile (i, j) spans origin + (i, j) * tile_size; the origin is a
// multiple of tile_size.
typedef struct {
    const BoatMapDescription *description;
    const BoatMap *map; // NULL for a land-only catalogue, which lists no water or shore.
    size_t water_count; // Leading water triangles; land triangles follow.
    float tile_size;
    Vector2 origin;
    int columns, rows;
    BoatMapTile *tiles; // columns * rows, row-major by Z.
    unsigned int *water; // Water triangle indices, ascending within a tile.
    unsigned int *shore; // Indices into map->shore, ascending within a tile.
    unsigned int *land; // Mesh triangle indices, ascending within a tile.
    size_t water_entries, shore_entries, land_entries;
} BoatMapTiles;

// Fails for an unready map or a non-positive size, leaving TILES zeroed (safe to destroy).
bool boat_map_tiles_init(BoatMapTiles *tiles, const BoatMap *map, float tile_size);
// Land ownership and bounds only, from a description without a collision map (renderers). The
// land lists equal those of boat_map_tiles_init for the same description and size. Fails for a
// missing or empty mesh, no leading water or a non-positive size, leaving TILES zeroed.
bool boat_map_tiles_init_land(
    BoatMapTiles *tiles, const BoatMapDescription *description, float tile_size);
// Safe on a zeroed, failed or already destroyed catalogue.
void boat_map_tiles_destroy(BoatMapTiles *tiles);
// The tile containing map east/Z point P, or NULL outside the catalogue.
const BoatMapTile *boat_map_tiles_at(const BoatMapTiles *tiles, Vector2 p);
// Index of the tile that owns geometry centred at P: the containing tile, clamped to the edge
// tiles outside the catalogue. This is the land ownership rule.
size_t boat_map_tiles_owner(const BoatMapTiles *tiles, Vector2 p);
size_t boat_map_tiles_count(const BoatMapTiles *tiles);
bool boat_map_tile_empty(const BoatMapTile *tile);
size_t boat_map_tiles_bytes(const BoatMapTiles *tiles);

#endif
