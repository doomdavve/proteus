#include "renderer_gpu.h"
#include "boat_clock.h"
#include "boat_water_mesh.h"
#include "waterfront_corner.h"

#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float *positions;
    float *normals;
    float *texcoords;
    unsigned char *colors;
    int count;
    int capacity;
} MeshBuilder;

static const Color LAMP_LENS_COLOR = { 255, 218, 132, 255 };
static const Color HEADLIGHT_LENS_COLOR = { 255, 246, 202, 255 };
// The generated map uses this colour as its water material identifier.
static const Color WATER_COLOR = { 45, 79, 104, 255 };

static void builder_destroy(MeshBuilder *builder)
{
    free(builder->positions);
    free(builder->normals);
    free(builder->texcoords);
    free(builder->colors);
    *builder = (MeshBuilder) { 0 };
}

static bool builder_reserve(MeshBuilder *builder, int additional)
{
    if (additional <= builder->capacity - builder->count)
        return true;
    int capacity = builder->capacity == 0 ? 256 : builder->capacity;
    while (capacity - builder->count < additional)
        capacity *= 2;
    float *positions = malloc((size_t)capacity * 3 * sizeof(*positions));
    float *normals = malloc((size_t)capacity * 3 * sizeof(*normals));
    float *texcoords = malloc((size_t)capacity * 2 * sizeof(*texcoords));
    unsigned char *colors = malloc((size_t)capacity * 4 * sizeof(*colors));
    if (positions == NULL || normals == NULL || texcoords == NULL || colors == NULL) {
        free(positions);
        free(normals);
        free(texcoords);
        free(colors);
        return false;
    }
    memcpy(positions, builder->positions, (size_t)builder->count * 3 * sizeof(*positions));
    memcpy(normals, builder->normals, (size_t)builder->count * 3 * sizeof(*normals));
    memcpy(texcoords, builder->texcoords, (size_t)builder->count * 2 * sizeof(*texcoords));
    memcpy(colors, builder->colors, (size_t)builder->count * 4 * sizeof(*colors));
    free(builder->positions);
    free(builder->normals);
    free(builder->texcoords);
    free(builder->colors);
    builder->positions = positions;
    builder->normals = normals;
    builder->texcoords = texcoords;
    builder->colors = colors;
    builder->capacity = capacity;
    return true;
}

// Legacy roof mapping. Ground triangles replace UVs with material IDs; facades
// replace them with wall-local metres. Both use filtered procedural finishes.
static Vector2 world_uv(Vector3 point, Vector3 normal)
{
    const float density = .04f;
    if (fabsf(normal.y) > fabsf(normal.x) && fabsf(normal.y) > fabsf(normal.z))
        return (Vector2) { point.x * density, point.z * density };
    if (fabsf(normal.x) > fabsf(normal.z))
        return (Vector2) { point.z * density, -point.y * density };
    return (Vector2) { point.x * density, -point.y * density };
}

static bool builder_triangle(MeshBuilder *builder, Vector3 a, Vector3 b, Vector3 c, Color color)
{
    if (!builder_reserve(builder, 3))
        return false;
    Vector3 normal
        = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
    Vector3 points[] = { a, b, c };
    for (int i = 0; i < 3; ++i) {
        int vertex = builder->count++;
        Vector2 uv = world_uv(points[i], normal);
        builder->positions[vertex * 3] = points[i].x;
        builder->positions[vertex * 3 + 1] = points[i].y;
        builder->positions[vertex * 3 + 2] = points[i].z;
        builder->normals[vertex * 3] = normal.x;
        builder->normals[vertex * 3 + 1] = normal.y;
        builder->normals[vertex * 3 + 2] = normal.z;
        builder->texcoords[vertex * 2] = uv.x;
        builder->texcoords[vertex * 2 + 1] = uv.y;
        builder->colors[vertex * 4] = color.r;
        builder->colors[vertex * 4 + 1] = color.g;
        builder->colors[vertex * 4 + 2] = color.b;
        builder->colors[vertex * 4 + 3] = color.a;
    }
    return true;
}

static bool builder_quad(
    MeshBuilder *builder, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color)
{
    return builder_triangle(builder, a, b, c, color) && builder_triangle(builder, a, c, d, color);
}

static bool builder_box(MeshBuilder *builder, Vector3 a, Vector3 b, float width, float depth)
{
    Vector3 direction = Vector3Normalize(Vector3Subtract(b, a));
    Vector3 reference = fabsf(direction.y) > .9f ? (Vector3) { 1, 0, 0 } : (Vector3) { 0, 1, 0 };
    Vector3 right
        = Vector3Scale(Vector3Normalize(Vector3CrossProduct(reference, direction)), width / 2);
    Vector3 up = Vector3Scale(Vector3Normalize(Vector3CrossProduct(direction, right)), depth / 2);
    Vector3 points[8];
    for (int i = 0; i < 8; ++i) {
        Vector3 center = i < 4 ? a : b;
        int corner = i % 4;
        points[i] = Vector3Add(center,
            Vector3Add(Vector3Scale(right, corner == 0 || corner == 3 ? -1 : 1),
                Vector3Scale(up, corner < 2 ? -1 : 1)));
    }
    static const unsigned int faces[][3]
        = { { 0, 2, 1 }, { 0, 3, 2 }, { 5, 7, 4 }, { 5, 6, 7 }, { 4, 3, 0 }, { 4, 7, 3 },
              { 1, 6, 5 }, { 1, 2, 6 }, { 3, 6, 2 }, { 3, 7, 6 }, { 4, 1, 5 }, { 4, 0, 1 } };
    for (size_t i = 0; i < sizeof(faces) / sizeof(faces[0]); ++i) {
        if (!builder_triangle(builder, points[faces[i][0]], points[faces[i][1]],
                points[faces[i][2]], (Color) { 107, 67, 62, 255 }))
            return false;
    }
    return true;
}

static Mesh builder_finish(MeshBuilder *builder)
{
    Mesh mesh = { .vertexCount = builder->count,
        .triangleCount = builder->count / 3,
        .vertices = builder->positions,
        .normals = builder->normals,
        .texcoords = builder->texcoords,
        .colors = builder->colors };
    builder->positions = NULL;
    builder->normals = NULL;
    builder->texcoords = NULL;
    builder->colors = NULL;
    UploadMesh(&mesh, false);
    *builder = (MeshBuilder) { 0 };
    return mesh;
}

static bool build_source_mesh(Mesh *mesh, const Mesh3D *source)
{
    MeshBuilder builder = { 0 };
    for (size_t i = 0; i < source->triangle_count; ++i) {
        const Triangle3D *t = &source->triangles[i];
        if (!builder_triangle(&builder, source->vertices[t->vertices[0]],
                source->vertices[t->vertices[1]], source->vertices[t->vertices[2]], t->color)) {
            builder_destroy(&builder);
            return false;
        }
    }
    *mesh = builder_finish(&builder);
    return mesh->vaoId != 0;
}

static void update_flag_mesh(Mesh *mesh, const BoatFlagSurface *surface, unsigned int skin)
{
    BoatFlagMesh storage;
    Mesh3D source = boat_flag_skin_mesh(surface, skin, &storage);
    if (!mesh->vaoId) {
        if (!build_source_mesh(mesh, &source))
            return;
    }
    for (size_t i = 0; i < source.triangle_count; ++i) {
        const Triangle3D *triangle = &source.triangles[i];
        Vector3 a = source.vertices[triangle->vertices[0]];
        Vector3 b = source.vertices[triangle->vertices[1]];
        Vector3 c = source.vertices[triangle->vertices[2]];
        Vector3 normal
            = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        for (int j = 0; j < 3; ++j) {
            size_t v = i * 3 + j;
            memcpy(
                &mesh->vertices[v * 3], &source.vertices[triangle->vertices[j]], sizeof(Vector3));
            memcpy(&mesh->normals[v * 3], &normal, sizeof(Vector3));
            memcpy(&mesh->colors[v * 4], &triangle->color, sizeof(Color));
        }
    }
    UpdateMeshBuffer(*mesh, 0, mesh->vertices, mesh->vertexCount * 3 * (int)sizeof(float), 0);
    UpdateMeshBuffer(*mesh, 2, mesh->normals, mesh->vertexCount * 3 * (int)sizeof(float), 0);
    UpdateMeshBuffer(*mesh, 3, mesh->colors, mesh->vertexCount * 4, 0);
}

typedef enum {
    CITY_SURFACE_FLAT,
    CITY_SURFACE_FACADE,
    CITY_SURFACE_ROOF,
    CITY_SURFACE_QUAY,
} CitySurface;

static bool builder_ground_triangle(MeshBuilder *builder, Vector3 a, Vector3 b, Vector3 c,
    Color color, WaterfrontGroundMaterial material)
{
    if (!builder_triangle(builder, a, b, c, color))
        return false;
    for (int i = builder->count - 3; i < builder->count; ++i) {
        builder->texcoords[i * 2] = (float)material;
        builder->texcoords[i * 2 + 1] = 0;
    }
    return true;
}

