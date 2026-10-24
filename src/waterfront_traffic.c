#include "waterfront_traffic.h"

#include "raymath.h"

#include <math.h>

static const BoatModelKind kinds[WATERFRONT_TRAFFIC_VESSELS]
    = { BOAT_MODEL_RUNABOUT, BOAT_MODEL_CRUISER, BOAT_MODEL_SAILBOAT, BOAT_MODEL_FERRY };
static const float dock_speed = 2.2f;
static const Vector2 river_along = { .8f, .6f }, river_across = { -.6f, .8f };
static BoatModel models[WATERFRONT_TRAFFIC_VESSELS];
static bool initialized;

BoatModelSpec waterfront_traffic_spec(int index)
{
    if (index < 0 || index >= WATERFRONT_TRAFFIC_VESSELS)
        return (BoatModelSpec) { 0 };
    BoatModelSpec spec = boat_model_preset(kinds[index]);
    if (index == 0) {
        spec.hull = (Color) { 227, 154, 60, 255 };
        spec.trim = (Color) { 242, 227, 196, 255 };
    }
    return spec;
}

const Mesh3D *waterfront_traffic_mesh(int index)
{
    if (index < 0 || index >= WATERFRONT_TRAFFIC_VESSELS)
        return NULL;
    if (!initialized) {
        for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i)
            boat_model_generate(&models[i], waterfront_traffic_spec(i));
        initialized = true;
    }
    return &models[index].mesh;
}

double waterfront_traffic_period(int index)
{
    if (index < 0 || index >= WATERFRONT_TRAFFIC_VESSELS)
        return 0;
    return index == 0 ? (260 + 28 * (double)PI) / dock_speed : index == 3 ? 320 : 260;
}

static double wrapped(double value, double period)
{
    value = fmod(value, period);
    return value < 0 ? value + period : value;
}

WaterfrontVesselMotion waterfront_traffic_motion(int index, double time)
{
    WaterfrontVesselMotion out = { 0 };
    if (index < 0 || index >= WATERFRONT_TRAFFIC_VESSELS || !isfinite(time))
        return out;
    Vector2 p, v, a = { 0 };
    if (index == 0) {
        // Two straight lanes with 14 m semicircular turns, entirely south of Dockbron.
        double distance = wrapped((double)time * dock_speed + 70, 260 + 28 * (double)PI);
        float arc = 14 * PI;
        if (distance < 130) {
            p = (Vector2) { 2, -45 + (float)distance };
            v = (Vector2) { 0, dock_speed };
        } else if (distance < 130 + arc) {
            float u = ((float)distance - 130) / 14;
            p = (Vector2) { 16 - 14 * cosf(u), 85 + 14 * sinf(u) };
            v = (Vector2) { dock_speed * sinf(u), dock_speed * cosf(u) };
            a = (Vector2) { dock_speed * dock_speed / 14 * cosf(u),
                -dock_speed * dock_speed / 14 * sinf(u) };
        } else if (distance < 260 + arc) {
            p = (Vector2) { 30, 85 - ((float)distance - 130 - arc) };
            v = (Vector2) { 0, -dock_speed };
        } else {
            float u = ((float)distance - 260 - arc) / 14;
            p = (Vector2) { 16 + 14 * cosf(u), -45 - 14 * sinf(u) };
            v = (Vector2) { -dock_speed * sinf(u), -dock_speed * cosf(u) };
            a = (Vector2) { -dock_speed * dock_speed / 14 * cosf(u),
                dock_speed * dock_speed / 14 * sinf(u) };
        }
    } else {
        // Nested river circuits keep the ferry and smaller vessels in separate corridors.
        float long_radius = index == 3 ? 300 : 210, short_radius = index == 3 ? 65 : 28;
        double rate = 2 * (double)PI / waterfront_traffic_period(index);
        float u
            = (float)wrapped((double)time * rate + (index == 2 ? PI / 2 : -PI / 2), 2 * (double)PI);
        Vector2 local = { long_radius * cosf(u), short_radius * sinf(u) };
        Vector2 first
            = { -long_radius * sinf(u) * (float)rate, short_radius * cosf(u) * (float)rate };
        Vector2 second = { -local.x * (float)(rate * rate), -local.y * (float)(rate * rate) };
        p = Vector2Add((Vector2) { 50, -290 },
            Vector2Add(Vector2Scale(river_along, local.x), Vector2Scale(river_across, local.y)));
        v = Vector2Add(Vector2Scale(river_along, first.x), Vector2Scale(river_across, first.y));
        a = Vector2Add(Vector2Scale(river_along, second.x), Vector2Scale(river_across, second.y));
    }
    out.position = (Vector3) { p.x, 0, p.y };
    out.velocity = (Vector3) { v.x, 0, v.y };
    out.heading = atan2f(-v.x, -v.y);
    out.yaw_rate = (v.y * a.x - v.x * a.y) / Vector2LengthSqr(v);
    return out;
}

