#include "renderer3d.h"

#include "raymath.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    float x, y, inverse_z;
} ProjectedVertex;

bool renderer3d_begin(
    Renderer3D *r, Color *pixels, int width, int height, View3D camera, Vector3 light_direction)
{
    Vector3 forward = Vector3Subtract(camera.target, camera.position);
    Vector3 right = Vector3CrossProduct(camera.up, forward);
    if (!pixels || width <= 0 || height <= 0 || !(camera.near_plane > 0.0f)
        || !(camera.far_plane > camera.near_plane)
        || !(camera.vertical_fov > 0.0f && camera.vertical_fov < PI)
        || !(Vector3Length(forward) > 0.0001f) || !(Vector3Length(right) > 0.0001f))
        return false;
    if ((size_t)width > SIZE_MAX / (size_t)height / sizeof(*r->depth))
        return false;
    size_t count = (size_t)width * (size_t)height;
    if (count > r->capacity) {
        float *depth = realloc(r->depth, count * sizeof(*depth));
        if (!depth)
            return false;
        r->depth = depth;
        r->capacity = count;
    }
    r->point_light = (PointLight3D) { 0 };
    r->stats = (Renderer3DStats) { 0 };
    r->pixels = pixels;
    r->width = width;
    r->height = height;
    r->camera = camera;
    r->forward = Vector3Normalize(forward);
    r->right = Vector3Normalize(right);
    r->up = Vector3CrossProduct(r->forward, r->right);
    r->light_direction = Vector3Normalize(light_direction);
    r->focal_length = (float)height * 0.5f / tanf(camera.vertical_fov * 0.5f);
    for (size_t i = 0; i < count; ++i)
        r->depth[i] = 0.0f;
    return true;
}

void renderer3d_destroy(Renderer3D *r)
{
    free(r->depth);
    *r = (Renderer3D) { 0 };
}

static Vector3 world_to_view(const Renderer3D *r, Vector3 world)
{
    Vector3 relative = Vector3Subtract(world, r->camera.position);
    return (Vector3) { Vector3DotProduct(relative, r->right), Vector3DotProduct(relative, r->up),
        Vector3DotProduct(relative, r->forward) };
}

static float plane_distance(const Renderer3D *r, Vector3 v, int plane)
{
    float half_width = (float)r->width * 0.5f / r->focal_length;
    float half_height = (float)r->height * 0.5f / r->focal_length;
    switch (plane) {
    case 0:
        return v.z - r->camera.near_plane;
    case 1:
        return r->camera.far_plane - v.z;
    case 2:
        return v.z * half_width + v.x;
    case 3:
        return v.z * half_width - v.x;
    case 4:
        return v.z * half_height + v.y;
    default:
        return v.z * half_height - v.y;
    }
}

// Sutherland-Hodgman clipping before projection keeps near-camera and offscreen geometry safe.
// A triangle gains at most one vertex per plane: nine vertices after all six planes.
static int clip_polygon(
    const Renderer3D *r, const Vector3 *input, int count, Vector3 *output, int plane)
{
    int result = 0;
    Vector3 previous = input[count - 1];
    float previous_distance = plane_distance(r, previous, plane);
    for (int i = 0; i < count; ++i) {
        Vector3 current = input[i];
        float distance = plane_distance(r, current, plane);
        if ((distance >= 0.0f) != (previous_distance >= 0.0f)) {
            float t = previous_distance / (previous_distance - distance);
            output[result++] = Vector3Lerp(previous, current, t);
        }
        if (distance >= 0.0f)
            output[result++] = current;
        previous = current;
        previous_distance = distance;
    }
    return result;
}

static ProjectedVertex project(const Renderer3D *r, Vector3 v)
{
    float inverse_z = 1.0f / v.z;
    return (ProjectedVertex) { (float)r->width * 0.5f + v.x * r->focal_length * inverse_z,
        (float)r->height * 0.5f - v.y * r->focal_length * inverse_z, inverse_z };
}

static float edge(ProjectedVertex a, ProjectedVertex b, float x, float y)
{
    return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}

