#ifndef PROTEUS_BOAT_PLUGIN_H
#define PROTEUS_BOAT_PLUGIN_H

// Lua plugin P0 (docs/PLUGIN_PLAN.md, docs/PLUGIN_API.md): a bounded, data-only loader that
// evaluates one plugin's manifest and Lua entry text and decodes its voyage into owned native
// data. Input is immutable bytes; nothing here touches files (boat_plugin_io.h acquires them).
// Every load uses a fresh restricted Lua state that is closed before the call returns.
#include "boat_course.h"
#include "boat_location.h"
#include "boat_race.h"
#include "boat_scene_definition.h"
#include "boat_voyage.h"

#include <stddef.h>
#include <stdint.h>

#define BOAT_PLUGIN_CALLBACK_INSTRUCTIONS 100000u
#define BOAT_PLUGIN_MIGRATION_INSTRUCTIONS 500000u

#define BOAT_PLUGIN_API 1
#define BOAT_PLUGIN_STATE_VERSION 1 // Descriptor schema; objectives author their own 1..65535.
// Host capacities. Limits passed to a load may be lower, never higher.
#define BOAT_PLUGIN_MANIFEST_MAX 4096
#define BOAT_PLUGIN_SOURCE_MAX 65536
// PC console: one query expression and its bounded rendering.
#define BOAT_PLUGIN_QUERY_MAX 2048
#define BOAT_PLUGIN_QUERY_REPLY_MAX 4096
#define BOAT_PLUGIN_TEXT 24 // Shelter id and name capacity, including the terminator.

typedef enum {
    BOAT_PLUGIN_KIND_NONE,
    BOAT_PLUGIN_KIND_VOYAGE,
    BOAT_PLUGIN_KIND_FREE,
    BOAT_PLUGIN_KIND_COURSE,
    BOAT_PLUGIN_KIND_RACE,
} BoatPluginKind;
// The manifest spelling ("voyage", "free", "course" or "race"), or NONE for an unknown one.
BoatPluginKind boat_plugin_kind_parse(const char *kind);
const char *boat_plugin_kind_name(BoatPluginKind kind);

typedef enum {
    BOAT_PLUGIN_OK,
    BOAT_PLUGIN_LIMIT, // Input larger than the host allows.
    BOAT_PLUGIN_IO, // The package's files could not be acquired (boat_plugin_io).
    BOAT_PLUGIN_MANIFEST, // plugin.json is malformed, unsupported or asks for what is not granted.
    BOAT_PLUGIN_SYNTAX, // The entry text does not compile, or is not text.
    BOAT_PLUGIN_RUNTIME, // The entry raised an error while evaluating.
    BOAT_PLUGIN_MEMORY, // The Lua state exhausted its memory limit.
    BOAT_PLUGIN_BUDGET, // Entry evaluation or a callback exhausted its instruction budget.
    BOAT_PLUGIN_SCHEMA, // The returned definition is not a valid voyage.
} BoatPluginStatus;

typedef struct {
    size_t manifest_bytes, source_bytes; // At most the capacities above.
    size_t memory_bytes; // Everything the Lua state allocates, compiler included.
    uint64_t instructions; // Lua VM instructions while evaluating the entry.
} BoatPluginLimits;
extern const BoatPluginLimits boat_plugin_default_limits; // 4 KiB, 64 KiB, 2 MiB, 1 M.

// Measured work of one load/transition; filled whether or not it succeeds.
// Host staging sums sequential descriptor/init work and takes their maximum heap peak.
typedef struct {
    // Sum of completed BOAT_PLUGIN_HOOK_STEP quanta. Each successful execution phase
    // can additionally perform less than one uncounted quantum.
    uint64_t instructions;
    uint64_t entry_instructions, callback_instructions; // Independent transition phases.
    size_t peak_bytes; // Largest Lua heap, compiler included.
    size_t leaked_bytes; // Still allocated after the state closed: always zero.
    unsigned fields; // Values the native decoder read.
} BoatPluginCost;
#define BOAT_PLUGIN_HOOK_STEP 100

typedef struct {
    BoatPluginStatus status;
    char file[16]; // "plugin.json" or the entry's name; empty when not about one file.
    int line; // Source line, or 0 when unknown (always 0 for schema errors).
    char field[64]; // Path of the offending value, e.g. "voyage.shelters[3].heading".
    char message[160];
    BoatPluginCost cost;
} BoatPluginDiagnostic;

typedef struct {
    const char *manifest;
    size_t manifest_size;
    const char *source; // The manifest's entry file.
    size_t source_size;
} BoatPluginBundle;

typedef struct {
    char id[64], name[64], kind[16], entry[16], location[16];
    unsigned api, state_version;
    bool scene; // Explicit scene.define capability (voyages only).
    bool objective; // Explicit objective.hud capability (voyages only).
    bool timers; // objective.timers additionally grants bounded tick timers.
} BoatPluginManifest;

