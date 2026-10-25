/* River sprint rules (docs/RACE_PLAN.md R1). Links only proteus_boat_core. Course geometry is
 * checked against the mapped river. Synthetic cases move hand-set hulls across the gates for
 * swept, reverse, lateral and multi-gate crossings, same-tick places and ties, recovery,
 * leavers, joiners, deadlines and grid rotation. Physical cases step a real fleet: the grid
 * hold through a full-throttle countdown, and a four-bot race through the ferry encounter
 * with a mid-race checkpoint restore. */
#include "boat_fleet_bots.h"
#include "boat_map.h"
#include "boat_race.h"
#include "boat_throttle.h"
#include "raymath.h"
#include "waterfront_city.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static BoatFleet fleet;
static BoatRace race;
static BoatRaceOutput out;

static const BoatRaceGate *gate(unsigned i) { return &race.course.gates[i]; }

static Vector2 along(const BoatRaceGate *g, float forward, float side)
{
    Vector2 across = { -g->direction.y, g->direction.x };
    return Vector2Add(
        g->centre, Vector2Add(Vector2Scale(g->direction, forward), Vector2Scale(across, side)));
}

static bool fresh(BoatRaceRules rules)
{
    return boat_fleet_init(&fleet, boat_location(BOAT_LOCATION_RIVER), (BoatWater) { 0 },
               BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT)
        && boat_race_init(&race, &rules, boat_race_river_course());
}

// Hand-set observation at TICK without stepping physics.
static void at(uint64_t tick)
{
    fleet.state.tick = tick;
    boat_race_observe(&race, &fleet, &out);
}

static void move(unsigned slot, Vector2 position)
{
    fleet.state.slots[slot].boat.position = position;
}

static unsigned count(BoatRaceKind kind, unsigned slot)
{
    unsigned n = 0;
    for (unsigned i = 0; i < out.count; ++i)
        n += out.events[i].kind == kind && (slot == BOAT_RACE_NOBODY || out.events[i].slot == slot);
    return n;
}

static bool phase_event(BoatRacePhase phase)
{
    for (unsigned i = 0; i < out.count; ++i)
        if (out.events[i].kind == BOAT_RACE_PHASE && out.events[i].value == phase)
            return true;
    return false;
}

static int geometry(void)
{
    CHECK(fresh(boat_race_default_rules()));
    const BoatRaceCourse *c = &race.course;
    // Skill openings and the full-width finish stay on water, with safe approach lines.
    for (unsigned i = 0; i < c->gate_count; ++i) {
        const BoatRaceGate *g = gate(i);
        for (int side = -1; side <= 1; side += 2) {
            CHECK(boat_map_water(along(g, 0, side * g->half_width), 0));
            if (i + 1 == c->gate_count)
                CHECK(!boat_map_water(along(g, 0, side * (g->half_width + 12)), 0));
            else
                CHECK(boat_map_water(along(g, 0, side * g->half_width), 8));
        }
        // Straight lines between anchors keep 12 m of mapped clearance.
        const BoatRaceGate *from = i ? gate(i - 1) : &c->start;
        for (int k = 0; k <= 100; ++k) {
            CHECK(boat_map_water(Vector2Lerp(from->centre, g->centre, k / 100.0f), 12));
            // Include the outer racing lanes, not just the centre line.
            for (int side = -1; side <= 1; side += 2)
                CHECK(boat_map_water(Vector2Lerp(
                    along(from, 0, (float)side * fminf(21, from->half_width - 4)),
                    along(g, 0, (float)side * fminf(21, g->half_width - 4)), k / 100.0f), 12));
        }
    }
    const BoatRaceGate *finish = gate(c->gate_count - 1);
    unsigned missed = 0;
    for (unsigned i = 0; i + 1 < c->gate_count; ++i)
        missed += boat_race_cross(gate(i), c->start.centre, finish->centre) < 0;
    CHECK(missed > 0);
    // No straight line through any two outer openings can also clear the middle one.
    // This guards the slalom's actual steering demand, including corner-cutting at edges.
    for (unsigned i = 1; i + 2 < c->gate_count; ++i) {
        const BoatRaceGate *g = gate(i), *from = gate(i - 1), *to = gate(i + 1);
        Vector2 side = { -g->direction.y, g->direction.x };
        float low = INFINITY, high = -INFINITY;
        for (int a = -1; a <= 1; a += 2)
            for (int b = -1; b <= 1; b += 2) {
                Vector2 start = along(from, 0, (float)a * from->half_width);
                Vector2 end = along(to, 0, (float)b * to->half_width);
                float da = Vector2DotProduct(Vector2Subtract(start, g->centre), g->direction);
                float db = Vector2DotProduct(Vector2Subtract(end, g->centre), g->direction);
                CHECK(da < 0 && db > 0);
                Vector2 at = Vector2Lerp(start, end, da / (da - db));
                float offset = Vector2DotProduct(Vector2Subtract(at, g->centre), side);
                low = fminf(low, offset);
                high = fmaxf(high, offset);
            }
        CHECK(low > g->half_width + 10 || high < -g->half_width - 10);
    }
    // Grid places face the start, behind it, clear of land and of each other.
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        Vector2 offset = Vector2Subtract(c->grid[i], c->start.centre);
        CHECK(Vector2DotProduct(offset, c->start.direction) < -10);
        for (unsigned j = 0; j < i; ++j)
            CHECK(Vector2Distance(c->grid[i], c->grid[j]) >= 13.9f);
        Boat boat;
        boat_reset(&boat);
        boat.position = c->grid[i];
        boat.heading = atan2f(c->start.direction.x, -c->start.direction.y);
        CHECK(boat_fleet_spawn_clear(&fleet, i, &boat));
    }
    float length = 0;
    for (unsigned i = 0; i < c->gate_count; ++i)
        length += Vector2Distance(i ? gate(i - 1)->centre : c->start.centre, gate(i)->centre);
    printf("course gates=%u centre_line_m=%.1f\n", c->gate_count, length);
    return 0;
}

