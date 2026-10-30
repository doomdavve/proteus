#include "boat_plugin_lua.h"

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// One evaluation's Lua state: its allocator account, instruction count and decoded output.
typedef struct {
    size_t live, peak, limit;
    uint64_t instructions, budget;
    bool exhausted; // The budget ran out; the resulting error is BOAT_PLUGIN_BUDGET.
    bool invalid; // The decoder rejected a value and filled the diagnostic.
    unsigned fields;
    BoatPluginVoyageData *data;
    BoatPluginRaceData *race;
    BoatPluginDiagnostic *diagnostic;
} Vm;

static void *allocate(void *context, void *block, size_t old_size, size_t size)
{
    Vm *vm = context;
    size_t old = block ? old_size : 0; // Without a block, OLD_SIZE is a type tag.
    if (size == 0) {
        free(block);
        vm->live -= old;
        return NULL;
    }
    if (size > old && size - old > vm->limit - vm->live)
        return NULL; // Lua raises a memory error, or shrinks without failing.
    void *resized = realloc(block, size);
    if (!resized)
        return NULL;
    vm->live = vm->live - old + size;
    if (vm->live > vm->peak)
        vm->peak = vm->live;
    return resized;
}

static void count(lua_State *L, lua_Debug *debug)
{
    void *context;
    lua_getallocf(L, &context);
    Vm *vm = context;
    vm->instructions += BOAT_PLUGIN_HOOK_STEP;
    if (vm->instructions > vm->budget) {
        vm->exhausted = true;
        lua_sethook(L, count, LUA_MASKCOUNT, 1); // Any further instruction fails as well.
        // Raised from the hook, the error carries the running line itself.
        lua_getinfo(L, "Sl", debug);
        luaL_error(L, "%s:%d: instruction budget exhausted", debug->short_src, debug->currentline);
    }
}

// The globals and library members a data-only definition may use. Everything else is removed,
// including load, require, pcall, metatables, string patterns, string.dump and math.random.
// io, os, debug, package, coroutine and utf8 are never compiled in.
static const char *const GLOBALS[]
    = { "assert", "error", "ipairs", "next", "pairs", "rawequal", "rawget", "rawlen", "select",
          "tonumber", "tostring", "type", "_G", "_VERSION", "string", "table", "math", NULL };
static const char *const STRING[]
    = { "byte", "char", "format", "len", "lower", "rep", "reverse", "sub", "upper", NULL };
static const char *const TABLE[] = { "concat", "insert", "pack", "remove", "sort", "unpack", NULL };
static const char *const MATH[] = { "abs", "acos", "asin", "atan", "ceil", "cos", "deg", "exp",
    "floor", "fmod", "huge", "log", "max", "maxinteger", "min", "mininteger", "modf", "pi", "rad",
    "sin", "sqrt", "tan", "tointeger", "type", "ult", NULL };

static bool listed(const char *const *names, const char *name)
{
    for (; *names; ++names)
        if (!strcmp(*names, name))
            return true;
    return false;
}

// Clear every field of the table at INDEX whose key is not in NAMES.
static void prune(lua_State *L, int index, const char *const *names)
{
    index = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, index)) {
        lua_pop(L, 1);
        if (lua_type(L, -1) != LUA_TSTRING || !listed(names, lua_tostring(L, -1))) {
            lua_pushvalue(L, -1);
            lua_pushnil(L);
            lua_rawset(L, index); // Clearing an existing field keeps the traversal valid.
        }
    }
}

static int setup(lua_State *L)
{
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    prune(L, -1, MATH);
    prune(L, -2, TABLE);
    prune(L, -3, STRING); // Also the strings' metatable __index.
    prune(L, -4, GLOBALS);
    return 0;
}

static int handler(lua_State *L)
{
    if (!lua_isstring(L, 1))
        lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    return 1;
}

static void text(char *out, size_t capacity, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(out, capacity, format, arguments);
    va_end(arguments);
}

// Bounded "PATH.KEY" or "PATH[INDEX]" without relying on truncating formats.
static void join(char *out, size_t capacity, const char *path, const char *key)
{
    size_t n = 0;
    for (const char *p = path; *p && n + 1 < capacity; ++p)
        out[n++] = *p;
    if (*path && n + 1 < capacity)
        out[n++] = '.';
    for (const char *p = key; *p && n + 1 < capacity; ++p)
        out[n++] = *p;
    out[n] = 0;
}

