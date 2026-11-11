#ifndef PROTEUS_BOAT_AMBIENT_H
#define PROTEUS_BOAT_AMBIENT_H

#include "core/boat_location.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A mapped location's baked ambient occlusion (graphics G5c): presentation-only corner values
// baked from its scenery pack's geometry by tools/ambient_bake.c into the sidecar
// assets/geometry/<location>/ambient.bin. Physics, the server and the multiplayer content
// digest never read it; scenery without a loaded sidecar draws unoccluded.
//
// File, little-endian: a 32-byte header of eight u32 (magic "PAMB", version, triangle count,
// the scenery key's low and high words, payload bytes, compressed bytes, zero) and one raw
// DEFLATE stream of exactly the payload. The payload holds three u8 per scenery triangle, one per
// corner in the triangle's own vertex order: how much of the open hemisphere above that corner
// nearby scenery closes off, 0 (open) to 255 (closed), from BOAT_AMBIENT_RAYS fixed rays of up to
// BOAT_AMBIENT_RANGE metres, each hit weighted by its nearness. Water triangles neither occlude
// nor carry a value (always 0). Only the pack's static geometry counts: no sun, actors, boats or
// the run-time kit.
#define BOAT_AMBIENT_MAGIC 0x424D4150u // "PAMB"
#define BOAT_AMBIENT_VERSION 1u
#define BOAT_AMBIENT_HEADER 32
#define BOAT_AMBIENT_RAYS 24
#define BOAT_AMBIENT_RANGE 6.0f // Metres.

// FNV-1a over MESH: vertex and triangle counts, then each vertex's X/Y/Z bits and each triangle's
// three indices and colour. A sidecar records the key of the scenery it was baked from; a
// different key is stale.
uint64_t boat_ambient_key(const Mesh3D *mesh);
// The loaded corner values of MAP's scenery (three per triangle of boat_scenery_of(MAP)), or NULL.
const unsigned char *boat_ambient_of(const BoatMapDescription *map);
// Validate sidecar bytes against MAP's loaded scenery and keep their values; true at once when
// already loaded. Fails, loading nothing, without loaded scenery, for a malformed or truncated
// file, a non-zero water corner, or a key that is not the scenery's.
bool boat_ambient_load_bytes(const BoatMapDescription *map, const void *bytes, size_t size);
bool boat_ambient_load(const BoatMapDescription *map, const char *path);
// ASSETS "geometry/<name>/ambient.bin" for a mapped LOCATION; false if it does not fit.
bool boat_ambient_path(
    const BoatWorldDefinition *location, const char *assets, char *path, size_t capacity);
// Load a mapped LOCATION's sidecar if it exists and is valid; false otherwise.
bool boat_ambient_load_location(const BoatWorldDefinition *location, const char *assets);
// The header of a sidecar for TRIANGLES corner triples with KEY whose stream is COMPRESSED bytes.
void boat_ambient_header(
    unsigned char out[BOAT_AMBIENT_HEADER], size_t triangles, uint64_t key, size_t compressed);
// Release every loaded sidecar.
void boat_ambient_unload(void);

#endif
