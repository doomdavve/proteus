#include "waterfront_corner.h"
#include "waterfront_city.h"
#include "waterfront_scene.h"

#include "raymath.h"

#include <math.h>
#include <stdlib.h>

// Local u follows the north facade east; v points out into the courtyard north.
// Ground is 3.5 m. The mapped west facade runs from v=0 to v=-13.9.
Vector3 waterfront_corner_position(float u, float height, float v)
{
    return (Vector3) { -71.82f + .99284f * u - .11952f * v, 3.5f + height,
        105.9f + .11952f * u + .99284f * v };
}

#define SCENERY_MAX_BOXES 16384

// Ground queries are only used during construction. Keep the actual map as the
// authority: no furniture on roofs, roads, water, or the existing planted areas.
typedef struct {
    Vector3 a, b, c, low, high;
} Surface;

// Construction scratch for one scenery owner; nothing outlives waterfront_scenery_init.
typedef struct {
    WaterfrontScenery *out;
    size_t capacity;
    bool overflow;
    Vector3 origin, axis_u, axis_v;
    Surface *surfaces;
    size_t surface_count;
    // Uniform XZ grid over the surfaces' bounds: cell i lists surfaces[cell_items[cell_start[i]
    // .. cell_start[i + 1]]], every surface whose bounds overlap it.
    Vector2 grid_low;
    float cell;
    int columns, rows;
    size_t *cell_start, *cell_items;
    int entrances, shops, parked_cars, benches, bicycles, quay_sections, sidewalk_sections;
    int forecourts, trees;
} Builder;

static const Color stone = { 171, 171, 158, 255 };
static const Color metal = { 69, 83, 89, 255 };
static const Color wood = { 167, 114, 72, 255 };
static const Color green = { 72, 120, 101, 255 };
static const Color cream = { 232, 210, 161, 255 };

static Vector3 place(Builder *k, Vector3 p)
{
    Vector3 q = Vector3Add(
        k->origin, Vector3Add(Vector3Scale(k->axis_u, p.x), Vector3Scale(k->axis_v, p.z)));
    q.y += p.y;
    return q;
}

static void frame(Builder *k, Vector3 p, Vector3 u, Vector3 v)
{
    k->origin = p;
    k->origin.y = 3.5f;
    k->axis_u = u;
    k->axis_v = v;
}

// Appends one box, growing storage up to the fixed cap; overflow fails the whole scenery.
static WaterfrontCornerBox *new_box(Builder *k)
{
    WaterfrontScenery *s = k->out;
    if (s->count == k->capacity) {
        if (k->capacity == SCENERY_MAX_BOXES) {
            k->overflow = true;
            return NULL;
        }
        size_t capacity = k->capacity ? k->capacity * 2 : 1024;
        WaterfrontCornerBox *grown = realloc(s->boxes, capacity * sizeof(*grown));
        if (!grown) {
            k->overflow = true;
            return NULL;
        }
        s->boxes = grown;
        k->capacity = capacity;
    }
    return &s->boxes[s->count++];
}

static void box(Builder *k, Vector3 p, Vector3 size, Color color, bool emissive)
{
    WaterfrontCornerBox *b = new_box(k);
    if (!b)
        return;
    *b = (WaterfrontCornerBox) { .color = color, .emissive = emissive };
    static const Vector3 corners[] = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
        { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } };
    for (int i = 0; i < 8; ++i) {
        Vector3 q = Vector3Add(p, Vector3Scale(Vector3Multiply(size, corners[i]), .5f));
        b->vertices[i] = place(k, q);
    }
}

static void beam(Builder *k, Vector3 a, Vector3 b, float thickness, Color color)
{
    WaterfrontCornerBox *out = new_box(k);
    if (!out)
        return;
    Vector3 direction = Vector3Normalize(Vector3Subtract(b, a));
    Vector3 reference = fabsf(direction.y) > .9f ? (Vector3) { 1, 0, 0 } : (Vector3) { 0, 1, 0 };
    Vector3 right = Vector3Scale(
        Vector3Normalize(Vector3CrossProduct(reference, direction)), thickness * .5f);
    Vector3 up
        = Vector3Scale(Vector3Normalize(Vector3CrossProduct(direction, right)), thickness * .5f);
    *out = (WaterfrontCornerBox) { .color = color };
    for (int i = 0; i < 8; ++i) {
        int corner = i % 4;
        Vector3 p = Vector3Add(i < 4 ? a : b,
            Vector3Add(Vector3Scale(right, corner == 0 || corner == 3 ? -1 : 1),
                Vector3Scale(up, corner < 2 ? -1 : 1)));
        out->vertices[i] = place(k, p);
    }
}

static void chair(Builder *k, float u, float v, float facing)
{
    box(k, (Vector3) { u, .64f, v }, (Vector3) { .65f, .12f, .65f }, wood, false);
    box(k, (Vector3) { u, 1.04f, v + facing * .27f }, (Vector3) { .65f, .65f, .1f }, green, false);
    for (int side = -1; side <= 1; side += 2)
        box(k, (Vector3) { u + side * .25f, .4f, v }, (Vector3) { .09f, .5f, .55f }, metal, false);
}

