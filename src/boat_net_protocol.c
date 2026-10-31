#include "boat_net_protocol.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <string.h>

static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128 && sizeof(float) == 4,
    "the wire format carries IEEE-754 binary32 floats");
static_assert(BOAT_OBJECT_CAPACITY <= 255, "a pool slot travels as one byte");
static_assert(BOAT_NET_MAX_CHECKPOINT <= (size_t)BOAT_NET_PAGE_BYTES * 65535, "page count");

enum {
    SLOT_BYTES = 174,
    SNAPSHOT_BYTES = 40 + BOAT_FLEET_SLOTS * SLOT_BYTES,
    RECORD_BYTES = 36,
    BIRTH_BYTES = 4 + RECORD_BYTES,
    CHECKPOINT_BASE_BYTES = 48 + BOAT_FLEET_SLOTS * SLOT_BYTES
        + BOAT_OBJECT_CAPACITY * RECORD_BYTES,
    CHECKPOINT_BYTES = CHECKPOINT_BASE_BYTES + BOAT_COMBAT_BYTES,
    RACE_ROW_BYTES = 16,
};
static_assert(BOAT_NET_HEADER_BYTES + SNAPSHOT_BYTES <= 800, "snapshot target is 800 bytes");
static_assert(BOAT_NET_HEADER_BYTES + 4 + BOAT_NET_MAX_BIRTHS * BIRTH_BYTES <= BOAT_NET_MAX_MESSAGE,
    "a birth batch fits one message");
static_assert(BOAT_NET_HEADER_BYTES + 8 + BOAT_NET_PAGE_BYTES <= BOAT_NET_MAX_MESSAGE,
    "a page fits one message");
static_assert(CHECKPOINT_BYTES <= BOAT_NET_MAX_CHECKPOINT, "checkpoint fits the bootstrap cap");

// Safety envelope: generous physical limits, independent of tuning.
static const float SPEED_LIMIT = 1000, SPIN_LIMIT = 10000, ANGLE_LIMIT = 1e6f;
static const float HEIGHT_LIMIT = 1000, TIMER_LIMIT = 1e9f, LOAD_LIMIT = 10000;
static const float AMPLITUDE_LIMIT = 3.5f, LENGTH_LIMIT = 10000, SUM_LIMIT = 1e7f;

typedef struct {
    uint8_t *data;
    size_t size, capacity;
    bool overflow;
} Writer;

static void put8(Writer *w, uint8_t value)
{
    if (w->size >= w->capacity) {
        w->overflow = true;
        return;
    }
    w->data[w->size++] = value;
}

static void put16(Writer *w, uint16_t value)
{
    put8(w, (uint8_t)(value >> 8));
    put8(w, (uint8_t)value);
}

static void put32(Writer *w, uint32_t value)
{
    put16(w, (uint16_t)(value >> 16));
    put16(w, (uint16_t)value);
}

static void put64(Writer *w, uint64_t value)
{
    put32(w, (uint32_t)(value >> 32));
    put32(w, (uint32_t)value);
}

static void putf(Writer *w, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put32(w, bits);
}

static void put_bytes(Writer *w, const void *data, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        put8(w, ((const uint8_t *)data)[i]);
}

typedef struct {
    const uint8_t *data;
    size_t size, at;
    BoatNetError error; // The first failure sticks; later reads return zero.
} Reader;

static void fail(Reader *r, BoatNetError error)
{
    if (r->error == BOAT_NET_OK)
        r->error = error;
}

static uint8_t get8(Reader *r)
{
    if (r->error)
        return 0;
    if (r->at >= r->size) {
        fail(r, BOAT_NET_BAD_LENGTH);
        return 0;
    }
    return r->data[r->at++];
}

static uint16_t get16(Reader *r)
{
    uint16_t high = get8(r);
    return (uint16_t)(high << 8 | get8(r));
}

static uint32_t get32(Reader *r)
{
    uint32_t high = get16(r);
    return high << 16 | get16(r);
}

static uint64_t get64(Reader *r)
{
    uint64_t high = get32(r);
    return high << 32 | get32(r);
}

static float getf(Reader *r, float low, float high)
{
    uint32_t bits = get32(r);
    float value;
    memcpy(&value, &bits, sizeof(value));
    if (!isfinite(value)) {
        fail(r, BOAT_NET_NONFINITE);
        return 0;
    }
    if (value < low || value > high) {
        fail(r, BOAT_NET_OUT_OF_RANGE);
        return 0;
    }
    return value;
}

static void reserved(Reader *r, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i)
        if (get8(r))
            fail(r, BOAT_NET_BAD_FIELD);
}

static void expect(Reader *r, bool valid)
{
    if (!valid)
        fail(r, BOAT_NET_BAD_FIELD);
}

static int32_t get_i32(Reader *r)
{
    uint32_t bits = get32(r);
    return bits < UINT32_C(0x80000000) ? (int32_t)bits : (int32_t)((int64_t)bits - 4294967296);
}

static int8_t get_rate(Reader *r)
{
    uint8_t byte = get8(r);
    expect(r, byte != 0x80); // -128 is never encoded.
    return (int8_t)(byte < 0x80 ? byte : (int)byte - 256);
}

static uint8_t rate_byte(int8_t value) { return (uint8_t)(value < 0 ? value + 256 : value); }

// Length of TEXT, or LIMIT + 1 when it is longer than LIMIT.
static size_t bounded_length(const char *text, size_t limit)
{
    size_t length = 0;
    while (length <= limit && text[length])
        ++length;
    return length;
}

// Printable ASCII only; STORAGE receives a NUL-terminated copy.
static void put_string(Writer *w, const char *text, size_t limit)
{
    size_t length = bounded_length(text, limit);
    if (length > limit) {
        w->overflow = true;
        return;
    }
    put8(w, (uint8_t)length);
    put_bytes(w, text, length);
}

static bool printable(const char *text, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)text[i] < 0x20 || (unsigned char)text[i] > 0x7e)
            return false;
    return true;
}

static void get_string(Reader *r, char *storage, size_t limit)
{
    size_t length = get8(r);
    expect(r, length <= limit);
    if (r->error)
        return;
    for (size_t i = 0; i < length; ++i)
        storage[i] = (char)get8(r);
    storage[length] = 0;
    expect(r, printable(storage, length));
}

// Fixed-width name field: length byte plus BOAT_NET_NAME_BYTES zero-padded bytes.
static void put_name(Writer *w, const char *name)
{
    size_t length = bounded_length(name, BOAT_NET_NAME_BYTES);
    if (length > BOAT_NET_NAME_BYTES) {
        w->overflow = true;
        return;
    }
    put8(w, (uint8_t)length);
    put_bytes(w, name, length);
    for (size_t i = length; i < BOAT_NET_NAME_BYTES; ++i)
        put8(w, 0);
}

static void get_name(Reader *r, char *storage)
{
    size_t length = get8(r);
    expect(r, length <= BOAT_NET_NAME_BYTES);
    if (r->error)
        return;
    for (size_t i = 0; i < BOAT_NET_NAME_BYTES; ++i) {
        char c = (char)get8(r);
        if (i < length)
            storage[i] = c;
        else
            expect(r, c == 0);
    }
    storage[length] = 0;
    expect(r, printable(storage, length));
}

static void put_boat(Writer *w, const Boat *b)
{
    const float values[] = { b->position.x, b->position.y, b->velocity.x, b->velocity.y, b->heading,
        b->throttle, b->engine_response, b->planing, b->height, b->vertical_velocity, b->pitch,
        b->pitch_velocity, b->roll, b->roll_velocity, b->dry_time, b->landing_time,
        b->inverted_contact_time, b->turn_roll_load, b->wave_spray, b->wheel, b->engine_angle,
        b->yaw_velocity, b->physical_wake_time, b->physical_wake_start.x,
        b->physical_wake_start.y };
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); ++i)
        putf(w, values[i]);
}

