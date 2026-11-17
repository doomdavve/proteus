#include "wake_transport.h"
#include "raymath.h"
#include <math.h>

static float dilation(float x) { return x <= 0 ? 1 : 1 + x * x * x / (.0625f + x * x); }

BoatWakeObjectSample r2_transport_piece(
    const BoatWakeObject *o, float k, Vector2 point, double seconds)
{
    if (k == 0)
        return r2_piece(o, R2_BASELINE, point, seconds);
    if (!o->amplitude || seconds < o->born / 120.0
        || seconds >= (o->born + BOAT_OBJECT_LIFE_TICKS) / 120.0)
        return (BoatWakeObjectSample) { 0 };
    Vector2 t = o->direction, n = { -t.y, t.x }, p = Vector2Subtract(point, o->position);
    float v = Vector2DotProduct(p, n);
    if (k * v >= 0)
        return r2_piece(o, R2_BASELINE, point, seconds);
    float u = Vector2DotProduct(p, t), x = -k * v;
    float denominator = .0625f + x * x, d = dilation(x);
    float dv = -k * x * x * (.1875f + x * x) / (denominator * denominator);
    float s = u / d;
    BoatWakeObjectSample h = r2_piece_coordinates(o, R2_BASELINE, s, v, seconds);
    float norm2 = Vector2LengthSqr(t);
    float hs = Vector2DotProduct(h.gradient, t) / norm2;
    float hv = Vector2DotProduct(h.gradient, n) / norm2;
    float hu = hs / (d * d), normal = (hv - dv / d * (s * hs + h.height)) / d;
    h.gradient = Vector2Add(Vector2Scale(t, hu), Vector2Scale(n, normal));
    h.height /= d;
    h.vertical_velocity /= d;
    h.crest_foam /= d; // Cosmetic policy only, not a mechanical/conservation criterion.
    return h;
}

BoatWakeObjectSample r2_transport_query(const R2Capture *c, Vector2 point, double seconds)
{
    BoatWakeObjectSample sum = { 0 };
    for (unsigned e = 0; e < BOAT_OBJECT_BOATS; ++e)
        for (unsigned i = 0; i < BOAT_OBJECT_PER_BOAT; ++i) {
            const BoatWakeObject *o = &c->field.records[e * R2_SLOTS + i];
            if (!o->amplitude || seconds < o->born / 120.0
                || seconds >= (o->born + BOAT_OBJECT_LIFE_TICKS) / 120.0)
                continue;
            float k = c->descriptors[e * BOAT_OBJECT_PER_BOAT + i].curvature;
            BoatWakeObjectSample h = r2_transport_piece(o, k, point, seconds);
            sum.height += h.height;
            sum.gradient = Vector2Add(sum.gradient, h.gradient);
            sum.vertical_velocity += h.vertical_velocity;
            sum.crest_foam += h.crest_foam;
        }
    return sum;
}

void r2_transport_bounds(const R2Capture *c, double seconds, Vector2 *low, Vector2 *high)
{
    *low = (Vector2) { INFINITY, INFINITY };
    *high = (Vector2) { -INFINITY, -INFINITY };
    for (unsigned e = 0; e < BOAT_OBJECT_BOATS; ++e)
        for (unsigned i = 0; i < BOAT_OBJECT_PER_BOAT; ++i) {
            const BoatWakeObject *o = &c->field.records[e * R2_SLOTS + i];
            if (!o->amplitude || seconds < o->born / 120.0
                || seconds >= (o->born + BOAT_OBJECT_LIFE_TICKS) / 120.0)
                continue;
            float age = (float)(seconds - o->born / 120.0);
            float r = .9f + 2.4f * fmaxf(0, age + o->length * o->inverse_speed) + 2;
            float d = dilation(fabsf(c->descriptors[e * BOAT_OBJECT_PER_BOAT + i].curvature) * r);
            Vector2 t = o->direction;
            Vector2 extent = { fabsf(t.x) * d * o->length + fabsf(t.y) * r,
                fabsf(t.y) * d * o->length + fabsf(t.x) * r };
            // The captured float direction is only approximately unit length. Invert its
            // actual orthogonal coordinate frame and round the support box outward.
            extent = Vector2Add(
                Vector2Scale(extent, 1 / Vector2LengthSqr(t)), (Vector2) { .001f, .001f });
            low->x = fminf(low->x, o->position.x - extent.x);
            low->y = fminf(low->y, o->position.y - extent.y);
            high->x = fmaxf(high->x, o->position.x + extent.x);
            high->y = fmaxf(high->y, o->position.y + extent.y);
        }
}
