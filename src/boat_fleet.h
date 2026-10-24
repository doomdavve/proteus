#ifndef PROTEUS_BOAT_FLEET_H
#define PROTEUS_BOAT_FLEET_H

#include "boat.h"

// Multiplayer M2: one authority's local fleet. Slot index is the stable player ID and also the
// slot's wake emitter. Every boat uses normalized mass BOAT_FLEET_MASS.
#define BOAT_FLEET_SLOTS BOAT_OBJECT_BOATS
#define BOAT_FLEET_PAIRS (BOAT_FLEET_SLOTS * (BOAT_FLEET_SLOTS - 1) / 2)
#define BOAT_FLEET_SOLVER_PASSES 4
#define BOAT_FLEET_MASS 1.0f
// A blocked spawn request retries at 10 Hz.
#define BOAT_FLEET_SPAWN_RETRY_TICKS 12

typedef struct {
    uint64_t requested; // Tick of the pending spawn request.
    Boat boat; // Physical state; meaningful while active.
    uint32_t life; // Spawn generation: increments with every placement, zero before the first.
    uint32_t request; // Queue order of the pending request; earlier requests spawn first.
    bool active; // Integrates, collides and emits wakes.
    bool pending; // Waiting for a safe spawn; an active boat stays physical meanwhile.
} BoatFleetSlot;

// Complete future-affecting fleet state, free of pointers: assignment saves a checkpoint and
// boat_fleet_restore() resumes it in a fleet built from the same definition and water.
typedef struct {
    uint64_t tick; // Authoritative world clock; equals wakes.tick.
    uint32_t next_request;
    BoatFleetSlot slots[BOAT_FLEET_SLOTS];
    BoatWakeObjects wakes; // The shared arcade pool and per-slot emitters.
} BoatFleetState;

// Diagnostics of the latest step; physics never reads them.
typedef struct {
    unsigned pair_tests; // Pair candidates examined over all passes, at most six per pass.
    unsigned contacts; // Touching pairs in the first pass.
    unsigned passes; // Solver passes that applied a response, at most four.
    unsigned residual; // Pairs still touching after the last pass.
    unsigned shore_holds; // Separations kept at the previous position instead of crossing land.
    unsigned spawned, spawn_blocked; // Requests placed, and requests left waiting.
    unsigned emission_failures; // Wake capacity contract violations; must stay zero.
    // Accumulated contact impulse per pair over all passes, pairs (i, j > i) in canonical order.
    float pair_impulse[BOAT_FLEET_PAIRS];
} BoatFleetStats;

typedef struct {
    BoatWorld world; // Immutable definition and water settings; objects borrow state.wakes.
    BoatFleetState state;
    BoatFleetStats stats;
} BoatFleet;

// An empty fleet at tick zero. Mapped fleets select the compiled crop (one map per process).
// WATER may not enable the legacy D3 trail, whose history is not checkpointed; its borrowed
// objects pointer is replaced by the fleet's own pool. False for unsupported water/wake recipes.
bool boat_fleet_init(BoatFleet *fleet, const BoatWorldDefinition *definition, BoatWater water,
    unsigned life_ticks, float crest_height);
// Queue a join or a reset of SLOT. It is placed at an authored safe candidate at a later tick
// boundary, in request order; repeated requests while pending keep their place.
bool boat_fleet_request_spawn(BoatFleet *fleet, unsigned slot);
// Place BOAT in SLOT now, bypassing the spawn rules (fixtures and adapters). Starts a new life.
void boat_fleet_place(BoatFleet *fleet, unsigned slot, Boat boat);
// Remove SLOT's hull now, between ticks; its live wakes continue and expire naturally.
void boat_fleet_leave(BoatFleet *fleet, unsigned slot);
// One authoritative tick n -> n+1 for every active slot, in the staged order documented in
// boat_fleet.c. Inputs of inactive slots are ignored. False at the clock limit.
bool boat_fleet_step(BoatFleet *fleet, const BoatInput inputs[BOAT_FLEET_SLOTS]);
// The fleet's world with its objects bound to this fleet's pool (borrowed; do not keep it past
// a move or copy of FLEET).
BoatWorld boat_fleet_world(const BoatFleet *fleet);
double boat_fleet_time(const BoatFleet *fleet);
void boat_fleet_save(const BoatFleet *fleet, BoatFleetState *checkpoint);
void boat_fleet_restore(BoatFleet *fleet, const BoatFleetState *checkpoint);
// Field-wise FNV-1a digest of every state value, independent of struct padding.
uint64_t boat_fleet_hash(const BoatFleetState *state);
// Simultaneous contact response for the candidate BOATS of one tick: up to four Jacobi passes
// over every active pair, accumulated non-negative impulses toward one restitution target per
// pair, and separations that would cross the shore held back. Exposed for validators.
BoatFleetStats boat_fleet_solve(Boat boats[BOAT_FLEET_SLOTS], const bool active[BOAT_FLEET_SLOTS],
    const BoatWorld *world, double time);

#endif
