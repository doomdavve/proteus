#include "boat_water_mesh.h"
#include "raymath.h"
#include "waterfront_city.h"

#include <math.h>
#include <stdlib.h>

typedef struct {
    Vector3 *vertices;
    Triangle3D *triangles;
    size_t count, capacity;
    Mesh3D mesh;
} Surface;
static Surface surfaces[2];
static Surface candidate_surface;
static Surface patches[4];
static Vector2 patch_centers[4];

static bool surface_triangle(Surface *s, Vector3 a, Vector3 b, Vector3 c)
{
    if (s->count == s->capacity) {
        size_t capacity = s->capacity ? s->capacity * 2 : 1024;
        Vector3 *v = realloc(s->vertices, capacity * 3 * sizeof(*v));
        if (!v)
            return false;
        s->vertices = v;
        Triangle3D *t = realloc(s->triangles, capacity * sizeof(*t));
        if (!t)
            return false;
        s->triangles = t;
        s->capacity = capacity;
    }
    unsigned int i = (unsigned int)(s->count * 3);
    s->vertices[i] = a;
    s->vertices[i + 1] = b;
    s->vertices[i + 2] = c;
    s->triangles[s->count++] = (Triangle3D) { .vertices = { i, i + 1, i + 2 } };
    return true;
}

static bool subdivide(Surface *s, Vector3 a, Vector3 b, Vector3 c, int depth)
{
    // CPU reference retains its original two-unit tessellation and displacement.
    float ab = Vector3DistanceSqr(a, b), bc = Vector3DistanceSqr(b, c);
    float ca = Vector3DistanceSqr(c, a);
    if (depth < 24 && fmaxf(ab, fmaxf(bc, ca)) > 4) {
        if (bc > ab && bc >= ca)
            return subdivide(s, b, c, a, depth);
        if (ca > ab && ca > bc)
            return subdivide(s, c, a, b, depth);
        Vector3 mid = Vector3Scale(Vector3Add(a, b), .5f);
        return subdivide(s, a, mid, c, depth + 1) && subdivide(s, mid, b, c, depth + 1);
    }
    return surface_triangle(s, a, b, c);
}

void boat_water_mesh_destroy(void)
{
    free(candidate_surface.vertices);
    free(candidate_surface.triangles);
    candidate_surface = (Surface) { 0 };
    for (int i = 0; i < 2; ++i) {
        free(surfaces[i].vertices);
        free(surfaces[i].triangles);
        surfaces[i] = (Surface) { 0 };
        free(patches[i].vertices);
        free(patches[i].triangles);
        patches[i] = (Surface) { 0 };
        free(patches[i + 2].vertices);
        free(patches[i + 2].triangles);
        patches[i + 2] = (Surface) { 0 };
    }
}

// Clip a convex polygon against one axis-aligned cell edge, retaining input winding.
static int clip_edge(
    const Vector3 *input, int count, Vector3 *output, int axis, float edge, bool greater)
{
    int written = 0;
    if (!count)
        return 0;
    Vector3 a = input[count - 1];
    float da = (axis ? a.z : a.x) - edge;
    for (int i = 0; i < count; ++i) {
        Vector3 b = input[i];
        float db = (axis ? b.z : b.x) - edge;
        bool inside_a = greater ? da >= 0 : da <= 0;
        bool inside_b = greater ? db >= 0 : db <= 0;
        if (inside_a != inside_b) {
            Vector3 p = Vector3Lerp(a, b, da / (da - db));
            if (axis)
                p.z = edge;
            else
                p.x = edge;
            output[written++] = p;
        }
        if (inside_b)
            output[written++] = b;
        a = b;
        da = db;
    }
    return written;
}

// A bounded 48 m central square uses .75 m cells; outer cells remain 1.5 m.
// One shared axis partition avoids cracks/T junctions at the density transition.
static float patch_coordinate(int cell, bool fine)
{
    if (!fine)
        return cell * 1.5f;
    if (cell < 48)
        return cell * 1.5f;
    if (cell < 112)
        return 72 + (cell - 48) * .75f;
    return 120 + (cell - 112) * 1.5f;
}

static int patch_cell(float coordinate, bool fine)
{
    if (!fine)
        return (int)floorf(coordinate / 1.5f);
    if (coordinate < 72)
        return (int)floorf(coordinate / 1.5f);
    if (coordinate < 120)
        return 48 + (int)floorf((coordinate - 72) / .75f);
    return 112 + (int)floorf((coordinate - 120) / 1.5f);
}