static void get_boat(Reader *r, Boat *b)
{
    const float x = BOAT_NET_WORLD_LIMIT;
    const float z = BOAT_NET_WORLD_LIMIT;
    b->position.x = getf(r, -x, x);
    b->position.y = getf(r, -z, z);
    b->velocity.x = getf(r, -SPEED_LIMIT, SPEED_LIMIT);
    b->velocity.y = getf(r, -SPEED_LIMIT, SPEED_LIMIT);
    b->heading = getf(r, -ANGLE_LIMIT, ANGLE_LIMIT);
    b->throttle = getf(r, -1, 1);
    b->engine_response = getf(r, -1, 1);
    b->planing = getf(r, 0, 1);
    b->height = getf(r, -HEIGHT_LIMIT, HEIGHT_LIMIT);
    b->vertical_velocity = getf(r, -SPEED_LIMIT, SPEED_LIMIT);
    b->pitch = getf(r, -ANGLE_LIMIT, ANGLE_LIMIT);
    b->pitch_velocity = getf(r, -SPIN_LIMIT, SPIN_LIMIT);
    b->roll = getf(r, -ANGLE_LIMIT, ANGLE_LIMIT);
    b->roll_velocity = getf(r, -SPIN_LIMIT, SPIN_LIMIT);
    b->dry_time = getf(r, 0, TIMER_LIMIT);
    b->landing_time = getf(r, 0, TIMER_LIMIT);
    b->inverted_contact_time = getf(r, 0, TIMER_LIMIT);
    b->turn_roll_load = getf(r, -LOAD_LIMIT, LOAD_LIMIT);
    b->wave_spray = getf(r, 0, 1);
    b->wheel = getf(r, -1, 1);
    b->engine_angle = getf(r, -PI, PI);
    b->yaw_velocity = getf(r, -SPIN_LIMIT, SPIN_LIMIT);
    b->physical_wake_time = getf(r, 0, TIMER_LIMIT);
    b->physical_wake_start.x = getf(r, -ANGLE_LIMIT, ANGLE_LIMIT);
    b->physical_wake_start.y = getf(r, -ANGLE_LIMIT, ANGLE_LIMIT);
}

// One slot: lifecycle, input record, boat and its emitter continuation (SLOT_BYTES).
static void put_slot(Writer *w, const BoatFleetSlot *slot, const BoatWakeObjectEmitter *e,
    const BoatNetSlotInput *input)
{
    put8(w, (uint8_t)((slot->active ? 1 : 0) | (slot->pending ? 2 : 0) | (slot->held ? 4 : 0)));
    put8(w, (uint8_t)slot->boat.motion_state);
    put8(w, input->state);
    put8(w, 0);
    put32(w, slot->life);
    put32(w, slot->request);
    put64(w, slot->requested);
    put64(w, input->tick);
    put8(w, rate_byte(input->used.steering));
    put8(w, rate_byte(input->used.throttle));
    put8(w, input->used.flags);
    put16(w, input->used.grenade);
    put8(w, input->used.torpedo);
    put32(w, slot->boat.landings);
    put_boat(w, &slot->boat);
    putf(w, e->start.x);
    putf(w, e->start.y);
    put64(w, e->started);
    put64(w, e->tracked);
    put8(w, (uint8_t)e->next);
    put8(w, e->initialized ? 1 : 0);
    put16(w, 0);
    putf(w, e->strength);
    putf(w, e->strength_sum);
}

static void get_slot(Reader *r, uint64_t tick, BoatFleetSlot *slot, BoatWakeObjectEmitter *e,
    BoatNetSlotInput *input)
{
    const float x = BOAT_NET_WORLD_LIMIT;
    const float z = BOAT_NET_WORLD_LIMIT;
    *slot = (BoatFleetSlot) { 0 };
    *e = (BoatWakeObjectEmitter) { 0 };
    *input = (BoatNetSlotInput) { 0 };
    uint8_t flags = get8(r);
    expect(r, flags <= 7 && (flags & 5) != 4); // Only an active boat is held.
    slot->active = flags & 1;
    slot->pending = flags & 2;
    slot->held = flags & 4;
    uint8_t motion = get8(r);
    expect(r, motion <= BOAT_CAPSIZED);
    input->state = get8(r);
    expect(r, input->state < BOAT_NET_INPUT_STATES);
    reserved(r, 1);
    slot->life = get32(r);
    slot->request = get32(r);
    slot->requested = get64(r);
    expect(r, slot->requested <= tick);
    input->tick = get64(r);
    expect(r, input->tick <= tick);
    input->used.steering = get_rate(r);
    input->used.throttle = get_rate(r);
    input->used.flags = get8(r);
    input->used.grenade = get16(r);
    expect(r, (input->used.flags & ~BOAT_NET_COMMAND_MASK) == 0);
    input->used.torpedo = get8(r);
    slot->boat.landings = get32(r);
    get_boat(r, &slot->boat);
    slot->boat.motion_state = (BoatMotionState)(motion <= BOAT_CAPSIZED ? motion : 0);
    e->start.x = getf(r, -x, x);
    e->start.y = getf(r, -z, z);
    e->started = get64(r);
    e->tracked = get64(r);
    expect(r, e->started <= tick && e->tracked <= tick);
    e->next = get8(r);
    expect(r, e->next < BOAT_OBJECT_PER_BOAT);
    uint8_t initialized = get8(r);
    expect(r, initialized <= 1);
    e->initialized = initialized;
    reserved(r, 2);
    e->strength = getf(r, 0, AMPLITUDE_LIMIT);
    e->strength_sum = getf(r, 0, SUM_LIMIT);
}

static void put_record(Writer *w, const BoatWakeObject *o)
{
    putf(w, o->position.x);
    putf(w, o->position.y);
    putf(w, o->direction.x);
    putf(w, o->direction.y);
    putf(w, o->length);
    putf(w, o->amplitude);
    putf(w, o->inverse_speed);
    put64(w, o->born);
}

static void get_record(Reader *r, uint64_t tick, BoatWakeObject *o)
{
    const float x = BOAT_NET_WORLD_LIMIT;
    const float z = BOAT_NET_WORLD_LIMIT;
    o->position.x = getf(r, -x, x);
    o->position.y = getf(r, -z, z);
    o->direction.x = getf(r, -1.001f, 1.001f);
    o->direction.y = getf(r, -1.001f, 1.001f);
    o->length = getf(r, 0, LENGTH_LIMIT);
    o->amplitude = getf(r, 0, AMPLITUDE_LIMIT);
    o->inverse_speed = getf(r, 0, LENGTH_LIMIT);
    o->born = get64(r);
    expect(r, o->born <= tick);
}

