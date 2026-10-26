/* A2.5: same-view scaling. The river is copied along its long axis into synthetic corridors of up
 * to about 25 km, with the original first so the local view never changes. At that view the
 * collision queries, tile lists, water patches and visible water surface must equal the river's,
 * and the candidates each visits must not grow with the number of distant copies.
 * `--report` (or PROTEUS_SCALING_REPORT=1) also prints per-size build cost, memory per owner and
 * per-operation times. Times are a report, never a gate: only counts and equality are asserted. */
#include "boat_map.h"
#include "boat_map_tiles.h"
#include "boat_water_mesh.h"
#include "waterfront_city.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define TILE 1024.0f
enum { SIZES = 6, SAMPLES = 4000, PATCHES = 3 };
static const int copies[SIZES] = { 1, 2, 4, 0, 8, 16 }; // 0: as many as span about 25 km.
static const Vector2 patch_centers[PATCHES] = { { 0, 0 }, { 40, 120 }, { -35, -260 } };

typedef struct {
    Vector3 *vertices;
    Triangle3D *triangles;
    Mesh3D mesh;
    BoatMapDescription description;
    int count;
} Corridor;

typedef struct {
    int copies;
    size_t water, triangles, tiles;
    double map_ms, tiles_ms, index_ms, query_us, patch_ms, surface_ms;
    size_t map_bytes, tile_bytes, land_bytes, cache_bytes, rest_bytes;
    size_t cells, candidates, edges, patch_sources, surface_sources, surface_triangles;
    size_t local_tiles, local_land;
} Row;

// What the first (river) size answered, which every later size must repeat.
typedef struct {
    unsigned char wet[SAMPLES];
    Vector2 normal[SAMPLES];
    size_t patch_sources[PATCHES], local_tiles, local_land;
    BoatWaterMeshSurface patch[PATCHES], surface;
} Baseline;

static bool long_axis_is_z;
static float stride, axis_min;
static Vector2 samples[SAMPLES];

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

// The stride is a whole number of tiles so every copy has the river's tile grid.
static void measure(void)
{
    const Mesh3D *river = waterfront_city_river_map.mesh;
    Vector2 lo = { INFINITY, INFINITY }, hi = { -INFINITY, -INFINITY };
    for (size_t i = 0; i < river->triangle_count; ++i)
        for (int j = 0; j < 3; ++j) {
            Vector3 v = river->vertices[river->triangles[i].vertices[j]];
            lo = (Vector2) { fminf(lo.x, v.x), fminf(lo.y, v.z) };
            hi = (Vector2) { fmaxf(hi.x, v.x), fmaxf(hi.y, v.z) };
        }
    long_axis_is_z = hi.y - lo.y >= hi.x - lo.x;
    axis_min = long_axis_is_z ? lo.y : lo.x;
    float extent = long_axis_is_z ? hi.y - lo.y : hi.x - lo.x;
    stride = ceilf((extent + 2 * TILE) / TILE) * TILE;
}

// COUNT translated copies, ordered as a description requires: every copy's water, then every
// copy's land, then every copy's bridge.
static bool corridor_init(Corridor *c, int count)
{
    const Mesh3D *river = waterfront_city_river_map.mesh;
    size_t water = boat_map_leading_water(river);
    size_t bridge = waterfront_city_river_map.bridge_first_triangle;
    size_t vertices = 0;
    for (size_t i = 0; i < river->triangle_count; ++i)
        for (int j = 0; j < 3; ++j)
            if (river->triangles[i].vertices[j] + 1 > vertices)
                vertices = river->triangles[i].vertices[j] + 1;
    memset(c, 0, sizeof(*c));
    c->count = count;
    c->vertices = malloc(vertices * count * sizeof(*c->vertices));
    c->triangles = malloc(river->triangle_count * count * sizeof(*c->triangles));
    if (!c->vertices || !c->triangles)
        return false;
    for (int k = 0; k < count; ++k)
        for (size_t i = 0; i < vertices; ++i) {
            Vector3 v = river->vertices[i];
            (long_axis_is_z ? &v.z : &v.x)[0] += k * stride;
            c->vertices[k * vertices + i] = v;
        }
    size_t out = 0;
    const size_t ranges[4] = { 0, water, bridge, river->triangle_count };
    for (int part = 0; part < 3; ++part)
        for (int k = 0; k < count; ++k)
            for (size_t i = ranges[part]; i < ranges[part + 1]; ++i) {
                Triangle3D t = river->triangles[i];
                for (int j = 0; j < 3; ++j)
                    t.vertices[j] += (unsigned int)(k * vertices);
                c->triangles[out++] = t;
            }
    c->mesh = (Mesh3D) { c->vertices, c->triangles, out };
    c->description = (BoatMapDescription) { &c->mesh, count * bridge };
    return true;
}

