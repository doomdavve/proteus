/* AR6 (ledger L0) gates for the private local session (src/boat_local.[ch]): the authority and
 * client in one process over the memory transport, driven by host grants. Every scenario runs the
 * game's direct free-boating path (BoatSession stepped by timed_boat_step with the arcade wake
 * pool) beside a local session fed the same frames, and requires bit-identical boats and wake
 * pools at every predicted tick and at every frame's authority tick. The L6 cases drive both with
 * the scripted drive instead (demo_step against the glue's scripted frames and capture drive). The
 * L2 cases run the time trial: a course autopilot through the direct session's own course and
 * recorder beside the local authority's course adapter, with equal course state, runs, bests,
 * notices and save requests after every frame.
 * Frame time is a scripted value, never wall time. `--bench` reports per-frame cost (not a test).
 */
#include "boat_fixtures.h"
#include "boat_helm.h"
#include "boat_local.h"
#include "boat_test_cases.h"
#include "boat_test_world.h"
#include "boat_time_control.h"
#include "wake_velocity_local.h"

#include "raymath.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

// One frame of a scripted drive: real frame seconds, the time-scale index and the helm.
typedef struct {
    double seconds;
    int speed;
    bool paused; // Menu, help or focus loss: the frame grants nothing.
    float steering_rate, thrust_target, steering_target;
} Frame;

typedef Frame (*Script)(unsigned frame);

typedef struct {
    BoatLocationId location;
    bool waves, current, crossing;
    const BoatHandling *handling;
} World;

// A free-boating session as the game holds it: the direct path steps it with timed_boat_step;
// the local path is the game's own glue (tools/wake_velocity_local.c) filling it from the client.
typedef struct {
    BoatSession session;
    ObjectStudy objects;
    double accumulator;
} Play;

static Play direct, play;
static WakeLocal game;
static BoatLocal *const lp = &game.local;
static World world;

// Per-tick boats, presentations and pool digests of the direct path, compared with the local
// path's session (filled from the client's prediction) at every predicted tick.
enum { TRACE = 128 };
typedef struct {
    uint64_t tick;
    Boat boat;
    BoatPresentation presentation;
    uint64_t wakes;
} Expected;
static Expected expected[TRACE];
// L6: both paths take the scripted drive (demo_step; the glue's scripted frames) instead of the
// frames' helm.
static bool scripted;
// L2: both paths take the course autopilot, each tracking the gates from its own boat.
typedef struct {
    BoatCourse course;
    Vector2 previous;
} Pilot;
static Pilot pilots[2];
static bool autopilot;
static unsigned expected_count, observed, mismatches;
static unsigned chunk_sizes[16], chunk_starts_mid, odd_chunks;

// The client's location table (indexed by BoatLocationId), with WORLD's location prepared.
static const BoatWorldResources *worlds(void)
{
    test_world(world.location);
    return test_worlds;
}

static BoatWater world_water(void)
{
    const BoatWorldResources *r = test_world(world.location);
    return boat_world_test_water(r->definition, world.waves, world.current, world.crossing);
}

static void play_open(Play *p)
{
    boat_session_init(&p->session, test_world(world.location), world_water());
    if (world.handling)
        p->session.handling = world.handling;
    object_study_reset(
        &p->objects, &p->session, false, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT);
    p->accumulator = 0;
}

// The game's reset branch: restart the session and its pool.
static void play_restart(Play *p)
{
    boat_session_restart(&p->session, world_water());
    object_study_reset(
        &p->objects, &p->session, false, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT);
    p->accumulator = 0;
}

static float particle_values(const BoatWaterParticle *q, unsigned i)
{
    const float v[] = { q->position.x, q->position.y, q->velocity.x, q->velocity.y, q->height,
        q->lift, q->age, q->lifetime, q->size, q->variation, (float)q->spray, (float)q->wash };
    return v[i];
}

// Field by field: the particle structs have padding.
static bool same_presentation(const BoatPresentation *a, const BoatPresentation *b)
{
    if (memcmp(&a->flag.surface, &b->flag.surface, sizeof(a->flag.surface))
        || memcmp(a->flag.previous, b->flag.previous, sizeof(a->flag.previous))
        || a->flag.flutter_phase != b->flag.flutter_phase
        || a->flag.initialized != b->flag.initialized || a->camera_heading != b->camera_heading
        || a->next_particle != b->next_particle || a->emission != b->emission
        || a->wake_emission != b->wake_emission || a->wake_distance != b->wake_distance
        || a->spray_time != b->spray_time || a->propeller_time != b->propeller_time
        || a->landing_strength != b->landing_strength || a->landing_age != b->landing_age
        || a->landing_serial != b->landing_serial || a->seen_landings != b->seen_landings
        || a->fall_speed != b->fall_speed)
        return false;
    for (unsigned i = 0; i < BOAT_WATER_PARTICLES; ++i)
        for (unsigned k = 0; k < 12; ++k)
            if (particle_values(&a->water[i], k) != particle_values(&b->water[i], k))
                return false;
    return true;
}

static void pilot_reset(Pilot *p, const Boat *boat)
{
    *p = (Pilot) { .course = { .enabled = true, .definition = &boat_fixture_course_default },
        .previous = boat->position };
}

