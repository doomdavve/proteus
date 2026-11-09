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

#include "boat_fixtures.h"
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
        "50424e54000e0d0001020304a0b0c0d01122334455667788000800000000000001020304050607"
        "08"));
    BoatNetMessage input = { .header = { .type = BOAT_NET_INPUT_BATCH, .epoch = 9, .tick = 200 },
        .input = { .life = 7, .count = 2, .snapshot_tick = 100 } };
    input.input.commands[0] = boat_net_command(
        (BoatInput) { .steering_rate = 1, .throttle_rate = -1, .torpedo = 255 }, 0);
    input.input.commands[1] = boat_net_command((BoatInput) { 0 }, BOAT_NET_COMMAND_INACTIVE);
    size = encode(input, bytes);
    CHECK(size == 60);
    CHECK(same_bytes(bytes, size,
        "50424e54000e09000000000900000000000000000000"
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
        "0301000000f0003c00f0000000000002" // Phase, placed, rule ticks, round.
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
    // ObjectiveState (v13): a suspended HUD with a negative value, then its invalid forms.
    BoatNetMessage hud = { .header = { .type = BOAT_NET_OBJECTIVE_STATE, .tick = 960 },
        .objective = { .revision = 7, .suspended = true, .row_count = 2 } };
    strcpy(hud.objective.rows[0].id, "hunt");
    strcpy(hud.objective.rows[0].label, "HUNT");
    hud.objective.rows[0].value = 3;
    strcpy(hud.objective.rows[1].id, "c1");
    strcpy(hud.objective.rows[1].label, "CREW 1");
    hud.objective.rows[1].value = -2;
    size = encode(hud, bytes);
    CHECK(size == 32 + 44);
    CHECK(same_bytes(bytes + 32, 44,
        "00000007" // Revision.
        "0102"
        "0000" // Suspended, two rows, reserved.
        "0468756e74"
        "0448554e54"
        "0000000000000003" // hunt: HUNT = 3.
        "026331"
        "06435245572031"
        "fffffffffffffffe")); // c1: CREW 1 = -2.
    BoatNetMessage hud_back;
    CHECK(boat_net_decode(bytes, size, &hud_back) == BOAT_NET_OK);
    CHECK(hud_back.objective.revision == 7 && hud_back.objective.suspended
        && hud_back.objective.rows[1].value == -2
        && boat_net_objective_state_equal(&hud_back.objective, &hud.objective));
    CHECK(boat_net_channel(BOAT_NET_OBJECTIVE_STATE) == BOAT_NET_CONTROL);
    for (size_t length = 0; length < size; ++length)
        CHECK(boat_net_decode(bytes, length, &hud_back) != BOAT_NET_OK);
    BoatNetMessage invalid[6];
    for (unsigned i = 0; i < 6; ++i)
        invalid[i] = hud;
    invalid[0].objective.row_count = BOAT_NET_OBJECTIVE_ROWS + 1;
    invalid[1].objective.rows[1].id[0] = 0; // Empty id.
    invalid[2].objective.rows[0].label[1] = '\t'; // Not printable.
    invalid[3].objective.revision = 0;
    invalid[4].objective.rows[0].id[0] = 'x'; // Valid control: only the id differs.
    memset(invalid[5].objective.rows[1].label, 'L', 24); // Unterminated, too long.
    for (unsigned i = 0; i < 6; ++i) {
        size = encode(invalid[i], bytes);
        bool accepted = size && boat_net_decode(bytes, size, &hud_back) == BOAT_NET_OK;
        CHECK(accepted == (i == 4));
    }
    size = encode(hud, bytes);
    bytes[32 + 4] = 2; // Flags with an undefined bit.
    CHECK(boat_net_decode(bytes, size, &hud_back) != BOAT_NET_OK);
    bytes[32 + 4] = 1;
    bytes[32 + 7] = 1; // Reserved.
    CHECK(boat_net_decode(bytes, size, &hud_back) != BOAT_NET_OK);
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
    CHECK(digest == UINT64_C(0x4fc3a7e799ace302)); // v14 header; snapshot payload unchanged.
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
        .accept = { 2, true, BOAT_NET_MODE_DEATHMATCH, BOAT_LOCATION_OUTER, 4, 12, 2, 12, 24, 120,
            1920, 1.8f, 3 } };
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
        .world_page = { .checkpoint = 100, .index = 0, .count = 1, .size = BOAT_NET_PAGE_BYTES } };
    m[21] = (BoatNetMessage) { .header = { .type = BOAT_NET_DEFINITION_BEGIN, .epoch = 3 },
        .definition_begin = { 2, 5332, 6, 99 } };
    m[22] = (BoatNetMessage) { .header = { .type = BOAT_NET_DEFINITION_PAGE, .epoch = 3 },
        .world_page = { .checkpoint = 2, .index = 5, .count = 6, .size = 212 } };
    m[23] = (BoatNetMessage) { .header = { .type = BOAT_NET_DEFINITION_END, .epoch = 3 },
        .checkpoint = 2 };
    m[24] = (BoatNetMessage) { .header
        = { .type = BOAT_NET_OBJECTIVE_STATE, .epoch = 3, .tick = 960 },
        .objective = { .revision = 4, .suspended = true, .row_count = 2 } };
    strcpy(m[24].objective.rows[0].id, "hunt");
    strcpy(m[24].objective.rows[0].label, "HUNT");
    m[24].objective.rows[0].value = 3;
    strcpy(m[24].objective.rows[1].id, "c1");
    strcpy(m[24].objective.rows[1].label, "CREW 1");
    m[24].objective.rows[1].value = -2;
    m[25]
        = (BoatNetMessage) { .header = { .type = BOAT_NET_REVISION_BEGIN, .epoch = 3, .tick = 960 },
              .revision_begin = { 4, 5332, 6, 99, 1200 } };
    m[26]
        = (BoatNetMessage) { .header = { .type = BOAT_NET_REVISION_PAGE, .epoch = 3, .tick = 960 },
              .world_page = { .checkpoint = 4, .index = 5, .count = 6, .size = 212 } };
    m[27] = (BoatNetMessage) { .header = { .type = BOAT_NET_REVISION_END, .epoch = 3, .tick = 960 },
        .checkpoint = 4 };
    m[28]
        = (BoatNetMessage) { .header = { .type = BOAT_NET_REVISION_READY, .epoch = 3, .tick = 970 },
              .checkpoint = 4 };
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
        if (s->bytes[6] != BOAT_NET_WORLD_PAGE && s->bytes[6] != BOAT_NET_DEFINITION_PAGE
            && s->bytes[6] != BOAT_NET_REVISION_PAGE && s->size < BOAT_NET_MAX_MESSAGE) {
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
        // Race state: phase, places below the finishers, reserved bytes, skip beyond recovery,
        // racing with an end tick and no deadline, a GO without a round, a deadline outside
        // racing, unknown row flags, a parked leaver, a status, a place without a finish, a
        // finish time while running and a non-entrant with gates.
        { 18, 32, BOAT_RACE_RESULTS + 1 }, { 18, 33, 0 }, { 18, 34, 2 }, { 18, 35, 1 },
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
            1.8f, 1 } }; // The river sprint needs the river.
    CHECK(boat_net_encode(&bad, b) == 0);
    bad.accept.location = BOAT_LOCATION_OUTER;
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
    BoatRoomDefinition definition; // The room's package definition, owned by this Room.
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

// A definition built in code, as the plan requires of this validator: it links no plugins.
static BoatRoomDefinition room_definition(
    BoatLocationId location, BoatRoomMode mode, const BoatMatchRules *rules)
{
    BoatRoomDefinition d = { .version = BOAT_ROOM_VERSION,
        .mode = (unsigned char)mode,
        .location = (unsigned char)location };
    snprintf(d.id, sizeof(d.id), "%s", "validator");
    snprintf(d.name, sizeof(d.name), "%s", "Validator room");
    if (mode == BOAT_ROOM_MODE_DEATHMATCH) {
        d.match_rules = rules ? *rules : boat_fixture_match_default_rules;
        d.combat_rules = boat_fixture_combat_default_rules;
    } else if (mode == BOAT_ROOM_MODE_RACE) {
        d.course = *(&boat_fixture_river_course);
        d.race_rules = boat_fixture_race_default_rules;
    }
    return d;
}

static bool room_open_marked(Room *room, uint64_t seed, BoatNetServerConfig config,
    BoatRoomMode mode, const BoatMatchRules *rules, const BoatMarkSet *marks)
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
    room->definition = room_definition(config.resources->definition->id, mode, rules);
    if (marks)
        room->definition.marks = *marks;
    config.room = &room->definition;
    BoatNetTransport transport;
    return room_listen(room, seed, &transport)
        && boat_net_server_init(&room->server, transport, &config);
}

static bool room_open_mode(Room *room, uint64_t seed, BoatNetServerConfig config, BoatRoomMode mode,
    const BoatMatchRules *rules)
{
    return room_open_marked(room, seed, config, mode, rules, NULL);
}

