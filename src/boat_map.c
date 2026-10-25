#include "boat_map.h"
#include "raymath.h"

#include <stdlib.h>

// Registration margin around each cell. It exceeds any float rounding in the containment and
// cell-coordinate arithmetic by orders of magnitude, so no candidate can be missed.
#define GRID_MARGIN BOAT_MAP_MARGIN
// Distance slack when proving that unvisited edges cannot be nearer than the best found.
#define SEARCH_SLACK .05f

static int compare_edge(const void *left, const void *right)
{
    const BoatMapEdge *a = left, *b = right;
    if (a->a != b->a)
        return a->a < b->a ? -1 : 1;
    return a->b < b->b ? -1 : a->b > b->b;
}

static Vector2 point(Vector3 p) { return (Vector2) { p.x, p.z }; }
static float cross(Vector2 a, Vector2 b, Vector2 p)
{
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

bool boat_map_polygon_near_rect(const Vector2 *p, int count, Vector2 lo, Vector2 hi, float margin)
{
    Vector2 min = p[0], max = p[0];
    for (int i = 1; i < count; ++i) {
        min = Vector2Min(min, p[i]);
        max = Vector2Max(max, p[i]);
    }
    if (min.x > hi.x + margin || max.x < lo.x - margin || min.y > hi.y + margin
        || max.y < lo.y - margin)
        return false;
    Vector2 center = Vector2Scale(Vector2Add(lo, hi), .5f);
    Vector2 half = Vector2Scale(Vector2Subtract(hi, lo), .5f);
    for (int i = 0; i < (count == 2 ? 1 : count); ++i) {
        Vector2 edge = Vector2Subtract(p[(i + 1) % count], p[i]);
        float length = Vector2Length(edge);
        if (!(length > 0))
            continue;
        Vector2 n = { -edge.y / length, edge.x / length };
        float low = INFINITY, high = -INFINITY;
        for (int j = 0; j < count; ++j) {
            float d = Vector2DotProduct(n, p[j]);
            low = fminf(low, d);
            high = fmaxf(high, d);
        }
        float c = Vector2DotProduct(n, center);
        float r = half.x * fabsf(n.x) + half.y * fabsf(n.y) + margin;
        if (low > c + r || high < c - r)
            return false;
    }
    return true;
}

static void cell_rect(const BoatMapGrid *grid, int column, int row, Vector2 *lo, Vector2 *hi)
{
    *lo = (Vector2) { grid->origin_x + column * grid->cell_size,
        grid->origin_z + row * grid->cell_size };
    *hi = (Vector2) { lo->x + grid->cell_size, lo->y + grid->cell_size };
}

// Cell column/row range overlapping [LO, HI], clamped to the grid; false when it is outside.
static bool cell_range(
    const BoatMapGrid *grid, Vector2 lo, Vector2 hi, int *x0, int *z0, int *x1, int *z1)
{
    float fx0 = floorf((lo.x - grid->origin_x) / grid->cell_size);
    float fz0 = floorf((lo.y - grid->origin_z) / grid->cell_size);
    float fx1 = floorf((hi.x - grid->origin_x) / grid->cell_size);
    float fz1 = floorf((hi.y - grid->origin_z) / grid->cell_size);
    if (!(fx1 >= 0 && fz1 >= 0 && fx0 < grid->columns && fz0 < grid->rows))
        return false;
    *x0 = fx0 < 0 ? 0 : (int)fx0;
    *z0 = fz0 < 0 ? 0 : (int)fz0;
    *x1 = fx1 >= grid->columns ? grid->columns - 1 : (int)fx1;
    *z1 = fz1 >= grid->rows ? grid->rows - 1 : (int)fz1;
    return true;
}

typedef struct {
    Vector2 p[3];
    int count;
} Shape;

static Shape water_shape(const BoatMap *map, size_t index)
{
    const Mesh3D *mesh = map->description->mesh;
    const Triangle3D *t = &mesh->triangles[index];
    return (Shape) { { point(mesh->vertices[t->vertices[0]]), point(mesh->vertices[t->vertices[1]]),
                         point(mesh->vertices[t->vertices[2]]) },
        3 };
}

static Shape shore_shape(const BoatMap *map, size_t index)
{
    const Mesh3D *mesh = map->description->mesh;
    return (Shape) { { point(mesh->vertices[map->shore[index].a]),
                         point(mesh->vertices[map->shore[index].b]) },
        2 };
}

// Lists every item near each cell: counts when LIST is NULL, otherwise fills it using and
// advancing the per-cell CURSOR offsets.
static void register_items(const BoatMap *map, const BoatMapGrid *grid, size_t count,
    Shape (*shape)(const BoatMap *, size_t), unsigned int *cursor, unsigned int *list)
{
    for (size_t i = 0; i < count; ++i) {
        Shape s = shape(map, i);
        Vector2 lo = s.p[0], hi = s.p[0];
        for (int j = 1; j < s.count; ++j) {
            lo = Vector2Min(lo, s.p[j]);
            hi = Vector2Max(hi, s.p[j]);
        }
        Vector2 m = { GRID_MARGIN, GRID_MARGIN };
        int x0, z0, x1, z1;
        if (!cell_range(grid, Vector2Subtract(lo, m), Vector2Add(hi, m), &x0, &z0, &x1, &z1))
            continue;
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                Vector2 cell_lo, cell_hi;
                cell_rect(grid, x, z, &cell_lo, &cell_hi);
                if (!boat_map_polygon_near_rect(s.p, s.count, cell_lo, cell_hi, GRID_MARGIN))
                    continue;
                size_t cell = (size_t)z * (size_t)grid->columns + (size_t)x;
                if (list)
                    list[cursor[cell]++] = (unsigned int)i;
                else
                    ++cursor[cell];
            }
    }
}

