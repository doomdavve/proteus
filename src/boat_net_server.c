#include "boat_net_server.h"

#include <math.h>
#include <string.h>

static uint8_t text_copy(char *storage, const char *text, size_t limit)
{
    size_t length = 0;
    while (text && text[length] && length < limit) {
        storage[length] = text[length];
        ++length;
    }
    storage[length] = 0;
    return (uint8_t)length;
}

bool boat_net_server_init(
    BoatNetServer *server, BoatNetTransport transport, const BoatNetServerConfig *config)
{
    if (!config || !boat_world_resources_ready(config->resources) || !config->epoch
        || config->max_players < 1 || config->max_players > BOAT_FLEET_SLOTS
        || (config->token && strlen(config->token) > BOAT_NET_TOKEN_BYTES))
        return false;
    BoatMatch match = { 0 };
    if (config->deathmatch && !boat_match_init(&match, &config->match))
        return false;
    BoatRace race = { 0 };
    if (config->race
        && (config->deathmatch || config->resources->definition->id != BOAT_LOCATION_RIVER
            || !boat_race_init(&race, &config->race_rules, boat_race_river_course())))
        return false;
    *server = (BoatNetServer) { .transport = transport, .config = *config };
    text_copy(server->token, config->token, BOAT_NET_TOKEN_BYTES);
    server->config.token = server->token;
    server->content_digest = boat_net_content_digest();
    const BoatWorldDefinition *definition = config->resources->definition;
    if (!boat_fleet_init(&server->fleet, config->resources,
            boat_world_test_water(definition, config->waves, false, false), BOAT_OBJECT_LIFE_TICKS,
            BOAT_OBJECT_DEFAULT_HEIGHT))
        return false;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        server->slot_peer[i] = -1;
    server->backlog_since = -1;
    server->next_checkpoint = 1;
    if (config->deathmatch)
        boat_combat_enable(&server->fleet);
    server->match = match;
    if (config->deathmatch)
        boat_net_match_state_of(&server->match, &server->match_sent);
    server->race = race;
    if (config->race)
        boat_net_race_state_of(&server->race, &server->race_sent);
    return true;
}

static void send_message(BoatNetServer *s, unsigned peer, BoatNetMessage *m)
{
    BoatNetServerPeer *p = &s->peers[peer];
    BoatNetChannel channel = boat_net_channel(m->header.type);
    m->header.epoch = s->config.epoch;
    m->header.sequence = p->sequence[channel]++;
    uint8_t bytes[BOAT_NET_MAX_MESSAGE];
    size_t size = boat_net_encode(m, bytes);
    if (!size) {
        ++s->stats.output_faults; // Our own state left the documented envelope.
        return;
    }
    boat_net_send(&s->transport, peer, channel, bytes, size);
}

static void send_roster(BoatNetServer *s)
{
    BoatNetMessage m = { .header = { .type = BOAT_NET_ROSTER, .tick = s->fleet.state.tick } };
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        int peer = s->slot_peer[i];
        if (peer < 0)
            continue;
        m.roster.slots[i].state = s->peers[peer].phase == BOAT_NET_PEER_READY
            ? BOAT_NET_ROSTER_PLAYING
            : BOAT_NET_ROSTER_JOINING;
        text_copy(m.roster.slots[i].name, s->peers[peer].name, BOAT_NET_NAME_BYTES);
    }
    for (unsigned peer = 0; peer < BOAT_NET_MAX_PEERS; ++peer)
        if (s->peers[peer].phase >= BOAT_NET_PEER_BOOTSTRAP)
            send_message(s, peer, &m);
}

static void broadcast(BoatNetServer *s, BoatNetMessage *m)
{
    for (unsigned peer = 0; peer < BOAT_NET_MAX_PEERS; ++peer)
        if (s->peers[peer].phase >= BOAT_NET_PEER_BOOTSTRAP)
            send_message(s, peer, m);
}

static BoatNetMessage match_state(const BoatNetServer *s)
{
    BoatNetMessage m = { .header = { .type = BOAT_NET_MATCH_STATE, .tick = s->fleet.state.tick } };
    boat_net_match_state_of(&s->match, &m.match);
    return m;
}

