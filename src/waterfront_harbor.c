#include "waterfront_harbor.h"

#include "raymath.h"

#include <math.h>

// Artistic berth placements beside mapped quays, not surveyed marina facilities.
// The central basin axis and the existing launch's X=12 route remain open.
static const WaterfrontBerth berths[WATERFRONT_HARBOR_BOATS] = {
    { BOAT_MODEL_SKIFF, { -13, 0, 167 }, -PI / 2 },
    { BOAT_MODEL_RUNABOUT, { -13, 0, 173 }, -PI / 2 },
    { BOAT_MODEL_SAILBOAT, { -13, 0, 179 }, -PI / 2 },
    { BOAT_MODEL_CRUISER, { -13, 0, 185 }, -PI / 2 },
    { BOAT_MODEL_SPEEDBOAT, { -13, 0, 191 }, -PI / 2 },
    { BOAT_MODEL_SAILBOAT, { -13, 0, 197 }, -PI / 2 },
    { BOAT_MODEL_WORKBOAT, { 44, 0, 35 }, -.10f },
};
static const Vector3 dock_center = { -20, 0, 182 };
static const Color timber = { 154, 123, 84, 255 };
static const Color rubber = { 44, 52, 55, 255 };
static const Color steel = { 128, 144, 146, 255 };
static BoatModel boats[WATERFRONT_HARBOR_BOATS], pontoon, quay;
static bool initialized;

const WaterfrontBerth *waterfront_harbor_berths(void) { return berths; }

BoatModelSpec waterfront_harbor_spec(int index)
{
    if (index < 0 || index >= WATERFRONT_HARBOR_BOATS)
        return (BoatModelSpec) { 0 };
    BoatModelSpec s = boat_model_preset(berths[index].kind);
    s.fenders = true;
    if (index == 5) {
        s.length = 7;
        s.beam = 2.35f;
        s.mast_height = 7.5f;
        s.cabin_length = 2.5f;
        s.hull = (Color) { 229, 223, 203, 255 };
        s.trim = (Color) { 75, 117, 108, 255 };
    }
    return s;
}

static void initialize(void)
{
    if (initialized)
        return;
    initialized = true;
    for (int i = 0; i < WATERFRONT_HARBOR_BOATS; ++i)
        boat_model_generate(&boats[i], waterfront_harbor_spec(i));
    boat_model_begin(&pontoon);
    boat_model_box(&pontoon, (Vector3) { 0, .26f, 0 }, (Vector3) { 2.1f, .7f, 37 }, rubber);
    // Individual broad planks and exposed dark floats, rather than a featureless slab.
    for (int i = 0; i < 56; ++i)
        boat_model_box(&pontoon, (Vector3) { 0, .65f, -18.15f + i * .66f },
            (Vector3) { 2.35f, .12f, .62f }, timber);
    for (int i = 0; i < 3; ++i) {
        float z = -12 + i * 12;
        boat_model_box(&pontoon, (Vector3) { 4.6f, .35f, z }, (Vector3) { 7, .45f, .85f }, rubber);
        boat_model_box(&pontoon, (Vector3) { 4.6f, .65f, z }, (Vector3) { 7.25f, .12f, 1 }, timber);
        boat_model_box(
            &pontoon, (Vector3) { 7.8f, .83f, z }, (Vector3) { .14f, .26f, .14f }, steel);
        boat_model_box(
            &pontoon, (Vector3) { 7.8f, .99f, z }, (Vector3) { .16f, .10f, .34f }, steel);
    }
    for (int i = 0; i < 7; ++i) {
        float z = -18 + i * 6;
        boat_model_box(
            &pontoon, (Vector3) { 1.05f, .87f, z }, (Vector3) { .14f, .35f, .14f }, steel);
        boat_model_box(
            &pontoon, (Vector3) { 1.05f, 1.04f, z }, (Vector3) { .16f, .10f, .38f }, steel);
    }
    boat_model_begin(&quay);
    // East quay working berth: grounded gear, a coiled hose and an equipment stack.
    for (int i = 0; i < 3; ++i)
        boat_model_box(&quay, (Vector3) { 50.7f, 3.85f + i * .48f, 37.8f },
            (Vector3) { 1.4f, .46f, 1.1f }, (Color) { 156, 112, 64, 255 });
    boat_model_box(&quay, (Vector3) { 49.6f, 4.05f, 32 }, (Vector3) { .85f, 1.1f, .85f },
        (Color) { 57, 102, 111, 255 });
    for (int i = 0; i < 12; ++i) {
        float a = i * PI / 6, b = (i + 1) * PI / 6;
        boat_model_beam(&quay, (Vector3) { 50 + .8f * cosf(a), 3.60f, 34 + .8f * sinf(a) },
            (Vector3) { 50 + .8f * cosf(b), 3.60f, 34 + .8f * sinf(b) }, .11f, rubber);
    }
    for (int i = 0; i < 2; ++i) {
        float z = 30 + i * 10;
        float x = 47.27f - .099f * (z - 30);
        boat_model_box(&quay, (Vector3) { x, 3.75f, z }, (Vector3) { .4f, .5f, .4f }, steel);
        boat_model_box(&quay, (Vector3) { x, 4, z }, (Vector3) { .7f, .12f, .4f }, steel);
    }
}

