#ifndef PROTEUS_BOAT_ARCHIPELAGO_H
#define PROTEUS_BOAT_ARCHIPELAGO_H

#include "core/boat_map.h"
#include "core/boat_model.h"
#include <stdint.h>

// Baked seed: immutable water geometry shared by collision and scenery validation.
extern const BoatMapDescription boat_archipelago_map;
// Presentation water includes the sea below solid pier decks. Never use for navigation.
extern const BoatMapDescription boat_archipelago_water_surface;

typedef struct {
    const char *name;
    Vector2 position; // Navigable approach point, simulation X/Z.
    float radius; // First-arrival radius in metres.
} BoatArchipelagoPlace;

// Stable bake order, home harbour first. The caller borrows process-lifetime data.
const BoatArchipelagoPlace *boat_archipelago_places(size_t *count);

typedef struct {
    Vector3 position, normal; // Pane centre and outward normal in renderer coordinates.
    float width, height;
    uint32_t seed; // Stable room identity, shared with Outer's residential occupancy recipe.
} BoatArchipelagoWindow;
const BoatArchipelagoWindow *boat_archipelago_windows(size_t *count);

typedef struct {
    Vector3 position; // Lantern centre in renderer coordinates, matched to the baked tower.
    float width;
} BoatArchipelagoLantern;
const BoatArchipelagoLantern *boat_archipelago_lanterns(size_t *count);

BoatModelSpec boat_archipelago_fishing_spec(void);
// Two fixed jetty tie points in renderer coordinates for each local berth.
const Vector3 *boat_archipelago_moorings(unsigned berth);

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
