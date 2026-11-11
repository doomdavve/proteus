#ifndef PROTEUS_WATERFRONT_WATER_HISTORY_H
#define PROTEUS_WATERFRONT_WATER_HISTORY_H

#include "raylib.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WATERFRONT_WATER_HISTORY_SIZE 512 // Texels per side.
#define WATERFRONT_WATER_HISTORY_SPACING 0.25f // Metres per texel: 128 m around the boat.
#define WATERFRONT_WATER_HISTORY_HZ 60 // Fixed substeps per simulated second.
#define WATERFRONT_WATER_HISTORY_CATCH_UP 4 // Most substeps one draw runs; older time is dropped.
#define WATERFRONT_WATER_HISTORY_PREROLL 60 // Substeps run after each reset.
#define WATERFRONT_WATER_HISTORY_JUMP 0.5 // Seconds forward that reset instead of catching up.
#define WATERFRONT_WATER_HISTORY_TELEPORT 32.0f // Metres the centre may move in one draw.
#define WATERFRONT_WATER_HISTORY_BLOCK 16 // The origin moves in blocks of this many texels.
#define WATERFRONT_WATER_HISTORY_FADE 8.0f // Metres over which shading fades out at the edge.
#define WATERFRONT_WATER_HISTORY_HULLS 8
#define WATERFRONT_WATER_HISTORY_CONTACTS 12
#define WATERFRONT_WATER_HISTORY_SOURCES 16

// Named presentation tunables. None of them feeds physics.
typedef struct {
    float foam_half_life; // Seconds for history foam to halve.
    float foam_seed_gain; // Scale on the stamped crest foam that seeds the history.
    Vector2 foam_drift; // Metres per second the foam drifts, shader world X/Z.
    float ripple_speed; // Metres per second; Courant number speed * dt / spacing stays < 0.5.
    float ripple_half_life; // Seconds for ripple energy to halve.
    float ripple_limit; // Metres; heights are clamped so injected energy stays bounded.
    float ripple_slope_gain; // Scale on the ripple slope added to the water normal.
    float bow_impulse; // Metres per substep pushed aside at each shoulder at full speed.
    float contact_impulse; // Peak metres per substep at a surface contact (gull fixture).
    float rain_rate; // Drops per square metre per second under a fully grown squall.
    float rain_impulse; // Metres one drop pushes down.
} WaterfrontWaterHistoryTuning;

WaterfrontWaterHistoryTuning waterfront_water_history_tuning(void);

// What one draw advances the history to. Positions are shader world X/Z (renderer X/Z).
typedef struct {
    double time; // Simulated seconds; a draw at the same time runs no substeps.
    uint64_t content; // Map/scene identity; a change resets the history.
    Vector2 centre; // Where the field is kept, normally the player's hull.
    int hull_count; // Slot order must stay stable across draws: it pairs hull motion.
    Vector3 hulls[WATERFRONT_WATER_HISTORY_HULLS]; // X/Z and radius of a hull: no water inside.
    int contact_count;
    Vector2 contacts[WATERFRONT_WATER_HISTORY_CONTACTS]; // Things bobbing in the surface.
    Vector4 rain; // Squall centre X/Z, radius and how grown (0..1); zero means no rain.
    const Mesh *water; // Water surface for the shore mask; NULL keeps all texels open.
    unsigned int stamp; // The G3a stamped field seeding foam, or 0.
    float stamp_rect[4]; // Its waterfront_wake_stamp_rect.
} WaterfrontWaterHistoryInput;

// Cosmetic, view-owned foam history and ripples for water shading: two RGBA16F ping-pong states
// (R foam, G ripple height, B the height one substep earlier) and an RGBA8 shore mask, advanced on
// fixed 60 Hz substeps of simulated time. Pausing (an unchanged time) freezes it, and the same
// sequence of inputs reproduces the same state. Nothing is read back while drawing.
typedef struct {
    Shader shader;
    int locations[20];
    Material material; // Draws the water meshes into the mask.
    RenderTexture2D states[2], mask;
    int current; // The state holding the latest substep.
    bool supported; // Float targets exist; otherwise water shading has no history.
    bool valid; // A reset has run since the last change of size or support.
    WaterfrontWaterHistoryTuning tuning;
    long long step; // Global index of the latest substep.
    int origin_x, origin_z; // Lower corner in whole texels of shader world X/Z.
    int mask_x, mask_z; // The origin the mask was drawn for.
    uint64_t content;
    Vector2 centre;
    int hull_count;
    Vector2 previous_hulls[WATERFRONT_WATER_HISTORY_HULLS];
    double previous_time;
    // Review counters.
    unsigned long long substeps, resets, masks;
    int last_substeps; // Substeps the latest advance ran.
} WaterfrontWaterHistory;

// Load the history shader from SHADER_DIRECTORY. FLOAT_TARGETS says whether RGBA16F targets
// render correctly (the G3a probe); without them the history stays unsupported. False means the
// shader failed to load.
bool waterfront_water_history_init(
    WaterfrontWaterHistory *history, const char *shader_directory, bool float_targets);
void waterfront_water_history_destroy(WaterfrontWaterHistory *history);
// Forget the state so the next advance resets and pre-rolls.
void waterfront_water_history_invalidate(WaterfrontWaterHistory *history);
// Run the substeps between the latest one and INPUT's time: reset and pre-roll on the first
// call, a rewind, a large jump, a teleport or a content change, else at most CATCH_UP. Returns
// the substeps run. Changes render targets, blending and matrices; the caller restores its own.
int waterfront_water_history_advance(
    WaterfrontWaterHistory *history, const WaterfrontWaterHistoryInput *input);
// For the water shader: origin X/Z, 1/extent and the edge fade in metres.
void waterfront_water_history_rect(const WaterfrontWaterHistory *history, float rect[4]);
Texture2D waterfront_water_history_texture(const WaterfrontWaterHistory *history);
size_t waterfront_water_history_bytes(const WaterfrontWaterHistory *history);

#endif
