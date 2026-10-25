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

#endif
