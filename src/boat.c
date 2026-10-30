#include "boat.h"
#include "boat_map.h"
#include "boat_model.h"
#include "boat_pose.h"
#include "boat_steering.h"
#include "raymath.h"
#include "waterfront_harbor.h"
#include "waterfront_traffic.h"

#include <assert.h>
#include <math.h>

// Normalized mass and world units: these are handling parameters, not a calibrated hull.
static const float MAX_ENGINE_ANGLE = 0.55f;
static const float ENGINE_RESPONSE_RATE = 3.5f;
// Overdamped spring return when steering is released; larger values centre faster (1/seconds).
static const float WHEEL_RETURN_RATE = 3.0f;
static const float YAW_INERTIA = 2.2f;
static const float HULL_DRAG_LINEAR = 0.22f;
static const float HULL_DRAG_QUADRATIC = 0.025f;
// Planing is a deliberately simple horizontal handling approximation.  The separate thresholds
// prevent a noisy speed trace from repeatedly changing modes; the state itself is rate-limited.
static const float PLANING_ENTER_SPEED = 6.5f;
static const float PLANING_EXIT_SPEED = 5.0f;
static const float PLANING_RISE_RATE = 0.85f;
static const float PLANING_FALL_RATE = 3.0f;
static const float PLANING_DRAG_MULTIPLIER = 0.62f;
// Let the bow hold a water-relative arc while stern thrust initiates the turn.
// Equal station scaling avoids adding a forward force-centre/yaw feedback term.
static const float PLANING_LATERAL_GRIP_MULTIPLIER = 3.0f;
// Planing redistributes the support reference; it does not add a second cosmetic rotation.
static const float TRIM_TRANSITION_PITCH = 8 * DEG2RAD;
static const float TRIM_CRUISE_PITCH = 3 * DEG2RAD;
// Normalized mass=1. Six unilateral supports include weight preload, spring and local damping.
static const float GRAVITY = 9.81f;
static const float BUOYANCY_STIFFNESS = 64.0f;
static const float BUOYANCY_DAMPING = 13.0f;
static const float PITCH_INERTIA = 2.0f;
static const float ROLL_INERTIA = .45f;
static const float PLANING_HEIGHT = .05f;
// Modest encounter damping: at cruise the local normal resistance grows by at most 35%.
static const float WAVE_DAMPING_GAIN = .35f;
static const float WAVE_CRUISE_SPEED = 12.0f;
static const float WAVE_SPRAY_ENTRY_SPEED = 1.2f;
static const float WAVE_SPRAY_RESPONSE = 8.0f;
static const float SKEG_RESISTANCE = 0.10f;
// One-way lateral-load coupling into the existing physical roll integration.
static const float TURN_ROLL_LOW_GAIN = .12f;
// Keep ordinary turns below sustained saturation so fading load can unwind the bank.
static const float TURN_ROLL_PLANING_GAIN = .28f;
static const float TURN_ROLL_SPEED_START = 1;
static const float TURN_ROLL_SPEED_FULL = 6;
static const float TURN_ROLL_RESPONSE_SECONDS = .06f;
static const float TURN_ROLL_SCALE = 12 * DEG2RAD;
static const float ROLL_GUARD_START = 25 * DEG2RAD;
static const float ROLL_GUARD_BLEND = 5 * DEG2RAD;
static const float ROLL_GUARD_STIFFNESS = 180;
static const float ROLL_GUARD_DAMPING = 12;
// Arcade tuning: resist uncommanded wake tumbles and recover toward an upright landing.
static const float ARCADE_AIR_ROLL_RESPONSE = 6;
static const float ARCADE_AIR_ROLL_RECOVERY = 12;
static const float ARCADE_WET_ROLL_RECOVERY = 14;
static const float ARCADE_WET_ROLL_DAMPING = 5;
// Landing weight: while a hull lands, supports plunging into the water faster than the free
// entry speed scrub water-relative horizontal speed by the squared excess, so hard and
// bow-first landings cost more while small hops and wave bobbing stay free.
static const float LANDING_SCRUB = .14f;
static const float LANDING_FREE_ENTRY = 2.5f;
// Contact samples form an intentionally simple, forgiving hull footprint.
static const float HULL_BOW = 2.0f;
static const float HULL_STERN = 1.6f;
static const float HULL_HALF_BEAM = .75f;
static const float HULL_SHORE_CLEARANCE = .35f;
static const float CONTACT_FRICTION = .15f;

void boat_reset(Boat *boat) { *boat = (Boat) { .position = { 0, 10 } }; }

