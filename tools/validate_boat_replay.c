#include "boat_replay.h"
#include "boat_test_session.h"
#include "boat_view.h"
#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "Replay check failed at line %d: %s\n", __LINE__, #condition);         \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

// Field by field: BoatReplaySample has padding before world_time, which memcmp would compare.
static bool same_samples(const BoatReplaySample *a, const BoatReplaySample *b, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const BoatReplaySample *x = &a[i], *y = &b[i];
        if (x->at != y->at || x->position.x != y->position.x || x->position.y != y->position.y
            || x->velocity.x != y->velocity.x || x->velocity.y != y->velocity.y
            || x->heading != y->heading || x->height != y->height || x->pitch != y->pitch
            || x->roll != y->roll || x->engine_angle != y->engine_angle || x->wheel != y->wheel
            || x->throttle != y->throttle || x->engine_response != y->engine_response
            || x->camera_heading != y->camera_heading || x->world_time != y->world_time)
            return false;
    }
    return true;
}

static int clock_contracts(void)
{
    const uint64_t ticks[] = { 0, 1, 28800, 72000, 432000 };
    const BoatWorldResources *basin = test_world(BOAT_LOCATION_BASIN);
    BoatSession session;
    boat_session_init(&session, basin, (BoatWater) { 0 });
    for (size_t i = 0; i < sizeof(ticks) / sizeof(ticks[0]); ++i) {
        boat_session_set_tick(&session, ticks[i]);
        CHECK(
            session.tick == ticks[i] && boat_session_time(&session) == boat_tick_seconds(ticks[i]));
    }
    boat_session_set_tick(&session, 432000);
    CHECK(boat_session_step(&session, (BoatInput) { 0 }));
    CHECK(session.tick == 432001 && boat_session_time(&session) == boat_tick_seconds(432001));
    boat_session_set_tick(&session, UINT64_MAX);
    Boat held = session.boat;
    CHECK(!boat_session_step(&session, (BoatInput) { 1, 1 }) && session.tick == UINT64_MAX);
    CHECK(memcmp(&held, &session.boat, sizeof(held)) == 0);
    boat_session_init(&session, basin, (BoatWater) { 0 });
    CHECK(session.tick == 0 && boat_session_time(&session) == 0);
    BoatCourse course = { .enabled = true };
    boat_course_step(&course, (Vector2) { 0, 36 }, (Vector2) { 0, 32 }, BOAT_TICK_SECONDS);
    CHECK(course.next_gate == 1 && course.splits[0] == 0);
    CHECK(course.elapsed == .75 * BOAT_TICK_SECONDS);
    boat_course_step(&course, (Vector2) { 22, 8 }, (Vector2) { 22, 6 }, BOAT_TICK_SECONDS);
    CHECK(course.next_gate == 2 && course.crossing[1] == .5);
    CHECK(course.splits[1] == .75 * BOAT_TICK_SECONDS + .5 * BOAT_TICK_SECONDS);
    course.next_gate = 3;
    course.elapsed = 10 * BOAT_TICK_SECONDS;
    boat_course_step(&course, (Vector2) { 0, -52 }, (Vector2) { 0, -54 }, BOAT_TICK_SECONDS);
    CHECK(course.next_gate == 4 && course.crossing[3] == .5);
    CHECK(course.splits[3] == 10.5 * BOAT_TICK_SECONDS);
    CHECK(course.elapsed == course.splits[3]);
    puts("integer clock, reset and exact fractional course crossings pass");
    return 0;
}

