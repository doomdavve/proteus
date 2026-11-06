#ifndef PROTEUS_BOAT_CARGO_H
#define PROTEUS_BOAT_CARGO_H

#include "boat_pose.h"

// S0 fixture: one upright 0.8 m crate, one kinematic service crane and one deck.
// All positions are renderer coordinates. No cargo/cargo or reaction-force coupling.
#define BOAT_CARGO_HALF .4f
#define BOAT_CARGO_DECK_Y .75f
#define BOAT_CARGO_DECK_X 1.3f
#define BOAT_CARGO_DECK_Z 1.9f

typedef enum {
    BOAT_CARGO_PAD,
    BOAT_CARGO_HANGING,
    BOAT_CARGO_LOOSE,
    BOAT_CARGO_SECURED,
    BOAT_CARGO_FLOATING
} BoatCargoState;

typedef struct {
    BoatCargoState state;
    Vector3 position, velocity, deck_local;
    Vector3 rotation;
    float settled; // Continuous seconds of low relative speed on the deck.
    bool deck_contact;
} BoatCargo;

typedef struct {
    Vector3 origin; // Work-site translation in renderer coordinates; Y remains zero.
    float angle, reach, rope;
} BoatCargoCrane;

typedef struct {
    BoatCargo cargo;
    BoatCargoCrane crane;
    bool moored;
} BoatCargoSandbox;

// Reset keeps no outside state. The application separately resets its boat and clock.
void boat_cargo_reset(BoatCargoSandbox *sandbox);
void boat_cargo_reset_at(BoatCargoSandbox *sandbox, Vector2 mooring);
Vector3 boat_cargo_tip(const BoatCargoCrane *crane);
Vector3 boat_cargo_hook(const BoatCargoSandbox *sandbox);
// Bounded motors at the same 120 Hz as the cargo/boat. Inputs are signed unit rates.
void boat_cargo_motor(BoatCargoCrane *crane, float slew, float reach, float hoist);
// Explicit generous attachment; release keeps the instantaneous load velocity.
bool boat_cargo_hook_toggle(BoatCargoSandbox *sandbox);
bool boat_cargo_lash_toggle(BoatCargoSandbox *sandbox, const Boat *boat);
// Reject departure with a suspended hook or loose deck cargo. Re-moor only at the fixture.
bool boat_cargo_moor_toggle(BoatCargoSandbox *sandbox, const Boat *boat);
void boat_cargo_step(
    BoatCargoSandbox *sandbox, const Boat *boat, const BoatWorld *world, double now);
// Experimental bounded secured-load acceleration/braking and trim. Empty is an exact no-op.
// Apply once after boat_step_handling; no global BoatHandling/recording changes.
void boat_cargo_load_effect(const BoatCargoSandbox *sandbox, const Boat *before, Boat *boat);

#endif
