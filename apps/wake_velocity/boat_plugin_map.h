// Agent map queries for the plugin worker: exact water/land probes and a bounded ASCII grid of the
// compiled map with the draft scene overlaid. Deterministic, read-only, no files or wall time.
#ifndef WAKE_VELOCITY_BOAT_PLUGIN_MAP_H
#define WAKE_VELOCITY_BOAT_PLUGIN_MAP_H

#include "core/boat_map.h"
#include "core/boat_scene_definition.h"
#include <stdbool.h>

#define BOAT_PLUGIN_MAP_PROBES 24
#define BOAT_PLUGIN_MAP_CELLS 4096

typedef struct {
    bool grid; // false: PROBES points
    bool hulls; // the registered hull recipes and the validator's envelope rule
    unsigned probe_count;
    float probe[BOAT_PLUGIN_MAP_PROBES][2]; // x, z
    float x0, z0, x1, z1, step; // grid box, simulation metres
} BoatPluginMapRequest;

// Parses the worker arguments that follow "--map": `probe X Z [X Z ...]`, `hulls` or
// `box X0 Z0 X1 Z1 STEP`. False (with MESSAGE) when malformed or over a limit.
bool boat_plugin_map_parse(
    int argc, char **argv, BoatPluginMapRequest *request, char *message, unsigned message_size);

// Prints one `"map":{...}` JSON member (no braces around it, no trailing comma). SCENE may be NULL
// when the draft is absent or failed validation; the overlay is then omitted.
void boat_plugin_map_print(
    const BoatPluginMapRequest *request, const BoatMap *map, const BoatSceneDefinition *scene);

#endif
