#include "boat_net_client.h"
#include "boat_steering.h"
#include "boat_throttle.h"

#include <math.h>
#include <string.h>

static void copy_text(char *storage, const char *text, size_t limit)
{
    size_t length = 0;
    while (text && text[length] && length < limit) {
        storage[length] = text[length];
        ++length;
    }
    storage[length] = 0;
}

void boat_net_client_init(BoatNetClient *client, BoatNetTransport transport,
    const BoatNetClientConfig *config, double now)
{
    *client = (BoatNetClient) { .transport = transport,
        .local = config->local,
        .first_sequence = config->first_sequence,
        .resources = config->resources,
        .predict = config->predict,
        .defer_ready = config->defer_ready,
        .started_at = now,
        .serviced_at = now,
        .pong_after = now,
        .heard_at = now,
        .mismatch_since = -1,
        .resync_at = -1e9,
        .ping_at = -1e9,
        .lead = 8 };
    copy_text(client->name, config->name, BOAT_NET_NAME_BYTES);
    copy_text(client->token, config->token, BOAT_NET_TOKEN_BYTES);
    copy_text(client->build, config->build, BOAT_NET_BUILD_BYTES);
    for (unsigned c = 0; c < BOAT_NET_CHANNELS; ++c)
        client->sequence[c] = config->first_sequence;
}

static void send_message(BoatNetClient *c, BoatNetMessage *m)
{
    BoatNetChannel channel = boat_net_channel(m->header.type);
    m->header.epoch = m->header.type == BOAT_NET_HELLO ? 0 : c->epoch;
    m->header.sequence = c->sequence[channel]++;
    uint8_t bytes[BOAT_NET_MAX_MESSAGE];
    size_t size = boat_net_encode(m, bytes);
    if (size)
        boat_net_send(&c->transport, 0, channel, bytes, size);
}

static uint64_t mirror_tick(const BoatNetClient *c) { return c->fleet.state.tick; }

// Write a birth into the mirror pool unless the slot already holds it or a newer one. Records
// that the mirror's clock has outlived are kept expired, as the authority's pool keeps them.
static void apply_birth(BoatNetClient *c, const BoatNetBirth *b)
{
    BoatWakeObjects *w = &c->fleet.state.wakes;
    BoatWakeObject *o = &w->objects[b->slot];
    if (b->record.born <= o->born) {
        ++c->stats.births_duplicate;
        return;
    }
    *o = b->record;
    if (w->tick - o->born >= w->life_ticks)
        o->amplitude = 0;
    ++c->stats.births_applied;
}

static void hold_birth(BoatNetClient *c, const BoatNetBirth *b)
{
    for (unsigned i = 0; i < c->pending_count; ++i)
        if (c->pending[i].slot == b->slot && c->pending[i].record.born == b->record.born)
            return;
    if (c->pending_count == BOAT_NET_PENDING_BIRTHS) {
        // Bounded: forget the oldest held birth; a later batch or resync repairs it.
        unsigned oldest = 0;
        for (unsigned i = 1; i < c->pending_count; ++i)
            if (c->pending[i].record.born < c->pending[oldest].record.born)
                oldest = i;
        c->pending[oldest] = c->pending[--c->pending_count];
        ++c->stats.births_dropped;
    }
    c->pending[c->pending_count++] = *b;
    ++c->stats.births_held;
}

// Births whose tick the mirror has reached; a record never enters the pool before its birth.
static void release_births(BoatNetClient *c)
{
    for (unsigned i = 0; i < c->pending_count;) {
        if (c->pending[i].record.born <= mirror_tick(c)) {
            apply_birth(c, &c->pending[i]);
            c->pending[i] = c->pending[--c->pending_count];
        } else {
            ++i;
        }
    }
}

// Compare the mirror's live records with the newest snapshot's digest. Births travel at 10 Hz
// and snapshots at 30 Hz, so a snapshot can name a birth whose batch is still on its way;
// only a mismatch that outlasts BOAT_NET_RESYNC_AFTER asks for a fresh checkpoint.
static void check_wakes(BoatNetClient *c, double now)
{
    bool consistent = boat_net_wake_digest(&c->fleet.state.wakes) == c->wake_digest;
    if (consistent && c->mismatch_since >= 0) {
        double waited = now - c->mismatch_since;
        if (waited > c->stats.max_mismatch)
            c->stats.max_mismatch = waited;
        ++c->stats.repaired;
        c->mismatch_since = -1;
    }
    if (!consistent && c->mismatch_since < 0)
        c->mismatch_since = now;
    c->wake_consistent = consistent;
}

static void definition_fail(BoatNetClient *c, uint8_t reason);
static void revision_apply_room(BoatNetClient *c);
static void revision_commit(BoatNetClient *c);

// Race rooms: the course source runs on the latest round's clock, in the mirror and in the
// prediction, which also releases grid holds at GO.
static void bind_race(BoatNetClient *c, uint64_t go)
{
    if (c->accept.mode != BOAT_NET_MODE_RACE || !c->room_valid)
        return;
    c->race_go = go;
    boat_net_race_bind(&c->fleet, &c->room.course, go);
    if (c->predict) {
        c->prediction.fleet.world.water.vessel = c->fleet.world.water.vessel;
        c->prediction.fleet.world.water.storm = c->fleet.world.water.storm;
        c->prediction.release_tick = go;
    }
}

