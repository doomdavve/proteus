#ifndef PROTEUS_BOAT_ARCHIPELAGO_H
#define PROTEUS_BOAT_ARCHIPELAGO_H

#include "core/boat_map.h"

// Baked seed: immutable water geometry shared by collision and scenery validation.
extern const BoatMapDescription boat_archipelago_map;

typedef struct {
    const char *name;
    Vector2 position; // Navigable approach point, simulation X/Z.
    float radius; // First-arrival radius in metres.
} BoatArchipelagoPlace;

// Stable bake order, home harbour first. The caller borrows process-lifetime data.
const BoatArchipelagoPlace *boat_archipelago_places(size_t *count);

#endif
