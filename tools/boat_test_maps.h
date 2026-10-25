#ifndef BOAT_TEST_MAPS_H
#define BOAT_TEST_MAPS_H
#include "boat_world.h"

// Synthetic map east of the river's reserved crane footprint: a 40 x 20 m basin with a quay
// wall along z = 0, a 3.5 m quay and one 15 m building wall facing the water. It has derived
// quay and frontage dressing but no authored river kit. Map east is reflected simulation X.
static const Vector3 test_harbor_vertices[] = { { 100, 0, -20 }, { 140, 0, -20 }, { 140, 0, 0 },
    { 100, 0, 0 }, { 100, 3.5f, 0 }, { 140, 3.5f, 0 }, { 140, 3.5f, 30 }, { 100, 3.5f, 30 },
    { 117.5f, 15, 25 }, { 110, 3.5f, 25 }, { 125, 3.5f, 25 } };
static const Triangle3D test_harbor_triangles[]
    = { { { 0, 1, 2 }, { 45, 79, 104, 255 } }, { { 0, 2, 3 }, { 45, 79, 104, 255 } },
          { { 3, 5, 2 }, { 135, 139, 144, 255 } }, { { 4, 6, 5 }, { 135, 139, 144, 255 } },
          { { 4, 7, 6 }, { 135, 139, 144, 255 } }, { { 9, 8, 10 }, { 185, 120, 90, 255 } } };
static const Mesh3D test_harbor_mesh = { test_harbor_vertices, test_harbor_triangles, 6 };
static const BoatMapDescription test_harbor_map = { &test_harbor_mesh, 6 };

// The river location's definition on the synthetic harbor, spawning in its basin.
static inline BoatWorldDefinition test_harbor_definition(const BoatWorldDefinition *river)
{
    BoatWorldDefinition definition = *river;
    definition.map = &test_harbor_map;
    definition.spawn = (Vector2) { -120, -10 };
    return definition;
}

#endif