// Queue the match's spawn requests, keep its events for the caller and tell the clients:
// events first, then the scoreboard whenever it changed.
static void match_output(BoatNetServer *s, const BoatMatchOutput *out)
{
    s->fleet.state.combat.firing = s->match.phase != BOAT_MATCH_INTERMISSION;
    for (unsigned i = 0; i < out->count; ++i)
        if (out->events[i].kind == BOAT_MATCH_ROUND_START)
            boat_combat_clear(&s->fleet);

    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        if (out->spawn[i])
            boat_fleet_request_spawn(&s->fleet, i);
    for (unsigned i = 0; i < out->count; ++i) {
        s->events[s->event_count++ % BOAT_NET_MATCH_EVENTS] = out->events[i];
        BoatNetMessage m
            = { .header = { .type = BOAT_NET_MATCH_EVENT, .tick = out->events[i].tick },
                  .match_event = boat_net_match_event_of(&s->match, &out->events[i]) };
        broadcast(s, &m);
    }
    BoatNetMessage m = match_state(s);
    if (!boat_net_match_state_equal(&m.match, &s->match_sent)) {
        s->match_sent = m.match;
        broadcast(s, &m);
    }
}

static BoatNetMessage race_state(const BoatNetServer *s)
{
    BoatNetMessage m = { .header = { .type = BOAT_NET_RACE_STATE, .tick = s->fleet.state.tick } };
    boat_net_race_state_of(&s->race, &m.race);
    return m;
}

// Keep the race's events for the caller and tell the clients: events first, then the race
// state whenever it changed (phases, gates, places, joins and leaves).
static void race_output(BoatNetServer *s, const BoatRaceOutput *out)
{
    for (unsigned i = 0; i < out->count; ++i) {
        s->race_events[s->race_event_count++ % BOAT_NET_MATCH_EVENTS] = out->events[i];
        BoatNetMessage m = { .header = { .type = BOAT_NET_RACE_EVENT, .tick = out->events[i].tick },
            .race_event = boat_net_race_event_of(&s->race, &out->events[i]) };
        broadcast(s, &m);
    }
    BoatNetMessage m = race_state(s);
    if (!boat_net_race_state_equal(&m.race, &s->race_sent)) {
        s->race_sent = m.race;
        broadcast(s, &m);
    }
}

// Free PEER's slot between ticks; its live wakes stay and expire naturally.

static void drop_peer(BoatNetServer *s, unsigned peer, bool disconnect)
{
    BoatNetServerPeer *p = &s->peers[peer];
    if (p->phase == BOAT_NET_PEER_FREE)
        return;
    bool owned = p->phase >= BOAT_NET_PEER_BOOTSTRAP;
    if (owned) {
        if (s->config.race)
            boat_race_leave(&s->race, &s->fleet, p->slot); // A running entrant becomes DNF.
        else
            boat_fleet_leave(&s->fleet, p->slot);
        s->slot_peer[p->slot] = -1;
        s->inputs[p->slot] = (BoatNetSlotInput) { 0 };
        ++s->stats.left;
    }
    *p = (BoatNetServerPeer) { 0 };
    if (disconnect)
        s->transport.disconnect(s->transport.context, peer);
    if (owned)
        send_roster(s);
}

static void leave(BoatNetServer *s, unsigned peer, uint8_t reason)
{
    BoatNetMessage m = { .header = { .type = BOAT_NET_DISCONNECT, .tick = s->fleet.state.tick },
        .leave = reason };
    send_message(s, peer, &m);
    drop_peer(s, peer, true);
}

