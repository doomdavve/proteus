/* Multiplayer M3/M4: protocol codec, authority/client sessions, client prediction and the
 * in-memory impairment transport. Links only proteus_boat_net (and through it the core); no sockets
 * or devices. Golden bytes pin the wire layout; every truncation and the invalid-field cases must
 * be rejected; checkpoints decode to bit-identical fleets; and seeded virtual networks check
 * handshake rejection, bounded queues, input policy, idempotence, sequence wrap, impairment,
 * timeouts and that a client mirror reproduces the authority's state hash. Prediction replays
 * from mid-flight and capsized checkpoints reproduce the authority bit for bit, and predicting
 * clients on virtual links report their correction sizes and replay cost. */
#include "boat_fleet_bots.h"
#include "boat_map.h"
#include "boat_net_client.h"
#ifdef BOAT_NET_TEST_ENET
#include "boat_net_enet.h"
#endif
#include "boat_net_server.h"
#include "boat_test_cases.h"
#include "boat_test_world.h"
#include "boat_throttle.h"
#include "raymath.h"

#include <inttypes.h>
#include <limits.h>
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

static const BoatWorldResources *basin(void) { return test_world(BOAT_LOCATION_BASIN); }

static size_t encode(BoatNetMessage m, uint8_t out[BOAT_NET_MAX_MESSAGE])
{
    return boat_net_encode(&m, out);
}

static bool same_bytes(const uint8_t *a, size_t size, const char *hex)
{
    if (strlen(hex) != size * 2)
        return false;
    for (size_t i = 0; i < size; ++i) {
        unsigned value;
        if (sscanf(hex + 2 * i, "%2x", &value) != 1 || value != a[i])
            return false;
    }
    return true;
}

static void put32(uint8_t *at, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        at[i] = (uint8_t)(value >> (24 - 8 * i));
}

static void putf(uint8_t *at, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put32(at, bits);
}

// A fleet with every slot used, wakes and a pending reset, after TICKS bot ticks.
static void busy_fleet(BoatFleet *fleet, unsigned ticks, bool waves)
{
    boat_fleet_init(fleet, basin(), boat_world_test_water(basin()->definition, waves, false, false),
        BOAT_OBJECT_LIFE_TICKS, 1.8f);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        boat_fleet_request_spawn(fleet, i);
    BoatFleetBots bots = { 0 };
    for (unsigned t = 0; t < ticks; ++t) {
        BoatInput inputs[BOAT_FLEET_SLOTS];
        boat_fleet_bots_drive(&bots, fleet, inputs);
        boat_fleet_step(fleet, inputs);
    }
}

static int golden(void)
{
    uint8_t bytes[BOAT_NET_MAX_MESSAGE];
    size_t size = encode((BoatNetMessage) { .header = { .type = BOAT_NET_PING,
                                                .epoch = 0x01020304u,
                                                .sequence = 0xa0b0c0d0u,
                                                .tick = UINT64_C(0x1122334455667788) },
                             .clock = UINT64_C(0x0102030405060708) },
        bytes);
    CHECK(size == 40);
    CHECK(same_bytes(bytes, size,
        "50424e5400070d0001020304a0b0c0d01122334455667788000800000000000001020304050607"
        "08"));
    BoatNetMessage input = { .header = { .type = BOAT_NET_INPUT_BATCH, .epoch = 9, .tick = 200 },
        .input = { .life = 7, .count = 2, .snapshot_tick = 100 } };
    input.input.commands[0] = boat_net_command(
        (BoatInput) { .steering_rate = 1, .throttle_rate = -1, .torpedo = 255 }, 0);
    input.input.commands[1] = boat_net_command((BoatInput) { 0 }, BOAT_NET_COMMAND_INACTIVE);
    size = encode(input, bytes);
    CHECK(size == 60);
    CHECK(same_bytes(bytes, size,
        "50424e54000709000000000900000000000000000000"
        "00c8001c000000000000"
        "00000007020000000000000000000064" // Life, count, latest snapshot tick.
        "7f81000000ff" // Steering +1, throttle -1.
        "000001000000")); // Zero rates, inactive.
    BoatNetMessage hello = { .header = { .type = BOAT_NET_HELLO },
        .hello = { .physics_revision = 6,
            .content_digest = UINT64_C(0xdeadbeefcafef00d),
            .name = "ab",
            .token = "",
            .build = "x" } };
    size = encode(hello, bytes);
    CHECK(size == 32 + 4 + 8 + 3 + 1 + 2);
    CHECK(same_bytes(bytes + 32, size - 32,
        "00000006deadbeefcafef00d"
        "026162"
        "00"
        "0178"));
    // Match messages (v2): a scoreboard row with a negative score and a credited bailout.
    BoatNetMessage board = { .header = { .type = BOAT_NET_MATCH_STATE, .tick = 1 },
        .match = { .phase = BOAT_MATCH_INTERMISSION,
            .respawn_ticks = 360,
            .skip_ticks = 120,
            .round = 1,
            .round_ticks = 1200,
            .phase_end = 2400 } };
    board.match.slots[1].present = true;
    board.match.slots[1].row = (BoatMatchRow) { -1, 0, 1, 1 };
    size = encode(board, bytes);
    CHECK(size == 32 + 104);
    CHECK(same_bytes(bytes + 32, 64,
        "0200016800780000" // Phase, reserved, respawn and skip ticks, reserved.
        "00000001000004b00000000000000960" // Round, round ticks, phase end.
        "0000000000000000000000000000000000000000" // Slot 0 absent.
        "01000000ffffffff000000000000000100000001")); // Score -1, 1 capsized, 1 self.
    BoatNetMessage event = { .header = { .type = BOAT_NET_MATCH_EVENT, .tick = 2 },
        .match_event = { BOAT_MATCH_CAPSIZE, 3, 1, BOAT_MATCH_CONTACT, true, 9, 1, 0 } };
    size = encode(event, bytes);
    CHECK(size == 48);
    CHECK(same_bytes(bytes + 32, 16, "00030101010000000000000900000001"));
    // Race messages (v4): a racing round with a parked winner and a running entrant.
    BoatNetMessage standings = { .header = { .type = BOAT_NET_RACE_STATE, .tick = 7000 },
        .race = { .phase = BOAT_RACE_RACING,
            .placed = 1,
            .recovery_ticks = 240,
            .skip_ticks = 60,
            .park_ticks = 240,
            .round = 2,
            .go_tick = 1000,
            .deadline = 11800 } };
    standings.race.slots[0] = (BoatNetRaceRow) { true, true, true, true, BOAT_RACE_FINISHED, 1, 3,
        1, UINT64_C(0x5a0000) };
    standings.race.slots[2]
        = (BoatNetRaceRow) { true, true, true, false, BOAT_RACE_RUNNING, 0, 2, 0, 0 };
    standings.race.slots[3].present = true; // Joined after the grid.
    size = encode(standings, bytes);
    CHECK(size == 32 + 104);
    CHECK(same_bytes(bytes + 32, 104,
        "0300010000f0003c00f0000000000002" // Phase, course, placed, rule ticks, round.
        "000000000000000000000000000003e80000000000002e18" // Phase end, GO, deadline.
        "0f0101030000000100000000005a0000" // Parked winner: place 1, 3 gates, 1 recovery.
        "00000000000000000000000000000000" // Empty slot.
        "07000002000000000000000000000000" // Running entrant past two gates.
        "01000000000000000000000000000000")); // Present, not an entrant.
    BoatNetMessage finish = { .header = { .type = BOAT_NET_RACE_EVENT, .tick = 6400 },
        .race_event = { BOAT_RACE_FINISH, 0, 1, 2, UINT64_C(0x5a0000) } };
    size = encode(finish, bytes);
    CHECK(size == 48);
    CHECK(same_bytes(bytes + 32, 16,
        "02000100000000020000000000"
        "5a0000"));
    // Snapshot layout: sizes and a pinned digest of a hand-built state's bytes.
    BoatFleetState state = { .tick = 1000, .next_request = 5 };
    BoatNetSlotInput records[BOAT_FLEET_SLOTS]
        = { { .tick = 999, .state = 1, .used = { 127, -64, 1, 0, 17 } } };
    state.slots[0] = (BoatFleetSlot) { .requested = 3, .life = 2, .request = 1, .active = true };
    state.slots[0].boat.position = (Vector2) { 12.5f, -3.25f };
    state.slots[0].boat.heading = 1.5f;
    state.slots[0].boat.motion_state = BOAT_AIRBORNE;
    state.slots[0].boat.landings = 4;
    state.wakes = (BoatWakeObjects) { .tick = 1000,
        .life_ticks = 1920,
        .crest_height = 1.8f,
        .emitted = 9,
        .expired = 8,
        .live = 1,
        .peak_live = 3 };
    state.wakes.objects[33] = (BoatWakeObject) { { 1, 2 }, { 0, 1 }, 2, .5f, .25f, 990 };
    state.wakes.emitters[1] = (BoatWakeObjectEmitter) { { 4, 5 }, 900, 1000, 2, .5f, 3, true };
    state.slots[0].held = true;
    BoatNetMessage snapshot = { .header = { .type = BOAT_NET_SNAPSHOT, .epoch = 1, .tick = 1000 } };
    boat_net_snapshot_of(&state, records, &snapshot.snapshot);
    snapshot.snapshot.race_go = 1234;
    size = encode(snapshot, bytes);
    CHECK(size == 768);
    uint8_t expected[4];
    putf(expected, 12.5f);
    CHECK(memcmp(bytes + 32 + 40 + 38, expected, 4) == 0); // Slot 0 position.x.
    CHECK(bytes[32 + 32 + 6] == 0x04 && bytes[32 + 32 + 7] == 0xd2); // Race GO tick 1234.
    CHECK(bytes[32 + 40] == 5); // Active and held.
    CHECK(bytes[32 + 40 + 1] == BOAT_AIRBORNE && bytes[32 + 40 + 28] == 0x7f);
    CHECK(bytes[32 + 40 + 29] == 0xc0); // -64 as a two's-complement byte.
    uint64_t digest = boat_net_bytes_digest(bytes, size);
    printf("golden: snapshot digest %016" PRIx64 "\n", digest);
    CHECK(digest == UINT64_C(0x818226576068a7dd));
    // Command quantization: keyboard values survive exactly.
    for (float value = -1; value <= 1; value += 1) {
        BoatInput back = boat_net_command_input(
            boat_net_command((BoatInput) { .steering_rate = value, .throttle_rate = value }, 0));
        CHECK(back.steering_rate == value && back.throttle_rate == value);
    }
    CHECK(boat_net_command((BoatInput) { .steering_rate = NAN, .throttle_rate = 2 }, 0).steering
        == 0);
    CHECK(boat_net_command((BoatInput) { .steering_rate = NAN, .throttle_rate = 2 }, 0).throttle
        == 127);
    CHECK(boat_net_sequence_newer(0, UINT32_MAX) && !boat_net_sequence_newer(UINT32_MAX, 0));
    CHECK(!boat_net_sequence_newer(5, 5) && !boat_net_sequence_newer(UINT32_C(0x80000000), 0));
    CHECK(boat_net_sequence_newer(UINT32_C(0x7fffffff), 0));
    printf("golden: header, input, hello, match, race and snapshot layouts pinned\n");
    return 0;
}

typedef struct {
    uint8_t bytes[BOAT_NET_MAX_MESSAGE];
    size_t size;
} Sample;

static unsigned samples(Sample *out)
{
    static BoatFleet fleet;
    busy_fleet(&fleet, 1500, false);
    BoatNetSlotInput records[BOAT_FLEET_SLOTS] = { 0 };
    unsigned n = 0;
    BoatNetMessage m[BOAT_NET_TYPE_LIMIT] = { 0 };
    m[0] = (BoatNetMessage) { .header = { .type = BOAT_NET_HELLO },
        .hello = { BOAT_PHYSICS_REVISION, 42, "name", "token", "build" } };
    m[1] = (BoatNetMessage) { .header = { .type = BOAT_NET_ACCEPT, .epoch = 3 },
        .accept = { 2, true, BOAT_NET_MODE_DEATHMATCH, BOAT_LOCATION_RIVER, 4, 12, 2, 12, 24, 120,
            1920, 1.8f } };
    m[2] = (BoatNetMessage) { .header = { .type = BOAT_NET_REJECT },
        .reject = { BOAT_NET_REJECT_TOKEN, 1, 6, 42 } };
    m[3] = (BoatNetMessage) { .header = { .type = BOAT_NET_ROSTER, .epoch = 3 } };
    m[3].roster.slots[1].state = BOAT_NET_ROSTER_PLAYING;
    strcpy(m[3].roster.slots[1].name, "sixteen-chars-ok");
    m[4] = (BoatNetMessage) { .header = { .type = BOAT_NET_WORLD_BEGIN, .epoch = 3, .tick = 9 },
        .world_begin = { 1, 5332, 6, 77, 88 } };
    m[5] = (BoatNetMessage) { .header = { .type = BOAT_NET_WORLD_PAGE, .epoch = 3 },
        .world_page = { .checkpoint = 1, .index = 5, .count = 6, .size = 212 } };
    m[6] = (BoatNetMessage) { .header = { .type = BOAT_NET_WORLD_END, .epoch = 3 },
        .checkpoint = 1 };
    m[7] = (BoatNetMessage) { .header = { .type = BOAT_NET_READY, .epoch = 3 }, .checkpoint = 1 };
    m[8] = (BoatNetMessage) { .header = { .type = BOAT_NET_INPUT_BATCH, .epoch = 3, .tick = 50 },
        .input = { .life = 1, .count = BOAT_NET_MAX_COMMANDS, .snapshot_tick = 40 } };
    m[9] = (BoatNetMessage) { .header
        = { .type = BOAT_NET_SNAPSHOT, .epoch = 3, .tick = fleet.state.tick } };
    boat_net_snapshot_of(&fleet.state, records, &m[9].snapshot);
    m[10] = (BoatNetMessage) { .header
        = { .type = BOAT_NET_WAKE_BIRTHS, .epoch = 3, .tick = fleet.state.tick } };
    for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY && m[10].births.count < BOAT_NET_MAX_BIRTHS; ++i)
        if (fleet.state.wakes.objects[i].amplitude)
            m[10].births.births[m[10].births.count++]
                = (BoatNetBirth) { (uint8_t)i, fleet.state.wakes.objects[i] };
    m[11] = (BoatNetMessage) { .header = { .type = BOAT_NET_SPAWN_REQUEST, .epoch = 3 },
        .spawn = { 4, 2 } };
    m[12] = (BoatNetMessage) { .header = { .type = BOAT_NET_PING, .epoch = 3 }, .clock = 5 };
    m[13] = (BoatNetMessage) { .header = { .type = BOAT_NET_PONG, .epoch = 3 }, .clock = 5 };
    m[14] = (BoatNetMessage) { .header = { .type = BOAT_NET_RESYNC_REQUEST, .epoch = 3 },
        .resync = { 1, 30 } };
    m[15] = (BoatNetMessage) { .header = { .type = BOAT_NET_DISCONNECT, .epoch = 3 },
        .leave = BOAT_NET_LEAVE_QUIT };
    m[16] = (BoatNetMessage) { .header = { .type = BOAT_NET_MATCH_STATE, .epoch = 3, .tick = 80 },
        .match = { .phase = BOAT_MATCH_PLAYING,
            .respawn_ticks = 360,
            .skip_ticks = 120,
            .round = 2,
            .round_ticks = 36000,
            .phase_end = 36080 } };
    m[16].match.slots[0].present = m[16].match.slots[2].present = true;
    m[16].match.slots[0].row = (BoatMatchRow) { -2, 1, 4, 3 };
    m[16].match.slots[2].row = (BoatMatchRow) { 3, 3, 0, 0 };
    m[17] = (BoatNetMessage) { .header = { .type = BOAT_NET_MATCH_EVENT, .epoch = 3, .tick = 80 },
        .match_event = { BOAT_MATCH_CAPSIZE, 0, 2, BOAT_MATCH_WAKE, true, 7, 2, 0 } };
    m[18] = (BoatNetMessage) { .header = { .type = BOAT_NET_RACE_STATE, .epoch = 3, .tick = 900 },
        .race = { .phase = BOAT_RACE_COUNTDOWN,
            .placed = 1,
            .recovery_ticks = 240,
            .skip_ticks = 60,
            .park_ticks = 240,
            .round = 3,
            .phase_end = 960,
            .go_tick = 960 } };
    // A finish kept from a leaver, a current entrant and a present non-entrant.
    m[18].race.slots[1]
        = (BoatNetRaceRow) { false, true, false, false, BOAT_RACE_FINISHED, 1, 3, 2, 40000 };
    m[18].race.slots[2]
        = (BoatNetRaceRow) { true, true, true, false, BOAT_RACE_RUNNING, 0, 0, 0, 0 };
    m[18].race.slots[3].present = true;
    m[19] = (BoatNetMessage) { .header = { .type = BOAT_NET_RACE_EVENT, .epoch = 3, .tick = 950 },
        .race_event = { BOAT_RACE_GATE, 2, 1, 3, 0 } };
    m[20] = (BoatNetMessage) { .header = { .type = BOAT_NET_COMBAT_PAGE, .epoch = 3, .tick = 100 },
        .world_page = { .checkpoint = 100, .index = 0, .count = 4, .size = BOAT_NET_PAGE_BYTES } };
    for (unsigned i = 0; i < BOAT_NET_TYPE_LIMIT - 1; ++i) {
        out[n].size = boat_net_encode(&m[i], out[n].bytes);
        if (!out[n].size) {
            fprintf(stderr, "sample type %u does not encode\n", (unsigned)m[i].header.type);
            return 0;
        }
        ++n;
    }
    return n;
}