// The banked-course controller (tools/validate_boat_replay.c) at a time trial's pace: the next
// gate from the boat's own crossings, full throttle on the straights.
static BoatInput pilot_input(Pilot *p, const Boat *b)
{
    boat_course_step(&p->course, p->previous, b->position, BOAT_TICK_SECONDS);
    p->previous = b->position;
    int gate = p->course.next_gate < BOAT_COURSE_GATES ? p->course.next_gate : 0;
    Vector2 target = boat_fixture_course_gates[gate].centre;
    target.y -= 2;
    Vector2 delta = Vector2Subtract(target, b->position);
    float error = remainderf(atan2f(delta.x, -delta.y) - b->heading, 2 * PI);
    float wheel = Clamp(3.0f * error - .7f * b->yaw_velocity, -1, 1);
    float throttle = fabsf(error) > .4f ? .25f : 1;
    return (BoatInput) { .steering_rate = Clamp((wheel - b->wheel) / BOAT_STEP, -1, 1),
        .throttle_rate
        = Clamp((throttle - b->throttle) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP), -1, 1) };
}

static BoatInput pilot_script(void *context, const Boat *predicted, uint64_t tick)
{
    (void)tick;
    return pilot_input(context, predicted);
}

static const BoatLocalScript PILOT = { &pilots[1], pilot_script };

// The game's direct free-boating branch for one frame.
static void direct_frame(Frame f)
{
    if (f.paused) {
        direct.accumulator = 0;
        return;
    }
    direct.accumulator += boat_time_frame_seconds(f.seconds, f.speed);
    unsigned ticks = boat_time_grant(&direct.accumulator, UINT_MAX);
    for (unsigned i = 0; i < ticks; ++i) {
        BoatSession *s = &direct.session;
        if (scripted)
            demo_step(s, (int)s->tick, NULL, NULL, &direct.objects);
        else if (autopilot)
            timed_boat_step(s, pilot_input(&pilots[0], &s->boat), NULL, NULL, &direct.objects);
        else
            timed_boat_step(s,
                boat_helm_input(&s->boat, (BoatInput) { .steering_rate = f.steering_rate },
                    f.thrust_target, f.steering_target),
                NULL, NULL, &direct.objects);
        if (expected_count < TRACE)
            expected[expected_count++] = (Expected) { s->tick, s->boat, s->presentation,
                boat_net_wake_digest(object_study_wakes(&direct.objects)) };
    }
}

static void traced(void *context, const BoatSession *session, const BoatFleet *view)
{
    (void)context;
    for (unsigned i = 0; i < expected_count; ++i)
        if (expected[i].tick == session->tick) {
            ++observed;
            mismatches += memcmp(&expected[i].boat, &session->boat, sizeof(Boat)) != 0
                || !same_presentation(&expected[i].presentation, &session->presentation)
                || expected[i].wakes != boat_net_wake_digest(&view->state.wakes);
            return;
        }
    ++mismatches;
}

static double now;

static bool same_authority(void)
{
    const BoatFleet *fleet = &lp->server.fleet;
    const BoatWakeObjects *wakes = object_study_wakes(&direct.objects);
    return fleet->state.tick == direct.session.tick && lp->tick == direct.session.tick
        && play.session.tick == direct.session.tick
        && memcmp(&fleet->state.slots[0].boat, &direct.session.boat, sizeof(Boat)) == 0
        && memcmp(&play.session.boat, &direct.session.boat, sizeof(Boat)) == 0
        && same_presentation(&play.session.presentation, &direct.session.presentation)
        && boat_net_wake_digest(&fleet->state.wakes) == boat_net_wake_digest(wakes)
        && boat_net_wake_digest(&play.objects.wakes) == boat_net_wake_digest(wakes)
        && fleet->state.wakes.emitted == wakes->emitted && fleet->state.wakes.live == wakes->live;
}

// One frame of the local path through the game's glue; false on an endpoint failure.
static bool local_frame(Frame f, double at)
{
    if (f.paused)
        play.accumulator = 0;
    else
        play.accumulator += boat_time_frame_seconds(f.seconds, f.speed);
    if (scripted || autopilot)
        return wake_local_scripted_frame(
            &game, &play.session, &play.objects, &play.accumulator, at, NULL);
    return wake_local_frame(&game, &play.session, &play.objects, &play.accumulator,
        (BoatInput) { .steering_rate = f.steering_rate }, f.thrust_target, f.steering_target, at,
        NULL);
}

static bool same_text(const char *a, const char *b) { return a == b || (a && b && !strcmp(a, b)); }

static bool same_run(const BoatReplay *a, const BoatReplay *b)
{
    if (a->count != b->count || a->complete != b->complete || a->failed != b->failed
        || a->waves != b->waves || memcmp(a->splits, b->splits, sizeof(a->splits)))
        return false;
    for (size_t i = 0; i < a->count; ++i) {
        const BoatReplaySample *x = &a->samples[i], *y = &b->samples[i];
        // Field by field: the sample has padding.
        if (x->at != y->at || memcmp(&x->position, &y->position, sizeof(Vector2))
            || memcmp(&x->velocity, &y->velocity, sizeof(Vector2)) || x->heading != y->heading
            || x->height != y->height || x->pitch != y->pitch || x->roll != y->roll
            || x->engine_angle != y->engine_angle || x->wheel != y->wheel
            || x->throttle != y->throttle || x->engine_response != y->engine_response
            || x->camera_heading != y->camera_heading || x->world_time != y->world_time)
            return false;
    }
    return true;
}