static void planter(Builder *k, float u, float v, float width, float depth)
{
    box(k, (Vector3) { u, .46f, v }, (Vector3) { width, .62f, depth }, wood, false);
    box(k, (Vector3) { u, .98f, v }, (Vector3) { width - .15f, .55f, depth - .15f },
        (Color) { 88, 134, 75, 255 }, false);
}

static void bicycle(Builder *k, float u, float v, Color color)
{
    ++k->bicycles;
    // Eight-sided wheels and a diamond frame; slight thickness survives near shots.
    for (int wheel = -1; wheel <= 1; wheel += 2) {
        float z = v + wheel * .64f;
        for (int i = 0; i < 8; ++i) {
            float a = i * PI / 4, b = (i + 1) * PI / 4;
            beam(k, (Vector3) { u, .56f + .34f * sinf(a), z + .34f * cosf(a) },
                (Vector3) { u, .56f + .34f * sinf(b), z + .34f * cosf(b) }, .09f, metal);
        }
    }
    Vector3 rear = { u, .56f, v - .64f }, front = { u, .56f, v + .64f };
    Vector3 crank = { u, .5f, v }, seat = { u, 1.06f, v - .22f };
    Vector3 head = { u, 1.06f, v + .42f };
    beam(k, rear, crank, .08f, color);
    beam(k, rear, seat, .08f, color);
    beam(k, crank, seat, .08f, color);
    beam(k, seat, head, .08f, color);
    beam(k, crank, head, .08f, color);
    beam(k, head, front, .08f, color);
    box(k, seat, (Vector3) { .22f, .1f, .35f }, metal, false);
    beam(k, head, (Vector3) { u, 1.3f, v + .45f }, .08f, metal);
    box(k, (Vector3) { u, 1.3f, v + .45f }, (Vector3) { .5f, .08f, .09f }, metal, false);
}

static void build_cafe(Builder *k)
{
    // Street centre is about u=-7.6. Leave both traffic lanes clear. The north terrace
    // stops short of the existing planted garden, which starts around v=5.
    box(k, (Vector3) { -2.65f, .08f, -5 }, (Vector3) { 5.1f, .16f, 26 }, stone, false);
    box(k, (Vector3) { 8.4f, .08f, 2.0f }, (Vector3) { 16.4f, .16f, 3.8f }, stone, false);
    box(k, (Vector3) { -5.2f, .15f, -5 }, (Vector3) { .24f, .3f, 26 }, cream, false);
    // Broad paving joints, not a high-frequency texture. Keep the walking strip open.
    for (int i = 0; i < 12; ++i)
        box(k, (Vector3) { -2.6f, .165f, -17 + i * 2.1f }, (Vector3) { 4.95f, .01f, .035f },
            (Color) { 119, 125, 128, 255 }, false);

    // A dark shop surround masks the generic ground-floor apartment windows.
    box(k, (Vector3) { -.14f, 1.95f, -7 }, (Vector3) { .24f, 3.7f, 13.4f }, green, false);
    for (int bay = 0; bay < 4; ++bay) {
        float v = -11.5f + bay * 2.85f;
        box(k, (Vector3) { -.285f, 1.95f, v }, (Vector3) { .05f, 2.4f, 2.2f },
            (Color) { 212, 154, 84, 255 }, true);
        box(k, (Vector3) { -.325f, 1.95f, v }, (Vector3) { .05f, 2.4f, .1f }, green, false);
        box(k, (Vector3) { -.36f, 1.03f, v }, (Vector3) { .12f, .12f, 2.2f }, wood, false);
    }
    // Striped canvas and its fascia establish a cafe silhouette even when text is tiny.
    for (int stripe = 0; stripe < 14; ++stripe) {
        Color color = stripe % 2 ? cream : green;
        box(k, (Vector3) { -1.2f, 3.62f, -12.75f + stripe * .92f }, (Vector3) { 2.6f, .18f, .92f },
            color, false);
        box(k, (Vector3) { -2.47f, 3.43f, -12.75f + stripe * .92f }, (Vector3) { .12f, .42f, .92f },
            color, false);
    }
    // Recessed doorway, glass and transom on the short north return.
    box(k, (Vector3) { 3.4f, 1.8f, .12f }, (Vector3) { 4.7f, 3.5f, .24f }, green, false);
    box(k, (Vector3) { 3.3f, 1.58f, .26f }, (Vector3) { 1.55f, 2.8f, .05f },
        (Color) { 119, 91, 65, 255 }, true);
    box(k, (Vector3) { 3.3f, 3.13f, .26f }, (Vector3) { 1.55f, .22f, .05f }, cream, true);
    box(k, (Vector3) { 3.88f, 1.25f, .33f }, (Vector3) { .08f, .42f, .09f }, cream, false);
    box(k, (Vector3) { 3.3f, .2f, .65f }, (Vector3) { 2.1f, .1f, .9f }, metal, false);
    box(k, (Vector3) { 3.3f, 3.72f, 1.0f }, (Vector3) { 5.0f, .2f, 2.0f }, green, false);

    // KAFFE in a five-row pixel alphabet on a projecting sign facing the approach.
    box(k, (Vector3) { -2.0f, 3.12f, .45f }, (Vector3) { 2.95f, .98f, .18f }, green, false);
    static const unsigned char letters[5][5] = { { 5, 5, 6, 5, 5 }, { 2, 5, 7, 5, 5 },
        { 7, 4, 6, 4, 4 }, { 7, 4, 6, 4, 4 }, { 7, 4, 6, 4, 7 } };
    for (int letter = 0; letter < 5; ++letter)
        for (int row = 0; row < 5; ++row)
            for (int col = 0; col < 3; ++col)
                if (letters[letter][row] & (4 >> col))
                    // Match the waterfront's horizontally corrected presentation.
                    box(k,
                        (Vector3) { -.8f - (letter * 4 + col) * .13f, 3.39f - row * .13f, .555f },
                        (Vector3) { .12f, .12f, .025f }, cream, true);

    // Two sidewalk tables and one courtyard table leave a continuous curbside walking strip.
    for (int i = 0; i < 3; ++i) {
        float u = i == 2 ? 8 : -1.4f;
        float v = i == 2 ? 2.0f : -10.0f + i * 5.0f;
        box(k, (Vector3) { u, .94f, v }, (Vector3) { 1.25f, .12f, 1.25f }, cream, false);
        box(k, (Vector3) { u, .55f, v }, (Vector3) { .18f, .7f, .18f }, metal, false);
        chair(k, u, v - 1.1f, -1);
        chair(k, u, v + 1.1f, 1);
    }
    planter(k, 5.3f, 3.6f, 1.6f, .7f);
    planter(k, 15.3f, 3.2f, 1.6f, .7f);
    planter(k, -4.1f, -15.8f, 1.25f, 1.4f);
    // Bench, waste bin and menu board beside the cafe, not in its doorway.
    box(k, (Vector3) { 5.3f, .67f, 1.3f }, (Vector3) { .65f, .16f, 1.8f }, wood, false);
    box(k, (Vector3) { 5.55f, 1.05f, 1.3f }, (Vector3) { .12f, .65f, 1.8f }, wood, false);
    for (int i = -1; i <= 1; i += 2)
        box(k, (Vector3) { 5.3f, .39f, 1.3f + i * .65f }, (Vector3) { .5f, .4f, .15f }, metal,
            false);
    box(k, (Vector3) { 15.5f, .65f, 1.3f }, (Vector3) { .65f, 1, .65f }, green, false);
    box(k, (Vector3) { -1.25f, .92f, 2.0f }, (Vector3) { .18f, 1.45f, .85f }, wood, false);
    box(k, (Vector3) { -1.36f, 1.05f, 2.0f }, (Vector3) { .035f, 1.0f, .65f }, metal, false);
    for (int i = 0; i < 3; ++i)
        box(k, (Vector3) { -1.39f, 1.3f - i * .2f, 2.0f },
            (Vector3) { .025f, .04f, .45f - i * .08f }, cream, false);
    // Cycle stands occupy the terrace's east edge. Parked bikes remain static in step 04.
    for (int i = 0; i < 3; ++i) {
        float u = 12 + i * 1.1f;
        beam(k, (Vector3) { u, .2f, 1 }, (Vector3) { u, 1.0f, 1 }, .1f, metal);
        beam(k, (Vector3) { u, 1.0f, 1 }, (Vector3) { u, 1.0f, 2.3f }, .1f, metal);
        beam(k, (Vector3) { u, 1.0f, 2.3f }, (Vector3) { u, .2f, 2.3f }, .1f, metal);
        if (i < 2)
            bicycle(k, u + .18f, 1.65f, i ? cream : (Color) { 191, 81, 53, 255 });
    }
}

