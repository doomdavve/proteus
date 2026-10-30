#ifndef PROTEUS_BOAT_H
#define PROTEUS_BOAT_H

#include "boat_clock.h"
#include "boat_input.h"
#include "boat_wake_player.h"
#include "boat_world.h"

#define BOAT_STEP (1.0f / BOAT_TICK_HZ)
#define BOAT_LIMIT_X (BOAT_BASIN_HALF_X - 2.5f)
#define BOAT_LIMIT_Z (BOAT_BASIN_HALF_Z - 2.5f)
#define BOAT_CONTACT_RESTITUTION 0.12f
// Handling compatibility: recordings, saved bests and network peers must match it.
#define BOAT_PHYSICS_REVISION 9
// Full command travel in 0.25 seconds; engine response remains separately smoothed.
#define BOAT_THROTTLE_COMMAND_RATE 4.0f

typedef enum { BOAT_WATERBORNE, BOAT_AIRBORNE, BOAT_LANDING, BOAT_CAPSIZED } BoatMotionState;

// Physical boat state. Cosmetic particles, cloth and camera follow live in BoatPresentation;
// course progress and the clock live in BoatSession.
typedef struct {
    Vector2 position; // Water-plane X/Z coordinates.
    Vector2 velocity;
    float heading; // Zero faces north (-Z); positive turns right.
    float throttle; // Signed thrust command, -1 (reverse) through 0 (idle) to 1 (forward).
    // Actual signed propulsion after deterministic engine response, -1..1.
    float engine_response;
    // 0 is displacement running; 1 is fully planing. Horizontal handling approximation.
    float planing;
    // Physical heave relative to the rest waterline, plus bow-up pitch and local Z roll.
    float height, vertical_velocity;
    float pitch, pitch_velocity, roll, roll_velocity;
    BoatMotionState motion_state;
    float dry_time, landing_time, inverted_contact_time;
    unsigned landings; // Whole-hull flights followed by renewed water support.
    float turn_roll_load; // Filtered gain-weighted lateral acceleration, not a pose angle.
    // Smoothed 0..1 bow-entry load; feeds spray particles and wave-entry audio, not forces.
    float wave_spray;
    float wheel; // Spring-centred steering setting, -1 (full port) to 1 (full starboard).
    float engine_angle; // Radians of thrust deflection; positive pushes stern to port.
    float yaw_velocity; // Heading change in radians/second, integrated from stern torque.
    float physical_wake_time;
    Vector2 physical_wake_start;
} Boat;

// Engine class: the forward thrust behind the hull's top speed. Everything else is shared.
// Courses, rooms, recordings and bests use the speedboat; the explorer is the solo free-riding
// boat, faster for long outings (about 30 m/s flat out against the speedboat's 23 m/s).
typedef struct {
    float thrust;
} BoatHandling;
extern const BoatHandling boat_handling_speedboat;
extern const BoatHandling boat_handling_explorer;
extern const BoatHandling boat_handling_cabin;

// Basin spawn at time zero; boat_reset_location() uses a definition's spawn instead.
void boat_reset(Boat *boat);
void boat_reset_location(Boat *boat, const BoatWorldDefinition *definition);
// Advance one fixed tick ending at world time NOW; the caller owns the clock (BoatSession).
// Translation starts from the position given, so callers sweep gates from it.
// Only WORLD's water history is written. Opposite steering unwinds the wheel three times faster.
void boat_step(Boat *boat, BoatWorld *world, double now, BoatInput input);
// boat_step() with HANDLING instead of the speedboat's.
void boat_step_handling(
    Boat *boat, BoatWorld *world, double now, BoatInput input, const BoatHandling *handling);
// Planar contact between two dynamic hulls whose transformed vertical intervals overlap:
// the separating-axis normal pushing A away from B, its penetration depth and both lever arms
// to the shared contact point.
typedef struct {
    Vector2 normal, arm_a, arm_b;
    float depth;
} BoatPairContact;
bool boat_pair_contact(const Boat *a, const Boat *b, BoatPairContact *contact);
// Relative normal speed of the two contact points; negative while approaching.
float boat_pair_speed(const Boat *a, const Boat *b, BoatPairContact contact);
// Normal impulse that changes the relative contact speed by CHANGE. Positive normalized masses
// also scale yaw inertia.
float boat_pair_impulse(BoatPairContact contact, float mass_a, float mass_b, float change);
// Apply IMPULSE (A along the normal, B opposite) and SEPARATION times the depth-plus-margin
// shift, split by inverse mass. The arms stay those of the contact.
void boat_pair_apply(Boat *a, float mass_a, Boat *b, float mass_b, BoatPairContact contact,
    float impulse, float separation);
// One-shot mutual planar impulse/separation for two dynamic hulls with vertical overlap.
// No allocation or route ownership. BoatFleet uses the helpers above in a simultaneous solver.
bool boat_collide_pair(Boat *a, float mass_a, Boat *b, float mass_b);
// True when no hull sample touches the world's shoreline or basin wall at TIME.
bool boat_hull_clear(const Boat *boat, const BoatWorld *world, double time);
BoatWakeStern boat_wake_stern(const Boat *boat, const BoatWorld *world, double time);

#endif
