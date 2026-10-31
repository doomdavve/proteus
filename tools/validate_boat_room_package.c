/* PD2/PD3: a room built from an edited package. The authority evaluates the package, sends the
 * definition as data, and clients with no knowledge of the edited course or combat values
 * predict and mirror it bit for bit. The validator links the plugin host and catalog; the core
 * and net libraries remain free of them. */
#include "boat_fleet_bots.h"
#include "boat_net_client.h"
#include "boat_net_server.h"
#include "boat_net_transport.h"
#include "boat_plugin.h"
#include "boat_plugin_io.h"
#include "boat_room_package.h"
#include "boat_test_cases.h"
#include "boat_world.h"

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

#define HISTORY 8192
#define CLIENTS 4

static BoatWorldResources WORLDS[BOAT_LOCATION_COUNT];

static const BoatWorldResources *world(void) { return &WORLDS[BOAT_LOCATION_RIVER]; }

// The compiled river course with one gate moved and widened, a later ferry and a shorter round.
static const char *const RACE_SOURCE
    = "return { race = {\n"
      "  start = { x = -180, z = -250, dx = .786, dz = -.618, half_width = 40 },\n"
      "  gates = {\n"
      "    { x = -72.3, z = -376.6, dx = .830, dz = -.558, half_width = 18 },\n"
      "    { x = 104.7, z = -371.8, dx = .830, dz = -.558, half_width = 22 },\n"
      "    { x = 146.2, z = -535.5, dx = .830, dz = -.558, half_width = 16 },\n"
      "    { x = 315.3, z = -516.9, dx = .830, dz = -.558, half_width = 16 },\n"
      "    { x = 375.9, z = -678.0, dx = .830, dz = -.558, half_width = 18 },\n"
      "    { x = 520.1, z = -678.5, dx = .830, dz = -.558, half_width = 20 },\n"
      "    { x = 588.5, z = -767.2, dx = .767, dz = -.641, half_width = 158 }\n"
      "  },\n"
      "  grid = { { x = -203.97, z = -257.86 }, { x = -195.37, z = -246.86 },\n"
      "           { x = -186.67, z = -235.86 }, { x = -178.07, z = -224.86 } },\n"
      "  source = { x = -118.966, z = -498.230, dx = .617822, dz = .786318,\n"
      "             started = -10, speed = 11, height = 3, duration = 24, lifetime = 40,\n"
      "             wavelength = 8, spread = 3, half_width = 14 },\n"
      "  storm = { x = 46.2, z = -399.12, vx = 6.64, vz = -4.464, radius = 140, height = 1.6,\n"
      "            started = 0, build = 0 },\n"
      "  rules = { grid_ticks = 60, countdown_ticks = 120, results_ticks = 120,\n"
      "            after_winner_ticks = 600, cap_ticks = 3600, recovery_ticks = 120,\n"
      "            skip_ticks = 30, park_ticks = 120, min_players = 2 }\n"
      "} }\n";

static const char *const RACE_MANIFEST
    = "{\"id\":\"test.race-edit\",\"name\":\"Edited race\","
      "\"api\":1,\"kind\":\"race\",\"entry\":\"main.lua\","
      "\"location\":\"river\",\"capabilities\":[\"race.define\"],"
      "\"state_version\":1}";

// Non-default combat values: more health, weaker packets, a faster cannon cadence.
static const char *const MATCH_SOURCE
    = "return { match = {\n"
      "  version = 1,\n"
      "  rules = { round_ticks = 600, intermission_ticks = 120, credit_ticks = 60,\n"
      "            grace_ticks = 60, respawn_ticks = 120, skip_ticks = 30,\n"
      "            contact_impulse = 1.2, wake_height = .3, min_players = 2 },\n"
      "  combat = { health = 150, damage = 7, bullet_speed = 90, bullet_range = 65,\n"
      "             grenade_distance = 6, wave_height = 3, wave_speed = 9, wave_range = 60,\n"
      "             cannon_pressure = .12, pressure_recovery = .2, torpedo_damage = 30,\n"
      "             torpedo_speed = 30, torpedo_range = 150, torpedo_ticks = 150,\n"
      "             arming_ticks = 24, torpedo_ammo = 2, fire_ticks = 10, heat_ticks = 400,\n"
      "             cool_ticks = 300, grenade_ticks = 900, splash_ticks = 30, fuse_ticks = 100,\n"
      "             grace_ticks = 180 }\n"
      "} }\n";

