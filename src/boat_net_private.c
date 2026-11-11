#include "boat_net_private.h"

#include <math.h>
#include <string.h>

enum { COMMAND_BYTES = 12, BATCH_BYTES = 16 };

static void put8(uint8_t **p, uint8_t value) { *(*p)++ = value; }

static void put16(uint8_t **p, uint16_t value)
{
    put8(p, (uint8_t)(value >> 8));
    put8(p, (uint8_t)value);
}

static void put32(uint8_t **p, uint32_t value)
{
    put16(p, (uint16_t)(value >> 16));
    put16(p, (uint16_t)value);
}

static void put64(uint8_t **p, uint64_t value)
{
    put32(p, (uint32_t)(value >> 32));
    put32(p, (uint32_t)value);
}

static void putf(uint8_t **p, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put32(p, bits);
}

static uint8_t get8(const uint8_t **p) { return *(*p)++; }

static uint16_t get16(const uint8_t **p)
{
    uint16_t high = get8(p);
    return (uint16_t)(high << 8 | get8(p));
}

static uint32_t get32(const uint8_t **p)
{
    uint32_t high = get16(p);
    return high << 16 | get16(p);
}

static uint64_t get64(const uint8_t **p)
{
    uint64_t high = get32(p);
    return high << 32 | get32(p);
}