static bool room_open(Room *room, uint64_t seed, BoatNetServerConfig config)
{
    return room_open_mode(room, seed, config, BOAT_ROOM_MODE_FREE, NULL);
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
                input = boat_fleet_bot_race_input(view, (&boat_fixture_river_course), row->gates,
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

static int combat_compact(void)
{
    const uint64_t tick = 100000;
    BoatCombat combat
        = { .enabled = true, .firing = true, .rules = boat_fixture_combat_default_rules };
    for (unsigned i = 0; i < BOAT_COMBAT_PLAYERS; ++i) {
        BoatCombatPlayer *p = &combat.players[i];
        p->health = combat.rules.health - (float)i;
        p->heat = .25f + .1f * (float)i;
        p->pressure = .05f * (float)i;
        p->life_start = tick - 100 - i;
        p->next_fire = tick + 15;
        p->next_grenade = tick + 960;
        p->next_torpedo = tick + 180;
        p->pressure_tick = tick - 5;
        p->generation = i + 1;
        p->request = i * 17;
        p->hit_serial = i * 3;
        p->killer = UINT32_MAX;
        p->torpedo_request = i + 1;
        p->ammo = 3;
        p->pressure_owner = UINT32_MAX;
        p->pressure_generation = i;
        for (unsigned j = 0; j < BOAT_COMBAT_PLAYERS; ++j) {
            p->damage[j] = 1.5f * (float)(i + j);
            p->damage_tick[j] = tick - 2;
            p->damage_life[j] = i + 10;
            p->damage_generation[j] = j;
        }
    }
    for (unsigned i = 0; i < BOAT_COMBAT_BULLETS; ++i) {
        BoatCombatObject *o = &combat.bullets[i];
        o->position = (Vector2) { 3.25f * (float)i, -2.5f * (float)i };
        o->direction = (Vector2) { cosf((float)i), sinf((float)i) };
        o->height = .1f * (float)i;
        o->born = tick - 10;
        o->generation = i + 1;
        o->life = i + 2;
        o->owner = i % BOAT_COMBAT_PLAYERS;
        o->active = true;
    }
    for (unsigned i = 0; i < BOAT_COMBAT_GRENADES; ++i) {
        BoatCombatObject *o = &combat.grenades[i];
        o->position = (Vector2) { (float)i, (float)i };
        o->direction = (Vector2) { 0, 1 };
        o->height = 1.0f;
        o->born = tick - 4;
        o->generation = 20 + i;
        o->life = 30 + i;
        o->owner = i;
        o->active = true;
    }
    for (unsigned i = 0; i < BOAT_COMBAT_TORPEDOES; ++i) {
        BoatCombatObject *o = &combat.torpedoes[i];
        o->position = (Vector2) { -(float)i, 2.0f * (float)i };
        o->direction = (Vector2) { 1, 0 };
        o->height = .2f;
        o->born = tick - 30;
        o->generation = 40 + i;
        o->life = 50 + i;
        o->owner = i % BOAT_COMBAT_PLAYERS;
        o->active = true;
    }
    for (unsigned i = 0; i < BOAT_COMBAT_WAVES; ++i) {
        BoatRadialWave *w = &combat.waves[i];
        w->centre = (Vector2) { (float)i, -(float)i };
        w->born = tick - 5;
        w->generation = 60 + i;
        w->life = 70 + i;
        w->owner = i;
        w->height = 2.0f;
        w->speed = 10.0f;
        w->range = 20.0f;
        w->active = true;
    }
    // A recent inactive hit echo stays in the compact state; stale inactive slots disappear.
    combat.bullets[0].active = false;
    combat.bullets[0].ended = tick - 3;
    combat.bullets[1] = (BoatCombatObject) { 0 };
    uint8_t bytes[BOAT_COMBAT_COMPACT_BYTES];
    size_t size = boat_combat_compact_encode(&combat, bytes, sizeof(bytes), tick);
    CHECK(size > 0 && size <= BOAT_COMBAT_COMPACT_BYTES);
    CHECK(size <= 3 * BOAT_NET_PAGE_BYTES);
    size_t full_size = size;
    BoatCombat decoded;
    CHECK(boat_combat_compact_decode(&decoded, bytes, size, tick, &combat.rules));
    uint8_t expected[BOAT_COMBAT_BYTES], actual[BOAT_COMBAT_BYTES];
    boat_combat_encode(&combat, expected);
    boat_combat_encode(&decoded, actual);
    CHECK(memcmp(expected, actual, sizeof(expected)) == 0);
    // Drive the real fleet/combat path and check that every authoritative tick survives the
    // compact round trip byte for byte.
    static BoatFleet fleet;
    CHECK(boat_fleet_init(&fleet, basin(),
        boat_world_test_water(basin()->definition, false, false, false), BOAT_OBJECT_LIFE_TICKS,
        1.2f));
    boat_combat_enable(&fleet, &boat_fixture_combat_default_rules);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        boat_fleet_request_spawn(&fleet, i);
    BoatFleetBots bots = { 0 };
    for (unsigned t = 0; t < 2000; ++t) {
        BoatInput inputs[BOAT_FLEET_SLOTS];
        boat_fleet_bots_drive(&bots, &fleet, inputs);
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            inputs[i].gun = true;
        boat_fleet_step(&fleet, inputs);
        size_t encoded = boat_combat_compact_encode(
            &fleet.state.combat, bytes, sizeof(bytes), fleet.state.tick);
        CHECK(encoded > 0);
        BoatCombat live;
        CHECK(boat_combat_compact_decode(
            &live, bytes, encoded, fleet.state.tick, &fleet.state.combat.rules));
        uint8_t live_bytes[BOAT_COMBAT_BYTES], decoded_bytes[BOAT_COMBAT_BYTES];
        boat_combat_encode(&fleet.state.combat, live_bytes);
        boat_combat_encode(&live, decoded_bytes);
        if (memcmp(live_bytes, decoded_bytes, sizeof(live_bytes)) != 0) {
            for (size_t k = 0; k < sizeof(live_bytes); ++k)
                if (live_bytes[k] != decoded_bytes[k]) {
                    fprintf(stderr, "compact mismatch at tick %u byte %zu: %02x != %02x\n", t, k,
                        live_bytes[k], decoded_bytes[k]);
                    const BoatCombatObject *a = &fleet.state.combat.bullets[0];
                    const BoatCombatObject *b = &live.bullets[0];
                    fprintf(stderr,
                        "bullet0 live active=%d born=%llu ended=%llu gen=%u life=%u owner=%u; "
                        "decoded active=%d born=%llu ended=%llu gen=%u life=%u owner=%u\n",
                        a->active, (unsigned long long)a->born, (unsigned long long)a->ended,
                        a->generation, a->life, a->owner, b->active, (unsigned long long)b->born,
                        (unsigned long long)b->ended, b->generation, b->life, b->owner);
                    break;
                }
            return 1;
        }
    }
    BoatCombat empty = { .enabled = true, .firing = true, .rules = combat.rules };
    size = boat_combat_compact_encode(&empty, bytes, sizeof(bytes), tick);
    CHECK(size > 0 && size <= BOAT_NET_PAGE_BYTES); // No projectiles: one page.
    printf("combat_compact: busy pool %zu bytes (<=%d), empty %zu bytes\n", full_size,
        BOAT_COMBAT_COMPACT_BYTES, size);
    return 0;
}

static int snapshot_resync(void)
{
    static Room room;
    CHECK(room_open(&room, 1, (BoatNetServerConfig) { 0 }));
    unsigned a = room_join(&room, "stall", NULL, 0);
    room_run(&room, 1);
    BoatNetClient *c = &room.clients[a];
    CHECK(c->phase == BOAT_NET_CLIENT_PLAYING);
    // Silence authoritative fleet state while control traffic keeps flowing: pings still get
    // pongs, so only the snapshot-stall resync can recover the world.
    room.server.tick_limit = room.server.fleet.state.tick;
    room_run(&room, 2.5);
    CHECK(c->phase == BOAT_NET_CLIENT_PLAYING && c->timed_out == 0);
    CHECK(c->stats.snapshot_resyncs > 0 && c->stats.resyncs > 0);
    CHECK(room.server.stats.resyncs > 0);
    printf("snapshot_resync: %llu snapshot stalls requested reliable checkpoints\n",
        (unsigned long long)c->stats.snapshot_resyncs);
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
           "%.1f%% received commands, max message %zu bytes, definition %" PRIu64
           " bytes/client, checkpoint %" PRIu64 " bytes\n",
        room.matches, room.checks,
        100.0 * (double)s->stats.input_states[BOAT_NET_INPUT_RECEIVED] / (double)used,
        net.max_message, s->stats.definition_bytes / s->stats.accepted,
        s->stats.bootstrap_bytes / s->stats.bootstraps);
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
    CHECK(same_bytes(query, 8, "50424c510001000e"));
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
    BoatMatchRules rules = boat_fixture_match_default_rules;
    rules.round_ticks = 8 * BOAT_TICK_HZ;
    rules.intermission_ticks = 2 * BOAT_TICK_HZ;
    CHECK(
        room_open_mode(&room, 11, (BoatNetServerConfig) { 0 }, BOAT_ROOM_MODE_DEATHMATCH, &rules));
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
    CHECK(room_open_mode(&room, 71,
        (BoatNetServerConfig) { .resources = test_world(BOAT_LOCATION_OUTER) }, BOAT_ROOM_MODE_RACE,
        NULL));
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
    const BoatWorldResources *river = test_world(BOAT_LOCATION_OUTER);
    CHECK(boat_fleet_init(&authority, river,
        boat_world_test_water(river->definition, false, false, false), BOAT_OBJECT_LIFE_TICKS,
        BOAT_OBJECT_DEFAULT_HEIGHT));
    const BoatRaceCourse *course = (&boat_fixture_river_course);
    for (unsigned i = 0; i < 2; ++i) {
        Boat boat;
        boat_reset(&boat);
        boat.position = course->grid[i];
        boat.heading = atan2f(course->start.direction.x, -course->start.direction.y);
        boat_fleet_place(&authority, i, boat);
        boat_fleet_hold(&authority, i, true);
    }
    uint64_t start = authority.state.tick, go = start + 12;
    boat_net_race_bind(&authority, course, go);
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
    const BoatWorldDefinition *river = boat_location(BOAT_LOCATION_OUTER);
    static BoatWorldResources kept, released;
    static BoatFleet authority[2], before;
    static BoatPrediction p[2];
    CHECK(boat_world_resources_init(&kept, river) && boat_world_resources_init(&released, river));
    const BoatWorldResources *owners[2] = { &kept, &released };
    const BoatRaceCourse *course = (&boat_fixture_river_course);
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
    room_run(&room, 8);
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
    // The definition bootstrap shifts the transport's loss draws slightly; this profile's share
    // of integrated ticks without a fresh command stays around 2-3%.
    CHECK(late < .03);
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
    room_run(&room, 8);
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
    const int id = BOAT_LOCATION_OUTER;
    BoatMatchRules rules = boat_fixture_match_default_rules;
    CHECK(room_open_mode(&room, 63, (BoatNetServerConfig) { .resources = test_world(id) },
        BOAT_ROOM_MODE_DEATHMATCH, &rules));
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
    // Freeze this client so its own next batch cannot overtake the injected one on the
    // impaired, reordering link.
    room.live[replacement] = false;
    inject(&room, replacement,
        (BoatNetMessage) {
            .header = { .type = BOAT_NET_INPUT_BATCH, .tick = room.server.fleet.state.tick + 20 },
            .input = { .life = old_life, .count = 1 } });
    room_run(&room, 2);
    room.live[replacement] = true;
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
    room_run(&room, 8);
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

// A checkpoint at the mirror's own tick is installed, unlike a snapshot at that tick: it carries
// the full wake records that a snapshot only digests, so it repairs a damaged mirror in place.
static int resync_same_tick(void)
{
    static Room room;
    CHECK(room_open(&room, 6, (BoatNetServerConfig) { 0 }));
    unsigned i = room_join(&room, "same", NULL, 0);
    room_run(&room, 8);
    BoatNetClient *c = &room.clients[i];
    CHECK(c->phase == BOAT_NET_CLIENT_PLAYING && c->fleet.state.wakes.live > 3);
    // Hold the authority at the next snapshot tick: the mirror reaches it, then no newer state
    // exists, so the stall resync can only return a checkpoint at the mirror's tick.
    uint64_t hold
        = (room.server.fleet.state.tick / BOAT_NET_SNAPSHOT_TICKS + 1) * BOAT_NET_SNAPSHOT_TICKS;
    room.server.tick_limit = hold;
    room_run(&room, .25);
    CHECK(room.server.fleet.state.tick == hold && c->fleet.state.tick == hold);
    CHECK(c->stats.bootstraps == 1 && c->stats.resyncs == 0);
    uint64_t authority = boat_fleet_hash(&room.server.fleet.state);
    CHECK(boat_fleet_hash(&c->fleet.state) == authority);
    BoatWakeObject *o = &c->fleet.state.wakes.objects[127];
    o->born = c->fleet.state.tick;
    o->amplitude = .5f;
    CHECK(boat_fleet_hash(&c->fleet.state) != authority);
    room_run(&room, 1.5);
    CHECK(c->phase == BOAT_NET_CLIENT_PLAYING && c->timed_out == 0);
    CHECK(c->stats.snapshot_resyncs >= 1 && room.server.stats.resyncs >= 1);
    CHECK(c->stats.bootstraps >= 2 && c->fleet.state.tick == hold);
    CHECK(boat_fleet_hash(&c->fleet.state) == authority);
    printf("resync_same_tick: a checkpoint at the mirror tick %" PRIu64 " replaced the damaged "
           "mirror (%" PRIu64 " bootstraps)\n",
        hold, c->stats.bootstraps);
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
            // Held corrections wait for births instead of dropping and re-adding records. This is
            // one random run, and float rounding or a new message reshuffles its draws, so these
            // bounds and the remote ones below sit about twice above the worst of 120 seeds.
            CHECK(ps->wakes_vanished <= 12 && ps->visible_vanished <= 2);
            CHECK(ps->visible_appeared <= 6 && ps->incomplete_corrections <= 20);
            report_prediction(keys ? "prediction F keys" : "prediction F", &room, i);
            report_remote(keys ? "remote F keys" : "remote F", &room, i);
            double steps = (double)room.view_steps[i];
            double delay = room.delay[i] / steps / BOAT_TICK_HZ;
            CHECK(room.remote_samples[i][BOAT_REMOTE_INTERPOLATED] > steps * .99);
            CHECK(room.remote_checked[i] > 5000 && room.remote_smooth_max_error[i] < .2);
            CHECK(room.remote_contact_max_error[i] < .5);
            CHECK(room.remote_error[i] / (double)room.remote_checked[i] < .005);
            CHECK(delay > .09 && delay < .125 && room.near_steps[i] > 0);
        }
        CHECK(room.server.stats.emission_failures == 0 && room.server.stats.output_faults == 0);
        room_close(&room);
    }
    return 0;
}

