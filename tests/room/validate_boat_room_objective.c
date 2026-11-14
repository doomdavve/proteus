// Plugin P5b: a free package's room objective on the authority. The bundled hunt loads with its
// marks and source, its Lua runs only behind the server's rules adapter, failures quarantine it
// while the room keeps running, and a memory-transport room with two clients scores real mark
// entries. Time is the virtual service clock only.
#include "net/boat_net_client.h"
#include "net/boat_net_server.h"
#include "room/boat_room_objective.h"
#include "room/boat_room_package.h"
#include "support/boat_test_cases.h"
#include "support/boat_test_world.h"

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
    CHECK(hunt.source_size > 0 && strstr(hunt.source, "mark.reached") && hunt.migrates);
    CHECK(hunt_room.mode == BOAT_ROOM_MODE_FREE && !hunt_room.has_scene);
    CHECK(!strcmp(hunt_room.id, "proteus.outer-hunt"));
    CHECK(hunt_room.marks.count == 5 && !memcmp(&hunt_room.marks, &hunt.marks, sizeof(hunt.marks)));
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

static bool client_row(const BoatNetClient *c, const char *id)
{
    for (unsigned i = 0; i < c->objective.row_count; ++i)
        if (!strcmp(c->objective.rows[i].id, id))
            return true;
    return false;
}

// Two clients join the hunt over the memory transport; O is the room's objective.
static bool start_room(BoatRoomObjective *o)
{
    if (!load_hunt() || !boat_room_objective_init(o, &hunt))
        return false;
    now = 0;
    network = boat_net_memory_create(5);
    if (!network)
        return false;
    BoatNetServerConfig config = { .token = "",
        .epoch = 0x5eed,
        .max_players = 2,
        .room = &hunt_room,
        .resources = test_world(BOAT_LOCATION_OUTER),
        .rules = boat_room_objective_rules(o) };
    if (!boat_net_server_init(&server, boat_net_memory_server(network), &config))
        return false;
    for (unsigned i = 0; i < 2; ++i) {
        unsigned link;
        BoatNetTransport transport;
        if (!boat_net_memory_client(network, &link, &transport))
            return false;
        BoatNetClientConfig c = {
            .name = i ? "two" : "one", .token = "", .build = "validate", .resources = test_worlds
        };
        boat_net_client_init(&clients[i], transport, &c, now);
    }
    step(1);
    return boat_net_server_players(&server) == 2 && server.fleet.state.slots[0].active
        && server.fleet.state.slots[1].active;
}

static int room(void)
{
    static BoatRoomObjective o;
    CHECK(start_room(&o));
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
    step(.2);
    // Both clients mirror the server's HUD, revision included.
    CHECK(server.has_objective && server.objective.revision > 1);
    for (unsigned i = 0; i < 2; ++i)
        CHECK(clients[i].objective_valid
            && clients[i].objective.revision == server.objective.revision
            && boat_net_objective_state_equal(&clients[i].objective, &server.objective));
    CHECK(client_row(&clients[0], "c2"));
    // Crew 2 leaves; its row goes.
    boat_net_client_leave(&clients[1]);
    step(.2);
    CHECK(boat_net_server_players(&server) == 1 && !row(&o, "c2") && value(&o, "c1") == 0);
    CHECK(clients[0].objective.revision == server.objective.revision
        && boat_net_objective_state_equal(&clients[0].objective, &server.objective)
        && !client_row(&clients[0], "c2") && client_row(&clients[0], "c1"));
    CHECK(!o.objective.quarantined && server.stats.ticks == server.fleet.state.tick);
    printf("room: ticks=%" PRIu64 " hud_changes=%" PRIu64 " acknowledged=%" PRId64 "\n",
        server.fleet.state.tick, o.hud_changes, o.objective.acknowledged);
    boat_net_memory_destroy(network);
    return 0;
}

// P5h: four crews on lossy, reordering links. Every client mirrors the HUD, a late joiner and a
// replacement for a leaver included, and a hunt finishes and restarts for everyone.
#define CREWS 4
static BoatNetClient crew[CREWS + 1];
static unsigned crew_link[CREWS + 1];
static bool crew_used[CREWS + 1];

static void crews_step(double seconds)
{
    for (double end = now + seconds; now < end;) {
        now += 1.0 / 240;
        boat_net_memory_set_time(network, now);
        boat_net_server_service(&server, now);
        for (unsigned i = 0; i <= CREWS; ++i)
            if (crew_used[i] && crew[i].phase < BOAT_NET_CLIENT_REJECTED)
                boat_net_client_service(&crew[i], now, (BoatInput) { 0 }, false);
    }
}

