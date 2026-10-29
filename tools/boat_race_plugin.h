#ifndef BOAT_RACE_PLUGIN_H
#define BOAT_RACE_PLUGIN_H
#include "boat_plugin_host.h"

// Native local trial adapter. The application supplies wall time and input outside ticks.
typedef struct {
    BoatPluginHost host;
    BoatPluginFiles files;
    BoatPluginWatch watch;
    const char *directory;
    char notice[256];
    bool staged; // This frame did synchronous staging; discard that frame's simulation delta.
} RacePlugin;
bool race_plugin_open(
    RacePlugin *plugin, const char *directory, const BoatWorldResources *resources, double now);
// Renames preserve the run. True only after a gameplay commit: the caller must reset its
// run/clock/water/wakes before another step. Undo goes through the same policy.
bool race_plugin_frame(RacePlugin *plugin, bool reload, bool undo, bool apply, double now);
#endif
