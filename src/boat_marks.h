#ifndef PROTEUS_BOAT_MARKS_H
#define PROTEUS_BOAT_MARKS_H

#include "boat_fleet.h"
#include "boat_map.h"

#include <stdbool.h>
#include <stdint.h>

// Plugin P5a: named circles on the water that a free package authors for its objective. The
// native side only reports when a fleet boat enters one; scoring is the package's business.
#define BOAT_MARKS_MAX 16
#define BOAT_MARK_TEXT 24 // Id and name capacity, including the terminator.
#define BOAT_MARK_RADIUS_MIN 10.0f
#define BOAT_MARK_RADIUS_MAX 200.0f
#define BOAT_MARK_COORDINATE_MAX 20000.0f
// A mark's centre needs this much open water around it.
#define BOAT_MARK_WATER_CLEARANCE 4.0f
#define BOAT_MARK_NONE 0xffu

typedef struct {
    char id[BOAT_MARK_TEXT]; // Event id: 1..23 of a-z, 0-9 and underscore.
    char name[BOAT_MARK_TEXT]; // Display name: 1..23 printable ASCII without edge spaces.
    float x, z, radius; // Simulation metres.
} BoatMark;

typedef struct {
    unsigned count;
    BoatMark items[BOAT_MARKS_MAX];
} BoatMarkSet;

typedef struct {
    char field[64];
    char message[160];
} BoatMarkDiagnostic;

// Structural checks without a map: count, ids, names, finite bounded coordinates, radius and no
// overlap between circles. DIAGNOSTIC may be NULL.
bool boat_marks_validate(const BoatMarkSet *marks, BoatMarkDiagnostic *diagnostic);
// Every centre on open water with BOAT_MARK_WATER_CLEARANCE. An unmapped location (MAP NULL)
// keeps each circle inside HALF_EXTENT instead.
bool boat_marks_check_map(const BoatMarkSet *marks, const BoatMap *map, Vector2 half_extent,
    BoatMarkDiagnostic *diagnostic);
// FNV-1a over every field, names included, with float bits least significant first.
uint64_t boat_marks_digest(const BoatMarkSet *marks);

// Which mark each fleet slot was inside after the previous observation. Pointer-free; a slot
// that is inactive or on a new life starts outside every mark.
typedef struct {
    uint32_t life[BOAT_FLEET_SLOTS];
    unsigned char inside[BOAT_FLEET_SLOTS]; // Mark index or BOAT_MARK_NONE.
} BoatMarkTracker;

typedef struct {
    unsigned char mark; // Index into the mark set.
    unsigned char slot; // Fleet slot; the objective's crew is slot + 1.
} BoatMarkEntry;

void boat_marks_tracker_reset(BoatMarkTracker *tracker);
// Compare every slot's position with the marks and write one entry per slot that moved from
// outside to inside a mark, in slot order. Circles never overlap, so a boat is inside at most
// one; OUT needs room for BOAT_FLEET_SLOTS entries. Returns the entry count.
unsigned boat_marks_observe(const BoatMarkSet *marks, BoatMarkTracker *tracker,
    const BoatFleetState *fleet, BoatMarkEntry out[BOAT_FLEET_SLOTS]);

#endif