const Mesh3D *waterfront_harbor_boat_mesh(int index)
{
    if (index < 0 || index >= WATERFRONT_HARBOR_BOATS)
        return NULL;
    initialize();
    return &boats[index].mesh;
}
const Mesh3D *waterfront_harbor_pontoon_mesh(void)
{
    initialize();
    return &pontoon.mesh;
}
const Mesh3D *waterfront_harbor_quay_mesh(void)
{
    initialize();
    return &quay.mesh;
}

Vector3 waterfront_harbor_attachment(Transform3D pose, Vector3 local)
{
    return boat_model_point(pose, local);
}

static Transform3D segment(Vector3 a, Vector3 b, float width)
{
    Vector3 forward = Vector3Normalize(Vector3Subtract(b, a));
    Vector3 reference = fabsf(forward.y) > .9f ? (Vector3) { 1, 0, 0 } : (Vector3) { 0, 1, 0 };
    Vector3 right = Vector3Normalize(Vector3CrossProduct(reference, forward));
    Vector3 up = Vector3CrossProduct(forward, right);
    Matrix r = { .m0 = right.x,
        .m1 = right.y,
        .m2 = right.z,
        .m4 = up.x,
        .m5 = up.y,
        .m6 = up.z,
        .m8 = forward.x,
        .m9 = forward.y,
        .m10 = forward.z,
        .m15 = 1 };
    return (Transform3D) { .position = Vector3Scale(Vector3Add(a, b), .5f),
        .rotation = boat_model_rotation(r),
        .scale = { width, width, Vector3Distance(a, b) } };
}

static void mooring(WaterfrontHarborFrame *frame, Vector3 a, Vector3 b)
{
    Vector3 middle = Vector3Scale(Vector3Add(a, b), .5f);
    middle.y -= .18f;
    frame->lines[frame->line_count++] = segment(a, middle, .055f);
    frame->lines[frame->line_count++] = segment(middle, b, .055f);
}

void waterfront_harbor_evaluate(BoatWater water, double time, WaterfrontHarborFrame *frame)
{
    waterfront_harbor_evaluate_ground(water, time, NULL, frame);
}

void waterfront_harbor_evaluate_ground(
    BoatWater water, double time, float (*ground)(float x, float z), WaterfrontHarborFrame *frame)
{
    *frame = (WaterfrontHarborFrame) { 0 };
    frame->pontoon = boat_water_pose(water, time, dock_center, 0, 37, 2.1f);
    Vector3 shore = { -34.1f, 3.57f, 182 };
    // The quay gear stands around (50, 35); the river kit's ground is 3.5 m.
    float quay_lift = ground ? ground(50, 35) - 3.5f : 0;
    if (ground)
        shore.y += ground(shore.x, shore.z) - 3.5f;
    frame->quay = (Transform3D) { .position = { 0, quay_lift, 0 }, .scale = { 1, 1, 1 } };
    Vector3 landing = waterfront_harbor_attachment(frame->pontoon, (Vector3) { -1, .74f, 0 });
    frame->gangway = segment(shore, landing, 1.5f);
    frame->gangway.scale.y = .18f;
    for (int side = -1; side <= 1; side += 2) {
        Vector3 a = shore, b = landing;
        a.y += .95f;
        b.y += .95f;
        a.z += side * .64f;
        b.z += side * .64f;
        frame->lines[frame->line_count++] = segment(a, b, .065f);
        frame->lines[frame->line_count++]
            = segment(Vector3Subtract(a, (Vector3) { 0, .95f, 0 }), a, .065f);
        frame->lines[frame->line_count++]
            = segment(Vector3Subtract(b, (Vector3) { 0, .95f, 0 }), b, .065f);
    }
    for (int i = 0; i < WATERFRONT_HARBOR_BOATS; ++i) {
        BoatModelSpec spec = waterfront_harbor_spec(i);
        frame->boats[i] = boat_water_pose(
            water, time, berths[i].position, berths[i].heading, spec.length, spec.beam);
        for (int end = -1; end <= 1; end += 2) {
            Vector3 attachment = waterfront_harbor_attachment(frame->boats[i],
                (Vector3) { spec.beam * .39f, spec.freeboard + .10f, end * spec.length * .24f });
            Vector3 anchor;
            if (i < 6) {
                anchor = waterfront_harbor_attachment(frame->pontoon,
                    end > 0 ? (Vector3) { 1.05f, 1.04f, -18 + i * 6 }
                            : (Vector3) { 7.8f, 1.04f, -12 + (i / 2) * 12 });
            } else {
                float z = 35 + end * 5;
                anchor = (Vector3) { 47.27f - .099f * (z - 30), 4.02f + quay_lift, z };
            }
            mooring(frame, attachment, anchor);
        }
    }
}

