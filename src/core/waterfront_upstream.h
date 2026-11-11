#ifndef PROTEUS_WATERFRONT_UPSTREAM_H
#define PROTEUS_WATERFRONT_UPSTREAM_H

#include "boat_model.h"
#include "waterfront_traffic.h"

// A3.5j: the outer map's river life upstream of the river crop, in renderer X/Z (east/north):
// two Älvsnabben-style ferries on the Stenpiren - Lindholmspiren - Eriksberg loop, a smaller
// ferry between Stenpiren and Frihamnen, and a schooner that passes under Hisingsbron, whose
// lift span rises for it. Every route is a closed loop of straight lines and circular arcs,
// travelled on one shared timetable that repeats every WATERFRONT_UPSTREAM_PERIOD seconds; the
// ferries stop at their piers and pull away smoothly. Motion is a pure function of time.
// Traffic indices WATERFRONT_TRAFFIC_VESSELS and up name these vessels (waterfront_traffic.h),
// active only where BoatWater.upstream is set.
#define WATERFRONT_UPSTREAM_PERIOD 1200.0
#define WATERFRONT_UPSTREAM_SCHOONER 3 // Index among the upstream vessels.

// Hisingsbron's deck axis, from its north end to its south end (the generator's manifest
// dressing.landmarks.hisingsbron), and the lift span's stations along it.
#define WATERFRONT_HISINGSBRON_NORTH ((Vector2) { 3381.92f, 2144.23f })
#define WATERFRONT_HISINGSBRON_SOUTH ((Vector2) { 3626.84f, 1780.81f })
#define WATERFRONT_LIFT_SPAN_START 177.6f
#define WATERFRONT_LIFT_SPAN_END 267.6f
#define WATERFRONT_LIFT_SPAN_WIDTH 40.0f // Across the axis, as the baked deck.
#define WATERFRONT_LIFT_UNDERSIDE 12.0f // Above the water when down.
#define WATERFRONT_LIFT_DEPTH 1.6f
#define WATERFRONT_LIFT_HEIGHT 16.0f // Raised: 28 m of clearance.

BoatModelSpec waterfront_upstream_spec(int index);
const Mesh3D *waterfront_upstream_mesh(int index);
WaterfrontVesselMotion waterfront_upstream_motion(int index, double time);
// The stop positions (renderer X/Z) of upstream vessel INDEX in timetable order; returns the
// count (zero for the schooner) and writes at most CAPACITY of them.
int waterfront_upstream_stops(int index, Vector2 *stops, int capacity);
// Metres the lift span is raised at TIME: 0 down, WATERFRONT_LIFT_HEIGHT fully up. It starts
// rising well before the schooner reaches the bridge, holds while it passes and comes down
// after; between passages it stays down for traffic over the bridge.
float waterfront_upstream_lift(double time);
// The lift span's pose at TIME for waterfront_upstream_span_mesh: renderer coordinates.
Transform3D waterfront_upstream_span_pose(double time);
const Mesh3D *waterfront_upstream_span_mesh(void);

#endif