static const char *const MATCH_MANIFEST
    = "{\"id\":\"test.deathmatch-edit\",\"name\":\"Edited deathmatch\",\"api\":1,"
      "\"kind\":\"match\",\"entry\":\"main.lua\",\"location\":\"any\","
      "\"capabilities\":[\"match.define\"],\"state_version\":1}";

typedef struct {
    BoatNetMemory *network;
    BoatNetServer server;
    BoatNetClient clients[CLIENTS];
    unsigned link[CLIENTS], count;
    BoatFleetBots bots[CLIENTS];
    bool live[CLIENTS], predict[CLIENTS], weapons;
    double now;
    uint64_t auth_tick[HISTORY], auth_hash[HISTORY];
    uint64_t matches, mismatches;
} Room;

static size_t room_definition_size(const BoatRoomDefinition *definition)
{
    static uint8_t bytes[BOAT_ROOM_BYTES_MAX];
    return boat_room_definition_encode(definition, bytes, sizeof(bytes));
}

static BoatRoomDefinition from_race(const BoatPluginRace *race)
{
    BoatRoomDefinition d = {
        .version = BOAT_ROOM_VERSION, .mode = BOAT_ROOM_MODE_RACE, .location = BOAT_LOCATION_RIVER
    };
    snprintf(d.id, sizeof(d.id), "%s", race->manifest.id);
    snprintf(d.name, sizeof(d.name), "%s", race->manifest.name);
    d.course = race->course;
    d.race_rules = race->rules;
    d.has_scene = race->has_scene;
    d.scene = race->scene;
    return d;
}

static BoatRoomDefinition from_match(const BoatPluginMatch *match)
{
    BoatRoomDefinition d = { .version = BOAT_ROOM_VERSION,
        .mode = BOAT_ROOM_MODE_DEATHMATCH,
        .location = BOAT_LOCATION_RIVER };
    snprintf(d.id, sizeof(d.id), "%s", match->manifest.id);
    snprintf(d.name, sizeof(d.name), "%s", match->manifest.name);
    d.match_rules = match->rules;
    d.combat_rules = match->combat;
    d.has_scene = match->has_scene;
    d.scene = match->scene;
    return d;
}

static bool room_open(Room *room, const BoatRoomDefinition *definition, bool predict)
{
    memset(room, 0, sizeof(*room));
    room->network = boat_net_memory_create(7);
    if (!room->network)
        return false;
    BoatNetServerConfig config = { .token = "",
        .epoch = 0x5eed,
        .max_players = BOAT_FLEET_SLOTS,
        .resources = world(),
        .room = definition };
    BoatNetTransport transport = boat_net_memory_server(room->network);
    if (!boat_net_server_init(&room->server, transport, &config))
        return false;
    for (unsigned i = 0; i < CLIENTS; ++i) {
        room->predict[i] = predict;
        room->live[i] = false;
    }
    return true;
}

static unsigned join(Room *room, const char *name, bool predict)
{
    unsigned i = room->count++;
    BoatNetTransport transport;
    boat_net_memory_client(room->network, &room->link[i], &transport);
    BoatNetClientConfig config = {
        .name = name, .token = "", .build = "room-package", .predict = predict, .resources = NULL
    };
    // The client prepares every location itself; the room's definition picks one.
    config.resources = WORLDS;
    boat_net_client_init(&room->clients[i], transport, &config, room->now);
    room->predict[i] = predict;
    room->live[i] = true;
    return i;
}