// Conservative OBBs use rest poses, padded by 0.45 m for modest wave attitude and fenders.
static bool obstacle(
    Vector2 p, Vector2 center, Vector2 half, float yaw, float clearance, Vector2 *normal)
{
    Vector2 delta = Vector2Subtract(p, center);
    Vector2 q
        = { cosf(yaw) * delta.x - sinf(yaw) * delta.y, sinf(yaw) * delta.x + cosf(yaw) * delta.y };
    half.x += clearance;
    half.y += clearance;
    if (fabsf(q.x) >= half.x || fabsf(q.y) >= half.y)
        return false;
    Vector2 n = half.x - fabsf(q.x) < half.y - fabsf(q.y) ? (Vector2) { q.x < 0 ? -1 : 1, 0 }
                                                          : (Vector2) { 0, q.y < 0 ? -1 : 1 };
    if (normal)
        *normal
            = (Vector2) { cosf(yaw) * n.x + sinf(yaw) * n.y, -sinf(yaw) * n.x + cosf(yaw) * n.y };
    return true;
}

bool waterfront_harbor_contact(Vector2 position, float clearance, Vector2 *normal)
{
    return waterfront_harbor_contact_height(
        position, clearance, -INFINITY, INFINITY, (BoatWater) { 0 }, 0, normal);
}

bool waterfront_harbor_contact_height(Vector2 position, float clearance, float bottom, float top,
    BoatWater water, double time, Vector2 *normal)
{
    Vector2 p = { -position.x, position.y }, world_normal;
    bool hit = false;
    for (int i = 0; i < WATERFRONT_HARBOR_BOATS && !hit; ++i) {
        BoatModelSpec s = waterfront_harbor_spec(i);
        hit = obstacle(p, (Vector2) { berths[i].position.x, berths[i].position.z },
            (Vector2) { s.beam * .5f + .45f, s.length * .5f + .9f }, berths[i].heading, clearance,
            &world_normal);
        if (hit && (isfinite(bottom) || isfinite(top))) {
            Transform3D pose = boat_water_pose(
                water, time, berths[i].position, berths[i].heading, s.length, s.beam);
            float low, high;
            boat_model_vertical_bounds(s, pose, &low, &high);
            hit = bottom <= high && top >= low;
        }
    }
    bool dock_overlap = fabsf(p.x - dock_center.x) < 12 + clearance
        && fabsf(p.y - dock_center.z) < 21 + clearance;
    if (dock_overlap && (isfinite(bottom) || isfinite(top))) {
        Transform3D pose = boat_water_pose(water, time, dock_center, 0, 37, 2.1f);
        float low, high;
        boat_model_vertical_bounds(
            (BoatModelSpec) { .length = 37, .beam = 2.35f, .draft = .4f, .freeboard = .2f }, pose,
            &low, &high);
        dock_overlap = bottom <= high && top >= low;
    }
    if (!hit && dock_overlap)
        hit = obstacle(p, (Vector2) { dock_center.x, dock_center.z }, (Vector2) { 1.6f, 19 }, 0,
            clearance, &world_normal);
    for (int i = 0; i < 3 && !hit && dock_overlap; ++i)
        hit = obstacle(p, (Vector2) { dock_center.x + 4.6f, dock_center.z - 12 + i * 12 },
            (Vector2) { 4.1f, 1 }, 0, clearance, &world_normal);
    if (!hit && bottom <= 4.5f && top >= -.5f)
        hit = obstacle(
            p, (Vector2) { -27.55f, 182 }, (Vector2) { 7.0f, 1.1f }, 0, clearance, &world_normal);
    if (hit && normal)
        *normal = (Vector2) { -world_normal.x, world_normal.y };
    return hit;
}

bool waterfront_harbor_camera_clear(Vector3 p, float clearance)
{
    for (int i = 0; i < WATERFRONT_HARBOR_BOATS; ++i) {
        BoatModelSpec s = waterfront_harbor_spec(i);
        float top = fmaxf(s.mast_height, s.freeboard + s.cabin_height + .8f);
        if (p.y < top + clearance && p.y > -s.draft - clearance
            && obstacle((Vector2) { p.x, p.z },
                (Vector2) { berths[i].position.x, berths[i].position.z },
                (Vector2) { s.beam * .5f + .5f, s.length * .5f + 1 }, berths[i].heading, clearance,
                NULL))
            return false;
    }
    if (p.y < 4.5f + clearance && p.y > -.5f - clearance
        && waterfront_harbor_contact((Vector2) { -p.x, p.z }, clearance, NULL))
        return false;
    return true;
}