static void install(BoatNetClient *c, double now)
{
    static BoatFleetState state;
    BoatNetSlotInput inputs[BOAT_FLEET_SLOTS];
    uint64_t race_go = 0;
    const BoatNetWorldBegin *b = &c->begin;
    c->assembling = false;
    bool decoded = boat_net_checkpoint_decode(c->assembly, b->bytes, &state, inputs, &race_go)
        == BOAT_NET_OK;
    // A checkpoint at or past a staged commit tick was taken under the new definition.
    bool revised = decoded && c->revision_staged && state.tick >= c->revision_begin.commit_tick;
    if (revised)
        revision_apply_room(c);
    if (decoded && c->room_valid && c->room.mode == BOAT_ROOM_MODE_DEATHMATCH
        && state.combat.enabled
        && !boat_combat_rules_equal(&state.combat.rules, &c->room.combat_rules)) {
        definition_fail(c, 2);
        return;
    }
    bool complete = c->room_valid && c->room.mode == c->accept.mode
        && c->room.location == c->accept.location && c->pages_received == b->pages
        && boat_net_bytes_digest(c->assembly, b->bytes) == b->digest && decoded
        && boat_fleet_hash(&state) == b->fleet_hash
        && state.wakes.life_ticks == c->accept.wake_life_ticks
        && state.wakes.crest_height == c->accept.crest_height
        && (c->room.mode == BOAT_ROOM_MODE_DEATHMATCH ? state.combat.enabled
                    && boat_combat_rules_equal(&state.combat.rules, &c->room.combat_rules)
                                                      : !state.combat.enabled);
    BoatFleet fleet;
    const BoatWorldResources *resources = c->resources && c->accept.location < BOAT_LOCATION_COUNT
        ? &c->resources[c->accept.location]
        : NULL;
    complete = complete && boat_world_resources_ready(resources)
        && resources->definition->id == c->accept.location;
    if (complete)
        complete = boat_fleet_init_scene(&fleet, resources,
            c->local.enabled
                ? c->local.water
                : boat_world_test_water(resources->definition, c->accept.waves, false, false),
            c->accept.wake_life_ticks, c->accept.crest_height,
            c->room.has_scene ? &c->room.scene : NULL);
    if (complete && c->local.enabled)
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            fleet.handling[i] = c->local.handling;
    if (!complete) {
        ++c->stats.bootstrap_failures;
        c->resync_at = now;
        BoatNetMessage m = { .header = { .type = BOAT_NET_RESYNC_REQUEST, .tick = state.tick },
            .resync = { .reason = 2, .snapshot_tick = mirror_tick(c) } };
        send_message(c, &m);
        return;
    }
    // A resync replaces the mirror unless the checkpoint is older. One at the mirror's own tick
    // carries the full wake records that a snapshot only digests, so it repairs the mirror in
    // place (a snapshot at that tick is stale).
    if (c->phase == BOAT_NET_CLIENT_PLAYING && state.tick < mirror_tick(c))
        return;
    memset(c->combat_sets, 0, sizeof(c->combat_sets));
    c->fleet = fleet;
    boat_fleet_restore(&c->fleet, &state);
    memcpy(c->inputs, inputs, sizeof(inputs));
    memcpy(c->previous, state.slots, sizeof(c->previous));
    c->previous_tick = state.tick;
    c->wake_digest = boat_net_wake_digest(&state.wakes);
    c->wake_consistent = true;
    c->mismatch_since = -1;
    c->snapshot_at = now;
    ++c->stats.bootstraps;
    release_births(c);
    check_wakes(c, now);
    if (c->predict && (c->phase != BOAT_NET_CLIENT_PLAYING || revised))
        boat_prediction_reset(&c->prediction, &c->fleet, c->inputs, c->slot);
    bind_race(c, race_go);
    if (c->phase != BOAT_NET_CLIENT_PLAYING)
        boat_remote_history_reset(&c->remotes);
    boat_remote_history_push(&c->remotes, state.tick, state.slots);
    c->predict_dirty = true;
    if (c->defer_ready && !c->ready_sent) {
        // The caller is still opening its window or audio device. Keep the authority's
        // spawn and roster traffic off the reliable channels until it can service frames.
        c->ready_pending = true;
        c->ready_checkpoint = b->checkpoint;
    } else {
        BoatNetMessage m = { .header = { .type = BOAT_NET_READY, .tick = state.tick },
            .checkpoint = b->checkpoint };
        send_message(c, &m);
        c->ready_sent = true;
    }
    c->phase = BOAT_NET_CLIENT_PLAYING;
}

bool boat_net_client_ready(BoatNetClient *c)
{
    if (c->phase != BOAT_NET_CLIENT_PLAYING)
        return false;
    if (!c->ready_pending)
        return true;
    BoatNetMessage m = { .header = { .type = BOAT_NET_READY, .tick = mirror_tick(c) },
        .checkpoint = c->ready_checkpoint };
    send_message(c, &m);
    c->ready_pending = false;
    c->ready_sent = true;
    return true;
}

static void snapshot(BoatNetClient *c, const BoatNetMessage *m, double now)
{
    const BoatNetSnapshot *s = &m->snapshot;
    uint64_t tick = m->header.tick;
    if (tick <= mirror_tick(c)) {
        ++c->stats.stale_snapshots;
        return;
    }
    BoatFleetState *state = &c->fleet.state;
    BoatWakeObjects *w = &state->wakes;
    memcpy(c->previous, state->slots, sizeof(c->previous));
    c->previous_tick = state->tick;
    for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i) {
        BoatWakeObject *o = &w->objects[i];
        if (o->amplitude && tick - o->born >= w->life_ticks)
            o->amplitude = 0;
    }
    state->tick = w->tick = tick;
    state->next_request = s->next_request;
    w->live = s->live;
    w->peak_live = s->peak_live;
    w->emitted = s->emitted;
    w->expired = s->expired;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        state->slots[i] = s->slots[i];
        w->emitters[i] = s->emitters[i];
        c->inputs[i] = s->inputs[i];
    }
    c->wake_digest = s->wake_digest;
    if (s->race_go != c->race_go)
        bind_race(c, s->race_go);
    release_births(c);
    check_wakes(c, now);
    ++*(c->wake_consistent ? &c->stats.consistent : &c->stats.inconsistent);
    c->snapshot_at = now;
    c->predict_dirty = true;
    const BoatFleetSlot *own = &state->slots[c->slot];
    if (c->resetting && own->active && own->life != c->reset_life) {
        c->resetting = false;
        ++c->stats.resets;
        c->stats.max_reset = fmax(c->stats.max_reset, now - c->reset_at);
    } else if (c->resetting && !own->pending && now - c->reset_at > 1) {
        c->resetting = false; // Never queued: the request was rate limited or lost.
        ++c->stats.resets_dropped;
    }
    boat_remote_history_push(&c->remotes, tick, state->slots);
    ++c->stats.snapshots;
    if (c->revision_staged && tick >= c->revision_begin.commit_tick)
        revision_commit(c);
    // The authority sends snapshot n right after tick n: allow half the round trip.
    double sample = (double)tick + c->rtt * .5 * BOAT_TICK_HZ - now * BOAT_TICK_HZ;
    if (!c->offset_valid || fabs(sample - c->offset) > 12) {
        c->offset = sample;
        c->offset_valid = true;
    } else {
        c->offset += (sample - c->offset) * .05;
    }
}