static bool build_list(const BoatMap *map, BoatMapGrid *grid, size_t count,
    Shape (*shape)(const BoatMap *, size_t), unsigned int **start, unsigned int **list)
{
    size_t cells = (size_t)grid->columns * (size_t)grid->rows;
    *start = calloc(cells + 1, sizeof(**start));
    if (!*start)
        return false;
    register_items(map, grid, count, shape, *start + 1, NULL);
    for (size_t i = 0; i < cells; ++i)
        (*start)[i + 1] += (*start)[i];
    *list = malloc(((size_t)(*start)[cells] + 1) * sizeof(**list));
    unsigned int *cursor = malloc(cells * sizeof(*cursor));
    if (!*list || !cursor) {
        free(cursor);
        return false;
    }
    for (size_t i = 0; i < cells; ++i)
        cursor[i] = (*start)[i];
    register_items(map, grid, count, shape, cursor, *list);
    free(cursor);
    return true;
}

static void grid_destroy(BoatMapGrid *grid)
{
    free(grid->water_start);
    free(grid->shore_start);
    free(grid->water);
    free(grid->shore);
    *grid = (BoatMapGrid) { 0 };
}

static bool build_grid(BoatMap *map, float cell_size)
{
    Vector2 lo = { INFINITY, INFINITY }, hi = { -INFINITY, -INFINITY };
    for (size_t i = 0; i < map->water_count; ++i) {
        Shape s = water_shape(map, i);
        for (int j = 0; j < 3; ++j) {
            lo = Vector2Min(lo, s.p[j]);
            hi = Vector2Max(hi, s.p[j]);
        }
    }
    // Cells align to multiples of the cell size, so they nest inside zero-origin tiles. Shore
    // edges are water-triangle edges, so this rectangle covers them too.
    BoatMapGrid *grid = &map->grid;
    grid->cell_size = cell_size;
    grid->origin_x = floorf((lo.x - GRID_MARGIN) / cell_size) * cell_size;
    grid->origin_z = floorf((lo.y - GRID_MARGIN) / cell_size) * cell_size;
    grid->columns = (int)ceilf((hi.x + GRID_MARGIN - grid->origin_x) / cell_size);
    grid->rows = (int)ceilf((hi.y + GRID_MARGIN - grid->origin_z) / cell_size);
    if (grid->columns <= 0 || grid->rows <= 0)
        return false;
    return build_list(map, grid, map->water_count, water_shape, &grid->water_start, &grid->water)
        && build_list(map, grid, map->shore_count, shore_shape, &grid->shore_start, &grid->shore);
}

