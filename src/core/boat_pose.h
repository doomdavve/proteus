#ifndef PROTEUS_BOAT_POSE_H
#define PROTEUS_BOAT_POSE_H

#include "boat.h"
#include "geometry3d.h"

// Outboard lever arm behind the hull centre, shared by thrust, stern sampling and wash.
#define BOAT_ENGINE_ARM 1.6f

// Pure hull pose math shared by physics, cosmetics and drawing. Renderer space mirrors
// simulation X: a water-plane point (x, z) is (-x, height, z). No state and no draw calls.

// Derivatives of a renderer-space attachment with respect to the three physical angles.
// Dotting force with each derivative gives its generalized torque, including the mirrored X.
typedef struct {
    Vector3 pitch, roll, yaw, velocity;
} BoatPoseMotion;

// Rendered player model parts at the boat's pose.
typedef struct {
    Transform3D hull, cabin, engine;
} BoatPoseParts;

// Local roll, then pitch, then heading.
Matrix boat_pose_rotation(const Boat *boat);
// Renderer XYZ Euler angles for a rotation matrix.
Vector3 boat_pose_euler(Matrix rotation);
// Renderer-space position of a hull-local point.
Vector3 boat_pose_attachment(const Boat *boat, Vector3 local);
BoatPoseMotion boat_pose_motion(const Boat *boat, Vector3 local);
// World-up component of the hull's up axis; negative when inverted.
float boat_pose_up(const Boat *boat);
// Water-plane direction of outboard thrust.
Vector2 boat_pose_thrust_direction(const Boat *boat);
// Renderer-space linear velocity of the hull centre.
Vector3 boat_pose_velocity(const Boat *boat);
// Unit-scale hull frame; the flag hoist attaches to it.
Transform3D boat_pose_hull(const Boat *boat);
BoatPoseParts boat_pose_parts(const Boat *boat);

#endif
