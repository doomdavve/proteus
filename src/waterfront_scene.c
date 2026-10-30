#include "waterfront_scene.h"

#include "boat_clock.h"
#include "raymath.h"

#include <math.h>
#include <stdint.h>

const Mesh3D *waterfront_player_hull_mesh(void)
{
    return boat_skin_mesh(BOAT_SKIN_SWEDEN, BOAT_SKIN_HULL);
}

const Mesh3D *waterfront_player_cabin_mesh(void)
{
    return boat_skin_mesh(BOAT_SKIN_SWEDEN, BOAT_SKIN_COCKPIT);
}

const Mesh3D *waterfront_player_engine_mesh(void)
{
    return boat_skin_mesh(BOAT_SKIN_SWEDEN, BOAT_SKIN_ENGINE);
}

static Vector3 sky_mix(Vector3 a, Vector3 b, float t)
{
    t = Clamp(t, 0, 1);
    return Vector3Lerp(a, b, t * t * (3 - 2 * t));
}

Vector3 waterfront_sun_direction(void) { return Vector3Normalize((Vector3) { .10f, .48f, -.87f }); }

// A fixed cloud deck sampled in world X/Z, so square tiles recede toward the horizon.
static float cloud_hash(int x, int z)
{
    uint32_t seed = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u;
    seed = (seed ^ (seed >> 13)) * 1274126177u;
    return (float)((seed ^ (seed >> 16)) & 65535u) / 65535;
}

static bool cloud_tile(float x, float z)
{
    int tile_x = (int)floorf(x / .16f), tile_z = (int)floorf(z / .16f);
    float gx = tile_x / 5.0f, gz = tile_z / 5.0f;
    int ix = (int)floorf(gx), iz = (int)floorf(gz);
    float tx = gx - ix, tz = gz - iz;
    tx = tx * tx * (3 - 2 * tx);
    tz = tz * tz * (3 - 2 * tz);
    float a = Lerp(cloud_hash(ix, iz), cloud_hash(ix + 1, iz), tx);
    float b = Lerp(cloud_hash(ix, iz + 1), cloud_hash(ix + 1, iz + 1), tx);
    // Broad joined patches, notched edges, holes, and occasional detached little tiles.
    return Lerp(a, b, tz) * .85f + cloud_hash(tile_x + 137, tile_z - 59) * .15f > .59f;
}

