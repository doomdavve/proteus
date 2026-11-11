#ifndef PROTEUS_BOAT_COMBAT_H
#define PROTEUS_BOAT_COMBAT_H

#include "boat.h"

#define BOAT_COMBAT_BULLETS 40 // Short-lived water packets.
#define BOAT_COMBAT_TORPEDOES 8
#define BOAT_COMBAT_GRENADES 4
#define BOAT_COMBAT_WAVES 4
#define BOAT_COMBAT_PLAYERS 4
#define BOAT_CANNON_LIFT 3.0f // Initial upward speed, m/s.
#define BOAT_CANNON_GRAVITY 16.0f // Downward acceleration, m/s squared.
#define BOAT_CANNON_RADIUS .275f // Half the visible water cube's side.
#define BOAT_CANNON_MUZZLE_HEIGHT .92f
#define BOAT_CANNON_MUZZLE_FORWARD 1.45f
#define BOAT_CANNON_MUZZLE_SIDE -.28f // Starboard in the renderer's mirrored-X hull frame.

// Vertical displacement from launch, shared with deterministic presentation fixtures.
float boat_combat_cannon_rise(float age);
// Renderer-space opening position, attached to the boat's full pitch/roll/heading pose.
Vector3 boat_combat_cannon_muzzle(const Boat *boat);

typedef struct {
    float health, damage, bullet_speed, bullet_range;
    float grenade_distance, wave_height, wave_speed, wave_range;
    float cannon_pressure, pressure_recovery, torpedo_damage, torpedo_speed, torpedo_range;
    uint32_t torpedo_ticks, arming_ticks, torpedo_ammo;
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
    float health, heat, pressure;
    uint64_t next_torpedo, pressure_tick;
    uint32_t torpedo_request, ammo, pressure_owner, pressure_generation;
    // Recent damage bursts, addressed by target and fenced by target life/generation.
    float damage[BOAT_COMBAT_PLAYERS];
    uint64_t damage_tick[BOAT_COMBAT_PLAYERS];
    uint32_t damage_life[BOAT_COMBAT_PLAYERS], damage_generation[BOAT_COMBAT_PLAYERS];
    uint64_t life_start, next_fire, next_grenade, defeated, hit_tick;
    uint32_t generation, request, hit_serial, killer;
    bool overheated;
} BoatCombatPlayer;

typedef struct BoatCombat {
    bool enabled, firing;
    BoatCombatRules rules;
    BoatCombatPlayer players[BOAT_COMBAT_PLAYERS];
    BoatCombatObject bullets[BOAT_COMBAT_BULLETS], grenades[BOAT_COMBAT_GRENADES];
    BoatCombatObject torpedoes[BOAT_COMBAT_TORPEDOES];
    BoatRadialWave waves[BOAT_COMBAT_WAVES];
} BoatCombat;

#define BOAT_COMBAT_BYTES 3716
#define BOAT_COMBAT_WORLD_LIMIT 8192.0f // Same horizontal envelope as the protocol.
#define BOAT_COMBAT_ECHO_TICKS 45 // Longest visual projectile echo after deactivation.
// Canonical big-endian checkpoint representation, including rules; no padding on wire.
void boat_combat_encode(const BoatCombat *combat, uint8_t bytes[BOAT_COMBAT_BYTES]);
bool boat_combat_decode(BoatCombat *combat, const uint8_t bytes[BOAT_COMBAT_BYTES], uint64_t tick);
// Compact per-tick representation: rules are taken from the installed checkpoint, inactive
// objects are omitted, and short-lived tick ages and directions are packed. At the full pool
// occupancy it fits in three 1024-byte pages; typical play fits in one or two.
#define BOAT_COMBAT_COMPACT_BYTES 3072
size_t boat_combat_compact_encode(
    const BoatCombat *combat, uint8_t *bytes, size_t capacity, uint64_t tick);
bool boat_combat_compact_decode(BoatCombat *combat, const uint8_t *bytes, size_t size,
    uint64_t tick, const BoatCombatRules *rules);

struct BoatFleet;
// Fieldwise equality, independent of padding: the checkpoint's rules must equal the room's.
bool boat_combat_rules_equal(const BoatCombatRules *a, const BoatCombatRules *b);
void boat_combat_enable(struct BoatFleet *fleet, const BoatCombatRules *rules);
void boat_combat_clear(struct BoatFleet *fleet);
void boat_combat_spawn(struct BoatFleet *fleet, unsigned slot);
void boat_combat_leave(struct BoatFleet *fleet, unsigned slot);
// Tick order: launches and fuse advancement, physical integration, then swept hits.
void boat_combat_advance(struct BoatFleet *fleet, const BoatInput inputs[BOAT_COMBAT_PLAYERS]);
void boat_combat_hits(struct BoatFleet *fleet, const Boat before[BOAT_COMBAT_PLAYERS]);
void boat_combat_bind(struct BoatFleet *fleet);
#endif