static float cross_xz(Vector3 a, Vector3 b, Vector3 p)
{
    return (b.x - a.x) * (p.z - a.z) - (b.z - a.z) * (p.x - a.x);
}

// Cell range of [low, high] along one grid axis, clamped to the grid; floor is monotone, so a
// point inside a surface's bounds always falls in one of the surface's cells.
// An empty span (span[0] > span[1]) lies outside the grid.
static void cell_span(float low, float high, float origin, float cell, int count, int span[2])
{
    span[0] = (int)fminf(count, fmaxf(0, floorf((low - origin) / cell)));
    span[1] = (int)fmaxf(-1, fminf(count - 1, floorf((high - origin) / cell)));
}

static bool surface_grid_init(Builder *k)
{
    Vector2 low = { INFINITY, INFINITY }, high = { -INFINITY, -INFINITY };
    for (size_t i = 0; i < k->surface_count; ++i) {
        low = Vector2Min(low, (Vector2) { k->surfaces[i].low.x, k->surfaces[i].low.z });
        high = Vector2Max(high, (Vector2) { k->surfaces[i].high.x, k->surfaces[i].high.z });
    }
    if (!k->surface_count)
        low = high = (Vector2) { 0 };
    // About 8 m cells, coarser for very large maps so the grid stays near 512 x 512.
    k->grid_low = low;
    k->cell = fmaxf(8, fmaxf(high.x - low.x, high.y - low.y) / 512);
    k->columns = (int)floorf((high.x - low.x) / k->cell) + 1;
    k->rows = (int)floorf((high.y - low.y) / k->cell) + 1;
    size_t cells = (size_t)k->columns * k->rows;
    k->cell_start = calloc(cells + 1, sizeof(*k->cell_start));
    size_t *fill = calloc(cells, sizeof(*fill));
    if (!k->cell_start || !fill) {
        free(fill);
        return false;
    }
    // Count per cell, take prefix sums as starts, then place each surface (a counting sort).
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < k->surface_count; ++i) {
            const Surface *s = &k->surfaces[i];
            int x[2], z[2];
            cell_span(s->low.x, s->high.x, low.x, k->cell, k->columns, x);
            cell_span(s->low.z, s->high.z, low.y, k->cell, k->rows, z);
            for (int row = z[0]; row <= z[1]; ++row)
                for (int column = x[0]; column <= x[1]; ++column) {
                    size_t cell = (size_t)row * k->columns + column;
                    if (pass)
                        k->cell_items[k->cell_start[cell] + fill[cell]++] = i;
                    else
                        ++k->cell_start[cell + 1];
                }
        }
        if (pass)
            break;
        for (size_t cell = 0; cell < cells; ++cell)
            k->cell_start[cell + 1] += k->cell_start[cell];
        size_t items = k->cell_start[cells];
        k->cell_items = malloc((items ? items : 1) * sizeof(*k->cell_items));
        if (!k->cell_items) {
            free(fill);
            return false;
        }
    }
    free(fill);
    return true;
}

