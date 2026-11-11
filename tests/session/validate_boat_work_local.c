// L3: the working harbour on its private local endpoint. The same frames drive a direct
// BoatWorkSession (the game's former path: boat_work_apply_frame, then each tick's helm and
// boat_work_session_tick) and a BoatWorkLocal. After every frame the authority's session equals
// the direct one (save bytes, objective digest, presented state and trail) and the client's mirror
// equals it in everything the game presents; each tick's presentation steps equally from the
// mirror. Fixture scripts and resumes are host management followed by a restart. Real bundled
// outer work package, no window, files or wall time.
#include "boat_location.h"
#include "boat_net_private.h"
#include "boat_plugin_catalog.h"
#include "boat_presentation.h"
#include "boat_test_cases.h"
#include "boat_work_local.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "work local check failed at %d: %s\n", __LINE__, #condition);          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static BoatWorldResources resources;
static BoatPluginRevision revision;
static bool ready;

static void setup(BoatWorkSession *e)
{
    if (!ready) {
        CHECK(boat_world_resources_init(&resources, boat_location(BOAT_LOCATION_OUTER)));
        const BoatPluginCatalogEntry *entry = boat_plugin_catalog_work_default("outer");
        CHECK(entry);
        BoatPluginDiagnostic diagnostic = { 0 };
        CHECK(boat_plugin_catalog_decode(entry, &revision, &diagnostic));
        CHECK(revision.free.has_scene && revision.free.scene.work_present);
        ready = true;
    }
    memset(e, 0, sizeof(*e));
    e->resources = &resources;
    e->writer = "boat_work_local";
    e->world.scene = &revision.free.scene;
    e->revision = revision.free.manifest.objective ? &revision : NULL;
    CHECK(e->revision);
    e->site_count = revision.free.scene.work_count;
    memcpy(e->sites, revision.free.scene.work, sizeof(e->sites));
    boat_work_session_reset(e);
}

static BoatWorkSession direct;
static BoatPresentation direct_presentation, local_presentation;
static BoatWorkLocal local;
static double now;
static uint64_t observed;

static void observe(void *context, const BoatWorkSession *view)
{
    (void)context;
    ++observed;
    boat_presentation_step(
        &local_presentation, &view->boat, &view->world, boat_tick_seconds(view->tick));
}

static const BoatWorkObserver observer = { NULL, observe };

static bool same_bytes(size_t (*encode)(const BoatWorkSession *, uint8_t *),
    const BoatWorkSession *a, const BoatWorkSession *b)
{
    uint8_t x[BOAT_NET_MAX_PAYLOAD], y[BOAT_NET_MAX_PAYLOAD];
    size_t n = encode(a, x), m = encode(b, y);
    return n && n == m && !memcmp(x, y, n);
}

static size_t motion(const BoatWorkSession *e, uint8_t *out)
{
    return boat_work_encode_motion(e, 0, true, out);
}

static bool same_trail(const BoatWorkSession *a, const BoatWorkSession *b)
{
    const BoatWater *x = &a->world.water, *y = &b->world.water;
    if (x->player_wake_next != y->player_wake_next || x->player_wake_count != y->player_wake_count)
        return false;
    for (unsigned i = 0; i < BOAT_WAKE_SEGMENTS; ++i) { // Field by field: the struct has padding.
        const BoatWakeSegment *p = &x->player_wake[i], *q = &y->player_wake[i];
        if (memcmp(&p->position, &q->position, sizeof(p->position))
            || memcmp(&p->direction, &q->direction, sizeof(p->direction))
            || memcmp(&p->emitted, &q->emitted, sizeof(p->emitted)) || p->length != q->length
            || p->amplitude != q->amplitude || p->inverse_speed != q->inverse_speed)
            return false;
    }
    return true;
}

// Everything the game presents: motion (boat, crate, cranes, clock, view flags), trail and
// status (HUD, notice, waiting), plus the tick.
static bool same_presented(const BoatWorkSession *a, const BoatWorkSession *b)
{
    return a->tick == b->tick && same_bytes(motion, a, b) && same_trail(a, b)
        && same_bytes(boat_work_encode_status, a, b);
}

