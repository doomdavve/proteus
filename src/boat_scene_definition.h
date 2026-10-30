#ifndef PROTEUS_BOAT_SCENE_DEFINITION_H
#define PROTEUS_BOAT_SCENE_DEFINITION_H

// Location-life L1.1/L1.2: pointer-free native storage for one authored scene revision.
// It belongs to proteus_boat_core: physics, presentation and validation read the same copied
// values. It never points at Lua, files or a plugin host. Simulation x/z coordinates and renderer
// headings follow the voyage API; a renderer converts x once at its boundary.
#include "boat_model.h"
#include "waterfront_route.h"

#include <stdbool.h>
#include <stdint.h>

#define BOAT_SCENE_VERSION 1
#define BOAT_SCENE_TEXT 24 // ID and attachment capacity, including the terminator.
#define BOAT_SCENE_BERTHS 7
#define BOAT_SCENE_MODULES 4
#define BOAT_SCENE_HALF_EXTENT 20000.0f
#define BOAT_SCENE_ENTRANCE_MIN_WIDTH 4.0f
#define BOAT_SCENE_ENTRANCE_MAX_WIDTH 40.0f
#define BOAT_SCENE_GANGWAY_MIN_LENGTH 2.0f
#define BOAT_SCENE_GANGWAY_MAX_LENGTH 20.0f

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
    BOAT_SCENE_MODULE_PONTOON,
    BOAT_SCENE_MODULE_PONTOON_SMALL,
    BOAT_SCENE_MODULE_GANGWAY,
    BOAT_SCENE_MODULE_QUAY,
    BOAT_SCENE_MODULE_KINDS
} BoatSceneModuleKind;

typedef enum {
    BOAT_SCENE_MOORING_NONE,
    BOAT_SCENE_MOORING_PONTOON,
    BOAT_SCENE_MOORING_QUAY, // Compiled workboat only; not accepted from Lua.
} BoatSceneMooring;

typedef struct {
    char id[BOAT_SCENE_TEXT];
    unsigned char recipe; // BoatSceneBerthRecipe
    unsigned char mooring; // BoatSceneMooring
    float x, z; // Simulation metres.
    float heading; // Renderer radians; forward is (sin, -cos).
} BoatSceneBerth;

typedef struct {
    char id[BOAT_SCENE_TEXT];
    unsigned char kind; // BoatSceneModuleKind
    float x, z; // Pontoon/quay centre in simulation metres; gangway shore anchor.
    float heading;
    float anchor_x, anchor_z; // Gangway shore anchor in simulation metres.
    char attaches[BOAT_SCENE_TEXT]; // Gangway target pontoon ID.
} BoatSceneModule;

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
BoatModelKind boat_scene_berth_kind(BoatSceneBerthRecipe recipe);

const BoatSceneBerth *boat_scene_find_berth(const BoatSceneDefinition *scene, const char *id);
const BoatSceneModule *boat_scene_find_module(const BoatSceneDefinition *scene, const char *id);

#endif