// Snapshot and compact combat pages may arrive in any order on independent channels. Never
// reconcile a hull against a partial combat state, and never retain superseded pages.
static void combat_snapshot(BoatNetClient *c, const BoatNetMessage *m, double now)
{
    uint64_t tick = m->header.tick;
    if (tick <= mirror_tick(c))
        return;
    BoatNetCombatSet *set
        = &c->combat_sets[(tick / BOAT_NET_SNAPSHOT_TICKS) % BOAT_NET_COMBAT_SETS];
    if (set->tick > tick)
        return;
    if (set->tick != tick)
        *set = (BoatNetCombatSet) { .tick = tick };
    if (m->header.type == BOAT_NET_SNAPSHOT) {
        set->snapshot = m->snapshot;
        set->snapshot_received = true;
    } else {
        const BoatNetWorldPage *p = &m->world_page;
        if (set->count && set->count != p->count) {
            ++c->stats.decode_errors;
            return;
        }
        if (!(set->received & (1u << p->index))) {
            set->count = p->count;
            set->received |= (uint8_t)(1u << p->index);
            set->size = (uint16_t)(set->size + p->size);
            memcpy(set->bytes + p->index * BOAT_NET_PAGE_BYTES, p->data, p->size);
        }
    }
    if (set->count && set->received == (uint8_t)((1u << set->count) - 1)
        && set->snapshot_received) {
        BoatCombat combat;
        if (!boat_combat_compact_decode(
                &combat, set->bytes, set->size, tick, &c->fleet.state.combat.rules)
            || !combat.enabled) {
            ++c->stats.decode_errors;
            return;
        }
        BoatNetMessage complete
            = { .header = { .type = BOAT_NET_SNAPSHOT, .tick = tick }, .snapshot = set->snapshot };
        c->fleet.state.combat = combat;
        boat_combat_bind(&c->fleet);
        snapshot(c, &complete, now);
        for (unsigned i = 0; i < BOAT_NET_COMBAT_SETS; ++i)
            if (c->combat_sets[i].tick <= tick)
                c->combat_sets[i] = (BoatNetCombatSet) { 0 };
    }
}

static void births(BoatNetClient *c, const BoatNetWakeBirths *b, double now)
{
    for (unsigned i = 0; i < b->count; ++i) {
        if (c->phase == BOAT_NET_CLIENT_PLAYING && b->births[i].record.born <= mirror_tick(c)) {
            uint64_t applied = c->stats.births_applied;
            apply_birth(c, &b->births[i]);
            c->predict_dirty |= c->stats.births_applied != applied;
        } else
            hold_birth(c, &b->births[i]);
    }
    if (c->phase == BOAT_NET_CLIENT_PLAYING)
        check_wakes(c, now);
}

// The definition arrives once, before the checkpoint, on the same reliable ordered channel.
// Pages must be contiguous; the client never trusts the sender's counts or digest.
static void definition_fail(BoatNetClient *c, uint8_t reason)
{
    c->definition_error = reason;
    c->defining = false;
    c->phase = BOAT_NET_CLIENT_CLOSED;
    c->transport.disconnect(c->transport.context, 0); // Give the seat back now.
}

static void definition_page(BoatNetClient *c, const BoatNetWorldPage *p)
{
    const BoatNetDefinitionBegin *b = &c->definition_begin;
    if (!c->defining || p->checkpoint != b->definition || p->count != b->pages
        || p->index != c->definition_pages_received) {
        definition_fail(c, 1);
        return;
    }
    size_t offset = (size_t)p->index * BOAT_NET_PAGE_BYTES;
    size_t expected
        = b->bytes - offset < BOAT_NET_PAGE_BYTES ? b->bytes - offset : BOAT_NET_PAGE_BYTES;
    if (p->size != expected) {
        definition_fail(c, 1);
        return;
    }
    memcpy(c->definition_assembly + offset, p->data, p->size);
    c->definition_page_received[p->index] = true;
    ++c->definition_pages_received;
}

static void definition_end(BoatNetClient *c, uint32_t id)
{
    const BoatNetDefinitionBegin *b = &c->definition_begin;
    if (!c->defining || id != b->definition || c->definition_pages_received != b->pages
        || boat_net_bytes_digest(c->definition_assembly, b->bytes) != b->digest) {
        definition_fail(c, 1);
        return;
    }
    BoatRoomDefinition room;
    BoatRoomDiagnostic diagnostic;
    if (!boat_room_definition_decode(c->definition_assembly, b->bytes, &room, &diagnostic)) {
        definition_fail(c, 1);
        return;
    }
    c->room = room;
    c->room_valid = true;
    c->defining = false;
    c->room_digest = b->digest;
    c->stats.definition_bytes += b->bytes;
}

// Definition revisions (v14). An announcement is assembled like a definition but never fails the
// connection: one the client cannot accept simply goes unanswered, and the authority drops the
// client at the commit tick.
static void revision_begin(BoatNetClient *c, const BoatNetMessage *m)
{
    const BoatNetRevisionBegin *b = &m->revision_begin;
    if (b->revision <= c->revision || b->revision <= c->revision_seen) {
        ++c->stats.revisions_ignored; // Duplicate or stale.
        return;
    }
    c->revision_seen = b->revision;
    c->revision_begin = *b;
    c->revising = true;
    c->revision_staged = false; // A newer announcement supersedes a staged one.
    c->revision_pages_received = 0;
}

