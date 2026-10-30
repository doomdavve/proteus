#include "waterfront_harbor.h"

#include "raymath.h"

#include <math.h>
#include <string.h>

// Artistic berth placements beside mapped quays, not surveyed marina facilities.
// The central basin axis and the existing launch's X=12 route remain open.
static const WaterfrontBerth berths[WATERFRONT_HARBOR_BOATS] = {
    { "skiff", BOAT_MODEL_SKIFF, BOAT_SCENE_BERTH_SKIFF, BOAT_SCENE_MOORING_PONTOON,
        { -13, 0, 167 }, -PI / 2 },
    { "runabout", BOAT_MODEL_RUNABOUT, BOAT_SCENE_BERTH_RUNABOUT, BOAT_SCENE_MOORING_PONTOON,
        { -13, 0, 173 }, -PI / 2 },
    { "sailboat", BOAT_MODEL_SAILBOAT, BOAT_SCENE_BERTH_SAILBOAT, BOAT_SCENE_MOORING_PONTOON,
        { -13, 0, 179 }, -PI / 2 },
    { "cruiser", BOAT_MODEL_CRUISER, BOAT_SCENE_BERTH_CRUISER, BOAT_SCENE_MOORING_PONTOON,
        { -13, 0, 185 }, -PI / 2 },
    { "speedboat", BOAT_MODEL_SPEEDBOAT, BOAT_SCENE_BERTH_SPEEDBOAT, BOAT_SCENE_MOORING_PONTOON,
        { -13, 0, 191 }, -PI / 2 },
    { "sailboat_7m", BOAT_MODEL_SAILBOAT, BOAT_SCENE_BERTH_SAILBOAT_7M, BOAT_SCENE_MOORING_PONTOON,
        { -13, 0, 197 }, -PI / 2 },
    { "workboat", BOAT_MODEL_WORKBOAT, BOAT_SCENE_BERTH_WORKBOAT, BOAT_SCENE_MOORING_QUAY,
        { 44, 0, 35 }, -.10f },
};
static const Vector3 dock_center = { -20, 0, 182 };
// The user asked for a Stena Line ferry "docked right across the Eriksbergs crane" (A3.5e): an
// authored berth on the quay facing the crane, not the real terminal, clear of the race start
// and the traffic circuits, 2-7 m off the mapped bank (its bollards stand 12 m off the hull's
// quay side to reach land). Size and draught are Stena Danica's (154.89 x 28.04 m, 6.31 m); the
// height, decks and colours are read from photos. The name is a made-up line's, so the real
// company's name and logo are not reproduced. The bow points upstream, so the river side shows
// the name reading from bow to stern, as in the user's photo.
static const WaterfrontFerry ferry = { { 237, 0, -376 }, 2.865f, 155, 28, 6.3f, 34 };
static const Color timber = { 154, 123, 84, 255 };
static const Color rubber = { 44, 52, 55, 255 };
static const Color steel = { 128, 144, 146, 255 };
static BoatModel boats[BOAT_SCENE_BERTH_RECIPES], pontoon, quay, ferry_model;
static bool initialized;

const WaterfrontBerth *waterfront_harbor_berths(void) { return berths; }
const WaterfrontFerry *waterfront_harbor_ferry(void) { return &ferry; }

unsigned waterfront_harbor_berth_count(const BoatSceneDefinition *scene)
{
    if (!scene || !scene->berths_present)
        return WATERFRONT_HARBOR_BOATS;
    return scene->berth_count < BOAT_SCENE_BERTHS ? scene->berth_count : BOAT_SCENE_BERTHS;
}

