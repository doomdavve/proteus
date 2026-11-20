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
    bool occupied;
    unsigned bay, floor;
} BuildingWindow;

// Number of windows of FACADE: bays * floors.
size_t building_window_count(const BoatSceneryFacade *facade);
// Window (BAY, FLOOR) of FACADE on WALL in STYLE; false outside the grid.
bool building_window(const BoatSceneryFacade *facade, const BoatSceneryWindowStyle *style,
    const BuildingWall *wall, unsigned bay, unsigned floor, BuildingWindow *window);
// Calls FN for every window of FACADE, floor by floor and bay by bay; returns how many.
typedef void (*BuildingWindowFn)(const BuildingWindow *window, void *user);
size_t building_windows(const BoatSceneryFacade *facade, const BoatSceneryWindowStyle *style,
    const BuildingWall *wall, BuildingWindowFn fn, void *user);

#endif
