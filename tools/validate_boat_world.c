/* W3 basin slice: explicit worlds and sessions stay independent. S2 basin slice: presentation
 * follows physics without feeding it, at any display rate. S4 basin slice: the session's
 * physical step is the core-only order validate_boat_core uses. W4 extends this file with
 * map/cache lifecycle checks. */
#include "boat_map.h"
#include "boat_test_maps.h"
#include "boat_test_session.h"
#include "boat_view.h"
#include "raymath.h"
#include "wake_velocity_state.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

enum { TICKS = 2400, SESSIONS = 3 };

// Powered loop through the boat's own wake; the same inputs for every world.
static void loop_inputs(const Boat *boat, int tick, float *wheel_change, float *throttle_change)
{
    float throttle = tick < 1440 ? 1 : 0;
    float wheel = tick >= 420 && tick < 1440 ? 1 : 0;
    *wheel_change = Clamp((wheel - boat->wheel) / BOAT_STEP, -1, 1);
    *throttle_change
        = Clamp((throttle - boat->throttle) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP), -1, 1);
}

static void drive(BoatSession *session, int tick)
{
    float wheel, throttle;
    loop_inputs(&session->boat, tick, &wheel, &throttle);
    boat_session_step(session, (BoatInput) { wheel, throttle });
}

static void start(BoatSession *session, int index)
{
    const BoatWorldDefinition *basin = boat_location(BOAT_LOCATION_BASIN);
    BoatWater calm = { .player_wake_enabled = true };
    BoatWater waves = boat_world_test_water(basin, true, true, false);
    waves.player_wake_enabled = true;
    BoatWater mapped = boat_water_gentle();
    if (index == 0)
        boat_session_init(session, test_world(BOAT_LOCATION_BASIN), calm);
    else if (index == 1)
        boat_session_init(session, test_world(BOAT_LOCATION_BASIN), waves);
    else
        boat_session_init(session, test_world(BOAT_LOCATION_RIVER), mapped);
}

static bool same_physics(const BoatSession *a, const BoatSession *b)
{
    return a->tick == b->tick && a->world.definition == b->world.definition
        && memcmp(&a->boat, &b->boat, sizeof(a->boat)) == 0
        && memcmp(&a->world.water, &b->world.water, sizeof(a->world.water)) == 0;
}

static bool same(const BoatSession *a, const BoatSession *b)
{
    return same_physics(a, b)
        && memcmp(&a->presentation, &b->presentation, sizeof(a->presentation)) == 0;
}

static const BoatPresentation rest;

static int live_particles(const BoatPresentation *presentation)
{
    int count = 0;
    for (int i = 0; i < BOAT_WATER_PARTICLES; ++i)
        count += presentation->water[i].lifetime > 0;
    return count;
}

// Solo references, then the same sessions stepped interleaved in one process.
static int interleaved(void)
{
    static BoatSession solo[SESSIONS], live[SESSIONS];
    for (int i = 0; i < SESSIONS; ++i) {
        start(&solo[i], i);
        for (int tick = 0; tick < TICKS; ++tick)
            drive(&solo[i], tick);
        start(&live[i], i);
    }
    for (int tick = 0; tick < TICKS; ++tick)
        for (int i = SESSIONS - 1; i >= 0; --i)
            drive(&live[i], tick);
    for (int i = 0; i < SESSIONS; ++i)
        CHECK(same(&solo[i], &live[i]));
    // The worlds really differ: otherwise equality would prove nothing about isolation.
    CHECK(live[0].world.water.player_wake_count > 0 && live[1].world.water.player_wake_count > 0);
    CHECK(Vector2Distance(live[0].boat.position, live[1].boat.position) > 1);
    CHECK(live[2].world.definition->mapped && !live[0].world.definition->mapped);
    printf("interleaved=ok sessions=basin-calm-d3,basin-waves-current-d3,river-waves "
           "ticks=%d d3=%d,%d\n",
        TICKS, live[0].world.water.player_wake_count, live[1].world.water.player_wake_count);
    return 0;
}

// Assignment clones a session; the clone's wake history and clock never alias the original.
static int clones(void)
{
    static BoatSession original, clone, reference;
    start(&original, 1);
    for (int tick = 0; tick < 1200; ++tick)
        drive(&original, tick);
    clone = original;
    reference = original;
    for (int tick = 1200; tick < 1800; ++tick) {
        drive(&original, tick);
        boat_session_step(&clone, (BoatInput) { -1, -1 });
    }
    for (int tick = 1200; tick < 1800; ++tick)
        drive(&reference, tick);
    CHECK(same(&original, &reference));
    CHECK(!same(&original, &clone) && clone.tick == original.tick);
    CHECK(original.world.water.player_wake_count != clone.world.water.player_wake_count
        || Vector2Distance(original.boat.position, clone.boat.position) > 1);
    puts("clone=ok");
    return 0;
}