void boat_reset_location(Boat *boat, const BoatWorldDefinition *definition)
{
    boat_reset(boat);
    boat->position = definition->spawn;
}

typedef struct {
    Vector2 local, normal, velocity;
} HullContact;

static Vector2 hull_point(Vector2 forward, Vector2 right, Vector2 position, Vector2 local)
{
    return Vector2Add(
        position, Vector2Add(Vector2Scale(forward, local.y), Vector2Scale(right, local.x)));
}

static void hull_vertical_bounds(const Boat *boat, float *bottom, float *top)
{
    // Physical hull plus cabin. Transform all corners so sideways/inverted jumps count.
    BoatModelSpec spec
        = { .length = 3.6f, .beam = 1.5f, .draft = .35f, .freeboard = .35f, .cabin_height = .8f };
    Transform3D pose = { .position = { 0, boat->height, 0 },
        .rotation = boat_model_rotation(boat_pose_rotation(boat)),
        .scale = { 1, 1, 1 } };
    boat_model_vertical_bounds(spec, pose, bottom, top);
}

static bool hull_contact(
    const Boat *boat, const BoatWorld *world, double time, HullContact *contact)
{
    float bottom, top;
    hull_vertical_bounds(boat, &bottom, &top);
    const Vector2 samples[] = { { 0, HULL_BOW }, { -HULL_HALF_BEAM, HULL_BOW },
        { HULL_HALF_BEAM, HULL_BOW }, { 0, -HULL_STERN }, { -HULL_HALF_BEAM, -HULL_STERN },
        { HULL_HALF_BEAM, -HULL_STERN } };
    Vector2 forward = { sinf(boat->heading), -cosf(boat->heading) };
    Vector2 right = { cosf(boat->heading), sinf(boat->heading) };
    for (size_t i = 0; i < sizeof(samples) / sizeof(*samples); ++i) {
        Vector2 point = hull_point(forward, right, boat->position, samples[i]);
        Vector2 normal = { 0 };
        bool water;
        if (world->definition->mapped) {
            water = boat_map_water_contact(
                &world->resources->map, point, HULL_SHORE_CLEARANCE, &normal);
            if (water
                && waterfront_harbor_contact_height_scene(world->scene, point, HULL_SHORE_CLEARANCE,
                    bottom, top, world->water, time, &normal))
                water = false;
        } else {
            Vector2 half = world->definition->half_extent;
            water = fabsf(point.x) <= half.x - HULL_SHORE_CLEARANCE
                && fabsf(point.y) <= half.y - HULL_SHORE_CLEARANCE;
            if (!water) {
                if (fabsf(point.x) - (half.x - HULL_SHORE_CLEARANCE)
                    > fabsf(point.y) - (half.y - HULL_SHORE_CLEARANCE))
                    normal.x = point.x < 0 ? 1 : -1;
                else
                    normal.y = point.y < 0 ? 1 : -1;
            }
        }
        if (!water) {
            *contact = (HullContact) { .local = samples[i], .normal = normal };
            return true;
        }
    }
    return false;
}

static void resolve_hull_contact(Boat *boat, Vector2 safe_position, HullContact contact)
{
    boat->position = safe_position;
    Vector2 forward = { sinf(boat->heading), -cosf(boat->heading) };
    Vector2 right = { cosf(boat->heading), sinf(boat->heading) };
    Vector2 arm
        = Vector2Add(Vector2Scale(right, contact.local.x), Vector2Scale(forward, contact.local.y));
    Vector2 point_velocity = Vector2Add(
        boat->velocity, (Vector2) { -boat->yaw_velocity * arm.y, boat->yaw_velocity * arm.x });
    float normal_speed
        = Vector2DotProduct(Vector2Subtract(point_velocity, contact.velocity), contact.normal);
    if (normal_speed < 0) {
        float arm_cross_normal = arm.x * contact.normal.y - arm.y * contact.normal.x;
        float impulse = -(1 + BOAT_CONTACT_RESTITUTION) * normal_speed
            / (1 + arm_cross_normal * arm_cross_normal / YAW_INERTIA);
        boat->velocity = Vector2Add(boat->velocity, Vector2Scale(contact.normal, impulse));
        boat->yaw_velocity += arm_cross_normal * impulse / YAW_INERTIA;
        // Limit tangential friction by impact strength so shallow scrapes retain momentum.
        float tangent_speed = Vector2DotProduct(Vector2Subtract(boat->velocity, contact.velocity),
            (Vector2) { -contact.normal.y, contact.normal.x });
        float friction = fminf(fabsf(tangent_speed), CONTACT_FRICTION * impulse);
        boat->velocity = Vector2Subtract(boat->velocity,
            Vector2Scale((Vector2) { -contact.normal.y, contact.normal.x },
                copysignf(friction, tangent_speed)));
    }
}

