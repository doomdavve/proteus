#include "render/boat_cargo_view.h"
#include "core/boat_cargo.h"
#include "raylib.h"
#include "raymath.h"
#include "render/boat_water_mesh.h"
#include "render/waterfront.h"

#include <math.h>
#include <string.h>

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

// The sandbox's work-site, crate and crane geometry, shared by the harness and the game (SV1e).
static const Color yellow = { 241, 183, 57, 255 }, teal = { 45, 116, 127, 255 };
static const Color ink = { 20, 31, 42, 255 }, green = { 99, 216, 156, 255 };

static const BoatCargoCrane *active_crane(const BoatCargoView *v)
{
    return &v->sandbox->cranes[v->sandbox->active];
}

static void box(BoatModel *m, Vector3 p, Vector3 size, Color color)
{
    boat_model_box(m, p, size, color);
}

static void boat_box(BoatModel *m, const Boat *boat, Vector3 p, Vector3 size, Color color)
{
    size_t first = m->mesh.triangle_count * 3;
    boat_model_box(m, p, size, color);
    for (size_t i = first; i < m->mesh.triangle_count * 3; ++i)
        m->vertices[i] = boat_pose_attachment(boat, m->vertices[i]);
}

// The scene mesh is built relative to the first site's berth, which boat_cargo_view_scene() adds
// back last.
static Vector3 site_vertex(const BoatCargoView *v, const BoatCargoCrane *c, Vector3 local)
{
    return Vector3Add(boat_cargo_site_direction(c, local),
        Vector3Subtract(c->origin, v->sandbox->cranes[0].origin));
}

static void to_site(const BoatCargoView *v, BoatModel *m, size_t first, const BoatCargoCrane *c)
{
    for (size_t i = first; i < m->mesh.triangle_count * 3; ++i)
        m->vertices[i] = site_vertex(v, c, m->vertices[i]);
}

static Vector3 berth_point(const BoatCargoView *v, const BoatCargoCrane *c, float x, float z)
{
    Vector3 p = boat_cargo_site_point(c, (Vector3) { x, 0, z });
    p.y = boat_water_query_borrowed(
              &v->world->water, (Vector2) { -p.x, p.z }, boat_tick_seconds(v->tick))
              .height
        + .1f;
    return Vector3Subtract(p, v->sandbox->cranes[0].origin);
}

static void berth_line(const BoatCargoView *v, const BoatCargoCrane *c, BoatModel *m, float ax,
    float az, float bx, float bz, Color color)
{
    boat_model_beam(m, berth_point(v, c, ax, az), berth_point(v, c, bx, bz), .065f, color);
}

// A site's platform and crane. A receiving platform's pad is marked as the delivery surface.
static void site_parts(const BoatCargoView *v, BoatModel *m, unsigned index)
{
    const BoatCargoSandbox *s = v->sandbox;
    const BoatCargoCrane *crane = &s->cranes[index];
    size_t first = m->mesh.triangle_count * 3;
    bool receiving = crane->preset == BOAT_SCENE_WORK_RECEIVING_PLATFORM;
    box(m, (Vector3) { -5, 0, 1 }, (Vector3) { 3, 2, 6 }, (Color) { 126, 134, 132, 255 });
    box(m, (Vector3) { -5, 1.01f, 0 }, (Vector3) { 2, .03f, 2.8f },
        receiving ? (Color) { 220, 207, 144, 255 } : teal);
    for (int side = -1; receiving && side <= 1; side += 2) {
        box(m, (Vector3) { -5 + side * .95f, 1.03f, 0 }, (Vector3) { .1f, .03f, 2.8f }, yellow);
        box(m, (Vector3) { -5, 1.03f, side * 1.35f }, (Vector3) { 2, .03f, .1f }, yellow);
    }
    for (int i = 0; i < 4; ++i)
        box(m, (Vector3) { -3.48f, .85f, -1.5f + i * 1.5f }, (Vector3) { .07f, .25f, .6f }, yellow);
    box(m, (Vector3) { -4, 1.3f, 3 }, (Vector3) { .9f, .6f, .9f }, ink);
    box(m, (Vector3) { -4, 3.3f, 3 }, (Vector3) { .35f, 4.4f, .35f }, yellow);
    Vector3 hook = index == s->active
        ? boat_cargo_hook(s)
        : Vector3Subtract(boat_cargo_tip(crane), (Vector3) { 0, crane->rope, 0 });
    Vector3 tip = boat_cargo_site_local(crane, boat_cargo_tip(crane));
    hook = boat_cargo_site_local(crane, hook);
    boat_model_beam(m, (Vector3) { -4, 5.5f, 3 }, tip, .22f, yellow);
    boat_model_beam(m, (Vector3) { -4, 4.6f, 3 }, tip, .07f, ink);
    boat_model_beam(m, tip, hook, .035f, ink);
    box(m, hook, (Vector3) { .16f, .23f, .16f }, yellow);
    to_site(v, m, first, crane);
}

