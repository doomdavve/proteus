#ifndef PROTEUS_BOAT_PRESENTATION_H
#define PROTEUS_BOAT_PRESENTATION_H

#include "boat.h"
#include "boat_flag.h"

#define BOAT_WATER_PARTICLES 384

typedef struct {
    Vector2 position, velocity;
    float height, lift;
    float age, lifetime, size;
    bool spray, wash;
    float variation; // Stable birth variation; independent of the front-spray sequence.
} BoatWaterParticle;

// Cosmetic per-boat state: particles, flag cloth and camera follow. Stepped on the same fixed
// ticks as physics, immediately after the boat; physics never reads it. Zero is a reset.
typedef struct {
    BoatFlag flag; // Visual cloth only; never contributes forces to the hull.
    float camera_heading; // Smoothed follow direction; replays record it as a view value.
    BoatWaterParticle water[BOAT_WATER_PARTICLES];
    unsigned int next_particle, emission, wake_emission;
    float wake_distance, spray_time, propeller_time;
    // Last landing: 0..1 strength from the fall speed, seconds since touchdown and a serial
    // that advances once per landing (splash and slap). Replays reset it.
    float landing_strength, landing_age;
    unsigned landing_serial;
    unsigned seen_landings; // Boat landing count + 1; zero until the first step.
    float fall_speed; // Downward speed after the previous tick, before any touchdown.
} BoatPresentation;

void boat_presentation_reset(BoatPresentation *presentation);
// Follow BOAT after its physics tick ending at NOW. Reads BOAT and WORLD; writes neither.
void boat_presentation_step(
    BoatPresentation *presentation, const Boat *boat, const BoatWorld *world, double now);
// Advance only the cloth; replay views rebuild it from sampled poses.
void boat_presentation_step_flag(BoatFlag *flag, const Boat *boat, double time);

#endif
