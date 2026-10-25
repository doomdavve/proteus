/* S4 basin slice: the boating core library alone. Links only proteus_boat_core, so it cannot
 * draw, open a window or audio, or reach session/replay persistence; it steps boats through
 * boat_step and the course sweep exactly as BoatSession orders them (validate_boat_world checks
 * that order against the session). */
#include "boat.h"
#include "boat_course.h"
#include "boat_map.h"
#include "raymath.h"
#include "waterfront_city.h"
#include "waterfront_harbor.h"

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

enum { TICKS = 2400, WORLDS = 3 };

// Physical state only: what a headless server would own per boat and world.
typedef struct {
    BoatWorld world;
    Boat boat;
    uint64_t tick;
    BoatCourse course;
} CoreRun;

static void core_step(CoreRun *run, float wheel_change, float throttle_change)
{
    Vector2 previous = run->boat.position;
    ++run->tick;
    boat_step(&run->boat, &run->world, boat_tick_seconds(run->tick),
        (BoatInput) { wheel_change, throttle_change });
    boat_course_step(&run->course, previous, run->boat.position, BOAT_TICK_SECONDS);
}

// Powered loop through the boat's own wake; the same inputs for every world.
static void drive(CoreRun *run, int tick)
{
    float throttle = tick < 1440 ? 1 : 0;
    float wheel = tick >= 420 && tick < 1440 ? 1 : 0;
    core_step(run, Clamp((wheel - run->boat.wheel) / BOAT_STEP, -1, 1),
        Clamp((throttle - run->boat.throttle) / (.5f * BOAT_STEP), -1, 1));
}

static void start(CoreRun *run, int index)
{
    const BoatWorldDefinition *basin = boat_location(BOAT_LOCATION_BASIN);
    const BoatWorldDefinition *definition = index == 2 ? boat_location(BOAT_LOCATION_RIVER) : basin;
    BoatWater water = { .player_wake_enabled = true };
    if (index == 1) {
        water = boat_world_test_water(basin, true, true, false);
        water.player_wake_enabled = true;
    } else if (index == 2) {
        water = boat_water_gentle();
    }
    *run = (CoreRun) { 0 };
    boat_world_init(&run->world, definition, water);
    boat_reset_location(&run->boat, definition);
}

static bool same(const CoreRun *a, const CoreRun *b)
{
    return a->tick == b->tick && a->world.definition == b->world.definition
        && memcmp(&a->boat, &b->boat, sizeof(a->boat)) == 0
        && memcmp(&a->world.water, &b->world.water, sizeof(a->world.water)) == 0
        && memcmp(&a->course, &b->course, sizeof(a->course)) == 0;
}

// Solo references, then the same worlds stepped interleaved in one process.
static int interleaved(void)
{
    static CoreRun solo[WORLDS], live[WORLDS];
    for (int i = 0; i < WORLDS; ++i) {
        start(&solo[i], i);
        for (int tick = 0; tick < TICKS; ++tick)
            drive(&solo[i], tick);
        start(&live[i], i);
    }
    for (int tick = 0; tick < TICKS; ++tick)
        for (int i = WORLDS - 1; i >= 0; --i)
            drive(&live[i], tick);
    for (int i = 0; i < WORLDS; ++i)
        CHECK(same(&solo[i], &live[i]));
    CHECK(live[0].world.water.player_wake_count > 0 && live[1].world.water.player_wake_count > 0);
    CHECK(Vector2Distance(live[0].boat.position, live[1].boat.position) > 1);
    CHECK(live[2].world.definition->mapped && !live[0].world.definition->mapped);
    printf(
        "interleaved=ok worlds=basin-calm-d3,basin-waves-current-d3,river-waves ticks=%d\n", TICKS);
    return 0;
}

// Swept gate crossings at known fractions, in order, ending in a finish.
static int course(void)
{
    BoatCourse course = { 0 };
    boat_course_step(&course, BOAT_COURSE_START, (Vector2) { 0, 30 }, 1);
    CHECK(course.next_gate == 0 && course.crossing[0] == -1);
    course.enabled = true;
    boat_course_step(&course, (Vector2) { 13, 40 }, (Vector2) { 13, 30 }, 1);
    CHECK(course.next_gate == 0 && course.elapsed == 0); // Outside the first gate's half-width.
    boat_course_step(&course, BOAT_COURSE_START, (Vector2) { 0, 30 }, 1);
    CHECK(course.next_gate == 1 && course.crossing[0] == .75 && course.splits[0] == 0);
    CHECK(course.elapsed == .25);
    Vector2 previous = { 0, 30 };
    for (int gate = 1; gate < BOAT_COURSE_GATES; ++gate) {
        Vector2 centre = boat_course_gates[gate].centre;
        Vector2 before = { centre.x, centre.y + 1 }, after = { centre.x, centre.y - 1 };
        boat_course_step(&course, previous, before, 1);
        boat_course_step(&course, before, after, 1);
        CHECK(course.next_gate == gate + 1 && course.crossing[gate] == .5);
        CHECK(course.splits[gate] > course.splits[gate - 1]);
        previous = after;
    }
    CHECK(course.elapsed == course.splits[BOAT_COURSE_GATES - 1]);
    double finished = course.elapsed;
    boat_course_step(&course, previous, (Vector2) { 0, -80 }, 1);
    CHECK(course.elapsed == finished && course.crossing[0] == -1);
    printf("course=ok elapsed=%.2f\n", finished);
    return 0;
}

// The borrowed query is the same function; flat water is level and still.
static int water(void)
{
    BoatWater waves = boat_world_test_water(boat_location(BOAT_LOCATION_BASIN), true, true, true);
    BoatWater flat = { 0 };
    for (int i = 0; i < 64; ++i) {
        Vector2 p = { -60 + 1.9f * i, 40 - 1.3f * i };
        double time = .37 * i;
        BoatWaterSample a = boat_water_query(waves, p, time);
        BoatWaterSample b = boat_water_query_borrowed(&waves, p, time);
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
        BoatWaterSample still = boat_water_query(flat, p, time);
        CHECK(still.height == 0 && Vector3Equals(still.normal, (Vector3) { 0, 1, 0 }));
    }
    CHECK(boat_water_displaced(waves) && !boat_water_displaced(flat));
    puts("water=ok");
    return 0;
}

// The mapped spawn is open water in the compiled crop, far points are not, and harbor
// envelopes block berths.
static int shoreline(void)
{
    const BoatWorldDefinition *definition = boat_location(BOAT_LOCATION_RIVER);
    boat_map_legacy_destroy();
    Vector2 normal = { 0 };
    CHECK(definition->mapped && boat_map_legacy_water(definition->spawn, 2));
    CHECK(!boat_map_legacy_water_contact((Vector2) { 5000, 5000 }, 2, &normal));
    CHECK(fabsf(Vector2Length(normal) - 1) < 1e-4f);
    boat_map_legacy_destroy();
    WaterfrontBerth berth = waterfront_harbor_berths()[0];
    CHECK(waterfront_harbor_contact((Vector2) { -berth.position.x, berth.position.z }, 1, &normal));
    puts("shoreline=ok");
    return 0;
}

int main(void)
{
    CHECK(interleaved() == 0);
    CHECK(course() == 0);
    CHECK(water() == 0);
    CHECK(shoreline() == 0);
    puts("boat_core=ok");
    return 0;
}
