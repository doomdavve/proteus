/* AR6 (ledger L0) gates for the private local session (src/boat_local.[ch]): the authority and
 * client in one process over the memory transport, driven by host grants. Every scenario runs the
 * game's direct free-boating path (BoatSession stepped by timed_boat_step with the arcade wake
 * pool) beside a local session fed the same frames, and requires bit-identical boats and wake
 * pools at every predicted tick and at every frame's authority tick. Frame time is a scripted
 * value, never wall time. `--bench` reports per-frame cost (not a test). */
#include "boat_local.h"
#include "boat_steering.h"
#include "boat_test_cases.h"
#include "boat_test_world.h"
#include "boat_throttle.h"
#include "boat_time_control.h"
#include "wake_velocity_drive.h"

#include <inttypes.h>
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

// The direct path, as the game's free-boating branch runs it.
typedef struct {
    BoatSession session;
    ObjectStudy objects;
    double accumulator;
} Direct;

static BoatLocal local;
static Direct direct;
static World world;

// Per-tick boats and pool digests of the direct path, compared with the client's predictions.
enum { TRACE = 4096 };
typedef struct {
    uint64_t tick;
    Boat boat;
    uint64_t wakes;
} Expected;
static Expected expected[TRACE];
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

static void direct_open(void)
{
    boat_session_init(&direct.session, test_world(world.location), world_water());
    if (world.handling)
        direct.session.handling = world.handling;
    object_study_reset(&direct.objects, &direct.session, false, BOAT_OBJECT_LIFE_TICKS,
        BOAT_OBJECT_DEFAULT_HEIGHT);
    direct.accumulator = 0;
}

static void direct_restart(void)
{
    boat_session_restart(&direct.session, world_water());
    object_study_reset(&direct.objects, &direct.session, false, BOAT_OBJECT_LIFE_TICKS,
        BOAT_OBJECT_DEFAULT_HEIGHT);
    direct.accumulator = 0;
}

static BoatLocalConfig local_config(void)
{
    BoatWater water = direct.session.world.water;
    water.objects = NULL;
    return (BoatLocalConfig) { .resources = worlds(),
        .location = world.location,
        .world = { .water = water,
            .handling = world.handling,
            .wake_life_ticks = BOAT_OBJECT_LIFE_TICKS,
            .crest_height = BOAT_OBJECT_DEFAULT_HEIGHT,
            .spawn = direct.session.boat } };
}

// The game's grant: the same accumulator arithmetic as its tick loop.
static unsigned grant(double *accumulator, Frame f)
{
    if (f.paused) {
        *accumulator = 0;
        return 0;
    }
    *accumulator += boat_time_frame_seconds(f.seconds, f.speed);
    unsigned ticks = 0;
    while (*accumulator >= BOAT_TICK_SECONDS) {
        *accumulator -= BOAT_TICK_SECONDS;
        ++ticks;
    }
    return ticks;
}

static void direct_frame(Frame f)
{
    unsigned ticks = grant(&direct.accumulator, f);
    for (unsigned i = 0; i < ticks; ++i) {
        BoatSession *s = &direct.session;
        BoatInput input = { .steering_rate = f.steering_rate };
        if (isfinite(f.steering_target))
            input.steering_rate = boat_steering_rate(s->boat.wheel, f.steering_target);
        input.throttle_rate = boat_throttle_rate(s->boat.throttle, f.thrust_target);
        timed_boat_step(s, input, NULL, NULL, &direct.objects);
        if (expected_count < TRACE)
            expected[expected_count++] = (Expected) { s->tick, s->boat,
                boat_net_wake_digest(object_study_wakes(&direct.objects)) };
    }
}

static void observe(void *context, const BoatFleet *view, unsigned slot, uint64_t tick)
{
    (void)context;
    for (unsigned i = 0; i < expected_count; ++i)
        if (expected[i].tick == tick) {
            ++observed;
            mismatches
                += memcmp(&expected[i].boat, &view->state.slots[slot].boat, sizeof(Boat)) != 0
                || expected[i].wakes != boat_net_wake_digest(&view->state.wakes);
            return;
        }
    ++mismatches;
}

static double local_accumulator, now;