static void surface_grid_destroy(Builder *k)
{
    free(k->cell_start);
    free(k->cell_items);
    k->cell_start = k->cell_items = NULL;
}

static float surface_height(Builder *k, Vector3 p)
{
    float height = -100;
    int x[2], z[2];
    cell_span(p.x, p.x, k->grid_low.x, k->cell, k->columns, x);
    cell_span(p.z, p.z, k->grid_low.y, k->cell, k->rows, z);
    if (x[0] > x[1] || z[0] > z[1])
        return height; // Outside every surface's bounds.
    size_t cell = (size_t)z[0] * k->columns + x[0];
    for (size_t item = k->cell_start[cell]; item < k->cell_start[cell + 1]; ++item) {
        const Surface *s = &k->surfaces[k->cell_items[item]];
        if (s->a.y <= height || p.x < s->low.x || p.x > s->high.x || p.z < s->low.z
            || p.z > s->high.z)
            continue;
        float a = cross_xz(s->a, s->b, p), b = cross_xz(s->b, s->c, p);
        float c = cross_xz(s->c, s->a, p);
        if ((a >= -.001f && b >= -.001f && c >= -.001f) || (a <= .001f && b <= .001f && c <= .001f))
            height = s->a.y;
    }
    return height;
}

static bool reserved(Vector3 p)
{
    // Preserve the authored cafe and the crane supports.
    return (p.x > -82 && p.x < -52 && p.z > 86 && p.z < 119)
        || (fabsf(p.x) > 39 && fabsf(p.x) < 56 && fabsf(p.z) < 14);
}

static bool space(Builder *k, float u, float v, float width, float depth)
{
    // Sample the footprint at <= 1 m intervals, not just its centre.
    int nx = (int)ceilf(width), nz = (int)ceilf(depth);
    for (int x = 0; x <= nx; ++x)
        for (int z = 0; z <= nz; ++z) {
            Vector3 p = place(k,
                (Vector3) {
                    u + width * ((float)x / nx - .5f), 0, v + depth * ((float)z / nz - .5f) });
            float height = surface_height(k, p);
            if (reserved(p) || fabsf(height - 3.5f) > .01f)
                return false;
        }
    return true;
}

static void bench(Builder *k, float u, float v)
{
    ++k->benches;
    box(k, (Vector3) { u, .6f, v }, (Vector3) { 2.3f, .16f, .65f }, wood, false);
    box(k, (Vector3) { u, 1.02f, v + .28f }, (Vector3) { 2.3f, .65f, .12f }, wood, false);
    for (int side = -1; side <= 1; side += 2)
        box(k, (Vector3) { u + side * .8f, .32f, v }, (Vector3) { .15f, .6f, .55f }, metal, false);
}

static void tree(Builder *k, float u, float v)
{
    ++k->trees;
    planter(k, u, v, 2.1f, 2.1f);
    box(k, (Vector3) { u, 2.1f, v }, (Vector3) { .3f, 3.6f, .3f }, wood, false);
    box(k, (Vector3) { u, 4.25f, v }, (Vector3) { 3.0f, 2.4f, 2.6f }, (Color) { 76, 109, 73, 255 },
        false);
    box(k, (Vector3) { u + .35f, 5.4f, v }, (Vector3) { 2.2f, .9f, 2 },
        (Color) { 98, 130, 78, 255 }, false);
}

static void parked_car(Builder *k, float u, int variant)
{
    static const Color paint[] = { { 126, 60, 52, 255 }, { 159, 162, 153, 255 },
        { 63, 90, 115, 255 }, { 178, 165, 121, 255 } };
    ++k->parked_cars;
    box(k, (Vector3) { u, .025f, 0 }, (Vector3) { 2.5f, .05f, 5.6f }, (Color) { 98, 106, 112, 255 },
        false);
    for (int side = -1; side <= 1; side += 2) {
        box(k, (Vector3) { u + side * 1.2f, .06f, 0 }, (Vector3) { .1f, .03f, 5.6f }, cream, false);
        for (int axle = -1; axle <= 1; axle += 2)
            box(k, (Vector3) { u + side * .91f, .39f, axle * 1.32f },
                (Vector3) { .19f, .57f, .65f }, (Color) { 36, 42, 49, 255 }, false);
        box(k, (Vector3) { u + side * .59f, .73f, -2.13f }, (Vector3) { .4f, .2f, .07f }, cream,
            false);
    }
    box(k, (Vector3) { u, .75f, 0 }, (Vector3) { 1.85f, .7f, 4.2f }, paint[variant % 4], false);
    box(k, (Vector3) { u, 1.29f, .18f }, (Vector3) { 1.55f, .65f, 2.35f },
        (Color) { 43, 65, 79, 255 }, false);
    box(k, (Vector3) { u, 1.63f, .18f }, (Vector3) { 1.65f, .12f, 2.4f }, paint[variant % 4],
        false);
}