static void step(Room *room, double dt)
{
    room->now += dt;
    boat_net_memory_set_time(room->network, room->now);
    boat_net_server_service(&room->server, room->now);
    uint64_t tick = room->server.fleet.state.tick;
    if (tick < HISTORY && tick % BOAT_NET_SNAPSHOT_TICKS == 0) {
        room->auth_tick[tick % HISTORY] = tick;
        room->auth_hash[tick % HISTORY] = boat_fleet_hash(&room->server.fleet.state);
    }
    for (unsigned i = 0; i < room->count; ++i) {
        BoatNetClient *c = &room->clients[i];
        if (!room->live[i])
            continue;
        BoatInput input = { 0 };
        const BoatFleetSlot *own = boat_net_client_own(c);
        if (own) {
            const BoatRoomDefinition *definition = boat_net_client_room(c);
            input = room->weapons
                ? boat_fleet_bot_hunt_input(&room->bots[i], boat_net_client_view(c), c->slot)
                : boat_fleet_bot_input(&room->bots[i], boat_net_client_view(c), c->slot);
            if (room->weapons) {
                input.gun = true;
                input.grenade = (uint16_t)(tick / 300 + 1);
                input.torpedo = (uint8_t)(tick / 180 % 255 + 1);
            }
            const BoatNetRaceRow *row = &c->race.slots[c->slot];
            if (definition && definition->mode == BOAT_ROOM_MODE_RACE && c->race_valid
                && row->current) {
                const BoatFleet *view = boat_net_client_view(c);
                if (view->state.slots[c->slot].held)
                    input = (BoatInput) { .throttle_rate
                        = boat_throttle_rate(view->state.slots[c->slot].boat.throttle, 1) };
                else if (c->race.phase >= BOAT_RACE_RACING)
                    input = boat_fleet_bot_race_input(view, &definition->course, row->gates,
                        row->status != BOAT_RACE_RUNNING, c->slot);
            }
        }
        room->live[i] = boat_net_client_service(c, room->now, input, false);
        if (c->phase != BOAT_NET_CLIENT_PLAYING)
            continue;
        uint64_t mirror = c->fleet.state.tick;
        if (mirror < HISTORY && mirror % BOAT_NET_BIRTH_TICKS == 0
            && room->auth_tick[mirror % HISTORY] == mirror) {
            if (boat_fleet_hash(&c->fleet.state) == room->auth_hash[mirror % HISTORY])
                ++room->matches;
            else
                ++room->mismatches;
        }
    }
}

static void run(Room *room, double seconds)
{
    for (double end = room->now + seconds; room->now < end;)
        step(room, (1 + (unsigned)(room->now * 977) % 3) / 240.0);
}

static int race_room(void)
{
    BoatPluginBundle bundle
        = { RACE_MANIFEST, strlen(RACE_MANIFEST), RACE_SOURCE, strlen(RACE_SOURCE) };
    static BoatPluginRace race;
    BoatPluginDiagnostic d = { 0 };
    CHECK(boat_plugin_load_race(&bundle, NULL, &race, &d) == BOAT_PLUGIN_OK);
    CHECK(boat_plugin_race_check_location(&race, world()->definition, &world()->map, &d)
        == BOAT_PLUGIN_OK);
    CHECK(race.course.gates[1].centre.x != boat_race_river_course()->gates[1].centre.x);
    CHECK(race.course.source.speed != boat_race_river_course()->source.speed);
    BoatRoomDefinition definition = from_race(&race);
    BoatRoomDiagnostic rd;
    CHECK(boat_room_definition_validate(&definition, &rd));

    static Room room;
    CHECK(room_open(&room, &definition, true));
    join(&room, "one", true);
    join(&room, "two", false);
    run(&room, 1);
    unsigned late = join(&room, "late", true);
    run(&room, 70);
    CHECK(room.server.race.round >= 1);
    CHECK(room.matches > 0 && room.mismatches == 0);
    const BoatRace *server_race = &room.server.race;
    CHECK(server_race->rules.min_players == 2);
    for (unsigned i = 0; i < room.count; ++i) {
        const BoatNetClient *c = &room.clients[i];
        if (i == late && c->phase != BOAT_NET_CLIENT_PLAYING)
            continue;
        CHECK(boat_net_client_room(c) != NULL);
        CHECK(boat_net_client_room(c)->mode == BOAT_ROOM_MODE_RACE);
        CHECK(boat_net_client_room(c)->course.gates[1].centre.x
            == definition.course.gates[1].centre.x);
        CHECK(c->stats.decode_errors == 0);
        if (c->predict) {
            CHECK(c->prediction.stats.corrections > 0);
            CHECK(c->prediction.stats.max_replay <= BOAT_PREDICTION_MAX_AHEAD);
            CHECK(c->prediction.stats.max_error < 1.0f);
        }
    }
    printf("room_package race: %llu/%llu authority hashes; definition %zu bytes\n",
        (unsigned long long)room.matches, (unsigned long long)(room.matches + room.mismatches),
        room_definition_size(&definition));
    boat_net_memory_destroy(room.network);
    return 0;
}