static void send_checkpoint(BoatNetServer *s, unsigned peer)
{
    static uint8_t bytes[BOAT_NET_MAX_CHECKPOINT];
    const BoatFleetState *state = &s->fleet.state;
    size_t size = boat_net_checkpoint_encode(
        state, s->inputs, boat_net_race_go(&s->race), bytes, sizeof(bytes));
    if (!size) {
        ++s->stats.output_faults;
        return;
    }
    uint32_t id = s->next_checkpoint++;
    uint16_t pages = (uint16_t)((size + BOAT_NET_PAGE_BYTES - 1) / BOAT_NET_PAGE_BYTES);
    BoatNetMessage m = { .header = { .type = BOAT_NET_WORLD_BEGIN, .tick = state->tick },
        .world_begin = { .checkpoint = id,
            .bytes = (uint32_t)size,
            .pages = pages,
            .digest = boat_net_bytes_digest(bytes, size),
            .fleet_hash = boat_fleet_hash(state) } };
    send_message(s, peer, &m);
    for (uint16_t page = 0; page < pages; ++page) {
        size_t offset = (size_t)page * BOAT_NET_PAGE_BYTES;
        size_t length = size - offset < BOAT_NET_PAGE_BYTES ? size - offset : BOAT_NET_PAGE_BYTES;
        m = (BoatNetMessage) { .header = { .type = BOAT_NET_WORLD_PAGE, .tick = state->tick },
            .world_page
            = { .checkpoint = id, .index = page, .count = pages, .size = (uint16_t)length } };
        memcpy(m.world_page.data, bytes + offset, length);
        send_message(s, peer, &m);
    }
    m = (BoatNetMessage) { .header = { .type = BOAT_NET_WORLD_END, .tick = state->tick },
        .checkpoint = id };
    send_message(s, peer, &m);
    ++s->stats.bootstraps;
    s->stats.bootstrap_bytes += size;
}

static void reject(BoatNetServer *s, unsigned peer, uint8_t reason)
{
    BoatNetMessage m = { .header = { .type = BOAT_NET_REJECT, .tick = s->fleet.state.tick },
        .reject = { .reason = reason,
            .version = BOAT_NET_VERSION,
            .physics_revision = BOAT_PHYSICS_REVISION,
            .content_digest = s->content_digest } };
    send_message(s, peer, &m);
    ++s->stats.rejected[reason];
    // Rejected before any slot or boat exists.
    s->peers[peer] = (BoatNetServerPeer) { 0 };
    s->transport.disconnect(s->transport.context, peer);
}

static void hello(BoatNetServer *s, unsigned peer, const BoatNetHello *h)
{
    uint8_t reason = 0;
    unsigned slot = BOAT_FLEET_SLOTS, players = 0;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        if (s->slot_peer[i] >= 0)
            ++players;
        else if (slot == BOAT_FLEET_SLOTS)
            slot = i;
    }
    if (h->physics_revision != BOAT_PHYSICS_REVISION)
        reason = BOAT_NET_REJECT_PHYSICS;
    else if (h->content_digest != s->content_digest)
        reason = BOAT_NET_REJECT_CONTENT;
    else if (strcmp(h->token, s->token) != 0)
        reason = BOAT_NET_REJECT_TOKEN;
    else if (slot == BOAT_FLEET_SLOTS || players >= s->config.max_players)
        reason = BOAT_NET_REJECT_FULL;
    if (reason) {
        reject(s, peer, reason);
        return;
    }
    BoatNetServerPeer *p = &s->peers[peer];
    p->phase = BOAT_NET_PEER_BOOTSTRAP;
    p->slot = slot;
    text_copy(p->name, h->name, BOAT_NET_NAME_BYTES);
    s->slot_peer[slot] = (int)peer;
    s->inputs[slot] = (BoatNetSlotInput) { 0 };
    ++s->stats.accepted;
    BoatNetMessage m = { .header = { .type = BOAT_NET_ACCEPT, .tick = s->fleet.state.tick },
        .accept = { .slot = (uint8_t)slot,
            .waves = s->config.waves,
            .location = (uint8_t)s->config.resources->definition->id,
            .snapshot_ticks = BOAT_NET_SNAPSHOT_TICKS,
            .birth_ticks = BOAT_NET_BIRTH_TICKS,
            .input_ticks = BOAT_NET_INPUT_TICKS,
            .max_commands = BOAT_NET_MAX_COMMANDS,
            .max_future = BOAT_NET_MAX_FUTURE_TICKS,
            .tick_hz = BOAT_TICK_HZ,
            .mode = s->config.race     ? BOAT_NET_MODE_RACE
                : s->config.deathmatch ? BOAT_NET_MODE_DEATHMATCH
                                       : BOAT_NET_MODE_FREE,
            .wake_life_ticks = s->fleet.state.wakes.life_ticks,
            .crest_height = s->fleet.state.wakes.crest_height } };
    send_message(s, peer, &m);
    send_roster(s);
    if (s->config.deathmatch) {
        m = match_state(s); // Join in progress: the current round and scores.
        send_message(s, peer, &m);
    }
    if (s->config.race) {
        m = race_state(s); // Join in progress: the phase, clock and rows.
        send_message(s, peer, &m);
    }
    send_checkpoint(s, peer);
}