static void revision_page(BoatNetClient *c, const BoatNetWorldPage *p)
{
    const BoatNetRevisionBegin *b = &c->revision_begin;
    if (!c->revising || p->checkpoint != b->revision)
        return; // Not the revision being assembled.
    size_t offset = (size_t)p->index * BOAT_NET_PAGE_BYTES;
    size_t expected
        = b->bytes - offset < BOAT_NET_PAGE_BYTES ? b->bytes - offset : BOAT_NET_PAGE_BYTES;
    if (p->count != b->pages || p->index != c->revision_pages_received || p->size != expected) {
        c->revising = false;
        ++c->stats.revisions_invalid;
        return;
    }
    memcpy(c->revision_assembly + offset, p->data, p->size);
    ++c->revision_pages_received;
}

static void revision_end(BoatNetClient *c, uint32_t revision)
{
    const BoatNetRevisionBegin *b = &c->revision_begin;
    if (!c->revising || revision != b->revision)
        return;
    c->revising = false;
    static BoatRoomDefinition room;
    BoatRoomDiagnostic diagnostic;
    bool valid = c->revision_pages_received == b->pages
        && boat_net_bytes_digest(c->revision_assembly, b->bytes) == b->digest
        && boat_room_definition_decode(c->revision_assembly, b->bytes, &room, &diagnostic)
        && room.mode == c->accept.mode && room.location == c->accept.location
        && (c->phase != BOAT_NET_CLIENT_PLAYING || mirror_tick(c) < b->commit_tick);
    if (!valid) {
        ++c->stats.revisions_invalid;
        return;
    }
    c->revision_room = room;
    c->revision_digest = b->digest;
    c->revision_staged = true;
    BoatNetMessage ready = { .header = { .type = BOAT_NET_REVISION_READY, .tick = mirror_tick(c) },
        .checkpoint = b->revision };
    send_message(c, &ready);
}

// The staged definition becomes the room's. The fleets are not touched here.
static void revision_apply_room(BoatNetClient *c)
{
    c->room = c->revision_room;
    c->room_digest = c->revision_digest;
    c->revision = c->revision_begin.revision;
    c->revision_staged = false;
    ++c->stats.revisions;
}

// The mirror reached the commit tick: switch the room and rebuild what borrows or derives from
// it. Prediction history never crosses a commit: it restarts from the mirror, like a resync.
static void revision_commit(BoatNetClient *c)
{
    revision_apply_room(c);
    c->fleet.world.scene = c->room.has_scene ? &c->room.scene : NULL;
    if (c->room.mode == BOAT_ROOM_MODE_DEATHMATCH)
        c->fleet.state.combat.rules = c->room.combat_rules;
    memset(c->combat_sets, 0, sizeof(c->combat_sets));
    if (c->predict) {
        boat_prediction_reset(&c->prediction, &c->fleet, c->inputs, c->slot);
        boat_combat_bind(&c->prediction.fleet);
    }
    bind_race(c, c->race_go);
    c->predict_dirty = true;
}

static void page(BoatNetClient *c, const BoatNetWorldPage *p)
{
    const BoatNetWorldBegin *b = &c->begin;
    if (!c->assembling || p->checkpoint != b->checkpoint || p->count != b->pages
        || c->page_received[p->index])
        return;
    size_t offset = (size_t)p->index * BOAT_NET_PAGE_BYTES;
    size_t expected
        = b->bytes - offset < BOAT_NET_PAGE_BYTES ? b->bytes - offset : BOAT_NET_PAGE_BYTES;
    if (p->size != expected) {
        c->assembling = false;
        ++c->stats.bootstrap_failures;
        return;
    }
    memcpy(c->assembly + offset, p->data, p->size);
    c->page_received[p->index] = true;
    ++c->pages_received;
}

static void match_event(BoatNetClient *c, const BoatNetMessage *m, double now)
{
    const BoatNetMatchEvent *e = &m->match_event;
    BoatNetFeedEntry *entry = &c->feed[c->feed_count++ % BOAT_NET_MATCH_FEED];
    *entry = (BoatNetFeedEntry) { .event = *e, .tick = m->header.tick, .at = now };
    if (e->victim < BOAT_FLEET_SLOTS)
        copy_text(entry->victim, c->roster.slots[e->victim].name, BOAT_NET_NAME_BYTES);
    if (e->attacker < BOAT_FLEET_SLOTS)
        copy_text(entry->attacker, c->roster.slots[e->attacker].name, BOAT_NET_NAME_BYTES);
}

static void race_event(BoatNetClient *c, const BoatNetMessage *m, double now)
{
    const BoatNetRaceEvent *e = &m->race_event;
    BoatNetRaceFeedEntry *entry = &c->race_feed[c->race_feed_count++ % BOAT_NET_MATCH_FEED];
    *entry = (BoatNetRaceFeedEntry) { .event = *e, .tick = m->header.tick, .at = now };
    if (e->slot < BOAT_FLEET_SLOTS)
        copy_text(entry->name, c->roster.slots[e->slot].name, BOAT_NET_NAME_BYTES);
}