static void resolve_traffic(Boat *boat, const BoatWorld *world, double time)
{
    if (!world->definition->mapped)
        return;
    float bottom, top;
    hull_vertical_bounds(boat, &bottom, &top);
    // Separate the full hull footprints, then match contact velocity against the moving hull.
    // Multiple passes cover a rare simultaneous contact without advancing traffic time.
    for (int pass = 0; pass < 4; ++pass) {
        WaterfrontTrafficContact traffic;
        if (!waterfront_traffic_contact_height_scene(world->scene, boat->position, boat->heading,
                HULL_BOW, HULL_STERN, HULL_HALF_BEAM, bottom, top, world->water, time, &traffic))
            break;
        Vector2 previous = boat->position;
        Vector2 shifted
            = Vector2Add(previous, Vector2Scale(traffic.normal, traffic.penetration + .002f));
        resolve_hull_contact(boat, shifted,
            (HullContact) {
                .local = traffic.local, .normal = traffic.normal, .velocity = traffic.velocity });
        HullContact shore;
        if (hull_contact(boat, world, time, &shore))
            resolve_hull_contact(boat, previous, shore);
    }
}

bool boat_pair_contact(const Boat *a, const Boat *b, BoatPairContact *contact)
{
    float low_a, high_a, low_b, high_b;
    hull_vertical_bounds(a, &low_a, &high_a);
    hull_vertical_bounds(b, &low_b, &high_b);
    if (low_a > high_b || low_b > high_a)
        return false;
    Vector2 forward[2]
        = { { sinf(a->heading), -cosf(a->heading) }, { sinf(b->heading), -cosf(b->heading) } };
    Vector2 right[2]
        = { { cosf(a->heading), sinf(a->heading) }, { cosf(b->heading), sinf(b->heading) } };
    Vector2 center[2] = { Vector2Add(a->position, Vector2Scale(forward[0], .2f)),
        Vector2Add(b->position, Vector2Scale(forward[1], .2f)) };
    Vector2 delta = Vector2Subtract(center[0], center[1]);
    Vector2 normal = { 0 };
    float depth = INFINITY;
    for (int axis = 0; axis < 4; ++axis) {
        Vector2 n = axis % 2 ? right[axis / 2] : forward[axis / 2];
        float radius = 0;
        for (int hull = 0; hull < 2; ++hull)
            radius += 1.8f * fabsf(Vector2DotProduct(n, forward[hull]))
                + HULL_HALF_BEAM * fabsf(Vector2DotProduct(n, right[hull]));
        float distance = Vector2DotProduct(delta, n);
        float overlap = radius - fabsf(distance);
        if (overlap <= 0)
            return false;
        if (overlap < depth) {
            depth = overlap;
            normal = Vector2Scale(n, distance < 0 ? -1 : 1);
        }
    }
    Vector2 point = Vector2Scale(Vector2Add(center[0], center[1]), .5f);
    *contact = (BoatPairContact) { .normal = normal,
        .arm_a = Vector2Subtract(point, a->position),
        .arm_b = Vector2Subtract(point, b->position),
        .depth = depth };
    return true;
}

float boat_pair_speed(const Boat *a, const Boat *b, BoatPairContact contact)
{
    Vector2 arm_a = contact.arm_a, arm_b = contact.arm_b;
    Vector2 va = Vector2Add(
        a->velocity, (Vector2) { -a->yaw_velocity * arm_a.y, a->yaw_velocity * arm_a.x });
    Vector2 vb = Vector2Add(
        b->velocity, (Vector2) { -b->yaw_velocity * arm_b.y, b->yaw_velocity * arm_b.x });
    return Vector2DotProduct(Vector2Subtract(va, vb), contact.normal);
}

float boat_pair_impulse(BoatPairContact contact, float mass_a, float mass_b, float change)
{
    assert(mass_a > 0 && mass_b > 0);
    float inverse_a = 1 / mass_a, inverse_b = 1 / mass_b;
    float inverse_sum = inverse_a + inverse_b;
    Vector2 normal = contact.normal, arm_a = contact.arm_a, arm_b = contact.arm_b;
    float ca = arm_a.x * normal.y - arm_a.y * normal.x;
    float cb = arm_b.x * normal.y - arm_b.y * normal.x;
    return change
        / (inverse_sum + ca * ca * inverse_a / YAW_INERTIA + cb * cb * inverse_b / YAW_INERTIA);
}