// L2: the client's course view and the session's runs equal the direct session's.
static bool same_course(void)
{
    const BoatCourse *a = &direct.session.course, *b = &play.session.course;
    if (a->enabled != b->enabled || a->next_gate != b->next_gate || a->elapsed != b->elapsed
        || memcmp(a->splits, b->splits, sizeof(a->splits))
        || memcmp(a->crossing, b->crossing, sizeof(a->crossing)))
        return false;
    const BoatReplayRuns *r = &direct.session.runs, *q = &play.session.runs;
    return r->finished == q->finished && r->practice == q->practice && r->save_best == q->save_best
        && r->export_last == q->export_last && r->report_finish == q->report_finish
        && same_text(r->notice, q->notice) && same_run(&r->last, &q->last)
        && same_run(&r->best, &q->best) && same_run(&r->opponent, &q->opponent);
}

// Run FRAMES of SCRIPT through both paths; compare at every tick and after every frame.
static int drive(Script script, unsigned frames)
{
    for (unsigned n = 0; n < frames; ++n) {
        Frame f = script(n);
        expected_count = 0;
        // The game's practice latch: altered time before the attempt finished.
        if (autopilot && f.speed != BOAT_TIME_NORMAL && !f.paused)
            for (unsigned i = 0; i < 2; ++i) {
                BoatReplayRuns *runs = i ? &play.session.runs : &direct.session.runs;
                if (!runs->finished)
                    runs->practice = true;
            }
        direct_frame(f);
        unsigned before = observed;
        uint64_t tick = lp->tick;
        unsigned ticks = expected_count;
        for (unsigned left = ticks, at = (unsigned)(tick % 12); left;) {
            unsigned k = boat_local_chunk(tick, left);
            chunk_sizes[k] += 1;
            chunk_starts_mid += at != 0;
            odd_chunks += k % 2;
            tick += k;
            left -= k;
            at = 0;
        }
        uint64_t chunks = lp->stats.chunks;
        now += f.seconds;
        CHECK(local_frame(f, now));
        CHECK(lp->tick == tick);
        CHECK(lp->stats.chunks - chunks <= 9);
        CHECK(observed - before == ticks);
        CHECK(mismatches == 0);
        CHECK(same_authority());
        if (autopilot) {
            CHECK(same_course());
            // The frame's storage service: both flush (an empty best path fails the save).
            boat_session_flush(&direct.session);
            boat_session_flush(&play.session);
            CHECK(same_course());
        }
    }
    return 0;
}

static int open_world(World w)
{
    world = w;
    scripted = autopilot = false;
    game.script = NULL;
    play_open(&direct);
    play_open(&play);
    now = 100;
    observed = mismatches = 0;
    memset(chunk_sizes, 0, sizeof(chunk_sizes));
    chunk_starts_mid = odd_chunks = 0;
    game.traced = traced;
    CHECK(wake_local_open(&game, &play.session, &play.objects, worlds(), now));
    CHECK(lp->stats.startup_rounds <= BOAT_LOCAL_STARTUP_ROUNDS);
    CHECK(same_authority());
    return 0;
}

// Both paths take the game's reset branch.
static int restart_both(void)
{
    play_restart(&direct);
    play_restart(&play);
    CHECK(wake_local_restart(&game, &play.session, &play.objects, now));
    CHECK(same_authority());
    return 0;
}

// The L0 command gates: every granted tick integrated the client's own command.
static int clean_commands(void)
{
    const BoatNetServerStats *s = &lp->server.stats;
    CHECK(s->rate_limited == 0);
    CHECK(s->late_commands == 0);
    CHECK(s->future_commands == 0);
    CHECK(s->stale_life_commands == 0);
    CHECK(s->duplicate_commands == 0);
    CHECK(s->malformed == 0 && s->forbidden == 0);
    CHECK(s->input_states[BOAT_NET_INPUT_RECEIVED] == s->ticks);
    CHECK(s->timeouts == 0);
    CHECK(lp->client.stats.resyncs == 0);
    CHECK(lp->client.stats.decode_errors == 0);
    CHECK(lp->client.prediction.stats.snaps == 0);
    CHECK(lp->client.prediction.stats.max_error == 0);
    return 0;
}

// The helm: keyboard steering that changes every 37 frames, a stretch of mouse steering and a
// thrust target that runs ahead, eases, reverses and returns.
static Frame helm(unsigned n)
{
    Frame f = { .seconds = 1.0 / 60, .speed = BOAT_TIME_NORMAL, .steering_target = NAN };
    f.steering_rate = (float)((int)(n / 37 % 3) - 1);
    if (n % 400 >= 200 && n % 400 < 300) {
        f.steering_rate = 0;
        f.steering_target = .8f * sinf((float)n * .05f);
    }
    f.thrust_target = n % 600 < 300 ? 1 : n % 600 < 450 ? .4f : n % 600 < 520 ? -.5f : .8f;
    return f;
}

static Frame one(unsigned n) { return helm(n); }

static Frame quarter(unsigned n)
{
    Frame f = helm(n);
    f.speed = 0;
    return f;
}

static Frame eight(unsigned n)
{
    Frame f = helm(n * 8);
    f.speed = BOAT_TIME_MAX_INDEX;
    return f;
}

// G = 96: 0.1 s frames and longer stalls clamped to 0.1 s at 8x, after a short first frame so the
// first long grant starts mid-cadence.
static Frame stalls(unsigned n)
{
    Frame f = eight(n);
    f.seconds = n == 0 ? 5.0 / 120 / 8 : n % 3 == 1 ? .25 : .1;
    return f;
}