static size_t encode_payload(const BoatNetMessage *m, Writer *w)
{
    switch (m->header.type) {
    case BOAT_NET_HELLO:
        put32(w, m->hello.physics_revision);
        put64(w, m->hello.content_digest);
        put_string(w, m->hello.name, BOAT_NET_NAME_BYTES);
        put_string(w, m->hello.token, BOAT_NET_TOKEN_BYTES);
        put_string(w, m->hello.build, BOAT_NET_BUILD_BYTES);
        break;
    case BOAT_NET_ACCEPT:
        put8(w, m->accept.slot);
        put8(w, m->accept.waves ? 1 : 0);
        put8(w, m->accept.snapshot_ticks);
        put8(w, m->accept.birth_ticks);
        put8(w, m->accept.input_ticks);
        put8(w, m->accept.max_commands);
        put8(w, m->accept.max_future);
        put8(w, m->accept.mode);
        put16(w, m->accept.tick_hz);
        put8(w, m->accept.location);
        put8(w, 0);
        put32(w, m->accept.wake_life_ticks);
        putf(w, m->accept.crest_height);
        break;
    case BOAT_NET_REJECT:
        put8(w, m->reject.reason);
        put8(w, 0);
        put16(w, m->reject.version);
        put32(w, m->reject.physics_revision);
        put64(w, m->reject.content_digest);
        break;
    case BOAT_NET_ROSTER:
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
            put8(w, m->roster.slots[i].state);
            put_name(w, m->roster.slots[i].name);
        }
        break;
    case BOAT_NET_WORLD_BEGIN:
        put32(w, m->world_begin.checkpoint);
        put32(w, m->world_begin.bytes);
        put16(w, m->world_begin.pages);
        put16(w, 0);
        put64(w, m->world_begin.digest);
        put64(w, m->world_begin.fleet_hash);
        break;
    case BOAT_NET_DEFINITION_BEGIN:
        put32(w, m->definition_begin.definition);
        put32(w, m->definition_begin.bytes);
        put16(w, m->definition_begin.pages);
        put16(w, 0);
        put64(w, m->definition_begin.digest);
        break;
    case BOAT_NET_COMBAT_PAGE:
    case BOAT_NET_DEFINITION_PAGE:
    case BOAT_NET_WORLD_PAGE:
        put32(w, m->world_page.checkpoint);
        put16(w, m->world_page.index);
        put16(w, m->world_page.count);
        if (m->world_page.size > BOAT_NET_PAGE_BYTES)
            w->overflow = true;
        else
            put_bytes(w, m->world_page.data, m->world_page.size);
        break;
    case BOAT_NET_WORLD_END:
    case BOAT_NET_READY:
    case BOAT_NET_DEFINITION_END:
        put32(w, m->checkpoint);
        put32(w, 0);
        break;
    case BOAT_NET_INPUT_BATCH:
        put32(w, m->input.life);
        put8(w, m->input.count);
        put8(w, 0);
        put16(w, 0);
        put64(w, m->input.snapshot_tick);
        for (unsigned i = 0; i < m->input.count && i < BOAT_NET_MAX_COMMANDS; ++i) {
            put8(w, rate_byte(m->input.commands[i].steering));
            put8(w, rate_byte(m->input.commands[i].throttle));
            put8(w, m->input.commands[i].flags);
            put16(w, m->input.commands[i].grenade);
            put8(w, m->input.commands[i].torpedo);
        }
        break;
    case BOAT_NET_SNAPSHOT: {
        const BoatNetSnapshot *s = &m->snapshot;
        put32(w, s->next_request);
        put16(w, s->live);
        put16(w, s->peak_live);
        put64(w, s->emitted);
        put64(w, s->expired);
        put64(w, s->wake_digest);
        put64(w, s->race_go);
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            put_slot(w, &s->slots[i], &s->emitters[i], &s->inputs[i]);
        break;
    }
    case BOAT_NET_WAKE_BIRTHS:
        put8(w, m->births.count);
        put8(w, 0);
        put16(w, 0);
        for (unsigned i = 0; i < m->births.count && i < BOAT_NET_MAX_BIRTHS; ++i) {
            put8(w, m->births.births[i].slot);
            put8(w, 0);
            put16(w, 0);
            put_record(w, &m->births.births[i].record);
        }
        break;
    case BOAT_NET_SPAWN_REQUEST:
        put32(w, m->spawn.request);
        put32(w, m->spawn.life);
        break;
    case BOAT_NET_PING:
    case BOAT_NET_PONG:
        put64(w, m->clock);
        break;
    case BOAT_NET_RESYNC_REQUEST:
        put8(w, m->resync.reason);
        put8(w, 0);
        put16(w, 0);
        put64(w, m->resync.snapshot_tick);
        break;
    case BOAT_NET_DISCONNECT:
        put8(w, m->leave);
        put8(w, 0);
        put16(w, 0);
        break;
    case BOAT_NET_MATCH_STATE: {
        const BoatNetMatchState *ms = &m->match;
        put8(w, ms->phase);
        put8(w, 0);
        put16(w, ms->respawn_ticks);
        put16(w, ms->skip_ticks);
        put16(w, 0);
        put32(w, ms->round);
        put32(w, ms->round_ticks);
        put64(w, ms->phase_end);
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
            const BoatMatchRow *row = &ms->slots[i].row;
            put8(w, ms->slots[i].present ? 1 : 0);
            put8(w, 0);
            put16(w, 0);
            put32(w, (uint32_t)row->score);
            put32(w, row->caused);
            put32(w, row->capsized);
            put32(w, row->self);
        }
        break;
    }
    case BOAT_NET_MATCH_EVENT: {
        const BoatNetMatchEvent *e = &m->match_event;
        put8(w, e->kind);
        put8(w, e->victim);
        put8(w, e->attacker);
        put8(w, e->cause);
        put8(w, e->bailout ? 1 : 0);
        put8(w, e->reason);
        put16(w, 0);
        put32(w, e->victim_life);
        put32(w, e->round);
        break;
    }
    case BOAT_NET_RACE_STATE: {
        const BoatNetRaceState *rs = &m->race;
        put8(w, rs->phase);
        put8(w, rs->placed);
        put16(w, 0);
        put16(w, rs->recovery_ticks);
        put16(w, rs->skip_ticks);
        put16(w, rs->park_ticks);
        put16(w, 0);
        put32(w, rs->round);
        put64(w, rs->phase_end);
        put64(w, rs->go_tick);
        put64(w, rs->deadline);
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
            put8(w,
                (uint8_t)((rs->slots[i].present ? 1 : 0) | (rs->slots[i].entrant ? 2 : 0)
                    | (rs->slots[i].current ? 4 : 0) | (rs->slots[i].parked ? 8 : 0)));
            put8(w, rs->slots[i].status);
            put8(w, rs->slots[i].place);
            put8(w, rs->slots[i].gates);
            put32(w, rs->slots[i].recoveries);
            put64(w, rs->slots[i].finish);
        }
        break;
    }
    case BOAT_NET_RACE_EVENT: {
        const BoatNetRaceEvent *e = &m->race_event;
        put8(w, e->kind);
        put8(w, e->slot);
        put8(w, e->value);
        put8(w, 0);
        put32(w, e->round);
        put64(w, e->time);
        break;
    }
    default:
        w->overflow = true;
    }
    return w->size;
}

static void put_header(Writer *w, const BoatNetHeader *h, size_t payload)
{
    put32(w, BOAT_NET_MAGIC);
    put16(w, BOAT_NET_VERSION);
    put8(w, h->type);
    put8(w, 0);
    put32(w, h->epoch);
    put32(w, h->sequence);
    put64(w, h->tick);
    put16(w, (uint16_t)payload);
    put16(w, 0);
    put32(w, 0);
}

size_t boat_net_encode(const BoatNetMessage *message, uint8_t out[BOAT_NET_MAX_MESSAGE])
{
    uint8_t payload[BOAT_NET_MAX_PAYLOAD];
    Writer body = { payload, 0, sizeof(payload), false };
    size_t size = encode_payload(message, &body);
    if (body.overflow)
        return 0;
    Writer w = { out, 0, BOAT_NET_MAX_MESSAGE, false };
    put_header(&w, &message->header, size);
    put_bytes(&w, payload, size);
    if (w.overflow)
        return 0;
    // The encoder only emits what its own decoder accepts.
    static BoatNetMessage check; // Large; no recursion or concurrency in this codec.
    if (boat_net_decode(out, w.size, &check) != BOAT_NET_OK)
        return 0;
    return w.size;
}

