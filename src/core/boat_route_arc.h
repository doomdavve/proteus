#ifndef PROTEUS_BOAT_ROUTE_ARC_H
#define PROTEUS_BOAT_ROUTE_ARC_H

#include "core/boat_route_leg.h"

#include <stdbool.h>
#include <stdint.h>

// Marina M3 motion groundwork: one circular open route arc evaluated directly at an explicit
// simulation tick. This primitive extends the straight leg with a turn but is not connected to
// the route system. It provides no collision, clearance or docking proof, and it is not a
// complete route or scene proof. Callers own those concerns.
//
// Coordinates are simulation metres. The arc is a circle of RADIUS around (CENTRE_X, CENTRE_Z),
// starting at START_ANGLE and sweeping SWEEP radians (signed; positive is counterclockwise in
// x/z). HEADING uses atan2(dx/ds, -dz/ds) with the sweep direction, matching the simulation's
// convention, and SPEED is metres per second along the arc. Positions come from the tick alone,
// so seeks are deterministic and no floating-point frame accumulation is involved.
#define BOAT_ROUTE_ARC_COORDINATE_LIMIT 20000.0

typedef struct BoatRouteArc {
    double centre_x, centre_z;
    double radius;
    double start_angle; // Radians, within [-2*pi, 2*pi].
    double sweep;
    uint64_t duration_ticks;
    double tick_seconds;
    double max_speed;
    double max_acceleration;
} BoatRouteArc;

// True when ARC describes a valid circular turn: non-null, finite centre and start angle,
// radius > 0, finite nonzero sweep with |sweep| <= pi, positive duration_ticks, finite positive
// tick_seconds, max_speed and max_acceleration, and the whole circle's conservative
// centre +/- radius bounds inside +/-BOAT_ROUTE_ARC_COORDINATE_LIMIT. The arc length is
// radius * |sweep|; the smoothstep blend peaks at 1.5 * arc_length / duration and its total
// acceleration stays within 6 * arc_length / duration^2 + peak_speed^2 / radius. Those bounds
// must stay within the limits, and every intermediate must be finite. No state is modified.
bool boat_route_arc_validate(const BoatRouteArc *arc);

// Evaluate ARC at TICK and write the pose only on success. The normalized progress is
// u = clamp(TICK / duration_ticks, 0, 1); the angle is start_angle + sweep * (3u^2 - 2u^3), the
// position is centre + radius * (cos(theta), sin(theta)), and the speed is
// radius * |sweep| * (6u - 6u^2) / duration_seconds. The tangent heading uses the sweep sign,
// including at the zero-speed endpoints. At or after duration_ticks the endpoint is returned with
// zero speed. Returns false for any invalid arc, a null POSE, or a nonfinite intermediate,
// leaving POSE unchanged.
bool boat_route_arc_evaluate(const BoatRouteArc *arc, uint64_t tick, BoatRoutePose *pose);

#endif
