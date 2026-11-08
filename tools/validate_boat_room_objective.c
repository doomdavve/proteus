// Plugin P5b: a free package's room objective on the authority. The bundled hunt loads with its
// marks and source, its Lua runs only behind the server's rules adapter, failures quarantine it
// while the room keeps running, and a memory-transport room with two clients scores real mark
// entries. Time is the virtual service clock only.
#include "boat_net_client.h"
#include "boat_net_server.h"
#include "boat_room_objective.h"
#include "boat_room_package.h"
#include "boat_test_cases.h"
#include "boat_test_world.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static BoatRoomObjectiveSource hunt;
static BoatRoomDefinition hunt_room;

static bool load_hunt(void)
{
    char error[220];
    if (boat_room_package_load_objective("proteus.outer-hunt", false, false, BOAT_LOCATION_OUTER,
            test_world(BOAT_LOCATION_OUTER), &hunt_room, &hunt, error, sizeof(error)))
        return true;
    fprintf(stderr, "outer-hunt: %s\n", error);
    return false;
}

static const BoatPluginHud *row(const BoatRoomObjective *o, const char *id)
{
    for (unsigned i = 0; i < o->objective.hud_count; ++i)
        if (!strcmp(o->objective.hud[i].id, id))
            return &o->objective.hud[i];
    return NULL;
}

static int64_t value(const BoatRoomObjective *o, const char *id)
{
    const BoatPluginHud *r = row(o, id);
    return r ? r->value : -1;
}

static void send(
    BoatNetRules *rules, uint64_t tick, BoatNetRuleKind kind, unsigned slot, unsigned mark)
{
    BoatNetRuleEvent e = { .kind = (uint8_t)kind, .slot = (uint8_t)slot, .mark = (uint8_t)mark };
    rules->event(rules->context, tick, &e);
}

// The loader returns the hunt's marks and source; the plain loader cannot host it.
static int package(void)
{
    CHECK(load_hunt());
    CHECK(hunt.present && hunt.marks.count == 5 && hunt.state_version == 1);
    CHECK(hunt.source_size > 0 && strstr(hunt.source, "mark.reached"));
    CHECK(hunt_room.mode == BOAT_ROOM_MODE_FREE && !hunt_room.has_scene);
    CHECK(!strcmp(hunt_room.id, "proteus.outer-hunt"));
    BoatRoomDefinition plain;
    char error[220];
    CHECK(!boat_room_package_load("proteus.outer-hunt", false, false, BOAT_LOCATION_OUTER,
        test_world(BOAT_LOCATION_OUTER), &plain, error, sizeof(error)));
    CHECK(strstr(error, "room objective"));
    // The same package from its directory, and a package without an objective.
    static BoatRoomObjectiveSource from_directory;
    CHECK(boat_room_package_load_objective("plugins/outer-hunt", false, false, BOAT_LOCATION_OUTER,
        test_world(BOAT_LOCATION_OUTER), &plain, &from_directory, error, sizeof(error)));
    CHECK(from_directory.present && from_directory.source_size == hunt.source_size
        && !memcmp(from_directory.source, hunt.source, hunt.source_size)
        && !memcmp(&from_directory.marks, &hunt.marks, sizeof(hunt.marks)));
    CHECK(boat_room_package_load_objective(NULL, false, false, BOAT_LOCATION_OUTER,
        test_world(BOAT_LOCATION_OUTER), &plain, &from_directory, error, sizeof(error)));
    CHECK(!from_directory.present);
    return 0;
}