static bool build_corner(Mesh *opaque, Mesh *emissive, MeshBuilder *ground)
{
    size_t count;
    const WaterfrontCornerBox *boxes = waterfront_corner_boxes(&count);
    if (!boxes)
        return false;
    static const unsigned char faces[][3]
        = { { 0, 2, 1 }, { 0, 3, 2 }, { 5, 7, 4 }, { 5, 6, 7 }, { 4, 3, 0 }, { 4, 7, 3 },
              { 1, 6, 5 }, { 1, 2, 6 }, { 3, 6, 2 }, { 3, 7, 6 }, { 4, 1, 5 }, { 4, 0, 1 } };
    MeshBuilder builders[2] = { 0 };
    for (size_t i = 0; i < count; ++i)
        for (size_t f = 0; f < sizeof(faces) / sizeof(faces[0]); ++f) {
            Vector3 a = boxes[i].vertices[faces[f][0]], b = boxes[i].vertices[faces[f][1]];
            Vector3 c = boxes[i].vertices[faces[f][2]];
            Color color = boxes[i].color;
            Vector3 normal = Vector3Normalize(
                Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
            int finish = 0;
            // The static kit's surface palette is semantic, like the retained map's.
            // Only tops at ground level: never texture benches, tree crowns, or kerb sides.
            if (!boxes[i].emissive && normal.y > .95f && a.y < 3.9f) {
                if (ColorIsEqual(color, (Color) { 171, 171, 158, 255 }))
                    finish = WATERFRONT_GROUND_PAVING;
                else if (ColorIsEqual(color, (Color) { 76, 109, 73, 255 }))
                    finish = WATERFRONT_GROUND_PLANTING;
                else if (ColorIsEqual(color, (Color) { 98, 106, 112, 255 }))
                    finish = WATERFRONT_GROUND_ASPHALT;
            }
            bool ok = finish ? builder_ground_triangle(ground, a, b, c, color, finish)
                             : builder_triangle(&builders[boxes[i].emissive], a, b, c, color);
            if (!ok) {
                builder_destroy(&builders[0]);
                builder_destroy(&builders[1]);
                return false;
            }
        }
    *opaque = builder_finish(&builders[0]);
    *emissive = builder_finish(&builders[1]);
    return opaque->vaoId != 0 && emissive->vaoId != 0;
}

static CitySurface city_surface(Vector3 a, Vector3 b, Vector3 c)
{
    Vector3 normal
        = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
    float top = fmaxf(a.y, fmaxf(b.y, c.y));
    if (normal.y > .95f)
        return top > 4.0f ? CITY_SURFACE_ROOF : CITY_SURFACE_FLAT;
    if (fabsf(normal.y) < .15f && top >= 9.0f)
        return CITY_SURFACE_FACADE;
    return CITY_SURFACE_FLAT;
}

static bool is_water_triangle(const Triangle3D *triangle)
{
    return triangle->color.r == WATER_COLOR.r && triangle->color.g == WATER_COLOR.g
        && triangle->color.b == WATER_COLOR.b;
}

static bool builder_facade_triangle(
    MeshBuilder *builder, Vector3 a, Vector3 b, Vector3 c, Color source)
{
    // Generated wall pairs are a,c,b and a,d,c: vertex zero is the same lower
    // corner, and the last vertex reaches the other end in both triangles.
    Vector3 edge = { c.x - a.x, 0, c.z - a.z };
    float length = Vector3Length(edge);
    if (length < .001f)
        return false;
    Vector3 along = Vector3Scale(edge, 1 / length);
    // The retained palette is stable per building (including its mapped parts).
    // Artistic material assignments, not claims about surveyed facade finishes.
    Color color = { 208, 198, 175, WATERFRONT_FACADE_PLASTER };
    if (source.r == 185)
        color = (Color) { 170, 104, 77, WATERFRONT_FACADE_BRICK };
    else if (source.r == 190)
        color = (Color) { 183, 154, 108, WATERFRONT_FACADE_BRICK };
    else if (source.r == 154)
        color = (Color) { 147, 165, 174, WATERFRONT_FACADE_PANEL };
    if (!builder_triangle(builder, a, b, c, color))
        return false;
    Vector3 points[] = { a, b, c };
    for (int i = 0; i < 3; ++i) {
        int vertex = builder->count - 3 + i;
        builder->texcoords[vertex * 2] = Vector3DotProduct(Vector3Subtract(points[i], a), along);
        builder->texcoords[vertex * 2 + 1] = points[i].y - 3.5f;
    }
    return true;
}

static bool build_city(Mesh *city_mesh, Mesh *facades, Mesh *roofs, Mesh *quay, Mesh *water,
    Mesh *corner, Mesh *corner_emissive)
{
    const Mesh3D *city = waterfront_city_mesh();
    MeshBuilder builders[] = { { 0 }, { 0 }, { 0 }, { 0 } };
    MeshBuilder water_builder = { 0 };
    for (size_t i = 0; i < city->triangle_count; ++i) {
        const Triangle3D *triangle = &city->triangles[i];
        Vector3 a = city->vertices[triangle->vertices[0]];
        Vector3 b = city->vertices[triangle->vertices[1]];
        Vector3 c = city->vertices[triangle->vertices[2]];
        if (is_water_triangle(triangle)) {
            if (!builder_triangle(&water_builder, a, b, c, (Color) { 67, 151, 180, 255 }))
                goto failed;
            continue;
        }
        bool planting = (triangle->color.r == 72 && triangle->color.g == 105)
            || (triangle->color.r == 61 && triangle->color.g == 91);
        // Authored bridge beams retain their flat steel/concrete colors.
        bool bridge = waterfront_city_is_bridge_triangle(i);
        CitySurface surface = planting || bridge ? CITY_SURFACE_FLAT : city_surface(a, b, c);
        Vector3 normal
            = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        bool road = ColorIsEqual(triangle->color, (Color) { 83, 89, 99, 255 });
        bool land = ColorIsEqual(triangle->color, (Color) { 135, 139, 144, 255 });
        if (normal.y > .95f && (road || land || planting) && fmaxf(a.y, fmaxf(b.y, c.y)) < 4.3f) {
            int finish = planting ? WATERFRONT_GROUND_PLANTING : WATERFRONT_GROUND_CONCRETE;
            if (road)
                finish = WATERFRONT_GROUND_ASPHALT;
            if (!builder_ground_triangle(
                    &builders[CITY_SURFACE_QUAY], a, b, c, triangle->color, finish))
                goto failed;
            continue;
        }
        bool ok = surface == CITY_SURFACE_FACADE
            ? builder_facade_triangle(&builders[surface], a, b, c, triangle->color)
            : builder_triangle(&builders[surface], a, b, c, triangle->color);
        if (!ok)
            goto failed;
    }
    if (builders[CITY_SURFACE_FLAT].count == 0 || builders[CITY_SURFACE_FACADE].count == 0
        || builders[CITY_SURFACE_ROOF].count == 0 || builders[CITY_SURFACE_QUAY].count == 0
        || water_builder.count == 0)
        goto failed;
    if (!build_corner(corner, corner_emissive, &builders[CITY_SURFACE_QUAY]))
        goto failed;
    *city_mesh = builder_finish(&builders[CITY_SURFACE_FLAT]);
    *facades = builder_finish(&builders[CITY_SURFACE_FACADE]);
    *roofs = builder_finish(&builders[CITY_SURFACE_ROOF]);
    *quay = builder_finish(&builders[CITY_SURFACE_QUAY]);
    *water = builder_finish(&water_builder);
    return city_mesh->vaoId != 0 && facades->vaoId != 0 && roofs->vaoId != 0 && quay->vaoId != 0
        && water->vaoId != 0;
failed:
    for (size_t i = 0; i < sizeof(builders) / sizeof(builders[0]); ++i)
        builder_destroy(&builders[i]);
    builder_destroy(&water_builder);
    return false;
}

static bool build_windows(Mesh *mesh)
{
    const Mesh3D *city = waterfront_city_mesh();
    MeshBuilder builder = { 0 };
    for (size_t i = 0; i < city->triangle_count; ++i) {
        if (waterfront_city_is_bridge_triangle(i))
            continue;
        const Triangle3D *triangle = &city->triangles[i];
        Vector3 a = city->vertices[triangle->vertices[0]];
        Vector3 c = city->vertices[triangle->vertices[1]];
        Vector3 b = city->vertices[triangle->vertices[2]];
        if (fabsf(a.y - 3.5f) > .001f || fabsf(b.y - a.y) > .001f || c.y < 9)
            continue;
        Vector3 edge = Vector3Subtract(b, a);
        float length = Vector3Length(edge);
        Vector3 along = Vector3Scale(edge, 1 / length);
        Vector3 normal = { along.z, 0, -along.x };
        for (int floor = 0; 6.0f + floor * 3.2f < c.y - 1; ++floor) {
            for (int column = 0; 2.5f + column * 4 < length - 1.5f; ++column) {
                unsigned int seed = (unsigned int)i * 1664525u + (unsigned int)column * 1013904223u
                    + (unsigned int)floor * 374761393u;
                Color color = seed % 7 < 3 ? (Color) { 138, 185, 204, 255 }
                    : seed % 7 == 3        ? (Color) { 181, 211, 221, 255 }
                                           : (Color) { 66, 100, 120, 255 };
                Vector3 p = Vector3Add(Vector3Add(a, Vector3Scale(along, 2.5f + column * 4)),
                    Vector3Scale(normal, .035f));
                p.y = 5.0f + floor * 3.2f;
                Vector3 q = Vector3Add(p, Vector3Scale(along, floor == 0 ? 2.2f : 1.5f));
                Vector3 up = { 0, floor == 0 ? 1.9f : 1.5f, 0 };
                if (!builder_quad(&builder, p, Vector3Add(p, up), Vector3Add(q, up), q, color)) {
                    builder_destroy(&builder);
                    return false;
                }
            }
        }
    }
    *mesh = builder_finish(&builder);
    return mesh->vaoId != 0;
}

static Vector3 crane_point(float x, float y, float z)
{
    const float angle = .056f;
    return (Vector3) { x * cosf(angle) - z * sinf(angle), y, x * sinf(angle) + z * cosf(angle) };
}

static bool build_crane(Mesh *mesh)
{
    MeshBuilder builder = { 0 };
    for (int side = -1; side <= 1; side += 2) {
        for (int face = -1; face <= 1; face += 2) {
            if (!builder_box(&builder, crane_point(side * 49, 3.5f, face * 5),
                    crane_point(side * 45, 73, face * 3), 3, 3))
                goto failed;
        }
        if (!builder_box(
                &builder, crane_point(side * 49, 5, -5), crane_point(side * 49, 5, 5), 4, 4)
            || !builder_box(
                &builder, crane_point(side * 47, 35, -4), crane_point(side * 47, 35, 4), 2, 2))
            goto failed;
    }
    if (!builder_box(&builder, crane_point(-55, 76, 0), crane_point(55, 76, 0), 8, 6)
        || !builder_box(&builder, crane_point(-15, 81, 0), crane_point(3, 81, 0), 8, 4)
        || !builder_box(&builder, crane_point(5, 75, 0), crane_point(5, 48, 0), .4f, .4f)
        || !builder_box(&builder, crane_point(2, 48, 0), crane_point(8, 48, 0), 1.5f, 1.5f))
        goto failed;
    *mesh = builder_finish(&builder);
    return mesh->vaoId != 0;
failed:
    builder_destroy(&builder);
    return false;
}

static bool build_unit_cube(Mesh *mesh)
{
    static const Vector3 points[] = {
        { -.5f, -.5f, -.5f },
        { .5f, -.5f, -.5f },
        { .5f, .5f, -.5f },
        { -.5f, .5f, -.5f },
        { -.5f, -.5f, .5f },
        { .5f, -.5f, .5f },
        { .5f, .5f, .5f },
        { -.5f, .5f, .5f },
    };
    static const unsigned char faces[][3] = {
        { 0, 2, 1 },
        { 0, 3, 2 },
        { 5, 7, 4 },
        { 5, 6, 7 },
        { 4, 3, 0 },
        { 4, 7, 3 },
        { 1, 6, 5 },
        { 1, 2, 6 },
        { 3, 6, 2 },
        { 3, 7, 6 },
        { 4, 1, 5 },
        { 4, 0, 1 },
    };
    MeshBuilder builder = { 0 };
    for (size_t i = 0; i < sizeof(faces) / sizeof(faces[0]); ++i) {
        if (!builder_triangle(
                &builder, points[faces[i][0]], points[faces[i][1]], points[faces[i][2]], WHITE)) {
            builder_destroy(&builder);
            return false;
        }
    }
    *mesh = builder_finish(&builder);
    return mesh->vaoId != 0;
}

static bool build_unit_plane(Mesh *mesh)
{
    MeshBuilder builder = { 0 };
    if (!builder_quad(&builder, (Vector3) { -.5f, 0, -.5f }, (Vector3) { -.5f, 0, .5f },
            (Vector3) { .5f, 0, .5f }, (Vector3) { .5f, 0, -.5f }, WHITE)) {
        builder_destroy(&builder);
        return false;
    }
    *mesh = builder_finish(&builder);
    return mesh->vaoId != 0;
}

static bool load_detail_texture(
    Texture2D *texture, const char *fragment_shader_path, const char *asset_name)
{
    const char *slash = strrchr(fragment_shader_path, '/');
    if (slash == NULL || (size_t)(slash - fragment_shader_path) > 900)
        return false;
    char texture_path[1024];
    snprintf(texture_path, sizeof(texture_path), "%.*s/../textures/%s",
        (int)(slash - fragment_shader_path), fragment_shader_path, asset_name);
    *texture = LoadTexture(texture_path);
    if (texture->id == 0)
        return false;
    SetTextureFilter(*texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(*texture, TEXTURE_WRAP_REPEAT);
    return true;
}

static bool init_detail_material(Material *material, Shader shader, Texture2D texture)
{
    *material = LoadMaterialDefault();
    if (material->maps == NULL)
        return false;
    material->shader = shader;
    material->maps[MATERIAL_MAP_DIFFUSE].texture = texture;
    return true;
}

static Shader load_city_shader(const char *fragment_shader_path)
{
    const char *slash = strrchr(fragment_shader_path, '/');
    if (slash == NULL || (size_t)(slash - fragment_shader_path) > 900)
        return (Shader) { 0 };
    char vertex_shader_path[1024];
    snprintf(vertex_shader_path, sizeof(vertex_shader_path), "%.*s/waterfront_city.vs",
        (int)(slash - fragment_shader_path), fragment_shader_path);
    return LoadShader(vertex_shader_path, fragment_shader_path);
}

static Shader load_shadow_shader(const char *fragment_shader_path)
{
    const char *slash = strrchr(fragment_shader_path, '/');
    if (slash == NULL || (size_t)(slash - fragment_shader_path) > 900)
        return (Shader) { 0 };
    char vertex_shader_path[1024];
    char fragment_path[1024];
    snprintf(vertex_shader_path, sizeof(vertex_shader_path), "%.*s/waterfront_shadow.vs",
        (int)(slash - fragment_shader_path), fragment_shader_path);
    snprintf(fragment_path, sizeof(fragment_path), "%.*s/waterfront_shadow.fs",
        (int)(slash - fragment_shader_path), fragment_shader_path);
    return LoadShader(vertex_shader_path, fragment_path);
}

static Shader load_water_shader(const char *fragment_shader_path)
{
    const char *slash = strrchr(fragment_shader_path, '/');
    if (slash == NULL || (size_t)(slash - fragment_shader_path) > 900)
        return (Shader) { 0 };
    char vertex_shader_path[1024];
    char fragment_path[1024];
    snprintf(vertex_shader_path, sizeof(vertex_shader_path), "%.*s/waterfront_water.vs",
        (int)(slash - fragment_shader_path), fragment_shader_path);
    snprintf(fragment_path, sizeof(fragment_path), "%.*s/waterfront_water.fs",
        (int)(slash - fragment_shader_path), fragment_shader_path);
    return LoadShader(vertex_shader_path, fragment_path);
}

static Shader load_sky_shader(const char *fragment_shader_path)
{
    const char *slash = strrchr(fragment_shader_path, '/');
    if (slash == NULL || (size_t)(slash - fragment_shader_path) > 900)
        return (Shader) { 0 };
    char path[1024];
    snprintf(path, sizeof(path), "%.*s/waterfront_sky.fs", (int)(slash - fragment_shader_path),
        fragment_shader_path);
    return LoadShader(NULL, path);
}

static bool create_shadow_map(RenderTexture2D *target, int size)
{
    *target = (RenderTexture2D) { 0 };
    target->id = rlLoadFramebuffer();
    if (target->id == 0)
        return false;
    target->texture.width = size;
    target->texture.height = size;
    target->depth.id = rlLoadTextureDepth(size, size, false);
    target->depth.width = size;
    target->depth.height = size;
    target->depth.mipmaps = 1;
    if (target->depth.id == 0) {
        rlUnloadFramebuffer(target->id);
        *target = (RenderTexture2D) { 0 };
        return false;
    }
    rlEnableFramebuffer(target->id);
    rlFramebufferAttach(
        target->id, target->depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    bool complete = rlFramebufferComplete(target->id);
    rlDisableFramebuffer();
    if (!complete) {
        rlUnloadFramebuffer(target->id);
        *target = (RenderTexture2D) { 0 };
        return false;
    }
    SetTextureFilter(target->depth, TEXTURE_FILTER_POINT);
    return true;
}

bool waterfront_gpu_scene_init(WaterfrontGpuScene *scene, const char *fragment_shader_path)
{
    *scene = (WaterfrontGpuScene) { 0 };
    scene->shader = load_city_shader(fragment_shader_path);
    scene->shadow_shader = load_shadow_shader(fragment_shader_path);
    scene->water_shader = load_water_shader(fragment_shader_path);
    scene->sky_shader = load_sky_shader(fragment_shader_path);
    if (!IsShaderValid(scene->shader) || scene->shader.id == rlGetShaderIdDefault()
        || !IsShaderValid(scene->shadow_shader) || scene->shadow_shader.id == rlGetShaderIdDefault()
        || !IsShaderValid(scene->water_shader) || scene->water_shader.id == rlGetShaderIdDefault()
        || !IsShaderValid(scene->sky_shader) || scene->sky_shader.id == rlGetShaderIdDefault())
        goto failed;
    scene->sky_texture = LoadTextureFromImage(waterfront_sky_image());
    if (!IsTextureValid(scene->sky_texture))
        goto failed;
    SetTextureFilter(scene->sky_texture, TEXTURE_FILTER_POINT);
    scene->city_material = LoadMaterialDefault();
    scene->life_material = LoadMaterialDefault();
    scene->shadow_material = LoadMaterialDefault();
    scene->water_material = LoadMaterialDefault();
    if (scene->city_material.maps == NULL || scene->life_material.maps == NULL
        || scene->shadow_material.maps == NULL || scene->water_material.maps == NULL)
        goto failed;
    scene->city_material.shader = scene->shader;
    scene->life_material.shader = scene->shader;
    scene->shadow_material.shader = scene->shadow_shader;
    scene->water_material.shader = scene->water_shader;
    if (!load_detail_texture(&scene->roof_texture, fragment_shader_path, "roof.png")
        || !init_detail_material(&scene->roof_material, scene->shader, scene->roof_texture))
        goto failed;
    if (!build_city(&scene->city, &scene->facades, &scene->roofs, &scene->quay, &scene->water,
            &scene->corner, &scene->corner_emissive)
        || !build_windows(&scene->windows) || !build_crane(&scene->crane))
        goto failed;
    for (int part = 0; part < WATERFRONT_GULL_PARTS; ++part)
        if (!build_source_mesh(&scene->gull[part], waterfront_gull_mesh(part)))
            goto failed;
    for (unsigned int skin = 0; skin < BOAT_SKIN_COUNT; ++skin)
        if (!build_source_mesh(&scene->player_hull[skin], boat_skin_mesh(skin, BOAT_SKIN_HULL))
            || !build_source_mesh(
                &scene->player_cabin[skin], boat_skin_mesh(skin, BOAT_SKIN_COCKPIT))
            || !build_source_mesh(
                &scene->player_engine[skin], boat_skin_mesh(skin, BOAT_SKIN_ENGINE)))
            goto failed;
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i)
        if (!build_source_mesh(&scene->traffic_boats[i], waterfront_traffic_mesh(i)))
            goto failed;
    for (int i = 0; i < WATERFRONT_HARBOR_BOATS; ++i)
        if (!build_source_mesh(&scene->harbor_boats[i], waterfront_harbor_boat_mesh(i)))
            goto failed;
    if (!build_source_mesh(&scene->harbor_pontoon, waterfront_harbor_pontoon_mesh())
        || !build_source_mesh(&scene->harbor_quay, waterfront_harbor_quay_mesh()))
        goto failed;
    if (!build_unit_cube(&scene->bridge) || !build_unit_cube(&scene->cube)
        || !build_unit_plane(&scene->plane))
        goto failed;
    scene->shadow_map_size = WATERFRONT_GPU_SHADOW_MAP_SIZE;
    if (!create_shadow_map(&scene->shadow_map, scene->shadow_map_size))
        goto failed;
    scene->reflection
        = LoadRenderTexture(WATERFRONT_GPU_REFLECTION_WIDTH, WATERFRONT_GPU_REFLECTION_HEIGHT);
    if (!IsRenderTextureValid(scene->reflection))
        goto failed;
    SetTextureFilter(scene->reflection.texture, TEXTURE_FILTER_POINT);
    scene->lighting_enabled = true;
    scene->shadows_enabled = true;
    scene->atmosphere_enabled = true;
    scene->reflections_enabled = true;
    scene->ready = true;
    return true;
failed:
    waterfront_gpu_scene_destroy(scene);
    return false;
}

static Matrix transform_matrix(Transform3D transform)
{
    // MatrixMultiply(A, B) composes B before A in raymath. This matches the CPU renderer's
    // explicit order: scale a mesh vertex, rotate it, then add its world position.
    return MatrixMultiply(
        MatrixMultiply(MatrixScale(transform.scale.x, transform.scale.y, transform.scale.z),
            MatrixRotateXYZ(transform.rotation)),
        MatrixTranslate(transform.position.x, transform.position.y, transform.position.z));
}

static void set_object_color(Shader shader, Color color)
{
    float values[] = {
        (float)color.r / 255.0f,
        (float)color.g / 255.0f,
        (float)color.b / 255.0f,
        (float)color.a / 255.0f,
    };
    int location = GetShaderLocation(shader, "objectColor");
    if (location >= 0)
        SetShaderValue(shader, location, values, SHADER_UNIFORM_VEC4);
}

static void set_emissive(Shader shader, bool emissive)
{
    int location = GetShaderLocation(shader, "emissive");
    if (location >= 0) {
        int value = emissive ? 1 : 0;
        SetShaderValue(shader, location, &value, SHADER_UNIFORM_INT);
    }
}

static void draw_colored_mesh(
    Mesh mesh, Material material, Transform3D transform, Color color, bool emissive)
{
    set_object_color(material.shader, color);
    set_emissive(material.shader, emissive);
    material.maps[MATERIAL_MAP_DIFFUSE].color = color;
    DrawMesh(mesh, material, transform_matrix(transform));
}

typedef enum {
    LIGHT_CRANE,
    LIGHT_SHOP,
    LIGHT_LAMP,
    LIGHT_HEADLIGHT,
} LightKind;

typedef struct {
    int id;
    WaterfrontGpuLight light;
    float score;
    LightKind kind;
} LightCandidate;

static bool light_was_selected(const WaterfrontGpuScene *scene, int id)
{
    for (int i = 0; i < scene->local_light_count; ++i) {
        if (scene->local_light_ids[i] == id)
            return true;
    }
    return false;
}

static void add_light_candidate(
    LightCandidate *candidates, int *count, int id, WaterfrontGpuLight light, LightKind kind)
{
    if (*count >= 72)
        return;
    candidates[*count] = (LightCandidate) { .id = id, .light = light, .kind = kind };
    ++*count;
}

static int select_light_class(const WaterfrontGpuScene *scene, const LightCandidate *candidates,
    int candidate_count, const WaterfrontSceneFrame *frame, LightKind kind,
    LightCandidate *selected, int capacity)
{
    Vector3 view_direction
        = Vector3Normalize(Vector3Subtract(frame->camera.target, frame->camera.position));
    int selected_count = 0;
    for (int candidate = 0; candidate < candidate_count; ++candidate) {
        LightCandidate current = candidates[candidate];
        if (current.kind != kind)
            continue;
        Vector3 from_camera = Vector3Subtract(current.light.position, frame->camera.position);
        float view_depth = Vector3DotProduct(from_camera, view_direction);
        if (view_depth < -current.light.radius)
            continue;
        // The target is the authored shot focus, unlike the eye position which may sit far away
        // or above the street. It keeps the selected lights relevant to the visible composition.
        current.score = Vector3DistanceSqr(current.light.position, frame->camera.target);
        if (light_was_selected(scene, current.id))
            current.score *= .35f;
        int slot = selected_count;
        while (slot > 0 && current.score < selected[slot - 1].score) {
            if (slot < capacity)
                selected[slot] = selected[slot - 1];
            --slot;
        }
        if (slot < capacity) {
            selected[slot] = current;
            if (selected_count < capacity)
                ++selected_count;
        }
    }
    return selected_count;
}

// Every authored lamp stays in the forward-light list, so its pool cannot pop off while the camera
// approaches. Reserve the cafe light, three crane lights, and nearby moving headlights.
static void select_local_lights(WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame)
{
    if (frame->boating_basin) {
        scene->local_light_count = 0;
        return;
    }
    LightCandidate candidates[72];
    int candidate_count = 0;
    add_light_candidate(candidates, &candidate_count, 40,
        (WaterfrontGpuLight) { .position = waterfront_corner_position(-2.8f, 3.1f, -6),
            .direction = { 0, -1, 0 },
            .color = { 255, 197, 119, 255 },
            .radius = 12,
            .intensity = 1.5f,
            .inner_cone_cos = -1,
            .outer_cone_cos = -1 },
        LIGHT_SHOP);
    add_light_candidate(candidates, &candidate_count, 10,
        (WaterfrontGpuLight) { .position = crane_point(-42, 75, -5),
            .direction = Vector3Normalize(crane_point(.2f, -1, .3f)),
            .color = { 255, 92, 48, 255 },
            .radius = 34,
            .intensity = 1.45f,
            .inner_cone_cos = .78f,
            .outer_cone_cos = .48f },
        LIGHT_CRANE);
    add_light_candidate(candidates, &candidate_count, 11,
        (WaterfrontGpuLight) { .position = crane_point(5, 75, -5),
            .direction = Vector3Normalize(crane_point(0, -1, .15f)),
            .color = { 106, 135, 255, 255 },
            .radius = 26,
            .intensity = 1.35f,
            .inner_cone_cos = .82f,
            .outer_cone_cos = .55f },
        LIGHT_CRANE);
    add_light_candidate(candidates, &candidate_count, 12,
        (WaterfrontGpuLight) { .position = crane_point(40, 75, -5),
            .direction = Vector3Normalize(crane_point(-.2f, -1, .3f)),
            .color = { 255, 78, 40, 255 },
            .radius = 31,
            .intensity = 1.3f,
            .inner_cone_cos = .78f,
            .outer_cone_cos = .48f },
        LIGHT_CRANE);
    for (int street_index = 0; street_index < 2; ++street_index) {
        for (int i = 0; i < frame->lamp_count; ++i) {
            const WaterfrontLampState *lamp = &frame->lamps[i];
            if (lamp->street != street_index)
                continue;
            add_light_candidate(candidates, &candidate_count, lamp->id,
                (WaterfrontGpuLight) { .position = lamp->lens,
                    .direction = { 0, -1, 0 },
                    .color = { 255, 202, 116, 255 },
                    .radius = 18,
                    .intensity = 1.0f,
                    .inner_cone_cos = -1,
                    .outer_cone_cos = -1 },
                LIGHT_LAMP);
        }
        for (int car = 0; !frame->player_active && car < WATERFRONT_CAR_COUNT; ++car) {
            const WaterfrontCarState *state = &frame->cars[car];
            if (state->street != street_index)
                continue;
            for (int side = 0; side < 2; ++side) {
                add_light_candidate(candidates, &candidate_count, state->id + side,
                    (WaterfrontGpuLight) { .position = state->lenses[1][side],
                        .direction = state->headlight_direction,
                        .color = { 255, 233, 174, 255 },
                        .radius = 18,
                        .intensity = 1.25f,
                        .inner_cone_cos = .90f,
                        .outer_cone_cos = .72f },
                    LIGHT_HEADLIGHT);
            }
        }
    }

    LightCandidate selected[WATERFRONT_GPU_MAX_LOCAL_LIGHTS];
    int selected_count
        = select_light_class(scene, candidates, candidate_count, frame, LIGHT_CRANE, selected, 3);
    selected_count += select_light_class(
        scene, candidates, candidate_count, frame, LIGHT_SHOP, &selected[selected_count], 1);
    selected_count += select_light_class(scene, candidates, candidate_count, frame, LIGHT_LAMP,
        &selected[selected_count], WATERFRONT_GPU_MAX_LOCAL_LIGHTS - selected_count - 3);
    selected_count += select_light_class(scene, candidates, candidate_count, frame, LIGHT_HEADLIGHT,
        &selected[selected_count], WATERFRONT_GPU_MAX_LOCAL_LIGHTS - selected_count);
    scene->local_light_count = selected_count;
    for (int i = 0; i < selected_count; ++i) {
        scene->local_light_ids[i] = selected[i].id;
        scene->local_lights[i] = selected[i].light;
    }
}

static void set_lighting_uniforms(WaterfrontGpuScene *scene)
{
    static const float ambient_color[] = { .48f, .57f, .65f };
    static const float directional_color[] = { 1.0f, .92f, .74f };
    Vector3 sun = waterfront_sun_direction();
    float directional_direction[] = { sun.x, sun.y, sun.z };
    int location = GetShaderLocation(scene->shader, "ambientColor");
    if (location >= 0)
        SetShaderValue(scene->shader, location, ambient_color, SHADER_UNIFORM_VEC3);
    location = GetShaderLocation(scene->shader, "directionalColor");
    if (location >= 0)
        SetShaderValue(scene->shader, location, directional_color, SHADER_UNIFORM_VEC3);
    location = GetShaderLocation(scene->shader, "directionalDirection");
    if (location >= 0)
        SetShaderValue(scene->shader, location, directional_direction, SHADER_UNIFORM_VEC3);
    location = GetShaderLocation(scene->shader, "directionalIntensity");
    if (location >= 0) {
        float intensity = .65f;
        SetShaderValue(scene->shader, location, &intensity, SHADER_UNIFORM_FLOAT);
    }
    int shadows_enabled = scene->shadows_enabled && scene->shadow_map.depth.id != 0;
    location = GetShaderLocation(scene->shader, "shadowsEnabled");
    if (location >= 0)
        SetShaderValue(scene->shader, location, &shadows_enabled, SHADER_UNIFORM_INT);
    if (shadows_enabled) {
        location = GetShaderLocation(scene->shader, "lightViewProjection");
        if (location >= 0)
            SetShaderValueMatrix(scene->shader, location, scene->light_view_projection);
        location = GetShaderLocation(scene->shader, "shadowMap");
        if (location >= 0) {
            int texture_slot = 10;
            rlEnableShader(scene->shader.id);
            rlActiveTextureSlot(texture_slot);
            rlEnableTexture(scene->shadow_map.depth.id);
            rlSetUniform(location, &texture_slot, SHADER_UNIFORM_INT, 1);
            rlActiveTextureSlot(0);
        }
    }
    int light_count = scene->lighting_enabled ? scene->local_light_count : 0;
    location = GetShaderLocation(scene->shader, "localLightCount");
    if (location >= 0)
        SetShaderValue(scene->shader, location, &light_count, SHADER_UNIFORM_INT);
    if (light_count == 0)
        return;

    float positions[WATERFRONT_GPU_MAX_LOCAL_LIGHTS * 4];
    float colors[WATERFRONT_GPU_MAX_LOCAL_LIGHTS * 4];
    float directions[WATERFRONT_GPU_MAX_LOCAL_LIGHTS * 4];
    float cones[WATERFRONT_GPU_MAX_LOCAL_LIGHTS * 4];
    for (int i = 0; i < light_count; ++i) {
        WaterfrontGpuLight light = scene->local_lights[i];
        float *position = &positions[i * 4];
        float *color = &colors[i * 4];
        float *direction = &directions[i * 4];
        float *cone = &cones[i * 4];
        position[0] = light.position.x;
        position[1] = light.position.y;
        position[2] = light.position.z;
        position[3] = light.radius;
        color[0] = (float)light.color.r / 255.0f;
        color[1] = (float)light.color.g / 255.0f;
        color[2] = (float)light.color.b / 255.0f;
        color[3] = light.intensity;
        direction[0] = light.direction.x;
        direction[1] = light.direction.y;
        direction[2] = light.direction.z;
        direction[3] = 0;
        cone[0] = light.inner_cone_cos;
        cone[1] = light.outer_cone_cos;
        cone[2] = 0;
        cone[3] = 0;
    }
    location = GetShaderLocation(scene->shader, "localLightPositionRadius[0]");
    if (location >= 0)
        SetShaderValueV(scene->shader, location, positions, SHADER_UNIFORM_VEC4, light_count);
    location = GetShaderLocation(scene->shader, "localLightColorIntensity[0]");
    if (location >= 0)
        SetShaderValueV(scene->shader, location, colors, SHADER_UNIFORM_VEC4, light_count);
    location = GetShaderLocation(scene->shader, "localLightDirection[0]");
    if (location >= 0)
        SetShaderValueV(scene->shader, location, directions, SHADER_UNIFORM_VEC4, light_count);
    location = GetShaderLocation(scene->shader, "localLightCone[0]");
    if (location >= 0)
        SetShaderValueV(scene->shader, location, cones, SHADER_UNIFORM_VEC4, light_count);
}

static void set_atmosphere_uniforms(WaterfrontGpuScene *scene, Vector3 camera_position)
{
    static const float fog_color[] = { 186.0f / 255.0f, 229.0f / 255.0f, 244.0f / 255.0f };
    float camera[] = { camera_position.x, camera_position.y, camera_position.z };
    float fog_start = 145.0f;
    float fog_end = 620.0f;
    float fog_opacity = .38f;
    int atmosphere_enabled = scene->atmosphere_enabled;
    int location = GetShaderLocation(scene->shader, "cameraPosition");
    if (location >= 0)
        SetShaderValue(scene->shader, location, camera, SHADER_UNIFORM_VEC3);
    location = GetShaderLocation(scene->shader, "fogColor");
    if (location >= 0)
        SetShaderValue(scene->shader, location, fog_color, SHADER_UNIFORM_VEC3);
    location = GetShaderLocation(scene->shader, "fogStart");
    if (location >= 0)
        SetShaderValue(scene->shader, location, &fog_start, SHADER_UNIFORM_FLOAT);
    location = GetShaderLocation(scene->shader, "fogEnd");
    if (location >= 0)
        SetShaderValue(scene->shader, location, &fog_end, SHADER_UNIFORM_FLOAT);
    location = GetShaderLocation(scene->shader, "fogOpacity");
    if (location >= 0)
        SetShaderValue(scene->shader, location, &fog_opacity, SHADER_UNIFORM_FLOAT);
    location = GetShaderLocation(scene->shader, "atmosphereEnabled");
    if (location >= 0)
        SetShaderValue(scene->shader, location, &atmosphere_enabled, SHADER_UNIFORM_INT);
}

static void set_water_uniforms(WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame)
{
    BoatWater water = frame->player_water;
    int objects = water.objects != NULL || water.traffic_objects != NULL;
    float object_life = water.objects ? water.objects->life_ticks / 120.0f : 16;
    SetShaderValue(scene->water_shader, GetShaderLocation(scene->water_shader, "objectWakeEnabled"),
        &objects, SHADER_UNIFORM_INT);
    SetShaderValue(scene->water_shader, GetShaderLocation(scene->water_shader, "objectWakeLife"),
        &object_life, SHADER_UNIFORM_FLOAT);
    int field_enabled = scene->wake_field.id != 0;
    SetShaderValue(scene->water_shader, GetShaderLocation(scene->water_shader, "wakeFieldEnabled"),
        &field_enabled, SHADER_UNIFORM_INT);
    if (field_enabled) {
        float grid[] = { (float)scene->wake_field.width, (float)scene->wake_field.height,
            scene->wake_spacing };
        SetShaderValue(scene->water_shader, GetShaderLocation(scene->water_shader, "wakeGrid"),
            grid, SHADER_UNIFORM_VEC3);
        int slot = 13;
        rlEnableShader(scene->water_shader.id);
        rlActiveTextureSlot(slot);
        rlEnableTexture(scene->wake_field.id);
        rlSetUniform(
            GetShaderLocation(scene->water_shader, "wakeField"), &slot, SHADER_UNIFORM_INT, 1);
        rlActiveTextureSlot(0);
    }
    int physical_waves = frame->player_active && (field_enabled || boat_water_displaced(water));
    SetShaderValue(scene->water_shader, GetShaderLocation(scene->water_shader, "physicalWaves"),
        &physical_waves, SHADER_UNIFORM_INT);
    float wake_amplitude = frame->player_active ? water.wake_amplitude : 0;
    float vessel[12];
    boat_vessel_wake_uniforms(
        frame->player_active ? water.vessel : (BoatVesselWake) { 0 }, frame->time, vessel);
    const char *vessel_uniforms[] = { "vesselOrigin", "vesselMotion", "vesselShape" };
    for (int i = 0; i < 3; ++i)
        SetShaderValue(scene->water_shader,
            GetShaderLocation(scene->water_shader, vessel_uniforms[i]), vessel + 4 * i,
            SHADER_UNIFORM_VEC4);
    SetShaderValue(scene->water_shader, GetShaderLocation(scene->water_shader, "wakeAmplitude"),
        &wake_amplitude, SHADER_UNIFORM_FLOAT);
    float wake_offset[] = { -water.wake_offset.x, water.wake_offset.y };
    SetShaderValue(scene->water_shader, GetShaderLocation(scene->water_shader, "wakeOffset"),
        wake_offset, SHADER_UNIFORM_VEC2);
    float wake_origins[BOAT_WAKE_SEGMENTS * 4], wake_parameters[BOAT_WAKE_SEGMENTS * 4];
    int wake_count = frame->player_active
        ? boat_water_wake_uniforms(water, frame->time, wake_origins, wake_parameters)
        : 0;
    if (objects && frame->player_active) {
        float records[BOAT_OBJECT_CAPACITY * 16] = { 0 };
        int capacity = BOAT_OBJECT_CAPACITY * (water.traffic_objects ? 2 : 1);
        float *parameters = records + capacity * 4;
        int players
            = water.objects ? boat_wake_objects_pack(water.objects, records, parameters) : 0;
        wake_count = players;
        if (water.traffic_objects)
            wake_count += boat_wake_objects_pack(
                water.traffic_objects, records + players * 4, parameters + players * 4);
        float traffic_life = water.traffic_objects ? water.traffic_objects->life_ticks / 120.0f : 0;
        SetShaderValue(scene->water_shader,
            GetShaderLocation(scene->water_shader, "objectWakePlayerCount"), &players,
            SHADER_UNIFORM_INT);
        SetShaderValue(scene->water_shader,
            GetShaderLocation(scene->water_shader, "objectWakeTrafficLife"), &traffic_life,
            SHADER_UNIFORM_FLOAT);
        if (scene->object_wakes.id && scene->object_wakes.width != capacity) {
            UnloadTexture(scene->object_wakes);
            scene->object_wakes = (Texture2D) { 0 };
        }
        if (!scene->object_wakes.id) {
            Image image = { records, capacity, 2, 1, PIXELFORMAT_UNCOMPRESSED_R32G32B32A32 };
            scene->object_wakes = LoadTextureFromImage(image);
            SetTextureFilter(scene->object_wakes, TEXTURE_FILTER_POINT);
        } else {
            UpdateTexture(scene->object_wakes, records);
        }
        int slot = 14;
        rlEnableShader(scene->water_shader.id);
        rlActiveTextureSlot(slot);
        rlEnableTexture(scene->object_wakes.id);
        rlSetUniform(
            GetShaderLocation(scene->water_shader, "objectWakeData"), &slot, SHADER_UNIFORM_INT, 1);
        rlActiveTextureSlot(0);
    }
    SetShaderValue(scene->water_shader, GetShaderLocation(scene->water_shader, "playerWakeCount"),
        &wake_count, SHADER_UNIFORM_INT);
    if (wake_count && !objects) {
        SetShaderValueV(scene->water_shader,
            GetShaderLocation(scene->water_shader, "playerWakeOrigins[0]"), wake_origins,
            SHADER_UNIFORM_VEC4, wake_count);
        SetShaderValueV(scene->water_shader,
            GetShaderLocation(scene->water_shader, "playerWakeParameters[0]"), wake_parameters,
            SHADER_UNIFORM_VEC4, wake_count);
    }
    int waterfront_water = frame->player_active && !frame->boating_basin;
    SetShaderValue(scene->water_shader, GetShaderLocation(scene->water_shader, "waterfrontWater"),
        &waterfront_water, SHADER_UNIFORM_INT);
    float patch[] = { frame->player.hull.position.x, frame->player.hull.position.z,
        BOAT_WATER_PATCH_FULL, BOAT_WATER_PATCH_RADIUS };
    SetShaderValue(scene->water_shader, GetShaderLocation(scene->water_shader, "wavePatch"), patch,
        SHADER_UNIFORM_VEC4);
    if (physical_waves) {
        float parameters[12];
        boat_water_wave_uniforms(water, frame->time, parameters);
        SetShaderValueV(scene->water_shader,
            GetShaderLocation(scene->water_shader, "waveParameters[0]"), parameters,
            SHADER_UNIFORM_VEC4, 3);
    }
    int shadows = scene->shadows_enabled && frame->player_active;
    Vector3 sun = waterfront_sun_direction();
    float sun_direction[] = { sun.x, sun.y, sun.z };
    SetShaderValue(scene->water_shader, GetShaderLocation(scene->water_shader, "sunDirection"),
        sun_direction, SHADER_UNIFORM_VEC3);
    int shadow_location = GetShaderLocation(scene->water_shader, "shadowsEnabled");
    SetShaderValue(scene->water_shader, shadow_location, &shadows, SHADER_UNIFORM_INT);
    SetShaderValueMatrix(scene->water_shader,
        GetShaderLocation(scene->water_shader, "lightViewProjection"),
        scene->light_view_projection);
    if (shadows) {
        int slot = 12;
        rlEnableShader(scene->water_shader.id);
        rlActiveTextureSlot(slot);
        rlEnableTexture(scene->shadow_map.depth.id);
        rlSetUniform(
            GetShaderLocation(scene->water_shader, "shadowMap"), &slot, SHADER_UNIFORM_INT, 1);
        rlActiveTextureSlot(0);
    }
    float camera[]
        = { frame->camera.position.x, frame->camera.position.y, frame->camera.position.z };
    int reflections_enabled = scene->reflections_enabled;
    int location = GetShaderLocation(scene->water_shader, "reflectionViewProjection");
    if (location >= 0)
        SetShaderValueMatrix(scene->water_shader, location, scene->reflection_view_projection);
    location = GetShaderLocation(scene->water_shader, "cameraPosition");
    if (location >= 0)
        SetShaderValue(scene->water_shader, location, camera, SHADER_UNIFORM_VEC3);
    // Double frame time never reaches a float uniform; upload bounded, reduced payloads.
    float crossing_clock = boat_water_crossing_clock(water, frame->time);
    location = GetShaderLocation(scene->water_shader, "crossingWakeTime");
    if (location >= 0)
        SetShaderValue(scene->water_shader, location, &crossing_clock, SHADER_UNIFORM_FLOAT);
    float detail_phase[] = { boat_phase(1.5, frame->time), boat_phase(2.1, frame->time) };
    location = GetShaderLocation(scene->water_shader, "detailRipplePhase");
    if (location >= 0)
        SetShaderValue(scene->water_shader, location, detail_phase, SHADER_UNIFORM_VEC2);
    float reflection_phase[] = { boat_phase(.80, frame->time), boat_phase(.55, frame->time),
        boat_phase(.65, frame->time), boat_phase(.35, frame->time) };
    location = GetShaderLocation(scene->water_shader, "reflectionRipplePhase");
    if (location >= 0)
        SetShaderValue(scene->water_shader, location, reflection_phase, SHADER_UNIFORM_VEC4);
    location = GetShaderLocation(scene->water_shader, "reflectionsEnabled");
    if (location >= 0)
        SetShaderValue(scene->water_shader, location, &reflections_enabled, SHADER_UNIFORM_INT);
    location = GetShaderLocation(scene->water_shader, "reflectionTexture");
    if (location >= 0) {
        int texture_slot = 11;
        rlEnableShader(scene->water_shader.id);
        rlActiveTextureSlot(texture_slot);
        rlEnableTexture(scene->reflection.texture.id);
        rlSetUniform(location, &texture_slot, SHADER_UNIFORM_INT, 1);
        rlActiveTextureSlot(0);
    }
}

// Called only in the main colour pass; ghosts never enter reflection or shadow passes.
static void draw_ghost(const WaterfrontGpuScene *scene, Material material,
    const WaterfrontSceneFrame *frame, int width)
{
    if (frame->ghost_opacity <= 0)
        return;
    int opacity = GetShaderLocation(material.shader, "ghostOpacity");
    int pixel_scale = GetShaderLocation(material.shader, "ghostPixelScale");
    float scale = (float)width / 320;
    SetShaderValue(material.shader, pixel_scale, &scale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(material.shader, opacity, &frame->ghost_opacity, SHADER_UNIFORM_FLOAT);
    draw_colored_mesh(scene->player_hull[boat_skin_index(frame->ghost_skin)], material,
        frame->ghost_hull, WHITE, true);
    draw_colored_mesh(scene->player_cabin[boat_skin_index(frame->ghost_skin)], material,
        frame->ghost_cabin, WHITE, true);
    draw_colored_mesh(scene->player_engine[boat_skin_index(frame->ghost_skin)], material,
        frame->ghost_engine, WHITE, true);
    if (scene->ghost_flag.vaoId)
        draw_colored_mesh(
            scene->ghost_flag, material, (Transform3D) { .scale = { 1, 1, 1 } }, WHITE, true);
    float zero = 0;
    SetShaderValue(material.shader, opacity, &zero, SHADER_UNIFORM_FLOAT);
}

static void draw_harbor(
    const WaterfrontGpuScene *scene, Material material, const WaterfrontSceneFrame *frame)
{
    const WaterfrontHarborFrame *harbor = &frame->harbor;
    draw_colored_mesh(
        scene->harbor_quay, material, (Transform3D) { .scale = { 1, 1, 1 } }, WHITE, false);
    draw_colored_mesh(scene->harbor_pontoon, material, harbor->pontoon, WHITE, false);
    draw_colored_mesh(scene->cube, material, harbor->gangway, (Color) { 154, 123, 84, 255 }, false);
    for (int i = 0; i < WATERFRONT_HARBOR_BOATS; ++i)
        draw_colored_mesh(scene->harbor_boats[i], material, harbor->boats[i], WHITE, false);
    for (int i = 0; i < harbor->line_count; ++i)
        draw_colored_mesh(
            scene->cube, material, harbor->lines[i], (Color) { 169, 153, 113, 255 }, false);
}

static void draw_traffic(const WaterfrontGpuScene *scene, Material material,
    const WaterfrontSceneFrame *frame, bool foam)
{
    const WaterfrontTrafficFrame *traffic = &frame->traffic;
    bool lights = material.shader.id != scene->shadow_shader.id;
    const Color colors[] = { { 222, 70, 52, 255 }, { 72, 191, 139, 255 }, { 235, 223, 188, 255 } };
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
        const WaterfrontVesselState *vessel = &traffic->vessels[i];
        draw_colored_mesh(scene->traffic_boats[i], material, vessel->pose, WHITE, false);
        float size = i == 3 ? .32f : .18f;
        for (int light = 0; lights && light < WATERFRONT_TRAFFIC_LIGHTS; ++light)
            draw_colored_mesh(scene->cube, material,
                (Transform3D) { .position = vessel->lights[light], .scale = { size, size, size } },
                colors[light], true);
    }
    for (int i = 0; foam && i < traffic->wake_count; ++i)
        draw_colored_mesh(
            scene->plane, material, traffic->wakes[i].pose, traffic->wakes[i].color, false);
}

typedef struct {
    Transform3D hull, cabin, engine;
    unsigned int skin;
    Mesh flag;
    const WaterfrontPlayerParticle *particles;
    int particle_count;
} BoatDrawParts;

static void draw_boat(
    const WaterfrontGpuScene *scene, Material material, BoatDrawParts b, bool emissive)
{
    unsigned int index = boat_skin_index(b.skin);
    draw_colored_mesh(scene->player_hull[index], material, b.hull, WHITE, false);
    draw_colored_mesh(scene->player_cabin[index], material, b.cabin, WHITE, false);
    draw_colored_mesh(scene->player_engine[index], material, b.engine, WHITE, false);
    Mesh flag = b.flag;
    const WaterfrontPlayerParticle *particles = b.particles;
    int particle_count = b.particle_count;
    if (flag.vaoId)
        draw_colored_mesh(flag, material, (Transform3D) { .scale = { 1, 1, 1 } }, WHITE, false);
    for (int i = 0; i < particle_count; ++i) {
        // Surface foam receives shadows/depth but does not cast a solid slab shadow.
        // Airborne spray participates in both the reflection and shadow passes.
        if (!emissive && !particles[i].spray)
            continue;
        draw_colored_mesh(scene->cube, material, particles[i].transform, particles[i].color, false);
    }
}

static void draw_scenery(
    const WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame, Material material)
{
    for (int i = 0; i < frame->scenery_count; ++i)
        draw_colored_mesh(
            scene->cube, material, frame->scenery[i].transform, frame->scenery[i].color, false);
}

static void draw_life(const WaterfrontGpuScene *scene, Material material,
    const WaterfrontSceneFrame *frame, bool emissive)
{
    if (!frame->boating_basin) {
        draw_scenery(scene, frame, material);
        draw_harbor(scene, material, frame);
        draw_traffic(scene, material, frame, emissive);
    }
    for (int i = 0; i < frame->gull_count; ++i)
        for (int part = 0; part < WATERFRONT_GULL_PARTS; ++part)
            draw_colored_mesh(scene->gull[part], material, frame->gulls[i][part], WHITE, true);
    if (frame->player_active) {
        for (int i = 0; i < frame->lamp_count; ++i) {
            const WaterfrontLampState *lamp = &frame->lamps[i];
            draw_colored_mesh(
                scene->cube, material, lamp->pole, (Color) { 58, 66, 79, 255 }, false);
            if (emissive)
                draw_colored_mesh(scene->cube, material,
                    (Transform3D) { .position = lamp->lens, .scale = { .85f, .38f, .85f } },
                    LAMP_LENS_COLOR, true);
        }
        const WaterfrontPlayerState *p = &frame->player;
        draw_boat(scene, material,
            (BoatDrawParts) { p->hull, p->cabin, p->engine, p->skin, scene->player_flag,
                p->particles, p->particle_count },
            emissive);
        for (int i = 0; i < frame->rival_count; ++i) {
            const WaterfrontRivalState *r = &frame->rivals[i];
            draw_boat(scene, material,
                (BoatDrawParts) { r->hull, r->cabin, r->engine, r->skin, scene->rival_flags[i],
                    r->particles, r->particle_count },
                emissive);
        }
        return;
    }
    for (int street_index = 0; street_index < 2; ++street_index) {
        for (int i = 0; i < frame->lamp_count; ++i) {
            const WaterfrontLampState *lamp = &frame->lamps[i];
            if (lamp->street != street_index)
                continue;
            draw_colored_mesh(
                scene->cube, material, lamp->pole, (Color) { 58, 66, 79, 255 }, false);
            if (emissive)
                draw_colored_mesh(scene->cube, material,
                    (Transform3D) { .position = lamp->lens, .scale = { .85f, .38f, .85f } },
                    LAMP_LENS_COLOR, true);
        }
        for (int car = 0; car < WATERFRONT_CAR_COUNT; ++car) {
            const WaterfrontCarState *state = &frame->cars[car];
            if (state->street != street_index)
                continue;
            draw_colored_mesh(scene->cube, material, state->body, state->color, false);
            draw_colored_mesh(
                scene->cube, material, state->cabin, (Color) { 49, 65, 81, 255 }, false);
            if (emissive) {
                // Source lenses are always emissive. The bounded forward-light selection only
                // decides which nearby headlights affect surrounding surfaces this frame->
                for (int end = -1; end <= 1; end += 2) {
                    for (int side = -1; side <= 1; side += 2) {
                        draw_colored_mesh(scene->cube, material,
                            (Transform3D) { .position = state->lenses[end > 0][side > 0],
                                .scale = { .48f, .34f, .48f } },
                            end > 0 ? HEADLIGHT_LENS_COLOR : (Color) { 240, 56, 34, 255 }, true);
                    }
                }
            }
        }
    }
}

static void draw_crane_lights(const WaterfrontGpuScene *scene, Material material)
{
    // Source fixtures stay intentionally chunky: at virtual resolution they read as the coloured
    // work lights in the reference rather than as a row of sub-pixel bulbs.
    draw_colored_mesh(scene->cube, material,
        (Transform3D) { .position = crane_point(-42, 75, -5), .scale = { 1.3f, .45f, .7f } },
        (Color) { 255, 92, 48, 255 }, true);
    draw_colored_mesh(scene->cube, material,
        (Transform3D) { .position = crane_point(5, 75, -5), .scale = { .85f, .85f, .85f } },
        (Color) { 122, 145, 255, 255 }, true);
    draw_colored_mesh(scene->cube, material,
        (Transform3D) { .position = crane_point(40, 75, -5), .scale = { 1.15f, .4f, .65f } },
        (Color) { 255, 86, 44, 255 }, true);
}

static Camera3D reflection_camera(const WaterfrontSceneFrame *frame)
{
    return (Camera3D) { .position
        = { frame->camera.position.x, -frame->camera.position.y, frame->camera.position.z },
        .target = { frame->camera.target.x, -frame->camera.target.y, frame->camera.target.z },
        .up = { 0, -1, 0 },
        .fovy = frame->camera.vertical_fov * RAD2DEG,
        .projection = CAMERA_PERSPECTIVE };
}

typedef struct {
    unsigned int framebuffer;
    int width, height;
    Matrix projection, modelview;
} RenderTargetState;

static RenderTargetState render_target_state(void)
{
    return (RenderTargetState) { .framebuffer = rlGetActiveFramebuffer(),
        .width = rlGetFramebufferWidth(),
        .height = rlGetFramebufferHeight(),
        .projection = rlGetMatrixProjection(),
        .modelview = rlGetMatrixModelview() };
}

static void restore_render_target(RenderTargetState state)
{
    // raylib's EndTextureMode returns to the window, including its 2D matrices. Nested
    // shadow/reflection passes must restore the caller's matrices as well as its framebuffer:
    // the sky and subsequent overlays otherwise draw at the window's size inside that target.
    if (state.framebuffer != 0)
        rlEnableFramebuffer(state.framebuffer);
    else
        rlDisableFramebuffer();
    rlViewport(0, 0, state.width, state.height);
    rlSetFramebufferWidth(state.width);
    rlSetFramebufferHeight(state.height);
    rlSetMatrixProjection(state.projection);
    rlSetMatrixModelview(state.modelview);
}

static void draw_sky(
    const WaterfrontGpuScene *scene, View3D camera, int width, int height, float aspect)
{
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Vector3 up = Vector3CrossProduct(right, forward);
    float scale[] = { tanf(camera.vertical_fov * .5f) * aspect, tanf(camera.vertical_fov * .5f) };
    Shader shader = scene->sky_shader;
    SetShaderValue(shader, GetShaderLocation(shader, "skyForward"), &forward, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "skyRight"), &right, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "skyUp"), &up, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "skyScale"), scale, SHADER_UNIFORM_VEC2);
    BeginShaderMode(shader);
    DrawTexturePro(scene->sky_texture,
        (Rectangle) { 0, 0, scene->sky_texture.width, scene->sky_texture.height },
        (Rectangle) { 0, 0, width, height }, (Vector2) { 0 }, 0, WHITE);
    EndShaderMode();
}

