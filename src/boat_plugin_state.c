#include "boat_plugin_state.h"
#include "boat_plugin_lua.h"

#include "lauxlib.h"
#include "lua.h"

#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(lua_Integer) == sizeof(int64_t), "objective state requires Lua int64");

// Canonical wire tags: booleans, signed 64-bit integer, byte string, array, record.
// Integers/counts are little-endian. Empty tables are records; records sort byte-string keys.
// No floating point: state/ticks/commands preserve Lua integers without JSON conversion.
typedef struct {
    BoatPluginObjective next;
    const BoatPluginEvent *events;
    unsigned event_count, previous_version;
    BoatPluginTransition transition;
    const void *seen[BOAT_PLUGIN_STATE_TABLES];
    unsigned seen_count;
    BoatPluginState encoded;
    unsigned offset;
    bool schema;
} Transition;

static int invalid(lua_State *L, Transition *t, const char *message)
{
    t->schema = true;
    return luaL_error(L, "%s", message);
}

static void write_bytes(lua_State *L, Transition *t, const void *bytes, size_t size)
{
    if (size > BOAT_PLUGIN_STATE_BYTES - t->encoded.size)
        invalid(L, t, "state exceeds 16 KiB");
    memcpy(t->encoded.bytes + t->encoded.size, bytes, size);
    t->encoded.size += (unsigned)size;
}

static void write_uint(lua_State *L, Transition *t, uint64_t value, unsigned count)
{
    unsigned char bytes[8];
    for (unsigned i = 0; i < count; ++i) {
        bytes[i] = (unsigned char)value;
        value >>= 8;
    }
    write_bytes(L, t, bytes, count);
}

static void encode(lua_State *L, Transition *t, int index, unsigned depth)
{
    if (depth > BOAT_PLUGIN_STATE_DEPTH || !lua_checkstack(L, 8))
        invalid(L, t, "state exceeds nesting limit");
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
    case LUA_TBOOLEAN:
        write_uint(L, t, lua_toboolean(L, index) ? 't' : 'f', 1);
        return;
    case LUA_TNUMBER:
        if (!lua_isinteger(L, index))
            invalid(L, t, "state numbers must be signed 64-bit integers");
        write_uint(L, t, 'i', 1);
        write_uint(L, t, (uint64_t)lua_tointeger(L, index), 8);
        return;
    case LUA_TSTRING: {
        size_t size;
        const char *value = lua_tolstring(L, index, &size);
        if (size > 1024)
            invalid(L, t, "state string exceeds 1024 bytes");
        write_uint(L, t, 's', 1);
        write_uint(L, t, size, 2);
        write_bytes(L, t, value, size);
        return;
    }
    case LUA_TTABLE:
        break;
    default:
        invalid(L, t, "state contains an unsupported value");
    }
    if (lua_getmetatable(L, index))
        invalid(L, t, "state metatables are forbidden");
    const void *identity = lua_topointer(L, index);
    for (unsigned i = 0; i < t->seen_count; ++i)
        if (t->seen[i] == identity)
            invalid(L, t, "state contains a cycle or shared table");
    if (t->seen_count == BOAT_PLUGIN_STATE_TABLES)
        invalid(L, t, "state exceeds table limit");
    t->seen[t->seen_count++] = identity;
    unsigned entries = 0, integers = 0;
    char keys[256][64];
    lua_pushnil(L);
    while (lua_next(L, index)) {
        lua_pop(L, 1);
        if (entries == 256)
            invalid(L, t, "state table exceeds 256 entries");
        if (lua_isinteger(L, -1)) {
            if (lua_tointeger(L, -1) < 1 || lua_tointeger(L, -1) > 256)
                invalid(L, t, "state arrays require dense positive indices");
            ++integers;
        } else if (lua_type(L, -1) == LUA_TSTRING) {
            size_t size;
            const char *key = lua_tolstring(L, -1, &size);
            if (!size || size >= sizeof(keys[0]) || memchr(key, 0, size))
                invalid(L, t, "state record keys require 1..63 bytes without NUL");
            memcpy(keys[entries], key, size + 1);
        } else
            invalid(L, t, "state keys must be strings or array indices");
        ++entries;
    }
    if (integers && integers != entries)
        invalid(L, t, "state cannot mix array and record keys");
    write_uint(L, t, integers ? 'a' : 'r', 1);
    write_uint(L, t, entries, 2);
    if (!integers) {
        // Insertion sort gives canonical byte ordering independent of Lua traversal order.
        for (unsigned i = 1; i < entries; ++i) {
            char key[64];
            memcpy(key, keys[i], sizeof(key));
            unsigned j = i;
            while (j && strcmp(keys[j - 1], key) > 0) {
                memcpy(keys[j], keys[j - 1], sizeof(key));
                --j;
            }
            memcpy(keys[j], key, sizeof(key));
        }
    }
    for (unsigned i = 0; i < entries; ++i) {
        if (integers) {
            lua_rawgeti(L, index, i + 1);
            if (lua_isnil(L, -1))
                invalid(L, t, "state array has a gap");
        } else {
            size_t size = strlen(keys[i]);
            write_uint(L, t, size, 1);
            write_bytes(L, t, keys[i], size);
            lua_getfield(L, index, keys[i]);
        }
        encode(L, t, -1, depth + 1);
        lua_pop(L, 1);
    }
}