// Uneven frame times at 1x and 2x: grants of every size, partial first chunks, odd remainders.
static Frame uneven(unsigned n)
{
    Frame f = helm(n * 3);
    uint32_t r = (n + 1) * 2654435761u;
    f.seconds = .001 + (r >> 8) % 1000 * .00007;
    f.speed = n % 5 == 4 ? 3 : BOAT_TIME_NORMAL;
    return f;
}

// Menu, help or focus loss for 30 s of frames, then play again.
static Frame pausing(unsigned n)
{
    Frame f = helm(n);
    f.paused = n >= 60 && n < 60 + 1800;
    return f;
}

static int run(World w, Script script, unsigned frames)
{
    if (open_world(w) || drive(script, frames) || clean_commands())
        return 1;
    return 0;
}

static const World BASIN = { BOAT_LOCATION_BASIN, true, false, false, NULL };
static const World OUTER = { BOAT_LOCATION_OUTER, true, true, true, NULL };

static int basin_1x(void) { return run(BASIN, one, 300); }
static int basin_quarter(void) { return run(BASIN, quarter, 400); }
static int basin_8x(void) { return run(BASIN, eight, 40); }
static int outer_8x(void) { return run(OUTER, eight, 30); }
static int outer_1x(void) { return run(OUTER, one, 200); }

// L6: `--demo` through the local session. The scripted drive's helm changes at ticks 420 and
// 1440; uneven frames at 1x and 8x cross both, every predicted tick equal to demo_step's.
static int scripted_demo(void)
{
    if (open_world(BASIN))
        return 1;
    scripted = true;
    if (drive(uneven, 300) || drive(eight, 160) || clean_commands())
        return 1;
    CHECK(lp->tick > 1500);
    return 0;
}

// L6: a capture's drive (wake_local_scripted_drive, grants of at most 96 on virtual time) ends
// on demo_step's session, boat, presentation and pool, after a restart too.
static int scripted_capture(void)
{
    for (unsigned round = 0; round < 2; ++round) {
        if (round ? restart_both() : open_world(BASIN))
            return 1;
        const uint64_t ticks = 1501;
        for (uint64_t tick = 0; tick < ticks; ++tick)
            demo_step(&direct.session, (int)tick, NULL, NULL, &direct.objects);
        game.traced = NULL;
        CHECK(wake_local_scripted_drive(&game, &play.session, &play.objects, ticks));
        game.traced = traced;
        CHECK(lp->tick == ticks && lp->stats.max_chunks <= 9);
        CHECK(same_authority());
        if (clean_commands())
            return 1;
    }
    CHECK(lp->generation == 2);
    return 0;
}

// A script is required: a scripted frame without one is refused and grants nothing.
static int scripted_needs_script(void)
{
    if (open_world(BASIN))
        return 1;
    CHECK(!boat_local_frame_scripted(lp, now, 12, NULL, NULL));
    BoatLocalScript empty = { NULL, NULL };
    CHECK(!boat_local_frame_scripted(lp, now, 12, &empty, NULL));
    CHECK(lp->tick == 0);
    return 0;
}

static int grant_96(void)
{
    if (run(BASIN, stalls, 8))
        return 1;
    CHECK(chunk_starts_mid > 0);
    CHECK(lp->stats.max_chunks == 9);
    return 0;
}

// A frame never grants more than 96 ticks: boat_local_frame refuses 97, and the game's glue
// grants 96 and carries the rest in its accumulator (the frame clamp keeps real play at 96).
static int grant_cap(void)
{
    if (open_world(BASIN))
        return 1;
    CHECK(!boat_local_frame(lp, now, BOAT_LOCAL_MAX_GRANT + 1, (BoatInput) { 0 }, 0, NAN, NULL));
    CHECK(lp->running && lp->tick == 0);
    play.accumulator = 100.5 * BOAT_TICK_SECONDS;
    CHECK(wake_local_frame(&game, &play.session, &play.objects, &play.accumulator,
        (BoatInput) { 0 }, 0, NAN, now, NULL));
    CHECK(lp->tick == BOAT_LOCAL_MAX_GRANT && play.session.tick == BOAT_LOCAL_MAX_GRANT);
    CHECK(fabs(play.accumulator - 4.5 * BOAT_TICK_SECONDS) < 1e-9);
    return 0;
}

static int odd_chunk_sizes(void)
{
    if (run(BASIN, uneven, 150))
        return 1;
    CHECK(chunk_starts_mid > 0 && odd_chunks > 0);
    unsigned sizes = 0;
    for (unsigned k = 1; k <= 12; ++k)
        sizes += chunk_sizes[k] != 0;
    CHECK(sizes >= 6);
    return 0;
}

static int explorer_handling(void)
{
    World w = BASIN;
    w.handling = &boat_handling_explorer;
    return run(w, one, 200);
}

static int pause_and_focus(void)
{
    if (run(BASIN, pausing, 2000))
        return 1;
    // Thirty paused seconds: no timeout, no stall or wake resync, no snapshot-silence close.
    CHECK(lp->client.phase == BOAT_NET_CLIENT_PLAYING);
    CHECK(lp->client.stats.snapshot_resyncs == 0);
    return 0;
}