static void draw_ground(const WaterfrontGpuScene *scene, Material material)
{
    int location = GetShaderLocation(scene->shader, "groundPass");
    int enabled = 1;
    SetShaderValue(scene->shader, location, &enabled, SHADER_UNIFORM_INT);
    DrawMesh(scene->quay, material, MatrixIdentity());
    enabled = 0;
    SetShaderValue(scene->shader, location, &enabled, SHADER_UNIFORM_INT);
}

static void draw_facades(const WaterfrontGpuScene *scene, Material material)
{
    int location = GetShaderLocation(scene->shader, "facadePass");
    int enabled = 1;
    SetShaderValue(scene->shader, location, &enabled, SHADER_UNIFORM_INT);
    DrawMesh(scene->facades, material, MatrixIdentity());
    enabled = 0;
    SetShaderValue(scene->shader, location, &enabled, SHADER_UNIFORM_INT);
}

static void draw_reflected_scene(const WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame,
    Material material, Material life)
{
    if (frame->boating_basin) {
        draw_scenery(scene, frame, material);
        draw_life(scene, life, frame, false);
        return;
    }
    set_object_color(scene->shader, WHITE);
    set_emissive(scene->shader, false);
    DrawMesh(scene->city, material, MatrixIdentity());
    draw_facades(scene, material);
    DrawMesh(scene->roofs, scene->roof_material, MatrixIdentity());
    draw_ground(scene, material);
    DrawMesh(scene->corner, material, MatrixIdentity());
    set_emissive(scene->shader, true);
    DrawMesh(scene->windows, material, MatrixIdentity());
    DrawMesh(scene->corner_emissive, material, MatrixIdentity());
    draw_colored_mesh(scene->bridge, life, frame->bridge, frame->bridge_color, false);
    // The reflection source deliberately excludes the water mesh and its wake, preventing a
    // feedback surface while retaining vehicles, boat hull, lamp poles, and the city silhouette.
    draw_life(scene, life, frame, false);
    set_emissive(scene->shader, false);
    DrawMesh(scene->crane, material, MatrixIdentity());
}