static size_t save_bytes(const BoatWorkSession *e, uint8_t *out, size_t capacity)
{
    static BoatWorkSession copy; // Saving queues events; keep the session untouched.
    copy = *e;
    BoatSandboxSave save;
    size_t size = 0;
    CHECK(boat_work_session_save(&copy, 0, &save));
    CHECK(boat_sandbox_save_encode(&save, out, capacity, &size));
    return size;
}

static bool same_authority(const BoatWorkSession *a, const BoatWorkSession *b)
{
    static uint8_t x[BOAT_SANDBOX_SAVE_MAX], y[BOAT_SANDBOX_SAVE_MAX];
    size_t n = save_bytes(a, x, sizeof(x)), m = save_bytes(b, y, sizeof(y));
    return same_presented(a, b) && n == m && !memcmp(x, y, n)
        && boat_plugin_objective_digest(&a->objective)
        == boat_plugin_objective_digest(&b->objective)
        && a->event_count == b->event_count && a->sequence == b->sequence
        && a->generation == b->generation;
}

static void check_equal(void)
{
    CHECK(same_authority(boat_work_local_authority(&local), &direct));
    CHECK(same_presented(boat_work_local_view(&local), &direct));
    CHECK(!memcmp(&local_presentation, &direct_presentation, sizeof(BoatPresentation)));
}

static void start(void)
{
    setup(&direct);
    boat_presentation_reset(&direct_presentation);
    boat_presentation_reset(&local_presentation);
    now = 0;
    observed = 0;
    local = (BoatWorkLocal) { 0 };
    CHECK(boat_work_local_start(&local, &direct, 1, now));
    check_equal();
}

// One frame on both paths.
static void frame(BoatWorkFrame f, unsigned grant)
{
    float slew, reach, hoist;
    boat_work_apply_frame(&direct, &f, &slew, &reach, &hoist);
    for (unsigned i = 0; i < grant; ++i) {
        boat_work_session_tick(&direct, slew, reach, hoist, boat_work_helm(&direct, &f));
        boat_presentation_step(
            &direct_presentation, &direct.boat, &direct.world, boat_tick_seconds(direct.tick));
    }
    uint64_t before = observed;
    now += 1.0 / 60;
    if (!boat_work_local_frame(&local, now, grant, &f, &observer)) {
        fprintf(stderr, "frame failed: %s\n", local.failure);
        exit(1);
    }
    CHECK(observed - before == grant);
    check_equal();
}

static BoatWorkFrame keys(unsigned actions)
{
    return (BoatWorkFrame) { .actions = actions,
        .active = true,
        .steering_target = NAN,
        .steering_rate = 0,
        .thrust_target = 0 };
}

static void frames(BoatWorkFrame f, unsigned count, unsigned grant)
{
    for (unsigned i = 0; i < count; ++i) {
        frame(f, grant);
        f.actions = 0; // Presses last one frame; held keys are re-sent by the caller.
    }
}

// The crane work at the home berth, then the helm: every action, both steering inputs, the brake,
// inactive frames and grants from 0 to 96.
static void play(void)
{
    frames(keys(0), 5, 2);
    frames(keys(BOAT_WORK_HOOK), 1, 2);
    BoatWorkFrame crane = keys(0);
    crane.hoist = 1;
    frames(crane, 30, 2);
    crane.hoist = 0;
    crane.slew = .7f;
    crane.reach = .5f;
    frames(crane, 40, 16);
    BoatWorkFrame aim = keys(0);
    aim.aim = 2;
    frames(aim, 1, 12);
    frames(keys(0), 60, 12);
    crane = keys(0);
    crane.hoist = -.5f;
    frames(crane, 60, 12);
    frames(keys(BOAT_WORK_HOOK), 1, 0);
    frames(keys(0), 40, 12);
    frames(keys(BOAT_WORK_LASH), 1, 1);
    frames(keys(BOAT_WORK_VIEW), 1, 2);
    frames(keys(BOAT_WORK_MOOR), 1, 2);
    frames(keys(0), 30, 7);
    BoatWorkFrame helm = keys(BOAT_WORK_NEXT_TARGET);
    helm.thrust_target = 1;
    helm.steering_rate = .3f;
    frames(helm, 120, 2);
    helm.steering_target = .5f;
    frames(helm, 60, 7);
    BoatWorkFrame brake = keys(BOAT_WORK_BRAKE);
    for (unsigned i = 0; i < 20; ++i)
        frame(brake, 3);
    BoatWorkFrame idle = { .steering_target = NAN }; // A menu has the keys.
    frames(idle, 10, 0);
    helm.steering_target = -.2f;
    frames(helm, 10, 96);
    BoatWorkFrame demo = helm;
    demo.demo = true;
    frames(demo, 10, 13);
    frames((BoatWorkFrame) { .actions = BOAT_WORK_RETURN_CRATE, .steering_target = NAN }, 1, 2);
    frames(keys(BOAT_WORK_VIEW), 1, 12);
    frames(keys(0), 20, 12);
}