BoatNetError boat_net_decode_header(const uint8_t *data, size_t size, BoatNetHeader *header)
{
    if (!data || size < BOAT_NET_HEADER_BYTES)
        return BOAT_NET_TRUNCATED;
    Reader r = { data, BOAT_NET_HEADER_BYTES, 0, BOAT_NET_OK };
    if (get32(&r) != BOAT_NET_MAGIC)
        return BOAT_NET_BAD_MAGIC;
    uint16_t version = get16(&r);
    BoatNetHeader h = { 0 };
    h.type = get8(&r);
    h.flags = get8(&r);
    h.epoch = get32(&r);
    h.sequence = get32(&r);
    h.tick = get64(&r);
    h.payload = get16(&r);
    uint16_t reserved16 = get16(&r);
    uint32_t reserved32 = get32(&r);
    if (version != BOAT_NET_VERSION) {
        *header = h;
        return BOAT_NET_BAD_VERSION;
    }
    if (h.flags || reserved16 || reserved32 || h.payload > BOAT_NET_MAX_PAYLOAD)
        return BOAT_NET_BAD_HEADER;
    if (h.type == 0 || h.type >= BOAT_NET_TYPE_LIMIT)
        return BOAT_NET_BAD_TYPE;
    if (size < BOAT_NET_HEADER_BYTES + (size_t)h.payload)
        return BOAT_NET_TRUNCATED;
    if (size > BOAT_NET_HEADER_BYTES + (size_t)h.payload)
        return BOAT_NET_BAD_LENGTH;
    *header = h;
    return BOAT_NET_OK;
}

