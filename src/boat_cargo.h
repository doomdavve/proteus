#ifndef PROTEUS_BOAT_CARGO_H
#define PROTEUS_BOAT_CARGO_H

#include "boat_pose.h"

// S0 fixture: one upright 0.8 m crate, one kinematic service crane and one deck.
// All positions are renderer coordinates. No cargo/cargo or reaction-force coupling.
#define BOAT_CARGO_HALF .4f
#define BOAT_CARGO_DECK_Y .75f
#define BOAT_CARGO_DECK_X 1.3f
#define BOAT_CARGO_DECK_Z 1.9f
#define BOAT_CARGO_BERTH_LEFT -.6f
#define BOAT_CARGO_BERTH_RIGHT 2.4f
#define BOAT_CARGO_BERTH_BACK 5.0f
#define BOAT_CARGO_BERTH_FRONT -3.5f
#define BOAT_CARGO_MOOR_SPEED 1.5f
#define BOAT_CARGO_MOOR_ANGLE .785398163f // 45 degrees.

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
    bool mooring; // Requested assisted alignment; G cancels it before reaching the berth.
} BoatCargoSandbox;

typedef enum {
    BOAT_CARGO_MOOR_READY,
    BOAT_CARGO_MOOR_DISTANCE,
    BOAT_CARGO_MOOR_HEADING,
    BOAT_CARGO_MOOR_FAST,
    BOAT_CARGO_MOOR_UPRIGHT
} BoatCargoMoorReason;

typedef struct {
    Vector2 offset; // Renderer X/Z relative to the marked berth; zero is the final pose.
    float distance, speed, heading_error;
    BoatCargoMoorReason reason;
} BoatCargoMoorStatus;

typedef enum {
    BOAT_CARGO_SHADOW_WATER,
    BOAT_CARGO_SHADOW_PAD,
    BOAT_CARGO_SHADOW_DECK
} BoatCargoSurface;
typedef struct {
    Vector3 points[4]; // Receiver-clipped landing footprint, wound upward; count is 0 or 4.
    unsigned count;
    float clearance; // Crate bottom above the actual receiver, before the drawing offset.
} BoatCargoShadow;

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
// Reject unsafe departure. In the capture zone, start bounded assisted alignment; G cancels.
bool boat_cargo_moor_toggle(BoatCargoSandbox *sandbox, const Boat *boat);
BoatCargoMoorStatus boat_cargo_moor_status(const BoatCargoSandbox *sandbox, const Boat *boat);
// The surveyed capture zone has clear water for this bounded, one-way assisted berth motion.
void boat_cargo_moor_step(BoatCargoSandbox *sandbox, Boat *boat);
// Native toy's hold-to-stop aid; leaves the ordinary game handling unchanged.
void boat_cargo_brake(Boat *boat);
BoatCargoShadow boat_cargo_shadow(const BoatCargoSandbox *sandbox, const Boat *boat,
    const BoatWorld *world, double now, BoatCargoSurface surface);
void boat_cargo_step(
    BoatCargoSandbox *sandbox, const Boat *boat, const BoatWorld *world, double now);
// Experimental bounded secured-load acceleration/braking and trim. Empty is an exact no-op.
// Apply once after boat_step_handling; no global BoatHandling/recording changes.
void boat_cargo_load_effect(const BoatCargoSandbox *sandbox, const Boat *before, Boat *boat);

#endif
