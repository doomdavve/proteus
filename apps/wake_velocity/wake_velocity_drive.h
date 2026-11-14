#ifndef WAKE_VELOCITY_DRIVE_H
#define WAKE_VELOCITY_DRIVE_H
#include "core/boat_fleet.h"
#include "session/boat_session.h"
#include "wake_velocity/boat_profile.h"
typedef struct {
    BoatWakeObjects wakes; // The single-boat arcade pool.
    BoatWakeObjects traffic; // Opt-in race trial scenery; never takes a player slot.
    bool traffic_enabled;
    // Crossing: the player (slot 0) and the scripted rival (slot 1) are two physical boats of
    // one multiplayer M2 fleet, which owns their shared pool instead of WAKES.
    BoatFleet fleet;
    BoatPresentation other_presentation; // Drawn as its pose and cloth.
    bool crossing;
    unsigned waypoint;
    float recovery_time;
    uint32_t other_life;
} ObjectStudy;

void object_study_reset(ObjectStudy *, BoatSession *, bool, unsigned, float);
// The live arcade pool (the fleet's in crossing mode) and the rival boat (crossing only).
const BoatWakeObjects *object_study_wakes(const ObjectStudy *);
const Boat *object_study_other(const ObjectStudy *);
void timed_boat_step(BoatSession *, BoatInput, BoatFrameMetrics *, ObjectStudy *);
// The scripted drive's helm at TICK (ticks since the session started) from BOAT. STUDIES: a wake
// field or the arcade pool is present. demo_step steps the session with it; a local session's
// script sends it from the predicted boat (ledger L6).
BoatInput demo_input(const Boat *boat, const BoatWater *water, bool studies, int tick);
void demo_step(BoatSession *, int, BoatFrameMetrics *, ObjectStudy *);
#endif
