#include "net/boat_net_client.h"
#include "room/boat_session_record.h"
#include "support/boat_test_cases.h"
#include "support/boat_test_world.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct {
    size_t offset;
    uint64_t tick, fleet, objective, definition, source, race_go;
    uint32_t revision, source_revision;
    bool suspended, has_objective;
    BoatNetObjectiveState hud;
} Expected;

typedef struct {
    uint8_t *bytes;
    size_t size, capacity;
    Expected keys[32];
    unsigned key_count, nonzero_inputs, events, mutations;
    unsigned calls, fail_at;
} Recording;

static BoatNetServer server;
static BoatNetClient clients[2];
static BoatRoomObjective objective;
static BoatRoomObjectiveSource source, candidate;
static BoatRoomDefinition room, revised;
static BoatSessionRecorder recorder;
static BoatSessionPlayback playback;
static BoatNetMemory *network;
static bool connected[2];
static double now;
static Recording recording;
static bool armed, combat_activity;

static bool sink(void *context, const uint8_t *bytes, size_t size)
{
    Recording *r = context;
    if (++r->calls == r->fail_at)
        return false;
    if (size > 32u * 1024u * 1024u - r->size)
        return false;
    if (size > r->capacity - r->size) {
        size_t capacity = r->capacity ? r->capacity * 2 : 1024 * 1024;
        while (capacity < r->size + size)
            capacity *= 2;
        uint8_t *next = realloc(r->bytes, capacity);
        if (!next)
            return false;
        r->bytes = next;
        r->capacity = capacity;
    }
    BoatSessionRecord record;
    if (!boat_session_record_decode(bytes, size, &record))
        return false;
    if (record.kind == BOAT_SESSION_RECORD_KEYFRAME) {
        if (r->key_count == 32)
            return false;
        const BoatRoomObjective *o = recorder.objective;
        r->keys[r->key_count++] = (Expected) { .offset = r->size,
            .tick = server.fleet.state.tick,
            .fleet = boat_fleet_hash(&server.fleet.state),
            .objective = o ? boat_plugin_objective_digest(&o->objective) : 0,
            .definition = boat_room_definition_digest(server.room),
            .source = o ? boat_net_bytes_digest(o->source->source, o->source->source_size) : 0,
            .race_go = server.race_room && server.race.round ? server.race.go_tick : 0,
            .revision = server.stats.revision,
            .source_revision = o ? o->revision : 0,
            .suspended = o && o->objective.quarantined,
            .has_objective = o != NULL,
            .hud = server.objective };
    }
    if (record.kind == BOAT_SESSION_RECORD_INPUT)
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            if (record.payload[i * 12] || record.payload[i * 12 + 4])
                ++r->nonzero_inputs;
    r->events += record.kind == BOAT_SESSION_RECORD_EVENT;
    r->mutations += record.kind == BOAT_SESSION_RECORD_MUTATION;
    memcpy(r->bytes + r->size, bytes, size);
    r->size += size;
    return true;
}

static bool initialize(void)
{
    free(recording.bytes);
    recording = (Recording) { 0 };
    memset(connected, 0, sizeof(connected));
    armed = combat_activity = false;
    now = 0;
    char error[220];
    if (!boat_room_package_load_objective("proteus.outer-hunt", false, false, BOAT_LOCATION_OUTER,
            test_world(BOAT_LOCATION_OUTER), &room, &source, error, sizeof(error))
        || !boat_room_objective_init(&objective, &source))
        return false;
    network = boat_net_memory_create(55);
    if (!network)
        return false;
    BoatNetServerConfig config = { .token = "",
        .epoch = 0x51ee,
        .max_players = 2,
        .room = &room,
        .resources = test_world(BOAT_LOCATION_OUTER),
        .rules = boat_room_objective_rules(&objective) };
    if (!boat_net_server_init(&server, boat_net_memory_server(network), &config))
        return false;
    if (!boat_session_record_begin(&recorder, &server, &objective, sink, &recording))
        return false;
    server.config.record = boat_session_record_adapter(&recorder);
    return boat_net_server_service(&server, 0);
}

