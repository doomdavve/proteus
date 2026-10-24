#ifndef BOAT_STUDY_DRIVE_H
#define BOAT_STUDY_DRIVE_H
#include "boat_fleet.h"
#include "boat_profile.h"
#include "boat_session.h"
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
void timed_boat_step(BoatSession *, BoatInput, BoatFrameMetrics *, BoatWakePlayer *, ObjectStudy *);
void demo_step(BoatSession *, int, BoatFrameMetrics *, BoatWakePlayer *, ObjectStudy *);
#endif