// Scripted rule events through the adapter: rows per crew, the 12-tick cadence, the first crew
// and the restart, and leaves.
static int adapter(void)
{
    CHECK(load_hunt());
    static BoatRoomObjective o;
    CHECK(boat_room_objective_init(&o, &hunt));
    CHECK(value(&o, "hunt") == 1 && !row(&o, "c1") && o.hud_changes == 1 && o.hud_changed);
    o.hud_changed = false;
    BoatNetRules rules = boat_room_objective_rules(&o);
    send(&rules, 3, BOAT_NET_RULE_CREW_JOINED, 0, 0);
    send(&rules, 3, BOAT_NET_RULE_CREW_JOINED, 2, 0);
    for (uint64_t tick = 4; tick < 12; ++tick)
        rules.after_tick(rules.context, tick);
    CHECK(!row(&o, "c1") && o.event_count == 2); // Not a boundary yet.
    rules.after_tick(rules.context, 12);
    CHECK(o.event_count == 0 && o.objective.acknowledged == 2 && o.objective.tick == 12);
    CHECK(value(&o, "c1") == 0 && value(&o, "c3") == 0 && !row(&o, "c2"));
    CHECK(o.hud_changed && o.hud_changes == 2);
    o.hud_changed = false;
    // A boundary with nothing new commits but leaves the HUD as it was.
    rules.after_tick(rules.context, 24);
    CHECK(o.objective.tick == 24 && !o.hud_changed && o.hud_changes == 2);
    // Crew 3 (slot 2) finds every mark, one twice; crew 1 finds one.
    for (unsigned m = 0; m < 5; ++m)
        send(&rules, 30, BOAT_NET_RULE_MARK_REACHED, 2, m);
    send(&rules, 31, BOAT_NET_RULE_MARK_REACHED, 2, 4);
    send(&rules, 31, BOAT_NET_RULE_MARK_REACHED, 0, 1);
    rules.after_tick(rules.context, 36);
    CHECK(value(&o, "c3") == 5 && value(&o, "c1") == 1 && value(&o, "first") == 3);
    CHECK(value(&o, "hunt") == 1);
    // Crew 1 leaves: everyone present has every mark, so a new hunt starts.
    send(&rules, 40, BOAT_NET_RULE_CREW_LEFT, 0, 0);
    rules.after_tick(rules.context, 48);
    CHECK(!row(&o, "c1") && value(&o, "c3") == 0 && value(&o, "hunt") == 2);
    CHECK(value(&o, "first") == 3 && !o.objective.quarantined);
    // Out-of-range events are not delivered.
    send(&rules, 50, BOAT_NET_RULE_MARK_REACHED, 2, 9);
    send(&rules, 50, BOAT_NET_RULE_CREW_JOINED, 7, 0);
    CHECK(o.event_count == 0);
    return 0;
}

// A failing update or a full queue quarantines the objective; later calls change nothing.
static int quarantine(void)
{
    static BoatRoomObjectiveSource broken;
    broken = (BoatRoomObjectiveSource) {
        .present = true, .marks = { .count = 1 }, .state_version = 1
    };
    strcpy(broken.marks.items[0].id, "a");
    const char *source = "return { init = function(c) return {}, {{kind='hud.set', "
                         "generation=c.generation, id='n', label='N', value=1}} end, "
                         "update = function(c, s) if #c.events > 0 then error('boom') end "
                         "return s, {} end }";
    broken.source_size = strlen(source);
    memcpy(broken.source, source, broken.source_size);
    static BoatRoomObjective o;
    CHECK(boat_room_objective_init(&o, &broken));
    BoatNetRules rules = boat_room_objective_rules(&o);
    rules.after_tick(rules.context, 12);
    CHECK(!o.objective.quarantined && o.objective.tick == 12);
    send(&rules, 13, BOAT_NET_RULE_CREW_JOINED, 0, 0);
    rules.after_tick(rules.context, 24);
    CHECK(o.objective.quarantined && strstr(o.diagnostic.message, "boom"));
    CHECK(o.objective.tick == 12 && o.objective.hud_count == 1 && o.objective.hud[0].value == 1);
    send(&rules, 25, BOAT_NET_RULE_CREW_JOINED, 1, 0);
    rules.after_tick(rules.context, 36);
    CHECK(o.event_count == 1 && o.objective.tick == 12); // Nothing queued or run after it.

    // A full event queue between boundaries.
    CHECK(load_hunt());
    CHECK(boat_room_objective_init(&o, &hunt));
    rules = boat_room_objective_rules(&o);
    for (unsigned i = 0; i <= BOAT_PLUGIN_EVENTS; ++i)
        send(&rules, 5, BOAT_NET_RULE_MARK_REACHED, i % 4, i % 5);
    CHECK(o.objective.quarantined && strstr(o.diagnostic.message, "queue full"));
    rules.after_tick(rules.context, 12);
    CHECK(o.objective.tick == 0);

    // An init failure leaves a quarantined objective that ignores everything.
    const char *bad = "return { init = function(c) error('no') end, update = function() end }";
    broken.source_size = strlen(bad);
    memcpy(broken.source, bad, broken.source_size);
    CHECK(!boat_room_objective_init(&o, &broken));
    CHECK(o.objective.quarantined && strstr(o.diagnostic.message, "no"));
    send(&rules, 1, BOAT_NET_RULE_CREW_JOINED, 0, 0);
    CHECK(o.event_count == 0);
    return 0;
}