static void render_reflection(
    WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame, float aspect)
{
    Camera3D camera = reflection_camera(frame);
    // Cover the output aspect plus 20% overscan for ripples and displaced wave vertices.
    camera.fovy = 2 * atanf(tanf(frame->camera.vertical_fov * .5f) * 1.2f) * RAD2DEG;
    RenderTargetState previous = render_target_state();
    BeginTextureMode(scene->reflection);
    ClearBackground(BLACK);
    View3D sky_camera = frame->camera;
    sky_camera.position = camera.position;
    sky_camera.target = camera.target;
    sky_camera.up = camera.up;
    sky_camera.vertical_fov = camera.fovy * DEG2RAD;
    // The fixed-size texture stores this wider frustum, independent of its pixel aspect.
    draw_sky(scene, sky_camera, scene->reflection.texture.width, scene->reflection.texture.height,
        aspect);
    BeginMode3D(camera);
    rlSetMatrixProjection(MatrixPerspective(
        camera.fovy * DEG2RAD, aspect, frame->camera.near_plane, frame->camera.far_plane));
    Matrix view = rlGetMatrixModelview();
    Matrix projection = rlGetMatrixProjection();
    // A reflected view reverses the projected winding. Disable culling for this pass so the
    // separate camera does not require mirrored geometry or per-mesh winding changes.
    rlDisableBackfaceCulling();
    draw_reflected_scene(scene, frame, scene->city_material, scene->life_material);
    rlEnableBackfaceCulling();
    EndMode3D();
    EndTextureMode();
    restore_render_target(previous);
    scene->reflection_view_projection = MatrixMultiply(view, projection);
}

