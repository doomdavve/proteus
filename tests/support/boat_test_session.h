#ifndef BOAT_TEST_SESSION_H
#define BOAT_TEST_SESSION_H
#include "boat_session.h"
#include "boat_test_world.h"

// The live view of *SESSION for one draw or scene call (block-scoped compound literal).
#define SESSION_VIEW(session)                                                                      \
    (&(BoatViewState) { .boat = (session)->boat,                                                   \
        .time = boat_session_time(session),                                                        \
        .course = (session)->course })

// Test fixture: BOAT, as authored, at tick zero in its own calm basin world.
static inline BoatSession basin_session(Boat boat)
{
    BoatSession session;
    boat_session_init(&session, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    session.boat = boat;
    return session;
}

// Test fixture for checks that read only physics, wakes or contacts: a calm session at LOCATION
// without the cosmetic presentation (flag cloth, particles), which leaves physics bit-identical
// (validate_boat_world's presentation_toggle) and otherwise dominates long scripted runs.
static inline void physics_session(BoatSession *session, BoatLocationId location)
{
    boat_session_init(session, test_world(location), (BoatWater) { 0 });
    session->presentation_enabled = false;
}

// basin_session without presentation, for physics-only checks.
static inline BoatSession physics_basin_session(Boat boat)
{
    BoatSession session = basin_session(boat);
    session.presentation_enabled = false;
    return session;
}

#endif
