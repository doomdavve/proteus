#include "waterfront_city.h"

#include "../assets/geometry/river/water.h"

static const Mesh3D river = { river_water_vertices, river_water_triangles,
    sizeof(river_water_triangles) / sizeof(river_water_triangles[0]) };

const BoatMapDescription waterfront_city_river_map
    = { &river, sizeof(river_water_triangles) / sizeof(river_water_triangles[0]) };
