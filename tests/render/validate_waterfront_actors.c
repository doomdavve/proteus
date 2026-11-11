#include "waterfront_scene.h"

#include "raymath.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool vector_equal(Vector3 a, Vector3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

static bool transform_equal(Transform3D a, Transform3D b)
{
    return vector_equal(a.position, b.position) && vector_equal(a.rotation, b.rotation)
        && vector_equal(a.scale, b.scale);
}

// Independent copy of the original street equations: catches phase, lane, orientation,
// light attachment, and wrap changes rather than merely repeating the new evaluator.
// Since C2, each lane's travel is reduced in double before the original float wrap.
static bool legacy_matches(const WaterfrontSceneFrame *frame)
{
    const Color colors[] = { { 153, 69, 57, 255 }, { 170, 178, 181, 255 }, { 51, 88, 117, 255 },
        { 183, 142, 72, 255 } };
    int lamp_count = 0;
    for (int street = 0; street < 2; ++street) {
        WaterfrontStreet route = waterfront_scene_street(street);
        float length = 0;
        for (int j = 1; j < route.point_count; ++j)
            length += Vector3Distance(route.points[j - 1], route.points[j]);
        int lamp_index = 0;
        for (float distance = 20; distance < length - 12; distance += 27, ++lamp_index) {
            if (lamp_count >= frame->lamp_count)
                return false;
            const WaterfrontLampState *lamp = &frame->lamps[lamp_count++];
            if (distance < 0)
                distance += length;
            Vector3 forward;
            Vector3 p = waterfront_scene_street_position(route, distance, &forward);
            Vector3 right = { forward.z, 0, -forward.x };
            p = Vector3Add(p, Vector3Scale(right, 2.6f));
            if (lamp->id != 100 + street * 100 + lamp_index || lamp->street != street
                || !vector_equal(lamp->position, p)
                || !vector_equal(lamp->pool_direction, Vector3Negate(right))
                || !vector_equal(lamp->lens, (Vector3) { p.x, 8.5f, p.z })
                || !transform_equal(lamp->pole,
                    (Transform3D) { .position = { p.x, 6, p.z }, .scale = { .16f, 5, .16f } }))
                return false;
        }
        for (int i = 0; i < 10; ++i) {
            const WaterfrontCarState *car = &frame->cars[street * 10 + i];
            int sign = i % 2 ? 1 : -1;
            float travel = (float)remainder(frame->time * (5.5f + street), length);
            float distance = fmodf(i * length / 10 + travel * sign + length, length);
            if (distance < 0)
                distance += length;
            Vector3 forward;
            Vector3 p = waterfront_scene_street_position(route, distance, &forward);
            forward = Vector3Scale(forward, (float)sign);
            Vector3 right = { forward.z, 0, -forward.x };
            p = Vector3Add(p, Vector3Scale(right, 1.25f));
            float yaw = atan2f(forward.x, forward.z);
            if (car->id != 1000 + street * 100 + i * 2 || car->street != street
                || !vector_equal(car->position, p) || !vector_equal(car->forward, forward)
                || !vector_equal(car->right, right) || !ColorIsEqual(car->color, colors[i % 4])
                || !transform_equal(car->body,
                    (Transform3D) { .position = { p.x, 4.2f, p.z },
                        .rotation = { 0, yaw, 0 },
                        .scale = { 1.7f, .9f, 3.9f } })
                || !transform_equal(car->cabin,
                    (Transform3D) { .position = { p.x, 4.9f, p.z },
                        .rotation = { 0, yaw, 0 },
                        .scale = { 1.4f, .65f, 1.9f } })
                || !vector_equal(car->pool_position, Vector3Add(p, Vector3Scale(forward, 2.05f)))
                || !vector_equal(car->headlight_direction,
                    Vector3Normalize(Vector3Add(forward, (Vector3) { 0, -.22f, 0 }))))
                return false;
            for (int end = -1; end <= 1; end += 2)
                for (int side = -1; side <= 1; side += 2) {
                    Vector3 lens = Vector3Add(p,
                        Vector3Add(
                            Vector3Scale(forward, end * 2.0f), Vector3Scale(right, side * .6f)));
                    lens.y = 4.3f;
                    if (!vector_equal(car->lenses[end > 0][side > 0], lens))
                        return false;
                }
        }
    }
    if (frame->lamp_count != lamp_count || lamp_count != 25)
        return false;
    return true;
}