static void message(BoatNetClient *c, const BoatNetEvent *event, double now)
{
    static BoatNetMessage m;
    if (boat_net_decode(event->data, event->size, &m) != BOAT_NET_OK) {
        ++c->stats.decode_errors;
        return;
    }
    BoatNetChannel channel = boat_net_channel(m.header.type);
    if (channel != event->channel) {
        ++c->stats.decode_errors;
        return;
    }
    // Any valid message from this session proves the path is alive, even if it is a stale
    // snapshot or a birth the client cannot apply yet. Snapshot liveness is separate.
    if (c->phase >= BOAT_NET_CLIENT_BOOTSTRAP && m.header.epoch == c->epoch)
        c->heard_at = now;
    if (c->phase == BOAT_NET_CLIENT_HANDSHAKE && channel == BOAT_NET_BOOTSTRAP) {
        if (c->early_bootstrap_count < BOAT_NET_EARLY_BOOTSTRAP) {
            c->early_bootstrap[c->early_bootstrap_count++] = *event;
            ++c->stats.early_bootstrap;
        } else
            ++c->stats.bootstrap_failures;
        return;
    }
    if (channel == BOAT_NET_STATE || channel == BOAT_NET_BIRTHS) {
        if (c->received_any[channel]
            && !boat_net_sequence_newer(m.header.sequence, c->received[channel])) {
            ++c->stats.stale_packets;
            return;
        }
        c->received_any[channel] = true;
        c->received[channel] = m.header.sequence;
    }
    if (m.header.type == BOAT_NET_REJECT && c->phase == BOAT_NET_CLIENT_HANDSHAKE) {
        c->reject = m.reject;
        c->heard_at = now;
        c->phase = BOAT_NET_CLIENT_REJECTED;
        c->early_bootstrap_count = 0;
        return;
    }
    if (m.header.type == BOAT_NET_ACCEPT && c->phase == BOAT_NET_CLIENT_HANDSHAKE) {
        c->accept = m.accept;
        c->epoch = m.header.epoch;
        c->heard_at = now;
        c->slot = m.accept.slot;
        c->revision = m.accept.revision;
        c->phase = BOAT_NET_CLIENT_BOOTSTRAP;
        unsigned count = c->early_bootstrap_count;
        c->early_bootstrap_count = 0;
        for (unsigned i = 0; i < count; ++i)
            message(c, &c->early_bootstrap[i], now);
        return;
    }
    if (c->phase < BOAT_NET_CLIENT_BOOTSTRAP || m.header.epoch != c->epoch) {
        ++c->stats.stale_packets;
        return;
    }
    switch (m.header.type) {
    case BOAT_NET_ROSTER:
        c->roster = m.roster;
        break;
    case BOAT_NET_DEFINITION_BEGIN:
        if (c->room_valid) { // Immutable for the connection: fleets borrow its scene.
            definition_fail(c, 1);
            break;
        }
        c->definition_begin = m.definition_begin;
        c->defining = true;
        c->definition_pages_received = 0;
        memset(c->definition_page_received, 0, sizeof(c->definition_page_received));
        break;
    case BOAT_NET_DEFINITION_PAGE:
        definition_page(c, &m.world_page);
        break;
    case BOAT_NET_DEFINITION_END:
        definition_end(c, m.checkpoint);
        break;
    case BOAT_NET_REVISION_BEGIN:
        revision_begin(c, &m);
        break;
    case BOAT_NET_REVISION_PAGE:
        revision_page(c, &m.world_page);
        break;
    case BOAT_NET_REVISION_END:
        revision_end(c, m.checkpoint);
        break;
    case BOAT_NET_WORLD_BEGIN:
        c->begin = m.world_begin;
        c->assembling = true;
        c->pages_received = 0;
        memset(c->page_received, 0, sizeof(c->page_received));
        break;
    case BOAT_NET_WORLD_PAGE:
        page(c, &m.world_page);
        break;
    case BOAT_NET_WORLD_END:
        if (c->assembling && m.checkpoint == c->begin.checkpoint) {
            if (!c->room_valid || c->room.mode != c->accept.mode
                || c->room.location != c->accept.location)
                definition_fail(c, 1);
            else
                install(c, now);
        }
        break;
    case BOAT_NET_COMBAT_PAGE:
    case BOAT_NET_SNAPSHOT:
        if (c->phase == BOAT_NET_CLIENT_PLAYING) {
            if (c->accept.mode == BOAT_NET_MODE_DEATHMATCH)
                combat_snapshot(c, &m, now);
            else if (m.header.type == BOAT_NET_SNAPSHOT)
                snapshot(c, &m, now);
        }
        break;
    case BOAT_NET_WAKE_BIRTHS:
        births(c, &m.births, now);
        break;
    case BOAT_NET_PONG: {
        double sent = (double)m.clock * 1e-6;
        if (sent >= c->pong_after && sent <= now && now - sent < 10) {
            // Falls at once, rises slowly: a pong read late after a local stall (a window
            // opening, a slow frame) must not throw the clock estimate and lead far ahead.
            double rtt = now - sent;
            c->rtt = !c->stats.pongs || rtt < c->rtt ? rtt : c->rtt + (rtt - c->rtt) * .1;
            ++c->stats.pongs;
            unsigned lead = (unsigned)ceil(c->rtt * .5 * BOAT_TICK_HZ) + 4;
            c->lead = lead < 4 ? 4 : lead > 16 ? 16 : lead;
        }
        break;
    }
    case BOAT_NET_MATCH_STATE:
        c->match = m.match;
        c->match_valid = true;
        if (m.match.phase == BOAT_MATCH_INTERMISSION && m.match.round != c->result.round)
            c->result = m.match;
        break;
    case BOAT_NET_MATCH_EVENT:
        match_event(c, &m, now);
        break;
    case BOAT_NET_RACE_STATE:
        c->race = m.race;
        c->race_valid = true;
        // Results grow until the round's last finisher; keep the latest rows of the round.
        if (m.race.phase == BOAT_RACE_RESULTS)
            c->race_result = m.race;
        break;
    case BOAT_NET_RACE_EVENT:
        race_event(c, &m, now);
        break;
    case BOAT_NET_OBJECTIVE_STATE:
        if (!c->objective_valid
            || boat_net_sequence_newer(m.objective.revision, c->objective.revision)) {
            c->objective = m.objective;
            c->objective_valid = true;
        }
        break;
    case BOAT_NET_DISCONNECT:
        c->leave = m.leave;
        c->phase = BOAT_NET_CLIENT_CLOSED;
        break;
    default:
        ++c->stats.decode_errors; // The authority never sends the other types.
    }
}