// A definition the client cannot decode closes it with a reason, installs no world and frees the
// seat. Corrupting the server's bytes keeps their digest correct, so decoding is what rejects it.
static int definition_rejected(void)
{
    static Room room;
    CHECK(room_open(&room, 95, (BoatNetServerConfig) { 0 }));
    room.server.definition[0] ^= 0xff;
    unsigned a = room_join(&room, "corrupt", NULL, 0);
    room_run(&room, 1);
    const BoatNetClient *c = &room.clients[a];
    CHECK(c->phase == BOAT_NET_CLIENT_CLOSED && !boat_net_client_room(c));
    CHECK(!boat_net_client_own(c) && c->stats.definition_bytes == 0);
    CHECK(strstr(boat_net_client_failure(c), "definition") != NULL);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        CHECK(room.server.slot_peer[i] == -1);
    room_close(&room);
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
    BoatMatchRules rules = boat_fixture_match_default_rules;
    rules.round_ticks = 20 * BOAT_TICK_HZ;
    CHECK(
        room_open_mode(&room, 61, (BoatNetServerConfig) { 0 }, BOAT_ROOM_MODE_DEATHMATCH, &rules));
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
    TEST_CASE(snapshot_resync),
    TEST_CASE(combat_compact),
    SLOW_CASE(enet_lifecycle),
    TEST_CASE(rejections),
    SLOW_CASE(deathmatch),
    TEST_CASE(resync),
    TEST_CASE(resync_same_tick),
    SLOW_CASE(race_room),
    SLOW_CASE(mapped_rooms),
};
#else

