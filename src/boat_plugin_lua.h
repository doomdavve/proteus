#ifndef PROTEUS_BOAT_PLUGIN_LUA_H
#define PROTEUS_BOAT_PLUGIN_LUA_H

// Private to proteus_plugins: the restricted Lua evaluation and the typed copy of its result.
// Values are copied as Lua gave them; boat_plugin.c checks their meaning.
#include "boat_plugin.h"

typedef struct {
    char id[BOAT_PLUGIN_TEXT], name[BOAT_PLUGIN_TEXT];
    double x, z, heading, radius;
} BoatPluginShelterData;

typedef struct {
    unsigned shelter_count;
    BoatPluginShelterData shelters[BOAT_VOYAGE_SHELTERS];
    struct {
        double x, z, radius, max_speed;
    } arrival;
    struct {
        double x, z, radius, height, build;
    } exposure;
    double exposed_fraction, weather_range;
} BoatPluginVoyageData;

#define BOAT_PLUGIN_GATE_FIELDS "x", "z", "dx", "dz", "half_width"
#define BOAT_PLUGIN_SOURCE_FIELDS                                                                  \
    "x", "z", "dx", "dz", "started", "speed", "height", "duration", "lifetime", "wavelength",      \
        "spread", "half_width"
#define BOAT_PLUGIN_STORM_FIELDS "x", "z", "vx", "vz", "radius", "height", "started", "build"
#define BOAT_PLUGIN_RULE_FIELDS                                                                    \
    "grid_ticks", "countdown_ticks", "results_ticks", "after_winner_ticks", "cap_ticks",           \
        "recovery_ticks", "skip_ticks", "park_ticks", "min_players"
typedef struct {
    double start[5], gates[BOAT_RACE_GATES][5], grid[BOAT_FLEET_SLOTS][2];
    unsigned gate_count;
    double source[12], storm[8], rules[9];
} BoatPluginRaceData;
BoatPluginStatus boat_plugin_lua_race(const char *source, size_t size, const char *chunk,
    const BoatPluginLimits *limits, BoatPluginRaceData *out, BoatPluginDiagnostic *diagnostic);

// Compile SOURCE as text named CHUNK, run it under LIMITS and copy the voyage it returns.
// DIAGNOSTIC is required; its cost is filled on every return.
BoatPluginStatus boat_plugin_lua_voyage(const char *source, size_t size, const char *chunk,
    const BoatPluginLimits *limits, BoatPluginVoyageData *out, BoatPluginDiagnostic *diagnostic);

#endif