static void element(char *out, size_t capacity, const char *path, unsigned index)
{
    char suffix[16];
    text(suffix, sizeof(suffix), "[%u]", index);
    size_t n = strlen(path), s = strlen(suffix);
    if (n + s >= capacity)
        n = capacity - s - 1;
    memcpy(out, path, n);
    memcpy(out + n, suffix, s + 1);
}

static bool reject(Vm *vm, const char *path, const char *format, ...)
{
    BoatPluginDiagnostic *d = vm->diagnostic;
    vm->invalid = true;
    d->status = BOAT_PLUGIN_SCHEMA;
    d->line = 0;
    text(d->field, sizeof(d->field), "%s", path);
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(d->message, sizeof(d->message), format, arguments);
    va_end(arguments);
    return false;
}

static bool only(lua_State *L, Vm *vm, int table, const char *path, const char *const *keys)
{
    lua_pushnil(L);
    while (lua_next(L, table)) {
        lua_pop(L, 1);
        if (lua_type(L, -1) != LUA_TSTRING)
            return reject(vm, path, "has a %s key; fields are named", luaL_typename(L, -1));
        if (!listed(keys, lua_tostring(L, -1))) {
            char field[64];
            join(field, sizeof(field), path, lua_tostring(L, -1));
            return reject(vm, field, "is not a known field");
        }
    }
    return true;
}

// Push the raw value of TABLE's KEY and return its type; PATH receives the field's path.
static int field(lua_State *L, int table, const char *key, const char *path, char *out)
{
    join(out, 64, path, key);
    lua_pushstring(L, key);
    return lua_rawget(L, table);
}

static bool number(lua_State *L, Vm *vm, int table, const char *path, const char *key, double *out)
{
    char at[64];
    int type = field(L, table, key, path, at);
    if (type != LUA_TNUMBER)
        return reject(vm, at, type == LUA_TNIL ? "is missing" : "must be a number, not a %s",
            lua_typename(L, type));
    *out = lua_tonumber(L, -1);
    lua_pop(L, 1);
    ++vm->fields;
    return true;
}

static bool string(
    lua_State *L, Vm *vm, int table, const char *path, const char *key, char *out, size_t capacity)
{
    char at[64];
    int type = field(L, table, key, path, at);
    if (type != LUA_TSTRING)
        return reject(vm, at, type == LUA_TNIL ? "is missing" : "must be a string, not a %s",
            lua_typename(L, type));
    size_t size;
    const char *value = lua_tolstring(L, -1, &size);
    if (size == 0)
        return reject(vm, at, "must not be empty");
    if (size >= capacity)
        return reject(vm, at, "is longer than %u characters", (unsigned)capacity - 1);
    if (memchr(value, 0, size))
        return reject(vm, at, "contains a NUL character");
    memcpy(out, value, size + 1);
    lua_pop(L, 1);
    ++vm->fields;
    return true;
}

// Push TABLE's KEY, which must itself be a table holding only KEYS; returns its index or 0.
static int record(lua_State *L, Vm *vm, int table, const char *path, const char *key,
    const char *const *keys, char *at)
{
    int type = field(L, table, key, path, at);
    if (type != LUA_TTABLE) {
        reject(vm, at, type == LUA_TNIL ? "is missing" : "must be a table, not a %s",
            lua_typename(L, type));
        return 0;
    }
    int index = lua_absindex(L, -1);
    return only(L, vm, index, at, keys) ? index : 0;
}

static const char *const ROOT[] = { "voyage", "scene", "init", "update", "migrate", NULL };
static const char *const SCENE[] = { "version", NULL };
static const char *const VOYAGE[]
    = { "shelters", "arrival", "exposure", "exposed_fraction", "weather_range", NULL };
static const char *const SHELTER[] = { "id", "name", "x", "z", "heading", "radius", NULL };
static const char *const ARRIVAL[] = { "x", "z", "radius", "max_speed", NULL };
static const char *const EXPOSURE[] = { "x", "z", "radius", "height", "build", NULL };

