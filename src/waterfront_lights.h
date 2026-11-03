#ifndef PROTEUS_WATERFRONT_LIGHTS_H
#define PROTEUS_WATERFRONT_LIGHTS_H

#include "raylib.h"

#include <stdbool.h>

// Clustered local lights. Every light submitted for a frame is bounded by its radius and
// assigned, by its X/Z footprint, to world cells of WATERFRONT_LIGHT_CELL metres. World cells wrap
// onto a WATERFRONT_LIGHT_GRID square of list slots, so the lists depend on no camera: the main
// view, the mirrored reflection and the water shade each point with the same lights, and a light
// never pops as a view moves across a cell boundary. A slot holds at most
// WATERFRONT_LIGHT_CELL_LIGHTS lights; shading tests each one's distance, so a slot shared by two
// far-apart world cells costs work, never correctness.
//
// Rank order decides what survives a limit, independent of submission order: higher
// intensity * radius first, then the lower stable ID. Lights beyond WATERFRONT_LIGHTS_MAX and
// assignments beyond a slot's capacity are dropped from the bottom of that order and counted.
#define WATERFRONT_LIGHTS_MAX 1024
#define WATERFRONT_LIGHT_CELL 8.0f
#define WATERFRONT_LIGHT_GRID 64
#define WATERFRONT_LIGHT_SLOTS (WATERFRONT_LIGHT_GRID * WATERFRONT_LIGHT_GRID)
#define WATERFRONT_LIGHT_CELL_LIGHTS 16
#define WATERFRONT_LIGHT_INDICES (WATERFRONT_LIGHT_SLOTS * WATERFRONT_LIGHT_CELL_LIGHTS)
// Upload layout, in RGBA float texels addressed row-major in a WATERFRONT_LIGHT_TEXTURE_WIDTH-wide
// texture: four texels per light (position/radius, colour/intensity, direction, cone), then one
// (offset, count) header per slot, then the index lists packed four to a texel.
#define WATERFRONT_LIGHT_TEXTURE_WIDTH 256
#define WATERFRONT_LIGHT_SLOT_BASE (WATERFRONT_LIGHTS_MAX * 4)
#define WATERFRONT_LIGHT_INDEX_BASE (WATERFRONT_LIGHT_SLOT_BASE + WATERFRONT_LIGHT_SLOTS)
#define WATERFRONT_LIGHT_TEXELS (WATERFRONT_LIGHT_INDEX_BASE + WATERFRONT_LIGHT_INDICES / 4)
#define WATERFRONT_LIGHT_TEXTURE_HEIGHT (WATERFRONT_LIGHT_TEXELS / WATERFRONT_LIGHT_TEXTURE_WIDTH)

typedef struct {
    int id; // Stable across frames and unique within one frame.
    Vector3 position;
    Vector3 direction; // Spot axis; unused when outer_cone_cos < 0.
    Color color;
    float radius; // Metres; no light beyond it.
    float intensity;
    float inner_cone_cos, outer_cone_cos; // Negative outer: omnidirectional.
} WaterfrontLight;

typedef struct {
    int submitted; // Lights offered this frame, including those dropped.
    int dropped_lights; // Beyond WATERFRONT_LIGHTS_MAX, or invalid (no radius or intensity).
    int clipped_lights; // Kept, but missing from at least one slot it reaches.
    int overflowed_slots; // Slots that refused an assignment.
    int dropped_assignments;
    int busiest_slot; // Most lights asked of one slot, before its capacity.
} WaterfrontLightStats;

typedef struct {
    WaterfrontLight lights[WATERFRONT_LIGHTS_MAX]; // Rank order after the build.
    int light_count;
    int slot_offset[WATERFRONT_LIGHT_SLOTS];
    unsigned char slot_count[WATERFRONT_LIGHT_SLOTS];
    unsigned short indices[WATERFRONT_LIGHT_INDICES];
    int index_count;
    WaterfrontLightStats stats;
    bool built;
} WaterfrontLightClusters;

void waterfront_lights_begin(WaterfrontLightClusters *clusters);
// Offers one light. Returns false when it was dropped at once (invalid, or ranked below a full
// set); a later, higher-ranked light may still displace one that was accepted.
bool waterfront_lights_add(WaterfrontLightClusters *clusters, WaterfrontLight light);
// Sorts the kept lights into rank order and fills the slot lists.
void waterfront_lights_build(WaterfrontLightClusters *clusters);
// The slot shading P reads; the shaders compute the same floor/wrap.
int waterfront_lights_slot(Vector3 position);
// True when A precedes B in rank order.
bool waterfront_lights_ranks_before(const WaterfrontLight *a, const WaterfrontLight *b);
// Writes the upload layout into TEXELS (WATERFRONT_LIGHT_TEXELS * 4 floats) and returns the rows
// that must be uploaded; later rows hold no live index.
int waterfront_lights_pack(const WaterfrontLightClusters *clusters, float *texels);

#endif
