#include "boat_model.h"

#include "raymath.h"

#include <math.h>

static const Color glass = { 47, 110, 132, 255 };
static const Color rubber = { 39, 48, 53, 255 };
static const Color cream = { 232, 226, 202, 255 };
static const Color steel = { 142, 158, 163, 255 };

Vector3 boat_model_rotation(Matrix r)
{
    float y = asinf(Clamp(r.m8, -1, 1));
    if (fabsf(cosf(y)) < .0001f)
        return (Vector3) { atan2f(r.m6, r.m5), y, 0 };
    return (Vector3) { atan2f(-r.m9, r.m10), y, atan2f(-r.m4, r.m0) };
}

Vector3 boat_model_point(Transform3D pose, Vector3 local)
{
    return Vector3Add(pose.position,
        Vector3Transform(Vector3Multiply(local, pose.scale), MatrixRotateXYZ(pose.rotation)));
}

void boat_model_begin(BoatModel *m)
{
    m->mesh = (Mesh3D) { m->vertices, m->triangles, 0 };
    m->overflow = false;
}

static void triangle(BoatModel *m, Vector3 a, Vector3 b, Vector3 c, Color color)
{
    if (m->mesh.triangle_count == BOAT_MODEL_MAX_TRIANGLES) {
        m->overflow = true;
        return;
    }
    unsigned int i = (unsigned int)m->mesh.triangle_count * 3;
    m->vertices[i] = a;
    m->vertices[i + 1] = b;
    m->vertices[i + 2] = c;
    m->triangles[m->mesh.triangle_count++] = (Triangle3D) { { i, i + 1, i + 2 }, color };
}

static void quad(BoatModel *m, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color)
{
    triangle(m, a, b, c, color);
    triangle(m, a, c, d, color);
}

void boat_model_quad(BoatModel *m, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color)
{
    quad(m, a, b, c, d, color);
}

static void box_vertices(BoatModel *m, const Vector3 *v, Color color)
{
    const int faces[][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 3, 7, 6, 2 },
        { 0, 4, 7, 3 }, { 1, 2, 6, 5 } };
    for (int i = 0; i < 6; ++i)
        quad(m, v[faces[i][0]], v[faces[i][1]], v[faces[i][2]], v[faces[i][3]], color);
}

void boat_model_box(BoatModel *m, Vector3 p, Vector3 size, Color color)
{
    const Vector3 corners[] = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
        { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } };
    Vector3 v[8];
    for (int i = 0; i < 8; ++i)
        v[i] = Vector3Add(p, Vector3Scale(Vector3Multiply(size, corners[i]), .5f));
    box_vertices(m, v, color);
}

void boat_model_beam(BoatModel *m, Vector3 a, Vector3 b, float width, Color color)
{
    Vector3 along = Vector3Normalize(Vector3Subtract(b, a));
    Vector3 ref = fabsf(along.y) > .9f ? (Vector3) { 1, 0, 0 } : (Vector3) { 0, 1, 0 };
    Vector3 side = Vector3Scale(Vector3Normalize(Vector3CrossProduct(ref, along)), width * .5f);
    Vector3 up = Vector3Scale(Vector3Normalize(Vector3CrossProduct(along, side)), width * .5f);
    Vector3 v[8];
    for (int i = 0; i < 8; ++i) {
        int j = i % 4;
        v[i] = Vector3Add(i < 4 ? a : b,
            Vector3Add(
                Vector3Scale(side, j == 0 || j == 3 ? -1 : 1), Vector3Scale(up, j < 2 ? -1 : 1)));
    }
    box_vertices(m, v, color);
}

