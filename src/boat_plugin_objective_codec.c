#include "boat_plugin_objective.h"

#include <string.h>

// The objective's canonical byte codec and timer validation. Kept apart from boat_plugin_state.c
// so linking the codec never pulls in the Lua evaluator: the sandbox save (and so the browser
// title) uses it, and the browser game must not carry Lua.

bool boat_plugin_objective_timers_valid(const BoatPluginObjective *o)
{
    if (o->timer_count > BOAT_PLUGIN_TIMERS || (!o->timers_enabled && o->timer_count))
        return false;
    for (unsigned i = 0; i < o->timer_count; ++i) {
        const BoatPluginTimer *timer = &o->timers[i];
        const char *end = memchr(timer->id, 0, sizeof(timer->id));
        if (!end || end == timer->id || timer->tick <= 0)
            return false;
        for (const char *p = timer->id; p < end; ++p)
            if ((unsigned char)*p < 32 || (unsigned char)*p > 126)
                return false;
        if (i
            && (o->timers[i - 1].tick > timer->tick
                || (o->timers[i - 1].tick == timer->tick
                    && strcmp(o->timers[i - 1].id, timer->id) >= 0)))
            return false;
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(o->timers[j].id, timer->id))
                return false;
    }
    return true;
}

static bool objective_put_uint(
    unsigned char *out, size_t capacity, size_t *at, uint64_t value, unsigned count)
{
    if (*at > capacity || (size_t)count > capacity - *at)
        return false;
    for (unsigned i = 0; i < count; ++i) {
        out[(*at)++] = (unsigned char)value;
        value >>= 8;
    }
    return true;
}

static bool objective_put_text(
    unsigned char *out, size_t capacity, size_t *at, const char *text, size_t limit)
{
    size_t length = 0;
    while (length < limit && text[length])
        ++length;
    if (length >= limit) // not NUL-terminated within its array
        return false;
    if (!objective_put_uint(out, capacity, at, (uint64_t)length, 1))
        return false;
    if (length > capacity - *at)
        return false;
    memcpy(out + *at, text, length);
    *at += length;
    return true;
}

static bool state_canonical(const unsigned char *bytes, unsigned size);

bool boat_plugin_objective_encode(
    const BoatPluginObjective *objective, unsigned char *out, size_t capacity, size_t *size)
{
    if (!objective || !out || !size || objective->hud_count > BOAT_PLUGIN_HUD
        || objective->timer_count > BOAT_PLUGIN_TIMERS
        || objective->state.size > BOAT_PLUGIN_STATE_BYTES
        || !boat_plugin_objective_timers_valid(objective)
        || !state_canonical(objective->state.bytes, objective->state.size))
        return false; // Never write what decode would refuse.
    unsigned char flags = (unsigned char)((objective->timers_enabled ? 1u : 0u)
        | (objective->work_enabled ? 2u : 0u) | (objective->initialized ? 4u : 0u)
        | (objective->quarantined ? 8u : 0u));
    size_t at = 0;
    if (!objective_put_uint(out, capacity, &at, (uint64_t)flags, 1)
        || !objective_put_uint(out, capacity, &at, (uint64_t)objective->state_version, 4)
        || !objective_put_uint(out, capacity, &at, (uint64_t)objective->generation, 4)
        || !objective_put_uint(out, capacity, &at, (uint64_t)objective->rng, 4)
        || !objective_put_uint(out, capacity, &at, (uint64_t)objective->acknowledged, 8)
        || !objective_put_uint(out, capacity, &at, (uint64_t)objective->tick, 8)
        || !objective_put_uint(out, capacity, &at, (uint64_t)objective->state.size, 2))
        return false;
    if (objective->state.size > capacity - at)
        return false;
    memcpy(out + at, objective->state.bytes, objective->state.size);
    at += objective->state.size;
    if (!objective_put_uint(out, capacity, &at, (uint64_t)objective->hud_count, 1))
        return false;
    for (unsigned i = 0; i < objective->hud_count; ++i) {
        if (!objective_put_text(
                out, capacity, &at, objective->hud[i].id, sizeof(objective->hud[i].id))
            || !objective_put_text(
                out, capacity, &at, objective->hud[i].label, sizeof(objective->hud[i].label))
            || !objective_put_uint(out, capacity, &at, (uint64_t)objective->hud[i].value, 8))
            return false;
    }
    if (!objective_put_uint(out, capacity, &at, (uint64_t)objective->timer_count, 1))
        return false;
    for (unsigned i = 0; i < objective->timer_count; ++i) {
        if (!objective_put_text(
                out, capacity, &at, objective->timers[i].id, sizeof(objective->timers[i].id))
            || !objective_put_uint(out, capacity, &at, (uint64_t)objective->timers[i].tick, 8))
            return false;
    }
    *size = at;
    return true;
}