static int storm_surface(void)
{
    BoatStorm storm = boat_race_river_course()->storm;
    const float epsilon = .01f;
    float peak = 0;
    for (int z = -160; z <= 160; z += 17)
        for (int x = -160; x <= 160; x += 19) {
            double t = 12.345;
            Vector2 centre = boat_water_storm_centre(storm, t);
            Vector2 p = Vector2Add(centre, (Vector2) { (float)x, (float)z });
            BoatStormSample q = boat_water_storm_query(storm, p, t);
            peak = fmaxf(peak, fabsf(q.height));
            if (Vector2Distance(p, centre) >= storm.radius) {
                CHECK(q.height == 0 && q.vertical_velocity == 0);
                CHECK(q.gradient.x == 0 && q.gradient.y == 0);
            }
            float dx = (boat_water_storm_query(storm, Vector2Add(p, (Vector2) { epsilon, 0 }), t).height
                - boat_water_storm_query(storm, Vector2Add(p, (Vector2) { -epsilon, 0 }), t).height)
                / (2 * epsilon);
            float dz = (boat_water_storm_query(storm, Vector2Add(p, (Vector2) { 0, epsilon }), t).height
                - boat_water_storm_query(storm, Vector2Add(p, (Vector2) { 0, -epsilon }), t).height)
                / (2 * epsilon);
            const double time_epsilon = .004;
            float dt = (boat_water_storm_query(storm, p, t + time_epsilon).height
                - boat_water_storm_query(storm, p, t - time_epsilon).height) / (2 * time_epsilon);
            CHECK(fabsf(q.gradient.x - dx) < .002f && fabsf(q.gradient.y - dz) < .002f);
            CHECK(fabsf(q.vertical_velocity - dt) < .003f);
            storm.started = 86400;
            BoatStormSample later = boat_water_storm_query(storm, p, t + 86400);
            CHECK(later.height == q.height && later.gradient.x == q.gradient.x
                && later.gradient.y == q.gradient.y && later.vertical_velocity == q.vertical_velocity);
            storm.started = 0;
        }
    CHECK(peak > 1);
    BoatWater water = { .storm = storm };
    CHECK(boat_water_displaced(water));
    Vector2 rendezvous = boat_water_storm_centre(storm, 45);
    CHECK(Vector2Distance(rendezvous, (Vector2) { 345, -600 }) < .01f);
    CHECK(fabsf(Vector2Length(storm.velocity) - 8) < .01f);
    Vector2 outside = Vector2Add(boat_water_storm_centre(storm, 12.345),
        (Vector2) { storm.radius + 1, 0 });
    BoatWaterSample calm = boat_water_query(water, outside, 12.345);
    CHECK(calm.height == 0 && calm.normal.y == 1 && calm.velocity.y == 0);
    CHECK(boat_water_storm_query((BoatStorm) { 0 }, storm.centre, 1).height == 0);
    return 0;
}

