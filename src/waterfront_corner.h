#ifndef PROTEUS_WATERFRONT_CORNER_H
#define PROTEUS_WATERFRONT_CORNER_H

#include "boat_map.h"
#include "boat_map_tiles.h"
#include "renderer3d.h"

// Static city dressing: the authored cafe at OSM way 282574767 and a map-constrained
// kit distributed along streets, exterior facades, and quays. Placements are artistic.
// Corners use the same ordering as the renderer's unit cube.
typedef struct {
    Vector3 vertices[8];
    Color color;
    bool emissive;
    bool waterline; // Wall fittings also drawn by the software reference.
} WaterfrontCornerBox;

// One scene's map-bound scenery, built from the description it borrows (NULL: the basin, which
// has none). Frontages, quays and forecourts are derived from the map. The authored river kit
// (cafe, street furniture, crane and bridge deck) belongs only to the compiled river crop, so
// RIVER is set only for waterfront_city_river_map. Each CPU/GPU view owns one. Never copy an
// initialized scenery; borrow it by pointer. TILES is the map's land-only catalogue at
// TILE_SIZE, which both renderers cull by; it is empty for the basin.
typedef struct {
    const BoatMapDescription *map;
    bool ready, river;
    WaterfrontCornerBox *boxes;
    BoundingBox *bounds; // Per-box world bounds for clearance queries.
    size_t count;
    float tile_size;
    BoatMapTiles tiles;
} WaterfrontScenery;

// Default render tile edge, in metres: chosen over 512 m by the A2.3 matrix.
#define WATERFRONT_TILE_SIZE 1024.0f

// Fails, leaving the scenery zeroed, for a missing or empty mesh, allocation failure or more than
// 16384 boxes. A NULL map succeeds without allocation.
bool waterfront_scenery_init(WaterfrontScenery *scenery, const BoatMapDescription *map);
// The same with a chosen positive render tile size, for comparisons. Rebinding keeps the size.
bool waterfront_scenery_init_tiled(
    WaterfrontScenery *scenery, const BoatMapDescription *map, float tile_size);
// Safe on a zeroed, failed or destroyed scenery.
void waterfront_scenery_destroy(WaterfrontScenery *scenery);
// Keep a ready scenery of MAP, or rebuild it for MAP.
bool waterfront_scenery_bind(WaterfrontScenery *scenery, const BoatMapDescription *map);
// An unready scenery is never clear.
bool waterfront_scenery_clear(const WaterfrontScenery *scenery, Vector3 position, float clearance);
// Authored cafe frame (river crop coordinates), used for its light.
Vector3 waterfront_corner_position(float u, float height, float v);

#endif