static int codec(void)
{
    static Sample sample[BOAT_NET_TYPE_LIMIT];
    unsigned n = samples(sample);
    CHECK(n == BOAT_NET_TYPE_LIMIT - 1);
    static BoatNetMessage m;
    unsigned truncations = 0;
    for (unsigned i = 0; i < n; ++i) {
        const Sample *s = &sample[i];
        CHECK(s->size <= BOAT_NET_MAX_MESSAGE);
        CHECK(boat_net_decode(s->bytes, s->size, &m) == BOAT_NET_OK);
        uint8_t again[BOAT_NET_MAX_MESSAGE];
        CHECK(boat_net_encode(&m, again) == s->size && memcmp(again, s->bytes, s->size) == 0);
        for (size_t length = 0; length < s->size; ++length, ++truncations)
            CHECK(boat_net_decode(s->bytes, length, &m) != BOAT_NET_OK);
        uint8_t longer[BOAT_NET_MAX_MESSAGE + 1];
        memcpy(longer, s->bytes, s->size);
        longer[s->size] = 0;
        CHECK(boat_net_decode(longer, s->size + 1, &m) == BOAT_NET_BAD_LENGTH);
        // A payload byte added and declared: still rejected by the type's exact layout,
        // except pages, whose data length is variable.
        if (s->bytes[6] != BOAT_NET_WORLD_PAGE && s->size < BOAT_NET_MAX_MESSAGE) {
            longer[25] = (uint8_t)(longer[25] + 1);
            if (longer[25] == 0)
                ++longer[24];
            CHECK(boat_net_decode(longer, s->size + 1, &m) != BOAT_NET_OK);
        }
    }
    printf("codec: %u types round-trip; %u truncations rejected\n", n, truncations);

    // Header faults.
    uint8_t b[BOAT_NET_MAX_MESSAGE];
    const Sample *ping = &sample[12];
    struct {
        size_t at;
        uint8_t value;
        BoatNetError error;
    } header_faults[] = { { 0, 0x51, BOAT_NET_BAD_MAGIC }, { 5, 1, BOAT_NET_BAD_VERSION },
        { 6, 0, BOAT_NET_BAD_TYPE }, { 6, BOAT_NET_TYPE_LIMIT, BOAT_NET_BAD_TYPE },
        { 7, 1, BOAT_NET_BAD_HEADER }, { 26, 1, BOAT_NET_BAD_HEADER },
        { 31, 1, BOAT_NET_BAD_HEADER }, { 24, 0xff, BOAT_NET_BAD_HEADER } };
    for (size_t i = 0; i < sizeof(header_faults) / sizeof(*header_faults); ++i) {
        memcpy(b, ping->bytes, ping->size);
        b[header_faults[i].at] = header_faults[i].value;
        CHECK(boat_net_decode(b, ping->size, &m) == header_faults[i].error);
    }

    // Every snapshot float of every slot: NaN and infinity are refused.
    const Sample *snap = &sample[9];
    unsigned nonfinite = 0;
    for (unsigned slot = 0; slot < BOAT_FLEET_SLOTS; ++slot) {
        size_t base = 32 + 40 + slot * 174;
        size_t floats[27];
        for (unsigned k = 0; k < 25; ++k)
            floats[k] = base + 38 + 4 * k;
        floats[25] = base + 138;
        floats[26] = base + 142;
        for (unsigned k = 0; k < 27; ++k)
            for (int inf = 0; inf < 2; ++inf, ++nonfinite) {
                memcpy(b, snap->bytes, snap->size);
                putf(b + floats[k], inf ? INFINITY : NAN);
                CHECK(boat_net_decode(b, snap->size, &m) == BOAT_NET_NONFINITE);
            }
        size_t more[] = { base + 166, base + 170 };
        for (unsigned k = 0; k < 2; ++k, ++nonfinite) {
            memcpy(b, snap->bytes, snap->size);
            putf(b + more[k], NAN);
            CHECK(boat_net_decode(b, snap->size, &m) == BOAT_NET_NONFINITE);
        }
    }
    struct {
        size_t at;
        uint8_t value;
        BoatNetError error;
    } snapshot_faults[] = {
        { 72 + 0, 8, BOAT_NET_BAD_FIELD }, // Slot flags.
        { 72 + 0, 4, BOAT_NET_BAD_FIELD }, // Held without an active boat.
        { 72 + 1, 4, BOAT_NET_BAD_FIELD }, // Motion state.
        { 72 + 2, BOAT_NET_INPUT_STATES, BOAT_NET_BAD_FIELD },
        { 72 + 3, 1, BOAT_NET_BAD_FIELD }, // Reserved.
        { 72 + 12, 0xff, BOAT_NET_BAD_FIELD }, // Request tick after the snapshot.
        { 72 + 28, 0x80, BOAT_NET_BAD_FIELD }, // -128 is never a rate.
        { 72 + 30, 4, BOAT_NET_BAD_FIELD }, // Command flags.
        { 72 + 162, 32, BOAT_NET_BAD_FIELD }, // Emitter cursor.
        { 72 + 163, 2, BOAT_NET_BAD_FIELD }, // Emitter initialized.
        { 32 + 4, 0xff, BOAT_NET_BAD_FIELD }, // Live count.
    };
    for (size_t i = 0; i < sizeof(snapshot_faults) / sizeof(*snapshot_faults); ++i) {
        memcpy(b, snap->bytes, snap->size);
        b[snapshot_faults[i].at] = snapshot_faults[i].value;
        CHECK(boat_net_decode(b, snap->size, &m) == snapshot_faults[i].error);
    }
    float ranges[][2] = { { 72 + 38, 1e4f }, { 72 + 38 + 4 * 5, 1.5f }, { 72 + 38 + 4 * 7, -.1f },
        { 72 + 38 + 4 * 8, -2000 }, { 72 + 38 + 4 * 14, -1 }, { 72 + 38 + 4 * 20, 4 } };
    for (size_t i = 0; i < sizeof(ranges) / sizeof(*ranges); ++i) {
        memcpy(b, snap->bytes, snap->size);
        putf(b + (size_t)ranges[i][0], ranges[i][1]);
        CHECK(boat_net_decode(b, snap->size, &m) == BOAT_NET_OUT_OF_RANGE);
    }
    // Other messages' counts, indices, strings and enums.
    struct {
        unsigned sample;
        size_t at;
        uint8_t value;
    } field_faults[] = { { 8, 36, 0 }, { 8, 36, BOAT_NET_MAX_COMMANDS + 1 }, { 8, 50, 4 },
        { 8, 37, 1 }, { 10, 32, BOAT_NET_MAX_BIRTHS + 1 }, { 10, 36, 200 }, { 5, 36, 6 },
        { 4, 40, 7 }, { 2, 32, 0 }, { 2, 32, BOAT_NET_REJECT_LIMIT }, { 0, 44, 0x01 },
        { 0, 46, 0x7f }, { 0, 44, 17 }, { 1, 32, BOAT_FLEET_SLOTS }, { 1, 33, 2 }, { 1, 41, 0 },
        { 3, 32 + 19, 3 }, { 3, 32 + 20, 17 }, { 3, 32 + 21 + 15, 'x' }, { 14, 32, 3 },
        { 15, 32, 0 }, { 6, 36, 1 }, { 1, 39, BOAT_NET_MODE_LIMIT }, { 1, 42, BOAT_LOCATION_COUNT },
        { 1, 43, 1 },
        // Match state: phase, reserved, skip beyond respawn, warmup with an end tick, a bad
        // presence flag, more self-capsizes than capsizes and a score on an absent slot.
        { 16, 32, 3 }, { 16, 33, 1 }, { 16, 38, 0x7f }, { 16, 32, BOAT_MATCH_WARMUP },
        { 16, 56, 2 }, { 16, 75, 9 }, { 16, 83, 1 },
        // Match event: kind, victim as attacker, slot range, credited cause, flags, reserved,
        // round zero and a bailout without an attacker.
        { 17, 32, 3 }, { 17, 33, 2 }, { 17, 34, 4 }, { 17, 35, 0 }, { 17, 36, 2 }, { 17, 37, 2 },
        { 17, 47, 0 }, { 17, 34, BOAT_MATCH_NOBODY },
        // Race state: phase, course, places beyond the finishers, reserved bytes, skip beyond
        // recovery, racing with an end tick and no deadline, a GO without a round, a deadline
        // outside racing, unknown row flags, a parked leaver, a status, a place without a
        // finish, a finish time while running and a non-entrant with gates.
        { 18, 32, BOAT_RACE_RESULTS + 1 }, { 18, 33, 1 }, { 18, 34, 2 }, { 18, 35, 1 },
        { 18, 38, 0xff }, { 18, 42, 1 }, { 18, 32, BOAT_RACE_RACING }, { 18, 47, 0 }, { 18, 79, 1 },
        { 18, 88, 16 }, { 18, 88, 10 }, { 18, 89, 3 }, { 18, 106, 1 }, { 18, 119, 1 },
        { 18, 123, 1 },
        // Race event: kind, a gate event without a slot, the gate range, reserved, round zero
        // and a time on a gate event.
        { 19, 32, BOAT_RACE_PARK + 1 }, { 19, 33, BOAT_RACE_NOBODY }, { 19, 34, BOAT_RACE_GATES },
        { 19, 35, 1 }, { 19, 39, 0 }, { 19, 47, 1 } };
    for (size_t i = 0; i < sizeof(field_faults) / sizeof(*field_faults); ++i) {
        const Sample *s = &sample[field_faults[i].sample];
        memcpy(b, s->bytes, s->size);
        b[field_faults[i].at] = field_faults[i].value;
        if (boat_net_decode(b, s->size, &m) == BOAT_NET_OK) {
            fprintf(stderr, "field fault %zu accepted\n", i);
            return 1;
        }
    }
    // The encoder refuses what the decoder would refuse.
    BoatNetMessage bad
        = { .header = { .type = BOAT_NET_INPUT_BATCH, .tick = 5 }, .input = { .count = 0 } };
    CHECK(boat_net_encode(&bad, b) == 0);
    bad = (BoatNetMessage) { .header = { .type = BOAT_NET_HELLO } };
    strcpy(bad.hello.name, "tab\there");
    CHECK(boat_net_encode(&bad, b) == 0);
    bad = (BoatNetMessage) { .header = { .type = BOAT_NET_ACCEPT },
        .accept = { 0, false, BOAT_NET_MODE_RACE, BOAT_LOCATION_BASIN, 4, 12, 2, 12, 24, 120, 1920,
            1.8f } }; // The river sprint needs the river.
    CHECK(boat_net_encode(&bad, b) == 0);
    bad.accept.location = BOAT_LOCATION_RIVER;
    CHECK(boat_net_encode(&bad, b) > 0);
    printf("codec: %u non-finite floats and header/field/range faults rejected\n", nonfinite);
    return 0;
}

static int checkpoints(void)
{
    static BoatFleet fleet, copy;
    static uint8_t bytes[BOAT_NET_MAX_CHECKPOINT], again[BOAT_NET_MAX_CHECKPOINT];
    static BoatFleetState state;
    for (int waves = 0; waves < 2; ++waves) {
        busy_fleet(&fleet, 2400, waves);
        boat_fleet_request_spawn(&fleet, 2); // A pending reset is part of the checkpoint.
        BoatNetSlotInput records[BOAT_FLEET_SLOTS] = { { 2399, 1, { 127, 0, 0, 0, 0 } } };
        uint64_t go = waves ? 4321 : 0, go_back = 1;
        size_t size = boat_net_checkpoint_encode(&fleet.state, records, go, bytes, sizeof(bytes));
        CHECK(size > 0 && size <= BOAT_NET_MAX_CHECKPOINT);
        CHECK(fleet.state.wakes.live > 20);
        BoatNetSlotInput decoded[BOAT_FLEET_SLOTS];
        CHECK(boat_net_checkpoint_decode(bytes, size, &state, decoded, &go_back) == BOAT_NET_OK);
        CHECK(boat_fleet_hash(&state) == boat_fleet_hash(&fleet.state) && go_back == go);
        CHECK(decoded[0].tick == 2399 && decoded[0].used.steering == 127);
        CHECK(boat_net_checkpoint_encode(&state, decoded, go, again, sizeof(again)) == size);
        CHECK(memcmp(bytes, again, size) == 0);
        boat_fleet_init(&copy, basin(),
            boat_world_test_water(basin()->definition, waves, false, false), BOAT_OBJECT_LIFE_TICKS,
            1.8f);
        boat_fleet_restore(&copy, &state);
        BoatFleetBots a = { 0 }, b = { 0 };
        for (unsigned t = 0; t < 600; ++t) {
            BoatInput ia[BOAT_FLEET_SLOTS], ib[BOAT_FLEET_SLOTS];
            boat_fleet_bots_drive(&a, &fleet, ia);
            boat_fleet_bots_drive(&b, &copy, ib);
            boat_fleet_step(&fleet, ia);
            boat_fleet_step(&copy, ib);
        }
        CHECK(boat_fleet_hash(&copy.state) == boat_fleet_hash(&fleet.state));
        for (size_t length = 0; length < size; length += 97)
            CHECK(boat_net_checkpoint_decode(bytes, length, &state, decoded, NULL) != BOAT_NET_OK);
        bytes[35] ^= 1; // Live count no longer matches the records.
        CHECK(boat_net_checkpoint_decode(bytes, size, &state, decoded, NULL) != BOAT_NET_OK);
        printf("checkpoint: %s water, %zu bytes, %u live records, resumed 600 ticks "
               "bit-identically\n",
            waves ? "wave" : "calm", size, fleet.state.wakes.live);
    }
    return 0;
}

// A virtual room: one authority, clients driven by bots from their own mirrors.
#define ROOM_CLIENTS 8
#define ROOM_HISTORY 1024
#define ROOM_ENET_HOSTS 16 // Joined and raw client hosts per ENet room.
typedef struct {
    BoatNetMemory *network; // NULL when the room runs over ENet.
#ifdef BOAT_NET_TEST_ENET
    // Real UDP on 127.0.0.1, still on the room's virtual clock: the listening host and every
    // client host, joined or raw. The server's peer index for a client is ENet's choice.
    BoatNetEnet *enet, *enet_clients[ROOM_ENET_HOSTS];
    unsigned enet_count;
#endif
    BoatNetServer server;
    BoatNetClient clients[ROOM_CLIENTS];
    unsigned link[ROOM_CLIENTS];
    bool live[ROOM_CLIENTS]; // Serviced each step.
    bool inactive[ROOM_CLIENTS];
    bool predict; // Clients joining from now on predict.
    bool detents;
    float thrust_target, steering_target;
    bool keyboard; // Bots press keys: each rate rounded to -1, 0 or 1.
    bool weapons; // Hunter inputs for combat paging/prediction stress.
    bool race; // Entrants race: full throttle on the grid, then the course in their own lane.
    double service_seconds[ROOM_CLIENTS]; // Processor time in client service calls.
    BoatFleetBots bots[ROOM_CLIENTS];
    unsigned count;
    double now;
    uint64_t history_tick[ROOM_HISTORY], history_hash[ROOM_HISTORY];
    uint64_t checked[ROOM_CLIENTS];
    // Authoritative boat positions by tick, and each predicting client's view of another boat.
    Vector2 history_position[ROOM_HISTORY][BOAT_FLEET_SLOTS];
    uint32_t history_life[ROOM_HISTORY][BOAT_FLEET_SLOTS];
    bool history_contact[ROOM_HISTORY];
    BoatRemoteProximity proximity[ROOM_CLIENTS];
    uint64_t remote_samples[ROOM_CLIENTS][BOAT_REMOTE_HELD + 1], remote_checked[ROOM_CLIENTS];
    uint64_t near_steps[ROOM_CLIENTS], view_steps[ROOM_CLIENTS];
    double remote_error[ROOM_CLIENTS], remote_max_error[ROOM_CLIENTS], delay[ROOM_CLIENTS];
    double remote_smooth_max_error[ROOM_CLIENTS], remote_contact_max_error[ROOM_CLIENTS];
    uint64_t remote_contact_samples[ROOM_CLIENTS];
    unsigned checks, matches, mismatches;
} Room;

// The room's server transport: the seeded in-memory network, or with BOAT_NET_TEST_ENET (the
// boat_net_enet tests) an ENet host on an ephemeral 127.0.0.1 port.
static bool room_listen(Room *room, uint64_t seed, BoatNetTransport *transport)
{
#ifdef BOAT_NET_TEST_ENET
    (void)seed;
    char error[128];
    room->enet = boat_net_enet_listen("127.0.0.1", 0, error, sizeof(error));
    if (!room->enet)
        return false;
    *transport = boat_net_enet_transport(room->enet);
#else
    room->network = boat_net_memory_create(seed);
    if (!room->network)
        return false;
    *transport = boat_net_memory_server(room->network);
#endif
    return true;
}

// A new client transport into the room. LINK is the in-memory link (UINT_MAX over ENet).
static BoatNetTransport room_transport(Room *room, unsigned *link)
{
    BoatNetTransport transport;
#ifdef BOAT_NET_TEST_ENET
    char error[128];
    BoatNetEnet *e = room->enet_count < ROOM_ENET_HOSTS
        ? boat_net_enet_connect("127.0.0.1", boat_net_enet_port(room->enet), error, sizeof(error))
        : NULL;
    if (!e) {
        fprintf(stderr, "cannot open an ENet client host\n");
        exit(1);
    }
    room->enet_clients[room->enet_count++] = e;
    *link = UINT_MAX;
    transport = boat_net_enet_transport(e);
#else
    boat_net_memory_client(room->network, link, &transport);
#endif
    return transport;
}

