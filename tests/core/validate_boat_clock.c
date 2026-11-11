/* C3 long-session clock gate: cheap direct-time checks and an opt-in simulated soak. */
#include "boat_clock.h"
#include "boat_fixtures.h"
#include "boat_map.h"
#include "boat_replay.h"
#include "boat_session.h"
#include "boat_test_cases.h"
#include "boat_test_session.h"
#include "boat_view.h"
#include "raymath.h"
#include "wake_velocity_state.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define DAY_TICKS (UINT64_C(86400) * BOAT_TICK_HZ)

static uint64_t next_random(uint64_t *state)
{
    *state = *state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return *state >> 11;
}

// Tick seconds stay exact, and the shared reduction matches libm remainder() to one float ulp.
static int reduction_checks(void)
{
    const uint64_t ticks[] = { 0, 1, 120, 28800, 432000, DAY_TICKS, 30 * DAY_TICKS,
        3650 * DAY_TICKS, (UINT64_C(1) << 40) + 7 };
    for (size_t i = 0; i < sizeof(ticks) / sizeof(ticks[0]); ++i) {
        double seconds = boat_tick_seconds(ticks[i]);
        CHECK((uint64_t)llround(seconds * BOAT_TICK_HZ) == ticks[i]);
        CHECK(boat_tick_seconds(ticks[i] + 1) > seconds);
    }
    uint64_t state = 1;
    float worst = 0;
    for (int i = 0; i < 200000; ++i) {
        double rate = .05 + (double)(next_random(&state) % 1000000) / 1000000 * 15;
        double seconds = (double)(next_random(&state) % (30 * DAY_TICKS)) / BOAT_TICK_HZ;
        float phase = boat_phase(rate, seconds);
        float exact = (float)remainder(rate * seconds, BOAT_TWO_PI);
        CHECK(fabsf(phase) <= 3.1415930f);
        // Exact half-period ties may land on either side of the wrap; the surface is equal.
        float error = fabsf(phase - exact);
        if (error > 6)
            error = fabsf(error - (float)BOAT_TWO_PI);
        worst = fmaxf(worst, error);
        double length = 50 + (double)(next_random(&state) % 1000);
        float cycle = boat_cycle(rate, seconds, length);
        CHECK(fabsf(cycle) <= length / 2 + 1e-3);
        CHECK(fabsf(cycle - (float)remainder(rate * seconds, length)) <= 1e-4f
            || fabsf(fabsf(cycle) - (float)length / 2) < 1e-3f);
    }
    CHECK(worst <= 4e-7f);
    printf("clock_reduction=ok samples=200000 worst_phase_error=%.3g\n", worst);
    return 0;
}

static uint64_t physical_hash(const Boat *boat, const ObjectStudy *study)
{
    return wake_velocity_state_hash(WAKE_VELOCITY_HASH_SEED, boat, 0, object_study_wakes(study));
}

// Calm-water handling and arcade wakes depend only on relative ticks: starting the fixed
// P3a input sequence a day or a month into a session reproduces every physical field.
static int calm_independence(void)
{
    const uint64_t starts[] = { 0, DAY_TICKS, 30 * DAY_TICKS };
    static BoatSession sessions[3];
    static ObjectStudy studies[3];
    for (int i = 0; i < 3; ++i) {
        physics_session(&sessions[i], BOAT_LOCATION_BASIN);
        object_study_reset(&studies[i], &sessions[i], false, BOAT_OBJECT_LIFE_TICKS, 1.8f);
        boat_session_set_tick(&sessions[i], starts[i]);
    }
    for (int tick = 0; tick < 14640; ++tick)
        for (int i = 0; i < 3; ++i) {
            demo_step(&sessions[i], tick, NULL, NULL, &studies[i]);
            CHECK(sessions[i].tick == starts[i] + (uint64_t)tick + 1);
            CHECK(boat_session_view(&sessions[i]).time == boat_tick_seconds(sessions[i].tick));
            if (i)
                CHECK(physical_hash(&sessions[i].boat, &studies[i])
                    == physical_hash(&sessions[0].boat, &studies[0]));
        }
    CHECK(studies[0].wakes.emitted > 0 && sessions[0].boat.landings > 0);
    CHECK(studies[0].wakes.live == 0);
    printf("clock arcade fixture: %llu births, %u landings, all three start times bit-identical\n",
        (unsigned long long)studies[0].wakes.emitted, sessions[0].boat.landings);
    // The legacy D3 trail stores double births; equal relative histories match closely.
    for (int i = 0; i < 3; ++i) {
        boat_session_init(&sessions[i], test_world(BOAT_LOCATION_BASIN),
            (BoatWater) { .player_wake_enabled = true });
        sessions[i].presentation_enabled = false;
        boat_session_set_tick(&sessions[i], starts[i]);
    }
    float divergence = 0;
    for (int tick = 0; tick < 2400; ++tick)
        for (int i = 0; i < 3; ++i) {
            demo_step(&sessions[i], tick, NULL, NULL, NULL);
            if (i) {
                divergence = fmaxf(divergence,
                    Vector2Distance(sessions[i].boat.position, sessions[0].boat.position)
                        + fabsf(sessions[i].boat.height - sessions[0].boat.height));
                CHECK(sessions[i].world.water.player_wake_count
                    == sessions[0].world.water.player_wake_count);
            }
        }
    CHECK(sessions[0].world.water.player_wake_count > 10 && divergence < 1e-3f);
    printf("calm_time_independence=ok arcade_ticks=14640 starts=0,1d,30d d3_divergence=%.3g\n",
        divergence);
    return 0;
}