// PD2: the room definition codec is fieldwise, bounded and validated without Lua or a map.
static int room_definition_codec(void)
{
    BoatRoomDefinition d = {
        .version = BOAT_ROOM_VERSION, .mode = BOAT_ROOM_MODE_FREE, .location = BOAT_LOCATION_BASIN
    };
    snprintf(d.id, sizeof(d.id), "%s", "validator.free");
    snprintf(d.name, sizeof(d.name), "%s", "Validator free");
    BoatRoomDiagnostic rd;
    CHECK(boat_room_definition_validate(&d, &rd));
    static uint8_t bytes[BOAT_ROOM_BYTES_MAX];
    size_t size = boat_room_definition_encode(&d, bytes, sizeof(bytes));
    CHECK(size > 0 && size <= BOAT_ROOM_BYTES_MAX);
    BoatRoomDefinition back;
    CHECK(boat_room_definition_decode(bytes, size, &back, &rd));
    CHECK(boat_room_definition_digest(&back) == boat_room_definition_digest(&d));
    CHECK(back.mode == BOAT_ROOM_MODE_FREE && back.location == BOAT_LOCATION_BASIN);
    CHECK(!boat_room_definition_decode(bytes, size - 1, &back, &rd));
    bytes[size] = 0;
    CHECK(!boat_room_definition_decode(bytes, size + 1, &back, &rd));
    CHECK(boat_room_definition_decode(bytes, size, &back, &rd)); // The original still decodes.
    // A non-finite start gate value is refused.
    BoatRoomDefinition race = {
        .version = BOAT_ROOM_VERSION, .mode = BOAT_ROOM_MODE_RACE, .location = BOAT_LOCATION_OUTER
    };
    snprintf(race.id, sizeof(race.id), "%s", "validator.race");
    snprintf(race.name, sizeof(race.name), "%s", "Validator race");
    race.course = *(&boat_fixture_river_course);
    race.race_rules = boat_fixture_race_default_rules;
    CHECK(boat_room_definition_validate(&race, &rd));
    size = boat_room_definition_encode(&race, bytes, sizeof(bytes));
    CHECK(size > 0);
    uint8_t nan[4] = { 0x7f, 0xc0, 0, 0 };
    memcpy(bytes + 1 + 1 + 1 + 1 + 1 + 64 + 1 + 64 + 1 + 1, nan, 4); // First start float.
    CHECK(!boat_room_definition_decode(bytes, size, &back, &rd));
    // Unknown mode and location are refused structurally.
    BoatRoomDefinition bad = d;
    bad.mode = BOAT_ROOM_MODE_LIMIT;
    CHECK(!boat_room_definition_validate(&bad, &rd));
    bad = d;
    bad.location = BOAT_LOCATION_COUNT;
    CHECK(!boat_room_definition_validate(&bad, &rd));
    bad = race;
    bad.location = BOAT_LOCATION_BASIN;
    CHECK(!boat_room_definition_validate(&bad, &rd)); // A race room needs the river.
    bad = race;
    bad.course.gate_count = 0;
    CHECK(!boat_room_definition_validate(&bad, &rd));
    // A scene berth is carried and validated.
    BoatRoomDefinition scene = d;
    scene.has_scene = true;
    scene.scene.version = BOAT_SCENE_VERSION;
    scene.scene.entrance_present = true;
    scene.scene.entrance_x = 1;
    scene.scene.entrance_z = 2;
    scene.scene.entrance_width = 8;
    scene.scene.berths_present = true;
    scene.scene.berth_count = 1;
    snprintf(scene.scene.berths[0].id, sizeof(scene.scene.berths[0].id), "%s", "skiff");
    scene.scene.berths[0].recipe = BOAT_SCENE_BERTH_SKIFF;
    scene.scene.berths[0].x = 10;
    scene.scene.berths[0].z = 20;
    CHECK(boat_room_definition_validate(&scene, &rd));
    size = boat_room_definition_encode(&scene, bytes, sizeof(bytes));
    CHECK(size > 0 && boat_room_definition_decode(bytes, size, &back, &rd));
    CHECK(back.has_scene && back.scene.berth_count == 1);
    size_t scene_size = boat_room_definition_encode(&scene, bytes, sizeof(bytes));
    scene.scene.berths[0].recipe = BOAT_SCENE_BERTH_RECIPES;
    CHECK(!boat_room_definition_validate(&scene, &rd));
    // Marks (v4): sixteen full-length marks round trip within the size budget; other modes and
    // invalid marks are refused.
    BoatRoomDefinition marked = d;
    marked.marks.count = BOAT_MARKS_MAX;
    for (unsigned i = 0; i < BOAT_MARKS_MAX; ++i) {
        BoatMark *m = &marked.marks.items[i];
        snprintf(m->id, sizeof(m->id), "mark_%017u", i);
        snprintf(m->name, sizeof(m->name), "MARK %018u", i);
        m->x = 500.0f * (float)i - 3000;
        m->z = -250.5f;
        m->radius = 25;
    }
    CHECK(boat_room_definition_validate(&marked, &rd));
    size = boat_room_definition_encode(&marked, bytes, sizeof(bytes));
    static uint8_t plain[BOAT_ROOM_BYTES_MAX];
    size_t plain_size = boat_room_definition_encode(&d, plain, sizeof(plain));
    CHECK(size > plain_size && size <= 1200);
    CHECK(boat_room_definition_decode(bytes, size, &back, &rd));
    CHECK(back.marks.count == BOAT_MARKS_MAX
        && !memcmp(&back.marks, &marked.marks, sizeof(marked.marks))
        && boat_room_definition_digest(&back) == boat_room_definition_digest(&marked));
    for (size_t length = 0; length < size; ++length)
        CHECK(!boat_room_definition_decode(bytes, length, &back, &rd));
    printf("room_definition: 16 marks %zu bytes (%zu without)\n", size, plain_size);
    bad = marked;
    bad.mode = BOAT_ROOM_MODE_DEATHMATCH;
    bad.match_rules = boat_fixture_match_default_rules;
    bad.combat_rules = boat_fixture_combat_default_rules;
    CHECK(!boat_room_definition_validate(&bad, &rd) && !strcmp(rd.field, "marks"));
    bad = marked;
    bad.marks.items[1].x = bad.marks.items[0].x; // Overlap.
    bad.marks.items[1].z = bad.marks.items[0].z;
    CHECK(!boat_room_definition_validate(&bad, &rd) && !strncmp(rd.field, "marks", 5));
    bad = marked;
    bad.marks.items[2].id[0] = 'X';
    CHECK(!boat_room_definition_validate(&bad, &rd));
    // Decoding refuses an unknown flag bit and an empty or oversized mark list.
    size = boat_room_definition_encode(&marked, bytes, sizeof(bytes));
    bytes[3] |= 8; // Flags: an undefined bit.
    CHECK(!boat_room_definition_decode(bytes, size, &back, &rd));
    bytes[3] = 0; // Marks flag cleared: the mark bytes trail the definition.
    CHECK(!boat_room_definition_decode(bytes, size, &back, &rd));
    bytes[3] = 4;
    size_t at = 1 + 1 + 1 + 1 + 1 + 14 + 1 + 14; // Count byte after the id and name text.
    CHECK(bytes[at] == BOAT_MARKS_MAX);
    bytes[at] = 0;
    CHECK(!boat_room_definition_decode(bytes, size, &back, &rd));
    bytes[at] = BOAT_MARKS_MAX + 1;
    CHECK(!boat_room_definition_decode(bytes, size, &back, &rd));
    // Definition paging, as the bootstrap channel carries it.
    static uint8_t definition[BOAT_ROOM_BYTES_MAX];
    size = boat_room_definition_encode(&race, definition, sizeof(definition));
    uint16_t pages = (uint16_t)((size + BOAT_NET_PAGE_BYTES - 1) / BOAT_NET_PAGE_BYTES);
    BoatNetMessage decoded_message;
    BoatNetMessage begin = { .header = { .type = BOAT_NET_DEFINITION_BEGIN, .epoch = 1 },
        .definition_begin = { .definition = 9,
            .bytes = (uint32_t)size,
            .pages = pages,
            .digest = boat_net_bytes_digest(definition, size) } };
    uint8_t wire[BOAT_NET_MAX_MESSAGE];
    size_t wire_size = boat_net_encode(&begin, wire);
    CHECK(wire_size > 0 && boat_net_decode(wire, wire_size, &decoded_message) == BOAT_NET_OK);
    CHECK(decoded_message.definition_begin.definition == 9
        && decoded_message.definition_begin.pages == pages);
    CHECK(boat_net_channel(BOAT_NET_DEFINITION_PAGE) == BOAT_NET_BOOTSTRAP);
    BoatNetWorldPage page = { .checkpoint = 9, .index = 0, .count = pages, .size = 4 };
    memcpy(page.data, definition, 4);
    BoatNetMessage message
        = { .header = { .type = BOAT_NET_DEFINITION_PAGE, .epoch = 1 }, .world_page = page };
    wire_size = boat_net_encode(&message, wire);
    CHECK(wire_size > 0 && boat_net_decode(wire, wire_size, &decoded_message) == BOAT_NET_OK);
    BoatNetMessage end
        = { .header = { .type = BOAT_NET_DEFINITION_END, .epoch = 1 }, .checkpoint = 9 };
    wire_size = boat_net_encode(&end, wire);
    CHECK(wire_size > 0 && boat_net_decode(wire, wire_size, &decoded_message) == BOAT_NET_OK);
    printf("room_definition: free %zu bytes, race %zu bytes, scene %zu bytes, %u pages\n",
        boat_room_definition_encode(&d, bytes, sizeof(bytes)),
        boat_room_definition_encode(&race, bytes, sizeof(bytes)), scene_size, (unsigned)pages);
    return 0;
}