typedef struct {
    const unsigned char *bytes;
    unsigned size;
    unsigned offset;
    unsigned tables;
} CanonicalWalk;

static bool canonical_read(CanonicalWalk *walk, unsigned count, uint64_t *value)
{
    if (walk->offset > walk->size || count > walk->size - walk->offset)
        return false;
    uint64_t result = 0;
    for (unsigned i = 0; i < count; ++i)
        result |= (uint64_t)walk->bytes[walk->offset++] << (i * 8);
    *value = result;
    return true;
}

static bool canonical_value(CanonicalWalk *walk, unsigned depth)
{
    if (depth > BOAT_PLUGIN_STATE_DEPTH)
        return false;
    uint64_t tag = 0;
    if (!canonical_read(walk, 1, &tag))
        return false;
    switch (tag) {
    case 't':
    case 'f':
        return true;
    case 'i': {
        uint64_t bits;
        if (!canonical_read(walk, 8, &bits))
            return false;
        (void)bits;
        return true;
    }
    case 's': {
        uint64_t length = 0;
        if (!canonical_read(walk, 2, &length) || length > 1024
            || length > walk->size - walk->offset)
            return false;
        walk->offset += (unsigned)length;
        return true;
    }
    case 'a':
    case 'r': {
        uint64_t entries = 0;
        if (!canonical_read(walk, 2, &entries) || entries > 256
            || ++walk->tables > BOAT_PLUGIN_STATE_TABLES)
            return false;
        unsigned entry_count = (unsigned)entries;
        if (tag == 'a') {
            for (unsigned i = 0; i < entry_count; ++i)
                if (!canonical_value(walk, depth + 1))
                    return false;
            return true;
        }
        char previous[64];
        bool have_previous = false;
        for (unsigned i = 0; i < entry_count; ++i) {
            uint64_t key_length = 0;
            if (!canonical_read(walk, 1, &key_length) || key_length < 1 || key_length > 63
                || key_length > walk->size - walk->offset)
                return false;
            const unsigned char *key = walk->bytes + walk->offset;
            if (memchr(key, 0, (size_t)key_length))
                return false;
            char current[64];
            memcpy(current, key, (size_t)key_length);
            current[(size_t)key_length] = 0;
            walk->offset += (unsigned)key_length;
            if (have_previous && strcmp(previous, current) >= 0)
                return false;
            memcpy(previous, current, (size_t)key_length + 1);
            have_previous = true;
            if (!canonical_value(walk, depth + 1))
                return false;
        }
        return true;
    }
    default:
        return false;
    }
}

static bool state_canonical(const unsigned char *bytes, unsigned size)
{
    if (!size)
        return true;
    if (!bytes)
        return false;
    CanonicalWalk walk = { bytes, size, 0, 0 };
    if (!canonical_value(&walk, 0))
        return false;
    return walk.offset == walk.size;
}

static bool objective_read_uint(
    const unsigned char *bytes, size_t size, size_t *at, unsigned count, uint64_t *value)
{
    if (*at > size || (size_t)count > size - *at)
        return false;
    uint64_t result = 0;
    for (unsigned i = 0; i < count; ++i)
        result |= (uint64_t)bytes[(*at)++] << (i * 8);
    *value = result;
    return true;
}

