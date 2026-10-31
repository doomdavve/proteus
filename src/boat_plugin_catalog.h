#ifndef PROTEUS_BOAT_PLUGIN_CATALOG_H
#define PROTEUS_BOAT_PLUGIN_CATALOG_H

// PD1: the bundled package catalog. tools/boat_plugin_catalog_gen.c evaluates each package at
// build time, runs the same validation as staging and embeds the decoded definition. Runtime
// sessions open an entry without a Lua VM. The generated table lives in this library.
#include "boat_plugin_host.h"

typedef struct {
    const char *id;
    const char *name;
    const char *kind; // Manifest spelling: voyage, free, course or race.
    const char *location;
    const unsigned char *wire; // BoatPluginDecoded after evaluation and full validation.
    size_t wire_size;
    uint64_t digest; // Gameplay digest of the decoded definition.
} BoatPluginCatalogEntry;

extern const BoatPluginCatalogEntry boat_plugin_catalog_table[];
extern const unsigned boat_plugin_catalog_count;

// Bundled defaults. A missing default is a build failure, never a launch-time fallback.
#define BOAT_PLUGIN_DEFAULT_BASIN_FREE "proteus.basin-free"
#define BOAT_PLUGIN_DEFAULT_RIVER_FREE "proteus.river-free"
#define BOAT_PLUGIN_DEFAULT_OUTER_VOYAGE "proteus.langedrag"
#define BOAT_PLUGIN_DEFAULT_RIVER_RACE "proteus.river-sprint"
#define BOAT_PLUGIN_DEFAULT_BASIN_COURSE "proteus.time-trial"

const BoatPluginCatalogEntry *boat_plugin_catalog_at(unsigned index);
const BoatPluginCatalogEntry *boat_plugin_catalog_find(const char *id);
// KIND's default for LOCATION, or NULL. FREE uses the basin or river package; VOYAGE the outer
// voyage; COURSE the basin time trial; RACE the river sprint.
const BoatPluginCatalogEntry *boat_plugin_catalog_default(
    BoatPluginKind kind, const char *location);
// Decode ENTRY into a typed revision with no Lua and no map sweep. The catalog generator already
// validated it against the compiled maps; this is the same receipt, decoded in this build.
bool boat_plugin_catalog_decode(const BoatPluginCatalogEntry *entry, BoatPluginRevision *revision,
    BoatPluginDiagnostic *diagnostic);

#endif
