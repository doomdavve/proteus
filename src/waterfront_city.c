#include "waterfront_city.h"

#include "../assets/geometry/eriksberg/geometry.h"
#include "../assets/geometry/river/geometry.h"

static bool river_enabled;

void waterfront_city_select_river(bool enabled) { river_enabled = enabled; }

bool waterfront_city_is_bridge_triangle(size_t index)
{
    return river_enabled && index >= river_bridge_first_triangle;
}

const Mesh3D *waterfront_city_mesh_crop(bool extended)
{
    static const Mesh3D mesh = { waterfront_vertices, waterfront_triangles,
        sizeof(waterfront_triangles) / sizeof(waterfront_triangles[0]) };
    static const Mesh3D river
        = { river_vertices, river_triangles, sizeof(river_triangles) / sizeof(river_triangles[0]) };
    return extended ? &river : &mesh;
}

const Mesh3D *waterfront_city_mesh(void) { return waterfront_city_mesh_crop(river_enabled); }