static bool join(unsigned index)
{
    unsigned link;
    BoatNetTransport transport;
    if (!boat_net_memory_client(network, &link, &transport))
        return false;
    BoatNetClientConfig config = { .name = index ? "two" : "one",
        .token = "",
        .build = "record-contract",
        .resources = test_worlds };
    boat_net_client_init(&clients[index], transport, &config, now);
    connected[index] = true;
    return true;
}

static void leave(unsigned index)
{
    boat_net_client_leave(&clients[index]);
    connected[index] = false;
}

static bool advance(uint64_t target)
{
    while (server.fleet.state.tick < target) {
        now += 1.0 / 240;
        boat_net_memory_set_time(network, now);
        if (!boat_net_server_service(&server, now))
            return false;
        for (unsigned i = 0; i < 2; ++i)
            if (connected[i])
                boat_net_client_service(&clients[i], now,
                    (BoatInput) { .steering_rate = i ? -.3f : .25f,
                        .throttle_rate = 1,
                        .gun = armed,
                        .grenade = armed ? 1 : 0,
                        .torpedo = armed ? 1 : 0 },
                    false);
        if (server.fleet.state.combat.enabled)
            for (unsigned i = 0; i < BOAT_COMBAT_BULLETS; ++i)
                combat_activity |= server.fleet.state.combat.bullets[i].born > 0;
        if (recorder.failed)
            return false;
    }
    return true;
}

static bool matches(const Expected *e)
{
    bool objective_matches = !e->has_objective
        ? !playback.has_objective && !playback.objective.source
        : boat_plugin_objective_digest(&playback.objective.objective) == e->objective
            && playback.objective.objective.quarantined == e->suspended
            && playback.objective.revision == e->source_revision && playback.objective.source
            && boat_net_bytes_digest(
                   playback.objective.source->source, playback.objective.source->source_size)
                == e->source;
    return playback.fleet.state.tick == e->tick
        && boat_fleet_hash(&playback.fleet.state) == e->fleet && objective_matches
        && playback.revision == e->revision && playback.race_go == e->race_go
        && boat_room_definition_digest(&playback.room) == e->definition
        && playback.hud.revision == e->hud.revision
        && boat_net_objective_state_equal(&playback.hud, &e->hud);
}

static int replay_from(unsigned first_key, bool seek)
{
    memset(&playback, 0, sizeof(playback));
    BoatSessionRecord record;
    CHECK(boat_session_record_decode(recording.bytes, recording.size, &record));
    CHECK(boat_session_record_apply(&playback, &record, false));
    size_t at = seek ? recording.keys[first_key].offset : record.bytes;
    unsigned key = seek ? first_key : 0;
    while (at < recording.size) {
        CHECK(boat_session_record_decode(recording.bytes + at, recording.size - at, &record));
        if (!boat_session_record_apply(&playback, &record, seek)) {
            fprintf(stderr, "replay: offset=%zu sequence=%u kind=%u tick=%" PRIu64 "\n", at,
                record.sequence, record.kind, record.tick);
            return 1;
        }
        if (record.kind == BOAT_SESSION_RECORD_KEYFRAME) {
            CHECK(key < recording.key_count && matches(&recording.keys[key]));
            ++key;
        }
        at += record.bytes;
    }
    CHECK(playback.ended && key == recording.key_count);
    CHECK(matches(&recording.keys[recording.key_count - 1]));
    boat_session_record_close(&playback);
    return 0;
}