static bool same_authority(void)
{
    const BoatFleet *fleet = &local.server.fleet;
    const BoatWakeObjects *wakes = object_study_wakes(&direct.objects);
    return fleet->state.tick == direct.session.tick && local.tick == direct.session.tick
        && memcmp(&fleet->state.slots[0].boat, &direct.session.boat, sizeof(Boat)) == 0
        && boat_net_wake_digest(&fleet->state.wakes) == boat_net_wake_digest(wakes)
        && fleet->state.wakes.emitted == wakes->emitted && fleet->state.wakes.live == wakes->live;
}

// Run FRAMES of SCRIPT through both paths; compare after every frame.
static int drive(Script script, unsigned frames)
{
    BoatLocalObserver observer = { NULL, observe };
    for (unsigned n = 0; n < frames; ++n) {
        Frame f = script(n);
        expected_count = 0;
        direct_frame(f);
        unsigned before = observed;
        uint64_t tick = local.tick;
        unsigned ticks = grant(&local_accumulator, f);
        CHECK(ticks == expected_count);
        for (unsigned left = ticks, at = (unsigned)(tick % 12); left;) {
            unsigned k = boat_local_chunk(tick, left);
            chunk_sizes[k] += 1;
            chunk_starts_mid += at != 0;
            odd_chunks += k % 2;
            tick += k;
            left -= k;
            at = 0;
        }
        uint64_t chunks = local.stats.chunks;
        now += f.seconds;
        CHECK(boat_local_frame(&local, now, ticks, (BoatInput) { .steering_rate = f.steering_rate },
            f.thrust_target, f.steering_target, &observer));
        CHECK(local.stats.chunks - chunks <= 9);
        CHECK(observed - before == ticks);
        CHECK(mismatches == 0);
        CHECK(same_authority());
    }
    return 0;
}

static int open_world(World w)
{
    world = w;
    direct_open();
    local_accumulator = 0;
    now = 100;
    observed = mismatches = 0;
    memset(chunk_sizes, 0, sizeof(chunk_sizes));
    chunk_starts_mid = odd_chunks = 0;
    BoatLocalConfig config = local_config();
    CHECK(boat_local_start(&local, &config, 1, now));
    CHECK(local.stats.startup_rounds <= BOAT_LOCAL_STARTUP_ROUNDS);
    CHECK(same_authority());
    return 0;
}