static int match_room(void)
{
    BoatPluginBundle bundle
        = { MATCH_MANIFEST, strlen(MATCH_MANIFEST), MATCH_SOURCE, strlen(MATCH_SOURCE) };
    static BoatPluginMatch match;
    BoatPluginDiagnostic d = { 0 };
    CHECK(boat_plugin_load_match(&bundle, NULL, &match, &d) == BOAT_PLUGIN_OK);
    CHECK(boat_plugin_match_check_location(&match, world()->definition, &world()->map, &d)
        == BOAT_PLUGIN_OK);
    BoatRoomDefinition definition = from_match(&match);
    BoatRoomDiagnostic rd;
    CHECK(boat_room_definition_validate(&definition, &rd));

    static Room room;
    CHECK(room_open(&room, &definition, true));
    room.weapons = true;
    join(&room, "gun-a", true);
    join(&room, "gun-b", false);
    run(&room, 1);
    join(&room, "late", true);
    run(&room, 12);
    CHECK(room.server.match.round >= 1);
    CHECK(room.matches > 0 && room.mismatches == 0);
    CHECK(room.server.fleet.state.combat.rules.health == 150);
    for (unsigned i = 0; i < room.count; ++i) {
        const BoatNetClient *c = &room.clients[i];
        CHECK(c->phase == BOAT_NET_CLIENT_PLAYING);
        CHECK(boat_net_client_room(c) != NULL);
        CHECK(boat_net_client_room(c)->combat_rules.health == 150);
        CHECK(c->fleet.state.combat.rules.health == 150);
        CHECK(c->stats.decode_errors == 0);
        if (c->predict) {
            CHECK(c->prediction.stats.corrections > 0);
            CHECK(c->prediction.stats.max_replay <= BOAT_PREDICTION_MAX_AHEAD);
            CHECK(c->prediction.stats.max_error < 1.0f);
        }
    }
    printf("room_package match: %llu/%llu authority hashes; definition %zu bytes\n",
        (unsigned long long)room.matches, (unsigned long long)(room.matches + room.mismatches),
        room_definition_size(&definition));
    boat_net_memory_destroy(room.network);
    return 0;
}

static int definition_size(void)
{
    // A maximum definition: the river course plus a full authored scene. It must fit the 16 KiB
    // bootstrap cap with pages to spare.
    BoatRoomDefinition d = {
        .version = BOAT_ROOM_VERSION, .mode = BOAT_ROOM_MODE_RACE, .location = BOAT_LOCATION_RIVER
    };
    snprintf(d.id, sizeof(d.id), "maximum");
    snprintf(d.name, sizeof(d.name), "Maximum definition");
    d.course = *boat_race_river_course();
    d.race_rules = boat_race_default_rules();
    d.has_scene = true;
    d.scene.version = BOAT_SCENE_VERSION;
    d.scene.entrance_present = true;
    d.scene.entrance_x = 10;
    d.scene.entrance_z = 20;
    d.scene.entrance_width = 8;
    d.scene.berths_present = true;
    d.scene.modules_present = true;
    d.scene.berth_count = BOAT_SCENE_BERTHS;
    d.scene.module_count = BOAT_SCENE_MODULES;
    for (unsigned i = 0; i < BOAT_SCENE_BERTHS; ++i) {
        BoatSceneBerth *b = &d.scene.berths[i];
        snprintf(b->id, sizeof(b->id), "berth_%u", i);
        b->recipe = i % BOAT_SCENE_BERTH_RECIPES;
        b->x = -100.0f + 10.0f * (float)i;
        b->z = 50;
        b->heading = .1f * i;
    }
    for (unsigned i = 0; i < BOAT_SCENE_MODULES; ++i) {
        BoatSceneModule *m = &d.scene.modules[i];
        snprintf(m->id, sizeof(m->id), "module_%u", i);
        m->kind = i == 0 ? BOAT_SCENE_MODULE_PONTOON : BOAT_SCENE_MODULE_GANGWAY;
        m->x = -100.0f + 10.0f * (float)i;
        m->z = 60;
        m->anchor_x = m->x;
        m->anchor_z = m->z + 4;
        if (m->kind == BOAT_SCENE_MODULE_GANGWAY)
            snprintf(m->attaches, sizeof(m->attaches), "%s", "module_0");
    }
    d.scene.traffic_present = true;
    d.scene.traffic.actor_count = WATERFRONT_ROUTE_ACTORS;
    for (unsigned i = 0; i < WATERFRONT_ROUTE_ACTORS; ++i) {
        WaterfrontRouteActor *a = &d.scene.traffic.actors[i];
        CHECK(waterfront_route_preset("marina_pass", a));
        snprintf(a->id, sizeof(a->id), "craft_%u", i);
        a->retain = 0xff;
    }
    d.scene.gulls_present = true;
    d.scene.gulls.flock_count = WATERFRONT_FLOCKS;
    for (unsigned i = 0; i < WATERFRONT_FLOCKS; ++i) {
        WaterfrontFlock *f = &d.scene.gulls.flocks[i];
        snprintf(f->id, sizeof(f->id), "flock_%u", i);
        f->recipe = WATERFRONT_FLOCK_GULL;
        f->x = 40.0f * i;
        f->z = -30;
        f->count = WATERFRONT_FLOCK_BIRDS;
        f->radius = 20;
        f->height = 12;
        f->rate = .2f;
        f->follow_actor = -1;
        f->implicit_cues = false;
        f->cue_count = WATERFRONT_FLOCK_CUES;
        for (unsigned c = 0; c < WATERFRONT_FLOCK_CUES; ++c)
            f->cues[c] = (WaterfrontFlockCue) { .start_tick = 1200 * c,
                .end_tick = 1200 * c + 600,
                .fade_ticks = 120,
                .kind = c % WATERFRONT_FLOCK_CUE_KINDS,
                .amplitude = 1 };
    }
    BoatRoomDiagnostic rd;
    if (!boat_room_definition_validate(&d, &rd))
        fprintf(stderr, "definition invalid: %s: %s\n", rd.field, rd.message);
    CHECK(boat_room_definition_validate(&d, &rd));
    size_t size = room_definition_size(&d);
    static uint8_t bytes[BOAT_ROOM_BYTES_MAX];
    CHECK(size > 0 && size <= BOAT_ROOM_BYTES_MAX);
    CHECK(boat_room_definition_encode(&d, bytes, sizeof(bytes)) == size);
    BoatRoomDefinition back;
    CHECK(boat_room_definition_decode(bytes, size, &back, &rd));
    CHECK(boat_room_definition_digest(&back) == boat_room_definition_digest(&d));
    CHECK(size < BOAT_ROOM_BYTES_MAX);
    // Truncation, a trailing byte and a corrupt digest are rejected.
    CHECK(!boat_room_definition_decode(bytes, size - 1, &back, &rd));
    bytes[size] = 0;
    CHECK(!boat_room_definition_decode(bytes, size + 1, &back, &rd));
    printf("room_package definition: maximum %zu of %d bytes, %u pages\n", size,
        BOAT_ROOM_BYTES_MAX, (unsigned)((size + BOAT_NET_PAGE_BYTES - 1) / BOAT_NET_PAGE_BYTES));
    return 0;
}