// Store each command for a target tick the authority has not finalized, once.
static void commands(
    BoatNetServer *s, BoatNetServerPeer *p, const BoatNetHeader *h, const BoatNetInputBatch *batch)
{
    const BoatFleetSlot *slot = &s->fleet.state.slots[p->slot];
    if (p->input_seen && !boat_net_sequence_newer(h->sequence, p->input_sequence)) {
        ++s->stats.stale_packets;
        return;
    }
    p->input_seen = true;
    p->input_sequence = h->sequence;
    if (p->input_tokens < 1) {
        ++s->stats.rate_limited;
        return;
    }
    p->input_tokens -= 1;
    if (!slot->active || batch->life != slot->life) {
        s->stats.stale_life_commands += batch->count;
        return;
    }
    BoatNetSlotControl *c = &s->control[p->slot];
    uint64_t finalized = s->fleet.state.tick;
    for (unsigned i = 0; i < batch->count; ++i) {
        uint64_t tick = h->tick - batch->count + 1 + i;
        if (tick <= finalized) {
            // Older redundant copies are expected; only a late newest command counts.
            s->stats.late_commands += i + 1 == batch->count;
            continue;
        }
        if (tick > finalized + BOAT_NET_MAX_FUTURE_TICKS) {
            ++s->stats.future_commands;
            continue;
        }
        BoatNetQueuedCommand *q = &c->ring[tick % BOAT_NET_INPUT_RING];
        if (q->tick == tick && q->life == batch->life) {
            ++s->stats.duplicate_commands; // Immutable once received.
            continue;
        }
        *q = (BoatNetQueuedCommand) { tick, batch->life, batch->commands[i] };
        ++s->stats.commands;
    }
}

static void misbehaved(BoatNetServer *s, unsigned peer)
{
    if (++s->peers[peer].misbehaviour >= BOAT_NET_MISBEHAVIOUR_LIMIT) {
        ++s->stats.misbehaving;
        leave(s, peer, BOAT_NET_LEAVE_MISBEHAVING);
    }
}

static void refill(BoatNetServerPeer *p, double now)
{
    double elapsed = now - p->refill_at;
    p->refill_at = now;
    if (elapsed <= 0)
        return;
    p->input_tokens = fmin(30, p->input_tokens + elapsed * 120);
    p->spawn_tokens = fmin(2, p->spawn_tokens + elapsed * 2);
}