// Prescribed route exercises the real swept gate timer and recorder without input/window timing.
static int flag_playback(void)
{
    BoatReplaySample samples[41] = { 0 };
    for (int i = 0; i < 41; ++i) {
        float t = i * .1f;
        samples[i] = (BoatReplaySample) { .at = t,
            .world_time = t + 7,
            .position = { t, -5 * t },
            .velocity = { 1, -5 },
            .heading = .2f * t,
            .roll = .15f * sinf(t * 2),
            .pitch = .03f * sinf(t) };
    }
    BoatReplay run = { .samples = samples, .count = 41 };
    BoatReplayFlag fine = { 0 }, coarse = { 0 };
    BoatViewState a, b;
    BoatFlag a_flag, b_flag;
    for (int tick = 0; tick < 240; ++tick) {
        CHECK(boat_replay_sample(&run, tick * BOAT_TICK_SECONDS, &a, NULL));
        boat_replay_animate_flag(&run, &a, &fine, &a_flag);
        if (tick % 2 == 0) {
            CHECK(boat_replay_sample(&run, tick * BOAT_TICK_SECONDS, &b, NULL));
            boat_replay_animate_flag(&run, &b, &coarse, &b_flag);
            CHECK(memcmp(&fine.flag, &coarse.flag, sizeof(BoatFlag)) == 0);
        }
    }
    BoatReplayFlag seek = { 0 }, fresh = { 0 };
    CHECK(boat_replay_sample(&run, 3.175, &a, NULL));
    boat_replay_animate_flag(&run, &a, &seek, &a_flag);
    BoatFlagSurface paused = a_flag.surface;
    boat_replay_animate_flag(&run, &a, &seek, &a_flag);
    CHECK(memcmp(&paused, &a_flag.surface, sizeof(paused)) == 0);
    CHECK(boat_replay_sample(&run, .2, &a, NULL));
    boat_replay_animate_flag(&run, &a, &seek, &a_flag);
    CHECK(boat_replay_sample(&run, 3.175, &a, NULL));
    CHECK(boat_replay_sample(&run, 3.175, &b, NULL));
    boat_replay_animate_flag(&run, &a, &seek, &a_flag);
    boat_replay_animate_flag(&run, &b, &fresh, &b_flag);
    CHECK(memcmp(&a_flag, &b_flag, sizeof(BoatFlag)) == 0);
    WaterfrontPlayerState player = { 0 };
    boat_player_pose(&a, &a_flag, &player);
    for (int row = 0; row < BOAT_FLAG_ROWS; ++row)
        CHECK(Vector3Distance(
                  player.flag.points[row * BOAT_FLAG_COLS], boat_flag_attachment(player.hull, row))
            < .00001f);
    return 0;
}

static int route(BoatReplay *recording, BoatReplayRuns *runs, int ticks, bool waves)
{
    const Vector2 points[] = { { 0, 37 }, { 22, 7 }, { -22, -23 }, { 0, -55 } };
    BoatSession session;
    boat_session_init(
        &session, test_world(BOAT_LOCATION_BASIN), waves ? boat_water_gentle() : (BoatWater) { 0 });
    Boat *boat = &session.boat;
    boat_session_start_course(&session);
    boat->position = points[0];
    for (int leg = 0; leg < 3; ++leg) {
        for (int tick = 1; tick <= ticks; ++tick) {
            BoatReplaySample before
                = boat_replay_pose(boat, &session.presentation, boat_session_time(&session));
            float t = (float)tick / ticks;
            boat->position = (Vector2) { points[leg].x + (points[leg + 1].x - points[leg].x) * t,
                points[leg].y + (points[leg + 1].y - points[leg].y) * t };
            boat->velocity = (Vector2) { (boat->position.x - before.position.x) / BOAT_STEP,
                (boat->position.y - before.position.y) / BOAT_STEP };
            boat->heading = atan2f(boat->velocity.x, -boat->velocity.y);
            session.presentation.camera_heading = boat->heading;
            boat_session_set_tick(&session, session.tick + 1);
            boat_course_step(&session.course, before.position, boat->position, BOAT_TICK_SECONDS);
            BoatReplaySample after
                = boat_replay_pose(boat, &session.presentation, boat_session_time(&session));
            if (recording)
                boat_replay_record(recording, before, after, &session.course, waves);
            if (runs)
                boat_replay_runs_step(runs, before, after, &session.course, waves);
            if (session.course.next_gate == BOAT_COURSE_GATES)
                return 0;
        }
    }
    CHECK(false);
}

static int malformed(BoatReplay *destination, const char *path, const char *contents)
{
    FILE *file = fopen(path, "w");
    CHECK(file);
    CHECK(fputs(contents, file) >= 0);
    CHECK(fclose(file) == 0);
    BoatReplaySample *samples = destination->samples;
    size_t count = destination->count;
    CHECK(!boat_replay_load(destination, path));
    CHECK(destination->samples == samples && destination->count == count);
    return 0;
}

