#ifndef PROTEUS_BOAT_ROUTE_PATH_H
#define PROTEUS_BOAT_ROUTE_PATH_H

#include "core/boat_route_arc.h"
#include "core/boat_route_leg.h"

#define BOAT_ROUTE_PATH_PIECES 4

typedef enum { BOAT_ROUTE_PATH_LINE, BOAT_ROUTE_PATH_ARC } BoatRoutePathKind;

typedef struct {
    BoatRoutePathKind kind;
    union {
        BoatRouteLeg line;
        BoatRouteArc arc;
    } shape;
    uint64_t dwell_ticks;
} BoatRoutePathPiece;

typedef struct {
    unsigned count;
    BoatRoutePathPiece pieces[BOAT_ROUTE_PATH_PIECES];
} BoatRoutePath;

// Pieces stop at their endpoints. Adjacent endpoints and headings must agree within 1 mm and
// 0.001 radians. All pieces share the simulation tick. Total includes the final dwell.
bool boat_route_path_validate(const BoatRoutePath *path, uint64_t *total_ticks);
bool boat_route_path_evaluate(
    const BoatRoutePath *path, uint64_t tick, BoatRoutePose *pose, double *yaw_rate);

#endif
