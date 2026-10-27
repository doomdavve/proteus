#include "waterfront_outer.h"

#include "../assets/geometry/outer/geometry.h"

#include <math.h>

static const Mesh3D outer
    = { outer_vertices, outer_triangles, sizeof(outer_triangles) / sizeof(outer_triangles[0]) };

_Static_assert(sizeof(outer_ground_cm) / sizeof(outer_ground_cm[0])
        == (size_t)outer_ground_columns * outer_ground_rows,
    "Ground lattice size");

float waterfront_outer_ground(float x, float z)
{
    float u = (x - outer_ground_x0) / outer_ground_step;
    float v = (z - outer_ground_z0) / outer_ground_step;
    if (!(u >= 0 && v >= 0 && u <= outer_ground_columns - 1 && v <= outer_ground_rows - 1))
        return 3.5f;
    int i = (int)fminf(floorf(u), outer_ground_columns - 2);
    int j = (int)fminf(floorf(v), outer_ground_rows - 2);
    float fu = u - (float)i, fv = v - (float)j;
    const short *row = &outer_ground_cm[j * outer_ground_columns + i];
    const short *next = row + outer_ground_columns;
    float south = row[0] + (row[1] - row[0]) * fu;
    float north = next[0] + (next[1] - next[0]) * fu;
    return (south + (north - south) * fv) * .01f;
}

const BoatMapDescription waterfront_outer_map = { &outer, outer_bridge_first_triangle };