// One evaluated mesh for both renderers, including rope, cargo and support markers.
size_t boat_cargo_view_scene(const BoatCargoView *v, BoatModel *m)
{
    const BoatCargoSandbox *s = v->sandbox;
    Vector3 origin = s->cranes[0].origin;
    BoatCargo cargo = s->cargo;
    cargo.position = Vector3Subtract(cargo.position, origin);
    const BoatCargo *c = &cargo;
    boat_model_begin(m);
    for (unsigned i = 0; i < s->crane_count; ++i)
        if (!s->cranes[i].hidden)
            site_parts(v, m, i);

    Boat local_boat = *v->boat;
    local_boat.position = Vector2Subtract(local_boat.position, boat_cargo_berth(&s->cranes[0]));
    const Boat *b = &local_boat;
    boat_box(m, b, (Vector3) { 0, .18f, 0 }, (Vector3) { 2.6f, .85f, 4.6f }, teal);
    boat_box(m, b, (Vector3) { 0, .69f, 0 }, (Vector3) { 2.6f, .12f, 3.8f },
        (Color) { 176, 151, 106, 255 });
    boat_box(m, b, (Vector3) { 0, 1.05f, -1.95f }, (Vector3) { 1.1f, .8f, .45f }, ink);
    boat_box(m, b, (Vector3) { 0, .45f, 2.4f }, (Vector3) { .5f, .7f, .5f }, ink);
    for (int side = -1; side <= 1; side += 2) {
        boat_box(m, b, (Vector3) { side * 1.32f, .35f, -.9f }, (Vector3) { .14f, .45f, .6f }, ink);
        boat_box(m, b, (Vector3) { side * 1.32f, .35f, 1.1f }, (Vector3) { .14f, .45f, .6f }, ink);
        boat_box(m, b, boat_cargo_deck_aim_point(side < 0 ? 2 : 3), (Vector3) { .85f, .025f, .85f },
            (Color) { 220, 207, 144, 255 });
    }
    if (s->moored) {
        boat_model_beam(m, site_vertex(v, active_crane(v), (Vector3) { -3.4f, 1, -1 }),
            boat_pose_attachment(b, (Vector3) { -1.25f, .8f, -1 }), .04f, yellow);
        boat_model_beam(m, site_vertex(v, active_crane(v), (Vector3) { -3.4f, 1, 1.5f }),
            boat_pose_attachment(b, (Vector3) { -1.25f, .8f, 1.5f }), .04f, yellow);
    }
    size_t receivers = m->mesh.triangle_count;
    size_t start = receivers * 3;
    box(m, (Vector3) { 0 }, (Vector3) { .8f, .8f, .8f },
        c->state == BOAT_CARGO_SECURED ? green : (Color) { 196, 111, 59, 255 });
    box(m, (Vector3) { 0, -.32f, 0 }, (Vector3) { .92f, .12f, .92f }, (Color) { 116, 78, 50, 255 });
    box(m, (Vector3) { 0, .05f, -.405f }, (Vector3) { .18f, .45f, .025f }, yellow);
    Matrix rotation = MatrixRotateXYZ(c->rotation);
    for (size_t i = start; i < m->mesh.triangle_count * 3; ++i)
        m->vertices[i] = Vector3Add(c->position, Vector3Transform(m->vertices[i], rotation));
    if (c->state == BOAT_CARGO_SECURED) {
        for (int side = -1; side <= 1; side += 2)
            boat_model_beam(m, boat_pose_attachment(b, (Vector3) { side * 1.2f, .78f, .65f }),
                Vector3Add(c->position, (Vector3) { 0, .43f, 0 }), .04f, ink);
    }
    BoatCargoMoorStatus berth = boat_cargo_moor_status(s, v->boat);
    for (unsigned i = 0; i < s->crane_count; ++i) {
        const BoatCargoCrane *crane = &s->cranes[i];
        if (crane->hidden)
            continue;
        Color berth_color = berth.site == i && berth.reason == BOAT_CARGO_MOOR_READY
            ? green
            : (Color) { 178, 220, 220, 255 };
        // The outline marks the actual final hull pose; the bow arrow faces the site heading.
        berth_line(v, crane, m, -1.45f, -2.45f, 1.45f, -2.45f, berth_color);
        berth_line(v, crane, m, -1.45f, 2.45f, 1.45f, 2.45f, berth_color);
        berth_line(v, crane, m, -1.45f, -2.45f, -1.45f, 2.45f, berth_color);
        berth_line(v, crane, m, 1.45f, -2.45f, 1.45f, 2.45f, berth_color);
        berth_line(v, crane, m, 0, -3.2f, -.5f, -2.6f, berth_color);
        berth_line(v, crane, m, 0, -3.2f, .5f, -2.6f, berth_color);
        berth_line(v, crane, m, 0, -3.2f, 0, -2.2f, berth_color);
        // Turning references and a berth approach, away from the work surface.
        size_t first = m->mesh.triangle_count * 3;
        for (int side = -1; side <= 1; side += 2) {
            box(m, (Vector3) { side * 11, .35f, -18 }, (Vector3) { .5f, .8f, .5f }, yellow);
        }
        to_site(v, m, first, crane);
    }
    for (size_t i = 0; i < m->mesh.triangle_count * 3; ++i)
        m->vertices[i] = Vector3Add(m->vertices[i], origin);
    return receivers;
}