static Vector3 wake_point(int index, double time, float age, int side, bool physical)
{
    BoatModelSpec spec = waterfront_traffic_spec(index);
    WaterfrontVesselMotion motion = waterfront_traffic_motion(index, time - age);
    Vector3 forward = Vector3Normalize(motion.velocity);
    Vector3 right = { -forward.z, 0, forward.x };
    return Vector3Add(Vector3Subtract(motion.position, Vector3Scale(forward, spec.length * .54f)),
        Vector3Scale(right, side * (physical ? .9f + age * 2.4f : spec.beam * .42f + age * .38f)));
}

static bool track_wakes(BoatWakeObjects *wakes, double time)
{
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
        WaterfrontVesselMotion m = waterfront_traffic_motion(i, time);
        BoatModelSpec spec = waterfront_traffic_spec(i);
        float speed = Vector3Length(m.velocity);
        Vector3 stern = Vector3Subtract(
            m.position, Vector3Scale(Vector3Normalize(m.velocity), spec.length * .54f));
        // Scenery displacement hulls leave gentle wakes below the player's planing threshold.
        // This gain changes only emission strength; recorded geometry uses actual stern motion.
        if (!boat_wake_objects_track(
                wakes, (unsigned)i, (Vector2) { -stern.x, stern.z }, speed * 4, 1))
            return false;
    }
    return true;
}

bool waterfront_traffic_wakes_init(BoatWakeObjects *wakes, double time)
{
    if (!boat_wake_objects_init(wakes, BOAT_OBJECT_LIFE_TICKS, .5f))
        return false;
    for (unsigned tick = 0; tick <= BOAT_OBJECT_LIFE_TICKS; ++tick) {
        if (tick)
            boat_wake_objects_advance(wakes);
        double past = time + ((double)tick - BOAT_OBJECT_LIFE_TICKS) / 120;
        if (!track_wakes(wakes, past))
            return false;
    }
    return true;
}

bool waterfront_traffic_wakes_step(BoatWakeObjects *wakes, double time)
{
    boat_wake_objects_advance(wakes);
    return track_wakes(wakes, time);
}

