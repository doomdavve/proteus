#ifndef PROTEUS_BOAT_VIEW_H
#define PROTEUS_BOAT_VIEW_H

#include "boat_presentation.h"
#include "boat_view_state.h"
#include "boat_water_mesh.h"
#include "renderer3d.h"
#include "waterfront_corner.h"
#include "waterfront_scene.h"

#define BOAT_CAMERA_DEFAULT ((BoatCamera) { .distance = 20, .height = 14 })

typedef struct {
    float distance; // Horizontal distance behind the boat, 8..48 world units.
    float height; // Height above the water, 4..36 world units.
    bool fixed_fov; // Disable the modest speed lens for a constant 55-degree vertical view.
} BoatCamera;

// Project a world point with the same camera as the scene. False behind the near plane.
bool boat_view_project(const BoatViewState *view, const BoatPresentation *presentation,
    const BoatWorld *world, BoatCamera settings, Vector3 point, int width, int height,
    Vector2 *screen);

// Other boats of a shared session, drawn as ordinary opaque hulls in every pass like the
// player. VIEW supplies pose and skin (its time is ignored); PRESENTATION (may be NULL)
// supplies spray, at most WATERFRONT_RIVAL_PARTICLES pieces, and cloth.
typedef struct {
    BoatViewState view;
    const BoatPresentation *presentation;
} BoatRival;

typedef struct {
    unsigned count; // At most WATERFRONT_RIVALS.
    BoatRival boats[WATERFRONT_RIVALS];
} BoatRivals;

// Views draw a VIEW (live session, replay sample or ghost) at its time in WORLD with its
// PRESENTATION (particles, cloth and camera follow). Drawing reads only; a ghost contributes
// its pose and optional cloth.

// One CPU view's map-bound resources: its displaced-water cache and the scenery of the map it
// last drew, both rebound when a draw's world has another map. Zero-initialize; never copy or
// share with another view; release with boat_cpu_view_destroy.
typedef struct {
    BoatWaterMeshCache water_mesh;
    WaterfrontScenery scenery;
} BoatCpuView;

// Safe on a zeroed or destroyed view.
void boat_cpu_view_destroy(BoatCpuView *cpu);

// FLAG may be NULL or uninitialized; the cloth then starts at rest on the hoist.
void boat_player_pose(
    const BoatViewState *view, const BoatFlag *flag, WaterfrontPlayerState *player);
void boat_scene_frame(const BoatViewState *view, const BoatPresentation *presentation,
    const BoatWorld *world, BoatCamera camera, WaterfrontSceneFrame *frame);
bool boat_draw(Renderer3D *renderer, BoatCpuView *cpu, Color *pixels, int width, int height,
    const BoatViewState *view, const BoatPresentation *presentation, const BoatWorld *world,
    BoatCamera camera_settings);
// Add RIVALS (may be NULL) to a frame built by boat_scene_frame at TIME.
void boat_scene_rivals(
    const BoatRivals *rivals, const BoatWorld *world, double time, WaterfrontSceneFrame *frame);
// The complete scene: the player, RIVALS (may be NULL) and an optional dithered GHOST.
bool boat_draw_scene(Renderer3D *renderer, BoatCpuView *cpu, Color *pixels, int width, int height,
    const BoatViewState *view, const BoatPresentation *presentation, const BoatWorld *world,
    BoatCamera camera, const BoatRivals *rivals, const BoatViewState *ghost,
    const BoatFlag *ghost_flag, float opacity);
bool boat_draw_with_ghost(Renderer3D *renderer, BoatCpuView *cpu, Color *pixels, int width,
    int height, const BoatViewState *view, const BoatPresentation *presentation,
    const BoatWorld *world, BoatCamera camera, const BoatViewState *ghost,
    const BoatFlag *ghost_flag, float opacity);

#endif