Image waterfront_sky_image(void)
{
    enum { WIDTH = 1024, HEIGHT = 512 };
    static Color pixels[WIDTH * HEIGHT];
    static bool ready;
    if (!ready) {
        Vector3 sun = waterfront_sun_direction();
        for (int y = 0; y < HEIGHT; ++y) {
            float elevation = 1 - 2 * ((float)y + .5f) / HEIGHT;
            for (int x = 0; x < WIDTH; ++x) {
                float azimuth = (((float)x + .5f) / WIDTH - .5f) * 2 * PI;
                Vector3 color = sky_mix(
                    (Vector3) { 186, 229, 244 }, (Vector3) { 66, 158, 226 }, elevation / .85f);
                float sun_x
                    = remainderf(azimuth - atan2f(sun.x, sun.z), 2 * PI) * sqrtf(1 - sun.y * sun.y);
                float sun_y = (elevation - sun.y) / sqrtf(1 - sun.y * sun.y);
                float sun_distance = sqrtf(sun_x * sun_x + sun_y * sun_y);
                // A warm, crisp disc with a quiet halo, anchored to the lighting direction.
                color = sky_mix(color, (Vector3) { 255, 237, 165 },
                    .24f * (1 - Clamp(sun_distance / .16f, 0, 1)));
                color = sky_mix(color, (Vector3) { 255, 244, 184 },
                    Clamp((.047f - sun_distance) / .004f, 0, 1));
                if (elevation > .075f) {
                    float horizontal = sqrtf(1 - elevation * elevation) / elevation;
                    float cx = sinf(azimuth) * horizontal;
                    float cz = cosf(azimuth) * horizontal;
                    // Looking up through a shallow slab: its underside covers the distant rim.
                    // Two ray/plane samples give the cloud deck a little visible thickness.
                    bool underside = cloud_tile(cx, cz);
                    bool rim = cloud_tile(cx * 1.035f, cz * 1.035f);
                    if (underside || rim) {
                        Vector3 cloud
                            = underside ? (Vector3) { 225, 242, 244 } : (Vector3) { 205, 225, 238 };
                        color = sky_mix(color, cloud, (elevation - .075f) / .10f);
                    }
                }
                // Subtle ordered grain at panorama resolution, fixed to the sky itself.
                float grain = ((x & 1) == (y & 1)) ? .6f : -.6f;
                pixels[y * WIDTH + x]
                    = (Color) { (unsigned char)roundf(Clamp(color.x + grain, 0, 255)),
                          (unsigned char)roundf(Clamp(color.y + grain, 0, 255)),
                          (unsigned char)roundf(Clamp(color.z + grain, 0, 255)), 255 };
            }
        }
        ready = true;
    }
    return (Image) { .data = pixels,
        .width = WIDTH,
        .height = HEIGHT,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
}

void waterfront_sky_draw(Color *pixels, int width, int height, View3D camera)
{
    waterfront_sky_draw_squall(pixels, width, height, camera, (WaterfrontSquall) { 0 });
}

// The cloud's edge at height fraction H (0 at the base, 1 at the tops), relative to the radius:
// full width low down, narrowing and billowing upward.
static float squall_edge(float angle, float h)
{
    return 1 - .5f * h * h + .07f * sinf(5 * angle + 7 * h) + .05f * sinf(13 * angle + 3);
}

Color waterfront_squall_sky(Color sky, Vector3 eye, Vector3 ray, WaterfrontSquall q)
{
    if (q.grown <= 0 || q.radius <= 0)
        return sky;
    float horizontal = fmaxf(sqrtf(ray.x * ray.x + ray.z * ray.z), 1e-4f);
    Vector2 direction = { ray.x / horizontal, ray.z / horizontal };
    float slope = ray.y / horizontal; // Height gained per horizontal metre.
    Vector2 to = { q.centre.x - eye.x, q.centre.y - eye.z };
    float along = to.x * direction.x + to.y * direction.y;
    float miss = to.x * to.x + to.y * to.y - along * along;
    if (miss >= q.radius * q.radius)
        return sky;
    float half = sqrtf(q.radius * q.radius - miss);
    float near = fmaxf(along - half, 0), far = along + half;
    if (far <= 0)
        return sky;
    // Below the base: rain over the inner disk, its shafts taken at the middle of the path.
    float rain = 0;
    float base = slope > 0 ? (q.base - eye.y) / slope : INFINITY; // Distance to the base.
    float inner = q.radius * q.radius * .85f * .85f;
    if (miss < inner) {
        float chord = sqrtf(inner - miss);
        float from = fmaxf(along - chord, 0), to = fminf(along + chord, base);
        if (to > from) {
            float t = (from + to) * .5f;
            Vector2 p
                = { eye.x + direction.x * t - q.centre.x, eye.z + direction.y * t - q.centre.y };
            float shafts = .5f
                + .5f * sinf(5 * atan2f(p.y, p.x) + 4 * sqrtf(p.x * p.x + p.y * p.y) / q.radius);
            float showers = Clamp((q.grown - .3f) / .4f, 0, 1);
            rain = 1.6f * (to - from) / q.radius * showers * (.35f + .65f * shafts);
        }
    }
    // Above it: march the cloud between the base and its billowing edge.
    float ceiling = q.base + (q.top - q.base) * q.grown;
    float from = fmaxf(near, base == INFINITY ? far : base);
    const int steps = 16;
    float step = (far - from) / steps;
    for (int i = 0; i < steps && step > 0; ++i) {
        float t = from + step * ((float)i + .5f);
        Vector2 p = { eye.x + direction.x * t - q.centre.x, eye.z + direction.y * t - q.centre.y };
        float h = (eye.y + slope * t - q.base) / fmaxf(ceiling - q.base, 1);
        if (h > 1)
            break; // Rising away above the tops.
        float reach = sqrtf(p.x * p.x + p.y * p.y) / q.radius;
        float edge = squall_edge(atan2f(p.y, p.x), h);
        if (reach > edge)
            continue;
        // Dark underneath, lit toward the tops; soft at the rim.
        float lit = Clamp(h, 0, 1);
        float cover = Clamp((edge - reach) * 8, 0, 1) * Clamp(q.grown * 3, 0, 1);
        Color cloud = { (unsigned char)(64 + 92 * lit), (unsigned char)(72 + 90 * lit),
            (unsigned char)(84 + 86 * lit), 255 };
        sky = ColorLerp(sky, cloud, cover);
        break;
    }
    return ColorLerp(sky, (Color) { 98, 108, 120, 255 }, fminf(rain, .85f));
}

void waterfront_sky_draw_squall(
    Color *pixels, int width, int height, View3D camera, WaterfrontSquall squall)
{
    Image sky = waterfront_sky_image();
    const Color *colors = sky.data;
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(camera.up, forward));
    Vector3 up = Vector3CrossProduct(forward, right);
    float scale = 2 * tanf(camera.vertical_fov * .5f) / height;
    for (int y = 0; y < height; ++y) {
        Vector3 row = Vector3Add(forward, Vector3Scale(up, (height * .5f - y - .5f) * scale));
        for (int x = 0; x < width; ++x) {
            Vector3 ray = Vector3Normalize(
                Vector3Add(row, Vector3Scale(right, (x + .5f - width * .5f) * scale)));
            float u = atan2f(ray.x, ray.z) / (2 * PI) + .5f;
            int sx = (int)(u * sky.width) % sky.width;
            int sy = (int)Clamp((1 - ray.y) * .5f * sky.height, 0, sky.height - 1);
            pixels[y * width + x]
                = waterfront_squall_sky(colors[sy * sky.width + sx], camera.position, ray, squall);
        }
    }
}

