#include "boat.h"
#include "boat_profile.h"
#include "boat_test_session.h"
#include "boat_view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition);                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int check_records(void)
{
    BoatProfile profile = { 0 };
    CHECK(boat_profile_stats(&profile, boat_metric_frame).mean == 0);
    CHECK(boat_metric_stats(NULL, 0).p95 == 0);
    float known[] = { 9, 1, 7, 3, 5 };
    BoatMetricStats stats = boat_metric_stats(known, 5);
    CHECK(stats.latest == 5 && stats.mean == 5 && stats.p50 == 5 && stats.p95 == 7 && stats.p99 == 7
        && stats.maximum == 9);
    CHECK(known[0] == 9); // Reporting must not reorder caller storage.
    // Whole-run reports exceed the overlay history and sort the caller's buffer.
    enum { RUN = 3 * BOAT_PROFILE_HISTORY + 1 };
    float run[RUN];
    for (int i = 0; i < RUN; ++i)
        run[i] = (float)((i * 37) % RUN);
    BoatMetricStats whole = boat_metric_stats_sort(run, RUN);
    CHECK(whole.latest == (float)(((RUN - 1) * 37) % RUN) && whole.maximum == RUN - 1);
    CHECK(whole.p50 == (RUN - 1) * 50 / 100 && whole.p95 == (RUN - 1) * 95 / 100);
    CHECK(whole.p99 == (RUN - 1) * 99 / 100 && run[0] == 0 && run[RUN - 1] == RUN - 1);
    CHECK(boat_metric_stats_sort(NULL, 3).maximum == 0);
    BoatFrameMetrics running = { .frame_ms = 16,
        .simulation_ms = 8,
        .physics_ms = 6,
        .physics_max_ms = 3,
        .course_replay_ms = 1,
        .audio_ms = 2,
        .snapshot_render_ms = 4,
        .present_ms = 2,
        .ticks = 2 };
    boat_profile_add(&profile, running);
    CHECK(boat_profile_stats(&profile, boat_metric_ticks).latest == 2);
    CHECK(boat_profile_stats(&profile, boat_metric_simulation).latest == 8);
    CHECK(boat_profile_stats(&profile, boat_metric_physics).latest == 6);
    CHECK(boat_profile_stats(&profile, boat_metric_course_replay).latest == 1);
    CHECK(boat_profile_stats(&profile, boat_metric_frame).latest == 16);
    CHECK(boat_profile_discarded_seconds(.16, true) > .0599f);
    CHECK(boat_profile_discarded_seconds(.16, false) == 0);
    CHECK(boat_profile_discarded_seconds(.08, true) == 0);
    BoatFrameMetrics paused = { .frame_ms = 16, .paused = true };
    boat_profile_add(&profile, paused);
    CHECK(profile.frames[1].paused && profile.frames[1].discarded_active_ms == 0);
    BoatProfileSnapshot snapshot = boat_profile_snapshot(&profile);
    CHECK(snapshot.rows[0].latest == 0);
    CHECK(snapshot.rows[8].mean == 1);
    return 0;
}

static int check_drive(void)
{
    const BoatWorldResources *basin = test_world(BOAT_LOCATION_BASIN);
    BoatSession plain_session, measured_session;
    boat_session_init(&plain_session, basin, (BoatWater) { 0 });
    boat_session_init(&measured_session, basin, (BoatWater) { 0 });
    const Boat *const plain_boat = &plain_session.boat,
                      *const measured_boat = &measured_session.boat;
    BoatProfile profile = { 0 };
    for (int tick = 0; tick < 360; ++tick) {
        float steering = tick > 80 && tick < 250 ? .4f : 0;
        float throttle = tick < 180 ? 1 : 0;
        boat_session_step(&plain_session, (BoatInput) { steering, throttle });
        BoatFrameMetrics frame = { 0 };
        double started = boat_profile_time(false);
        boat_session_step(&measured_session, (BoatInput) { steering, throttle });
        frame.physics_ms = (float)((boat_profile_time(false) - started) * 1000.0);
        frame.physics_max_ms = frame.physics_ms;
        frame.simulation_ms = frame.physics_ms;
        frame.ticks = 1;
        boat_profile_add(&profile, frame);
    }
    CHECK(profile.count == BOAT_PROFILE_HISTORY);
    CHECK(plain_boat->position.x == measured_boat->position.x
        && plain_boat->position.y == measured_boat->position.y);
    CHECK(plain_boat->velocity.x == measured_boat->velocity.x
        && plain_boat->velocity.y == measured_boat->velocity.y);
    CHECK(plain_boat->turn_roll_load == measured_boat->turn_roll_load
        && plain_boat->roll == measured_boat->roll
        && plain_boat->roll_velocity == measured_boat->roll_velocity);
    CHECK(plain_boat->heading == measured_boat->heading
        && plain_session.tick == measured_session.tick);
    CHECK(plain_session.world.water.player_wake_count
        == measured_session.world.water.player_wake_count);
    Color *left = calloc(320 * 200, sizeof(Color));
    Color *right = calloc(320 * 200, sizeof(Color));
    CHECK(left && right);
    Renderer3D renderer_left = { 0 }, renderer_right = { 0 };
    BoatCpuView cpu_left = { 0 }, cpu_right = { 0 };
    CHECK(boat_draw(&renderer_left, &cpu_left, left, 320, 200, SESSION_VIEW(&plain_session),
        &plain_session.presentation, &plain_session.world, BOAT_CAMERA_DEFAULT));
    CHECK(boat_draw(&renderer_right, &cpu_right, right, 320, 200, SESSION_VIEW(&measured_session),
        &measured_session.presentation, &measured_session.world, BOAT_CAMERA_DEFAULT));
    CHECK(memcmp(left, right, 320 * 200 * sizeof(Color)) == 0);
    boat_cpu_view_destroy(&cpu_left);
    boat_cpu_view_destroy(&cpu_right);
    renderer3d_destroy(&renderer_left);
    renderer3d_destroy(&renderer_right);
    free(left);
    free(right);
    return 0;
}

int main(void)
{
    test_worlds_open();
    if (check_records() || check_drive())
        return 1;
    puts("boating profile records and deterministic drive passed");
    test_worlds_close();
    return 0;
}