static uint64_t read_uint(lua_State *L, Transition *t, unsigned count)
{
    if (t->offset > t->next.state.size || count > t->next.state.size - t->offset)
        invalid(L, t, "truncated state checkpoint");
    uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i)
        value |= (uint64_t)t->next.state.bytes[t->offset++] << (i * 8);
    return value;
}

static void decode(lua_State *L, Transition *t, unsigned depth)
{
    if (depth > BOAT_PLUGIN_STATE_DEPTH || !lua_checkstack(L, 8))
        invalid(L, t, "checkpoint exceeds nesting limit");
    unsigned tag = (unsigned)read_uint(L, t, 1);
    switch (tag) {
    case 't':
    case 'f':
        lua_pushboolean(L, tag == 't');
        return;
    case 'i': {
        uint64_t bits = read_uint(L, t, 8);
        int64_t value;
        memcpy(&value, &bits, sizeof(value));
        lua_pushinteger(L, value);
        return;
    }
    case 's': {
        unsigned size = (unsigned)read_uint(L, t, 2);
        if (size > 1024 || size > t->next.state.size - t->offset)
            invalid(L, t, "invalid checkpoint string");
        lua_pushlstring(L, (const char *)t->next.state.bytes + t->offset, size);
        t->offset += size;
        return;
    }
    case 'a':
    case 'r': {
        unsigned count = (unsigned)read_uint(L, t, 2);
        if (count > 256 || ++t->seen_count > BOAT_PLUGIN_STATE_TABLES)
            invalid(L, t, "checkpoint exceeds table limits");
        lua_newtable(L);
        for (unsigned i = 0; i < count; ++i) {
            if (tag == 'r') {
                unsigned size = (unsigned)read_uint(L, t, 1);
                if (!size || size > 63 || size > t->next.state.size - t->offset)
                    invalid(L, t, "invalid checkpoint key");
                lua_pushlstring(L, (const char *)t->next.state.bytes + t->offset, size);
                t->offset += size;
            } else
                lua_pushinteger(L, i + 1);
            decode(L, t, depth + 1);
            lua_rawset(L, -3);
        }
        return;
    }
    default:
        invalid(L, t, "unknown checkpoint tag");
    }
}

static int random_integer(lua_State *L)
{
    Transition *t = lua_touserdata(L, lua_upvalueindex(1));
    uint32_t x = t->next.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    t->next.rng = x;
    lua_pushinteger(L, x);
    return 1;
}

static void integer_field(lua_State *L, const char *name, int64_t value)
{
    lua_pushinteger(L, value);
    lua_setfield(L, -2, name);
}

static void string_field(lua_State *L, const char *name, const char *value)
{
    lua_pushstring(L, value);
    lua_setfield(L, -2, name);
}

