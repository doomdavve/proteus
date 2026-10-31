#include "boat_voyage.h"
#include "raymath.h"

#include <math.h>
#include <string.h>

static void notify(BoatVoyage *voyage, BoatVoyageEvent event, unsigned shelter, double time,
    BoatVoyageEvents *events)
{
    if (events)
        events->items[events->count++] = (BoatVoyageNotice) { event, shelter };
    voyage->event = event;
    voyage->event_shelter = shelter;
    voyage->event_time = time;
}

void boat_voyage_start(BoatVoyage *voyage, const BoatVoyageRoute *route, double time)
{
    *voyage = (BoatVoyage) { .route = route, .started = time, .event_time = time };
}

void boat_voyage_restart(BoatVoyage *voyage, const BoatVoyageRoute *route, double time)
{
    BoatVoyage previous = *voyage;
    boat_voyage_start(voyage, route, time);
    voyage->observed = previous.observed;
    voyage->weather = previous.weather;
    voyage->weather_started = previous.weather_started;
}

void boat_voyage_rebind(BoatVoyage *voyage, const BoatVoyageRoute *route, const char *old_id,
    const char *const *new_ids, unsigned new_count)
{
    if (!route)
        return;
    unsigned shelter = 0;
    for (unsigned i = 0; old_id && new_ids && i < new_count && i < route->shelter_count; ++i)
        if (!strcmp(old_id, new_ids[i])) {
            shelter = i;
            break;
        }
    // The destination's identity is its last shelter; keep an arrival only when it survives.
    bool arrived = voyage->arrived && new_count && new_ids
        && !strcmp(old_id ? old_id : "", new_ids[new_count - 1]);
    voyage->route = route;
    voyage->shelter = arrived ? route->shelter_count - 1 : shelter;
    voyage->arrived = arrived;
}

void boat_voyage_step_events(
    BoatVoyage *voyage, Vector2 position, Vector2 velocity, double time, BoatVoyageEvents *events)
{
    if (events)
        *events = (BoatVoyageEvents) { 0 };
    const BoatVoyageRoute *route = voyage->route;
    if (!route)
        return;
    unsigned destination = route->shelter_count - 1;
    for (unsigned i = 0; i < destination; ++i)
        if (i != voyage->shelter
            && Vector2Distance(position, route->shelters[i].position)
                <= route->shelters[i].radius) {
            voyage->shelter = i;
            notify(voyage, BOAT_VOYAGE_SHELTER, i, time, events);
        }
    BoatStorm storm = route->exposure;
    if (!voyage->weather
        && (route->weather_range <= 0
            || Vector2Distance(position, boat_water_storm_centre(storm, time))
                <= route->weather_range)) {
        voyage->weather = true;
        voyage->weather_started = time;
        if (!voyage->observed || route->weather_range <= 0)
            voyage->weather_started -= storm.build; // Already grown when first seen.
        else
            notify(voyage, BOAT_VOYAGE_WEATHER, voyage->shelter, time, events);
    }
    voyage->observed = true;
    bool exposed = storm.radius > 0
        && Vector2Distance(position, boat_water_storm_centre(storm, time))
            < storm.radius * route->exposed_fraction;
    if (exposed != voyage->exposed) {
        voyage->exposed = exposed;
        notify(voyage, exposed ? BOAT_VOYAGE_EXPOSED : BOAT_VOYAGE_SHELTERED, voyage->shelter, time,
            events);
    }
    bool inside = Vector2Distance(position, route->arrival_centre) <= route->arrival_radius;
    bool slow = Vector2Length(velocity) <= route->arrival_speed;
    voyage->too_fast = inside && !slow && !voyage->arrived;
    if (inside && slow && !voyage->arrived) {
        voyage->arrived = true;
        voyage->arrival = time;
        voyage->shelter = destination;
        notify(voyage, BOAT_VOYAGE_ARRIVED, destination, time, events);
    } else if (inside && slow && voyage->shelter != destination) {
        voyage->shelter = destination; // Back at the destination after leaving it.
        notify(voyage, BOAT_VOYAGE_SHELTER, destination, time, events);
    }
}

BoatStorm boat_voyage_weather(const BoatVoyage *voyage)
{
    if (!voyage->route || !voyage->weather)
        return (BoatStorm) { 0 };
    BoatStorm storm = voyage->route->exposure;
    storm.started = voyage->weather_started;
    return storm;
}

void boat_voyage_step(BoatVoyage *voyage, Vector2 position, Vector2 velocity, double time)
{
    boat_voyage_step_events(voyage, position, velocity, time, NULL);
}

void boat_voyage_recover(BoatVoyage *voyage, Boat *boat, double time)
{
    if (!voyage->route)
        return;
    const BoatVoyageShelter *shelter = &voyage->route->shelters[voyage->shelter];
    boat_reset(boat);
    boat->position = shelter->position;
    boat->heading = shelter->heading;
    ++voyage->recoveries;
    voyage->too_fast = false;
    notify(voyage, BOAT_VOYAGE_RECOVERED, voyage->shelter, time, NULL);
}

float boat_voyage_bearing(
    const BoatVoyage *voyage, Vector2 position, float heading, float *distance)
{
    if (!voyage->route) {
        *distance = 0;
        return 0;
    }
    Vector2 to = Vector2Subtract(voyage->route->arrival_centre, position);
    *distance = Vector2Length(to);
    Vector2 forward = { sinf(heading), -cosf(heading) };
    Vector2 right = { cosf(heading), sinf(heading) };
    return atan2f(Vector2DotProduct(to, right), Vector2DotProduct(to, forward));
}

const char *boat_voyage_destination(const BoatVoyageRoute *route)
{
    return route->shelters[route->shelter_count - 1].name;
}
