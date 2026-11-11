#ifndef PROTEUS_BOAT_VESSEL_WAKE_H
#define PROTEUS_BOAT_VESSEL_WAKE_H

#include "raylib.h"

// Conservative R0 ferry footprint, shared by its box presentation and contact envelope.
#define BOAT_VESSEL_LENGTH 40.0f
#define BOAT_VESSEL_BEAM 10.0f

// One authored straight crossing, independent of the four player emitter slots.
// The bounded history is analytic: each point's along-track coordinate identifies its
// emission time. Zero height disables it. No allocations or accumulated clock state.
typedef struct {
    Vector2 origin, direction; // Simulation coordinates; direction must be unit length.
    double started; // Absolute world seconds; negative values provide a fixed pre-roll.
    float speed, height, duration, lifetime;
    float wavelength, spread, half_width;
} BoatVesselWake;

typedef struct {
    float height, vertical_velocity;
    Vector2 gradient;
} BoatVesselWakeSample;

bool boat_vessel_wake_valid(BoatVesselWake wake);
BoatVesselWakeSample boat_vessel_wake_query(BoatVesselWake wake, Vector2 point, double time);
// Visible hull centre, including the stationary runout after emission ends.
bool boat_vessel_wake_position(BoatVesselWake wake, double time, Vector2 *position);
// Three renderer/world-space vec4 uniforms; inactive clock is -1. CPU and GLSL use the
// same bounded float age, after subtracting the absolute start in double precision.
void boat_vessel_wake_uniforms(BoatVesselWake wake, double time, float values[12]);

#endif
