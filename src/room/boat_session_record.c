#include "room/boat_session_record.h"

#include "core/boat_clock.h"

#include <math.h>
#include <string.h>

// Framing and payload scalars are big endian; embedded net and objective codecs retain their
// own versions and byte order. No C representation, pointer or padding is written.
typedef struct {
    uint8_t *p;
    size_t at, size;
    bool bad;
} Writer;
typedef struct {
    const uint8_t *p;
    size_t at, size;
    bool bad;
} Reader;
static void put(Writer *w, uint64_t n, unsigned count)
{
    if (count > w->size - w->at) {
        w->bad = true;
        return;
    }
    for (unsigned i = count; i; --i)
        w->p[w->at++] = (uint8_t)(n >> ((i - 1) * 8));
}
static uint64_t get(Reader *r, unsigned count)
{
    if (count > r->size - r->at) {
        r->bad = true;
        return 0;
    }
    uint64_t n = 0;
    for (unsigned i = 0; i < count; ++i)
        n = (n << 8) | r->p[r->at++];
    return n;
}
static void block(Writer *w, const void *p, size_t size)
{
    if (size > w->size - w->at) {
        w->bad = true;
        return;
    }
    memcpy(w->p + w->at, p, size);
    w->at += size;
}
static const uint8_t *take(Reader *r, size_t size)
{
    if (size > r->size - r->at) {
        r->bad = true;
        return NULL;
    }
    const uint8_t *p = r->p + r->at;
    r->at += size;
    return p;
}
static uint64_t go(const BoatNetServer *s)
{
    return s->race_room && s->race.round ? s->race.go_tick : 0;
}
static bool emit(BoatSessionRecorder *r, unsigned kind, uint64_t tick, Writer *payload)
{
    if (r->failed || payload->bad || r->sequence == UINT32_MAX)
        return !(r->failed = true);
    Writer h = { r->bytes, 0, BOAT_SESSION_RECORD_HEADER, false };
    block(&h, "P5GR", 4);
    put(&h, kind, 4);
    put(&h, payload->at, 4);
    put(&h, r->sequence++, 4);
    put(&h, tick, 8);
    uint64_t digest
        = boat_net_bytes_digest(r->bytes, 24) ^ boat_net_bytes_digest(payload->p, payload->at);
    put(&h, digest, 8);
    if (!r->sink(r->context, r->bytes, BOAT_SESSION_RECORD_HEADER + payload->at))
        r->failed = true;
    return !r->failed;
}
static Writer payload(BoatSessionRecorder *r)
{
    return (
        Writer) { r->bytes + BOAT_SESSION_RECORD_HEADER, 0, BOAT_SESSION_RECORD_BYTES_MAX, false };
}
static void write_fleet(Writer *w, const BoatNetServer *s)
{
    uint8_t bytes[BOAT_NET_MAX_CHECKPOINT];
    BoatNetSlotInput empty[BOAT_FLEET_SLOTS] = { 0 };
    size_t n = boat_net_checkpoint_encode(&s->fleet.state, empty, go(s), bytes, sizeof(bytes));
    if (!n) {
        w->bad = true;
        return;
    }
    put(w, n, 4);
    block(w, bytes, n);
    put(w, boat_fleet_hash(&s->fleet.state), 8);
}
static void write_objective(Writer *w, const BoatRoomObjective *o)
{
    put(w, o != NULL, 1);
    if (!o)
        return;
    const BoatRoomObjectiveSource *s = o->source;
    if (!s || !s->present || s->source_size > BOAT_PLUGIN_SOURCE_MAX) {
        w->bad = true;
        return;
    }
    put(w, s->state_version, 4);
    put(w, s->migrates, 1);
    put(w, s->source_size, 4);
    put(w, boat_net_bytes_digest(s->source, s->source_size), 8);
    block(w, s->source, s->source_size);
    // A definition callback straddles old/new marks: source provenance owns its own marks.
    static BoatRoomDefinition marks;
    marks = (BoatRoomDefinition) { .version = BOAT_ROOM_VERSION,
        .mode = BOAT_ROOM_MODE_FREE,
        .location = BOAT_LOCATION_OUTER,
        .marks = s->marks };
    memcpy(marks.id, "record.source", sizeof("record.source"));
    memcpy(marks.name, "Recorded source", sizeof("Recorded source"));
    uint8_t mark_bytes[BOAT_ROOM_BYTES_MAX];
    size_t mark_size = boat_room_definition_encode(&marks, mark_bytes, sizeof(mark_bytes));
    if (!mark_size) {
        w->bad = true;
        return;
    }
    put(w, mark_size, 4);
    block(w, mark_bytes, mark_size);
    size_t at = w->at;
    put(w, 0, 4);
    size_t n = boat_room_record_objective_encode(o, w->p + w->at, w->size - w->at);
    if (!n) {
        w->bad = true;
        return;
    }
    Writer length = { w->p + at, 0, 4, false };
    put(&length, n, 4);
    w->at += n;
}
static void write_hud(Writer *w, const BoatNetServer *s)
{
    put(w, s->has_objective, 1);
    if (!s->has_objective)
        return;
    BoatNetMessage m
        = { .header = { .type = BOAT_NET_OBJECTIVE_STATE }, .objective = s->objective };
    uint8_t bytes[BOAT_NET_MAX_MESSAGE];
    size_t n = boat_net_encode(&m, bytes);
    if (!n) {
        w->bad = true;
        return;
    }
    put(w, n, 4);
    block(w, bytes, n);
}
static void keyframe(BoatSessionRecorder *r, const BoatNetServer *s)
{
    Writer w = payload(r);
    put(&w, s->definition_bytes, 4);
    block(&w, s->definition, s->definition_bytes);
    put(&w, s->stats.revision, 4);
    write_fleet(&w, s);
    write_objective(&w, r->objective);
    write_hud(&w, s);
    emit(r, BOAT_SESSION_RECORD_KEYFRAME, s->fleet.state.tick, &w);
    r->key_tick = s->fleet.state.tick;
    r->revision = s->stats.revision;
    r->source_revision = r->objective ? r->objective->revision : 0;
    r->quarantined = r->objective && r->objective->objective.quarantined;
    r->fleet_hash = boat_fleet_hash(&s->fleet.state);
    r->race_go = go(s);
}
bool boat_session_record_begin(BoatSessionRecorder *r, const BoatNetServer *s,
    const BoatRoomObjective *o, BoatSessionRecordSink sink, void *context)
{
    memset(r, 0, sizeof(*r));
    r->sink = sink;
    r->context = context;
    r->objective = o;
    if (!sink || s->config.local.enabled)
        return false;
    Writer w = payload(r);
    put(&w, BOAT_SESSION_RECORD_VERSION, 4);
    put(&w, BOAT_NET_VERSION, 4);
    put(&w, BOAT_PHYSICS_REVISION, 4);
    put(&w, BOAT_TICK_HZ, 4);
    put(&w, boat_net_content_digest(), 8);
    put(&w, s->config.waves, 1);
    // Compiler/architecture identity is descriptive: bit identity is qualified on this build.
    const char build[] = PROTEUS_RECORD_BUILD;
    put(&w, sizeof(build), 4);
    block(&w, build, sizeof(build));
    if (!emit(r, BOAT_SESSION_RECORD_IDENTITY, s->fleet.state.tick, &w))
        return false;
    r->begun = true;
    keyframe(r, s);
    return !r->failed;
}
static void observe(
    void *context, const BoatNetServer *s, BoatNetRecordPhase phase, const BoatInput *inputs)
{
    BoatSessionRecorder *r = context;
    if (!r->begun || r->failed)
        return;
    Writer w = payload(r);
    uint64_t tick = s->fleet.state.tick;
    switch (phase) {
    case BOAT_NET_RECORD_INPUT:
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
            uint32_t f;
            memcpy(&f, &inputs[i].steering_rate, 4);
            put(&w, f, 4);
            memcpy(&f, &inputs[i].throttle_rate, 4);
            put(&w, f, 4);
            put(&w, inputs[i].gun, 1);
            put(&w, inputs[i].grenade, 2);
            put(&w, inputs[i].torpedo, 1);
        }
        emit(r, BOAT_SESSION_RECORD_INPUT, tick + 1, &w);
        break;
    case BOAT_NET_RECORD_RESULT:
        r->fleet_hash = boat_fleet_hash(&s->fleet.state);
        put(&w, r->fleet_hash, 8);
        emit(r, BOAT_SESSION_RECORD_RESULT, tick, &w);
        break;
    case BOAT_NET_RECORD_BEFORE_OBJECTIVE:
    case BOAT_NET_RECORD_AFTER_OBJECTIVE:
    case BOAT_NET_RECORD_BEFORE_DEFINITION:
    case BOAT_NET_RECORD_AFTER_DEFINITION:
        if (r->objective
            && (tick % BOAT_ROOM_OBJECTIVE_CADENCE == 0
                || phase == BOAT_NET_RECORD_BEFORE_DEFINITION
                || phase == BOAT_NET_RECORD_AFTER_DEFINITION)) {
            if (phase == BOAT_NET_RECORD_BEFORE_DEFINITION) {
                put(&w, s->definition_bytes, 4);
                block(&w, s->definition, s->definition_bytes);
            }
            write_objective(&w, r->objective);
            unsigned kind = phase == BOAT_NET_RECORD_BEFORE_OBJECTIVE
                ? BOAT_SESSION_RECORD_OBJECTIVE_BEFORE
                : phase == BOAT_NET_RECORD_AFTER_OBJECTIVE   ? BOAT_SESSION_RECORD_OBJECTIVE_AFTER
                : phase == BOAT_NET_RECORD_BEFORE_DEFINITION ? BOAT_SESSION_RECORD_DEFINITION_BEFORE
                                                             : BOAT_SESSION_RECORD_DEFINITION_AFTER;
            emit(r, kind, tick, &w);
            if (phase == BOAT_NET_RECORD_AFTER_OBJECTIVE
                || phase == BOAT_NET_RECORD_AFTER_DEFINITION)
                r->quarantined = r->objective->objective.quarantined;
        }
        break;
    case BOAT_NET_RECORD_BOUNDARY:
        // Event queue overflow can suspend between cadence boundaries, including just before
        // shutdown. It is a native host transition, not a Lua callback to diagnose.
        if (r->objective && r->quarantined != r->objective->objective.quarantined) {
            write_objective(&w, r->objective);
            emit(r, BOAT_SESSION_RECORD_OBJECTIVE_NATIVE, tick, &w);
            r->quarantined = r->objective->objective.quarantined;
            w = payload(r);
        }
        // Polling precedes a definition commit in the authority; preserve its mirrored HUD.
        write_hud(&w, s);
        {
            uint64_t digest = boat_net_bytes_digest(w.p, w.at);
            if (digest != r->objective_hash) {
                emit(r, BOAT_SESSION_RECORD_EVENT, tick, &w);
                r->objective_hash = digest;
            }
        }
        w = payload(r);
        {
            uint64_t hash = boat_fleet_hash(&s->fleet.state);
            if (hash != r->fleet_hash || go(s) != r->race_go) {
                write_fleet(&w, s);
                emit(r, BOAT_SESSION_RECORD_MUTATION, tick, &w);
                r->fleet_hash = hash;
                r->race_go = go(s);
            }
            if (r->revision != s->stats.revision
                || (r->objective && r->source_revision != r->objective->revision)
                || tick - r->key_tick >= BOAT_TICK_HZ * 10)
                keyframe(r, s);
        }
        break;
    }
}
static void event(void *context, uint64_t tick, const BoatNetRuleEvent *event)
{
    // Ordered crew/mark event records use a tag distinct from mirrored HUD updates.
    BoatSessionRecorder *r = context;
    if (!r->begun || r->failed)
        return;
    Writer w = payload(r);
    put(&w, 2, 1);
    put(&w, event->kind, 1);
    put(&w, event->slot, 1);
    put(&w, event->mark, 1);
    emit(r, BOAT_SESSION_RECORD_EVENT, tick, &w);
}
BoatNetRecord boat_session_record_adapter(BoatSessionRecorder *r)
{
    return (BoatNetRecord) { .context = r, .observe = observe, .event = event };
}
bool boat_session_record_end(BoatSessionRecorder *r, const BoatNetServer *s)
{
    if (!r->begun || r->failed)
        return false;
    observe(r, s, BOAT_NET_RECORD_BOUNDARY, NULL);
    keyframe(r, s);
    Writer w = payload(r);
    put(&w, boat_fleet_hash(&s->fleet.state), 8);
    bool ok = emit(r, BOAT_SESSION_RECORD_END, s->fleet.state.tick, &w);
    r->begun = false;
    return ok;
}
bool boat_session_record_decode(const uint8_t *bytes, size_t size, BoatSessionRecord *out)
{
    if (!bytes || size < BOAT_SESSION_RECORD_HEADER || memcmp(bytes, "P5GR", 4))
        return false;
    Reader r = { bytes, 4, size, false };
    BoatSessionRecord v = { .kind = (uint32_t)get(&r, 4) };
    v.size = (size_t)get(&r, 4);
    v.sequence = (uint32_t)get(&r, 4);
    v.tick = get(&r, 8);
    uint64_t digest = get(&r, 8);
    if (v.kind < BOAT_SESSION_RECORD_IDENTITY || v.kind > BOAT_SESSION_RECORD_END
        || v.size > BOAT_SESSION_RECORD_BYTES_MAX || v.size > size - BOAT_SESSION_RECORD_HEADER)
        return false;
    v.payload = bytes + BOAT_SESSION_RECORD_HEADER;
    v.bytes = BOAT_SESSION_RECORD_HEADER + v.size;
    if (digest != (boat_net_bytes_digest(bytes, 24) ^ boat_net_bytes_digest(v.payload, v.size)))
        return false;
    *out = v;
    return true;
}
static bool read_objective(Reader *r, BoatSessionPlayback *p, BoatRoomObjective *out)
{
    unsigned present = (unsigned)get(r, 1);
    if (present > 1)
        return false;
    if (!present) {
        memset(out, 0, sizeof(*out));
        return !r->bad;
    }
    static BoatRoomObjectiveSource source;
    source = (BoatRoomObjectiveSource) { .present = true, .marks = p->room.marks };
    source.state_version = (unsigned)get(r, 4);
    unsigned migrates = (unsigned)get(r, 1);
    source.migrates = migrates != 0;
    source.source_size = (size_t)get(r, 4);
    uint64_t source_hash = get(r, 8);
    if (migrates > 1 || source.source_size > BOAT_PLUGIN_SOURCE_MAX)
        return false;
    const uint8_t *text = take(r, source.source_size);
    if (!text || source_hash != boat_net_bytes_digest(text, source.source_size))
        return false;
    memcpy(source.source, text, source.source_size);
    size_t mark_size = (size_t)get(r, 4);
    const uint8_t *mark_bytes = take(r, mark_size);
    static BoatRoomDefinition marks;
    BoatRoomDiagnostic diagnostic;
    if (!mark_bytes || !boat_room_definition_decode(mark_bytes, mark_size, &marks, &diagnostic)
        || marks.mode != BOAT_ROOM_MODE_FREE)
        return false;
    source.marks = marks.marks;
    size_t n = (size_t)get(r, 4);
    const uint8_t *bytes = take(r, n);
    return bytes && boat_room_record_objective_decode(bytes, n, &source, out);
}
static bool read_hud(Reader *r, BoatSessionPlayback *p)
{
    unsigned present = (unsigned)get(r, 1);
    if (present > 1)
        return false;
    p->has_objective = present != 0;
    if (!present) {
        p->hud = (BoatNetObjectiveState) { 0 };
        return !r->bad;
    }
    size_t n = (size_t)get(r, 4);
    const uint8_t *bytes = take(r, n);
    BoatNetMessage message;
    if (!bytes || boat_net_decode(bytes, n, &message) != BOAT_NET_OK
        || message.header.type != BOAT_NET_OBJECTIVE_STATE)
        return false;
    p->hud = message.objective;
    return true;
}
static bool read_fleet(Reader *r, BoatFleetState *state, uint64_t *race_go)
{
    size_t n = (size_t)get(r, 4);
    const uint8_t *bytes = take(r, n);
    BoatNetSlotInput inputs[BOAT_FLEET_SLOTS];
    if (!bytes || boat_net_checkpoint_decode(bytes, n, state, inputs, race_go) != BOAT_NET_OK)
        return false;
    return get(r, 8) == boat_fleet_hash(state) && !r->bad;
}
static void bind(BoatSessionPlayback *p)
{
    p->fleet.world.scene = p->room.has_scene ? &p->room.scene : NULL;
    if (p->room.mode == BOAT_ROOM_MODE_RACE) {
        BoatRace race
            = { .course = p->room.course, .round = p->race_go != 0, .go_tick = p->race_go };
        boat_race_bind(&race, &p->fleet);
    }
}
static bool same_committed(const BoatPluginObjective *a, const BoatPluginObjective *b)
{
    if (boat_plugin_objective_digest(a) != boat_plugin_objective_digest(b)
        || a->quarantined != b->quarantined || a->command_count != b->command_count)
        return false;
    for (unsigned i = 0; i < a->command_count; ++i) {
        const BoatPluginCommand *x = &a->commands[i], *y = &b->commands[i];
        if (strcmp(x->kind, y->kind) || strcmp(x->id, y->id) || strcmp(x->label, y->label)
            || x->value != y->value || x->generation != y->generation)
            return false;
    }
    return true;
}