static int hunt(void)
{
    CHECK(initialize());
    CHECK(join(0) && join(1) && advance(120));
    CHECK(boat_net_server_players(&server) == 2);
    CHECK(server.fleet.state.slots[0].active && server.fleet.state.slots[1].active);
    // Native placement is an explicit between-tick mutation; detection produces real mark events.
    Boat boat = server.fleet.state.slots[0].boat;
    boat.position = (Vector2) { room.marks.items[0].x, room.marks.items[0].z };
    boat.velocity = (Vector2) { 0 };
    boat_fleet_relocate(&server.fleet, 0, boat);
    CHECK(advance(144));
    bool scored = false;
    for (unsigned i = 0; i < objective.objective.hud_count; ++i)
        if (!strcmp(objective.objective.hud[i].id, "c1") && objective.objective.hud[i].value == 1)
            scored = true;
    CHECK(scored);
    uint32_t life = server.fleet.state.slots[0].life;
    boat_net_client_request_reset(&clients[0]);
    CHECK(advance(240));
    CHECK(server.stats.spawn_requests > 0 && server.fleet.state.slots[0].life > life);
    candidate = source;
    char *label = strstr(candidate.source, "CREW ");
    CHECK(label);
    memcpy(label, "TEAM ", 5);
    CHECK(boat_room_objective_stage(&objective, &candidate, NULL));
    CHECK(advance(264) && objective.revision == 2);
    leave(1);
    CHECK(advance(288) && boat_net_server_players(&server) == 1);
    CHECK(join(1) && advance(384) && boat_net_server_players(&server) == 2);
    revised = room;
    revised.marks.items[0].x += 100;
    candidate.marks = revised.marks;
    CHECK(boat_room_objective_stage_revision(&objective, &server, &revised, &candidate, NULL));
    CHECK(advance(408) && server.stats.revision == 1);
    leave(0);
    leave(1);
    CHECK(advance(432) && server.stats.revision == 2 && objective.revision == 3);
    CHECK(join(0) && advance(552));
    CHECK(boat_net_server_players(&server) == 1);
    CHECK(advance(1800)); // More than ten seconds since both accepted revisions.
    // A valid reload that errors in update preserves its checkpoint and records suspension.
    candidate = *objective.source;
    static const char broken[]
        = "return {init=function(c) return {},{} end, "
          "migrate=function(c,s) return s,{} end, update=function(c,s) error('record test') end}";
    memset(candidate.source, 0, sizeof(candidate.source));
    memcpy(candidate.source, broken, sizeof(broken) - 1);
    candidate.source_size = sizeof(broken) - 1;
    CHECK(boat_room_objective_stage(&objective, &candidate, NULL));
    CHECK(advance(1836) && objective.objective.quarantined);
    CHECK(boat_session_record_end(&recorder, &server));
    CHECK(recording.key_count >= 6 && recording.nonzero_inputs > 100 && recording.mutations >= 5);
    CHECK(recording.events > 5 && server.stats.accepted == 4);
    bool periodic = false;
    for (unsigned i = 1; i < recording.key_count; ++i)
        if (recording.keys[i].tick - recording.keys[i - 1].tick == 1200)
            periodic = true;
    CHECK(periodic);
    CHECK(replay_from(0, false) == 0);
    for (unsigned i = 0; i < recording.key_count; ++i)
        CHECK(replay_from(i, true) == 0);
    printf("record hunt: ticks=%" PRIu64 " bytes=%zu keyframes=%u\n", server.fleet.state.tick,
        recording.size, recording.key_count);
    boat_net_memory_destroy(network);
    network = NULL;
    return 0;
}

static void checksum(uint8_t *bytes, size_t size)
{
    uint64_t digest = boat_net_bytes_digest(bytes, 24)
        ^ boat_net_bytes_digest(
            bytes + BOAT_SESSION_RECORD_HEADER, size - BOAT_SESSION_RECORD_HEADER);
    for (unsigned i = 0; i < 8; ++i)
        bytes[24 + i] = (uint8_t)(digest >> (56 - i * 8));
}