static void build_streets(Builder *k)
{
    for (int index = 0; index < 2; ++index) {
        WaterfrontStreet street = waterfront_scene_street(index);
        float length = 0;
        for (int i = 1; i < street.point_count; ++i)
            length += Vector3Distance(street.points[i - 1], street.points[i]);
        for (float distance = 3; distance < length - 3; distance += 3) {
            Vector3 along;
            Vector3 p = waterfront_scene_street_position(street, distance, &along);
            frame(k, p, (Vector3) { along.z, 0, -along.x }, along);
            for (int side = -1; side <= 1; side += 2) {
                if (!space(k, side * 5.0f, 0, 2.6f, 3.0f))
                    continue;
                ++k->sidewalk_sections;
                box(k, (Vector3) { side * 5.0f, .075f, 0 }, (Vector3) { 2.6f, .15f, 3.0f }, stone,
                    false);
                box(k, (Vector3) { side * 3.75f, .14f, 0 }, (Vector3) { .18f, .28f, 3.0f }, cream,
                    false);
            }
        }
        for (int cluster = 0; cluster * 23 + 12 < length - 6; ++cluster) {
            Vector3 along;
            Vector3 p = waterfront_scene_street_position(street, cluster * 23 + 12, &along);
            frame(k, p, (Vector3) { along.z, 0, -along.x }, along);
            for (int side = -1; side <= 1; side += 2) {
                float u = side * 8.1f;
                if (!space(k, u, 0, 3.4f, 6))
                    continue;
                int variant = (cluster + index * 3 + (side > 0)) % 5;
                if (variant < 3)
                    parked_car(k, u, cluster + index);
                else if (variant == 3)
                    tree(k, u, 0);
                else {
                    bench(k, u, -1.8f);
                    bicycle(k, u, .9f, green);
                    planter(k, u, 2.3f, 2.5f, 1);
                }
            }
        }
    }
}

static float street_distance(Vector3 p)
{
    float best = 10000;
    for (int i = 0; i < 2; ++i) {
        WaterfrontStreet street = waterfront_scene_street(i);
        for (int j = 1; j < street.point_count; ++j) {
            Vector3 a = street.points[j - 1], delta = Vector3Subtract(street.points[j], a);
            Vector3 offset = Vector3Subtract(p, a);
            float t = Clamp(Vector3DotProduct(offset, delta) / Vector3LengthSqr(delta), 0, 1);
            best = fminf(best, Vector3Distance(p, Vector3Add(a, Vector3Scale(delta, t))));
        }
    }
    return best;
}

static void build_frontages(Builder *k, const Mesh3D *city)
{
    static const Color trims[] = { { 65, 112, 106, 255 }, { 113, 65, 60, 255 },
        { 64, 86, 113, 255 }, { 153, 133, 89, 255 } };
    for (size_t i = 0; i < city->triangle_count; ++i) {
        const Triangle3D *t = &city->triangles[i];
        Vector3 a = city->vertices[t->vertices[0]], c = city->vertices[t->vertices[1]];
        Vector3 b = city->vertices[t->vertices[2]];
        if (fabsf(a.y - 3.5f) > .001f || fabsf(b.y - a.y) > .001f || c.y < 12)
            continue;
        float length = Vector3Distance(a, b);
        Vector3 middle = Vector3Scale(Vector3Add(a, b), .5f);
        if (length < 9 || middle.x < -380 || middle.x > 470 || middle.z < -260 || middle.z > 510)
            continue;
        Vector3 along = Vector3Scale(Vector3Subtract(b, a), 1 / length);
        Vector3 normal = { along.z, 0, -along.x };
        frame(k, middle, Vector3Negate(along), normal);
        if (!space(k, 0, 1.5f, 3.5f, 2.8f))
            continue; // Reject interior part walls and obstructed k->entrances.
        ++k->entrances;
        Color trim = trims[(i / 2) % 4];
        box(k, (Vector3) { 0, 1.5f, .11f }, (Vector3) { 2.8f, 3, .22f }, trim, false);
        box(k, (Vector3) { 0, 1.4f, .24f }, (Vector3) { 1.65f, 2.7f, .06f }, metal, false);
        box(k, (Vector3) { 0, 2.65f, .28f }, (Vector3) { 1.5f, .38f, .04f },
            (Color) { 171, 137, 93, 255 }, true);
        box(k, (Vector3) { -.55f, 1.2f, .31f }, (Vector3) { .09f, .4f, .08f }, cream, false);
        box(k, (Vector3) { 0, 3.2f, .9f }, (Vector3) { 3.4f, .18f, 1.9f }, trim, false);
        box(k, (Vector3) { 0, .075f, 1.5f }, (Vector3) { 3.4f, .15f, 2.8f }, stone, false);
        if (length > 14 && street_distance(middle) < 26 && space(k, 4.5f, 1.25f, 5, 2.4f)) {
            ++k->shops;
            box(k, (Vector3) { 4.5f, 1.8f, .14f }, (Vector3) { 5.2f, 3.5f, .28f }, trim, false);
            for (int bay = 0; bay < 2; ++bay)
                box(k, (Vector3) { 3.25f + 2.5f * bay, 1.8f, .31f }, (Vector3) { 2.1f, 2.4f, .06f },
                    (Color) { 191, 148, 90, 255 }, true);
            box(k, (Vector3) { 4.5f, 3.55f, 1.1f }, (Vector3) { 5.4f, .2f, 2.2f }, trim, false);
            box(k, (Vector3) { 4.5f, 3.3f, 2.12f }, (Vector3) { 5.4f, .45f, .12f }, cream, false);
        }
        if (length > 15 && i % 4 == 0 && space(k, -4.6f, 2, 3.3f, 3.5f)) {
            planter(k, -4.6f, 2.8f, 2.8f, 1.1f);
            if (i % 8 == 0)
                bicycle(k, -4.6f, 1.2f, trim);
            else
                bench(k, -4.6f, 1.1f);
        }
    }
}