static int crossing(void)
{
    CHECK(fresh(boat_race_default_rules()));
    const BoatRaceGate *g = gate(0);
    float f = boat_race_cross(g, along(g, -3, 0), along(g, 1, 0));
    CHECK(fabsf(f - .75f) < 1e-4f);
    CHECK(boat_race_cross(g, along(g, 1, 0), along(g, -3, 0)) < 0); // Reverse.
    CHECK(boat_race_cross(g, along(g, -1, g->half_width + 1), along(g, 1, g->half_width + 1)) < 0);
    CHECK(boat_race_cross(g, along(g, -1, g->half_width - 1), along(g, 1, g->half_width - 1)) >= 0);
    CHECK(boat_race_cross(g, along(g, -1, 0), along(g, 0, 0)) == 1); // Landing on the line.
    CHECK(boat_race_cross(g, along(g, 0, 0), along(g, 1, 0)) < 0); // Leaving it again.
    CHECK(boat_race_cross(g, along(g, -2, 0), along(g, -1, 0)) < 0);
    return 0;
}

// Two (or more) joined players through the grid to GO, hand-set. Returns the GO tick.
static int grid_to_go(unsigned players, uint64_t *go)
{
    CHECK(fresh(boat_race_default_rules()));
    CHECK(boat_race_join(&race, &fleet, 0));
    CHECK(!boat_race_join(&race, &fleet, 0)); // Occupied.
    at(1);
    CHECK(race.phase == BOAT_RACE_WAITING && fleet.state.slots[0].pending);
    for (unsigned i = 1; i < players; ++i)
        CHECK(boat_race_join(&race, &fleet, i));
    at(2);
    CHECK(race.phase == BOAT_RACE_GRID && race.round == 1 && phase_event(BOAT_RACE_GRID));
    for (unsigned i = 0; i < players; ++i) {
        CHECK(boat_race_entrant(&race, i));
        CHECK(fleet.state.slots[i].active && fleet.state.slots[i].held);
        CHECK(Vector2Equals(fleet.state.slots[i].boat.position, race.course.grid[i]));
    }
    CHECK(fleet.world.water.vessel.height > 0);
    CHECK(fleet.world.water.storm.height > 0);
    CHECK(fleet.world.water.storm.started == boat_tick_seconds(race.go_tick));
    CHECK(fabs(fleet.world.water.vessel.started - (boat_tick_seconds(race.go_tick) - 12)) < 1e-9);
    at(race.phase_end);
    CHECK(race.phase == BOAT_RACE_COUNTDOWN);
    boat_race_reset(&race, &fleet, 0); // Ignored while held.
    CHECK(!race.slots[0].reset_wanted);
    at(race.go_tick - 1);
    CHECK(race.phase == BOAT_RACE_COUNTDOWN && fleet.state.slots[0].held);
    at(race.go_tick);
    CHECK(race.phase == BOAT_RACE_RACING && phase_event(BOAT_RACE_RACING));
    for (unsigned i = 0; i < players; ++i)
        CHECK(!fleet.state.slots[i].held);
    *go = race.go_tick;
    return 0;
}