static int restart_generation(void)
{
    if (open_world(BASIN) || drive(one, 120))
        return 1;
    uint32_t old_epoch = lp->generation;
    uint32_t old_sequence = lp->client.sequence[BOAT_NET_STATE];
    if (restart_both())
        return 1;
    CHECK(lp->generation == old_epoch + 1 && lp->tick == 0);
    // Late traffic of the earlier generation: a command batch and a READY acknowledgement.
    BoatNetHeader header = { .epoch = old_epoch, .sequence = old_sequence + 100, .tick = 2 };
    BoatNetInputBatch batch = { .life = 1, .count = 2 };
    batch.commands[0] = batch.commands[1]
        = boat_net_precise_command((BoatInput) { .steering_rate = 1, .throttle_rate = 1 }, 0);
    uint8_t bytes[BOAT_NET_MAX_MESSAGE];
    size_t size = boat_net_private_encode_input(&header, &batch, bytes);
    CHECK(size);
    CHECK(boat_net_send(&lp->client.transport, 0, BOAT_NET_STATE, bytes, size));
    BoatNetMessage ready
        = { .header = { .type = BOAT_NET_READY, .epoch = old_epoch, .tick = 0 }, .checkpoint = 2 };
    size = boat_net_encode(&ready, bytes);
    CHECK(size);
    CHECK(boat_net_send(&lp->client.transport, 0, BOAT_NET_CONTROL, bytes, size));
    if (drive(one, 120))
        return 1;
    CHECK(lp->server.stats.stale_packets == 2);
    return clean_commands();
}

static int repeated_sessions(void)
{
    if (open_world(BASIN))
        return 1;
    for (unsigned session = 0; session < 100; ++session) {
        if (drive(one, 6))
            return 1;
        if (restart_both())
            return 1;
    }
    CHECK(lp->generation == 101);
    if (drive(one, 30))
        return 1;
    return clean_commands();
}

static int startup_failure(void)
{
    world = BASIN;
    BoatLocalConfig config = { .resources = worlds(), .location = BOAT_LOCATION_COUNT };
    CHECK(!boat_local_start(lp, &config, 1, 0));
    CHECK(!lp->network && !lp->running && lp->failure[0]);
    // Released after a partial start: the authority refuses a water recipe it cannot checkpoint.
    config.location = BOAT_LOCATION_BASIN;
    config.world.water = boat_water_test(false, false, false);
    config.world.water.player_wake_enabled = true;
    CHECK(!boat_local_start(lp, &config, 1, 0));
    CHECK(!lp->network && !lp->running);
    // A stopped session refuses frames.
    CHECK(!boat_local_frame(lp, 1, 1, (BoatInput) { 0 }, 0, NAN, NULL));
    return 0;
}

// A saturated queue is an endpoint failure, never a silently dropped command batch: unreliable
// overflow drops the batch, reliable overflow closes the link. Both stop the session.
static int queue_saturation(void)
{
    for (unsigned reliable = 0; reliable < 2; ++reliable) {
        if (open_world(BASIN) || drive(one, 30))
            return 1;
        BoatNetMessage ping = { .header = { .type = BOAT_NET_PING, .epoch = lp->generation } };
        uint8_t bytes[BOAT_NET_MAX_MESSAGE];
        size_t size = boat_net_encode(&ping, bytes);
        CHECK(size);
        for (unsigned i = 0; i < BOAT_NET_MEMORY_QUEUE; ++i)
            boat_net_send(&lp->client.transport, 0, reliable ? BOAT_NET_CONTROL : BOAT_NET_STATE,
                bytes, size);
        now += 1.0 / 60;
        CHECK(!boat_local_frame(lp, now, 2, (BoatInput) { 0 }, 0, NAN, NULL));
        CHECK(!lp->running && lp->failure[0]);
        CHECK(!boat_local_frame(lp, now, 2, (BoatInput) { 0 }, 0, NAN, NULL));
        boat_local_stop(lp);
    }
    return 0;
}

// A live record the authority never emitted, far from the boat, in the client's mirror pool: the
// wake digest no longer matches the snapshots.
static void inject_mismatch(void)
{
    BoatWakeObjects *w = &lp->client.fleet.state.wakes;
    w->objects[3 * BOAT_OBJECT_PER_BOAT + 5] = (BoatWakeObject) { .position = { 5000, 5000 },
        .direction = { 1, 0 },
        .length = 2,
        .amplitude = 1,
        .inverse_speed = .01f,
        .born = w->tick };
}

static bool (*real_poll)(void *context, BoatNetEvent *event);

// The client's transport with every checkpoint message lost; snapshots and births still arrive.
static bool deaf_poll(void *context, BoatNetEvent *event)
{
    while (real_poll(context, event)) {
        BoatNetMessage m;
        if (event->kind != BOAT_NET_EVENT_MESSAGE
            || boat_net_decode(event->data, event->size, &m) != BOAT_NET_OK
            || m.header.type < BOAT_NET_WORLD_BEGIN || m.header.type > BOAT_NET_WORLD_END)
            return true;
    }
    return false;
}

// The view, the authority and the pools agree after a repaired stall.
static int repaired(unsigned resyncs)
{
    const BoatNetClient *c = &lp->client;
    CHECK(c->stats.resyncs == resyncs && lp->server.stats.resyncs == resyncs);
    CHECK(lp->server.stats.rate_limited == 0);
    CHECK(c->phase == BOAT_NET_CLIENT_PLAYING && c->wake_consistent && c->local_resyncs == 0);
    const BoatFleet *view = boat_local_view(lp);
    CHECK(view->state.tick == lp->server.fleet.state.tick);
    CHECK(memcmp(&view->state.slots[0].boat, &lp->server.fleet.state.slots[0].boat, sizeof(Boat))
        == 0);
    CHECK(boat_net_wake_digest(&view->state.wakes)
        == boat_net_wake_digest(&lp->server.fleet.state.wakes));
    return 0;
}