// Over ENet, deliver every datagram sent so far before the clock moves on. Loopback delivery
// is asynchronous, and the virtual clock would otherwise outrun it on a loaded machine. The
// wall-clock limit is only a failure deadline.
static void room_flush(Room *room)
{
#ifdef BOAT_NET_TEST_ENET
    BoatNetEnet *hosts[ROOM_ENET_HOSTS + 1] = { room->enet };
    memcpy(hosts + 1, room->enet_clients, room->enet_count * sizeof(*hosts));
    if (!boat_net_enet_settle(hosts, room->enet_count + 1, 5)) {
        fprintf(stderr, "ENet loopback did not settle within 5 s\n");
        exit(1);
    }
#else
    (void)room;
#endif
}

static bool room_open(Room *room, uint64_t seed, BoatNetServerConfig config)
{
    // Clear in place: a Room is over 1 MB, more than Windows' default stack, so a compound
    // literal temporary would overflow it.
    memset(room, 0, sizeof(*room));
    room->steering_target = NAN;
    if (!config.epoch)
        config.epoch = 0x5eed;
    if (!config.max_players)
        config.max_players = BOAT_FLEET_SLOTS;
    if (!config.token)
        config.token = "room";
    if (!config.resources)
        config.resources = basin();
    BoatNetTransport transport;
    return room_listen(room, seed, &transport)
        && boat_net_server_init(&room->server, transport, &config);
}

static void room_close(Room *room)
{
#ifdef BOAT_NET_TEST_ENET
    for (unsigned i = 0; i < room->enet_count; ++i)
        boat_net_enet_close(room->enet_clients[i], 0); // Destroy now; never wait for acks.
    boat_net_enet_close(room->enet, 0);
#else
    boat_net_memory_destroy(room->network);
#endif
}

static unsigned room_join_ex(
    Room *room, const char *name, const char *token, uint32_t sequence, bool defer_ready)
{
    if (room->count == ROOM_CLIENTS) {
        fprintf(stderr, "room_join: more than %d clients\n", ROOM_CLIENTS);
        exit(1);
    }
    unsigned i = room->count++;
    BoatNetTransport transport = room_transport(room, &room->link[i]);
    BoatNetClientConfig config = { .name = name,
        .token = token ? token : "room",
        .build = "validate",
        .first_sequence = sequence,
        .predict = room->predict,
        .defer_ready = defer_ready,
        .resources = test_worlds };
    boat_net_client_init(&room->clients[i], transport, &config, room->now);
    room->live[i] = true;
    return i;
}

static unsigned room_join(Room *room, const char *name, const char *token, uint32_t sequence)
{
    return room_join_ex(room, name, token, sequence, false);
}

// What a predicting client would draw for the first other boat: the interpolated pose,
// checked against the authority's position at the drawn tick, and the proximity weight.
static void room_view_remote(Room *room, unsigned i, uint64_t tick, double dt)
{
    const BoatNetClient *c = &room->clients[i];
    const BoatFleet *view = boat_net_client_view(c);
    unsigned other = BOAT_FLEET_SLOTS;
    for (unsigned k = 0; k < BOAT_FLEET_SLOTS && other == BOAT_FLEET_SLOTS; ++k)
        if (k != c->slot && view->state.slots[k].active)
            other = k;
    const BoatFleetSlot *own = &view->state.slots[c->slot];
    if (other == BOAT_FLEET_SLOTS || !own->active)
        return;
    double shown = boat_net_client_server_tick(c, room->now) - BOAT_REMOTE_DELAY_TICKS;
    Boat pose;
    uint32_t life;
    BoatRemoteSample sample = boat_remote_history_pose(&c->remotes, other, shown, &pose, &life);
    ++room->remote_samples[i][sample];
    ++room->view_steps[i];
    room->delay[i] += (double)tick - shown;
    const BoatFleetSlot *remote = &view->state.slots[other];
    room->near_steps[i] += boat_remote_proximity_update(&room->proximity[i], &own->boat,
                               &remote->boat, remote->life, room->now, (float)dt)
        > 0;
    if (shown < 0) // No history precedes the first remote delay.
        return;
    uint64_t a = (uint64_t)floor(shown);
    // Across a respawn the view shows the new life up to one snapshot early, by design.
    if (sample != BOAT_REMOTE_INTERPOLATED || room->history_tick[a % ROOM_HISTORY] != a
        || room->history_tick[(a + 1) % ROOM_HISTORY] != a + 1
        || room->history_life[a % ROOM_HISTORY][other] != life
        || room->history_life[(a + 1) % ROOM_HISTORY][other] != life)
        return;
    Vector2 truth = Vector2Lerp(room->history_position[a % ROOM_HISTORY][other],
        room->history_position[(a + 1) % ROOM_HISTORY][other], (float)(shown - (double)a));
    double error = Vector2Distance(truth, pose.position);
    // A 30 Hz snapshot chord cannot reproduce a 120 Hz contact impulse's sharp corner.
    // Keep the smooth-motion gate strict and report contact-adjacent samples separately.
    bool contact = false;
    uint64_t first = a > BOAT_NET_SNAPSHOT_TICKS ? a - BOAT_NET_SNAPSHOT_TICKS : 0;
    for (uint64_t k = first; k <= a + BOAT_NET_SNAPSHOT_TICKS; ++k)
        contact
            |= room->history_tick[k % ROOM_HISTORY] == k && room->history_contact[k % ROOM_HISTORY];
    if (contact) {
        room->remote_contact_max_error[i] = fmax(room->remote_contact_max_error[i], error);
        ++room->remote_contact_samples[i];
    } else
        room->remote_smooth_max_error[i] = fmax(room->remote_smooth_max_error[i], error);
    room->remote_error[i] += error;
    room->remote_max_error[i] = fmax(room->remote_max_error[i], error);
    ++room->remote_checked[i];
}

static void room_step(Room *room, double dt)
{
    room->now += dt;
    if (room->network)
        boat_net_memory_set_time(room->network, room->now);
    boat_net_server_service(&room->server, room->now);
    room_flush(room);
    uint64_t tick = room->server.fleet.state.tick;
    // Client mirrors only ever hold snapshot ticks, so only those are hashed (once each).
    if (room->history_tick[tick % ROOM_HISTORY] != tick || tick == 0)
        room->history_hash[tick % ROOM_HISTORY]
            = tick % BOAT_NET_SNAPSHOT_TICKS == 0 ? boat_fleet_hash(&room->server.fleet.state) : 0;
    room->history_tick[tick % ROOM_HISTORY] = tick;
    room->history_contact[tick % ROOM_HISTORY] = room->server.fleet.stats.contacts > 0;
    for (unsigned k = 0; k < BOAT_FLEET_SLOTS; ++k) {
        room->history_position[tick % ROOM_HISTORY][k]
            = room->server.fleet.state.slots[k].boat.position;
        room->history_life[tick % ROOM_HISTORY][k] = room->server.fleet.state.slots[k].life;
    }
    for (unsigned i = 0; i < room->count; ++i) {
        BoatNetClient *c = &room->clients[i];
        if (!room->live[i])
            continue;
        BoatInput input = { 0 };
        const BoatFleetSlot *own = boat_net_client_own(c);
        if (own)
            input = boat_fleet_bot_input(&room->bots[i], boat_net_client_view(c), c->slot);
        if (own && room->weapons) {
            input = boat_fleet_bot_hunt_input(&room->bots[i], boat_net_client_view(c), c->slot);
            input.gun = true;
            input.grenade = (uint16_t)(room->server.fleet.state.tick / 960 + 1);
            input.torpedo = (uint8_t)(room->server.fleet.state.tick / 240 % 255 + 1);
        }
        const BoatNetRaceRow *row = &c->race.slots[c->slot];
        if (own && room->race && c->race_valid && row->current) {
            const BoatFleet *view = boat_net_client_view(c);
            if (view->state.slots[c->slot].held)
                input = (BoatInput) { .throttle_rate
                    = boat_throttle_rate(view->state.slots[c->slot].boat.throttle, 1) };
            else if (c->race.phase >= BOAT_RACE_RACING)
                input = boat_fleet_bot_race_input(view, boat_race_river_course(), row->gates,
                    row->status != BOAT_RACE_RUNNING, c->slot);
        }
        if (room->keyboard) {
            input.steering_rate
                = fabsf(input.steering_rate) < .3f ? 0 : copysignf(1, input.steering_rate);
            input.throttle_rate
                = fabsf(input.throttle_rate) < .3f ? 0 : copysignf(1, input.throttle_rate);
        }
        clock_t started = clock();
        room->live[i] = room->detents
            ? boat_net_client_service_targets(c, room->now, (BoatInput) { 0 }, room->inactive[i],
                  room->thrust_target, room->steering_target)
            : boat_net_client_service(c, room->now, input, room->inactive[i]);
        room->service_seconds[i] += (double)(clock() - started) / CLOCKS_PER_SEC;
        if (c->predict && c->phase == BOAT_NET_CLIENT_PLAYING)
            room_view_remote(room, i, tick, dt);
        // At birth-batch ticks a lossless link has delivered every birth the snapshot names.
        uint64_t mirror = c->fleet.state.tick;
        if (c->phase == BOAT_NET_CLIENT_PLAYING && mirror != room->checked[i]
            && mirror % BOAT_NET_BIRTH_TICKS == 0
            && room->history_tick[mirror % ROOM_HISTORY] == mirror) {
            room->checked[i] = mirror;
            ++room->checks;
            if (boat_fleet_hash(&c->fleet.state) == room->history_hash[mirror % ROOM_HISTORY])
                ++room->matches;
            else
                ++room->mismatches;
        }
    }
    room_flush(room);
}

static void room_run(Room *room, double seconds)
{
    // Uneven service steps exercise the accumulator and catch-up.
    for (double end = room->now + seconds; room->now < end;)
        room_step(room, (1 + (unsigned)(room->now * 977) % 3) / 240.0);
}

static unsigned active_boats(const BoatNetServer *server)
{
    unsigned n = 0;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        n += server->fleet.state.slots[i].active;
    return n;
}

static int deferred_ready(void)
{
    static Room room;
    CHECK(room_open(&room, 1, (BoatNetServerConfig) { 0 }));
    unsigned a = room_join_ex(&room, "late", NULL, 0, true);
    room_run(&room, .5);
    BoatNetClient *c = &room.clients[a];
    CHECK(c->phase == BOAT_NET_CLIENT_PLAYING);
    CHECK(c->ready_pending && !c->ready_sent);
    CHECK(room.server.slot_peer[c->slot] >= 0);
    CHECK(room.server.peers[room.server.slot_peer[c->slot]].phase == BOAT_NET_PEER_BOOTSTRAP);
    CHECK(!room.server.fleet.state.slots[c->slot].active);
    CHECK(!boat_net_client_own(c));
    uint64_t tick = c->fleet.state.tick;
    room_run(&room, .5);
    CHECK(c->fleet.state.tick > tick); // Snapshots still flow before READY.
    CHECK(boat_net_client_ready(c));
    CHECK(!c->ready_pending && c->ready_sent);
    CHECK(boat_net_client_ready(c)); // Idempotent.
    room_run(&room, 1);
    CHECK(room.server.peers[room.server.slot_peer[c->slot]].phase == BOAT_NET_PEER_READY);
    CHECK(room.server.fleet.state.slots[c->slot].active);
    CHECK(boat_net_client_own(c) != NULL);
    printf("deferred_ready: snapshots flowed before READY, then the room spawned the boat\n");
    room_close(&room);
    return 0;
}

static int lossless(void)
{
    static Room room;
    CHECK(room_open(&room, 1, (BoatNetServerConfig) { 0 }));
    unsigned a = room_join(&room, "alpha", NULL, 0);
    room_run(&room, 8);
    unsigned b = room_join(&room, "beta", NULL, 0); // Late join with a populated pool.
    room_run(&room, 12);
    const BoatNetServer *s = &room.server;
    for (unsigned i = 0; i < 2; ++i) {
        const BoatNetClient *c = &room.clients[i == 0 ? a : b];
        CHECK(c->phase == BOAT_NET_CLIENT_PLAYING);
        CHECK(boat_net_client_own(c) != NULL);
        CHECK(c->stats.decode_errors == 0 && c->stats.resyncs == 0);
        CHECK(c->roster.slots[0].state == BOAT_NET_ROSTER_PLAYING);
        CHECK(c->roster.slots[1].state == BOAT_NET_ROSTER_PLAYING);
        CHECK(strcmp(c->roster.slots[1].name, "beta") == 0);
        CHECK(c->accept.mode == BOAT_NET_MODE_FREE && !c->match_valid && !c->feed_count);
    }
    CHECK(room.clients[a].slot == 0 && room.clients[b].slot == 1);
    CHECK(active_boats(s) == 2);
    CHECK(s->fleet.state.wakes.live > 10);
    CHECK(room.checks > 100 && room.mismatches == 0);
    // The authority integrated the clients' own commands almost every tick.
    uint64_t used = 0;
    for (unsigned k = 0; k < BOAT_NET_INPUT_STATES; ++k)
        used += s->stats.input_states[k];
    CHECK(s->stats.input_states[BOAT_NET_INPUT_RECEIVED] > used * 95 / 100);
    CHECK(s->stats.output_faults == 0 && s->stats.emission_failures == 0);
    CHECK(s->stats.malformed == 0 && s->stats.forbidden == 0 && s->stats.late_commands < 20);
    CHECK(s->stats.max_backlog <= 2);
    // Both boats took their bots' throttle settings (0.65 and 0.75) from the commands.
    for (unsigned i = 0; i < 2; ++i)
        CHECK(fabsf(s->fleet.state.slots[i].boat.throttle - (.65f + .1f * (float)i)) < .02f
            || s->fleet.state.slots[i].boat.motion_state == BOAT_CAPSIZED);
    BoatNetTransportStats net = s->transport.stats(s->transport.context);
    CHECK(net.max_message <= BOAT_NET_MAX_MESSAGE && net.max_queued < BOAT_NET_MEMORY_QUEUE / 2);
    printf("lossless: 2 clients (one late) matched the authority hash at %u/%u checks; "
           "%.1f%% received commands, max message %zu bytes, bootstrap %" PRIu64 " bytes\n",
        room.matches, room.checks,
        100.0 * (double)s->stats.input_states[BOAT_NET_INPUT_RECEIVED] / (double)used,
        net.max_message, s->stats.bootstrap_bytes / s->stats.bootstraps);
    // A one-second local stall (a window opening) leaves pongs and snapshots queued; the
    // client must not then aim its commands beyond the authority's future window.
    uint64_t future = s->stats.future_commands;
    BoatNetImpairment lan = { .delay = .01 };
    boat_net_memory_impair(room.network, room.link[a], lan, lan);
    room_run(&room, .5);
    room.clients[a].ping_at = -1e9; // Leave a ping in flight when the stall begins.
    room_step(&room, 1 / 240.0);
    room.live[a] = false;
    room_run(&room, 1);
    room.live[a] = true;
    room_run(&room, 2);
    CHECK(s->stats.future_commands == future && room.clients[a].lead <= 6);
    CHECK(room.clients[a].rtt < .05);
    boat_net_memory_impair(
        room.network, room.link[a], (BoatNetImpairment) { 0 }, (BoatNetImpairment) { 0 });
    // Graceful leave: the hull goes, its wakes stay.
    unsigned live = s->fleet.state.wakes.live;
    boat_net_client_leave(&room.clients[b]);
    room.live[b] = false;
    room_run(&room, .1);
    CHECK(active_boats(s) == 1 && s->slot_peer[1] < 0 && s->stats.left == 1);
    CHECK(s->fleet.state.wakes.live + 4 >= live);
    CHECK(room.clients[a].roster.slots[1].state == BOAT_NET_ROSTER_EMPTY);
    room_close(&room);
    return 0;
}

// Send raw bytes from a fresh virtual client and return the server's first reply type.
static uint8_t raw_hello(Room *room, const uint8_t *bytes, size_t size, BoatNetMessage *reply)
{
    unsigned link;
    BoatNetTransport t = room_transport(room, &link);
    room_step(room, 1 / 240.0);
    BoatNetEvent event;
    while (boat_net_poll(&t, &event)) { }
    boat_net_send(&t, 0, BOAT_NET_CONTROL, bytes, size);
    for (int i = 0; i < 4; ++i)
        room_step(room, 1 / 240.0);
    while (boat_net_poll(&t, &event))
        if (event.kind == BOAT_NET_EVENT_MESSAGE
            && boat_net_decode(event.data, event.size, reply) == BOAT_NET_OK)
            return reply->header.type;
    return 0;
}