static bool patch_triangle(Surface *s, Vector3 a, Vector3 b, Vector3 c, Vector2 origin, bool fine)
{
    int last = fine ? 159 : 127;
    int min_z = patch_cell(fminf(a.z, fminf(b.z, c.z)) - origin.y, fine);
    int max_z = patch_cell(fmaxf(a.z, fmaxf(b.z, c.z)) - origin.y, fine);
    min_z = min_z < 0 ? 0 : min_z;
    max_z = max_z > last ? last : max_z;
    for (int z = min_z; z <= max_z; ++z) {
        float bottom = origin.y + patch_coordinate(z, fine);
        Vector3 triangle[3] = { a, b, c }, strip[12], temporary[12];
        int strip_count = clip_edge(triangle, 3, temporary, 1, bottom, true);
        strip_count = clip_edge(
            temporary, strip_count, strip, 1, origin.y + patch_coordinate(z + 1, fine), false);
        if (strip_count < 3)
            continue;
        // A row-clipped triangle often spans only a few cells even when its whole AABB
        // is large. Avoid clipping every empty cell in long diagonal source triangles.
        float min = strip[0].x, max = strip[0].x;
        for (int j = 1; j < strip_count; ++j) {
            min = fminf(min, strip[j].x);
            max = fmaxf(max, strip[j].x);
        }
        int min_x = patch_cell(min - origin.x, fine);
        int max_x = patch_cell(max - origin.x, fine);
        min_x = min_x < 0 ? 0 : min_x;
        max_x = max_x > last ? last : max_x;
        for (int x = min_x; x <= max_x; ++x) {
            float left = origin.x + patch_coordinate(x, fine);
            Vector3 p[12], q[12];
            int count = clip_edge(strip, strip_count, q, 0, left, true);
            count = clip_edge(q, count, p, 0, origin.x + patch_coordinate(x + 1, fine), false);
            for (int j = 1; j + 1 < count; ++j) {
                Vector3 ab = Vector3Subtract(p[j], p[0]);
                Vector3 ac = Vector3Subtract(p[j + 1], p[0]);
                if (fabsf(ab.x * ac.z - ab.z * ac.x) < 1e-7f)
                    continue;
                if (!surface_triangle(s, p[0], p[j], p[j + 1]))
                    return false;
            }
        }
    }
    return true;
}

static const Mesh3D *water_patch(Vector2 world_center, bool mapped, bool fine)
{
    int index = mapped + 2 * fine;
    Surface *s = &patches[index];
    Vector2 center = { roundf(world_center.x / 12) * 12, roundf(world_center.y / 12) * 12 };
    if (s->count && Vector2Equals(center, patch_centers[index]))
        return &s->mesh;
    s->count = 0;
    Vector2 origin = { center.x - 96, center.y - 96 };
    bool ok = true;
    if (mapped) {
        const Mesh3D *city = waterfront_city_mesh();
        for (size_t i = 0; ok && i < city->triangle_count; ++i) {
            Triangle3D t = city->triangles[i];
            Vector3 a = city->vertices[t.vertices[0]], b = city->vertices[t.vertices[1]];
            Vector3 c = city->vertices[t.vertices[2]];
            if (a.y != 0 || b.y != 0 || c.y != 0)
                break;
            ok = patch_triangle(s, a, b, c, origin, fine);
        }
    } else {
        ok = patch_triangle(s, (Vector3) { -120, 0, -80 }, (Vector3) { -120, 0, 80 },
                 (Vector3) { 120, 0, 80 }, origin, fine)
            && patch_triangle(s, (Vector3) { -120, 0, -80 }, (Vector3) { 120, 0, 80 },
                (Vector3) { 120, 0, -80 }, origin, fine);
    }
    if (!ok) {
        s->count = 0;
        return NULL;
    }
    patch_centers[index] = center;
    s->mesh = (Mesh3D) { s->vertices, s->triangles, s->count };
    return &s->mesh;
}

const Mesh3D *boat_water_patch(Vector2 center, bool mapped)
{
    return water_patch(center, mapped, false);
}

const Mesh3D *boat_water_patch_arcade(Vector2 center, bool mapped)
{
    return water_patch(center, mapped, true);
}

// Same presentation-only phase/energy proxy as waveCrestFoam in the water shader.
static float wave_crest_foam(float height, Vector2 gradient, float k)
{
    float kh = k * height;
    float energy = sqrtf(kh * kh + Vector2LengthSqr(gradient));
    float phase = Clamp((kh / fmaxf(energy, .0001f) - .90f) / .09f, 0, 1);
    float strength = Clamp((energy - .12f) / .38f, 0, 1);
    return phase * phase * (3 - 2 * phase) * strength * strength * (3 - 2 * strength);
}