// P5b: a free room's marks and rules adapter. The server reports joins and leaves between ticks
// and mark entries after the tick; it runs no scripts itself. Other modes refuse marks.
typedef struct {
    unsigned count;
    BoatNetRuleEvent events[64];
    uint64_t ticks[64];
    uint64_t after_ticks, last_after;
    bool ordered;
} RuleLog;

static void log_rule(void *context, uint64_t tick, const BoatNetRuleEvent *event)
{
    RuleLog *log = context;
    if (log->count < 64) {
        log->events[log->count] = *event;
        log->ticks[log->count++] = tick;
    }
}

static void log_after(void *context, uint64_t tick)
{
    RuleLog *log = context;
    log->ordered &= tick == log->last_after + 1;
    log->last_after = tick;
    ++log->after_ticks;
}

static int rule_events(void)
{
    static Room room;
    static RuleLog log;
    memset(&log, 0, sizeof(log));
    log.ordered = true;
    BoatMarkSet marks = { .count = 2,
        .items = { { .id = "near", .name = "NEAR", .x = 60, .z = 60, .radius = 20 },
            { .id = "far", .name = "FAR", .x = -60, .z = -60, .radius = 20 } } };
    BoatNetServerConfig config
        = { .rules = { .context = &log, .event = log_rule, .after_tick = log_after } };
    CHECK(room_open_marked(&room, 71, config, BOAT_ROOM_MODE_FREE, NULL, &marks));
    BoatNetServer *s = &room.server;
    unsigned a = room_join(&room, "a", NULL, 0), b = room_join(&room, "b", NULL, 0);
    room_run(&room, .5);
    CHECK(active_boats(s) == 2 && log.ordered && log.after_ticks == s->fleet.state.tick);
    CHECK(log.count == 2);
    for (unsigned i = 0; i < 2; ++i)
        CHECK(log.events[i].kind == BOAT_NET_RULE_CREW_JOINED && log.events[i].slot == i);
    // Slot 1 is moved into the far mark between ticks; the entry is reported once, after the
    // tick that observed it.
    unsigned slot = room.clients[b].slot;
    s->fleet.state.slots[slot].boat.position = (Vector2) { -60, -55 };
    s->fleet.state.slots[slot].boat.velocity = (Vector2) { 0 };
    uint64_t moved = s->fleet.state.tick;
    room_run(&room, .25);
    CHECK(log.count == 3 && log.events[2].kind == BOAT_NET_RULE_MARK_REACHED);
    CHECK(log.events[2].slot == slot && log.events[2].mark == 1 && log.ticks[2] == moved + 1);
    // Both boats in the near mark on one tick: two entries in slot order.
    for (unsigned i = 0; i < 2; ++i) {
        s->fleet.state.slots[i].boat.position = (Vector2) { 60, 50 + 15 * (float)i };
        s->fleet.state.slots[i].boat.velocity = (Vector2) { 0 };
    }
    room_run(&room, .25);
    CHECK(log.count == 5);
    for (unsigned i = 0; i < 2; ++i)
        CHECK(log.events[3 + i].kind == BOAT_NET_RULE_MARK_REACHED && log.events[3 + i].slot == i
            && log.events[3 + i].mark == 0 && log.ticks[3 + i] == log.ticks[3]);
    // A leave is reported between ticks with the leaver's slot.
    boat_net_client_leave(&room.clients[a]);
    room.live[a] = false;
    room_run(&room, .1);
    CHECK(log.count == 6 && log.events[5].kind == BOAT_NET_RULE_CREW_LEFT);
    CHECK(log.events[5].slot == room.clients[a].slot && log.ordered);
    CHECK(log.after_ticks == s->fleet.state.tick);
    room_close(&room);

    // Marks belong to free rooms, and must be valid.
    BoatMatchRules rules = boat_fixture_match_default_rules;
    CHECK(!room_open_marked(&room, 72, config, BOAT_ROOM_MODE_DEATHMATCH, &rules, &marks));
    room_close(&room);
    marks.items[1].x = marks.items[1].z = 70; // Overlaps the near mark.
    CHECK(!room_open_marked(&room, 73, config, BOAT_ROOM_MODE_FREE, NULL, &marks));
    room_close(&room);
    return 0;
}

// P5c: the objective HUD reaches every client through the control channel. A fake, Lua-free
// adapter changes its rows on given ticks; the server numbers each change, tells every peer and
// every joiner (before its checkpoint), and a lossy, reordering link still ends in agreement.
#define FAKE_CHANGES 5
typedef struct {
    unsigned version, next;
    bool pending;
} FakeObjective;

static const uint64_t fake_change_ticks[FAKE_CHANGES] = { 60, 240, 420, 600, 780 };

static void fake_after(void *context, uint64_t tick)
{
    FakeObjective *f = context;
    if (f->next < FAKE_CHANGES && tick == fake_change_ticks[f->next]) {
        ++f->version;
        ++f->next;
        f->pending = true;
    }
}

static bool fake_state(void *context, BoatNetObjectiveState *out)
{
    FakeObjective *f = context;
    if (!f->pending)
        return false;
    f->pending = false;
    out->suspended = f->version == 3;
    out->row_count = 2;
    strcpy(out->rows[0].id, "score");
    strcpy(out->rows[0].label, "SCORE");
    out->rows[0].value = 10 * (int64_t)f->version;
    strcpy(out->rows[1].id, "delta");
    strcpy(out->rows[1].label, "DELTA");
    out->rows[1].value = -(int64_t)f->version;
    return true;
}

static bool hud_mirrored(const Room *room, unsigned i)
{
    const BoatNetClient *c = &room->clients[i];
    const BoatNetServer *s = &room->server;
    return c->objective_valid && c->objective.revision == s->objective.revision
        && boat_net_objective_state_equal(&c->objective, &s->objective);
}

static int objective_state(void)
{
    static Room room;
    static FakeObjective fake;
    memset(&fake, 0, sizeof(fake));
    fake.pending = true; // The initial HUD, reported at init.
    BoatNetServerConfig config
        = { .rules = { .context = &fake, .after_tick = fake_after, .state = fake_state } };
    CHECK(room_open(&room, 81, config));
    BoatNetServer *s = &room.server;
    CHECK(s->has_objective && s->objective.revision == 1 && s->objective.row_count == 2);
    unsigned a = room_join(&room, "a", NULL, 0), b = room_join(&room, "b", NULL, 0);
    room_run(&room, .4); // Before the first change (tick 60).
    CHECK(s->objective.revision == 1 && hud_mirrored(&room, a) && hud_mirrored(&room, b));
    CHECK(room.clients[a].objective.rows[0].value == 0);
    room_run(&room, .6);
    CHECK(s->objective.revision == 2 && s->objective.rows[0].value == 10);
    CHECK(hud_mirrored(&room, a) && hud_mirrored(&room, b));
    room_run(&room, 1.6); // Past the second change (tick 240).
    CHECK(
        s->objective.revision == 3 && s->objective.rows[1].value == -2 && !s->objective.suspended);
    CHECK(hud_mirrored(&room, a) && hud_mirrored(&room, b));

    // A later joiner has the current HUD before its checkpoint, so before any snapshot.
    unsigned c = room_join(&room, "c", NULL, 0);
    for (unsigned n = 0; n < 1200 && !room.clients[c].objective_valid; ++n)
        room_step(&room, 1.0 / 240);
    CHECK(room.clients[c].objective_valid && hud_mirrored(&room, c));
    CHECK(room.clients[c].stats.snapshots == 0);
    room_run(&room, 1);
    CHECK(room.clients[c].phase == BOAT_NET_CLIENT_PLAYING && hud_mirrored(&room, c));

    // The rest of the changes cross a lossy, reordering link; the clients still agree.
    BoatNetImpairment f = { .04, .01, .01, .002, .01, .02 };
    for (unsigned i = 0; i < 3; ++i)
        boat_net_memory_impair(room.network, room.link[i], f, f);
    room_run(&room, 7);
    CHECK(fake.next == FAKE_CHANGES && s->objective.revision == 1 + FAKE_CHANGES);
    CHECK(s->objective.suspended == (fake.version == 3) && s->objective.rows[0].value == 50);
    for (unsigned i = 0; i < 3; ++i)
        CHECK(hud_mirrored(&room, i) && room.clients[i].stats.decode_errors == 0);
    CHECK(s->stats.objective_states >= 3 + 2 * 2 + 3 * 3 && s->stats.malformed == 0);
    printf("objective_state: revision %u, %" PRIu64 " messages, three clients identical\n",
        s->objective.revision, s->stats.objective_states);
    room_close(&room);
    return 0;
}

// P5f: definition revisions. A candidate is staged on the authority and commits only at a
// boundary; every client switches at the same tick, or is dropped for not being ready.