// A late service catches up several ticks at once; it must still stop exactly at the limit.
static int lan_discovery(void)
{
    uint8_t query[BOAT_NET_DISCOVERY_BYTES], bytes[BOAT_NET_DISCOVERY_BYTES];
    CHECK(boat_net_lan_query_encode(query) == BOAT_NET_DISCOVERY_BYTES);
    CHECK(same_bytes(query, 8, "50424c5100010007"));
    CHECK(boat_net_lan_query_decode(query, sizeof(query)));
    CHECK(!boat_net_lan_query_decode(query, sizeof(query) - 1));
    query[51] = 1;
    CHECK(!boat_net_lan_query_decode(query, sizeof(query)));

    BoatNetLanRoom room = { .version = BOAT_NET_VERSION,
        .physics_revision = BOAT_PHYSICS_REVISION,
        .content_digest = boat_net_content_digest(),
        .epoch = 0x01020304u,
        .address = 0xc0a80114u,
        .port = BOAT_NET_PORT,
        .mode = BOAT_NET_MODE_DEATHMATCH,
        .players = 2,
        .max_players = 4,
        .token = true,
        .name = "ferry" };
    CHECK(boat_net_lan_room_encode(&room, bytes) == BOAT_NET_DISCOVERY_BYTES);
    CHECK(same_bytes(bytes, 4, "50424c52"));
    CHECK(same_bytes(bytes + 20, 32,
        "01020304c0a801146068010204010566657272790000000000000000"
        "00000000"));
    BoatNetLanRoom back;
    CHECK(boat_net_lan_room_decode(bytes, sizeof(bytes), &back));
    CHECK(back.epoch == room.epoch && back.address == room.address && back.port == room.port);
    CHECK(back.mode == room.mode && back.players == 2 && back.max_players == 4 && back.token);
    CHECK(!strcmp(back.name, "ferry") && boat_net_lan_room_compatible(&back));
    char endpoint[32];
    boat_net_lan_endpoint(&back, endpoint, sizeof(endpoint));
    CHECK(!strcmp(endpoint, "192.168.1.20:24680"));
    for (size_t size = 0; size < sizeof(bytes); ++size)
        CHECK(!boat_net_lan_room_decode(bytes, size, &back));
    CHECK(!boat_net_lan_room_decode(query, sizeof(query), &back)); // A query is not an answer.
    BoatNetLanRoom bad = room;
    bad.players = 5;
    CHECK(!boat_net_lan_room_encode(&bad, bytes));
    bad = room;
    bad.port = 0;
    CHECK(!boat_net_lan_room_encode(&bad, bytes));
    bad = room;
    bad.version = BOAT_NET_VERSION + 1; // Other versions still decode, to say why not to join.
    CHECK(boat_net_lan_room_encode(&bad, bytes) && boat_net_lan_room_decode(bytes, 52, &back));
    CHECK(!boat_net_lan_room_compatible(&back));

    // The same room heard over loopback then the LAN keeps its first address and position.
    BoatNetLanList list = { 0 };
    BoatNetLanRoom local = room, other = room;
    local.address = 0x7f000001u;
    other.epoch = 9;
    boat_net_lan_list_note(&list, &local, 0);
    boat_net_lan_list_note(&list, &other, 0.5);
    room.players = 3;
    boat_net_lan_list_note(&list, &room, 1);
    CHECK(list.count == 2 && list.rooms[0].address == 0x7f000001u && list.rooms[0].players == 3);
    boat_net_lan_list_expire(&list, 4.2, 3.5);
    CHECK(list.count == 1 && list.rooms[0].epoch == 0x01020304u);
    for (uint32_t i = 0; i < 2 * BOAT_NET_LAN_ROOMS; ++i) {
        other.epoch = 100 + i;
        boat_net_lan_list_note(&list, &other, 5);
    }
    CHECK(list.count == BOAT_NET_LAN_ROOMS);
    return 0;
}

static int tick_limit(void)
{
    static Room room;
    CHECK(room_open(&room, 3, (BoatNetServerConfig) { 0 }));
    room.server.tick_limit = 10;
    CHECK(boat_net_server_service(&room.server, 0));
    CHECK(boat_net_server_service(&room.server, 8.0 / BOAT_TICK_HZ));
    CHECK(room.server.fleet.state.tick == 8);
    CHECK(boat_net_server_service(&room.server, 14.0 / BOAT_TICK_HZ));
    CHECK(room.server.fleet.state.tick == 10);
    CHECK(boat_net_server_service(&room.server, 1));
    CHECK(room.server.fleet.state.tick == 10);
    room_close(&room);
    return 0;
}

static int rejections(void)
{
    static Room room;
    CHECK(room_open(&room, 2, (BoatNetServerConfig) { .max_players = 1 }));
    unsigned first = room_join(&room, "first", NULL, 0);
    room_run(&room, 1);
    CHECK(room.clients[first].phase == BOAT_NET_CLIENT_PLAYING);
    uint64_t hash = boat_fleet_hash(&room.server.fleet.state);
    (void)hash;
    unsigned full = room_join(&room, "second", NULL, 0);
    unsigned token = room_join(&room, "third", "wrong", 0);
    room_run(&room, .5);
    CHECK(room.clients[full].phase == BOAT_NET_CLIENT_REJECTED);
    CHECK(room.clients[full].reject.reason == BOAT_NET_REJECT_FULL);
    CHECK(room.clients[token].phase == BOAT_NET_CLIENT_REJECTED);
    CHECK(room.clients[token].reject.reason == BOAT_NET_REJECT_TOKEN);
    CHECK(strcmp(boat_net_client_failure(&room.clients[token]), "wrong room token") == 0);
    // Crafted Hellos: another protocol version, handling revision and content.
    static BoatNetMessage reply;
    uint8_t bytes[BOAT_NET_MAX_MESSAGE];
    BoatNetMessage hello = { .header = { .type = BOAT_NET_HELLO },
        .hello = { BOAT_PHYSICS_REVISION, boat_net_content_digest(), "x", "room", "" } };
    size_t size = boat_net_encode(&hello, bytes);
    bytes[5] = 1; // A v1 client meets a v2 room.
    CHECK(raw_hello(&room, bytes, size, &reply) == BOAT_NET_REJECT);
    CHECK(
        reply.reject.reason == BOAT_NET_REJECT_VERSION && reply.reject.version == BOAT_NET_VERSION);
    hello.hello.physics_revision = BOAT_PHYSICS_REVISION + 1;
    size = boat_net_encode(&hello, bytes);
    CHECK(raw_hello(&room, bytes, size, &reply) == BOAT_NET_REJECT);
    CHECK(reply.reject.reason == BOAT_NET_REJECT_PHYSICS);
    CHECK(reply.reject.physics_revision == BOAT_PHYSICS_REVISION);
    hello.hello.physics_revision = BOAT_PHYSICS_REVISION;
    hello.hello.content_digest ^= 1;
    size = boat_net_encode(&hello, bytes);
    CHECK(raw_hello(&room, bytes, size, &reply) == BOAT_NET_REJECT);
    CHECK(reply.reject.reason == BOAT_NET_REJECT_CONTENT);
    uint8_t garbage[40] = { 0x50, 0x42, 0x4e, 0x54, 0, BOAT_NET_VERSION, BOAT_NET_HELLO };
    CHECK(raw_hello(&room, garbage, sizeof(garbage), &reply) == BOAT_NET_REJECT);
    CHECK(reply.reject.reason == BOAT_NET_REJECT_MALFORMED);
    const BoatNetServerStats *s = &room.server.stats;
    CHECK(s->rejected[BOAT_NET_REJECT_FULL] == 1 && s->rejected[BOAT_NET_REJECT_TOKEN] == 1);
    CHECK(s->rejected[BOAT_NET_REJECT_VERSION] == 1 && s->rejected[BOAT_NET_REJECT_PHYSICS] == 1);
    CHECK(s->rejected[BOAT_NET_REJECT_CONTENT] == 1);
    CHECK(s->rejected[BOAT_NET_REJECT_MALFORMED] == 1);
    // No rejected peer ever held a slot or a boat.
    CHECK(s->accepted == 1 && active_boats(&room.server) == 1);
    CHECK(room.clients[first].phase == BOAT_NET_CLIENT_PLAYING);
    for (unsigned peer = 0; peer < BOAT_NET_MAX_PEERS; ++peer)
        CHECK(peer == (unsigned)room.server.slot_peer[room.clients[first].slot]
            || room.server.peers[peer].phase == BOAT_NET_PEER_FREE);
    printf("rejections: full, token, version, handling, content and malformed Hellos refused "
           "before any slot\n");
    room_close(&room);
    return 0;
}

// Send a crafted message as client I, using its epoch and the next sequence on its channel.
static void inject(Room *room, unsigned i, BoatNetMessage m)
{
    BoatNetClient *c = &room->clients[i];
    BoatNetChannel channel = boat_net_channel(m.header.type);
    m.header.epoch = c->epoch;
    m.header.sequence = c->sequence[channel]++;
    uint8_t bytes[BOAT_NET_MAX_MESSAGE];
    size_t size = boat_net_encode(&m, bytes);
    if (size)
        boat_net_send(&c->transport, 0, channel, bytes, size);
}

// Turn slot I upside down on the authority; the ordinary capsize rule latches it.
static void overturn(BoatNetServer *server, unsigned i)
{
    Boat *boat = &server->fleet.state.slots[i].boat;
    boat->roll = PI;
    boat->roll_velocity = 0;
}

static bool same_match(const BoatNetClient *c, const BoatNetServer *s)
{
    BoatNetMatchState state;
    boat_net_match_state_of(&s->match, &state);
    return c->match_valid && boat_net_match_state_equal(&c->match, &state);
}

// Deathmatch rooms (protocol v2): scoreboards and events reach every client, including one that
// joins mid-round, and a capsize credited by the authority appears in each kill feed by name.
static int deathmatch(void)
{
    static Room room;
    BoatMatchRules rules = boat_match_default_rules();
    rules.round_ticks = 8 * BOAT_TICK_HZ;
    rules.intermission_ticks = 2 * BOAT_TICK_HZ;
    CHECK(room_open(&room, 11, (BoatNetServerConfig) { .deathmatch = true, .match = rules }));
    BoatNetServer *s = &room.server;
    unsigned a = room_join(&room, "alpha", NULL, 0);
    room_run(&room, 1);
    CHECK(room.clients[a].accept.mode == BOAT_NET_MODE_DEATHMATCH);
    CHECK(same_match(&room.clients[a], s) && room.clients[a].match.phase == BOAT_MATCH_WARMUP);
    unsigned b = room_join(&room, "beta", NULL, 0);
    room_run(&room, 3); // Past the new lives' credit grace.
    CHECK(s->match.phase == BOAT_MATCH_PLAYING && s->match.round == 1);
    for (unsigned i = 0; i < 2; ++i) {
        const BoatNetClient *c = &room.clients[i];
        CHECK(same_match(c, s) && c->feed_count == 1);
        CHECK(c->feed[0].event.kind == BOAT_MATCH_ROUND_START && c->feed[0].event.round == 1);
    }
    // Beta capsizes after a wake touch by alpha: +1 alpha, and both feeds name them.
    s->match.slots[1].touch
        = (BoatMatchTouch) { .tick = s->fleet.state.tick, .slot = 0, .cause = BOAT_MATCH_WAKE };
    overturn(s, 1);
    room_run(&room, 1.5);
    CHECK(s->match.rows[0].score == 1 && s->match.rows[1].capsized == 1);
    for (unsigned i = 0; i < 2; ++i) {
        const BoatNetClient *c = &room.clients[i];
        const BoatNetFeedEntry *e = &c->feed[(c->feed_count - 1) % BOAT_NET_MATCH_FEED];
        CHECK(same_match(c, s) && c->feed_count == 2);
        CHECK(e->event.kind == BOAT_MATCH_CAPSIZE && e->event.victim == 1);
        CHECK(e->event.attacker == 0 && e->event.cause == BOAT_MATCH_WAKE);
        CHECK(e->event.victim_life == s->fleet.state.slots[1].life);
        CHECK(strcmp(e->victim, "beta") == 0 && strcmp(e->attacker, "alpha") == 0);
    }
    // A mid-round joiner receives the scoreboard during bootstrap and sees alpha's capsize.
    unsigned c3 = room_join(&room, "gamma", NULL, 0);
    room_run(&room, .5);
    CHECK(room.clients[c3].phase == BOAT_NET_CLIENT_PLAYING && same_match(&room.clients[c3], s));
    CHECK(room.clients[c3].match.slots[0].row.score == 1 && room.clients[c3].feed_count == 0);
    overturn(s, 0);
    room_run(&room, 1.5);
    CHECK(s->match.rows[0].capsized == 1); // Credited to whoever touched alpha last, if anyone.
    for (unsigned i = 0; i < 3; ++i) {
        const BoatNetClient *c = &room.clients[i];
        const BoatNetFeedEntry *e = &c->feed[(c->feed_count - 1) % BOAT_NET_MATCH_FEED];
        CHECK(same_match(c, s) && e->event.kind == BOAT_MATCH_CAPSIZE && e->event.victim == 0);
        CHECK((e->event.attacker == BOAT_MATCH_NOBODY) == (s->match.rows[0].self == 1));
    }
    // The round ends: every client keeps the announced result through the intermission.
    room_run(&room, 2.5);
    CHECK(s->match.phase == BOAT_MATCH_INTERMISSION);
    BoatNetMatchState final;
    boat_net_match_state_of(&s->match, &final);
    for (unsigned i = 0; i < 3; ++i) {
        const BoatNetClient *c = &room.clients[i];
        CHECK(same_match(c, s) && boat_net_match_state_equal(&c->result, &final));
        CHECK(
            c->feed[(c->feed_count - 1) % BOAT_NET_MATCH_FEED].event.kind == BOAT_MATCH_ROUND_END);
        CHECK(c->stats.decode_errors == 0);
    }
    // A leaver's row goes at once; the next round starts with the two who stayed.
    boat_net_client_leave(&room.clients[b]);
    room.live[b] = false;
    room_run(&room, 3);
    CHECK(s->match.phase == BOAT_MATCH_PLAYING && s->match.round == 2);
    CHECK(same_match(&room.clients[a], s) && !room.clients[a].match.slots[1].present);
    CHECK(s->stats.output_faults == 0 && room.mismatches == 0);
    printf("deathmatch: 3 clients matched the authority's scoreboard through two capsizes, "
           "a mid-round join, a round end and a leaver\n");
    room_close(&room);
    return 0;
}

static bool same_race(const BoatNetClient *c, const BoatNetServer *s)
{
    BoatNetRaceState state;
    boat_net_race_state_of(&s->race, &state);
    return c->race_valid && boat_net_race_state_equal(&c->race, &state);
}

// Race rooms (protocol v4): predicting entrants are held on the grid and released with the
// authority at GO, every mirror and prediction runs the course ferry on the authority's clock,
// a mid-race joiner waits for the next grid, and results reach every client.
static int race_room(void)
{
    static Room room;
    CHECK(room_open(&room, 71,
        (BoatNetServerConfig) { .resources = test_world(BOAT_LOCATION_RIVER),
            .race = true,
            .race_rules = boat_race_default_rules() }));
    BoatNetServer *s = &room.server;
    room.predict = room.race = true;
    unsigned a = room_join(&room, "alpha", NULL, 0);
    room_run(&room, 1);
    const BoatNetClient *ca = &room.clients[a];
    CHECK(ca->accept.mode == BOAT_NET_MODE_RACE && same_race(ca, s));
    CHECK(ca->race.phase == BOAT_RACE_WAITING && boat_net_client_own(ca)); // Boating freely.
    CHECK(ca->race_go == 0 && ca->fleet.world.water.vessel.height == 0);
    unsigned b = room_join(&room, "beta", NULL, 0);
    for (unsigned n = 0; n < 2400 && s->race.phase != BOAT_RACE_COUNTDOWN; ++n)
        room_step(&room, 1.0 / 240);
    CHECK(s->race.phase == BOAT_RACE_COUNTDOWN && s->race.round == 1);
    room_run(&room, 1);
    for (unsigned i = 0; i < 2; ++i) {
        const BoatNetClient *c = &room.clients[i];
        const BoatVesselWake *ferry = &s->fleet.world.water.vessel;
        CHECK(same_race(c, s) && c->race.slots[c->slot].current);
        CHECK(c->race_go == s->race.go_tick);
        CHECK(c->fleet.world.water.vessel.started == ferry->started && ferry->height > 0);
        CHECK(c->prediction.fleet.world.water.vessel.started == ferry->started);
        CHECK(c->prediction.release_tick == s->race.go_tick);
        // Full throttle on the grid: both the mirror and the prediction stay put.
        const BoatFleetSlot *own = &c->prediction.fleet.state.slots[c->slot];
        CHECK(c->prediction.fleet.state.tick > c->fleet.state.tick && own->held);
        CHECK(Vector2Equals(own->boat.position, s->fleet.state.slots[c->slot].boat.position));
        CHECK(own->boat.throttle > .5f);
    }
    // Through GO: the prediction releases on the authority's tick, so nothing needs correcting.
    for (unsigned i = 0; i < 2; ++i)
        room.clients[i].prediction.stats = (BoatPredictionStats) { 0 };
    room_run(&room, 4);
    CHECK(s->race.phase == BOAT_RACE_RACING);
    for (unsigned i = 0; i < 2; ++i) {
        const BoatPredictionStats *p = &room.clients[i].prediction.stats;
        CHECK(p->corrections > 50 && p->snaps == 0 && p->max_error < .01f);
        CHECK(!room.clients[i].prediction.fleet.state.slots[room.clients[i].slot].held);
    }
    // A joiner during the race waits unplaced, with the rows and clock of the round.
    unsigned g = room_join(&room, "gamma", NULL, 0);
    room_run(&room, 1);
    const BoatNetClient *cg = &room.clients[g];
    CHECK(cg->phase == BOAT_NET_CLIENT_PLAYING && same_race(cg, s) && !boat_net_client_own(cg));
    CHECK(cg->race.slots[cg->slot].present && !cg->race.slots[cg->slot].entrant);
    CHECK(cg->race_go == s->race.go_tick && cg->fleet.world.water.vessel.height > 0);
    for (unsigned n = 0; n < 120 * 240 && s->race.phase != BOAT_RACE_RESULTS; ++n)
        room_step(&room, 1.0 / 240);
    CHECK(s->race.phase == BOAT_RACE_RESULTS);
    room_run(&room, 3); // Every finisher parked.
    for (unsigned i = 0; i < 3; ++i) {
        const BoatNetClient *c = &room.clients[i];
        unsigned finishes = 0;
        for (uint64_t n = c->race_feed_count > BOAT_NET_MATCH_FEED
                ? c->race_feed_count - BOAT_NET_MATCH_FEED
                : 0;
            n < c->race_feed_count; ++n)
            finishes += c->race_feed[n % BOAT_NET_MATCH_FEED].event.kind == BOAT_RACE_FINISH;
        CHECK(same_race(c, s) && boat_net_race_state_equal(&c->race_result, &c->race));
        CHECK(c->race_result.placed == s->race.placed && finishes > 0);
        CHECK(c->stats.decode_errors == 0);
    }
    for (unsigned i = 0; i < 2; ++i)
        CHECK(s->race.entrants[i].status == BOAT_RACE_FINISHED && s->race.slots[i].parked);
    double first = (double)s->race.entrants[0].finish / BOAT_RACE_SUBTICKS / BOAT_TICK_HZ;
    double second = (double)s->race.entrants[1].finish / BOAT_RACE_SUBTICKS / BOAT_TICK_HZ;
    // The next grid enters all three.
    for (unsigned n = 0; n < 12 * 240 && s->race.round < 2; ++n)
        room_step(&room, 1.0 / 240);
    room_run(&room, .5);
    CHECK(s->race.round == 2 && s->race.phase == BOAT_RACE_GRID);
    CHECK(same_race(cg, s) && cg->race.slots[cg->slot].current && boat_net_client_own(cg));
    CHECK(s->stats.output_faults == 0 && room.mismatches == 0 && room.matches > 100);
    printf("race room: 2 predicted entrants held and released at GO (max own correction "
           "%.4f m), finished in %.2f s and %.2f s; a mid-race joiner gridded next round; "
           "%u/%u exact mirrors\n",
        (double)fmaxf(
            room.clients[a].prediction.stats.max_error, room.clients[b].prediction.stats.max_error),
        first, second, room.matches, room.checks);
    room_close(&room);
    return 0;
}