static void crews_join(unsigned i, const char *name)
{
    BoatNetTransport transport;
    boat_net_memory_client(network, &crew_link[i], &transport);
    BoatNetClientConfig c
        = { .name = name, .token = "", .build = "validate", .resources = test_worlds };
    boat_net_client_init(&crew[i], transport, &c, now);
    crew_used[i] = true;
}

static void crews_visit(unsigned slot, const BoatMark *mark)
{
    Boat *boat = &server.fleet.state.slots[slot].boat;
    boat->position = (Vector2) { mark->x, mark->z };
    boat->velocity = (Vector2) { 0 };
    crews_step(.15);
}

static bool crews_agree(const BoatNetClient *c)
{
    return c->objective_valid && c->objective.revision == server.objective.revision
        && boat_net_objective_state_equal(&c->objective, &server.objective);
}

static int four_crews(void)
{
    static BoatRoomObjective o;
    CHECK(load_hunt() && boat_room_objective_init(&o, &hunt));
    now = 0;
    memset(crew, 0, sizeof(crew));
    memset(crew_used, 0, sizeof(crew_used));
    network = boat_net_memory_create(9);
    CHECK(network);
    BoatNetServerConfig config = { .token = "",
        .epoch = 0x5eed,
        .max_players = CREWS,
        .room = &hunt_room,
        .resources = test_world(BOAT_LOCATION_OUTER),
        .rules = boat_room_objective_rules(&o) };
    CHECK(boat_net_server_init(&server, boat_net_memory_server(network), &config));
    const char *names[CREWS + 1] = { "one", "two", "three", "four", "five" };
    BoatNetImpairment f = { .04, .01, .01, .002, .01, .02 };
    for (unsigned i = 0; i < 3; ++i) {
        crews_join(i, names[i]);
        boat_net_memory_impair(network, crew_link[i], f, f);
    }
    crews_step(2);
    CHECK(boat_net_server_players(&server) == 3);
    // Crews 1 and 3 each find marks; crew 2 stays at sea.
    for (unsigned m = 0; m < hunt.marks.count - 1; ++m)
        crews_visit(0, &hunt.marks.items[m]);
    crews_visit(2, &hunt.marks.items[1]);
    // A fourth crew joins late and a fifth is refused as full.
    crews_join(3, names[3]);
    boat_net_memory_impair(network, crew_link[3], f, f);
    crews_step(2);
    CHECK(boat_net_server_players(&server) == CREWS && crew[3].phase == BOAT_NET_CLIENT_PLAYING);
    crews_join(4, names[4]);
    crews_step(2);
    CHECK(crew[4].phase == BOAT_NET_CLIENT_REJECTED);
    CHECK(value(&o, "c1") == 4 && value(&o, "c3") == 1 && value(&o, "c4") == 0);
    // A crew leaves mid-hunt; its replacement starts at zero in the freed slot.
    unsigned left = crew[1].slot;
    boat_net_client_leave(&crew[1]);
    crews_step(1);
    CHECK(boat_net_server_players(&server) == CREWS - 1);
    crews_join(1, "replacement");
    boat_net_memory_impair(network, crew_link[1], f, f);
    crews_step(2);
    CHECK(crew[1].phase == BOAT_NET_CLIENT_PLAYING && crew[1].slot == left);
    CHECK(value(&o, "c2") == 0);
    // Crew 1 finishes the hunt: it restarts for all four.
    for (unsigned m = 0; m < hunt.marks.count; ++m)
        crews_visit(0, &hunt.marks.items[m]);
    for (unsigned m = 0; m < hunt.marks.count; ++m)
        crews_visit(1, &hunt.marks.items[m]);
    crews_visit(2, &hunt.marks.items[0]);
    crews_visit(3, &hunt.marks.items[0]);
    for (unsigned m = 1; m < hunt.marks.count; ++m) {
        crews_visit(2, &hunt.marks.items[m]);
        crews_visit(3, &hunt.marks.items[m]);
    }
    crews_step(1);
    CHECK(value(&o, "hunt") == 2 && value(&o, "c1") == 0 && value(&o, "c4") == 0);
    // Let the lossy links settle, then every client mirrors the authority's HUD.
    for (unsigned i = 0; i < CREWS; ++i)
        boat_net_memory_impair(
            network, crew_link[i], (BoatNetImpairment) { 0 }, (BoatNetImpairment) { 0 });
    crews_step(1);
    for (unsigned i = 0; i < CREWS; ++i)
        CHECK(crews_agree(&crew[i]) && crew[i].stats.decode_errors == 0);
    CHECK(!o.objective.quarantined && server.stats.malformed == 0);
    printf("four_crews: ticks=%" PRIu64 " revision=%u hunt=%" PRId64 "\n", server.fleet.state.tick,
        server.objective.revision, value(&o, "hunt"));
    boat_net_memory_destroy(network);
    return 0;
}

