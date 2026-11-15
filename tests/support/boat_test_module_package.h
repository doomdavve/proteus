#ifndef BOAT_TEST_MODULE_PACKAGE_H
#define BOAT_TEST_MODULE_PACKAGE_H

// Validators only (Marina M5 W3): a scratch copy of a shipped single-file package whose whole
// entry moved into one module, "shipped". The copy's manifest declares that module and its
// main.lua is `return require('shipped')`, so it must evaluate exactly as the original.
#include "plugins/boat_plugin_io.h"
#include "support/boat_test_flat_package.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define BOAT_TEST_MKDIR(path) _mkdir(path)
#else
#define BOAT_TEST_MKDIR(path) mkdir(path, 0755)
#endif

// Write SCRATCH/NAME from SOURCE_DIRECTORY into DIRECTORY (CAPACITY bytes). False on any failure.
static inline bool boat_test_module_package(const char *source_directory, const char *scratch,
    const char *name, char *directory, size_t capacity)
{
    static BoatPluginFiles files;
    BoatPluginDiagnostic d;
    if (boat_plugin_read(source_directory, NULL, &files, &d) != BOAT_PLUGIN_OK
        || !boat_test_flatten_package(&files))
        return false;
    int n = snprintf(directory, capacity, "%s/%s", scratch, name);
    if (n < 0 || (size_t)n >= capacity)
        return false;
    char path[1100];
    BOAT_TEST_MKDIR(scratch);
    BOAT_TEST_MKDIR(directory);
    snprintf(path, sizeof(path), "%s/modules", directory);
    BOAT_TEST_MKDIR(path);
    // The manifest gains "modules": ["shipped"] before its closing brace.
    const char *close = NULL;
    for (const char *c = files.manifest + files.manifest_size; c > files.manifest && !close;)
        if (*--c == '}')
            close = c;
    char manifest[BOAT_PLUGIN_MANIFEST_MAX + 64];
    if (!close)
        return false;
    int m = snprintf(manifest, sizeof(manifest), "%.*s,\n  \"modules\": [\"shipped\"]\n}\n",
        (int)(close - files.manifest) - 1, files.manifest);
    if (m <= 0 || (size_t)m >= sizeof(manifest))
        return false;
    const struct {
        const char *name;
        const char *text;
        size_t size;
    } parts[] = { { "plugin.json", manifest, (size_t)m },
        { "main.lua", "return require('shipped')\n", 26 },
        { "modules/shipped.lua", files.source, files.source_size } };
    for (unsigned i = 0; i < 3; ++i) {
        snprintf(path, sizeof(path), "%s/%s", directory, parts[i].name);
        FILE *file = fopen(path, "wb");
        if (!file)
            return false;
        bool ok = fwrite(parts[i].text, 1, parts[i].size, file) == parts[i].size;
        if (fclose(file) || !ok)
            return false;
    }
    return true;
}

#endif