static int trace(void)
{
    start();
    play();
    CHECK(direct.tick > 3000 && local.stats.ticks == direct.tick);
    CHECK(direct.world.water.player_wake_count > 0); // The trail travelled in motions.
    CHECK(local.stats.statuses > 1 && local.stats.restarts == 0);
    boat_work_local_stop(&local);
    return 0;
}

// Host management: a fixture script on both sessions between frames, then a restart; the
// checkpoint brings the client's mirror to the scripted state.
static int restart(void)
{
    start();
    frames(keys(0), 10, 12);
    CHECK(boat_work_script_landing_delivery(&direct));
    CHECK(boat_work_script_landing_delivery(boat_work_local_authority(&local)));
    uint32_t generation = local.generation;
    CHECK(boat_work_local_restart(&local, now));
    CHECK(local.generation == generation + 1 && local.stats.restarts == 1);
    check_equal();
    CHECK(boat_work_local_view(&local)->objective.hud_count > 0);
    frames(keys(0), 30, 12);
    // A reset is host management too.
    boat_work_session_reset(&direct);
    boat_work_session_reset(boat_work_local_authority(&local));
    CHECK(boat_work_local_restart(&local, now));
    check_equal();
    frames(keys(BOAT_WORK_HOOK), 10, 12);
    boat_work_local_stop(&local);
    return 0;
}

// A resumed save: the authority starts from the resumed session; the mirror matches at once.
static int resume(void)
{
    start();
    frames(keys(0), 10, 12);
    CHECK(boat_work_script_landing_delivery(&direct));
    static uint8_t bytes[BOAT_SANDBOX_SAVE_MAX];
    size_t size = save_bytes(&direct, bytes, sizeof(bytes));
    boat_work_local_stop(&local);
    setup(&direct);
    const char *reason = NULL;
    CHECK(boat_work_session_load(&direct, bytes, size, &reason, NULL));
    boat_presentation_reset(&direct_presentation);
    boat_presentation_reset(&local_presentation);
    local = (BoatWorkLocal) { 0 };
    CHECK(boat_work_local_start(&local, &direct, 7, now));
    check_equal();
    CHECK(direct.tick > 0 && boat_work_local_view(&local)->tick == direct.tick);
    frames(keys(BOAT_WORK_VIEW), 1, 12);
    frames(keys(0), 50, 12);
    boat_work_local_stop(&local);
    return 0;
}

