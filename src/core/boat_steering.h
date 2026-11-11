#ifndef PROTEUS_BOAT_STEERING_H
#define PROTEUS_BOAT_STEERING_H

#include "boat.h"
#include <math.h>

// Shared with the integrator so target control respects faster countersteering.
#define BOAT_WHEEL_UNWIND_RATE 3.0f

// Relative movement accumulates a held wheel position, independent of window size or
// frame duration. 300 mouse pixels take the requested wheel from centre to full lock.
// Clamp the stored state itself so reversing from full lock responds immediately.
static inline float boat_mouse_wheel(float wheel, float delta_x)
{
    if (!isfinite(wheel))
        wheel = 0;
    if (!isfinite(delta_x))
        delta_x = 0;
    return fmaxf(-1, fminf(1, wheel + delta_x / 300.0f));
}

// Follow an absolute wheel target through ordinary rate input. A nonzero hold needs a
// tiny signed rate: zero input means spring return. The minimum is one wire quantum,
// keeping both solo and multiplayer within 0.0002 of the desired wheel position.
static inline float boat_steering_rate(float current, float target)
{
    if (!isfinite(target) || target == 0)
        return 0;
    target = fmaxf(-1, fminf(1, target));
    float difference = target - current;
    float rate = difference / (BOAT_STEP * BOAT_WHEEL_UNWIND_RATE);
    float direction = difference == 0 ? target : difference;
    return copysignf(fmaxf(1.0f / 127, fminf(1, fabsf(rate))), direction);
}
#endif