static void command_text(lua_State *L, Transition *t, int table, const char *key, char *out)
{
    lua_getfield(L, table, key);
    size_t size = 0;
    const char *value = lua_type(L, -1) == LUA_TSTRING ? lua_tolstring(L, -1, &size) : NULL;
    if (!value || !size || size >= BOAT_PLUGIN_TEXT)
        invalid(L, t, "resource id/label requires 1..23 printable ASCII characters");
    for (size_t i = 0; i < size; ++i)
        if ((unsigned char)value[i] < 32 || (unsigned char)value[i] > 126)
            invalid(L, t, "resource text requires printable ASCII");
    memcpy(out, value, size);
    out[size] = 0;
    lua_pop(L, 1);
}

static void commands(lua_State *L, Transition *t, int list)
{
    if (!lua_istable(L, list))
        invalid(L, t, "commands must be a dense list");
    size_t count = lua_rawlen(L, list), entries = 0;
    if (count > BOAT_PLUGIN_COMMANDS)
        invalid(L, t, "command capacity exceeded");
    lua_pushnil(L);
    while (lua_next(L, list)) {
        lua_pop(L, 1);
        if (!lua_isinteger(L, -1) || lua_tointeger(L, -1) < 1
            || (lua_Unsigned)lua_tointeger(L, -1) > count)
            invalid(L, t, "commands must be a dense list");
        ++entries;
    }
    if (entries != count)
        invalid(L, t, "command list has gaps");
    t->next.command_count = (unsigned)count;
    memset(t->next.commands, 0, sizeof(t->next.commands));
    char touched[BOAT_PLUGIN_COMMANDS][BOAT_PLUGIN_TEXT];
    bool timer_touched[BOAT_PLUGIN_COMMANDS];
    for (unsigned i = 0; i < count; ++i) {
        lua_rawgeti(L, list, i + 1);
        if (!lua_istable(L, -1))
            invalid(L, t, "command must be a record");
        int command = lua_absindex(L, -1);
        lua_getfield(L, command, "kind");
        const char *kind = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
        bool set = !strcmp(kind, "timer.set"), cancel = !strcmp(kind, "timer.cancel");
        bool timer = set || cancel;
        bool remove = !strcmp(kind, "hud.remove");
        if (lua_rawlen(L, -1) != strlen(kind) || (!timer && !remove && strcmp(kind, "hud.set")))
            invalid(L, t, "unsupported command kind");
        if (timer && !t->next.timers_enabled)
            invalid(L, t, "timer commands require objective.timers");
        BoatPluginCommand *intent = &t->next.commands[i];
        snprintf(intent->kind, sizeof(intent->kind), "%s", kind);
        intent->generation = t->next.generation;
        lua_pop(L, 1);
        lua_pushnil(L);
        while (lua_next(L, command)) {
            lua_pop(L, 1);
            const char *key = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
            if (lua_rawlen(L, -1) != strlen(key)
                || (strcmp(key, "kind") && strcmp(key, "id") && strcmp(key, "generation")
                    && (timer ? (!set || strcmp(key, "tick"))
                              : (remove || (strcmp(key, "label") && strcmp(key, "value"))))))
                invalid(L, t, "unknown command field");
        }
        lua_getfield(L, command, "generation");
        if (!lua_isinteger(L, -1) || lua_tointeger(L, -1) != t->next.generation)
            invalid(L, t, "stale resource generation");
        lua_pop(L, 1);
        BoatPluginHud hud = { 0 };
        command_text(L, t, command, "id", hud.id);
        for (unsigned j = 0; j < i; ++j)
            if (timer_touched[j] == timer && !strcmp(touched[j], hud.id))
                invalid(L, t, "conflicting commands for one resource id");
        memcpy(intent->id, hud.id, sizeof(hud.id));
        memcpy(touched[i], hud.id, sizeof(hud.id));
        timer_touched[i] = timer;
        if (timer) {
            int64_t due = 0;
            if (set) {
                lua_getfield(L, command, "tick");
                if (!lua_isinteger(L, -1) || lua_tointeger(L, -1) <= t->next.tick)
                    invalid(L, t, "timer tick must be an integer after the current tick");
                due = lua_tointeger(L, -1);
                intent->value = due;
                lua_pop(L, 1);
            }
            unsigned slot = 0;
            while (slot < t->next.timer_count && strcmp(t->next.timers[slot].id, hud.id))
                ++slot;
            if (slot < t->next.timer_count) {
                --t->next.timer_count;
                memmove(t->next.timers + slot, t->next.timers + slot + 1,
                    (t->next.timer_count - slot) * sizeof(t->next.timers[0]));
            }
            if (set) {
                if (t->next.timer_count == BOAT_PLUGIN_TIMERS)
                    invalid(L, t, "timer capacity exceeded");
                // Canonical order defines simultaneous delivery, independent of command order.
                slot = 0;
                while (slot < t->next.timer_count
                    && (t->next.timers[slot].tick < due
                        || (t->next.timers[slot].tick == due
                            && strcmp(t->next.timers[slot].id, hud.id) < 0)))
                    ++slot;
                memmove(t->next.timers + slot + 1, t->next.timers + slot,
                    (t->next.timer_count - slot) * sizeof(t->next.timers[0]));
                t->next.timers[slot] = (BoatPluginTimer) { .tick = due };
                memcpy(t->next.timers[slot].id, hud.id, sizeof(hud.id));
                ++t->next.timer_count;
            }
            memset(t->next.timers + t->next.timer_count, 0,
                (BOAT_PLUGIN_TIMERS - t->next.timer_count) * sizeof(t->next.timers[0]));
            lua_pop(L, 1);
            continue;
        }
        if (remove) {
            unsigned slot = 0;
            while (slot < t->next.hud_count && strcmp(t->next.hud[slot].id, hud.id))
                ++slot;
            if (slot < t->next.hud_count) {
                --t->next.hud_count;
                memmove(t->next.hud + slot, t->next.hud + slot + 1,
                    (t->next.hud_count - slot) * sizeof(t->next.hud[0]));
                memset(t->next.hud + t->next.hud_count, 0, sizeof(t->next.hud[0]));
            }
            lua_pop(L, 1);
            continue;
        }
        command_text(L, t, command, "label", hud.label);
        lua_getfield(L, command, "value");
        if (!lua_isinteger(L, -1))
            invalid(L, t, "HUD value must be an integer");
        hud.value = lua_tointeger(L, -1);
        intent->value = hud.value;
        memcpy(intent->label, hud.label, sizeof(hud.label));
        lua_pop(L, 1);
        unsigned slot = 0;
        while (slot < t->next.hud_count && strcmp(t->next.hud[slot].id, hud.id))
            ++slot;
        if (slot == BOAT_PLUGIN_HUD)
            invalid(L, t, "HUD capacity exceeded");
        if (slot == t->next.hud_count)
            ++t->next.hud_count;
        t->next.hud[slot] = hud;
        lua_pop(L, 1);
    }
}

