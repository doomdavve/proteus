#ifndef PROTEUS_BOAT_ASSETS_H
#define PROTEUS_BOAT_ASSETS_H

#include "raylib.h"
#include <stdbool.h>
#include <stdio.h>

// Bundled resources on macOS; executable-adjacent assets in development and on Windows.
static inline bool boat_asset_path(char *path, size_t size, const char *relative)
{
    const char *directory = GetApplicationDirectory();
    const char *assets = "assets";
#if defined(__APPLE__)
    char resources[4096];
    int count = snprintf(resources, sizeof(resources), "%s../Resources/assets", directory);
    if (count > 0 && (size_t)count < sizeof(resources) && DirectoryExists(resources))
        assets = "../Resources/assets";
#endif
    int count_path = snprintf(path, size, "%s%s/%s", directory, assets, relative);
    return count_path > 0 && (size_t)count_path < size;
}

#endif