WaterfrontBerth waterfront_harbor_berth_at(const BoatSceneDefinition *scene, unsigned index)
{
    if (scene && scene->berths_present) {
        if (index >= scene->berth_count || index >= BOAT_SCENE_BERTHS)
            return (WaterfrontBerth) { 0 };
        const BoatSceneBerth *source = &scene->berths[index];
        WaterfrontBerth out = { .kind = boat_scene_berth_kind((BoatSceneBerthRecipe)source->recipe),
            .recipe = source->recipe,
            .mooring = source->mooring,
            .position = { -source->x, 0, source->z },
            .heading = source->heading };
        memcpy(out.id, source->id, sizeof(out.id));
        return out;
    }
    return index < WATERFRONT_HARBOR_BOATS ? berths[index] : (WaterfrontBerth) { 0 };
}

BoatModelSpec waterfront_harbor_berth_spec(const WaterfrontBerth *berth)
{
    return berth ? boat_scene_berth_spec((BoatSceneBerthRecipe)berth->recipe)
                 : (BoatModelSpec) { 0 };
}

BoatModelSpec waterfront_harbor_spec(int index)
{
    return index >= 0 && index < WATERFRONT_HARBOR_BOATS
        ? waterfront_harbor_berth_spec(&berths[index])
        : (BoatModelSpec) { 0 };
}

// Five-row pixel capitals, as the cafe's KAFFE sign; '#' is a filled cell, '|' ends a row.
static const char *glyph(char c)
{
    switch (c) {
    case 'V':
        return "#...#|#...#|.#.#.|.#.#.|..#..";
    case 'G':
        return "###|#..|#.#|#.#|###";
    case 'E':
        return "###|#..|##.|#..|###";
    case 'N':
        return "#..#|##.#|#.##|#..#|#..#";
    case 'A':
        return ".#.|#.#|###|#.#|#.#";
    case 'L':
        return "#..|#..|#..|#..|###";
    case 'I':
        return "#|#|#|#|#";
    default:
        return "..|..|..|..|..";
    }
}

// Lettering on one hull side (SIDE: +1 river, -1 quay), reading from bow to stern as seen from
// that side. Cells are CELL metres; the text starts at local z = START and rows run down from TOP.
// Each run of filled cells is one outward panel just off the hull.
static void lettering(
    BoatModel *m, const char *text, int side, float start, float top, float cell, Color color)
{
    float x = side * (ferry.beam * .5f + .05f);
    float column = 0;
    for (const char *c = text; *c; ++c) {
        int row = 0, at = 0, width = 0, run = -1;
        for (const char *q = glyph(*c);; ++q) {
            if (*q == '#' && run < 0)
                run = at;
            if (*q != '#' && run >= 0) {
                float y0 = top - (row + 1) * cell, y1 = top - row * cell;
                float z0 = start + side * (column + run) * cell;
                float z1 = start + side * (column + at) * cell;
                if (z0 > z1) {
                    float swap = z0;
                    z0 = z1;
                    z1 = swap;
                }
                if (side > 0)
                    boat_model_quad(m, (Vector3) { x, y0, z0 }, (Vector3) { x, y1, z0 },
                        (Vector3) { x, y1, z1 }, (Vector3) { x, y0, z1 }, color);
                else
                    boat_model_quad(m, (Vector3) { x, y0, z0 }, (Vector3) { x, y0, z1 },
                        (Vector3) { x, y1, z1 }, (Vector3) { x, y1, z0 }, color);
                run = -1;
            }
            if (!*q)
                break;
            if (*q == '|') {
                ++row;
                at = 0;
                continue;
            }
            if (++at > width)
                width = at;
        }
        column += width + 1;
    }
}

