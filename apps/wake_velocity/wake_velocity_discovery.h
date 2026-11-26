#ifndef PROTEUS_WAKE_VELOCITY_DISCOVERY_H
#define PROTEUS_WAKE_VELOCITY_DISCOVERY_H

#include "core/boat_location.h"

#include <stdint.h>

// Presentation-only visits for one trip. Boat recovery keeps visits; a new launch starts empty.
// Time is simulation time, so pausing also pauses the arrival notice.
typedef struct {
    uint32_t visited;
    unsigned notice; // Place index + 1, or zero.
    double until, previous_time;
} WakeDiscovery;

// The newly reached place remains visible for six simulated seconds. NULL outside Skerry Reach
// or after the notice expires. Neither map/physics nor save data is changed.
const char *wake_discovery_update(
    WakeDiscovery *discovery, BoatLocationId location, Vector2 position, double now);

#endif