// Views and respawn read or reset only what they own; drawing twice gives the same frame.
static int views(void)
{
    static BoatSession session, before;
    start(&session, 1);
    for (int tick = 0; tick < 900; ++tick)
        drive(&session, tick);
    before = session;
    static WaterfrontSceneFrame frame;
    Renderer3D renderer = { 0 };
    BoatCpuView cpu_view = { 0 };
    Color *pixels = malloc(320 * 200 * sizeof(*pixels));
    Color *again = malloc(320 * 200 * sizeof(*again));
    CHECK(pixels && again);
    boat_scene_frame(
        SESSION_VIEW(&session), &session.presentation, &session.world, BOAT_CAMERA_DEFAULT, &frame);
    CHECK(boat_draw(&renderer, &cpu_view, pixels, 320, 200, SESSION_VIEW(&session),
        &session.presentation, &session.world, BOAT_CAMERA_DEFAULT));
    boat_scene_frame(
        SESSION_VIEW(&session), &session.presentation, &session.world, BOAT_CAMERA_DEFAULT, &frame);
    CHECK(boat_draw(&renderer, &cpu_view, again, 320, 200, SESSION_VIEW(&session),
        &session.presentation, &session.world, BOAT_CAMERA_DEFAULT));
    CHECK(same(&session, &before));
    CHECK(memcmp(pixels, again, 320 * 200 * sizeof(*pixels)) == 0);
    CHECK(frame.time == boat_session_time(&session));
    CHECK(frame.player.particle_count == live_particles(&session.presentation));
    boat_session_respawn(&session);
    CHECK(session.tick == before.tick && boat_session_time(&session) == boat_session_time(&before));
    CHECK(memcmp(&session.world.water, &before.world.water, sizeof(session.world.water)) == 0);
    CHECK(Vector2Equals(session.boat.position, session.world.definition->spawn));
    CHECK(memcmp(&session.presentation, &rest, sizeof(rest)) == 0);
    boat_cpu_view_destroy(&cpu_view);
    renderer3d_destroy(&renderer);
    free(pixels);
    free(again);
    puts("views=ok draw/frame twice unchanged respawn keeps clock+water, resets presentation");
    return 0;
}

// Disabling presentation leaves physics, D3 history and arcade wakes identical; the disabled
// presentation never advances.
static int presentation_toggle(void)
{
    static BoatSession on, off;
    for (int i = 0; i < SESSIONS; ++i) {
        start(&on, i);
        start(&off, i);
        off.presentation_enabled = false;
        int peak = 0;
        for (int tick = 0; tick < TICKS; ++tick) {
            drive(&on, tick);
            drive(&off, tick);
            CHECK(same_physics(&on, &off));
            int live = live_particles(&on.presentation);
            peak = live > peak ? live : peak;
        }
        CHECK(peak > 0 && on.presentation.flag.initialized);
        CHECK(memcmp(&off.presentation, &rest, sizeof(rest)) == 0);
    }
    // The default arcade wake loop with the scripted crossing boat and its own cloth.
    const BoatWorldResources *basin = test_world(BOAT_LOCATION_BASIN);
    static ObjectStudy objects_on, objects_off;
    boat_session_init(&on, basin, boat_water_test(false, false, false));
    boat_session_init(&off, basin, boat_water_test(false, false, false));
    off.presentation_enabled = false;
    object_study_reset(&objects_on, &on, true, BOAT_OBJECT_LIFE_TICKS, 1.8f);
    object_study_reset(&objects_off, &off, true, BOAT_OBJECT_LIFE_TICKS, 1.8f);
    for (int tick = 0; tick < 3840; ++tick) {
        demo_step(&on, tick, NULL, NULL, &objects_on);
        demo_step(&off, tick, NULL, NULL, &objects_off);
        CHECK(wake_velocity_state_hash(WAKE_VELOCITY_HASH_SEED, &on.boat, boat_session_time(&on),
                  object_study_wakes(&objects_on))
            == wake_velocity_state_hash(WAKE_VELOCITY_HASH_SEED, &off.boat, boat_session_time(&off),
                object_study_wakes(&objects_off)));
        CHECK(
            memcmp(object_study_other(&objects_on), object_study_other(&objects_off), sizeof(Boat))
            == 0);
    }
    CHECK(object_study_wakes(&objects_on)->emitted > 0
        && objects_on.other_presentation.flag.initialized);
    CHECK(memcmp(&off.presentation, &rest, sizeof(rest)) == 0);
    CHECK(memcmp(&objects_off.other_presentation, &rest, sizeof(rest)) == 0);
    printf("presentation_toggle=ok sessions=%d+crossing ticks=%d,3840 births=%llu\n", SESSIONS,
        TICKS, (unsigned long long)object_study_wakes(&objects_on)->emitted);
    return 0;
}