static Camera3D shadow_camera(const WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame)
{
    const float coverage = frame->player_active ? 96.0f : 500.0f;
    const float texel_size = coverage / scene->shadow_map_size;
    Vector3 light_direction = waterfront_sun_direction();
    Vector3 right = Vector3Normalize(Vector3CrossProduct((Vector3) { 0, 1, 0 }, light_direction));
    Vector3 up = Vector3CrossProduct(light_direction, right);
    Vector3 target = frame->camera.target;
    float right_offset = roundf(Vector3DotProduct(target, right) / texel_size) * texel_size
        - Vector3DotProduct(target, right);
    float up_offset = roundf(Vector3DotProduct(target, up) / texel_size) * texel_size
        - Vector3DotProduct(target, up);
    target = Vector3Add(
        target, Vector3Add(Vector3Scale(right, right_offset), Vector3Scale(up, up_offset)));
    return (Camera3D) { .position = Vector3Add(target, Vector3Scale(light_direction, 600)),
        .target = target,
        .up = up,
        .fovy = coverage,
        .projection = CAMERA_ORTHOGRAPHIC };
}

static void draw_shadow_casters(
    const WaterfrontGpuScene *scene, Material material, const WaterfrontSceneFrame *frame)
{
    if (frame->boating_basin) {
        draw_scenery(scene, frame, material);
        draw_life(scene, material, frame, false);
        return;
    }
    set_object_color(scene->shadow_shader, WHITE);
    set_emissive(scene->shadow_shader, false);
    rlEnableBackfaceCulling();
    DrawMesh(scene->city, material, MatrixIdentity());
    DrawMesh(scene->facades, material, MatrixIdentity());
    DrawMesh(scene->roofs, material, MatrixIdentity());
    DrawMesh(scene->quay, material, MatrixIdentity());
    DrawMesh(scene->corner, material, MatrixIdentity());
    draw_colored_mesh(scene->bridge, material, frame->bridge, frame->bridge_color, false);
    // Cars, boats, and lamp poles matter to the road shadow, but their lenses and wake do not.
    rlDisableBackfaceCulling();
    draw_life(scene, material, frame, false);
    rlEnableBackfaceCulling();
    DrawMesh(scene->crane, material, MatrixIdentity());
}