// Owns its strings: ROUTE's shelter names point into SHELTER_NAMES, so copy it only with
// boat_plugin_voyage_copy.
typedef struct BoatPluginVoyage {
    BoatPluginManifest manifest;
    bool migrates; // Optional explicit migrate callback.
    BoatVoyageRoute route;
    // L1.2: an optional, pointer-free scene envelope. Content fields arrive in later tasks; an
    // absent/empty scene means the compiled marina.
    bool has_scene;
    BoatSceneDefinition scene;
    uint64_t scene_digest; // boat_scene_digest(&scene); zero when absent.
    char shelter_ids[BOAT_VOYAGE_SHELTERS][BOAT_PLUGIN_TEXT];
    char shelter_names[BOAT_VOYAGE_SHELTERS][BOAT_PLUGIN_TEXT];
    uint64_t digest; // boat_plugin_route_digest(&route).
} BoatPluginVoyage;

// P2: pointer-free owned river race definition. DIGEST covers gameplay only; the manifest
// name is the display label and can change without restarting a run.
typedef struct {
    BoatPluginManifest manifest;
    BoatRaceCourse course;
    BoatRaceRules rules;
    uint64_t digest;
} BoatPluginRace;

// PD1: a free-boating session definition. It selects the location and nothing else; the
// engine's registered default content applies, exactly as an absent subsystem does.
typedef struct {
    BoatPluginManifest manifest;
    uint64_t digest;
} BoatPluginFree;

// PD1: a time-trial session definition. The course is the same value the session binds.
typedef struct {
    BoatPluginManifest manifest;
    BoatCourseDefinition course;
    uint64_t digest;
} BoatPluginCourse;

BoatPluginStatus boat_plugin_load_race(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginRace *out, BoatPluginDiagnostic *diagnostic);
BoatPluginStatus boat_plugin_load_free(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginFree *out, BoatPluginDiagnostic *diagnostic);
BoatPluginStatus boat_plugin_load_course(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginCourse *out, BoatPluginDiagnostic *diagnostic);
BoatPluginStatus boat_plugin_race_check_location(const BoatPluginRace *race,
    const BoatWorldDefinition *location, const BoatMap *map, BoatPluginDiagnostic *diagnostic);
BoatPluginStatus boat_plugin_free_check_location(const BoatPluginFree *free,
    const BoatWorldDefinition *location, BoatPluginDiagnostic *diagnostic);
BoatPluginStatus boat_plugin_course_check_location(const BoatPluginCourse *course,
    const BoatWorldDefinition *location, const BoatMap *map, BoatPluginDiagnostic *diagnostic);
uint64_t boat_plugin_race_digest(const BoatRaceCourse *course, const BoatRaceRules *rules);

const char *boat_plugin_status_name(BoatPluginStatus status);
// Parse and check a manifest against the host policy. OUT is written only on success; DIAGNOSTIC
// (may be NULL) is always written.
BoatPluginStatus boat_plugin_parse_manifest(const char *json, size_t size,
    const BoatPluginLimits *limits, BoatPluginManifest *out, BoatPluginDiagnostic *diagnostic);
// Evaluate BUNDLE and decode its voyage. OUT is written only on success, so a failure leaves the
// caller's last good definition intact. LIMITS NULL selects the defaults.
BoatPluginStatus boat_plugin_load_voyage(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginVoyage *out, BoatPluginDiagnostic *diagnostic);
void boat_plugin_voyage_copy(BoatPluginVoyage *destination, const BoatPluginVoyage *source);
// FNV-1a over every field of ROUTE, names included, with float bits least significant first.
uint64_t boat_plugin_route_digest(const BoatVoyageRoute *route);

// P1 staging: VOYAGE belongs to LOCATION, starts at its spawn, and its shelters and arrival are
// open water of MAP (LOCATION's compiled water). DIAGNOSTIC (may be NULL) is always written.
BoatPluginStatus boat_plugin_check_location(const BoatPluginVoyage *voyage,
    const BoatWorldDefinition *location, const BoatMap *map, BoatPluginDiagnostic *diagnostic);

// How a staged definition may replace the active one (docs/PLUGIN_API.md, reload policies).
typedef enum {
    BOAT_PLUGIN_UNCHANGED, // Identical: nothing to install.
    BOAT_PLUGIN_RENAMED, // Only display names differ: rebind them and keep all progress.
    // PC: a scene layout or voyage-progress edit that can be installed between ticks. The boat,
    // clock, water and wakes carry on; the boat is never displaced. A physical layout change
    // waits until the hull clears the candidate envelopes.
    BOAT_PLUGIN_LIVE,
    BOAT_PLUGIN_MIGRATE, // Compatible native route; explicit migration on Apply.
    BOAT_PLUGIN_RESTART_VOYAGE, // Route gameplay differs and the water does not.
    BOAT_PLUGIN_RESTART_SESSION, // The exposure or its weather range differs.
    BOAT_PLUGIN_INCOMPATIBLE, // Another plugin, kind, location or API: relaunch to use it.
} BoatPluginChange;

BoatPluginChange boat_plugin_race_classify(
    const BoatPluginRace *active, const BoatPluginRace *candidate);
BoatPluginChange boat_plugin_free_classify(
    const BoatPluginFree *active, const BoatPluginFree *candidate);
BoatPluginChange boat_plugin_course_classify(
    const BoatPluginCourse *active, const BoatPluginCourse *candidate);
const char *boat_plugin_change_name(BoatPluginChange change);
// Shelter identity is by ordered ID: a renamed ID or a reordering restarts the voyage.
BoatPluginChange boat_plugin_classify(
    const BoatPluginVoyage *active, const BoatPluginVoyage *candidate);

#endif
