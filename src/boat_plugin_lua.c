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
    BoatPluginFreeData *free;
    BoatPluginCourseData *course;
    BoatPluginDiagnostic *diagnostic;
} Vm;

static unsigned lua_states;

unsigned boat_plugin_lua_states(void) { return lua_states; }

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
static const char *const SCENE[] = { "version", "marina", "traffic", "gulls", NULL };
static const char *const MARINA[] = { "entrance", "berths", "modules", NULL };
static const char *const TRAFFIC[] = { "vessels", NULL };
static const char *const GULLS[] = { "flocks", NULL };
static const char *const FLOCK[] = { "id", "recipe", "x", "z", "count", "seed", "phase", "radius",
    "height", "rate", "cues", NULL };
static const char *const CUE[] = { "kind", "start", "stop", "fade", "amplitude", NULL };
static const char *const VESSEL[]
    = { "id", "retain", "recipe", "route", "speed", "phase", "period", "stops", NULL };
static const char *const STOP[] = { "id", "dwell", NULL };
static const char *const ENTRANCE[] = { "x", "z", "width", NULL };
static const char *const BERTH[] = { "id", "recipe", "x", "z", "heading", "mooring", NULL };
static const char *const MODULE[] = { "id", "kind", "x", "z", "heading", NULL };
static const char *const GANGWAY[] = { "id", "kind", "anchor", "attaches", NULL };
static const char *const ANCHOR[] = { "x", "z", NULL };
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