bool boat_map_init_cell(BoatMap *map, const BoatMapDescription *description, float cell_size)
{
    *map = (BoatMap) { 0 };
    if (!description || !description->mesh || !(cell_size > 0))
        return false;
    const Mesh3D *mesh = description->mesh;
    // The importer emits the triangulated water surface first, followed by raised land.
    size_t water_count = 0;
    while (water_count < mesh->triangle_count) {
        const Triangle3D *t = &mesh->triangles[water_count];
        if (mesh->vertices[t->vertices[0]].y != 0 || mesh->vertices[t->vertices[1]].y != 0
            || mesh->vertices[t->vertices[2]].y != 0)
            break;
        ++water_count;
    }
    if (!water_count)
        return false;
    BoatMapEdge *shore = malloc(water_count * 3 * sizeof(*shore));
    if (!shore)
        return false;
    size_t count = 0;
    for (size_t i = 0; i < water_count; ++i) {
        for (int j = 0; j < 3; ++j) {
            unsigned int a = mesh->triangles[i].vertices[j];
            unsigned int b = mesh->triangles[i].vertices[(j + 1) % 3];
            shore[count++] = a < b ? (BoatMapEdge) { a, b } : (BoatMapEdge) { b, a };
        }
    }
    qsort(shore, count, sizeof(*shore), compare_edge);
    size_t shore_count = 0;
    for (size_t i = 0; i < count;) {
        size_t end = i + 1;
        while (end < count && compare_edge(&shore[i], &shore[end]) == 0)
            ++end;
        // Shared triangle edges are internal water; single edges bound shore or map crop.
        if (end == i + 1)
            shore[shore_count++] = shore[i];
        i = end;
    }
    *map = (BoatMap) { description, shore, shore_count, water_count, { 0 } };
    if (!build_grid(map, cell_size)) {
        boat_map_destroy(map);
        return false;
    }
    return true;
}

bool boat_map_init(BoatMap *map, const BoatMapDescription *description)
{
    return boat_map_init_cell(map, description, BOAT_MAP_CELL_SIZE);
}

void boat_map_destroy(BoatMap *map)
{
    free(map->shore);
    grid_destroy(&map->grid);
    *map = (BoatMap) { 0 };
}

bool boat_map_ready(const BoatMap *map)
{
    return map && map->description && map->shore && map->grid.water_start;
}

bool boat_map_is_bridge_triangle(const BoatMapDescription *description, size_t index)
{
    return index >= description->bridge_first_triangle;
}

size_t boat_map_bytes(const BoatMap *map)
{
    const BoatMapGrid *grid = &map->grid;
    if (!grid->water_start)
        return map->shore ? map->water_count * 3 * sizeof(*map->shore) : 0;
    size_t cells = (size_t)grid->columns * (size_t)grid->rows;
    return map->water_count * 3 * sizeof(*map->shore) + 2 * (cells + 1) * sizeof(*grid->water_start)
        + ((size_t)grid->water_start[cells] + 1 + (size_t)grid->shore_start[cells] + 1)
        * sizeof(*grid->water);
}

static bool contains(const BoatMap *map, size_t index, Vector2 p)
{
    Shape s = water_shape(map, index);
    float x = cross(s.p[0], s.p[1], p), y = cross(s.p[1], s.p[2], p), z = cross(s.p[2], s.p[0], p);
    return (x >= 0 && y >= 0 && z >= 0) || (x <= 0 && y <= 0 && z <= 0);
}

