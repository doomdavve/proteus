#ifndef PROTEUS_BOAT_RACE_H
#define PROTEUS_BOAT_RACE_H

#include "boat_fleet.h"

// River sprint rules (docs/RACE_PLAN.md R1). Authority only: the race observes the committed
// fleet after each step and owns race placements, grid holds and the course's ferry clock.
// A BoatRace value is pointer-free; assignment saves it beside a fleet checkpoint.
#define BOAT_RACE_GATES 8 // Ordered checkpoints, the last of which is the finish.
#define BOAT_RACE_NOBODY 0xffu
#define BOAT_RACE_MAX_EVENTS (BOAT_FLEET_SLOTS * (BOAT_RACE_GATES + 2) + 2)
// Finish times are counted in 1/1024 ticks after GO; equal values are exact ties.
#define BOAT_RACE_SUBTICKS 1024u

typedef enum {
    BOAT_RACE_WAITING, // Free boating until enough players are present.
    BOAT_RACE_GRID, // Roster frozen; entrants placed and held behind the start line.
    BOAT_RACE_COUNTDOWN, // Still held; throttle may be prepared. Ends at the GO tick.
    BOAT_RACE_RACING,
    BOAT_RACE_RESULTS, // Places fixed; then the present players are gridded again.
} BoatRacePhase;

typedef enum { BOAT_RACE_RUNNING, BOAT_RACE_FINISHED, BOAT_RACE_DNF } BoatRaceStatus;

typedef enum {
    BOAT_RACE_PHASE, // The race entered PHASE.
    BOAT_RACE_GATE, // SLOT cleared GATE (not reported for the finish).
    BOAT_RACE_FINISH, // SLOT finished in PLACE at TIME.
    BOAT_RACE_OUT, // SLOT did not finish: it left or ran out of time.
    BOAT_RACE_RECOVER, // SLOT was placed at its recovery point.
    BOAT_RACE_PARK, // Finished SLOT was parked and held past the finish, facing back up-course.
    BOAT_RACE_ABANDON, // Every entrant left; back to waiting.
} BoatRaceKind;

// A line through CENTRE across the unit DIRECTION of travel, HALF_WIDTH to either side.
typedef struct {
    Vector2 centre, direction;
    float half_width;
} BoatRaceGate;

typedef struct {
    BoatRaceGate start; // Visible start line; progress and early recoveries measure from it.
    BoatRaceGate gates[BOAT_RACE_GATES];
    unsigned gate_count;
    Vector2 grid[BOAT_FLEET_SLOTS]; // Abreast behind the start line, facing its direction.
    // Authored crossing source; STARTED is relative to GO (negative for a pre-roll).
    BoatVesselWake source;
    BoatStorm storm; // Authored local weather patch; STARTED relative to GO.
} BoatRaceCourse;

typedef struct {
    uint64_t grid_ticks, countdown_ticks, results_ticks;
    uint64_t after_winner_ticks; // Remaining entrants may finish this long after the winner.
    uint64_t cap_ticks; // Absolute racing time limit after GO.
    uint64_t recovery_ticks; // Automatic recovery after a capsize.
    uint64_t skip_ticks; // A reset request after a capsize recovers no earlier than this.
    uint64_t park_ticks; // A finisher coasts this long, then is parked out of the corridor.
    unsigned min_players;
} BoatRaceRules;

// One entrant of the current or latest round. Rows outlive a disconnect.
typedef struct {
    uint32_t identity; // The join that entered the round; zero for no entrant.
    uint64_t finish; // Subticks after GO; meaningful once finished.
    uint32_t recoveries;
    uint8_t status, place, gates; // Gates cleared, the finish included; place 0 unplaced.
} BoatRaceEntrant;

typedef struct {
    Vector2 previous; // Position at the last observation of this life.
    uint64_t capsized_at;
    uint32_t identity; // Zero when absent; a new value for every join.
    uint32_t life; // Fleet life last observed.
    bool tracked; // PREVIOUS belongs to LIFE.
    bool capsized; // Latched once per life.
    bool recovering; // Waiting for a clear recovery (or, once finished, parking) point.
    bool parked; // Finished and held past the finish until the round ends.
    bool reset_wanted;
} BoatRaceSlot;

typedef struct {
    BoatRaceRules rules;
    BoatRaceCourse course;
    BoatRacePhase phase;
    uint64_t phase_end; // Tick the timed phase ends; zero while waiting or racing.
    uint64_t go_tick; // Tick boundary that released the current round.
    uint64_t deadline; // Racing ends here: the cap, or after_winner_ticks after the winner.
    uint32_t round, joins;
    unsigned placed; // Places awarded this round.
    BoatRaceSlot slots[BOAT_FLEET_SLOTS];
    BoatRaceEntrant entrants[BOAT_FLEET_SLOTS]; // Indexed by slot.
} BoatRace;

typedef struct {
    uint64_t tick, time; // Finish time in subticks.
    uint8_t kind, slot, value; // Value: the phase, the gate or the place.
} BoatRaceEvent;

typedef struct {
    unsigned count;
    BoatRaceEvent events[BOAT_RACE_MAX_EVENTS];
} BoatRaceOutput;

// 1 s grid, 3 s countdown, 8 s results, 15 s after the winner, 90 s cap, 2 s recovery,
// finishers parked 2 s after their finish.
BoatRaceRules boat_race_default_rules(void);
// The shortened Eriksberg/bridge sprint (R0 route) with its crossing ferry.
const BoatRaceCourse *boat_race_river_course(void);
// False for invalid rules or course geometry.
bool boat_race_init(BoatRace *race, const BoatRaceRules *rules, const BoatRaceCourse *course);
// A player joins SLOT. While waiting they boat freely from an ordinary fleet spawn; otherwise
// they stay unplaced until the next grid. False for an invalid or occupied slot.
bool boat_race_join(BoatRace *race, BoatFleet *fleet, unsigned slot);
// SLOT's player leaves; their hull is removed. A running entrant becomes DNF.
void boat_race_leave(BoatRace *race, BoatFleet *fleet, unsigned slot);
// Apply the latest round's course source to FLEET's water, as gridding does. Call it after
// restoring a race and fleet checkpoint into a fleet initialised from the definition.
void boat_race_bind(const BoatRace *race, BoatFleet *fleet);
// After each boat_fleet_step: gate crossings, finishes, recoveries and phase changes.
void boat_race_observe(BoatRace *race, BoatFleet *fleet, BoatRaceOutput *out);
// A reset request (R). Racing entrants use the same recovery as a capsize; a finisher parks at
// once. Held boats (grid, countdown, parked) ignore it.
void boat_race_reset(BoatRace *race, BoatFleet *fleet, unsigned slot);
// Swept forward crossing of GATE from A to B: the fraction along the segment, or -1.
float boat_race_cross(const BoatRaceGate *gate, Vector2 a, Vector2 b);
// Metres along the current segment (the last cleared gate, or the start, to the next).
float boat_race_progress(const BoatRace *race, const BoatFleet *fleet, unsigned slot);
// Live position of an entrant among this round's entrants: fixed places for finishers, then
// running boats by gates and progress (provisional), then DNF. Zero for a non-entrant.
unsigned boat_race_position(const BoatRace *race, const BoatFleet *fleet, unsigned slot);
// True when SLOT's current player entered this round.
bool boat_race_entrant(const BoatRace *race, unsigned slot);

#endif
