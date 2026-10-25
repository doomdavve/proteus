#include "waterfront_city.h"

#include "../assets/geometry/river/geometry.h"

static const Mesh3D river
    = { river_vertices, river_triangles, sizeof(river_triangles) / sizeof(river_triangles[0]) };

const BoatMapDescription waterfront_city_river_map = { &river, river_bridge_first_triangle };

const BoatMapDescription *waterfront_city_map(void) { return &waterfront_city_river_map; }

const Mesh3D *waterfront_city_mesh(void) { return waterfront_city_river_map.mesh; }

bool waterfront_city_is_bridge_triangle(size_t index)
{
    return boat_map_is_bridge_triangle(&waterfront_city_river_map, index);
}
