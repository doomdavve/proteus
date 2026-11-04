#include "waterfront_outer.h"

#include "../assets/geometry/outer/water.h"

#include <math.h>

static const Mesh3D outer = { outer_water_vertices, outer_water_triangles,
    sizeof(outer_water_triangles) / sizeof(outer_water_triangles[0]) };

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

const BoatMapDescription waterfront_outer_map
    = { &outer, sizeof(outer_water_triangles) / sizeof(outer_water_triangles[0]) };

WaterfrontGround waterfront_kit_ground(const BoatMapDescription *map)
{
    return map == &waterfront_outer_map ? waterfront_outer_ground : NULL;
}

float waterfront_outer_overhead(float x, float z)
{
    float lowest = INFINITY;
    for (size_t i = 0; i < sizeof(outer_bridges) / sizeof(outer_bridges[0]); ++i) {
        const WaterfrontBridge *b = &outer_bridges[i];
        float dx = x - b->x, dz = z - b->z;
        float s = dx * b->ux + dz * b->uz, w = dz * b->ux - dx * b->uz;
        if (s < b->s0 || s > b->s1 || fabsf(w) > b->half_width)
            continue;
        // Matches outer_bridges.underside: a parabola from the middle of the water to the quays.
        float mid = (b->wet0 + b->wet1) * .5f, half = fmaxf((b->wet1 - b->wet0) * .5f, 1);
        float t = fminf(fabsf(s - mid) / half, 1);
        lowest = fminf(lowest, b->under - (b->under - b->spring) * t * t);
    }
    return lowest;
}

WaterfrontOverhead waterfront_overhead(const BoatMapDescription *map)
{
    return map == &waterfront_outer_map ? waterfront_outer_overhead : NULL;
}