// Authored traffic follows retained Monsungatan and Ostindiefararen centreline vertices.
// Speeds, lane offsets, lamps and occupancy are artistic, not geographic source data.
static const Vector3 west_street[] = { { -99.4f, 3.56f, 308.7f }, { -86.9f, 3.56f, 186.7f },
    { -80.6f, 3.56f, 116.5f }, { -73.9f, 3.56f, 53.6f }, { -68.8f, 3.56f, -5.7f },
    { -64.8f, 3.56f, -35.8f }, { -61.5f, 3.56f, -69.6f } };
static const Vector3 east_street[] = { { 67.4f, 3.56f, 406.2f }, { 82.5f, 3.56f, 269.5f },
    { 92.7f, 3.56f, 167.2f }, { 95.6f, 3.56f, 133.7f }, { 103.7f, 3.56f, 67.1f } };

static void evaluate_life(
    double time, float (*ground)(float x, float z), WaterfrontSceneFrame *frame)
{
    static const Color colors[] = { { 153, 69, 57, 255 }, { 170, 178, 181, 255 },
        { 51, 88, 117, 255 }, { 183, 142, 72, 255 } };
    frame->lamp_count = 0;
    for (int street = 0; street < 2; ++street) {
        WaterfrontStreet route = waterfront_scene_street(street);
        float length = 0;
        for (int i = 1; i < route.point_count; ++i)
            length += Vector3Distance(route.points[i - 1], route.points[i]);
        int lamp_index = 0;
        for (float distance = 20; distance < length - 12; distance += 27, ++lamp_index) {
            // Capacity covers the two authored routes; validated by the scene test.
            if (frame->lamp_count == WATERFRONT_MAX_LAMPS)
                break;
            Vector3 direction;
            Vector3 p = waterfront_scene_street_position(route, distance, &direction);
            Vector3 side = { direction.z, 0, -direction.x };
            p = Vector3Add(p, Vector3Scale(side, 2.6f));
            float lift = ground ? ground(p.x, p.z) - 3.5f : 0;
            p.y += lift;
            frame->lamps[frame->lamp_count++]
                = (WaterfrontLampState) { .id = 100 + street * 100 + lamp_index,
                      .street = street,
                      .position = p,
                      .pool_direction = Vector3Negate(side),
                      .pole = { .position = { p.x, 6 + lift, p.z }, .scale = { .16f, 5, .16f } },
                      .lens = { p.x, 8.5f + lift, p.z } };
        }
        for (int car = 0; car < 10; ++car) {
            int sign = car % 2 ? 1 : -1;
            // Reduce lane travel in double, then keep the original phase, lane and float wrap.
            float travel = boat_cycle(5.5f + street, time, length);
            float distance = fmodf(car * length / 10 + travel * sign + length, length);
            if (distance < 0)
                distance += length;
            Vector3 forward;
            Vector3 p = waterfront_scene_street_position(route, distance, &forward);
            forward = Vector3Scale(forward, (float)sign);
            Vector3 right = { forward.z, 0, -forward.x };
            p = Vector3Add(p, Vector3Scale(right, 1.25f));
            float lift = ground ? ground(p.x, p.z) - 3.5f : 0;
            p.y += lift;
            float yaw = atan2f(forward.x, forward.z);
            WaterfrontCarState *out = &frame->cars[street * 10 + car];
            *out = (WaterfrontCarState) { .id = 1000 + street * 100 + car * 2,
                .street = street,
                .position = p,
                .forward = forward,
                .right = right,
                .body = { .position = { p.x, 4.2f + lift, p.z },
                    .rotation = { 0, yaw, 0 },
                    .scale = { 1.7f, .9f, 3.9f } },
                .cabin = { .position = { p.x, 4.9f + lift, p.z },
                    .rotation = { 0, yaw, 0 },
                    .scale = { 1.4f, .65f, 1.9f } },
                .color = colors[car % 4],
                .headlight_direction
                = Vector3Normalize(Vector3Add(forward, (Vector3) { 0, -.22f, 0 })),
                .pool_position = Vector3Add(p, Vector3Scale(forward, 2.05f)) };
            for (int end = -1; end <= 1; end += 2)
                for (int side = -1; side <= 1; side += 2) {
                    Vector3 lens = Vector3Add(p,
                        Vector3Add(
                            Vector3Scale(forward, end * 2.0f), Vector3Scale(right, side * .6f)));
                    lens.y = 4.3f + lift;
                    out->lenses[end > 0][side > 0] = lens;
                }
        }
    }
}

