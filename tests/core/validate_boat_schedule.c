// Marina M3 groundwork: exact common tick period for planned route clearance.
#include "core/boat_schedule.h"

#include <stdio.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int common_periods(void)
{
    uint64_t out = 0;
    const uint64_t coprime[] = { 3, 5, 7 };
    CHECK(boat_schedule_common_period(coprime, 3, 105, &out) && out == 105);

    // Shared factors combine through the running least common multiple, not the product.
    const uint64_t shared[] = { 6, 9, 15 };
    out = 0;
    CHECK(boat_schedule_common_period(shared, 3, 90, &out) && out == 90);

    const uint64_t nested[] = { 12, 4, 6 };
    out = 0;
    CHECK(boat_schedule_common_period(nested, 3, 12, &out) && out == 12);

    const uint64_t unit[] = { 1, 42 };
    out = 0;
    CHECK(boat_schedule_common_period(unit, 2, 42, &out) && out == 42);

    const uint64_t single[] = { 7 };
    out = 0;
    CHECK(boat_schedule_common_period(single, 1, 7, &out) && out == 7);
    return 0;
}

static int bounds(void)
{
    const uint64_t periods[] = { 2, 3, 5 };
    uint64_t out = 0;
    CHECK(boat_schedule_common_period(periods, 3, 30, &out) && out == 30);

    // One tick below the exact period fails without touching the output.
    out = 30;
    CHECK(!boat_schedule_common_period(periods, 3, 29, &out) && out == 30);

    // A single period above the bound fails the same way.
    out = 7;
    CHECK(!boat_schedule_common_period(periods, 1, 1, &out) && out == 7);
    return 0;
}

static int overflow(void)
{
    // The true common period would be 2 * UINT64_MAX, which cannot be represented.
    const uint64_t doubling[] = { UINT64_MAX, 2 };
    uint64_t out = 11;
    CHECK(!boat_schedule_common_period(doubling, 2, UINT64_MAX, &out) && out == 11);

    // Dividing by the shared factor first keeps the largest representable product exact:
    // (2^32 - 1) * 2^32 = 2^64 - 2^32, just below UINT64_MAX.
    const uint64_t near_top[] = { UINT32_MAX, (uint64_t)UINT32_MAX + 1 };
    out = 0;
    CHECK(boat_schedule_common_period(near_top, 2, UINT64_MAX, &out)
        && out == UINT64_C(18446744069414584320));

    // 2^63 and 3 are coprime, so their common period is 3 * 2^63: one and a half times the maximum.
    const uint64_t high_bit[] = { UINT64_C(1) << 63, 3 };
    out = 5;
    CHECK(!boat_schedule_common_period(high_bit, 2, UINT64_MAX, &out) && out == 5);
    return 0;
}

static int invalid_input(void)
{
    const uint64_t periods[] = { 2, 3 };
    uint64_t out = 17;

    CHECK(!boat_schedule_common_period(NULL, 2, 100, &out) && out == 17);
    CHECK(!boat_schedule_common_period(periods, 2, 100, NULL));
    CHECK(!boat_schedule_common_period(periods, 0, 100, &out) && out == 17);
    CHECK(!boat_schedule_common_period(periods, 2, 0, &out) && out == 17);

    const uint64_t zero_period[] = { 4, 0, 6 };
    CHECK(!boat_schedule_common_period(zero_period, 3, 100, &out) && out == 17);
    return 0;
}

int main(void)
{
    if (common_periods() || bounds() || overflow() || invalid_input())
        return 1;
    puts("boat_schedule: coprime and shared-factor periods, exact bound, overflow and invalid "
         "input");
    return 0;
}