static void decode_payload(Reader *r, BoatNetMessage *m)
{
    uint64_t tick = m->header.tick;
    switch (m->header.type) {
    case BOAT_NET_HELLO:
        m->hello.physics_revision = get32(r);
        m->hello.content_digest = get64(r);
        get_string(r, m->hello.name, BOAT_NET_NAME_BYTES);
        get_string(r, m->hello.token, BOAT_NET_TOKEN_BYTES);
        get_string(r, m->hello.build, BOAT_NET_BUILD_BYTES);
        break;
    case BOAT_NET_ACCEPT: {
        BoatNetAccept *a = &m->accept;
        a->slot = get8(r);
        expect(r, a->slot < BOAT_FLEET_SLOTS);
        uint8_t waves = get8(r);
        expect(r, waves <= 1);
        a->waves = waves;
        a->snapshot_ticks = get8(r);
        a->birth_ticks = get8(r);
        a->input_ticks = get8(r);
        a->max_commands = get8(r);
        a->max_future = get8(r);
        expect(r, a->snapshot_ticks && a->birth_ticks && a->input_ticks);
        expect(r, a->max_commands && a->max_commands <= BOAT_NET_MAX_COMMANDS);
        a->mode = get8(r);
        expect(r, a->mode < BOAT_NET_MODE_LIMIT);
        a->tick_hz = get16(r);
        expect(r, a->tick_hz == BOAT_TICK_HZ);
        a->location = get8(r);
        expect(r, a->location < BOAT_LOCATION_COUNT);
        // The river sprint is the only race course.
        expect(r, a->mode != BOAT_NET_MODE_RACE || a->location == BOAT_LOCATION_RIVER);
        reserved(r, 1);
        a->wake_life_ticks = get32(r);
        expect(r, a->wake_life_ticks >= 240 && a->wake_life_ticks <= BOAT_OBJECT_LIFE_TICKS);
        a->crest_height = getf(r, .1f, 3);
        break;
    }
    case BOAT_NET_REJECT:
        m->reject.reason = get8(r);
        expect(r, m->reject.reason && m->reject.reason < BOAT_NET_REJECT_LIMIT);
        reserved(r, 1);
        m->reject.version = get16(r);
        m->reject.physics_revision = get32(r);
        m->reject.content_digest = get64(r);
        break;
    case BOAT_NET_ROSTER:
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
            m->roster.slots[i].state = get8(r);
            expect(r, m->roster.slots[i].state <= BOAT_NET_ROSTER_PLAYING);
            get_name(r, m->roster.slots[i].name);
        }
        break;
    case BOAT_NET_WORLD_BEGIN: {
        BoatNetWorldBegin *b = &m->world_begin;
        b->checkpoint = get32(r);
        b->bytes = get32(r);
        b->pages = get16(r);
        reserved(r, 2);
        b->digest = get64(r);
        b->fleet_hash = get64(r);
        expect(r, b->bytes && b->bytes <= BOAT_NET_MAX_CHECKPOINT);
        expect(r, b->pages == (b->bytes + BOAT_NET_PAGE_BYTES - 1) / BOAT_NET_PAGE_BYTES);
        break;
    }
    case BOAT_NET_DEFINITION_BEGIN: {
        BoatNetDefinitionBegin *b = &m->definition_begin;
        b->definition = get32(r);
        b->bytes = get32(r);
        b->pages = get16(r);
        reserved(r, 2);
        b->digest = get64(r);
        expect(r, b->bytes && b->bytes <= BOAT_NET_MAX_DEFINITION);
        expect(r, b->pages == (b->bytes + BOAT_NET_PAGE_BYTES - 1) / BOAT_NET_PAGE_BYTES);
        break;
    }
    case BOAT_NET_COMBAT_PAGE:
    case BOAT_NET_DEFINITION_PAGE:
    case BOAT_NET_WORLD_PAGE: {
        BoatNetWorldPage *p = &m->world_page;
        p->checkpoint = get32(r);
        p->index = get16(r);
        p->count = get16(r);
        expect(r, p->index < p->count);
        expect(r, p->count <= BOAT_NET_MAX_CHECKPOINT / BOAT_NET_PAGE_BYTES);
        if (m->header.type == BOAT_NET_COMBAT_PAGE)
            expect(r,
                p->count >= 1 && p->count <= 3 && p->index < p->count
                    && p->checkpoint == (uint32_t)m->header.tick);

        size_t size = r->size - r->at;
        expect(r, size > 0 && size <= BOAT_NET_PAGE_BYTES);
        if (r->error)
            break;
        p->size = (uint16_t)size;
        if (m->header.type == BOAT_NET_COMBAT_PAGE && p->index + 1 < p->count)
            expect(r, size == BOAT_NET_PAGE_BYTES);

        for (size_t i = 0; i < size; ++i)
            p->data[i] = get8(r);
        break;
    }
    case BOAT_NET_WORLD_END:
    case BOAT_NET_READY:
    case BOAT_NET_DEFINITION_END:
        m->checkpoint = get32(r);
        reserved(r, 4);
        break;
    case BOAT_NET_INPUT_BATCH: {
        BoatNetInputBatch *b = &m->input;
        b->life = get32(r);
        b->count = get8(r);
        expect(r, b->count >= 1 && b->count <= BOAT_NET_MAX_COMMANDS);
        expect(r, (uint64_t)b->count <= tick);
        reserved(r, 3);
        b->snapshot_tick = get64(r);
        if (r->error)
            break;
        for (unsigned i = 0; i < b->count; ++i) {
            b->commands[i].steering = get_rate(r);
            b->commands[i].throttle = get_rate(r);
            b->commands[i].flags = get8(r);
            b->commands[i].grenade = get16(r);
            expect(r, (b->commands[i].flags & ~BOAT_NET_COMMAND_MASK) == 0);
            b->commands[i].torpedo = get8(r);
        }
        break;
    }
    case BOAT_NET_SNAPSHOT: {
        BoatNetSnapshot *s = &m->snapshot;
        s->next_request = get32(r);
        s->live = get16(r);
        s->peak_live = get16(r);
        expect(r, s->live <= BOAT_OBJECT_CAPACITY && s->peak_live <= BOAT_OBJECT_CAPACITY);
        s->emitted = get64(r);
        s->expired = get64(r);
        expect(r, s->expired <= s->emitted);
        s->wake_digest = get64(r);
        s->race_go = get64(r);
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            get_slot(r, tick, &s->slots[i], &s->emitters[i], &s->inputs[i]);
        break;
    }
    case BOAT_NET_WAKE_BIRTHS:
        m->births.count = get8(r);
        expect(r, m->births.count <= BOAT_NET_MAX_BIRTHS);
        reserved(r, 3);
        if (r->error)
            break;
        for (unsigned i = 0; i < m->births.count; ++i) {
            m->births.births[i].slot = get8(r);
            expect(r, m->births.births[i].slot < BOAT_OBJECT_CAPACITY);
            reserved(r, 3);
            get_record(r, tick, &m->births.births[i].record);
        }
        break;
    case BOAT_NET_SPAWN_REQUEST:
        m->spawn.request = get32(r);
        m->spawn.life = get32(r);
        break;
    case BOAT_NET_PING:
    case BOAT_NET_PONG:
        m->clock = get64(r);
        break;
    case BOAT_NET_RESYNC_REQUEST:
        m->resync.reason = get8(r);
        expect(r, m->resync.reason == 1 || m->resync.reason == 2);
        reserved(r, 3);
        m->resync.snapshot_tick = get64(r);
        break;
    case BOAT_NET_DISCONNECT:
        m->leave = get8(r);
        expect(r, m->leave && m->leave < BOAT_NET_LEAVE_LIMIT);
        reserved(r, 3);
        break;
    case BOAT_NET_MATCH_STATE: {
        BoatNetMatchState *ms = &m->match;
        ms->phase = get8(r);
        expect(r, ms->phase <= BOAT_MATCH_INTERMISSION);
        reserved(r, 1);
        ms->respawn_ticks = get16(r);
        ms->skip_ticks = get16(r);
        expect(r, ms->skip_ticks <= ms->respawn_ticks);
        reserved(r, 2);
        ms->round = get32(r);
        ms->round_ticks = get32(r);
        expect(r, ms->round_ticks > 0);
        ms->phase_end = get64(r);
        expect(r, (ms->phase_end == 0) == (ms->phase == BOAT_MATCH_WARMUP));
        expect(r, ms->phase == BOAT_MATCH_WARMUP || ms->round > 0);
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
            BoatMatchRow *row = &ms->slots[i].row;
            uint8_t present = get8(r);
            expect(r, present <= 1);
            ms->slots[i].present = present;
            reserved(r, 3);
            row->score = get_i32(r);
            row->caused = get32(r);
            row->capsized = get32(r);
            row->self = get32(r);
            expect(r, row->self <= row->capsized);
            expect(r, present || (!row->score && !row->caused && !row->capsized));
        }
        break;
    }
    case BOAT_NET_MATCH_EVENT: {
        BoatNetMatchEvent *e = &m->match_event;
        e->kind = get8(r);
        e->victim = get8(r);
        e->attacker = get8(r);
        e->cause = get8(r);
        uint8_t flags = get8(r);
        expect(r, flags <= 1);
        e->bailout = flags;
        e->reason = get8(r);
        expect(r, e->reason <= BOAT_MATCH_DEFEAT_HULL);
        reserved(r, 2);
        e->victim_life = get32(r);
        e->round = get32(r);
        expect(r, e->round > 0);
        if (e->kind == BOAT_MATCH_CAPSIZE) {
            bool self = e->attacker == BOAT_MATCH_NOBODY;
            expect(r, e->victim < BOAT_FLEET_SLOTS && e->attacker != e->victim);
            expect(r, self || e->attacker < BOAT_FLEET_SLOTS);
            expect(r,
                self ? e->cause == BOAT_MATCH_CAUSE_NONE && !e->bailout
                     : e->cause >= BOAT_MATCH_CONTACT && e->cause <= BOAT_MATCH_TORPEDO);
        } else {
            expect(r, e->kind == BOAT_MATCH_ROUND_START || e->kind == BOAT_MATCH_ROUND_END);
            expect(r, e->victim == BOAT_MATCH_NOBODY && e->attacker == BOAT_MATCH_NOBODY);
            expect(r,
                e->cause == BOAT_MATCH_CAUSE_NONE && !e->bailout && !e->victim_life
                    && e->reason == 0);
        }
        break;
    }
    case BOAT_NET_RACE_STATE: {
        BoatNetRaceState *rs = &m->race;
        rs->phase = get8(r);
        expect(r, rs->phase <= BOAT_RACE_RESULTS);
        rs->placed = get8(r);
        expect(r, rs->placed <= BOAT_FLEET_SLOTS);
        reserved(r, 2);
        rs->recovery_ticks = get16(r);
        rs->skip_ticks = get16(r);
        expect(r, rs->skip_ticks <= rs->recovery_ticks);
        rs->park_ticks = get16(r);
        reserved(r, 2);
        rs->round = get32(r);
        rs->phase_end = get64(r);
        rs->go_tick = get64(r);
        rs->deadline = get64(r);
        bool timed = rs->phase == BOAT_RACE_GRID || rs->phase == BOAT_RACE_COUNTDOWN
            || rs->phase == BOAT_RACE_RESULTS;
        expect(r, (rs->phase_end != 0) == timed);
        expect(r, (rs->deadline != 0) == (rs->phase == BOAT_RACE_RACING));
        expect(r, (rs->round == 0) == (rs->go_tick == 0));
        expect(r, rs->phase == BOAT_RACE_WAITING || rs->round > 0);
        unsigned finished = 0;
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
            uint8_t flags = get8(r);
            expect(r, flags <= 15);
            rs->slots[i].present = flags & 1;
            rs->slots[i].entrant = flags & 2;
            rs->slots[i].current = flags & 4;
            rs->slots[i].parked = flags & 8;
            rs->slots[i].status = get8(r);
            rs->slots[i].place = get8(r);
            rs->slots[i].gates = get8(r);
            rs->slots[i].recoveries = get32(r);
            rs->slots[i].finish = get64(r);
            bool done = rs->slots[i].status == BOAT_RACE_FINISHED;
            finished += done;
            expect(r, rs->slots[i].status <= BOAT_RACE_DNF);
            expect(r, rs->slots[i].gates <= BOAT_RACE_GATES);
            expect(r, (rs->slots[i].place != 0) == done && rs->slots[i].place <= rs->placed);
            expect(r, done || !rs->slots[i].finish);
            expect(r, !rs->slots[i].current || (rs->slots[i].present && rs->slots[i].entrant));
            expect(r, !rs->slots[i].parked || (rs->slots[i].current && done));
            expect(r,
                rs->slots[i].entrant
                    || (flags <= 1 && !rs->slots[i].status && !rs->slots[i].gates
                        && !rs->slots[i].recoveries));
        }
        expect(r, finished == rs->placed);
        break;
    }
    case BOAT_NET_RACE_EVENT: {
        BoatNetRaceEvent *e = &m->race_event;
        e->kind = get8(r);
        e->slot = get8(r);
        e->value = get8(r);
        reserved(r, 1);
        e->round = get32(r);
        e->time = get64(r);
        expect(r, e->round > 0 && e->kind <= BOAT_RACE_PARK);
        bool nobody = e->kind == BOAT_RACE_PHASE || e->kind == BOAT_RACE_ABANDON;
        expect(r, nobody ? e->slot == BOAT_RACE_NOBODY : e->slot < BOAT_FLEET_SLOTS);
        expect(r, (e->time != 0) <= (e->kind == BOAT_RACE_FINISH));
        if (e->kind == BOAT_RACE_PHASE)
            expect(r, e->value <= BOAT_RACE_RESULTS);
        else if (e->kind == BOAT_RACE_GATE)
            expect(r, e->value < BOAT_RACE_GATES);
        else if (e->kind == BOAT_RACE_FINISH)
            expect(r, e->value >= 1 && e->value <= BOAT_FLEET_SLOTS);
        else
            expect(r, e->value == 0);
        break;
    }
    default:
        fail(r, BOAT_NET_BAD_TYPE);
    }
    if (r->at != r->size)
        fail(r, BOAT_NET_BAD_LENGTH);
}

BoatNetError boat_net_decode(const uint8_t *data, size_t size, BoatNetMessage *message)
{
    static BoatNetMessage decoded; // Never written to MESSAGE unless valid.
    decoded = (BoatNetMessage) { 0 };
    BoatNetError error = boat_net_decode_header(data, size, &decoded.header);
    if (error)
        return error;
    Reader r = { data + BOAT_NET_HEADER_BYTES, decoded.header.payload, 0, BOAT_NET_OK };
    decode_payload(&r, &decoded);
    if (r.error)
        return r.error;
    *message = decoded;
    return BOAT_NET_OK;
}