// Race holds in prediction: replays from authoritative states before GO release the grid
// holds at GO as the authority does, so the predicted launch matches it bit for bit.
static int prediction_release(void)
{
    static BoatFleet authority, before;
    static BoatPrediction p, stuck;
    const BoatWorldResources *river = test_world(BOAT_LOCATION_RIVER);
    CHECK(boat_fleet_init(&authority, river,
        boat_world_test_water(river->definition, false, false, false), BOAT_OBJECT_LIFE_TICKS,
        BOAT_OBJECT_DEFAULT_HEIGHT));
    const BoatRaceCourse *course = boat_race_river_course();
    for (unsigned i = 0; i < 2; ++i) {
        Boat boat;
        boat_reset(&boat);
        boat.position = course->grid[i];
        boat.heading = atan2f(course->start.direction.x, -course->start.direction.y);
        boat_fleet_place(&authority, i, boat);
        boat_fleet_hold(&authority, i, true);
    }
    uint64_t start = authority.state.tick, go = start + 12;
    boat_net_race_bind(&authority, BOAT_NET_COURSE_RIVER, go);
    BoatNetSlotInput inputs[BOAT_FLEET_SLOTS] = { 0 };
    boat_prediction_reset(&p, &authority, inputs, 0);
    p.release_tick = go;
    stuck = p;
    stuck.fleet.world.water.objects = &stuck.fleet.state.wakes;
    stuck.release_tick = 0;
    BoatNetCommand full
        = boat_net_command((BoatInput) { .steering_rate = .3f, .throttle_rate = 1 }, 0);
    BoatInput steps[BOAT_FLEET_SLOTS] = { boat_net_command_input(full) };
    for (uint64_t t = start + 1; t <= start + 30; ++t) {
        boat_prediction_record(&p, t, authority.state.slots[0].life, full);
        boat_prediction_record(&stuck, t, authority.state.slots[0].life, full);
        if (authority.state.tick == go) // The race releases after observing the GO tick.
            for (unsigned i = 0; i < 2; ++i)
                boat_fleet_hold(&authority, i, false);
        if (authority.state.tick == go - 4) {
            before = authority;
            before.world.water.objects = &before.state.wakes;
        }
        boat_fleet_step(&authority, steps);
    }
    CHECK(boat_prediction_advance(&p, start + 30) == 30);
    CHECK(boat_fleet_hash(&p.fleet.state) == boat_fleet_hash(&authority.state));
    CHECK(Vector2Distance(p.fleet.state.slots[0].boat.position, course->grid[0]) > 0);
    CHECK(boat_prediction_advance(&stuck, start + 30) == 30);
    CHECK(boat_fleet_hash(&stuck.fleet.state) != boat_fleet_hash(&authority.state));
    // A correction from a held state four ticks before GO replays through the release too.
    CHECK(boat_prediction_correct(&p, &before, inputs, NULL));
    CHECK(p.stats.max_replay == 22 && p.error[0].position.x == 0 && !p.error[0].snap);
    CHECK(boat_fleet_hash(&p.fleet.state) == boat_fleet_hash(&authority.state));
    printf("prediction release: held launches at GO replay bit-identically from before GO\n");
    return 0;
}

// A1.5: a prediction borrows its authority's owner. Releasing another room's owner (and its
// authority and prediction) mid-run leaves the surviving prediction replaying bit-identically.
static int prediction_owners(void)
{
    const BoatWorldDefinition *river = boat_location(BOAT_LOCATION_RIVER);
    static BoatWorldResources kept, released;
    static BoatFleet authority[2], before;
    static BoatPrediction p[2];
    CHECK(boat_world_resources_init(&kept, river) && boat_world_resources_init(&released, river));
    const BoatWorldResources *owners[2] = { &kept, &released };
    const BoatRaceCourse *course = boat_race_river_course();
    BoatNetSlotInput inputs[BOAT_FLEET_SLOTS] = { 0 };
    for (int r = 0; r < 2; ++r) {
        CHECK(boat_fleet_init(&authority[r], owners[r], boat_water_gentle(), BOAT_OBJECT_LIFE_TICKS,
            BOAT_OBJECT_DEFAULT_HEIGHT));
        for (unsigned i = 0; i < 2; ++i) {
            Boat boat;
            boat_reset(&boat);
            boat.position = course->grid[i];
            boat.heading = atan2f(course->start.direction.x, -course->start.direction.y);
            boat_fleet_place(&authority[r], i, boat);
        }
        boat_prediction_reset(&p[r], &authority[r], inputs, 0);
        CHECK(p[r].fleet.world.resources == owners[r]);
    }
    uint64_t start = authority[0].state.tick;
    for (uint64_t t = start + 1; t <= start + 30; ++t) {
        BoatNetCommand command = boat_net_command(
            (BoatInput) { .steering_rate = t % 20 < 10 ? .4f : -.4f, .throttle_rate = 1 }, 0);
        BoatInput steps[BOAT_FLEET_SLOTS] = { boat_net_command_input(command) };
        for (int r = 0; r < (t <= start + 15 ? 2 : 1); ++r) {
            boat_prediction_record(&p[r], t, authority[r].state.slots[0].life, command);
            boat_fleet_step(&authority[r], steps);
        }
        if (t == start + 15)
            boat_world_resources_destroy(&released);
        if (t == start + 20) {
            before = authority[0];
            before.world.water.objects = &before.state.wakes;
        }
    }
    CHECK(boat_prediction_advance(&p[0], start + 30) == 30);
    CHECK(boat_fleet_hash(&p[0].fleet.state) == boat_fleet_hash(&authority[0].state));
    CHECK(boat_prediction_correct(&p[0], &before, inputs, NULL));
    CHECK(p[0].stats.max_replay == 10 && p[0].fleet.world.resources == &kept);
    CHECK(boat_fleet_hash(&p[0].fleet.state) == boat_fleet_hash(&authority[0].state));
    boat_world_resources_destroy(&kept);
    puts("prediction owners: the surviving owner's prediction replays bit-identically");
    return 0;
}

static int authority_rules(void)
{
    static Room room;
    CHECK(room_open(&room, 3, (BoatNetServerConfig) { 0 }));
    unsigned i = room_join(&room, "driver", NULL, 0);
    room_run(&room, 3);
    BoatNetServer *s = &room.server;
    BoatNetClient *c = &room.clients[i];
    CHECK(boat_net_client_own(c));
    unsigned slot = c->slot;
    // Silence: repeat for six ticks, then zero, then neutral from 250 ms.
    room.live[i] = false;
    float throttle = s->fleet.state.slots[slot].boat.throttle;
    CHECK(throttle > .5f);
    uint8_t seen[160];
    unsigned ticks = 0;
    while (ticks < 160) {
        uint64_t before = s->fleet.state.tick;
        room_step(&room, 1 / 240.0);
        if (s->fleet.state.tick != before)
            seen[ticks++] = s->inputs[slot].state;
    }
    unsigned first_repeat = 0, first_zero = 0, first_neutral = 0;
    for (unsigned k = 0; k < ticks; ++k) {
        if (!first_repeat && seen[k] == BOAT_NET_INPUT_REPEATED)
            first_repeat = k + 1;
        if (!first_zero && seen[k] == BOAT_NET_INPUT_ZERO)
            first_zero = k + 1;
        if (!first_neutral && seen[k] == BOAT_NET_INPUT_NEUTRAL)
            first_neutral = k + 1;
    }
    CHECK(first_repeat && first_zero && first_neutral);
    CHECK(first_zero - first_repeat == BOAT_NET_REPEAT_TICKS);
    CHECK(first_neutral - first_repeat == BOAT_NET_NEUTRAL_TICKS - 1);
    CHECK(seen[ticks - 1] == BOAT_NET_INPUT_NEUTRAL);
    CHECK(s->fleet.state.slots[slot].boat.throttle < throttle - .3f);
    CHECK(s->fleet.state.slots[slot].active); // Still physical, never removed for silence.
    printf("input loss: repeat %u ticks, zero until tick %u, then neutral (throttle %.2f -> "
           "%.2f)\n",
        first_zero - first_repeat, first_neutral - first_repeat, throttle,
        s->fleet.state.slots[slot].boat.throttle);

    // Immutable commands, late/future/stale-life/stale-sequence rejection.
    uint32_t life = s->fleet.state.slots[slot].life;
    uint64_t target = s->fleet.state.tick + 10;
    BoatNetMessage batch = { .header = { .type = BOAT_NET_INPUT_BATCH, .tick = target },
        .input = { .life = life, .count = 1 } };
    batch.input.commands[0] = boat_net_command((BoatInput) { .steering_rate = 1 }, 0);
    inject(&room, i, batch);
    batch.input.commands[0] = boat_net_command((BoatInput) { .steering_rate = -1 }, 0);
    inject(&room, i, batch); // Same tick, other command: ignored.
    BoatNetServerStats before = s->stats;
    room_step(&room, 1 / 480.0);
    CHECK(s->stats.duplicate_commands == before.duplicate_commands + 1);
    CHECK(s->control[slot].ring[target % BOAT_NET_INPUT_RING].command.steering == 127);
    batch.header.tick = s->fleet.state.tick; // Already finalized.
    inject(&room, i, batch);
    batch.header.tick = s->fleet.state.tick + BOAT_NET_MAX_FUTURE_TICKS + 1;
    inject(&room, i, batch);
    batch.header.tick = target + 1;
    batch.input.life = life + 1;
    inject(&room, i, batch);
    room_step(&room, 1 / 480.0);
    CHECK(s->stats.late_commands >= before.late_commands + 1);
    CHECK(s->stats.future_commands == before.future_commands + 1);
    CHECK(s->stats.stale_life_commands == before.stale_life_commands + 1);
    // An older sequence on the command channel is discarded as stale.
    batch.input.life = life;
    BoatNetMessage old = batch;
    old.header.epoch = c->epoch;
    old.header.sequence = c->sequence[BOAT_NET_STATE] - 10;
    uint8_t bytes[BOAT_NET_MAX_MESSAGE];
    size_t size = boat_net_encode(&old, bytes);
    boat_net_send(&c->transport, 0, BOAT_NET_STATE, bytes, size);
    // Another session's epoch never reaches the room.
    old.header.epoch = c->epoch + 1;
    old.header.sequence = c->sequence[BOAT_NET_STATE] + 100;
    size = boat_net_encode(&old, bytes);
    boat_net_send(&c->transport, 0, BOAT_NET_STATE, bytes, size);
    room_step(&room, 1 / 480.0);
    CHECK(s->stats.stale_packets >= before.stale_packets + 2);
    CHECK(s->control[slot].ring[(target + 1) % BOAT_NET_INPUT_RING].tick != target + 1);
    room.live[i] = true;
    room_run(&room, 1);
    CHECK(s->control[slot].ring[target % BOAT_NET_INPUT_RING].tick != target
        || s->control[slot].ring[target % BOAT_NET_INPUT_RING].command.steering == 127);

    // Inactive clients are neutralized at once, not after 250 ms.
    room.inactive[i] = true;
    room_run(&room, .3);
    CHECK(s->inputs[slot].state == BOAT_NET_INPUT_NEUTRAL);
    CHECK(s->inputs[slot].used.flags & BOAT_NET_COMMAND_INACTIVE);
    room.inactive[i] = false;
    room_run(&room, .5);
    CHECK(s->inputs[slot].state == BOAT_NET_INPUT_RECEIVED);

    // Reset: one request is one new life; a repeat naming the old life does nothing more.
    life = s->fleet.state.slots[slot].life;
    boat_net_client_request_reset(c);
    room_run(&room, .002);
    CHECK(boat_net_client_reset_state(c, room.now, NULL) != BOAT_NET_RESET_NONE);
    room_run(&room, 2);
    CHECK(s->fleet.state.slots[slot].life == life + 1);
    // The client saw the new life end its reset, within a round trip and a spawn retry.
    CHECK(boat_net_client_reset_state(c, room.now, NULL) == BOAT_NET_RESET_NONE);
    CHECK(c->stats.resets == 1 && c->stats.max_reset < .3 && c->stats.resets_dropped == 0);
    inject(&room, i,
        (BoatNetMessage) { .header = { .type = BOAT_NET_SPAWN_REQUEST }, .spawn = { 99, life } });
    room_run(&room, 1);
    CHECK(s->fleet.state.slots[slot].life == life + 1 && !s->fleet.state.slots[slot].pending);
    CHECK(boat_net_client_own(c)->life == life + 1);
    // A request the authority never queues (rate limited here) stops showing after a second.
    room.server.peers[room.link[i]].spawn_tokens = 0;
    boat_net_client_request_reset(c);
    room_run(&room, .1);
    CHECK(boat_net_client_reset_state(c, room.now, NULL) == BOAT_NET_RESET_SENT);
    room_run(&room, 1.2);
    CHECK(boat_net_client_reset_state(c, room.now, NULL) == BOAT_NET_RESET_NONE);
    CHECK(c->stats.resets_dropped == 1 && boat_net_client_own(c)->life == life + 1);

    // No client transform is accepted: world-state messages from a client are forbidden and
    // change nothing; persistent offenders are disconnected.
    BoatNetMessage forged
        = { .header = { .type = BOAT_NET_SNAPSHOT, .tick = c->fleet.state.tick } };
    BoatNetSlotInput records[BOAT_FLEET_SLOTS] = { 0 };
    BoatFleetState fake = c->fleet.state;
    fake.slots[slot].boat.position = (Vector2) { 99, 66 };
    boat_net_snapshot_of(&fake, records, &forged.snapshot);
    inject(&room, i, forged);
    room_step(&room, 1 / 240.0);
    CHECK(s->stats.forbidden == 1);
    CHECK(Vector2Distance(s->fleet.state.slots[slot].boat.position, (Vector2) { 99, 66 }) > 5);
    for (unsigned k = 0; k < BOAT_NET_MISBEHAVIOUR_LIMIT; ++k)
        inject(&room, i,
            (BoatNetMessage) { .header = { .type = BOAT_NET_WORLD_END }, .checkpoint = 1 });
    room_run(&room, .5);
    CHECK(s->stats.misbehaving == 1 && s->slot_peer[slot] < 0 && active_boats(s) == 0);
    CHECK(c->phase == BOAT_NET_CLIENT_CLOSED && c->leave == BOAT_NET_LEAVE_MISBEHAVING);
    printf("authority: immutable, late, future, stale-life, stale-sequence and foreign-epoch "
           "commands handled; inactive neutral; idempotent reset with client progress; dropped "
           "reset expires; forged state refused\n");
    room_close(&room);
    return 0;
}

static int wrap_and_flood(void)
{
    static Room room;
    uint32_t start = UINT32_MAX - 100;
    CHECK(room_open(&room, 4, (BoatNetServerConfig) { .first_sequence = start }));
    unsigned i = room_join(&room, "wrap", NULL, start);
    room_run(&room, 6);
    BoatNetClient *c = &room.clients[i];
    CHECK(c->phase == BOAT_NET_CLIENT_PLAYING);
    CHECK(c->sequence[BOAT_NET_STATE] < start && c->received[BOAT_NET_STATE] < start);
    CHECK(room.server.peers[room.link[i]].input_sequence < start);
    CHECK(c->stats.stale_packets == 0 && room.server.stats.stale_packets == 0);
    CHECK(room.checks > 30 && room.mismatches == 0);
    printf("sequence wrap: both directions wrapped with no stale discards; %u/%u hash "
           "matches\n",
        room.matches, room.checks);
    // Flood: far more packets than the bounded queue and rate allow, in one instant.
    uint64_t tick = room.server.fleet.state.tick;
    BoatNetMessage batch = { .header = { .type = BOAT_NET_INPUT_BATCH, .tick = tick + 5 },
        .input = { .life = room.server.fleet.state.slots[c->slot].life, .count = 1 } };
    for (unsigned k = 0; k < 2000; ++k)
        inject(&room, i, batch);
    BoatNetTransportStats sent = c->transport.stats(c->transport.context);
    CHECK(sent.dropped > 1000 && sent.max_queued <= BOAT_NET_MEMORY_QUEUE);
    room_run(&room, 1);
    const BoatNetServerStats *s = &room.server.stats;
    CHECK(s->rate_limited > 100 && s->max_messages_per_service <= BOAT_NET_MESSAGES_PER_SERVICE);
    CHECK(room.server.fleet.state.tick >= tick + 115 && s->max_backlog <= 2);
    CHECK(c->phase == BOAT_NET_CLIENT_PLAYING);
    printf("flood: %" PRIu64 " sends dropped at the %d-message queue, %" PRIu64
           " packets rate-limited, authority stayed on time\n",
        sent.dropped, BOAT_NET_MEMORY_QUEUE, s->rate_limited);
    room_close(&room);
    return 0;
}