static bool furnished(Builder *k, float u, float v, float width, float depth)
{
    Vector3 low = { 10000, 0, 10000 }, high = { -10000, 0, -10000 };
    for (int x = -1; x <= 1; x += 2)
        for (int z = -1; z <= 1; z += 2) {
            Vector3 p = place(k, (Vector3) { u + x * width * .5f, 0, v + z * depth * .5f });
            low = Vector3Min(low, p);
            high = Vector3Max(high, p);
        }
    const WaterfrontScenery *s = k->out;
    for (size_t i = 0; i < s->count; ++i) {
        Vector3 a = s->boxes[i].vertices[0], b = a;
        for (int j = 1; j < 8; ++j) {
            a = Vector3Min(a, s->boxes[i].vertices[j]);
            b = Vector3Max(b, s->boxes[i].vertices[j]);
        }
        if (b.y > 3.9f && a.x < high.x && b.x > low.x && a.z < high.z && b.z > low.z)
            return true;
    }
    return false;
}

static void garden_court(Builder *k, float v)
{
    ++k->forecourts;
    box(k, (Vector3) { 0, .08f, v }, (Vector3) { 10, .16f, 8 }, stone, false);
    box(k, (Vector3) { 0, .2f, v + 1.5f }, (Vector3) { 8.5f, .24f, 4 },
        (Color) { 76, 109, 73, 255 }, false);
    for (int side = -1; side <= 1; side += 2) {
        tree(k, side * 3.4f, v + 1.5f);
        bench(k, side * 2.2f, v - 2.2f);
        planter(k, side * 4.4f, v - 2.8f, 1, 2);
    }
    box(k, (Vector3) { 0, .9f, v + 1 }, (Vector3) { 2.2f, .16f, 1.2f }, wood, false);
    box(k, (Vector3) { 0, .48f, v + 1 }, (Vector3) { 1.2f, .8f, .6f }, metal, false);
    for (int side = -1; side <= 1; side += 2) {
        chair(k, side * .65f, v - .1f, -1);
        chair(k, side * .65f, v + 2.1f, 1);
    }
}

static void build_forecourts(Builder *k, const Mesh3D *city)
{
    // Larger purposeful groups carry across a whole frame, where individual chairs
    // disappear. These are inferred uses of empty land, not surveyed businesses.
    for (size_t i = 0; i < city->triangle_count; ++i) {
        const Triangle3D *t = &city->triangles[i];
        Vector3 a = city->vertices[t->vertices[0]], c = city->vertices[t->vertices[1]];
        Vector3 b = city->vertices[t->vertices[2]];
        if (fabsf(a.y - 3.5f) > .001f || fabsf(b.y - a.y) > .001f || c.y < 12)
            continue;
        float length = Vector3Distance(a, b);
        Vector3 p = Vector3Scale(Vector3Add(a, b), .5f);
        if (length < 12 || p.x < -330 || p.x > 380 || p.z < -220 || p.z > 460)
            continue;
        Vector3 along = Vector3Scale(Vector3Subtract(b, a), 1 / length);
        frame(k, p, Vector3Negate(along), (Vector3) { along.z, 0, -along.x });
        if (!space(k, 0, 8.5f, 10, 8) || furnished(k, 0, 8.5f, 10, 8))
            continue;
        if (k->forecourts % 3 == 0 && street_distance(p) < 45) {
            ++k->forecourts;
            box(k, (Vector3) { 0, .04f, 8.5f }, (Vector3) { 10, .08f, 8 }, stone, false);
            Vector3 parking_origin = place(k, (Vector3) { 0, 0, 7.5f });
            Vector3 u = k->axis_u, v = k->axis_v;
            frame(k, parking_origin, u, v);
            for (int car = -1; car <= 1; ++car)
                parked_car(k, car * 3.2f, k->forecourts + car);
            planter(k, -3.2f, 3.7f, 2.5f, 1);
            planter(k, 3.2f, 3.7f, 2.5f, 1);
        } else {
            garden_court(k, 8.5f);
        }
    }
}