static int progress_and_places(void)
{
    uint64_t t;
    CHECK(grid_to_go(3, &t) == 0);
    // A late joiner waits unplaced for the next grid.
    CHECK(boat_race_join(&race, &fleet, 3));
    at(++t);
    CHECK(!boat_race_entrant(&race, 3) && !fleet.state.slots[3].active
        && !fleet.state.slots[3].pending);
    // One high-speed segment clears two gates in order.
    move(0, along(gate(0), -2, 0));
    at(++t);
    move(0, along(gate(1), 2, 0));
    at(++t);
    CHECK(race.entrants[0].gates == 2 && count(BOAT_RACE_GATE, 0) == 2);
    CHECK(out.events[0].value == 0 && out.events[1].value == 1);
    // Outside the lateral bound, or reversing, gains nothing.
    move(1, along(gate(0), -2, gate(0)->half_width + 5));
    at(++t);
    move(1, along(gate(0), 2, gate(0)->half_width + 5));
    at(++t);
    CHECK(race.entrants[1].gates == 0);
    move(1, along(gate(0), 2, 0));
    at(++t);
    move(1, along(gate(0), -2, 0));
    at(++t);
    CHECK(race.entrants[1].gates == 0);
    move(1, along(gate(0), 3, 0));
    at(++t);
    CHECK(race.entrants[1].gates == 1);
    // Live positions: gates first, then progress along the shared segment.
    move(2, along(gate(0), -50, 0));
    at(++t);
    CHECK(boat_race_position(&race, &fleet, 0) == 1 && boat_race_position(&race, &fleet, 1) == 2
        && boat_race_position(&race, &fleet, 2) == 3 && boat_race_position(&race, &fleet, 3) == 0);
    // A teleport across a gate is never a crossing: a new life starts a fresh segment.
    Boat jump = fleet.state.slots[2].boat;
    jump.position = along(gate(0), 5, 0);
    boat_fleet_place(&fleet, 2, jump);
    at(++t);
    CHECK(race.entrants[2].gates == 0);
    // Same observation: slot 1 crosses the finish earlier in the tick than slot 0.
    // Clear the intervening skill gates before comparing finish fractions.
    for (unsigned g = 2; g + 1 < race.course.gate_count; ++g) {
        move(0, along(gate(g), -2, 0));
        at(++t);
        move(0, along(gate(g), 2, 0));
        at(++t);
    }
    for (unsigned g = 1; g + 1 < race.course.gate_count; ++g) {
        move(1, along(gate(g), -2, 0));
        at(++t);
        move(1, along(gate(g), 2, 0));
        at(++t);
    }
    const BoatRaceGate *finish = gate(race.course.gate_count - 1);
    move(0, along(finish, -4, 0));
    move(1, along(finish, -4, 30));
    at(++t);
    move(1, along(finish, -1, 30));
    at(++t);
    CHECK(race.entrants[1].gates == race.course.gate_count - 1);
    move(0, along(finish, 4, 0)); // Fraction 0.5.
    move(1, along(finish, 3, 30)); // Fraction 0.25.
    at(++t);
    CHECK(race.entrants[0].status == BOAT_RACE_FINISHED && race.entrants[1].place == 1
        && race.entrants[0].place == 2 && count(BOAT_RACE_FINISH, BOAT_RACE_NOBODY) == 2);
    CHECK(out.events[0].slot == 1 && out.events[0].value == 1 && out.events[1].slot == 0);
    uint64_t expected
        = (uint64_t)llround(((double)(t - 1 - race.go_tick) + .25) * BOAT_RACE_SUBTICKS);
    CHECK(race.entrants[1].finish == expected && race.entrants[0].finish > expected);
    CHECK(race.deadline == t + race.rules.after_winner_ticks);
    // Crossing the finish after skipping skill gates cannot award a finish or progress.
    move(2, along(finish, -2, 0));
    at(++t);
    move(2, along(finish, 2, 0));
    at(++t);
    CHECK(race.entrants[2].gates == 0 && race.entrants[2].status == BOAT_RACE_RUNNING
        && count(BOAT_RACE_FINISH, 2) == 0);
    // Places are fixed: further motion past the line changes nothing.
    move(0, along(finish, -5, 0));
    at(++t);
    move(0, along(finish, 5, 0));
    at(++t);
    CHECK(race.entrants[0].place == 2 && count(BOAT_RACE_FINISH, BOAT_RACE_NOBODY) == 0);
    CHECK(boat_race_position(&race, &fleet, 2) == 3);
    // The straggler runs out of time 15 s after the winner.
    at(race.deadline - 1);
    CHECK(race.phase == BOAT_RACE_RACING);
    at(race.deadline);
    CHECK(race.phase == BOAT_RACE_RESULTS && race.entrants[2].status == BOAT_RACE_DNF
        && count(BOAT_RACE_OUT, 2) == 1);
    // Results, then all four present players are gridded with places rotated by one.
    at(race.phase_end);
    CHECK(race.phase == BOAT_RACE_GRID && race.round == 2);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        CHECK(boat_race_entrant(&race, i) && race.entrants[i].status == BOAT_RACE_RUNNING);
        CHECK(Vector2Equals(
            fleet.state.slots[i].boat.position, race.course.grid[(i + 1) % BOAT_FLEET_SLOTS]));
    }
    return 0;
}

// Walk SLOT forward across gate G at lateral SIDE: one observation each side of the line.
static void through(unsigned slot, unsigned g, float side, uint64_t *t)
{
    move(slot, along(gate(g), -2, side));
    at(++*t);
    move(slot, along(gate(g), 2, side));
    at(++*t);
}