const BoatFleetSlot *boat_net_client_own(const BoatNetClient *c)
{
    if (c->phase != BOAT_NET_CLIENT_PLAYING || !c->fleet.state.slots[c->slot].active)
        return NULL;
    return &c->fleet.state.slots[c->slot];
}

double boat_net_client_server_tick(const BoatNetClient *c, double now)
{
    return c->offset_valid ? now * BOAT_TICK_HZ + c->offset : (double)mirror_tick(c);
}

const BoatFleet *boat_net_client_view(const BoatNetClient *c)
{
    return c->predict ? &c->prediction.fleet : &c->fleet;
}

const BoatRoomDefinition *boat_net_client_room(const BoatNetClient *c)
{
    return c->room_valid ? &c->room : NULL;
}

uint32_t boat_net_client_revision(const BoatNetClient *c) { return c->revision; }

// One command per local tick, aimed at the estimated server tick plus the lead.
static void command(
    BoatNetClient *c, double now, BoatNetCommand next, float thrust_target, float steering_target)
{
    const BoatFleetSlot *own = boat_net_client_own(c);
    if (!own || !c->offset_valid)
        return;
    bool new_life = own->life != c->command_life;
    if (new_life) {
        next.flags &= (uint8_t)~BOAT_NET_COMMAND_GUN;
        next.grenade = 0;
        next.torpedo = 0;
        c->command_life = own->life;
        c->command_count = 0;
        c->command_tick = 0;
    }
    if (isfinite(thrust_target) && !(next.flags & BOAT_NET_COMMAND_INACTIVE)) {
        const BoatFleetSlot *predicted = &boat_net_client_view(c)->state.slots[c->slot];
        float target_thrust = new_life ? 0 : thrust_target;
        next.throttle
            = boat_net_command((BoatInput) { .throttle_rate
                                   = boat_throttle_rate(predicted->boat.throttle, target_thrust) },
                0)
                  .throttle;
    }
    if (isfinite(steering_target) && !(next.flags & BOAT_NET_COMMAND_INACTIVE)) {
        const Boat *boat = &boat_net_client_view(c)->state.slots[c->slot].boat;
        next.steering = boat_net_command(
            (BoatInput) {
                .steering_rate = boat_steering_rate(boat->wheel, new_life ? 0 : steering_target) },
            0)
                            .steering;
    }
    uint64_t target = (uint64_t)fmax(0, floor(now * BOAT_TICK_HZ + c->offset)) + c->lead;
    uint64_t tick = c->command_tick + 1;
    if (c->command_tick == 0 || target > tick + 2) {
        tick = target; // Start, or jump forward after the estimate moved.
    } else if (target + 3 < tick) {
        ++c->stats.skipped_commands; // Running ahead: let the authority catch up.
        return;
    }
    if (tick <= mirror_tick(c))
        tick = mirror_tick(c) + 1; // Finalized ticks take no commands.
    if (tick != c->command_tick + 1)
        c->command_count = 0; // A batch covers consecutive ticks only.
    if (c->command_count == BOAT_NET_MAX_COMMANDS)
        memmove(c->history, c->history + 1, sizeof(*c->history) * (BOAT_NET_MAX_COMMANDS - 1));
    else
        ++c->command_count;
    c->history[c->command_count - 1] = next;
    c->command_tick = tick;
    ++c->stats.commands;
    if (c->predict) {
        boat_prediction_record(&c->prediction, tick, own->life, next);
        boat_prediction_advance(&c->prediction, tick);
    }
    if (++c->commands_since_send < (c->accept.input_ticks ? c->accept.input_ticks : 2))
        return;
    c->commands_since_send = 0;
    BoatNetMessage m = { .header = { .type = BOAT_NET_INPUT_BATCH, .tick = tick },
        .input = { .life = own->life,
            .count = (uint8_t)c->command_count,
            .snapshot_tick = mirror_tick(c) } };
    memcpy(m.input.commands, c->history, sizeof(*c->history) * c->command_count);
    send_message(c, &m);
    ++c->stats.input_packets;
}

// Handle at most BOAT_NET_MESSAGES_PER_SERVICE events. False once rejected or closed.
static bool handle_events(BoatNetClient *c, double now)
{
    BoatNetEvent event;
    for (unsigned handled = 0;
        handled < BOAT_NET_MESSAGES_PER_SERVICE && boat_net_poll(&c->transport, &event);
        ++handled) {
        if (event.kind == BOAT_NET_EVENT_CONNECT && c->phase == BOAT_NET_CLIENT_CONNECTING) {
            BoatNetMessage m = { .header = { .type = BOAT_NET_HELLO },
                .hello = { .physics_revision = BOAT_PHYSICS_REVISION,
                    .content_digest = boat_net_content_digest() } };
            copy_text(m.hello.name, c->name, BOAT_NET_NAME_BYTES);
            copy_text(m.hello.token, c->token, BOAT_NET_TOKEN_BYTES);
            copy_text(m.hello.build, c->build, BOAT_NET_BUILD_BYTES);
            send_message(c, &m);
            c->phase = BOAT_NET_CLIENT_HANDSHAKE;
        } else if (event.kind == BOAT_NET_EVENT_DISCONNECT) {
            if (c->phase != BOAT_NET_CLIENT_REJECTED)
                c->phase = BOAT_NET_CLIENT_CLOSED;
        } else if (event.kind == BOAT_NET_EVENT_MESSAGE) {
            message(c, &event, now);
        }
        if (c->phase >= BOAT_NET_CLIENT_REJECTED)
            return false;
    }
    return c->phase < BOAT_NET_CLIENT_REJECTED;
}

