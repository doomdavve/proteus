#ifndef PROTEUS_BOAT_VOYAGE_H
#define PROTEUS_BOAT_VOYAGE_H

#include "boat.h"

// Adventure A3.4 (docs/FREE_RIDING_ADVENTURE_FIRST_STEPS.md): one optional destination,
// sheltered recovery points and a patch of exposed rough water, authored in a mapped location's
// simulation X/Z. Free boating continues whether or not the destination is pursued. Observing a
// voyage never changes the boat; only a recovery places it.
#define BOAT_VOYAGE_SHELTERS 4

typedef struct {
    const char *name; // HUD spelling, upper-case ASCII.
    Vector2 position; // Recovery position in open, navigable water.
    float heading; // Recovery heading in radians; forward is (sin, -cos).
    float radius; // Passing within this distance reaches the shelter.
} BoatVoyageShelter;

typedef struct BoatVoyageRoute {
    // Shelter 0 is the start. The last is the destination's, reached only by arriving.
    BoatVoyageShelter shelters[BOAT_VOYAGE_SHELTERS];
    unsigned shelter_count;
    Vector2 arrival_centre;
    float arrival_radius;
    float arrival_speed; // Arrive inside the area at or below this ground speed (m/s).
    // Stationary rough water carried by the location's free-boating water. Inside
    // exposed_fraction of its radius the boat counts as exposed.
    BoatStorm exposure;
    float exposed_fraction;
} BoatVoyageRoute;

typedef enum {
    BOAT_VOYAGE_NONE,
    BOAT_VOYAGE_SHELTER, // Reached SHELTER.
    BOAT_VOYAGE_EXPOSED, // Entered the exposed water.
    BOAT_VOYAGE_SHELTERED, // Left it again.
    BOAT_VOYAGE_ARRIVED, // Arrived at the destination; SHELTER is its shelter.
    BOAT_VOYAGE_RECOVERED, // Placed at SHELTER.
} BoatVoyageEvent;

// Pointer-free apart from the borrowed route; assignment copies it.
typedef struct {
    const BoatVoyageRoute *route; // NULL: no voyage, every call is a no-op.
    unsigned shelter; // The most recently reached shelter, where recovery places the boat.
    bool exposed, arrived;
    bool too_fast; // Inside the arrival area above the arrival speed, not yet arrived.
    double started, arrival; // World seconds; ARRIVAL is meaningful once arrived.
    unsigned recoveries;
    BoatVoyageEvent event; // The latest event, at EVENT_TIME, about EVENT_SHELTER.
    unsigned event_shelter;
    double event_time;
} BoatVoyage;

extern const BoatVoyageRoute boat_voyage_outer;

// At the start shelter at world time TIME. A NULL route disables the voyage.
void boat_voyage_start(BoatVoyage *voyage, const BoatVoyageRoute *route, double time);
// Observe the boat after a tick at TIME.
void boat_voyage_step(BoatVoyage *voyage, Vector2 position, Vector2 velocity, double time);
// Put a reset BOAT at the most recently reached shelter.
void boat_voyage_recover(BoatVoyage *voyage, Boat *boat, double time);
// Metres to the arrival area's centre and the bearing to it relative to HEADING, in radians,
// positive toward the boat's right (the direction its heading turns with right steering).
float boat_voyage_bearing(
    const BoatVoyage *voyage, Vector2 position, float heading, float *distance);
// The destination's name: its shelter's.
const char *boat_voyage_destination(const BoatVoyageRoute *route);

#endif
