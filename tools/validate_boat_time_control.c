#include "boat_session.h"
#include "boat_test_world.h"
#include "boat_time_control.h"

#include <stdio.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void)
{
    CHECK(boat_time_speed_step(0, -1) == 0);
    CHECK(boat_time_speed_step(4, 1) == 4);
    CHECK(boat_time_speed_step(BOAT_TIME_NORMAL, 0) == BOAT_TIME_NORMAL);
    CHECK(boat_time_frame_seconds(1, 4) == .4);
    CHECK(boat_time_frame_seconds(-1, 4) == 0);
    CHECK(boat_time_frame_seconds(NAN, 4) == 0);
    CHECK(boat_time_frame_seconds(INFINITY, 4) == 0);
    BoatInput input = { .throttle_rate = 1, .steering_rate = .15f };
    BoatSession reference;
    boat_session_init(
        &reference, test_world(BOAT_LOCATION_BASIN), boat_water_test(true, false, false));
    reference.presentation_enabled = false;
    for (int i = 0; i < 24; ++i)
        CHECK(boat_session_step(&reference, input));
    for (int index = 0; index <= 4; ++index) {
        BoatSession scaled;
        boat_session_init(
            &scaled, test_world(BOAT_LOCATION_BASIN), boat_water_test(true, false, false));
        scaled.presentation_enabled = false;
        double accumulator = 0;
        int frames = (int)(12 / boat_time_rate(index));
        for (int frame = 0; frame < frames; ++frame) {
            accumulator += boat_time_frame_seconds(1.0 / 60, index);
            while (accumulator >= BOAT_TICK_SECONDS) {
                CHECK(boat_session_step(&scaled, input));
                accumulator -= BOAT_TICK_SECONDS;
            }
        }
        CHECK(scaled.tick == reference.tick);
        CHECK(memcmp(&scaled.boat, &reference.boat, sizeof(scaled.boat)) == 0);
        boat_session_destroy(&scaled);
    }
    boat_session_destroy(&reference);
    test_worlds_close();
    puts("boat_time_control=ok");
    return 0;
}
