#include "boat_location.h"
#include "waterfront_outer.h"

#include <string.h>

static const BoatWorldDefinition locations[BOAT_LOCATION_COUNT] = {
    [BOAT_LOCATION_BASIN] = { .id = BOAT_LOCATION_BASIN,
        .name = "basin",
        .title = "BASIN",
        .half_extent = { BOAT_BASIN_HALF_X, BOAT_BASIN_HALF_Z },
        .spawn = { 0, 10 },
        .camera_distance = 20,
        .camera_height = 14,
        .far_plane = 350,
        .particle_water = { 36, 107, 125, 255 } },
    [BOAT_LOCATION_OUTER] = { .id = BOAT_LOCATION_OUTER,
        .name = "outer",
        .title = "GOTHENBURG HARBOUR",
        .mapped = true,
        .map = &waterfront_outer_map,
        .spawn = { -12, 60 },
        .fleet_spawn = { -50, -290 },
        .current_center = -12,
        .wake_offset = { -140, -220 },
        .upstream = true,
        .camera_distance = 12,
        .camera_height = 6,
        .far_plane = 4500,
        .particle_water = { 45, 79, 104, 255 } },
};

const BoatWorldDefinition *boat_location(BoatLocationId id)
{
    return id >= 0 && id < BOAT_LOCATION_COUNT ? &locations[id] : NULL;
}

const BoatWorldDefinition *boat_location_find(const char *name)
{
    for (int i = 0; i < BOAT_LOCATION_COUNT; ++i)
        if (strcmp(locations[i].name, name) == 0)
            return &locations[i];
    return NULL;
}