// With presentation off, a course session steps exactly as the core-only order: boat_step at
// the new tick, then the gate sweep from the pre-step position.
static int core_order(void)
{
    static BoatSession session;
    start(&session, 1);
    boat_session_start_course(&session);
    session.presentation_enabled = false;
    BoatWorld world = session.world;
    Boat boat = session.boat;
    BoatCourse course = session.course;
    uint64_t tick = 0;
    for (int i = 0; i < TICKS; ++i) {
        float wheel, throttle;
        loop_inputs(&boat, i, &wheel, &throttle);
        Vector2 previous = boat.position;
        boat_step(&boat, &world, boat_tick_seconds(++tick), (BoatInput) { wheel, throttle });
        boat_course_step(&course, previous, boat.position, BOAT_TICK_SECONDS);
        drive(&session, i);
        CHECK(session.tick == tick && memcmp(&session.boat, &boat, sizeof(boat)) == 0);
        CHECK(memcmp(&session.world.water, &world.water, sizeof(world.water)) == 0);
        CHECK(memcmp(&session.course, &course, sizeof(course)) == 0);
    }
    CHECK(course.next_gate > 0 && world.water.player_wake_count > 0);
    CHECK(memcmp(&session.presentation, &rest, sizeof(rest)) == 0);
    printf("core_order=ok ticks=%d gates=%d\n", TICKS, course.next_gate);
    return 0;
}

// Fixed ticks indexed by tick number give the same session at 30, 60 and 144 Hz display rates;
// frames between ticks only read. Wall-clock keyboard sampling is outside this check.
static int render_rates(void)
{
    static BoatSession reference, shown;
    static WaterfrontSceneFrame frame;
    start(&reference, 1);
    for (int tick = 0; tick < TICKS; ++tick)
        drive(&reference, tick);
    Renderer3D renderer = { 0 };
    BoatCpuView cpu_view = { 0 };
    Color *pixels = malloc(320 * 200 * sizeof(*pixels));
    CHECK(pixels);
    const int rates[] = { 30, 60, 144 };
    for (size_t r = 0; r < sizeof(rates) / sizeof(*rates); ++r) {
        start(&shown, 1);
        double accumulator = 0;
        int frames = 0;
        while (shown.tick < TICKS) {
            accumulator += fmin(1.0 / rates[r], .1);
            while (accumulator >= BOAT_TICK_SECONDS && shown.tick < TICKS) {
                drive(&shown, (int)shown.tick);
                accumulator -= BOAT_TICK_SECONDS;
            }
            boat_scene_frame(SESSION_VIEW(&shown), &shown.presentation, &shown.world,
                BOAT_CAMERA_DEFAULT, &frame);
            if (frames++ % rates[r] == 0)
                CHECK(boat_draw(&renderer, &cpu_view, pixels, 320, 200, SESSION_VIEW(&shown),
                    &shown.presentation, &shown.world, BOAT_CAMERA_DEFAULT));
        }
        CHECK(same(&shown, &reference));
        printf("render_rate=%dHz frames=%d ok\n", rates[r], frames);
    }
    boat_cpu_view_destroy(&cpu_view);
    renderer3d_destroy(&renderer);
    free(pixels);
    return 0;
}