static void corridor_destroy(Corridor *c)
{
    free(c->vertices);
    free(c->triangles);
    memset(c, 0, sizeof(*c));
}

static bool same_mesh(const Mesh3D *a, const Mesh3D *b)
{
    if (!a || !b || a->triangle_count != b->triangle_count)
        return false;
    for (size_t i = 0; i < a->triangle_count; ++i) {
        if (memcmp(&a->triangles[i].color, &b->triangles[i].color, sizeof(Color)))
            return false;
        for (int k = 0; k < 3; ++k)
            if (memcmp(&a->vertices[a->triangles[i].vertices[k]],
                    &b->vertices[b->triangles[i].vertices[k]], sizeof(Vector3)))
                return false;
    }
    return true;
}

// Keeps a flat copy of M, which the cache may overwrite on its next call.
static bool remember(BoatWaterMeshSurface *dst, const Mesh3D *m)
{
    dst->vertices = malloc(m->triangle_count * 3 * sizeof(Vector3) + 1);
    dst->triangles = malloc(m->triangle_count * sizeof(Triangle3D) + 1);
    if (!dst->vertices || !dst->triangles)
        return false;
    for (size_t i = 0; i < m->triangle_count; ++i) {
        dst->triangles[i] = m->triangles[i];
        for (int k = 0; k < 3; ++k) {
            dst->vertices[i * 3 + k] = m->vertices[m->triangles[i].vertices[k]];
            dst->triangles[i].vertices[k] = (unsigned int)(i * 3 + k);
        }
    }
    dst->mesh = (Mesh3D) { dst->vertices, dst->triangles, m->triangle_count };
    return true;
}

typedef struct {
    Vector2 min, max;
} Window;

static bool window_visible(void *context, Vector3 min, Vector3 max)
{
    const Window *w = context;
    return max.x >= w->min.x && min.x <= w->max.x && max.z >= w->min.y && min.z <= w->max.y;
}

static size_t surface_bytes(const BoatWaterMeshSurface *s)
{
    return s->capacity * (3 * sizeof(Vector3) + sizeof(Triangle3D));
}

static size_t water_cache_bytes(const BoatWaterMeshCache *c)
{
    const BoatWaterIndex *i = &c->index;
    return ((size_t)i->columns * i->rows + 1) * sizeof(unsigned int)
        + (i->entries + i->count + i->gathered_capacity + 2 * i->count) * sizeof(unsigned int)
        + surface_bytes(&c->rest) + surface_bytes(&c->surface) + surface_bytes(&c->patches[0])
        + surface_bytes(&c->patches[1]);
}

// Points in and around the river's water, in simulation X/Z (map east is reflected). Odd points
// are moved up to 40 m so that shores and land are covered.
static void make_samples(void)
{
    const Mesh3D *river = waterfront_city_river_map.mesh;
    size_t water = boat_map_leading_water(river);
    unsigned int seed = 12345;
    for (int i = 0; i < SAMPLES; ++i) {
        float r[5];
        for (int k = 0; k < 5; ++k) {
            seed = seed * 1664525u + 1013904223u;
            r[k] = (seed >> 8) / 16777216.0f;
        }
        const Triangle3D *t = &river->triangles[(size_t)(r[0] * (float)water)];
        Vector3 a = river->vertices[t->vertices[0]], b = river->vertices[t->vertices[1]],
                c = river->vertices[t->vertices[2]];
        float u = r[1], v = r[2] * (1 - u);
        Vector3 p = { a.x + (b.x - a.x) * u + (c.x - a.x) * v, 0,
            a.z + (b.z - a.z) * u + (c.z - a.z) * v };
        if (i & 1) {
            p.x += (r[3] - .5f) * 80;
            p.z += (r[4] - .5f) * 80;
        }
        samples[i] = (Vector2) { -p.x, p.z };
    }
}

