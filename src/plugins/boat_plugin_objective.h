#ifndef PROTEUS_BOAT_PLUGIN_OBJECTIVE_H
#define PROTEUS_BOAT_PLUGIN_OBJECTIVE_H

// Plugin data: the objective checkpoint values and their byte codec.
#include "plugins/boat_plugin.h"

// P3.1 transactions, P3.2 native objective.hud adapter, P3.3a optional objective.timers.
#define BOAT_PLUGIN_STATE_BYTES 16384
#define BOAT_PLUGIN_STATE_DEPTH 16
#define BOAT_PLUGIN_STATE_TABLES 256
#define BOAT_PLUGIN_EVENTS 64
#define BOAT_PLUGIN_HUD 8
#define BOAT_PLUGIN_TIMERS 16
#define BOAT_PLUGIN_COMMANDS 32

typedef struct {
    unsigned size;
    unsigned char bytes[BOAT_PLUGIN_STATE_BYTES];
} BoatPluginState;

typedef struct {
    int64_t tick, sequence;
    char kind[48], id[BOAT_PLUGIN_TEXT];
    int64_t load; // S1d cargo events: the crate's load number, from 1; zero omits the field.
    int64_t crew; // P5 room events: the player's crew (fleet slot + 1); zero omits the field.
} BoatPluginEvent;

typedef struct {
    char id[BOAT_PLUGIN_TEXT], label[BOAT_PLUGIN_TEXT];
    int64_t value;
} BoatPluginHud;

typedef struct {
    char id[BOAT_PLUGIN_TEXT];
    int64_t tick; // Absolute simulation tick; consumed by the first successful due update.
} BoatPluginTimer;

typedef struct {
    char kind[16], id[BOAT_PLUGIN_TEXT], label[BOAT_PLUGIN_TEXT];
    int64_t value; // HUD value or timer deadline; zero for removal.
    unsigned generation;
} BoatPluginCommand;

// Caller-owned checkpoint. No Lua pointers; assignment copies the entire committed result.
// One owner, generation fixed by host. Failed calls retain all fields except quarantine.
typedef struct {
    BoatPluginState state;
    BoatPluginCommand commands[BOAT_PLUGIN_COMMANDS]; // Last accepted batch, inspection only.
    unsigned command_count;
    BoatPluginHud hud[BOAT_PLUGIN_HUD];
    unsigned hud_count;
    BoatPluginTimer timers[BOAT_PLUGIN_TIMERS]; // Sorted by (tick, id).
    unsigned timer_count;
    bool timers_enabled; // Host capability grant; scripts cannot change it.
    // S1c host grant for a free package's work sites: accepts work.enable {id}, and since S1d
    // cargo.supply {id} and work.guide {id} (each at most once per batch). The commands only
    // record intent in COMMANDS; the host validates and applies them before it commits the
    // batch, and keeps the previous checkpoint if it cannot.
    bool work_enabled;
    uint32_t rng;
    int64_t acknowledged, tick;
    unsigned state_version; // Zero in older harness fixtures means schema 1.
    unsigned generation;
    bool initialized, quarantined;
} BoatPluginObjective;

// Timer table invariants shared by stepping, the digest and the byte codec.
bool boat_plugin_objective_timers_valid(const BoatPluginObjective *objective);
// Canonical hash of the complete committed checkpoint, excluding quarantine/struct padding.
uint64_t boat_plugin_objective_digest(const BoatPluginObjective *objective);
// Explicit little-endian checkpoint codec. Commands are inspection-only and never encoded.
bool boat_plugin_objective_encode(
    const BoatPluginObjective *objective, unsigned char *out, size_t capacity, size_t *size);
bool boat_plugin_objective_decode(
    const unsigned char *bytes, size_t size, BoatPluginObjective *objective);

#endif