static bool site_camera_clear(const BoatCargoView *v, Vector3 p)
{
    for (unsigned i = 0; i < v->sandbox->crane_count; ++i) {
        const BoatCargoCrane *crane = &v->sandbox->cranes[i];
        if (crane->hidden)
            continue;
        Vector3 q = boat_cargo_site_local(crane, p);
        if (q.x > -7 && q.x < -3 && q.z > -2.5f && q.z < 4.5f && q.y < 1.5f)
            return false;
        if (q.x > -4.6f && q.x < -3.4f && q.z > 2.4f && q.z < 3.6f && q.y < 6)
            return false;
        Vector3 a = boat_cargo_site_point(crane, (Vector3) { -4, 5.5f, 3 });
        Vector3 b = boat_cargo_tip(crane), axis = Vector3Subtract(b, a);
        float t
            = Clamp(Vector3DotProduct(Vector3Subtract(p, a), axis) / Vector3LengthSqr(axis), 0, 1);
        if (Vector3Distance(p, Vector3Add(a, Vector3Scale(axis, t))) <= .6f)
            return false;
    }
    return true;
}

View3D boat_cargo_view_camera(const BoatCargoView *v, const WaterfrontScenery *scenery)
{
    Vector3 centre = boat_cargo_site_point(active_crane(v), (Vector3) { -2, 2.2f, 1 });
    Vector3 eye = boat_cargo_site_point(active_crane(v), (Vector3) { 8.5f, 9, 12 });
    if (!v->crane_view) {
        centre = boat_pose_attachment(v->boat, (Vector3) { 0, 1, 0 });
        eye = Vector3Add(
            centre, (Vector3) { sinf(v->boat->heading) * 13, 9, cosf(v->boat->heading) * 13 });
    }
    if (v->world->definition->mapped) {
        Vector3 safe = centre;
        for (int i = 1; i <= 80; ++i) {
            Vector3 p = Vector3Lerp(centre, eye, i / 80.0f);
            if (!site_camera_clear(v, p) || !waterfront_scenery_clear(scenery, p, .45f)
                || !waterfront_harbor_camera_clear_scene(v->world->scene, p, .45f)
                || !waterfront_traffic_camera_clear_scene(
                    v->world->scene, p, boat_tick_seconds(v->tick), .45f))
                break;
            safe = p;
        }
        eye = Vector3Distance(safe, centre) > .05f ? safe
                                                   : Vector3Add(centre, (Vector3) { 0, .6f, 0 });
    }
    return (View3D) { eye, centre, { 0, 1, 0 }, 55 * DEG2RAD, .1f,
        v->world->definition->mapped ? 4500 : 240 };
}