// BASE with variant N of what its mode revises: a mark, the respawn and combat rules, or the
// recovery time.
static BoatRoomDefinition revised_room(const BoatRoomDefinition *base, unsigned n)
{
    BoatRoomDefinition d = *base;
    if (d.mode == BOAT_ROOM_MODE_FREE) {
        d.marks.count = 1;
        BoatMark *m = &d.marks.items[0];
        snprintf(m->id, sizeof(m->id), "rev_%u", n);
        snprintf(m->name, sizeof(m->name), "REVISION %u", n);
        m->x = 40.0f * (float)n;
        m->z = 0;
        m->radius = 20;
    } else if (d.mode == BOAT_ROOM_MODE_DEATHMATCH) {
        d.match_rules.respawn_ticks += 60 * n;
        d.combat_rules.health += (float)n;
    } else {
        d.race_rules.recovery_ticks += 60 * n;
    }
    return d;
}

static size_t encode_room(const BoatRoomDefinition *d, uint8_t *out)
{
    return boat_room_definition_encode(d, out, BOAT_ROOM_BYTES_MAX);
}

static bool stage_room(Room *room, const BoatRoomDefinition *d, BoatRoomDiagnostic *diagnostic)
{
    static uint8_t bytes[BOAT_ROOM_BYTES_MAX];
    size_t size = encode_room(d, bytes);
    return size && boat_net_server_stage_revision(&room->server, bytes, size, diagnostic);
}

// True when client I runs exactly definition D, as revision REVISION.
static bool client_runs(
    const Room *room, unsigned i, const BoatRoomDefinition *d, uint32_t revision)
{
    const BoatNetClient *c = &room->clients[i];
    const BoatRoomDefinition *r = boat_net_client_room(c);
    return r && boat_room_definition_digest(r) == boat_room_definition_digest(d)
        && boat_net_client_revision(c) == revision;
}

typedef struct {
    unsigned count;
    uint64_t tick, digest;
} DefinitionLog;

static void log_definition(void *context, uint64_t tick, const BoatRoomDefinition *room)
{
    DefinitionLog *log = context;
    ++log->count;
    log->tick = tick;
    log->digest = boat_room_definition_digest(room);
}

// Step the room one tick at a time until PREDICATE holds, for at most LIMIT steps.
#define ROOM_UNTIL(room, limit, predicate)                                                         \
    for (unsigned guard_ = 0; guard_ < (limit) && !(predicate); ++guard_)                          \
    room_step((room), 1.0 / 240)

// A free room revises only while empty, with no announcement; later joiners see the new marks.
// Invalid candidates and replaced ones leave the running revision alone.
static int revision_free(void)
{
    static Room room;
    static DefinitionLog log;
    memset(&log, 0, sizeof(log));
    CHECK(room_open(&room, 21,
        (BoatNetServerConfig) { .rules = { .context = &log, .definition = log_definition } }));
    BoatNetServer *s = &room.server;
    const BoatRoomDefinition v1 = room.definition, a = revised_room(&v1, 1),
                             b = revised_room(&v1, 2), c = revised_room(&v1, 3);
    unsigned first = room_join(&room, "alpha", NULL, 0);
    room_run(&room, 1);
    CHECK(room.clients[first].accept.revision == 1 && client_runs(&room, first, &v1, 1));
    CHECK(s->stats.revision == 1 && !boat_net_server_revision_pending(s));

    // Refused candidates: garbage, another mode, an invalid mark set.
    BoatRoomDiagnostic diagnostic;
    CHECK(!boat_net_server_stage_revision(s, (const uint8_t *)"nope", 4, &diagnostic));
    CHECK(diagnostic.message[0]);
    BoatRoomDefinition other
        = room_definition(BOAT_LOCATION_BASIN, BOAT_ROOM_MODE_DEATHMATCH, NULL);
    CHECK(!stage_room(&room, &other, &diagnostic) && strstr(diagnostic.message, "mode"));
    BoatRoomDefinition twin = revised_room(&v1, 1);
    twin.marks.count = 2;
    twin.marks.items[1] = twin.marks.items[0];
    CHECK(!stage_room(&room, &twin, &diagnostic));
    CHECK(s->stats.revisions_refused == 2 || s->stats.revisions_refused == 3);
    CHECK(!boat_net_server_revision_pending(s) && s->stats.revision == 1
        && s->room == &room.definition);

    // Staged with a player present: it waits, silently, for as long as the room is occupied.
    CHECK(stage_room(&room, &a, NULL) && boat_net_server_revision_pending(s));
    room_run(&room, 3);
    CHECK(s->stats.revision == 1 && s->stats.revisions_announced == 0 && log.count == 0);
    CHECK(client_runs(&room, first, &v1, 1) && s->room->marks.count == 0);
    boat_net_client_leave(&room.clients[first]);
    room.live[first] = false;
    room_run(&room, .5);
    CHECK(s->stats.revision == 2 && s->stats.revisions_committed == 1);
    CHECK(s->stats.revisions_announced == 0 && !boat_net_server_revision_pending(s));
    CHECK(log.count == 1 && log.digest == boat_room_definition_digest(&a));
    CHECK(s->room == &s->revised && s->config.room == s->room && s->room->marks.count == 1);
    CHECK(s->fleet.world.scene == NULL);
    unsigned second = room_join(&room, "beta", NULL, 0);
    room_run(&room, 1);
    CHECK(room.clients[second].accept.revision == 2 && client_runs(&room, second, &a, 2));

    // A newer candidate replaces a waiting one: one commit, one revision step, the newest content.
    CHECK(stage_room(&room, &b, NULL) && stage_room(&room, &c, NULL));
    room_run(&room, 2);
    CHECK(s->stats.revision == 2 && client_runs(&room, second, &a, 2));
    boat_net_client_leave(&room.clients[second]);
    room.live[second] = false;
    room_run(&room, .5);
    CHECK(s->stats.revision == 3 && s->stats.revisions_committed == 2 && log.count == 2);
    CHECK(log.digest == boat_room_definition_digest(&c) && s->stats.revisions_staged == 3);
    // A scene commits too: the fleet's borrowed scene follows, on the authority and a client.
    BoatRoomDefinition scene = revised_room(&v1, 4);
    scene.has_scene = true;
    scene.scene.version = BOAT_SCENE_VERSION;
    scene.scene.berths_present = true;
    scene.scene.berth_count = 1;
    snprintf(scene.scene.berths[0].id, sizeof(scene.scene.berths[0].id), "%s", "skiff");
    scene.scene.berths[0].recipe = BOAT_SCENE_BERTH_SKIFF;
    scene.scene.berths[0].x = 10;
    scene.scene.berths[0].z = 20;
    scene.scene.entrance_present = true;
    scene.scene.entrance_x = 1;
    scene.scene.entrance_z = 2;
    scene.scene.entrance_width = 8;
    CHECK(stage_room(&room, &scene, &diagnostic));
    room_run(&room, .5);
    CHECK(s->stats.revision == 4 && s->fleet.world.scene == &s->revised.scene);
    unsigned third = room_join(&room, "gamma", NULL, 0);
    room_run(&room, 1);
    const BoatNetClient *g = &room.clients[third];
    CHECK(g->phase == BOAT_NET_CLIENT_PLAYING && client_runs(&room, third, &scene, 4));
    CHECK(g->fleet.world.scene == &g->room.scene && g->stats.decode_errors == 0);
    CHECK(s->stats.output_faults == 0 && room.mismatches == 0);
    printf("revision_free: refused %" PRIu64
           " candidates; waited while occupied, committed at once "
           "when empty (revisions 2-4), joiners got each\n",
        s->stats.revisions_refused);
    room_close(&room);
    return 0;
}

// Send MESSAGE to client I as if the authority had, bypassing the revision machinery.
static void inject_to_client(Room *room, unsigned i, BoatNetMessage m)
{
    static uint32_t sequence = 0x7000;
    uint8_t bytes[BOAT_NET_MAX_MESSAGE];
    m.header.epoch = room->server.config.epoch;
    m.header.sequence = sequence++;
    size_t size = boat_net_encode(&m, bytes);
    if (size)
        boat_net_send(&room->server.transport,
            (unsigned)room->server.slot_peer[room->clients[i].slot],
            boat_net_channel(m.header.type), bytes, size);
}

