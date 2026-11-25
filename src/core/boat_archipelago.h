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

struct BoatSceneDefinition;
// Native local life only: independent of plugin-authored scenes and the public-room digest.
const struct BoatSceneDefinition *boat_archipelago_life(void);

typedef struct {
    Vector3 start, end; // Simulation X, terrain Y, simulation Z; reflected only by presentation.
    float period, phase;
    Color coat;
} BoatArchipelagoResident;

const BoatArchipelagoResident *boat_archipelago_residents(size_t *count);
// Smooth conservative terrain envelope for flying scenery; not water depth or collision.
float boat_archipelago_ground(Vector2 position);

#endif
