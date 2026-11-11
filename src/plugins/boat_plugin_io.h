#ifndef PROTEUS_BOAT_PLUGIN_IO_H
#define PROTEUS_BOAT_PLUGIN_IO_H

// Acquire a plugin directory's plugin.json and its entry as immutable bytes. The only plugin code
// that touches the filesystem; boat_plugin.h evaluates what it read.
#include "boat_plugin.h"

typedef struct {
    char manifest[BOAT_PLUGIN_MANIFEST_MAX];
    size_t manifest_size;
    char source[BOAT_PLUGIN_SOURCE_MAX];
    size_t source_size;
} BoatPluginFiles;

// Read DIRECTORY/plugin.json, check it, then read its entry. Each must be a regular file, not a
// symbolic link, within LIMITS (NULL: the defaults). FILES is written only on success.
BoatPluginStatus boat_plugin_read(const char *directory, const BoatPluginLimits *limits,
    BoatPluginFiles *files, BoatPluginDiagnostic *diagnostic);
// A bundle borrowing FILES.
BoatPluginBundle boat_plugin_bundle(const BoatPluginFiles *files);

#endif