// Measures one corridor and asserts every local result equals the baseline's (recording it when
// FIRST).
static int measure_size(const Corridor *c, Row *row, Baseline *base, bool first)
{
    const BoatMapDescription *d = &c->description;
    memset(row, 0, sizeof(*row));
    row->copies = c->count;
    row->water = boat_map_leading_water(d->mesh);
    row->triangles = d->mesh->triangle_count;

    BoatMap map;
    double t = now();
    CHECK(boat_map_init(&map, d));
    row->map_ms = now() - t;
    row->map_bytes = boat_map_bytes(&map);
    BoatMapTiles tiles, land;
    t = now();
    CHECK(boat_map_tiles_init(&tiles, &map, TILE));
    row->tiles_ms = now() - t;
    row->tile_bytes = boat_map_tiles_bytes(&tiles);
    CHECK(boat_map_tiles_init_land(&land, d, TILE));
    row->land_bytes = boat_map_tiles_bytes(&land);
    row->tiles = boat_map_tiles_count(&tiles);

    // Collision: indexed answers equal the river's, and every 40th equals the exhaustive scan.
    size_t wet = 0;
    for (int i = 0; i < SAMPLES; ++i) {
        Vector2 normal = { 0, 0 }, expected = { 0, 0 };
        BoatMapQueryCost cost = { 0 };
        bool inside = boat_map_water_contact_cost(&map, samples[i], 3, &normal, &cost);
        row->cells += cost.cells;
        row->candidates += cost.triangles;
        row->edges += cost.edges;
        wet += inside;
        if (first) {
            base->wet[i] = inside;
            base->normal[i] = normal;
        } else {
            CHECK(base->wet[i] == inside);
            CHECK(!memcmp(&base->normal[i], &normal, sizeof(normal)));
        }
        if (i % 40 == 0) {
            bool ref = boat_map_water_contact_reference(&map, samples[i], 3, &expected);
            CHECK(ref == inside && !memcmp(&expected, &normal, sizeof(normal)));
        }
    }
    CHECK(wet > SAMPLES / 4 && wet < SAMPLES);
    t = now();
    for (int rep = 0; rep < 8; ++rep)
        for (int i = 0; i < SAMPLES; ++i)
            wet += boat_map_water(&map, samples[i], 3);
    row->query_us = (now() - t) * 1000 / (8.0 * SAMPLES);

    // Tiles within the first copy's extent list what the river's tiles list there.
    for (size_t i = 0; i < boat_map_tiles_count(&tiles); ++i) {
        const BoatMapTile *tile = &tiles.tiles[i];
        if ((long_axis_is_z ? tile->max.y : tile->max.x) > axis_min + stride - TILE)
            continue;
        row->local_tiles += tile->water_count + tile->shore_count;
        row->local_land += tile->land_count;
    }
    if (first) {
        base->local_tiles = row->local_tiles;
        base->local_land = row->local_land;
    } else {
        CHECK(row->local_tiles == base->local_tiles);
        CHECK(row->local_land == base->local_land);
    }

    // Patches at the local view clip the same triangles and make the same mesh.
    BoatWaterMeshCache cache = { 0 };
    t = now();
    CHECK(boat_water_patch(&cache, d, (Vector2) { 500, 500 }));
    row->index_ms = now() - t; // First call after a bind: index and rest tessellation.
    for (int p = 0; p < PATCHES; ++p) {
        const Mesh3D *patch = boat_water_patch(&cache, d, patch_centers[p]);
        CHECK(patch);
        row->patch_sources += cache.patch_sources[0];
        if (first) {
            CHECK(remember(&base->patch[p], patch));
            base->patch_sources[p] = cache.patch_sources[0];
        } else {
            CHECK(same_mesh(patch, &base->patch[p].mesh));
            CHECK(cache.patch_sources[0] == base->patch_sources[p]);
        }
    }
    t = now();
    for (int rep = 0; rep < 20; ++rep)
        CHECK(boat_water_patch(&cache, d, (Vector2) { rep & 1 ? 700 : 500, 0 }));
    row->patch_ms = (now() - t) / 20;

    // The CPU surface in a local window displaces the same triangles at any corridor length.
    Window window = { { -200, -300 }, { 200, 100 } };
    BoatWater water = boat_water_gentle();
    const Mesh3D *surface
        = boat_water_surface_visible(&cache, d, water, .7, window_visible, &window);
    CHECK(surface && surface->triangle_count > 0);
    row->surface_triangles = surface->triangle_count;
    row->surface_sources = cache.surface_sources;
    if (first)
        CHECK(remember(&base->surface, surface));
    else
        CHECK(same_mesh(surface, &base->surface.mesh));
    t = now();
    for (int rep = 0; rep < 4; ++rep)
        CHECK(boat_water_surface_visible(&cache, d, water, .7 + rep, window_visible, &window));
    row->surface_ms = (now() - t) / 4;
    row->cache_bytes = water_cache_bytes(&cache);
    row->rest_bytes = surface_bytes(&cache.rest) + 2 * cache.index.count * sizeof(unsigned int);

    // Rebinding a used cache to the river and back gives the same local patch.
    if (!first) {
        const BoatMapDescription *river = &waterfront_city_river_map;
        CHECK(same_mesh(boat_water_patch(&cache, river, patch_centers[1]), &base->patch[1].mesh));
        CHECK(same_mesh(boat_water_patch(&cache, d, patch_centers[1]), &base->patch[1].mesh));
    }
    boat_water_mesh_cache_destroy(&cache);
    boat_map_tiles_destroy(&land);
    boat_map_tiles_destroy(&tiles);
    boat_map_destroy(&map);
    return 0;
}

