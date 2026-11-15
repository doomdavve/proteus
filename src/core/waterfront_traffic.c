#include "core/waterfront_traffic.h"

#include "core/boat_scene_definition.h"
#include "core/waterfront_route.h"
#include "core/waterfront_service.h"
#include "core/waterfront_upstream.h"
#include "raylib.h"
#include "raymath.h"

#include <math.h>

BoatModelSpec waterfront_traffic_spec_routes(const WaterfrontRouteDefinition *routes, int index)
{
    return waterfront_route_spec(routes, index);
}

BoatModelSpec waterfront_traffic_spec(int index)
{
    return waterfront_traffic_spec_routes(NULL, index);
}

const Mesh3D *waterfront_traffic_mesh_routes(const WaterfrontRouteDefinition *routes, int index)
{
    return waterfront_route_mesh(routes, index);
}

const Mesh3D *waterfront_traffic_mesh(int index)
{
    return waterfront_traffic_mesh_routes(NULL, index);
}

const Mesh3D *waterfront_traffic_mesh_slot(int slot)
{
    if (slot < WATERFRONT_TRAFFIC_ALL)
        return waterfront_traffic_mesh(slot);
    int recipe = slot - WATERFRONT_TRAFFIC_ALL;
    if (recipe < 0 || recipe >= WATERFRONT_TRAFFIC_RECIPE_SLOTS)
        return NULL;
    return waterfront_route_recipe_mesh((WaterfrontCraftRecipe)recipe);
}

double waterfront_traffic_period_routes(const WaterfrontRouteDefinition *routes, int index)
{
    return waterfront_route_period(routes, index);
}

double waterfront_traffic_period(int index)
{
    return waterfront_traffic_period_routes(NULL, index);
}

WaterfrontVesselMotion waterfront_traffic_motion_routes(
    const WaterfrontRouteDefinition *routes, int index, double time)
{
    return waterfront_route_motion(routes, index, time);
}

WaterfrontVesselMotion waterfront_traffic_motion(int index, double time)
{
    return waterfront_traffic_motion_routes(NULL, index, time);
}

int waterfront_traffic_actor_count(const WaterfrontRouteDefinition *routes)
{
    return routes ? (int)routes->actor_count : WATERFRONT_TRAFFIC_ALL;
}

static Vector3 wake_point_routes(const WaterfrontRouteDefinition *routes, int index, double time,
    float age, int side, bool physical)
{
    BoatModelSpec spec = waterfront_traffic_spec_routes(routes, index);
    WaterfrontVesselMotion motion = waterfront_traffic_motion_routes(routes, index, time - age);
    bool stopping = routes ? (unsigned)routes->actors[index].recipe >= WATERFRONT_CRAFT_ALVSNABBEN
                           : index >= WATERFRONT_TRAFFIC_VESSELS;
    // Stopping services follow the heading while stationary.
    Vector3 forward = stopping ? (Vector3) { -sinf(motion.heading), 0, -cosf(motion.heading) }
                               : Vector3Normalize(motion.velocity);
    Vector3 right = { -forward.z, 0, forward.x };
    return Vector3Add(Vector3Subtract(motion.position, Vector3Scale(forward, spec.length * .54f)),
        Vector3Scale(right, side * (physical ? .9f + age * 2.4f : spec.beam * .42f + age * .38f)));
}

static Vector3 wake_point(int index, double time, float age, int side, bool physical)
{
    return wake_point_routes(NULL, index, time, age, side, physical);
}

// The same unit-plane recipe serves ordinary traffic and historical service hulls.
static void append_wake(BoatWater water, double time, WaterfrontTrafficFrame *frame, Vector3 a,
    Vector3 b, float width, float fade, float patch_width, float patch_length)
{
    if (frame->wake_count >= WATERFRONT_TRAFFIC_WAKE_CAPACITY)
        return;
    Vector3 direction = Vector3Subtract(b, a);
    float length = Vector3Length(direction);
    if (!(length > 1e-5f) || !isfinite(length))
        return;
    Vector3 center = Vector3Scale(Vector3Add(a, b), .5f);
    Transform3D pose
        = boat_water_pose(water, time, center, atan2f(-direction.x, -direction.z), length, width);
    pose.position.y += .035f;
    pose.scale = (Vector3) { width * patch_width, 1, length * patch_length };
    frame->wakes[frame->wake_count++] = (WaterfrontTrafficWake) { .pose = pose,
        .color = { (unsigned char)Lerp(45, 188, fade), (unsigned char)Lerp(79, 207, fade),
            (unsigned char)Lerp(104, 202, fade), 255 } };
}