static bool boat_finite(const Boat *b)
{
    const float values[] = { b->position.x, b->position.y, b->velocity.x, b->velocity.y, b->heading,
        b->height, b->vertical_velocity, b->pitch, b->roll, b->yaw_velocity };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        if (!isfinite(values[i]))
            return false;
    return true;
}

// Time-driven water, traffic and moored boats at one day and thirty days: cheap direct
// starts with powered turns through gentle waves in every location.
static int large_time_locations(void)
{
    const uint64_t starts[] = { DAY_TICKS, 30 * DAY_TICKS };
    for (int location = 0; location < BOAT_LOCATION_COUNT; ++location) {
        const BoatWorldDefinition *definition = boat_location((BoatLocationId)location);
        for (int s = 0; s < 2; ++s) {
            BoatSession session;
            BoatWater water = boat_water_gentle();
            water.player_wake_enabled = true;
            boat_session_init(&session, test_world(definition->id), water);
            session.presentation_enabled = false; // Time-driven physics only.
            boat_session_set_tick(&session, starts[s]);
            const Boat *boat = &session.boat;
            float peak = 0;
            for (int tick = 0; tick < 120 * 30; ++tick) {
                float throttle = tick < 120 * 20 ? .8f : 0;
                float wheel = tick > 240 && tick < 120 * 12 ? .5f : 0;
                boat_session_step(&session,
                    (BoatInput) { .steering_rate = Clamp((wheel - boat->wheel) / BOAT_STEP, -1, 1),
                        .throttle_rate = Clamp(
                            (throttle - boat->throttle) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP),
                            -1, 1) });
                CHECK(boat_finite(boat) && fabsf(boat->height) < 1.5f);
                CHECK(boat_session_time(&session)
                    == boat_tick_seconds(starts[s] + (uint64_t)tick + 1));
                if (location)
                    CHECK(test_river_water(boat->position, 0));
                else
                    CHECK(fabsf(boat->position.x) < BOAT_BASIN_HALF_X
                        && fabsf(boat->position.y) < BOAT_BASIN_HALF_Z);
                peak = fmaxf(peak, fabsf(boat->height));
            }
            CHECK(peak > .02f); // The hull actually rides the time-driven waves.
            WaterfrontSceneFrame frame;
            BoatViewState view = boat_session_view(&session);
            boat_scene_frame(
                &view, &session.presentation, &session.world, BOAT_CAMERA_DEFAULT, &frame);
            CHECK(frame.time == boat_session_time(&session));
        }
    }
    puts("large_time_locations=ok basin,river starts=1d,30d waves+d3");
    return 0;
}

// Course recordings keep world time as recorded; a run started late in a session must load.
static char replay_path[4096];