// Local heights are relative to the 3.5 m quay. These fittings hug the mapped wall
// (at most 0.42 m outboard); no new obstacle or berth extends into the boat channel.
static void waterline(Builder *k, float span, bool access)
{
    size_t first = k->out->count;
    const Color wet = { 56, 77, 77, 255 };
    const Color rubber = { 40, 49, 54, 255 };
    const Color rope = { 151, 132, 98, 255 };
    // A submerged toe and a restrained tide stain, on the wall rather than floating foam.
    box(k, (Vector3) { 0, -3.28f, -.025f }, (Vector3) { span, .7f, .06f }, wet, false);
    box(k, (Vector3) { 0, -2.83f, -.018f }, (Vector3) { span, .2f, .045f },
        (Color) { 94, 109, 96, 255 }, false);
    if (access) {
        // Ladder reaches below water; raised return handles mark the rail opening.
        for (int side = -1; side <= 1; side += 2) {
            float u = side * .46f;
            beam(k, (Vector3) { u, -3.85f, -.24f }, (Vector3) { u, .75f, -.24f }, .12f,
                (Color) { 185, 157, 92, 255 });
            beam(k, (Vector3) { u, .75f, -.24f }, (Vector3) { u, .75f, .35f }, .12f, metal);
            beam(k, (Vector3) { u, .75f, .35f }, (Vector3) { u, .12f, .35f }, .12f, metal);
        }
        for (int rung = 0; rung < 12; ++rung) {
            float y = -3.65f + rung * .34f;
            beam(k, (Vector3) { -.46f, y, -.24f }, (Vector3) { .46f, y, -.24f }, .11f,
                (Color) { 179, 167, 124, 255 });
        }
    } else {
        // Paired hanging rubbing blocks, with short ropes to dedicated mooring cleats.
        for (int side = -1; side <= 1; side += 2) {
            float u = side * span * .26f;
            box(k, (Vector3) { u, -2.05f, -.22f }, (Vector3) { .52f, 1.65f, .4f }, rubber, false);
            box(k, (Vector3) { u, -2.05f, -.395f }, (Vector3) { .31f, 1.35f, .05f },
                (Color) { 61, 69, 70, 255 }, false);
            beam(k, (Vector3) { u, -1.25f, -.25f }, (Vector3) { u, .23f, .32f }, .075f, rope);
            box(k, (Vector3) { u, .22f, .4f }, (Vector3) { .23f, .4f, .25f }, metal, false);
            box(k, (Vector3) { u, .42f, .4f }, (Vector3) { .65f, .14f, .28f }, metal, false);
        }
    }
    for (size_t i = first; i < k->out->count; ++i)
        k->out->boxes[i].waterline = true;
}

static void build_quays(Builder *k, const Mesh3D *city)
{
    for (size_t i = 0; i < city->triangle_count; ++i) {
        const Triangle3D *t = &city->triangles[i];
        Vector3 a = city->vertices[t->vertices[0]], c = city->vertices[t->vertices[1]];
        Vector3 b = city->vertices[t->vertices[2]];
        if (fabsf(a.y) > .001f || fabsf(b.y) > .001f || fabsf(c.y - 3.5f) > .001f)
            continue;
        float length = Vector3Distance(a, b);
        if (length < 1)
            continue;
        Vector3 along = Vector3Scale(Vector3Subtract(b, a), 1 / length);
        Vector3 inward = { -along.z, 0, along.x };
        int sections = (int)ceilf(length / 4);
        float span = length / sections;
        for (int j = 0; j < sections; ++j) {
            Vector3 p = Vector3Add(a, Vector3Scale(along, (j + .5f) * span));
            if (p.x < -330 || p.x > 360 || p.z < -270 || p.z > 340)
                continue;
            frame(k, p, along, inward);
            if (fabsf(surface_height(k, place(k, (Vector3) { 0, 0, -1 }))) > .01f
                || !space(k, 0, 1.5f, span, 2.5f))
                continue;
            ++k->quay_sections;
            // One equipment station per ~16 m, with access ladders every ~48 m.
            // Leave intervening wall clear so the harbour keeps its quiet character.
            if (k->quay_sections % 4 == 0)
                waterline(k, span, k->quay_sections % 12 == 0);
            box(k, (Vector3) { 0, .065f, 1.5f }, (Vector3) { span, .13f, 2.5f }, stone, false);
            box(k, (Vector3) { 0, .14f, .26f }, (Vector3) { span, .28f, .22f }, cream, false);
            float post = k->quay_sections % 12 == 0 ? span * .4f : 0;
            box(k, (Vector3) { post, .52f, .6f }, (Vector3) { .24f, .9f, .24f }, metal, false);
            box(k, (Vector3) { post, .9f, .6f }, (Vector3) { .5f, .16f, .28f }, metal, false);
            if (k->quay_sections % 7 == 0 && space(k, 0, 12, 10, 8) && !furnished(k, 0, 12, 10, 8))
                garden_court(k, 12);
            // Rail sections alternate with mooring/access openings, not a sealed fence.
            if (k->quay_sections % 4 != 0) {
                beam(k, (Vector3) { -span * .5f, 1.08f, .6f }, (Vector3) { span * .5f, 1.08f, .6f },
                    .09f, metal);
                beam(k, (Vector3) { -span * .5f, .55f, .6f }, (Vector3) { span * .5f, .55f, .6f },
                    .07f, metal);
            }
            if (k->quay_sections % 5 == 0 && space(k, 0, 4.8f, 3.4f, 3.6f)
                && !furnished(k, 0, 4.8f, 3.4f, 3.6f)) {
                if (k->quay_sections % 10 == 0)
                    tree(k, 0, 4.8f);
                else {
                    bench(k, 0, 4.2f);
                    planter(k, 0, 5.7f, 2.8f, 1);
                }
            }
        }
    }
}

