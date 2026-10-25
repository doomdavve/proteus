#include "waterfront_city.h"

#include "../assets/geometry/river/geometry.h"

bool waterfront_city_is_bridge_triangle(size_t index)
{
    return index >= river_bridge_first_triangle;
}

const Mesh3D *waterfront_city_mesh(void)
{
    static const Mesh3D river
        = { river_vertices, river_triangles, sizeof(river_triangles) / sizeof(river_triangles[0]) };
    return &river;
}