static int ties(void)
{
    uint64_t t;
    CHECK(grid_to_go(3, &t) == 0);
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned g = 0; g + 1 < race.course.gate_count; ++g)
            through(i, g, (float)i * 5, &t);
    through(2, race.course.gate_count - 1, 10, &t);
    CHECK(race.entrants[2].place == 1);
    // Slots 0 and 1 cross the finish at exactly the same time.
    for (unsigned i = 0; i < 2; ++i)
        move(i, along(gate(race.course.gate_count - 1), -2, (float)i * 5));
    at(++t);
    for (unsigned i = 0; i < 2; ++i)
        move(i, along(gate(race.course.gate_count - 1), 2, (float)i * 5));
    at(++t);
    CHECK(race.entrants[0].place == 2 && race.entrants[1].place == 2);
    CHECK(race.entrants[0].finish == race.entrants[1].finish);
    CHECK(count(BOAT_RACE_FINISH, BOAT_RACE_NOBODY) == 2);
    // Everyone finished: results without waiting for the deadline.
    CHECK(race.phase == BOAT_RACE_RESULTS && count(BOAT_RACE_OUT, BOAT_RACE_NOBODY) == 0);
    return 0;
}

// Finishers coast for park_ticks, then wait held past the finish, facing back up-course, until
// the next grid; R and capsizes park at once and are ignored once parked.
static int parking(void)
{
    uint64_t t;
    CHECK(grid_to_go(3, &t) == 0);
    for (unsigned g = 0; g < race.course.gate_count; ++g)
        through(0, g, 0, &t);
    uint64_t finished = t;
    for (unsigned g = 0; g < race.course.gate_count; ++g)
        through(1, g, 10, &t);
    CHECK(race.entrants[0].status == BOAT_RACE_FINISHED && race.placed == 2);
    uint32_t life = fleet.state.slots[0].life;
    at(finished + race.rules.park_ticks - 1);
    CHECK(fleet.state.slots[0].life == life && !race.slots[0].parked);
    boat_race_reset(&race, &fleet, 1); // R parks a finisher at once.
    at(finished + race.rules.park_ticks);
    CHECK(race.slots[0].parked && race.slots[1].parked);
    CHECK(count(BOAT_RACE_PARK, 0) == 1 && count(BOAT_RACE_PARK, 1) == 1);
    const BoatRaceGate *finish = gate(race.course.gate_count - 1);
    for (unsigned i = 0; i < 2; ++i) {
        const BoatFleetSlot *f = &fleet.state.slots[i];
        Vector2 offset = Vector2Subtract(f->boat.position, finish->centre);
        CHECK(f->held && f->life == life + 1); // Every slot started on life 1 at the grid.
        CHECK(Vector2DotProduct(offset, finish->direction) >= 60);
        CHECK(
            Vector2Distance(fleet.state.slots[0].boat.position, fleet.state.slots[1].boat.position)
            > 20);
        // Facing back towards the line; no recovery is counted.
        Vector2 forward = { sinf(f->boat.heading), -cosf(f->boat.heading) };
        CHECK(Vector2DotProduct(forward, finish->direction) < -.99f);
        CHECK(race.entrants[i].recoveries == 0 && race.entrants[i].place == i + 1);
    }
    boat_race_reset(&race, &fleet, 0);
    at(finished + race.rules.park_ticks + 1);
    CHECK(fleet.state.slots[0].life == life + 1 && !race.slots[0].reset_wanted);
    // The last runner finishes; the parked boats stay held through the results.
    for (unsigned g = 0; g < race.course.gate_count; ++g)
        through(2, g, 10, &t);
    CHECK(race.phase == BOAT_RACE_RESULTS && fleet.state.slots[0].held);
    at(t + race.rules.park_ticks);
    CHECK(race.slots[2].parked && fleet.state.slots[2].held);
    at(race.phase_end);
    CHECK(race.phase == BOAT_RACE_GRID && !race.slots[0].parked);
    return 0;
}