static int transition(lua_State *L)
{
    Transition *t = lua_touserdata(L, 1);
    if (!lua_istable(L, 2))
        return invalid(L, t, "objective entry must return a callback table");
    const char *name = t->transition == BOAT_PLUGIN_OBJECTIVE_UPDATE ? "update"
        : t->transition == BOAT_PLUGIN_OBJECTIVE_MIGRATE             ? "migrate"
                                                                     : "init";
    lua_getfield(L, 2, name);
    if (!lua_isfunction(L, -1))
        return invalid(L, t, "objective callback is missing");
    lua_newtable(L);
    integer_field(L, "tick", t->next.tick);
    integer_field(L, "generation", t->next.generation);
    integer_field(L, "state_version", t->next.state_version ? t->next.state_version : 1);
    if (t->transition == BOAT_PLUGIN_OBJECTIVE_MIGRATE)
        integer_field(L, "previous_state_version", t->previous_version);
    lua_pushlightuserdata(L, t);
    lua_pushcclosure(L, random_integer, 1);
    lua_setfield(L, -2, "random");
    lua_newtable(L);
    for (unsigned i = 0; i < t->event_count; ++i) {
        const BoatPluginEvent *event = &t->events[i];
        lua_newtable(L);
        integer_field(L, "tick", event->tick);
        integer_field(L, "sequence", event->sequence);
        string_field(L, "kind", event->kind);
        string_field(L, "id", event->id);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "events");
    if (t->next.timers_enabled) {
        lua_newtable(L);
        unsigned due = 0;
        if (t->transition == BOAT_PLUGIN_OBJECTIVE_UPDATE)
            while (due < t->next.timer_count && t->next.timers[due].tick <= t->next.tick) {
                lua_newtable(L);
                integer_field(L, "tick", t->next.timers[due].tick);
                string_field(L, "id", t->next.timers[due].id);
                lua_rawseti(L, -2, due + 1);
                ++due;
            }
        lua_setfield(L, -2, "timers");
        t->next.timer_count -= due;
        memmove(
            t->next.timers, t->next.timers + due, t->next.timer_count * sizeof(t->next.timers[0]));
        memset(t->next.timers + t->next.timer_count, 0,
            (BOAT_PLUGIN_TIMERS - t->next.timer_count) * sizeof(t->next.timers[0]));
    }
    bool with_state = t->transition == BOAT_PLUGIN_OBJECTIVE_UPDATE
        || t->transition == BOAT_PLUGIN_OBJECTIVE_MIGRATE;
    if (with_state) {
        decode(L, t, 0);
        t->seen_count = 0;
        encode(L, t, -1, 0);
        if (t->offset != t->next.state.size || t->encoded.size != t->next.state.size
            || memcmp(t->encoded.bytes, t->next.state.bytes, t->encoded.size))
            return invalid(L, t, "checkpoint is not canonical");
        t->encoded.size = 0;
        t->seen_count = 0;
    }
    lua_call(L, with_state ? 2 : 1, 2);
    encode(L, t, -2, 0);
    commands(L, t, lua_absindex(L, -1));
    t->next.state = t->encoded;
    if (t->event_count)
        t->next.acknowledged = t->events[t->event_count - 1].sequence;
    t->next.initialized = true;
    t->next.quarantined = false;
    return 0;
}

