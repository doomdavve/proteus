#ifndef PROTEUS_BOAT_PLUGIN_LUA_H
#define PROTEUS_BOAT_PLUGIN_LUA_H

// Private to proteus_plugins: the restricted Lua evaluation that produces descriptor data.
// Values are copied as Lua gave them; boat_plugin.c checks their meaning.
#include "boat_plugin_decode.h"

BoatPluginStatus boat_plugin_lua_free(const char *source, size_t size, const char *chunk,
    const BoatPluginLimits *limits, BoatPluginFreeData *out, BoatPluginDiagnostic *diagnostic);
BoatPluginStatus boat_plugin_lua_course(const char *source, size_t size, const char *chunk,
    const BoatPluginLimits *limits, BoatPluginCourseData *out, BoatPluginDiagnostic *diagnostic);

BoatPluginStatus boat_plugin_lua_race(const char *source, size_t size, const char *chunk,
    const BoatPluginLimits *limits, BoatPluginRaceData *out, BoatPluginDiagnostic *diagnostic);
BoatPluginStatus boat_plugin_lua_match(const char *source, size_t size, const char *chunk,
    const BoatPluginLimits *limits, BoatPluginMatchData *out, BoatPluginDiagnostic *diagnostic);

// How many Lua states this process has created, for the no-Lua runtime gate.
unsigned boat_plugin_lua_states(void);

// Compile SOURCE as text named CHUNK, run it under LIMITS and copy the voyage it returns.
// DIAGNOSTIC is required; its cost is filled on every return.
BoatPluginStatus boat_plugin_lua_voyage(const char *source, size_t size, const char *chunk,
    const BoatPluginLimits *limits, BoatPluginVoyageData *out, BoatPluginDiagnostic *diagnostic);

// PC console: evaluate SOURCE, then render EXPRESSION against the table it returned, bound as the
// global DEF for the expression only. The rendering is bounded by depth and size and never runs
// metamethods; OUT is NUL-terminated. No native state is exposed and no revision is created.
BoatPluginStatus boat_plugin_lua_query(const char *source, size_t size, const char *chunk,
    const char *expression, const BoatPluginLimits *limits, char *out, size_t capacity,
    BoatPluginDiagnostic *diagnostic);

// Private protected execution for the P3 transition decoder. CALLBACK receives the entry's
// returned table at index 2 and CONTEXT as light userdata at index 1. All Lua work (including
// setup, state decoding and output staging) remains inside a protected call and allocator.
typedef struct lua_State lua_State;
BoatPluginStatus boat_plugin_lua_transition(const char *source, size_t size,
    const BoatPluginLimits *limits, uint64_t callback_budget, int (*callback)(lua_State *),
    void *context, BoatPluginDiagnostic *diagnostic);

#endif
