/* Frame pacer deadline chain: synthetic clocks, no window or sleeping. */
#include "frame_pacer.h"

#include <math.h>
#include <stdio.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define NEAR(a, b) (fabs((a) - (b)) < 1e-9)

int main(void)
{
    const double t = 1.0 / 60;
    FramePacer pacer;

    frame_pacer_init(&pacer, 0);
    CHECK(frame_pacer_next(&pacer, 3) == 3); // Disabled: never waits.

    // The first frame starts the chain; steady 5 ms frames wait for exact period boundaries.
    frame_pacer_init(&pacer, 60);
    CHECK(frame_pacer_next(&pacer, 10) == 10);
    double now = 10;
    for (int i = 1; i <= 600; ++i) {
        double until = frame_pacer_next(&pacer, now + 0.005);
        CHECK(NEAR(until, 10 + i * t));
        now = until + 0.0004; // Every wait oversleeps; the next frame repays it.
    }

    // A late frame is repaid from the following one instead of shifting the chain.
    frame_pacer_init(&pacer, 60);
    frame_pacer_next(&pacer, 0);
    CHECK(NEAR(frame_pacer_next(&pacer, 1.5 * t), 1.5 * t));
    CHECK(NEAR(frame_pacer_next(&pacer, 1.6 * t), 2 * t));

    // More than a period behind (a stall), the chain restarts instead of bursting.
    frame_pacer_init(&pacer, 60);
    frame_pacer_next(&pacer, 0);
    CHECK(NEAR(frame_pacer_next(&pacer, 2.5 * t), 2.5 * t));
    CHECK(NEAR(frame_pacer_next(&pacer, 2.6 * t), 3.5 * t));

    // A vsync backstop above the display rate never waits while swaps arrive at the display rate.
    frame_pacer_init(&pacer, 120 * FRAME_PACER_VSYNC_HEADROOM);
    frame_pacer_next(&pacer, 0);
    for (int i = 1; i <= 1200; ++i) {
        double vblank = i / 120.0 + 0.004;
        CHECK(frame_pacer_next(&pacer, vblank) == vblank);
    }
    puts("frame pacer deadline chain pass");
    return 0;
}