static void message(BoatNetServer *s, unsigned peer, const BoatNetEvent *event, double now)
{
    BoatNetServerPeer *p = &s->peers[peer];
    static BoatNetMessage m;
    BoatNetError error = boat_net_decode(event->data, event->size, &m);
    if (error == BOAT_NET_BAD_VERSION && p->phase == BOAT_NET_PEER_HANDSHAKE) {
        BoatNetHeader header;
        boat_net_decode_header(event->data, event->size, &header);
        if (header.type == BOAT_NET_HELLO) {
            reject(s, peer, BOAT_NET_REJECT_VERSION);
            return;
        }
    }
    if (error) {
        ++s->stats.malformed;
        if (p->phase == BOAT_NET_PEER_HANDSHAKE)
            reject(s, peer, BOAT_NET_REJECT_MALFORMED);
        else
            misbehaved(s, peer);
        return;
    }
    p->heard_at = now;
    refill(p, now);
    if (p->phase == BOAT_NET_PEER_HANDSHAKE) {
        if (m.header.type == BOAT_NET_HELLO && m.header.epoch == 0)
            hello(s, peer, &m.hello);
        else
            reject(s, peer, BOAT_NET_REJECT_MALFORMED);
        return;
    }
    if (m.header.epoch != s->config.epoch) {
        ++s->stats.stale_packets; // Another session's traffic never reaches this room.
        return;
    }
    switch (m.header.type) {
    case BOAT_NET_READY:
        if (p->phase == BOAT_NET_PEER_BOOTSTRAP) {
            p->phase = BOAT_NET_PEER_READY;
            // A race places joiners itself: free boating while waiting, else the next grid.
            if (!s->config.race || !boat_race_join(&s->race, &s->fleet, p->slot))
                boat_fleet_request_spawn(&s->fleet, p->slot);
            send_roster(s);
        }
        break;
    case BOAT_NET_INPUT_BATCH:
        if (p->phase == BOAT_NET_PEER_READY)
            commands(s, p, &m.header, &m.input);
        break;
    case BOAT_NET_SPAWN_REQUEST: {
        const BoatFleetSlot *slot = &s->fleet.state.slots[p->slot];
        if (p->phase != BOAT_NET_PEER_READY)
            break;
        if (p->spawn_tokens < 1) {
            ++s->stats.rate_limited;
            break;
        }
        p->spawn_tokens -= 1;
        // Idempotent: a request names the life it resets, so repeats cannot reset twice.
        if (!slot->active || slot->life == m.spawn.life) {
            ++s->stats.spawn_requests;
            BoatMatchOutput out = { 0 };
            // A deathmatch may hold a fresh capsize back or score a bailout first; a race
            // recovers behind the last cleared gate on its next observation.
            if (s->config.race)
                boat_race_reset(&s->race, &s->fleet, p->slot);
            else if (!s->config.deathmatch || boat_match_reset(&s->match, &s->fleet, p->slot, &out))
                boat_fleet_request_spawn(&s->fleet, p->slot);
            if (s->config.deathmatch)
                match_output(s, &out);
        }
        break;
    }
    case BOAT_NET_PING: {
        BoatNetMessage pong = { .header = { .type = BOAT_NET_PONG, .tick = s->fleet.state.tick },
            .clock = m.clock };
        send_message(s, peer, &pong);
        break;
    }
    case BOAT_NET_RESYNC_REQUEST:
        if (now - p->last_resync < 1) {
            ++s->stats.rate_limited;
            break;
        }
        p->last_resync = now;
        ++s->stats.resyncs;
        send_checkpoint(s, peer);
        break;
    case BOAT_NET_DISCONNECT:
        drop_peer(s, peer, true);
        break;
    default:
        // Clients never send world state: snapshots, births, checkpoints, accepts...
        ++s->stats.forbidden;
        misbehaved(s, peer);
    }
}

static void handle_events(BoatNetServer *s, double now)
{
    BoatNetEvent event;
    unsigned handled = 0;
    while (handled < BOAT_NET_MESSAGES_PER_SERVICE && boat_net_poll(&s->transport, &event)) {
        ++handled;
        if (event.peer >= BOAT_NET_MAX_PEERS)
            continue;
        BoatNetServerPeer *p = &s->peers[event.peer];
        switch (event.kind) {
        case BOAT_NET_EVENT_CONNECT:
            drop_peer(s, event.peer, false);
            *p = (BoatNetServerPeer) { .phase = BOAT_NET_PEER_HANDSHAKE,
                .connected_at = now,
                .heard_at = now,
                .refill_at = now,
                .last_resync = -1e9,
                .input_tokens = 30,
                .spawn_tokens = 2 };
            for (unsigned c = 0; c < BOAT_NET_CHANNELS; ++c)
                p->sequence[c] = s->config.first_sequence;
            break;
        case BOAT_NET_EVENT_DISCONNECT:
            drop_peer(s, event.peer, false);
            break;
        case BOAT_NET_EVENT_MESSAGE:
            if (p->phase != BOAT_NET_PEER_FREE)
                message(s, event.peer, &event, now);
            break;
        default:
            break;
        }
    }
    if (handled > s->stats.max_messages_per_service)
        s->stats.max_messages_per_service = handled;
}