static void print_report(const Row *rows, int n)
{
    printf("stride %.0f m along %s; the local view is the first copy\n", stride,
        long_axis_is_z ? "Z" : "X");
    printf("%6s %9s %9s %6s | %8s %8s %8s | %8s %8s %8s %7s\n", "copies", "water", "tris", "tiles",
        "map ms", "tile ms", "idx ms", "query us", "patch ms", "surf ms", "cand/q");
    for (int s = 0; s < n; ++s)
        printf("%6d %9zu %9zu %6zu | %8.2f %8.2f %8.2f | %8.3f %8.3f %8.3f %7.2f\n", rows[s].copies,
            rows[s].water, rows[s].triangles, rows[s].tiles, rows[s].map_ms, rows[s].tiles_ms,
            rows[s].index_ms, rows[s].query_us, rows[s].patch_ms, rows[s].surface_ms,
            (double)rows[s].candidates / SAMPLES);
    printf("%6s %12s %12s %12s %12s %12s\n", "copies", "map bytes", "tile bytes", "land bytes",
        "water cache", "of it rest");
    for (int s = 0; s < n; ++s)
        printf("%6d %12zu %12zu %12zu %12zu %12zu\n", rows[s].copies, rows[s].map_bytes,
            rows[s].tile_bytes, rows[s].land_bytes, rows[s].cache_bytes, rows[s].rest_bytes);
}

int main(int argc, char **argv)
{
    bool report = (argc > 1 && !strcmp(argv[1], "--report")) || getenv("PROTEUS_SCALING_REPORT");
    measure();
    make_samples();
    Row rows[SIZES];
    static Baseline base;
    int n = 0;
    for (int s = 0; s < SIZES; ++s) {
        int count = copies[s] ? copies[s] : (int)ceilf(25000.0f / stride);
        if (n && count <= rows[n - 1].copies)
            break;
        Corridor c;
        CHECK(corridor_init(&c, count));
        int failed = measure_size(&c, &rows[n], &base, n == 0);
        corridor_destroy(&c);
        if (failed)
            return 1;
        ++n;
    }
    // Per-query and per-rebuild work is exactly the river's; only build cost and memory scale.
    for (int s = 1; s < n; ++s) {
        CHECK(rows[s].candidates == rows[0].candidates); // Cells differ only off the river map.
        CHECK(rows[s].edges == rows[0].edges && rows[s].patch_sources == rows[0].patch_sources);
        CHECK(rows[s].surface_triangles == rows[0].surface_triangles);
        CHECK(rows[s].surface_sources == rows[0].surface_sources);
        CHECK(rows[s].cache_bytes < rows[0].cache_bytes + rows[0].cache_bytes / 20); // Lazy rest.
    }
    if (report)
        print_report(rows, n);
    printf("boat_map_scaling=ok\n");
    return 0;
}
