#ifndef PROTEUS_BOAT_WAKE_PLAYER_H
#define PROTEUS_BOAT_WAKE_PLAYER_H
#include "boat_wake.h"

/* Application-owned world, separate from copyable Boat/BoatWater values. Boat and
 * rendering borrow its published endpoint. Destroy only after all borrowers stop. */
typedef struct BoatWakePlayer BoatWakePlayer;
typedef struct {
    double x, z; /* Actual transformed stern, metres. */
    double vx, vz; /* Actual water-relative stern velocity, m/s, including yaw. */
    double wet; /* 0..1 stern support fraction; dry flight is exactly zero. */
    double resistance; /* Existing longitudinal hull resistance magnitude, m/s^2. */
    double drag_power; /* Existing longitudinal resistance * along speed, W/kg. */
} BoatWakeStern;
typedef struct {
    double resistance_work; /* Available existing drag work, J; NOT a second drag. */
    double source_seconds, update_seconds, query_audit_seconds;
    double peak_height, peak_slope;
    size_t steps;
    BoatWakeLedger energy;
} BoatWakePlayerReport;

/* The review world covers the whole 240x160m basin, plus an absorbing exterior.
 * Same .75m / order256 / spatial8 discretization as the C oracle. */
BoatWakePlayer *boat_wake_player_create(double dt);
void boat_wake_player_destroy(BoatWakePlayer *world);
void boat_wake_player_reset(BoatWakePlayer *world);
const BoatWake *boat_wake_player_field(const BoatWakePlayer *world);
bool boat_wake_player_step(BoatWakePlayer *world, BoatWakeStern start, BoatWakeStern end);
BoatWakePlayerReport boat_wake_player_report(BoatWakePlayer *world);
size_t boat_wake_player_storage(const BoatWakePlayer *world);

/* Fixed physical calibration: mass maps the handling model's per-mass forces to
 * newtons; fresh water density maps the field Hamiltonian to joules. */
#define BOAT_WAKE_MASS 2000.0
#define BOAT_WAKE_DENSITY 1000.0
#define BOAT_WAKE_SOURCE_REVISION 3
#endif
