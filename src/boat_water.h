#ifndef PROTEUS_BOAT_WATER_H
#define PROTEUS_BOAT_WATER_H

#include "boat_vessel_wake.h"
#include "boat_wake.h"
#include "boat_wake_objects.h"
#include "geometry3d.h"

typedef struct {
    // Zero initialization selects perfectly flat, still water. Direction is simulation X/Z.
    float amplitude, wavelength, angular_speed;
    Vector2 direction; // Unit vector along wave travel.
} BoatWave;

// Compact race obstacle. Zero radius/height disables it; STARTED anchors periodic waves
// to the shared race clock, including on predicted clients.
typedef struct {
    Vector2 centre, velocity; // Centre at STARTED, travel in simulation metres/second.
    float radius, height;
    double started;
    // Seconds over which the patch grows smoothly from flat water at STARTED to HEIGHT; zero
    // is full height at once. Before STARTED the water is flat.
    float build;
} BoatStorm;

typedef struct {
    float height, vertical_velocity;
    Vector2 gradient;
} BoatStormSample;

Vector2 boat_water_storm_centre(BoatStorm storm, double time);
// The fraction of HEIGHT grown by TIME, 0..1, and its rate per second in RATE (may be NULL).
float boat_water_storm_grown(BoatStorm storm, double time, float *rate);
BoatStormSample boat_water_storm_query(BoatStorm storm, Vector2 position, double time);
void boat_water_storm_uniforms(BoatStorm storm, double time, float parameters[12]);

#define BOAT_RADIAL_WAVES 4
typedef struct {
    Vector2 centre;
    uint64_t born;
    uint32_t generation, life, owner;
    float height, speed, range;
    bool active;
} BoatRadialWave;
BoatStormSample boat_water_radial_query(BoatRadialWave wave, Vector2 position, double time);
void boat_water_radial_uniforms(const BoatRadialWave *waves, double time, float parameters[32]);

#define BOAT_WAKE_SEGMENTS 64
#define BOAT_WAKE_LIFETIME 20.0f

typedef struct {
    Vector2 position, direction; // Segment centre and unit travel direction in simulation X/Z.
    double emitted; // Absolute world time; subtract before narrowing the bounded age.
    float length, amplitude, inverse_speed;
} BoatWakeSegment;

typedef struct {
    float amplitude, wavelength, angular_speed;
    const BoatWakeObjects *objects; // Borrowed app-owned arcade world; copies do not own it.
    const BoatWakeObjects *traffic_objects; // Optional independent scenery pool (race trial).
    Vector2 direction;
    Vector2 current; // Uniform flow, or a compact channel when current_width > 0.
    float current_center, current_width; // Simulation X centre and half-width.
    float wake_amplitude; // Opt-in bounded scripted source, independent of ambient waves.
    Vector2 wake_offset; // Translate the authored source and its wave field together.
    bool player_wake_enabled;
    int player_wake_count, player_wake_next;
    BoatWakeSegment player_wake[BOAT_WAKE_SEGMENTS]; // Owned history: copies never alias a boat.
    BoatWave crossing[2]; // Smaller components; all parameters are shared with the GPU.
    const BoatRadialWave *radial; // Borrowed combat pool, independent of ordinary wakes.
    BoatStorm storm; // Localised physical rough water; no player wake slots.
    BoatVesselWake vessel; // Opt-in race trial; independent bounded source, no player slot.
} BoatWater;

typedef struct {
    float height;
    float crest_foam;
    Vector3 normal, velocity; // Y-up, simulation X/Z; velocity is local surface motion.
} BoatWaterSample;

// Kinematic four-sample rigid pose in renderer/world X/Z (X reflected from simulation).
// Shared by moored and travelling scenery; the player retains its dynamic six-support model.
// All evaluation times are double world seconds; periodic phases and bounded ages are reduced
// before narrowing to float.
Transform3D boat_water_pose(
    BoatWater water, double time, Vector3 position, float heading, float length, float beam);
BoatWater boat_water_gentle(void);
BoatWater boat_water_test(bool waves, bool current, bool wake);
bool boat_water_displaced(BoatWater water);
// Append a travelled segment. Clear/zero the water settings to discard its entire history.
void boat_water_emit_wake(BoatWater *water, Vector2 start, Vector2 end, double emitted,
    float duration, float water_speed);
// Pack live D3 segments into renderer/world-space vec4 arrays for production and probes.
// Parameters are support half-length, age at TIME, amplitude and inverse speed.
int boat_water_wake_uniforms(BoatWater water, double time, float *origins, float *parameters);
// Pack three world-space vec4 ambient components: wave vector X/Z, amplitude and the phase
// remainder(angular_speed * time, 2*pi). CPU queries subtract the same float phase.
void boat_water_wave_uniforms(BoatWater water, double time, float *parameters);
// Scripted crossing clock, or -1 outside its finite 0..24 s support (never recurring).
float boat_water_crossing_clock(BoatWater water, double time);
Vector2 boat_water_current(BoatWater water, Vector2 position);
// One source travels X=-24..24 at Z=-8 plus wake_offset over the first 12 seconds.
bool boat_water_wake_source(BoatWater water, double time, Vector2 *position);
BoatWaterSample boat_water_query(BoatWater water, Vector2 position, double time);
// The same query reading WATER in place, for per-vertex loops over one settings value.
BoatWaterSample boat_water_query_borrowed(const BoatWater *water, Vector2 position, double time);
BoatWaterSample boat_water_query_field(
    BoatWater water, const BoatWake *field, Vector2 position, double time);
// Dissipative normal resistance in simulation coordinates. Positive support is the local upward
// spring force; it caps exit damping so the combined support never pulls the hull underwater.
Vector3 boat_water_resistance(
    BoatWaterSample water, Vector3 point_velocity, float damping, float support);

#endif
