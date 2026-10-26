#ifndef PROTEUS_BOAT_COMBAT_H
#define PROTEUS_BOAT_COMBAT_H

#include "boat.h"

#define BOAT_COMBAT_BULLETS 64
#define BOAT_COMBAT_GRENADES 4
#define BOAT_COMBAT_WAVES 4
#define BOAT_COMBAT_PLAYERS 4

typedef struct {
    float health, damage, bullet_speed, bullet_range;
    float grenade_distance, wave_height, wave_speed, wave_range;
    uint32_t fire_ticks, heat_ticks, cool_ticks, grenade_ticks, splash_ticks, fuse_ticks,
        grace_ticks;
} BoatCombatRules;

typedef struct {
    Vector2 position, direction;
    float height;
    uint64_t born, ended;
    uint32_t generation, life, owner;
    bool active;
} BoatCombatObject;

typedef struct {
    float health, heat;
    uint64_t life_start, next_fire, next_grenade, defeated, hit_tick;
    uint32_t generation, request, hit_serial, killer;
    bool overheated;
} BoatCombatPlayer;

typedef struct BoatCombat {
    bool enabled, firing;
    BoatCombatRules rules;
    BoatCombatPlayer players[BOAT_COMBAT_PLAYERS];
    BoatCombatObject bullets[BOAT_COMBAT_BULLETS], grenades[BOAT_COMBAT_GRENADES];
    BoatRadialWave waves[BOAT_COMBAT_WAVES];
} BoatCombat;

#define BOAT_COMBAT_BYTES 4052
// Canonical big-endian checkpoint representation, including rules; no padding on wire.
void boat_combat_encode(const BoatCombat *combat, uint8_t bytes[BOAT_COMBAT_BYTES]);
bool boat_combat_decode(BoatCombat *combat, const uint8_t bytes[BOAT_COMBAT_BYTES], uint64_t tick);

struct BoatFleet;
BoatCombatRules boat_combat_default_rules(void);
void boat_combat_enable(struct BoatFleet *fleet);
void boat_combat_clear(struct BoatFleet *fleet);
void boat_combat_spawn(struct BoatFleet *fleet, unsigned slot);
void boat_combat_leave(struct BoatFleet *fleet, unsigned slot);
// Tick order: launches and fuse advancement, physical integration, then swept hits.
void boat_combat_advance(struct BoatFleet *fleet, const BoatInput inputs[BOAT_COMBAT_PLAYERS]);
void boat_combat_hits(struct BoatFleet *fleet, const Boat before[BOAT_COMBAT_PLAYERS]);
void boat_combat_bind(struct BoatFleet *fleet);
#endif
