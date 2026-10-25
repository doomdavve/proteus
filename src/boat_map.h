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

// Caller-owned collision cache prepared from one description, which it borrows. Queries never
// allocate or mutate it. Never shallow-copy an initialized map; borrow it by pointer.
typedef struct {
    const BoatMapDescription *description;
    BoatMapEdge *shore; // Unshared water-triangle edges, sorted by vertex index.
    size_t shore_count, water_count;
} BoatMap;

// Fails without allocation for a missing description or mesh, or a mesh without leading water.
// On failure the map is left zeroed, which is safe to destroy.
bool boat_map_init(BoatMap *map, const BoatMapDescription *description);
// Safe on a zeroed, failed or already destroyed map.
void boat_map_destroy(BoatMap *map);
bool boat_map_ready(const BoatMap *map);
bool boat_map_is_bridge_triangle(const BoatMapDescription *description, size_t index);
// Positions are simulation X/Z. An unready map has no water.
bool boat_map_water(const BoatMap *map, Vector2 position, float clearance);
// If the point is too close to shore, return the direction back toward open water.
bool boat_map_water_contact(
    const BoatMap *map, Vector2 position, float clearance, Vector2 *water_normal);

#endif
