#ifndef PROTEUS_BOAT_RESET_HOLD_H
#define PROTEUS_BOAT_RESET_HOLD_H
#include <math.h>
#include <stdbool.h>

#define BOAT_RESET_HOLD_SECONDS 1.0f
// Shared device-free shortcut state. Cancellation clears progress, but a fired hold remains
// latched until release even if focus/menu state changes in between.
typedef struct {
    float seconds;
    bool fired;
} BoatResetHold;
static inline bool boat_reset_hold_update(BoatResetHold *hold, bool down, bool active, float dt)
{
    if (!down) {
        *hold = (BoatResetHold) { 0 };
        return false;
    }
    if (!active) {
        hold->seconds = 0;
        return false;
    }
    if (hold->fired)
        return false;
    hold->seconds += isfinite(dt) ? fmaxf(0, fminf(dt, .1f)) : 0;
    if (hold->seconds + .000001f < BOAT_RESET_HOLD_SECONDS)
        return false;
    hold->seconds = BOAT_RESET_HOLD_SECONDS;
    hold->fired = true;
    return true;
}
#endif
