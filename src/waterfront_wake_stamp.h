#ifndef PROTEUS_WATERFRONT_WAKE_STAMP_H
#define PROTEUS_WATERFRONT_WAKE_STAMP_H

#include "raylib.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WATERFRONT_WAKE_STAMP_SIZE 1024 // Texels per side.
#define WATERFRONT_WAKE_STAMP_SPACING 0.25f // Metres per texel.
#define WATERFRONT_WAKE_STAMP_PIECES 256 // Both object pools, player and traffic.

typedef struct {
    Vector2 corners[4]; // Shader world X/Z.
    int piece;
} WaterfrontWakeStampQuad;

// A boat-following, cosmetic sampled copy of the object wake pieces for water shading: RGBA16F
// height, world X/Z slope and crest foam, rebuilt from the current records by additive quads
// each time it is built. It never feeds physics, and nothing is read back while drawing.
// Fragments outside the field evaluate the pieces reaching beyond it analytically, so distant
// wakes keep their exact shading. The origin snaps to whole texels, so texels stay
// world-anchored while the field follows the boat.
typedef struct {
    RenderTexture2D target; // Colour is the field; no depth attachment.
    Shader shader; // Stamps pieces; a probe mode checks float rendering and blending.
    int locations[7];
    int size;
    float spacing;
    int origin_x, origin_z; // Lower corner in whole texels of shader world X/Z.
    bool blending; // The probe added signed values in a float target correctly.
    bool supported; // A field exists; otherwise water shading keeps its analytic loop.
    bool valid; // The field was built at least once.
    uint64_t placed_key, built_key; // Records, origin and lives; equal keys skip a rebuild.
    unsigned long long builds; // Review: how many times the field was stamped.
    WaterfrontWakeStampQuad quads[2 * WATERFRONT_WAKE_STAMP_PIECES];
    int quad_count;
    int pieces, outside; // Placed pieces and those reaching outside the sampled area.
    size_t texels; // Texels covered by the placed quads, a measure of build work.
} WaterfrontWakeStamp;

// Load the stamp shader from SHADER_DIRECTORY and probe float render targets and blending.
// A failed probe still returns true with supported false; false means a shader load failed.
bool waterfront_wake_stamp_init(
    WaterfrontWakeStamp *stamp, const char *shader_directory, int size, float spacing);
void waterfront_wake_stamp_destroy(WaterfrontWakeStamp *stamp);
// Replace the field with one of SIZE texels per side at SPACING metres. False leaves none.
bool waterfront_wake_stamp_resize(WaterfrontWakeStamp *stamp, int size, float spacing);
// Place the field around CENTRE (shader world X/Z) for COUNT packed object records: the origins
// and parameters of boat_wake_objects_pack, the first PLAYERS from the player pool. Lists each
// piece's support as quads and writes the indices of pieces reaching outside the sampled area to
// the first components of OUTSIDE's vec4s, a texture row, returning how many. Placing the records
// of the built field again keeps it.
int waterfront_wake_stamp_place(WaterfrontWakeStamp *stamp, Vector2 centre, const float *origins,
    const float *parameters, int count, int players, float life, float traffic_life,
    float *outside);
// Stamp the placed quads, reading the records from RECORDS (uploaded after placing). Changes
// the render target, blending and matrices; the caller saves and restores its target.
void waterfront_wake_stamp_build(
    WaterfrontWakeStamp *stamp, Texture2D records, int players, float life, float traffic_life);
// The sampled area for the water shader: origin X/Z, 1/extent and the half-texel guard in UV.
void waterfront_wake_stamp_rect(const WaterfrontWakeStamp *stamp, float rect[4]);
size_t waterfront_wake_stamp_bytes(const WaterfrontWakeStamp *stamp);

#endif
