#ifndef PROTEUS_BOAT_SCENE_DEFINITION_H
#define PROTEUS_BOAT_SCENE_DEFINITION_H

// Location-life L1.1/L1.2: pointer-free native storage for one authored scene revision.
// It belongs to proteus_boat_core: physics, presentation and validation read the same copied
// values. It never points at Lua, files or a plugin host. Simulation x/z coordinates and renderer
// headings follow the voyage API; a renderer converts x once at its boundary.
#include "boat_model.h"
#include "waterfront_flock.h"
#include "waterfront_route.h"

#include <stdbool.h>
#include <stdint.h>

#define BOAT_SCENE_VERSION 3
#define BOAT_SCENE_TEXT 24 // ID and attachment capacity, including the terminator.
#define BOAT_SCENE_BERTHS 32
#define BOAT_SCENE_MODULES 64
#define BOAT_SCENE_HALF_EXTENT 20000.0f
#define BOAT_SCENE_ENTRANCE_MIN_WIDTH 4.0f
#define BOAT_SCENE_ENTRANCE_MAX_WIDTH 40.0f
#define BOAT_SCENE_GANGWAY_MIN_LENGTH 2.0f
#define BOAT_SCENE_GANGWAY_MAX_LENGTH 20.0f
#define BOAT_SCENE_WORK_SITES 4

typedef enum {
    BOAT_SCENE_BERTH_SKIFF,
    BOAT_SCENE_BERTH_RUNABOUT,
    BOAT_SCENE_BERTH_SAILBOAT,
    BOAT_SCENE_BERTH_CRUISER,
    BOAT_SCENE_BERTH_SPEEDBOAT,
    BOAT_SCENE_BERTH_SAILBOAT_7M,
    BOAT_SCENE_BERTH_WORKBOAT,
    BOAT_SCENE_BERTH_RECIPES
} BoatSceneBerthRecipe;

typedef enum {
    BOAT_SCENE_MODULE_WALKWAY,
    BOAT_SCENE_MODULE_PLATFORM,
    BOAT_SCENE_MODULE_GANGWAY,
    BOAT_SCENE_MODULE_QUAY,
    BOAT_SCENE_MODULE_FINGER,
    BOAT_SCENE_MODULE_KINDS
} BoatSceneModuleKind;

typedef enum {
    BOAT_SCENE_MOORING_NONE,
    BOAT_SCENE_MOORING_PONTOON,
    BOAT_SCENE_MOORING_QUAY, // Compiled workboat only; not accepted from Lua.
} BoatSceneMooring;

typedef struct {
    // Zero dimensions or all-zero colors inherit the recipe. Lua rejects explicit zero
    // dimensions; its decoder canonicalizes explicit recipe defaults back to these sentinels.
    float length, beam, mast_height;
    Color hull, deck, trim;
    char name[BOAT_MODEL_NAME_BYTES];
} BoatSceneAppearance;

typedef struct {
    char id[BOAT_SCENE_TEXT];
    unsigned char recipe; // BoatSceneBerthRecipe
    unsigned char mooring; // BoatSceneMooring
    float x, z; // Simulation metres.
    float heading; // Renderer radians; forward is (sin, -cos).
    BoatSceneAppearance appearance;
} BoatSceneBerth;

typedef struct {
    char id[BOAT_SCENE_TEXT];
    unsigned char kind; // BoatSceneModuleKind
    float x, z; // Piece centre in simulation metres; gangway shore anchor.
    float heading;
    float length, width, deck_height; // Metres; top above calm water (quay: world Y).
    float anchor_x, anchor_z; // Gangway shore anchor in simulation metres.
    char attaches[BOAT_SCENE_TEXT]; // Gangway target or earlier floating parent; edge-to-edge join.
} BoatSceneModule;

// S1a: a registered work-site layout. The preset fixes the platform, crane, pad and assisted
// berth relative to the site; Lua chooses only where a site is, which way it faces and its cargo.
// Both presets share one layout: a service platform starts with its load on the pad, and a
// receiving platform starts empty and marks its pad as the place to deliver that load (S1b).
typedef enum {
    BOAT_SCENE_WORK_SERVICE_PLATFORM,
    BOAT_SCENE_WORK_RECEIVING_PLATFORM,
    BOAT_SCENE_WORK_PRESETS
} BoatSceneWorkPreset;
typedef enum { BOAT_SCENE_CARGO_CRATE, BOAT_SCENE_CARGO_PRESETS } BoatSceneCargoPreset;

