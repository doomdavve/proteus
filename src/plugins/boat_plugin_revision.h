#ifndef PROTEUS_BOAT_PLUGIN_REVISION_H
#define PROTEUS_BOAT_PLUGIN_REVISION_H

// Plugin data: one immutable definition revision.
#include "plugins/boat_plugin.h"

typedef struct {
    BoatPluginManifest manifest; // Canonical copy; the kind-specific record repeats it.
    // Exactly one record is live, selected by MANIFEST.kind. A union keeps ten retained
    // revisions (each with a possible scene) inside the fixed native owner budget.
    union {
        BoatPluginVoyage voyage;
        BoatPluginRace race; // Used instead of VOYAGE by a race host.
        BoatPluginFree free; // Used by a free-boating host.
        BoatPluginCourse course; // Used by a time-trial host.
        BoatPluginMatch match; // Used by a room authority.
    };
    char source[BOAT_PLUGIN_SOURCE_MAX];
    size_t source_size;
    unsigned number; // 1 for the launch revision, then one more for each staged definition.
} BoatPluginRevision;

#endif
