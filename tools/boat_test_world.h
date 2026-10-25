#ifndef BOAT_TEST_WORLD_H
#define BOAT_TEST_WORLD_H
#include "boat_world.h"

#include <stdio.h>
#include <stdlib.h>

// A validator or review tool's owners of every location's world resources, indexed by
// BoatLocationId. Open them before the first world and close them after the last.
static BoatWorldResources test_worlds[BOAT_LOCATION_COUNT];

static inline void test_worlds_open(void)
{
    for (int i = 0; i < BOAT_LOCATION_COUNT; ++i)
        if (!boat_world_resources_init(&test_worlds[i], boat_location((BoatLocationId)i))) {
            fprintf(
                stderr, "Cannot prepare the %s world.\n", boat_location((BoatLocationId)i)->name);
            exit(1);
        }
}

static inline void test_worlds_close(void)
{
    for (int i = 0; i < BOAT_LOCATION_COUNT; ++i)
        boat_world_resources_destroy(&test_worlds[i]);
}

static inline const BoatWorldResources *test_world(BoatLocationId id) { return &test_worlds[id]; }

// Shoreline checks of authored river geometry against the tool's own river map.
static inline bool test_river_water(Vector2 position, float clearance)
{
    return boat_map_water(&test_worlds[BOAT_LOCATION_RIVER].map, position, clearance);
}

#endif
