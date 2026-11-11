/* AR6 (ledger L0) gates for the private local session (src/boat_local.[ch]): the authority and
 * client in one process over the memory transport, driven by host grants. Every scenario runs the
 * game's direct free-boating path (BoatSession stepped by timed_boat_step with the arcade wake
 * pool) beside a local session fed the same frames, and requires bit-identical boats and wake
 * pools at every predicted tick and at every frame's authority tick. The L6 cases drive both with
 * the scripted drive instead (demo_step against the glue's scripted frames and capture drive). The
 * L2 cases run the time trial: a course autopilot through the direct session's own course and
 * recorder beside the local authority's course adapter, with equal course state, runs, bests,
 * notices and save requests after every frame. The L1 cases run a voyage or tour: the direct
 * session's own voyage beside the authority's voyage adapter, with equal voyage state, weather
 * and boats through a recovery.
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
// L1: both sessions carry a voyage; every frame compares voyage state and weather.
static bool voyaging;
// L1b: with a revision, the direct session's objective (REFERENCE, stepped as the direct path's
// wake_plugin_before_tick/after_tick step it) beside the authority adapter's objective.
static const BoatPluginRevision *objective_revision;
static WakeObjective reference;
// The frame after a migration request: the host rebinds its view only when the result arrives.
static bool route_lag;
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
// wake_plugin_after_tick's native path over the direct session's voyage events.
static void reference_after_tick(void)
{
    const BoatSession *s = &direct.session;
    if (reference.objective.initialized
        && wake_objective_queue(&reference, objective_revision, s->tick, &s->voyage_events))
        wake_objective_update(&reference, objective_revision, s->tick);
}

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
        if (objective_revision)
            wake_objective_before_tick(&reference);
        if (scripted)
            demo_step(s, (int)s->tick, NULL, NULL, &direct.objects);
        else if (autopilot)
            timed_boat_step(s, pilot_input(&pilots[0], &s->boat), NULL, NULL, &direct.objects);
        else
            timed_boat_step(s,
                boat_helm_input(&s->boat, (BoatInput) { .steering_rate = f.steering_rate },
                    f.thrust_target, f.steering_target),
                NULL, NULL, &direct.objects);
        if (objective_revision)
            reference_after_tick();
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

// L1: the client's voyage view and storm, and the authority adapter's voyage and the authority's
// storm, equal the direct session's.
static bool same_voyage(void)
{
    uint8_t a[WAKE_VOYAGE_STATE_BYTES], b[WAKE_VOYAGE_STATE_BYTES], c[WAKE_VOYAGE_STATE_BYTES];
    wake_voyage_encode(&direct.session.voyage, a);
    wake_voyage_encode(&play.session.voyage, b);
    wake_voyage_encode(&game.voyage.voyage, c);
    const BoatStorm *storm = &direct.session.world.water.storm;
    return !memcmp(a, b, sizeof(a)) && !memcmp(a, c, sizeof(a))
        && (route_lag || play.session.voyage.route == direct.session.voyage.route)
        && game.voyage.voyage.route == direct.session.voyage.route
        && !memcmp(storm, &play.session.world.water.storm, sizeof(*storm))
        && !memcmp(storm, &lp->server.fleet.world.water.storm, sizeof(*storm))
        && !memcmp(storm, &lp->client.prediction.fleet.world.water.storm, sizeof(*storm));
}

static bool same_trace(const WakeObjective *a, const WakeObjective *b)
{
    if (a->trace_count != b->trace_count)
        return false;
    for (unsigned i = 0; i < a->trace_count; ++i) {
        const WakePluginTrace *x = &a->trace[i], *y = &b->trace[i];
        if (x->tick != y->tick || x->revision != y->revision || x->events != y->events
            || x->transition != y->transition || x->status != y->status || x->before != y->before
            || x->after != y->after || x->command_count != y->command_count)
            return false;
    }
    return true;
}

// L1b: the authority's objective transaction equals the direct path's.
static bool same_objective_authority(void)
{
    const WakeObjective *a = &reference, *b = &game.voyage.objective;
    if (boat_plugin_objective_digest(&a->objective) != boat_plugin_objective_digest(&b->objective)
        || a->objective.quarantined != b->objective.quarantined
        || a->objective_pending != b->objective_pending
        || (a->objective_pending
            && boat_plugin_objective_digest(&a->staged_objective)
                != boat_plugin_objective_digest(&b->staged_objective))
        || a->event_count != b->event_count || a->sequence != b->sequence
        || a->generation != b->generation || !same_trace(a, b)
        || strcmp(a->objective_diagnostic.message, b->objective_diagnostic.message))
        return false;
    for (unsigned i = 0; i < a->event_count; ++i)
        if (a->events[i].tick != b->events[i].tick || a->events[i].sequence != b->events[i].sequence
            || strcmp(a->events[i].kind, b->events[i].kind)
            || strcmp(a->events[i].id, b->events[i].id))
            return false;
    return true;
}

// ... and the client's view shows its HUD, generation and suspension.
static bool same_objective(void)
{
    if (!same_objective_authority())
        return false;
    const WakeObjective *a = &reference;
    const WakeObjectiveView *v = &game.objective;
    const BoatPluginObjective *o = &a->objective;
    if (!v->valid || v->generation != a->generation || v->suspended != o->quarantined
        || v->hud_count != o->hud_count
        || (v->commit_status != WAKE_COMMIT_FAILED
            && strcmp(v->message, o->quarantined ? a->objective_diagnostic.message : "")))
        return false;
    for (unsigned i = 0; i < o->hud_count; ++i)
        if (strcmp(v->hud[i].id, o->hud[i].id) || strcmp(v->hud[i].label, o->hud[i].label)
            || v->hud[i].value != o->hud[i].value)
            return false;
    return true;
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
        if (voyaging)
            CHECK(same_voyage());
        if (objective_revision)
            CHECK(same_objective());
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
    scripted = autopilot = voyaging = false;
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

// L2: PrivateState (0xC1) never crosses a hosted link. A hosted server counts it as malformed and
// rejects the sender; a playing hosted client counts it as a decode error and keeps no state.
// Virtual time: every service is stamped by the loop, never the wall clock.
static int private_state_rejected(void)
{
    uint8_t state[WAKE_COURSE_STATE_BYTES] = { 0 }, bytes[BOAT_NET_MAX_MESSAGE];
    static BoatNetServer hosted;
    BoatRoomDefinition room = {
        .version = BOAT_ROOM_VERSION, .mode = BOAT_ROOM_MODE_FREE, .location = BOAT_LOCATION_BASIN
    };
    memcpy(room.id, "hosted", 7);
    memcpy(room.name, "Hosted", 7);
    BoatNetServerConfig config = { .token = "",
        .epoch = 1,
        .max_players = 1,
        .room = &room,
        .resources = test_world(BOAT_LOCATION_BASIN) };
    for (unsigned to_client = 0; to_client < 2; ++to_client) {
        BoatNetMemory *network = boat_net_memory_create(11);
        CHECK(network);
        BoatNetTransport transport;
        unsigned peer;
        bool ok = boat_net_server_init(&hosted, boat_net_memory_server(network), &config)
            && boat_net_memory_client(network, &peer, &transport);
        if (!to_client) {
            BoatNetHeader header = { .epoch = 1, .tick = 1 };
            size_t size = boat_net_private_encode_state(&header, state, sizeof(state), bytes);
            ok = ok && size && boat_net_send(&transport, 0, BOAT_NET_CONTROL, bytes, size)
                && boat_net_server_service(&hosted, 0);
            boat_net_memory_destroy(network);
            CHECK(ok);
            CHECK(hosted.stats.malformed == 1
                && hosted.stats.rejected[BOAT_NET_REJECT_MALFORMED] == 1);
            continue;
        }
        static BoatNetClient remote;
        world = BASIN;
        BoatNetClientConfig client = { .name = "remote", .token = "", .resources = worlds() };
        boat_net_client_init(&remote, transport, &client, 0);
        double at = 0;
        for (unsigned round = 0; ok && round < 600 && remote.phase != BOAT_NET_CLIENT_PLAYING;
            ++round) {
            at += 1.0 / 120;
            boat_net_memory_set_time(network, at);
            ok = boat_net_server_service(&hosted, at)
                && boat_net_client_service(&remote, at, (BoatInput) { 0 }, false);
        }
        CHECK(ok && remote.phase == BOAT_NET_CLIENT_PLAYING);
        uint64_t errors = remote.stats.decode_errors;
        BoatNetHeader header = { .epoch = remote.epoch, .tick = hosted.fleet.state.tick };
        size_t size = boat_net_private_encode_state(&header, state, sizeof(state), bytes);
        ok = size && boat_net_send(&hosted.transport, peer, BOAT_NET_CONTROL, bytes, size);
        at += 1.0 / 120;
        boat_net_memory_set_time(network, at);
        ok = ok && boat_net_client_service(&remote, at, (BoatInput) { 0 }, false);
        boat_net_memory_destroy(network);
        CHECK(ok);
        CHECK(remote.stats.decode_errors == errors + 1 && remote.stats.private_states == 0);
        CHECK(remote.private_state_size == 0 && remote.phase == BOAT_NET_CLIENT_PLAYING);
    }
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

// Drive SCRIPT until the attempt has crossed GATE on both paths.
static int until_gate(Script script, int gate)
{
    for (unsigned frames = 0; direct.session.course.next_gate < gate && frames < 20000; ++frames)
        if (drive(script, 1))
            return 1;
    CHECK(direct.session.course.next_gate >= gate && play.session.course.next_gate >= gate);
    return 0;
}

// A restart mid-attempt (after the second gate): a new generation at the course start with no
// finish, save or practice carried, and the new attempt equal on both paths.
static int course_restart_mid(void)
{
    if (open_course(BASIN) || until_gate(trial, 2))
        return 1;
    const BoatReplayRuns *runs = &play.session.runs;
    CHECK(!runs->finished && runs->recording.count == 0 && game.course.recording.count > 0);
    if (restart_course())
        return 1;
    CHECK(lp->generation == 2 && lp->tick == 0 && game.course.recording.count == 0);
    CHECK(play.session.course.elapsed == 0 && !runs->finished && !runs->save_best);
    CHECK(!runs->last.complete && !runs->best.complete && !game.course.has_best);
    if (drive(trial, 120))
        return 1;
    return clean_commands();
}

// Altered time mid-run: one 8x frame after the first gate latches practice on the authority for
// the rest of the attempt, so the finish is recorded but never becomes best or saves.
static int course_practice_mid(void)
{
    if (open_course(BASIN) || until_gate(trial, 1))
        return 1;
    CHECK(!play.session.runs.practice && !game.course.practice);
    if (drive(trial_8x, 1))
        return 1;
    CHECK(play.session.runs.practice && game.course.practice);
    if (attempt(trial))
        return 1;
    const BoatReplayRuns *runs = &play.session.runs;
    CHECK(runs->practice && runs->last.complete && !runs->best.complete && !runs->save_best);
    CHECK(!strcmp(runs->notice, "Practice recorded; best and export preserved"));
    return 0;
}

// A new best is saved by the frame's flush and, on the next launch, selected from the data
// directory and raced as the ghost: the local authority starts from its time, and an equal finish
// is not a new best. Files go under the test's working directory.
static int course_saved_ghost(void)
{
    static const char directory[] = "boat-local-saves";
    char path[2048];
    if (open_course(BASIN))
        return 1;
    for (unsigned i = 0; i < 2; ++i) {
        BoatReplayRuns *runs = i ? &play.session.runs : &direct.session.runs;
        boat_replay_runs_select_best(runs, i ? directory : "boat-local-saves-direct", world.waves,
            BOAT_COURSE_STANDARD_DIGEST);
        remove(runs->best_path);
    }
    snprintf(path, sizeof(path), "%s", play.session.runs.best_path);
    CHECK(path[0] && !FileExists(path));
    if (attempt(trial))
        return 1;
    CHECK(!strcmp(play.session.runs.notice, "New personal best!") && FileExists(path));
    BoatReplay first = { 0 };
    CHECK(boat_replay_copy(&first, &play.session.runs.best));
    remove(direct.session.runs.best_path);
    // The next launch: a fresh session selects the saved best and races it as the ghost.
    wake_local_close(&game);
    for (unsigned i = 0; i < 2; ++i) {
        Play *p = i ? &play : &direct;
        boat_session_destroy(&p->session);
        course_open(p);
        CHECK(boat_replay_runs_select_best(
            &p->session.runs, directory, world.waves, BOAT_COURSE_STANDARD_DIGEST));
        boat_replay_runs_reset(&p->session.runs);
    }
    CHECK(same_run(&play.session.runs.best, &first) && play.session.runs.opponent.complete);
    CHECK(play.session.runs.opponent.count == first.count);
    course_pilots();
    CHECK(wake_local_open(&game, &play.session, &play.objects, worlds(), now));
    CHECK(game.course.has_best && game.course.best == first.splits[BOAT_COURSE_GATES - 1]);
    // Neither session writes this time (an equal run is not a best).
    if (attempt(trial))
        return 1;
    CHECK(!strcmp(play.session.runs.notice, "Run recorded") && !play.session.runs.save_best);
    BoatViewState ghost;
    CHECK(boat_replay_sample(&play.session.runs.opponent, 1, &ghost, NULL));
    remove(path);
    remove(directory); // Empty directories; left behind where remove() does not take them.
    remove("boat-local-saves-direct");
    boat_replay_destroy(&first);
    return 0;
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

// L1: a test route laid along the outer spawn's heading, so a short straight drive meets it: a
// shelter 40 m ahead, weather that begins 110 m from a storm 160 m ahead (60 m radius, grown over
// 4 s, exposed inside 48 m) and the destination 300 m ahead, reached at any speed. A tour passes
// the same stops in order.
static BoatVoyageRoute test_route;

static void route_along(const Boat *spawn, bool ordered)
{
    Vector2 forward = { sinf(spawn->heading), -cosf(spawn->heading) };
    Vector2 at[4];
    const float distance[4] = { 0, 40, 160, 300 };
    for (unsigned i = 0; i < 4; ++i)
        at[i] = Vector2Add(spawn->position, Vector2Scale(forward, distance[i]));
    test_route = (BoatVoyageRoute) {
        .shelters = { { "START", at[0], spawn->heading, 15, NULL },
            { "AHEAD", at[1], spawn->heading, 15, NULL },
            { "NEAR STORM", Vector2Add(at[2], (Vector2) { forward.y * 90, -forward.x * 90 }),
                spawn->heading, 20, NULL },
            { "END", at[3], spawn->heading, 0, NULL } },
        .shelter_count = 4,
        .ordered = ordered,
        .arrival_centre = at[3],
        .arrival_radius = 25,
        .arrival_speed = 40, // Arrives at full throttle.
        .exposure = { .centre = at[2], .radius = 60, .height = 1.6f, .build = 4 },
        .exposed_fraction = .8f,
        .weather_range = 110,
    };
}

static void voyage_open(Play *p)
{
    boat_session_init(&p->session, test_world(world.location), world_water());
    p->session.voyage_route = &test_route;
    boat_session_restart(&p->session, world_water());
    object_study_reset(
        &p->objects, &p->session, false, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT);
    p->accumulator = 0;
}

static int open_voyage(World w, bool ordered)
{
    if (open_world(w))
        return 1;
    route_along(&direct.session.boat, ordered);
    wake_local_close(&game);
    voyage_open(&direct);
    voyage_open(&play);
    voyaging = true;
    game.revision = objective_revision;
    game.generation = 1;
    if (objective_revision) {
        memset(&reference, 0, sizeof(reference));
        if (wake_objective_clear(&reference, objective_revision, 0))
            wake_objective_init(&reference, objective_revision, 0);
    }
    CHECK(wake_local_open(&game, &play.session, &play.objects, worlds(), now));
    CHECK(game.voyage_active && !game.course_active && same_authority() && same_voyage());
    // The client's view arrives with the first frame's state.
    CHECK(!objective_revision || same_objective_authority());
    // The straight line ahead is open water as far as the destination.
    for (float d = 0; d <= 320; d += 5) {
        Vector2 at = Vector2Add(test_route.shelters[0].position,
            Vector2Scale((Vector2) { sinf(test_route.shelters[0].heading),
                             -cosf(test_route.shelters[0].heading) },
                d));
        CHECK(boat_map_water(&test_world(w.location)->map, at, 2.5f));
    }
    return 0;
}

// Straight ahead at full throttle: 1x with an occasional 0.1 s frame, or 8x.
static Frame ahead(unsigned n)
{
    return (Frame) { .seconds = n % 7 == 3 ? .1 : 1.0 / 60,
        .speed = BOAT_TIME_NORMAL,
        .thrust_target = 1,
        .steering_target = NAN };
}

static Frame ahead_8x(unsigned n)
{
    Frame f = ahead(n);
    f.speed = BOAT_TIME_MAX_INDEX;
    return f;
}

// Both paths take the game's recover branch between frames.
static int recover_both(void)
{
    boat_session_recover(&direct.session);
    if (objective_revision)
        reference_after_tick();
    direct.accumulator = 0;
    uint64_t recoveries = lp->server.stats.recoveries;
    CHECK(wake_local_recover(&game, &play.session, &play.objects));
    play.accumulator = 0;
    CHECK(!memcmp(&play.session.boat, &direct.session.boat, sizeof(Boat)));
    CHECK(play.session.voyage.recoveries == direct.session.voyage.recoveries);
    CHECK(play.session.voyage_events.count == 1
        && play.session.voyage_events.items[0].kind == BOAT_VOYAGE_RECOVERED);
    // The authority places it at the next frame's start.
    if (drive(ahead, 1))
        return 1;
    CHECK(lp->server.stats.recoveries == recoveries + 1 && !lp->server.stats.recoveries_ignored);
    return 0;
}

// Drive SCRIPT until the direct voyage satisfies DONE, at most FRAMES frames.
static int voyage_until(Script script, bool (*done)(const BoatVoyage *), unsigned frames)
{
    for (unsigned n = 0; n < frames && !done(&direct.session.voyage); ++n)
        if (drive(script, 1))
            return 1;
    CHECK(done(&direct.session.voyage));
    return 0;
}

static bool past_ahead(const BoatVoyage *v) { return v->shelter == 1; }
static bool weather_begun(const BoatVoyage *v) { return v->weather; }
static bool exposed(const BoatVoyage *v) { return v->exposed; }
static bool arrived(const BoatVoyage *v) { return v->arrived; }

// Equal voyage progress, events, weather and boats from the same commands: the shelter ahead, the
// weather beginning and growing, exposed water under the hull, a recovery back to the shelter and
// on to the destination.
static int voyage_trace(void)
{
    if (open_voyage(OUTER, false) || voyage_until(ahead, past_ahead, 2000)
        || voyage_until(ahead, weather_begun, 2000))
        return 1;
    CHECK(direct.session.voyage.event == BOAT_VOYAGE_WEATHER);
    CHECK(lp->server.fleet.world.water.storm.radius > 0);
    if (voyage_until(ahead, exposed, 2000) || drive(ahead, 120))
        return 1;
    if (recover_both())
        return 1;
    CHECK(play.session.voyage.recoveries == 1 && play.session.voyage.shelter == 1);
    if (voyage_until(ahead_8x, arrived, 4000) || drive(ahead, 30))
        return 1;
    printf("voyage: arrived at %.2f s, %u recovery\n", play.session.voyage.arrival,
        play.session.voyage.recoveries);
    return clean_commands();
}

// A tour starts at its first stop and passes the stops in order; a restart is a new generation
// with the weather met so far kept, as boat_session_restart keeps none of it (both paths alike).
static int tour_trace(void)
{
    if (open_voyage(OUTER, true) || voyage_until(ahead_8x, past_ahead, 2000)
        || voyage_until(ahead_8x, weather_begun, 2000))
        return 1;
    play_restart(&direct);
    play_restart(&play);
    CHECK(wake_local_restart(&game, &play.session, &play.objects, now));
    CHECK(same_authority() && same_voyage() && play.session.voyage.shelter == 0);
    if (voyage_until(ahead_8x, past_ahead, 2000) || recover_both())
        return 1;
    CHECK(play.session.voyage.shelter == 1 && lp->generation == 2);
    return clean_commands();
}

// L1b: a voyage revision over the test route whose objective counts the voyage's events in its
// HUD and re-arms a timer; FAILING raises an error at the first weather event instead.
static BoatPluginRevision objective_test_revision;

static const BoatPluginRevision *objective_test(bool failing)
{
    BoatPluginRevision *r = &objective_test_revision;
    memset(r, 0, sizeof(*r));
    r->number = 1;
    r->manifest
        = (BoatPluginManifest) { .api = 1, .state_version = 1, .objective = true, .timers = true };
    snprintf(r->manifest.kind, sizeof(r->manifest.kind), "voyage");
    r->voyage.manifest = r->manifest;
    static const char *const ids[] = { "start", "ahead", "near_storm", "end" };
    for (unsigned i = 0; i < 4; ++i)
        snprintf(r->voyage.shelter_ids[i], sizeof(r->voyage.shelter_ids[i]), "%s", ids[i]);
    int n = snprintf(r->source, sizeof(r->source),
        "local function hud(c, s)\n"
        "  return {{kind='hud.set', generation=c.generation, id='reached', label='REACHED',"
        " value=s.reached},\n"
        "    {kind='hud.set', generation=c.generation, id='recovered', label='RECOVERED',"
        " value=s.recovered},\n"
        "    {kind='hud.set', generation=c.generation, id='timer', label='TIMER', value=s.fired}}\n"
        "end\n"
        "local function arm(c, commands)\n"
        "  commands[#commands + 1] = {kind='timer.set', generation=c.generation, id='tick',"
        " tick=c.tick + 300}\n"
        "  return commands\n"
        "end\n"
        "return {\n"
        "  init = function(c)\n"
        "    local s = {reached=0, recovered=0, fired=0}\n"
        "    return s, arm(c, hud(c, s))\n"
        "  end,\n"
        "  update = function(c, s)\n"
        "    for _, e in ipairs(c.events) do\n"
        "      if e.kind == 'voyage.shelter_reached' then s.reached = s.reached + 1 end\n"
        "      if e.kind == 'voyage.recovered' then s.recovered = s.recovered + 1 end\n"
        "      if %s and e.kind == 'voyage.weather' then error('storm in the objective') end\n"
        "    end\n"
        "    s.fired = s.fired + #c.timers\n"
        "    local commands = hud(c, s)\n"
        "    if #c.timers > 0 then arm(c, commands) end\n"
        "    return s, commands\n"
        "  end\n"
        "}\n",
        failing ? "true" : "false");
    r->source_size = (size_t)n;
    return r;
}

// Equal objective transactions from the same commands: the objective's checkpoint, queue,
// staged batch, generation and trace on the authority equal the direct path's after every frame
// through a shelter, the weather, a recovery and arrival, and again after a restart (a new
// generation); the client shows its HUD.
static int objective_trace(void)
{
    objective_revision = objective_test(false);
    int failed = open_voyage(OUTER, false) || voyage_until(ahead, past_ahead, 2000)
        || voyage_until(ahead, weather_begun, 2000) || voyage_until(ahead, exposed, 2000)
        || recover_both() || voyage_until(ahead_8x, arrived, 4000) || drive(ahead, 30);
    if (!failed) {
        CHECK(game.objective.hud_count == 3 && game.objective.hud[1].value == 1);
        CHECK(game.objective.hud[0].value >= 1 && game.objective.hud[2].value >= 1);
        CHECK(reference.trace_count > 4 && !reference.objective.quarantined);
        // A restart: both start the objective over as generation 2.
        play_restart(&direct);
        play_restart(&play);
        if (wake_objective_clear(&reference, objective_revision, 0))
            wake_objective_init(&reference, objective_revision, 0);
        game.generation = 2;
        CHECK(wake_local_restart(&game, &play.session, &play.objects, now));
        CHECK(same_objective_authority());
        failed = voyage_until(ahead, past_ahead, 2000) || drive(ahead, 30);
        CHECK(!failed && game.objective.generation == 2 && game.objective.hud[0].value == 1);
    }
    printf("objective: %u trace entries, HUD reached %lld, recovered %lld, timer %lld\n",
        reference.trace_count, (long long)game.objective.hud[0].value,
        (long long)game.objective.hud[1].value, (long long)game.objective.hud[2].value);
    objective_revision = NULL;
    game.revision = NULL;
    return failed;
}

// A failing callback quarantines the objective on both paths at the same boundary; the voyage
// keeps running and the client shows the suspension and its text.
static int objective_quarantine(void)
{
    objective_revision = objective_test(true);
    int failed
        = open_voyage(OUTER, false) || voyage_until(ahead, weather_begun, 2000) || drive(ahead, 30);
    if (!failed) {
        CHECK(reference.objective.quarantined && game.objective.suspended);
        CHECK(strstr(game.objective.message, "storm in the objective"));
        failed = voyage_until(ahead_8x, arrived, 4000);
    }
    objective_revision = NULL;
    game.revision = NULL;
    return failed;
}

// L1b commits: test revisions numbered from 1 (the objective's), resolved by the authority through
// this table as the game's host table resolves its own.
enum { REVISIONS = 8 };
static BoatPluginRevision revisions[REVISIONS];

static const BoatPluginRevision *resolve(void *context, unsigned number)
{
    (void)context;
    return number >= 1 && number <= REVISIONS && revisions[number - 1].number == number
        ? &revisions[number - 1]
        : NULL;
}

// Revision NUMBER as a copy of BASE with ROUTE (NULL keeps BASE's) and SOURCE appended to its
// objective's table (NULL keeps the source).
static BoatPluginRevision *revision_from(unsigned number, const BoatPluginRevision *base,
    const BoatVoyageRoute *route, const char *source)
{
    BoatPluginRevision *r = &revisions[number - 1];
    *r = *base;
    r->number = number;
    if (route)
        r->voyage.route = *route;
    if (source) {
        int n = snprintf(r->source, sizeof(r->source),
            "local p=(function() %.*s\nend)(); %s; return p", (int)base->source_size, base->source,
            source);
        r->source_size = (size_t)n;
    }
    return r;
}

// The host's commit on both paths: the direct session and objective as wake_velocity_plugin.c's
// commit changes them, and the local host's view followed by its request to the authority. A
// migration's result reaches the local host a frame later, so it is checked by the caller.
static int commit_both(BoatPluginRevision *next, BoatPluginChange change)
{
    const BoatPluginRevision *previous = resolve(NULL, game.voyage.revision->number);
    BoatSession *sessions[2] = { &direct.session, &play.session };
    bool moved = false;
    if (change == BOAT_PLUGIN_LIVE) {
        const char *ids[BOAT_VOYAGE_SHELTERS];
        for (unsigned i = 0; i < next->voyage.route.shelter_count; ++i)
            ids[i] = next->voyage.shelter_ids[i];
        const char *old = previous->voyage.shelter_ids[direct.session.voyage.shelter];
        for (unsigned i = 0; i < 2; ++i)
            boat_session_rebind_voyage_live(
                sessions[i], &next->voyage.route, old, ids, next->voyage.route.shelter_count);
    } else if (change == BOAT_PLUGIN_RESTART_VOYAGE) {
        for (unsigned i = 0; i < 2; ++i)
            boat_session_restart_voyage(sessions[i], &next->voyage.route);
        if (wake_objective_clear(&reference, next, direct.session.tick))
            wake_objective_init(&reference, next, direct.session.tick);
        moved = next->voyage.route.ordered;
    } else if (change == BOAT_PLUGIN_MIGRATE) {
        static BoatPluginObjective migrated;
        if (wake_objective_migrate(
                &reference, next, previous->number, direct.session.tick, &migrated)) {
            wake_objective_adopt(&reference, next->number, &migrated);
            boat_session_rebind_voyage(&direct.session, &next->voyage.route);
        }
    } else {
        for (unsigned i = 0; i < 2; ++i)
            boat_session_rebind_voyage(sessions[i], &next->voyage.route);
    }
    objective_revision
        = change == BOAT_PLUGIN_MIGRATE && direct.session.voyage.route != &next->voyage.route
        ? objective_revision
        : next;
    CHECK(wake_local_commit(&game, &play.session, &play.objects, next, change, moved));
    return 0;
}

// Every commit kind lands between the same ticks as on the direct path: a rebind, a live route
// edit that moves the reached shelter's index, a migration that fails (nothing changes; the host
// hears why) and one that succeeds (a new generation from the migrated state), and a voyage
// restart onto an ordered tour that moves the boat to its start; the voyage, storm, boats and
// objective stay equal after every frame.
static int objective_commits(void)
{
    objective_revision = objective_test(false);
    memset(revisions, 0, sizeof(revisions));
    revisions[0] = objective_test_revision;
    objective_revision = &revisions[0];
    game.resolve = resolve;
    int failed = open_voyage(OUTER, false);
    revisions[0].voyage.route = test_route;
    failed = failed || voyage_until(ahead, past_ahead, 2000) || drive(ahead, 20);
    if (!failed) {
        CHECK(game.voyage.revision == &revisions[0] && game.objective.revision == 1);
        // A rebind: the same route in revision 2's storage.
        if (commit_both(revision_from(2, &revisions[0], NULL, NULL), BOAT_PLUGIN_RENAMED)
            || drive(ahead, 15))
            failed = 1;
        CHECK(!failed && game.objective.revision == 2 && lp->server.stats.commits == 1);
        // A live edit: a new shelter before AHEAD, so the reached one moves from index 1 to 2.
        BoatVoyageRoute live = test_route;
        memmove(&live.shelters[2], &live.shelters[1], 3 * sizeof(live.shelters[0]));
        live.shelters[1] = (BoatVoyageShelter) { "EXTRA",
            Vector2Add(test_route.shelters[0].position, (Vector2) { 200, 200 }),
            test_route.shelters[0].heading, 10, NULL };
        live.shelter_count = 5;
        BoatPluginRevision *r3 = revision_from(3, &revisions[1], &live, NULL);
        static const char *const ids[] = { "start", "extra", "ahead", "near_storm", "end" };
        for (unsigned i = 0; i < 5; ++i)
            snprintf(r3->voyage.shelter_ids[i], sizeof(r3->voyage.shelter_ids[i]), "%s", ids[i]);
        if (commit_both(r3, BOAT_PLUGIN_LIVE) || drive(ahead, 15))
            failed = 1;
        CHECK(!failed && play.session.voyage.shelter == 2 && game.voyage.voyage.shelter == 2);
        // A migration that fails: everything stays, and the host hears why a frame later.
        if (commit_both(
                revision_from(4, r3, NULL, "p.migrate=function(c,s) error('no migrate') end"),
                BOAT_PLUGIN_MIGRATE)
            || drive(ahead, 1))
            failed = 1;
        CHECK(!failed && game.objective.commit == 4
            && game.objective.commit_status == WAKE_COMMIT_FAILED
            && strstr(game.objective.message, "no migrate") && game.objective.revision == 3
            && game.voyage.revision == r3 && reference.generation == 1
            && game.objective.generation == 1);
        failed = drive(ahead, 10);
        // A migration that succeeds: generation 2 from the migrated state.
        BoatPluginRevision *r5 = revision_from(5, r3, NULL,
            "p.migrate=function(c,s) s.reached=s.reached+100; return s, "
            "{{kind='hud.set', generation=c.generation, id='reached', label='REACHED',"
            " value=s.reached}} end");
        route_lag = true;
        if (failed || commit_both(r5, BOAT_PLUGIN_MIGRATE) || drive(ahead, 1))
            failed = 1;
        route_lag = false;
        CHECK(!failed && game.objective.commit == 5
            && game.objective.commit_status == WAKE_COMMIT_APPLIED && game.objective.revision == 5
            && game.objective.generation == 2);
        boat_session_rebind_voyage(&play.session, &r5->voyage.route); // The host commits now.
        if (drive(ahead, 15))
            failed = 1;
        CHECK(!failed && game.objective.hud[0].value >= 101);
        // A voyage restart onto an ordered tour: the boat goes back to its start, generation 3.
        BoatVoyageRoute tour = live;
        tour.ordered = true;
        if (commit_both(revision_from(6, r5, &tour, NULL), BOAT_PLUGIN_RESTART_VOYAGE)
            || drive(ahead, 1))
            failed = 1;
        CHECK(!failed && game.objective.generation == 3 && play.session.voyage.shelter == 0
            && lp->server.stats.commits == 5 && !lp->server.stats.commits_ignored);
        // The tour's next stop (EXTRA) lies off the line: the drive passes AHEAD out of order.
        failed = drive(ahead, 300);
    }
    printf("commits: %" PRIu64 " applied, generation %u, HUD reached %lld\n",
        lp->server.stats.commits, game.objective.generation,
        (long long)game.objective.hud[0].value);
    objective_revision = NULL;
    game.revision = NULL;
    game.resolve = NULL;
    return failed;
}

// The voyage codec round-trips and rejects malformed bytes; the recover request is private.
static int voyage_state(void)
{
    route_along(&(Boat) { .position = { 0, 0 } }, false);
    BoatVoyage v = { .route = &test_route,
        .shelter = 2,
        .exposed = true,
        .weather = true,
        .observed = true,
        .weather_started = 3.5,
        .started = 1,
        .recoveries = 7,
        .event = BOAT_VOYAGE_EXPOSED,
        .event_shelter = 2,
        .event_time = 9.25 };
    uint8_t bytes[BOAT_NET_PRIVATE_STATE_BYTES];
    size_t size = wake_voyage_encode(&v, bytes);
    CHECK(size == WAKE_VOYAGE_STATE_BYTES);
    BoatVoyage back = { .route = &test_route };
    CHECK(wake_voyage_decode(bytes, size, &back));
    uint8_t again[WAKE_VOYAGE_STATE_BYTES];
    CHECK(wake_voyage_encode(&back, again) == size && !memcmp(bytes, again, size));
    BoatStorm storm;
    CHECK(wake_voyage_weather((void *)&test_route, bytes, size, &storm));
    CHECK(storm.radius == 60 && storm.started == 3.5);
    CHECK(!wake_voyage_decode(bytes, size - 1, &back));
    bytes[0] = 4; // Outside the route's shelters.
    CHECK(!wake_voyage_decode(bytes, size, &back) && back.shelter == 2);
    BoatVoyage none = { 0 };
    CHECK(!wake_voyage_decode(again, size, &none));
    // The recover request: private, header only, never v14 or an input batch.
    BoatNetHeader header = { .epoch = 3, .sequence = 4, .tick = 48 }, read;
    uint8_t message[BOAT_NET_MAX_MESSAGE];
    size_t length = boat_net_private_encode_recover(&header, message);
    CHECK(length == BOAT_NET_HEADER_BYTES && boat_net_private_message(message, length));
    CHECK(boat_net_private_type(message, length) == BOAT_NET_PRIVATE_RECOVER);
    static BoatNetMessage decoded;
    CHECK(boat_net_decode(message, length, &decoded) == BOAT_NET_BAD_TYPE);
    BoatNetInputBatch batch;
    CHECK(boat_net_private_decode_input(message, length, &read, &batch) == BOAT_NET_BAD_TYPE);
    CHECK(boat_net_private_decode_recover(message, length, &read) == BOAT_NET_OK);
    CHECK(read.tick == 48 && read.epoch == 3);
    CHECK(boat_net_private_decode_recover(message, length - 1, &read) != BOAT_NET_OK);
    // The commit request (L1b): private, revision and change, never v14, input or recover.
    length = boat_net_private_encode_commit(&header, 7, WAKE_VOYAGE_LIVE, message);
    CHECK(length == BOAT_NET_HEADER_BYTES + BOAT_NET_PRIVATE_COMMIT_BYTES);
    CHECK(boat_net_private_type(message, length) == BOAT_NET_PRIVATE_COMMIT);
    CHECK(boat_net_decode(message, length, &decoded) == BOAT_NET_BAD_TYPE);
    CHECK(boat_net_private_decode_recover(message, length, &read) == BOAT_NET_BAD_TYPE);
    uint32_t revision = 0;
    uint8_t change = 0;
    CHECK(
        boat_net_private_decode_commit(message, length, &read, &revision, &change) == BOAT_NET_OK);
    CHECK(revision == 7 && change == WAKE_VOYAGE_LIVE && read.tick == 48);
    CHECK(boat_net_private_decode_commit(message, length - 1, &read, &revision, &change)
        != BOAT_NET_OK);
    // The objective tail after the voyage's bytes.
    static WakeVoyageAuthority authority;
    static BoatPluginRevision tail_revision;
    tail_revision.number = 3;
    authority.voyage = v;
    authority.revision = &tail_revision;
    authority.objective.generation = 9;
    authority.commit = 3;
    authority.commit_status = WAKE_COMMIT_FAILED;
    snprintf(authority.commit_message, sizeof(authority.commit_message), "bad migrate");
    BoatNetRules rules = wake_voyage_rules(&authority);
    size = rules.private_state(&authority, bytes);
    CHECK(size == WAKE_VOYAGE_STATE_BYTES + WAKE_VOYAGE_OBJECTIVE_BYTES + 11);
    WakeObjectiveView view = { 0 };
    BoatNetObjectiveState hud = { .row_count = 1, .rows = { { "a", "ROW", 4 } } };
    CHECK(wake_voyage_objective_view(bytes, size, &hud, &view));
    CHECK(view.valid && view.generation == 9 && view.revision == 3 && view.commit == 3
        && view.commit_status == WAKE_COMMIT_FAILED && !strcmp(view.message, "bad migrate")
        && view.hud_count == 1 && view.hud[0].value == 4);
    CHECK(wake_voyage_decode(bytes, size, &back)); // The voyage reads its own bytes.
    CHECK(!wake_voyage_objective_view(bytes, size - 1, &hud, &view));
    CHECK(!wake_voyage_objective_view(bytes, WAKE_VOYAGE_STATE_BYTES, &hud, &view));
    return 0;
}

// A recover request for a tick other than the authority's is ignored, and a free-boating
// authority without a voyage adapter ignores every request.
static int recover_ignored(void)
{
    if (open_world(OUTER) || drive(one, 10))
        return 1;
    CHECK(boat_local_recover(lp, play.session.boat));
    if (drive(one, 1))
        return 1;
    // The prediction moved; the authority did not, so the frame's snapshot corrected it.
    CHECK(lp->server.stats.recoveries == 0 && lp->server.stats.recoveries_ignored == 1);
    if (open_voyage(OUTER, false) || drive(ahead, 10))
        return 1;
    CHECK(!boat_net_client_private_recover(&lp->client, lp->tick + 1, play.session.boat));
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
        SLOW_CASE(course_restart_mid),
        SLOW_CASE(course_practice_mid),
        SLOW_CASE(course_saved_ghost),
        TEST_CASE(course_state),
        TEST_CASE(private_state_rejected),
        SLOW_CASE(voyage_trace),
        SLOW_CASE(tour_trace),
        SLOW_CASE(objective_trace),
        SLOW_CASE(objective_quarantine),
        SLOW_CASE(objective_commits),
        TEST_CASE(voyage_state),
        TEST_CASE(recover_ignored),
    };
    int result = test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
    boat_local_stop(lp);
    test_worlds_close();
    return result;
}