// A segment may cross a continuous piece join, but never any dwell, however short.
static bool service_interval(
    const BoatSceneDefinition *scene, unsigned service, double earlier, double later)
{
    const BoatSceneService *s = &scene->services[service];
    int route = boat_scene_find_route(scene, s->route);
    if (route < 0 || earlier < 0)
        return false;
    const BoatRoutePath *path = &scene->traffic.actors[route].open_path;
    uint64_t first = waterfront_service_tick(earlier), last = waterfront_service_tick(later);
    if (path->loop || first < s->start_tick)
        return false;
    first -= s->start_tick;
    last -= s->start_tick;
    uint64_t at = 0;
    for (unsigned i = 0; i < path->count; ++i) {
        const BoatRoutePathPiece *p = &path->pieces[i];
        at += p->kind == BOAT_ROUTE_PATH_LINE ? p->shape.line.duration_ticks
                                              : p->shape.arc.duration_ticks;
        if (p->dwell_ticks && first < at + p->dwell_ticks && last >= at)
            return false;
        at += p->dwell_ticks;
    }
    return last < at;
}

static Vector3 service_wake_point(const WaterfrontServiceHull *hull, float age, int side)
{
    Vector3 forward = { -sinf(hull->heading), 0, -cosf(hull->heading) };
    Vector3 right = { -forward.z, 0, forward.x };
    return Vector3Add(
        Vector3Subtract(hull->position, Vector3Scale(forward, hull->spec.length * .54f)),
        Vector3Scale(right, side * (hull->spec.beam * .42f + age * .38f)));
}