static int native_room(BoatRoomMode mode)
{
    free(recording.bytes);
    recording = (Recording) { 0 };
    memset(connected, 0, sizeof(connected));
    armed = mode == BOAT_ROOM_MODE_DEATHMATCH;
    combat_activity = false;
    now = 0;
    BoatLocationId location
        = mode == BOAT_ROOM_MODE_RACE ? BOAT_LOCATION_OUTER : BOAT_LOCATION_BASIN;
    char error[220];
    CHECK(boat_room_package_load(NULL, mode == BOAT_ROOM_MODE_DEATHMATCH,
        mode == BOAT_ROOM_MODE_RACE, location, test_world(location), &room, error, sizeof(error)));
    // The shipped course and combat rules, with shorter native match rounds to cross an
    // intermission and clear/restart the combat pool within this virtual-clock fixture.
    if (mode == BOAT_ROOM_MODE_DEATHMATCH) {
        room.match_rules.round_ticks = 600;
        room.match_rules.intermission_ticks = 120;
    }
    network = boat_net_memory_create(91);
    CHECK(network);
    BoatNetServerConfig config = { .token = "",
        .epoch = 0x51ef,
        .max_players = 2,
        .waves = mode == BOAT_ROOM_MODE_FREE,
        .room = &room,
        .resources = test_world(location) };
    CHECK(boat_net_server_init(&server, boat_net_memory_server(network), &config));
    CHECK(boat_session_record_begin(&recorder, &server, NULL, sink, &recording));
    server.config.record = boat_session_record_adapter(&recorder);
    CHECK(boat_net_server_service(&server, 0));
    CHECK(join(0) && join(1) && advance(600));
    CHECK(boat_net_server_players(&server) == 2 && !server.has_objective);
    if (mode == BOAT_ROOM_MODE_RACE) {
        CHECK(server.race.round == 1 && server.race.go_tick > 0);
        CHECK(server.race.phase == BOAT_RACE_RACING);
        // Recovery after GO changes placement; replay must also retain the course's clock.
        boat_net_client_request_reset(&clients[0]);
    } else
        boat_net_client_request_reset(&clients[1]);
    CHECK(advance(1320));
    if (mode == BOAT_ROOM_MODE_DEATHMATCH)
        CHECK(server.match.round >= 2 && server.fleet.state.combat.enabled && combat_activity);
    if (mode == BOAT_ROOM_MODE_RACE)
        CHECK(server.race.entrants[0].recoveries > 0);
    CHECK(server.stats.spawn_requests > 0);
    CHECK(boat_session_record_end(&recorder, &server));
    CHECK(recording.key_count == 3 && recording.keys[1].tick == 1200);
    CHECK(recording.nonzero_inputs > 100 && recording.mutations > 0);
    CHECK(replay_from(0, false) == 0);
    for (unsigned i = 0; i < recording.key_count; ++i)
        CHECK(replay_from(i, true) == 0);
    printf("record native: mode=%u ticks=%" PRIu64 " bytes=%zu keyframes=%u\n", (unsigned)mode,
        server.fleet.state.tick, recording.size, recording.key_count);
    boat_net_memory_destroy(network);
    network = NULL;
    return 0;
}

static int free_room(void) { return native_room(BOAT_ROOM_MODE_FREE); }

static int race_room(void) { return native_room(BOAT_ROOM_MODE_RACE); }

static int deathmatch_room(void) { return native_room(BOAT_ROOM_MODE_DEATHMATCH); }

static int queue_quarantine(void)
{
    CHECK(initialize() && advance(13));
    // Multiple native event producers can fill the objective queue between its cadence ticks.
    // Preserve their production ordering: recording observes each event before the adapter.
    BoatNetRuleEvent event = { .kind = BOAT_NET_RULE_MARK_REACHED, .slot = 0, .mark = 0 };
    for (unsigned i = 0; i <= BOAT_PLUGIN_EVENTS; ++i) {
        server.config.record.event(server.config.record.context, 13, &event);
        server.config.rules.event(server.config.rules.context, 13, &event);
    }
    CHECK(objective.objective.quarantined && objective.objective.tick == 12);
    CHECK(advance(14) && server.objective.suspended);
    CHECK(boat_session_record_end(&recorder, &server));
    CHECK(replay_from(0, false) == 0);
    CHECK(replay_from(recording.key_count - 1, true) == 0);
    boat_net_memory_destroy(network);
    network = NULL;
    return 0;
}