static bool objective_read_text(
    const unsigned char *bytes, size_t size, size_t *at, char *out, size_t capacity)
{
    uint64_t length = 0;
    if (!objective_read_uint(bytes, size, at, 1, &length) || length < 1 || length >= capacity
        || length > size - *at)
        return false;
    for (size_t i = 0; i < (size_t)length; ++i) {
        unsigned char c = bytes[*at + i];
        if (c < 32 || c > 126)
            return false;
    }
    memcpy(out, bytes + *at, (size_t)length);
    out[(size_t)length] = 0;
    *at += (size_t)length;
    return true;
}

bool boat_plugin_objective_decode(
    const unsigned char *bytes, size_t size, BoatPluginObjective *objective)
{
    if (!objective)
        return false;
    BoatPluginObjective next = { 0 };
    if (!size) {
        *objective = next;
        return true;
    }
    if (!bytes)
        return false;
    size_t at = 0;
    uint64_t flags = 0, state_version = 0, generation = 0, rng = 0, acknowledged = 0, tick = 0;
    if (!objective_read_uint(bytes, size, &at, 1, &flags) || (flags & ~UINT64_C(15))
        || !objective_read_uint(bytes, size, &at, 4, &state_version)
        || !objective_read_uint(bytes, size, &at, 4, &generation)
        || !objective_read_uint(bytes, size, &at, 4, &rng)
        || !objective_read_uint(bytes, size, &at, 8, &acknowledged)
        || !objective_read_uint(bytes, size, &at, 8, &tick))
        return false;
    if (!state_version || state_version > 65535)
        return false;
    next.timers_enabled = (flags & 1) != 0;
    next.work_enabled = (flags & 2) != 0;
    next.initialized = (flags & 4) != 0;
    next.quarantined = (flags & 8) != 0;
    next.state_version = (unsigned)state_version;
    next.generation = (uint32_t)generation;
    next.rng = (uint32_t)rng;
    memcpy(&next.acknowledged, &acknowledged, sizeof(next.acknowledged));
    memcpy(&next.tick, &tick, sizeof(next.tick));
    uint64_t state_size = 0;
    if (!objective_read_uint(bytes, size, &at, 2, &state_size)
        || state_size > BOAT_PLUGIN_STATE_BYTES || state_size > size - at)
        return false;
    memcpy(next.state.bytes, bytes + at, (size_t)state_size);
    next.state.size = (unsigned)state_size;
    at += (size_t)state_size;
    uint64_t hud_count = 0;
    if (!objective_read_uint(bytes, size, &at, 1, &hud_count) || hud_count > BOAT_PLUGIN_HUD)
        return false;
    next.hud_count = (unsigned)hud_count;
    for (unsigned i = 0; i < next.hud_count; ++i) {
        if (!objective_read_text(bytes, size, &at, next.hud[i].id, sizeof(next.hud[i].id))
            || !objective_read_text(bytes, size, &at, next.hud[i].label, sizeof(next.hud[i].label)))
            return false;
        uint64_t value = 0;
        if (!objective_read_uint(bytes, size, &at, 8, &value))
            return false;
        memcpy(&next.hud[i].value, &value, sizeof(next.hud[i].value));
    }
    uint64_t timer_count = 0;
    if (!objective_read_uint(bytes, size, &at, 1, &timer_count) || timer_count > BOAT_PLUGIN_TIMERS)
        return false;
    next.timer_count = (unsigned)timer_count;
    for (unsigned i = 0; i < next.timer_count; ++i) {
        if (!objective_read_text(bytes, size, &at, next.timers[i].id, sizeof(next.timers[i].id)))
            return false;
        uint64_t due = 0;
        if (!objective_read_uint(bytes, size, &at, 8, &due))
            return false;
        memcpy(&next.timers[i].tick, &due, sizeof(next.timers[i].tick));
    }
    if (at != size || !boat_plugin_objective_timers_valid(&next)
        || !state_canonical(next.state.bytes, next.state.size))
        return false;
    *objective = next;
    return true;
}