static void evaluate_actors(const BoatSceneDefinition *scene, double time, BoatWater water,
    float (*ground)(float x, float z), WaterfrontSceneFrame *frame)
{
    frame->time = time;
    evaluate_life(time, ground, frame);
    waterfront_harbor_evaluate_scene(scene, water, time, ground, &frame->harbor);
    waterfront_traffic_evaluate_routes(
        scene && scene->traffic_present ? &scene->traffic : NULL, water, time, &frame->traffic);
}

void waterfront_scene_evaluate_actors(double time, BoatWater water, WaterfrontSceneFrame *frame)
{
    evaluate_actors(NULL, time, water, NULL, frame);
}

void waterfront_scene_evaluate(double time, BoatWater water, WaterfrontSceneFrame *frame)
{
    waterfront_scene_evaluate_ground(time, water, NULL, frame);
}

void waterfront_scene_evaluate_ground(
    double time, BoatWater water, float (*ground)(float x, float z), WaterfrontSceneFrame *frame)
{
    waterfront_scene_evaluate_ground_scene(NULL, time, water, ground, frame);
}

void waterfront_scene_evaluate_scene(
    const BoatSceneDefinition *scene, double time, BoatWater water, WaterfrontSceneFrame *frame)
{
    waterfront_scene_evaluate_ground_scene(scene, time, water, NULL, frame);
}

void waterfront_scene_evaluate_ground_scene(const BoatSceneDefinition *scene, double time,
    BoatWater water, float (*ground)(float x, float z), WaterfrontSceneFrame *frame)
{
    *frame = (WaterfrontSceneFrame) { 0 };
    frame->bridge = (Transform3D) {
        .position = { 11, 7, 126 }, .rotation = { 0, -.08f, 0 }, .scale = { 68, 1, 3 }
    };
    frame->bridge_color = (Color) { 119, 128, 145, 255 };
    evaluate_actors(scene, time, water, ground, frame);
}

WaterfrontStreet waterfront_scene_street(int index)
{
    return index
        ? (WaterfrontStreet) { east_street, (int)(sizeof(east_street) / sizeof(east_street[0])) }
        : (WaterfrontStreet) { west_street, (int)(sizeof(west_street) / sizeof(west_street[0])) };
}

Vector3 waterfront_scene_street_position(
    WaterfrontStreet street, float distance, Vector3 *direction)
{
    for (int i = 1; i < street.point_count; ++i) {
        Vector3 delta = Vector3Subtract(street.points[i], street.points[i - 1]);
        float length = Vector3Length(delta);
        if (distance <= length || i == street.point_count - 1) {
            *direction = Vector3Scale(delta, 1 / length);
            return Vector3Add(street.points[i - 1], Vector3Scale(*direction, distance));
        }
        distance -= length;
    }
    return street.points[0];
}