// The command integrated for slot I on the coming tick, by the documented loss policy.
static BoatInput select_input(BoatNetServer *s, unsigned i, uint64_t tick)
{
    const BoatFleetSlot *slot = &s->fleet.state.slots[i];
    BoatNetSlotControl *c = &s->control[i];
    BoatNetSlotInput *record = &s->inputs[i];
    if (!slot->active) {
        *record = (BoatNetSlotInput) { 0 };
        return (BoatInput) { 0 };
    }
    if (c->life != slot->life) {
        // A new life: earlier commands steered the old one.
        c->life = slot->life;
        c->fresh = s->fleet.state.tick;
        c->last = (BoatNetCommand) { 0 };
        record->tick = 0;
    }
    const BoatNetQueuedCommand *q = &c->ring[tick % BOAT_NET_INPUT_RING];
    BoatNetCommand command;
    if (q->tick == tick && q->life == slot->life) {
        command = c->last = q->command;
        c->fresh = tick;
        record->tick = tick;
        record->state = BOAT_NET_INPUT_RECEIVED;
    } else if (tick - c->fresh <= BOAT_NET_REPEAT_TICKS) {
        command = c->last;
        record->state = BOAT_NET_INPUT_REPEATED;
    } else if (tick - c->fresh < BOAT_NET_NEUTRAL_TICKS) {
        command = (BoatNetCommand) { 0 };
        record->state = BOAT_NET_INPUT_ZERO;
    } else {
        command = (BoatNetCommand) { 0 };
        record->state = BOAT_NET_INPUT_NEUTRAL;
    }
    if (command.flags & BOAT_NET_COMMAND_INACTIVE)
        record->state = BOAT_NET_INPUT_NEUTRAL;
    if (record->state == BOAT_NET_INPUT_NEUTRAL)
        command = boat_net_neutral_command(&slot->boat, command.flags);
    record->used = command;
    ++s->stats.input_states[record->state];
    return boat_net_command_input(command);
}

static void send_births(BoatNetServer *s)
{
    const BoatWakeObjects *w = &s->fleet.state.wakes;
    BoatNetMessage m = { .header = { .type = BOAT_NET_WAKE_BIRTHS, .tick = w->tick } };
    // Births of the last 1.5 s, newest first when more than a batch would hold.
    for (uint64_t age = 0; age < BOAT_NET_BIRTH_WINDOW_TICKS && age < w->tick; ++age) {
        uint64_t born = w->tick - age;
        for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i) {
            if (w->objects[i].born != born || born == 0)
                continue;
            if (m.births.count == BOAT_NET_MAX_BIRTHS) {
                ++s->stats.births_truncated;
                continue;
            }
            m.births.births[m.births.count++]
                = (BoatNetBirth) { .slot = (uint8_t)i, .record = w->objects[i] };
        }
    }
    for (unsigned peer = 0; peer < BOAT_NET_MAX_PEERS; ++peer)
        if (s->peers[peer].phase >= BOAT_NET_PEER_BOOTSTRAP)
            send_message(s, peer, &m);
    ++s->stats.birth_batches;
    s->stats.births_sent += m.births.count;
}

static void send_snapshots(BoatNetServer *s)
{
    BoatNetMessage m = { .header = { .type = BOAT_NET_SNAPSHOT, .tick = s->fleet.state.tick } };
    boat_net_snapshot_of(&s->fleet.state, s->inputs, &m.snapshot);
    m.snapshot.race_go = boat_net_race_go(&s->race);
    for (unsigned peer = 0; peer < BOAT_NET_MAX_PEERS; ++peer)
        if (s->peers[peer].phase >= BOAT_NET_PEER_BOOTSTRAP)
            send_message(s, peer, &m);
    if (s->fleet.state.combat.enabled) {
        uint8_t bytes[BOAT_COMBAT_BYTES];
        boat_combat_encode(&s->fleet.state.combat, bytes);
        for (unsigned i = 0; i < 4; ++i) {
            BoatNetMessage page
                = { .header = { .type = BOAT_NET_COMBAT_PAGE, .tick = s->fleet.state.tick } };
            BoatNetWorldPage *p = &page.world_page;
            p->checkpoint = (uint32_t)s->fleet.state.tick;
            p->index = (uint16_t)i;
            p->count = 4;
            p->size = (uint16_t)(i == 3 ? BOAT_COMBAT_BYTES - 3 * BOAT_NET_PAGE_BYTES
                                        : BOAT_NET_PAGE_BYTES);
            memcpy(p->data, bytes + i * BOAT_NET_PAGE_BYTES, p->size);
            for (unsigned peer = 0; peer < BOAT_NET_MAX_PEERS; ++peer)
                if (s->peers[peer].phase >= BOAT_NET_PEER_BOOTSTRAP)
                    send_message(s, peer, &page);
        }
    }
    ++s->stats.snapshots;
}