// L2.2 marina berths. The list is dense 0..7; every field is copied, and names are resolved
// against the native registry before the Lua state closes.
static bool scene_berths(lua_State *L, Vm *vm, int marina, BoatSceneDefinition *s)
{
    char at[64];
    int type = field(L, marina, "berths", "scene.marina", at);
    if (type == LUA_TNIL) {
        lua_pop(L, 1);
        return true;
    }
    if (type != LUA_TTABLE)
        return reject(vm, at, "must be a table, not a %s", lua_typename(L, type));
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
    if (length > BOAT_SCENE_BERTHS)
        return reject(vm, at, "must list 0 to %d berths, not %llu", BOAT_SCENE_BERTHS,
            (unsigned long long)length);
    s->berths_present = true;
    s->berth_count = (unsigned)length;
    for (unsigned i = 0; i < length; ++i) {
        BoatSceneBerth *b = &s->berths[i];
        char path[64], recipe[24], mooring[24];
        element(path, sizeof(path), at, i + 1);
        lua_rawgeti(L, list, i + 1);
        if (lua_type(L, -1) != LUA_TTABLE)
            return reject(vm, path, "must be a table, not a %s", luaL_typename(L, -1));
        int entry = lua_absindex(L, -1);
        double x, z, heading;
        if (!only(L, vm, entry, path, BERTH)
            || !string(L, vm, entry, path, "id", b->id, sizeof(b->id))
            || !string(L, vm, entry, path, "recipe", recipe, sizeof(recipe))
            || !string(L, vm, entry, path, "mooring", mooring, sizeof(mooring))
            || !number(L, vm, entry, path, "x", &x) || !number(L, vm, entry, path, "z", &z)
            || !number(L, vm, entry, path, "heading", &heading))
            return false;
        b->x = (float)x;
        b->z = (float)z;
        b->heading = (float)heading;
        BoatSceneBerthRecipe r;
        BoatSceneMooring m;
        if (!boat_scene_berth_recipe_from_name(recipe, &r)) {
            char field[64];
            join(field, sizeof(field), path, "recipe");
            return reject(vm, field, "is not a registered berth recipe");
        }
        if (!boat_scene_mooring_from_name(mooring, &m)) {
            char field[64];
            join(field, sizeof(field), path, "mooring");
            return reject(vm, field, "must be pontoon or none");
        }
        b->recipe = (unsigned char)r;
        b->mooring = (unsigned char)m;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return true;
}

static bool scene_entrance(lua_State *L, Vm *vm, int marina, BoatSceneDefinition *s)
{
    char at[64];
    int type = field(L, marina, "entrance", "scene.marina", at);
    if (type == LUA_TNIL) {
        lua_pop(L, 1);
        return true;
    }
    if (type != LUA_TTABLE)
        return reject(vm, at, "must be a table, not a %s", lua_typename(L, type));
    int entrance = lua_absindex(L, -1);
    double x, z, width;
    if (!only(L, vm, entrance, at, ENTRANCE) || !number(L, vm, entrance, at, "x", &x)
        || !number(L, vm, entrance, at, "z", &z) || !number(L, vm, entrance, at, "width", &width))
        return false;
    s->entrance_x = (float)x;
    s->entrance_z = (float)z;
    s->entrance_width = (float)width;
    s->entrance_present = true;
    lua_pop(L, 1);
    return true;
}

static bool scene_modules(lua_State *L, Vm *vm, int marina, BoatSceneDefinition *s)
{
    char at[64];
    int type = field(L, marina, "modules", "scene.marina", at);
    if (type == LUA_TNIL) {
        lua_pop(L, 1);
        return true;
    }
    if (type != LUA_TTABLE)
        return reject(vm, at, "must be a table, not a %s", lua_typename(L, type));
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
    if (length > BOAT_SCENE_MODULES)
        return reject(vm, at, "must list 0 to %d modules, not %llu", BOAT_SCENE_MODULES,
            (unsigned long long)length);
    s->modules_present = true;
    s->module_count = (unsigned)length;
    for (unsigned i = 0; i < length; ++i) {
        BoatSceneModule *m = &s->modules[i];
        char path[64], kind[24];
        element(path, sizeof(path), at, i + 1);
        lua_rawgeti(L, list, i + 1);
        if (lua_type(L, -1) != LUA_TTABLE)
            return reject(vm, path, "must be a table, not a %s", luaL_typename(L, -1));
        int entry = lua_absindex(L, -1);
        char kind_path[64];
        if (field(L, entry, "kind", path, kind_path) != LUA_TSTRING) {
            reject(vm, kind_path, "must be a string");
            return false;
        }
        size_t kind_size;
        const char *kind_text = lua_tolstring(L, -1, &kind_size);
        if (kind_size >= sizeof(kind)) {
            reject(vm, kind_path, "is longer than %u characters", (unsigned)sizeof(kind) - 1);
            return false;
        }
        memcpy(kind, kind_text, kind_size + 1);
        lua_pop(L, 1);
        BoatSceneModuleKind k;
        if (!boat_scene_module_kind_from_name(kind, &k)) {
            reject(vm, kind_path, "is not a registered module kind");
            return false;
        }
        m->kind = (unsigned char)k;
        if (k == BOAT_SCENE_MODULE_GANGWAY) {
            if (!only(L, vm, entry, path, GANGWAY)
                || !string(L, vm, entry, path, "id", m->id, sizeof(m->id))
                || !string(L, vm, entry, path, "attaches", m->attaches, sizeof(m->attaches)))
                return false;
            char anchor_path[64];
            int anchor_type = field(L, entry, "anchor", path, anchor_path);
            if (anchor_type != LUA_TTABLE)
                return reject(
                    vm, anchor_path, "must be a table, not a %s", lua_typename(L, anchor_type));
            int anchor = lua_absindex(L, -1);
            double x, z;
            if (!only(L, vm, anchor, anchor_path, ANCHOR)
                || !number(L, vm, anchor, anchor_path, "x", &x)
                || !number(L, vm, anchor, anchor_path, "z", &z))
                return false;
            m->anchor_x = (float)x;
            m->anchor_z = (float)z;
            m->x = (float)x;
            m->z = (float)z;
            m->heading = 0;
            lua_pop(L, 1);
        } else {
            double x, z, heading;
            if (!only(L, vm, entry, path, MODULE)
                || !string(L, vm, entry, path, "id", m->id, sizeof(m->id))
                || !number(L, vm, entry, path, "x", &x) || !number(L, vm, entry, path, "z", &z)
                || !number(L, vm, entry, path, "heading", &heading))
                return false;
            m->x = (float)x;
            m->z = (float)z;
            m->heading = (float)heading;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return true;
}

// Decode a nonnegative integer count/tick without narrowing silently.
static bool unsigned_value(
    lua_State *L, Vm *vm, int table, const char *path, const char *key, double high, unsigned *out)
{
    double value;
    if (!number(L, vm, table, path, key, &value))
        return false;
    if (!(value >= 0) || value > high || value != (double)(unsigned)value) {
        char at[64];
        join(at, sizeof(at), path, key);
        return reject(vm, at, "must be an integer in 0..%u", (unsigned)high);
    }
    *out = (unsigned)value;
    return true;
}

// L4.2: bounded gull flocks and cue intervals. Cue envelopes are evaluated natively from the
// session tick; no spawn/despawn callback or timer state is created here.
static bool scene_gulls(lua_State *L, Vm *vm, int scene, BoatSceneDefinition *s)
{
    char at[64];
    int type = field(L, scene, "gulls", "scene", at);
    if (type == LUA_TNIL) {
        lua_pop(L, 1);
        return true;
    }
    if (type != LUA_TTABLE)
        return reject(vm, at, "must be a table, not a %s", lua_typename(L, type));
    int gulls = lua_absindex(L, -1);
    if (!only(L, vm, gulls, at, GULLS))
        return false;
    char list_path[64];
    type = field(L, gulls, "flocks", at, list_path);
    if (type != LUA_TTABLE)
        return reject(vm, list_path, "must be a table, not a %s", lua_typename(L, type));
    int list = lua_absindex(L, -1);
    lua_Unsigned length = lua_rawlen(L, list), entries = 0;
    lua_pushnil(L);
    while (lua_next(L, list)) {
        lua_pop(L, 1);
        if (!lua_isinteger(L, -1) || lua_tointeger(L, -1) < 1
            || (lua_Unsigned)lua_tointeger(L, -1) > length)
            return reject(vm, list_path, "must be a list numbered from 1 without gaps");
        ++entries;
    }
    if (entries != length)
        return reject(vm, list_path, "must be a list numbered from 1 without gaps");
    if (length > WATERFRONT_FLOCKS)
        return reject(vm, list_path, "must list 0 to %d flocks, not %llu", WATERFRONT_FLOCKS,
            (unsigned long long)length);
    s->gulls_present = true;
    s->gulls.flock_count = (unsigned)length;
    for (unsigned i = 0; i < length; ++i) {
        WaterfrontFlock *f = &s->gulls.flocks[i];
        char path[64], recipe[WATERFRONT_FLOCK_TEXT];
        element(path, sizeof(path), list_path, i + 1);
        lua_rawgeti(L, list, i + 1);
        if (lua_type(L, -1) != LUA_TTABLE)
            return reject(vm, path, "must be a table, not a %s", luaL_typename(L, -1));
        int entry = lua_absindex(L, -1);
        double x, z, phase, radius, height, rate;
        char recipe_path[64];
        join(recipe_path, sizeof(recipe_path), path, "recipe");
        if (!only(L, vm, entry, path, FLOCK)
            || !string(L, vm, entry, path, "id", f->id, sizeof(f->id))
            || !string(L, vm, entry, path, "recipe", recipe, sizeof(recipe))
            || !number(L, vm, entry, path, "x", &x) || !number(L, vm, entry, path, "z", &z)
            || !number(L, vm, entry, path, "phase", &phase)
            || !number(L, vm, entry, path, "radius", &radius)
            || !number(L, vm, entry, path, "height", &height)
            || !number(L, vm, entry, path, "rate", &rate))
            return false;
        WaterfrontFlockRecipe r;
        if (!waterfront_flock_recipe_from_name(recipe, &r))
            return reject(vm, recipe_path, "is not a registered flock recipe");
        f->recipe = (unsigned char)r;
        f->x = (float)x;
        f->z = (float)z;
        f->phase = (float)phase;
        f->radius = (float)radius;
        f->height = (float)height;
        f->rate = (float)rate;
        f->follow_actor = -1;
        f->implicit_cues = false;
        if (!unsigned_value(L, vm, entry, path, "count", WATERFRONT_FLOCK_BIRDS, &f->count)
            || !unsigned_value(L, vm, entry, path, "seed", 4294967295.0, &f->seed))
            return false;
        char cues_path[64];
        join(cues_path, sizeof(cues_path), path, "cues");
        int cues_type = field(L, entry, "cues", path, cues_path);
        if (cues_type == LUA_TNIL) {
            lua_pop(L, 1);
            f->cue_count = 0;
            lua_pop(L, 1);
            continue;
        }
        if (cues_type != LUA_TTABLE)
            return reject(vm, cues_path, "must be a table, not a %s", lua_typename(L, cues_type));
        int cues = lua_absindex(L, -1);
        lua_Unsigned cue_count = lua_rawlen(L, cues);
        if (cue_count > WATERFRONT_FLOCK_CUES)
            return reject(vm, cues_path, "must list 0 to %d cues, not %llu", WATERFRONT_FLOCK_CUES,
                (unsigned long long)cue_count);
        f->cue_count = (unsigned)cue_count;
        for (unsigned c = 0; c < cue_count; ++c) {
            WaterfrontFlockCue *cue = &f->cues[c];
            char cue_path[64], kind_path[64], kind[16];
            element(cue_path, sizeof(cue_path), cues_path, c + 1);
            lua_rawgeti(L, cues, c + 1);
            if (lua_type(L, -1) != LUA_TTABLE)
                return reject(vm, cue_path, "must be a table, not a %s", luaL_typename(L, -1));
            int item = lua_absindex(L, -1);
            join(kind_path, sizeof(kind_path), cue_path, "kind");
            if (!only(L, vm, item, cue_path, CUE)
                || !string(L, vm, item, cue_path, "kind", kind, sizeof(kind))
                || !unsigned_value(L, vm, item, cue_path, "start", 4294967295.0, &cue->start_tick)
                || !unsigned_value(L, vm, item, cue_path, "stop", 4294967295.0, &cue->end_tick)
                || !number(L, vm, item, cue_path, "amplitude", &x))
                return false;
            if (!strcmp(kind, "feeding"))
                cue->kind = WATERFRONT_FLOCK_CUE_FEEDING;
            else if (!strcmp(kind, "alarm"))
                cue->kind = WATERFRONT_FLOCK_CUE_ALARM;
            else
                return reject(vm, kind_path, "must be feeding or alarm");
            cue->amplitude = (float)x;
            char fade_path[64];
            join(fade_path, sizeof(fade_path), cue_path, "fade");
            int fade_type = field(L, item, "fade", cue_path, fade_path);
            if (fade_type == LUA_TNIL) {
                lua_pop(L, 1);
                cue->fade_ticks = 0;
            } else {
                lua_pop(L, 1);
                if (!unsigned_value(L, vm, item, cue_path, "fade", 4294967295.0, &cue->fade_ticks))
                    return false;
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 2); // cues list and flock entry
    }
    lua_pop(L, 2); // flocks list and gulls table
    return true;
}

// L3.3: ordered stop overrides for a registered stopped route. IDs must match the preset's
// named pier anchors in order; an omitted dwell leaves that stop on the shared remainder.
static bool scene_stops(
    lua_State *L, Vm *vm, int entry, const char *path, WaterfrontRouteActor *v, char *out)
{
    int type = field(L, entry, "stops", path, out);
    if (type == LUA_TNIL) {
        lua_pop(L, 1);
        return true;
    }
    if (type != LUA_TTABLE)
        return reject(vm, out, "must be a table, not a %s", lua_typename(L, type));
    int list = lua_absindex(L, -1);
    lua_Unsigned length = lua_rawlen(L, list), entries = 0;
    lua_pushnil(L);
    while (lua_next(L, list)) {
        lua_pop(L, 1);
        if (!lua_isinteger(L, -1) || lua_tointeger(L, -1) < 1
            || (lua_Unsigned)lua_tointeger(L, -1) > length)
            return reject(vm, out, "must be a list numbered from 1 without gaps");
        ++entries;
    }
    if (entries != length)
        return reject(vm, out, "must be a list numbered from 1 without gaps");
    if ((int)length != (int)v->stop_count)
        return reject(vm, out, "must list all %u stops of route %s", v->stop_count, v->id);
    for (unsigned i = 0; i < length; ++i) {
        char stop_path[64], id[WATERFRONT_ROUTE_TEXT];
        element(stop_path, sizeof(stop_path), out, i + 1);
        lua_rawgeti(L, list, i + 1);
        if (lua_type(L, -1) != LUA_TTABLE)
            return reject(vm, stop_path, "must be a table, not a %s", luaL_typename(L, -1));
        int stop = lua_absindex(L, -1);
        if (!only(L, vm, stop, stop_path, STOP)
            || !string(L, vm, stop, stop_path, "id", id, sizeof(id)))
            return false;
        if (strcmp(id, v->stops[i].id))
            return reject(vm, stop_path, "must name the route's stop %s", v->stops[i].id);
        char dwell_path[64];
        int dwell_type = field(L, stop, "dwell", stop_path, dwell_path);
        if (dwell_type == LUA_TNIL) {
            lua_pop(L, 1);
            v->stops[i].dwell = -1;
        } else {
            lua_pop(L, 1);
            double dwell;
            if (!number(L, vm, stop, stop_path, "dwell", &dwell) || !(dwell >= 0))
                return reject(vm, dwell_path, "must be zero or positive seconds");
            v->stops[i].dwell = (float)dwell;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return true;
}

// L3.2: a present vessel list replaces all travelling craft. Entries either retain a
// registered native actor or select a registered route and override its timetable.
static bool scene_traffic(lua_State *L, Vm *vm, int scene, BoatSceneDefinition *s)
{
    char at[64];
    int type = field(L, scene, "traffic", "scene", at);
    if (type == LUA_TNIL) {
        lua_pop(L, 1);
        return true;
    }
    if (type != LUA_TTABLE)
        return reject(vm, at, "must be a table, not a %s", lua_typename(L, type));
    int traffic = lua_absindex(L, -1);
    if (!only(L, vm, traffic, at, TRAFFIC))
        return false;
    char list_path[64];
    type = field(L, traffic, "vessels", at, list_path);
    if (type != LUA_TTABLE)
        return reject(vm, list_path, "must be a table, not a %s", lua_typename(L, type));
    int list = lua_absindex(L, -1);
    lua_Unsigned length = lua_rawlen(L, list), entries = 0;
    lua_pushnil(L);
    while (lua_next(L, list)) {
        lua_pop(L, 1);
        if (!lua_isinteger(L, -1) || lua_tointeger(L, -1) < 1
            || (lua_Unsigned)lua_tointeger(L, -1) > length)
            return reject(vm, list_path, "must be a list numbered from 1 without gaps");
        ++entries;
    }
    if (entries != length)
        return reject(vm, list_path, "must be a list numbered from 1 without gaps");
    if (length > WATERFRONT_ROUTE_ACTORS)
        return reject(vm, list_path, "must list 0 to %d vessels, not %llu", WATERFRONT_ROUTE_ACTORS,
            (unsigned long long)length);
    s->traffic_present = true;
    s->traffic.actor_count = (unsigned)length;
    for (unsigned i = 0; i < length; ++i) {
        WaterfrontRouteActor *v = &s->traffic.actors[i];
        char path[64], id[WATERFRONT_ROUTE_TEXT], route[WATERFRONT_ROUTE_TEXT],
            recipe[WATERFRONT_ROUTE_TEXT], retained[WATERFRONT_ROUTE_TEXT];
        element(path, sizeof(path), list_path, i + 1);
        lua_rawgeti(L, list, i + 1);
        if (lua_type(L, -1) != LUA_TTABLE)
            return reject(vm, path, "must be a table, not a %s", luaL_typename(L, -1));
        int entry = lua_absindex(L, -1);
        if (!only(L, vm, entry, path, VESSEL) || !string(L, vm, entry, path, "id", id, sizeof(id)))
            return false;
        char retain_path[64];
        int retain_type = field(L, entry, "retain", path, retain_path);
        if (retain_type != LUA_TNIL) {
            lua_pop(L, 1);
            if (!string(L, vm, entry, path, "retain", retained, sizeof(retained)))
                return false;
            if (!waterfront_route_preset(retained, v))
                return reject(vm, retain_path, "is not a registered route actor");
            v->retain = 0xff;
            const WaterfrontRouteDefinition *defaults = waterfront_route_defaults();
            for (unsigned d = 0; d < defaults->actor_count; ++d)
                if (!strcmp(defaults->actors[d].id, retained))
                    v->retain = (unsigned char)d;
            if (!strcmp(retained, "alvsnabben"))
                v->retain = WATERFRONT_TRAFFIC_VESSELS; // The first compiled upstream ferry.
            memcpy(v->id, id, sizeof(id));
            // A retain entry may override its named stop dwells; the route and motion stay
            // the compiled actor's own.
            if (!only(L, vm, entry, path, (const char *const[]) { "id", "retain", "stops", NULL }))
                return false;
            char stops_path[64];
            join(stops_path, sizeof(stops_path), path, "stops");
            if (!scene_stops(L, vm, entry, path, v, stops_path))
                return false;
        } else {
            double speed, phase, period;
            char route_path[64], recipe_path[64];
            join(route_path, sizeof(route_path), path, "route");
            join(recipe_path, sizeof(recipe_path), path, "recipe");
            if (!string(L, vm, entry, path, "route", route, sizeof(route))
                || !string(L, vm, entry, path, "recipe", recipe, sizeof(recipe))
                || !number(L, vm, entry, path, "speed", &speed)
                || !number(L, vm, entry, path, "phase", &phase)
                || !number(L, vm, entry, path, "period", &period))
                return false;
            if (!(speed > 0) || !(period > 0)) {
                char speed_path[64], period_path[64];
                join(speed_path, sizeof(speed_path), path, "speed");
                join(period_path, sizeof(period_path), path, "period");
                return reject(
                    vm, !(speed > 0) ? speed_path : period_path, "must be greater than zero");
            }
            if (!waterfront_route_preset(route, v))
                return reject(vm, route_path, "is not a registered route");
            WaterfrontCraftRecipe r;
            if (!waterfront_route_recipe_from_name(recipe, &r))
                return reject(vm, recipe_path, "is not a registered craft recipe");
            v->recipe = (unsigned char)r;
            v->speed = (float)speed;
            v->phase = (float)phase;
            v->period = period;
            memcpy(v->id, id, sizeof(id));
            char stops_path[64];
            join(stops_path, sizeof(stops_path), path, "stops");
            if (!scene_stops(L, vm, entry, path, v, stops_path))
                return false;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 2); // vessels list and traffic table
    return true;
}

static bool scene_marina(lua_State *L, Vm *vm, int scene, BoatSceneDefinition *s)
{
    char at[64];
    int type = field(L, scene, "marina", "scene", at);
    if (type == LUA_TNIL) {
        lua_pop(L, 1);
        return true;
    }
    if (type != LUA_TTABLE)
        return reject(vm, at, "must be a table, not a %s", lua_typename(L, type));
    int marina = lua_absindex(L, -1);
    if (!only(L, vm, marina, at, MARINA) || !scene_entrance(L, vm, marina, s)
        || !scene_berths(L, vm, marina, s) || !scene_modules(L, vm, marina, s))
        return false;
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
            || !number(L, vm, scene, "scene", "version", &version)
            || !scene_marina(L, vm, scene, &d->scene) || !scene_traffic(L, vm, scene, &d->scene)
            || !scene_gulls(L, vm, scene, &d->scene))
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

static bool numeric_record(lua_State *L, Vm *vm, int parent, const char *path, const char *key,
    const char *const *keys, double *out)
{
    char at[64];
    int table = record(L, vm, parent, path, key, keys, at);
    if (!table || !numbers(L, vm, table, at, keys, out))
        return false;
    lua_pop(L, 1);
    return true;
}

static bool numeric_list(lua_State *L, Vm *vm, int parent, const char *path, const char *key,
    const char *const *keys, double *out, unsigned stride, unsigned min, unsigned max,
    unsigned *count_out)
{
    char at[64];
    if (field(L, parent, key, path, at) != LUA_TTABLE)
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
    if (!race || !numeric_record(L, vm, race, "race", "start", GATE, d->start)
        || !numeric_list(L, vm, race, "race", "gates", GATE, &d->gates[0][0], 5, 1, BOAT_RACE_GATES,
            &d->gate_count)
        || !numeric_list(L, vm, race, "race", "grid", POINT, &d->grid[0][0], 2, BOAT_FLEET_SLOTS,
            BOAT_FLEET_SLOTS, &grid_count)
        || !numeric_record(L, vm, race, "race", "source", SOURCE, d->source)
        || !numeric_record(L, vm, race, "race", "storm", STORM, d->storm)
        || !numeric_record(L, vm, race, "race", "rules", RULES, d->rules))
        return 0;
    return 0;
}

static const char *const FREE_ROOT[] = { "free", NULL };
static const char *const FREE[] = { "version", NULL };
static const char *const COURSE_ROOT[] = { "course", NULL };
static const char *const COURSE[] = { "start", "gates", NULL };
static const char *const COURSE_START[] = { "x", "z", "heading", NULL };
static const char *const COURSE_GATE[] = { "x", "z", "half_width", NULL };

static int decode_free(lua_State *L)
{
    Vm *vm = lua_touserdata(L, 1);
    if (lua_type(L, 2) != LUA_TTABLE) {
        reject(vm, "", "the entry must return a table");
        return 0;
    }
    char at[64];
    if (!only(L, vm, 2, "", FREE_ROOT))
        return 0;
    int free = record(L, vm, 2, "", "free", FREE, at);
    if (!free || !number(L, vm, free, at, "version", &vm->free->version))
        return 0;
    return 0;
}

static int decode_course(lua_State *L)
{
    Vm *vm = lua_touserdata(L, 1);
    if (lua_type(L, 2) != LUA_TTABLE) {
        reject(vm, "", "the entry must return a table");
        return 0;
    }
    char at[64];
    if (!only(L, vm, 2, "", COURSE_ROOT))
        return 0;
    int course = record(L, vm, 2, "", "course", COURSE, at);
    BoatPluginCourseData *d = vm->course;
    unsigned gate_count;
    if (!course || !numeric_record(L, vm, course, "course", "start", COURSE_START, d->start)
        || !numeric_list(L, vm, course, "course", "gates", COURSE_GATE, &d->gates[0][0], 3,
            BOAT_COURSE_GATES, BOAT_COURSE_GATES, &gate_count))
        return 0;
    d->gate_count = gate_count;
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
    lua_pushcfunction(L,
        vm->race         ? decode_race
            : vm->free   ? decode_free
            : vm->course ? decode_course
                         : decode);
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
        ++lua_states;
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
        ++lua_states;
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

BoatPluginStatus boat_plugin_lua_free(const char *source, size_t size, const char *chunk,
    const BoatPluginLimits *limits, BoatPluginFreeData *out, BoatPluginDiagnostic *diagnostic)
{
    BoatPluginFreeData data = { 0 };
    Vm vm = { .limit = limits->memory_bytes,
        .budget = limits->instructions,
        .free = &data,
        .diagnostic = diagnostic };
    BoatPluginStatus status;
    lua_State *L = lua_newstate(allocate, &vm);
    if (!L) {
        failure(diagnostic, BOAT_PLUGIN_MEMORY, chunk, "not enough memory for a Lua state");
        status = BOAT_PLUGIN_MEMORY;
    } else {
        ++lua_states;
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

BoatPluginStatus boat_plugin_lua_course(const char *source, size_t size, const char *chunk,
    const BoatPluginLimits *limits, BoatPluginCourseData *out, BoatPluginDiagnostic *diagnostic)
{
    BoatPluginCourseData data = { 0 };
    Vm vm = { .limit = limits->memory_bytes,
        .budget = limits->instructions,
        .course = &data,
        .diagnostic = diagnostic };
    BoatPluginStatus status;
    lua_State *L = lua_newstate(allocate, &vm);
    if (!L) {
        failure(diagnostic, BOAT_PLUGIN_MEMORY, chunk, "not enough memory for a Lua state");
        status = BOAT_PLUGIN_MEMORY;
    } else {
        ++lua_states;
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

typedef struct {
    char *out;
    size_t capacity, size;
    bool truncated;
} Render;

static void render_put(Render *r, const char *text, size_t size)
{
    if (r->truncated)
        return;
    if (size >= r->capacity - r->size) {
        r->truncated = true;
        return;
    }
    memcpy(r->out + r->size, text, size);
    r->size += size;
}

static void render_text(Render *r, const char *text) { render_put(r, text, strlen(text)); }

static void render_string(Render *r, const char *text, size_t size)
{
    render_text(r, "\"");
    for (size_t i = 0; i < size && i < 96 && !r->truncated; ++i) {
        char escape[8], c = text[i];
        if (c == '"' || c == '\\') {
            escape[0] = '\\';
            escape[1] = c;
            render_put(r, escape, 2);
        } else if (c == '\n') {
            render_text(r, "\\n");
        } else if (c == '\r') {
            render_text(r, "\\r");
        } else if (c == '\t') {
            render_text(r, "\\t");
        } else if ((unsigned char)c < 32 || (unsigned char)c == 127) {
            snprintf(escape, sizeof(escape), "\\%03u", (unsigned char)c);
            render_text(r, escape);
        } else {
            render_put(r, &c, 1);
        }
    }
    if (size > 96 || r->truncated) {
        r->truncated = false;
        render_text(r, "...");
    }
    render_text(r, "\"");
}

static bool plain_key(const char *key)
{
    if (!((*key >= 'a' && *key <= 'z') || (*key >= 'A' && *key <= 'Z') || *key == '_'))
        return false;
    for (; *key; ++key)
        if (!((*key >= 'a' && *key <= 'z') || (*key >= 'A' && *key <= 'Z')
                || (*key >= '0' && *key <= '9') || *key == '_'))
            return false;
    return true;
}

static void render_value(lua_State *L, int index, int depth, Render *r)
{
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
    case LUA_TNIL:
        render_text(r, "nil");
        return;
    case LUA_TBOOLEAN:
        render_text(r, lua_toboolean(L, index) ? "true" : "false");
        return;
    case LUA_TNUMBER:
        if (lua_isinteger(L, index)) {
            char number[32];
            snprintf(number, sizeof(number), "%lld", (long long)lua_tointeger(L, index));
            render_text(r, number);
        } else {
            char number[48];
            snprintf(number, sizeof(number), "%.14g", lua_tonumber(L, index));
            render_text(r, number);
        }
        return;
    case LUA_TSTRING: {
        size_t size;
        const char *text = lua_tolstring(L, index, &size);
        render_string(r, text, size);
        return;
    }
    case LUA_TFUNCTION:
        render_text(r, "<function>");
        return;
    case LUA_TUSERDATA:
        render_text(r, "<userdata>");
        return;
    case LUA_TTHREAD:
        render_text(r, "<thread>");
        return;
    case LUA_TTABLE:
        break;
    default:
        render_text(r, "<value>");
        return;
    }
    if (depth >= 4) {
        render_text(r, "{ ... }");
        return;
    }
    render_text(r, "{ ");
    unsigned entries = 0;
    bool more = false;
    lua_pushnil(L);
    while (lua_next(L, index)) {
        if (entries == 12) {
            more = true;
            lua_pop(L, 2);
            break;
        }
        if (entries++)
            render_text(r, ", ");
        int key = lua_absindex(L, -2), value = lua_absindex(L, -1);
        if (lua_type(L, key) == LUA_TSTRING) {
            const char *name = lua_tostring(L, key);
            if (plain_key(name)) {
                render_text(r, name);
            } else {
                render_text(r, "[");
                render_value(L, key, depth + 1, r);
                render_text(r, "]");
            }
        } else {
            render_text(r, "[");
            render_value(L, key, depth + 1, r);
            render_text(r, "]");
        }
        render_text(r, " = ");
        render_value(L, value, depth + 1, r);
        lua_pop(L, 1);
    }
    render_text(r, more ? ", ... }" : " }");
}

BoatPluginStatus boat_plugin_lua_query(const char *source, size_t size, const char *chunk,
    const char *expression, const BoatPluginLimits *limits, char *out, size_t capacity,
    BoatPluginDiagnostic *diagnostic)
{
    limits = limits ? limits : &boat_plugin_default_limits;
    *diagnostic = (BoatPluginDiagnostic) { 0 };
    out[0] = 0;
    Vm vm = {
        .limit = limits->memory_bytes, .budget = limits->instructions, .diagnostic = diagnostic
    };
    lua_State *L = lua_newstate(allocate, &vm);
    if (!L) {
        failure(diagnostic, BOAT_PLUGIN_MEMORY, chunk, "not enough memory for a Lua state");
        return BOAT_PLUGIN_MEMORY;
    }
    ++lua_states;
    BoatPluginStatus status = BOAT_PLUGIN_OK;
    lua_pushcfunction(L, setup);
    int lua_status = lua_pcall(L, 0, 0, 0);
    if (lua_status == LUA_OK)
        lua_status = luaL_loadbufferx(L, source, size, "=main.lua", "t");
    if (lua_status != LUA_OK)
        failure(diagnostic, lua_status == LUA_ERRMEM ? BOAT_PLUGIN_MEMORY : BOAT_PLUGIN_SYNTAX,
            "main.lua", lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "non-string error");
    else {
        lua_sethook(L, count, LUA_MASKCOUNT, BOAT_PLUGIN_HOOK_STEP);
        lua_status = lua_pcall(L, 0, 1, 0);
        lua_sethook(L, NULL, 0, 0);
        if (lua_status != LUA_OK)
            failure(diagnostic,
                vm.exhausted                   ? BOAT_PLUGIN_BUDGET
                    : lua_status == LUA_ERRMEM ? BOAT_PLUGIN_MEMORY
                                               : BOAT_PLUGIN_RUNTIME,
                "main.lua",
                lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "non-string error");
        else {
            lua_setglobal(L, "def");
            char text[BOAT_PLUGIN_QUERY_MAX + 8];
            snprintf(text, sizeof(text), "return %s", expression);
            int loaded = luaL_loadbufferx(L, text, strlen(text), "=console", "t");
            if (loaded != LUA_OK)
                failure(diagnostic, loaded == LUA_ERRMEM ? BOAT_PLUGIN_MEMORY : BOAT_PLUGIN_SYNTAX,
                    "console",
                    lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "non-string error");
            else {
                vm.instructions = 0;
                vm.budget = BOAT_PLUGIN_CALLBACK_INSTRUCTIONS;
                lua_sethook(L, count, LUA_MASKCOUNT, BOAT_PLUGIN_HOOK_STEP);
                lua_status = lua_pcall(L, 0, 1, 0);
                lua_sethook(L, NULL, 0, 0);
                if (lua_status != LUA_OK)
                    failure(diagnostic,
                        vm.exhausted                   ? BOAT_PLUGIN_BUDGET
                            : lua_status == LUA_ERRMEM ? BOAT_PLUGIN_MEMORY
                                                       : BOAT_PLUGIN_RUNTIME,
                        "console",
                        lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "non-string error");
                else {
                    Render render = { .out = out, .capacity = capacity };
                    render_value(L, -1, 0, &render);
                    out[render.size] = 0;
                }
            }
        }
    }
    if (status == BOAT_PLUGIN_OK)
        status = diagnostic->status;
    diagnostic->cost = (BoatPluginCost) { .instructions = vm.instructions,
        .entry_instructions = vm.instructions,
        .peak_bytes = vm.peak,
        .leaked_bytes = vm.live,
        .fields = vm.fields };
    lua_close(L);
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
        ++lua_states;
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