static int impaired(void)
{
    static Room room;
    CHECK(room_open(&room, 5, (BoatNetServerConfig) { .waves = true }));
    // Profile F one way: 40 ms +-10, 1% loss, 0.2% duplicates, 1% reordered by 20 ms.
    BoatNetImpairment f = { .04, .01, .01, .002, .01, .02 };
    unsigned a = room_join(&room, "f-one", NULL, 0);
    unsigned b = room_join(&room, "f-two", NULL, 0);
    boat_net_memory_impair(room.network, room.link[a], f, f);
    boat_net_memory_impair(room.network, room.link[b], f, f);
    room_run(&room, 60);
    const BoatNetServerStats *s = &room.server.stats;
    for (unsigned i = 0; i < 2; ++i) {
        const BoatNetClient *c = &room.clients[i];
        CHECK(c->phase == BOAT_NET_CLIENT_PLAYING && c->stats.decode_errors == 0);
        CHECK(c->stats.resyncs <= 3);
        CHECK(room.server.fleet.state.tick - c->fleet.state.tick < 24);
        CHECK(c->rtt > .06 && c->rtt < .12);
        CHECK(c->lead >= 4 && c->lead <= 16);
        printf("impaired F: client %u rtt %.0f ms lead %u, %" PRIu64 " snapshots, %" PRIu64
               " repaired mismatches (max %.0f ms), %" PRIu64 " resyncs\n",
            i, c->rtt * 1000, c->lead, c->stats.snapshots, c->stats.repaired,
            c->stats.max_mismatch * 1000, c->stats.resyncs);
    }
    uint64_t used = 0;
    for (unsigned k = 0; k < BOAT_NET_INPUT_STATES; ++k)
        used += s->input_states[k];
    double late = 1 - (double)s->input_states[BOAT_NET_INPUT_RECEIVED] / (double)used;
    CHECK(late < .02);
    CHECK(room.matches > room.checks * 8 / 10);
    CHECK(s->output_faults == 0 && s->emission_failures == 0 && s->malformed == 0);
    BoatNetTransportStats net = room.server.transport.stats(room.server.transport.context);
    CHECK(net.max_queued < BOAT_NET_MEMORY_QUEUE);
    printf("impaired F: %.2f%% of integrated ticks lacked a command; %u/%u hash matches; "
           "queue high-water %zu\n",
        late * 100, room.matches, room.checks, net.max_queued);

    // A one-second blackout recovers; six seconds times the slot out on both ends.
    unsigned slot = room.clients[b].slot;
    boat_net_memory_blackout(room.network, room.link[b], true);
    room_run(&room, 1);
    boat_net_memory_blackout(room.network, room.link[b], false);
    room_run(&room, 3);
    CHECK(room.clients[b].phase == BOAT_NET_CLIENT_PLAYING && room.server.slot_peer[slot] >= 0);
    CHECK(!boat_net_client_stale(&room.clients[b], room.now));
    boat_net_memory_blackout(room.network, room.link[b], true);
    room_run(&room, 6);
    CHECK(room.server.slot_peer[slot] < 0 && s->timeouts == 1);
    CHECK(room.clients[b].phase == BOAT_NET_CLIENT_CLOSED);
    CHECK(room.clients[a].phase == BOAT_NET_CLIENT_PLAYING);
    printf("outages: 1 s blackout recovered; 6 s blackout timed out on both ends\n");
    room_close(&room);
    return 0;
}

static int mapped_rooms(void)
{
    static Room room;
    const int id = BOAT_LOCATION_RIVER;
    BoatMatchRules rules = boat_match_default_rules();
    CHECK(room_open(&room, 63,
        (BoatNetServerConfig) { .resources = test_world(id), .deathmatch = true, .match = rules }));
    room.predict = true;
    for (unsigned i = 0; i < 4; ++i)
        room_join(&room, "mapped", NULL, 0);
    room_run(&room, .5);
    CHECK(active_boats(&room.server) == 4);
    for (unsigned i = 0; i < 4; ++i) {
        const BoatNetClient *c = &room.clients[i];
        CHECK(c->phase == BOAT_NET_CLIENT_PLAYING && c->accept.location == id);
        CHECK(c->fleet.world.definition->id == (BoatLocationId)id);
        CHECK(c->prediction.fleet.world.definition->id == (BoatLocationId)id);
        // Mirror, prediction and authority borrow the same owner; none cloned it.
        CHECK(c->fleet.world.resources == test_world(id)
            && c->prediction.fleet.world.resources == test_world(id)
            && room.server.fleet.world.resources == test_world(id));
        const BoatFleetSlot *own = boat_net_client_own(c);
        CHECK(own && boat_hull_clear(&own->boat, &c->fleet.world, room.now));
        CHECK(boat_map_water(&c->fleet.world.resources->map, own->boat.position, 35));
        CHECK(Vector2Distance(own->boat.position, boat_location(id)->spawn) > 200);
        for (unsigned j = 0; j < i; ++j)
            CHECK(Vector2Distance(
                      own->boat.position, boat_net_client_own(&room.clients[j])->boat.position)
                > 50);
    }
    CHECK(room.server.match.phase == BOAT_MATCH_PLAYING);
    // A window/renderer loading pause must not become a huge first RTT/clock estimate.
    room.clients[0].stats.pongs = 0;
    room.clients[0].rtt = 0;
    inject(&room, 0,
        (BoatNetMessage) {
            .header = { .type = BOAT_NET_PING }, .clock = (uint64_t)(room.now * 1e6) });
    room.live[0] = false;
    room_run(&room, 1.5);
    room.live[0] = true;
    uint64_t future = room.server.stats.future_commands;
    room_run(&room, 1);
    CHECK(room.clients[0].rtt < .05 && room.clients[0].stats.pongs > 0);
    CHECK(room.server.stats.future_commands == future);
    // Put a real hull in navigable mapped water beyond the old basin decode envelope.
    Boat distant;
    bool found = false;
    for (int x = -1200; x <= 1200 && !found; x += 100)
        for (int z = -900; z <= 900 && !found; z += 100) {
            if (abs(x) < 300 && abs(z) < 300)
                continue;
            boat_reset(&distant);
            distant.position = (Vector2) { (float)x, (float)z };
            if (boat_map_water(&room.server.fleet.world.resources->map, distant.position, 10)
                && boat_hull_clear(&distant, &room.server.fleet.world, room.now))
                found = true;
        }
    CHECK(found);
    boat_fleet_place(&room.server.fleet, room.clients[0].slot, distant);
    room_run(&room, .5);
    CHECK(room.clients[0].stats.decode_errors == 0);
    CHECK(Vector2Distance(boat_net_client_own(&room.clients[0])->boat.position, distant.position)
        < 10);
    boat_net_client_request_reset(&room.clients[0]);
    room_run(&room, 1);
    CHECK(room.clients[0].stats.resets == 1);
    // Exact wake reconstruction is guaranteed on birth-batch snapshots.
    for (unsigned step = 0; step < 48
        && (room.server.fleet.state.tick % BOAT_NET_BIRTH_TICKS != 0
            || room.clients[0].fleet.state.tick != room.server.fleet.state.tick);
        ++step)
        room_step(&room, 1.0 / 240);
    CHECK(room.clients[0].fleet.state.tick == room.server.fleet.state.tick);
    CHECK(room.server.fleet.state.tick % BOAT_NET_BIRTH_TICKS == 0);
    for (unsigned i = 0; i < 4; ++i) {
        const BoatNetClient *c = &room.clients[i];
        CHECK(c->phase == BOAT_NET_CLIENT_PLAYING && c->stats.decode_errors == 0);
        CHECK(c->wake_consistent);
        CHECK(boat_fleet_hash(&c->fleet.state)
            == room.history_hash[c->fleet.state.tick % ROOM_HISTORY]);
    }
    CHECK(room.server.stats.output_faults == 0 && room.server.stats.emission_failures == 0);
    printf("mapped %s: four predicted clients, shoreline-safe spawns, distant snapshot, "
           "deathmatch round and reset; exact authoritative mirrors\n",
        boat_location(id)->name);
    room_close(&room);
    return 0;
}

static int four_player_lifecycle(void)
{
    static Room room;
    CHECK(room_open(&room, 51, (BoatNetServerConfig) { .waves = true }));
    room.predict = true;
    BoatNetImpairment f = { .04, .01, .01, .002, .01, .02 };
    for (unsigned i = 0; i < 3; ++i) {
        unsigned k = room_join(&room, "fleet", NULL, 0);
        boat_net_memory_impair(room.network, room.link[k], f, f);
    }
    room_run(&room, 8);
    CHECK(room.server.fleet.state.wakes.live > 0);
    unsigned fourth = room_join(&room, "late", NULL, 0);
    boat_net_memory_impair(room.network, room.link[fourth], f, f);
    room_run(&room, 4);
    CHECK(active_boats(&room.server) == 4);
    CHECK(room.clients[fourth].phase == BOAT_NET_CLIENT_PLAYING);
    unsigned full = room_join(&room, "full", NULL, 0);
    room_run(&room, 1);
    CHECK(room.clients[full].phase == BOAT_NET_CLIENT_REJECTED);
    CHECK(room.clients[full].reject.reason == BOAT_NET_REJECT_FULL);

    uint32_t lives[BOAT_FLEET_SLOTS];
    uint64_t early = 0;
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(boat_net_client_own(&room.clients[i]));
        CHECK(room.clients[i].stats.bootstraps == 1);
        early += room.clients[i].stats.early_bootstrap;
        lives[i] = boat_net_client_own(&room.clients[i])->life;
        boat_net_client_request_reset(&room.clients[i]);
    }
    CHECK(early > 0); // This seed delivers bootstrap traffic before control-channel Accept.
    room_run(&room, 3);
    for (unsigned i = 0; i < 4; ++i) {
        const BoatNetClient *c = &room.clients[i];
        CHECK(boat_net_client_own(c));
        CHECK(boat_net_client_own(c)->life == lives[i] + 1);
        CHECK(c->stats.resets == 1 && !c->resetting);
    }
    room.inactive[1] = true;
    room_run(&room, .5);
    CHECK(room.server.inputs[room.clients[1].slot].state == BOAT_NET_INPUT_NEUTRAL);
    CHECK(room.server.inputs[room.clients[0].slot].state == BOAT_NET_INPUT_RECEIVED);
    room.inactive[1] = false;

    // One slow peer must neither freeze the authority nor time out the other three.
    boat_net_memory_blackout(room.network, room.link[fourth], true);
    room_run(&room, 1);
    boat_net_memory_blackout(room.network, room.link[fourth], false);
    room_run(&room, 3);
    CHECK(room.clients[fourth].phase == BOAT_NET_CLIENT_PLAYING);
    CHECK(!boat_net_client_stale(&room.clients[fourth], room.now));
    unsigned slot = room.clients[fourth].slot;
    uint32_t old_life = room.server.fleet.state.slots[slot].life;
    boat_net_memory_blackout(room.network, room.link[fourth], true);
    room_run(&room, 6);
    CHECK(room.server.stats.timeouts == 1 && active_boats(&room.server) == 3);
    unsigned replacement = room_join(&room, "replacement", NULL, 0);
    boat_net_memory_impair(room.network, room.link[replacement], f, f);
    room_run(&room, 3);
    BoatNetClient *c = &room.clients[replacement];
    CHECK(c->phase == BOAT_NET_CLIENT_PLAYING && c->slot == slot);
    CHECK(boat_net_client_own(c));
    CHECK(boat_net_client_own(c)->life > old_life && active_boats(&room.server) == 4);
    uint64_t stale = room.server.stats.stale_life_commands;
    inject(&room, replacement,
        (BoatNetMessage) {
            .header = { .type = BOAT_NET_INPUT_BATCH, .tick = room.server.fleet.state.tick + 20 },
            .input = { .life = old_life, .count = 1 } });
    room_run(&room, 2);
    CHECK(room.server.stats.stale_life_commands > stale);
    // A lossless tail lets every live mirror demonstrate exact wake/checkpoint convergence.
    for (unsigned i = 0; i < room.count; ++i)
        boat_net_memory_impair(
            room.network, room.link[i], (BoatNetImpairment) { 0 }, (BoatNetImpairment) { 0 });
    room_run(&room, 2);
    // Births travel at 10 Hz, so a snapshot may still be counting one in flight: allow two batches.
    for (double end = room.now + .2; room.now < end;) {
        bool settled = true;
        for (unsigned i = 0; i < room.count; ++i)
            settled &= i == fourth || i == full || room.clients[i].wake_consistent;
        if (settled)
            break;
        room_run(&room, 1 / 240.0);
    }
    for (unsigned i = 0; i < room.count; ++i) {
        if (i == fourth || i == full)
            continue;
        c = &room.clients[i];
        CHECK(c->phase == BOAT_NET_CLIENT_PLAYING && c->stats.decode_errors == 0);
        CHECK(c->wake_consistent && c->stats.resyncs <= 3);
        uint64_t tick = c->fleet.state.tick;
        CHECK(room.history_tick[tick % ROOM_HISTORY] == tick);
        CHECK(boat_fleet_hash(&c->fleet.state) == room.history_hash[tick % ROOM_HISTORY]);
        CHECK(c->prediction.stats.max_replay <= BOAT_PREDICTION_MAX_AHEAD);
    }
    CHECK(room.server.stats.accepted == 5 && room.server.stats.max_backlog <= 2);
    CHECK(room.server.stats.output_faults == 0 && room.server.stats.emission_failures == 0);
    printf("four-player F: late join, full rejection, simultaneous resets, inactive controls, "
           "1 s recovery, 6 s timeout and slot reuse; all live mirrors converged exactly\n");
    room_close(&room);
    return 0;
}

static int resync(void)
{
    static Room room;
    CHECK(room_open(&room, 6, (BoatNetServerConfig) { 0 }));
    unsigned i = room_join(&room, "resync", NULL, 0);
    room_run(&room, 6);
    BoatNetClient *c = &room.clients[i];
    CHECK(c->fleet.state.wakes.live > 3);
    // Damage the mirror: a live record the authority never had.
    BoatWakeObject *o = &c->fleet.state.wakes.objects[127];
    o->born = c->fleet.state.tick;
    o->amplitude = .5f;
    room_run(&room, .5);
    CHECK(!c->wake_consistent && c->stats.resyncs == 0);
    room_run(&room, 1.5);
    CHECK(c->stats.resyncs == 1 && c->stats.bootstraps == 2 && room.server.stats.resyncs == 1);
    CHECK(c->wake_consistent);
    CHECK(boat_net_client_own(c) != NULL && room.server.stats.spawn_requests == 0);
    unsigned matches = room.matches;
    room_run(&room, 2);
    CHECK(room.matches > matches);
    printf("resync: a damaged mirror asked for one fresh checkpoint after 1 s and matched "
           "again\n");
    room_close(&room);
    return 0;
}

