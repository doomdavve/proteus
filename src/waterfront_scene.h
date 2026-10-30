#ifndef PROTEUS_WATERFRONT_SCENE_H
#define PROTEUS_WATERFRONT_SCENE_H

#include "boat_flag.h"
#include "boat_skin.h"
#include "boat_water.h"
#include "renderer3d.h"
#include "waterfront_city.h"
#include "waterfront_flock.h"
#include "waterfront_harbor.h"
#include "waterfront_traffic.h"

// Renderer-independent authored data and time evaluation for the waterfront.
// The returned mesh refers to scene-owned, static generated geometry.
#define WATERFRONT_GULL_PARTS 3
#define WATERFRONT_HARBOR_GULLS 32
#define WATERFRONT_CAR_COUNT 20
#define WATERFRONT_MAX_LAMPS 32
#define WATERFRONT_PLAYER_PARTICLES 384

typedef struct {
    Transform3D transform;
    Color color;
    bool spray;
} WaterfrontPlayerParticle;

typedef struct {
    Transform3D hull, cabin, engine;
    unsigned int skin; // Cosmetic preset, shared across all render passes.
    BoatFlagSurface flag;
    int particle_count;
    WaterfrontPlayerParticle particles[WATERFRONT_PLAYER_PARTICLES];
} WaterfrontPlayerState;

// Other boats in a shared session: ordinary opaque hulls drawn in every pass like the player,
// each with its own skin, flag and a smaller spray budget. Cosmetic only.
#define WATERFRONT_RIVALS 3
#define WATERFRONT_RIVAL_PARTICLES 96

typedef struct {
    Transform3D hull, cabin, engine;
    unsigned int skin;
    BoatFlagSurface flag;
    int particle_count;
    WaterfrontPlayerParticle particles[WATERFRONT_RIVAL_PARTICLES];
} WaterfrontRivalState;

typedef struct {
    int id; // Stable first-headlight ID; the second headlight is id + 1.
    int street;
    Vector3 position, forward, right;
    Transform3D body, cabin;
    Color color;
    Vector3 lenses[2][2]; // rear/front, then left/right; shared with light selection
    Vector3 headlight_direction;
    Vector3 pool_position;
} WaterfrontCarState;

typedef struct {
    int id;
    int street;
    Vector3 position, pool_direction;
    Transform3D pole;
    Vector3 lens;
} WaterfrontLampState;

// A3.5: a weather patch painted into the sky behind all geometry: a cloud column of RADIUS
// metres over CENTRE (renderer X/Z) from BASE up to lumpy tops at most TOP, and rain beneath
// it, both scaled by GROWN (0..1; zero draws nothing). Shared by the CPU sky and the GPU sky
// shader, which must stay in step with waterfront_squall_sky.
typedef struct {
    Vector2 centre;
    float radius, base, top, grown;
} WaterfrontSquall;

// Owned snapshot: no simulation state or pointers into mutable actor storage.
// Evaluate once and share it across colour, reflection, shadow, and lighting passes.
typedef struct {
    View3D camera;
    bool player_active;
    bool boating_basin;
    int scenery_count;
    struct {
        Transform3D transform;
        Color color;
    } scenery[704];
    BoatWater player_water; // Shared analytic boating parameters.
    WaterfrontPlayerState player;
    int rival_count;
    WaterfrontRivalState rivals[WATERFRONT_RIVALS];
    float ghost_opacity;
    unsigned int ghost_skin;
    Transform3D ghost_hull, ghost_cabin, ghost_engine;
    BoatFlagSurface ghost_flag;
    double time; // World seconds; GPU uploads only reduced float phases and bounded clocks.
    WaterfrontSquall squall; // Drawn in the sky pass only.
    Transform3D bridge;
    Color bridge_color;
    WaterfrontCarState cars[WATERFRONT_CAR_COUNT];
    WaterfrontLampState lamps[WATERFRONT_MAX_LAMPS];
    int lamp_count;
    WaterfrontHarborFrame harbor;
    WaterfrontTrafficFrame traffic;
    int gull_count;
    Transform3D gulls[WATERFRONT_HARBOR_GULLS][WATERFRONT_GULL_PARTS];
} WaterfrontSceneFrame;

typedef struct {
    const Vector3 *points;
    int point_count;
} WaterfrontStreet;

// Reevaluate scene-owned actors at explicit time/water, preserving the caller's camera/player.
void waterfront_scene_evaluate_actors(double time, BoatWater water, WaterfrontSceneFrame *frame);
// Boat-study flocks: explicit-time social cues, feeding passes, and vessel avoidance.
// Player position/velocity use renderer coordinates; no accumulated simulation state.
void waterfront_boating_gulls(
    double time, Vector3 player, Vector3 velocity, WaterfrontSceneFrame *frame);
// L4.1: use the accepted traffic definition for avoidance while the compiled flocks remain.
void waterfront_boating_gulls_scene(const BoatSceneDefinition *scene, double time, Vector3 player,
    Vector3 velocity, WaterfrontSceneFrame *frame);
const Mesh3D *waterfront_gull_mesh(int part);
const Mesh3D *waterfront_player_hull_mesh(void);
const Mesh3D *waterfront_player_cabin_mesh(void);
const Mesh3D *waterfront_player_engine_mesh(void);

// Static scenery and actors at an explicit time; the caller supplies the camera.
void waterfront_scene_evaluate(double time, BoatWater water, WaterfrontSceneFrame *frame);
// The same with the river kit's street cars, lamps and harbour set on GROUND, the land height
// under renderer X/Z, instead of the river's flat 3.5 m ground; NULL keeps that.
void waterfront_scene_evaluate_ground(
    double time, BoatWater water, float (*ground)(float x, float z), WaterfrontSceneFrame *frame);
// L2.2: fill the shared frame from the accepted scene. NULL keeps the compiled marina.
void waterfront_scene_evaluate_scene(
    const BoatSceneDefinition *scene, double time, BoatWater water, WaterfrontSceneFrame *frame);
void waterfront_scene_evaluate_ground_scene(const BoatSceneDefinition *scene, double time,
    BoatWater water, float (*ground)(float x, float z), WaterfrontSceneFrame *frame);
WaterfrontStreet waterfront_scene_street(int index);
Vector3 waterfront_scene_street_position(
    WaterfrontStreet street, float distance, Vector3 *direction);

// Borrowed static panorama, shared by CPU sky rays and the GPU sky shader. Do not unload.
// U is atan2(direction.x, direction.z) / (2*pi) + 0.5; V is (1-direction.y) / 2.
Vector3 waterfront_sun_direction(void);
Image waterfront_sky_image(void);
void waterfront_sky_draw(Color *pixels, int width, int height, View3D camera);
// The sky with SQUALL over it, seen from the camera.
void waterfront_sky_draw_squall(
    Color *pixels, int width, int height, View3D camera, WaterfrontSquall squall);
// SKY along the unit RAY from EYE, behind SQUALL's cloud and rain.
Color waterfront_squall_sky(Color sky, Vector3 eye, Vector3 ray, WaterfrontSquall squall);

#endif
