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

// Version 3 packs also carry per-building records (tools/map_pack.py documents the layout): a
// stable id hash, the building's triangle range and bounds, where its attached detail starts, the
// explicit role (and facade) of each run of its triangles, and a facade record for each wall that
// takes windows. Buildings without records (a version 1 pack, or unmigrated buildings) are the
// legacy path: renderers classify their triangles by colour and geometry. Triangles of a migrated
// building are never classified that way, and never get generic windows: the pack holds no
// window triangles, only facade records that building_windows.h expands.
typedef enum {
    BOAT_SCENERY_ROLE_NONE = 0, // No record: legacy colour/geometry classification.
    BOAT_SCENERY_ROLE_WALL, // Plaster facade wall in the generated pair order; no generic windows.
    BOAT_SCENERY_ROLE_ROOF,
    BOAT_SCENERY_ROLE_DETAIL, // Anything else of the building: drawn flat.
    BOAT_SCENERY_ROLE_BALCONY, // Balcony slabs and fronts, flat in the run's own colour.
    BOAT_SCENERY_ROLE_CANOPY, // Canopies, awnings and entrance roofs, flat.
    BOAT_SCENERY_ROLE_TRIM, // Plinths, cornices, parapets, piers and frames, flat.
    BOAT_SCENERY_ROLE_WALL_PLASTER, // Authored walls: the material is the role, the colour the
                                    // run's.
    BOAT_SCENERY_ROLE_WALL_BRICK,
    BOAT_SCENERY_ROLE_WALL_PANEL,
    BOAT_SCENERY_ROLE_COUNT
} BoatSceneryRole;

// True for the roles of facade walls (they take facade records and window grids).
static inline bool boat_scenery_role_is_wall(unsigned char role)
{
    return role == BOAT_SCENERY_ROLE_WALL || role == BOAT_SCENERY_ROLE_WALL_PLASTER
        || role == BOAT_SCENERY_ROLE_WALL_BRICK || role == BOAT_SCENERY_ROLE_WALL_PANEL;
}

typedef struct {
    uint32_t id_hash; // crc32 of the stable source id ("way/123").
    uint32_t first, end; // Triangle range [first, end).
    uint32_t detail_first; // Attached detail starts here; walls and roof come before it.
    uint32_t run_first, run_count; // Index range into boat_scenery_runs.
    uint32_t facade_first, facade_count; // Index range into boat_scenery_facades.
    Vector3 min, max; // Bounds of its triangles (base y is min.y, top y max.y).
} BoatSceneryBuilding;

typedef struct {
    uint32_t first, end; // Triangle range.
    uint8_t role; // BoatSceneryRole.
    uint8_t facade; // Facade index, 255 for none.
    uint16_t heading; // Outward direction, 0..65535 turns from +X toward +Z.
} BoatSceneryRun;

// A wall that takes windows. WALL is the first triangle of its pair (a, c, b), (a, d, c): corner a
// is its origin on the base, b the end of its base, c the top corner above b, so the mesh gives
// its length, base y, top y and outward direction. INDEX is its number in the building (a window
// seed input), BAYS and FLOORS the grid size and STYLE an index into the window styles.
// ID_HASH is its building's.
typedef struct {
    uint32_t id_hash;
    uint32_t wall;
    uint16_t index, bays, floors;
    uint8_t style, profile; // PROFILE: the lighting schedule (scenery/building_windows.h).
} BoatSceneryFacade;

// A window grid: window F of bay B spans [first_bay + B bay_pitch, + width] along the wall and
// [first_floor + F floor_pitch, + height] above its base, on a plane OFFSET metres out from it;
// the ground floor (F = 0) has its own size.
typedef struct {
    float first_bay, bay_pitch, first_floor, floor_pitch;
    float ground_width, ground_height, upper_width, upper_height;
    float offset;
} BoatSceneryWindowStyle;

// The loaded full description of MAP, a location's compiled description, or NULL until loaded.
const BoatMapDescription *boat_scenery_of(const BoatMapDescription *map);
// The building records of MAP (a compiled map or its loaded full description), sorted by first
// triangle, or NULL with *COUNT 0 for a version 1 pack or an unloaded map. COUNT may be NULL.
const BoatSceneryBuilding *boat_scenery_buildings(const BoatMapDescription *map, size_t *count);
const BoatSceneryRun *boat_scenery_runs(const BoatMapDescription *map, size_t *count);
// One BoatSceneryRole per triangle of MAP's loaded scenery, or NULL when it has no building
// records (every triangle is then BOAT_SCENERY_ROLE_NONE).
const unsigned char *boat_scenery_roles(const BoatMapDescription *map);
// The facade records of MAP in building order, and the window styles they refer to; NULL with
// *COUNT 0 without records. COUNT may be NULL.
const BoatSceneryFacade *boat_scenery_facades(const BoatMapDescription *map, size_t *count);
const BoatSceneryWindowStyle *boat_scenery_window_styles(
    const BoatMapDescription *map, size_t *count);
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
