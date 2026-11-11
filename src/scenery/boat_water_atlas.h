#ifndef PROTEUS_BOAT_WATER_ATLAS_H
#define PROTEUS_BOAT_WATER_ATLAS_H

#include "boat_location.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A mapped location's water atlas (graphics G4): presentation-only fields baked from its compiled
// water by tools/water_atlas_bake.c into the sidecar assets/geometry/<location>/water_atlas.bin.
// Physics, the server and the multiplayer content digest never read it; a location without a
// loaded atlas keeps the shaders' hand-written fallback.
//
// File, little-endian: a 48-byte header of twelve u32 (magic "PWAT", version, width, height,
// origin X, origin Z and spacing as float32 bits, the compiled water key low and high words,
// payload bytes, compressed bytes, zero) and one raw DEFLATE stream of exactly the payload.
// The payload is WIDTH x HEIGHT RGBA8 texels, rows of increasing Z; texel (i, j) covers renderer
// X/Z from origin + (i, j) * spacing for one spacing. Channels:
//   R: distance from the texel centre to the nearest shore edge, metres * 4 (0..63.75); 0 marks
//      land (a centre off the water).
//   G: estimated depth, metres * 10 (0..25.5): a bank profile of shore distance, not a survey.
//   B: shelter, 0..1 * 255: how much land closes in, from the mean open fetch in 16 directions
//      up to 200 m (1 when every direction meets land at once).
//   A: chop, 0..1 * 255: the mean open fetch upwind of the cosmetic wind, (0.6, 0.8) in X/Z,
//      and 22.5 degrees either side, each up to 600 m.
// Land within two texels of water carries the mean shelter and chop of the water around it, so
// bilinear sampling at the shore stays meaningful; land never has a shore distance or depth.
#define BOAT_WATER_ATLAS_MAGIC 0x54415750u // "PWAT"
#define BOAT_WATER_ATLAS_VERSION 1u
#define BOAT_WATER_ATLAS_HEADER 48
#define BOAT_WATER_ATLAS_MAX_SIZE 4096 // Texels per side.
#define BOAT_WATER_ATLAS_SHORE_SCALE 4.0f // R units per metre.
#define BOAT_WATER_ATLAS_DEPTH_SCALE 10.0f // G units per metre.
#define BOAT_WATER_ATLAS_SHELTER_FETCH 200.0f // Metres of open fetch that count as open.
#define BOAT_WATER_ATLAS_CHOP_FETCH 600.0f // Metres of upwind fetch that count as full chop.

typedef struct {
    int width, height;
    float origin_x, origin_z, spacing; // Renderer X/Z of texel (0, 0)'s corner; metres per texel.
    const unsigned char *texels; // WIDTH * HEIGHT RGBA8, rows of increasing Z.
} BoatWaterAtlas;

// FNV-1a over MAP's compiled water: triangle count, then each triangle's vertex X/Y/Z bits and
// colour. An atlas records the key of the water it was baked from; a different key is stale.
uint64_t boat_water_atlas_key(const BoatMapDescription *map);
// MAP's loaded atlas, or NULL.
const BoatWaterAtlas *boat_water_atlas_of(const BoatMapDescription *map);
// Validate atlas file bytes for MAP and keep their texels; true at once when already loaded.
// Fails, loading nothing, for a malformed or truncated file, sizes or values out of range, or
// a key that is not MAP's water.
bool boat_water_atlas_load_bytes(const BoatMapDescription *map, const void *bytes, size_t size);
bool boat_water_atlas_load(const BoatMapDescription *map, const char *path);
// ASSETS "geometry/<name>/water_atlas.bin" for a mapped LOCATION; false if it does not fit.
bool boat_water_atlas_path(
    const BoatWorldDefinition *location, const char *assets, char *path, size_t capacity);
// Load a mapped LOCATION's atlas if its sidecar exists and is valid; false otherwise.
bool boat_water_atlas_load_location(const BoatWorldDefinition *location, const char *assets);
// Encode an atlas into a file. TEXELS are WIDTH * HEIGHT RGBA8; COMPRESSED holds the DEFLATE
// stream of exactly those bytes. Writes HEADER bytes of header to OUT.
void boat_water_atlas_header(unsigned char out[BOAT_WATER_ATLAS_HEADER], int width, int height,
    float origin_x, float origin_z, float spacing, uint64_t key, size_t compressed);
// Release every loaded atlas.
void boat_water_atlas_unload(void);

#endif
