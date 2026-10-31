#ifndef PROTEUS_BOAT_ROOM_DEFINITION_H
#define PROTEUS_BOAT_ROOM_DEFINITION_H

// PD2/PD3: one room's complete, pointer-free definition. The authority evaluates a package once
// and sends this value; clients decode and validate it without Lua, files or the compiled race and
// harbour defaults. It belongs to proteus_boat_core: physics, prediction and presentation read the
// same copied values.
#include "boat_match.h"
#include "boat_race.h"
#include "boat_scene_definition.h"

#define BOAT_ROOM_VERSION 1
#define BOAT_ROOM_ID_BYTES 64
#define BOAT_ROOM_NAME_BYTES 64
// The largest encoded definition: a scene, a course and their rules. The codec rejects more.
#define BOAT_ROOM_BYTES_MAX 16384

typedef enum {
    BOAT_ROOM_MODE_FREE, // Shared free boating.
    BOAT_ROOM_MODE_DEATHMATCH, // Timed combat rounds.
    BOAT_ROOM_MODE_RACE, // River race rounds.
    BOAT_ROOM_MODE_LIMIT
} BoatRoomMode;

typedef struct {
    char field[64];
    char message[160];
} BoatRoomDiagnostic;

// Package identity and every value the room's simulation and presentation needs. `mode` decides
// which of the optional blocks apply; the others are zero and are not encoded.
typedef struct {
    unsigned version;
    char id[BOAT_ROOM_ID_BYTES]; // Package ID, for display and diagnostics.
    char name[BOAT_ROOM_NAME_BYTES]; // Package display name.
    unsigned char mode; // BoatRoomMode
    unsigned char location; // BoatLocationId; the room's compiled map.
    BoatRaceCourse course;
    BoatRaceRules race_rules;
    BoatMatchRules match_rules;
    BoatCombatRules combat_rules;
    bool has_scene;
    BoatSceneDefinition scene;
} BoatRoomDefinition;

// Structural validation without a map: version, mode/location, names, course geometry and rules,
// scene structure. Map-dependent checks (water, shoreline, fleet spawn) belong to the package
// loader on the authority. DIAGNOSTIC may be NULL.
bool boat_room_definition_validate(const BoatRoomDefinition *room, BoatRoomDiagnostic *diagnostic);
// The structural race checks shared by package staging and room validation: gate count, unit
// directions, segment geometry, grid placement, ferry recipe and rule ranges.
bool boat_race_course_validate(
    const BoatRaceCourse *course, const BoatRaceRules *rules, BoatRoomDiagnostic *diagnostic);
// Canonical fieldwise encoding (big-endian integers, binary32 floats, bounded strings). Returns
// the byte count, or zero when the value is invalid or exceeds the capacity.
size_t boat_room_definition_encode(const BoatRoomDefinition *room, uint8_t *out, size_t capacity);
// Rejects truncation, extra bytes, non-finite values, out-of-range counts and unknown enums. On
// failure ROOM is left untouched.
bool boat_room_definition_decode(
    const uint8_t *bytes, size_t size, BoatRoomDefinition *room, BoatRoomDiagnostic *diagnostic);
// FNV-1a 64 over the canonical encoding: one identity for the values, not for their authorship.
uint64_t boat_room_definition_digest(const BoatRoomDefinition *room);

const char *boat_room_mode_name(unsigned mode);

#endif