// Squared distance from P to shore edge INDEX and the offset from the edge point to P.
static float edge_distance(const BoatMap *map, size_t index, Vector2 p, Vector2 *offset)
{
    Shape s = shore_shape(map, index);
    Vector2 a = s.p[0], b = s.p[1];
    Vector2 ab = Vector2Subtract(b, a);
    float length = Vector2LengthSqr(ab);
    float t = length > 0 ? Clamp(Vector2DotProduct(Vector2Subtract(p, a), ab) / length, 0, 1) : 0;
    Vector2 edge_point = Vector2Add(a, Vector2Scale(ab, t));
    *offset = Vector2Subtract(p, edge_point);
    return Vector2DistanceSqr(p, edge_point);
}

static bool finish(
    bool inside, float nearest_distance, Vector2 nearest, float clearance, Vector2 *water_normal)
{
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

bool boat_map_water_contact_reference(
    const BoatMap *map, Vector2 position, float clearance, Vector2 *water_normal)
{
    if (!boat_map_ready(map))
        return false;
    Vector2 p = { -position.x, position.y };
    bool inside = false;
    for (size_t i = 0; i < map->water_count && !inside; ++i)
        inside = contains(map, i, p);
    Vector2 nearest = { 0 };
    float nearest_distance = INFINITY;
    for (size_t i = 0; i < map->shore_count; ++i) {
        Vector2 offset;
        float distance = edge_distance(map, i, p, &offset);
        if (distance < nearest_distance) {
            nearest_distance = distance;
            nearest = offset;
        }
    }
    return finish(inside, nearest_distance, nearest, clearance, water_normal);
}

typedef struct {
    float distance;
    unsigned int index;
    Vector2 offset;
} Nearest;

// Folds the shore edges of cells [X0..X1] x [Z0..Z1] into BEST, keeping the first edge in shore
// order among equal distances, as the exhaustive scan does.
static void visit_edges(const BoatMap *map, int x0, int z0, int x1, int z1, Nearest *best,
    BoatMapQueryCost *cost, Vector2 p)
{
    const BoatMapGrid *grid = &map->grid;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            size_t cell = (size_t)z * (size_t)grid->columns + (size_t)x;
            if (cost)
                ++cost->cells;
            for (unsigned int k = grid->shore_start[cell]; k < grid->shore_start[cell + 1]; ++k) {
                unsigned int index = grid->shore[k];
                Vector2 offset;
                float distance = edge_distance(map, index, p, &offset);
                if (cost)
                    ++cost->edges;
                if (distance < best->distance
                    || (distance == best->distance && index < best->index))
                    *best = (Nearest) { distance, index, offset };
            }
        }
}

// Nearest shore edge to P: grow a square window around P until every edge outside it is
// provably farther than the best inside. Edges lie inside the grid, so a window covering the
// grid has visited all of them.
static Nearest nearest_edge(const BoatMap *map, Vector2 p, BoatMapQueryCost *cost)
{
    const BoatMapGrid *grid = &map->grid;
    Nearest best = { INFINITY, (unsigned int)-1, { 0 } };
    Vector2 grid_lo = { grid->origin_x, grid->origin_z };
    Vector2 grid_hi = { grid->origin_x + grid->columns * grid->cell_size,
        grid->origin_z + grid->rows * grid->cell_size };
    // Chebyshev distance from P to the grid: windows smaller than this hold no cells.
    float outside = fmaxf(
        fmaxf(grid_lo.x - p.x, p.x - grid_hi.x), fmaxf(fmaxf(grid_lo.y - p.y, p.y - grid_hi.y), 0));
    int vx0 = 0, vz0 = 0, vx1 = -1, vz1 = -1; // Visited cell block; empty at first.
    for (float radius = outside + grid->cell_size;; radius += grid->cell_size) {
        Vector2 r = { radius, radius };
        int x0, z0, x1, z1;
        if (cell_range(grid, Vector2Subtract(p, r), Vector2Add(p, r), &x0, &z0, &x1, &z1)) {
            if (vx1 < vx0) {
                visit_edges(map, x0, z0, x1, z1, &best, cost, p);
            } else {
                // Only the cells new to the grown block: full rows above and below the old
                // block, then the columns either side of it.
                if (z0 < vz0)
                    visit_edges(map, x0, z0, x1, vz0 - 1, &best, cost, p);
                if (z1 > vz1)
                    visit_edges(map, x0, vz1 + 1, x1, z1, &best, cost, p);
                if (x0 < vx0)
                    visit_edges(map, x0, vz0, vx0 - 1, vz1, &best, cost, p);
                if (x1 > vx1)
                    visit_edges(map, vx1 + 1, vz0, x1, vz1, &best, cost, p);
            }
            vx0 = x0, vz0 = z0, vx1 = x1, vz1 = z1;
        }
        bool all = vx0 == 0 && vz0 == 0 && vx1 == grid->columns - 1 && vz1 == grid->rows - 1;
        // Every unvisited edge lies outside the window, at least RADIUS from P.
        float bound = radius - SEARCH_SLACK;
        if (all || (best.distance < INFINITY && best.distance < bound * bound))
            return best;
    }
}

