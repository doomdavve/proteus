#include "boat_voyage.h"
#include "raymath.h"

#include <math.h>

// The outer location's first-slice outing (A0.3 route, A3.1 map), in simulation X = -east,
// Z = north. The dock and the river below Älvsborgsbron are sheltered; the east shore of the
// outer fjord sits in the lee of the exposed patch; Långedrag's old harbour basin is the
// destination. The exposure lies over the open fjord west of the direct route's southern leg
// (east -3850): the direct line crosses its eastern flank, the east shore stays nearly calm and
// the middle of the fjord is the full patch. It is an authored encounter, not observed weather.
// It grows over a minute once the boat is 2.5 km off, on the fjord past Älvsborgsbron, so it
// builds while the boat crosses toward it (A3.5).
const BoatVoyageRoute boat_voyage_outer = {
    .shelters = { { "DOCK", { -12, 60 }, 0, 60 }, { "BRIDGE", { 650, -850 }, 1.33f, 150 },
        { "EAST SHORE", { 3250, -2000 }, .86f, 200 },
        { "LANGEDRAG", { 3600, -3370 }, 1.57f, 0 } }, // Facing out of the basin.
    .shelter_count = 4,
    .arrival_centre = { 3595, -3370 },
    .arrival_radius = 75,
    .arrival_speed = 6,
    .exposure = { .centre = { 4300, -2650 }, .radius = 700, .height = 1.6f, .build = 60 },
    .exposed_fraction = .8f,
    .weather_range = 2500,
};

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