// Blocky ro-pax: bow at local -z, waterline at y = 0, river side +x.
static void build_ferry(BoatModel *m)
{
    const Color white = { 236, 238, 240, 255 }, navy = { 27, 49, 104, 255 };
    const Color antifouling = { 118, 46, 40, 255 }, glass = { 38, 52, 66, 255 };
    const Color green = { 73, 168, 82, 255 }, orange = { 228, 112, 34, 255 };
    float l = ferry.length, b = ferry.beam, d = ferry.draft;
    boat_model_begin(m);
    // Hull: a square stern, a parallel body and a bow stepped narrower toward the stem.
    static const float bow_widths[] = { .86f, .7f, .52f, .32f, .14f };
    for (int i = 0; i < 6; ++i) {
        float front = -l * .5f + 30 - i * 5, back = i ? front + 5 : l * .5f;
        float width = i ? b * bow_widths[i - 1] : b;
        float z = (front + back) * .5f, length = back - front;
        boat_model_box(m, (Vector3) { 0, (-d - .5f) * .5f, z },
            (Vector3) { width, d - .5f, length }, antifouling);
        boat_model_box(m, (Vector3) { 0, 1.5f, z }, (Vector3) { width, 4, length }, navy);
        boat_model_box(m, (Vector3) { 0, 7.25f, z }, (Vector3) { width, 7.5f, length }, white);
    }
    // The car deck's stern door, and the name in navy on both sides with the green wave aft.
    boat_model_box(m, (Vector3) { 0, 7, l * .5f + .05f }, (Vector3) { 16, 6, .14f }, glass);
    for (int side = -1; side <= 1; side += 2) {
        lettering(m, "VINGA LINE", side, side > 0 ? -38 : 22, 10.2f, 1.2f, navy);
        for (int stripe = 0; stripe < 2; ++stripe) {
            float z = 26 + stripe * 2.4f;
            boat_model_beam(m, (Vector3) { side * (b * .5f + .08f), 4.6f, z },
                (Vector3) { side * (b * .5f + .08f), 10.4f, z + 9 }, 1.1f, green);
        }
    }
    // Superstructure tiers (bottom, top, front, back, inset) with window bands, the bridge
    // forward with its wings, lifeboats, a funnel aft and a mast.
    static const float tiers[][5]
        = { { 11, 17, -48, 58, 2 }, { 17, 22.5f, -44, 46, 4 }, { 22.5f, 27, -44, 10, 6 } };
    for (int i = 0; i < 3; ++i) {
        const float *t = tiers[i];
        float z = (t[2] + t[3]) * .5f, length = t[3] - t[2];
        boat_model_box(m, (Vector3) { 0, (t[0] + t[1]) * .5f, z },
            (Vector3) { b - t[4], t[1] - t[0], length }, white);
        boat_model_box(m, (Vector3) { 0, t[0] + (t[1] - t[0]) * .55f, z },
            (Vector3) { b - t[4] + .2f, 1.3f, length - 1 }, glass);
    }
    boat_model_box(m, (Vector3) { 0, 25, -44.2f }, (Vector3) { b + 1, 2.6f, 3 }, white);
    boat_model_box(m, (Vector3) { 0, 25.3f, -45.8f }, (Vector3) { b + 1.2f, 1.2f, .3f }, glass);
    for (int side = -1; side <= 1; side += 2)
        for (int i = 0; i < 3; ++i)
            boat_model_box(m, (Vector3) { side * (b * .5f - .6f), 18.6f, -20 + i * 22 },
                (Vector3) { 2.4f, 2.2f, 8.5f }, orange);
    boat_model_box(m, (Vector3) { 0, 28.5f, 32 }, (Vector3) { 6, 9, 9 }, white);
    boat_model_box(m, (Vector3) { 0, 33.4f, 32 }, (Vector3) { 6.2f, 1.2f, 9.2f }, navy);
    boat_model_beam(m, (Vector3) { 0, 27, -36 }, (Vector3) { 0, ferry.height, -36 }, .5f, white);
}

