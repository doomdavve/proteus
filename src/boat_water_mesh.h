#ifndef PROTEUS_BOAT_WATER_MESH_H
#define PROTEUS_BOAT_WATER_MESH_H

#include "boat_map.h"
#include "boat_water.h"

// Presentation-side water geometry built from the shared water queries. Physics never reads it.

typedef struct {
    Vector3 *vertices;
    Triangle3D *triangles;
    size_t count, capacity;
    Mesh3D mesh; // Points into vertices/triangles.
} BoatWaterMeshSurface;

// One view's water geometry, bound to the map description it last built from (NULL: the
// 240 x 160 m basin). Zero-initialize it; asking for a different description rebinds it and
// rebuilds the map-dependent surfaces on that call. Each view owns its own cache. Never copy an
// initialized cache: its meshes point into its own storage. Borrow it by pointer.
typedef struct {
    const BoatMapDescription *map;
    BoatWaterMeshSurface surface; // CPU reference surface of the bound map.
    BoatWaterMeshSurface field; // Basin grid for the retired physical wake field.
    BoatWaterMeshSurface patches[2]; // GPU rest patches: ordinary and arcade (fine) grid.
    Vector2 patch_centers[2];
} BoatWaterMeshCache;

// Every returned mesh is borrowed from CACHE and stays valid until the next call on the same
// cache, or its destruction; other caches never invalidate it. NULL on allocation failure.

// Tessellated surface of MAP's water; renderer X is reflected from simulation X. CPU reference
// draws the colors and full displaced surface. Each call updates the cache's surface.
const Mesh3D *boat_water_surface(
    BoatWaterMeshCache *cache, const BoatMapDescription *map, BoatWater water, double time);
const Mesh3D *boat_water_surface_field(
    BoatWaterMeshCache *cache, BoatWater water, const BoatWake *field, double time);
// GPU-only rest topology, clipped to MAP's water. World X/Z, not simulation X.
// Cached on a fixed 1.5 m grid; shifts by 12 m outside the visible displacement radius.
#define BOAT_WATER_PATCH_FULL 48.0f
#define BOAT_WATER_PATCH_RADIUS 80.0f
const Mesh3D *boat_water_patch(
    BoatWaterMeshCache *cache, const BoatMapDescription *map, Vector2 world_center);
const Mesh3D *boat_water_patch_arcade(
    BoatWaterMeshCache *cache, const BoatMapDescription *map, Vector2 world_center);
// Free the cache's storage and leave it zeroed; safe on a zeroed or destroyed cache.
void boat_water_mesh_cache_destroy(BoatWaterMeshCache *cache);

#endif