// Recover world position from reciprocal depth so texture and light stay perspective-correct
// even on clipped triangles. World-aligned mapping also keeps adjacent boxes at one texel scale.
static Color shade_material(const Renderer3D *r, const Material3D *material, Vector3 normal,
    float px, float py, float inverse_z)
{
    float z = 1.0f / inverse_z;
    float vx = (px - (float)r->width * 0.5f) * z / r->focal_length;
    float vy = ((float)r->height * 0.5f - py) * z / r->focal_length;
    Vector3 world = Vector3Add(r->camera.position,
        Vector3Add(Vector3Scale(r->forward, z),
            Vector3Add(Vector3Scale(r->right, vx), Vector3Scale(r->up, vy))));
    float u, v;
    if (fabsf(normal.y) > fabsf(normal.x) && fabsf(normal.y) > fabsf(normal.z)) {
        u = world.x;
        v = world.z;
    } else if (fabsf(normal.x) > fabsf(normal.z)) {
        u = world.z;
        v = -world.y;
    } else {
        u = world.x;
        v = -world.y;
    }
    int tx = (int)floorf(u * material->texels_per_unit) % material->width;
    int ty = (int)floorf(v * material->texels_per_unit) % material->height;
    if (tx < 0)
        tx += material->width;
    if (ty < 0)
        ty += material->height;
    float light = 0.16f + 0.12f * fmaxf(0, Vector3DotProduct(normal, r->light_direction));
    Vector3 delta = Vector3Subtract(r->point_light.position, world);
    float distance_squared = Vector3DotProduct(delta, delta);
    float radius = r->point_light.radius;
    if (radius > 0 && distance_squared < radius * radius) {
        float distance = sqrtf(fmaxf(distance_squared, 0.0001f));
        float incidence = fmaxf(0, Vector3DotProduct(normal, delta) / distance);
        float attenuation = 1.0f - distance / radius;
        light += r->point_light.intensity * incidence * attenuation * attenuation;
    }
    int level = material->emissive ? 15 : (int)(fminf(1, fmaxf(0, light)) * 15.0f);
    unsigned char texel = material->texels[ty * material->width + tx];
    return material->palette[level * 16 + texel];
}

static void rasterize(Renderer3D *r, Vector3 va, Vector3 vb, Vector3 vc, Color color,
    const Material3D *material, Vector3 normal)
{
    ProjectedVertex a = project(r, va), b = project(r, vb), c = project(r, vc);
    float area = edge(a, b, c.x, c.y);
    if (fabsf(area) < 0.0001f)
        return;
    int min_x = (int)fmaxf(0, floorf(fminf(a.x, fminf(b.x, c.x))));
    int max_x = (int)fminf((float)r->width - 1, ceilf(fmaxf(a.x, fmaxf(b.x, c.x))));
    int min_y = (int)fmaxf(0, floorf(fminf(a.y, fminf(b.y, c.y))));
    int max_y = (int)fminf((float)r->height - 1, ceilf(fmaxf(a.y, fmaxf(b.y, c.y))));
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float wa = edge(b, c, px, py) / area;
            float wb = edge(c, a, px, py) / area;
            float wc = edge(a, b, px, py) / area;
            if (wa < 0 || wb < 0 || wc < 0)
                continue;
            if (r->ghost_opacity > 0) {
                static const int bayer[16]
                    = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
                if ((bayer[(y & 3) * 4 + (x & 3)] + .5f) / 16 >= r->ghost_opacity)
                    continue;
                color = (Color) { 90, 240, 255, 255 };
            }
            // Reciprocal view depth is linear in screen space; view depth itself is not.
            float inverse_z = wa * a.inverse_z + wb * b.inverse_z + wc * c.inverse_z;
            size_t index = (size_t)y * (size_t)r->width + (size_t)x;
            if (inverse_z > r->depth[index]) {
                r->depth[index] = inverse_z;
                r->pixels[index]
                    = material ? shade_material(r, material, normal, px, py, inverse_z) : color;
            }
        }
    }
}

// LIST (may be NULL: every triangle in order) selects COUNT of MESH's triangles.
static void draw_mesh_list(Renderer3D *r, const Mesh3D *mesh, const unsigned int *list,
    size_t count, Transform3D transform, const Material3D *material, bool emissive)
{
    Matrix rotation = MatrixRotateXYZ(transform.rotation);
    ++r->stats.meshes;
    r->stats.triangles += count;
    for (size_t i = 0; i < count; ++i) {
        const Triangle3D *triangle = &mesh->triangles[list ? list[i] : i];
        Vector3 world[3];
        for (int j = 0; j < 3; ++j) {
            Vector3 scaled
                = Vector3Multiply(mesh->vertices[triangle->vertices[j]], transform.scale);
            world[j] = Vector3Add(Vector3Transform(scaled, rotation), transform.position);
        }
        // Deriving the normal after transforming also handles nonuniform building scales.
        Vector3 normal = Vector3Normalize(Vector3CrossProduct(
            Vector3Subtract(world[1], world[0]), Vector3Subtract(world[2], world[0])));
        if (Vector3DotProduct(normal, Vector3Subtract(r->camera.position, world[0])) <= 0)
            continue;
        ++r->stats.front_triangles;
        float brightness = emissive
            ? 1
            : 0.30f + 0.70f * fmaxf(0, Vector3DotProduct(normal, r->light_direction));
        Color color = { (unsigned char)(triangle->color.r * brightness),
            (unsigned char)(triangle->color.g * brightness),
            (unsigned char)(triangle->color.b * brightness), 255 };
        Vector3 buffers[2][12];
        for (int j = 0; j < 3; ++j)
            buffers[0][j] = world_to_view(r, world[j]);
        int count = 3, source = 0;
        for (int plane = 0; plane < 6 && count >= 3; ++plane) {
            count = clip_polygon(r, buffers[source], count, buffers[1 - source], plane);
            source = 1 - source;
        }
        r->stats.rasterized += count >= 3 ? (size_t)count - 2 : 0;
        for (int j = 1; j + 1 < count; ++j)
            rasterize(r, buffers[source][0], buffers[source][j], buffers[source][j + 1], color,
                material, normal);
    }
}

