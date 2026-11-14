#include "core/boat_schedule.h"

static uint64_t greatest_common_divisor(uint64_t a, uint64_t b)
{
    while (b != 0) {
        uint64_t remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}

bool boat_schedule_common_period(
    const uint64_t *periods, size_t count, uint64_t max_ticks, uint64_t *out_ticks)
{
    if (periods == NULL || out_ticks == NULL || count == 0 || max_ticks == 0)
        return false;
    uint64_t common = 1;
    for (size_t i = 0; i < count; ++i) {
        uint64_t period = periods[i];
        if (period == 0)
            return false;
        // Divide by the shared factor before multiplying, then compare against the caller's
        // bound. Because MAX_TICKS is at most UINT64_MAX, this also rejects uint64 overflow.
        uint64_t divisor = greatest_common_divisor(common, period);
        uint64_t reduced = common / divisor;
        if (period > max_ticks / reduced)
            return false;
        common = reduced * period;
    }
    *out_ticks = common;
    return true;
}
