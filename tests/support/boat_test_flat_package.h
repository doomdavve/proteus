#ifndef BOAT_TEST_FLAT_PACKAGE_H
#define BOAT_TEST_FLAT_PACKAGE_H

// Validators only (Marina M5 W5): turn a package that carries modules back into one that does
// not. Several validators edit a shipped package's text (a berth name, a route, a coordinate) or
// pair its manifest with synthetic source. harbor-life and harbor-room now keep their marina in
// a module; flattening inlines each `require("name")` of the entry as an immediately called
// function holding that module's text, and drops "modules" from the manifest, so the result
// evaluates to the same data as the shipped package and is plain text again. One level only: a
// module that itself requires another is not supported (and not needed by the shipped packages).
#include "plugins/boat_plugin_image.h"
#include "plugins/boat_plugin_io.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static inline bool boat_test_flatten_package(BoatPluginFiles *files)
{
    if (!boat_plugin_image_is(files->source, files->source_size))
        return true;
    BoatPluginImageFile parts[BOAT_PLUGIN_MODULES_MAX + 1];
    unsigned count;
    if (!boat_plugin_image_unpack(files->source, files->source_size, parts, &count))
        return false;
    static char text[BOAT_PLUGIN_SOURCE_MAX + 1];
    size_t size = parts[0].size;
    if (size >= sizeof(text))
        return false;
    memcpy(text, parts[0].data, size);
    text[size] = 0;
    for (unsigned i = 1; i < count; ++i) {
        for (int quote = 0; quote < 2; ++quote) {
            char call[BOAT_PLUGIN_MODULE_NAME_MAX + 16];
            const char q = quote ? '\'' : '"';
            int n = snprintf(call, sizeof(call), "require(%c%.*s%c)", q, (int)parts[i].name_size,
                parts[i].name, q);
            char *at;
            while (n > 0 && (at = strstr(text, call)) != NULL) {
                static const char open[] = "(function()\n", close[] = "\nend)()";
                size_t tail = strlen(at + n);
                size_t added = sizeof(open) - 1 + parts[i].size + sizeof(close) - 1;
                if (size - (size_t)n + added >= sizeof(text))
                    return false;
                memmove(at + added, at + n, tail + 1);
                memcpy(at, open, sizeof(open) - 1);
                memcpy(at + sizeof(open) - 1, parts[i].data, parts[i].size);
                memcpy(at + sizeof(open) - 1 + parts[i].size, close, sizeof(close) - 1);
                size = size - (size_t)n + added;
            }
        }
    }
    memcpy(files->source, text, size);
    files->source_size = size;
    // Drop the manifest's "modules": [...] member and the separator after it.
    char *manifest = files->manifest;
    size_t length = files->manifest_size, at = 0;
    static const char key[] = "\"modules\"";
    while (at + sizeof(key) - 1 <= length && memcmp(manifest + at, key, sizeof(key) - 1))
        ++at;
    if (at + sizeof(key) - 1 <= length) {
        size_t end = at;
        while (end < length && manifest[end] != ']')
            ++end;
        if (end == length)
            return false;
        ++end;
        if (end < length && manifest[end] == ',')
            ++end;
        while (end < length && (manifest[end] == ' ' || manifest[end] == '\n'))
            ++end;
        size_t start = at;
        while (start > 0 && (manifest[start - 1] == ' ' || manifest[start - 1] == '\t'))
            --start;
        memmove(manifest + start, manifest + end, length - end);
        files->manifest_size = length - (end - start);
        manifest[files->manifest_size] = 0; // Callers pass the manifest as a C string.
    }
    return true;
}

#endif