static void draw_mesh(Renderer3D *r, const Mesh3D *mesh, Transform3D transform,
    const Material3D *material, bool emissive)
{
    draw_mesh_list(r, mesh, NULL, mesh->triangle_count, transform, material, emissive);
}

void renderer3d_draw_mesh_triangles(
    Renderer3D *r, const Mesh3D *mesh, const unsigned int *triangles, size_t count)
{
    draw_mesh_list(r, mesh, triangles, count, (Transform3D) { .scale = { 1, 1, 1 } }, NULL, false);
}

bool renderer3d_box_visible(Renderer3D *r, Vector3 min, Vector3 max)
{
    if (r->culling_disabled)
        return true;
    // Planes are linear in view space, so a triangle inside the box lies outside a plane that
    // all eight corners are outside of. The margin covers rounding against its own vertices.
    int outside[6] = { 0 };
    for (int i = 0; i < 8; ++i) {
        Vector3 v = world_to_view(
            r, (Vector3) { i & 1 ? max.x : min.x, i & 2 ? max.y : min.y, i & 4 ? max.z : min.z });
        for (int plane = 0; plane < 6; ++plane)
            outside[plane] += plane_distance(r, v, plane) < -.01f;
    }
    for (int plane = 0; plane < 6; ++plane)
        if (outside[plane] == 8) {
            ++r->stats.boxes_culled;
            return false;
        }
    return true;
}

void renderer3d_draw_mesh(Renderer3D *r, const Mesh3D *mesh, Transform3D transform)
{
    draw_mesh(r, mesh, transform, NULL, false);
}

void renderer3d_draw_emissive_mesh(Renderer3D *r, const Mesh3D *mesh, Transform3D transform)
{
    draw_mesh(r, mesh, transform, NULL, true);
}

static void draw_box(Renderer3D *r, Transform3D transform, Color color, const Material3D *material)
{
    static const Vector3 vertices[] = {
        { -0.5f, -0.5f, -0.5f },
        { 0.5f, -0.5f, -0.5f },
        { 0.5f, 0.5f, -0.5f },
        { -0.5f, 0.5f, -0.5f },
        { -0.5f, -0.5f, 0.5f },
        { 0.5f, -0.5f, 0.5f },
        { 0.5f, 0.5f, 0.5f },
        { -0.5f, 0.5f, 0.5f },
    };
    Triangle3D triangles[] = {
        { { 0, 2, 1 }, color },
        { { 0, 3, 2 }, color },
        { { 5, 7, 4 }, color },
        { { 5, 6, 7 }, color },
        { { 4, 3, 0 }, color },
        { { 4, 7, 3 }, color },
        { { 1, 6, 5 }, color },
        { { 1, 2, 6 }, color },
        { { 3, 6, 2 }, color },
        { { 3, 7, 6 }, color },
        { { 4, 1, 5 }, color },
        { { 4, 0, 1 }, color },
    };
    Mesh3D mesh = { vertices, triangles, sizeof(triangles) / sizeof(triangles[0]) };
    draw_mesh(r, &mesh, transform, material, false);
}

void renderer3d_draw_box(Renderer3D *r, Transform3D transform, Color color)
{
    draw_box(r, transform, color, NULL);
}

void renderer3d_draw_textured_box(Renderer3D *r, Transform3D transform, const Material3D *material)
{
    if (!material || !material->texels || !material->palette || material->width <= 0
        || material->height <= 0 || !(material->texels_per_unit > 0))
        return;
    draw_box(r, transform, WHITE, material);
}