// Checkpoint/input replay: the authority's remote slots hold one bot command per 32-tick block
// and its own slot changes command every tick, so a prediction holding the remote rates and
// replaying the recorded own commands must reproduce every authoritative tick bit for bit,
// including restores from flights, landings and capsizes.
#define REPLAY_BLOCK 32
static int prediction_replay(void)
{
    static BoatFleet fleet, base;
    static BoatFleetState at8, at16;
    static BoatPrediction p;
    const unsigned own = 1;
    boat_fleet_init(&fleet, basin(), boat_world_test_water(basin()->definition, true, false, false),
        BOAT_OBJECT_LIFE_TICKS, 1.8f);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        boat_fleet_request_spawn(&fleet, i);
    BoatFleetBots bots = { 0 };
    unsigned windows = 0, matches = 0, states[4] = { 0 };
    float worst = 0;
    BoatNetSlotInput inputs8[BOAT_FLEET_SLOTS], inputs16[BOAT_FLEET_SLOTS];
    for (unsigned window = 0; window < 240; ++window) {
        // Stabilized steering need not accidentally flip this route anymore. Deliberately
        // seed an inverted authority state so checkpoint replay still covers capsized boats.
        if (window == 120) {
            Boat *boat = &fleet.state.slots[own].boat;
            boat->roll = PI;
            boat->pitch = boat->height = boat->roll_velocity = 0;
            boat->motion_state = BOAT_CAPSIZED;
        }
        uint64_t a = fleet.state.tick;
        BoatNetCommand held[BOAT_FLEET_SLOTS], own_command[REPLAY_BLOCK + 1];
        uint32_t own_life[REPLAY_BLOCK + 1];
        uint64_t hash24 = 0, hash32 = 0;
        unsigned resets = 0;
        for (unsigned t = 1; t <= REPLAY_BLOCK; ++t) {
            if (t == 9)
                resets = bots.resets;
            BoatInput bot[BOAT_FLEET_SLOTS], inputs[BOAT_FLEET_SLOTS];
            boat_fleet_bots_drive(&bots, &fleet, bot);
            for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
                if (t == 1)
                    held[i] = boat_net_command(bot[i], 0);
                BoatNetCommand command = i == own ? boat_net_command(bot[i], 0) : held[i];
                inputs[i] = boat_net_command_input(command);
            }
            own_command[t] = boat_net_command(bot[own], 0);
            own_life[t] = fleet.state.slots[own].life;
            boat_fleet_step(&fleet, inputs);
            BoatNetSlotInput *record = t == 8 ? inputs8 : t == 16 ? inputs16 : NULL;
            for (unsigned i = 0; record && i < BOAT_FLEET_SLOTS; ++i)
                record[i] = (BoatNetSlotInput) { .used = i == own ? own_command[t] : held[i] };
            if (t == 8)
                boat_fleet_save(&fleet, &at8);
            if (t == 16)
                boat_fleet_save(&fleet, &at16);
            if (t == 24)
                hash24 = boat_fleet_hash(&fleet.state);
            if (t == 32)
                hash32 = boat_fleet_hash(&fleet.state);
        }
        ++windows;
        // A reset request is an authority lifecycle event the replay cannot know about.
        if (bots.resets != resets)
            continue;
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            if (at8.slots[i].active)
                ++states[at8.slots[i].boat.motion_state];
        base = fleet;
        boat_fleet_restore(&base, &at8);
        boat_prediction_reset(&p, &base, inputs8, own);
        for (unsigned t = 9; t <= REPLAY_BLOCK; ++t)
            boat_prediction_record(&p, a + t, own_life[t], own_command[t]);
        CHECK(boat_prediction_advance(&p, a + 24) == 16);
        CHECK(boat_fleet_hash(&p.fleet.state) == hash24);
        boat_fleet_restore(&base, &at16);
        boat_prediction_correct(&p, &base, inputs16, NULL);
        CHECK(p.fleet.state.tick == a + 24 && p.stats.max_replay == 8);
        CHECK(boat_fleet_hash(&p.fleet.state) == hash24);
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
            const BoatPredictionError *e = &p.error[i];
            if (!p.fleet.state.slots[i].active)
                continue;
            float size = Vector2Length(e->position) + fabsf(e->heading) + fabsf(e->height);
            worst = fmaxf(worst, size);
            CHECK(!e->snap && size == 0);
        }
        CHECK(boat_prediction_advance(&p, a + 32) == 8);
        CHECK(boat_fleet_hash(&p.fleet.state) == hash32);
        ++matches;
    }
    CHECK(matches * 10 > windows * 9 && states[BOAT_AIRBORNE] > 0 && states[BOAT_LANDING] > 0
        && states[BOAT_CAPSIZED] > 0);
    printf("prediction replay: %u/%u windows (the rest held a bot reset request) bit-identical "
           "after advance, correction and replay; base states %u waterborne, %u airborne, %u "
           "landing, %u capsized; "
           "%u bot resets\n",
        matches, windows, states[BOAT_WATERBORNE], states[BOAT_AIRBORNE], states[BOAT_LANDING],
        states[BOAT_CAPSIZED], bots.resets);
    return 0;
}

// One bot-driven slot: a birth the mirror lacks is kept from the speculative pool only when it
// completes the snapshot digest; a wrong one holds the correction, then corrects without it,
// and the repaired mirror restores bit-identity; an early authoritative birth (even repeated)
// replaces the replay's own version of that pool slot and tick.
#define WAKE_TICKS 2400
static int prediction_wakes(void)
{
    static BoatFleet fleet, mirror;
    static BoatFleetState base, before_birth, at_n, history[WAKE_TICKS];
    static BoatNetCommand commands[WAKE_TICKS];
    static BoatPrediction p;
    boat_fleet_init(&fleet, basin(),
        boat_world_test_water(basin()->definition, false, false, false), BOAT_OBJECT_LIFE_TICKS,
        1.8f);
    boat_fleet_request_spawn(&fleet, 0);
    BoatFleetBots bots = { 0 };
    BoatNetSlotInput used[BOAT_FLEET_SLOTS] = { 0 };
    uint64_t born = 0;
    unsigned slot = 0;
    while (fleet.state.tick + 1 < WAKE_TICKS) {
        BoatInput inputs[BOAT_FLEET_SLOTS] = { 0 };
        uint64_t emitted = fleet.state.wakes.emitted;
        commands[fleet.state.tick + 1] = boat_net_command(
            fleet.state.slots[0].active ? boat_fleet_bot_input(&bots, &fleet, 0) : inputs[0], 0);
        inputs[0] = boat_net_command_input(commands[fleet.state.tick + 1]);
        boat_fleet_step(&fleet, inputs);
        history[fleet.state.tick] = fleet.state;
        if (!born && fleet.state.tick > 600 && fleet.state.wakes.emitted != emitted) {
            born = fleet.state.tick;
            for (unsigned i = 0; i < BOAT_OBJECT_PER_BOAT; ++i)
                if (fleet.state.wakes.objects[i].born == born)
                    slot = i;
        }
    }
    CHECK(born && born + 12 < WAKE_TICKS);
    base = history[born - 4];
    before_birth = history[born - 1];
    at_n = history[born + 4];
    uint64_t digest = boat_net_wake_digest(&at_n.wakes);
    uint64_t target = boat_fleet_hash(&history[born + 12]);
    mirror = fleet;
    // The mirror at born + 4 whose birth batch has not arrived: slot as it was before.
    BoatFleetState missing = at_n;
    missing.wakes.objects[slot] = before_birth.wakes.objects[slot];
    CHECK(boat_net_wake_digest(&missing.wakes) != digest);
#define PREDICT_FROM(state)                                                                        \
    do {                                                                                           \
        boat_fleet_restore(&mirror, &(state));                                                     \
        boat_prediction_reset(&p, &mirror, used, 0);                                               \
        for (uint64_t t = (state).tick + 1; t <= born + 12; ++t)                                   \
            boat_prediction_record(&p, t, (state).slots[0].life, commands[t]);                     \
        boat_prediction_advance(&p, born + 12);                                                    \
    } while (0)
    PREDICT_FROM(base);
    CHECK(boat_fleet_hash(&p.fleet.state) == target);
    boat_fleet_restore(&mirror, &missing);
    BoatPredictionWakes wakes = { digest, NULL, 0, true };
    CHECK(boat_prediction_correct(&p, &mirror, used, &wakes));
    CHECK(p.stats.carried == 1 && p.stats.wakes_vanished == 0);
    CHECK(boat_fleet_hash(&p.fleet.state) == target);

    // A speculative record that differs cannot complete the digest: hold, then correct.
    PREDICT_FROM(base);
    p.fleet.state.wakes.objects[slot].amplitude *= .9f;
    uint32_t serial = p.correction;
    boat_fleet_restore(&mirror, &missing);
    CHECK(!boat_prediction_correct(&p, &mirror, used, &wakes));
    CHECK(p.correction == serial && p.stats.held_corrections == 1 && p.stats.carried == 0);
    wakes.hold_incomplete = false;
    CHECK(boat_prediction_correct(&p, &mirror, used, &wakes));
    CHECK(p.stats.incomplete_corrections == 1 && p.stats.wakes_vanished == 1);
    CHECK(p.fleet.state.wakes.objects[slot].born != born);
    // The repaired mirror (its birth batch arrived) restores bit-identity.
    boat_fleet_restore(&mirror, &at_n);
    CHECK(boat_prediction_correct(&p, &mirror, used, &wakes));
    CHECK(boat_fleet_hash(&p.fleet.state) == target && p.stats.wakes_appeared == 1);

    // Early birth: the mirror is before the birth, and the client's commands near it differ
    // from what the authority integrated, so its replayed record differs.
    PREDICT_FROM(base);
    for (uint64_t t = born - 2; t <= born; ++t) {
        BoatNetCommand wrong = commands[t];
        wrong.steering = (int8_t)(wrong.steering > 0 ? wrong.steering - 60 : wrong.steering + 60);
        boat_prediction_record(&p, t, base.slots[0].life, wrong);
    }
    BoatNetBirth early[2] = { { (uint8_t)slot, history[born].wakes.objects[slot] } };
    early[1] = early[0]; // A duplicate copy changes nothing.
    boat_fleet_restore(&mirror, &history[born - 3]);
    BoatPredictionWakes ahead = { boat_net_wake_digest(&history[born - 3].wakes), early, 2, true };
    CHECK(boat_prediction_correct(&p, &mirror, used, &ahead));
    const BoatWakeObject *o = &p.fleet.state.wakes.objects[slot];
    CHECK(p.stats.early_applied >= 1 && o->born == born);
    const BoatWakeObject *e = &early[0].record;
    CHECK(o->position.x == e->position.x && o->position.y == e->position.y
        && o->direction.x == e->direction.x && o->length == e->length
        && o->amplitude == e->amplitude && o->inverse_speed == e->inverse_speed);
    printf("prediction wakes: a missing birth carried only when it completes the digest; a "
           "wrong one held, then corrected without it and repaired bit-identically; an early "
           "birth (duplicated) replaced the replayed record of slot %u tick %" PRIu64 "\n",
        slot, born);
    return 0;
}

// Remote view rules on hand-built snapshots and poses.
static int remote_view(void)
{
    static BoatRemoteHistory h;
    BoatFleetSlot slots[BOAT_FLEET_SLOTS] = { 0 };
    boat_remote_history_reset(&h);
    Boat pose;
    uint32_t life;
    CHECK(boat_remote_history_pose(&h, 1, 100, &pose, &life) == BOAT_REMOTE_NONE);
    slots[1] = (BoatFleetSlot) { .active = true, .life = 1, .boat = { .heading = 3.1f } };
    boat_remote_history_push(&h, 100, slots);
    slots[1].boat = (Boat) { .position = { 4, 0 }, .heading = -3.1f, .pitch = 7 };
    boat_remote_history_push(&h, 104, slots);
    boat_remote_history_push(&h, 104, slots); // Not newer: ignored.
    CHECK(h.count == 2);
    CHECK(boat_remote_history_pose(&h, 1, 102, &pose, &life) == BOAT_REMOTE_INTERPOLATED);
    // Halfway, heading the short way across the wrap, pitch continuous (a flip is not undone).
    CHECK(fabsf(pose.position.x - 2) < 1e-5f && fabsf(fabsf(pose.heading) - PI) < 1e-3f);
    CHECK(fabsf(pose.pitch - 3.5f) < 1e-5f && life == 1);
    slots[1] = (BoatFleetSlot) {
        .active = true, .life = 2, .boat = { .position = { 50, 0 }, .velocity = { 6, 0 } }
    };
    boat_remote_history_push(&h, 108, slots);
    // Never blended across lives: the newer life is shown.
    CHECK(boat_remote_history_pose(&h, 1, 106, &pose, &life) == BOAT_REMOTE_INTERPOLATED);
    CHECK(life == 2 && pose.position.x == 50);
    CHECK(boat_remote_history_pose(&h, 1, 110, &pose, &life) == BOAT_REMOTE_EXTRAPOLATED);
    CHECK(fabsf(pose.position.x - 50.1f) < 1e-4f);
    CHECK(boat_remote_history_pose(&h, 1, 130, &pose, &life) == BOAT_REMOTE_HELD);
    CHECK(fabsf(pose.position.x - 50.6f) < 1e-4f); // 100 ms of extrapolation, then held.
    // Before the history: the oldest pose of the current life.
    CHECK(boat_remote_history_pose(&h, 1, 90, &pose, &life) == BOAT_REMOTE_HELD);
    CHECK(life == 2 && pose.position.x == 50);
    slots[1].boat.position.x = 65; // A jump over the teleport distance is not blended.
    boat_remote_history_push(&h, 112, slots);
    CHECK(boat_remote_history_pose(&h, 1, 110, &pose, &life) == BOAT_REMOTE_INTERPOLATED);
    CHECK(pose.position.x == 65);
    for (uint64_t t = 116; t < 400; t += 4)
        boat_remote_history_push(&h, t, slots);
    CHECK(h.count == BOAT_REMOTE_HISTORY);
    slots[1].active = false;
    boat_remote_history_push(&h, 400, slots);
    CHECK(boat_remote_history_pose(&h, 1, 398, &pose, &life) == BOAT_REMOTE_NONE);

    // Proximity: enter within 12 m over 100 ms, stay up to 16 m, leave after 0.5 s beyond it.
    BoatRemoteProximity x = { 0 };
    Boat own = { 0 }, remote = { .position = { 20, 0 } };
    double now = 0;
    const float dt = 1 / 120.0f;
    CHECK(boat_remote_proximity_update(&x, &own, &remote, 1, now, dt) == 0);
    remote.position.x = 10;
    float w = 0;
    for (unsigned k = 0; k < 6; ++k)
        w = boat_remote_proximity_update(&x, &own, &remote, 1, now += dt, dt);
    CHECK(w > .45f && w < .55f);
    for (unsigned k = 0; k < 7; ++k)
        w = boat_remote_proximity_update(&x, &own, &remote, 1, now += dt, dt);
    CHECK(w == 1);
    remote.position.x = 14;
    for (unsigned k = 0; k < 120; ++k)
        w = boat_remote_proximity_update(&x, &own, &remote, 1, now += dt, dt);
    CHECK(w == 1 && x.near);
    remote.position.x = 20;
    for (unsigned k = 0; k < 59; ++k)
        w = boat_remote_proximity_update(&x, &own, &remote, 1, now += dt, dt);
    CHECK(w == 1 && x.near);
    for (unsigned k = 0; k < 14; ++k)
        w = boat_remote_proximity_update(&x, &own, &remote, 1, now += dt, dt);
    CHECK(!x.near && w == 0);
    // A closing boat 25 m away enters before it is near; a new life starts outside.
    remote = (Boat) { .position = { 25, 0 }, .velocity = { -60, 0 } };
    CHECK(boat_remote_proximity_update(&x, &own, &remote, 1, now += dt, dt) > 0 && x.near);
    CHECK(
        boat_remote_proximity_update(&x, &own, &(Boat) { .position = { 40, 0 } }, 2, now, dt) == 0);
    printf("remote view: interpolation, wrap, flips, lives, teleports, extrapolation, holds "
           "and proximity hysteresis behave as specified\n");
    return 0;
}

static void report_remote(const char *label, const Room *room, unsigned i)
{
    double steps = (double)room->view_steps[i];
    printf("%s: client %u other boat drawn %.0f ms behind the authority; %.1f%% interpolated, "
           "%.1f%% extrapolated, %.1f%% held; position error mean %.3f m max %.3f m over %" PRIu64
           " samples; smooth max %.3f m, contact max %.3f m (%" PRIu64
           " samples); %.1f%% in proximity mode\n",
        label, i, room->delay[i] / steps * 1000 / BOAT_TICK_HZ,
        100 * (double)room->remote_samples[i][BOAT_REMOTE_INTERPOLATED] / steps,
        100 * (double)room->remote_samples[i][BOAT_REMOTE_EXTRAPOLATED] / steps,
        100 * (double)room->remote_samples[i][BOAT_REMOTE_HELD] / steps,
        room->remote_error[i] / (double)room->remote_checked[i], room->remote_max_error[i],
        room->remote_checked[i], room->remote_smooth_max_error[i],
        room->remote_contact_max_error[i], room->remote_contact_samples[i],
        100 * (double)room->near_steps[i] / steps);
}

static void report_prediction(const char *label, const Room *room, unsigned i)
{
    const BoatNetClient *c = &room->clients[i];
    const BoatPredictionStats *p = &c->prediction.stats;
    printf("%s: client %u rtt %.0f ms lead %u, %" PRIu64 " corrections replaying %.1f ticks "
           "(max %u), own error mean %.3f m max %.3f m, remote mean %.3f m max %.3f m, %" PRIu64
           " snaps, %" PRIu64 " held ticks, service %.3f ms per correction; wakes vanished %" PRIu64
           " (%" PRIu64 " visible), appeared %" PRIu64 " (%" PRIu64 "), reshaped %" PRIu64
           " (%" PRIu64 "); carried %" PRIu64 ", held %" PRIu64 ", incomplete %" PRIu64
           ", early %" PRIu64 "\n",
        label, i, c->rtt * 1000, c->lead, p->corrections,
        (double)p->replayed_ticks / (double)p->corrections, p->max_replay,
        p->total_error / (double)p->corrections, p->max_error,
        p->remote_errors ? p->total_remote_error / (double)p->remote_errors : 0,
        p->max_remote_error, p->snaps, p->held_ticks,
        room->service_seconds[i] * 1000 / (double)p->corrections, p->wakes_vanished,
        p->visible_vanished, p->wakes_appeared, p->visible_appeared, p->wakes_reshaped,
        p->visible_reshaped, p->carried, p->held_corrections, p->incomplete_corrections,
        p->early_applied);
}