bool boat_session_record_apply(BoatSessionPlayback *p, const BoatSessionRecord *v, bool seek)
{
    if (p->ended
        || (v->sequence != p->sequence
            && !(seek && p->identity && !p->ready && v->kind == BOAT_SESSION_RECORD_KEYFRAME)))
        return false;
    Reader r = { v->payload, 0, v->size, false };
    bool ok = true;
    if (!p->identity && v->kind != BOAT_SESSION_RECORD_IDENTITY)
        return false;
    if (v->kind == BOAT_SESSION_RECORD_IDENTITY) {
        if (p->identity || v->sequence != 0)
            return false;
        ok = get(&r, 4) == BOAT_SESSION_RECORD_VERSION && get(&r, 4) == BOAT_NET_VERSION
            && get(&r, 4) == BOAT_PHYSICS_REVISION && get(&r, 4) == BOAT_TICK_HZ
            && get(&r, 8) == boat_net_content_digest();
        unsigned waves = (unsigned)get(&r, 1);
        p->waves = waves != 0;
        size_t n = (size_t)get(&r, 4);
        ok = ok && waves <= 1 && n > 0 && n <= 256 && take(&r, n);
        p->identity = ok;
    } else if (v->kind == BOAT_SESSION_RECORD_KEYFRAME) {
        if (p->awaiting_result)
            return false;
        static BoatRoomDefinition room;
        BoatRoomDiagnostic diagnostic;
        size_t n = (size_t)get(&r, 4);
        const uint8_t *bytes = take(&r, n);
        if (!bytes || !boat_room_definition_decode(bytes, n, &room, &diagnostic))
            return false;
        uint32_t revision = (uint32_t)get(&r, 4);
        static BoatFleetState state;
        uint64_t race_go;
        if (!revision || !read_fleet(&r, &state, &race_go) || state.tick != v->tick)
            return false;
        if (p->ready
            && (boat_fleet_hash(&p->fleet.state) != boat_fleet_hash(&state)
                || p->room.location != room.location || p->room.mode != room.mode
                || revision < p->revision || (uint64_t)revision > (uint64_t)p->revision + 1
                || (revision == p->revision
                    && (boat_room_definition_digest(&p->room) != boat_room_definition_digest(&room)
                        || race_go != p->race_go))))
            return false;
        if (!p->ready) {
            if (!boat_world_resources_init(&p->resources, boat_location(room.location)))
                return false;
            if (!boat_fleet_init_scene(&p->fleet, &p->resources,
                    boat_world_test_water(p->resources.definition, p->waves, false, false),
                    state.wakes.life_ticks, state.wakes.crest_height, NULL))
                return false;
        }
        bool verify_objective = p->ready;
        static BoatPluginObjective previous_objective;
        previous_objective = p->objective.objective;
        uint64_t previous_source_hash = p->objective.source
            ? boat_net_bytes_digest(p->objective.source->source, p->objective.source->source_size)
            : 0;
        unsigned previous_source_revision = p->objective.revision;
        BoatNetObjectiveState previous_hud = p->hud;
        p->room = room;
        p->revision = revision;
        p->race_go = race_go;
        boat_fleet_restore(&p->fleet, &state);
        bind(p);
        ok = read_objective(&r, p, &p->objective) && read_hud(&r, p);
        if (verify_objective
            && (!same_committed(&previous_objective, &p->objective.objective)
                || previous_source_hash
                    != (p->objective.source ? boat_net_bytes_digest(p->objective.source->source,
                                                  p->objective.source->source_size)
                                            : 0)
                || previous_source_revision != p->objective.revision
                || !boat_net_objective_state_equal(&previous_hud, &p->hud)))
            return false;
        p->ready = ok;
        ++p->keyframes;
    } else {
        if (!p->ready)
            return false;
        if (v->kind != BOAT_SESSION_RECORD_INPUT && v->tick != p->fleet.state.tick)
            return false;
        switch (v->kind) {
        case BOAT_SESSION_RECORD_INPUT: {
            if (p->awaiting_result || v->tick != p->fleet.state.tick + 1)
                return false;
            BoatInput inputs[BOAT_FLEET_SLOTS] = { 0 };
            for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
                uint32_t f = (uint32_t)get(&r, 4);
                memcpy(&inputs[i].steering_rate, &f, 4);
                f = (uint32_t)get(&r, 4);
                memcpy(&inputs[i].throttle_rate, &f, 4);
                unsigned gun = (unsigned)get(&r, 1);
                inputs[i].gun = gun != 0;
                inputs[i].grenade = (uint16_t)get(&r, 2);
                inputs[i].torpedo = (uint8_t)get(&r, 1);
                if (gun > 1 || !isfinite(inputs[i].steering_rate)
                    || !isfinite(inputs[i].throttle_rate) || fabsf(inputs[i].steering_rate) > 1
                    || fabsf(inputs[i].throttle_rate) > 1)
                    return false;
            }
            if (r.bad || r.at != r.size)
                return false;
            ok = boat_fleet_step(&p->fleet, inputs);
            p->awaiting_result = true;
            break;
        }
        case BOAT_SESSION_RECORD_RESULT:
            ok = p->awaiting_result && get(&r, 8) == boat_fleet_hash(&p->fleet.state);
            p->awaiting_result = false;
            break;
        case BOAT_SESSION_RECORD_MUTATION: {
            static BoatFleetState state;
            uint64_t race_go;
            if (p->awaiting_result || !read_fleet(&r, &state, &race_go) || state.tick != v->tick)
                return false;
            boat_fleet_restore(&p->fleet, &state);
            p->race_go = race_go;
            bind(p);
            break;
        }
        case BOAT_SESSION_RECORD_DEFINITION_BEFORE: {
            size_t n = (size_t)get(&r, 4);
            const uint8_t *bytes = take(&r, n);
            BoatRoomDiagnostic diagnostic;
            if (!bytes || !boat_room_definition_decode(bytes, n, &p->callback_room, &diagnostic)
                || p->callback_room.mode != p->room.mode
                || p->callback_room.location != p->room.location)
                return false;
            ok = !p->awaiting_result && read_objective(&r, p, &p->before);
            p->has_before = ok;
            break;
        }
        case BOAT_SESSION_RECORD_OBJECTIVE_BEFORE:
            ok = !p->awaiting_result && read_objective(&r, p, &p->before);
            p->has_before = ok;
            break;
        case BOAT_SESSION_RECORD_OBJECTIVE_AFTER:
        case BOAT_SESSION_RECORD_DEFINITION_AFTER:
            ok = p->has_before && read_objective(&r, p, &p->objective);
            p->has_before = false;
            break;
        case BOAT_SESSION_RECORD_OBJECTIVE_NATIVE:
            ok = !p->awaiting_result && read_objective(&r, p, &p->objective);
            break;
        case BOAT_SESSION_RECORD_EVENT:
            if (r.size && r.p[0] == 2) {
                get(&r, 1);
                unsigned kind = (unsigned)get(&r, 1), slot = (unsigned)get(&r, 1);
                unsigned mark = (unsigned)get(&r, 1);
                ok = kind <= BOAT_NET_RULE_MARK_REACHED && slot < BOAT_FLEET_SLOTS
                    && (kind != BOAT_NET_RULE_MARK_REACHED || mark < p->room.marks.count);
            } else
                ok = read_hud(&r, p);
            break;
        case BOAT_SESSION_RECORD_END:
            ok = !p->awaiting_result && get(&r, 8) == boat_fleet_hash(&p->fleet.state);
            p->ended = ok;
            break;
        default:
            return false;
        }
    }
    if (!ok || r.bad || r.at != r.size)
        return false;
    p->sequence = v->sequence + 1;
    ++p->records;
    return true;
}
void boat_session_record_close(BoatSessionPlayback *p)
{
    boat_world_resources_destroy(&p->resources);
    memset(p, 0, sizeof(*p));
}