static int replay_world_time(void)
{
    const char *path = replay_path;
    BoatSession session;
    boat_session_init(&session, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat_session_set_course_definition(&session, &boat_fixture_course_default);
    boat_session_start_course(&session);
    boat_session_set_tick(&session, 30 * DAY_TICKS + 17);
    Boat *boat = &session.boat;
    BoatReplay run = { 0 }, loaded = { 0 };
    const Vector2 points[] = { { 0, 37 }, { 22, 7 }, { -22, -23 }, { 0, -55 } };
    for (int leg = 0; leg < 3 && !run.complete; ++leg)
        for (int tick = 1; tick <= 480 && !run.complete; ++tick) {
            BoatReplaySample before
                = boat_replay_pose(boat, &session.presentation, boat_session_time(&session));
            float t = (float)tick / 480;
            boat->position = Vector2Lerp(points[leg], points[leg + 1], t);
            boat->velocity = Vector2Scale(Vector2Subtract(boat->position, before.position), 120);
            boat->heading = session.presentation.camera_heading
                = atan2f(boat->velocity.x, -boat->velocity.y);
            boat_session_set_tick(&session, session.tick + 1);
            boat_course_step(&session.course, before.position, boat->position, BOAT_TICK_SECONDS);
            boat_replay_record(&run, before,
                boat_replay_pose(boat, &session.presentation, boat_session_time(&session)),
                &session.course, session.world.water.amplitude != 0);
        }
    CHECK(run.complete && run.samples[0].world_time > 30 * 86400.0);
    CHECK(boat_replay_save(&run, path) && boat_replay_load(&loaded, path));
    CHECK(loaded.count == run.count);
    for (size_t i = 0; i < run.count; ++i)
        CHECK(loaded.samples[i].world_time == run.samples[i].world_time);
    BoatViewState view;
    CHECK(boat_replay_sample(&loaded, loaded.samples[1].at, &view, NULL));
    CHECK(view.time == loaded.samples[1].world_time);
    boat_replay_destroy(&run);
    boat_replay_destroy(&loaded);
    remove(path);
    puts("replay_world_time=ok start=30d round_trip=exact");
    return 0;
}

// Bounded basin soak: arcade wakes, a crossing boat and endless steered loops. Checks every
// tick; renders only a few representative CPU frames.
static int soak(double seconds, const char *directory, bool waves)
{
    static BoatSession session;
    static ObjectStudy study;
    // Since M2 the crossing boat shares the player's fleet world, waves included.
    boat_session_init(
        &session, test_world(BOAT_LOCATION_BASIN), waves ? boat_water_gentle() : (BoatWater) { 0 });
    object_study_reset(&study, &session, true, BOAT_OBJECT_LIFE_TICKS, 1.8f);
    const BoatWakeObjects *pool = object_study_wakes(&study);
    const Boat *other_boat = object_study_other(&study);
    Boat *boat = &session.boat;
    uint64_t total = (uint64_t)llround(seconds * BOAT_TICK_HZ);
    const uint64_t samples[] = { 120 * 60, total / 2, total };
    Renderer3D renderer = { 0 };
    BoatCpuView cpu_view = { 0 };
    Color *pixels = malloc(320 * 200 * sizeof(*pixels));
    CHECK(pixels);
    uint64_t hash = WAKE_VELOCITY_HASH_SEED, recoveries = 0, stuck_ticks = 0, landings = 0;
    unsigned waypoint = 0;
    float max_speed = 0, max_height = 0;
    clock_t started = clock();
    for (uint64_t tick = 0; tick < total; ++tick) {
        // Steer a counter-rotating loop through the crossing boat's route and its wakes,
        // weaving on each leg; every fifth 40 s cycle coasts in neutral for ten seconds.
        static const Vector2 route[] = { { -70, 30 }, { 70, 30 }, { 70, -35 }, { -70, -35 } };
        Vector2 delta = Vector2Subtract(route[waypoint], boat->position);
        if (Vector2Length(delta) < 18)
            waypoint = (waypoint + 1) % 4;
        float weave = .35f * sinf((float)(tick % 1080) / 1080 * 2 * PI);
        float error = remainderf(atan2f(delta.x, -delta.y) + weave - boat->heading, 2 * PI);
        float wheel = Clamp(1.5f * error - .3f * boat->yaw_velocity, -1, 1);
        float throttle = tick % (200 * 120) < 190 * 120 ? 1 : 0;
        timed_boat_step(&session,
            (BoatInput) { .steering_rate = Clamp((wheel - boat->wheel) / BOAT_STEP, -1, 1),
                .throttle_rate
                = Clamp((throttle - boat->throttle) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP), -1,
                    1) },
            NULL, NULL, &study);
        // Recover pose only after a capsize or a shoreline stall: the world clock, water
        // settings and wake history continue.
        bool stalled = throttle > 0 && Vector2Length(boat->velocity) < .5f;
        stuck_ticks = boat->motion_state == BOAT_CAPSIZED || stalled ? stuck_ticks + 1 : 0;
        if (stuck_ticks >= 5 * 120) {
            landings += boat->landings;
            boat_session_respawn(&session);
            stuck_ticks = 0;
            ++recoveries;
        }
        CHECK(boat_finite(boat) && boat_finite(other_boat));
        CHECK(session.tick == tick + 1 && pool->tick == tick + 1);
        CHECK(fabsf(boat->position.x) < BOAT_BASIN_HALF_X
            && fabsf(boat->position.y) < BOAT_BASIN_HALF_Z);
        CHECK(pool->live <= BOAT_OBJECT_CAPACITY && pool->peak_live <= BOAT_OBJECT_CAPACITY);
        CHECK(pool->emitted == pool->expired + pool->live);
        max_speed = fmaxf(max_speed, Vector2Length(boat->velocity));
        max_height = fmaxf(max_height, fabsf(boat->height));
        if ((tick + 1) % 2 == 0)
            hash = wake_velocity_state_hash(hash, boat, boat_session_time(&session), pool);
        if ((tick + 1) % (600 * 120) == 0)
            printf("soak minute=%" PRIu64 " emitted=%" PRIu64 " live=%u peak=%u landings=%" PRIu64
                   " "
                   "recoveries=%" PRIu64 " position=%.2f,%.2f\n",
                (tick + 1) / 7200, pool->emitted, pool->live, pool->peak_live,
                landings + boat->landings, recoveries, boat->position.x, boat->position.y);
        for (int s = 0; s < 3; ++s)
            if (tick + 1 == samples[s] && (s == 0 || samples[s] != samples[s - 1])) {
                char path[4096];
                CHECK(snprintf(path, sizeof(path), "%s/soak%s-%" PRIu64 "s.png", directory,
                          waves ? "-waves" : "", samples[s] / 120)
                    < (int)sizeof(path));
                BoatViewState view = boat_session_view(&session);
                BoatViewState other = { .boat = *other_boat, .time = view.time };
                CHECK(boat_draw_with_ghost(&renderer, &cpu_view, pixels, 320, 200, &view,
                    &session.presentation, &session.world, BOAT_CAMERA_DEFAULT, &other,
                    &study.other_presentation.flag, 1));
                Image image = { pixels, 320, 200, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
                CHECK(ExportImage(image, path));
            }
    }
    double cpu = (double)(clock() - started) / CLOCKS_PER_SEC;
    CHECK(pool->emitted > total / 2400); // Emission continued throughout the session.
    printf("soak=ok seconds=%.0f waves=%d ticks=%" PRIu64 " time=%.17g emitted=%" PRIu64
           " expired=%" PRIu64 " peak_live=%u/%d landings=%" PRIu64 " recoveries=%" PRIu64
           " max_speed=%.3f max_height=%.3f hash=%016" PRIx64 " cpu_s=%.2f\n",
        seconds, waves, session.tick, boat_session_time(&session), pool->emitted, pool->expired,
        pool->peak_live, BOAT_OBJECT_CAPACITY, landings + boat->landings, recoveries, max_speed,
        max_height, hash, cpu);
    boat_cpu_view_destroy(&cpu_view);
    renderer3d_destroy(&renderer);
    free(pixels);
    return 0;
}

static const TestCase cases[] = {
    TEST_CASE(reduction_checks),
    SLOW_CASE(calm_independence),
    SLOW_CASE(large_time_locations),
    TEST_CASE(replay_world_time),
};

int main(int argc, char **argv)
{
    SetTraceLogLevel(LOG_WARNING);
    if (argc >= 4 && strcmp(argv[1], "--soak") == 0) {
        char *end;
        double seconds = strtod(argv[2], &end);
        bool waves = argc == 5 && strcmp(argv[4], "--waves") == 0;
        if (*end || !(seconds >= 60 && seconds <= 86400) || (argc == 5 && !waves) || argc > 5) {
            fputs("Usage: validate_boat_clock [--soak 60..86400 DIRECTORY [--waves]]\n", stderr);
            return 2;
        }
        return soak(seconds, argv[3], waves);
    }
    if (argc < 2 || argv[1][0] == '-') {
        fputs("Usage: validate_boat_clock TEMP_DIRECTORY [--list | --case NAME] | --soak SECONDS "
              "DIRECTORY [--waves]\n",
            stderr);
        return 2;
    }
    CHECK(snprintf(replay_path, sizeof(replay_path), "%s/late-world.run", argv[1])
        < (int)sizeof(replay_path));
    argv[1] = argv[0];
    int result = test_cases_main(cases, TEST_CASE_COUNT(cases), argc - 1, argv + 1);
    if (!result && argc == 2)
        puts("boat_clock=ok");
    test_worlds_close();
    return result;
}