static void hash_float(uint64_t *hash, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    *hash = (*hash ^ bits) * UINT64_C(1099511628211);
}

static void hash_vector(uint64_t *hash, Vector3 v)
{
    hash_float(hash, v.x);
    hash_float(hash, v.y);
    hash_float(hash, v.z);
}

static void hash_transform(uint64_t *hash, Transform3D t)
{
    hash_vector(hash, t.position);
    hash_vector(hash, t.rotation);
    hash_vector(hash, t.scale);
}

// Hash fields, not struct padding. These are same-run comparisons, not platform baselines.
static uint64_t snapshot_hash(const WaterfrontSceneFrame *f)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    hash_float(&hash, f->time);
    hash_vector(&hash, f->camera.position);
    hash_vector(&hash, f->camera.target);
    hash_vector(&hash, f->camera.up);
    hash_float(&hash, f->camera.vertical_fov);
    hash_float(&hash, f->camera.near_plane);
    hash_float(&hash, f->camera.far_plane);
    hash_transform(&hash, f->bridge);
    for (int i = 0; i < WATERFRONT_CAR_COUNT; ++i) {
        const WaterfrontCarState *c = &f->cars[i];
        hash_float(&hash, (float)c->id);
        hash_float(&hash, (float)c->street);
        hash_vector(&hash, c->position);
        hash_vector(&hash, c->forward);
        hash_vector(&hash, c->right);
        hash_transform(&hash, c->body);
        hash_transform(&hash, c->cabin);
        hash_vector(&hash, c->pool_position);
        hash_vector(&hash, c->headlight_direction);
        hash_float(&hash, c->color.r);
        hash_float(&hash, c->color.g);
        hash_float(&hash, c->color.b);
        hash_float(&hash, c->color.a);
        for (int end = 0; end < 2; ++end)
            for (int side = 0; side < 2; ++side)
                hash_vector(&hash, c->lenses[end][side]);
    }
    hash_float(&hash, (float)f->lamp_count);
    for (int i = 0; i < f->lamp_count; ++i) {
        const WaterfrontLampState *l = &f->lamps[i];
        hash_float(&hash, (float)l->id);
        hash_float(&hash, (float)l->street);
        hash_vector(&hash, l->position);
        hash_vector(&hash, l->pool_direction);
        hash_vector(&hash, l->lens);
        hash_transform(&hash, l->pole);
    }
    hash_transform(&hash, f->harbor.pontoon);
    hash_transform(&hash, f->harbor.gangway);
    for (int i = 0; i < WATERFRONT_HARBOR_BOATS; ++i)
        hash_transform(&hash, f->harbor.boats[i]);
    for (int i = 0; i < f->harbor.line_count; ++i)
        hash_transform(&hash, f->harbor.lines[i]);
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
        const WaterfrontVesselState *v = &f->traffic.vessels[i];
        hash_float(&hash, (float)v->id);
        hash_transform(&hash, v->pose);
        hash_vector(&hash, v->motion.position);
        hash_vector(&hash, v->motion.velocity);
        hash_float(&hash, v->motion.heading);
        hash_float(&hash, v->motion.yaw_rate);
        for (int j = 0; j < WATERFRONT_TRAFFIC_LIGHTS; ++j)
            hash_vector(&hash, v->lights[j]);
    }
    hash_float(&hash, (float)f->traffic.wake_count);
    for (int i = 0; i < f->traffic.wake_count; ++i) {
        hash_transform(&hash, f->traffic.wakes[i].pose);
        Color c = f->traffic.wakes[i].color;
        hash_float(&hash, c.r);
        hash_float(&hash, c.g);
        hash_float(&hash, c.b);
    }
    hash_float(&hash, (float)f->gull_count);
    for (int i = 0; i < f->gull_count; ++i)
        for (int part = 0; part < WATERFRONT_GULL_PARTS; ++part)
            hash_transform(&hash, f->gulls[i][part]);
    return hash;
}