// A1.4: each CPU view binds its own scenery to the map it draws. Two river views and a view of
// the synthetic harbor draw interleaved; destroying one leaves the others' frames unchanged.
static int view_binding(void)
{
    const BoatWorldDefinition definition
        = test_harbor_definition(boat_location(BOAT_LOCATION_RIVER));
    BoatWorldResources harbor;
    CHECK(boat_world_resources_init(&harbor, &definition));
    BoatSession river, other;
    boat_session_init(&river, test_world(BOAT_LOCATION_RIVER), (BoatWater) { 0 });
    boat_session_init(&other, &harbor, (BoatWater) { 0 });
    Renderer3D ra = { 0 }, rb = { 0 }, rc = { 0 };
    BoatCpuView a = { 0 }, b = { 0 }, c = { 0 };
    size_t size = 320 * 200 * sizeof(Color);
    Color *first = malloc(size), *pixels = malloc(size), *harbor_pixels = malloc(size);
    CHECK(first && pixels && harbor_pixels);
    BoatCamera camera = BOAT_CAMERA_DEFAULT;
    CHECK(boat_draw(
        &ra, &a, first, 320, 200, SESSION_VIEW(&river), &river.presentation, &river.world, camera));
    CHECK(a.scenery.map == &waterfront_city_river_map && a.scenery.river);
    CHECK(boat_draw(&rc, &c, harbor_pixels, 320, 200, SESSION_VIEW(&other), &other.presentation,
        &other.world, camera));
    CHECK(c.scenery.map == &test_harbor_map && !c.scenery.river);
    CHECK(c.scenery.count > 0 && c.scenery.count < a.scenery.count);
    CHECK(memcmp(first, harbor_pixels, size) != 0);
    CHECK(boat_draw(&rb, &b, pixels, 320, 200, SESSION_VIEW(&river), &river.presentation,
        &river.world, camera));
    CHECK(memcmp(first, pixels, size) == 0 && b.scenery.boxes != a.scenery.boxes);
    boat_cpu_view_destroy(&b);
    boat_cpu_view_destroy(&b);
    CHECK(!b.scenery.ready && !b.scenery.boxes);
    CHECK(boat_draw(&ra, &a, pixels, 320, 200, SESSION_VIEW(&river), &river.presentation,
        &river.world, camera));
    CHECK(memcmp(first, pixels, size) == 0);
    // The harbor view rebinds to the river and back, reproducing both frames.
    CHECK(boat_draw(&rc, &c, pixels, 320, 200, SESSION_VIEW(&river), &river.presentation,
        &river.world, camera));
    CHECK(memcmp(first, pixels, size) == 0 && c.scenery.river);
    CHECK(boat_draw(&rc, &c, pixels, 320, 200, SESSION_VIEW(&other), &other.presentation,
        &other.world, camera));
    CHECK(memcmp(harbor_pixels, pixels, size) == 0 && !c.scenery.river);
    printf(
        "cpu_view_binding=ok harbor_boxes=%zu river_boxes=%zu\n", c.scenery.count, a.scenery.count);
    boat_cpu_view_destroy(&a);
    boat_cpu_view_destroy(&c);
    renderer3d_destroy(&ra);
    renderer3d_destroy(&rb);
    renderer3d_destroy(&rc);
    free(first);
    free(pixels);
    free(harbor_pixels);
    boat_session_destroy(&river);
    boat_session_destroy(&other);
    boat_world_resources_destroy(&harbor);
    return 0;
}

// A1.5 lifecycle: each participant owns its world resources, session, CPU view and course
// replay; a fixed script of phases yields one physics and one frame hash per phase.
typedef struct {
    const BoatWorldDefinition *definition;
    BoatWater water, restart_water;
    BoatCamera camera;
    float steer; // Mirrors the loop so the view centres differ.
    bool course;
} Script;

typedef struct {
    const Script *script;
    BoatWorldResources resources;
    BoatSession session;
    BoatCpuView view;
    BoatReplay replay; // Poses recorded here, viewed in a playback world on the same owner.
    int tick;
    uint64_t hashes[8];
    int count;
} Participant;

enum { PHASES = 4, PHASE_TICKS = 240 };

static void hash_bytes(uint64_t *hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; ++i)
        *hash = (*hash ^ bytes[i]) * UINT64_C(1099511628211);
}

static void participant_drive(Participant *p, int ticks)
{
    for (int i = 0; i < ticks; ++i, ++p->tick) {
        float wheel, throttle;
        loop_inputs(&p->session.boat, p->tick % 1800, &wheel, &throttle);
        boat_session_step(&p->session, (BoatInput) { wheel * p->script->steer, throttle });
        if (p->replay.count < p->replay.capacity) {
            BoatReplaySample sample = boat_replay_pose(
                &p->session.boat, &p->session.presentation, boat_session_time(&p->session));
            sample.at = p->replay.count * BOAT_TICK_SECONDS;
            p->replay.samples[p->replay.count++] = sample;
        }
    }
}