static int codec(void)
{
    BoatWorkFrame f = { .actions = BOAT_WORK_HOOK | BOAT_WORK_BRAKE,
        .aim = 3,
        .active = true,
        .slew = -1,
        .reach = .5f,
        .hoist = 1,
        .steering_target = NAN,
        .steering_rate = .25f,
        .thrust_target = -.5f };
    uint8_t bytes[BOAT_NET_MAX_PAYLOAD];
    size_t n = boat_work_encode_frame(&f, bytes);
    BoatWorkFrame g;
    CHECK(n && boat_work_decode_frame(bytes, n, &g));
    CHECK(g.actions == f.actions && g.aim == 3 && g.active && !g.demo && g.slew == -1
        && isnan(g.steering_target) && g.steering_rate == .25f && g.thrust_target == -.5f);
    CHECK(!boat_work_decode_frame(bytes, n - 1, &g));
    uint8_t bad[BOAT_NET_MAX_PAYLOAD];
    memcpy(bad, bytes, n);
    bad[0] = 0x80; // An unknown action.
    CHECK(!boat_work_decode_frame(bad, n, &g));
    memcpy(bad, bytes, n);
    bad[1] = 4; // No fourth aim.
    CHECK(!boat_work_decode_frame(bad, n, &g));
    float infinite = INFINITY;
    memcpy(bad, bytes, n);
    uint32_t bits;
    memcpy(&bits, &infinite, sizeof(bits));
    for (int i = 0; i < 4; ++i)
        bad[4 + i] = (uint8_t)(bits >> (24 - 8 * i)); // A non-finite slew.
    CHECK(!boat_work_decode_frame(bad, n, &g));

    static BoatWorkSession a, b;
    setup(&a);
    setup(&b);
    b.boat.position.x += 3;
    b.notice = NULL;
    n = boat_work_encode_motion(&a, 0, true, bytes);
    CHECK(n && boat_work_decode_motion(bytes, n, 9, &b) && b.tick == 9);
    CHECK(b.boat.position.x == a.boat.position.x);
    CHECK(!boat_work_decode_motion(bytes, n - 1, 9, &b));
    char notice[BOAT_WORK_LOCAL_TEXT], waiting[BOAT_WORK_LOCAL_TEXT];
    n = boat_work_encode_status(&a, bytes);
    CHECK(n && boat_work_decode_status(bytes, n, &b, notice, waiting));
    CHECK(!strcmp(b.notice, a.notice) && b.waiting == NULL);
    CHECK(!boat_work_decode_status(bytes, n + 1, &b, notice, waiting));
    // Segments appended across the trail's end arrive at their own indices.
    a.world.water.player_wake_next = 2;
    a.world.water.player_wake_count = BOAT_WAKE_SEGMENTS;
    for (unsigned i = 0; i < BOAT_WAKE_SEGMENTS; ++i)
        a.world.water.player_wake[i] = (BoatWakeSegment) { .length = (float)i + 1, .emitted = i };
    n = boat_work_encode_motion(&a, 62, false, bytes);
    CHECK(n && boat_work_decode_motion(bytes, n, 10, &b));
    CHECK(b.world.water.player_wake_next == 2 && b.world.water.player_wake_count == 64);
    CHECK(b.world.water.player_wake[62].length == 63 && b.world.water.player_wake[63].length == 64
        && b.world.water.player_wake[0].length == 1 && b.world.water.player_wake[1].length == 2
        && b.world.water.player_wake[2].length != 3);
    n = boat_work_encode_trail(&a, 48, bytes);
    CHECK(n && boat_work_decode_trail(bytes, n, &b));
    CHECK(!boat_work_encode_trail(&a, BOAT_WAKE_SEGMENTS, bytes));
    // The work messages lie in the private range: a hosted decoder refuses them.
    BoatNetHeader header = { .epoch = 1 };
    uint8_t message[BOAT_NET_MAX_MESSAGE];
    n = boat_net_private_encode_payload(&header, BOAT_NET_PRIVATE_WORK_FRAME, bytes, 10, message);
    CHECK(n && boat_net_private_message(message, n));
    CHECK(boat_net_private_type(message, n) == BOAT_NET_PRIVATE_WORK_FRAME);
    CHECK(!boat_net_private_encode_payload(&header, 0x20, bytes, 10, message));
    size_t size;
    CHECK(boat_net_private_decode_payload(
              message, n, BOAT_NET_PRIVATE_WORK_STATUS, &header, bytes, &size)
        != BOAT_NET_OK);
    return 0;
}

// An endpoint failure stops the session with a reason (D10), and a grant above the bound fails.
static int failures(void)
{
    start();
    BoatWorkFrame f = keys(0);
    CHECK(!boat_work_local_frame(&local, now, BOAT_WORK_LOCAL_MAX_GRANT + 1, &f, NULL));
    CHECK(strstr(local.failure, "grant"));
    boat_work_local_stop(&local);
    CHECK(!boat_work_local_frame(&local, now, 1, &f, NULL));
    local = (BoatWorkLocal) { 0 };
    CHECK(!boat_work_local_start(&local, &direct, 0, now) && local.failure[0]);
    return 0;
}

int main(int argc, char **argv)
{
    static const TestCase cases[] = {
        TEST_CASE(codec),
        TEST_CASE(failures),
        SLOW_CASE(trace),
        SLOW_CASE(restart),
        SLOW_CASE(resume),
    };
    return test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
}
