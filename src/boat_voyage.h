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
    // exposed_fraction of its radius the boat counts as exposed. Its CENTRE, RADIUS, HEIGHT
    // and BUILD are authored; the voyage sets STARTED.
    BoatStorm exposure;
    float exposed_fraction;
    // A3.5 weather transition: the patch is flat until the boat first comes within this many
    // metres of its centre, then grows over EXPOSURE.BUILD seconds. A boat first observed in
    // range finds it grown. Zero: grown from the start.
    float weather_range;
} BoatVoyageRoute;

typedef enum {
    BOAT_VOYAGE_NONE,
    BOAT_VOYAGE_SHELTER, // Reached SHELTER.
    BOAT_VOYAGE_EXPOSED, // Entered the exposed water.
    BOAT_VOYAGE_SHELTERED, // Left it again.
    BOAT_VOYAGE_ARRIVED, // Arrived at the destination; SHELTER is its shelter.
    BOAT_VOYAGE_RECOVERED, // Placed at SHELTER.
    BOAT_VOYAGE_WEATHER, // The exposure began to grow.
} BoatVoyageEvent;

// Pointer-free apart from the borrowed route; assignment copies it.
typedef struct {
    const BoatVoyageRoute *route; // NULL: no voyage, every call is a no-op.
    unsigned shelter; // The most recently reached shelter, where recovery places the boat.
    bool exposed, arrived;
    bool too_fast; // Inside the arrival area above the arrival speed, not yet arrived.
    bool observed; // Stepped at least once since the start.
    bool weather; // The exposure has begun to grow, from WEATHER_STARTED.
    double weather_started;
    double started, arrival; // World seconds; ARRIVAL is meaningful once arrived.
    unsigned recoveries;
    BoatVoyageEvent event; // The latest event, at EVENT_TIME, about EVENT_SHELTER.
    unsigned event_shelter;
    double event_time;
} BoatVoyage;

// One operation can emit every shelter, weather, exposure and arrival transition.
#define BOAT_VOYAGE_EVENTS (BOAT_VOYAGE_SHELTERS + 3)
typedef struct {
    BoatVoyageEvent kind;
    unsigned shelter;
} BoatVoyageNotice;
typedef struct {
    BoatVoyageNotice items[BOAT_VOYAGE_EVENTS];
    unsigned count;
} BoatVoyageEvents;

extern const BoatVoyageRoute boat_voyage_outer;

// At the start shelter at world time TIME. A NULL route disables the voyage.
void boat_voyage_start(BoatVoyage *voyage, const BoatVoyageRoute *route, double time);
// Begin ROUTE again at the start shelter at TIME, keeping the weather the boat has already met.
// ROUTE's exposure and weather range must equal the current route's: the water is unchanged.
void boat_voyage_restart(BoatVoyage *voyage, const BoatVoyageRoute *route, double time);
// A live route edit: keep progress, the clock and the boat where they are, and identify the most
// recently reached shelter by OLD_ID among NEW_IDS. A removed shelter falls back to the start
// shelter, which is always a usable recovery. The exposure fields must still agree.
void boat_voyage_rebind(BoatVoyage *voyage, const BoatVoyageRoute *route, const char *old_id,
    const char *const *new_ids, unsigned new_count);
// Observe the boat after a tick at TIME.
void boat_voyage_step_events(
    BoatVoyage *voyage, Vector2 position, Vector2 velocity, double time, BoatVoyageEvents *events);
void boat_voyage_step(BoatVoyage *voyage, Vector2 position, Vector2 velocity, double time);
// The exposure as the location's water carries it now: flat until the weather begins.
BoatStorm boat_voyage_weather(const BoatVoyage *voyage);
// Put a reset BOAT at the most recently reached shelter.
void boat_voyage_recover(BoatVoyage *voyage, Boat *boat, double time);
// Metres to the arrival area's centre and the bearing to it relative to HEADING, in radians,
// positive toward the boat's right (the direction its heading turns with right steering).
float boat_voyage_bearing(
    const BoatVoyage *voyage, Vector2 position, float heading, float *distance);
// The destination's name: its shelter's.
const char *boat_voyage_destination(const BoatVoyageRoute *route);

#endif