static int prediction_network(void)
{
    static Room room;
    // One player on a steady 30 ms link: no remote guesses, so corrections come only from
    // births the mirror has not received yet.
    CHECK(room_open(&room, 7, (BoatNetServerConfig) { 0 }));
    room.predict = true;
    unsigned a = room_join(&room, "steady", NULL, 0);
    BoatNetImpairment steady = { .delay = .03 };
    boat_net_memory_impair(room.network, room.link[a], steady, steady);
    room_run(&room, 20);
    const BoatNetClient *c = &room.clients[a];
    CHECK(c->phase == BOAT_NET_CLIENT_PLAYING && boat_net_client_own(c));
    CHECK(c->prediction.stats.corrections > 500 && c->prediction.stats.max_replay <= 32);
    CHECK(c->prediction.fleet.state.tick > c->fleet.state.tick + 4);
    report_prediction("prediction steady", &room, a);
    room_close(&room);

    // Profile F both ways, two players in waves, steering smoothly and then with keys.
    for (unsigned keys = 0; keys < 2; ++keys) {
        CHECK(room_open(&room, 8, (BoatNetServerConfig) { .waves = true }));
        room.predict = true;
        room.keyboard = keys;
        BoatNetImpairment f = { .04, .01, .01, .002, .01, .02 };
        for (unsigned i = 0; i < 2; ++i) {
            unsigned k = room_join(&room, i ? "f-two" : "f-one", NULL, 0);
            boat_net_memory_impair(room.network, room.link[k], f, f);
        }
        room_run(&room, 60);
        for (unsigned i = 0; i < 2; ++i) {
            c = &room.clients[i];
            CHECK(c->phase == BOAT_NET_CLIENT_PLAYING && c->stats.resyncs <= 3);
            const BoatPredictionStats *ps = &c->prediction.stats;
            CHECK(ps->max_replay <= BOAT_PREDICTION_MAX_AHEAD);
            // Held corrections wait for births instead of dropping and re-adding records.
            CHECK(ps->wakes_vanished <= 5 && ps->visible_vanished == 0);
            CHECK(ps->visible_appeared == 0 && ps->incomplete_corrections <= 5);
            report_prediction(keys ? "prediction F keys" : "prediction F", &room, i);
            report_remote(keys ? "remote F keys" : "remote F", &room, i);
            double steps = (double)room.view_steps[i];
            double delay = room.delay[i] / steps / BOAT_TICK_HZ;
            CHECK(room.remote_samples[i][BOAT_REMOTE_INTERPOLATED] > steps * .99);
            CHECK(room.remote_checked[i] > 5000 && room.remote_smooth_max_error[i] < .05);
            CHECK(room.remote_contact_max_error[i] < .15);
            CHECK(room.remote_error[i] / (double)room.remote_checked[i] < .005);
            CHECK(delay > .09 && delay < .125 && room.near_steps[i] > 0);
        }
        CHECK(room.server.stats.emission_failures == 0 && room.server.stats.output_faults == 0);
        room_close(&room);
    }
    return 0;
}

static int throttle_detents(void)
{
    static Room room;
    CHECK(room_open(&room, 92, (BoatNetServerConfig) { 0 }));
    room.predict = room.detents = true;
    unsigned i = room_join(&room, "detents", NULL, 0);
    room_run(&room, 1);
    BoatNetClient *c = &room.clients[i];
    CHECK(boat_net_client_own(c));
    const int steps[] = { 3, 4, 5, 6, 5, 4, 3, 2, 1, 0, 1, 2 };
    for (unsigned k = 0; k < sizeof(steps) / sizeof(steps[0]); ++k) {
        room.thrust_target = boat_throttle_setting(steps[k]);
        room_run(&room, 2.5);
        CHECK(fabsf(room.server.fleet.state.slots[c->slot].boat.throttle - room.thrust_target)
            < BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP / 127 + .000001f);
        CHECK(
            fabsf(boat_net_client_view(c)->state.slots[c->slot].boat.throttle - room.thrust_target)
            < BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP / 127 + .000001f);
    }
    // The new pedal targets also settle through quantized predicted commands, including
    // release, direct reversal, both precision directions and a transition out of precision.
    const float pedals[] = { 1, 0, -.20f, -.05f, .05f, 1, 0 };
    for (unsigned k = 0; k < sizeof(pedals) / sizeof(pedals[0]); ++k) {
        room.thrust_target = pedals[k];
        room_run(&room, .8);
        float tolerance = BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP / 127 + .000001f;
        CHECK(fabsf(room.server.fleet.state.slots[c->slot].boat.throttle - pedals[k]) < tolerance);
        CHECK(fabsf(boat_net_client_view(c)->state.slots[c->slot].boat.throttle - pedals[k])
            < tolerance);
    }
    room.thrust_target = 0;
    const float wheel[] = { -.8f, .35f, 1, 0, -1, .01f, -.01f, 0 };
    for (unsigned k = 0; k < sizeof(wheel) / sizeof(wheel[0]); ++k) {
        room.steering_target = wheel[k];
        room_run(&room, 3);
        CHECK(fabsf(room.server.fleet.state.slots[c->slot].boat.wheel - wheel[k]) < .0002f);
        CHECK(fabsf(boat_net_client_view(c)->state.slots[c->slot].boat.wheel - wheel[k]) < .0002f);
    }
    room_close(&room);
    return 0;
}

static int combat_room(void)
{
    static Room room;
    BoatMatchRules rules = boat_match_default_rules();
    rules.round_ticks = 20 * BOAT_TICK_HZ;
    CHECK(room_open(&room, 61, (BoatNetServerConfig) { .deathmatch = true, .match = rules }));
    room.predict = true;
    room.weapons = true;
    room_join(&room, "gun-a", NULL, 0);
    room_join(&room, "gun-b", NULL, 0);
    room_run(&room, 5);
    room_join(&room, "late-c", NULL, 0);
    room_join(&room, "late-d", NULL, 0);
    room_run(&room, 4);
    CHECK(room.mismatches == 0 && room.matches > 100);
    BoatNetTransportStats before = room.server.transport.stats(room.server.transport.context);
    BoatNetImpairment loss = { .04, .02, .12, .15, .25, .07 };
    for (unsigned i = 0; i < 4; ++i)
        boat_net_memory_impair(room.network, room.link[i], loss, loss);
    room_run(&room, 12);
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(room.clients[i].phase == BOAT_NET_CLIENT_PLAYING);
        CHECK(room.clients[i].stats.decode_errors == 0);
        CHECK(room.server.fleet.state.tick - room.clients[i].fleet.state.tick < 120);
        CHECK(room.clients[i].prediction.stats.corrections > 20);
    }
    boat_net_memory_blackout(room.network, room.link[0], true);
    room_run(&room, 1);
    boat_net_memory_blackout(room.network, room.link[0], false);
    room_run(&room, 2);
    for (unsigned i = 0; i < 4; ++i)
        boat_net_memory_impair(
            room.network, room.link[i], (BoatNetImpairment) { 0 }, (BoatNetImpairment) { 0 });
    unsigned matches = room.matches, errors = room.mismatches;
    room_run(&room, 3);
    CHECK(room.matches > matches && room.mismatches == errors);
    CHECK(!room.server.stats.output_faults);
    BoatNetTransportStats after = room.server.transport.stats(room.server.transport.context);
    printf("combat room: four predicted clients, late joins, 12%% loss, duplicate/reordered pages, "
           "one-second outage and complete-set recovery; max message %zu; down %.1f KiB/s/client\n",
        after.max_message, (double)(after.sent_bytes - before.sent_bytes) / (18 * 4 * 1024));
    // Departure clears all objects from that connection; slot reuse gets a fresh generation.
    unsigned slot = room.clients[0].slot;
    uint32_t generation = room.server.fleet.state.combat.players[slot].generation;
    boat_net_client_leave(&room.clients[0]);
    room.live[0] = false;
    room_run(&room, .5);
    CHECK(room.server.fleet.state.combat.players[slot].generation == generation + 1);
    room_close(&room);
    return 0;
}

#ifdef BOAT_NET_TEST_ENET
static BoatNetTransportStats room_client_stats(const Room *room, unsigned i)
{
    BoatNetTransport t = boat_net_enet_transport(room->enet_clients[i]);
    return t.stats(t.context);
}

// Two bots and a wrong-token client over real UDP: what the former boat_net_loopback process
// test checked, on the room's virtual clock. Mirrors must match the authority bit for bit.
static int enet_loopback(void)
{
    static Room room;
    CHECK(room_open(&room, 1, (BoatNetServerConfig) { .token = "smoke" }));
    uint16_t port = boat_net_enet_port(room.enet);
    CHECK(port != 0);
    char error[128] = "";
    CHECK(!boat_net_enet_listen("127.0.0.1", port, error, sizeof(error)));
    CHECK(strstr(error, "cannot listen") != NULL);
    unsigned a = room_join(&room, "smoke-a", "smoke", 0);
    unsigned b = room_join(&room, "smoke-b", "smoke", 0);
    unsigned intruder = room_join(&room, "intruder", "wrong", 0);
    room_run(&room, .5);
    CHECK(room.clients[intruder].phase == BOAT_NET_CLIENT_REJECTED);
    CHECK(room.clients[intruder].reject.reason == BOAT_NET_REJECT_TOKEN);
    room.live[intruder] = false;
    Vector2 start[2];
    for (unsigned k = 0; k < 2; ++k) {
        const BoatNetClient *c = &room.clients[k == 0 ? a : b];
        CHECK(c->phase == BOAT_NET_CLIENT_PLAYING);
        start[k] = room.server.fleet.state.slots[c->slot].boat.position;
    }
    uint64_t snapshots[2] = { room.clients[a].stats.snapshots, room.clients[b].stats.snapshots };
    room_run(&room, 4);
    CHECK(room.checks > 0 && room.mismatches == 0);
    for (unsigned k = 0; k < 2; ++k) {
        unsigned i = k == 0 ? a : b;
        const BoatNetClient *c = &room.clients[i];
        const BoatNetClientStats *st = &c->stats;
        CHECK(c->phase == BOAT_NET_CLIENT_PLAYING);
        CHECK(st->snapshots - snapshots[k] >= 4 * BOAT_TICK_HZ / BOAT_NET_SNAPSHOT_TICKS - 1);
        CHECK(st->resyncs == 0 && st->bootstraps == 1 && st->decode_errors == 0);
        CHECK(st->births_applied > 0 && st->births_dropped == 0);
        Vector2 now = room.server.fleet.state.slots[c->slot].boat.position;
        CHECK(Vector2Distance(start[k], now) > 10);
        BoatNetTransportStats t = room_client_stats(&room, i);
        CHECK(t.max_message <= 1100 && t.max_datagram > 0 && t.max_datagram <= 1200);
        unsigned mtu = boat_net_enet_peer_mtu(room.enet_clients[i], 0);
        CHECK(mtu > 0 && mtu <= 1200);
    }
    boat_net_client_leave(&room.clients[a]);
    boat_net_client_leave(&room.clients[b]);
    room_run(&room, .5);
    const BoatNetServerStats *st = &room.server.stats;
    CHECK(st->accepted == 2 && st->left == 2 && st->rejected[BOAT_NET_REJECT_TOKEN] == 1);
    CHECK(boat_net_server_players(&room.server) == 0);
    CHECK(!st->malformed && !st->forbidden && !st->misbehaving && !st->output_faults
        && !st->emission_failures);
    BoatNetTransport server = boat_net_enet_transport(room.enet);
    CHECK(server.stats(server.context).max_message <= 1100);
    printf("enet_loopback: two bots, %.0f snapshots each, token refused, port clash\n",
        (double)room.clients[a].stats.snapshots);
    room_close(&room);
    return 0;
}

// A loopback room answers a LAN search on its own discovery socket.
static int enet_lan(void)
{
    char error[128];
    BoatNetLan *answer = boat_net_lan_listen("127.0.0.1", 0, error, sizeof(error));
    BoatNetLan *search = boat_net_lan_search(error, sizeof(error));
    CHECK(answer && search && boat_net_lan_port(answer) != 0);
    BoatNetLanRoom advert = { .version = BOAT_NET_VERSION,
        .physics_revision = BOAT_PHYSICS_REVISION,
        .content_digest = boat_net_content_digest(),
        .epoch = 0x5eed,
        .port = 24601,
        .mode = BOAT_NET_MODE_RACE,
        .max_players = 4,
        .players = 1,
        .token = true,
        .name = "lan-room" };
    CHECK(boat_net_parse_ipv4("127.0.0.1", &advert.address));
    boat_net_lan_query(search, boat_net_lan_port(answer));
    // Loopback delivery is asynchronous: each wait returns when its datagram arrives.
    CHECK(boat_net_lan_wait(answer, 5)); // 5 s deadline
    CHECK(boat_net_lan_answer(answer, &advert) >= 1);
    BoatNetLanRoom heard;
    bool found = false;
    CHECK(boat_net_lan_wait(search, 5)); // 5 s deadline
    while (boat_net_lan_receive(search, &heard) && !found)
        found = heard.port == 24601 && heard.epoch == 0x5eed;
    CHECK(found && heard.mode == BOAT_NET_MODE_RACE && heard.players == 1 && heard.token);
    CHECK(boat_net_lan_room_compatible(&heard) && strcmp(heard.name, "lan-room") == 0);
    char endpoint[32];
    boat_net_lan_endpoint(&heard, endpoint, sizeof(endpoint));
    CHECK(strcmp(endpoint, "127.0.0.1:24601") == 0);
    boat_net_lan_close(search);
    boat_net_lan_close(answer);
    return 0;
}

// Three bots stay while a fourth leaves, a fifth is refused as full, one crashes (falls silent
// with its socket open) and times out on the room clock, and a replacement takes the slot:
// the former boat_net_session process test without its wall-clock waits.
static int enet_lifecycle(void)
{
    static Room room;
    CHECK(room_open(&room, 2, (BoatNetServerConfig) { .waves = true }));
    room.predict = true;
    unsigned stay[3];
    for (unsigned k = 0; k < 3; ++k)
        stay[k] = room_join(&room, "stay", NULL, 0);
    unsigned leaver = room_join(&room, "leave", NULL, 0);
    room_run(&room, 1);
    unsigned full = room_join(&room, "full", NULL, 0);
    room_run(&room, .5);
    CHECK(room.clients[full].phase == BOAT_NET_CLIENT_REJECTED);
    CHECK(room.clients[full].reject.reason == BOAT_NET_REJECT_FULL);
    room.live[full] = false;
    CHECK(room.clients[leaver].phase == BOAT_NET_CLIENT_PLAYING);
    boat_net_client_leave(&room.clients[leaver]);
    room_run(&room, .5);
    room.live[leaver] = false;
    CHECK(room.server.stats.left == 1 && boat_net_server_players(&room.server) == 3);
    unsigned crash = room_join(&room, "crash", NULL, 0);
    room_run(&room, 1);
    CHECK(room.clients[crash].phase == BOAT_NET_CLIENT_PLAYING);
    unsigned slot = room.clients[crash].slot;
    room.live[crash] = false; // Silent from now on, as a killed process.
    room_run(&room, BOAT_NET_TIMEOUT + 1);
    CHECK(room.server.stats.timeouts == 1 && room.server.slot_peer[slot] == -1);
    unsigned replacement = room_join(&room, "rejoin", NULL, 0);
    room_run(&room, 1.5);
    CHECK(room.clients[replacement].phase == BOAT_NET_CLIENT_PLAYING);
    CHECK(room.clients[replacement].slot == slot);
    for (unsigned k = 0; k < 3; ++k) {
        const BoatNetClient *c = &room.clients[stay[k]];
        CHECK(c->phase == BOAT_NET_CLIENT_PLAYING && c->stats.decode_errors == 0);
        CHECK(c->stats.resyncs == 0 && c->wake_consistent);
    }
    CHECK(room.checks > 0 && room.mismatches == 0);
    const BoatNetServerStats *st = &room.server.stats;
    CHECK(st->accepted == 6 && st->rejected[BOAT_NET_REJECT_FULL] == 1);
    CHECK(!st->malformed && !st->forbidden && !st->output_faults && !st->emission_failures);
    printf(
        "enet_lifecycle: leave, full, crash timeout on the room clock and slot %u reused\n", slot);
    room_close(&room);
    return 0;
}

// Room cases without simulated impairment run unchanged over real UDP.
static const TestCase cases[] = {
    TEST_CASE(enet_loopback),
    TEST_CASE(enet_lan),
    TEST_CASE(deferred_ready),
    SLOW_CASE(enet_lifecycle),
    TEST_CASE(rejections),
    SLOW_CASE(deathmatch),
    TEST_CASE(resync),
    SLOW_CASE(race_room),
    SLOW_CASE(mapped_rooms),
};
#else
static const TestCase cases[] = {
    SLOW_CASE(throttle_detents),
    TEST_CASE(golden),
    TEST_CASE(codec),
    SLOW_CASE(checkpoints),
    TEST_CASE(deferred_ready),
    TEST_CASE(lossless),
    TEST_CASE(deathmatch),
    SLOW_CASE(combat_room),
    TEST_CASE(prediction_release),
    TEST_CASE(prediction_owners),
    SLOW_CASE(race_room),
    TEST_CASE(lan_discovery),
    TEST_CASE(tick_limit),
    TEST_CASE(rejections),
    TEST_CASE(authority_rules),
    TEST_CASE(wrap_and_flood),
    SLOW_CASE(impaired),
    SLOW_CASE(mapped_rooms),
    SLOW_CASE(four_player_lifecycle),
    TEST_CASE(resync),
    SLOW_CASE(prediction_replay),
    TEST_CASE(prediction_wakes),
    TEST_CASE(remote_view),
    SLOW_CASE(prediction_network),
};
#endif

int main(int argc, char **argv)
{
#ifdef BOAT_NET_TEST_ENET
    if (!boat_net_enet_startup()) {
        fprintf(stderr, "ENet startup failed\n");
        return 1;
    }
    const char *name = "boat_net_enet";
#else
    const char *name = "boat_net";
#endif
    int status = test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
    if (status == 0 && argc == 1)
        printf("%s=ok\n", name);
    test_worlds_close();
#ifdef BOAT_NET_TEST_ENET
    boat_net_enet_cleanup();
#endif
    return status;
}
