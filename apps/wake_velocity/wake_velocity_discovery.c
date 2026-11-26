#include "wake_velocity/wake_velocity_discovery.h"

#include "core/boat_archipelago.h"

const char *wake_discovery_update(
    WakeDiscovery *discovery, BoatLocationId location, Vector2 position, double now)
{
    if (location != BOAT_LOCATION_ARCHIPELAGO)
        return NULL;
    size_t count;
    const BoatArchipelagoPlace *places = boat_archipelago_places(&count);
    // Recovering the boat rewinds the session clock, but does not erase the trip's visits.
    if (now < discovery->previous_time)
        discovery->notice = 0;
    discovery->previous_time = now;
    for (size_t i = 0; i < count && i < 32; ++i) {
        uint32_t bit = UINT32_C(1) << i;
        float dx = position.x - places[i].position.x;
        float dz = position.y - places[i].position.y;
        if (!(discovery->visited & bit)
            && dx * dx + dz * dz <= places[i].radius * places[i].radius) {
            discovery->visited |= bit;
            discovery->notice = (unsigned)i + 1;
            discovery->until = now + 6;
            break;
        }
    }
    return discovery->notice && now < discovery->until ? places[discovery->notice - 1].name : NULL;
}