// Section 5: a mismatch that one checkpoint repairs recovers in place; a stall that outlasts
// BOAT_NET_LOCAL_RESYNCS resyncs is endpoint failure (D10). The second stall cannot be repaired:
// a wake mismatch whose checkpoints the client's transport loses, so each resync the authority
// serves completes without applied state.
static int resync_escalation(void)
{
    if (open_world(BASIN) || drive(one, 30))
        return 1;
    inject_mismatch();
    for (unsigned n = 0; n < 200; ++n) {
        now += 1.0 / 60;
        CHECK(local_frame(helm(n), now));
    }
    if (repaired(1))
        return 1;

    inject_mismatch();
    real_poll = lp->client.transport.poll;
    lp->client.transport.poll = deaf_poll;
    unsigned n = 0;
    while (n < 1000 && (now += 1.0 / 60, local_frame(helm(n), now)))
        ++n;
    const BoatNetClient *c = &lp->client;
    CHECK(n < 1000 && !lp->running && strstr(lp->failure, "repeated resyncs"));
    CHECK(c->stats.resyncs == 1 + BOAT_NET_LOCAL_RESYNCS);
    CHECK(lp->server.stats.resyncs == 1 + BOAT_NET_LOCAL_RESYNCS);
    CHECK(lp->server.stats.rate_limited == 0);
    CHECK(!local_frame(helm(n), now));
    return 0;
}

// At 8x a second of granted ticks is an eighth of a second of host time: the authority must count
// the resync rate limit in granted ticks as the client does, or it refuses the client's next
// requests and a repairable stall escalates to failure. Two mismatches in quick succession are
// both repaired.
static int resync_8x(void)
{
    if (open_world(BASIN) || drive(eight, 4))
        return 1;
    for (unsigned resyncs = 1; resyncs <= 2; ++resyncs) {
        inject_mismatch();
        unsigned n = 0;
        while (n < 40 && lp->client.stats.resyncs < resyncs) {
            now += 1.0 / 60;
            CHECK(local_frame(eight(n++), now));
        }
        for (unsigned k = 0; k < 4; ++k) {
            now += 1.0 / 60;
            CHECK(local_frame(eight(n++), now));
        }
        if (repaired(resyncs))
            return 1;
    }
    return 0;
}

// The private codec round-trips full precision; a hosted room never accepts the private range: it
// is malformed there, as any unknown type, and rejects the sender.
static int private_range(void)
{
    uint8_t bytes[BOAT_NET_MAX_MESSAGE];
    BoatNetHeader header = { .epoch = 1, .tick = 1 };
    BoatNetInputBatch batch = { .life = 1, .count = 1 };
    CHECK(boat_net_private_encode_input(&header, &batch, bytes));
    BoatNetMessage decoded;
    size_t size = boat_net_private_encode_input(&header, &batch, bytes);
    CHECK(boat_net_decode(bytes, size, &decoded) == BOAT_NET_BAD_TYPE);
    CHECK(boat_net_private_message(bytes, size));
    BoatNetInputBatch back;
    BoatNetHeader back_header;
    CHECK(boat_net_private_decode_input(bytes, size, &back_header, &back) == BOAT_NET_OK);
    CHECK(back.commands[0].precise && back_header.tick == 1);
    CHECK(boat_net_private_decode_input(bytes, size - 1, &back_header, &back) != BOAT_NET_OK);
    batch.commands[0] = boat_net_precise_command((BoatInput) { .steering_rate = .123456f }, 0);
    size = boat_net_private_encode_input(&header, &batch, bytes);
    CHECK(boat_net_private_decode_input(bytes, size, &back_header, &back) == BOAT_NET_OK);
    CHECK(boat_net_command_input(back.commands[0]).steering_rate == .123456f);
    batch.commands[0].steering_rate = NAN;
    CHECK(!boat_net_private_encode_input(&header, &batch, bytes));
    batch.commands[0].steering_rate = 0;
    size = boat_net_private_encode_input(&header, &batch, bytes);

    static BoatNetServer hosted;
    BoatRoomDefinition room = {
        .version = BOAT_ROOM_VERSION, .mode = BOAT_ROOM_MODE_FREE, .location = BOAT_LOCATION_BASIN
    };
    memcpy(room.id, "hosted", 7);
    memcpy(room.name, "Hosted", 7);
    BoatNetMemory *network = boat_net_memory_create(7);
    CHECK(network);
    BoatNetServerConfig config = { .token = "",
        .epoch = 1,
        .max_players = 1,
        .room = &room,
        .resources = test_world(BOAT_LOCATION_BASIN) };
    BoatNetTransport client;
    unsigned peer;
    bool ok = boat_net_server_init(&hosted, boat_net_memory_server(network), &config)
        && boat_net_memory_client(network, &peer, &client)
        && boat_net_send(&client, 0, BOAT_NET_STATE, bytes, size);
    ok = ok && boat_net_server_service(&hosted, 0) && !boat_net_server_grant(&hosted, 0, 1);
    boat_net_memory_destroy(network);
    CHECK(ok);
    CHECK(hosted.stats.malformed == 1 && hosted.stats.rejected[BOAT_NET_REJECT_MALFORMED] == 1);
    CHECK(hosted.stats.commands == 0 && hosted.fleet.state.tick == 0);
    return 0;
}

