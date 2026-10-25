#include "boat_world.h"

#include <assert.h>

bool boat_world_resources_init(BoatWorldResources *resources, const BoatWorldDefinition *definition)
{
    *resources = (BoatWorldResources) { 0 };
    if (!definition || definition->mapped != (definition->map != NULL))
        return false;
    if (definition->mapped && !boat_map_init(&resources->map, definition->map))
        return false;
    resources->definition = definition;
    return true;
}

void boat_world_resources_destroy(BoatWorldResources *resources)
{
    boat_map_destroy(&resources->map);
    *resources = (BoatWorldResources) { 0 };
}

bool boat_world_resources_ready(const BoatWorldResources *resources)
{
    if (!resources || !resources->definition)
        return false;
    if (!resources->definition->mapped)
        return !resources->map.shore;
    return boat_map_ready(&resources->map)
        && resources->map.description == resources->definition->map;
}

void boat_world_init(BoatWorld *world, const BoatWorldResources *resources, BoatWater water)
{
    assert(boat_world_resources_ready(resources));
    *world = (BoatWorld) {
        .definition = resources->definition, .resources = resources, .water = water
    };
}

void boat_world_restart(BoatWorld *world, BoatWater water)
{
    boat_world_init(world, world->resources, water);
}

bool boat_world_displaced(const BoatWorld *world)
{
    return world->wake_field || boat_water_displaced(world->water);
}

BoatWater boat_world_test_water(
    const BoatWorldDefinition *definition, bool waves, bool current, bool crossing)
{
    BoatWater water = boat_water_test(waves, current, crossing);
    water.current_center = definition->current_center;
    water.wake_offset = definition->wake_offset;
    return water;
}