static void tick(BoatNetServer *s)
{
    uint64_t next = s->fleet.state.tick + 1;
    BoatInput inputs[BOAT_FLEET_SLOTS];
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        inputs[i] = select_input(s, i, next);
    boat_fleet_step(&s->fleet, inputs);
    s->stats.emission_failures += s->fleet.stats.emission_failures;
    if (s->config.deathmatch) {
        BoatMatchOutput out;
        boat_match_observe(&s->match, &s->fleet, &out);
        match_output(s, &out);
    }
    if (s->config.race) {
        BoatRaceOutput out;
        boat_race_observe(&s->race, &s->fleet, &out);
        race_output(s, &out);
    }
    ++s->stats.ticks;
    // Births first, so a lossless link delivers them before the snapshot that counts them.
    if (next % BOAT_NET_BIRTH_TICKS == 0)
        send_births(s);
    if (next % BOAT_NET_SNAPSHOT_TICKS == 0)
        send_snapshots(s);
}

bool boat_net_server_service(BoatNetServer *s, double now)
{
    if (!s->started) {
        s->started = true;
        s->origin = now;
        s->origin_tick = s->fleet.state.tick;
    }
    handle_events(s, now);
    for (unsigned peer = 0; peer < BOAT_NET_MAX_PEERS; ++peer) {
        BoatNetServerPeer *p = &s->peers[peer];
        if (p->phase == BOAT_NET_PEER_FREE)
            continue;
        bool silent
            = now - p->heard_at > (s->peer_timeout > 0 ? s->peer_timeout : BOAT_NET_TIMEOUT);
        bool unready
            = p->phase != BOAT_NET_PEER_READY && now - p->connected_at > BOAT_NET_HANDSHAKE_TIMEOUT;
        if (silent || unready) {
            ++s->stats.timeouts;
            leave(s, peer, BOAT_NET_LEAVE_TIMEOUT);
        }
    }
    uint64_t due = s->origin_tick + (uint64_t)floor((now - s->origin) * BOAT_TICK_HZ);
    if (s->tick_limit && due > s->tick_limit)
        due = s->tick_limit;
    for (unsigned n = 0; n < BOAT_NET_CATCH_UP_TICKS && s->fleet.state.tick < due; ++n)
        tick(s);
    uint64_t backlog = due > s->fleet.state.tick ? due - s->fleet.state.tick : 0;
    if (backlog > s->stats.max_backlog)
        s->stats.max_backlog = (unsigned)backlog;
    if (backlog > BOAT_TICK_HZ / 2) {
        if (s->backlog_since < 0)
            s->backlog_since = now;
        else if (now - s->backlog_since > 1)
            s->overloaded = true;
    } else {
        s->backlog_since = -1;
    }
    return !s->overloaded;
}

double boat_net_server_next_tick(const BoatNetServer *s)
{
    return s->origin + (double)(s->fleet.state.tick + 1 - s->origin_tick) / BOAT_TICK_HZ;
}

unsigned boat_net_server_players(const BoatNetServer *s)
{
    unsigned players = 0;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        players += s->slot_peer[i] >= 0;
    return players;
}

void boat_net_server_shutdown(BoatNetServer *s)
{
    for (unsigned peer = 0; peer < BOAT_NET_MAX_PEERS; ++peer)
        if (s->peers[peer].phase != BOAT_NET_PEER_FREE)
            leave(s, peer, BOAT_NET_LEAVE_SHUTDOWN);
}