static bool valid_timers(const BoatPluginObjective *o)
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

static BoatPluginStatus step(BoatPluginObjective *objective, const char *source, size_t size,
    BoatPluginTransition action, int64_t tick, const BoatPluginEvent *events, unsigned event_count,
    const BoatPluginLimits *limits, BoatPluginDiagnostic *diagnostic, unsigned previous_version)
{
    BoatPluginDiagnostic local = { 0 };
    if (!diagnostic)
        diagnostic = &local;
    *diagnostic = (BoatPluginDiagnostic) { 0 };
    if (!limits)
        limits = &boat_plugin_default_limits;
    bool reset = action == BOAT_PLUGIN_OBJECTIVE_RESET;
    bool init = action == BOAT_PLUGIN_OBJECTIVE_INIT;
    const char *error = NULL;
    if (!objective || !source || size > BOAT_PLUGIN_SOURCE_MAX || size > limits->source_bytes)
        error = "invalid objective or source size";
    else if (limits->memory_bytes > boat_plugin_default_limits.memory_bytes
        || limits->instructions > boat_plugin_default_limits.instructions)
        error = "transition budgets cannot exceed host limits";
    else if (action < BOAT_PLUGIN_OBJECTIVE_INIT || action > BOAT_PLUGIN_OBJECTIVE_RESET || tick < 0
        || objective->state_version > 65535 || !objective->generation || !objective->rng
        || objective->hud_count > BOAT_PLUGIN_HUD || !valid_timers(objective)
        || objective->state.size > BOAT_PLUGIN_STATE_BYTES || objective->acknowledged < 0
        || objective->tick < 0)
        error = "invalid checkpoint or transition";
    else if ((!init && !reset && !objective->initialized) || (init && objective->initialized))
        error = "transition requires the appropriate initialized state";
    else if (objective->quarantined && !reset && action != BOAT_PLUGIN_OBJECTIVE_MIGRATE)
        error = "objective is quarantined; reset or migrate explicitly";
    else if (tick < objective->tick
        || (action == BOAT_PLUGIN_OBJECTIVE_UPDATE && (tick <= objective->tick || tick % 12)))
        error = "update requires a later 12-tick boundary";
    else if (event_count > BOAT_PLUGIN_EVENTS || (event_count && !events)
        || ((init || reset || action == BOAT_PLUGIN_OBJECTIVE_MIGRATE) && event_count))
        error = "invalid event batch or event capacity exceeded";
    for (unsigned i = 0; !error && i < objective->hud_count; ++i)
        if (!memchr(objective->hud[i].id, 0, sizeof(objective->hud[i].id))
            || !memchr(objective->hud[i].label, 0, sizeof(objective->hud[i].label)))
            error = "invalid HUD checkpoint text";
    int64_t sequence = objective ? objective->acknowledged : 0;
    int64_t previous_tick = 0;
    for (unsigned i = 0; !error && i < event_count; ++i) {
        const BoatPluginEvent *event = &events[i];
        if (event->tick < previous_tick || event->tick > tick || event->sequence <= sequence
            || !memchr(event->kind, 0, sizeof(event->kind))
            || !memchr(event->id, 0, sizeof(event->id)))
            error = "events must be ordered, bounded and newer than acknowledgment";
        previous_tick = event->tick;
        sequence = event->sequence;
    }
    if (error) {
        diagnostic->status = BOAT_PLUGIN_SCHEMA;
        snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", error);
        if (objective && !init && !reset && action != BOAT_PLUGIN_OBJECTIVE_MIGRATE)
            objective->quarantined = true;
        return diagnostic->status;
    }
    Transition t = { .next = *objective,
        .events = events,
        .event_count = event_count,
        .transition = action,
        .previous_version = previous_version };
    t.next.tick = tick;
    if (init || reset) {
        t.next.state = (BoatPluginState) { 0 };
        t.next.timer_count = 0;
        memset(t.next.timers, 0, sizeof(t.next.timers));
        t.next.hud_count = 0;
        memset(t.next.hud, 0, sizeof(t.next.hud));
    }
    BoatPluginStatus status = boat_plugin_lua_transition(source, size, limits,
        action == BOAT_PLUGIN_OBJECTIVE_MIGRATE ? BOAT_PLUGIN_MIGRATION_INSTRUCTIONS
                                                : BOAT_PLUGIN_CALLBACK_INSTRUCTIONS,
        transition, &t, diagnostic);
    if (t.schema)
        status = diagnostic->status = BOAT_PLUGIN_SCHEMA;
    if (status == BOAT_PLUGIN_OK)
        *objective = t.next;
    else if (action != BOAT_PLUGIN_OBJECTIVE_MIGRATE)
        objective->quarantined = true;
    return status;
}