void boat_cargo_view_water(const BoatCargoView *v, BoatModel *m)
{
    // A small finite basin patch. Water queries share the physical ambient/player wakes.
    boat_model_begin(m);
    float cx = -v->boat->position.x, cz = v->boat->position.y;
    for (int z = -10; z < 10; ++z)
        for (int x = -10; x < 10; ++x) {
            Vector3 p[4] = { { cx + x * 5, 0, cz + z * 5 }, { cx + x * 5, 0, cz + (z + 1) * 5 },
                { cx + (x + 1) * 5, 0, cz + (z + 1) * 5 }, { cx + (x + 1) * 5, 0, cz + z * 5 } };
            for (int i = 0; i < 4; ++i)
                p[i].y = boat_water_query_borrowed(
                    &v->world->water, (Vector2) { -p[i].x, p[i].z }, boat_tick_seconds(v->tick))
                             .height;
            boat_model_quad(m, p[0], p[1], p[2], p[3],
                (x + z) % 2 ? (Color) { 45, 107, 122, 255 } : (Color) { 48, 113, 127, 255 });
        }
}

WaterfrontLighting boat_cargo_view_work_lighting(WaterfrontLighting light)
{
    light.cpu_ambient = fmaxf(light.cpu_ambient, .8f * Clamp(light.window_light, 0, 1));
    return light;
}

bool boat_cargo_view_gpu_init(BoatCargoViewGpu *g)
{
    enum { VERTICES = BOAT_MODEL_MAX_TRIANGLES * 3 };
    g->mesh = (Mesh) { .vertexCount = VERTICES, .triangleCount = VERTICES / 3 };
    g->mesh.vertices = MemAlloc(VERTICES * 3 * sizeof(float));
    g->mesh.colors = MemAlloc(VERTICES * 4);
    if (!g->mesh.vertices || !g->mesh.colors) {
        MemFree(g->mesh.vertices);
        MemFree(g->mesh.colors);
        *g = (BoatCargoViewGpu) { 0 };
        return false;
    }
    memset(g->mesh.vertices, 0, VERTICES * 3 * sizeof(float));
    memset(g->mesh.colors, 255, VERTICES * 4);
    UploadMesh(&g->mesh, true);
    g->material = LoadMaterialDefault();
    return g->mesh.vaoId != 0;
}

// One upload/draw loop for the legacy harness look and the frame-lit game entry. FLAT leaves
// colours unchanged; FIXED preserves the harness's original key light and byte truncation; FRAME
// applies the CPU flat-material frame policy.
typedef enum { CARGO_GPU_FLAT, CARGO_GPU_FIXED, CARGO_GPU_FRAME } CargoGpuShading;