void boat_pair_apply(Boat *a, float mass_a, Boat *b, float mass_b, BoatPairContact contact,
    float impulse, float separation)
{
    assert(mass_a > 0 && mass_b > 0);
    float inverse_a = 1 / mass_a, inverse_b = 1 / mass_b;
    float inverse_sum = inverse_a + inverse_b;
    Vector2 normal = contact.normal, arm_a = contact.arm_a, arm_b = contact.arm_b;
    if (impulse != 0) {
        float ca = arm_a.x * normal.y - arm_a.y * normal.x;
        float cb = arm_b.x * normal.y - arm_b.y * normal.x;
        a->velocity = Vector2Add(a->velocity, Vector2Scale(normal, impulse * inverse_a));
        b->velocity = Vector2Subtract(b->velocity, Vector2Scale(normal, impulse * inverse_b));
        a->yaw_velocity += ca * impulse * inverse_a / YAW_INERTIA;
        b->yaw_velocity -= cb * impulse * inverse_b / YAW_INERTIA;
    }
    if (separation != 0) {
        float shift = (contact.depth + .002f) * separation;
        a->position
            = Vector2Add(a->position, Vector2Scale(normal, shift * inverse_a / inverse_sum));
        b->position
            = Vector2Subtract(b->position, Vector2Scale(normal, shift * inverse_b / inverse_sum));
    }
}

bool boat_collide_pair(Boat *a, float mass_a, Boat *b, float mass_b)
{
    assert(mass_a > 0 && mass_b > 0);
    BoatPairContact contact;
    if (!boat_pair_contact(a, b, &contact))
        return false;
    float speed = boat_pair_speed(a, b, contact);
    float impulse = speed < 0
        ? boat_pair_impulse(contact, mass_a, mass_b, -(1 + BOAT_CONTACT_RESTITUTION) * speed)
        : 0;
    boat_pair_apply(a, mass_a, b, mass_b, contact, impulse, 1);
    return true;
}

bool boat_hull_clear(const Boat *boat, const BoatWorld *world, double time)
{
    HullContact contact;
    return !hull_contact(boat, world, time, &contact);
}

BoatWakeStern boat_wake_stern(const Boat *boat, const BoatWorld *world, double time)
{
    Vector3 p = boat_pose_attachment(boat, (Vector3) { 0, 0, BOAT_ENGINE_ARM });
    BoatPoseMotion motion = boat_pose_motion(boat, (Vector3) { 0, 0, BOAT_ENGINE_ARM });
    BoatWaterSample water
        = boat_water_query_field(world->water, world->wake_field, (Vector2) { -p.x, p.z }, time);
    double wet = 0;
    float trim = 4 * boat->planing * (1 - boat->planing) * TRIM_TRANSITION_PITCH
        + boat->planing * TRIM_CRUISE_PITCH;
    for (int side = -1; side <= 1; side += 2) {
        Vector3 q = boat_pose_attachment(boat, (Vector3) { side * .65f, 0, 1.4f });
        BoatWaterSample s = boat_water_query_field(
            world->water, world->wake_field, (Vector2) { -q.x, q.z }, time);
        float support = GRAVITY / 6
            + BUOYANCY_STIFFNESS / 6
                * (s.height + PLANING_HEIGHT * boat->planing - 1.4f * sinf(trim) - q.y);
        wet += .5 * Clamp(support / (GRAVITY / 6), 0, 1);
    }
    Vector2 forward = { sinf(boat->heading), -cosf(boat->heading) };
    Vector2 relative
        = Vector2Subtract(boat->velocity, boat_water_current(world->water, boat->position));
    float along = Vector2DotProduct(relative, forward);
    double drag = Lerp(1, PLANING_DRAG_MULTIPLIER, boat->planing)
        * (HULL_DRAG_LINEAR + HULL_DRAG_QUADRATIC * fabsf(along)) * fabsf(along);
    return (BoatWakeStern) { -p.x, p.z, -motion.velocity.x - water.velocity.x,
        motion.velocity.z - water.velocity.z, wet, drag, drag * fabsf(along) };
}

// Use the same six support locations and preload as buoyancy, rather than stern dryness.
static float hull_wet(const Boat *boat, const BoatWorld *world, double time)
{
    float trim = 4 * boat->planing * (1 - boat->planing) * TRIM_TRANSITION_PITCH
        + boat->planing * TRIM_CRUISE_PITCH;
    int wet = 0;
    for (int end = -1; end <= 1; ++end) {
        for (int side = -1; side <= 1; side += 2) {
            float fore = end * 1.4f;
            Vector3 point = boat_pose_attachment(boat, (Vector3) { side * .65f, 0, -fore });
            BoatWaterSample water = boat_water_query_field(
                world->water, world->wake_field, (Vector2) { -point.x, point.z }, time);
            float depth = water.height + PLANING_HEIGHT * boat->planing + fore * sinf(trim)
                + GRAVITY / BUOYANCY_STIFFNESS - point.y;
            wet += depth > 0;
        }
    }
    return wet / 6.0f;
}

