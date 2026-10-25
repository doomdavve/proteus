/* Compare the frozen 2994e0d app driver against the extracted production helper. */
#include "boat_drive_baseline.h"
#include "boat_test_world.h"
#include "boat_view.h"
#include "raymath.h"
#include "wake_velocity_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
int main(void)
{
    test_worlds_open();
    const BoatWorldResources *basin = test_world(BOAT_LOCATION_BASIN);
    BoatSession a, b;
    BaselineStudy wa;
    ObjectStudy wb;
    boat_session_init(&a, basin, boat_water_test(false, false, false));
    boat_session_init(&b, basin, boat_water_test(false, false, false));
    baseline_reset(&wa, &a, false, BOAT_OBJECT_LIFE_TICKS, 1.8f);
    object_study_reset(&wb, &b, false, BOAT_OBJECT_LIFE_TICKS, 1.8f);
    Renderer3D ra = { 0 }, rb = { 0 };
    BoatCpuView cpu_a = { 0 }, cpu_b = { 0 };
    Color pa[320 * 200], pb[320 * 200];
    for (int tick = 0; tick < 3840; ++tick) {
        baseline_demo(&a, tick, NULL, NULL, &wa);
        demo_step(&b, tick, NULL, NULL, &wb);
        CHECK(wake_velocity_state_hash(
                  WAKE_VELOCITY_HASH_SEED, &a.boat, boat_session_time(&a), &wa.wakes)
              == wake_velocity_state_hash(WAKE_VELOCITY_HASH_SEED, &b.boat, boat_session_time(&b),
                  object_study_wakes(&wb)));
        if (tick == 1079 || tick == 2399) {
            BoatViewState va = boat_session_view(&a), vb = boat_session_view(&b);
            CHECK(boat_draw(
                &ra, &cpu_a, pa, 320, 200, &va, &a.presentation, &a.world, BOAT_CAMERA_DEFAULT));
            CHECK(boat_draw(
                &rb, &cpu_b, pb, 320, 200, &vb, &b.presentation, &b.world, BOAT_CAMERA_DEFAULT));
            CHECK(memcmp(pa, pb, sizeof(pa)) == 0);
        }
    }
    boat_cpu_view_destroy(&cpu_a);
    boat_cpu_view_destroy(&cpu_b);
    renderer3d_destroy(&ra);
    renderer3d_destroy(&rb);
    puts("3840 tick physical/wake hashes and 1080/2400 tick CPU pixels match old driver");
    test_worlds_close();
    return 0;
}