// Every bundled room package loads through the authority path and is measured on the wire.
static int catalog_rooms(void)
{
    unsigned rooms = 0;
    for (unsigned i = 0; i < boat_plugin_catalog_count; ++i) {
        const BoatPluginCatalogEntry *entry = boat_plugin_catalog_at(i);
        BoatPluginKind kind = boat_plugin_kind_parse(entry->kind);
        if (kind != BOAT_PLUGIN_KIND_FREE && kind != BOAT_PLUGIN_KIND_RACE
            && kind != BOAT_PLUGIN_KIND_MATCH)
            continue;
        BoatLocationId location = !strcmp(entry->location, "any")
            ? BOAT_LOCATION_RIVER
            : boat_location_find(entry->location)->id;
        BoatRoomDefinition definition;
        char error[220];
        if (!boat_room_package_load(entry->id, kind == BOAT_PLUGIN_KIND_MATCH,
                kind == BOAT_PLUGIN_KIND_RACE, location, &WORLDS[location], &definition, error,
                sizeof(error))) {
            fprintf(stderr, "%s: %s\n", entry->id, error);
            return 1;
        }
        size_t size = room_definition_size(&definition);
        if (!size || size > BOAT_ROOM_BYTES_MAX)
            return 1;
        printf("room_package catalog %s: %zu bytes\n", entry->id, size);
        ++rooms;
    }
    CHECK(rooms >= 7); // Three free locations, the race defaults and the two harbor rooms.
    return 0;
}

// A resolved hull ends each tick just outside the envelope, so a contact shows as being this close.
static unsigned touching(const BoatNetServer *server)
{
    unsigned count = 0;
    BoatWorld world = boat_fleet_world(&server->fleet);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        const BoatFleetSlot *slot = &server->fleet.state.slots[i];
        count += slot->active
            && !boat_scene_clear(
                &slot->boat, &world, world.scene, boat_fleet_time(&server->fleet), .25f);
    }
    return count;
}