static void cargo_gpu_draw(BoatCargoViewGpu *g, const Mesh3D *m, CargoGpuShading shading,
    const WaterfrontLighting *lighting)
{
    if (!g->mesh.vaoId || !m->triangle_count || m->triangle_count > BOAT_MODEL_MAX_TRIANGLES
        || (shading == CARGO_GPU_FRAME && !lighting))
        return;
    Vector3 light = Vector3Normalize((Vector3) { -1, 3, 2 });
    for (size_t t = 0; t < m->triangle_count; ++t) {
        Triangle3D triangle = m->triangles[t];
        Vector3 a = m->vertices[triangle.vertices[0]], b = m->vertices[triangle.vertices[1]],
                c = m->vertices[triangle.vertices[2]];
        Vector3 normal
            = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        float shade = 1;
        Vector3 levels = { 1, 1, 1 };
        if (shading == CARGO_GPU_FIXED)
            shade = .3f + .7f * fmaxf(0, Vector3DotProduct(normal, light));
        else if (shading == CARGO_GPU_FRAME) {
            float brightness = lighting->cpu_ambient
                + lighting->cpu_direct
                    * fmaxf(0, Vector3DotProduct(normal, lighting->sun_direction));
            levels = (Vector3) { Clamp(brightness * lighting->cpu_tint.x, 0, 1),
                Clamp(brightness * lighting->cpu_tint.y, 0, 1),
                Clamp(brightness * lighting->cpu_tint.z, 0, 1) };
        }
        for (int i = 0; i < 3; ++i) {
            size_t v = t * 3 + i;
            Vector3 p = m->vertices[triangle.vertices[i]];
            memcpy(g->mesh.vertices + v * 3, &p, sizeof(p));
            if (shading == CARGO_GPU_FRAME) {
                g->mesh.colors[v * 4] = (unsigned char)(triangle.color.r * levels.x);
                g->mesh.colors[v * 4 + 1] = (unsigned char)(triangle.color.g * levels.y);
                g->mesh.colors[v * 4 + 2] = (unsigned char)(triangle.color.b * levels.z);
            } else {
                g->mesh.colors[v * 4] = (unsigned char)(triangle.color.r * shade);
                g->mesh.colors[v * 4 + 1] = (unsigned char)(triangle.color.g * shade);
                g->mesh.colors[v * 4 + 2] = (unsigned char)(triangle.color.b * shade);
            }
            g->mesh.colors[v * 4 + 3] = 255;
        }
    }
    g->mesh.triangleCount = (int)m->triangle_count;
    g->mesh.vertexCount = g->mesh.triangleCount * 3;
    UpdateMeshBuffer(g->mesh, 0, g->mesh.vertices, g->mesh.triangleCount * 9 * sizeof(float), 0);
    UpdateMeshBuffer(g->mesh, 3, g->mesh.colors, g->mesh.triangleCount * 12, 0);
    DrawMesh(g->mesh, g->material, MatrixIdentity());
}

void boat_cargo_view_gpu_draw(BoatCargoViewGpu *g, const Mesh3D *m, bool lit)
{
    cargo_gpu_draw(g, m, lit ? CARGO_GPU_FIXED : CARGO_GPU_FLAT, NULL);
}

void boat_cargo_view_gpu_draw_lighting(
    BoatCargoViewGpu *g, const Mesh3D *m, const WaterfrontLighting *lighting)
{
    WaterfrontLighting work = boat_cargo_view_work_lighting(*lighting);
    cargo_gpu_draw(g, m, CARGO_GPU_FRAME, &work);
}

void boat_cargo_view_gpu_destroy(BoatCargoViewGpu *g)
{
    if (g->mesh.vaoId)
        UnloadMesh(g->mesh);
    if (g->material.maps)
        UnloadMaterial(g->material);
    *g = (BoatCargoViewGpu) { 0 };
}