const char *boat_net_error_name(BoatNetError error)
{
    static const char *names[] = { "ok", "truncated", "bad magic", "protocol version mismatch",
        "bad header", "unknown type", "bad length", "invalid field", "non-finite value",
        "value out of range" };
    return (unsigned)error < sizeof(names) / sizeof(*names) ? names[error] : "unknown error";
}

const char *boat_net_reject_text(uint8_t reason)
{
    switch (reason) {
    case BOAT_NET_REJECT_VERSION:
        return "protocol version mismatch";
    case BOAT_NET_REJECT_PHYSICS:
        return "handling revision mismatch";
    case BOAT_NET_REJECT_CONTENT:
        return "map/content mismatch";
    case BOAT_NET_REJECT_TOKEN:
        return "wrong room token";
    case BOAT_NET_REJECT_FULL:
        return "room is full";
    case BOAT_NET_REJECT_MALFORMED:
        return "malformed handshake";
    default:
        return "unknown reason";
    }
}

BoatNetChannel boat_net_channel(uint8_t type)
{
    switch (type) {
    case BOAT_NET_COMBAT_PAGE:
        return BOAT_NET_REGISTRY;
    case BOAT_NET_INPUT_BATCH:
    case BOAT_NET_SNAPSHOT:
    case BOAT_NET_PING:
    case BOAT_NET_PONG:
        return BOAT_NET_STATE;
    case BOAT_NET_WAKE_BIRTHS:
        return BOAT_NET_BIRTHS;
    case BOAT_NET_WORLD_BEGIN:
    case BOAT_NET_WORLD_PAGE:
    case BOAT_NET_WORLD_END:
    case BOAT_NET_DEFINITION_BEGIN:
    case BOAT_NET_DEFINITION_PAGE:
    case BOAT_NET_DEFINITION_END:
        return BOAT_NET_BOOTSTRAP;
    default:
        return BOAT_NET_CONTROL;
    }
}

bool boat_net_sequence_newer(uint32_t a, uint32_t b)
{
    uint32_t difference = a - b;
    return difference != 0 && difference < UINT32_C(0x80000000);
}

static int8_t rate(float value)
{
    if (!isfinite(value))
        return 0;
    return (int8_t)lroundf(fmaxf(-1, fminf(1, value)) * 127);
}

BoatNetCommand boat_net_command(BoatInput input, uint8_t flags)
{
    return (BoatNetCommand) { rate(input.steering_rate), rate(input.throttle_rate),
        (uint8_t)((flags & BOAT_NET_COMMAND_INACTIVE) | (input.gun ? BOAT_NET_COMMAND_GUN : 0)),
        input.grenade, input.torpedo };
}

BoatInput boat_net_command_input(BoatNetCommand command)
{
    return (BoatInput) { command.steering / 127.0f, command.throttle / 127.0f,
        (command.flags & BOAT_NET_COMMAND_GUN) != 0, command.grenade, command.torpedo };
}

BoatNetCommand boat_net_neutral_command(const Boat *boat, uint8_t flags)
{
    // Zero steering springs the wheel back; throttle moves toward idle at the ordinary
    // adjustment rate, so the engine response decays. The boat stays physical.
    float throttle = boat->throttle / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP);
    return boat_net_command((BoatInput) { .throttle_rate = fmaxf(-1, fminf(1, -throttle)) }, flags);
}

typedef struct {
    uint64_t value;
} Digest;

static void digest_bytes(Digest *d, const void *data, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        d->value ^= ((const uint8_t *)data)[i];
        d->value *= UINT64_C(1099511628211);
    }
}

static void digest_u64(Digest *d, uint64_t value)
{
    uint8_t bytes[8];
    for (int i = 0; i < 8; ++i)
        bytes[i] = (uint8_t)(value >> (56 - 8 * i));
    digest_bytes(d, bytes, sizeof(bytes));
}

static void digest_float(Digest *d, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    digest_u64(d, bits);
}

uint64_t boat_net_bytes_digest(const void *data, size_t size)
{
    Digest d = { UINT64_C(14695981039346656037) };
    digest_bytes(&d, data, size);
    return d.value;
}

uint64_t boat_net_content_digest(void)
{
    // Compiled geometry is immutable. LAN rows may ask for compatibility every frame.
    static uint64_t cached;
    static bool ready;
    if (ready)
        return cached;
    Digest d = { UINT64_C(14695981039346656037) };
    digest_float(&d, BOAT_CANNON_LIFT);
    digest_float(&d, BOAT_CANNON_GRAVITY);
    digest_float(&d, BOAT_CANNON_RADIUS);
    digest_float(&d, BOAT_CANNON_MUZZLE_HEIGHT);
    digest_float(&d, BOAT_CANNON_MUZZLE_FORWARD);
    digest_float(&d, BOAT_CANNON_MUZZLE_SIDE);
    digest_bytes(&d, "proteus-boat-net", 16);
    for (int id = 0; id < BOAT_LOCATION_COUNT; ++id) {
        const BoatWorldDefinition *definition = boat_location(id);
        digest_u64(&d, (uint64_t)definition->id);
        digest_u64(&d, definition->mapped);
        digest_float(&d, definition->half_extent.x);
        digest_float(&d, definition->half_extent.y);
        digest_float(&d, definition->spawn.x);
        digest_float(&d, definition->spawn.y);
        digest_float(&d, definition->fleet_spawn.x);
        digest_float(&d, definition->fleet_spawn.y);
        digest_float(&d, definition->current_center);
        digest_float(&d, definition->wake_offset.x);
        digest_float(&d, definition->wake_offset.y);
        digest_u64(&d, definition->upstream);
        if (!definition->mapped)
            continue;
        // The definition-bound description, in its canonical triangle order.
        const Mesh3D *mesh = definition->map->mesh;
        digest_u64(&d, mesh->triangle_count);
        for (size_t i = 0; i < mesh->triangle_count; ++i)
            for (unsigned j = 0; j < 3; ++j) {
                unsigned index = mesh->triangles[i].vertices[j];
                Vector3 p = mesh->vertices[index];
                digest_u64(&d, index);
                digest_float(&d, p.x);
                digest_float(&d, p.y);
                digest_float(&d, p.z);
            }
    }
    const uint64_t contract[]
        = { BOAT_TICK_HZ, BOAT_FLEET_SLOTS, BOAT_FLEET_SOLVER_PASSES, BOAT_FLEET_SPAWN_RETRY_TICKS,
              BOAT_OBJECT_PER_BOAT, BOAT_OBJECT_EMIT_TICKS, BOAT_OBJECT_LIFE_TICKS };
    for (size_t i = 0; i < sizeof(contract) / sizeof(*contract); ++i)
        digest_u64(&d, contract[i]);
    digest_float(&d, BOAT_FLEET_MASS);
    digest_float(&d, BOAT_CONTACT_RESTITUTION);
    // Weather recipe revisions stay engine content. The course, rules and harbour contacts
    // travel in the room definition instead.
    digest_u64(&d, 3); // Local storm recipe revision (CPU and production GLSL).
    digest_u64(&d, 2); // Ferry recipe revision: centred crest and tapered three-wave packet.
    // Authored harbour contacts travel in the room definition, so only the contact recipe
    // revision stays in the engine digest.
    digest_u64(&d, 1); // Harbour contact recipe revision: berth hulls, dock and ferry boxes.
    cached = d.value;
    ready = true;
    return cached;
}

