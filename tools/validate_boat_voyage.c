/* A3.4: the outer location's voyage. Shelters and the destination lie in navigable water of the
 * baked map; the exposed rough water crosses the direct route's southern leg while the east shore
 * and every shelter stay outside it; the voyage reaches shelters, reports exposure, arrives only
 * at controlled speed and recovers to the last shelter; the bearing cue turns the way right
 * steering turns the boat; and an autopiloted boat from the east shore arrives at Långedrag.
 * A3.5: the exposure is calm until the boat comes within range, then grows smoothly. */
#include "boat_session.h"
#include "boat_steering.h"
#include "boat_throttle.h"
#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

// The A0.3 slice route's southern legs, in map east/north metres (survey.py SLICE_ROUTE).
static const Vector2 direct[] = { { -3300, -1550 }, { -3850, -2400 }, { -3850, -3150 } };
// A coastal alternative along the east shore of the outer fjord.
static const Vector2 coastal[]
    = { { -3300, -1550 }, { -3600, -2300 }, { -3680, -2700 }, { -3700, -3000 }, { -3720, -3150 } };
#define COASTAL ((int)(sizeof(coastal) / sizeof(coastal[0])))

static Vector2 sim(Vector2 east_north) { return (Vector2) { -east_north.x, east_north.y }; }

// Smallest distance to the exposure centre, relative to its radius, along a polyline.
static float closest(const Vector2 *points, int count, Vector2 centre, float radius)
{
    float best = INFINITY;
    for (int i = 1; i < count; ++i)
        for (int s = 0; s <= 100; ++s) {
            Vector2 p = Vector2Lerp(sim(points[i - 1]), sim(points[i]), (float)s / 100);
            best = fminf(best, Vector2Distance(p, centre) / radius);
        }
    return best;
}

typedef struct {
    bool arrived;
    unsigned capsizes;
    double seconds, exposed_seconds;
    float peak_heave; // Largest boat height above calm water while exposed.
} Run;

// Autopilot from the fjord turn (the first point of PATH) along PATH, then on the voyage's cue
// into the harbour, slowing near it on the same thrust whatever the engine. A capsize recovers to
// the last shelter, as Backspace does.
static Run autopilot(const BoatWorldResources *world, BoatWater water, const Vector2 *path,
    int count, const BoatHandling *handling)
{
    Run run = { 0 };
    BoatSession session;
    boat_session_init(&session, world, water);
    session.presentation_enabled = false;
    session.handling = handling;
    session.boat.position = sim(path[0]);
    Vector2 first = Vector2Subtract(sim(path[1]), session.boat.position);
    session.boat.heading = atan2f(first.x, -first.y);
    uint64_t limit = (uint64_t)(300 / BOAT_TICK_SECONDS), capsized_at = 0;
    int waypoint = 1;
    while (!session.voyage.arrived && session.tick < limit) {
        float distance;
        float bearing = boat_voyage_bearing(
            &session.voyage, session.boat.position, session.boat.heading, &distance);
        if (waypoint < count) {
            BoatVoyage leg = session.voyage;
            BoatVoyageRoute towards = *session.voyage.route;
            towards.arrival_centre = sim(path[waypoint]);
            leg.route = &towards;
            float remaining;
            bearing = boat_voyage_bearing(
                &leg, session.boat.position, session.boat.heading, &remaining);
            if (remaining < 60)
                ++waypoint;
        }
        // Slow settings give the same push on either engine.
        float slow = boat_handling_speedboat.thrust / handling->thrust;
        float target = distance > 250 ? 1.0f : distance > 120 ? .35f * slow : .15f * slow;
        float wheel = Clamp(bearing * 1.5f, -1, 1);
        boat_session_step(&session,
            (BoatInput) { .steering_rate = boat_steering_rate(session.boat.wheel, wheel),
                .throttle_rate = boat_throttle_rate(session.boat.throttle, target) });
        if (session.voyage.exposed) {
            run.exposed_seconds += BOAT_TICK_SECONDS;
            run.peak_heave = fmaxf(run.peak_heave, session.boat.height);
        }
        if (session.boat.motion_state != BOAT_CAPSIZED)
            capsized_at = 0;
        else if (!capsized_at)
            capsized_at = session.tick;
        else if (session.tick - capsized_at > (uint64_t)(1 / BOAT_TICK_SECONDS)) {
            ++run.capsizes;
            boat_session_recover(&session);
            capsized_at = 0;
        }
    }
    run.arrived = session.voyage.arrived
        && session.voyage.shelter == session.voyage.route->shelter_count - 1
        && Vector2Length(session.boat.velocity) <= session.voyage.route->arrival_speed;
    run.seconds = session.voyage.arrival - session.voyage.started;
    boat_session_destroy(&session);
    return run;
}