// Physics and frame hashes of one phase, drawn through the participant's own view.
static bool participant_record(Participant *p, Renderer3D *renderer, Color *pixels,
    const BoatViewState *view, const BoatPresentation *presentation, const BoatWorld *world,
    const BoatViewState *ghost)
{
    if (!boat_draw_with_ghost(renderer, &p->view, pixels, 320, 200, view, presentation, world,
            p->script->camera, ghost, NULL, .5f))
        return false;
    uint64_t physics = UINT64_C(1469598103934665603), frame = physics;
    const BoatSession *s = &p->session;
    hash_bytes(&physics, &s->tick, sizeof(s->tick));
    hash_bytes(&physics, &s->boat, sizeof(s->boat));
    hash_bytes(&physics, &s->world.water, sizeof(s->world.water));
    hash_bytes(&physics, &s->presentation, sizeof(s->presentation));
    hash_bytes(&physics, &s->course, sizeof(s->course));
    hash_bytes(&frame, pixels, 320 * 200 * sizeof(*pixels));
    p->hashes[p->count++] = physics;
    p->hashes[p->count++] = frame;
    return true;
}

static bool participant_start(Participant *p, const Script *script)
{
    p->script = script;
    p->tick = p->count = 0;
    if (!boat_world_resources_init(&p->resources, script->definition))
        return false;
    boat_session_init(&p->session, &p->resources, script->water);
    p->replay = (BoatReplay) { .samples = malloc(PHASE_TICKS * sizeof(BoatReplaySample)),
        .capacity = PHASE_TICKS,
        .waves = script->water.amplitude != 0 };
    return p->replay.samples;
}

// Phase 0 drives; 1 respawns and drives; 2 restarts (the basin's course too) and drives;
// 3 views the recorded run from a playback world on the same owner, with the live boat as
// its ghost.
static bool participant_phase(Participant *p, int phase, Renderer3D *renderer, Color *pixels)
{
    BoatSession *s = &p->session;
    if (phase == 1)
        boat_session_respawn(s);
    if (phase == 2 && p->script->course)
        boat_session_start_course(s);
    if (phase == 2)
        boat_session_restart(s, p->script->restart_water);
    if (phase < 3) {
        participant_drive(p, PHASE_TICKS);
        return participant_record(
            p, renderer, pixels, SESSION_VIEW(s), &s->presentation, &s->world, NULL);
    }
    BoatViewState playback, live = boat_session_view(s);
    BoatPresentation presentation;
    BoatWorld world;
    if (!boat_replay_sample(
            &p->replay, PHASE_TICKS * BOAT_TICK_SECONDS / 2, &playback, &presentation))
        return false;
    boat_world_init(&world, s->world.resources, boat_replay_water(&p->replay));
    return world.resources == &p->resources
        && participant_record(p, renderer, pixels, &playback, &presentation, &world, &live);
}

static void participant_end(Participant *p)
{
    boat_session_destroy(&p->session);
    boat_cpu_view_destroy(&p->view);
    boat_replay_destroy(&p->replay);
    boat_world_resources_destroy(&p->resources);
}