// The L0 command gates: every granted tick integrated the client's own command.
static int clean_commands(void)
{
    const BoatNetServerStats *s = &local.server.stats;
    CHECK(s->rate_limited == 0);
    CHECK(s->late_commands == 0);
    CHECK(s->future_commands == 0);
    CHECK(s->stale_life_commands == 0);
    CHECK(s->duplicate_commands == 0);
    CHECK(s->malformed == 0 && s->forbidden == 0);
    CHECK(s->input_states[BOAT_NET_INPUT_RECEIVED] == s->ticks);
    CHECK(s->timeouts == 0);
    CHECK(local.client.stats.resyncs == 0);
    CHECK(local.client.stats.decode_errors == 0);
    CHECK(local.client.prediction.stats.snaps == 0);
    CHECK(local.client.prediction.stats.max_error == 0);
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

static int grant_96(void)
{
    if (run(BASIN, stalls, 8))
        return 1;
    CHECK(chunk_starts_mid > 0);
    CHECK(local.stats.max_chunks == 9);
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
    CHECK(local.client.phase == BOAT_NET_CLIENT_PLAYING);
    CHECK(local.client.stats.snapshot_resyncs == 0);
    return 0;
}

static int restart_generation(void)
{
    if (open_world(BASIN) || drive(one, 120))
        return 1;
    uint32_t old_epoch = local.generation;
    uint32_t old_sequence = local.client.sequence[BOAT_NET_STATE];
    direct_restart();
    local_accumulator = 0;
    BoatLocalConfig config = local_config();
    CHECK(boat_local_restart(&local, &config, now));
    CHECK(local.generation == old_epoch + 1 && local.tick == 0);
    CHECK(same_authority());
    // Late traffic of the earlier generation: a command batch and a READY acknowledgement.
    BoatNetHeader header = { .epoch = old_epoch, .sequence = old_sequence + 100, .tick = 2 };
    BoatNetInputBatch batch = { .life = 1, .count = 2 };
    batch.commands[0] = batch.commands[1]
        = boat_net_precise_command((BoatInput) { .steering_rate = 1, .throttle_rate = 1 }, 0);
    uint8_t bytes[BOAT_NET_MAX_MESSAGE];
    size_t size = boat_net_private_encode_input(&header, &batch, bytes);
    CHECK(size);
    CHECK(boat_net_send(&local.client.transport, 0, BOAT_NET_STATE, bytes, size));
    BoatNetMessage ready
        = { .header = { .type = BOAT_NET_READY, .epoch = old_epoch, .tick = 0 }, .checkpoint = 2 };
    size = boat_net_encode(&ready, bytes);
    CHECK(size);
    CHECK(boat_net_send(&local.client.transport, 0, BOAT_NET_CONTROL, bytes, size));
    if (drive(one, 120))
        return 1;
    CHECK(local.server.stats.stale_packets == 2);
    return clean_commands();
}

static int repeated_sessions(void)
{
    if (open_world(BASIN))
        return 1;
    for (unsigned session = 0; session < 100; ++session) {
        if (drive(one, 6))
            return 1;
        direct_restart();
        local_accumulator = 0;
        BoatLocalConfig config = local_config();
        CHECK(boat_local_restart(&local, &config, now));
        CHECK(same_authority());
    }
    CHECK(local.generation == 101);
    if (drive(one, 30))
        return 1;
    return clean_commands();
}

static int startup_failure(void)
{
    world = BASIN;
    BoatLocalConfig config = { .resources = worlds(), .location = BOAT_LOCATION_COUNT };
    CHECK(!boat_local_start(&local, &config, 1, 0));
    CHECK(!local.network && !local.running && local.failure[0]);
    // Released after a partial start: the authority refuses a water recipe it cannot checkpoint.
    config.location = BOAT_LOCATION_BASIN;
    config.world.water = boat_water_test(false, false, false);
    config.world.water.player_wake_enabled = true;
    CHECK(!boat_local_start(&local, &config, 1, 0));
    CHECK(!local.network && !local.running);
    // A stopped session refuses frames.
    CHECK(!boat_local_frame(&local, 1, 1, (BoatInput) { 0 }, 0, NAN, NULL));
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

// Per-frame cost at 8x on outer: processor time of one frame's grant through both endpoints.
static int bench(void)
{
    if (open_world(OUTER))
        return 1;
    BoatLocalObserver observer = { NULL, NULL };
    const unsigned frames = 1200;
    double worst = 0, total = 0, worst_96 = 0, total_96 = 0;
    for (unsigned n = 0; n < 2 * frames; ++n) {
        Frame f = eight(n);
        if (n >= frames)
            f.seconds = .1; // G = 96
        unsigned ticks = grant(&local_accumulator, f);
        now += f.seconds;
        clock_t start = clock();
        CHECK(boat_local_frame(&local, now, ticks, (BoatInput) { .steering_rate = f.steering_rate },
            f.thrust_target, f.steering_target, &observer));
        double ms = (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC;
        if (n < frames) {
            total += ms;
            worst = fmax(worst, ms);
        } else {
            total_96 += ms;
            worst_96 = fmax(worst_96, ms);
        }
    }
    printf("outer 8x 60 Hz (16 ticks/frame): mean %.3f ms, worst %.3f ms over %u frames\n",
        total / frames, worst, frames);
    printf("outer 8x 0.1 s frames (G = 96): mean %.3f ms, worst %.3f ms over %u frames\n",
        total_96 / frames, worst_96, frames);
    // The direct path for the same 16-tick frames, without presentation (the local frames above
    // stepped none).
    direct_open();
    direct.session.presentation_enabled = false;
    direct.accumulator = 0;
    clock_t start = clock();
    for (unsigned n = 0; n < frames; ++n) {
        expected_count = 0;
        direct_frame(eight(n));
    }
    printf("direct path, outer 8x 60 Hz: mean %.3f ms per frame\n",
        (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC / frames);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--bench")) {
        int result = bench();
        boat_local_stop(&local);
        test_worlds_close();
        return result;
    }
    static const TestCase cases[] = {
        TEST_CASE(basin_1x),
        TEST_CASE(basin_quarter),
        TEST_CASE(basin_8x),
        TEST_CASE(outer_1x),
        TEST_CASE(outer_8x),
        TEST_CASE(grant_96),
        TEST_CASE(odd_chunk_sizes),
        TEST_CASE(explorer_handling),
        TEST_CASE(pause_and_focus),
        TEST_CASE(restart_generation),
        TEST_CASE(repeated_sessions),
        TEST_CASE(startup_failure),
        TEST_CASE(private_range),
    };
    int result = test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
    boat_local_stop(&local);
    test_worlds_close();
    return result;
}