const Mesh3D *boat_water_surface(BoatWater water, double time, bool mapped)
{
    Surface *s = &surfaces[mapped];
    if (!s->count) {
        bool ok = true;
        if (mapped) {
            const Mesh3D *city = waterfront_city_mesh();
            for (size_t i = 0; ok && i < city->triangle_count; ++i) {
                Triangle3D t = city->triangles[i];
                Vector3 a = city->vertices[t.vertices[0]], b = city->vertices[t.vertices[1]];
                Vector3 c = city->vertices[t.vertices[2]];
                if (a.y != 0 || b.y != 0 || c.y != 0)
                    break; // Imported water triangles precede land.
                ok = subdivide(s, a, b, c, 0);
            }
        } else {
            ok = subdivide(s, (Vector3) { -120, 0, -80 }, (Vector3) { -120, 0, 80 },
                     (Vector3) { 120, 0, 80 }, 0)
                && subdivide(s, (Vector3) { -120, 0, -80 }, (Vector3) { 120, 0, 80 },
                    (Vector3) { 120, 0, -80 }, 0);
        }
        if (!ok) {
            boat_water_mesh_destroy();
            return NULL;
        }
    }
    BoatWater ambient = water;
    ambient.wake_amplitude = 0;
    ambient.player_wake_enabled = false;
    ambient.objects = NULL;
    ambient.traffic_objects = NULL;
    ambient.vessel.height = 0;
    for (size_t i = 0; i < s->count; ++i) {
        Vector3 centre = { 0 };
        for (int j = 0; j < 3; ++j) {
            Vector3 *v = &s->vertices[i * 3 + j];
            v->y = boat_water_query_borrowed(&water, (Vector2) { -v->x, v->z }, time).height;
            centre = Vector3Add(centre, Vector3Scale(*v, 1.0f / 3));
        }
        BoatWaterSample q
            = boat_water_query_borrowed(&water, (Vector2) { -centre.x, centre.z }, time);
        // Restrained slope light; geometry and GPU reflections carry the wave shape.
        float light = Clamp(.95f + .55f * (-q.normal.x + q.normal.z), .85f, 1.05f);
        Color base = mapped ? (Color) { 45, 79, 104, 255 } : (Color) { 36, 107, 125, 255 };
        Vector2 point = { -centre.x, centre.z };
        BoatStormSample storm = boat_water_storm_query(water.storm, point, time);
        BoatVesselWakeSample vessel = boat_vessel_wake_query(water.vessel, point, time);
        float wake_height = q.height - storm.height - vessel.height;
        float crest = 0;
        if (water.objects || water.traffic_objects)
            crest = .65f * Clamp(q.crest_foam - Clamp(storm.height / .8f, 0, 1), 0, 1)
                * Clamp(wake_height / .3f, 0, 1);
        else if (water.wake_amplitude > 0 || water.player_wake_enabled)
            crest = .65f
                * Clamp((q.height - vessel.height
                            - boat_water_query_borrowed(
                                &ambient, (Vector2) { -centre.x, centre.z }, time)
                                .height)
                        / .38f,
                    0, 1);
        crest = fmaxf(crest, .28f * wave_crest_foam(storm.height, storm.gradient, .42f));
        if (water.vessel.wavelength > 0)
            crest = fmaxf(crest,
                .38f
                    * wave_crest_foam(
                        vessel.height, vessel.gradient, 2 * PI / water.vessel.wavelength));
        s->triangles[i].color = (Color) { (unsigned char)Lerp(base.r * light, 166, crest),
            (unsigned char)Lerp(base.g * light, 199, crest),
            (unsigned char)Lerp(base.b * light, 204, crest), 255 };
    }
    s->mesh = (Mesh3D) { s->vertices, s->triangles, s->count };
    return &s->mesh;
}

const Mesh3D *boat_water_surface_field(BoatWater water, const BoatWake *field, double time)
{
    Surface *s = &candidate_surface;
    if (!s->count) {
        for (int z = 0; z < 215; ++z)
            for (int x = 0; x < 321; ++x) {
                float left = x == 0 ? -120 : -119.625f + (x - 1) * .75f;
                float right = x == 320 ? 120 : -119.625f + x * .75f;
                float bottom = z == 0 ? -80 : -79.875f + (z - 1) * .75f;
                float top = z == 214 ? 80 : -79.875f + z * .75f;
                Vector3 a = { left, 0, bottom }, b = { left, 0, top }, c = { right, 0, top },
                        d = { right, 0, bottom };
                if (!surface_triangle(s, a, b, c) || !surface_triangle(s, a, c, d)) {
                    s->count = 0; // Retry the complete basin after an allocation failure.
                    return NULL;
                }
            }
    }
    for (size_t i = 0; i < s->count; ++i) {
        Vector3 centre = { 0 };
        for (int j = 0; j < 3; ++j) {
            Vector3 *v = &s->vertices[3 * i + j];
            v->y = boat_water_query_field(water, field, (Vector2) { -v->x, v->z }, time).height;
            centre = Vector3Add(centre, Vector3Scale(*v, 1.0f / 3));
        }
        BoatWaterSample q
            = boat_water_query_field(water, field, (Vector2) { -centre.x, centre.z }, time);
        float light = Clamp(.95f + .55f * (-q.normal.x + q.normal.z), .85f, 1.05f);
        float crest = .65f * Clamp(q.height / .38f, 0, 1);
        s->triangles[i].color = (Color) { (unsigned char)Lerp(36 * light, 166, crest),
            (unsigned char)Lerp(107 * light, 199, crest),
            (unsigned char)Lerp(125 * light, 204, crest), 255 };
    }
    s->mesh = (Mesh3D) { s->vertices, s->triangles, s->count };
    return &s->mesh;
}
