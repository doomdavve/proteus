#ifndef BOAT_TEST_SCENERY_H
#define BOAT_TEST_SCENERY_H
#include "boat_assets.h"
#include "boat_scenery.h"

#include <stdio.h>
#include <stdlib.h>

// Validators and review tools load every mapped location's scenery pack from the source tree
// (PROTEUS_SOURCE_DIR, set by CMake), exiting if one cannot be loaded. Safe to call again.
static inline void test_scenery_load(void)
{
    // Packaged review tools keep assets beside the executable. Repository tools without an
    // adjacent package fall back to the source-tree fixture path below.
    char assets[4096];
    if (boat_asset_path(assets, sizeof(assets), "") && DirectoryExists(assets)
        && boat_scenery_load_all(assets))
        return;
    if (!boat_scenery_load_all(PROTEUS_SOURCE_DIR "/assets/")) {
        fprintf(stderr,
            "Cannot load the scenery packs from " PROTEUS_SOURCE_DIR "/assets/geometry/.\n");
        exit(1);
    }
}

// The loaded scenery of compiled map MAP (the whole bake: water, land, buildings, bridge).
static inline const BoatMapDescription *test_scenery(const BoatMapDescription *map)
{
    test_scenery_load();
    return boat_scenery_of(map);
}

#endif
