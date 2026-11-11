#include "net/boat_local.h"

#include <math.h>
#include <string.h>

static void fail(BoatLocal *local, const char *text)
{
    if (!text || !*text)
        text = "the local session closed";
    size_t length = strlen(text);
    if (length >= sizeof(local->failure))
        length = sizeof(local->failure) - 1;
    memcpy(local->failure, text, length);
    local->failure[length] = 0;
}

void boat_local_stop(BoatLocal *local)
{
    if (local->network)
        boat_net_memory_destroy(local->network);
    local->network = NULL;
    local->running = false;
}

static bool started(const BoatLocal *local)
{
    const BoatNetClient *c = &local->client;
    return c->phase == BOAT_NET_CLIENT_PLAYING && c->ready_sent && boat_net_client_own(c)
        && local->server.peers[0].phase == BOAT_NET_PEER_READY;
}

bool boat_local_start(
    BoatLocal *local, const BoatLocalConfig *config, uint32_t generation, double now)
{
    boat_local_stop(local);
    BoatLocalStats stats = local->stats;
    memset(local, 0, sizeof(*local));
    local->stats = stats;
    local->config = *config;
    local->generation = generation;
    if (!generation || !config->resources || config->location >= BOAT_LOCATION_COUNT) {
        fail(local, "invalid local session configuration");
        return false;
    }
    const BoatWorldResources *resources = &config->resources[config->location];
    BoatRoomDefinition *room = &local->room;
    *room = (BoatRoomDefinition) { .version = BOAT_ROOM_VERSION,
        .mode = BOAT_ROOM_MODE_FREE,
        .location = (unsigned char)config->location };
    memcpy(room->id, "local", 6);
    memcpy(room->name, "Single player", 14);
    if (config->scene) {
        room->has_scene = true;
        room->scene = *config->scene;
    }
    local->network = boat_net_memory_create(generation);
    if (!local->network) {
        fail(local, "cannot create the local transport");
        return false;
    }
    boat_net_memory_set_time(local->network, now);
    BoatNetPrivateSession world = config->world;
    world.enabled = true;
    BoatNetServerConfig server = { .token = "",
        .waves = world.water.amplitude != 0,
        .epoch = generation,
        .max_players = 1,
        .room = room,
        .resources = resources,
        .rules = config->rules,
        .local = world };
    BoatNetTransport client_transport;
    unsigned peer;
    if (!boat_net_server_init(&local->server, boat_net_memory_server(local->network), &server)
        || !boat_net_memory_client(local->network, &peer, &client_transport)) {
        fail(local, "the local authority rejected its configuration");
        boat_local_stop(local);
        return false;
    }
    world.spawn = (Boat) { 0 };
    BoatNetClientConfig client = { .name = "player",
        .token = "",
        .predict = true,
        .resources = config->resources,
        .local = world };
    boat_net_client_init(&local->client, client_transport, &client, now);
    local->running = true;
    unsigned round = 0;
    while (round < BOAT_LOCAL_STARTUP_ROUNDS && !started(local)) {
        ++round;
        if (!boat_net_server_grant(&local->server, now, 0)
            || !boat_net_client_local_service(&local->client, now))
            break;
    }
    local->stats.startup_rounds = round;
    if (!started(local)) {
        fail(local,
            local->client.phase >= BOAT_NET_CLIENT_REJECTED
                ? boat_net_client_failure(&local->client)
                : "the local session did not start");
        boat_local_stop(local);
        return false;
    }
    return true;
}

bool boat_local_restart(BoatLocal *local, const BoatLocalConfig *config, double now)
{
    uint32_t generation = local->generation + 1 ? local->generation + 1 : 1;
    ++local->stats.restarts;
    return boat_local_start(local, config, generation, now);
}

unsigned boat_local_chunk(uint64_t tick, unsigned remaining)
{
    unsigned boundary = BOAT_LOCAL_CHUNK_TICKS - (unsigned)(tick % BOAT_LOCAL_CHUNK_TICKS);
    return remaining < boundary ? remaining : boundary;
}

// The client handles what the authority sent; the weather reader sees a new private state.
static bool receive(BoatLocal *local, double now)
{
    BoatNetClient *client = &local->client;
    if (!boat_net_client_local_service(client, now)) {
        fail(local, boat_net_client_failure(client));
        local->running = false;
        return false;
    }
    uint64_t states = client->stats.private_states;
    if (local->config.weather && states != local->weather_states) {
        local->weather_states = states;
        BoatStorm storm;
        if (local->config.weather(local->config.weather_context, client->private_state,
                client->private_state_size, &storm))
            boat_net_client_private_storm(client, storm);
    }
    return true;
}

