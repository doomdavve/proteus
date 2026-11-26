#include "core/boat_archipelago.h"
#include "wake_velocity/wake_velocity_discovery.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    size_t count;
    const BoatArchipelagoPlace *places = boat_archipelago_places(&count);
    assert(count >= 3 && count <= 16);
    WakeDiscovery trip = { 0 };
    assert(!wake_discovery_update(&trip, BOAT_LOCATION_OUTER, places[0].position, 0));
    assert(!trip.visited);
    const char *name
        = wake_discovery_update(&trip, BOAT_LOCATION_ARCHIPELAGO, places[0].position, 0);
    assert(name && !strcmp(name, places[0].name));
    assert(trip.visited == 1);
    assert(wake_discovery_update(&trip, BOAT_LOCATION_ARCHIPELAGO, places[0].position, 5.99));
    assert(!wake_discovery_update(&trip, BOAT_LOCATION_ARCHIPELAGO, places[0].position, 6));
    // Leaving and returning to home never retriggers its notice on the same trip.
    Vector2 sea = { 100000, 100000 };
    assert(!wake_discovery_update(&trip, BOAT_LOCATION_ARCHIPELAGO, sea, 8));
    assert(!wake_discovery_update(&trip, BOAT_LOCATION_ARCHIPELAGO, places[0].position, 10));
    // A distinct destination is found, while time rewind/recovery retains the visited set.
    name = wake_discovery_update(&trip, BOAT_LOCATION_ARCHIPELAGO, places[1].position, 15);
    assert(name && !strcmp(name, places[1].name));
    assert((trip.visited & 3) == 3);
    assert(!wake_discovery_update(&trip, BOAT_LOCATION_ARCHIPELAGO, places[0].position, 0));
    assert((trip.visited & 3) == 3);
    WakeDiscovery next_trip = { 0 };
    assert(wake_discovery_update(&next_trip, BOAT_LOCATION_ARCHIPELAGO, places[0].position, 0));
    puts("wake_discovery=ok");
    return 0;
}