// A sparse, low-poly gull: pointed body, swept wings, and charcoal outer feathers.
// Nose is local +X so wingbeats/banking rotate around X before heading around Y.
const Mesh3D *waterfront_gull_mesh(int part)
{
    static const Vector3 body[] = { { .65f, 0, 0 }, { -.55f, 0, 0 }, { 0, .16f, 0 },
        { 0, -.13f, 0 }, { 0, 0, .14f }, { 0, 0, -.14f } };
    static const Triangle3D body_faces[] = {
        { { 0, 2, 4 }, { 226, 225, 216, 255 } },
        { { 0, 5, 2 }, { 226, 225, 216, 255 } },
        { { 0, 4, 3 }, { 183, 190, 197, 255 } },
        { { 0, 3, 5 }, { 183, 190, 197, 255 } },
        { { 1, 4, 2 }, { 226, 225, 216, 255 } },
        { { 1, 2, 5 }, { 226, 225, 216, 255 } },
        { { 1, 3, 4 }, { 183, 190, 197, 255 } },
        { { 1, 5, 3 }, { 183, 190, 197, 255 } },
    };
    static const Vector3 left[] = { { .18f, 0, 0 }, { -.3f, 0, 0 }, { .08f, 0, -.8f },
        { -.34f, 0, -.9f }, { -.45f, 0, -1.35f } };
    static const Vector3 right[] = { { .18f, 0, 0 }, { -.3f, 0, 0 }, { .08f, 0, .8f },
        { -.34f, 0, .9f }, { -.45f, 0, 1.35f } };
    // Both sides are explicit so wings remain visible through a bank or upstroke.
    static const Triangle3D wing_faces[] = {
        { { 0, 1, 2 }, { 220, 222, 217, 255 } },
        { { 2, 1, 0 }, { 185, 195, 204, 255 } },
        { { 1, 3, 2 }, { 220, 222, 217, 255 } },
        { { 2, 3, 1 }, { 185, 195, 204, 255 } },
        { { 2, 3, 4 }, { 54, 63, 76, 255 } },
        { { 4, 3, 2 }, { 54, 63, 76, 255 } },
    };
    static const Mesh3D meshes[] = {
        { body, body_faces, sizeof(body_faces) / sizeof(body_faces[0]) },
        { left, wing_faces, sizeof(wing_faces) / sizeof(wing_faces[0]) },
        { right, wing_faces, sizeof(wing_faces) / sizeof(wing_faces[0]) },
    };
    return &meshes[part];
}

// Four little communities rather than one rigid formation. Each bird follows the same
// circling/feeding cue a little late, so turns and alarm climbs ripple through the flock.
// Everything is sampled analytically: no random generator, integration, or seek history.
// Each periodic term reduces its own phase in double before float trigonometry.
static Vector3 harbor_gull_path(int index, double time, float *alarm)
{
    int group = index / 8, bird = index % 8;
    static const Vector3 roosts[]
        = { { 15, 0, 38 }, { 16, 0, -20 }, { -70, 0, -245 }, { 135, 0, -235 } };
    double social_time = time - bird * .21f;
    float cue = .5f + .5f * sinf(boat_phase(.31f, social_time) + group * 1.9f);
    // A brief imagined splash/food discovery, then a slower return to the thermal.
    float feeding = powf(cue, 6);
    float startle = fmaxf(0, sinf(boat_phase(.47f, social_time) + group * 2.3f));
    *alarm = powf(startle, 8);
    float circling = (.29f + group * .025f) * (group % 2 ? -1 : 1);
    float angle = boat_phase(circling, time) + bird * (2 * PI / 8)
        + .14f * sinf(boat_phase(.61f, social_time) + group)
        + .18f * sinf(boat_phase(.23f, time) + bird * 1.3f);
    float radius = 13 + (bird % 3) * 3.2f + 5 * feeding + 7 * *alarm
        + 1.2f * sinf(boat_phase(.37f, time) + bird * 2.1f);
    Vector3 center = roosts[group];
    if (group == 2) {
        // Opportunists wheel behind the cruiser, drawn to its disturbed water.
        WaterfrontVesselMotion boat = waterfront_traffic_motion(1, time);
        center = Vector3Subtract(boat.position, Vector3Scale(Vector3Normalize(boat.velocity), 18));
    }
    return (Vector3) { center.x + radius * cosf(angle),
        5 + group * 2 + (bird % 3) * 1.1f + 1.1f * sinf(boat_phase(.83f, time) + bird * 1.7f)
            - 2.5f * feeding + 6 * *alarm,
        center.z + radius * .75f * sinf(angle) };
}