// P5e: reloading the hunt's source on a live objective.
static BoatRoomObjectiveSource edited; // The hunt with one textual change.

// EDITED = HUNT with the first FROM replaced by TO; false when FROM is not in the source.
static bool edit(const char *from, const char *to)
{
    edited = hunt;
    const char *at = strstr(hunt.source, from);
    if (!at)
        return false;
    size_t head = (size_t)(at - hunt.source), cut = strlen(from), add = strlen(to);
    if (hunt.source_size - cut + add >= sizeof(edited.source))
        return false;
    memcpy(edited.source + head, to, add);
    memcpy(edited.source + head + add, hunt.source + head + cut, hunt.source_size - head - cut);
    edited.source_size = hunt.source_size - cut + add;
    edited.source[edited.source_size] = 0;
    edited.migrates = strstr(edited.source, "migrate = function") != NULL;
    return true;
}

static const char MIGRATE[] = "migrate = function(c, s) return s, hud(c, s) end,";

// A hunt with crews 1 and 3 present, crew 3 holding three marks, committed at tick 12.
static bool scored(BoatRoomObjective *o, BoatNetRules *rules)
{
    if (!load_hunt() || !boat_room_objective_init(o, &hunt))
        return false;
    *rules = boat_room_objective_rules(o);
    send(rules, 3, BOAT_NET_RULE_CREW_JOINED, 0, 0);
    send(rules, 3, BOAT_NET_RULE_CREW_JOINED, 2, 0);
    for (unsigned m = 0; m < 3; ++m)
        send(rules, 4, BOAT_NET_RULE_MARK_REACHED, 2, m);
    rules->after_tick(rules->context, 12);
    return value(o, "c3") == 3 && value(o, "c1") == 0 && o->revision == 1;
}

// An edit with a migrate keeps every count; the new label shows and the generation moves.
static int reload_keeps_counts(void)
{
    static BoatRoomObjective o;
    BoatNetRules rules;
    BoatNetObjectiveState state;
    CHECK(scored(&o, &rules));
    CHECK(rules.state(rules.context, &state)); // The clients' picture so far.
    CHECK(edit("\"CREW \"", "\"TEAM \"") && edited.migrates);
    BoatPluginDiagnostic d;
    CHECK(boat_room_objective_stage(&o, &edited, &d));
    // Staging changes nothing by itself; an event queued before the boundary reaches new code.
    send(&rules, 15, BOAT_NET_RULE_MARK_REACHED, 0, 1);
    rules.after_tick(rules.context, 20);
    CHECK(o.revision == 1 && !strcmp(row(&o, "c3")->label, "CREW 3") && o.event_count == 1);
    o.hud_changed = false;
    rules.after_tick(rules.context, 24);
    CHECK(o.reload.kind == BOAT_ROOM_RELOAD_MIGRATED && o.reload.tick == 24 && o.revision == 2);
    CHECK(o.objective.generation == 2 && o.source == &o.owned && !o.has_pending);
    CHECK(!strcmp(row(&o, "c3")->label, "TEAM 3") && value(&o, "c3") == 3);
    CHECK(value(&o, "c1") == 1 && value(&o, "hunt") == 1 && o.event_count == 0);
    CHECK(o.hud_changed && o.objective.tick == 24 && !o.objective.quarantined);
    // The reload reports even when the rows are the same.
    CHECK(rules.state(rules.context, &state) && state.row_count == o.objective.hud_count);
    CHECK(!rules.state(rules.context, &state));
    CHECK(edit("local MARKS = 5", "local MARKS = 5 -- comment"));
    CHECK(boat_room_objective_stage(&o, &edited, &d));
    rules.after_tick(rules.context, 36);
    CHECK(o.revision == 3 && o.objective.generation == 3 && value(&o, "c3") == 3);
    CHECK(rules.state(rules.context, &state));
    return 0;
}