static void render_shadow_map(WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame)
{
    Camera3D camera = shadow_camera(scene, frame);
    RenderTargetState previous = render_target_state();
    BeginTextureMode(scene->shadow_map);
    ClearBackground(WHITE);
    BeginMode3D(camera);
    float half_coverage = camera.fovy * .5f;
    rlSetMatrixProjection(
        MatrixOrtho(-half_coverage, half_coverage, -half_coverage, half_coverage, .25, 1400));
    Matrix view = rlGetMatrixModelview();
    Matrix projection = rlGetMatrixProjection();
    draw_shadow_casters(scene, scene->shadow_material, frame);
    EndMode3D();
    EndTextureMode();
    restore_render_target(previous);
    // raylib composes matrices in this order for its shaders; retain the pinned example's
    // convention so this matrix maps world positions into the depth texture's clip space.
    scene->light_view_projection = MatrixMultiply(view, projection);
}

static void draw_water(WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame, int width)
{
    set_object_color(scene->water_shader, WHITE);
    set_water_uniforms(scene, frame);
    float pixel_scale = fmaxf(1, width / 320.0f);
    SetShaderValue(scene->water_shader, GetShaderLocation(scene->water_shader, "surfacePixelScale"),
        &pixel_scale, SHADER_UNIFORM_FLOAT);
    int pass = scene->near_water.vaoId ? 1 : 0;
    int location = GetShaderLocation(scene->water_shader, "waterPatchPass");
    SetShaderValue(scene->water_shader, location, &pass, SHADER_UNIFORM_INT);
    DrawMesh(scene->water, scene->water_material, MatrixIdentity());
    if (pass) {
        pass = 2;
        set_object_color(scene->water_shader, WHITE);
        SetShaderValue(scene->water_shader, location, &pass, SHADER_UNIFORM_INT);
        DrawMesh(scene->near_water, scene->water_material, MatrixIdentity());
        if (scene->near_water_extra.vaoId)
            DrawMesh(scene->near_water_extra, scene->water_material, MatrixIdentity());
    }
}

