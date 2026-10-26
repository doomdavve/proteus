#ifndef BOAT_TEST_WORLD_H
#define BOAT_TEST_WORLD_H
#include "boat_world.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// A validator or review tool's owners of every location's world resources, indexed by
// BoatLocationId. Each is prepared on first use, so a basin-only check never builds the river
// or outer maps; close them after the last world.
static BoatWorldResources test_worlds[BOAT_LOCATION_COUNT];
static bool test_worlds_ready[BOAT_LOCATION_COUNT];

static inline const BoatWorldResources *test_world(BoatLocationId id)
{
    if (!test_worlds_ready[id]) {
        if (!boat_world_resources_init(&test_worlds[id], boat_location(id))) {
            fprintf(stderr, "Cannot prepare the %s world.\n", boat_location(id)->name);
            exit(1);
        }
        test_worlds_ready[id] = true;
    }
    return &test_worlds[id];
}

static inline void test_worlds_close(void)
{
    for (int i = 0; i < BOAT_LOCATION_COUNT; ++i)
        if (test_worlds_ready[i]) {
            boat_world_resources_destroy(&test_worlds[i]);
            test_worlds_ready[i] = false;
        }
}

// Shoreline checks of authored river geometry against the tool's own river map.
static inline bool test_river_water(Vector2 position, float clearance)
{
    return boat_map_water(&test_world(BOAT_LOCATION_RIVER)->map, position, clearance);
}

#endif