static int malformed(void)
{
    CHECK(initialize() && advance(24));
    CHECK(boat_session_record_end(&recorder, &server));
    BoatSessionRecord identity;
    CHECK(boat_session_record_decode(recording.bytes, recording.size, &identity));
    for (size_t i = 0; i < identity.bytes; ++i) {
        BoatSessionRecord untouched = { .sequence = 777 };
        CHECK(!boat_session_record_decode(recording.bytes, i, &untouched));
        CHECK(untouched.sequence == 777);
    }
    uint8_t copy[512];
    CHECK(identity.bytes <= sizeof(copy));
    memcpy(copy, recording.bytes, identity.bytes);
    copy[BOAT_SESSION_RECORD_HEADER + 3] ^= 1; // Unknown file version, with a valid checksum.
    checksum(copy, identity.bytes);
    BoatSessionRecord invalid;
    CHECK(boat_session_record_decode(copy, identity.bytes, &invalid));
    memset(&playback, 0, sizeof(playback));
    CHECK(!boat_session_record_apply(&playback, &invalid, false));
    boat_session_record_close(&playback);
    memcpy(copy, recording.bytes, identity.bytes);
    copy[identity.bytes - 1] ^= 1;
    CHECK(!boat_session_record_decode(copy, identity.bytes, &invalid));

    memset(&playback, 0, sizeof(playback));
    size_t at = 0;
    while (at < recording.size) {
        BoatSessionRecord record;
        CHECK(boat_session_record_decode(recording.bytes + at, recording.size - at, &record));
        CHECK(!boat_session_record_decode(recording.bytes + at, record.bytes - 1, &invalid));
        if (record.kind == BOAT_SESSION_RECORD_END)
            break;
        CHECK(boat_session_record_apply(&playback, &record, false));
        at += record.bytes;
    }
    CHECK(!playback.ended); // EOF on a record boundary still lacks the mandatory closing record.
    boat_session_record_close(&playback);

    memset(&playback, 0, sizeof(playback));
    CHECK(boat_session_record_apply(&playback, &identity, false));
    CHECK(!boat_session_record_apply(&playback, &identity, false)); // Repeated/out-of-order record.
    boat_session_record_close(&playback);
    boat_net_memory_destroy(network);
    network = NULL;
    return 0;
}

static int sink_failure(void)
{
    CHECK(initialize());
    recording.fail_at = recording.calls + 1;
    CHECK(!advance(12) && recorder.failed);
    unsigned calls = recording.calls;
    CHECK(!boat_session_record_end(&recorder, &server));
    CHECK(recording.calls == calls); // Permanent failure: no partial END or further writes.
    boat_net_memory_destroy(network);
    network = NULL;
    return 0;
}

// A syntactically valid, re-checksummed input edit must fail at the immediately following
// result, before any mutation or keyframe could replace the replayed physical state.
static int input_mismatch(void)
{
    CHECK(initialize() && join(0) && advance(120));
    CHECK(boat_session_record_end(&recorder, &server));
    memset(&playback, 0, sizeof(playback));
    bool changed = false, rejected = false;
    size_t at = 0;
    while (at < recording.size) {
        BoatSessionRecord record;
        CHECK(boat_session_record_decode(recording.bytes + at, recording.size - at, &record));
        if (record.kind == BOAT_SESSION_RECORD_INPUT && record.tick == 100) {
            uint8_t bytes[128];
            CHECK(record.bytes <= sizeof(bytes));
            memcpy(bytes, recording.bytes + at, record.bytes);
            bytes[BOAT_SESSION_RECORD_HEADER + 4] ^= 0x80; // Flip throttle's sign bit.
            checksum(bytes, record.bytes);
            BoatSessionRecord edited;
            CHECK(boat_session_record_decode(bytes, record.bytes, &edited));
            CHECK(boat_session_record_apply(&playback, &edited, false));
            changed = true;
        } else if (changed && record.kind == BOAT_SESSION_RECORD_RESULT) {
            CHECK(!boat_session_record_apply(&playback, &record, false));
            rejected = true;
            break;
        } else
            CHECK(boat_session_record_apply(&playback, &record, false));
        at += record.bytes;
    }
    CHECK(changed && rejected);
    boat_session_record_close(&playback);
    boat_net_memory_destroy(network);
    network = NULL;
    return 0;
}

static const TestCase cases[] = { SLOW_CASE(malformed), SLOW_CASE(sink_failure),
    SLOW_CASE(input_mismatch), SLOW_CASE(free_room), SLOW_CASE(race_room),
    SLOW_CASE(deathmatch_room), SLOW_CASE(queue_quarantine) };

int main(int argc, char **argv)
{
    int result;
    if (argc == 3 && !strcmp(argv[1], "--write")) {
        result = hunt();
        if (!result) {
            FILE *file = fopen(argv[2], "wb");
            if (!file)
                result = 1;
            else {
                if (fwrite(recording.bytes, 1, recording.size, file) != recording.size)
                    result = 1;
                if (fclose(file))
                    result = 1;
            }
        }
    } else {
        result = argc == 1 ? hunt() : 0;
        if (!result)
            result = test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
    }
    free(recording.bytes);
    test_worlds_close();
    return result;
}
