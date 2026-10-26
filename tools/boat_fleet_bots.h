#ifndef BOAT_FLEET_BOTS_H
#define BOAT_FLEET_BOTS_H
#include "boat_fleet.h"
#include "boat_race.h"
#include "boat_throttle.h"
#include "raymath.h"

// Scripted bots may observe a delayed network mirror. A quarter-second feedback horizon
// avoids chasing that stale command with full-rate oscillation after the faster pedal change.
// Player pedals use boat_throttle_rate() against their locally predicted boat instead.
static inline float boat_fleet_bot_throttle_rate(float current, float target)
{
    return Clamp((target - current) / (BOAT_THROTTLE_COMMAND_RATE * .25f), -1, 1);
}

// Scripted fleet drivers for validators and boat_server. Every slot laps the crossing rival's
// four basin waypoints, odd slots in the opposite direction, so trails cross and boats meet
// head-on. Two seconds after a capsize a bot asks the fleet for a reset.
typedef struct {
    unsigned waypoint[BOAT_FLEET_SLOTS];
    unsigned capsized[BOAT_FLEET_SLOTS]; // Ticks spent capsized.
    unsigned resets; // Reset requests made.
} BoatFleetBots;

static inline BoatInput boat_fleet_bot_input(
    BoatFleetBots *bots, const BoatFleet *fleet, unsigned slot)
{
    static const Vector2 route[] = { { 60, -12 }, { 0, -55 }, { -60, -12 }, { 0, 45 } };
    const Boat *boat = &fleet->state.slots[slot].boat;
    unsigned lap = bots->waypoint[slot] % 4;
    Vector2 goal = route[slot % 2 ? 3 - lap : lap];
    if (fleet->world.definition->mapped)
        goal = Vector2Add(fleet->world.definition->fleet_spawn, Vector2Scale(goal, 1.5f));
    Vector2 delta = Vector2Subtract(goal, boat->position);
    if (Vector2Length(delta) < 16)
        ++bots->waypoint[slot];
    float target = atan2f(delta.x, -delta.y);
    float error = remainderf(target - boat->heading, 2 * PI);
    float wheel = Clamp(2 * error - boat->yaw_velocity, -1, 1);
    float throttle = .65f + .1f * (float)slot;
    return (BoatInput) { Clamp((wheel - boat->wheel) / BOAT_STEP, -1, 1),
        boat_fleet_bot_throttle_rate(boat->throttle, throttle) };
}

// Deathmatch hunter (boat_bot --hunt): full throttle at a point just ahead of the nearest
// other boat, so it rams it or lays its wake across the target's path. Falls back to the route
// when it is alone.
static inline BoatInput boat_fleet_bot_hunt_input(
    BoatFleetBots *bots, const BoatFleet *fleet, unsigned slot)
{
    const Boat *boat = &fleet->state.slots[slot].boat;
    const Boat *prey = NULL;
    float nearest = INFINITY;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        const BoatFleetSlot *other = &fleet->state.slots[i];
        float distance = Vector2Distance(other->boat.position, boat->position);
        if (i != slot && other->active && other->boat.motion_state != BOAT_CAPSIZED
            && distance < nearest) {
            nearest = distance;
            prey = &other->boat;
        }
    }
    if (!prey)
        return boat_fleet_bot_input(bots, fleet, slot);
    Vector2 aim = Vector2Add(prey->position, Vector2Scale(prey->velocity, .6f));
    Vector2 delta = Vector2Subtract(aim, boat->position);
    float target = atan2f(delta.x, -delta.y);
    float error = remainderf(target - boat->heading, 2 * PI);
    float wheel = Clamp(2 * error - boat->yaw_velocity, -1, 1);
    return (BoatInput) { Clamp((wheel - boat->wheel) / BOAT_STEP, -1, 1),
        boat_fleet_bot_throttle_rate(boat->throttle, 1) };
}

// Race route bot (validate_boat_race, boat_bot --race): the next gate's centre, offset to the
// slot's own lane, inset from the gate edges, at full throttle; idle once finished. GATES is
// the entrant's cleared count.
static inline BoatInput boat_fleet_bot_race_input(const BoatFleet *fleet,
    const BoatRaceCourse *course, unsigned gates, bool finished, unsigned slot)
{
    const Boat *boat = &fleet->state.slots[slot].boat;
    const BoatRaceGate *gate
        = &course->gates[gates < course->gate_count ? gates : course->gate_count - 1];
    Vector2 across = { -gate->direction.y, gate->direction.x };
    float lane = fminf(14, (gate->half_width - 12) / 1.5f);
    Vector2 target = Vector2Add(gate->centre, Vector2Scale(across, ((float)slot - 1.5f) * lane));
    float heading = atan2f(target.x - boat->position.x, boat->position.y - target.y);
    float error = remainderf(heading - boat->heading, 2 * PI);
    float wheel = Clamp(1.5f * error - .8f * boat->yaw_velocity, -.6f, .6f);
    return (BoatInput) { Clamp((wheel - boat->wheel) / BOAT_STEP, -1, 1),
        boat_fleet_bot_throttle_rate(boat->throttle, finished ? 0 : 1) };
}

// Inputs for every slot, then recovery requests for boats capsized for two seconds.
static inline void boat_fleet_bots_drive(
    BoatFleetBots *bots, BoatFleet *fleet, BoatInput inputs[BOAT_FLEET_SLOTS])
{
    for (unsigned slot = 0; slot < BOAT_FLEET_SLOTS; ++slot) {
        const BoatFleetSlot *s = &fleet->state.slots[slot];
        inputs[slot] = s->active ? boat_fleet_bot_input(bots, fleet, slot) : (BoatInput) { 0 };
        if (!s->active || s->boat.motion_state != BOAT_CAPSIZED) {
            bots->capsized[slot] = 0;
        } else if (++bots->capsized[slot] == 2 * BOAT_TICK_HZ) {
            boat_fleet_request_spawn(fleet, slot);
            bots->waypoint[slot] = 0;
            ++bots->resets;
        }
    }
}

#endif