BoatPluginStatus boat_plugin_objective_step(BoatPluginObjective *objective, const char *source,
    size_t size, BoatPluginTransition action, int64_t tick, const BoatPluginEvent *events,
    unsigned event_count, const BoatPluginLimits *limits, BoatPluginDiagnostic *diagnostic)
{
    return step(objective, source, size, action, tick, events, event_count, limits, diagnostic,
        objective && objective->state_version ? objective->state_version : 1);
}

BoatPluginStatus boat_plugin_objective_migrate(BoatPluginObjective *objective, const char *source,
    size_t size, unsigned version, unsigned generation, bool timers, int64_t tick,
    BoatPluginDiagnostic *diagnostic)
{
    if (!objective || !version || version > 65535 || generation <= objective->generation) {
        if (diagnostic) {
            *diagnostic = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_SCHEMA };
            snprintf(diagnostic->message, sizeof(diagnostic->message),
                "invalid migration schema or generation");
        }
        return BOAT_PLUGIN_SCHEMA;
    }
    BoatPluginObjective next = *objective;
    unsigned previous = next.state_version ? next.state_version : 1;
    next.state_version = version;
    next.generation = generation;
    next.timers_enabled = timers;
    if (!timers) {
        next.timer_count = 0;
        memset(next.timers, 0, sizeof(next.timers));
    }
    BoatPluginStatus status = step(&next, source, size, BOAT_PLUGIN_OBJECTIVE_MIGRATE, tick, NULL,
        0, NULL, diagnostic, previous);
    if (status == BOAT_PLUGIN_OK)
        *objective = next;
    return status;
}

