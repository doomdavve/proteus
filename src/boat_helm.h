#ifndef PROTEUS_BOAT_HELM_H
#define PROTEUS_BOAT_HELM_H

#include "boat.h"
#include "boat_steering.h"
#include "boat_throttle.h"

#include <math.h>

// One tick's helm rates from the frame's INPUT and targets, read from BOAT before the tick: a
// finite STEERING_TARGET (mouse or touch wheel) replaces INPUT's steering rate and a finite
// THRUST_TARGET replaces its throttle rate. Solo, the local client and their validator share it.
static inline BoatInput boat_helm_input(
    const Boat *boat, BoatInput input, float thrust_target, float steering_target)
{
    if (isfinite(steering_target))
        input.steering_rate = boat_steering_rate(boat->wheel, steering_target);
    if (isfinite(thrust_target))
        input.throttle_rate = boat_throttle_rate(boat->throttle, thrust_target);
    return input;
}

#endif
