#ifndef PROTEUS_BOAT_TIME_CONTROL_H
#define PROTEUS_BOAT_TIME_CONTROL_H

#include <math.h>
#include <stdbool.h>

#define BOAT_TIME_NORMAL 2
#define BOAT_TIME_MAX_INDEX 5

// Live play and replay share the same six rates. The physical tick stays at 1/120 s.
static inline float boat_time_rate(int index)
{
    static const float rates[] = { .25f, .5f, 1, 2, 4, 8 };
    return rates[index < 0 ? 0 : index > BOAT_TIME_MAX_INDEX ? BOAT_TIME_MAX_INDEX : index];
}

static inline int boat_time_speed_step(int index, int direction)
{
    index += (direction > 0) - (direction < 0);
    return index < 0 ? 0 : index > BOAT_TIME_MAX_INDEX ? BOAT_TIME_MAX_INDEX : index;
}

// Clamp real frame stalls before scaling; clamping afterwards would truncate fast-forward.
static inline double boat_time_frame_seconds(double seconds, int index)
{
    return (isfinite(seconds) ? fmax(0, fmin(seconds, .1)) : 0) * boat_time_rate(index);
}

// A work session resumes without consuming loading, focus loss or suspension time. A gap over
// one second is treated as suspension even when the platform never delivered focus loss; shorter
// stalls keep the existing clamp. This filters the shared world/light tick input, not a new clock.
static inline double boat_time_work_frame_seconds(double seconds, int index, bool was_active)
{
    if (!was_active || !isfinite(seconds) || seconds > 1.0)
        return 0;
    return boat_time_frame_seconds(seconds, index);
}

#endif
