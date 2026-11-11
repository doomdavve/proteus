#ifndef PROTEUS_BOAT_CLOCK_H
#define PROTEUS_BOAT_CLOCK_H

#include <math.h>
#include <stdint.h>

// The float integrator step remains BOAT_STEP. Live time is derived from this integer clock.
#define BOAT_TICK_HZ 120
#define BOAT_TICK_SECONDS (1.0 / BOAT_TICK_HZ)

#define BOAT_TWO_PI 6.283185307179586476925

static inline double boat_tick_seconds(uint64_t tick) { return (double)tick / BOAT_TICK_HZ; }

// Reduce value to [-period/2, period/2] in double, like remainder() but cheap enough for every
// water query. The fused subtraction keeps month-long sessions within about 1e-9 of exact.
static inline double boat_reduce(double value, double period)
{
    return fma(-nearbyint(value * (1 / period)), period, value);
}

// Reduce rate * seconds to [-pi, pi] in double before narrowing. Use one phase per periodic
// component; never wrap a shared time by an arbitrary period.
static inline float boat_phase(double angular_speed, double seconds)
{
    return (float)boat_reduce(angular_speed * seconds, BOAT_TWO_PI);
}

// Same reduction for a linear coordinate on a closed route of the given length.
static inline float boat_cycle(double speed, double seconds, double length)
{
    return (float)boat_reduce(speed * seconds, length);
}

#endif
