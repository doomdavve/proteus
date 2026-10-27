#ifndef PROTEUS_BOAT_BANKING_FIXTURE_H
#define PROTEUS_BOAT_BANKING_FIXTURE_H

#include "boat_session.h"
#include "boat_test_world.h"
#include "raymath.h"

// Shared free-drive inputs for numerical measurements and presentation captures. Each
// fixture session owns a calm basin world and starts its clock at zero.
static inline void banking_fixture_reset(BoatSession *session, int scenario)
{
    boat_session_init(session, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    Boat *boat = &session->boat;
    boat->position.y = scenario == 5 ? -40 : scenario == 3 ? 0 : 40;
    if (scenario == 4 || scenario == 6)
        boat->velocity.y = -12;
}

static inline int banking_fixture_ticks(int scenario)
{
    return scenario == 0 ? 720 : scenario == 5 ? 600 : 960;
}

static inline float banking_fixture_input(Boat *boat, int scenario, int tick)
{
    boat->throttle = scenario == 5 ? -1
        : scenario == 4            ? 0
        : scenario == 6            ? (tick < 240 ? -1 : 0)
                                   : 1;
    return scenario == 1 ? -1
        : scenario == 2  ? 1
        : scenario == 3  ? (tick < 480 ? 1 : -1)
        : scenario == 4  ? .5f
                         : 0;
}
// Hold a fast turn for eight seconds, then release steering while keeping power.
// Translate the complete presentation history once on uniform calm water so the
// three-second exit remains clear of basin walls. No velocity/attitude is prescribed.
#define BANKING_EXIT_HOLD_TICKS 960
#define BANKING_EXIT_TICKS (BANKING_EXIT_HOLD_TICKS + 360)
static inline void banking_exit_reset(BoatSession *session)
{
    boat_session_init(session, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    Boat *boat = &session->boat;
    boat->position = (Vector2) { 0 };
    boat->velocity.y = -12;
    boat->throttle = 1;
}

static inline void banking_exit_step(BoatSession *session, float wheel, int tick)
{
    Boat *boat = &session->boat;
    BoatPresentation *view = &session->presentation;
    if (tick == BANKING_EXIT_HOLD_TICKS) {
        Vector2 translation = Vector2Negate(boat->position);
        boat->position = (Vector2) { 0 };
        for (int i = 0; i < BOAT_WATER_PARTICLES; ++i)
            view->water[i].position = Vector2Add(view->water[i].position, translation);
        Vector3 cloth_shift = { -translation.x, 0, translation.y };
        for (int i = 0; i < BOAT_FLAG_POINTS; ++i) {
            view->flag.surface.points[i] = Vector3Add(view->flag.surface.points[i], cloth_shift);
            view->flag.previous[i] = Vector3Add(view->flag.previous[i], cloth_shift);
        }
    }
    float input
        = tick < BANKING_EXIT_HOLD_TICKS ? Clamp((wheel - boat->wheel) / BOAT_STEP, -1, 1) : 0;
    boat_session_step(session, (BoatInput) { .steering_rate = input });
}
// Common powered entry: steer progressively for one second, then let the wheel return.
#define BOAT_TURN_ENTRY_TICKS 480
static inline void boat_turn_entry_reset(BoatSession *session)
{
    boat_session_init(session, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    Boat *boat = &session->boat;
    boat->position = (Vector2) { 0 };
    boat->velocity.y = -12;
    boat->throttle = boat->engine_response = boat->planing = 1;
    boat->height = .05f;
    boat->pitch = 3 * DEG2RAD;
}

static inline void boat_turn_entry_step(BoatSession *session, int tick, int direction)
{
    boat_session_step(session, (BoatInput) { .steering_rate = tick < 120 ? direction : 0 });
}
#endif