void waterfront_gpu_scene_draw(
    WaterfrontGpuScene *scene, const WaterfrontSceneFrame *frame, int width, int height)
{
    if (frame->player_active)
        update_flag_mesh(&scene->player_flag, &frame->player.flag, frame->player.skin);
    if (frame->ghost_opacity > 0)
        update_flag_mesh(&scene->ghost_flag, &frame->ghost_flag, frame->ghost_skin);
    for (int i = 0; frame->player_active && i < frame->rival_count; ++i)
        update_flag_mesh(&scene->rival_flags[i], &frame->rivals[i].flag, frame->rivals[i].skin);
    Camera3D camera = { .position = frame->camera.position,
        .target = frame->camera.target,
        .up = frame->camera.up,
        .fovy = frame->camera.vertical_fov * RAD2DEG,
        .projection = CAMERA_PERSPECTIVE };
    Material material = scene->city_material;
    Material life_material = scene->life_material;
    if (scene->shadows_enabled)
        render_shadow_map(scene, frame);
    select_local_lights(scene, frame);
    set_lighting_uniforms(scene);
    if (scene->reflections_enabled) {
        set_atmosphere_uniforms(scene, reflection_camera(frame).position);
        render_reflection(scene, frame, (float)width / height);
    }
    set_atmosphere_uniforms(scene, frame->camera.position);
    ClearBackground(BLACK);
    draw_sky(scene, frame->camera, width, height, (float)width / height);
    BeginMode3D(camera);
    rlSetMatrixProjection(MatrixPerspective(frame->camera.vertical_fov,
        (float)width / (float)height, frame->camera.near_plane, frame->camera.far_plane));
    rlEnableBackfaceCulling();
    if (frame->boating_basin) {
        draw_scenery(scene, frame, material);
        draw_water(scene, frame, width);
        rlDisableBackfaceCulling();
        draw_life(scene, life_material, frame, true);
        draw_ghost(scene, life_material, frame, width);
        EndMode3D();
        return;
    }
    set_object_color(scene->shader, WHITE);
    set_emissive(scene->shader, false);
    DrawMesh(scene->city, material, MatrixIdentity());
    draw_facades(scene, material);
    DrawMesh(scene->roofs, scene->roof_material, MatrixIdentity());
    draw_ground(scene, material);
    DrawMesh(scene->corner, material, MatrixIdentity());
    draw_water(scene, frame, width);
    set_emissive(scene->shader, true);
    DrawMesh(scene->windows, material, MatrixIdentity());
    DrawMesh(scene->corner_emissive, material, MatrixIdentity());
    draw_colored_mesh(scene->bridge, life_material, frame->bridge, frame->bridge_color, false);
    // These small closed meshes use reusable, non-indexed buffers. Keep them two-sided while
    // retaining depth testing, so their visibility does not depend on backend cull-state defaults.
    rlDisableBackfaceCulling();
    draw_life(scene, life_material, frame, true);
    rlEnableBackfaceCulling();
    set_object_color(scene->shader, WHITE);
    set_emissive(scene->shader, false);
    DrawMesh(scene->crane, material, MatrixIdentity());
    rlDisableBackfaceCulling();
    draw_crane_lights(scene, life_material);
    rlEnableBackfaceCulling();
    EndMode3D();
}

