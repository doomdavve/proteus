#include "boat_cargo_view.h"
#include "boat_cargo.h"
#include "boat_water_mesh.h"
#include "raymath.h"

#include <math.h>

static float coordinate(Vector3 p, int axis) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }

static int clip(const Vector3 *input, int count, Vector3 *output, int axis, float edge, bool lower)
{
    int used = 0;
    Vector3 a = input[count - 1];
    float da = (coordinate(a, axis) - edge) * (lower ? 1 : -1);
    for (int i = 0; i < count; ++i) {
        Vector3 b = input[i];
        float db = (coordinate(b, axis) - edge) * (lower ? 1 : -1);
        if ((da > 0 && db < 0) || (da < 0 && db > 0))
            output[used++] = Vector3Lerp(a, b, da / (da - db));
        if (db >= 0)
            output[used++] = b;
        a = b;
        da = db;
    }
    return used;
}

bool boat_cargo_decal(BoatModel *out, const Mesh3D *receivers, Vector3 cargo,
    const BoatWater *rest_water, double now, Vector2 focus, Color mask)
{
    const float half = .5f;
    for (size_t i = 0; i < receivers->triangle_count; ++i) {
        Triangle3D triangle = receivers->triangles[i];
        Vector3 p[12], q[12]; // A triangle clipped by five planes needs at most eight vertices.
        for (int j = 0; j < 3; ++j)
            p[j] = receivers->vertices[triangle.vertices[j]];
        if (fmaxf(p[0].x, fmaxf(p[1].x, p[2].x)) < cargo.x - half
            || fminf(p[0].x, fminf(p[1].x, p[2].x)) > cargo.x + half
            || fmaxf(p[0].z, fmaxf(p[1].z, p[2].z)) < cargo.z - half
            || fminf(p[0].z, fminf(p[1].z, p[2].z)) > cargo.z + half)
            continue;
        if (rest_water) {
            for (int j = 0; j < 3; ++j) {
                float distance = Vector2Distance((Vector2) { p[j].x, p[j].z }, focus);
                float fade = Clamp((distance - BOAT_WATER_PATCH_FULL)
                        / (BOAT_WATER_PATCH_RADIUS - BOAT_WATER_PATCH_FULL),
                    0, 1);
                fade = 1 - fade * fade * (3 - 2 * fade);
                p[j].y = boat_water_query_borrowed(rest_water, (Vector2) { -p[j].x, p[j].z }, now)
                             .height
                    * fade;
            }
        }
        Vector3 normal
            = Vector3CrossProduct(Vector3Subtract(p[1], p[0]), Vector3Subtract(p[2], p[0]));
        float length = Vector3Length(normal);
        if (length < .000001f || normal.y < .15f * length)
            continue;
        int count = clip(p, 3, q, 0, cargo.x - half, true);
        if (!count)
            continue;
        count = clip(q, count, p, 0, cargo.x + half, false);
        if (!count)
            continue;
        count = clip(p, count, q, 2, cargo.z - half, true);
        if (!count)
            continue;
        count = clip(q, count, p, 2, cargo.z + half, false);
        if (!count)
            continue;
        count = clip(p, count, q, 1, cargo.y - BOAT_CARGO_HALF + .08f, false);
        if (count < 3)
            continue;
        Color color = mask;
        if (!mask.a) {
            float gap = cargo.y - BOAT_CARGO_HALF - q[0].y;
            float factor = Lerp(.32f, .6f, Clamp(gap / 4, 0, 1));
            Color albedo
                = rest_water || !triangle.color.a ? (Color) { 45, 79, 104, 255 } : triangle.color;
            color = (Color) { (unsigned char)(albedo.r * factor),
                (unsigned char)(albedo.g * factor), (unsigned char)(albedo.b * factor), 255 };
        }
        for (int j = 0; j < count; ++j)
            q[j].y += .012f; // A small separation from the exact receiver, not a wave-height guess.
        for (int j = 1; j + 1 < count; ++j)
            boat_model_triangle(out, q[0], q[j], q[j + 1], color);
        if (out->overflow)
            return false;
    }
    return true;
}
