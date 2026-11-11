#include "boat_pose.h"
#include "raymath.h"

#include <math.h>

Matrix boat_pose_rotation(const Boat *boat)
{
    return MatrixMultiply(MatrixMultiply(MatrixRotateZ(boat->roll), MatrixRotateX(boat->pitch)),
        MatrixRotateY(boat->heading));
}

// Raymath's XYZ Euler order is not local pitch followed by heading. Compose the attachment
// in hull space first, then convert to the existing renderer transform convention.
Vector3 boat_pose_euler(Matrix rotation)
{
    // atan2 retains precision near +/-pi/2 where asin rounds the sine to one.
    float cy = hypotf(rotation.m0, rotation.m4);
    float y = atan2f(rotation.m8, cy);
    if (cy < .000001f)
        return (Vector3) { atan2f(rotation.m6, rotation.m5), y, 0 };
    return (Vector3) { atan2f(-rotation.m9, rotation.m10), y, atan2f(-rotation.m4, rotation.m0) };
}

Vector3 boat_pose_attachment(const Boat *boat, Vector3 local)
{
    return Vector3Add((Vector3) { -boat->position.x, boat->height, boat->position.y },
        Vector3Transform(local, boat_pose_rotation(boat)));
}

BoatPoseMotion boat_pose_motion(const Boat *boat, Vector3 local)
{
    Matrix rotation = boat_pose_rotation(boat);
    Vector3 arm = Vector3Transform(local, rotation);
    Vector3 pitch_axis = { cosf(boat->heading), 0, -sinf(boat->heading) };
    Vector3 roll_axis = Vector3Transform((Vector3) { 0, 0, 1 },
        MatrixMultiply(MatrixRotateX(boat->pitch), MatrixRotateY(boat->heading)));
    BoatPoseMotion motion = { .pitch = Vector3CrossProduct(pitch_axis, arm),
        .roll = Vector3CrossProduct(roll_axis, arm),
        .yaw = Vector3CrossProduct((Vector3) { 0, 1, 0 }, arm),
        .velocity = { -boat->velocity.x, boat->vertical_velocity, boat->velocity.y } };
    motion.velocity = Vector3Add(motion.velocity, Vector3Scale(motion.pitch, boat->pitch_velocity));
    motion.velocity = Vector3Add(motion.velocity, Vector3Scale(motion.roll, boat->roll_velocity));
    motion.velocity = Vector3Add(motion.velocity, Vector3Scale(motion.yaw, boat->yaw_velocity));
    return motion;
}

float boat_pose_up(const Boat *boat)
{
    return Vector3Transform((Vector3) { 0, 1, 0 }, boat_pose_rotation(boat)).y;
}

Vector2 boat_pose_thrust_direction(const Boat *boat)
{
    float angle = boat->heading - boat->engine_angle;
    return (Vector2) { sinf(angle), -cosf(angle) };
}

Vector3 boat_pose_velocity(const Boat *boat)
{
    return (Vector3) { -boat->velocity.x, boat->vertical_velocity, boat->velocity.y };
}

Transform3D boat_pose_hull(const Boat *boat)
{
    return (Transform3D) { .position = { -boat->position.x, boat->height, boat->position.y },
        .rotation = boat_pose_euler(boat_pose_rotation(boat)),
        .scale = { 1, 1, 1 } };
}

BoatPoseParts boat_pose_parts(const Boat *boat)
{
    BoatPoseParts parts;
    Matrix rotation = boat_pose_rotation(boat);
    parts.hull = (Transform3D) { .position = { -boat->position.x, boat->height, boat->position.y },
        .rotation = boat_pose_euler(rotation),
        .scale = { 1.4f, 1.4f, 1.4f } };
    parts.cabin = (Transform3D) { .position = boat_pose_attachment(boat, (Vector3) { 0, 1.1f, 0 }),
        .rotation = parts.hull.rotation,
        .scale = { 1, .7f, 1 } };
    parts.engine = (Transform3D) { .position
        = boat_pose_attachment(boat, (Vector3) { 0, .4f, BOAT_ENGINE_ARM }),
        .rotation = boat_pose_euler(MatrixMultiply(MatrixRotateY(-boat->engine_angle), rotation)),
        .scale = { .55f, .65f, .9f } };
    return parts;
}