// A new state_version with a migrate that transforms the state.
static int reload_migrates(void)
{
    static BoatRoomObjective o;
    BoatNetRules rules;
    CHECK(scored(&o, &rules));
    CHECK(edit(MIGRATE, "migrate = function(c, s) s.hunt = s.hunt * 5 return s, hud(c, s) end,"));
    edited.state_version = 2;
    BoatPluginDiagnostic d;
    CHECK(boat_room_objective_stage(&o, &edited, &d));
    rules.after_tick(rules.context, 24);
    CHECK(o.reload.kind == BOAT_ROOM_RELOAD_MIGRATED && o.objective.state_version == 2);
    CHECK(value(&o, "hunt") == 5 && value(&o, "c3") == 3 && o.objective.generation == 2);
    return 0;
}

// A candidate without migrate restarts: state resets, present crews are announced again, and
// marks queued before the boundary count in the new run.
static int reload_restart(void)
{
    static BoatRoomObjective o;
    BoatNetRules rules;
    CHECK(scored(&o, &rules));
    send(&rules, 20, BOAT_NET_RULE_CREW_JOINED, 1, 0);
    send(&rules, 20, BOAT_NET_RULE_CREW_LEFT, 1, 0);
    rules.after_tick(rules.context, 24);
    CHECK(edit(MIGRATE, "") && !edited.migrates);
    BoatPluginDiagnostic d;
    CHECK(boat_room_objective_stage(&o, &edited, &d));
    send(&rules, 30, BOAT_NET_RULE_MARK_REACHED, 0, 4);
    rules.after_tick(rules.context, 36);
    CHECK(o.reload.kind == BOAT_ROOM_RELOAD_RESTARTED && o.revision == 2);
    CHECK(o.objective.generation == 2 && o.objective.tick == 36 && o.event_count == 0);
    CHECK(value(&o, "c1") == 1 && value(&o, "c3") == 0 && !row(&o, "c2") && !row(&o, "c4"));
    CHECK(value(&o, "hunt") == 1 && !row(&o, "first"));
    CHECK(o.sequence > 8);
    // Later events keep increasing sequence numbers and are delivered.
    send(&rules, 40, BOAT_NET_RULE_MARK_REACHED, 2, 0);
    rules.after_tick(rules.context, 48);
    CHECK(value(&o, "c3") == 1 && !o.objective.quarantined);
    return 0;
}