static bool run_frame(BoatLocal *local, double now, unsigned grant, BoatInput input,
    float thrust_target, float steering_target, const BoatLocalScript *script,
    const BoatLocalObserver *observer)
{
    if (!local->running) {
        fail(local, "the local session is not running");
        return false;
    }
    if (grant > BOAT_LOCAL_MAX_GRANT) {
        fail(local, "a frame granted more than 96 ticks");
        return false;
    }
    ++local->stats.frames;
    boat_net_memory_set_time(local->network, now);
    unsigned chunks = 0;
    for (unsigned remaining = grant; remaining;) {
        unsigned count = boat_local_chunk(local->tick, remaining);
        BoatNetClient *client = &local->client;
        for (unsigned i = 1; i <= count; ++i) {
            uint64_t tick = local->tick + i;
            if (observer && observer->tick) {
                // The pool at TICK - 1, advanced as the fleet does before stepping the boats.
                local->water = client->prediction.fleet.state.wakes;
                boat_wake_objects_advance(&local->water);
            }
            bool commanded = script
                ? boat_net_client_command(client, tick,
                      script->command(script->context,
                          &client->prediction.fleet.state.slots[client->slot].boat, tick),
                      NAN, NAN)
                : boat_net_client_command(client, tick, input, thrust_target, steering_target);
            if (!commanded) {
                fail(local, "the local client could not command its boat");
                local->running = false;
                return false;
            }
            if (observer && observer->tick)
                observer->tick(observer->context, &client->prediction.fleet, client->slot, tick,
                    &local->water);
        }
        BoatNetTransport *transport = &client->transport;
        uint64_t dropped = transport->stats(transport->context).dropped;
        // A full queue drops an unreliable batch without refusing it: in-process that is an
        // endpoint failure, never a silent repeat of the previous command.
        if (!boat_net_client_flush(client)
            || transport->stats(transport->context).dropped != dropped) {
            fail(local, "the local transport refused the client's commands");
            local->running = false;
            return false;
        }
        boat_net_server_grant(&local->server, now, count);
        local->tick += count;
        if (!receive(local, now))
            return false;
        remaining -= count;
        ++chunks;
    }
    // L4b: a frame without ticks still services the authority, so its requests apply and an
    // asynchronous evaluation can complete while the host holds the clock.
    if (!grant) {
        boat_net_server_grant(&local->server, now, 0);
        if (!receive(local, now))
            return false;
    }
    local->stats.chunks += chunks;
    local->stats.ticks += grant;
    if (chunks > local->stats.max_chunks)
        local->stats.max_chunks = chunks;
    return true;
}

bool boat_local_frame(BoatLocal *local, double now, unsigned grant, BoatInput input,
    float thrust_target, float steering_target, const BoatLocalObserver *observer)
{
    return run_frame(local, now, grant, input, thrust_target, steering_target, NULL, observer);
}

bool boat_local_frame_scripted(BoatLocal *local, double now, unsigned grant,
    const BoatLocalScript *script, const BoatLocalObserver *observer)
{
    if (!script || !script->command) {
        fail(local, "the local session has no script");
        return false;
    }
    return run_frame(local, now, grant, (BoatInput) { 0 }, NAN, NAN, script, observer);
}

bool boat_local_recover(BoatLocal *local, Boat pose)
{
    if (!local->running || !boat_net_client_private_recover(&local->client, local->tick, pose)) {
        fail(local, "the local client could not recover its boat");
        return false;
    }
    return true;
}

bool boat_local_commit(BoatLocal *local, uint32_t revision, uint8_t change,
    const BoatNetCommitResult *predicted, BoatStorm storm)
{
    if (!local->running
        || !boat_net_client_private_commit(
            &local->client, local->tick, revision, change, predicted)) {
        fail(local, "the local client could not commit a revision");
        return false;
    }
    boat_net_client_private_storm(&local->client, storm);
    return true;
}

const BoatFleet *boat_local_view(const BoatLocal *local) { return &local->client.prediction.fleet; }

unsigned boat_local_slot(const BoatLocal *local) { return local->client.slot; }

const uint8_t *boat_local_state(const BoatLocal *local, size_t *size, uint64_t *tick)
{
    const BoatNetClient *c = &local->client;
    *size = c->private_state_size;
    *tick = c->private_state_tick;
    return c->private_state_size ? c->private_state : NULL;
}

const BoatNetObjectiveState *boat_local_objective(const BoatLocal *local)
{
    return local->client.objective_valid ? &local->client.objective : NULL;
}

bool boat_local_inspect(BoatLocal *local)
{
    if (!local->running || !boat_net_client_private_inspect(&local->client)) {
        fail(local, "the local client could not request an inspection");
        return false;
    }
    return true;
}

const char *boat_local_inspection(const BoatLocal *local, size_t *size, uint64_t *count)
{
    const BoatNetClient *c = &local->client;
    *size = c->inspection_size;
    *count = c->stats.inspections;
    return c->stats.inspections ? c->inspection : NULL;
}