void waterfront_gpu_scene_set_lighting_enabled(WaterfrontGpuScene *scene, bool enabled)
{
    scene->lighting_enabled = enabled;
}

void waterfront_gpu_scene_set_shadows_enabled(WaterfrontGpuScene *scene, bool enabled)
{
    scene->shadows_enabled = enabled;
}

void waterfront_gpu_scene_set_atmosphere_enabled(WaterfrontGpuScene *scene, bool enabled)
{
    scene->atmosphere_enabled = enabled;
}

void waterfront_gpu_scene_set_reflections_enabled(WaterfrontGpuScene *scene, bool enabled)
{
    scene->reflections_enabled = enabled;
}

bool waterfront_gpu_scene_set_shadow_map_size(WaterfrontGpuScene *scene, int size)
{
    if (size != 512 && size != WATERFRONT_GPU_SHADOW_MAP_SIZE)
        return false;
    if (size == scene->shadow_map_size)
        return true;
    RenderTexture2D replacement;
    if (!create_shadow_map(&replacement, size))
        return false;
    if (scene->shadow_map.id != 0)
        rlUnloadFramebuffer(scene->shadow_map.id);
    scene->shadow_map = replacement;
    scene->shadow_map_size = size;
    return true;
}

Texture2D waterfront_gpu_scene_shadow_map(const WaterfrontGpuScene *scene)
{
    return scene->shadow_map.depth;
}

void waterfront_gpu_scene_destroy(WaterfrontGpuScene *scene)
{
    if (scene->city.vaoId != 0)
        UnloadMesh(scene->city);
    if (scene->facades.vaoId != 0)
        UnloadMesh(scene->facades);
    if (scene->roofs.vaoId != 0)
        UnloadMesh(scene->roofs);
    if (scene->quay.vaoId != 0)
        UnloadMesh(scene->quay);
    if (scene->water.vaoId != 0)
        UnloadMesh(scene->water);
    if (scene->windows.vaoId != 0)
        UnloadMesh(scene->windows);
    if (scene->corner.vaoId != 0)
        UnloadMesh(scene->corner);
    if (scene->corner_emissive.vaoId != 0)
        UnloadMesh(scene->corner_emissive);
    if (scene->crane.vaoId != 0)
        UnloadMesh(scene->crane);
    if (scene->bridge.vaoId != 0)
        UnloadMesh(scene->bridge);
    for (int part = 0; part < WATERFRONT_GULL_PARTS; ++part)
        if (scene->gull[part].vaoId != 0)
            UnloadMesh(scene->gull[part]);
    if (scene->cube.vaoId != 0)
        UnloadMesh(scene->cube);
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i)
        if (scene->traffic_boats[i].vaoId)
            UnloadMesh(scene->traffic_boats[i]);
    for (int i = 0; i < WATERFRONT_HARBOR_BOATS; ++i)
        if (scene->harbor_boats[i].vaoId)
            UnloadMesh(scene->harbor_boats[i]);
    if (scene->harbor_pontoon.vaoId)
        UnloadMesh(scene->harbor_pontoon);
    if (scene->harbor_quay.vaoId)
        UnloadMesh(scene->harbor_quay);
    if (scene->player_flag.vaoId)
        UnloadMesh(scene->player_flag);
    if (scene->ghost_flag.vaoId)
        UnloadMesh(scene->ghost_flag);
    for (int i = 0; i < WATERFRONT_RIVALS; ++i)
        if (scene->rival_flags[i].vaoId)
            UnloadMesh(scene->rival_flags[i]);
    for (int skin = 0; skin < BOAT_SKIN_COUNT; ++skin)
        if (scene->player_hull[skin].vaoId != 0)
            UnloadMesh(scene->player_hull[skin]);
    for (int skin = 0; skin < BOAT_SKIN_COUNT; ++skin)
        if (scene->player_cabin[skin].vaoId != 0)
            UnloadMesh(scene->player_cabin[skin]);
    for (int skin = 0; skin < BOAT_SKIN_COUNT; ++skin)
        if (scene->player_engine[skin].vaoId != 0)
            UnloadMesh(scene->player_engine[skin]);
    if (scene->plane.vaoId != 0)
        UnloadMesh(scene->plane);
    if (scene->city_material.maps != NULL) {
        scene->city_material.shader.id = rlGetShaderIdDefault();
        UnloadMaterial(scene->city_material);
    }
    if (scene->roof_material.maps != NULL) {
        scene->roof_material.shader.id = rlGetShaderIdDefault();
        UnloadMaterial(scene->roof_material);
    }
    if (scene->life_material.maps != NULL) {
        scene->life_material.shader.id = rlGetShaderIdDefault();
        UnloadMaterial(scene->life_material);
    }
    if (scene->shadow_material.maps != NULL) {
        scene->shadow_material.shader.id = rlGetShaderIdDefault();
        UnloadMaterial(scene->shadow_material);
    }
    if (scene->water_material.maps != NULL) {
        scene->water_material.shader.id = rlGetShaderIdDefault();
        UnloadMaterial(scene->water_material);
    }
    if (scene->sky_texture.id != 0)
        UnloadTexture(scene->sky_texture);
    if (scene->object_wakes.id != 0)
        UnloadTexture(scene->object_wakes);
    if (scene->sky_shader.id != 0 && scene->sky_shader.id != rlGetShaderIdDefault())
        UnloadShader(scene->sky_shader);
    if (scene->roof_texture.id != 0)
        UnloadTexture(scene->roof_texture);
    if (scene->shadow_map.id != 0)
        rlUnloadFramebuffer(scene->shadow_map.id);
    if (scene->reflection.id != 0)
        UnloadRenderTexture(scene->reflection);
    if (scene->shader.id != 0 && scene->shader.id != rlGetShaderIdDefault())
        UnloadShader(scene->shader);
    if (scene->shadow_shader.id != 0 && scene->shadow_shader.id != rlGetShaderIdDefault())
        UnloadShader(scene->shadow_shader);
    if (scene->water_shader.id != 0 && scene->water_shader.id != rlGetShaderIdDefault())
        UnloadShader(scene->water_shader);
    *scene = (WaterfrontGpuScene) { 0 };
}