void waterfront_traffic_evaluate(BoatWater water, double time, WaterfrontTrafficFrame *frame)
{
    *frame = (WaterfrontTrafficFrame) { 0 };
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
        BoatModelSpec spec = waterfront_traffic_spec(i);
        WaterfrontVesselState *vessel = &frame->vessels[i];
        vessel->id = 3000 + i;
        vessel->motion = waterfront_traffic_motion(i, time);
        vessel->pose = boat_water_pose(
            water, time, vessel->motion.position, vessel->motion.heading, spec.length, spec.beam);
        for (int light = 0; light < WATERFRONT_TRAFFIC_LIGHTS; ++light)
            vessel->lights[light] = boat_model_point(vessel->pose,
                (Vector3) { light == 2 ? 0 : (light ? 1 : -1) * spec.beam * .42f,
                    spec.freeboard + .32f, light == 2 ? spec.length * .42f : -spec.length * .12f });
        for (int side = -1; side <= 1; side += 2)
            for (int j = 0; j < (water.traffic_objects ? 16 : WATERFRONT_TRAFFIC_WAKE_SEGMENTS);
                ++j) {
                float age = j * .7f;
                Vector3 a = wake_point(i, time, age, side, water.traffic_objects != NULL);
                Vector3 b = wake_point(i, time, age + .7f, side, water.traffic_objects != NULL);
                Vector3 center = Vector3Scale(Vector3Add(a, b), .5f);
                float length = Vector3Distance(a, b);
                Vector3 direction = Vector3Subtract(b, a);
                float yaw = atan2f(-direction.x, -direction.z);
                float width = water.traffic_objects ? .65f + .035f * age : i == 3 ? .48f : .22f;
                Transform3D pose = boat_water_pose(water, time, center, yaw, length, width);
                pose.position.y += .035f;
                pose.scale = (Vector3) { width, 1, length };
                if (water.traffic_objects) {
                    float variation = sinf((float)remainder(time - age, 16) * 2.3f + i * 1.7f);
                    pose.scale.x *= .65f + .35f * variation * variation;
                    pose.scale.z *= .65f; // Broken patches instead of solid lines across the river.
                }
                float fade
                    = (1
                          - (j + .5f)
                              / (water.traffic_objects ? 16 : WATERFRONT_TRAFFIC_WAKE_SEGMENTS))
                    * (i == 2 ? .65f : 1);
                frame->wakes[frame->wake_count++] = (WaterfrontTrafficWake) { .pose = pose,
                    .color
                    = { (unsigned char)Lerp(45, 188, fade), (unsigned char)Lerp(79, 207, fade),
                        (unsigned char)Lerp(104, 202, fade), 255 } };
            }
    }
    if (water.vessel.height > 0) {
        BoatVesselWake w = water.vessel;
        Vector2 right = { -w.direction.y, w.direction.x };
        for (int side = -1; side <= 1; side += 2)
            for (int j = 0; j < 16; ++j) {
                float age = j * .5f;
                double emitted = time - age;
                if (emitted < w.started || emitted > w.started + w.duration)
                    continue;
                Vector2 a, b;
                if (!boat_vessel_wake_position(w, emitted, &a)
                    || !boat_vessel_wake_position(w, emitted - .5, &b))
                    continue;
                a = Vector2Add(
                    Vector2Subtract(a, Vector2Scale(w.direction, BOAT_VESSEL_LENGTH * .5f)),
                    Vector2Scale(right, side * (BOAT_VESSEL_BEAM * .5f + age * w.spread)));
                b = Vector2Add(
                    Vector2Subtract(b, Vector2Scale(w.direction, BOAT_VESSEL_LENGTH * .5f)),
                    Vector2Scale(right, side * (BOAT_VESSEL_BEAM * .5f + (age + .5f) * w.spread)));
                Vector3 centre = { -(a.x + b.x) * .5f, 0, (a.y + b.y) * .5f };
                float length = Vector2Distance(a, b);
                float yaw = atan2f(b.x - a.x, a.y - b.y);
                Transform3D pose = boat_water_pose(water, time, centre, yaw, length, 1);
                pose.position.y += .045f;
                float variation = sinf((float)remainder(emitted, 16) * 2.3f);
                pose.scale = (Vector3) { (.8f + age * .06f) * (.65f + .35f * variation * variation),
                    1, length * .7f };
                float fade = (1 - (j + .5f) / 16) * .9f;
                frame->wakes[frame->wake_count++] = (WaterfrontTrafficWake) { .pose = pose,
                    .color
                    = { (unsigned char)Lerp(45, 206, fade), (unsigned char)Lerp(79, 222, fade),
                        (unsigned char)Lerp(104, 218, fade), 255 } };
            }
    }
}

// SAT uses both hull frames, avoiding centre-point or corner-sample tunnelling at crossings.
static bool rectangles(Vector2 delta, Vector2 player_forward, Vector2 player_right,
    Vector2 vessel_forward, Vector2 vessel_right, Vector2 player_half, Vector2 vessel_half,
    Vector2 *normal, float *penetration)
{
    Vector2 axes[] = { player_forward, player_right, vessel_forward, vessel_right };
    *penetration = INFINITY;
    for (int i = 0; i < 4; ++i) {
        Vector2 axis = axes[i];
        float radius = player_half.x * fabsf(Vector2DotProduct(player_right, axis))
            + player_half.y * fabsf(Vector2DotProduct(player_forward, axis))
            + vessel_half.x * fabsf(Vector2DotProduct(vessel_right, axis))
            + vessel_half.y * fabsf(Vector2DotProduct(vessel_forward, axis));
        float distance = Vector2DotProduct(delta, axis);
        float overlap = radius - fabsf(distance);
        if (overlap <= 0)
            return false;
        if (overlap < *penetration) {
            *penetration = overlap;
            *normal = Vector2Scale(axis, distance < 0 ? -1 : 1);
        }
    }
    return true;
}

bool waterfront_traffic_contact(Vector2 position, float heading, float bow, float stern,
    float half_beam, double time, WaterfrontTrafficContact *contact)
{
    return waterfront_traffic_contact_height(position, heading, bow, stern, half_beam, -INFINITY,
        INFINITY, (BoatWater) { 0 }, time, contact);
}