static int lifecycle(void)
{
    const BoatWorldDefinition *basin = boat_location(BOAT_LOCATION_BASIN);
    const BoatWorldDefinition *river = boat_location(BOAT_LOCATION_RIVER);
    const BoatWorldDefinition harbor = test_harbor_definition(river);
    BoatWater waves = boat_world_test_water(basin, true, true, false), gentle = boat_water_gentle();
    waves.player_wake_enabled = gentle.player_wake_enabled = true;
    const Script scripts[] = {
        { basin, waves, { 0 }, BOAT_CAMERA_DEFAULT, 1, true },
        { river, gentle, { .player_wake_enabled = true }, BOAT_CAMERA_DEFAULT, 1, false },
        { river, { .player_wake_enabled = true }, gentle, { 30, 22, true }, -1, false },
        { &harbor, gentle, gentle, { 12, 6, false }, .5f, false },
        { basin, { 0 }, waves, { 40, 30, true }, -.5f, true },
    };
    enum { SCRIPTS = sizeof(scripts) / sizeof(scripts[0]) };
    static Participant solo, slots[3];
    uint64_t reference[SCRIPTS][8];
    Renderer3D renderer = { 0 };
    Color *pixels = malloc(320 * 200 * sizeof(*pixels));
    CHECK(pixels);
    for (int i = 0; i < SCRIPTS; ++i) {
        Renderer3D own = { 0 };
        CHECK(participant_start(&solo, &scripts[i]));
        for (int phase = 0; phase < PHASES; ++phase)
            CHECK(participant_phase(&solo, phase, &own, pixels));
        memcpy(reference[i], solo.hashes, sizeof(reference[i]));
        participant_end(&solo);
        renderer3d_destroy(&own);
    }
    // The scripts really differ, and the replay frame is not the live one.
    for (int i = 0; i < SCRIPTS; ++i)
        for (int j = i + 1; j < SCRIPTS; ++j)
            CHECK(reference[i][1] != reference[j][1] && reference[i][0] != reference[j][0]);
    CHECK(reference[1][7] != reference[1][5]);

    // A zeroed or failed owner and view release nothing; a failure over a stale slot zeroes it.
    Participant *a = &slots[0], *b = &slots[1], *h = &slots[2];
    participant_end(a);
    participant_end(a);
    BoatWorldDefinition unmapped_river = *river;
    unmapped_river.mapped = false;
    a->resources = (BoatWorldResources) { river, { 0 } };
    CHECK(!boat_world_resources_init(&a->resources, &unmapped_river));
    CHECK(!boat_world_resources_ready(&a->resources) && !a->resources.definition);

    // One renderer throughout. Slot 0 reloads basin -> river A -> (A and B alive with the
    // harbor) -> A released while B and the harbor continue -> basin.
    CHECK(participant_start(a, &scripts[0]));
    CHECK(participant_phase(a, 0, &renderer, pixels) && participant_phase(a, 1, &renderer, pixels));
    CHECK(participant_phase(a, 2, &renderer, pixels) && participant_phase(a, 3, &renderer, pixels));
    CHECK(memcmp(a->hashes, reference[0], sizeof(reference[0])) == 0);
    participant_end(a);
    CHECK(participant_start(a, &scripts[1]));
    CHECK(participant_phase(a, 0, &renderer, pixels));
    CHECK(participant_start(b, &scripts[2]) && participant_start(h, &scripts[3]));
    CHECK(participant_phase(h, 0, &renderer, pixels) && participant_phase(b, 0, &renderer, pixels));
    CHECK(participant_phase(a, 1, &renderer, pixels) && participant_phase(b, 1, &renderer, pixels));
    CHECK(participant_phase(h, 1, &renderer, pixels) && participant_phase(a, 2, &renderer, pixels));
    CHECK(a->view.scenery.boxes != b->view.scenery.boxes
        && a->resources.map.shore != b->resources.map.shore);
    CHECK(participant_phase(a, 3, &renderer, pixels));
    CHECK(memcmp(a->hashes, reference[1], sizeof(reference[1])) == 0);
    participant_end(a);
    CHECK(participant_phase(b, 2, &renderer, pixels) && participant_phase(h, 2, &renderer, pixels));
    CHECK(participant_phase(b, 3, &renderer, pixels) && participant_phase(h, 3, &renderer, pixels));
    CHECK(memcmp(b->hashes, reference[2], sizeof(reference[2])) == 0);
    CHECK(memcmp(h->hashes, reference[3], sizeof(reference[3])) == 0);
    participant_end(h);
    participant_end(b);
    CHECK(participant_start(a, &scripts[4]));
    for (int phase = 0; phase < PHASES; ++phase)
        CHECK(participant_phase(a, phase, &renderer, pixels));
    CHECK(memcmp(a->hashes, reference[4], sizeof(reference[4])) == 0);
    participant_end(a);
    printf("lifecycle=ok scripts=basin,river-a,river-b,harbor,basin phases=%d "
           "river_a_frame=%016" PRIx64 "\n",
        PHASES, reference[1][1]);
    renderer3d_destroy(&renderer);
    free(pixels);
    return 0;
}

int main(void)
{
    test_worlds_open();
    SetTraceLogLevel(LOG_WARNING);
    CHECK(boat_location(BOAT_LOCATION_COUNT) == NULL && !boat_location_find("harbour"));
    for (int i = 0; i < BOAT_LOCATION_COUNT; ++i)
        CHECK(boat_location_find(boat_location(i)->name) == boat_location(i));
    CHECK(interleaved() == 0);
    CHECK(clones() == 0);
    CHECK(views() == 0);
    CHECK(presentation_toggle() == 0);
    CHECK(render_rates() == 0);
    CHECK(core_order() == 0);
    CHECK(view_binding() == 0);
    CHECK(lifecycle() == 0);
    puts("boat_world=ok");
    test_worlds_close();
    return 0;
}