// Failed candidates change nothing; later good ones still apply; a fix clears quarantine.
static int reload_broken(void)
{
    static BoatRoomObjective o;
    BoatNetRules rules;
    CHECK(scored(&o, &rules));
    BoatPluginDiagnostic d;
    CHECK(edit("local MARKS = 5", "local MARKS = = 5"));
    CHECK(boat_room_objective_stage(&o, &edited, &d));
    send(&rules, 15, BOAT_NET_RULE_MARK_REACHED, 0, 1);
    rules.after_tick(rules.context, 24);
    CHECK(o.reload.kind == BOAT_ROOM_RELOAD_REJECTED && o.reload.diagnostic.message[0]);
    CHECK(o.revision == 1 && o.objective.generation == 1 && o.source != &o.owned);
    CHECK(!o.objective.quarantined && !o.has_pending && o.reloads == 1);
    // The running code still got the queued event at the same boundary.
    CHECK(value(&o, "c1") == 1 && value(&o, "c3") == 3 && o.objective.tick == 24);
    CHECK(edit(MIGRATE, "migrate = function(c, s) error('nope') end,"));
    CHECK(boat_room_objective_stage(&o, &edited, &d));
    rules.after_tick(rules.context, 36);
    CHECK(
        o.reload.kind == BOAT_ROOM_RELOAD_REJECTED && strstr(o.reload.diagnostic.message, "nope"));
    CHECK(o.revision == 1 && o.objective.generation == 1 && value(&o, "c3") == 3 && o.reloads == 2);
    // A newer stage replaces an older pending one.
    CHECK(edit("\"CREW \"", "\"ONE \""));
    CHECK(boat_room_objective_stage(&o, &edited, &d));
    CHECK(edit("\"CREW \"", "\"TWO \""));
    CHECK(boat_room_objective_stage(&o, &edited, &d));
    rules.after_tick(rules.context, 48);
    CHECK(o.revision == 2 && !strcmp(row(&o, "c3")->label, "TWO 3") && value(&o, "c3") == 3);

    // A quarantined objective (full queue) keeps its queue and status through a bad candidate,
    // and a good one clears the quarantine and delivers the queue.
    CHECK(scored(&o, &rules));
    for (unsigned i = 0; i < BOAT_PLUGIN_EVENTS; ++i)
        send(&rules, 14, BOAT_NET_RULE_MARK_REACHED, 1, i % 5);
    send(&rules, 14, BOAT_NET_RULE_MARK_REACHED, 1, 0);
    CHECK(o.objective.quarantined && o.event_count == BOAT_PLUGIN_EVENTS);
    char reason[sizeof(o.diagnostic.message)];
    snprintf(reason, sizeof(reason), "%s", o.diagnostic.message);
    CHECK(edit("local MARKS = 5", "local MARKS = = 5"));
    CHECK(boat_room_objective_stage(&o, &edited, &d));
    rules.after_tick(rules.context, 24);
    CHECK(o.reload.kind == BOAT_ROOM_RELOAD_REJECTED && o.objective.quarantined);
    CHECK(o.event_count == BOAT_PLUGIN_EVENTS && !strcmp(o.diagnostic.message, reason));
    CHECK(edit("\"CREW \"", "\"FIX \""));
    CHECK(boat_room_objective_stage(&o, &edited, &d));
    rules.after_tick(rules.context, 36);
    CHECK(o.reload.kind == BOAT_ROOM_RELOAD_MIGRATED && !o.objective.quarantined);
    CHECK(o.event_count == 0 && o.diagnostic.status == BOAT_PLUGIN_OK && value(&o, "c2") == 5);
    // The same fix as a restart also clears quarantine.
    CHECK(scored(&o, &rules));
    for (unsigned i = 0; i <= BOAT_PLUGIN_EVENTS; ++i)
        send(&rules, 14, BOAT_NET_RULE_MARK_REACHED, 1, i % 5);
    CHECK(o.objective.quarantined);
    CHECK(edit(MIGRATE, "") && !edited.migrates);
    CHECK(boat_room_objective_stage(&o, &edited, &d));
    rules.after_tick(rules.context, 24);
    CHECK(o.reload.kind == BOAT_ROOM_RELOAD_RESTARTED && !o.objective.quarantined);
    CHECK(value(&o, "c1") == 0 && value(&o, "c3") == 0);
    return 0;
}

// A live reload cannot change the room definition's marks: that is a definition revision.
static int reload_marks_changed(void)
{
    static BoatRoomObjective o;
    BoatNetRules rules;
    CHECK(scored(&o, &rules));
    CHECK(edit("local MARKS = 5", "local MARKS = 5"));
    edited.marks.items[0].x += 1;
    BoatPluginDiagnostic d = { 0 };
    CHECK(!boat_room_objective_stage(&o, &edited, &d));
    CHECK(strstr(d.message, "marks changed") && strstr(d.message, "definition revision"));
    edited.marks = hunt.marks;
    edited.present = false;
    CHECK(!boat_room_objective_stage(&o, &edited, &d));
    CHECK(!o.has_pending);
    rules.after_tick(rules.context, 24);
    CHECK(o.reloads == 0 && o.revision == 1);
    return 0;
}

// A reload whose rows are identical still moves the clients' objective revision.
static int reload_reaches_clients(void)
{
    static BoatRoomObjective o;
    CHECK(start_room(&o));
    visit(0, &hunt.marks.items[0]);
    step(.2);
    CHECK(server.has_objective && value(&o, "c1") == 1);
    unsigned revision = server.objective.revision;
    for (unsigned i = 0; i < 2; ++i)
        CHECK(clients[i].objective_valid && clients[i].objective.revision == revision);
    CHECK(edit("local MARKS = 5", "local MARKS = 5 -- an edit that changes no row"));
    BoatPluginDiagnostic d;
    CHECK(boat_room_objective_stage(&o, &edited, &d));
    step(.3);
    CHECK(o.revision == 2 && o.reload.kind == BOAT_ROOM_RELOAD_MIGRATED && value(&o, "c1") == 1);
    CHECK(server.objective.revision > revision);
    for (unsigned i = 0; i < 2; ++i)
        CHECK(clients[i].objective.revision == server.objective.revision
            && boat_net_objective_state_equal(&clients[i].objective, &server.objective)
            && client_row(&clients[i], "c1") && client_row(&clients[i], "c2"));
    boat_net_memory_destroy(network);
    return 0;
}