static bool service(BoatNetClient *c, double now, BoatInput input, bool inactive,
    float thrust_target, float steering_target)
{
    if (now - c->serviced_at > .25)
        c->pong_after = now; // Loading a mapped view is not network round-trip time.
    c->serviced_at = now;
    uint32_t life_before
        = c->phase == BOAT_NET_CLIENT_PLAYING ? c->fleet.state.slots[c->slot].life : 0;
    if (!handle_events(c, now))
        return false;
    bool joining = c->phase < BOAT_NET_CLIENT_PLAYING;
    bool unresponsive = !joining && now - c->heard_at > BOAT_NET_TIMEOUT;
    bool silent = !joining && now - c->snapshot_at > BOAT_NET_SNAPSHOT_SILENCE;
    if (joining ? now - c->started_at > BOAT_NET_HANDSHAKE_TIMEOUT : (unresponsive || silent)) {
        c->timed_out = joining ? 1 : unresponsive ? 2 : 3;
        c->phase = BOAT_NET_CLIENT_CLOSED;
        c->transport.disconnect(c->transport.context, 0);
        return false;
    }
    if (c->phase >= BOAT_NET_CLIENT_BOOTSTRAP && now - c->ping_at >= .5) {
        c->ping_at = now;
        BoatNetMessage m = { .header = { .type = BOAT_NET_PING, .tick = mirror_tick(c) },
            .clock = (uint64_t)(now * 1e6) };
        send_message(c, &m);
        ++c->stats.pings;
    }
    if (c->phase != BOAT_NET_CLIENT_PLAYING)
        return true;
    bool wake_stalled = !c->wake_consistent && c->mismatch_since >= 0
        && now - c->mismatch_since > BOAT_NET_RESYNC_AFTER;
    // The authority is still talking (pongs, births, roster) but no authoritative fleet state
    // has applied. Ask for a reliable checkpoint before the hard snapshot-silence ceiling.
    bool snapshot_stalled = now - c->snapshot_at > BOAT_NET_RESYNC_AFTER
        && now - c->heard_at <= BOAT_NET_RESYNC_AFTER;
    if ((wake_stalled || snapshot_stalled) && now - c->resync_at > BOAT_NET_RESYNC_AFTER) {
        c->resync_at = now;
        ++c->stats.resyncs;
        if (snapshot_stalled && !wake_stalled)
            ++c->stats.snapshot_resyncs;
        BoatNetMessage m = { .header = { .type = BOAT_NET_RESYNC_REQUEST, .tick = mirror_tick(c) },
            .resync = { .reason = wake_stalled ? 1 : 2, .snapshot_tick = mirror_tick(c) } };
        send_message(c, &m);
    }
    if (c->predict && c->predict_dirty) {
        // Births travel at 10 Hz: hold a correction up to BOAT_NET_HOLD_INCOMPLETE for births
        // the snapshot counts rather than replay a wake set known to be incomplete.
        bool hold = c->mismatch_since >= 0 && now - c->mismatch_since < BOAT_NET_HOLD_INCOMPLETE;
        BoatPredictionWakes wakes = { c->wake_digest, c->pending, c->pending_count, hold };
        c->predict_dirty = !boat_prediction_correct(&c->prediction, &c->fleet, c->inputs, &wakes);
    }
    if (c->fleet.state.slots[c->slot].life != life_before) {
        thrust_target = isfinite(thrust_target) ? 0 : NAN;
        steering_target = isfinite(steering_target) ? 0 : NAN;
    }
    if (c->input_clock == 0 || now - c->input_clock > .25)
        c->input_clock = now; // Never replay a backlog of held keys after a stall.
    BoatNetCommand next = boat_net_command(input, inactive ? BOAT_NET_COMMAND_INACTIVE : 0);
    while (c->input_clock + BOAT_TICK_SECONDS <= now) {
        c->input_clock += BOAT_TICK_SECONDS;
        command(c, now, next, thrust_target, steering_target);
    }
    if (c->reset_request) {
        const BoatFleetSlot *own = &c->fleet.state.slots[c->slot];
        BoatNetMessage m = { .header = { .type = BOAT_NET_SPAWN_REQUEST, .tick = mirror_tick(c) },
            .spawn = { .request = c->reset_request, .life = own->life } };
        send_message(c, &m);
        c->reset_request = 0;
        if (own->active && !c->resetting) {
            c->resetting = true;
            c->reset_life = own->life;
            c->reset_at = now;
        }
    }
    return true;
}

bool boat_net_client_service(BoatNetClient *c, double now, BoatInput input, bool inactive)
{
    return service(c, now, input, inactive, NAN, NAN);
}

bool boat_net_client_service_thrust(
    BoatNetClient *c, double now, BoatInput input, bool inactive, float thrust_target)
{
    return service(c, now, input, inactive, thrust_target, NAN);
}

bool boat_net_client_service_targets(BoatNetClient *c, double now, BoatInput input, bool inactive,
    float thrust_target, float steering_target)
{
    return service(c, now, input, inactive, thrust_target, steering_target);
}

// AR6: the granted-tick clock of a private session, in seconds so the hosted thresholds keep their
// meaning (BOAT_NET_RESYNC_AFTER is 120 ticks, BOAT_NET_HOLD_INCOMPLETE 12).
static double local_clock(const BoatNetClient *c) { return (double)c->command_tick / BOAT_TICK_HZ; }

bool boat_net_client_command(
    BoatNetClient *c, uint64_t tick, BoatInput input, float thrust_target, float steering_target)
{
    const BoatFleetSlot *own = boat_net_client_own(c);
    if (!c->local.enabled || !c->predict || !own || tick <= mirror_tick(c)
        || (c->command_tick && tick != c->command_tick + 1)
        || c->prediction.fleet.state.tick + 1 != tick || c->command_count == BOAT_NET_MAX_COMMANDS)
        return false;
    // Solo's per-tick expressions, from the predicted boat the authority will step.
    const Boat *boat = &c->prediction.fleet.state.slots[c->slot].boat;
    if (isfinite(steering_target))
        input.steering_rate = boat_steering_rate(boat->wheel, steering_target);
    if (isfinite(thrust_target))
        input.throttle_rate = boat_throttle_rate(boat->throttle, thrust_target);
    BoatNetCommand next = boat_net_precise_command(input, 0);
    if (own->life != c->command_life)
        c->command_count = 0; // A batch steers one life.
    c->history[c->command_count++] = next;
    c->command_tick = tick;
    c->command_life = own->life;
    ++c->stats.commands;
    boat_prediction_record(&c->prediction, tick, own->life, next);
    boat_prediction_advance(&c->prediction, tick);
    return c->prediction.fleet.state.tick == tick;
}