BoatModelSpec boat_model_preset(BoatModelKind kind)
{
    BoatModelSpec s = { .length = 4.5f,
        .beam = 1.9f,
        .draft = .35f,
        .freeboard = .65f,
        .bow_width = .10f,
        .stern_width = .85f,
        .windshield_rake = .35f,
        .hull = { 64, 124, 122, 255 },
        .deck = { 184, 147, 98, 255 },
        .trim = { 234, 222, 190, 255 } };
    switch (kind) {
    case BOAT_MODEL_RUNABOUT:
        s.length = 5.8f;
        s.beam = 2.15f;
        s.cabin_length = 1.3f;
        s.cabin_height = .55f;
        s.cabin_offset = -.5f;
        s.engines = 1;
        s.hull = (Color) { 181, 66, 49, 255 };
        break;
    case BOAT_MODEL_CRUISER:
        s.length = 7.6f;
        s.beam = 2.8f;
        s.freeboard = .95f;
        s.cabin_length = 3.2f;
        s.cabin_height = 1.3f;
        s.cabin_offset = -.7f;
        s.engines = 1;
        s.hull = cream;
        s.trim = (Color) { 42, 82, 113, 255 };
        break;
    case BOAT_MODEL_SAILBOAT:
        s.length = 8.4f;
        s.beam = 2.65f;
        s.draft = .8f;
        s.freeboard = .8f;
        s.stern_width = .6f;
        s.cabin_length = 2.8f;
        s.cabin_height = .65f;
        s.cabin_offset = -.3f;
        s.mast_height = 9;
        s.hull = (Color) { 45, 72, 109, 255 };
        break;
    case BOAT_MODEL_WORKBOAT:
        s.length = 11;
        s.beam = 3.7f;
        s.draft = .85f;
        s.freeboard = 1.2f;
        s.bow_width = .32f;
        s.stern_width = 1;
        s.cabin_length = 3.6f;
        s.cabin_height = 1.9f;
        s.cabin_offset = -1.5f;
        s.windshield_rake = .10f;
        s.work_deck = true;
        s.hull = (Color) { 43, 83, 90, 255 };
        s.trim = (Color) { 222, 158, 58, 255 };
        break;
    case BOAT_MODEL_SPEEDBOAT:
        s.length = 8.1f;
        s.beam = 2.25f;
        s.freeboard = .6f;
        s.bow_width = .035f;
        s.cabin_length = 1.7f;
        s.cabin_height = .5f;
        s.cabin_offset = .1f;
        s.windshield_rake = .6f;
        s.engines = 2;
        s.hull = (Color) { 225, 228, 221, 255 };
        s.trim = (Color) { 203, 62, 40, 255 };
        break;
    case BOAT_MODEL_FERRY:
        s.length = 24;
        s.beam = 6.2f;
        s.draft = 1.2f;
        s.freeboard = 1.6f;
        s.bow_width = .25f;
        s.stern_width = .9f;
        s.cabin_length = 12;
        s.cabin_height = 2.4f;
        s.cabin_offset = -.6f;
        s.windshield_rake = .15f;
        s.passenger_deck = true;
        s.hull = (Color) { 222, 220, 201, 255 };
        s.trim = (Color) { 27, 85, 112, 255 };
        break;
    default:
        break;
    }
    return s;
}

static bool between(float value, float low, float high)
{
    return isfinite(value) && value >= low && value <= high;
}

