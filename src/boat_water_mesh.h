#ifndef PROTEUS_BOAT_WATER_MESH_H
#define PROTEUS_BOAT_WATER_MESH_H

#include "boat_water.h"

// Presentation-side water geometry built from the shared water queries. The surfaces and
// patches are process caches keyed by the compiled city crop; physics never reads them.

// Borrowed, tessellated surface; renderer X is reflected from simulation X. NULL on allocation
// failure. CPU reference draws the colors and full displaced surface. Each call updates storage.
const Mesh3D *boat_water_surface(BoatWater water, double time, bool mapped);
const Mesh3D *boat_water_surface_field(BoatWater water, const BoatWake *field, double time);
// GPU-only rest topology, clipped to the original shoreline. World X/Z, not simulation X.
// Cached on a fixed 1.5 m grid; shifts by 12 m outside the visible displacement radius.
#define BOAT_WATER_PATCH_FULL 48.0f
#define BOAT_WATER_PATCH_RADIUS 80.0f
const Mesh3D *boat_water_patch(Vector2 world_center, bool mapped);
const Mesh3D *boat_water_patch_arcade(Vector2 world_center, bool mapped);
// Free the cached surfaces and patches; the next call rebuilds them.
void boat_water_mesh_destroy(void);

#endif