bool boat_net_client_flush(BoatNetClient *c)
{
    if (!c->local.enabled || c->command_count > BOAT_NET_MAX_COMMANDS)
        return false;
    if (!c->command_count)
        return true;
    BoatNetHeader header = { .type = BOAT_NET_PRIVATE_INPUT_BATCH,
        .epoch = c->epoch,
        .sequence = c->sequence[BOAT_NET_STATE]++,
        .tick = c->command_tick };
    BoatNetInputBatch batch = {
        .life = c->command_life, .count = (uint8_t)c->command_count, .snapshot_tick = mirror_tick(c)
    };
    memcpy(batch.commands, c->history, sizeof(*c->history) * c->command_count);
    c->command_count = 0;
    uint8_t bytes[BOAT_NET_MAX_MESSAGE];
    size_t size = boat_net_private_encode_input(&header, &batch, bytes);
    if (!size || !boat_net_send(&c->transport, 0, BOAT_NET_STATE, bytes, size))
        return false;
    ++c->stats.input_packets;
    return true;
}

bool boat_net_client_local_service(BoatNetClient *c, double now)
{
    if (!c->local.enabled)
        return false;
    double clock = local_clock(c);
    if (!handle_events(c, clock))
        return false;
    if (c->phase < BOAT_NET_CLIENT_PLAYING) {
        // Startup is the one elapsed deadline; the host bounds it in service rounds too.
        if (now - c->started_at > BOAT_NET_HANDSHAKE_TIMEOUT) {
            c->timed_out = 1;
            c->phase = BOAT_NET_CLIENT_CLOSED;
            c->transport.disconnect(c->transport.context, 0);
            return false;
        }
        return true;
    }
    bool wake_stalled = !c->wake_consistent && c->mismatch_since >= 0
        && clock - c->mismatch_since > BOAT_NET_RESYNC_AFTER;
    bool snapshot_stalled = clock - c->snapshot_at > BOAT_NET_RESYNC_AFTER;
    if ((wake_stalled || snapshot_stalled) && clock - c->resync_at > BOAT_NET_RESYNC_AFTER) {
        c->resync_at = clock;
        ++c->stats.resyncs;
        if (snapshot_stalled && !wake_stalled)
            ++c->stats.snapshot_resyncs;
        BoatNetMessage m = { .header = { .type = BOAT_NET_RESYNC_REQUEST, .tick = mirror_tick(c) },
            .resync = { .reason = wake_stalled ? 1 : 2, .snapshot_tick = mirror_tick(c) } };
        send_message(c, &m);
    }
    if (c->predict_dirty) {
        bool hold = c->mismatch_since >= 0 && clock - c->mismatch_since < BOAT_NET_HOLD_INCOMPLETE;
        BoatPredictionWakes wakes = { c->wake_digest, c->pending, c->pending_count, hold };
        c->predict_dirty = !boat_prediction_correct(&c->prediction, &c->fleet, c->inputs, &wakes);
    }
    return true;
}

void boat_net_client_request_reset(BoatNetClient *c)
{
    c->reset_request = ++c->requests ? c->requests : ++c->requests;
}

BoatNetResetState boat_net_client_reset_state(const BoatNetClient *c, double now, double *seconds)
{
    if (seconds)
        *seconds = c->resetting ? now - c->reset_at : 0;
    if (!c->resetting)
        return BOAT_NET_RESET_NONE;
    return c->fleet.state.slots[c->slot].pending ? BOAT_NET_RESET_WAITING : BOAT_NET_RESET_SENT;
}

void boat_net_client_leave(BoatNetClient *c)
{
    if (c->phase >= BOAT_NET_CLIENT_HANDSHAKE && c->phase <= BOAT_NET_CLIENT_PLAYING) {
        BoatNetMessage m = { .header = { .type = BOAT_NET_DISCONNECT, .tick = mirror_tick(c) },
            .leave = BOAT_NET_LEAVE_QUIT };
        send_message(c, &m);
    }
    c->transport.disconnect(c->transport.context, 0);
    if (c->phase != BOAT_NET_CLIENT_REJECTED)
        c->phase = BOAT_NET_CLIENT_CLOSED;
}

bool boat_net_client_stale(const BoatNetClient *c, double now)
{
    return c->phase != BOAT_NET_CLIENT_PLAYING || now - c->snapshot_at > BOAT_NET_STALE;
}

const char *boat_net_client_failure(const BoatNetClient *c)
{
    if (c->phase == BOAT_NET_CLIENT_REJECTED)
        return boat_net_reject_text(c->reject.reason);
    if (c->definition_error == 1)
        return "the room definition was malformed or did not match its digest";
    if (c->definition_error == 2)
        return "the room definition and checkpoint combat rules differ";
    if (c->timed_out == 1)
        return "no answer within 10 s (is the room running and its network endpoint reachable?)";
    if (c->timed_out == 2)
        return "no message from the server for 5 s";
    if (c->timed_out == 3)
        return "the server is reachable but sent no authoritative state for 10 s";
    switch (c->leave) {
    case BOAT_NET_LEAVE_TIMEOUT:
        return "the server timed this client out";
    case BOAT_NET_LEAVE_SHUTDOWN:
        return "the server closed the room";
    case BOAT_NET_LEAVE_MISBEHAVING:
        return "the server dropped this client for invalid messages";
    case BOAT_NET_LEAVE_REVISION:
        return "the room changed its definition and this client was not ready in time";
    case BOAT_NET_LEAVE_QUIT:
        return "left the room";
    default:
        return c->phase == BOAT_NET_CLIENT_CLOSED ? "connection closed or timed out" : "";
    }
}