// L2: a time trial as the game opens one: the course start, recording runs, the pool reset after
// the start. No best path, so a new best's save fails (the storage failure path) without files.
static void course_open(Play *p)
{
    boat_session_init(&p->session, test_world(world.location), world_water());
    boat_session_set_course_definition(&p->session, &boat_fixture_course_default);
    boat_session_start_course(&p->session);
    p->session.record_runs = true;
    p->session.runs.ghost_visible = true;
    object_study_reset(
        &p->objects, &p->session, false, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT);
    p->accumulator = 0;
}

static void course_pilots(void)
{
    pilot_reset(&pilots[0], &direct.session.boat);
    pilot_reset(&pilots[1], &play.session.boat);
}

static int open_course(World w)
{
    if (open_world(w))
        return 1;
    boat_session_destroy(&direct.session);
    boat_session_destroy(&play.session);
    wake_local_close(&game);
    course_open(&direct);
    course_open(&play);
    autopilot = true;
    game.script = &PILOT;
    course_pilots();
    CHECK(wake_local_open(&game, &play.session, &play.objects, worlds(), now));
    CHECK(game.course_active && same_authority() && same_course());
    return 0;
}

static int restart_course(void)
{
    if (restart_both())
        return 1;
    course_pilots();
    CHECK(same_course() && play.session.course.next_gate == 0 && !play.session.runs.finished);
    return 0;
}

// Time-trial frames: 1x with an occasional 0.1 s frame (12-tick grants), or 8x.
static Frame trial(unsigned n)
{
    return (Frame) {
        .seconds = n % 7 == 3 ? .1 : 1.0 / 60, .speed = BOAT_TIME_NORMAL, .steering_target = NAN
    };
}

static Frame trial_8x(unsigned n)
{
    Frame f = trial(n);
    f.speed = BOAT_TIME_MAX_INDEX;
    return f;
}

// Drive SCRIPT until both paths finished the attempt and a few frames beyond.
static int attempt(Script script)
{
    unsigned frames = 0;
    while (!direct.session.runs.finished && frames < 20000) {
        if (drive(script, 1))
            return 1;
        ++frames;
    }
    CHECK(direct.session.runs.finished && play.session.runs.finished);
    if (drive(script, 30))
        return 1;
    CHECK(direct.session.course.next_gate == BOAT_COURSE_GATES);
    return clean_commands();
}

// Equal course progress, splits, finish and recorded run from the same commands; best selection
// (a first finish is a new best, an equal time is not, beating a slower saved best is); the save
// request's failure notice; a restart starts a new attempt against the new best as ghost.
static int course_trace(void)
{
    if (open_course(BASIN) || attempt(trial))
        return 1;
    const BoatReplayRuns *runs = &play.session.runs;
    CHECK(runs->last.complete && runs->best.complete && runs->last.count > 1000);
    CHECK(!strcmp(runs->notice, "New best kept in memory; could not save to disk"));
    double time = runs->best.splits[BOAT_COURSE_GATES - 1];
    printf("time trial: %.3f s, %zu samples\n", time, runs->last.count);
    if (restart_course() || attempt(trial))
        return 1;
    CHECK(!strcmp(runs->notice, "Run recorded") && !runs->save_best);
    CHECK(runs->best.splits[BOAT_COURSE_GATES - 1] == time && runs->opponent.complete);
    // A slower saved best: the next finish replaces it.
    direct.session.runs.best.splits[BOAT_COURSE_GATES - 1] += 1;
    play.session.runs.best.splits[BOAT_COURSE_GATES - 1] += 1;
    if (restart_course() || attempt(trial))
        return 1;
    CHECK(!strcmp(runs->notice, "New best kept in memory; could not save to disk"));
    CHECK(runs->best.splits[BOAT_COURSE_GATES - 1] == time);
    CHECK(lp->generation == 3);
    return 0;
}

// Altered time latches practice: the run is recorded but never replaces the best or saves, in
// waves too; the next attempt at 1x is not practice.
static int course_practice(void)
{
    World w = BASIN;
    w.waves = true;
    if (open_course(w) || attempt(trial_8x))
        return 1;
    const BoatReplayRuns *runs = &play.session.runs;
    CHECK(runs->practice && runs->last.complete && runs->last.waves && !runs->best.complete);
    CHECK(!strcmp(runs->notice, "Practice recorded; best and export preserved"));
    CHECK(!runs->save_best && !runs->export_last);
    if (restart_course())
        return 1;
    CHECK(!runs->practice);
    if (drive(trial, 300))
        return 1;
    CHECK(!runs->practice && play.session.course.elapsed > 0);
    return clean_commands();
}