// The shelters are a sequence: integer keys exactly 1..n.
static bool shelters(lua_State *L, Vm *vm, int voyage)
{
    char at[64];
    int type = field(L, voyage, "shelters", "voyage", at);
    if (type != LUA_TTABLE)
        return reject(vm, at, type == LUA_TNIL ? "is missing" : "must be a table, not a %s",
            lua_typename(L, type));
    int list = lua_absindex(L, -1);
    lua_Unsigned length = lua_rawlen(L, list), entries = 0;
    lua_pushnil(L);
    while (lua_next(L, list)) {
        lua_pop(L, 1);
        if (!lua_isinteger(L, -1) || lua_tointeger(L, -1) < 1
            || (lua_Unsigned)lua_tointeger(L, -1) > length)
            return reject(vm, at, "must be a list numbered from 1 without gaps");
        ++entries;
    }
    if (entries != length)
        return reject(vm, at, "must be a list numbered from 1 without gaps");
    if (length < 2 || length > BOAT_VOYAGE_SHELTERS)
        return reject(vm, at, "must list 2 to %d shelters, not %llu", BOAT_VOYAGE_SHELTERS,
            (unsigned long long)length);
    vm->data->shelter_count = (unsigned)length;
    for (unsigned i = 0; i < length; ++i) {
        BoatPluginShelterData *s = &vm->data->shelters[i];
        char path[64];
        element(path, sizeof(path), at, i + 1);
        lua_rawgeti(L, list, i + 1);
        if (lua_type(L, -1) != LUA_TTABLE)
            return reject(vm, path, "must be a table, not a %s", luaL_typename(L, -1));
        int shelter = lua_absindex(L, -1);
        if (!only(L, vm, shelter, path, SHELTER)
            || !string(L, vm, shelter, path, "id", s->id, sizeof(s->id))
            || !string(L, vm, shelter, path, "name", s->name, sizeof(s->name))
            || !number(L, vm, shelter, path, "x", &s->x)
            || !number(L, vm, shelter, path, "z", &s->z)
            || !number(L, vm, shelter, path, "heading", &s->heading)
            || !number(L, vm, shelter, path, "radius", &s->radius))
            return false;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return true;
}

// Copy the definition at index 2 into the data, using raw access only: no metamethod runs.
static int decode(lua_State *L)
{
    Vm *vm = lua_touserdata(L, 1);
    BoatPluginVoyageData *d = vm->data;
    if (lua_type(L, 2) != LUA_TTABLE) {
        reject(vm, "", "the entry must return a table, not a %s", luaL_typename(L, 2));
        return 0;
    }
    char at[64], part[64];
    if (!only(L, vm, 2, "", ROOT))
        return 0;
    lua_getfield(L, 2, "init");
    lua_getfield(L, 2, "update");
    d->callbacks = !lua_isnil(L, -1) || !lua_isnil(L, -2);
    if (d->callbacks && (!lua_isfunction(L, -1) || !lua_isfunction(L, -2))) {
        reject(vm, "init/update", "both objective callbacks must be functions");
        return 0;
    }
    lua_pop(L, 2);
    lua_getfield(L, 2, "migrate");
    d->migrates = !lua_isnil(L, -1);
    if (d->migrates && (!d->callbacks || !lua_isfunction(L, -1))) {
        reject(vm, "migrate", "requires init/update and must be a function");
        return 0;
    }
    lua_pop(L, 1);
    // L1.2 accepts only the versioned scene envelope. Later tasks add marina fields here and
    // validate them with the same copied-data rules.
    lua_pushstring(L, "scene");
    int scene_type = lua_rawget(L, 2);
    if (scene_type == LUA_TTABLE) {
        int scene = lua_absindex(L, -1);
        double version;
        if (!only(L, vm, scene, "scene", SCENE)
            || !number(L, vm, scene, "scene", "version", &version))
            return 0;
        d->has_scene = true;
        d->scene.version = version == (double)BOAT_SCENE_VERSION ? BOAT_SCENE_VERSION : 0;
    } else if (scene_type != LUA_TNIL) {
        reject(vm, "scene", "must be a table, not a %s", lua_typename(L, scene_type));
        return 0;
    }
    lua_pop(L, 1);
    int voyage = record(L, vm, 2, "", "voyage", VOYAGE, at);
    if (!voyage || !shelters(L, vm, voyage))
        return 0;
    int arrival = record(L, vm, voyage, at, "arrival", ARRIVAL, part);
    if (!arrival || !number(L, vm, arrival, part, "x", &d->arrival.x)
        || !number(L, vm, arrival, part, "z", &d->arrival.z)
        || !number(L, vm, arrival, part, "radius", &d->arrival.radius)
        || !number(L, vm, arrival, part, "max_speed", &d->arrival.max_speed))
        return 0;
    int exposure = record(L, vm, voyage, at, "exposure", EXPOSURE, part);
    if (!exposure || !number(L, vm, exposure, part, "x", &d->exposure.x)
        || !number(L, vm, exposure, part, "z", &d->exposure.z)
        || !number(L, vm, exposure, part, "radius", &d->exposure.radius)
        || !number(L, vm, exposure, part, "height", &d->exposure.height)
        || !number(L, vm, exposure, part, "build", &d->exposure.build))
        return 0;
    if (number(L, vm, voyage, at, "exposed_fraction", &d->exposed_fraction))
        number(L, vm, voyage, at, "weather_range", &d->weather_range);
    return 0;
}

// A Lua error message, "CHUNK:LINE: text" when it has a position.
static void failure(
    BoatPluginDiagnostic *d, BoatPluginStatus status, const char *chunk, const char *message)
{
    d->status = status;
    d->line = 0;
    d->field[0] = 0;
    text(d->file, sizeof(d->file), "%s", chunk);
    size_t n = strlen(chunk);
    if (message && !strncmp(message, chunk, n) && message[n] == ':') {
        const char *p = message + n + 1;
        int line = 0;
        while (*p >= '0' && *p <= '9' && line < 10000000)
            line = line * 10 + *p++ - '0';
        if (p > message + n + 1 && *p == ':') {
            d->line = line;
            message = p + 1 + (p[1] == ' ');
        }
    }
    text(d->message, sizeof(d->message), "%s", message ? message : "unknown error");
}

static const char *const RACE_ROOT[] = { "race", NULL };
static const char *const RACE[] = { "start", "gates", "grid", "source", "storm", "rules", NULL };
static const char *const GATE[] = { BOAT_PLUGIN_GATE_FIELDS, NULL };
static const char *const POINT[] = { "x", "z", NULL };
static const char *const SOURCE[] = { BOAT_PLUGIN_SOURCE_FIELDS, NULL };
static const char *const STORM[] = { BOAT_PLUGIN_STORM_FIELDS, NULL };
static const char *const RULES[] = { BOAT_PLUGIN_RULE_FIELDS, NULL };

static bool numbers(
    lua_State *L, Vm *vm, int table, const char *path, const char *const *keys, double *out)
{
    if (!only(L, vm, table, path, keys))
        return false;
    for (unsigned i = 0; keys[i]; ++i)
        if (!number(L, vm, table, path, keys[i], &out[i]))
            return false;
    return true;
}

static bool numeric_record(
    lua_State *L, Vm *vm, int parent, const char *key, const char *const *keys, double *out)
{
    char at[64];
    int table = record(L, vm, parent, "race", key, keys, at);
    if (!table || !numbers(L, vm, table, at, keys, out))
        return false;
    lua_pop(L, 1);
    return true;
}

static bool numeric_list(lua_State *L, Vm *vm, int parent, const char *key, const char *const *keys,
    double *out, unsigned stride, unsigned min, unsigned max, unsigned *count_out)
{
    char at[64];
    if (field(L, parent, key, "race", at) != LUA_TTABLE)
        return reject(vm, at, "must be a dense list");
    int list = lua_absindex(L, -1);
    size_t count = lua_rawlen(L, list), entries = 0;
    if (count < min || count > max)
        return reject(vm, at, "requires %u..%u entries", min, max);
    lua_pushnil(L);
    while (lua_next(L, list)) {
        lua_pop(L, 1);
        if (!lua_isinteger(L, -1) || lua_tointeger(L, -1) < 1
            || (lua_Unsigned)lua_tointeger(L, -1) > count)
            return reject(vm, at, "must be a dense list without extra keys");
        ++entries;
    }
    if (entries != count)
        return reject(vm, at, "must be a dense list without gaps");
    for (unsigned i = 0; i < count; ++i) {
        char path[64];
        element(path, sizeof(path), at, i + 1);
        lua_rawgeti(L, list, i + 1);
        if (lua_type(L, -1) != LUA_TTABLE)
            return reject(vm, path, "must be a table");
        if (!numbers(L, vm, lua_absindex(L, -1), path, keys, out + i * stride))
            return false;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    *count_out = (unsigned)count;
    return true;
}

static int decode_race(lua_State *L)
{
    Vm *vm = lua_touserdata(L, 1);
    if (lua_type(L, 2) != LUA_TTABLE) {
        reject(vm, "", "the entry must return a table");
        return 0;
    }
    char at[64];
    if (!only(L, vm, 2, "", RACE_ROOT))
        return 0;
    int race = record(L, vm, 2, "", "race", RACE, at);
    BoatPluginRaceData *d = vm->race;
    unsigned grid_count;
    if (!race || !numeric_record(L, vm, race, "start", GATE, d->start)
        || !numeric_list(
            L, vm, race, "gates", GATE, &d->gates[0][0], 5, 1, BOAT_RACE_GATES, &d->gate_count)
        || !numeric_list(L, vm, race, "grid", POINT, &d->grid[0][0], 2, BOAT_FLEET_SLOTS,
            BOAT_FLEET_SLOTS, &grid_count)
        || !numeric_record(L, vm, race, "source", SOURCE, d->source)
        || !numeric_record(L, vm, race, "storm", STORM, d->storm)
        || !numeric_record(L, vm, race, "rules", RULES, d->rules))
        return 0;
    return 0;
}

static BoatPluginStatus run(
    lua_State *L, Vm *vm, const char *source, size_t size, const char *chunk)
{
    BoatPluginDiagnostic *d = vm->diagnostic;
    lua_pushcfunction(L, setup);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        failure(d, BOAT_PLUGIN_MEMORY, chunk, "not enough memory to open the libraries");
        return d->status;
    }
    char name[24];
    text(name, sizeof(name), "=%s", chunk);
    lua_pushcfunction(L, handler);
    int status = luaL_loadbufferx(L, source, size, name, "t");
    if (status != LUA_OK) {
        failure(d, status == LUA_ERRMEM ? BOAT_PLUGIN_MEMORY : BOAT_PLUGIN_SYNTAX, chunk,
            lua_tostring(L, -1));
        return d->status;
    }
    lua_sethook(L, count, LUA_MASKCOUNT, BOAT_PLUGIN_HOOK_STEP);
    status = lua_pcall(L, 0, 1, -2);
    lua_sethook(L, NULL, 0, 0);
    if (status != LUA_OK) {
        failure(d,
            vm->exhausted              ? BOAT_PLUGIN_BUDGET
                : status == LUA_ERRMEM ? BOAT_PLUGIN_MEMORY
                                       : BOAT_PLUGIN_RUNTIME,
            chunk, lua_tostring(L, -1));
        return d->status;
    }
    int result = lua_gettop(L);
    lua_pushcfunction(L, vm->race ? decode_race : decode);
    lua_pushlightuserdata(L, vm);
    lua_pushvalue(L, result);
    status = lua_pcall(L, 2, 0, 0);
    if (status != LUA_OK) {
        failure(d, BOAT_PLUGIN_MEMORY, chunk, "not enough memory to decode the definition");
        return d->status;
    }
    if (vm->invalid) {
        text(d->file, sizeof(d->file), "%s", chunk);
        return d->status;
    }
    return BOAT_PLUGIN_OK;
}

BoatPluginStatus boat_plugin_lua_voyage(const char *source, size_t size, const char *chunk,
    const BoatPluginLimits *limits, BoatPluginVoyageData *out, BoatPluginDiagnostic *diagnostic)
{
    BoatPluginVoyageData data = { 0 };
    Vm vm = { .limit = limits->memory_bytes,
        .budget = limits->instructions,
        .data = &data,
        .diagnostic = diagnostic };
    BoatPluginStatus status;
    lua_State *L = lua_newstate(allocate, &vm);
    if (!L) {
        failure(diagnostic, BOAT_PLUGIN_MEMORY, chunk, "not enough memory for a Lua state");
        status = BOAT_PLUGIN_MEMORY;
    } else {
        status = run(L, &vm, source, size, chunk);
        lua_close(L);
    }
    diagnostic->cost = (BoatPluginCost) { .instructions = vm.instructions,
        .entry_instructions = vm.instructions,
        .peak_bytes = vm.peak,
        .leaked_bytes = vm.live,
        .fields = vm.fields };
    if (status == BOAT_PLUGIN_OK)
        *out = data;
    return status;
}

BoatPluginStatus boat_plugin_lua_race(const char *source, size_t size, const char *chunk,
    const BoatPluginLimits *limits, BoatPluginRaceData *out, BoatPluginDiagnostic *diagnostic)
{
    BoatPluginRaceData data = { 0 };
    Vm vm = { .limit = limits->memory_bytes,
        .budget = limits->instructions,
        .race = &data,
        .diagnostic = diagnostic };
    BoatPluginStatus status;
    lua_State *L = lua_newstate(allocate, &vm);
    if (!L) {
        failure(diagnostic, BOAT_PLUGIN_MEMORY, chunk, "not enough memory for a Lua state");
        status = BOAT_PLUGIN_MEMORY;
    } else {
        status = run(L, &vm, source, size, chunk);
        lua_close(L);
    }
    diagnostic->cost = (BoatPluginCost) { .instructions = vm.instructions,
        .entry_instructions = vm.instructions,
        .peak_bytes = vm.peak,
        .leaked_bytes = vm.live,
        .fields = vm.fields };
    if (status == BOAT_PLUGIN_OK)
        *out = data;
    return status;
}

BoatPluginStatus boat_plugin_lua_transition(const char *source, size_t size,
    const BoatPluginLimits *limits, uint64_t callback_budget, int (*callback)(lua_State *),
    void *context, BoatPluginDiagnostic *diagnostic)
{
    Vm vm = {
        .limit = limits->memory_bytes, .budget = limits->instructions, .diagnostic = diagnostic
    };
    lua_State *L = lua_newstate(allocate, &vm);
    int status = LUA_ERRMEM;
    uint64_t entry_instructions = 0;
    bool callback_started = false;
    if (L) {
        lua_pushcfunction(L, setup);
        status = lua_pcall(L, 0, 0, 0);
        if (status == LUA_OK)
            status = luaL_loadbufferx(L, source, size, "=main.lua", "t");
        if (status != LUA_OK)
            failure(diagnostic, status == LUA_ERRMEM ? BOAT_PLUGIN_MEMORY : BOAT_PLUGIN_SYNTAX,
                "main.lua",
                lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "non-string Lua error");
        else {
            lua_sethook(L, count, LUA_MASKCOUNT, BOAT_PLUGIN_HOOK_STEP);
            status = lua_pcall(L, 0, 1, 0);
            entry_instructions = vm.instructions;
            if (status == LUA_OK) {
                callback_started = true;
                vm.instructions = 0;
                vm.budget = callback_budget < limits->instructions ? callback_budget
                                                                   : limits->instructions;
                // Reset the hook counter so each phase gets its own instruction allowance.
                lua_sethook(L, count, LUA_MASKCOUNT, BOAT_PLUGIN_HOOK_STEP);
                lua_pushcfunction(L, callback);
                lua_pushlightuserdata(L, context);
                lua_pushvalue(L, 1);
                status = lua_pcall(L, 2, 0, 0);
            }
            lua_sethook(L, NULL, 0, 0);
            if (status != LUA_OK)
                failure(diagnostic,
                    vm.exhausted               ? BOAT_PLUGIN_BUDGET
                        : status == LUA_ERRMEM ? BOAT_PLUGIN_MEMORY
                                               : BOAT_PLUGIN_RUNTIME,
                    "main.lua",
                    lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "non-string Lua error");
        }
        lua_close(L);
    } else
        failure(diagnostic, BOAT_PLUGIN_MEMORY, "main.lua", "not enough memory for a Lua state");
    diagnostic->cost = (BoatPluginCost) { .instructions
        = entry_instructions + (callback_started ? vm.instructions : 0),
        .entry_instructions = entry_instructions,
        .callback_instructions = callback_started ? vm.instructions : 0,
        .peak_bytes = vm.peak,
        .leaked_bytes = vm.live };
    return status == LUA_OK ? BOAT_PLUGIN_OK : diagnostic->status;
}