static void hash_bytes(uint64_t *hash, const void *bytes, size_t size)
{
    const unsigned char *p = bytes;
    for (size_t i = 0; i < size; ++i)
        *hash = (*hash ^ p[i]) * UINT64_C(1099511628211);
}

static void hash_integer(uint64_t *hash, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) {
        unsigned char byte = (unsigned char)value;
        hash_bytes(hash, &byte, 1);
        value >>= 8;
    }
}

uint64_t boat_plugin_objective_digest(const BoatPluginObjective *objective)
{
    if (!objective || objective->state.size > BOAT_PLUGIN_STATE_BYTES
        || objective->hud_count > BOAT_PLUGIN_HUD || !valid_timers(objective))
        return 0;
    for (unsigned i = 0; i < objective->hud_count; ++i)
        if (!memchr(objective->hud[i].id, 0, sizeof(objective->hud[i].id))
            || !memchr(objective->hud[i].label, 0, sizeof(objective->hud[i].label)))
            return 0;
    uint64_t hash = UINT64_C(14695981039346656037);
    hash_integer(&hash, objective->state.size);
    hash_bytes(&hash, objective->state.bytes, objective->state.size);
    hash_integer(&hash, objective->hud_count);
    for (unsigned i = 0; i < objective->hud_count; ++i) {
        hash_bytes(&hash, objective->hud[i].id, strlen(objective->hud[i].id) + 1);
        hash_bytes(&hash, objective->hud[i].label, strlen(objective->hud[i].label) + 1);
        hash_integer(&hash, (uint64_t)objective->hud[i].value);
    }
    hash_integer(&hash, objective->rng);
    hash_integer(&hash, (uint64_t)objective->acknowledged);
    hash_integer(&hash, (uint64_t)objective->tick);
    if (objective->state_version > 1)
        hash_integer(&hash, objective->state_version);
    hash_integer(&hash, objective->generation);
    hash_integer(&hash, objective->initialized);
    if (objective->timers_enabled) {
        hash_bytes(&hash, "timers", 6);
        hash_integer(&hash, objective->timer_count);
        for (unsigned i = 0; i < objective->timer_count; ++i) {
            hash_bytes(&hash, objective->timers[i].id, strlen(objective->timers[i].id) + 1);
            hash_integer(&hash, (uint64_t)objective->timers[i].tick);
        }
    }
    return hash;
}

bool boat_plugin_hud_compose(const BoatPluginHudOwner *owners, unsigned owner_count,
    BoatPluginHudRow *rows, unsigned capacity, unsigned *count)
{
    unsigned total = 0;
    if (!count || owner_count > BOAT_PLUGIN_HUD || (owner_count && !owners))
        return false;
    for (unsigned i = 0; i < owner_count; ++i) {
        const BoatPluginHudOwner *o = &owners[i];
        if (!o->owner || !o->owner[0] || strlen(o->owner) >= sizeof(rows[0].owner) || !o->objective
            || o->objective->hud_count > BOAT_PLUGIN_HUD
            || o->objective->hud_count > capacity - total)
            return false;
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(o->owner, owners[j].owner))
                return false;
        total += o->objective->hud_count;
    }
    if (total && !rows)
        return false;
    total = 0;
    for (unsigned i = 0; i < owner_count; ++i)
        for (unsigned j = 0; j < owners[i].objective->hud_count; ++j) {
            BoatPluginHudRow *row = &rows[total++];
            memset(row, 0, sizeof(*row));
            snprintf(row->owner, sizeof(row->owner), "%s", owners[i].owner);
            row->row = owners[i].objective->hud[j];
        }
    *count = total;
    return true;
}