static float getf(const uint8_t **p)
{
    uint32_t bits = get32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

bool boat_net_private_message(const uint8_t *data, size_t size)
{
    return data && size >= BOAT_NET_HEADER_BYTES && data[0] == 0x50 && data[1] == 0x42
        && data[2] == 0x4e && data[3] == 0x54 && data[4] == 0 && data[5] == BOAT_NET_VERSION
        && data[6] >= BOAT_NET_PRIVATE_FIRST;
}

size_t boat_net_private_encode_input(
    const BoatNetHeader *header, const BoatNetInputBatch *batch, uint8_t out[BOAT_NET_MAX_MESSAGE])
{
    if (batch->count < 1 || batch->count > BOAT_NET_MAX_COMMANDS || batch->count > header->tick)
        return 0;
    for (unsigned i = 0; i < batch->count; ++i) {
        const BoatNetCommand *c = &batch->commands[i];
        if (!isfinite(c->steering_rate) || !isfinite(c->throttle_rate)
            || (c->flags & ~BOAT_NET_COMMAND_MASK))
            return 0;
    }
    size_t payload = BATCH_BYTES + (size_t)batch->count * COMMAND_BYTES;
    uint8_t *p = out;
    put32(&p, BOAT_NET_MAGIC);
    put16(&p, BOAT_NET_VERSION);
    put8(&p, BOAT_NET_PRIVATE_INPUT_BATCH);
    put8(&p, 0);
    put32(&p, header->epoch);
    put32(&p, header->sequence);
    put64(&p, header->tick);
    put16(&p, (uint16_t)payload);
    put16(&p, 0);
    put32(&p, 0);
    put32(&p, batch->life);
    put8(&p, batch->count);
    put8(&p, 0);
    put16(&p, 0);
    put64(&p, batch->snapshot_tick);
    for (unsigned i = 0; i < batch->count; ++i) {
        const BoatNetCommand *c = &batch->commands[i];
        putf(&p, c->steering_rate);
        putf(&p, c->throttle_rate);
        put8(&p, c->flags);
        put16(&p, c->grenade);
        put8(&p, c->torpedo);
    }
    return (size_t)(p - out);
}

BoatNetError boat_net_private_decode_input(
    const uint8_t *data, size_t size, BoatNetHeader *header, BoatNetInputBatch *batch)
{
    if (!data || size < BOAT_NET_HEADER_BYTES)
        return BOAT_NET_TRUNCATED;
    const uint8_t *p = data;
    if (get32(&p) != BOAT_NET_MAGIC)
        return BOAT_NET_BAD_MAGIC;
    if (get16(&p) != BOAT_NET_VERSION)
        return BOAT_NET_BAD_VERSION;
    BoatNetHeader h = { 0 };
    h.type = get8(&p);
    h.flags = get8(&p);
    h.epoch = get32(&p);
    h.sequence = get32(&p);
    h.tick = get64(&p);
    h.payload = get16(&p);
    uint16_t reserved16 = get16(&p);
    uint32_t reserved32 = get32(&p);
    if (h.flags || reserved16 || reserved32)
        return BOAT_NET_BAD_HEADER;
    if (h.type != BOAT_NET_PRIVATE_INPUT_BATCH)
        return BOAT_NET_BAD_TYPE;
    if (size != BOAT_NET_HEADER_BYTES + (size_t)h.payload || h.payload < BATCH_BYTES)
        return BOAT_NET_BAD_LENGTH;
    BoatNetInputBatch b = { 0 };
    b.life = get32(&p);
    b.count = get8(&p);
    uint8_t reserved8 = get8(&p);
    reserved16 = get16(&p);
    b.snapshot_tick = get64(&p);
    if (reserved8 || reserved16 || b.count < 1 || b.count > BOAT_NET_MAX_COMMANDS
        || b.count > h.tick)
        return BOAT_NET_BAD_FIELD;
    if (h.payload != BATCH_BYTES + (size_t)b.count * COMMAND_BYTES)
        return BOAT_NET_BAD_LENGTH;
    for (unsigned i = 0; i < b.count; ++i) {
        BoatNetCommand *c = &b.commands[i];
        float steering = getf(&p), throttle = getf(&p);
        uint8_t flags = get8(&p);
        uint16_t grenade = get16(&p);
        uint8_t torpedo = get8(&p);
        if (!isfinite(steering) || !isfinite(throttle))
            return BOAT_NET_NONFINITE;
        if (flags & ~BOAT_NET_COMMAND_MASK)
            return BOAT_NET_BAD_FIELD;
        *c = boat_net_precise_command((BoatInput) { .steering_rate = steering,
                                          .throttle_rate = throttle,
                                          .gun = (flags & BOAT_NET_COMMAND_GUN) != 0,
                                          .grenade = grenade,
                                          .torpedo = torpedo },
            flags);
    }
    *header = h;
    *batch = b;
    return BOAT_NET_OK;
}

size_t boat_net_private_encode_state(const BoatNetHeader *header, const uint8_t *state, size_t size,
    uint8_t out[BOAT_NET_MAX_MESSAGE])
{
    if (!state || size < 1 || size > BOAT_NET_PRIVATE_STATE_BYTES)
        return 0;
    uint8_t *p = out;
    put32(&p, BOAT_NET_MAGIC);
    put16(&p, BOAT_NET_VERSION);
    put8(&p, BOAT_NET_PRIVATE_STATE);
    put8(&p, 0);
    put32(&p, header->epoch);
    put32(&p, header->sequence);
    put64(&p, header->tick);
    put16(&p, (uint16_t)size);
    put16(&p, 0);
    put32(&p, 0);
    memcpy(p, state, size);
    return (size_t)(p - out) + size;
}

BoatNetError boat_net_private_decode_state(const uint8_t *data, size_t size, BoatNetHeader *header,
    uint8_t state[BOAT_NET_PRIVATE_STATE_BYTES], size_t *state_size)
{
    if (!data || size < BOAT_NET_HEADER_BYTES)
        return BOAT_NET_TRUNCATED;
    const uint8_t *p = data;
    if (get32(&p) != BOAT_NET_MAGIC)
        return BOAT_NET_BAD_MAGIC;
    if (get16(&p) != BOAT_NET_VERSION)
        return BOAT_NET_BAD_VERSION;
    BoatNetHeader h = { 0 };
    h.type = get8(&p);
    h.flags = get8(&p);
    h.epoch = get32(&p);
    h.sequence = get32(&p);
    h.tick = get64(&p);
    h.payload = get16(&p);
    uint16_t reserved16 = get16(&p);
    uint32_t reserved32 = get32(&p);
    if (h.flags || reserved16 || reserved32)
        return BOAT_NET_BAD_HEADER;
    if (h.type != BOAT_NET_PRIVATE_STATE)
        return BOAT_NET_BAD_TYPE;
    if (size != BOAT_NET_HEADER_BYTES + (size_t)h.payload || h.payload < 1
        || h.payload > BOAT_NET_PRIVATE_STATE_BYTES)
        return BOAT_NET_BAD_LENGTH;
    memcpy(state, p, h.payload);
    *state_size = h.payload;
    *header = h;
    return BOAT_NET_OK;
}

size_t boat_net_private_encode_recover(
    const BoatNetHeader *header, uint8_t out[BOAT_NET_MAX_MESSAGE])
{
    uint8_t *p = out;
    put32(&p, BOAT_NET_MAGIC);
    put16(&p, BOAT_NET_VERSION);
    put8(&p, BOAT_NET_PRIVATE_RECOVER);
    put8(&p, 0);
    put32(&p, header->epoch);
    put32(&p, header->sequence);
    put64(&p, header->tick);
    put16(&p, 0);
    put16(&p, 0);
    put32(&p, 0);
    return (size_t)(p - out);
}

uint8_t boat_net_private_type(const uint8_t *data, size_t size)
{
    return boat_net_private_message(data, size) ? data[6] : 0;
}

BoatNetError boat_net_private_decode_recover(
    const uint8_t *data, size_t size, BoatNetHeader *header)
{
    if (!data || size < BOAT_NET_HEADER_BYTES)
        return BOAT_NET_TRUNCATED;
    const uint8_t *p = data;
    if (get32(&p) != BOAT_NET_MAGIC)
        return BOAT_NET_BAD_MAGIC;
    if (get16(&p) != BOAT_NET_VERSION)
        return BOAT_NET_BAD_VERSION;
    BoatNetHeader h = { 0 };
    h.type = get8(&p);
    h.flags = get8(&p);
    h.epoch = get32(&p);
    h.sequence = get32(&p);
    h.tick = get64(&p);
    h.payload = get16(&p);
    uint16_t reserved16 = get16(&p);
    uint32_t reserved32 = get32(&p);
    if (h.flags || reserved16 || reserved32)
        return BOAT_NET_BAD_HEADER;
    if (h.type != BOAT_NET_PRIVATE_RECOVER)
        return BOAT_NET_BAD_TYPE;
    if (h.payload || size != BOAT_NET_HEADER_BYTES)
        return BOAT_NET_BAD_LENGTH;
    *header = h;
    return BOAT_NET_OK;
}

size_t boat_net_private_encode_commit(const BoatNetHeader *header, uint32_t revision,
    uint8_t change, uint8_t out[BOAT_NET_MAX_MESSAGE])
{
    uint8_t *p = out;
    put32(&p, BOAT_NET_MAGIC);
    put16(&p, BOAT_NET_VERSION);
    put8(&p, BOAT_NET_PRIVATE_COMMIT);
    put8(&p, 0);
    put32(&p, header->epoch);
    put32(&p, header->sequence);
    put64(&p, header->tick);
    put16(&p, BOAT_NET_PRIVATE_COMMIT_BYTES);
    put16(&p, 0);
    put32(&p, 0);
    put32(&p, revision);
    put8(&p, change);
    return (size_t)(p - out);
}

BoatNetError boat_net_private_decode_commit(
    const uint8_t *data, size_t size, BoatNetHeader *header, uint32_t *revision, uint8_t *change)
{
    if (!data || size < BOAT_NET_HEADER_BYTES)
        return BOAT_NET_TRUNCATED;
    const uint8_t *p = data;
    if (get32(&p) != BOAT_NET_MAGIC)
        return BOAT_NET_BAD_MAGIC;
    if (get16(&p) != BOAT_NET_VERSION)
        return BOAT_NET_BAD_VERSION;
    BoatNetHeader h = { 0 };
    h.type = get8(&p);
    h.flags = get8(&p);
    h.epoch = get32(&p);
    h.sequence = get32(&p);
    h.tick = get64(&p);
    h.payload = get16(&p);
    uint16_t reserved16 = get16(&p);
    uint32_t reserved32 = get32(&p);
    if (h.flags || reserved16 || reserved32)
        return BOAT_NET_BAD_HEADER;
    if (h.type != BOAT_NET_PRIVATE_COMMIT)
        return BOAT_NET_BAD_TYPE;
    if (h.payload != BOAT_NET_PRIVATE_COMMIT_BYTES
        || size != BOAT_NET_HEADER_BYTES + BOAT_NET_PRIVATE_COMMIT_BYTES)
        return BOAT_NET_BAD_LENGTH;
    *revision = get32(&p);
    *change = get8(&p);
    *header = h;
    return BOAT_NET_OK;
}

// The v14 header of a private message of TYPE with PAYLOAD bytes.
static void private_header(uint8_t **p, const BoatNetHeader *header, uint8_t type, uint16_t payload)
{
    put32(p, BOAT_NET_MAGIC);
    put16(p, BOAT_NET_VERSION);
    put8(p, type);
    put8(p, 0);
    put32(p, header->epoch);
    put32(p, header->sequence);
    put64(p, header->tick);
    put16(p, payload);
    put16(p, 0);
    put32(p, 0);
}

// Read a private header of TYPE whose payload is the rest of SIZE.
static BoatNetError read_private_header(
    const uint8_t **p, const uint8_t *data, size_t size, uint8_t type, BoatNetHeader *out)
{
    if (!data || size < BOAT_NET_HEADER_BYTES)
        return BOAT_NET_TRUNCATED;
    *p = data;
    if (get32(p) != BOAT_NET_MAGIC)
        return BOAT_NET_BAD_MAGIC;
    if (get16(p) != BOAT_NET_VERSION)
        return BOAT_NET_BAD_VERSION;
    BoatNetHeader h = { 0 };
    h.type = get8(p);
    h.flags = get8(p);
    h.epoch = get32(p);
    h.sequence = get32(p);
    h.tick = get64(p);
    h.payload = get16(p);
    uint16_t reserved16 = get16(p);
    uint32_t reserved32 = get32(p);
    if (h.flags || reserved16 || reserved32)
        return BOAT_NET_BAD_HEADER;
    if (h.type != type)
        return BOAT_NET_BAD_TYPE;
    if (size != BOAT_NET_HEADER_BYTES + (size_t)h.payload)
        return BOAT_NET_BAD_LENGTH;
    *out = h;
    return BOAT_NET_OK;
}

size_t boat_net_private_encode_inspect(
    const BoatNetHeader *header, uint8_t out[BOAT_NET_MAX_MESSAGE])
{
    uint8_t *p = out;
    private_header(&p, header, BOAT_NET_PRIVATE_INSPECT, 0);
    return (size_t)(p - out);
}

BoatNetError boat_net_private_decode_inspect(
    const uint8_t *data, size_t size, BoatNetHeader *header)
{
    const uint8_t *p;
    BoatNetHeader h;
    BoatNetError error = read_private_header(&p, data, size, BOAT_NET_PRIVATE_INSPECT, &h);
    if (error != BOAT_NET_OK)
        return error;
    if (h.payload)
        return BOAT_NET_BAD_LENGTH;
    *header = h;
    return BOAT_NET_OK;
}

enum { INSPECTION_CHUNKS = BOAT_NET_PRIVATE_INSPECTION_BYTES / BOAT_NET_PRIVATE_INSPECTION_CHUNK };

size_t boat_net_private_encode_inspection(const BoatNetHeader *header, unsigned index,
    unsigned count, const char *text, size_t size, uint8_t out[BOAT_NET_MAX_MESSAGE])
{
    if (!count || count > INSPECTION_CHUNKS || index >= count
        || size > BOAT_NET_PRIVATE_INSPECTION_CHUNK || (size && !text))
        return 0;
    uint8_t *p = out;
    private_header(&p, header, BOAT_NET_PRIVATE_INSPECTION, (uint16_t)(4 + size));
    put16(&p, (uint16_t)index);
    put16(&p, (uint16_t)count);
    memcpy(p, text, size);
    return (size_t)(p - out) + size;
}

BoatNetError boat_net_private_decode_inspection(const uint8_t *data, size_t size,
    BoatNetHeader *header, unsigned *index, unsigned *count,
    char text[BOAT_NET_PRIVATE_INSPECTION_CHUNK], size_t *text_size)
{
    const uint8_t *p;
    BoatNetHeader h;
    BoatNetError error = read_private_header(&p, data, size, BOAT_NET_PRIVATE_INSPECTION, &h);
    if (error != BOAT_NET_OK)
        return error;
    if (h.payload < 4 || h.payload > 4 + BOAT_NET_PRIVATE_INSPECTION_CHUNK)
        return BOAT_NET_BAD_LENGTH;
    unsigned i = get16(&p), n = get16(&p);
    if (!n || n > INSPECTION_CHUNKS || i >= n)
        return BOAT_NET_BAD_LENGTH;
    *index = i;
    *count = n;
    *text_size = h.payload - 4u;
    memcpy(text, p, *text_size);
    *header = h;
    return BOAT_NET_OK;
}
