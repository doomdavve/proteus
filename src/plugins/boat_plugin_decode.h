#ifndef PROTEUS_BOAT_PLUGIN_DECODE_H
#define PROTEUS_BOAT_PLUGIN_DECODE_H

// Plugin data: descriptor data as an evaluator returned it, and its Lua-free checks and decoders.
#include "plugins/boat_plugin.h"

typedef struct {
    char id[BOAT_PLUGIN_TEXT], name[BOAT_PLUGIN_TEXT];
    char caption[BOAT_PLUGIN_CAPTION]; // Tour stops only.
    double x, z, heading, radius;
} BoatPluginShelterData;

typedef struct {
    bool callbacks, migrates, has_scene;
    bool tour; // The entry returned `tour` (stops and arrival) instead of `voyage`.
    BoatSceneDefinition scene; // Copied native/worker data; no Lua pointers.
    unsigned shelter_count;
    BoatPluginShelterData shelters[BOAT_VOYAGE_SHELTERS];
    struct {
        double x, z, radius, max_speed;
    } arrival;
    struct {
        double x, z, radius, height, build;
    } exposure;
    double exposed_fraction, weather_range;
} BoatPluginVoyageData;

#define BOAT_PLUGIN_GATE_FIELDS "x", "z", "dx", "dz", "half_width"
#define BOAT_PLUGIN_SOURCE_FIELDS                                                                  \
    "x", "z", "dx", "dz", "started", "speed", "height", "duration", "lifetime", "wavelength",      \
        "spread", "half_width"
#define BOAT_PLUGIN_STORM_FIELDS "x", "z", "vx", "vz", "radius", "height", "started", "build"
#define BOAT_PLUGIN_RULE_FIELDS                                                                    \
    "grid_ticks", "countdown_ticks", "results_ticks", "after_winner_ticks", "cap_ticks",           \
        "recovery_ticks", "skip_ticks", "park_ticks", "min_players"
typedef struct {
    double start[5], gates[BOAT_RACE_GATES][5], grid[BOAT_FLEET_SLOTS][2];
    unsigned gate_count;
    double source[12], storm[8], rules[9];
    bool has_scene;
    BoatSceneDefinition scene; // Copied native/worker data; no Lua pointers.
} BoatPluginRaceData;

typedef struct {
    double version;
    bool callbacks; // S1c: init and update beside `free`, for objective.hud.
    bool migrates; // SV1i: optional migrate beside them.
    bool has_scene;
    BoatSceneDefinition scene;
    // P5a: optional marks for a room objective; copied as given, checked in boat_plugin.c.
    unsigned mark_count;
    struct {
        char id[BOAT_MARK_TEXT], name[BOAT_MARK_TEXT];
        double x, z, radius;
    } marks[BOAT_MARKS_MAX];
} BoatPluginFreeData;

typedef struct {
    double start[3], gates[BOAT_COURSE_GATES][3];
    unsigned gate_count;
} BoatPluginCourseData;

#define BOAT_PLUGIN_MATCH_RULE_FIELDS                                                              \
    "round_ticks", "intermission_ticks", "credit_ticks", "grace_ticks", "respawn_ticks",           \
        "skip_ticks", "contact_impulse", "wake_height", "min_players"
#define BOAT_PLUGIN_COMBAT_FIELDS                                                                  \
    "health", "damage", "bullet_speed", "bullet_range", "grenade_distance", "wave_height",         \
        "wave_speed", "wave_range", "cannon_pressure", "pressure_recovery", "torpedo_damage",      \
        "torpedo_speed", "torpedo_range", "torpedo_ticks", "arming_ticks", "torpedo_ammo",         \
        "fire_ticks", "heat_ticks", "cool_ticks", "grenade_ticks", "splash_ticks", "fuse_ticks",   \
        "grace_ticks"

// PD2: a deathmatch room definition as Lua returned it. Rules are integers; combat values mix
// finite numbers with integer tick counts, both validated by the native decoder.
typedef struct {
    double version;
    double rules[9], combat[23];
    bool has_scene;
    BoatSceneDefinition scene;
} BoatPluginMatchData;

// Parse BUNDLE's manifest under LIMITS (defaults when NULL), require KIND and bound the source:
// the checks every load makes before evaluating the entry.
BoatPluginStatus boat_plugin_bundle_manifest(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginKind kind, BoatPluginManifest *manifest,
    BoatPluginDiagnostic *diagnostic);

// PD2: the shared scene capability check for free, race and match packages.
BoatPluginStatus boat_plugin_take_scene(const BoatPluginManifest *manifest, bool has_scene,
    const BoatSceneDefinition *scene, bool *out_has_scene, BoatSceneDefinition *out_scene,
    uint64_t *out_digest, BoatPluginDiagnostic *diagnostic);

// PD1: decode already evaluated free/course data without opening a Lua VM.
BoatPluginStatus boat_plugin_decode_free(const BoatPluginManifest *manifest,
    const BoatPluginFreeData *data, BoatPluginFree *out, BoatPluginDiagnostic *diagnostic);
BoatPluginStatus boat_plugin_decode_course(const BoatPluginManifest *manifest,
    const BoatPluginCourseData *data, BoatPluginCourse *out, BoatPluginDiagnostic *diagnostic);
BoatPluginStatus boat_plugin_decode_race(const BoatPluginManifest *manifest,
    const BoatPluginRaceData *data, BoatPluginRace *out, BoatPluginDiagnostic *diagnostic);
BoatPluginStatus boat_plugin_decode_match(const BoatPluginManifest *manifest,
    const BoatPluginMatchData *data, BoatPluginMatch *out, BoatPluginDiagnostic *diagnostic);

// Decode already evaluated data using the same checks, without opening a Lua VM.
BoatPluginStatus boat_plugin_decode_voyage(const BoatPluginManifest *manifest,
    const BoatPluginVoyageData *data, BoatPluginVoyage *out, BoatPluginDiagnostic *diagnostic);

#endif