static int ghost_rendering(void)
{
    enum { WIDTH = 320, HEIGHT = 200 };
    Color *plain = malloc(WIDTH * HEIGHT * sizeof(*plain));
    Color *pixels = malloc(WIDTH * HEIGHT * sizeof(*pixels));
    CHECK(plain && pixels);
    Renderer3D renderer = { 0 };
    BoatCpuView cpu_view = { 0 };
    BoatViewState boat = { .course = { .enabled = true } }, ghost;
    static const BoatPresentation view;
    BoatWorld world;
    boat_world_init(&world, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat_reset(&boat.boat);
    boat.boat.position = BOAT_COURSE_START;
    ghost = boat;
    CHECK(boat_draw(
        &renderer, &cpu_view, plain, WIDTH, HEIGHT, &boat, &view, &world, BOAT_CAMERA_DEFAULT));
    CHECK(boat_draw_with_ghost(&renderer, &cpu_view, pixels, WIDTH, HEIGHT, &boat, &view, &world,
        BOAT_CAMERA_DEFAULT, &ghost, NULL, .5f));
    // Coincident ghost surfaces cannot overwrite the player's hull.
    CHECK(memcmp(plain, pixels, WIDTH * HEIGHT * sizeof(*plain)) == 0);
    ghost.boat.position.y -= 10;
    CHECK(boat_draw_with_ghost(&renderer, &cpu_view, pixels, WIDTH, HEIGHT, &boat, &view, &world,
        BOAT_CAMERA_DEFAULT, &ghost, NULL, .5f));
    int cyan = 0;
    for (int i = 0; i < WIDTH * HEIGHT; ++i)
        cyan += pixels[i].r == 90 && pixels[i].g == 240 && pixels[i].b == 255;
    CHECK(cyan > 10);
    CHECK(boat_draw_with_ghost(&renderer, &cpu_view, pixels, WIDTH, HEIGHT, &boat, &view, &world,
        BOAT_CAMERA_DEFAULT, &ghost, NULL, 0));
    CHECK(memcmp(plain, pixels, WIDTH * HEIGHT * sizeof(*plain)) == 0);
    CHECK(renderer.ghost_opacity == 0);
    boat_cpu_view_destroy(&cpu_view);
    renderer3d_destroy(&renderer);
    free(plain);
    free(pixels);
    return 0;
}

static int playback_controls(const BoatReplay *run)
{
    BoatReplayRuns runs = { 0 };
    boat_replay_runs_reset(&runs);
    CHECK(boat_replay_speed(&runs) == 1);
    boat_replay_seek(&runs, 5);
    boat_replay_advance(&runs, .1, true);
    boat_replay_speed_step(&runs, 1);
    CHECK(runs.replay_time == 0 && boat_replay_speed(&runs) == 1);
    CHECK(!boat_replay_play(&runs, NULL));
    BoatReplay empty = { 0 };
    CHECK(!boat_replay_play(&runs, &empty));
    CHECK(boat_replay_play(&runs, run));
    CHECK(runs.playing == run && runs.replay_time == 0 && !runs.paused);
    double finish = run->splits[BOAT_COURSE_GATES - 1];
    unsigned char first[sizeof(BoatReplaySample)], last[sizeof(BoatReplaySample)];
    memcpy(first, &run->samples[0], sizeof(first));
    memcpy(last, &run->samples[run->count - 1], sizeof(last));
    // Every speed advances in course seconds, with no dependence on render frame count.
    const float speeds[] = { .25f, .5f, 1, 2, 4 };
    boat_replay_speed_step(&runs, -1);
    boat_replay_speed_step(&runs, -1);
    boat_replay_speed_step(&runs, -1);
    for (int i = 0; i < 5; ++i) {
        CHECK(boat_replay_speed(&runs) == speeds[i]);
        boat_replay_seek(&runs, 0);
        for (int frame = 0; frame < 60; ++frame)
            boat_replay_advance(&runs, 1.0 / 60, true);
        CHECK(fabs(runs.replay_time - speeds[i]) < .000001);
        double sixty = runs.replay_time;
        boat_replay_seek(&runs, 0);
        for (int frame = 0; frame < 120; ++frame)
            boat_replay_advance(&runs, 1.0 / 120, true);
        CHECK(fabs(runs.replay_time - sixty) < .000001);
        boat_replay_seek(&runs, 0);
        for (int frame = 0; frame < 30; ++frame)
            boat_replay_advance(&runs, 1.0 / 30, true);
        CHECK(fabs(runs.replay_time - sixty) < .000001);
        boat_replay_speed_step(&runs, 1);
    }
    CHECK(boat_replay_speed(&runs) == 4); // Clamp at both ends of the speed range.
    double before = runs.replay_time;
    boat_replay_advance(&runs, .1, false);
    CHECK(runs.replay_time == before && !runs.paused);
    runs.paused = true;
    boat_replay_advance(&runs, .1, true);
    CHECK(runs.replay_time == before);
    // Seeking works while paused and updates pose, heading, world time and gate progress.
    double gate = run->splits[1];
    boat_replay_seek(&runs, gate + .01);
    BoatViewState view;
    CHECK(runs.paused && boat_replay_sample(run, runs.replay_time, &view, NULL));
    CHECK(view.course.next_gate == 2);
    boat_replay_seek(&runs, gate - .01);
    CHECK(boat_replay_sample(run, runs.replay_time, &view, NULL) && view.course.next_gate == 1);
    CHECK(view.time > 0 && view.course.elapsed == runs.replay_time);
    boat_replay_seek(&runs, finish + 10);
    CHECK(runs.replay_time == finish && runs.paused);
    CHECK(boat_replay_sample(run, runs.replay_time, &view, NULL));
    CHECK(view.boat.position.y == -53 && view.course.next_gate == BOAT_COURSE_GATES);
    boat_replay_seek(&runs, -10);
    CHECK(runs.replay_time == 0 && runs.paused);
    boat_replay_seek(&runs, NAN);
    CHECK(runs.replay_time == 0);
    runs.paused = false;
    boat_replay_advance(&runs, INFINITY, true);
    boat_replay_advance(&runs, NAN, true);
    boat_replay_advance(&runs, -1, true);
    CHECK(runs.replay_time == 0);
    boat_replay_advance(&runs, 10, true);
    CHECK(fabs(runs.replay_time - .4) < .000001); // Long frames match the live time cap.
    boat_replay_seek(&runs, finish - .01);
    boat_replay_advance(&runs, .1, true);
    CHECK(runs.replay_time == finish && runs.paused);
    CHECK(boat_replay_play(&runs, run));
    CHECK(runs.replay_time == 0 && !runs.paused && boat_replay_speed(&runs) == 1);
    CHECK(memcmp(&run->samples[0], first, sizeof(first)) == 0);
    CHECK(memcmp(&run->samples[run->count - 1], last, sizeof(last)) == 0);
    boat_replay_runs_reset(&runs);
    CHECK(!runs.playing && runs.replay_time == 0 && boat_replay_speed(&runs) == 1);
    boat_replay_runs_destroy(&runs);
    return 0;
}

// Freely simulated banked S-course: the controller supplies inputs only.
static int banked_course(const char *directory)
{
    BoatSession session;
    boat_session_init(&session, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat_session_start_course(&session);
    // The session also records its own runs; its finish must wait for the flush.
    session.record_runs = true;
    char best[2048];
    CHECK(snprintf(best, sizeof(best), "%s/session-best.run", directory) < (int)sizeof(best));
    remove(best);
    snprintf(session.runs.best_path, sizeof(session.runs.best_path), "%s", best);
    Boat *b = &session.boat;
    BoatReplay live = { 0 }, loaded = { 0 };
    float peak = 0;
    for (int tick = 0; tick < 120 * 120 && !live.complete; ++tick) {
        int gate = session.course.next_gate;
        CHECK(gate < BOAT_COURSE_GATES);
        Vector2 target = boat_course_gates[gate].centre;
        target.y -= 2;
        Vector2 delta = Vector2Subtract(target, b->position);
        float error = remainderf(atan2f(delta.x, -delta.y) - b->heading, 2 * PI);
        // H3's firmer hull needs less anticipatory yaw braking and full steering
        // authority through the offset gates. This is a test driver, not live input.
        float wheel = Clamp(3.0f * error - .7f * b->yaw_velocity, -1, 1);
        float throttle = fabsf(error) > .4f ? .25f : .45f;
        BoatReplaySample before
            = boat_replay_pose(b, &session.presentation, boat_session_time(&session));
        boat_session_step(&session,
            (BoatInput) { Clamp((wheel - b->wheel) / BOAT_STEP, -1, 1),
                Clamp(
                    (throttle - b->throttle) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP), -1, 1) });
        boat_replay_record(&live, before,
            boat_replay_pose(b, &session.presentation, boat_session_time(&session)),
            &session.course, false);
        if (live.count && !live.complete) {
            BoatViewState sampled;
            CHECK(boat_replay_sample(&live, session.course.elapsed, &sampled, NULL));
            CHECK(fabsf(sampled.boat.roll - b->roll) < .000002f
                && fabsf(sampled.boat.pitch - b->pitch) < .000002f);
            WaterfrontPlayerState actual = { 0 }, recorded = { 0 };
            boat_player_pose(SESSION_VIEW(&session), &session.presentation.flag, &actual);
            boat_player_pose(&sampled, NULL, &recorded);
            CHECK(Vector3Distance(actual.engine.position, recorded.engine.position) < .00002f);
        }
        peak = fmaxf(peak, fabsf(b->roll));
        CHECK(fabsf(b->position.x) + 3 < BOAT_BASIN_HALF_X
            && fabsf(b->position.y) + 3 < BOAT_BASIN_HALF_Z);
    }
    printf("Banked course complete=%d gates=%d peak=%.6f position=%.3f,%.3f\n", live.complete,
        session.course.next_gate, peak * RAD2DEG, b->position.x, b->position.y);
    CHECK(live.complete && peak > 5 * DEG2RAD);
    // Session-owned recording is the manual recording, sample for sample.
    const BoatReplay *own = &session.runs.last;
    CHECK(session.runs.finished && own->complete && own->count == live.count);
    CHECK(same_samples(own->samples, live.samples, live.count));
    CHECK(memcmp(own->splits, live.splits, sizeof(live.splits)) == 0);
    CHECK(session.runs.save_best && !FileExists(best));
    for (int tick = 0; tick < 120; ++tick) // Finished: further ticks neither record nor save.
        CHECK(boat_session_step(&session, (BoatInput) { 0 }));
    CHECK(own->count == live.count && !FileExists(best));
    boat_session_flush(&session);
    CHECK(!session.runs.save_best && boat_replay_load(&loaded, best));
    CHECK(loaded.count == live.count && loaded.splits[3] == live.splits[3]);
    // Restart begins a new attempt at the course start against the saved best; runs survive.
    boat_session_restart(&session, (BoatWater) { 0 });
    CHECK(session.tick == 0 && session.course.enabled && session.course.next_gate == 0);
    CHECK(Vector2Equals(session.boat.position, BOAT_COURSE_START) && session.record_runs);
    CHECK(session.runs.last.complete && session.runs.best.complete && !session.runs.finished);
    CHECK(session.runs.opponent.complete && session.runs.opponent.count == live.count);
    CHECK(!session.runs.recording.count);
    boat_session_destroy(&session);
    remove(best);
    char path[2048];
    CHECK(snprintf(path, sizeof(path), "%s/banked.run", directory) < (int)sizeof(path));
    CHECK(boat_replay_save(&live, path) && boat_replay_load(&loaded, path));
    CHECK(loaded.count == live.count);
    BoatReplayRuns transport = { 0 };
    CHECK(boat_replay_play(&transport, &loaded));
    transport.paused = true;
    // Exact samples and midpoints, then backward seeking through the same banked poses.
    for (int direction = 0; direction < 2; ++direction)
        for (size_t j = 0; j + 1 < live.count; ++j) {
            size_t i = direction ? live.count - 2 - j : j;
            for (int half = 0; half <= 1; ++half) {
                double at = live.samples[i].at;
                if (half)
                    at = (at + live.samples[i + 1].at) * .5;
                BoatViewState original, replay, ghost;
                CHECK(boat_replay_sample(&live, at, &original, NULL));
                boat_replay_seek(&transport, at);
                boat_replay_advance(&transport, .1, true);
                CHECK(transport.paused && transport.replay_time == at);
                CHECK(boat_replay_sample(transport.playing, transport.replay_time, &replay, NULL));
                CHECK(boat_replay_sample(&loaded, at, &ghost, NULL));
                float expected = half ? (live.samples[i].roll + live.samples[i + 1].roll) * .5f
                                      : live.samples[i].roll;
                CHECK(fabsf(replay.boat.roll - expected) < .000002f);
                CHECK(fabsf(replay.boat.pitch - original.boat.pitch) < .000002f);
                CHECK(fabsf(replay.boat.roll - original.boat.roll) < .000002f);
                CHECK(replay.boat.turn_roll_load == 0 && replay.boat.roll_velocity == 0);
                WaterfrontPlayerState a = { 0 }, c = { 0 }, g = { 0 };
                boat_player_pose(&original, NULL, &a);
                boat_player_pose(&replay, NULL, &c);
                boat_player_pose(&ghost, NULL, &g);
                CHECK(Vector3Distance(a.engine.position, c.engine.position) < .00002f);
                CHECK(Vector3Distance(c.cabin.position, g.cabin.position) < .000001f);
                CHECK(Vector3Distance(c.hull.rotation, g.hull.rotation) < .000001f);
            }
        }
    // Physical stress angles fit the existing file bounds; arbitrary and nonfinite angles fail.
    loaded.samples[1].roll = 29.7f * DEG2RAD;
    CHECK(boat_replay_save(&loaded, path));
    loaded.samples[1].roll = PI;
    CHECK(!boat_replay_save(&loaded, path));
    loaded.samples[1].roll = NAN;
    CHECK(!boat_replay_save(&loaded, path));
    CHECK(boat_replay_save(&live, path));
    boat_replay_runs_destroy(&transport);
    boat_replay_destroy(&live);
    boat_replay_destroy(&loaded);
    return 0;
}

int main(int argc, char **argv)
{
    test_worlds_open();
    CHECK(clock_contracts() == 0);
    CHECK(flag_playback() == 0);
    CHECK(argc == 2);
    CHECK(ghost_rendering() == 0);
    CHECK(MakeDirectory(argv[1]) == 0);
    char fixture[2048], best_path[2048], invalid[2048], wave_path[2048], saved_best[2048];
    CHECK(snprintf(fixture, sizeof(fixture), "%s/replay-fixture.run", argv[1]) > 0);
    CHECK(snprintf(best_path, sizeof(best_path), "%s/best.run", argv[1]) > 0);
    CHECK(snprintf(invalid, sizeof(invalid), "%s/invalid.run", argv[1]) > 0);
    CHECK(snprintf(wave_path, sizeof(wave_path), "%s/waves.run", argv[1]) > 0);
    CHECK(snprintf(saved_best, sizeof(saved_best), "%s/course-calm-physics%d.run", argv[1],
              BOAT_PHYSICS_REVISION)
        > 0);
    CHECK(banked_course(argv[1]) == 0);
    BoatReplay run = { 0 }, loaded = { 0 }, waves = { 0 };
    CHECK(route(&run, NULL, 480, false) == 0);
    CHECK(run.complete && !run.failed && run.count > 1000);
    CHECK(playback_controls(&run) == 0);
    CHECK(run.samples[0].at == 0);
    CHECK(fabsf(run.samples[0].position.y - 35) < .00001f);
    CHECK(fabsf(run.samples[run.count - 1].position.y + 53) < .00001f);
    CHECK(run.splits[0] == 0 && run.splits[3] == run.samples[run.count - 1].at);
    for (int i = 1; i < BOAT_COURSE_GATES; ++i)
        CHECK(run.splits[i] > run.splits[i - 1]);
    CHECK(boat_replay_save(&run, fixture));
    CHECK(boat_replay_save(&run, saved_best));
    CHECK(boat_replay_load(&loaded, fixture));
    CHECK(loaded.complete && !loaded.waves && loaded.count == run.count);
    CHECK(memcmp(loaded.splits, run.splits, sizeof(run.splits)) == 0);
    for (size_t i = 0; i < run.count; ++i) {
        CHECK(loaded.samples[i].at == run.samples[i].at);
        CHECK(loaded.samples[i].position.x == run.samples[i].position.x);
        CHECK(loaded.samples[i].position.y == run.samples[i].position.y);
        CHECK(loaded.samples[i].heading == run.samples[i].heading);
        CHECK(loaded.samples[i].world_time == run.samples[i].world_time);
    }
    // A version-2 recording retains world times finer than float precision at one hour.
    BoatReplay precise = { 0 };
    CHECK(boat_replay_copy(&precise, &run));
    for (size_t i = 0; i < precise.count; ++i)
        precise.samples[i].world_time = 3600.123456789 + (double)i * BOAT_TICK_SECONDS;
    CHECK(boat_replay_save(&precise, invalid));
    CHECK(boat_replay_load(&loaded, invalid));
    for (size_t i = 0; i < precise.count; ++i)
        CHECK(loaded.samples[i].world_time == precise.samples[i].world_time);
    BoatViewState view;
    CHECK(boat_replay_sample(&loaded, loaded.samples[0].at, &view, NULL));
    CHECK(view.time == precise.samples[0].world_time);
    double halfway = (loaded.samples[0].at + loaded.samples[1].at) * .5;
    CHECK(boat_replay_sample(&loaded, halfway, &view, NULL));
    CHECK(view.time > precise.samples[0].world_time && view.time < precise.samples[1].world_time);
    boat_replay_destroy(&precise);
    CHECK(boat_replay_load(&loaded, fixture));
    BoatViewState playback;
    CHECK(boat_replay_sample(&loaded, -5, &playback, NULL));
    CHECK(playback.boat.position.y == 35 && playback.course.next_gate == 1);
    CHECK(boat_replay_sample(&loaded, 1000, &playback, NULL));
    CHECK(playback.boat.position.y == -53 && playback.course.next_gate == BOAT_COURSE_GATES);
    CHECK(!boat_replay_sample(&loaded, NAN, &playback, NULL));
    size_t mid = run.count / 2;
    double at = (run.samples[mid].at + run.samples[mid + 1].at) / 2;
    CHECK(boat_replay_sample(&run, at, &playback, NULL));
    CHECK(fabsf(playback.boat.position.x
              - (run.samples[mid].position.x + run.samples[mid + 1].position.x) / 2)
        < .00001f);
    CHECK(playback.course.next_gate == 2);
    // Angular interpolation must cross pi rather than spin through zero.
    loaded.samples[mid].heading = 3.13f;
    loaded.samples[mid + 1].heading = -3.13f;
    loaded.samples[mid].camera_heading = 3.13f;
    loaded.samples[mid + 1].camera_heading = -3.13f;
    BoatPresentation playback_view;
    CHECK(boat_replay_sample(&loaded, at, &playback, &playback_view));
    CHECK(fabsf(fabsf(playback.boat.heading) - PI) < .0001f);
    CHECK(fabsf(fabsf(playback_view.camera_heading) - PI) < .0001f);
    // Reject truncated, unsupported, oversized and non-finite recordings without losing a best.
    CHECK(malformed(&loaded, invalid, "") == 0);
    CHECK(malformed(&loaded, invalid, "PROTEUS_RUN 999 1 2 0.0083333337679505348 0 2\n") == 0);
    CHECK(malformed(&loaded, invalid, "PROTEUS_RUN 1 1 2 0.0083333337679505348 0 99999999\n") == 0);
    CHECK(malformed(&loaded, invalid, "PROTEUS_RUN 1 1 2 0.0083333337679505348 0 2\n0 1 2 nan\n")
        == 0);
    // A version-1 file with otherwise current metadata is rejected without replacing a run.
    CHECK(boat_replay_save(&run, invalid));
    FILE *old_version_file = fopen(invalid, "r+");
    CHECK(old_version_file && fseek(old_version_file, 12, SEEK_SET) == 0
        && fputc('1', old_version_file) != EOF && fclose(old_version_file) == 0);
    BoatReplaySample *kept = loaded.samples;
    CHECK(!boat_replay_load(&loaded, invalid) && loaded.samples == kept);
    old_version_file = fopen(invalid, "r");
    int legacy_version;
    CHECK(old_version_file && fscanf(old_version_file, "PROTEUS_RUN %d", &legacy_version) == 1);
    CHECK(legacy_version == 1 && fclose(old_version_file) == 0);
    // An otherwise valid previous-handling run cannot become an opponent or personal best.
    CHECK(boat_replay_save(&run, invalid));
    FILE *old = fopen(invalid, "r+");
    CHECK(old && fseek(old, 16, SEEK_SET) == 0 && fputc('0' + BOAT_PHYSICS_REVISION - 1, old) != EOF
        && fclose(old) == 0);
    BoatReplaySample *retained_samples = loaded.samples;
    CHECK(!boat_replay_load(&loaded, invalid) && loaded.samples == retained_samples);
    old = fopen(invalid, "r");
    int old_version, old_course, old_physics;
    CHECK(old && fscanf(old, "PROTEUS_RUN %d %d %d", &old_version, &old_course, &old_physics) == 3);
    CHECK(old_physics == BOAT_PHYSICS_REVISION - 1
        && fclose(old) == 0); // Rejection leaves the old file intact.

    CHECK(boat_replay_save(&run, invalid));
    FILE *trailer = fopen(invalid, "a");
    CHECK(trailer && fputs("garbage", trailer) >= 0 && fclose(trailer) == 0);
    CHECK(!boat_replay_load(&loaded, invalid));
    CHECK(boat_replay_copy(&loaded, &run));
    loaded.samples[mid].at = loaded.samples[mid - 1].at;
    CHECK(!boat_replay_save(&loaded, fixture)); // Existing valid file is untouched.
    CHECK(boat_replay_load(&loaded, fixture));
    CHECK(loaded.samples[mid].at == run.samples[mid].at);
    CHECK(route(&waves, NULL, 480, true) == 0);
    CHECK(boat_replay_save(&waves, wave_path));
    CHECK(boat_replay_load(&loaded, wave_path));
    CHECK(loaded.waves && boat_replay_sample(&loaded, 1, &playback, NULL));
    CHECK(boat_replay_water(&loaded).amplitude == boat_water_gentle().amplitude);
    // An exact end-of-tick start yields one time-zero sample, not duplicate timestamps.
    Boat boat;
    BoatCourse course = { .enabled = true };
    static const BoatPresentation still;
    boat_reset(&boat);
    boat.position = (Vector2) { 0, 36 };
    BoatReplaySample before = boat_replay_pose(&boat, &still, 0);
    boat.position.y = 35;
    boat_course_step(&course, before.position, boat.position, BOAT_TICK_SECONDS);
    BoatReplay boundary = { 0 };
    boat_replay_record(&boundary, before, boat_replay_pose(&boat, &still, 0), &course, false);
    CHECK(boundary.count == 1 && boundary.samples[0].at == 0);
    before = boat_replay_pose(&boat, &still, 0);
    boat.position.y = 34;
    boat_course_step(&course, before.position, boat.position, BOAT_TICK_SECONDS);
    boat_replay_record(&boundary, before, boat_replay_pose(&boat, &still, 0), &course, false);
    CHECK(boundary.count == 2 && boundary.samples[1].at == BOAT_TICK_SECONDS);
    // A stalled/overlong attempt stops recording and leaves existing completed runs alone.
    course.elapsed = BOAT_REPLAY_MAX_SECONDS + 1;
    boat_replay_record(&boundary, before, boat_replay_pose(&boat, &still, 0), &course, false);
    CHECK(boundary.failed && !boundary.complete);
    boat_replay_destroy(&boundary);
    // Session lifecycle: completion, pinned ghost, aborted retry, slower/tied/faster attempts.
    // A finish settles in memory only; its best is written by the flush after the tick loop.
    BoatReplayRuns session = { .ghost_visible = true };
    snprintf(session.best_path, sizeof(session.best_path), "%s", best_path);
    remove(best_path);
    CHECK(route(NULL, &session, 480, false) == 0);
    CHECK(session.last.complete && session.best.complete && !session.opponent.complete);
    CHECK(session.save_best && session.report_finish && !FileExists(best_path));
    boat_replay_runs_flush(&session);
    CHECK(!session.save_best && !session.report_finish && FileExists(best_path));
    double first = session.best.splits[3];
    BoatReplaySample *first_samples = session.last.samples;
    CHECK(route(NULL, &session, 300, false) == 0);
    CHECK(session.last.samples == first_samples && session.best.splits[3] == first);
    boat_replay_runs_reset(&session);
    CHECK(session.opponent.complete && session.opponent.splits[3] == first);
    CHECK(route(NULL, &session, 600, false) == 0);
    CHECK(session.last.splits[3] > first && session.best.splits[3] == first);
    CHECK(session.opponent.splits[3] == first);
    boat_replay_runs_reset(&session);
    CHECK(route(NULL, &session, 480, false) == 0);
    CHECK(session.best.splits[3] == first);
    BoatReplaySample *old_best = session.best.samples;
    boat_replay_runs_reset(&session);
    CHECK(route(NULL, &session, 480, false) == 0);
    CHECK(session.best.samples == old_best); // Ties do not replace the best.
    boat_replay_runs_reset(&session);
    CHECK(route(NULL, &session, 360, false) == 0);
    CHECK(session.best.splits[3] < first && session.opponent.splits[3] == first);
    CHECK(boat_replay_load(&loaded, best_path) && loaded.splits[3] == first);
    // A reset flushes the pending best before starting the next attempt.
    boat_replay_runs_reset(&session);
    CHECK(!session.save_best && boat_replay_load(&loaded, best_path));
    CHECK(loaded.splits[3] == session.best.splits[3]);
    CHECK(session.opponent.splits[3] == session.best.splits[3]);
    CHECK(!session.recording.count && session.last.complete);
    course = (BoatCourse) { .enabled = true };
    boat.position = (Vector2) { 0, 36 };
    before = boat_replay_pose(&boat, &still, 0);
    boat.position.y = 34;
    boat_course_step(&course, before.position, boat.position, BOAT_TICK_SECONDS);
    boat_replay_runs_step(&session, before, boat_replay_pose(&boat, &still, 0), &course, false);
    CHECK(session.recording.count > 0);
    double retained = session.best.splits[3];
    boat_replay_runs_reset(&session);
    CHECK(!session.recording.count && session.best.splits[3] == retained);
    CHECK(session.last.complete);
    // Save failure keeps a playable best in memory.
    snprintf(session.best_path, sizeof(session.best_path), "%s/missing/best.run", fixture);
    CHECK(route(NULL, &session, 300, false) == 0);
    CHECK(session.best.complete && session.last.complete);
    CHECK(strcmp(session.notice, "New personal best!") == 0);
    boat_replay_runs_flush(&session);
    CHECK(strstr(session.notice, "memory"));
    // Export follows every finish, also through the flush.
    char exported[2048];
    CHECK(snprintf(exported, sizeof(exported), "%s/exported.run", argv[1]) > 0);
    remove(exported);
    session.export_path = exported;
    boat_replay_runs_reset(&session);
    CHECK(route(NULL, &session, 700, false) == 0);
    CHECK(session.export_last && !FileExists(exported));
    boat_replay_runs_flush(&session);
    CHECK(!session.export_last && boat_replay_load(&loaded, exported));
    CHECK(loaded.splits[3] == session.last.splits[3]);
    remove(exported);
    boat_replay_runs_destroy(&session);
    boat_replay_destroy(&run);
    boat_replay_destroy(&loaded);
    boat_replay_destroy(&waves);
    remove(best_path);
    remove(invalid);
    puts("Boat recording, playback and personal-best checks passed");
    test_worlds_close();
    return 0;
}