// The hunt in a memory-transport room: two clients join, boats are moved through the marks on
// the authority, and the objective's HUD follows. The room keeps running with no clients' Lua.
static BoatNetServer server;
static BoatNetClient clients[2];
static BoatNetMemory *network;
static double now;

static void step(double seconds)
{
    for (double end = now + seconds; now < end;) {
        now += 1.0 / 240;
        boat_net_memory_set_time(network, now);
        boat_net_server_service(&server, now);
        for (unsigned i = 0; i < 2; ++i)
            if (clients[i].phase < BOAT_NET_CLIENT_REJECTED)
                boat_net_client_service(&clients[i], now, (BoatInput) { 0 }, false);
    }
}

static void visit(unsigned slot, const BoatMark *mark)
{
    Boat *boat = &server.fleet.state.slots[slot].boat;
    boat->position = (Vector2) { mark->x, mark->z };
    boat->velocity = (Vector2) { 0 };
    step(.15); // More than one objective boundary.
}

static int room(void)
{
    CHECK(load_hunt());
    static BoatRoomObjective o;
    CHECK(boat_room_objective_init(&o, &hunt));
    network = boat_net_memory_create(5);
    CHECK(network);
    BoatNetServerConfig config = { .token = "",
        .epoch = 0x5eed,
        .max_players = 2,
        .room = &hunt_room,
        .resources = test_world(BOAT_LOCATION_OUTER),
        .marks = &hunt.marks,
        .rules = boat_room_objective_rules(&o) };
    CHECK(boat_net_server_init(&server, boat_net_memory_server(network), &config));
    for (unsigned i = 0; i < 2; ++i) {
        unsigned link;
        BoatNetTransport transport;
        CHECK(boat_net_memory_client(network, &link, &transport));
        BoatNetClientConfig c = {
            .name = i ? "two" : "one", .token = "", .build = "validate", .resources = test_worlds
        };
        boat_net_client_init(&clients[i], transport, &c, now);
    }
    step(1);
    CHECK(boat_net_server_players(&server) == 2);
    CHECK(server.fleet.state.slots[0].active && server.fleet.state.slots[1].active);
    CHECK(value(&o, "c1") == 0 && value(&o, "c2") == 0 && value(&o, "hunt") == 1);
    // Crew 1 visits every mark; crew 2 one of them.
    for (unsigned m = 0; m < hunt.marks.count; ++m)
        visit(0, &hunt.marks.items[m]);
    visit(1, &hunt.marks.items[2]);
    CHECK(value(&o, "c1") == 5 && value(&o, "c2") == 1 && value(&o, "first") == 1);
    // Staying inside a mark adds nothing.
    step(.5);
    CHECK(value(&o, "c2") == 1);
    for (unsigned m = 0; m < hunt.marks.count; ++m)
        if (m != 2)
            visit(1, &hunt.marks.items[m]);
    CHECK(value(&o, "hunt") == 2 && value(&o, "c1") == 0 && value(&o, "c2") == 0);
    // Crew 2 leaves; its row goes.
    boat_net_client_leave(&clients[1]);
    step(.2);
    CHECK(boat_net_server_players(&server) == 1 && !row(&o, "c2") && value(&o, "c1") == 0);
    CHECK(!o.objective.quarantined && server.stats.ticks == server.fleet.state.tick);
    printf("room: ticks=%" PRIu64 " hud_changes=%" PRIu64 " acknowledged=%" PRId64 "\n",
        server.fleet.state.tick, o.hud_changes, o.objective.acknowledged);
    boat_net_memory_destroy(network);
    return 0;
}

static const TestCase cases[] = {
    TEST_CASE(package),
    TEST_CASE(adapter),
    TEST_CASE(quarantine),
    TEST_CASE(room),
};

int main(int argc, char **argv)
{
    int result = test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
    test_worlds_close();
    return result;
}