static int recovery(void)
{
    uint64_t t;
    CHECK(grid_to_go(2, &t) == 0);
    move(0, along(gate(0), -2, 0));
    at(++t);
    move(0, along(gate(0), 40, 10));
    at(++t);
    CHECK(race.entrants[0].gates == 1);
    // A capsize recovers automatically behind the last cleared gate, keeping progress.
    uint32_t life = fleet.state.slots[0].life;
    fleet.state.slots[0].boat.motion_state = BOAT_CAPSIZED;
    uint64_t capsized = ++t;
    at(t);
    at(capsized + race.rules.recovery_ticks - 1);
    CHECK(fleet.state.slots[0].life == life);
    at(capsized + race.rules.recovery_ticks);
    CHECK(fleet.state.slots[0].life == life + 1 && count(BOAT_RACE_RECOVER, 0) == 1);
    Vector2 p = fleet.state.slots[0].boat.position;
    CHECK(Vector2DotProduct(Vector2Subtract(p, gate(0)->centre), gate(0)->direction) < -20);
    CHECK(race.entrants[0].gates == 1 && race.entrants[0].recoveries == 1);
    CHECK(fleet.state.slots[0].boat.motion_state != BOAT_CAPSIZED);
    // Driving forward through the cleared gate again gains nothing; the next gate still does.
    t = capsized + race.rules.recovery_ticks;
    move(0, along(gate(0), 2, 0));
    at(++t);
    CHECK(race.entrants[0].gates == 1 && count(BOAT_RACE_GATE, 0) == 0);
    // R on an upright boat recovers on the next observation.
    boat_race_reset(&race, &fleet, 0);
    at(++t);
    CHECK(count(BOAT_RACE_RECOVER, 0) == 1 && race.entrants[0].recoveries == 2);
    // R straight after a capsize waits for skip_ticks.
    fleet.state.slots[0].boat.motion_state = BOAT_CAPSIZED;
    capsized = ++t;
    at(t);
    boat_race_reset(&race, &fleet, 0);
    at(capsized + race.rules.skip_ticks - 1);
    CHECK(race.entrants[0].recoveries == 2);
    at(capsized + race.rules.skip_ticks);
    CHECK(race.entrants[0].recoveries == 3);
    // An occupied recovery point moves the boat to the next clear candidate.
    Vector2 first = fleet.state.slots[0].boat.position;
    move(1, first);
    boat_race_reset(&race, &fleet, 0);
    at(++t);
    CHECK(race.entrants[0].recoveries == 4
        && Vector2Distance(fleet.state.slots[0].boat.position, first) > 10);
    // Before any gate, recovery is behind the start line.
    move(1, along(&race.course.start, 30, 0));
    boat_race_reset(&race, &fleet, 1);
    at(++t);
    p = fleet.state.slots[1].boat.position;
    CHECK(
        Vector2DotProduct(Vector2Subtract(p, race.course.start.centre), race.course.start.direction)
        < -20);
    return 0;
}

static int leavers(void)
{
    uint64_t t;
    CHECK(grid_to_go(3, &t) == 0);
    uint32_t identity = race.entrants[1].identity;
    boat_race_leave(&race, &fleet, 1);
    CHECK(!fleet.state.slots[1].active);
    at(++t);
    CHECK(race.entrants[1].status == BOAT_RACE_DNF && race.entrants[1].identity == identity
        && count(BOAT_RACE_OUT, 1) == 1);
    // A replacement in the same slot inherits nothing and waits.
    CHECK(boat_race_join(&race, &fleet, 1));
    at(++t);
    CHECK(!boat_race_entrant(&race, 1) && race.slots[1].identity != identity
        && !fleet.state.slots[1].active && race.entrants[1].status == BOAT_RACE_DNF);
    CHECK(boat_race_position(&race, &fleet, 1) == 3); // The old row ranks after running boats.
    // A finish survives its entrant's disconnect.
    for (unsigned g = 0; g < race.course.gate_count; ++g)
        through(0, g, 0, &t);
    CHECK(race.entrants[0].place == 1);
    boat_race_leave(&race, &fleet, 0);
    at(++t);
    CHECK(race.entrants[0].status == BOAT_RACE_FINISHED && race.entrants[0].place == 1);
    // The last entrant leaving abandons the round; the waiting joiner boats freely again.
    boat_race_leave(&race, &fleet, 2);
    at(++t);
    CHECK(race.phase == BOAT_RACE_WAITING && count(BOAT_RACE_ABANDON, BOAT_RACE_NOBODY) == 1);
    CHECK(fleet.state.slots[1].pending);
    // Leaving during the countdown is a DNF too, and the cap ends a stalled race.
    CHECK(grid_to_go(2, &t) == 0);
    at(race.go_tick + race.rules.cap_ticks - 1);
    CHECK(race.phase == BOAT_RACE_RACING);
    at(race.go_tick + race.rules.cap_ticks);
    CHECK(race.phase == BOAT_RACE_RESULTS && count(BOAT_RACE_OUT, BOAT_RACE_NOBODY) == 2);
    boat_race_leave(&race, &fleet, 1);
    at(race.phase_end);
    CHECK(race.phase == BOAT_RACE_WAITING); // One player is too few for a grid.
    CHECK(fresh(boat_race_default_rules()));
    CHECK(boat_race_join(&race, &fleet, 0) && boat_race_join(&race, &fleet, 1));
    at(1);
    CHECK(race.phase == BOAT_RACE_GRID);
    boat_race_leave(&race, &fleet, 1);
    at(race.phase_end);
    at(race.go_tick);
    at(race.go_tick + 1);
    CHECK(race.entrants[1].status == BOAT_RACE_DNF && race.phase == BOAT_RACE_RACING);
    return 0;
}

