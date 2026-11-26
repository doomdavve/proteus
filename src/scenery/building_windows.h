#ifndef PROTEUS_BUILDING_WINDOWS_H
#define PROTEUS_BUILDING_WINDOWS_H

#include "core/boat_map.h"
#include "scenery/boat_scenery.h"

#include "raylib.h"

#include <stddef.h>
#include <stdint.h>

// The one generator of building windows. The scenery pack (docs/BUILDINGS.md) stores no window
// triangles: a facade record (BoatSceneryFacade) names a wall of the mesh, a bay and floor count
// and a window style, and this module expands them into window quads and decides each window's
// glass and lit state. The CPU renderer draws the quads of facades near the camera; the GPU
// renderer expands the cells near its anchor into meshes and its facade shader draws the same grid
// beyond, with seeds, glass and occupancy computed by the formulas below (kept bit for bit in
// assets/shaders/waterfront_city.fs). Both renderers therefore show the same windows, lit alike.
// No drawing, input, files or allocation here.

// Horizontal distance from the camera (the followed boat) within which windows are quads; beyond
// it the facade shader draws them (a window under about 1.5 m is then a pixel or less).
#define BUILDING_WINDOW_NEAR_RANGE 430.0f

// Within this horizontal distance a window also has real relief (building_window_reveal): a
// surround and returns that project from the wall around the glass. The wall is a flat quad
// pair, not cut, so the opening reads as sunken by the glass sitting behind a projecting frame.
#define BUILDING_WINDOW_DEEP_RANGE 40.0f
// Everything a window adds lies within this distance of its wall (surround depth and rim, with a
// margin): callers grow wall and cell bounds by it.
#define BUILDING_WINDOW_MARGIN 0.3f

// Seeds are identity, never position or order: FACADE_SEED is crc32 of "<id>/<facade>" with the
// id's own crc32 ID_HASH (the pack's id hash, crc32 of "way/164963584") continued over
// "/<facade number>", and a window's seed mixes in its bay and floor with
// fmix32(facade_seed + bay * 0x9E3779B1 + floor * 0x85EBCA77), murmur3's finalizer.
uint32_t building_window_facade_seed(uint32_t id_hash, unsigned facade);
uint32_t building_window_seed(uint32_t facade_seed, unsigned bay, unsigned floor);
// Daytime glass of a seed: seed % 7 under 3 mid blue, 3 pale, else dark. Occupied windows are lit
// as the window light rises: the hashed 3 of 8.
Color building_window_glass(uint32_t seed);
bool building_window_occupied(uint32_t seed);

// When a window is lit. A facade record names one of these schedules (the lighting profile of its
// building's family); a window follows it only if it is occupied, so the profile says how many of
// the lit rooms keep their light on, and when. MINUTE is the minute of the day, 0..1439; a
// negative MINUTE means no clock (a fixed daylight phase) and every occupied window is lit.
// Integer arithmetic only: assets/shaders/waterfront_city.fs repeats it exactly.
//   RESIDENTIAL  evening from 16:00-20:00 to 22:00-01:00, 3 in 16 stay lit until 05:00 and 2 in 16
//                rise at 05:00-07:00 (about 7% and 5% of all windows).
//   WORKPLACE    shops and offices: lit through a working evening to 18:00-22:00, 1 in 16 all
//   night. INDUSTRIAL   a quarter lit until a 21:00-23:00 shift end, 1 in 16 all night, 1 in 16
//   early. QUIET        boatyards and clubhouses: 1 in 4 lit until 19:00-21:00, otherwise dark.
enum {
    BUILDING_PROFILE_RESIDENTIAL,
    BUILDING_PROFILE_WORKPLACE,
    BUILDING_PROFILE_INDUSTRIAL,
    BUILDING_PROFILE_QUIET,
    BUILDING_PROFILE_COUNT
};
bool building_window_lit(unsigned profile, uint32_t seed, int minute);
// The share of all windows of PROFILE lit at MINUTE (3/8 without a clock): what distant facades,
// where windows are under a pixel, settle on.
float building_window_lit_share(unsigned profile, int minute);

// A facade's wall as the mesh gives it: its base corners ORIGIN (a) and END (b), top y, unit
// direction ALONG from a to b and the outward unit normal (the wall is a, c, b above b).
typedef struct {
    Vector3 origin, end, along, normal;
    float length, top;
} BuildingWall;

// The wall of FACADE in MESH (the loaded scenery of its map). False for a facade whose wall pair
// is not the generated a, c, b / a, d, c.
bool building_wall(const Mesh3D *mesh, const BoatSceneryFacade *facade, BuildingWall *wall);

// One window: its quad (bottom left, top left, top right, bottom right, seen from outside), glass
// and occupancy, and its place in the facade.
typedef struct {
    Vector3 corners[4];
    Color glass;
    bool occupied; // Lit when it is the only state given: callers set it from building_window_lit.
    unsigned bay, floor;
    uint32_t seed;
    uint8_t profile;
} BuildingWindow;

// Number of windows of FACADE: bays * floors.
size_t building_window_count(const BoatSceneryFacade *facade);
// Window (BAY, FLOOR) of FACADE on WALL in STYLE; false outside the grid.
bool building_window(const BoatSceneryFacade *facade, const BoatSceneryWindowStyle *style,
    const BuildingWall *wall, unsigned bay, unsigned floor, BuildingWindow *window);
// One flat coloured face of a window. Color.a is the face's lit share at night: the windows pass
// mixes the dark glass toward the window light by it (the glass of an occupied window is 255).
typedef struct {
    Vector3 corners[4]; // As BuildingWindow: bottom left, top left, top right, bottom right.
    Color color;
} BuildingWindowFace;

// The window pane in the plane of the wall, as 4 faces: the glass, and an impression of the
// reveal drawn on it, a dark band under the lintel, a mid band along the left jamb and a light
// sill (shares of the window, shaded from its glass; assets/shaders/waterfront_city.fs draws the
// same bands beyond the quads). Returns the face count, at most BUILDING_WINDOW_FACES_MAX.
#define BUILDING_WINDOW_FACES_MAX 4
size_t building_window_faces(const BuildingWindow *window, BuildingWindowFace faces[4]);
// The relief of a window within BUILDING_WINDOW_DEEP_RANGE: sill, lintel and jamb returns, a frame
// rim around them and, on wide ground-floor windows, a mullion. At most
// BUILDING_WINDOW_REVEAL_MAX faces, all lit share 0; their back sides face away from the viewer.
#define BUILDING_WINDOW_REVEAL_MAX 7
size_t building_window_reveal(
    const BuildingWindow *window, const BuildingWall *wall, BuildingWindowFace faces[7]);

// Calls FN for every window of FACADE, floor by floor and bay by bay; returns how many.
typedef void (*BuildingWindowFn)(const BuildingWindow *window, void *user);
size_t building_windows(const BoatSceneryFacade *facade, const BoatSceneryWindowStyle *style,
    const BuildingWall *wall, BuildingWindowFn fn, void *user);

#endif
