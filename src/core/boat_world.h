#ifndef PROTEUS_BOAT_WORLD_H
#define PROTEUS_BOAT_WORLD_H

#include "boat_location.h"
#include "boat_scene_definition.h"
#include "boat_water.h"

// Caller-owned physical resources of one location: its definition and, when mapped, the
// collision map prepared from its description. Worlds, sessions, fleets and all their copies
// borrow it read-only; create it before them and destroy it after the last borrower. Never
// shallow-copy an initialized owner; borrow it by pointer.
typedef struct {
    const BoatWorldDefinition *definition;
    BoatMap map; // Ready exactly when the definition is mapped.
} BoatWorldResources;

// False, leaving RESOURCES zeroed, for a missing definition, a mapped flag that disagrees with
// the map description, or a map that cannot be prepared. The basin allocates nothing.
bool boat_world_resources_init(
    BoatWorldResources *resources, const BoatWorldDefinition *definition);
// Safe on a zeroed, failed or already destroyed owner.
void boat_world_resources_destroy(BoatWorldResources *resources);
bool boat_world_resources_ready(const BoatWorldResources *resources);

// One physical environment. Assignment clones it: the water history is held by value and the
// resources, arcade wake objects and wake field are borrowed, never owned or freed here.
typedef struct {
    const BoatWorldDefinition *definition; // Always resources->definition.
    const BoatWorldResources *resources; // Shoreline queries use resources->map.
    const struct BoatCombat *combat; // Optional presentation view; simulation uses fleet state.
    BoatWater water; // Settings plus the legacy D3 trail history that boats publish into.
    const BoatWake *wake_field; // Optional legacy CPU field endpoint (basin review only).
    // L1.3: the accepted authored scene, borrowed until the session rebinds or exits. NULL keeps
    // every compiled marina default. Physics and presentation read this same pointer.
    const BoatSceneDefinition *scene;
} BoatWorld;

// RESOURCES must be ready.
void boat_world_init(BoatWorld *world, const BoatWorldResources *resources, BoatWater water);
// Start over with WATER, keeping the borrowed resources; drops any wake field.
void boat_world_restart(BoatWorld *world, BoatWater water);
// True when the surface moves: ambient/scripted/D3/object sources or a borrowed wake field.
bool boat_world_displaced(const BoatWorld *world);
// Study water with the definition's current and crossing-source placement.
BoatWater boat_world_test_water(
    const BoatWorldDefinition *definition, bool waves, bool current, bool crossing);

#endif