// The course codec: its private state round-trips into the view and rejects malformed bytes.
static int course_state(void)
{
    WakeCourseAuthority a = { 0 };
    BoatSession s = { 0 };
    s.course_definition = &boat_fixture_course_default;
    s.presentation_enabled = true;
    wake_course_init(&a, &s, 0);
    a.course.next_gate = 2;
    a.course.elapsed = 12.5;
    a.course.splits[0] = 1.25;
    a.course.crossing[1] = .5;
    a.practice = true;
    uint8_t bytes[BOAT_NET_PRIVATE_STATE_BYTES];
    size_t size = wake_course_encode(&a, bytes);
    CHECK(size == WAKE_COURSE_STATE_BYTES);
    s.course = (BoatCourse) { .enabled = true, .definition = &boat_fixture_course_default };
    CHECK(wake_course_view(bytes, size, &s));
    CHECK(s.course.next_gate == 2 && s.course.elapsed == 12.5 && s.course.splits[0] == 1.25
        && s.course.crossing[1] == .5 && s.course.enabled && s.runs.practice);
    CHECK(!wake_course_view(bytes, size - 1, &s));
    bytes[0] = BOAT_COURSE_GATES + 1;
    CHECK(!wake_course_view(bytes, size, &s) && s.course.next_gate == 2);
    // The private state message: decoded only as private state, never as v14.
    BoatNetHeader header = { .epoch = 3, .sequence = 9, .tick = 40 }, back;
    uint8_t message[BOAT_NET_MAX_MESSAGE], state[BOAT_NET_PRIVATE_STATE_BYTES];
    size_t length = boat_net_private_encode_state(&header, bytes, size, message), got = 0;
    CHECK(length == BOAT_NET_HEADER_BYTES + size && boat_net_private_message(message, length));
    static BoatNetMessage decoded;
    CHECK(boat_net_decode(message, length, &decoded) == BOAT_NET_BAD_TYPE);
    BoatNetInputBatch batch;
    CHECK(boat_net_private_decode_input(message, length, &back, &batch) == BOAT_NET_BAD_TYPE);
    CHECK(boat_net_private_decode_state(message, length, &back, state, &got) == BOAT_NET_OK);
    CHECK(got == size && !memcmp(state, bytes, size) && back.tick == 40 && back.epoch == 3);
    CHECK(boat_net_private_decode_state(message, length - 1, &back, state, &got) != BOAT_NET_OK);
    CHECK(!boat_net_private_encode_state(&header, bytes, 0, message));
    CHECK(
        !boat_net_private_encode_state(&header, bytes, BOAT_NET_PRIVATE_STATE_BYTES + 1, message));
    wake_course_destroy(&a);
    return 0;
}

// Per-frame cost at 8x on outer: processor time of one frame's grant through both endpoints.
static int bench(void)
{
    if (open_world(OUTER))
        return 1;
    const unsigned frames = 1200;
    double worst = 0, total = 0, worst_96 = 0, total_96 = 0;
    for (unsigned n = 0; n < 2 * frames; ++n) {
        Frame f = eight(n);
        if (n >= frames)
            f.seconds = .1; // G = 96
        now += f.seconds;
        clock_t start = clock();
        CHECK(local_frame(f, now));
        double ms = (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC;
        if (n < frames) {
            total += ms;
            worst = fmax(worst, ms);
        } else {
            total_96 += ms;
            worst_96 = fmax(worst_96, ms);
        }
    }
    printf("startup: %u service rounds\n", lp->stats.startup_rounds);
    printf("outer 8x 60 Hz (16 ticks/frame): mean %.3f ms, worst %.3f ms over %u frames\n",
        total / frames, worst, frames);
    printf("outer 8x 0.1 s frames (G = 96): mean %.3f ms, worst %.3f ms over %u frames\n",
        total_96 / frames, worst_96, frames);
    // The direct path for the same frames, with presentation, as the local frames above.
    play_open(&direct);
    worst = total = worst_96 = total_96 = 0;
    for (unsigned n = 0; n < 2 * frames; ++n) {
        Frame f = eight(n);
        if (n >= frames)
            f.seconds = .1;
        expected_count = 0;
        clock_t start = clock();
        direct_frame(f);
        double ms = (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC;
        if (n < frames) {
            total += ms;
            worst = fmax(worst, ms);
        } else {
            total_96 += ms;
            worst_96 = fmax(worst_96, ms);
        }
    }
    printf("direct path, outer 8x 60 Hz: mean %.3f ms, worst %.3f ms\n", total / frames, worst);
    printf(
        "direct path, outer 8x G = 96: mean %.3f ms, worst %.3f ms\n", total_96 / frames, worst_96);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--bench")) {
        int result = bench();
        boat_local_stop(lp);
        test_worlds_close();
        return result;
    }
    static const TestCase cases[] = {
        TEST_CASE(basin_1x),
        TEST_CASE(basin_quarter),
        TEST_CASE(basin_8x),
        TEST_CASE(outer_1x),
        TEST_CASE(outer_8x),
        SLOW_CASE(scripted_demo),
        SLOW_CASE(scripted_capture),
        TEST_CASE(scripted_needs_script),
        TEST_CASE(grant_96),
        TEST_CASE(grant_cap),
        TEST_CASE(odd_chunk_sizes),
        TEST_CASE(explorer_handling),
        TEST_CASE(pause_and_focus),
        TEST_CASE(restart_generation),
        TEST_CASE(repeated_sessions),
        TEST_CASE(startup_failure),
        TEST_CASE(queue_saturation),
        TEST_CASE(resync_escalation),
        TEST_CASE(resync_8x),
        TEST_CASE(private_range),
        SLOW_CASE(course_trace),
        SLOW_CASE(course_practice),
        TEST_CASE(course_state),
    };
    int result = test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
    boat_local_stop(lp);
    test_worlds_close();
    return result;
}