static int rejects(void)
{
    BoatRaceRules rules = boat_race_default_rules();
    BoatRaceCourse course = *boat_race_river_course();
    CHECK(boat_race_init(&race, &rules, &course));
    course.storm.radius = NAN;
    CHECK(!boat_race_init(&race, &rules, &course));
    course = *boat_race_river_course();
    course.storm.height = -1;
    CHECK(!boat_race_init(&race, &rules, &course));
    course = *boat_race_river_course();
    rules.countdown_ticks = 0;
    CHECK(!boat_race_init(&race, &rules, &course));
    rules = boat_race_default_rules();
    rules.skip_ticks = rules.recovery_ticks + 1;
    CHECK(!boat_race_init(&race, &rules, &course));
    rules = boat_race_default_rules();
    course.gate_count = 0;
    CHECK(!boat_race_init(&race, &rules, &course));
    course = *boat_race_river_course();
    course.gates[1].direction = (Vector2) { 1, 1 };
    CHECK(!boat_race_init(&race, &rules, &course));
    course = *boat_race_river_course();
    course.source.speed = 0;
    CHECK(!boat_race_init(&race, &rules, &course));
    return 0;
}

// Route bot (shared with boat_bot --race).
static BoatInput bot(const BoatFleet *f, const BoatRace *r, unsigned slot)
{
    return boat_fleet_bot_race_input(f, &r->course, r->entrants[slot].gates,
        r->entrants[slot].status == BOAT_RACE_FINISHED, slot);
}

typedef struct {
    unsigned jumps[BOAT_FLEET_SLOTS], capsizes[BOAT_FLEET_SLOTS];
    bool airborne[BOAT_FLEET_SLOTS];
} Tally;

static void step(BoatFleet *f, BoatRace *r, Tally *tally)
{
    BoatInput inputs[BOAT_FLEET_SLOTS] = { 0 };
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        if (f->state.slots[i].active)
            inputs[i] = bot(f, r, i);
    boat_fleet_step(f, inputs);
    boat_race_observe(r, f, &out);
    for (unsigned i = 0; tally && i < BOAT_FLEET_SLOTS; ++i) {
        const Boat *b = &f->state.slots[i].boat;
        bool air = b->motion_state == BOAT_AIRBORNE;
        tally->jumps[i] += air && !tally->airborne[i];
        tally->airborne[i] = air;
    }
    for (unsigned i = 0; tally && i < out.count; ++i)
        if (out.events[i].kind == BOAT_RACE_RECOVER)
            ++tally->capsizes[out.events[i].slot];
}