static void motion_step(Boat *boat, float wet)
{
    if (boat->motion_state == BOAT_CAPSIZED)
        return; // Latched until an explicit reset; waves still move the hull.
    if (wet == 0) {
        boat->dry_time += BOAT_STEP;
        boat->inverted_contact_time = 0;
        boat->landing_time = 0;
        if (boat->dry_time >= .05f)
            boat->motion_state = BOAT_AIRBORNE;
    } else {
        if (boat->motion_state == BOAT_AIRBORNE) {
            boat->motion_state = BOAT_LANDING;
            boat->landing_time = .35f;
            ++boat->landings;
        }
        boat->dry_time = 0;
        boat->landing_time = fmaxf(0, boat->landing_time - BOAT_STEP);
        if (boat->landing_time == 0)
            boat->motion_state = BOAT_WATERBORNE;
        // World-up dot product handles either flip axis and any number of rotations.
        boat->inverted_contact_time
            = boat_pose_up(boat) < -.25f ? boat->inverted_contact_time + BOAT_STEP : 0;
        if (boat->inverted_contact_time >= .6f) {
            boat->motion_state = BOAT_CAPSIZED;
            boat->throttle = boat->engine_response = 0;
        }
    }
}

static void buoyancy_step(Boat *boat, const BoatWorld *world, double time, float turn_moment)
{
    float trim = 4 * boat->planing * (1 - boat->planing) * TRIM_TRANSITION_PITCH
        + boat->planing * TRIM_CRUISE_PITCH;
    float bank = remainderf(boat->roll, 2 * PI);
    float excess = fmaxf(fabsf(bank) - ROLL_GUARD_START, 0);
    float guard_mix = Clamp(excess / ROLL_GUARD_BLEND, 0, 1);
    guard_mix = guard_mix * guard_mix * (3 - 2 * guard_mix);
    float guard = -ROLL_GUARD_STIFFNESS * excess * copysignf(1, bank)
        - ROLL_GUARD_DAMPING * guard_mix * boat->roll_velocity;
    float wet = hull_wet(boat, world, time);
    // The ordinary turn guard belongs to wet, upright turns. Arcade air control is separate
    // below; both use wrapped angles so complete rotations do not accumulate spring torque.
    float upright = Clamp((boat_pose_up(boat) - .5f) / .25f, 0, 1);
    guard *= (wet > 0) * upright * (boat->motion_state != BOAT_CAPSIZED);
    float acceleration = -GRAVITY, pitch_torque = 0, roll_torque = turn_moment + guard;
    if (world->water.objects && boat->motion_state != BOAT_CAPSIZED) {
        float recovery = Clamp((boat_pose_up(boat) + .2f) / .7f, 0, 1);
        if (wet == 0) {
            // Steering retains modest yaw, but cannot command a roll. Automatic recovery
            // stays identical for keyboard and held-wheel mouse turns, including wake takeoff.
            roll_torque += ROLL_INERTIA
                * (-ARCADE_AIR_ROLL_RESPONSE * boat->roll_velocity
                    - ARCADE_AIR_ROLL_RECOVERY * recovery * sinf(bank));
            pitch_torque
                += PITCH_INERTIA * (-5 * recovery * sinf(boat->pitch) - 3 * boat->pitch_velocity);
        } else {
            // Recover further into a sideways landing, but leave an inverted hull capsizable.
            float upset = Clamp((fabsf(bank) - 15 * DEG2RAD) / (15 * DEG2RAD), 0, 1);
            roll_torque += ROLL_INERTIA * recovery
                * (-ARCADE_WET_ROLL_RECOVERY * upset * sinf(bank)
                    - ARCADE_WET_ROLL_DAMPING * boat->roll_velocity);
        }
    }
    float sp = sinf(boat->pitch), cp = cosf(boat->pitch);
    float sr = sinf(boat->roll), cr = cosf(boat->roll);
    Vector3 wave_force = { 0 };
    float wave_yaw = 0, bow_entry = 0, plunge = 0;
    bool waves = boat_world_displaced(world);
    for (int end = -1; end <= 1; ++end) {
        for (int side = -1; side <= 1; side += 2) {
            float fore = end * 1.4f, across = side * .65f;
            Vector3 point = boat_pose_attachment(boat, (Vector3) { across, 0, -fore });
            BoatWaterSample water = boat_water_query_field(
                world->water, world->wake_field, (Vector2) { -point.x, point.z }, time);
            // Exact vertical Jacobians of local roll, then pitch; heading leaves Y unchanged.
            float jp = fore * cp - across * sr * sp;
            float jr = across * cr * cp;
            float speed = boat->vertical_velocity + jp * boat->pitch_velocity
                + jr * boat->roll_velocity - water.velocity.y;
            float support_height
                = water.height + PLANING_HEIGHT * boat->planing + fore * sinf(trim);
            float force = GRAVITY / 6 + BUOYANCY_STIFFNESS / 6 * (support_height - point.y);
            float entry = fmaxf(0, -speed - LANDING_FREE_ENTRY);
            if (force > 0)
                plunge += entry * entry / 6;
            if (waves) {
                BoatPoseMotion motion = boat_pose_motion(boat, (Vector3) { across, 0, -fore });
                Vector3 velocity = { -motion.velocity.x, motion.velocity.y, motion.velocity.z };
                Vector3 relative = Vector3Subtract(velocity, water.velocity);
                float travel = hypotf(relative.x, relative.z);
                float damping = BUOYANCY_DAMPING / 6
                    * (1 + WAVE_DAMPING_GAIN * Clamp(travel / WAVE_CRUISE_SPEED, 0, 1));
                Vector3 resistance = boat_water_resistance(water, velocity, damping, force);
                Vector3 rendered = { -resistance.x, resistance.y, resistance.z };
                wave_force = Vector3Add(wave_force, resistance);
                pitch_torque += Vector3DotProduct(rendered, motion.pitch);
                roll_torque += Vector3DotProduct(rendered, motion.roll);
                wave_yaw += Vector3DotProduct(rendered, motion.yaw);
                if (end == 1 && force > 0)
                    bow_entry += .5f * fmaxf(0, -Vector3DotProduct(relative, water.normal));
                force = fmaxf(0, force); // Hydrostatic support; damping is applied as a 3D force.
            } else {
                // Retain the accepted C2 flat-water support and damping exactly.
                if (force > 0)
                    force = fmaxf(0, force - BUOYANCY_DAMPING / 6 * speed);
                else
                    force = 0;
            }
            acceleration += force;
            pitch_torque += jp * force;
            roll_torque += jr * force;
        }
    }
    boat->velocity.x += wave_force.x * BOAT_STEP;
    boat->velocity.y += wave_force.z * BOAT_STEP;
    if (boat->motion_state == BOAT_LANDING && plunge > 0) {
        Vector2 flow = boat_water_current(world->water, boat->position);
        Vector2 relative = Vector2Subtract(boat->velocity, flow);
        float keep = expf(-LANDING_SCRUB * plunge * BOAT_STEP);
        boat->velocity = Vector2Add(flow, Vector2Scale(relative, keep));
    }
    boat->yaw_velocity += wave_yaw / YAW_INERTIA * BOAT_STEP;
    acceleration += wave_force.y;
    float spray_target = Clamp(bow_entry / WAVE_SPRAY_ENTRY_SPEED, 0, 1);
    boat->wave_spray = waves
        ? Lerp(boat->wave_spray, spray_target, 1 - expf(-WAVE_SPRAY_RESPONSE * BOAT_STEP))
        : 0;
    boat->vertical_velocity += acceleration * BOAT_STEP;
    boat->pitch_velocity += pitch_torque / PITCH_INERTIA * BOAT_STEP;
    boat->roll_velocity += roll_torque / ROLL_INERTIA * BOAT_STEP;
    boat->height += boat->vertical_velocity * BOAT_STEP;
    boat->pitch += boat->pitch_velocity * BOAT_STEP;
    boat->roll += boat->roll_velocity * BOAT_STEP;
}

