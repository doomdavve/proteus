#include "waterfront_outer.h"

#include "../assets/geometry/outer/geometry.h"

static const Mesh3D outer
    = { outer_vertices, outer_triangles, sizeof(outer_triangles) / sizeof(outer_triangles[0]) };

const BoatMapDescription waterfront_outer_map = { &outer, outer_bridge_first_triangle };