uint64_t boat_net_wake_digest(const BoatWakeObjects *wakes)
{
    Digest d = { UINT64_C(14695981039346656037) };
    for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i) {
        const BoatWakeObject *o = &wakes->objects[i];
        if (!o->amplitude)
            continue;
        digest_u64(&d, i);
        digest_float(&d, o->position.x);
        digest_float(&d, o->position.y);
        digest_float(&d, o->direction.x);
        digest_float(&d, o->direction.y);
        digest_float(&d, o->length);
        digest_float(&d, o->amplitude);
        digest_float(&d, o->inverse_speed);
        digest_u64(&d, o->born);
    }
    return d.value;
}

void boat_net_snapshot_of(const BoatFleetState *state,
    const BoatNetSlotInput inputs[BOAT_FLEET_SLOTS], BoatNetSnapshot *snapshot)
{
    const BoatWakeObjects *w = &state->wakes;
    *snapshot = (BoatNetSnapshot) { .next_request = state->next_request,
        .live = (uint16_t)w->live,
        .peak_live = (uint16_t)w->peak_live,
        .emitted = w->emitted,
        .expired = w->expired,
        .wake_digest = boat_net_wake_digest(w) };
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        snapshot->slots[i] = state->slots[i];
        snapshot->emitters[i] = w->emitters[i];
        snapshot->inputs[i] = inputs[i];
    }
}

size_t boat_net_checkpoint_encode(const BoatFleetState *state,
    const BoatNetSlotInput inputs[BOAT_FLEET_SLOTS], uint64_t race_go, uint8_t *out,
    size_t capacity)
{
    const BoatWakeObjects *w = &state->wakes;
    Writer writer = { out, 0, capacity, false };
    put64(&writer, state->tick);
    put32(&writer, state->next_request);
    put32(&writer, w->life_ticks);
    putf(&writer, w->crest_height);
    put16(&writer, (uint16_t)w->live);
    put16(&writer, (uint16_t)w->peak_live);
    put64(&writer, w->emitted);
    put64(&writer, w->expired);
    put64(&writer, race_go);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        put_slot(&writer, &state->slots[i], &w->emitters[i], &inputs[i]);
    for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i)
        put_record(&writer, &w->objects[i]);
    uint8_t combat[BOAT_COMBAT_BYTES];
    if (state->combat.enabled) {
        boat_combat_encode(&state->combat, combat);
        put_bytes(&writer, combat, sizeof(combat));
    }
    if (writer.overflow || w->tick != state->tick)
        return 0;
    assert(writer.size == (state->combat.enabled ? CHECKPOINT_BYTES : CHECKPOINT_BASE_BYTES));
    return writer.size;
}

BoatNetError boat_net_checkpoint_decode(const uint8_t *data, size_t size, BoatFleetState *state,
    BoatNetSlotInput inputs[BOAT_FLEET_SLOTS], uint64_t *race_go)
{
    static BoatFleetState decoded;
    BoatNetSlotInput records[BOAT_FLEET_SLOTS];
    decoded = (BoatFleetState) { 0 };
    Reader r = { data, size, 0, BOAT_NET_OK };
    BoatWakeObjects *w = &decoded.wakes;
    decoded.tick = w->tick = get64(&r);
    decoded.next_request = get32(&r);
    w->life_ticks = get32(&r);
    expect(&r, w->life_ticks >= 240 && w->life_ticks <= BOAT_OBJECT_LIFE_TICKS);
    w->crest_height = getf(&r, .1f, 3);
    w->live = get16(&r);
    w->peak_live = get16(&r);
    expect(&r, w->live <= BOAT_OBJECT_CAPACITY && w->peak_live <= BOAT_OBJECT_CAPACITY);
    w->emitted = get64(&r);
    w->expired = get64(&r);
    expect(&r, w->expired <= w->emitted);
    uint64_t go = get64(&r);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        get_slot(&r, decoded.tick, &decoded.slots[i], &w->emitters[i], &records[i]);
    unsigned live = 0;
    for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i) {
        get_record(&r, decoded.tick, &w->objects[i]);
        live += w->objects[i].amplitude != 0;
    }
    expect(&r, live == w->live);
    if (r.at + BOAT_COMBAT_BYTES <= r.size) {
        expect(&r, boat_combat_decode(&decoded.combat, r.data + r.at, decoded.tick));
        r.at += BOAT_COMBAT_BYTES;
    } else if (r.at != r.size)
        fail(&r, BOAT_NET_BAD_LENGTH);
    if (r.at != r.size)
        fail(&r, BOAT_NET_BAD_LENGTH);
    if (r.error)
        return r.error;
    *state = decoded;
    memcpy(inputs, records, sizeof(records));
    if (race_go)
        *race_go = go;
    return BOAT_NET_OK;
}

void boat_net_match_state_of(const BoatMatch *match, BoatNetMatchState *state)
{
    const BoatMatchRules *rules = &match->rules;
    *state = (BoatNetMatchState) { .phase = (uint8_t)match->phase,
        .respawn_ticks
        = (uint16_t)(rules->respawn_ticks < UINT16_MAX ? rules->respawn_ticks : UINT16_MAX),
        .skip_ticks = (uint16_t)(rules->skip_ticks < UINT16_MAX ? rules->skip_ticks : UINT16_MAX),
        .round = match->round,
        .round_ticks = (uint32_t)rules->round_ticks,
        .phase_end = match->phase == BOAT_MATCH_WARMUP ? 0 : match->phase_end };
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        if (match->slots[i].present) {
            state->slots[i].present = true;
            state->slots[i].row = match->rows[i];
        }
}

BoatNetMatchEvent boat_net_match_event_of(const BoatMatch *match, const BoatMatchEvent *event)
{
    return (BoatNetMatchEvent) { .kind = event->kind,
        .victim = event->victim,
        .attacker = event->attacker,
        .cause = event->cause,
        .bailout = event->bailout,
        .reason = event->reason,
        .victim_life = event->kind == BOAT_MATCH_CAPSIZE ? event->victim_life : 0,
        .round = match->round };
}

bool boat_net_match_state_equal(const BoatNetMatchState *a, const BoatNetMatchState *b)
{
    if (a->phase != b->phase || a->respawn_ticks != b->respawn_ticks
        || a->skip_ticks != b->skip_ticks || a->round != b->round
        || a->round_ticks != b->round_ticks || a->phase_end != b->phase_end)
        return false;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        const BoatMatchRow *x = &a->slots[i].row, *y = &b->slots[i].row;
        if (a->slots[i].present != b->slots[i].present || x->score != y->score
            || x->caused != y->caused || x->capsized != y->capsized || x->self != y->self)
            return false;
    }
    return true;
}

enum {
    LAN_QUERY_MAGIC = 0x50424c51u, // "PBLQ"
    LAN_ROOM_MAGIC = 0x50424c52u, // "PBLR"
    LAN_TOKEN = 1u,
};

size_t boat_net_lan_query_encode(uint8_t out[BOAT_NET_DISCOVERY_BYTES])
{
    Writer w = { out, 0, BOAT_NET_DISCOVERY_BYTES, false };
    put32(&w, LAN_QUERY_MAGIC);
    put16(&w, BOAT_NET_DISCOVERY_FORMAT);
    put16(&w, BOAT_NET_VERSION);
    while (w.size < BOAT_NET_DISCOVERY_BYTES)
        put8(&w, 0); // Padding to the answer's size.
    return w.size;
}

bool boat_net_lan_query_decode(const uint8_t *data, size_t size)
{
    if (size != BOAT_NET_DISCOVERY_BYTES)
        return false;
    Reader r = { data, size, 0, BOAT_NET_OK };
    expect(&r, get32(&r) == LAN_QUERY_MAGIC);
    expect(&r, get16(&r) == BOAT_NET_DISCOVERY_FORMAT);
    get16(&r); // The searcher's protocol version: any version may learn about this room.
    reserved(&r, BOAT_NET_DISCOVERY_BYTES - 8);
    return r.error == BOAT_NET_OK;
}