// P5f: a marks change is a definition revision. It waits while the free room is occupied, commits
// when it empties, and the objective restarts on the new source and marks.
static void join(unsigned i, const char *name)
{
    unsigned link;
    BoatNetTransport transport;
    boat_net_memory_client(network, &link, &transport);
    BoatNetClientConfig c
        = { .name = name, .token = "", .build = "validate", .resources = test_worlds };
    boat_net_client_init(&clients[i], transport, &c, now);
}

// The hunt's definition with mark 0 moved, and the matching candidate source.
static void moved(BoatRoomDefinition *room, BoatRoomObjectiveSource *source, float dx)
{
    *room = hunt_room;
    room->marks.items[0].x += dx;
    *source = edited = hunt;
    source->marks = room->marks;
}

static int revision_marks(void)
{
    static BoatRoomObjective o;
    static BoatRoomDefinition room;
    static BoatRoomObjectiveSource candidate;
    CHECK(start_room(&o));
    CHECK(edit("local MARKS = 5", "local MARKS = 5")); // Prepare EDITED's storage only.
    moved(&room, &candidate, 100);
    BoatPluginDiagnostic d = { 0 };
    // Two crews are present: the revision is staged but waits, and the old marks keep scoring.
    CHECK(boat_room_objective_stage_revision(&o, &server, &room, &candidate, &d));
    CHECK(o.has_revision && boat_net_server_revision_pending(&server) && !o.has_pending);
    step(2);
    CHECK(server.stats.revision == 1 && server.stats.revisions_announced == 0);
    CHECK(server.room->marks.items[0].x == hunt_room.marks.items[0].x && o.revision == 1);
    visit(0, &hunt.marks.items[0]);
    CHECK(value(&o, "c1") == 1);
    // A Lua-only edit while it waits goes to the revision's source, not the running one.
    CHECK(edit("\"CREW \"", "\"TEAM \""));
    edited.marks = room.marks;
    CHECK(boat_room_objective_stage(&o, &edited, &d) && !o.has_pending);
    step(.5);
    CHECK(o.revision == 1 && !strcmp(row(&o, "c1")->label, "CREW 1"));
    // One crew leaves: still occupied. The last leaves: the revision commits.
    boat_net_client_leave(&clients[1]);
    step(.5);
    CHECK(boat_net_server_players(&server) == 1 && server.stats.revision == 1);
    boat_net_client_leave(&clients[0]);
    step(.5);
    CHECK(boat_net_server_players(&server) == 0 && server.stats.revision == 2);
    CHECK(server.stats.revisions_committed == 1 && !boat_net_server_revision_pending(&server));
    CHECK(server.room->marks.items[0].x == room.marks.items[0].x);
    CHECK(!o.has_revision && o.revision == 2 && o.source == &o.owned);
    CHECK(o.reload.kind == BOAT_ROOM_RELOAD_RESTARTED && o.objective.generation == 2);
    CHECK(o.owned.marks.items[0].x == room.marks.items[0].x && value(&o, "hunt") == 1);
    CHECK(!o.objective.quarantined);
    // A new crew joins revision 2 and scores on the moved mark, not on the old spot.
    join(0, "three");
    step(1);
    CHECK(clients[0].phase == BOAT_NET_CLIENT_PLAYING && clients[0].accept.revision == 2);
    CHECK(boat_net_client_room(&clients[0])->marks.items[0].x == room.marks.items[0].x);
    visit(0, &hunt.marks.items[0]); // The old position is no longer a mark.
    CHECK(value(&o, "c1") == 0 && !strcmp(row(&o, "c1")->label, "TEAM 1"));
    visit(0, &room.marks.items[0]);
    CHECK(value(&o, "c1") == 1);
    step(.2);
    CHECK(clients[0].objective.revision == server.objective.revision
        && boat_net_objective_state_equal(&clients[0].objective, &server.objective));
    boat_net_memory_destroy(network);
    return 0;
}

