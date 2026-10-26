#ifndef PROTEUS_BOAT_MATCH_H
#define PROTEUS_BOAT_MATCH_H

#include "boat_fleet.h"

// Capsize deathmatch rules (docs/DEATHMATCH_PLAN.md). Authority only: the match observes the
// committed fleet after each step and never changes physics, the fleet hash or prediction.
#define BOAT_MATCH_NOBODY 0xffu
#define BOAT_MATCH_MAX_EVENTS 8 // Events from one observation; at most one capsize per slot.

typedef enum { BOAT_MATCH_WARMUP, BOAT_MATCH_PLAYING, BOAT_MATCH_INTERMISSION } BoatMatchPhase;
typedef enum { BOAT_MATCH_CAUSE_NONE, BOAT_MATCH_CONTACT, BOAT_MATCH_WAKE } BoatMatchCause;
typedef enum { BOAT_MATCH_CAPSIZE, BOAT_MATCH_ROUND_START, BOAT_MATCH_ROUND_END } BoatMatchKind;

typedef struct {
    uint64_t round_ticks, intermission_ticks;
    uint64_t credit_ticks; // A touch credits a capsize this long afterwards.
    uint64_t grace_ticks; // A new life neither records nor gives touches this long.
    uint64_t respawn_ticks; // Automatic respawn after a capsize.
    uint64_t skip_ticks; // A reset request after a capsize respawns no earlier than this.
    float contact_impulse; // Accumulated pair impulse that counts as a touch.
    float wake_height; // One emitter's |height| under the hull that counts as a touch.
    unsigned min_players; // Present players needed to leave warmup.
} BoatMatchRules;

typedef struct {
    int32_t score;
    uint32_t caused, capsized, self; // Self-capsizes are also counted in capsized.
} BoatMatchRow;

typedef struct {
    uint64_t tick;
    uint8_t slot, cause; // BOAT_MATCH_NOBODY and BOAT_MATCH_CAUSE_NONE for none.
} BoatMatchTouch;

typedef struct {
    BoatMatchTouch touch;
    uint64_t life_start, capsized_at;
    uint32_t life;
    bool present; // Active or waiting for a spawn.
    bool out; // Capsized or bailed out this life; scored once.
    bool respawn_requested, reset_wanted, airborne;
} BoatMatchSlot;

typedef struct {
    BoatMatchRules rules;
    BoatMatchPhase phase;
    uint64_t phase_end; // Tick the current phase ends; zero in warmup.
    uint32_t round; // Rounds started.
    BoatMatchRow rows[BOAT_FLEET_SLOTS];
    BoatMatchSlot slots[BOAT_FLEET_SLOTS];
} BoatMatch;

typedef struct {
    uint64_t tick;
    uint32_t victim_life;
    uint8_t kind, victim, attacker, cause; // Attacker BOAT_MATCH_NOBODY for a self-capsize.
    bool bailout; // Reset while upright after a touch.
} BoatMatchEvent;

typedef struct {
    unsigned count;
    BoatMatchEvent events[BOAT_MATCH_MAX_EVENTS];
    bool spawn[BOAT_FLEET_SLOTS]; // Spawn requests for the caller to queue on the fleet.
} BoatMatchOutput;

// 5 min rounds, 10 s intermission, 5 s credit window, 2 s grace, 3 s respawn (R after 1 s).
BoatMatchRules boat_match_default_rules(void);
// False for rules without a round, a credit window or a positive threshold.
bool boat_match_init(BoatMatch *match, const BoatMatchRules *rules);
// After each boat_fleet_step: presence and lives, touches, capsizes, respawns and rounds.
void boat_match_observe(BoatMatch *match, const BoatFleet *fleet, BoatMatchOutput *out);
// A reset request (R) for SLOT. True when the caller should queue the spawn now; a capsized
// boat inside skip_ticks waits, and observe requests it when the time comes. An upright boat
// touched within the credit window scores a bailout capsize for the toucher.
bool boat_match_reset(
    BoatMatch *match, const BoatFleet *fleet, unsigned slot, BoatMatchOutput *out);
// Score rank of SLOT among present players: 1 for the lead, shared on ties.
unsigned boat_match_rank(const BoatMatch *match, unsigned slot);

#endif