size_t boat_net_lan_room_encode(const BoatNetLanRoom *room, uint8_t out[BOAT_NET_DISCOVERY_BYTES])
{
    uint8_t bytes[BOAT_NET_DISCOVERY_BYTES];
    Writer w = { bytes, 0, sizeof(bytes), false };
    put32(&w, LAN_ROOM_MAGIC);
    put16(&w, BOAT_NET_DISCOVERY_FORMAT);
    put16(&w, room->version);
    put32(&w, room->physics_revision);
    put64(&w, room->content_digest);
    put32(&w, room->epoch);
    put32(&w, room->address);
    put16(&w, room->port);
    put8(&w, room->mode);
    put8(&w, room->players);
    put8(&w, room->max_players);
    put8(&w, room->token ? LAN_TOKEN : 0);
    put_name(&w, room->name);
    put8(&w, 0);
    BoatNetLanRoom check;
    if (w.overflow || w.size != sizeof(bytes) || !boat_net_lan_room_decode(bytes, w.size, &check))
        return 0;
    memcpy(out, bytes, sizeof(bytes));
    return w.size;
}

bool boat_net_lan_room_decode(const uint8_t *data, size_t size, BoatNetLanRoom *room)
{
    if (size != BOAT_NET_DISCOVERY_BYTES)
        return false;
    Reader r = { data, size, 0, BOAT_NET_OK };
    BoatNetLanRoom decoded = { 0 };
    expect(&r, get32(&r) == LAN_ROOM_MAGIC);
    expect(&r, get16(&r) == BOAT_NET_DISCOVERY_FORMAT);
    decoded.version = get16(&r);
    decoded.physics_revision = get32(&r);
    decoded.content_digest = get64(&r);
    decoded.epoch = get32(&r);
    decoded.address = get32(&r);
    decoded.port = get16(&r);
    expect(&r, decoded.port != 0);
    decoded.mode = get8(&r);
    decoded.players = get8(&r);
    decoded.max_players = get8(&r);
    expect(&r, decoded.max_players > 0 && decoded.players <= decoded.max_players);
    uint8_t flags = get8(&r);
    expect(&r, (flags & ~LAN_TOKEN) == 0);
    decoded.token = flags & LAN_TOKEN;
    get_name(&r, decoded.name);
    reserved(&r, 1);
    if (r.error)
        return false;
    *room = decoded;
    return true;
}

bool boat_net_lan_room_compatible(const BoatNetLanRoom *room)
{
    return room->version == BOAT_NET_VERSION && room->physics_revision == BOAT_PHYSICS_REVISION
        && room->content_digest == boat_net_content_digest();
}

// Decimal digits of VALUE at TEXT; returns the end.
static char *put_decimal(char *text, unsigned value)
{
    char digits[5];
    int count = 0;
    do
        digits[count++] = (char)('0' + value % 10);
    while ((value /= 10) != 0);
    while (count)
        *text++ = digits[--count];
    return text;
}

void boat_net_lan_endpoint(const BoatNetLanRoom *room, char *out, size_t size)
{
    char text[24], *end = text; // "255.255.255.255:65535"; no stdio in this library.
    for (int shift = 24; shift >= 0; shift -= 8) {
        end = put_decimal(end, room->address >> shift & 255);
        *end++ = shift ? '.' : ':';
    }
    end = put_decimal(end, room->port);
    size_t length = (size_t)(end - text);
    if (!size)
        return;
    length = length < size - 1 ? length : size - 1;
    memcpy(out, text, length);
    out[length] = 0;
}

void boat_net_lan_list_note(BoatNetLanList *list, const BoatNetLanRoom *room, double now)
{
    unsigned i = 0;
    while (i < list->count
        && (list->rooms[i].epoch != room->epoch || list->rooms[i].port != room->port
            || strcmp(list->rooms[i].name, room->name) != 0))
        ++i;
    if (i == BOAT_NET_LAN_ROOMS)
        return;
    uint32_t address = i < list->count ? list->rooms[i].address : room->address;
    list->rooms[i] = *room;
    list->rooms[i].address = address;
    list->seen[i] = now;
    if (i == list->count)
        ++list->count;
}

void boat_net_lan_list_expire(BoatNetLanList *list, double now, double age)
{
    unsigned kept = 0;
    for (unsigned i = 0; i < list->count; ++i)
        if (now - list->seen[i] <= age) {
            list->rooms[kept] = list->rooms[i];
            list->seen[kept++] = list->seen[i];
        }
    list->count = kept;
}

uint64_t boat_net_race_go(const BoatRace *race) { return race->round ? race->go_tick : 0; }

void boat_net_race_bind(BoatFleet *fleet, const BoatRaceCourse *course, uint64_t go)
{
    if (!course) {
        // Not a race room: the ambient water stays as the fleet was initialised.
        return;
    }
    BoatRace race = { .round = go != 0, .go_tick = go, .course = *course };
    boat_race_bind(&race, fleet);
}

void boat_net_race_state_of(const BoatRace *race, BoatNetRaceState *state)
{
    const BoatRaceRules *rules = &race->rules;
    bool timed = race->phase == BOAT_RACE_GRID || race->phase == BOAT_RACE_COUNTDOWN
        || race->phase == BOAT_RACE_RESULTS;
    *state = (BoatNetRaceState) { .phase = (uint8_t)race->phase,
        .placed = (uint8_t)race->placed,
        .recovery_ticks
        = (uint16_t)(rules->recovery_ticks < UINT16_MAX ? rules->recovery_ticks : UINT16_MAX),
        .skip_ticks = (uint16_t)(rules->skip_ticks < UINT16_MAX ? rules->skip_ticks : UINT16_MAX),
        .park_ticks = (uint16_t)(rules->park_ticks < UINT16_MAX ? rules->park_ticks : UINT16_MAX),
        .round = race->round,
        .phase_end = timed ? race->phase_end : 0,
        .go_tick = boat_net_race_go(race),
        .deadline = race->phase == BOAT_RACE_RACING ? race->deadline : 0 };
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        const BoatRaceEntrant *e = &race->entrants[i];
        state->slots[i].present = race->slots[i].identity != 0;
        if (!e->identity)
            continue;
        state->slots[i].entrant = true;
        state->slots[i].current = boat_race_entrant(race, i);
        state->slots[i].parked = state->slots[i].current && race->slots[i].parked;
        state->slots[i].status = e->status;
        state->slots[i].place = e->place;
        state->slots[i].gates = e->gates;
        state->slots[i].recoveries = e->recoveries;
        state->slots[i].finish = e->status == BOAT_RACE_FINISHED ? e->finish : 0;
    }
}

BoatNetRaceEvent boat_net_race_event_of(const BoatRace *race, const BoatRaceEvent *event)
{
    return (BoatNetRaceEvent) { .kind = event->kind,
        .slot = event->slot,
        .value = event->kind == BOAT_RACE_PHASE || event->kind == BOAT_RACE_GATE
                || event->kind == BOAT_RACE_FINISH
            ? event->value
            : 0,
        .round = race->round,
        .time = event->kind == BOAT_RACE_FINISH ? event->time : 0 };
}

bool boat_net_race_state_equal(const BoatNetRaceState *a, const BoatNetRaceState *b)
{
    if (a->phase != b->phase || a->placed != b->placed || a->recovery_ticks != b->recovery_ticks
        || a->skip_ticks != b->skip_ticks || a->park_ticks != b->park_ticks || a->round != b->round
        || a->phase_end != b->phase_end || a->go_tick != b->go_tick || a->deadline != b->deadline)
        return false;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        const BoatNetRaceRow *x = &a->slots[i], *y = &b->slots[i];
        if (x->present != y->present || x->entrant != y->entrant || x->current != y->current
            || x->parked != y->parked || x->status != y->status || x->place != y->place
            || x->gates != y->gates || x->recoveries != y->recoveries || x->finish != y->finish)
            return false;
    }
    return true;
}
