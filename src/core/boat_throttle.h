#ifndef PROTEUS_BOAT_THROTTLE_H
#define PROTEUS_BOAT_THROTTLE_H

#include "boat.h"
#include <math.h>
#include <stdbool.h>

// Pedals apply power only while held. Opposing pedals select neutral; precision preserves
// the original 5% docking settings in both directions and the normal 20% reverse limit.
static inline float boat_pedal_target(bool forward, bool reverse, bool precision)
{
    if (forward == reverse)
        return 0;
    return forward ? (precision ? .05f : 1) : (precision ? -.05f : -.20f);
}

// Legacy reference levels, also used to space the thrust instrument and review fixtures.
// Two reverse levels, neutral, and four forward levels. Index 2 is neutral.
#define BOAT_THROTTLE_NEUTRAL 2
#define BOAT_THROTTLE_COUNT 7
static inline float boat_throttle_setting(int step)
{
    static const float settings[BOAT_THROTTLE_COUNT] = { -.20f, -.05f, 0, .05f, .20f, .50f, 1 };
    return settings[step < 0 ? 0 : step >= BOAT_THROTTLE_COUNT ? BOAT_THROTTLE_COUNT - 1 : step];
}

static inline int boat_throttle_step(int step, int direction)
{
    step += (direction > 0) - (direction < 0);
    return step < 0 ? 0 : step >= BOAT_THROTTLE_COUNT ? BOAT_THROTTLE_COUNT - 1 : step;
}

// A held key moves one detent per BOAT_THROTTLE_REPEAT seconds after its first step. Each new
// press steps at once and restarts the wait, so quick taps change detents faster than holding.
// PRESSED and HELD are signed directions (+1 forward): newly pressed, and currently down.
#define BOAT_THROTTLE_REPEAT .5f
static inline int boat_throttle_hold(int step, int pressed, int held, float dt, float *waited)
{
    if (pressed || !held || !isfinite(dt)) {
        *waited = 0;
        return boat_throttle_step(step, pressed);
    }
    *waited += fmaxf(0, dt);
    if (*waited < BOAT_THROTTLE_REPEAT)
        return step;
    *waited = fminf(*waited - BOAT_THROTTLE_REPEAT, BOAT_THROTTLE_REPEAT);
    return boat_throttle_step(step, held);
}

// One scroll unit per detent; accumulate fractional trackpad motion. Bound stored
// remainder at either end so overscrolling cannot delay the next reverse scroll.
static inline int boat_throttle_scroll(int step, float scroll, float *remainder)
{
    if (!isfinite(scroll))
        scroll = 0;
    float amount = fmaxf(-BOAT_THROTTLE_COUNT, fminf(BOAT_THROTTLE_COUNT, *remainder + scroll));
    int whole = (int)amount;
    *remainder = amount - whole;
    step += whole;
    if (step <= 0 && amount < 0) {
        *remainder = 0;
        return 0;
    }
    if (step >= BOAT_THROTTLE_COUNT - 1 && amount > 0) {
        *remainder = 0;
        return BOAT_THROTTLE_COUNT - 1;
    }
    return step;
}

// Preserve the core's signed rate input and engine spool, stopping exactly at the target.
static inline float boat_throttle_rate(float current, float target)
{
    float rate = (target - current) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP);
    return fmaxf(-1, fminf(1, rate));
}

// Equal visual spacing per detent, interpolated for engine-command travel and old replays.
static inline float boat_throttle_position(float thrust)
{
    static const float positions[BOAT_THROTTLE_COUNT] = { 1, .5f, 0, -.25f, -.5f, -.75f, -1 };
    if (!isfinite(thrust))
        return 0;
    for (int i = 1; i < BOAT_THROTTLE_COUNT; ++i) {
        float low = boat_throttle_setting(i - 1), high = boat_throttle_setting(i);
        if (thrust <= high) {
            float fraction = fmaxf(0, (thrust - low) / (high - low));
            return positions[i - 1] + fraction * (positions[i] - positions[i - 1]);
        }
    }
    return -1;
}
#endif