static void initialize(void)
{
    if (initialized)
        return;
    initialized = true;
    for (int i = 0; i < BOAT_SCENE_BERTH_RECIPES; ++i)
        boat_model_generate(&boats[i], boat_scene_berth_spec((BoatSceneBerthRecipe)i));
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
    build_ferry(&ferry_model);
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

const Mesh3D *waterfront_harbor_recipe_mesh(unsigned recipe)
{
    if (recipe >= BOAT_SCENE_BERTH_RECIPES)
        return NULL;
    initialize();
    return &boats[recipe].mesh;
}

const Mesh3D *waterfront_harbor_boat_mesh(int index)
{
    if (index < 0 || index >= WATERFRONT_HARBOR_BOATS)
        return NULL;
    return waterfront_harbor_recipe_mesh(berths[index].recipe);
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
const Mesh3D *waterfront_harbor_ferry_mesh(void)
{
    initialize();
    return &ferry_model.mesh;
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
    waterfront_harbor_evaluate_scene(NULL, water, time, NULL, frame);
}

void waterfront_harbor_evaluate_ground(
    BoatWater water, double time, float (*ground)(float x, float z), WaterfrontHarborFrame *frame)
{
    waterfront_harbor_evaluate_scene(NULL, water, time, ground, frame);
}

void waterfront_harbor_evaluate_scene(const BoatSceneDefinition *scene, BoatWater water,
    double time, float (*ground)(float x, float z), WaterfrontHarborFrame *frame)
{
    *frame = (WaterfrontHarborFrame) { 0 };
    bool authored = scene && scene->berths_present;
    unsigned count = waterfront_harbor_berth_count(scene);
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
    frame->berth_count = (int)count;
    for (unsigned i = 0; i < count; ++i) {
        WaterfrontBerth berth = waterfront_harbor_berth_at(scene, i);
        BoatModelSpec spec = waterfront_harbor_berth_spec(&berth);
        frame->recipes[i] = berth.recipe;
        frame->boats[i]
            = boat_water_pose(water, time, berth.position, berth.heading, spec.length, spec.beam);
        for (int end = -1; end <= 1; end += 2) {
            Vector3 attachment = waterfront_harbor_attachment(frame->boats[i],
                (Vector3) { spec.beam * .39f, spec.freeboard + .10f, end * spec.length * .24f });
            Vector3 anchor;
            if (!authored && i < 6) {
                anchor = waterfront_harbor_attachment(frame->pontoon,
                    end > 0 ? (Vector3) { 1.05f, 1.04f, -18 + (int)i * 6 }
                            : (Vector3) { 7.8f, 1.04f, -12 + ((int)i / 2) * 12 });
            } else if (!authored) {
                float z = 35 + end * 5;
                anchor = (Vector3) { 47.27f - .099f * (z - 30), 4.02f + quay_lift, z };
            } else if (berth.mooring == BOAT_SCENE_MOORING_PONTOON) {
                // Authored berths reuse the compiled pontoon's native cleat region until L2.3
                // exposes authored dock modules. The boat cleat still follows the wave pose.
                float reach = 37 * .5f - 1;
                float local = Clamp(berth.position.z - dock_center.z + end * 2, -reach, reach);
                anchor = waterfront_harbor_attachment(
                    frame->pontoon, (Vector3) { 1.05f, 1.04f, local });
            } else {
                continue;
            }
            mooring(frame, attachment, anchor);
        }
    }
    // The ferry rides the water averaged over its length. Bow, stern and two spring lines run
    // from its quay-side deck edge to bollards on the quay, set on the ground there.
    frame->ferry
        = boat_water_pose(water, time, ferry.position, ferry.heading, ferry.length, ferry.beam);
    Transform3D rest = { .position = ferry.position,
        .rotation = boat_model_rotation(MatrixRotateY(ferry.heading)),
        .scale = { 1, 1, 1 } };
    static const float line_z[WATERFRONT_FERRY_LINES][2]
        = { { -70, -82 }, { -30, -12 }, { 30, 12 }, { 70, 82 } };
    for (int i = 0; i < WATERFRONT_FERRY_LINES; ++i) {
        Vector3 deck = waterfront_harbor_attachment(
            frame->ferry, (Vector3) { -ferry.beam * .5f + 1, 11.2f, line_z[i][0] });
        Vector3 bollard = waterfront_harbor_attachment(
            rest, (Vector3) { -ferry.beam * .5f - 12, 0, line_z[i][1] });
        bollard.y = (ground ? ground(bollard.x, bollard.z) : 3.5f) + .4f;
        mooring(frame, deck, bollard);
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
    return waterfront_harbor_contact_height_scene(
        NULL, position, clearance, bottom, top, water, time, normal);
}

// The docked ferry, compiled pontoon and quay/land fittings. P is renderer X/Z; the normal is
// renderer-oriented and the caller converts it once. Authored hulls are deliberately excluded.
static bool fixtures_contact(Vector2 p, float clearance, float bottom, float top, BoatWater water,
    double time, Vector2 *normal)
{
    Vector2 world_normal;
    bool hit = obstacle(p, (Vector2) { ferry.position.x, ferry.position.z },
        (Vector2) { ferry.beam * .5f + .45f, ferry.length * .5f + .9f }, ferry.heading, clearance,
        &world_normal);
    if (hit && (isfinite(bottom) || isfinite(top))) {
        Transform3D pose
            = boat_water_pose(water, time, ferry.position, ferry.heading, ferry.length, ferry.beam);
        hit = bottom <= pose.position.y + ferry.height && top >= pose.position.y - ferry.draft;
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
        *normal = world_normal;
    return hit;
}

bool waterfront_harbor_contact_height_scene(const BoatSceneDefinition *scene, Vector2 position,
    float clearance, float bottom, float top, BoatWater water, double time, Vector2 *normal)
{
    Vector2 p = { -position.x, position.y }, world_normal;
    bool hit = false;
    unsigned count = waterfront_harbor_berth_count(scene);
    for (unsigned i = 0; i < count && !hit; ++i) {
        WaterfrontBerth berth = waterfront_harbor_berth_at(scene, i);
        BoatModelSpec s = waterfront_harbor_berth_spec(&berth);
        hit = obstacle(p, (Vector2) { berth.position.x, berth.position.z },
            (Vector2) { s.beam * .5f + .45f, s.length * .5f + .9f }, berth.heading, clearance,
            &world_normal);
        if (hit && (isfinite(bottom) || isfinite(top))) {
            Transform3D pose
                = boat_water_pose(water, time, berth.position, berth.heading, s.length, s.beam);
            float low, high;
            boat_model_vertical_bounds(s, pose, &low, &high);
            hit = bottom <= high && top >= low;
        }
    }
    if (!hit)
        hit = fixtures_contact(p, clearance, bottom, top, water, time, &world_normal);
    if (hit && normal)
        *normal = (Vector2) { -world_normal.x, world_normal.y };
    return hit;
}

bool waterfront_harbor_camera_clear(Vector3 p, float clearance)
{
    return waterfront_harbor_camera_clear_scene(NULL, p, clearance);
}

bool waterfront_harbor_camera_clear_scene(
    const BoatSceneDefinition *scene, Vector3 p, float clearance)
{
    unsigned count = waterfront_harbor_berth_count(scene);
    for (unsigned i = 0; i < count; ++i) {
        WaterfrontBerth berth = waterfront_harbor_berth_at(scene, i);
        BoatModelSpec s = waterfront_harbor_berth_spec(&berth);
        float top = fmaxf(s.mast_height, s.freeboard + s.cabin_height + .8f);
        if (p.y < top + clearance && p.y > -s.draft - clearance
            && obstacle((Vector2) { p.x, p.z }, (Vector2) { berth.position.x, berth.position.z },
                (Vector2) { s.beam * .5f + .5f, s.length * .5f + 1 }, berth.heading, clearance,
                NULL))
            return false;
    }
    if (p.y < ferry.height + clearance && p.y > -ferry.draft - clearance
        && obstacle((Vector2) { p.x, p.z }, (Vector2) { ferry.position.x, ferry.position.z },
            (Vector2) { ferry.beam * .5f + .5f, ferry.length * .5f + 1 }, ferry.heading, clearance,
            NULL))
        return false;
    if (p.y < 4.5f + clearance && p.y > -.5f - clearance
        && fixtures_contact(
            (Vector2) { p.x, p.z }, clearance, -INFINITY, INFINITY, (BoatWater) { 0 }, 0, NULL))
        return false;
    return true;
}
