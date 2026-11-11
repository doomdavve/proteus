#ifndef PROTEUS_BOAT_CARGO_VIEW_H
#define PROTEUS_BOAT_CARGO_VIEW_H

#include "boat_cargo.h"
#include "boat_model.h"
#include "boat_water.h"
#include "boat_world.h"
#include "renderer3d.h"
#include "waterfront.h"
#include "waterfront_lighting.h"

// Append a vertical landing decal clipped to actual receiver triangles. Start OUT with
// boat_model_begin; do not pass OUT as RECEIVERS. Geometry is caller-owned and bounded.
// REST_WATER optionally displaces only intersecting rest-water triangles with the same
// vertex samples/fade as the GPU near patch. NULL uses receiver vertices exactly (CPU/solids).
// FOCUS is renderer X/Z; fading is irrelevant when REST_WATER is NULL.
// MASK is a diagnostic colour; alpha zero selects darkened receiver albedo instead.
bool boat_cargo_decal(BoatModel *out, const Mesh3D *receivers, Vector3 cargo,
    const BoatWater *rest_water, double now, Vector2 focus, Color mask);

// What the cargo view reads of a work session; borrowed for one call.
typedef struct {
    const BoatCargoSandbox *sandbox;
    const Boat *boat;
    const BoatWorld *world;
    uint64_t tick;
    bool crane_view; // false: the camera follows the boat (helm).
} BoatCargoView;

// One evaluated mesh for both renderers (sites, boat boxes, crate, straps, berth outlines),
// relative to the first site's berth and returned with the origin added back. The result is the
// number of leading triangles that receive the landing decal (everything before the crate).
size_t boat_cargo_view_scene(const BoatCargoView *v, BoatModel *m);
// The crane or helm camera; mapped locations pull the eye in until the line of sight is clear.
View3D boat_cargo_view_camera(const BoatCargoView *v, const WaterfrontScenery *scenery);
// A small finite water patch around the boat for the non-GPU harness views.
void boat_cargo_view_water(const BoatCargoView *v, BoatModel *m);

// LIGHT with the work solids' and landing decal's ambient floor applied: cpu_ambient becomes at
// least .8 * clamp(window_light) while every other field, including cpu_tint and the sun, is
// unchanged. The floor follows shared occupied/night intensity, so day (window_light 0) is
// bit-identical and night keeps its tint. Returned by value; the argument is not modified.
WaterfrontLighting boat_cargo_view_work_lighting(WaterfrontLighting light);

// GPU upload of a BoatModel-sized Mesh3D per call (flat-shaded, vertex colours, raylib's default
// material), for the harness and the game's cargo props. Zero-initialize; init needs a window.
typedef struct {
    Mesh mesh;
    Material material;
} BoatCargoViewGpu;

bool boat_cargo_view_gpu_init(BoatCargoViewGpu *g);
// Draw M inside BeginMode3D with a fixed key light (LIT) or flat colours (not LIT). This is the
// standalone cargo harness's legacy look and intentionally does not follow the daylight frame, so
// its regression fixtures keep their original shading and truncated colour bytes. Game scenes that
// should share the authored frame light use boat_cargo_view_gpu_draw_lighting instead.
void boat_cargo_view_gpu_draw(BoatCargoViewGpu *g, const Mesh3D *m, bool lit);
// Draw M inside BeginMode3D using LIGHTING's CPU flat-material frame policy: each triangle's unit
// normal is lit by cpu_ambient + cpu_direct * max(0, N.sun_direction), tinted per channel by
// cpu_tint and clamped to the byte range. This gives the workboat, crane and cargo props the same
// authored light as the CPU frame. LIGHTING is borrowed for the call.
void boat_cargo_view_gpu_draw_lighting(
    BoatCargoViewGpu *g, const Mesh3D *m, const WaterfrontLighting *lighting);
void boat_cargo_view_gpu_destroy(BoatCargoViewGpu *g);

#endif
