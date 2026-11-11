#ifndef BOAT_TEST_LOCATION_H
#define BOAT_TEST_LOCATION_H
#include "core/boat_location.h"
#include "core/waterfront_city.h"

// The Eriksberg river crop as a validator's small mapped fixture. It was the river location
// before that retired into the outer map, which holds the same water in the same frame; its
// values are the retired record's. Never a location: boat_location and the network never see it.
#define TEST_RIVER BOAT_LOCATION_COUNT

static const BoatWorldDefinition test_river_definition = { .id = TEST_RIVER,
    .name = "river",
    .mapped = true,
    .map = &waterfront_city_river_map,
    .spawn = { -12, 60 },
    .fleet_spawn = { -50, -290 },
    .current_center = -12,
    .wake_offset = { -140, -220 },
    .camera_distance = 12,
    .camera_height = 6,
    .far_plane = 2200,
    .particle_water = { 45, 79, 104, 255 } };

// A location by ID, or the river fixture for TEST_RIVER.
static inline const BoatWorldDefinition *test_location(BoatLocationId id)
{
    return id == TEST_RIVER ? &test_river_definition : boat_location(id);
}

#endif
