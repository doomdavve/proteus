#include "waterfront_city.h"

#include "../assets/geometry/river/geometry.h"

static const Mesh3D river
    = { river_vertices, river_triangles, sizeof(river_triangles) / sizeof(river_triangles[0]) };

const BoatMapDescription waterfront_city_river_map = { &river, river_bridge_first_triangle };