bool waterfront_traffic_contact_height(Vector2 position, float heading, float bow, float stern,
    float half_beam, float bottom, float top, BoatWater water, double time,
    WaterfrontTrafficContact *contact)
{
    Vector2 forward = { sinf(heading), -cosf(heading) }, right = { cosf(heading), sinf(heading) };
    float mid = (bow - stern) * .5f;
    Vector2 center = Vector2Add(position, Vector2Scale(forward, mid));
    for (int i = 0; i <= WATERFRONT_TRAFFIC_VESSELS; ++i) {
        // The opt-in race trial has its own authored source, separate from scenery traffic.
        // Its conservative box agrees with the shared CPU/GPU trial ferry presentation.
        bool trial = i == WATERFRONT_TRAFFIC_VESSELS;
        Vector2 source;
        if (trial && !boat_vessel_wake_position(water.vessel, time, &source))
            continue;
        BoatModelSpec spec = waterfront_traffic_spec(i);
        WaterfrontVesselMotion motion = waterfront_traffic_motion(i, time);
        if (trial) {
            spec = boat_model_preset(BOAT_MODEL_FERRY);
            spec.length = BOAT_VESSEL_LENGTH;
            spec.beam = BOAT_VESSEL_BEAM;
            spec.draft = 1.2f;
            spec.freeboard = 1.6f;
            spec.cabin_height = 3.2f;
            motion.position = (Vector3) { -source.x, 0, source.y };
            motion.heading = atan2f(water.vessel.direction.x, -water.vessel.direction.y);
            float speed
                = time - water.vessel.started < water.vessel.duration ? water.vessel.speed : 0;
            motion.velocity = (Vector3) { -water.vessel.direction.x * speed, 0,
                water.vessel.direction.y * speed };
            motion.yaw_rate = 0;
        }
        Vector2 ship = { -motion.position.x, motion.position.z };
        Vector2 vf = { sinf(motion.heading), -cosf(motion.heading) };
        Vector2 vr = { cosf(motion.heading), sinf(motion.heading) };
        Vector2 normal;
        float depth;
        if (!rectangles(Vector2Subtract(center, ship), forward, right, vf, vr,
                (Vector2) { half_beam + .15f, (bow + stern) * .5f + .15f },
                (Vector2) { spec.beam * .5f + .25f, spec.length * .5f + .5f }, &normal, &depth))
            continue;
        Transform3D pose
            = boat_water_pose(water, time, motion.position, motion.heading, spec.length, spec.beam);
        float low, high;
        boat_model_vertical_bounds(spec, pose, &low, &high);
        if (bottom > high || top < low)
            continue;
        float nr = Vector2DotProduct(normal, right), nf = Vector2DotProduct(normal, forward);
        Vector2 local = { fabsf(nr) < .001f ? 0 : -copysignf(half_beam, nr),
            fabsf(nf) < .001f ? mid
                : nf < 0      ? bow
                              : -stern };
        Vector2 point = Vector2Add(
            position, Vector2Add(Vector2Scale(right, local.x), Vector2Scale(forward, local.y)));
        Vector2 arm = Vector2Subtract(point, ship);
        if (contact)
            *contact = (WaterfrontTrafficContact) { .vessel = i,
                .normal = normal,
                .local = local,
                .penetration = depth,
                .velocity = { -motion.velocity.x - motion.yaw_rate * arm.y,
                    motion.velocity.z + motion.yaw_rate * arm.x } };
        return true;
    }
    return false;
}

bool waterfront_traffic_camera_clear(Vector3 p, double time, float clearance)
{
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
        BoatModelSpec spec = waterfront_traffic_spec(i);
        WaterfrontVesselMotion m = waterfront_traffic_motion(i, time);
        Vector3 q = Vector3Transform(Vector3Subtract(p, m.position), MatrixRotateY(-m.heading));
        float top = fmaxf(spec.mast_height,
            spec.freeboard + spec.cabin_height + (spec.passenger_deck ? 2.6f : .8f));
        if (fabsf(q.x) < spec.beam * .5f + clearance
            && fabsf(q.z) < spec.length * .5f + 1 + clearance && q.y > -spec.draft - clearance
            && q.y < top + clearance)
            return false;
    }
    return true;
}
