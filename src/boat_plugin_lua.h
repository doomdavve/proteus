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

// Compile SOURCE as text named CHUNK, run it under LIMITS and copy the voyage it returns.
// DIAGNOSTIC is required; its cost is filled on every return.
BoatPluginStatus boat_plugin_lua_voyage(const char *source, size_t size, const char *chunk,
    const BoatPluginLimits *limits, BoatPluginVoyageData *out, BoatPluginDiagnostic *diagnostic);

#endif