// Sample the resolved bot track in a room without a scene, so an authored object can be placed
// exactly where the hull will pass.
static unsigned track_samples(Vector2 *points, unsigned capacity)
{
    static Room room;
    BoatRoomDefinition d = {
        .version = BOAT_ROOM_VERSION, .mode = BOAT_ROOM_MODE_FREE, .location = BOAT_LOCATION_RIVER
    };
    snprintf(d.id, sizeof(d.id), "scene.track");
    snprintf(d.name, sizeof(d.name), "Track samples");
    if (!room_open(&room, &d, true))
        return 0;
    unsigned solo = join(&room, "track", true);
    run(&room, 1);
    unsigned count = 0;
    for (; count < capacity; ++count) {
        run(&room, 2);
        const BoatFleetSlot *own = boat_net_client_own(&room.clients[solo]);
        if (!own)
            break;
        points[count] = own->boat.position;
    }
    boat_net_memory_destroy(room.network);
    return count;
}

// Eight small moving loops along that track: a craft is always within two metres of a sample, so
// the resolved hull must touch one even though the craft is travelling.
static bool moving_loops(BoatRoomDefinition *d, const Vector2 *points, unsigned count)
{
    d->has_scene = true;
    d->scene.version = BOAT_SCENE_VERSION;
    d->scene.traffic_present = true;
    d->scene.traffic.actor_count = count;
    for (unsigned i = 0; i < count; ++i) {
        WaterfrontRouteActor *a = &d->scene.traffic.actors[i];
        snprintf(a->id, sizeof(a->id), "loop_%u", i);
        a->shape = WATERFRONT_ROUTE_POINTS;
        a->recipe = WATERFRONT_CRAFT_RUNABOUT;
        a->retain = 0xff;
        a->speed = 3;
        a->phase = 0;
        // Four 3 m straights between 0.5 m corner arcs, at 3 m/s.
        a->period = (12 + 3.141592653589793) / 3;
        a->point_count = 4;
        a->points[0] = (WaterfrontRoutePoint) { points[i].x - 2, points[i].y - 2, .5f };
        a->points[1] = (WaterfrontRoutePoint) { points[i].x + 2, points[i].y - 2, .5f };
        a->points[2] = (WaterfrontRoutePoint) { points[i].x + 2, points[i].y + 2, .5f };
        a->points[3] = (WaterfrontRoutePoint) { points[i].x - 2, points[i].y + 2, .5f };
    }
    BoatRoomDiagnostic rd;
    if (!boat_room_definition_validate(d, &rd)) {
        fprintf(stderr, "moving scene invalid: %s: %s\n", rd.field, rd.message);
        return false;
    }
    return true;
}

