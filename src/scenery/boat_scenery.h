#ifndef PROTEUS_BOAT_SCENERY_H
#define PROTEUS_BOAT_SCENERY_H

#include "core/boat_location.h"

#include <stdint.h>

// Presentation meshes of the mapped locations, loaded at run time from their scenery packs
// (tools/map_pack.py writes assets/geometry/<location>/scenery.bin). A pack holds a location's
// whole bake: its water, exactly the triangles of the compiled collision description, followed
// by land, quay walls, buildings, the bridge and dressing. The renderers, minimap and river kit
// draw these; physics, the server and the multiplayer content digest use only the compiled
// water. A loaded pack is read-only and lives until boat_scenery_unload. Not thread-safe: load
// and unload from the thread that draws, never while a view borrows a mesh.

// Version 2 packs also carry per-building records (tools/map_pack.py documents the layout): a
// stable id hash, the building's triangle range and bounds, where its attached detail starts and
// the explicit role (and facade) of each run of its triangles. Buildings without records (a
// version 1 pack, or unmigrated buildings) are the legacy path: renderers classify their
// triangles by colour and geometry. Triangles of a migrated building are never classified
// that way.
typedef enum {
    BOAT_SCENERY_ROLE_NONE = 0, // No record: legacy colour/geometry classification.
    BOAT_SCENERY_ROLE_WALL, // Facade wall in the generated pair order; no generic windows.
    BOAT_SCENERY_ROLE_ROOF,
    BOAT_SCENERY_ROLE_WINDOW, // Baked window quad: two triangles (a, b, c), (a, c, d).
    BOAT_SCENERY_ROLE_DETAIL, // Anything else of the building: drawn flat.
    BOAT_SCENERY_ROLE_COUNT
} BoatSceneryRole;

typedef struct {
    uint32_t id_hash; // crc32 of the stable source id ("way/123").
    uint32_t first, end; // Triangle range [first, end).
    uint32_t detail_first; // Attached detail starts here; walls and roof come before it.
    uint32_t run_first, run_count; // Index range into boat_scenery_runs.
    Vector3 min, max; // Bounds of its triangles (base y is min.y, top y max.y).
} BoatSceneryBuilding;

typedef struct {
    uint32_t first, end; // Triangle range.
    uint8_t role; // BoatSceneryRole.
    uint8_t facade; // Facade index, 255 for none.
    uint16_t heading; // Outward direction, 0..65535 turns from +X toward +Z.
} BoatSceneryRun;

// The loaded full description of MAP, a location's compiled description, or NULL until loaded.
const BoatMapDescription *boat_scenery_of(const BoatMapDescription *map);
// The building records of MAP (a compiled map or its loaded full description), sorted by first
// triangle, or NULL with *COUNT 0 for a version 1 pack or an unloaded map. COUNT may be NULL.
const BoatSceneryBuilding *boat_scenery_buildings(const BoatMapDescription *map, size_t *count);
const BoatSceneryRun *boat_scenery_runs(const BoatMapDescription *map, size_t *count);
// One BoatSceneryRole per triangle of MAP's loaded scenery, or NULL when it has no building
// records (every triangle is then BOAT_SCENERY_ROLE_NONE).
const unsigned char *boat_scenery_roles(const BoatMapDescription *map);
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
// Load a mapped LOCATION's pack from boat_scenery_path, then its optional water atlas
// (boat_water_atlas.h) and ambient occlusion (boat_ambient.h) sidecars; true for an unmapped
// location. A map with a river kit ground (the outer map) also loads the Eriksberg river crop's
// pack (ASSETS "geometry/river/scenery.bin"), which lays the kit out.
bool boat_scenery_load_location(const BoatWorldDefinition *location, const char *assets);
// Every mapped location's pack; stops at the first failure.
bool boat_scenery_load_all(const char *assets);
// Bytes MAP's loaded scenery holds (0 when not loaded).
size_t boat_scenery_bytes(const BoatMapDescription *map);
// Release every loaded pack and its sidecars.
void boat_scenery_unload(void);

#endif