// Sweep the old 30.72 s storyboard span: legacy motion plus forward, backward and direct seeks.
static bool check_seeks(void)
{
    const float duration = 30.72f;
    int steps = (int)ceilf(duration * 120);
    uint64_t *hashes = calloc((size_t)steps + 1, sizeof(*hashes));
    if (!hashes)
        return false;
    bool ok = true;
    WaterfrontSceneFrame frame, other;
    for (int i = 0; i <= steps; ++i) {
        float time = duration * (float)i / (float)steps;
        waterfront_scene_evaluate(time, (BoatWater) { 0 }, &frame);
        if (!legacy_matches(&frame)) {
            fprintf(stderr, "Legacy actor mismatch at %.9g\n", time);
            ok = false;
            break;
        }
        hashes[i] = snapshot_hash(&frame);
    }
    for (int i = steps; ok && i >= 0; --i) {
        float time = duration * (float)i / (float)steps;
        waterfront_scene_evaluate(time, (BoatWater) { 0 }, &frame);
        // Interleave a distant seek; the retained snapshot must not mutate.
        waterfront_scene_evaluate(duration - time, (BoatWater) { 0 }, &other);
        if (snapshot_hash(&frame) != hashes[i])
            ok = false;
        waterfront_scene_evaluate(time, (BoatWater) { 0 }, &other);
        if (snapshot_hash(&other) != hashes[i])
            ok = false;
    }
    // Probe immediately around discontinuities as well as the regular sweep.
    for (int i = 0; i < 40 && ok; ++i) {
        float time = i * .7f;
        for (int side = -1; side <= 1; ++side) {
            waterfront_scene_evaluate(time + side * .0001f, (BoatWater) { 0 }, &frame);
            if (!legacy_matches(&frame))
                ok = false;
        }
    }
    free(hashes);
    return ok;
}

// Day-long boating sessions keep the lane equations and smooth per-tick car travel.
static bool check_long_session(void)
{
    const double epochs[] = { 3600.25, 86400.25, 30 * 86400.25 };
    WaterfrontSceneFrame frame, next;
    for (int e = 0; e < 3; ++e) {
        int stalls[WATERFRONT_CAR_COUNT] = { 0 };
        for (int tick = 0; tick < 240; ++tick) {
            double time = epochs[e] + tick / 120.0;
            waterfront_scene_evaluate_actors(time, (BoatWater) { 0 }, &frame);
            waterfront_scene_evaluate_actors(time + 1 / 120.0, (BoatWater) { 0 }, &next);
            if (frame.time != time || !legacy_matches(&frame))
                return false;
            for (int i = 0; i < WATERFRONT_CAR_COUNT; ++i) {
                // About 5 cm per tick. Only an end-of-street wrap may jump; a lane-offset
                // corner can briefly shorten one step. Float time would stall every ~15 ticks
                // at one day and jump about 1.5 m at thirty days.
                float step = Vector3Distance(frame.cars[i].position, next.cars[i].position);
                if (step > .2f && step < 20)
                    return false;
                stalls[i] += step < .01f;
            }
        }
        for (int i = 0; i < WATERFRONT_CAR_COUNT; ++i)
            if (stalls[i] > 4)
                return false;
    }
    return true;
}

int main(void)
{
    if (!check_seeks() || !check_long_session()) {
        fputs("Actor motion or seek regression failed\n", stderr);
        return 1;
    }
    puts("Car lanes match the reduced-time equations at 1 hour, 24 hours, and 30 days.");
    printf("Actor snapshots match legacy motion and forward/backward/direct seeks.\n"
           "%d cars, 25 lamps, %d travelling vessels, %d wake patches; snapshot=%zu bytes.\n",
        WATERFRONT_CAR_COUNT, WATERFRONT_TRAFFIC_VESSELS, WATERFRONT_TRAFFIC_WAKES,
        sizeof(WaterfrontSceneFrame));
    return 0;
}