// A whole announcement of D as REVISION, committing at COMMIT, optionally with a bad digest.
static void inject_revision(Room *room, unsigned i, const BoatRoomDefinition *d, uint32_t revision,
    uint64_t commit, bool corrupt)
{
    static uint8_t bytes[BOAT_ROOM_BYTES_MAX];
    size_t size = encode_room(d, bytes);
    uint64_t tick = room->server.fleet.state.tick;
    inject_to_client(room, i,
        (BoatNetMessage) { .header = { .type = BOAT_NET_REVISION_BEGIN, .tick = tick },
            .revision_begin = { .revision = revision,
                .bytes = (uint32_t)size,
                .pages = (uint16_t)((size + BOAT_NET_PAGE_BYTES - 1) / BOAT_NET_PAGE_BYTES),
                .digest = boat_net_bytes_digest(bytes, size) ^ (corrupt ? 1 : 0),
                .commit_tick = commit } });
    for (size_t page = 0; page * BOAT_NET_PAGE_BYTES < size; ++page) {
        size_t offset = page * BOAT_NET_PAGE_BYTES;
        size_t length = size - offset < BOAT_NET_PAGE_BYTES ? size - offset : BOAT_NET_PAGE_BYTES;
        BoatNetMessage m = { .header = { .type = BOAT_NET_REVISION_PAGE, .tick = tick },
            .world_page = { .checkpoint = revision,
                .index = (uint16_t)page,
                .count = (uint16_t)((size + BOAT_NET_PAGE_BYTES - 1) / BOAT_NET_PAGE_BYTES),
                .size = (uint16_t)length } };
        memcpy(m.world_page.data, bytes + offset, length);
        inject_to_client(room, i, m);
    }
    inject_to_client(room, i,
        (BoatNetMessage) {
            .header = { .type = BOAT_NET_REVISION_END, .tick = tick }, .checkpoint = revision });
}

// A race room revises in RESULTS: both predicting clients switch on the commit tick, prediction
// restarts there, and duplicate, stale and invalid announcements change nothing.
static int revision_race(void)
{
    static Room room;
    CHECK(room_open_mode(&room, 71,
        (BoatNetServerConfig) { .resources = test_world(BOAT_LOCATION_OUTER) }, BOAT_ROOM_MODE_RACE,
        NULL));
    BoatNetServer *s = &room.server;
    room.predict = room.race = true;
    const BoatRoomDefinition v1 = room.definition, v2 = revised_room(&v1, 1),
                             v3 = revised_room(&v1, 2);
    unsigned a = room_join(&room, "alpha", NULL, 0), b = room_join(&room, "beta", NULL, 0);
    room_run(&room, 1);
    ROOM_UNTIL(&room, 3000, s->race.phase == BOAT_RACE_RACING);
    CHECK(s->race.phase == BOAT_RACE_RACING);
    room_run(&room, 2);
    // Staged mid-race, it waits for RESULTS; end the race at once.
    CHECK(stage_room(&room, &v2, NULL));
    room_run(&room, .5);
    CHECK(s->stats.revisions_announced == 0 && s->stats.revision == 1);
    s->race.deadline = s->fleet.state.tick + 1;
    ROOM_UNTIL(&room, 100, s->race.phase == BOAT_RACE_RESULTS);
    CHECK(s->race.phase == BOAT_RACE_RESULTS);
    ROOM_UNTIL(&room, 100, s->announced.present);
    CHECK(s->announced.present && s->announced.revision == 2 && s->stats.revisions_announced == 1);
    uint64_t commit = s->announced.commit_tick, announced = s->fleet.state.tick;
    CHECK(commit % BOAT_NET_SNAPSHOT_TICKS == 0
        && commit >= announced + BOAT_NET_REVISION_LEAD_TICKS);
    CHECK(commit < s->race.phase_end);
    // Until the commit nobody runs the new definition; the clients stage it and answer READY.
    room_run(&room, .2);
    for (unsigned i = 0; i < 2; ++i) {
        const BoatNetClient *c = &room.clients[i];
        CHECK(c->stats.revisions == 0 && c->revision_staged && c->revision == 1);
        CHECK(s->peers[s->slot_peer[c->slot]].revision_ready == 2);
    }
    // The server commits on the tick; each client switches when its mirror reaches it, and its
    // prediction restarts there instead of replaying across the switch.
    bool committed = false, switched[2] = { false, false };
    for (unsigned n = 0; n < 2000 && !(committed && switched[0] && switched[1]); ++n) {
        uint64_t before[2] = { room.clients[0].prediction.stats.corrections,
            room.clients[1].prediction.stats.corrections };
        room_step(&room, 1.0 / 240);
        if (!committed && s->stats.revision == 2) {
            committed = true;
            CHECK(s->fleet.state.tick == commit && s->race.phase == BOAT_RACE_RESULTS);
            CHECK(s->race.rules.recovery_ticks == v2.race_rules.recovery_ticks);
            CHECK(s->stats.revision_leaves == 0 && !s->announced.present);
        }
        for (unsigned i = 0; i < 2; ++i) {
            const BoatNetClient *c = &room.clients[i];
            if (switched[i] || c->stats.revisions == 0)
                continue;
            switched[i] = true;
            CHECK(c->stats.revisions == 1 && c->revision == 2 && s->stats.revision == 2);
            CHECK(before[i] > 5 && c->prediction.stats.corrections <= 1);
            CHECK(c->prediction.authority_tick >= commit && c->fleet.state.tick >= commit);
            CHECK(c->prediction.fleet.state.tick >= c->prediction.authority_tick);
        }
    }
    CHECK(committed && switched[0] && switched[1]);
    room_run(&room, 1);
    for (unsigned i = 0; i < 2; ++i) {
        const BoatNetClient *c = &room.clients[i];
        CHECK(client_runs(&room, i, &v2, 2) && same_race(c, s)
            && c->phase == BOAT_NET_CLIENT_PLAYING);
        CHECK(
            c->race.recovery_ticks == v2.race_rules.recovery_ticks && c->stats.decode_errors == 0);
        CHECK(
            c->prediction.fleet.world.water.vessel.started == s->fleet.world.water.vessel.started);
    }
    CHECK(room.mismatches == 0 && s->stats.output_faults == 0);
    // Duplicate (the committed revision), stale (an older one), then a newer one that is valid
    // and one that is not: only the valid newer one is staged, and the server ignores the answers
    // to what it never announced.
    BoatNetClient *c = &room.clients[a];
    uint64_t far = s->fleet.state.tick + 100000;
    inject_revision(&room, a, &v2, 2, far, false);
    inject_revision(&room, a, &v1, 1, far, false);
    room_run(&room, .2);
    CHECK(c->stats.revisions_ignored == 2 && !c->revision_staged && c->revision == 2);
    inject_revision(&room, a, &v3, 3, far, false);
    room_run(&room, .2);
    CHECK(c->revision_staged && c->revision_begin.revision == 3 && c->stats.revisions_invalid == 0);
    inject_revision(&room, a, &v3, 3, far, false); // The same announcement again.
    room_run(&room, .2);
    CHECK(c->stats.revisions_ignored == 3 && c->revision_staged && c->revision == 2);
    inject_revision(&room, a, &v3, 4, far, true); // Newer, but its digest is wrong.
    room_run(&room, .2);
    CHECK(c->stats.revisions_invalid == 1 && !c->revision_staged && c->revision == 2);
    CHECK(s->peers[s->slot_peer[c->slot]].revision_ready == 2 && s->stats.revision == 2);
    CHECK(c->phase == BOAT_NET_CLIENT_PLAYING && room.clients[b].revision == 2);
    printf("revision_race: both clients switched at tick %" PRIu64 " (announced at %" PRIu64
           "), prediction restarted, duplicates ignored\n",
        commit, announced);
    room_close(&room);
    return 0;
}

// A joiner that is still downloading delays the announcement; a Hello during the lead is held
// and served the new definition after the commit.
static int revision_joiners(void)
{
    static Room room;
    CHECK(room_open_mode(&room, 72,
        (BoatNetServerConfig) { .resources = test_world(BOAT_LOCATION_OUTER) }, BOAT_ROOM_MODE_RACE,
        NULL));
    BoatNetServer *s = &room.server;
    room.race = true;
    const BoatRoomDefinition v1 = room.definition, v2 = revised_room(&v1, 1);
    unsigned a = room_join(&room, "alpha", NULL, 0), b = room_join(&room, "beta", NULL, 0);
    room_run(&room, 1);
    ROOM_UNTIL(&room, 3000, s->race.phase == BOAT_RACE_RACING);
    unsigned late = room_join_ex(&room, "late", NULL, 0, true); // Holds READY.
    room_run(&room, .5);
    BoatNetClient *g = &room.clients[late];
    CHECK(g->phase == BOAT_NET_CLIENT_PLAYING && g->ready_pending);
    CHECK(s->peers[s->slot_peer[g->slot]].phase == BOAT_NET_PEER_BOOTSTRAP);
    s->race.deadline = s->fleet.state.tick + 1;
    ROOM_UNTIL(&room, 100, s->race.phase == BOAT_RACE_RESULTS);
    CHECK(stage_room(&room, &v2, NULL));
    room_run(&room, 1);
    CHECK(s->stats.revisions_announced == 0 && s->stats.revision == 1); // Still joining.
    CHECK(boat_net_client_ready(g));
    ROOM_UNTIL(&room, 1200, s->announced.present);
    CHECK(s->announced.present && s->stats.revisions_announced == 1);
    // A Hello now waits for the commit.
    unsigned d = room_join(&room, "delta", NULL, 0);
    room_run(&room, .3);
    CHECK(s->stats.revision_held_hellos == 1 && room.clients[d].phase == BOAT_NET_CLIENT_HANDSHAKE);
    CHECK(boat_net_server_players(s) == 3);
    ROOM_UNTIL(&room, 2000, s->stats.revision == 2);
    CHECK(s->stats.revision == 2 && s->stats.revision_leaves == 0);
    room_run(&room, 2);
    CHECK(room.clients[d].phase == BOAT_NET_CLIENT_PLAYING && room.clients[d].accept.revision == 2);
    for (unsigned i = 0; i < 4; ++i)
        CHECK(client_runs(&room, i, &v2, 2) && room.clients[i].phase == BOAT_NET_CLIENT_PLAYING);
    CHECK(boat_net_server_players(s) == 4 && room.clients[a].stats.decode_errors == 0);
    CHECK(room.clients[b].stats.revisions == 1 && room.clients[d].stats.revisions == 0);
    printf("revision_joiners: a joiner mid-download delayed the announcement; a Hello during the "
           "lead was held and served revision 2\n");
    room_close(&room);
    return 0;
}