static bool cell_contains(const BoatMap *map, Vector2 p, BoatMapQueryCost *cost)
{
    const BoatMapGrid *grid = &map->grid;
    int x0, z0, x1, z1;
    if (!cell_range(grid, p, p, &x0, &z0, &x1, &z1))
        return false;
    size_t cell = (size_t)z0 * (size_t)grid->columns + (size_t)x0;
    if (cost)
        ++cost->cells;
    for (unsigned int k = grid->water_start[cell]; k < grid->water_start[cell + 1]; ++k) {
        if (cost)
            ++cost->triangles;
        if (contains(map, grid->water[k], p))
            return true;
    }
    return false;
}

// True when every shore edge is at least CLEARANCE from P (the exhaustive comparison).
static bool clear_of_shore(const BoatMap *map, Vector2 p, float clearance, BoatMapQueryCost *cost)
{
    const BoatMapGrid *grid = &map->grid;
    float reach = fabsf(clearance) + SEARCH_SLACK;
    Vector2 r = { reach, reach };
    int x0, z0, x1, z1;
    if (!cell_range(grid, Vector2Subtract(p, r), Vector2Add(p, r), &x0, &z0, &x1, &z1))
        return true;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            size_t cell = (size_t)z * (size_t)grid->columns + (size_t)x;
            if (cost)
                ++cost->cells;
            for (unsigned int k = grid->shore_start[cell]; k < grid->shore_start[cell + 1]; ++k) {
                Vector2 offset;
                if (cost)
                    ++cost->edges;
                if (!(edge_distance(map, grid->shore[k], p, &offset) >= clearance * clearance))
                    return false;
            }
        }
    return true;
}

bool boat_map_water_contact_cost(const BoatMap *map, Vector2 position, float clearance,
    Vector2 *water_normal, BoatMapQueryCost *cost)
{
    if (!boat_map_ready(map))
        return false;
    Vector2 p = { -position.x, position.y };
    // The growing window needs a finite position whose window steps still change its radius;
    // anything else (never a boat position) takes the exhaustive answer directly.
    if (!(fabsf(p.x) < 1e6f && fabsf(p.y) < 1e6f))
        return boat_map_water_contact_reference(map, position, clearance, water_normal);
    bool inside = cell_contains(map, p, cost);
    if (inside && clear_of_shore(map, p, clearance, cost))
        return true;
    if (!water_normal)
        return false;
    Nearest best = nearest_edge(map, p, cost);
    return finish(inside, best.distance, best.offset, clearance, water_normal);
}

bool boat_map_water_contact(
    const BoatMap *map, Vector2 position, float clearance, Vector2 *water_normal)
{
    return boat_map_water_contact_cost(map, position, clearance, water_normal, NULL);
}

bool boat_map_water(const BoatMap *map, Vector2 position, float clearance)
{
    return boat_map_water_contact(map, position, clearance, NULL);
}
