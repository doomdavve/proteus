#ifndef PROTEUS_BOAT_LOCATION_H
#define PROTEUS_BOAT_LOCATION_H

#include "raylib.h"

#define BOAT_BASIN_HALF_X 120.0f
#define BOAT_BASIN_HALF_Z 80.0f

typedef enum { BOAT_LOCATION_BASIN, BOAT_LOCATION_RIVER, BOAT_LOCATION_COUNT } BoatLocationId;

// Immutable, process-lifetime location record. Worlds borrow it; nothing stores an active one.
// The mapped record uses the compiled river crop and the boat_map/boat_water_mesh caches.
typedef struct {
    BoatLocationId id;
    const char *name; // Command-line spelling.
    bool mapped; // Shoreline, harbor, traffic and gulls come from the compiled map crop.
    Vector2 half_extent; // Basin wall half-sizes in simulation X/Z; zero for mapped records.
    Vector2 spawn; // Free-boating start position in simulation X/Z.
    Vector2 fleet_spawn; // Multiplayer start area; mapped fleets use open river water.
    // Authored current/crossing-source placement for opt-in water tests.
    float current_center;
    Vector2 wake_offset;
    // Presentation companion values; physics never reads these.
    float camera_distance, camera_height;
    float far_plane;
    Color particle_water; // Base colour that foam and spray fade from.
} BoatWorldDefinition;

const BoatWorldDefinition *boat_location(BoatLocationId id);
// NULL for an unknown name.
const BoatWorldDefinition *boat_location_find(const char *name);

#endif