// A revision with no player commits at once; a refused one (another mode, a source that cannot
// start) leaves the room, the objective and the server alone.
static int revision_refused(void)
{
    static BoatRoomObjective o;
    static BoatRoomDefinition room;
    static BoatRoomObjectiveSource candidate;
    CHECK(start_room(&o));
    boat_net_client_leave(&clients[0]);
    boat_net_client_leave(&clients[1]);
    step(.5);
    CHECK(boat_net_server_players(&server) == 0);
    BoatPluginDiagnostic d = { 0 };
    // Another mode is refused by the server.
    moved(&room, &candidate, 100);
    room.mode = BOAT_ROOM_MODE_DEATHMATCH;
    CHECK(!boat_room_objective_stage_revision(&o, &server, &room, &candidate, &d));
    CHECK(d.message[0] && !o.has_revision && !boat_net_server_revision_pending(&server));
    // A source that fails to start never reaches the server.
    moved(&room, &candidate, 100);
    CHECK(edit("local MARKS = 5", "local MARKS = = 5"));
    edited.marks = room.marks;
    CHECK(!boat_room_objective_stage_revision(&o, &server, &room, &edited, &d));
    CHECK(d.message[0] && !o.has_revision && !boat_net_server_revision_pending(&server));
    CHECK(server.stats.revision == 1 && server.stats.revisions_staged == 0);
    // A good one commits in the empty room without waiting.
    moved(&room, &candidate, -100);
    CHECK(boat_room_objective_stage_revision(&o, &server, &room, &candidate, &d));
    step(.3);
    CHECK(server.stats.revision == 2 && o.revision == 2 && !o.has_revision);
    CHECK(o.source->marks.items[0].x == room.marks.items[0].x);
    boat_net_memory_destroy(network);
    return 0;
}

// P5h: the waiting line is printed once per pending revision, not at every offer of the file.
static unsigned lines_with(FILE *f, const char *text)
{
    char line[256];
    unsigned n = 0;
    rewind(f);
    while (fgets(line, sizeof(line), f))
        n += strstr(line, text) != NULL;
    return n;
}

static int revision_waits_once(void)
{
    static BoatRoomObjective o;
    static BoatRoomDefinition room;
    static BoatRoomObjectiveSource candidate;
    static BoatRoomReloadWatch watch;
    CHECK(start_room(&o));
    FILE *out = tmpfile();
    CHECK(out);
    boat_room_reload_watch_init(&watch, &hunt_room, &hunt);
    // The same file re-offered changes nothing and prints nothing.
    boat_room_reload_offer(&watch, &o, &server, &hunt_room, &hunt, out);
    CHECK(!lines_with(out, "objective reload"));
    // Edits while the room is occupied: one line for revision 2, however often they arrive.
    const float shifts[] = { 100, 200, 100, 300, 100 };
    for (unsigned i = 0; i < 5; ++i) {
        moved(&room, &candidate, shifts[i]);
        boat_room_reload_offer(&watch, &o, &server, &room, &candidate, out);
        step(.2);
    }
    CHECK(boat_net_server_revision_pending(&server) && server.stats.revision == 1);
    CHECK(lines_with(out, "revision 2 waits for an empty room") == 1);
    CHECK(lines_with(out, "waits for an empty room") == 1);
    // The commit moves on; the next pending revision announces itself once more.
    boat_net_client_leave(&clients[0]);
    boat_net_client_leave(&clients[1]);
    step(.5);
    CHECK(server.stats.revision == 2);
    join(0, "three");
    step(1);
    CHECK(boat_net_server_players(&server) == 1);
    for (unsigned i = 0; i < 3; ++i) {
        moved(&room, &candidate, 400 + 10 * (float)i);
        boat_room_reload_offer(&watch, &o, &server, &room, &candidate, out);
        step(.2);
    }
    CHECK(lines_with(out, "revision 3 waits for an empty room") == 1);
    CHECK(lines_with(out, "rejected") == 0);
    fclose(out);
    boat_net_memory_destroy(network);
    return 0;
}

static const TestCase cases[] = {
    TEST_CASE(package),
    TEST_CASE(adapter),
    TEST_CASE(quarantine),
    TEST_CASE(room),
    TEST_CASE(four_crews),
    TEST_CASE(reload_keeps_counts),
    TEST_CASE(reload_migrates),
    TEST_CASE(reload_restart),
    TEST_CASE(reload_broken),
    TEST_CASE(reload_marks_changed),
    TEST_CASE(reload_reaches_clients),
    TEST_CASE(revision_waits_once),
    TEST_CASE(revision_marks),
    TEST_CASE(revision_refused),
};

int main(int argc, char **argv)
{
    int result = test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
    test_worlds_close();
    return result;
}
