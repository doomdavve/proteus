#include "boat_map.h"
#include "raymath.h"
#include "waterfront_city.h"

#include <stdlib.h>

typedef struct {
    unsigned int a, b;
} Edge;
static Edge *shore;
static size_t shore_count, water_count;
static const Mesh3D *cached_mesh;

static int compare_edge(const void *left, const void *right)
{
    const Edge *a = left, *b = right;
    if (a->a != b->a)
        return a->a < b->a ? -1 : 1;
    return a->b < b->b ? -1 : a->b > b->b;
}

static bool init(void)
{
    const Mesh3D *mesh = waterfront_city_mesh();
    if (shore && cached_mesh == mesh)
        return true;
    boat_map_destroy();
    cached_mesh = mesh;
    // The importer emits the triangulated water surface first, followed by raised land.
    water_count = 0;
    while (water_count < mesh->triangle_count) {
        const Triangle3D *t = &mesh->triangles[water_count];
        if (mesh->vertices[t->vertices[0]].y != 0 || mesh->vertices[t->vertices[1]].y != 0
            || mesh->vertices[t->vertices[2]].y != 0)
            break;
        ++water_count;
    }
    if (!water_count)
        return false;
    shore = malloc(water_count * 3 * sizeof(*shore));
    if (!shore)
        return false;
    size_t count = 0;
    for (size_t i = 0; i < water_count; ++i) {
        for (int j = 0; j < 3; ++j) {
            unsigned int a = mesh->triangles[i].vertices[j];
            unsigned int b = mesh->triangles[i].vertices[(j + 1) % 3];
            shore[count++] = a < b ? (Edge) { a, b } : (Edge) { b, a };
        }
    }
    qsort(shore, count, sizeof(*shore), compare_edge);
    shore_count = 0;
    for (size_t i = 0; i < count;) {
        size_t end = i + 1;
        while (end < count && compare_edge(&shore[i], &shore[end]) == 0)
            ++end;
        // Shared triangle edges are internal water; single edges bound shore or map crop.
        if (end == i + 1)
            shore[shore_count++] = shore[i];
        i = end;
    }
    return true;
}

static Vector2 point(Vector3 p) { return (Vector2) { p.x, p.z }; }
static float cross(Vector2 a, Vector2 b, Vector2 p)
{
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

bool boat_map_water_contact(Vector2 position, float clearance, Vector2 *water_normal)
{
    if (!init())
        return false;
    Vector2 p = { -position.x, position.y };
    const Mesh3D *mesh = waterfront_city_mesh();
    bool inside = false;
    for (size_t i = 0; i < water_count; ++i) {
        const Triangle3D *t = &mesh->triangles[i];
        Vector2 a = point(mesh->vertices[t->vertices[0]]);
        Vector2 b = point(mesh->vertices[t->vertices[1]]);
        Vector2 c = point(mesh->vertices[t->vertices[2]]);
        float x = cross(a, b, p), y = cross(b, c, p), z = cross(c, a, p);
        if ((x >= 0 && y >= 0 && z >= 0) || (x <= 0 && y <= 0 && z <= 0)) {
            inside = true;
            break;
        }
    }
    Vector2 nearest = { 0 };
    float nearest_distance = INFINITY;
    for (size_t i = 0; i < shore_count; ++i) {
        Vector2 a = point(mesh->vertices[shore[i].a]), b = point(mesh->vertices[shore[i].b]);
        Vector2 ab = Vector2Subtract(b, a);
        float length = Vector2LengthSqr(ab);
        float t
            = length > 0 ? Clamp(Vector2DotProduct(Vector2Subtract(p, a), ab) / length, 0, 1) : 0;
        Vector2 edge_point = Vector2Add(a, Vector2Scale(ab, t));
        float distance = Vector2DistanceSqr(p, edge_point);
        if (distance < nearest_distance) {
            nearest_distance = distance;
            nearest = Vector2Subtract(p, edge_point);
        }
    }
    if (inside && nearest_distance >= clearance * clearance)
        return true;
    if (water_normal) {
        if (Vector2LengthSqr(nearest) > .000001f)
            nearest = Vector2Normalize(nearest);
        else
            nearest = (Vector2) { 0, 1 };
        // Convert the map's east coordinate back to simulation X.
        *water_normal = (Vector2) { -nearest.x, nearest.y };
    }
    return false;
}

bool boat_map_water(Vector2 position, float clearance)
{
    return boat_map_water_contact(position, clearance, NULL);
}

void boat_map_destroy(void)
{
    free(shore);
    shore = NULL;
    shore_count = water_count = 0;
}