static int hold(void)
{
    BoatRaceRules rules = boat_race_default_rules();
    BoatWater water = { 0 };
    CHECK(boat_fleet_init(&fleet, boat_location(BOAT_LOCATION_RIVER), water, BOAT_OBJECT_LIFE_TICKS,
        BOAT_OBJECT_DEFAULT_HEIGHT));
    CHECK(boat_race_init(&race, &rules, boat_race_river_course()));
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        CHECK(boat_race_join(&race, &fleet, i));
    while (race.phase != BOAT_RACE_GRID)
        step(&fleet, &race, NULL);
    uint64_t emitted = fleet.state.wakes.emitted;
    // Full throttle on the grid and through the countdown: no planar motion at all.
    while (race.phase != BOAT_RACE_RACING) {
        BoatInput inputs[BOAT_FLEET_SLOTS];
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            inputs[i]
                = (BoatInput) { 1, boat_throttle_rate(fleet.state.slots[i].boat.throttle, 1) };
        CHECK(boat_fleet_step(&fleet, inputs));
        boat_race_observe(&race, &fleet, &out);
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
            const Boat *b = &fleet.state.slots[i].boat;
            unsigned place = (i + race.round - 1) % BOAT_FLEET_SLOTS;
            CHECK(Vector2Equals(b->position, race.course.grid[place]));
            CHECK(b->velocity.x == 0 && b->velocity.y == 0);
        }
    }
    CHECK(fleet.state.tick == race.go_tick && fleet.state.wakes.emitted == emitted);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        CHECK(fleet.state.slots[i].boat.throttle == 1
            && fleet.state.slots[i].boat.engine_response > .5f);
    // Released on one tick: every boat moves on the first free step.
    Vector2 before[BOAT_FLEET_SLOTS];
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        before[i] = fleet.state.slots[i].boat.position;
    step(&fleet, &race, NULL);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        CHECK(Vector2Distance(before[i], fleet.state.slots[i].boat.position) > 0);
    return 0;
}

static int full_race(void)
{
    BoatRaceRules rules = boat_race_default_rules();
    CHECK(boat_fleet_init(&fleet, boat_location(BOAT_LOCATION_RIVER), (BoatWater) { 0 },
        BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT));
    CHECK(boat_race_init(&race, &rules, boat_race_river_course()));
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        CHECK(boat_race_join(&race, &fleet, i));
    Tally tally = { 0 };
    static BoatFleet copy;
    static BoatRace race_copy;
    bool saved = false;
    unsigned finish_events = 0;
    while (race.round < 2 || race.phase != BOAT_RACE_RACING) {
        step(&fleet, &race, &tally);
        CHECK(fleet.stats.emission_failures == 0);
        finish_events += count(BOAT_RACE_FINISH, BOAT_RACE_NOBODY);
        if (saved && race.round == 1) {
            BoatRaceOutput kept = out;
            step(&copy, &race_copy, NULL);
            CHECK(boat_fleet_hash(&copy.state) == boat_fleet_hash(&fleet.state));
            CHECK(race_copy.placed == race.placed && race_copy.phase == race.phase);
            out = kept;
        }
        if (race.round == 1 && race.phase == BOAT_RACE_RACING && !saved
            && fleet.state.tick == race.go_tick + 20 * BOAT_TICK_HZ) {
            // Checkpoint inside the ferry encounter: a fresh fleet resumes identically.
            CHECK(boat_fleet_init(&copy, boat_location(BOAT_LOCATION_RIVER), (BoatWater) { 0 },
                BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT));
            BoatFleetState state;
            boat_fleet_save(&fleet, &state);
            boat_fleet_restore(&copy, &state);
            race_copy = race;
            boat_race_bind(&race_copy, &copy);
            saved = true;
        }
        if (race.phase == BOAT_RACE_RESULTS && out.count && phase_event(BOAT_RACE_RESULTS)) {
            printf("round 1 results:");
            for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
                const BoatRaceEntrant *e = &race.entrants[i];
                printf(" %u=%s%u/%.2fs/jumps%u/recoveries%u", i + 1,
                    e->status == BOAT_RACE_FINISHED ? "P" : "DNF", e->place,
                    (double)e->finish / BOAT_RACE_SUBTICKS / BOAT_TICK_HZ, tally.jumps[i],
                    e->recoveries);
            }
            printf("\n");
            for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
                const BoatRaceEntrant *e = &race.entrants[i];
                double seconds = (double)e->finish / BOAT_RACE_SUBTICKS / BOAT_TICK_HZ;
                CHECK(e->status == BOAT_RACE_FINISHED && seconds >= 45 && seconds <= 85);
                CHECK(tally.jumps[i] > 0);
            }
            CHECK(saved && race_copy.placed == BOAT_FLEET_SLOTS);
        }
        CHECK(fleet.state.tick < 200 * BOAT_TICK_HZ);
    }
    CHECK(finish_events == BOAT_FLEET_SLOTS && race.round == 2);
    printf("round 2 racing at tick %" PRIu64 "\n", fleet.state.tick);
    return 0;
}

int main(void)
{
    if (geometry() || storm_surface() || crossing() || progress_and_places() || ties() || parking() || recovery()
        || leavers() || rejects() || hold() || full_race())
        return 1;
    printf("boat_race=ok\n");
    return 0;
}
