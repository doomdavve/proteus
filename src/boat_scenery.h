#ifndef PROTEUS_BOAT_SCENERY_H
#define PROTEUS_BOAT_SCENERY_H

#include "boat_location.h"

// Presentation meshes of the mapped locations, loaded at run time from their scenery packs
// (tools/map_pack.py writes assets/geometry/<location>/scenery.bin). A pack holds a location's
// whole bake: its water, exactly the triangles of the compiled collision description, followed
// by land, quay walls, buildings, the bridge and dressing. The renderers, minimap and river kit
// draw these; physics, the server and the multiplayer content digest use only the compiled
// water. A loaded pack is read-only and lives until boat_scenery_unload. Not thread-safe: load
// and unload from the thread that draws, never while a view borrows a mesh.

// The loaded full description of MAP, a location's compiled description, or NULL until loaded.
const BoatMapDescription *boat_scenery_of(const BoatMapDescription *map);
// What presentation draws for MAP: its loaded scenery; else MAP itself when it holds land after
// its water (a test map); else NULL (a water-only compiled map whose pack is not loaded).
const BoatMapDescription *boat_scenery_drawn(const BoatMapDescription *map);
// Load pack file PATH as MAP's scenery; true at once when MAP is already loaded. Fails, loading
// nothing, for an unreadable or malformed pack or one whose leading water is not exactly MAP's.
bool boat_scenery_load(const BoatMapDescription *map, const char *path);
// The same from pack bytes, which are not kept.
bool boat_scenery_load_bytes(const BoatMapDescription *map, const void *bytes, size_t size);
// The pack path of a mapped LOCATION in the asset directory ASSETS (ending in '/', such as
// "assets/"): ASSETS "geometry/<name>/scenery.bin". False if it does not fit.
bool boat_scenery_path(
    const BoatWorldDefinition *location, const char *assets, char *path, size_t capacity);
// Load a mapped LOCATION's pack from boat_scenery_path; true for an unmapped location.
bool boat_scenery_load_location(const BoatWorldDefinition *location, const char *assets);
// Every mapped location's pack; stops at the first failure.
bool boat_scenery_load_all(const char *assets);
// Bytes MAP's loaded scenery holds (0 when not loaded).
size_t boat_scenery_bytes(const BoatMapDescription *map);
// Release every loaded pack.
void boat_scenery_unload(void);

#endif
