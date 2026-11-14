#ifndef PROTEUS_BOAT_ROUTE_CLEARANCE_H
#define PROTEUS_BOAT_ROUTE_CLEARANCE_H

#include "core/boat_route_leg.h"

#include <stdbool.h>
#include <stdint.h>

// Marina M3 motion groundwork: conservative clearance between two straight open routes. This is a
// pairwise check only. It is not a fleet scene proof, a timetable feasibility proof, a turn or
// docking rule, and it does not model hull shape beyond enclosing circles.
//
// Both legs start at tick zero and share one finite positive tick_seconds. Each leg's position
// follows boat_route_leg_evaluate: a straight smoothstep that clamps to its endpoint after its
// duration. The horizon is max(duration_ticks); every integer sample from zero through the horizon
// inclusive is checked. Max_intervals bounds the number of intervals examined, so a horizon larger
// than it is rejected as BUDGET before any sampling.
//
// Sampling alone cannot prove that two moving hulls never meet between samples. The check therefore
// adds a Lipschitz safety margin. A smoothstep leg moves at most its peak speed 1.5 * distance /
// duration_seconds, so during half a tick its position changes by at most that peak times
// tick_seconds / 2. For any instant between two adjacent samples, the combined relative travel is
// at most (peak_speed_a + peak_speed_b) * tick_seconds / 2. If the sampled centre distance exceeds
// radius_a + radius_b + that margin, the centre distance throughout both adjacent half intervals
// stays above the sum of the radii. A peak is used even after a leg has clamped to its endpoint,
// which only makes the bound more conservative.
//
// This margin can report CONFLICT for pairs that would in fact pass at different times or on curved
// real paths; those are conservative false positives, not errors. CLEAR means only that this
// pairwise circle model found no potential contact on the sampled horizon.
typedef enum BoatRouteClearance {
    BOAT_ROUTE_CLEARANCE_CLEAR = 0,
    BOAT_ROUTE_CLEARANCE_CONFLICT,
    BOAT_ROUTE_CLEARANCE_INVALID,
    BOAT_ROUTE_CLEARANCE_BUDGET
} BoatRouteClearance;

// Check two valid legs with positive finite enclosing-circle radii and a positive max_intervals.
// Returns CONFLICT when any sampled centre distance is at most radius_a + radius_b + margin, with
// the margin described above. Returns INVALID for null legs, invalid legs, mismatched tick_seconds,
// nonpositive or nonfinite radii or max_intervals, and nonfinite intermediate arithmetic. Returns
// BUDGET when max(duration_ticks) exceeds max_intervals. The inputs are read only; nothing is
// allocated or retained.
BoatRouteClearance boat_route_clearance_check(const BoatRouteLeg *a, double radius_a,
    const BoatRouteLeg *b, double radius_b, uint64_t max_intervals);

#endif
