#ifndef PROTEUS_BOAT_SERVICE_H
#define PROTEUS_BOAT_SERVICE_H

// Marina M4: who owns a vessel's one hull at a simulation tick. A service binds a vessel's
// authored path to a berth. Ownership is a pure function of the tick, so seeks, restarts and
// independent sessions agree, and a vessel is never drawn or contacted twice. Core data only:
// no Lua, files or drawing. Ticks are the shared simulation ticks (BOAT_TICK_HZ).
#include "core/boat_route_path.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BOAT_SERVICE_RESIDENT, // Moored at its berth outside the path window.
    BOAT_SERVICE_VISITOR, // Absent outside the path window; moored during the dock dwell.
    BOAT_SERVICE_KINDS
} BoatServiceKind;

typedef enum {
    BOAT_OWNER_ABSENT, // No hull, contact, camera exclusion or new trail emission.
    BOAT_OWNER_MOORED, // The berth owns the hull: waves, ropes and berth contacts.
    BOAT_OWNER_TRAVELLING // The path owns the hull pose and moving contacts.
} BoatOwner;

typedef struct {
    BoatServiceKind kind;
    uint64_t start_tick; // Path tick 0 occurs at this simulation tick. One shot: no repeat.
    // Visitor: the path piece whose dwell is the stay; its end pose is the berth pose.
    // Resident: ignored; the path starts and ends at the berth pose.
    unsigned dock_piece;
} BoatServiceDefinition;

typedef struct {
    BoatOwner owner;
    uint64_t path_tick; // Path tick for TRAVELLING, or the visitor's dock dwell while MOORED.
} BoatServiceState;

// Berth pose in simulation metres; HEADING uses the path convention atan2(dx, -dz).
typedef struct {
    double x, z, heading;
} BoatServiceBerthPose;

#define BOAT_SERVICE_POSE_TOLERANCE 0.001 // Metres and radians, as path joins.

// Half-open path-tick interval [BEGIN, END) of PIECE's dwell. False for a missing piece or no
// dwell.
bool boat_service_dock_interval(
    const BoatRoutePath *path, unsigned piece, uint64_t *begin, uint64_t *end);
// Ownership at TICK. False for an invalid path or definition, leaving STATE unchanged.
// Resident: TRAVELLING in [start, start+total), MOORED otherwise. Visitor: TRAVELLING in the
// window except MOORED over the dock dwell, ABSENT outside the window. Window boundaries are
// half-open: travelling on the first tick, moored/absent on start+total.
bool boat_service_state(const BoatServiceDefinition *service, const BoatRoutePath *path,
    uint64_t tick, BoatServiceState *state);
// Pose on the path while TRAVELLING; the berth pose while MOORED. False when ABSENT or invalid.
bool boat_service_pose(const BoatServiceDefinition *service, const BoatRoutePath *path,
    uint64_t tick, BoatRoutePose *pose, double *yaw_rate);
typedef enum {
    BOAT_SERVICE_OK,
    BOAT_SERVICE_FAULT_PATH, // Not a valid open path, or an invalid service.
    BOAT_SERVICE_FAULT_DWELL, // Visitor dock piece is missing or has no dwell.
    BOAT_SERVICE_FAULT_START, // Resident path start is not the berth pose.
    BOAT_SERVICE_FAULT_END, // Resident path end is not the berth pose.
    BOAT_SERVICE_FAULT_DOCK // Visitor dock piece end is not the berth pose.
} BoatServiceFault;

// Terminal poses equal the berth pose within BOAT_SERVICE_POSE_TOLERANCE: resident start and
// end; visitor dock piece end. The path must be an open path (not a loop). On a pose fault,
// GOT (optional) receives the offending path pose so a caller can name it with the berth pose.
BoatServiceFault boat_service_validate(const BoatServiceDefinition *service,
    const BoatRoutePath *path, BoatServiceBerthPose berth, BoatRoutePose *got);
// Half-open tick interval the service reserves its berth: [0, UINT64_MAX) for a resident,
// the path window for a visitor. False for an invalid service/path.
bool boat_service_reservation(const BoatServiceDefinition *service, const BoatRoutePath *path,
    uint64_t *begin, uint64_t *end);
bool boat_service_intervals_overlap(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1);

#endif
