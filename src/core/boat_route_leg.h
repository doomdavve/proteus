#ifndef PROTEUS_BOAT_ROUTE_LEG_H
#define PROTEUS_BOAT_ROUTE_LEG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Marina M3 motion groundwork: one straight open route leg evaluated directly at an explicit
// simulation tick. This primitive is not connected to the route system. It provides no turns,
// no collision proof and no docking; callers own those concerns.
//
// Coordinates are simulation metres. HEADING uses atan2(dx, -dz), matching the simulation's
// convention, and SPEED is metres per second along the fixed direction. Positions are computed
// from the tick alone, so seeks are deterministic and no floating-point frame accumulation is
// involved.
#define BOAT_ROUTE_LEG_COORDINATE_LIMIT 20000.0

typedef struct BoatRouteLeg {
    double start_x, start_z;
    double end_x, end_z;
    uint64_t duration_ticks;
    double tick_seconds;
    double max_speed;
    double max_acceleration;
} BoatRouteLeg;

typedef struct BoatRoutePose {
    double x, z, heading, speed;
} BoatRoutePose;

// True when LEG describes a valid straight leg: non-null, finite coordinates within
// +/-BOAT_ROUTE_LEG_COORDINATE_LIMIT, nonzero length, positive duration_ticks, finite positive
// tick_seconds, max_speed and max_acceleration, and finite motion whose peak speed
// 1.5 * distance / duration and peak absolute acceleration 6 * distance / duration^2 stay within
// those limits. No state is modified.
bool boat_route_leg_validate(const BoatRouteLeg *leg);

// Evaluate LEG at TICK and write the pose only on success. The normalized progress is
// u = clamp(TICK / duration_ticks, 0, 1); the position blends from start to end with
// 3u^2 - 2u^3, the heading is fixed, and the speed is distance * (6u - 6u^2) / duration_seconds.
// At or before tick zero and at or after duration_ticks the endpoints are returned with zero
// speed. Returns false for any invalid leg, a null POSE, or a nonfinite intermediate, leaving
// POSE unchanged.
bool boat_route_leg_evaluate(const BoatRouteLeg *leg, uint64_t tick, BoatRoutePose *pose);

#endif
