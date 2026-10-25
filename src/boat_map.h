#ifndef PROTEUS_BOAT_MAP_H
#define PROTEUS_BOAT_MAP_H

#include "geometry3d.h"

#include <stdbool.h>

// Read-only, process-lifetime map description. Water triangles come first (all three vertices at
// y = 0), followed by raised land and buildings; triangles from bridge_first_triangle on are
// authored bridge/approach geometry. Coordinates use the map's east axis, reflected relative to
// simulation X.
typedef struct {
    const Mesh3D *mesh;
    size_t bridge_first_triangle;
} BoatMapDescription;

typedef struct {
    unsigned int a, b;
} BoatMapEdge;

// Default collision cell, in metres. It divides the 512 m and 1 km tile sizes, so cells nest
// inside tiles that share the grid's zero origin.
#define BOAT_MAP_CELL_SIZE 16.0f

// Uniform candidate grid in map east/Z. Cell (i, j) spans origin + (i, j) * cell_size. Each cell
// lists, in ascending order, the water triangles and shore edges within a small margin of it.
typedef struct {
    float origin_x, origin_z, cell_size;
    int columns, rows;
    unsigned int *water_start, *shore_start; // columns * rows + 1 offsets into the lists below.
    unsigned int *water; // Water triangle indices.
    unsigned int *shore; // Indices into BoatMap.shore.
} BoatMapGrid;

// Caller-owned collision cache prepared from one description, which it borrows. Queries never
// allocate or mutate it. Never shallow-copy an initialized map; borrow it by pointer.
typedef struct {
    const BoatMapDescription *description;
    BoatMapEdge *shore; // Unshared water-triangle edges, sorted by vertex index.
    size_t shore_count, water_count;
    BoatMapGrid grid;
} BoatMap;

// Candidates one query inspected, for review and tests.
typedef struct {
    unsigned int cells, triangles, edges;
} BoatMapQueryCost;

// Fails without allocation for a missing description or mesh, or a mesh without leading water.
// On failure, including allocation failure, the map is left zeroed, which is safe to destroy.
bool boat_map_init(BoatMap *map, const BoatMapDescription *description);
// The same with a chosen positive collision cell size, for comparisons.
bool boat_map_init_cell(BoatMap *map, const BoatMapDescription *description, float cell_size);
// Safe on a zeroed, failed or already destroyed map.
void boat_map_destroy(BoatMap *map);
bool boat_map_ready(const BoatMap *map);
bool boat_map_is_bridge_triangle(const BoatMapDescription *description, size_t index);
// Positions are simulation X/Z. An unready map has no water.
bool boat_map_water(const BoatMap *map, Vector2 position, float clearance);
// If the point is too close to shore, return the direction back toward open water.
bool boat_map_water_contact(
    const BoatMap *map, Vector2 position, float clearance, Vector2 *water_normal);
// boat_map_water_contact, adding the candidates it inspected to COST (may be NULL).
bool boat_map_water_contact_cost(const BoatMap *map, Vector2 position, float clearance,
    Vector2 *water_normal, BoatMapQueryCost *cost);
// The exhaustive scan of every water triangle and shore edge that the indexed queries reproduce
// exactly: the same containment, nearest edge (the first in shore order on a tie), clearance
// comparison and normal. For tests and review only.
bool boat_map_water_contact_reference(
    const BoatMap *map, Vector2 position, float clearance, Vector2 *water_normal);
// Bytes the map allocated for its shore edges and grid.
size_t boat_map_bytes(const BoatMap *map);
// Margin within which the grid and tiles list an item near a cell or tile, in metres.
#define BOAT_MAP_MARGIN .5f
// Conservative separating-axis test: false only when the convex polygon of COUNT (2 or 3) points
// is farther than MARGIN from the rectangle [LO, HI] on some axis.
bool boat_map_polygon_near_rect(
    const Vector2 *points, int count, Vector2 lo, Vector2 hi, float margin);

#endif