const BoatHandling boat_handling_speedboat = { .thrust = 11.0f };
const BoatHandling boat_handling_explorer = { .thrust = 18.0f };
const BoatHandling boat_handling_cabin = { .thrust = 4.45f };

void boat_step(Boat *boat, BoatWorld *world, double now, BoatInput input)
{
    boat_step_handling(boat, world, now, input, &boat_handling_speedboat);
}

void boat_step_handling(
    Boat *boat, BoatWorld *world, double now, BoatInput input, const BoatHandling *handling)
{
    float wheel_change = input.steering_rate;
    float throttle_change = input.throttle_rate;
    double time = now;
    float wet = hull_wet(boat, world, time) > 0 ? 1 : 0;
    motion_step(boat, wet);
    if (boat->motion_state == BOAT_CAPSIZED)
        throttle_change = 0;
    // One second from centre to either lock. Countersteering spends only 1/3 second
    // unwinding full lock; any time left in this tick uses the normal turn-in rate.
    // Splitting at centre keeps the faster rate from overshooting into the new turn.
    if (wheel_change != 0) {
        float rate = Clamp(wheel_change, -1, 1);
        float remaining = BOAT_STEP;
        if (rate * boat->wheel < 0) {
            float unwind = fminf(remaining, fabsf(boat->wheel / rate) / BOAT_WHEEL_UNWIND_RATE);
            boat->wheel += rate * BOAT_WHEEL_UNWIND_RATE * unwind;
            remaining -= unwind;
        }
        boat->wheel = Clamp(boat->wheel + rate * remaining, -1, 1);
    } else {
        boat->wheel *= expf(-WHEEL_RETURN_RATE * BOAT_STEP);
        if (fabsf(boat->wheel) < 0.001f)
            boat->wheel = 0;
    }
    // Fast command travel through neutral; integrate on fixed ticks, not keyboard repeat.
    boat->throttle = Clamp(
        boat->throttle + Clamp(throttle_change, -1, 1) * BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP, -1,
        1);
    float engine_target = boat->throttle;
    boat->engine_response += Clamp(engine_target - boat->engine_response,
        -ENGINE_RESPONSE_RATE * BOAT_STEP, ENGINE_RESPONSE_RATE * BOAT_STEP);
    // The outboard follows the wheel as it turns or springs back to centre.
    float target_angle = boat->wheel * MAX_ENGINE_ANGLE;
    boat->engine_angle += Clamp(target_angle - boat->engine_angle, -2 * BOAT_STEP, 2 * BOAT_STEP);
    Vector2 forward = { sinf(boat->heading), -cosf(boat->heading) };
    Vector2 right = { cosf(boat->heading), sinf(boat->heading) };
    Vector2 relative
        = Vector2Subtract(boat->velocity, boat_water_current(world->water, boat->position));
    float along = Vector2DotProduct(relative, forward);
    // Only forward water-relative motion can establish plane.
    float planing_target = 0;
    if (wet > 0 && along > (boat->planing > 0 ? PLANING_EXIT_SPEED : PLANING_ENTER_SPEED))
        planing_target = 1;
    float planing_rate = planing_target > boat->planing ? PLANING_RISE_RATE : PLANING_FALL_RATE;
    boat->planing = Clamp(boat->planing
            + Clamp(planing_target - boat->planing, -planing_rate * BOAT_STEP,
                planing_rate * BOAT_STEP),
        0, 1);
    // Propeller contact is separate from whole-hull support: a dry stern cannot propel.
    float propulsion = boat_wake_stern(boat, world, time).wet > 0 && boat_pose_up(boat) > 0
            && boat->motion_state != BOAT_CAPSIZED
        ? 1
        : 0;
    Vector2 force = Vector2Scale(
        boat_pose_thrust_direction(boat), handling->thrust * boat->engine_response * propulsion);
    float side_load = Vector2DotProduct(force, right);
    // Torque = lever arm x force: a portward stern force produces starboard yaw.
    float torque = -BOAT_ENGINE_ARM * Vector2DotProduct(force, right);
    float drag_multiplier = Lerp(1, PLANING_DRAG_MULTIPLIER, boat->planing);
    float drag = drag_multiplier * (HULL_DRAG_LINEAR + HULL_DRAG_QUADRATIC * fabsf(along)) * along;
    force = Vector2Subtract(force, Vector2Scale(forward, drag * wet));
    // Two hull stations resist their own lateral water velocity, including rotation.
    // This produces both lateral grip and yaw damping rather than prescribing a turn rate.
    for (int end = -1; end <= 1; end += 2) {
        float arm = end * 1.2f;
        Vector2 station = Vector2Add(boat->position, Vector2Scale(forward, arm));
        Vector2 flow = boat_water_current(world->water, station);
        float lateral_speed = Vector2DotProduct(Vector2Subtract(boat->velocity, flow), right)
            + boat->yaw_velocity * arm;
        float grip = 0.7f + 0.055f * fabsf(along) + 0.10f * fabsf(lateral_speed);
        grip *= Lerp(1, PLANING_LATERAL_GRIP_MULTIPLIER, boat->planing);
        float lateral_force = -grip * lateral_speed * wet;
        force = Vector2Add(force, Vector2Scale(right, lateral_force));
        torque += arm * lateral_force;
        side_load += lateral_force;
    }
    // Outboard point velocity is relative to its local flow, including stern rotation.
    // The submerged outboard resists flow across its angled plane. Apply both components of
    // this force and its stern torque: it can redirect a coast, but cannot add kinetic energy.
    Vector2 stern_velocity = Vector2Subtract(
        Vector2Subtract(boat->velocity,
            boat_water_current(world->water,
                Vector2Subtract(boat->position, Vector2Scale(forward, BOAT_ENGINE_ARM)))),
        Vector2Scale(right, BOAT_ENGINE_ARM * boat->yaw_velocity));
    Vector2 skeg_normal = Vector2Add(Vector2Scale(right, cosf(boat->engine_angle)),
        Vector2Scale(forward, sinf(boat->engine_angle)));
    float cross_flow = Vector2DotProduct(stern_velocity, skeg_normal);
    Vector2 skeg_force = Vector2Scale(
        skeg_normal, -SKEG_RESISTANCE * Vector2Length(stern_velocity) * cross_flow * propulsion);
    force = Vector2Add(force, skeg_force);
    torque -= BOAT_ENGINE_ARM * Vector2DotProduct(skeg_force, right);
    side_load += Vector2DotProduct(skeg_force, right);
    // Snapshot water-relative forces before contacts; exclude drag and water-normal resistance.
    float forward_mix = Clamp(
        (along - TURN_ROLL_SPEED_START) / (TURN_ROLL_SPEED_FULL - TURN_ROLL_SPEED_START), 0, 1);
    forward_mix = forward_mix * forward_mix * (3 - 2 * forward_mix);
    float gain = Lerp(TURN_ROLL_LOW_GAIN, TURN_ROLL_PLANING_GAIN, boat->planing * forward_mix);
    boat->turn_roll_load += (gain * side_load - boat->turn_roll_load)
        * (1 - expf(-BOAT_STEP / TURN_ROLL_RESPONSE_SECONDS));
    float equivalent = TURN_ROLL_SCALE * tanhf((boat->turn_roll_load / GRAVITY) / TURN_ROLL_SCALE);
    // Sum of the six support stiffnesses times their squared across lever arms.
    float turn_moment = BUOYANCY_STIFFNESS * .65f * .65f * equivalent * wet
        * (boat->motion_state != BOAT_CAPSIZED) * Clamp((boat_pose_up(boat) - .5f) / .25f, 0, 1);
    torque -= 0.45f * boat->yaw_velocity * wet;
    // Modest air yaw, .3 rad/s² at full wheel, without air thrust or hydrodynamic forces.
    // Object-wake flight also has the separate attitude controller in buoyancy_step().
    if (boat->motion_state != BOAT_CAPSIZED)
        torque += YAW_INERTIA * .3f * boat->wheel * (1 - wet);
    boat->velocity = Vector2Add(boat->velocity, Vector2Scale(force, BOAT_STEP));
    boat->yaw_velocity += torque / YAW_INERTIA * BOAT_STEP;
    // Accept translation and rotation separately. A rejected rotation must not leave a hull
    // sample on land, otherwise even an outward move can be rejected on the next tick.
    Vector2 previous = boat->position;
    boat->position = Vector2Add(previous, Vector2Scale(boat->velocity, BOAT_STEP));
    HullContact contact;
    if (hull_contact(boat, world, time, &contact))
        resolve_hull_contact(boat, previous, contact);
    float previous_heading = boat->heading;
    boat->heading = remainderf(boat->heading + boat->yaw_velocity * BOAT_STEP, 2 * PI);
    if (hull_contact(boat, world, time, &contact)) {
        resolve_hull_contact(boat, boat->position, contact);
        boat->heading = previous_heading;
    }
    resolve_traffic(boat, world, time);
    buoyancy_step(boat, world, time, turn_moment);
    if (world->water.player_wake_enabled) {
        if (boat->physical_wake_time == 0)
            boat->physical_wake_start = previous;
        boat->physical_wake_time += BOAT_STEP;
        if (boat->physical_wake_time >= .4f) {
            Vector2 flow = boat_water_current(world->water, boat->position);
            float speed = Vector2Length(Vector2Subtract(boat->velocity, flow));
            // Move the source behind the hull so fresh packets cannot lift their emitter.
            Vector2 offset = Vector2Scale(forward, -BOAT_ENGINE_ARM);
            boat_water_emit_wake(&world->water, Vector2Add(boat->physical_wake_start, offset),
                Vector2Add(boat->position, offset), time, boat->physical_wake_time, speed);
            boat->physical_wake_time = 0;
        }
    }
}
