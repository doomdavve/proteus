#ifndef PROTEUS_WATERFRONT_ROUTE_H
#define PROTEUS_WATERFRONT_ROUTE_H

#include "boat_model.h"
#include "waterfront_traffic.h"

#include <stdbool.h>
#include <stdint.h>

// L3.1: caller-owned travelling-craft routes and timetables. Core data only: no Lua, host,
// renderer or file access. Simulation x/z metres; renderer X/Z follows the traffic module's
// existing conversion. NULL always selects the compiled defaults below.
#define WATERFRONT_ROUTE_ACTORS 8
#define WATERFRONT_ROUTE_POINT_CAPACITY 32
#define WATERFRONT_ROUTE_STOPS 8
#define WATERFRONT_ROUTE_TEXT 24
#define WATERFRONT_ROUTE_HALF_EXTENT 20000.0f
#define WATERFRONT_ROUTE_MIN_PERIOD 5.0
#define WATERFRONT_ROUTE_MAX_PERIOD 86400.0

typedef enum {
    WATERFRONT_ROUTE_DOCK_CIRCUIT, // Two straight lanes joined by 14 m semicircles.
    WATERFRONT_ROUTE_ELLIPSE, // Nested analytic river circuit.
    WATERFRONT_ROUTE_POINTS, // Closed loop of straight pieces and circular corners.
    WATERFRONT_ROUTE_SHAPES
} WaterfrontRouteShape;

typedef enum {
    WATERFRONT_CRAFT_RUNABOUT,
    WATERFRONT_CRAFT_CRUISER,
    WATERFRONT_CRAFT_SAILBOAT,
    WATERFRONT_CRAFT_FERRY,
    WATERFRONT_CRAFT_ALVSNABBEN,
    WATERFRONT_CRAFT_FRIHAMNEN,
    WATERFRONT_CRAFT_SCHOONER,
    WATERFRONT_CRAFT_WORKBOAT,
    WATERFRONT_CRAFT_RECIPES
} WaterfrontCraftRecipe;

typedef struct {
    float x, z, radius;
} WaterfrontRoutePoint;

typedef struct {
    unsigned char segment; // Index of the straight piece after corner SEGMENT.
    float dwell; // Seconds stopped; negative distributes the period's remainder.
} WaterfrontRouteStop;

typedef struct {
    char id[WATERFRONT_ROUTE_TEXT];
    unsigned char shape; // WaterfrontRouteShape
    unsigned char recipe; // WaterfrontCraftRecipe
    unsigned char retain; // Compiled actor index, or 0xff for an authored/independent craft.
    bool bridge; // Tall vessel whose crossings schedule the Hisingsbron lift.
    float speed, accel, phase; // m/s, seconds to cruise, timetable phase seconds.
    double period; // Positive repeat period in seconds.
    // WATERFRONT_ROUTE_POINTS: closed corner list, then stops on line pieces.
    unsigned point_count, stop_count;
    WaterfrontRoutePoint points[WATERFRONT_ROUTE_POINT_CAPACITY];
    WaterfrontRouteStop stops[WATERFRONT_ROUTE_STOPS];
    // WATERFRONT_ROUTE_ELLIPSE: centre, radii and shared lane vector.
    float origin_x, origin_z, long_radius, short_radius;
    // WATERFRONT_ROUTE_DOCK_CIRCUIT: straight-lane speed, start distance and turn radius.
    float start_distance, turn_radius;
} WaterfrontRouteActor;

typedef struct WaterfrontRouteDefinition {
    unsigned actor_count; // 0..WATERFRONT_ROUTE_ACTORS
    WaterfrontRouteActor actors[WATERFRONT_ROUTE_ACTORS];
} WaterfrontRouteDefinition;

typedef struct {
    char field[64];
    char message[160];
} WaterfrontRouteDiagnostic;

// The compiled actors: four river craft and four outer upstream vessels on a 1,200 s timetable.
const WaterfrontRouteDefinition *waterfront_route_defaults(void);
// Structural checks: shape/recipe enums, counts, IDs, finite numbers, loop continuity,
// period/stop arithmetic and bridge actor references. Map clearance belongs to the plugin host.
bool waterfront_route_validate(
    const WaterfrontRouteDefinition *routes, WaterfrontRouteDiagnostic *diagnostic);
bool waterfront_route_has_content(const WaterfrontRouteDefinition *routes);
bool waterfront_route_has_bridge(const WaterfrontRouteDefinition *routes);
uint64_t waterfront_route_digest(const WaterfrontRouteDefinition *routes);

const char *waterfront_route_shape_name(WaterfrontRouteShape shape);
const char *waterfront_route_recipe_name(WaterfrontCraftRecipe recipe);
bool waterfront_route_recipe_from_name(const char *name, WaterfrontCraftRecipe *recipe);
// L3.2: registered route presets. The returned actor owns a ready timetable; callers replace
// its ID and may override recipe/speed/phase/period. "marina_pass" is a small craft loop south
// of the authored marina; the remaining IDs name the compiled actors.
bool waterfront_route_preset(const char *id, WaterfrontRouteActor *actor);
const char *waterfront_route_preset_name(unsigned index);
unsigned waterfront_route_preset_count(void);
BoatModelSpec waterfront_route_spec(const WaterfrontRouteDefinition *routes, int index);
const Mesh3D *waterfront_route_mesh(const WaterfrontRouteDefinition *routes, int index);
// Stable generated mesh for a registered craft recipe, shared by authored traffic slots.
const Mesh3D *waterfront_route_recipe_mesh(WaterfrontCraftRecipe recipe);
double waterfront_route_period(const WaterfrontRouteDefinition *routes, int index);
// Closed authored routes; negative time supports trails across the start and lap boundary.
WaterfrontVesselMotion waterfront_route_motion(
    const WaterfrontRouteDefinition *routes, int index, double time);
int waterfront_route_stops(const WaterfrontRouteDefinition *routes, int index, Vector2 *stops,
    int capacity);
// Highest lift the bridge needs for this definition at TIME, 0 when it stays down.
float waterfront_route_lift(const WaterfrontRouteDefinition *routes, double time);
// Schedule diagnostics used by L3.4. A bridge actor's hull must cross the span fully inside
// one raised window, and the definition must leave enough time for the lift to cycle.
bool waterfront_route_bridge_window(const WaterfrontRouteDefinition *routes, int index,
    double *entry, double *leave, WaterfrontRouteDiagnostic *diagnostic);

#endif
