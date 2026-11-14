#ifndef PROTEUS_BOAT_SCHEDULE_H
#define PROTEUS_BOAT_SCHEDULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Marina M3 groundwork: the exact common period of strictly positive integer tick periods.
// This internal helper only computes a least common multiple. It does not validate routes or
// claim that a schedule is clear; callers own that interpretation.
//
// PERIODS holds COUNT entries, COUNT is positive, and MAX_TICKS bounds the result. The product
// is formed with overflow-safe arithmetic: the running common period is divided by its greatest
// common divisor with the next period before multiplication. A result above MAX_TICKS is rejected.
// OUT_TICKS receives the
// common period only on success and is left unchanged on failure. Returns false for a null
// PERIODS or OUT_TICKS, zero COUNT, zero MAX_TICKS, a zero period, or a common period above
// MAX_TICKS. No clock, floating point, allocation, global mutable state or I/O.
bool boat_schedule_common_period(
    const uint64_t *periods, size_t count, uint64_t max_ticks, uint64_t *out_ticks);

#endif