// Flat-out ground speed after a minute at full throttle on calm open water. The boat is held
// at one point of the fjord so that no shore interferes; speed does not depend on position.
static float top_speed(const BoatWorldResources *world, BoatWater water, const BoatHandling *h)
{
    BoatSession session;
    boat_session_init(&session, world, water);
    session.presentation_enabled = false;
    session.handling = h;
    Vector2 hold = sim((Vector2) { -3850, -2700 });
    while (session.tick < (uint64_t)(60 / BOAT_TICK_SECONDS)) {
        session.boat.position = hold;
        boat_session_step(&session,
            (BoatInput) { .throttle_rate = boat_throttle_rate(session.boat.throttle, 1) });
    }
    float speed = Vector2Length(session.boat.velocity);
    boat_session_destroy(&session);
    return speed;
}

int main(void)
{
    const BoatWorldDefinition *outer = boat_location(BOAT_LOCATION_OUTER);
    CHECK(outer->voyage == &boat_voyage_outer);
    CHECK(
        !boat_location(BOAT_LOCATION_RIVER)->voyage && !boat_location(BOAT_LOCATION_BASIN)->voyage);
    const BoatVoyageRoute *route = outer->voyage;
    BoatWorldResources world;
    CHECK(boat_world_resources_init(&world, outer));

    // Shelters and the arrival area are open water with room for the hull; the start shelter
    // is the location's spawn.
    CHECK(route->shelter_count >= 2 && route->shelter_count <= BOAT_VOYAGE_SHELTERS);
    CHECK(Vector2Equals(route->shelters[0].position, outer->spawn));
    for (unsigned i = 0; i < route->shelter_count; ++i)
        CHECK(boat_map_water(&world.map, route->shelters[i].position, 8));
    CHECK(boat_map_water(&world.map, route->arrival_centre, 20));
    CHECK(Vector2Distance(route->shelters[route->shelter_count - 1].position, route->arrival_centre)
        < route->arrival_radius);
    CHECK(strcmp(boat_voyage_destination(route), "LANGEDRAG") == 0);

    // The exposure: the direct route enters the exposed zone but not its middle, the coastal
    // alternative stays clear of it, and no shelter or the arrival area lies in rough water.
    BoatStorm storm = route->exposure;
    CHECK(storm.radius > 0 && storm.height > 0 && storm.velocity.x == 0 && storm.velocity.y == 0);
    float direct_reach = closest(direct, 3, storm.centre, storm.radius);
    float coastal_reach = closest(coastal, COASTAL, storm.centre, storm.radius);
    CHECK(direct_reach < route->exposed_fraction && direct_reach > .3f);
    CHECK(coastal_reach > route->exposed_fraction);
    for (int i = 1; i < COASTAL; ++i)
        for (int s = 0; s <= 100; ++s)
            CHECK(boat_map_water(
                &world.map, Vector2Lerp(sim(coastal[i - 1]), sim(coastal[i]), (float)s / 100), 8));
    for (unsigned i = 0; i < route->shelter_count; ++i)
        CHECK(Vector2Distance(route->shelters[i].position, storm.centre) > storm.radius);
    CHECK(Vector2Distance(route->arrival_centre, storm.centre)
        > storm.radius + route->arrival_radius);

    // The free-boating water carries the grown patch: rough at its centre, untouched at the
    // harbour.
    BoatWater water = boat_world_test_water(outer, false, false, false);
    water.storm = storm;
    float low = INFINITY, high = -INFINITY;
    for (int i = 0; i < 200; ++i) {
        double t = storm.build + i * .1;
        BoatWaterSample s = boat_water_query(water, storm.centre, t);
        low = fminf(low, s.height);
        high = fmaxf(high, s.height);
        CHECK(boat_water_query(water, route->arrival_centre, t).height == 0);
    }
    CHECK(high - low > 1.5f);

    // A3.5: the patch grows from flat water over BUILD seconds. Growth is smooth, its height
    // rate includes the growth, and the shaders get the grown height.
    CHECK(storm.build >= 30 && route->weather_range > storm.radius * 2);
    float rate;
    CHECK(boat_water_storm_grown(storm, -1, &rate) == 0 && rate == 0);
    CHECK(boat_water_storm_grown(storm, storm.build * .5, &rate) == .5f && rate > 0);
    CHECK(boat_water_storm_grown(storm, storm.build + 1, &rate) == 1 && rate == 0);
    CHECK(boat_water_query(water, storm.centre, -1).height == 0);
    float uniforms[12];
    boat_water_storm_uniforms(storm, storm.build * .5, uniforms);
    CHECK(uniforms[3] == storm.height * .5f);
    Vector2 flank = Vector2Add(storm.centre, (Vector2) { 120, -80 });
    float worst = 0, rising = 0;
    for (int i = 1; i < 600; ++i) {
        double t = i * .1, h = 1e-3;
        float above = boat_water_query(water, flank, t + h).height;
        float below = boat_water_query(water, flank, t - h).height;
        float expected = (float)((above - below) / (2 * h));
        float actual = boat_water_query(water, flank, t).velocity.y;
        worst = fmaxf(worst, fabsf(actual - expected));
        rising = fmaxf(rising, fabsf(boat_water_query(water, flank, t).height));
        if (t < storm.build * .05)
            CHECK(fabsf(boat_water_query(water, flank, t).height) < .02f * storm.height);
    }
    CHECK(worst < .02f && rising > .5f);

    // The weather begins once the boat comes within range, with a notice, and then stays.
    BoatVoyage weather;
    boat_voyage_start(&weather, route, 0);
    CHECK(boat_voyage_weather(&weather).radius == 0);
    boat_voyage_step(&weather, route->shelters[0].position, (Vector2) { 0 }, 1);
    boat_voyage_step(&weather, route->shelters[1].position, (Vector2) { 0 }, 40);
    CHECK(!weather.weather && boat_voyage_weather(&weather).radius == 0);
    Vector2 onset = Vector2Add(storm.centre, (Vector2) { -route->weather_range + 10, 0 });
    boat_voyage_step(&weather, onset, (Vector2) { 0 }, 100);
    CHECK(weather.weather && weather.weather_started == 100);
    CHECK(weather.event == BOAT_VOYAGE_WEATHER && !weather.exposed);
    BoatStorm grown = boat_voyage_weather(&weather);
    CHECK(grown.started == 100 && grown.radius == storm.radius && grown.height == storm.height);
    boat_voyage_step(&weather, route->shelters[1].position, (Vector2) { 0 }, 140);
    CHECK(weather.weather && weather.weather_started == 100);
    // A boat first seen in range (a --start placement) finds the patch already grown.
    boat_voyage_start(&weather, route, 0);
    boat_voyage_step(&weather, storm.centre, (Vector2) { 0 }, 2);
    CHECK(weather.weather && weather.weather_started == 2 - storm.build);
    CHECK(weather.event == BOAT_VOYAGE_EXPOSED);

    // State machine on synthetic positions.
    BoatVoyage voyage;
    boat_voyage_start(&voyage, route, 10);
    CHECK(voyage.shelter == 0 && !voyage.arrived && !voyage.exposed);
    Vector2 still = { 0, 0 }, fast = { 12, 0 }, slow = { 3, 0 };
    boat_voyage_step(&voyage, route->shelters[1].position, fast, 20);
    CHECK(voyage.shelter == 1 && voyage.event == BOAT_VOYAGE_SHELTER && voyage.event_shelter == 1);
    boat_voyage_step(&voyage, storm.centre, fast, 30);
    CHECK(voyage.exposed && voyage.event == BOAT_VOYAGE_EXPOSED && voyage.shelter == 1);
    boat_voyage_step(&voyage, route->shelters[2].position, fast, 40);
    CHECK(!voyage.exposed && voyage.shelter == 2 && voyage.event == BOAT_VOYAGE_SHELTERED);
    boat_voyage_step(&voyage, route->arrival_centre, fast, 50);
    CHECK(voyage.too_fast && !voyage.arrived && voyage.shelter == 2);
    boat_voyage_step(&voyage, route->arrival_centre, slow, 51);
    CHECK(voyage.arrived && !voyage.too_fast && voyage.arrival == 51);
    CHECK(voyage.shelter == route->shelter_count - 1 && voyage.event == BOAT_VOYAGE_ARRIVED);
    // Leaving and coming back slowly reaches the destination's shelter again, without a second
    // arrival.
    boat_voyage_step(&voyage, route->shelters[1].position, still, 60);
    CHECK(voyage.shelter == 1);
    boat_voyage_step(&voyage, route->arrival_centre, slow, 70);
    CHECK(voyage.shelter == route->shelter_count - 1 && voyage.arrival == 51);
    CHECK(voyage.event == BOAT_VOYAGE_SHELTER);

    // Bearing: dead ahead is zero; a target to the boat's right is positive.
    float distance;
    BoatVoyage cue;
    boat_voyage_start(&cue, route, 0);
    Vector2 from = Vector2Add(route->arrival_centre, (Vector2) { 0, 500 });
    CHECK(fabsf(boat_voyage_bearing(&cue, from, 0, &distance)) < 1e-4f);
    CHECK(fabsf(distance - 500) < .01f);
    CHECK(fabsf(boat_voyage_bearing(&cue, from, -PI / 2, &distance) - PI / 2) < 1e-4f);

    // A session at the outer location: right steering turns the heading the positive way, so the
    // cue's sign is the steering direction.
    BoatSession session;
    boat_session_init(&session, &world, water);
    session.presentation_enabled = false;
    CHECK(session.voyage.route == route && session.voyage.shelter == 0);
    CHECK(session.world.water.storm.radius == 0); // Calm until the weather begins.
    float start_heading = session.boat.heading;
    for (int tick = 0; tick < 360; ++tick)
        boat_session_step(&session,
            (BoatInput) { .steering_rate = 1,
                .throttle_rate = boat_throttle_rate(session.boat.throttle, .5f) });
    CHECK(remainderf(session.boat.heading - start_heading, 2 * PI) > .2f);
    CHECK(session.world.water.storm.radius == 0 && !session.voyage.weather);

    // Recovery returns to the latest shelter with a reset boat; the clock continues.
    session.voyage.shelter = 1;
    uint64_t tick = session.tick;
    boat_session_recover(&session);
    CHECK(session.tick == tick && session.voyage.recoveries == 1);
    CHECK(Vector2Equals(session.boat.position, route->shelters[1].position));
    CHECK(session.boat.heading == route->shelters[1].heading);
    CHECK(Vector2Length(session.boat.velocity) == 0);
    CHECK(session.voyage.event == BOAT_VOYAGE_RECOVERED && session.voyage.event_shelter == 1);
    boat_session_destroy(&session);

    // Both ways from the fjord turn arrive at controlled speed. The coastal way stays calm; the
    // direct line crosses the exposed flank.
    Run calm = autopilot(&world, water, coastal, COASTAL, &boat_handling_speedboat);
    Run rough = autopilot(&world, water, direct, 3, &boat_handling_speedboat);
    CHECK(calm.arrived && calm.capsizes == 0 && calm.exposed_seconds == 0);
    CHECK(rough.arrived && rough.exposed_seconds > 5);

    // The explorer is about 30 m/s (60 knots) flat out against the speedboat's 23 m/s, and still
    // makes both ways in, sooner and at controlled arrival speed.
    BoatWater flat = boat_world_test_water(outer, false, false, false);
    float stock = top_speed(&world, flat, &boat_handling_speedboat);
    float explorer = top_speed(&world, flat, &boat_handling_explorer);
    float cabin = top_speed(&world, flat, &boat_handling_cabin);
    CHECK(cabin > 12.75f && cabin < 12.97f); // 25 knots = 12.861 m/s, calm water.
    CHECK(stock > 21 && stock < 25);
    CHECK(explorer > 29 && explorer < 31);
    Run quick_calm = autopilot(&world, water, coastal, COASTAL, &boat_handling_explorer);
    Run quick_rough = autopilot(&world, water, direct, 3, &boat_handling_explorer);
    CHECK(quick_calm.arrived && quick_calm.capsizes == 0 && quick_calm.seconds < calm.seconds);
    CHECK(quick_rough.arrived && quick_rough.seconds < rough.seconds);
    BoatSession cabin_session;
    boat_session_init(&cabin_session, &world, flat);
    cabin_session.handling = &boat_handling_cabin;
    boat_session_restart(&cabin_session, flat);
    CHECK(cabin_session.handling == &boat_handling_cabin);
    boat_session_start_course(&cabin_session);
    CHECK(cabin_session.course.enabled);
    BoatSession course_standard;
    boat_session_init(&course_standard, &world, flat);
    boat_session_start_course(&course_standard);
    cabin_session.presentation_enabled = false;
    course_standard.presentation_enabled = false;
    for (int tick = 0; tick < 600; ++tick) {
        BoatInput input = { .throttle_rate = 1 };
        boat_session_step(&cabin_session, input);
        boat_session_step(&course_standard, input);
    }
    CHECK(Vector2Distance(cabin_session.boat.velocity, course_standard.boat.velocity) < .00001f);
    CHECK(Vector2Distance(cabin_session.boat.position, course_standard.boat.position) < .00001f);
    boat_session_destroy(&course_standard);
    boat_session_destroy(&cabin_session);
    boat_world_resources_destroy(&world);
    printf("cabin calm top speed %.2f m/s (%.1f knots)\n", cabin, cabin / .514444f);
    printf("boat_voyage=ok reach direct=%.2f coastal=%.2f; centre swell %.2f m; coastal %.1f s; "
           "direct %.1f s, %.1f s exposed, heave %.2f m, %u capsizes; top speed %.1f m/s, "
           "explorer %.1f m/s: coastal %.1f s, direct %.1f s, heave %.2f m, %u capsizes\n",
        direct_reach, coastal_reach, high - low, calm.seconds, rough.seconds, rough.exposed_seconds,
        rough.peak_heave, rough.capsizes, stock, explorer, quick_calm.seconds, quick_rough.seconds,
        quick_rough.peak_heave, quick_rough.capsizes);
    return 0;
}
