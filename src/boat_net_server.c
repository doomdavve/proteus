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

static void objective_poll(BoatNetServer *s);
static void hello(BoatNetServer *s, unsigned peer, const BoatNetHello *h);

bool boat_net_server_init(
    BoatNetServer *server, BoatNetTransport transport, const BoatNetServerConfig *config)
{
    if (!config || !boat_world_resources_ready(config->resources) || !config->epoch
        || config->max_players < 1 || config->max_players > BOAT_FLEET_SLOTS
        || (config->token && strlen(config->token) > BOAT_NET_TOKEN_BYTES) || !config->room
        || config->room->location != config->resources->definition->id)
        return false;
    const BoatRoomDefinition *room = config->room;
    BoatRoomDiagnostic check;
    if (!boat_room_definition_validate(room, &check))
        return false;
    bool deathmatch = room->mode == BOAT_ROOM_MODE_DEATHMATCH;
    bool race = room->mode == BOAT_ROOM_MODE_RACE;
    BoatMatch match = { 0 };
    if (deathmatch && !boat_match_init(&match, &room->match_rules))
        return false;
    BoatRace boat_race = { 0 };
    if (race && !boat_race_init(&boat_race, &room->race_rules, &room->course))
        return false;
    // Encode once: every accepted client receives this exact definition before its checkpoint.
    static uint8_t bytes[BOAT_ROOM_BYTES_MAX];
    size_t size = boat_room_definition_encode(room, bytes, sizeof(bytes));
    if (!size)
        return false;
    *server = (BoatNetServer) { .transport = transport,
        .config = *config,
        .room = room,
        .deathmatch = deathmatch,
        .race_room = race };
    memcpy(server->definition, bytes, size);
    server->definition_bytes = (uint32_t)size;
    text_copy(server->token, config->token, BOAT_NET_TOKEN_BYTES);
    server->config.token = server->token;
    server->content_digest = boat_net_content_digest();
    const BoatWorldDefinition *definition = config->resources->definition;
    const BoatNetPrivateSession *local = &config->local;
    if (!boat_fleet_init_scene(&server->fleet, config->resources,
            local->enabled ? local->water
                           : boat_world_test_water(definition, config->waves, false, false),
            local->enabled && local->wake_life_ticks ? local->wake_life_ticks
                                                     : BOAT_OBJECT_LIFE_TICKS,
            local->enabled && local->crest_height ? local->crest_height
                                                  : BOAT_OBJECT_DEFAULT_HEIGHT,
            room->has_scene ? &room->scene : NULL))
        return false;
    if (local->enabled)
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            server->fleet.handling[i] = local->handling;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        server->slot_peer[i] = -1;
    server->deferred_slot = -1;
    boat_marks_tracker_reset(&server->marks);
    server->backlog_since = -1;
    server->next_checkpoint = 1;
    server->stats.revision = 1;
    if (deathmatch)
        boat_combat_enable(&server->fleet, &room->combat_rules);
    server->match = match;
    if (deathmatch)
        boat_net_match_state_of(&server->match, &server->match_sent);
    server->race = boat_race;
    if (race)
        boat_net_race_state_of(&server->race, &server->race_sent);
    objective_poll(server);
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

static BoatNetMessage objective_message(const BoatNetServer *s)
{
    BoatNetMessage m
        = { .header = { .type = BOAT_NET_OBJECTIVE_STATE, .tick = s->fleet.state.tick } };
    m.objective = s->objective;
    return m;
}

// Ask the adapter whether its HUD changed; if so number it and tell every connected peer.
static void objective_poll(BoatNetServer *s)
{
    BoatNetObjectiveState next;
    if (!s->config.rules.state)
        return;
    next = (BoatNetObjectiveState) { 0 };
    if (!s->config.rules.state(s->config.rules.context, &next))
        return;
    next.revision = s->objective.revision + 1;
    s->objective = next;
    s->has_objective = true;
    BoatNetMessage m = objective_message(s);
    for (unsigned peer = 0; peer < BOAT_NET_MAX_PEERS; ++peer)
        if (s->peers[peer].phase >= BOAT_NET_PEER_BOOTSTRAP) {
            send_message(s, peer, &m);
            ++s->stats.objective_states;
        }
}

static void rule_event(BoatNetServer *s, BoatNetRuleKind kind, unsigned slot, unsigned mark)
{
    if (!s->config.rules.event)
        return;
    BoatNetRuleEvent e = { .kind = (uint8_t)kind, .slot = (uint8_t)slot, .mark = (uint8_t)mark };
    s->config.rules.event(s->config.rules.context, s->fleet.state.tick, &e);
}

// Free PEER's slot between ticks; its live wakes stay and expire naturally.

static void drop_peer(BoatNetServer *s, unsigned peer, bool disconnect)
{
    BoatNetServerPeer *p = &s->peers[peer];
    if (p->phase == BOAT_NET_PEER_FREE)
        return;
    bool owned = p->phase >= BOAT_NET_PEER_BOOTSTRAP;
    if (owned) {
        if (s->race_room)
            boat_race_leave(&s->race, &s->fleet, p->slot); // A running entrant becomes DNF.
        else
            boat_fleet_leave(&s->fleet, p->slot);
        s->slot_peer[p->slot] = -1;
        s->inputs[p->slot] = (BoatNetSlotInput) { 0 };
        ++s->stats.left;
        rule_event(s, BOAT_NET_RULE_CREW_LEFT, p->slot, 0);
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

static void send_definition(BoatNetServer *s, unsigned peer)
{
    // The definition is immutable for the connection: a resync reuses the one already installed.
    uint32_t id = s->next_checkpoint++;
    uint16_t pages
        = (uint16_t)((s->definition_bytes + BOAT_NET_PAGE_BYTES - 1) / BOAT_NET_PAGE_BYTES);
    BoatNetMessage m
        = { .header = { .type = BOAT_NET_DEFINITION_BEGIN, .tick = s->fleet.state.tick },
              .definition_begin = { .definition = id,
                  .bytes = s->definition_bytes,
                  .pages = pages,
                  .digest = boat_net_bytes_digest(s->definition, s->definition_bytes) } };
    send_message(s, peer, &m);
    for (uint16_t page = 0; page < pages; ++page) {
        size_t offset = (size_t)page * BOAT_NET_PAGE_BYTES;
        size_t length = s->definition_bytes - offset < BOAT_NET_PAGE_BYTES
            ? s->definition_bytes - offset
            : BOAT_NET_PAGE_BYTES;
        m = (BoatNetMessage) { .header
            = { .type = BOAT_NET_DEFINITION_PAGE, .tick = s->fleet.state.tick },
            .world_page
            = { .checkpoint = id, .index = page, .count = pages, .size = (uint16_t)length } };
        memcpy(m.world_page.data, s->definition + offset, length);
        send_message(s, peer, &m);
    }
    m = (BoatNetMessage) {
        .header = { .type = BOAT_NET_DEFINITION_END, .tick = s->fleet.state.tick }, .checkpoint = id
    };
    send_message(s, peer, &m);
    s->stats.definition_bytes += s->definition_bytes;
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
    rule_event(s, BOAT_NET_RULE_CREW_JOINED, slot, 0);
    // A private session's boat is in the world before the checkpoint that bootstraps its
    // client, so the client commands it from the first granted tick.
    if (s->config.local.enabled)
        boat_fleet_place(&s->fleet, slot, s->config.local.spawn);
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
            .mode = s->race_room ? BOAT_NET_MODE_RACE
                : s->deathmatch  ? BOAT_NET_MODE_DEATHMATCH
                                 : BOAT_NET_MODE_FREE,
            .wake_life_ticks = s->fleet.state.wakes.life_ticks,
            .crest_height = s->fleet.state.wakes.crest_height,
            .revision = s->stats.revision } };
    send_message(s, peer, &m);
    send_roster(s);
    if (s->deathmatch) {
        m = match_state(s); // Join in progress: the current round and scores.
        send_message(s, peer, &m);
    }
    if (s->race_room) {
        m = race_state(s); // Join in progress: the phase, clock and rows.
        send_message(s, peer, &m);
    }
    if (s->has_objective) {
        m = objective_message(s); // Join in progress: the current HUD.
        send_message(s, peer, &m);
        ++s->stats.objective_states;
    }
    send_definition(s, peer);
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

// AR6: the private range, accepted only by a private session (a hosted room's decoder counts it
// as malformed like any unknown type).
// L1: a recover request at the frame boundary it names; any other tick is ignored.
static void private_recover(BoatNetServer *s, unsigned peer, const BoatNetEvent *event, double now)
{
    BoatNetServerPeer *p = &s->peers[peer];
    BoatNetHeader header;
    if (boat_net_private_decode_recover(event->data, event->size, &header) != BOAT_NET_OK
        || event->channel != BOAT_NET_CONTROL || p->phase == BOAT_NET_PEER_HANDSHAKE) {
        ++s->stats.malformed;
        if (p->phase == BOAT_NET_PEER_HANDSHAKE)
            reject(s, peer, BOAT_NET_REJECT_MALFORMED);
        else
            misbehaved(s, peer);
        return;
    }
    p->heard_at = now;
    if (header.epoch != s->config.epoch) {
        ++s->stats.stale_packets;
        return;
    }
    BoatFleetSlot *slot = &s->fleet.state.slots[p->slot];
    Boat boat = slot->boat;
    if (p->phase != BOAT_NET_PEER_READY || !slot->active || header.tick != s->fleet.state.tick
        || !s->config.rules.recover
        || !s->config.rules.recover(s->config.rules.context, header.tick, p->slot, &boat)) {
        ++s->stats.recoveries_ignored;
        return;
    }
    boat_fleet_relocate(&s->fleet, p->slot, boat);
    ++s->stats.recoveries;
}

// L1b: install what a commit changes besides the adapter's state, then read the storm again.
static void commit_apply(BoatNetServer *s, unsigned slot, const BoatNetCommitResult *result)
{
    if (result->moved)
        boat_fleet_relocate(&s->fleet, slot, result->boat);
    if (result->rescene)
        s->fleet.world.scene = result->scene;
    BoatStorm storm;
    if (s->config.rules.weather && s->config.rules.weather(s->config.rules.context, &storm))
        s->fleet.world.water.storm = storm;
    ++s->stats.commits;
}

// L4b: the deferred commit's outcome, once the adapter has it.
static void commit_complete(BoatNetServer *s)
{
    if (s->deferred_slot < 0 || !s->config.rules.completion)
        return;
    unsigned slot = (unsigned)s->deferred_slot;
    BoatNetCommitResult result = { .boat = s->fleet.state.slots[slot].boat };
    if (!s->config.rules.completion(s->config.rules.context, &result))
        return;
    s->deferred_slot = -1;
    ++s->stats.commits_completed;
    if (result.moved || result.rescene)
        commit_apply(s, slot, &result);
}

// L1b: a commit request at the frame boundary it names; any other tick is ignored.
static void private_commit(BoatNetServer *s, unsigned peer, const BoatNetEvent *event, double now)
{
    BoatNetServerPeer *p = &s->peers[peer];
    BoatNetHeader header;
    uint32_t revision;
    uint8_t change;
    if (boat_net_private_decode_commit(event->data, event->size, &header, &revision, &change)
            != BOAT_NET_OK
        || event->channel != BOAT_NET_CONTROL || p->phase == BOAT_NET_PEER_HANDSHAKE) {
        ++s->stats.malformed;
        if (p->phase == BOAT_NET_PEER_HANDSHAKE)
            reject(s, peer, BOAT_NET_REJECT_MALFORMED);
        else
            misbehaved(s, peer);
        return;
    }
    p->heard_at = now;
    if (header.epoch != s->config.epoch) {
        ++s->stats.stale_packets;
        return;
    }
    BoatFleetSlot *slot = &s->fleet.state.slots[p->slot];
    BoatNetCommitResult result = { .boat = slot->boat };
    if (p->phase != BOAT_NET_PEER_READY || !slot->active || header.tick != s->fleet.state.tick
        || !s->config.rules.commit
        || !s->config.rules.commit(
            s->config.rules.context, header.tick, p->slot, revision, change, &result)) {
        ++s->stats.commits_ignored;
        return;
    }
    // L4b: a request the adapter took while one was deferred (a cancel) settles that one.
    s->deferred_slot = -1;
    if (result.deferred) {
        s->deferred_slot = (int)p->slot;
        ++s->stats.commits_deferred;
        return;
    }
    commit_apply(s, p->slot, &result);
}

// L1b: the rules adapter's inspection text in reliable chunks, in order.
static void private_inspect(BoatNetServer *s, unsigned peer, const BoatNetEvent *event, double now)
{
    BoatNetServerPeer *p = &s->peers[peer];
    BoatNetHeader header;
    if (boat_net_private_decode_inspect(event->data, event->size, &header) != BOAT_NET_OK
        || event->channel != BOAT_NET_CONTROL || p->phase == BOAT_NET_PEER_HANDSHAKE) {
        ++s->stats.malformed;
        if (p->phase == BOAT_NET_PEER_HANDSHAKE)
            reject(s, peer, BOAT_NET_REJECT_MALFORMED);
        else
            misbehaved(s, peer);
        return;
    }
    p->heard_at = now;
    if (header.epoch != s->config.epoch || p->phase != BOAT_NET_PEER_READY
        || !s->config.rules.inspect)
        return;
    static char text[BOAT_NET_PRIVATE_INSPECTION_BYTES];
    size_t size = s->config.rules.inspect(s->config.rules.context, text, sizeof(text));
    if (size > sizeof(text))
        size = sizeof(text);
    unsigned count = size ? (unsigned)((size + BOAT_NET_PRIVATE_INSPECTION_CHUNK - 1)
                                / BOAT_NET_PRIVATE_INSPECTION_CHUNK)
                          : 1;
    for (unsigned i = 0; i < count; ++i) {
        size_t at = (size_t)i * BOAT_NET_PRIVATE_INSPECTION_CHUNK;
        size_t chunk = size - at < BOAT_NET_PRIVATE_INSPECTION_CHUNK
            ? size - at
            : BOAT_NET_PRIVATE_INSPECTION_CHUNK;
        BoatNetHeader out = { .epoch = s->config.epoch,
            .sequence = p->sequence[BOAT_NET_CONTROL]++,
            .tick = s->fleet.state.tick };
        uint8_t bytes[BOAT_NET_MAX_MESSAGE];
        size_t length = boat_net_private_encode_inspection(
            &out, i, count, size ? text + at : NULL, size ? chunk : 0, bytes);
        if (!length) {
            ++s->stats.output_faults;
            return;
        }
        boat_net_send(&s->transport, peer, BOAT_NET_CONTROL, bytes, length);
    }
    ++s->stats.inspections;
}

static void private_message(BoatNetServer *s, unsigned peer, const BoatNetEvent *event, double now)
{
    if (boat_net_private_type(event->data, event->size) == BOAT_NET_PRIVATE_INSPECT) {
        private_inspect(s, peer, event, now);
        return;
    }
    if (boat_net_private_type(event->data, event->size) == BOAT_NET_PRIVATE_RECOVER) {
        private_recover(s, peer, event, now);
        return;
    }
    if (boat_net_private_type(event->data, event->size) == BOAT_NET_PRIVATE_COMMIT) {
        private_commit(s, peer, event, now);
        return;
    }
    BoatNetServerPeer *p = &s->peers[peer];
    BoatNetHeader header;
    BoatNetInputBatch batch;
    if (boat_net_private_decode_input(event->data, event->size, &header, &batch) != BOAT_NET_OK
        || event->channel != BOAT_NET_STATE) {
        ++s->stats.malformed;
        if (p->phase == BOAT_NET_PEER_HANDSHAKE)
            reject(s, peer, BOAT_NET_REJECT_MALFORMED);
        else
            misbehaved(s, peer);
        return;
    }
    p->heard_at = now;
    if (p->phase == BOAT_NET_PEER_HANDSHAKE) {
        reject(s, peer, BOAT_NET_REJECT_MALFORMED);
        return;
    }
    if (header.epoch != s->config.epoch) {
        ++s->stats.stale_packets; // An earlier generation's traffic never reaches this one.
        return;
    }
    if (p->phase == BOAT_NET_PEER_READY)
        commands(s, p, &header, &batch);
}

static void message(BoatNetServer *s, unsigned peer, const BoatNetEvent *event, double now)
{
    BoatNetServerPeer *p = &s->peers[peer];
    static BoatNetMessage m;
    if (s->config.local.enabled && boat_net_private_message(event->data, event->size)) {
        private_message(s, peer, event, now);
        return;
    }
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
    if (!s->config.local.enabled)
        refill(p, now); // A private session refills per granted tick instead.
    if (p->phase == BOAT_NET_PEER_HANDSHAKE) {
        if (m.header.type == BOAT_NET_HELLO && m.header.epoch == 0 && s->announced.present) {
            // A revision is announced: serve this Hello the new definition once it commits, so
            // the room's population cannot change under the announcement.
            if (!p->hello_held) {
                p->hello_held = true;
                p->held = m.hello;
                ++s->stats.revision_held_hellos;
            }
        } else if (m.header.type == BOAT_NET_HELLO && m.header.epoch == 0)
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
            if (s->config.local.enabled)
                ; // Placed on accept.
            else if (!s->race_room || !boat_race_join(&s->race, &s->fleet, p->slot))
                boat_fleet_request_spawn(&s->fleet, p->slot);
            send_roster(s);
        }
        break;
    case BOAT_NET_REVISION_READY:
        // Only the announced revision counts; a stale or early answer is ignored.
        if (p->phase == BOAT_NET_PEER_READY && s->announced.present
            && m.checkpoint == s->announced.revision)
            p->revision_ready = m.checkpoint;
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
            if (s->race_room)
                boat_race_reset(&s->race, &s->fleet, p->slot);
            else if (!s->deathmatch || boat_match_reset(&s->match, &s->fleet, p->slot, &out))
                boat_fleet_request_spawn(&s->fleet, p->slot);
            if (s->deathmatch)
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
    case BOAT_NET_RESYNC_REQUEST: {
        // One resync per second: elapsed time in hosted rooms, granted ticks in a private session
        // (its client asks on the same clock; elapsed time would refuse it at 8x).
        double clock = s->config.local.enabled ? boat_tick_seconds(s->fleet.state.tick) : now;
        if (clock - p->last_resync < 1) {
            ++s->stats.rate_limited;
            break;
        }
        p->last_resync = clock;
        ++s->stats.resyncs;
        send_checkpoint(s, peer);
        break;
    }
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
        uint8_t bytes[BOAT_COMBAT_COMPACT_BYTES];
        size_t size = boat_combat_compact_encode(
            &s->fleet.state.combat, bytes, sizeof(bytes), s->fleet.state.tick);
        if (!size) {
            ++s->stats.output_faults;
        } else {
            uint16_t pages = (uint16_t)((size + BOAT_NET_PAGE_BYTES - 1) / BOAT_NET_PAGE_BYTES);
            for (uint16_t i = 0; i < pages; ++i) {
                BoatNetMessage page
                    = { .header = { .type = BOAT_NET_COMBAT_PAGE, .tick = s->fleet.state.tick } };
                BoatNetWorldPage *p = &page.world_page;
                size_t offset = (size_t)i * BOAT_NET_PAGE_BYTES;
                p->checkpoint = (uint32_t)s->fleet.state.tick;
                p->index = i;
                p->count = pages;
                p->size = (uint16_t)(size - offset < BOAT_NET_PAGE_BYTES ? size - offset
                                                                         : BOAT_NET_PAGE_BYTES);
                memcpy(p->data, bytes + offset, p->size);
                for (unsigned peer = 0; peer < BOAT_NET_MAX_PEERS; ++peer)
                    if (s->peers[peer].phase >= BOAT_NET_PEER_BOOTSTRAP)
                        send_message(s, peer, &page);
            }
        }
    }
    ++s->stats.snapshots;
}

// P5f: definition revisions. A candidate is staged, waits for a boundary, is announced to every
// ready player and commits at a fixed tick. See boat_net_server_stage_revision.
static bool revision_phase(const BoatNetServer *s)
{
    if (s->race_room)
        return s->race.phase == BOAT_RACE_WAITING || s->race.phase == BOAT_RACE_RESULTS;
    if (s->deathmatch)
        return s->match.phase == BOAT_MATCH_WARMUP || s->match.phase == BOAT_MATCH_INTERMISSION;
    return false;
}

// The tick after which the boundary phase is certain to still hold: its timed end, or never.
static uint64_t revision_phase_end(const BoatNetServer *s)
{
    if (s->race_room && s->race.phase == BOAT_RACE_RESULTS)
        return s->race.phase_end;
    if (s->deathmatch && s->match.phase == BOAT_MATCH_INTERMISSION)
        return s->match.phase_end;
    return UINT64_MAX;
}

static bool revision_peers_settled(const BoatNetServer *s)
{
    for (unsigned peer = 0; peer < BOAT_NET_MAX_PEERS; ++peer)
        if (s->peers[peer].phase == BOAT_NET_PEER_BOOTSTRAP)
            return false; // Joining: it would change the room under the announcement.
    return true;
}

static void revision_announce(BoatNetServer *s, uint64_t commit_tick)
{
    s->announced.present = true;
    s->announced.revision = s->stats.revision + 1;
    s->announced.commit_tick = commit_tick;
    s->announced.bytes = s->waiting.bytes;
    memcpy(s->announced.data, s->waiting.data, s->waiting.bytes);
    s->announced.room = s->waiting.room;
    s->waiting.present = false;
    uint16_t pages
        = (uint16_t)((s->announced.bytes + BOAT_NET_PAGE_BYTES - 1) / BOAT_NET_PAGE_BYTES);
    uint64_t now = s->fleet.state.tick;
    BoatNetMessage begin = { .header = { .type = BOAT_NET_REVISION_BEGIN, .tick = now },
        .revision_begin = { .revision = s->announced.revision,
            .bytes = s->announced.bytes,
            .pages = pages,
            .digest = boat_net_bytes_digest(s->announced.data, s->announced.bytes),
            .commit_tick = commit_tick } };
    for (unsigned peer = 0; peer < BOAT_NET_MAX_PEERS; ++peer) {
        if (s->peers[peer].phase != BOAT_NET_PEER_READY)
            continue;
        send_message(s, peer, &begin);
        for (uint16_t page = 0; page < pages; ++page) {
            size_t offset = (size_t)page * BOAT_NET_PAGE_BYTES;
            size_t length = s->announced.bytes - offset < BOAT_NET_PAGE_BYTES
                ? s->announced.bytes - offset
                : BOAT_NET_PAGE_BYTES;
            BoatNetMessage m = { .header = { .type = BOAT_NET_REVISION_PAGE, .tick = now },
                .world_page = { .checkpoint = s->announced.revision,
                    .index = page,
                    .count = pages,
                    .size = (uint16_t)length } };
            memcpy(m.world_page.data, s->announced.data + offset, length);
            send_message(s, peer, &m);
        }
        BoatNetMessage end = { .header = { .type = BOAT_NET_REVISION_END, .tick = now },
            .checkpoint = s->announced.revision };
        send_message(s, peer, &end);
        s->stats.revision_bytes += s->announced.bytes;
    }
    ++s->stats.revisions_announced;
}

// Install ROOM (encoded as BYTES) as the running definition after the tick just simulated.
static void revision_install(
    BoatNetServer *s, const BoatRoomDefinition *room, const uint8_t *bytes, uint32_t size)
{
    s->revised = *room;
    s->room = &s->revised;
    s->config.room = &s->revised;
    memcpy(s->definition, bytes, size);
    s->definition_bytes = size;
    s->fleet.world.scene = s->revised.has_scene ? &s->revised.scene : NULL;
    if (s->deathmatch) {
        s->match.rules = s->revised.match_rules;
        s->fleet.state.combat.rules = s->revised.combat_rules;
        BoatNetMessage m = match_state(s);
        if (!boat_net_match_state_equal(&m.match, &s->match_sent)) {
            s->match_sent = m.match;
            broadcast(s, &m);
        }
    }
    if (s->race_room) {
        s->race.rules = s->revised.race_rules;
        s->race.course = s->revised.course;
        boat_race_bind(&s->race, &s->fleet);
        BoatNetMessage m = race_state(s);
        if (!boat_net_race_state_equal(&m.race, &s->race_sent)) {
            s->race_sent = m.race;
            broadcast(s, &m);
        }
    }
    boat_marks_tracker_reset(&s->marks);
    ++s->stats.revisions_committed;
    if (s->config.rules.definition)
        s->config.rules.definition(s->config.rules.context, s->fleet.state.tick, s->room);
}

// At the commit tick: drop whoever is not ready, switch, then serve the Hellos held meanwhile.
static void revision_commit(BoatNetServer *s)
{
    for (unsigned peer = 0; peer < BOAT_NET_MAX_PEERS; ++peer) {
        BoatNetServerPeer *p = &s->peers[peer];
        if (p->phase >= BOAT_NET_PEER_BOOTSTRAP && p->revision_ready != s->announced.revision) {
            ++s->stats.revision_leaves;
            leave(s, peer, BOAT_NET_LEAVE_REVISION);
        }
    }
    s->stats.revision = s->announced.revision;
    s->announced.present = false;
    revision_install(s, &s->announced.room, s->announced.data, s->announced.bytes);
    for (unsigned peer = 0; peer < BOAT_NET_MAX_PEERS; ++peer) {
        BoatNetServerPeer *p = &s->peers[peer];
        if (p->phase == BOAT_NET_PEER_HANDSHAKE && p->hello_held) {
            p->hello_held = false;
            hello(s, peer, &p->held);
        }
    }
}

// After each tick's rules: commit the announced revision when its tick comes, else announce or
// commit the waiting candidate at a boundary.
static void revision_tick(BoatNetServer *s)
{
    uint64_t tick = s->fleet.state.tick;
    if (s->announced.present) {
        if (tick >= s->announced.commit_tick)
            revision_commit(s);
        return;
    }
    if (!s->waiting.present)
        return;
    bool players = boat_net_server_players(s) != 0;
    bool rounds = s->race_room || s->deathmatch;
    if (rounds ? !revision_phase(s) : players)
        return; // A free room revises only while empty.
    if (!players) {
        // Nobody connected: nothing to announce.
        ++s->stats.revision;
        s->waiting.present = false;
        revision_install(s, &s->waiting.room, s->waiting.data, s->waiting.bytes);
        return;
    }
    uint64_t commit = (tick + BOAT_NET_REVISION_LEAD_TICKS + BOAT_NET_SNAPSHOT_TICKS - 1)
        / BOAT_NET_SNAPSHOT_TICKS * BOAT_NET_SNAPSHOT_TICKS;
    if (commit < revision_phase_end(s) && revision_peers_settled(s))
        revision_announce(s, commit);
}

bool boat_net_server_stage_revision(
    BoatNetServer *s, const uint8_t *bytes, size_t size, BoatRoomDiagnostic *diagnostic)
{
    static BoatRoomDefinition room;
    BoatRoomDiagnostic local;
    BoatRoomDiagnostic *d = diagnostic ? diagnostic : &local;
    *d = (BoatRoomDiagnostic) { 0 };
    const char *error = NULL;
    if (!boat_room_definition_decode(bytes, size, &room, d)) {
        if (!d->message[0])
            text_copy(d->message, "is not a valid room definition", sizeof(d->message) - 1);
        ++s->stats.revisions_refused;
        return false;
    }
    BoatMatch match;
    BoatRace race;
    if (room.mode != s->room->mode || room.location != s->room->location)
        error = "changes the room's mode or location";
    else if (room.mode == BOAT_ROOM_MODE_DEATHMATCH && !boat_match_init(&match, &room.match_rules))
        error = "has invalid match rules";
    else if (room.mode == BOAT_ROOM_MODE_RACE
        && !boat_race_init(&race, &room.race_rules, &room.course))
        error = "has an invalid race course";
    if (error) {
        text_copy(d->message, error, sizeof(d->message) - 1);
        ++s->stats.revisions_refused;
        return false;
    }
    // The canonical encoding is what clients receive and digest.
    static uint8_t encoded[BOAT_ROOM_BYTES_MAX];
    size_t length = boat_room_definition_encode(&room, encoded, sizeof(encoded));
    if (!length) {
        text_copy(d->message, "does not encode", sizeof(d->message) - 1);
        ++s->stats.revisions_refused;
        return false;
    }
    s->waiting.present = true;
    s->waiting.bytes = (uint32_t)length;
    memcpy(s->waiting.data, encoded, length);
    s->waiting.room = room;
    ++s->stats.revisions_staged;
    return true;
}

bool boat_net_server_revision_pending(const BoatNetServer *s)
{
    return s->waiting.present || s->announced.present;
}

static void tick(BoatNetServer *s)
{
    uint64_t next = s->fleet.state.tick + 1;
    BoatInput inputs[BOAT_FLEET_SLOTS];
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        inputs[i] = select_input(s, i, next);
    boat_fleet_step(&s->fleet, inputs);
    s->stats.emission_failures += s->fleet.stats.emission_failures;
    if (s->deathmatch) {
        BoatMatchOutput out;
        boat_match_observe(&s->match, &s->fleet, &out);
        match_output(s, &out);
    }
    if (s->race_room) {
        BoatRaceOutput out;
        boat_race_observe(&s->race, &s->fleet, &out);
        race_output(s, &out);
    }
    if (s->room->marks.count) {
        BoatMarkEntry entries[BOAT_FLEET_SLOTS];
        unsigned count = boat_marks_observe(&s->room->marks, &s->marks, &s->fleet.state, entries);
        for (unsigned i = 0; i < count; ++i)
            rule_event(s, BOAT_NET_RULE_MARK_REACHED, entries[i].slot, entries[i].mark);
    }
    if (s->config.local.enabled && s->config.rules.committed)
        s->config.rules.committed(s->config.rules.context, next, &s->fleet);
    BoatStorm storm;
    if (s->config.local.enabled && s->config.rules.weather
        && s->config.rules.weather(s->config.rules.context, &storm))
        s->fleet.world.water.storm = storm;
    if (s->config.rules.after_tick)
        s->config.rules.after_tick(s->config.rules.context, next);
    objective_poll(s);
    revision_tick(s);
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

// L2: the rules adapter's private state to every ready peer, when it changed since the last send.
static void send_private_state(BoatNetServer *s)
{
    if (!s->config.rules.private_state)
        return;
    uint8_t state[BOAT_NET_PRIVATE_STATE_BYTES];
    size_t size = s->config.rules.private_state(s->config.rules.context, state);
    if (!size || size > sizeof(state)
        || (size == s->private_sent_size && !memcmp(state, s->private_sent, size)))
        return;
    bool sent = false;
    for (unsigned peer = 0; peer < BOAT_NET_MAX_PEERS; ++peer) {
        BoatNetServerPeer *p = &s->peers[peer];
        if (p->phase != BOAT_NET_PEER_READY)
            continue;
        BoatNetHeader header = { .epoch = s->config.epoch,
            .sequence = p->sequence[BOAT_NET_CONTROL]++,
            .tick = s->fleet.state.tick };
        uint8_t bytes[BOAT_NET_MAX_MESSAGE];
        size_t length = boat_net_private_encode_state(&header, state, size, bytes);
        if (!length) {
            ++s->stats.output_faults;
            return;
        }
        boat_net_send(&s->transport, peer, BOAT_NET_CONTROL, bytes, length);
        ++s->stats.private_states;
        sent = true;
    }
    if (sent) {
        memcpy(s->private_sent, state, size);
        s->private_sent_size = size;
    }
}

bool boat_net_server_grant(BoatNetServer *s, double now, unsigned ticks)
{
    if (!s->config.local.enabled)
        return false;
    handle_events(s, now);
    if (s->config.rules.service) {
        s->config.rules.service(s->config.rules.context, now);
        commit_complete(s);
        objective_poll(s);
    }
    for (unsigned n = 0; n < ticks; ++n) {
        // The hosted buckets refill 120 input and 2 spawn tokens per elapsed second: per granted
        // tick here, so 8x is not rate limited and a pause refills nothing.
        for (unsigned peer = 0; peer < BOAT_NET_MAX_PEERS; ++peer) {
            BoatNetServerPeer *p = &s->peers[peer];
            if (p->phase == BOAT_NET_PEER_FREE)
                continue;
            p->input_tokens = fmin(30, p->input_tokens + 1);
            p->spawn_tokens = fmin(2, p->spawn_tokens + 2.0 / BOAT_TICK_HZ);
        }
        tick(s);
    }
    send_private_state(s);
    return true;
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