static Vector3 gull_avoid(Vector3 p, Vector3 source, float radius, float strength, float *alarm)
{
    // Smooth finite response even when directly overhead; horizontal distance is intentional.
    Vector3 away = { p.x - source.x, 0, p.z - source.z };
    float distance2 = Vector3LengthSqr(away);
    float response = expf(-distance2 / (radius * radius));
    *alarm = fmaxf(*alarm, response * strength);
    p = Vector3Add(p, Vector3Scale(away, response * strength * .4f));
    p.y += response * strength * 7;
    return p;
}

static Vector3 harbor_gull_position(
    int index, double time, Vector3 player, Vector3 velocity, float *alarm)
{
    Vector3 p = harbor_gull_path(index, time, alarm);
    // Soft personal space from immediate flockmates, evaluated from unmodified paths so
    // evaluation order cannot change the result. Coherent paths supply alignment/cohesion.
    Vector3 separation = { 0 };
    for (int neighbor = index / 8 * 8; neighbor < index / 8 * 8 + 8; ++neighbor) {
        if (neighbor == index)
            continue;
        float ignored;
        Vector3 delta = Vector3Subtract(p, harbor_gull_path(neighbor, time, &ignored));
        float distance2 = Vector3LengthSqr(delta);
        separation = Vector3Add(separation, Vector3Scale(delta, 2 * expf(-distance2 / 16)));
    }
    p = Vector3Add(p, separation);
    float speed = Vector3Length(velocity);
    // Look ahead of an approaching player; faster engines provoke a wider, stronger climb.
    p = gull_avoid(p, Vector3Add(player, Vector3Scale(velocity, .8f)), 10 + fminf(speed, 12),
        .45f + fminf(speed * .08f, .9f), alarm);
    for (int vessel = 0; vessel < WATERFRONT_TRAFFIC_VESSELS; ++vessel) {
        WaterfrontVesselMotion boat = waterfront_traffic_motion(vessel, time);
        p = gull_avoid(p, boat.position, vessel == 3 ? 23 : 9, .65f, alarm);
    }
    return p;
}

void waterfront_boating_gulls(
    double time, Vector3 player, Vector3 velocity, WaterfrontSceneFrame *frame)
{
    frame->gull_count = WATERFRONT_HARBOR_GULLS;
    for (int i = 0; i < frame->gull_count; ++i) {
        float alarm, ignored;
        Vector3 p = harbor_gull_position(i, time, player, velocity, &alarm);
        // Match orientation to the curved flight, including climbs and dives. Extrapolate
        // the player's pose too, avoiding artificial turns when sampling a moving stimulus.
        const float dt = .06f;
        Vector3 before = harbor_gull_position(
            i, time - dt, Vector3Subtract(player, Vector3Scale(velocity, dt)), velocity, &ignored);
        Vector3 after = harbor_gull_position(
            i, time + dt, Vector3Add(player, Vector3Scale(velocity, dt)), velocity, &ignored);
        Vector3 forward = Vector3Normalize(Vector3Subtract(after, before));
        Vector3 incoming = Vector3Normalize(Vector3Subtract(p, before));
        Vector3 outgoing = Vector3Normalize(Vector3Subtract(after, p));
        float yaw = atan2f(-forward.z, forward.x);
        float pitch = asinf(Clamp(forward.y, -.8f, .8f));
        float bank = Clamp(-Vector3CrossProduct(incoming, outgoing).y * 22, -.65f, .65f);
        // Wingbeat cycles per second become one reduced angular phase.
        float beat = boat_phase(BOAT_TWO_PI * (1.7f + (i % 5) * .09f), time);
        float phase = beat + i * 1.37f * 2 * PI;
        float effort = .5f + .5f * sinf(boat_phase(.72f, time) + i * 2.1f);
        effort = Clamp((effort - .58f) * 2.4f + alarm * .9f + fmaxf(0, forward.y), 0, 1);
        float flap = .65f * effort * sinf(phase);
        float size = .78f + (i % 5) * .045f;
        for (int part = 0; part < WATERFRONT_GULL_PARTS; ++part) {
            // Slight asymmetric wing trim makes gliding birds look less mechanical.
            float trim = .035f * sinf(boat_phase(1.9f, time) + i);
            float wing = part == 1 ? -.12f - flap + trim : part == 2 ? .12f + flap + trim : 0;
            frame->gulls[i][part] = (Transform3D) { .position = p,
                .rotation = { bank + wing, yaw, pitch },
                .scale = { size, size, size } };
        }
    }
}