bool waterfront_scenery_init(WaterfrontScenery *scenery, const BoatMapDescription *map)
{
    return waterfront_scenery_init_tiled(scenery, map, WATERFRONT_TILE_SIZE);
}

bool waterfront_scenery_init_tiled(
    WaterfrontScenery *scenery, const BoatMapDescription *map, float tile_size)
{
    *scenery = (WaterfrontScenery) { .map = map, .ready = !map, .tile_size = tile_size };
    if (!(tile_size > 0)) {
        *scenery = (WaterfrontScenery) { 0 };
        return false;
    }
    if (!map)
        return true; // The basin: no map-derived or authored river scenery.
    const Mesh3D *city = map->mesh;
    if (!city || !city->triangle_count
        || !boat_map_tiles_init_land(&scenery->tiles, map, tile_size)) {
        *scenery = (WaterfrontScenery) { 0 };
        return false;
    }
    scenery->river = map == &waterfront_city_river_map;
    Builder builder = { .out = scenery,
        .origin = { -71.82f, 3.5f, 105.9f },
        .axis_u = { .99284f, 0, .11952f },
        .axis_v = { -.11952f, 0, .99284f } };
    Builder *k = &builder;
    k->surfaces = calloc(city->triangle_count, sizeof(*k->surfaces));
    if (!k->surfaces)
        goto failed;
    for (size_t i = 0; i < city->triangle_count; ++i) {
        const Triangle3D *t = &city->triangles[i];
        Vector3 a = city->vertices[t->vertices[0]], b = city->vertices[t->vertices[1]];
        Vector3 c = city->vertices[t->vertices[2]];
        if (fabsf(a.y - b.y) < .001f && fabsf(a.y - c.y) < .001f)
            k->surfaces[k->surface_count++] = (Surface) { a, b, c, Vector3Min(a, Vector3Min(b, c)),
                Vector3Max(a, Vector3Max(b, c)) };
    }
    if (!surface_grid_init(k))
        goto failed;
    // The cafe and street furniture are authored for the river crop's streets; frontages,
    // quays and forecourts are derived from whichever map is bound.
    if (scenery->river) {
        build_cafe(k);
        build_streets(k);
    }
    build_frontages(k, city);
    build_quays(k, city);
    build_forecourts(k, city);
    free(k->surfaces);
    k->surfaces = NULL;
    surface_grid_destroy(k);
    if (k->overflow)
        goto failed;
    scenery->bounds = malloc((scenery->count ? scenery->count : 1) * sizeof(*scenery->bounds));
    if (!scenery->bounds)
        goto failed;
    for (size_t i = 0; i < scenery->count; ++i) {
        const WaterfrontCornerBox *b = &scenery->boxes[i];
        scenery->bounds[i].min = scenery->bounds[i].max = b->vertices[0];
        for (int j = 1; j < 8; ++j) {
            scenery->bounds[i].min = Vector3Min(scenery->bounds[i].min, b->vertices[j]);
            scenery->bounds[i].max = Vector3Max(scenery->bounds[i].max, b->vertices[j]);
        }
    }
    scenery->ready = true;
    TraceLog(LOG_INFO,
        "City dressing: %zu boxes, %d entrances, %d shops plus cafe, %d parked cars, "
        "%d benches, %d bicycles, %d pavement / %d quay sections",
        scenery->count, k->entrances, k->shops, k->parked_cars, k->benches + 1, k->bicycles,
        k->sidewalk_sections, k->quay_sections);
    TraceLog(LOG_INFO, "City dressing: %d shared forecourts, %d additional trees", k->forecourts,
        k->trees);
    return true;
failed:
    free(k->surfaces);
    surface_grid_destroy(k);
    waterfront_scenery_destroy(scenery);
    return false;
}

void waterfront_scenery_destroy(WaterfrontScenery *scenery)
{
    free(scenery->boxes);
    free(scenery->bounds);
    boat_map_tiles_destroy(&scenery->tiles);
    *scenery = (WaterfrontScenery) { 0 };
}

bool waterfront_scenery_bind(WaterfrontScenery *scenery, const BoatMapDescription *map)
{
    if (scenery->ready && scenery->map == map)
        return true;
    float tile_size = scenery->tile_size > 0 ? scenery->tile_size : WATERFRONT_TILE_SIZE;
    waterfront_scenery_destroy(scenery);
    return waterfront_scenery_init_tiled(scenery, map, tile_size);
}

bool waterfront_scenery_clear(const WaterfrontScenery *scenery, Vector3 position, float clearance)
{
    if (!scenery->ready)
        return false;
    for (size_t i = 0; i < scenery->count; ++i) {
        Vector3 low = scenery->bounds[i].min, high = scenery->bounds[i].max;
        if (Vector3Distance(position, Vector3Clamp(position, low, high)) < clearance)
            return false;
    }
    return true;
}