typedef struct {
    char id[BOAT_SCENE_TEXT];
    unsigned char preset; // BoatSceneWorkPreset
    unsigned char cargo; // BoatSceneCargoPreset: the starting or the accepted load.
    float x, z; // Final workboat berth in simulation metres.
    float heading; // Final workboat heading, renderer radians; the whole layout turns with it.
} BoatSceneWorkSite;

// A present subsystem replaces its whole compiled collection. An absent one keeps the compiled
// default; an empty present list disables it where the native owner allows that.
typedef struct BoatSceneDefinition {
    unsigned version;
    bool entrance_present, berths_present, modules_present;
    float entrance_x, entrance_z, entrance_width;
    unsigned berth_count, module_count;
    BoatSceneBerth berths[BOAT_SCENE_BERTHS];
    BoatSceneModule modules[BOAT_SCENE_MODULES];
    // L3.2: a present vessel list replaces all travelling craft. Count zero disables them.
    bool traffic_present;
    WaterfrontRouteDefinition traffic;
    // L4.2: a present flock list replaces the compiled gull groups. Count zero disables them.
    bool gulls_present;
    WaterfrontFlockDefinition gulls;
    // S1a: cargo work sites. Only free packages may author them; nothing is compiled by default.
    bool work_present;
    unsigned work_count;
    BoatSceneWorkSite work[BOAT_SCENE_WORK_SITES];
} BoatSceneDefinition;

typedef struct {
    char field[64];
    char message[160];
} BoatSceneDiagnostic;

// True when the definition is structurally valid. Cross-checks against the compiled map and
// voyage belong to the plugin staging adapter, which owns those inputs.
bool boat_scene_validate(const BoatSceneDefinition *scene, BoatSceneDiagnostic *diagnostic);
// True when any subsystem is authored. An envelope-only scene changes no runtime content.
bool boat_scene_has_content(const BoatSceneDefinition *scene);
// True when two scenes differ only in gull cues or berth colors/names: a compatible cosmetic
// change that keeps the session clock, unlike dimensions, a flock count/centre or layout edit.
// With BERTH_APPEARANCE false, ignore only gull cues (the existing callback source fence).
bool boat_scene_same_except_cosmetics(
    const BoatSceneDefinition *a, const BoatSceneDefinition *b, bool berth_appearance);
// FNV-1a over explicit fields only: presence, counts, IDs, recipes, positions and attachments.
// Padding and the diagnostic are excluded.
uint64_t boat_scene_digest(const BoatSceneDefinition *scene);

bool boat_scene_identifier(const char *text);
const char *boat_scene_berth_recipe_name(BoatSceneBerthRecipe recipe);
bool boat_scene_berth_recipe_from_name(const char *name, BoatSceneBerthRecipe *recipe);
const char *boat_scene_module_kind_name(BoatSceneModuleKind kind);
bool boat_scene_module_kind_from_name(const char *name, BoatSceneModuleKind *kind);
const char *boat_scene_mooring_name(BoatSceneMooring mooring);
bool boat_scene_mooring_from_name(const char *name, BoatSceneMooring *mooring);
// Registered native hull recipe. No authored dimensions or vertices.
BoatModelSpec boat_scene_berth_spec(BoatSceneBerthRecipe recipe);
// One resolved spec for geometry, clearance, ropes, contacts and every renderer.
BoatModelSpec boat_scene_berth_resolve(const BoatSceneBerth *berth);
void boat_scene_berth_canonicalize(BoatSceneBerth *berth);
BoatModelKind boat_scene_berth_kind(BoatSceneBerthRecipe recipe);

// Distance in simulation metres to a non-gangway module footprint.
float boat_scene_module_distance(const BoatSceneModule *module, float x, float z);

const BoatSceneBerth *boat_scene_find_berth(const BoatSceneDefinition *scene, const char *id);
const BoatSceneModule *boat_scene_find_module(const BoatSceneDefinition *scene, const char *id);
const char *boat_scene_work_preset_name(BoatSceneWorkPreset preset);
bool boat_scene_work_preset_from_name(const char *name, BoatSceneWorkPreset *preset);
const char *boat_scene_cargo_preset_name(BoatSceneCargoPreset preset);
bool boat_scene_cargo_preset_from_name(const char *name, BoatSceneCargoPreset *preset);

#endif