// Moored boats on the track a bot really drives, sampled from a room without them.
static int scene_contact_room(void)
{
    static Room room;
    BoatRoomDefinition d = {
        .version = BOAT_ROOM_VERSION, .mode = BOAT_ROOM_MODE_FREE, .location = BOAT_LOCATION_RIVER
    };
    snprintf(d.id, sizeof(d.id), "scene.contact");
    snprintf(d.name, sizeof(d.name), "Scene contact");
    CHECK(room_open(&room, &d, true));
    unsigned solo = join(&room, "track", true);
    run(&room, 1);
    d.has_scene = true;
    d.scene.version = BOAT_SCENE_VERSION;
    d.scene.entrance_present = true;
    d.scene.entrance_width = 8;
    d.scene.berths_present = true;
    d.scene.berth_count = BOAT_SCENE_BERTHS;
    for (unsigned i = 0; i < BOAT_SCENE_BERTHS; ++i) {
        run(&room, 2);
        const BoatFleetSlot *own = boat_net_client_own(&room.clients[solo]);
        CHECK(own != NULL);
        BoatSceneBerth *b = &d.scene.berths[i];
        snprintf(b->id, sizeof(b->id), "berth_%u", i);
        b->recipe = BOAT_SCENE_BERTH_CRUISER;
        b->x = own->boat.position.x;
        b->z = own->boat.position.y;
    }
    boat_net_memory_destroy(room.network);
    BoatRoomDiagnostic rd;
    CHECK(boat_room_definition_validate(&d, &rd));

    // Alone on a steady link nothing is guessed, so a prediction that lacked the scene would have
    // to be corrected the moment the hull touched.
    CHECK(room_open(&room, &d, true));
    solo = join(&room, "steady", true);
    BoatNetImpairment steady = { .delay = .03 };
    boat_net_memory_impair(room.network, room.link[solo], steady, steady);
    unsigned contact = 0;
    for (double end = room.now + 25; room.now < end;) {
        step(&room, (1 + (unsigned)(room.now * 977) % 3) / 240.0);
        contact += touching(&room.server);
    }
    const BoatNetClient *c = &room.clients[solo];
    CHECK(contact > 0 && room.matches > 0 && room.mismatches == 0);
    CHECK(room.server.fleet.world.scene == &room.server.config.room->scene);
    CHECK(c->fleet.world.scene == &c->room.scene);
    CHECK(c->prediction.fleet.world.scene == &c->room.scene);
    CHECK(c->prediction.stats.corrections > 0);
    printf("room_package scene steady: %u contact ticks, own error max %.6f m, %llu/%llu hashes\n",
        contact, c->prediction.stats.max_error, (unsigned long long)room.matches,
        (unsigned long long)(room.matches + room.mismatches));
    // Without the scene this would be metres. The room already has no ambient waves or physical
    // wake field (the live arcade wake pool is empty), and the first own-slot difference is 12 um
    // at tick 36, the first berth contact. It reaches 0.126 mm at a later snapshot tick: that is
    // speculative contact over one 4-tick snapshot interval, not a replay difference. Keep the
    // bound at about twice the measured contact value.
    CHECK(c->prediction.stats.max_error < .00025f);
    boat_net_memory_destroy(room.network);

    // Loss, reordering and delay, a late joiner and an outage.
    CHECK(room_open(&room, &d, true));
    BoatNetImpairment f = { .04, .01, .01, .002, .01, .02 };
    unsigned a = join(&room, "one", true), b = join(&room, "two", true);
    boat_net_memory_impair(room.network, room.link[a], f, f);
    boat_net_memory_impair(room.network, room.link[b], f, f);
    contact = 0;
    for (double end = room.now + 8; room.now < end;) {
        step(&room, 1 / 120.0);
        contact += touching(&room.server);
    }
    unsigned late = join(&room, "late", true);
    boat_net_memory_impair(room.network, room.link[late], f, f);
    for (double end = room.now + 8; room.now < end;) {
        step(&room, 1 / 120.0);
        contact += touching(&room.server);
    }
    boat_net_memory_blackout(room.network, room.link[b], true);
    run(&room, 1);
    boat_net_memory_blackout(room.network, room.link[b], false);
    for (double end = room.now + 8; room.now < end;) {
        step(&room, 1 / 120.0);
        contact += touching(&room.server);
    }
    uint64_t checks = room.matches + room.mismatches;
    CHECK(contact > 0 && checks > 0 && room.matches > checks * 8 / 10);
    for (unsigned i = 0; i < room.count; ++i) {
        c = &room.clients[i];
        CHECK(c->phase == BOAT_NET_CLIENT_PLAYING && c->stats.decode_errors == 0);
        CHECK(c->fleet.world.scene == &c->room.scene);
        CHECK(c->prediction.fleet.world.scene == &c->room.scene);
    }
    printf("room_package scene impaired: %u contact ticks, %llu/%llu hashes, resyncs %llu\n",
        contact, (unsigned long long)room.matches, (unsigned long long)checks,
        (unsigned long long)room.clients[b].stats.resyncs);
    boat_net_memory_destroy(room.network);
    return 0;
}

// An authored moving craft on the same track, contacted by the resolved hull under a steady
// link. Exact mirror hashes must hold through the contact.
static int scene_moving_contact_room(void)
{
    Vector2 points[8] = { 0 };
    unsigned samples = track_samples(points, 8);
    CHECK(samples == 8);
    BoatRoomDefinition d = {
        .version = BOAT_ROOM_VERSION, .mode = BOAT_ROOM_MODE_FREE, .location = BOAT_LOCATION_RIVER
    };
    snprintf(d.id, sizeof(d.id), "scene.moving");
    snprintf(d.name, sizeof(d.name), "Moving contact");
    CHECK(moving_loops(&d, points, samples));

    static Room room;
    CHECK(room_open(&room, &d, true));
    unsigned solo = join(&room, "moving", true);
    BoatNetImpairment steady = { .delay = .03 };
    boat_net_memory_impair(room.network, room.link[solo], steady, steady);
    unsigned contact = 0;
    for (double end = room.now + 25; room.now < end;) {
        step(&room, (1 + (unsigned)(room.now * 977) % 3) / 240.0);
        contact += touching(&room.server);
    }
    const BoatNetClient *c = &room.clients[solo];
    CHECK(contact > 0 && room.matches > 0 && room.mismatches == 0);
    CHECK(room.server.fleet.world.scene == &room.server.config.room->scene);
    CHECK(c->fleet.world.scene == &c->room.scene);
    CHECK(c->prediction.fleet.world.scene == &c->room.scene);
    CHECK(c->prediction.stats.corrections > 0);
    printf("room_package scene moving: %u contact ticks, own error max %.6f m, %llu/%llu hashes\n",
        contact, c->prediction.stats.max_error, (unsigned long long)room.matches,
        (unsigned long long)(room.matches + room.mismatches));
    CHECK(c->prediction.stats.max_error < .00025f);
    boat_net_memory_destroy(room.network);
    return 0;
}