bool boat_model_generate(BoatModel *m, BoatModelSpec s)
{
    if (!m)
        return false;
    boat_model_begin(m);
    if (!between(s.length, 3, 40) || !between(s.beam, 1, 10) || !between(s.draft, .15f, 1.5f)
        || !between(s.freeboard, .35f, 3) || !between(s.bow_width, .02f, .45f)
        || !between(s.stern_width, .5f, 1) || !between(s.cabin_length, 0, s.length * .55f)
        || !between(s.cabin_height, 0, 2.5f)
        || !between(s.cabin_offset, -s.length * .2f, s.length * .2f)
        || !between(s.windshield_rake, 0, .6f) || !between(s.mast_height, 0, 12) || s.engines < 0
        || s.engines > 2
        || (s.cabin_height > 0
            && (s.cabin_length < .5f
                || s.cabin_length <= s.cabin_height * s.windshield_rake + .15f))
        || (s.mast_height > 0 && s.mast_height < s.freeboard + 2))
        return false;
    float l = s.length, b = s.beam;
    Vector2 outline[] = { { -b * s.stern_width * .5f, l * .5f },
        { b * s.stern_width * .5f, l * .5f }, { b * .5f, l * .05f }, { b * .40f, -l * .30f },
        { b * s.bow_width * .5f, -l * .5f }, { -b * s.bow_width * .5f, -l * .5f },
        { -b * .40f, -l * .30f }, { -b * .5f, l * .05f } };
    // Chine, colored side, stripe, raised gunwale, inset cockpit edge, cockpit floor.
    float y[] = { -s.draft, s.freeboard * .70f, s.freeboard * .82f, s.freeboard, s.freeboard,
        s.freeboard * .62f };
    float scale[] = { .66f, 1, 1, 1, .86f, .86f };
    Color colors[] = { s.hull, s.trim, s.hull, cream, s.deck };
    Vector3 rings[6][8];
    for (int r = 0; r < 6; ++r)
        for (int i = 0; i < 8; ++i)
            rings[r][i] = (Vector3) { outline[i].x * scale[r], y[r], outline[i].y * scale[r] };
    for (int r = 0; r < 5; ++r)
        for (int i = 0; i < 8; ++i) {
            int j = (i + 1) % 8;
            quad(m, rings[r][i], rings[r][j], rings[r + 1][j], rings[r + 1][i], colors[r]);
        }
    for (int i = 0; i < 8; ++i) {
        triangle(m, (Vector3) { 0, y[5], 0 }, rings[5][i], rings[5][(i + 1) % 8], s.deck);
        triangle(m, (Vector3) { 0, y[0], 0 }, rings[0][(i + 1) % 8], rings[0][i], rubber);
    }
    float floor = y[5];
    // Broad foredeck, open rear cockpit, and seats keep the silhouettes readable at 320x200.
    boat_model_box(
        m, (Vector3) { 0, floor + .04f, -l * .27f }, (Vector3) { b * .55f, .08f, l * .18f }, cream);
    for (int end = 0; end < 2; ++end)
        boat_model_box(m, (Vector3) { 0, floor + .22f, l * (.22f + end * .14f) },
            (Vector3) { b * .68f, .28f, .32f }, s.trim);
    if (s.cabin_height > 0) {
        float w = b * .67f, h = s.cabin_height, z = s.cabin_offset, d = s.cabin_length;
        float bottom = floor + .08f, top = bottom + h;
        Vector3 v[] = { { -w * .5f, bottom, z - d * .5f }, { w * .5f, bottom, z - d * .5f },
            { w * .45f, top, z - d * .5f + h * s.windshield_rake },
            { -w * .45f, top, z - d * .5f + h * s.windshield_rake },
            { -w * .5f, bottom, z + d * .5f }, { w * .5f, bottom, z + d * .5f },
            { w * .45f, top, z + d * .5f }, { -w * .45f, top, z + d * .5f } };
        box_vertices(m, v, cream);
        // Windows inset from the panel boundaries, offset outward to avoid coplanar faces.
        Vector3 front[4];
        for (int i = 0; i < 4; ++i) {
            Vector3 center
                = Vector3Scale(Vector3Add(Vector3Add(v[0], v[1]), Vector3Add(v[2], v[3])), .25f);
            front[i] = Vector3Lerp(center, v[i], .76f);
            front[i].z -= .015f;
        }
        quad(m, front[0], front[3], front[2], front[1], glass);
        for (int side = 0; side < 2; ++side) {
            const int ids[2][4] = { { 0, 4, 7, 3 }, { 5, 1, 2, 6 } };
            Vector3 pane[4], center = { 0 };
            for (int i = 0; i < 4; ++i)
                center = Vector3Add(center, Vector3Scale(v[ids[side][i]], .25f));
            for (int i = 0; i < 4; ++i) {
                pane[i] = Vector3Lerp(center, v[ids[side][i]], .73f);
                pane[i].x += side ? .015f : -.015f;
            }
            if (!s.passenger_deck) {
                quad(m, pane[0], pane[1], pane[2], pane[3], glass);
            } else {
                for (int window = 0; window < 8; ++window) {
                    float a = (window + .08f) / 8, b = (window + .92f) / 8;
                    quad(m, Vector3Lerp(pane[0], pane[1], a), Vector3Lerp(pane[0], pane[1], b),
                        Vector3Lerp(pane[3], pane[2], b), Vector3Lerp(pane[3], pane[2], a), glass);
                }
            }
        }
        boat_model_box(m, (Vector3) { 0, top + .07f, z + h * s.windshield_rake * .25f },
            (Vector3) { w * 1.05f, .14f, d - h * s.windshield_rake * .5f + .12f }, s.trim);
    }
    if (s.passenger_deck) {
        float deck = floor + .08f + s.cabin_height + .14f;
        // Raised forward bridge, red funnel, open upper deck and chunky safety rails.
        boat_model_box(m, (Vector3) { 0, deck + .78f, -l * .15f },
            (Vector3) { b * .48f, 1.55f, l * .14f }, cream);
        boat_model_box(
            m, (Vector3) { 0, deck + .95f, -l * .221f }, (Vector3) { b * .39f, .65f, .06f }, glass);
        for (int side = -1; side <= 1; side += 2) {
            boat_model_box(m, (Vector3) { side * b * .242f, deck + .95f, -l * .15f },
                (Vector3) { .06f, .65f, l * .11f }, glass);
            for (int post = 0; post < 7; ++post)
                boat_model_box(m,
                    (Vector3) { side * b * .31f, deck + .48f, -l * .22f + post * l * .075f },
                    (Vector3) { .09f, .96f, .09f }, cream);
            boat_model_box(m, (Vector3) { side * b * .31f, deck + .96f, l * .005f },
                (Vector3) { .10f, .10f, l * .46f }, cream);
        }
        boat_model_box(m, (Vector3) { 0, deck + 1.6f, -l * .15f },
            (Vector3) { b * .53f, .17f, l * .16f }, s.trim);
        boat_model_box(m, (Vector3) { 0, deck + .85f, l * .16f },
            (Vector3) { b * .22f, 1.7f, l * .08f }, (Color) { 190, 66, 43, 255 });
        boat_model_box(m, (Vector3) { 0, deck + 1.72f, l * .16f },
            (Vector3) { b * .24f, .18f, l * .09f }, rubber);
        boat_model_box(
            m, (Vector3) { 0, deck + .22f, l * .05f }, (Vector3) { b * .4f, .44f, .5f }, s.trim);
    }
    for (int engine = 0; engine < s.engines; ++engine) {
        float x = s.engines == 1 ? 0 : (engine ? .25f : -.25f) * b;
        boat_model_box(m, (Vector3) { x, .05f, l * .51f }, (Vector3) { .23f, .65f, .21f }, steel);
        boat_model_box(m, (Vector3) { x, s.freeboard * .65f, l * .54f },
            (Vector3) { .42f, .52f, .5f }, rubber);
    }
    if (s.mast_height > 0) {
        Vector3 foot = { 0, floor, -l * .10f };
        Vector3 head = { 0, s.mast_height, -l * .10f };
        boat_model_beam(m, foot, head, .13f, steel);
        boat_model_beam(m, (Vector3) { 0, floor + 1.2f, -l * .10f },
            (Vector3) { 0, floor + 1.2f, l * .30f }, .18f, cream);
        for (int side = -1; side <= 1; side += 2)
            boat_model_beam(
                m, head, (Vector3) { side * b * .4f, s.freeboard, l * .05f }, .035f, steel);
        boat_model_beam(m, head, (Vector3) { 0, s.freeboard, -l * .43f }, .035f, steel);
    }
    if (s.work_deck) {
        boat_model_box(
            m, (Vector3) { 0, floor + .45f, l * .25f }, (Vector3) { 1.3f, .9f, 1.1f }, s.trim);
        boat_model_beam(m, (Vector3) { -.9f, floor, l * .3f },
            (Vector3) { -.9f, floor + 2.5f, l * .3f }, .18f, s.trim);
        boat_model_beam(m, (Vector3) { -.9f, floor + 2.5f, l * .3f },
            (Vector3) { -.9f, floor + 2.5f, l * .04f }, .14f, s.trim);
        boat_model_beam(m, (Vector3) { -.9f, floor + 2.5f, l * .04f },
            (Vector3) { -.9f, floor + 1.5f, l * .04f }, .05f, rubber);
    }
    for (int side = -1; side <= 1; side += 2)
        for (int end = -1; end <= 1; end += 2) {
            float z = end * l * .24f;
            if (s.fenders)
                boat_model_box(m, (Vector3) { side * b * .5f, s.freeboard * .48f, z },
                    (Vector3) { .22f, .55f, .25f }, cream);
            boat_model_box(m, (Vector3) { side * b * .39f, s.freeboard + .05f, z },
                (Vector3) { .23f, .10f, .12f }, steel);
        }
    if (m->overflow)
        m->mesh.triangle_count = 0;
    return !m->overflow;
}

void boat_model_vertical_bounds(BoatModelSpec spec, Transform3D pose, float *bottom, float *top)
{
    *bottom = INFINITY;
    *top = -INFINITY;
    float high = fmaxf(spec.mast_height, spec.freeboard + spec.cabin_height + .8f);
    for (int x = -1; x <= 1; x += 2)
        for (int z = -1; z <= 1; z += 2)
            for (int y = 0; y <= 1; ++y) {
                Vector3 point = boat_model_point(pose,
                    (Vector3) { x * (spec.beam * .5f + .3f), y ? high : -spec.draft - .2f,
                        z * (spec.length * .5f + .8f) });
                *bottom = fminf(*bottom, point.y);
                *top = fmaxf(*top, point.y);
            }
}
