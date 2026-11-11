#ifndef PROTEUS_WATERFRONT_FLOCK_H
#define PROTEUS_WATERFRONT_FLOCK_H

#include "boat_model.h"
#include "waterfront_route.h"

#include <stdbool.h>
#include <stdint.h>

// L4.1: caller-owned gull flocks. Core data and pure-time evaluation only: no meshes, Lua,
// renderer or files. Defaults reproduce the compiled four groups; NULL selects them.
#define WATERFRONT_FLOCKS 4
#define WATERFRONT_FLOCK_BIRDS 8
#define WATERFRONT_FLOCK_MAX_BIRDS 32
#define WATERFRONT_FLOCK_CUES 16
#define WATERFRONT_FLOCK_PARTS 3
#define WATERFRONT_FLOCK_TEXT 24
#define WATERFRONT_FLOCK_HALF_EXTENT 20000.0f

typedef enum {
    WATERFRONT_FLOCK_CUE_FEEDING,
    WATERFRONT_FLOCK_CUE_ALARM,
    WATERFRONT_FLOCK_CUE_KINDS
} WaterfrontFlockCueKind;

typedef enum {
    WATERFRONT_FLOCK_GULL, // The only registered bird recipe for the first slice.
    WATERFRONT_FLOCK_RECIPES
} WaterfrontFlockRecipe;

typedef struct {
    unsigned start_tick, end_tick; // Half-open [start, end), session 120 Hz ticks.
    unsigned fade_ticks;
    unsigned char kind; // WaterfrontFlockCueKind
    float amplitude; // 0..1
} WaterfrontFlockCue;

typedef struct {
    char id[WATERFRONT_FLOCK_TEXT];
    unsigned char recipe; // WaterfrontFlockRecipe
    float x, z; // Simulation metres; conversion to renderer X happens once at evaluation.
    unsigned count; // 1..WATERFRONT_FLOCK_BIRDS
    unsigned seed; // Stable per-bird phase offset.
    float phase; // Seconds added to the circling phase only; cues stay on session time.
    float radius, height, rate; // Circling radius scale, base height, signed turns/second.
    int follow_actor; // Traffic actor index whose disturbed water attracts the flock, or -1.
    bool implicit_cues; // Compiled trigonometric feeding/alarm story; false uses CUES only.
    unsigned cue_count;
    WaterfrontFlockCue cues[WATERFRONT_FLOCK_CUES];
} WaterfrontFlock;

typedef struct {
    unsigned flock_count; // 0..WATERFRONT_FLOCKS
    WaterfrontFlock flocks[WATERFRONT_FLOCKS];
} WaterfrontFlockDefinition;

typedef struct {
    char field[64];
    char message[160];
} WaterfrontFlockDiagnostic;

typedef struct {
    int count; // 0..WATERFRONT_FLOCK_MAX_BIRDS
    Transform3D birds[WATERFRONT_FLOCK_MAX_BIRDS][WATERFRONT_FLOCK_PARTS];
} WaterfrontFlockFrame;

const WaterfrontFlockDefinition *waterfront_flock_defaults(void);
bool waterfront_flock_validate(
    const WaterfrontFlockDefinition *flocks, WaterfrontFlockDiagnostic *diagnostic);
bool waterfront_flock_has_content(const WaterfrontFlockDefinition *flocks);
uint64_t waterfront_flock_digest(const WaterfrontFlockDefinition *flocks);
const char *waterfront_flock_cue_name(WaterfrontFlockCueKind kind);
const char *waterfront_flock_recipe_name(WaterfrontFlockRecipe recipe);
bool waterfront_flock_recipe_from_name(const char *name, WaterfrontFlockRecipe *recipe);

// Evaluate every flock at TIME. PLAYER and VELOCITY use renderer coordinates like the rest of
// the scene. TRAFFIC is the accepted route definition (NULL keeps compiled traffic). No hidden
// time accumulation or global random state: forward/backward seeks are identical.
void waterfront_flock_evaluate(const WaterfrontFlockDefinition *flocks,
    const WaterfrontRouteDefinition *traffic, double time, Vector3 player, Vector3 velocity,
    WaterfrontFlockFrame *frame);

#endif