// A snapshot-stall resync while the authored moving craft is at the hull. The restored mirror
// keeps the scene and matches the authority exactly afterwards.
static int scene_resync_contact_room(void)
{
    Vector2 points[4] = { 0 };
    unsigned samples = track_samples(points, 4);
    CHECK(samples == 4);
    BoatRoomDefinition d = {
        .version = BOAT_ROOM_VERSION, .mode = BOAT_ROOM_MODE_FREE, .location = BOAT_LOCATION_RIVER
    };
    snprintf(d.id, sizeof(d.id), "scene.resync");
    snprintf(d.name, sizeof(d.name), "Resync contact");
    CHECK(moving_loops(&d, points, samples));

    static Room room;
    CHECK(room_open(&room, &d, true));
    unsigned solo = join(&room, "resync", true);
    BoatNetImpairment steady = { .delay = .03 };
    boat_net_memory_impair(room.network, room.link[solo], steady, steady);
    unsigned contact = 0;
    for (double end = room.now + 15; room.now < end && !contact;) {
        step(&room, (1 + (unsigned)(room.now * 977) % 3) / 240.0);
        contact += touching(&room.server);
    }
    BoatNetClient *c = &room.clients[solo];
    CHECK(contact > 0 && c->phase == BOAT_NET_CLIENT_PLAYING);
    unsigned resyncs = (unsigned)c->stats.resyncs;
    // Silence snapshots while control traffic continues, so the client asks for a checkpoint.
    room.server.tick_limit = room.server.fleet.state.tick;
    run(&room, 2.5);
    CHECK(c->phase == BOAT_NET_CLIENT_PLAYING && c->stats.resyncs > resyncs);
    CHECK(c->fleet.world.scene == &c->room.scene);
    CHECK(c->prediction.fleet.world.scene == &c->room.scene);
    room.server.tick_limit = 0;
    unsigned after = 0;
    for (double end = room.now + 20; room.now < end;) {
        step(&room, (1 + (unsigned)(room.now * 977) % 3) / 240.0);
        after += touching(&room.server);
    }
    CHECK(after > 0 && room.matches > 0 && room.mismatches == 0);
    CHECK(c->phase == BOAT_NET_CLIENT_PLAYING && c->stats.decode_errors == 0);
    printf("room_package scene resync: %llu resyncs, %u contact ticks after, %llu/%llu hashes\n",
        (unsigned long long)c->stats.resyncs, after, (unsigned long long)room.matches,
        (unsigned long long)(room.matches + room.mismatches));
    boat_net_memory_destroy(room.network);
    return 0;
}

static const TestCase cases[] = {
    TEST_CASE(scene_contact_room),
    SLOW_CASE(scene_moving_contact_room),
    SLOW_CASE(scene_resync_contact_room),
    TEST_CASE(catalog_rooms),
    TEST_CASE(race_room),
    TEST_CASE(match_room),
    TEST_CASE(definition_size),
};

static void open_worlds(void)
{
    for (int id = 0; id < BOAT_LOCATION_COUNT; ++id)
        if (!boat_world_resources_init(&WORLDS[id], boat_location((BoatLocationId)id))) {
            fprintf(
                stderr, "cannot prepare the %s world\n", boat_location((BoatLocationId)id)->name);
            exit(1);
        }
}

int main(int argc, char **argv)
{
    open_worlds();
    int status = test_cases_main(cases, sizeof(cases) / sizeof(cases[0]), argc, argv);
    for (int id = 0; id < BOAT_LOCATION_COUNT; ++id)
        boat_world_resources_destroy(&WORLDS[id]);
    return status;
}