// A client that never answers READY is dropped with REVISION at the commit tick; the rest stay.
static int revision_not_ready(void)
{
    static Room room;
    BoatMatchRules rules = boat_fixture_match_default_rules;
    rules.round_ticks = 8 * BOAT_TICK_HZ;
    rules.intermission_ticks = 5 * BOAT_TICK_HZ;
    CHECK(
        room_open_mode(&room, 73, (BoatNetServerConfig) { 0 }, BOAT_ROOM_MODE_DEATHMATCH, &rules));
    BoatNetServer *s = &room.server;
    const BoatRoomDefinition v1 = room.definition, v2 = revised_room(&v1, 1);
    unsigned a = room_join(&room, "alpha", NULL, 0), b = room_join(&room, "beta", NULL, 0);
    room_run(&room, 4);
    CHECK(s->match.phase == BOAT_MATCH_PLAYING);
    s->match.phase_end = s->fleet.state.tick + 1;
    ROOM_UNTIL(&room, 100, s->match.phase == BOAT_MATCH_INTERMISSION);
    room.live[b] = false; // Beta stops answering before it can say READY.
    CHECK(stage_room(&room, &v2, NULL));
    ROOM_UNTIL(&room, 100, s->announced.present);
    CHECK(s->announced.present);
    unsigned slot_b = room.clients[b].slot;
    ROOM_UNTIL(&room, 2000, s->stats.revision == 2);
    CHECK(s->stats.revision == 2 && s->stats.revision_leaves == 1);
    CHECK(s->slot_peer[slot_b] < 0 && boat_net_server_players(s) == 1);
    CHECK(s->slot_peer[room.clients[a].slot] >= 0);
    room_run(&room, 1);
    BoatNetClient *ca = &room.clients[a];
    CHECK(ca->phase == BOAT_NET_CLIENT_PLAYING && client_runs(&room, a, &v2, 2));
    CHECK(ca->fleet.state.combat.rules.health == v2.combat_rules.health && same_match(ca, s));
    CHECK(s->match.rules.respawn_ticks == v2.match_rules.respawn_ticks);
    // The dropped client hears why when it is serviced again.
    room.live[b] = true;
    room_run(&room, .5);
    BoatNetClient *cb = &room.clients[b];
    CHECK(cb->phase == BOAT_NET_CLIENT_CLOSED && cb->leave == BOAT_NET_LEAVE_REVISION);
    CHECK(strstr(boat_net_client_failure(cb), "definition") != NULL);
    // A new player joins revision 2 and installs its checkpoint (the combat rules agree).
    unsigned g = room_join(&room, "gamma", NULL, 0);
    room_run(&room, 1);
    CHECK(room.clients[g].phase == BOAT_NET_CLIENT_PLAYING && client_runs(&room, g, &v2, 2));
    CHECK(room.clients[g].stats.bootstrap_failures == 0 && s->stats.output_faults == 0);
    printf("revision_not_ready: only the silent client was dropped (REVISION); a joiner installed "
           "revision 2\n");
    room_close(&room);
    return 0;
}

// A deathmatch commit lands on the last tick of the intermission, the next round runs the new
// rules; a commit that would not fit before the round starts waits for the next intermission.
static int revision_boundary(void)
{
    static Room room;
    BoatMatchRules rules = boat_fixture_match_default_rules;
    rules.round_ticks = 8 * BOAT_TICK_HZ;
    rules.intermission_ticks = 5 * BOAT_TICK_HZ;
    CHECK(
        room_open_mode(&room, 74, (BoatNetServerConfig) { 0 }, BOAT_ROOM_MODE_DEATHMATCH, &rules));
    BoatNetServer *s = &room.server;
    const BoatRoomDefinition v1 = room.definition, v2 = revised_room(&v1, 1),
                             v3 = revised_room(&v1, 2);
    unsigned a = room_join(&room, "alpha", NULL, 0), b = room_join(&room, "beta", NULL, 0);
    room_run(&room, 4);
    CHECK(s->match.phase == BOAT_MATCH_PLAYING);
    // Into the intermission, then fix its end one tick past the commit tick.
    s->match.phase_end = s->fleet.state.tick + 1;
    ROOM_UNTIL(&room, 100, s->match.phase == BOAT_MATCH_INTERMISSION);
    CHECK(stage_room(&room, &v2, NULL));
    uint64_t next = s->fleet.state.tick + 1;
    uint64_t commit = (next + BOAT_NET_REVISION_LEAD_TICKS + 3) / 4 * 4;
    uint32_t round = s->match.round;
    uint64_t exact = commit;
    s->match.phase_end = commit + 1;
    ROOM_UNTIL(&room, 2000, s->stats.revision == 2);
    CHECK(s->stats.revision == 2 && s->fleet.state.tick == commit
        && s->stats.revisions_announced == 1);
    CHECK(s->match.phase == BOAT_MATCH_INTERMISSION && s->match.round == round);
    CHECK(s->match.rules.respawn_ticks == v2.match_rules.respawn_ticks);
    ROOM_UNTIL(&room, 10, s->match.round == round + 1);
    CHECK(s->match.phase == BOAT_MATCH_PLAYING && s->fleet.state.tick == commit + 1);
    CHECK(s->match.rules.respawn_ticks == v2.match_rules.respawn_ticks);
    room_run(&room, 1);
    CHECK(client_runs(&room, a, &v2, 2) && client_runs(&room, b, &v2, 2)
        && same_match(&room.clients[a], s));
    // The next candidate: the intermission ends exactly at the would-be commit tick, so it does
    // not fit; the room waits for the following intermission.
    s->match.phase_end = s->fleet.state.tick + 1;
    ROOM_UNTIL(&room, 100, s->match.phase == BOAT_MATCH_INTERMISSION);
    CHECK(stage_room(&room, &v3, NULL));
    commit = (s->fleet.state.tick + 1 + BOAT_NET_REVISION_LEAD_TICKS + 3) / 4 * 4;
    s->match.phase_end = commit;
    ROOM_UNTIL(&room, 2000, s->match.phase == BOAT_MATCH_PLAYING);
    CHECK(s->match.phase == BOAT_MATCH_PLAYING && s->stats.revisions_announced == 1);
    CHECK(s->stats.revision == 2 && boat_net_server_revision_pending(s));
    room_run(&room, 1);
    s->match.phase_end = s->fleet.state.tick + 1;
    ROOM_UNTIL(&room, 2000, s->stats.revision == 3);
    CHECK(s->stats.revision == 3 && s->match.phase == BOAT_MATCH_INTERMISSION);
    CHECK(s->stats.revisions_announced == 2 && s->stats.revision_leaves == 0);
    room_run(&room, 1);
    CHECK(client_runs(&room, a, &v3, 3) && client_runs(&room, b, &v3, 3));
    CHECK(room.clients[a].phase == BOAT_NET_CLIENT_PLAYING && room.mismatches == 0);
    printf("revision_boundary: committed on the intermission's last tick %" PRIu64
           "; one that did not fit waited for the next\n",
        exact);
    room_close(&room);
    return 0;
}

static const TestCase cases[] = {
    SLOW_CASE(throttle_detents),
    TEST_CASE(golden),
    TEST_CASE(room_definition_codec),
    TEST_CASE(definition_rejected),
    TEST_CASE(codec),
    SLOW_CASE(checkpoints),
    TEST_CASE(deferred_ready),
    TEST_CASE(snapshot_resync),
    TEST_CASE(combat_compact),
    TEST_CASE(lossless),
    TEST_CASE(rule_events),
    TEST_CASE(objective_state),
    SLOW_CASE(revision_free),
    SLOW_CASE(revision_race),
    SLOW_CASE(revision_joiners),
    SLOW_CASE(revision_not_ready),
    SLOW_CASE(revision_boundary),
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
    TEST_CASE(resync_same_tick),
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