static void service_wakes(
    const BoatSceneDefinition *scene, BoatWater water, double time, WaterfrontTrafficFrame *frame)
{
    _Static_assert(BOAT_SCENE_VESSELS == WATERFRONT_SERVICE_WAKE_VESSELS,
        "service trail counters must cover the vessel slots");
    if (!scene || !scene->services_present || !isfinite(time))
        return;
    // Routes are location-validated afloat; kinematic service hulls have no dynamic dry state.
    // Sample each historical hull once and reuse it on both sides. History survives docking
    // and disappearance; only the historical emission endpoints need to be travelling.
    for (unsigned v = 0; v < scene->vessel_count && v < BOAT_SCENE_VESSELS; ++v) {
        WaterfrontServiceHull samples[WATERFRONT_TRAFFIC_WAKE_SEGMENTS + 1] = { 0 };
        bool eligible[WATERFRONT_TRAFFIC_WAKE_SEGMENTS + 1] = { false };
        for (unsigned j = 0; j <= WATERFRONT_TRAFFIC_WAKE_SEGMENTS; ++j) {
            double t = time - j * .7;
            eligible[j] = t >= 0 && waterfront_service_hull(scene, v, t, &samples[j])
                && samples[j].owner == BOAT_OWNER_TRAVELLING
                && Vector3Length(samples[j].velocity) > .3f;
        }
        int before = frame->wake_count;
        for (unsigned j = 0; j < WATERFRONT_TRAFFIC_WAKE_SEGMENTS; ++j) {
            WaterfrontServiceHull *a = &samples[j], *b = &samples[j + 1];
            if (!eligible[j] || !eligible[j + 1] || a->service != b->service
                || !service_interval(scene, a->service, time - (j + 1) * .7, time - j * .7)
                || Vector3Distance(a->position, b->position) <= .3f * .7f)
                continue;
            float strength = fminf(Vector3Length(a->velocity), Vector3Length(b->velocity)) / 3;
            float width = (a->spec.mast_height > 0 ? .4f : .62f) * fminf(strength, 1);
            float fade = (1 - (j + .5f) / WATERFRONT_TRAFFIC_WAKE_SEGMENTS) * fminf(strength, 1);
            for (int side = -1; side <= 1; side += 2)
                append_wake(water, time, frame, service_wake_point(a, j * .7f, side),
                    service_wake_point(b, (j + 1) * .7f, side), width, fade, 1, 1);
        }
        frame->service_wake_count[v] = frame->wake_count - before;
    }
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

static void evaluate_impl(const WaterfrontRouteDefinition *routes, const bool *serviced,
    BoatWater water, double time, WaterfrontTrafficFrame *frame)
{
    *frame = (WaterfrontTrafficFrame) { 0 };
    frame->routes = routes;
    frame->vessel_count = routes ? (int)routes->actor_count
        : water.upstream         ? WATERFRONT_TRAFFIC_ALL
                                 : WATERFRONT_TRAFFIC_VESSELS;
    for (int i = 0; i < frame->vessel_count; ++i) {
        BoatModelSpec spec = waterfront_traffic_spec_routes(routes, i);
        WaterfrontVesselState *vessel = &frame->vessels[i];
        frame->mesh[i] = routes ? (unsigned char)(WATERFRONT_TRAFFIC_ALL + routes->actors[i].recipe)
                                : (unsigned char)i;
        vessel->id = 3000 + i;
        vessel->motion = waterfront_traffic_motion_routes(routes, i, time);
        vessel->pose = boat_water_pose(
            water, time, vessel->motion.position, vessel->motion.heading, spec.length, spec.beam);
        for (int light = 0; light < WATERFRONT_TRAFFIC_LIGHTS; ++light)
            vessel->lights[light] = boat_model_point(vessel->pose,
                (Vector3) { light == 2 ? 0 : (light ? 1 : -1) * spec.beam * .42f,
                    spec.freeboard + .32f, light == 2 ? spec.length * .42f : -spec.length * .12f });
        // M4: a serviced actor only provides a path. Its hull is drawn from the harbor frame, so
        // it has no mesh, lights or wake here.
        if (serviced && serviced[i]) {
            frame->hidden[i] = true;
            continue;
        }
        // Stopping services keep the longer visual trails and fade out as they slow.
        bool upstream = routes ? (unsigned)routes->actors[i].recipe >= WATERFRONT_CRAFT_ALVSNABBEN
                               : i >= WATERFRONT_TRAFFIC_VESSELS;
        // Ordinary authored traffic keeps visual trails. Physical wakes need an explicitly
        // budgeted recipe, so a scene never turns them on by accident.
        bool physical = !routes && water.traffic_objects && !upstream;
        int segments = physical ? 16 : WATERFRONT_TRAFFIC_WAKE_SEGMENTS;
        float step = upstream ? 1.2f : .7f;
        for (int side = -1; side <= 1; side += 2)
            for (int j = 0; j < segments; ++j) {
                float age = j * step, strength = 1;
                if (upstream) {
                    strength = Vector3Length(
                                   waterfront_traffic_motion_routes(routes, i, time - age).velocity)
                        / 3;
                    if (strength < .1f)
                        continue;
                }
                Vector3 a = routes ? wake_point_routes(routes, i, time, age, side, physical)
                                   : wake_point(i, time, age, side, physical);
                Vector3 b = routes ? wake_point_routes(routes, i, time, age + step, side, physical)
                                   : wake_point(i, time, age + step, side, physical);
                float width = physical ? .65f + .035f * age
                    : upstream         ? (spec.mast_height > 0 ? .4f : .62f) * fminf(strength, 1)
                    : i == 3           ? .48f
                                       : .22f;
                float variation
                    = physical ? sinf((float)remainder(time - age, 16) * 2.3f + i * 1.7f) : 0;
                float fade = (1 - (j + .5f) / segments) * (i == 2 ? .65f : 1);
                append_wake(water, time, frame, a, b, width, fade,
                    physical ? .65f + .35f * variation * variation : 1, physical ? .65f : 1);
            }
    }
    bool bridge = routes ? waterfront_route_has_bridge(routes) : water.upstream;
    if (bridge) {
        frame->lift = true;
        frame->lift_pose = waterfront_upstream_span_pose(time);
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

void waterfront_traffic_evaluate_routes(const WaterfrontRouteDefinition *routes, BoatWater water,
    double time, WaterfrontTrafficFrame *frame)
{
    evaluate_impl(routes, NULL, water, time, frame);
}

void waterfront_traffic_evaluate_scene(
    const BoatSceneDefinition *scene, BoatWater water, double time, WaterfrontTrafficFrame *frame)
{
    bool serviced[WATERFRONT_TRAFFIC_ALL] = { false };
    bool any = false;
    for (unsigned i = 0; scene && scene->traffic_present && i < scene->traffic.actor_count
        && i < WATERFRONT_TRAFFIC_ALL;
        ++i)
        any |= serviced[i] = waterfront_service_for_route(scene, i) >= 0;
    evaluate_impl(scene && scene->traffic_present ? &scene->traffic : NULL, any ? serviced : NULL,
        water, time, frame);
    service_wakes(scene, water, time, frame);
}

void waterfront_traffic_evaluate(BoatWater water, double time, WaterfrontTrafficFrame *frame)
{
    waterfront_traffic_evaluate_routes(NULL, water, time, frame);
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

// One moving or moored hull against the player's rectangle. MOTION is renderer-space.
static bool hull_contact(int index, BoatModelSpec spec, WaterfrontVesselMotion motion,
    Vector2 position, Vector2 forward, Vector2 right, Vector2 center, float bow, float stern,
    float half_beam, float mid, float bottom, float top, BoatWater water, double time,
    WaterfrontTrafficContact *contact)
{
    Vector2 ship = { -motion.position.x, motion.position.z };
    Vector2 vf = { sinf(motion.heading), -cosf(motion.heading) };
    Vector2 vr = { cosf(motion.heading), sinf(motion.heading) };
    Vector2 normal;
    float depth;
    if (!rectangles(Vector2Subtract(center, ship), forward, right, vf, vr,
            (Vector2) { half_beam + .15f, (bow + stern) * .5f + .15f },
            (Vector2) { spec.beam * .5f + .25f, spec.length * .5f + .5f }, &normal, &depth))
        return false;
    Transform3D pose
        = boat_water_pose(water, time, motion.position, motion.heading, spec.length, spec.beam);
    float low, high;
    boat_model_vertical_bounds(spec, pose, &low, &high);
    if (bottom > high || top < low)
        return false;
    float nr = Vector2DotProduct(normal, right), nf = Vector2DotProduct(normal, forward);
    Vector2 local = { fabsf(nr) < .001f ? 0 : -copysignf(half_beam, nr),
        fabsf(nf) < .001f ? mid
            : nf < 0      ? bow
                          : -stern };
    Vector2 point = Vector2Add(
        position, Vector2Add(Vector2Scale(right, local.x), Vector2Scale(forward, local.y)));
    Vector2 arm = Vector2Subtract(point, ship);
    if (contact)
        *contact = (WaterfrontTrafficContact) { .vessel = index,
            .normal = normal,
            .local = local,
            .penetration = depth,
            .velocity = { -motion.velocity.x - motion.yaw_rate * arm.y,
                motion.velocity.z + motion.yaw_rate * arm.x } };
    return true;
}

static bool contact_height_routes(const BoatSceneDefinition *scene,
    const WaterfrontRouteDefinition *routes, Vector2 position, float heading, float bow,
    float stern, float half_beam, float bottom, float top, BoatWater water, double time,
    WaterfrontTrafficContact *contact)
{
    Vector2 forward = { sinf(heading), -cosf(heading) }, right = { cosf(heading), sinf(heading) };
    float mid = (bow - stern) * .5f;
    Vector2 center = Vector2Add(position, Vector2Scale(forward, mid));
    int count = routes ? (int)routes->actor_count : WATERFRONT_TRAFFIC_ALL + 1;
    for (int i = 0; i < count; ++i) {
        if (!routes && i >= WATERFRONT_TRAFFIC_VESSELS && i < WATERFRONT_TRAFFIC_ALL
            && !water.upstream)
            continue;
        // The opt-in race trial has its own authored source, separate from scenery traffic.
        // Its conservative box agrees with the shared CPU/GPU trial ferry presentation.
        bool trial = !routes && i == WATERFRONT_TRAFFIC_TRIAL;
        Vector2 source;
        if (trial && !boat_vessel_wake_position(water.vessel, time, &source))
            continue;
        BoatModelSpec spec = waterfront_traffic_spec_routes(routes, i);
        WaterfrontVesselMotion motion = waterfront_traffic_motion_routes(routes, i, time);
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
        if (waterfront_service_for_route(scene, (unsigned)i) >= 0)
            continue; // Its vessel is evaluated below from the same service pose that is drawn.
        if (hull_contact(i, spec, motion, position, forward, right, center, bow, stern, half_beam,
                mid, bottom, top, water, time, contact))
            return true;
    }
    // M4: moored and travelling service hulls. The contact names the route actor, as before.
    for (unsigned v = 0; scene && scene->services_present && v < scene->vessel_count; ++v) {
        WaterfrontServiceHull hull;
        if (!waterfront_service_hull(scene, v, time, &hull))
            continue;
        int actor = boat_scene_find_route(scene, scene->services[hull.service].route);
        WaterfrontVesselMotion motion = { .position = hull.position,
            .velocity = hull.velocity,
            .heading = hull.heading,
            .yaw_rate = hull.yaw_rate };
        if (hull_contact(actor, hull.spec, motion, position, forward, right, center, bow, stern,
                half_beam, mid, bottom, top, water, time, contact))
            return true;
    }
    return false;
}

bool waterfront_traffic_contact_height(Vector2 position, float heading, float bow, float stern,
    float half_beam, float bottom, float top, BoatWater water, double time,
    WaterfrontTrafficContact *contact)
{
    return contact_height_routes(
        NULL, NULL, position, heading, bow, stern, half_beam, bottom, top, water, time, contact);
}

bool waterfront_traffic_contact_height_scene(const BoatSceneDefinition *scene, Vector2 position,
    float heading, float bow, float stern, float half_beam, float bottom, float top,
    BoatWater water, double time, WaterfrontTrafficContact *contact)
{
    const WaterfrontRouteDefinition *routes
        = scene && scene->traffic_present ? &scene->traffic : NULL;
    return contact_height_routes(
        scene, routes, position, heading, bow, stern, half_beam, bottom, top, water, time, contact);
}

static bool hull_blocks_camera(
    BoatModelSpec spec, Vector3 position, float heading, Vector3 p, float clearance)
{
    Vector3 q = Vector3Transform(Vector3Subtract(p, position), MatrixRotateY(-heading));
    float top = fmaxf(
        spec.mast_height, spec.freeboard + spec.cabin_height + (spec.passenger_deck ? 2.6f : .8f));
    return fabsf(q.x) < spec.beam * .5f + clearance
        && fabsf(q.z) < spec.length * .5f + 1 + clearance && q.y > -spec.draft - clearance
        && q.y < top + clearance;
}

static bool camera_clear_routes(const BoatSceneDefinition *scene,
    const WaterfrontRouteDefinition *routes, Vector3 p, double time, float clearance)
{
    int count = routes ? (int)routes->actor_count : WATERFRONT_TRAFFIC_VESSELS;
    for (int i = 0; i < count; ++i) {
        if (waterfront_service_for_route(scene, (unsigned)i) >= 0)
            continue;
        WaterfrontVesselMotion m = waterfront_traffic_motion_routes(routes, i, time);
        if (hull_blocks_camera(
                waterfront_traffic_spec_routes(routes, i), m.position, m.heading, p, clearance))
            return false;
    }
    for (unsigned v = 0; scene && scene->services_present && v < scene->vessel_count; ++v) {
        WaterfrontServiceHull hull;
        if (waterfront_service_hull(scene, v, time, &hull)
            && hull_blocks_camera(hull.spec, hull.position, hull.heading, p, clearance))
            return false;
    }
    return true;
}

bool waterfront_traffic_camera_clear(Vector3 p, double time, float clearance)
{
    return camera_clear_routes(NULL, NULL, p, time, clearance);
}

bool waterfront_traffic_camera_clear_scene(
    const BoatSceneDefinition *scene, Vector3 p, double time, float clearance)
{
    const WaterfrontRouteDefinition *routes
        = scene && scene->traffic_present ? &scene->traffic : NULL;
    return camera_clear_routes(scene, routes, p, time, clearance);
}
