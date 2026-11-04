#ifndef BOAT_TEST_WORLD_H
#define BOAT_TEST_WORLD_H
#include "boat_test_location.h"
#include "boat_world.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// A validator or review tool's owners of every location's world resources and the river fixture's,
// indexed by BoatLocationId or TEST_RIVER. Each is prepared on first use, so a basin-only check
// never builds the river or outer maps; close them after the last world.
static BoatWorldResources test_worlds[TEST_RIVER + 1];
static bool test_worlds_ready[TEST_RIVER + 1];

static inline const BoatWorldResources *test_world(BoatLocationId id)
{
    if (!test_worlds_ready[id]) {
        if (!boat_world_resources_init(&test_worlds[id], test_location(id))) {
            fprintf(stderr, "Cannot prepare the %s world.\n", test_location(id)->name);
            exit(1);
        }
        test_worlds_ready[id] = true;
    }
    return &test_worlds[id];
}

static inline void test_worlds_close(void)
{
    for (int i = 0; i <= TEST_RIVER; ++i)
        if (test_worlds_ready[i]) {
            boat_world_resources_destroy(&test_worlds[i]);
            test_worlds_ready[i] = false;
        }
}

// Shoreline checks of authored river geometry against the tool's own river fixture map.
static inline bool test_river_water(Vector2 position, float clearance)
{
    return boat_map_water(&test_world(TEST_RIVER)->map, position, clearance);
}

// Shoreline checks of authored race geometry against the outer map, where races run.
static inline bool test_outer_water(Vector2 position, float clearance)
{
    return boat_map_water(&test_world(BOAT_LOCATION_OUTER)->map, position, clearance);
}

#endif
