/* A2.2: the in-memory tile catalogue and the indexed collision grid of the current river.
 * Indexed queries must return exactly what the exhaustive scan returns (containment, clearance
 * and normal, bit for bit) on dense, shore, seam, corner, random and far-away points for several
 * cell sizes, and both tile sizes must list every item where it belongs. Reports index, tile
 * and per-query candidate counts. */
#include "boat_location.h"
#include "boat_map.h"
#include "boat_map_tiles.h"
#include "raymath.h"
#include "waterfront_city.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

enum { CELL_SIZES = 3, CLEARANCES = 4 };
static const float cell_sizes[CELL_SIZES] = { 8, BOAT_MAP_CELL_SIZE, 32 };
static const float clearances[CLEARANCES] = { 0, .5f, 3, 12 };
static const float tile_sizes[] = { 512, 1024 };

typedef struct {
    BoatMap maps[CELL_SIZES];
    size_t queries, mismatches;
    // Candidate edges and triangles per default-cell query with a normal at 3 m clearance,
    // for points in open water (at 0 m) and for the rest.
    unsigned int *edges[2], *triangles[2];
    size_t samples[2], capacity[2];
} Parity;

static Parity parity;

// Map east/Z bounds of the whole description (land included).
static void bounds(Vector2 *lo, Vector2 *hi)
{
    const Mesh3D *mesh = waterfront_city_river_map.mesh;
    *lo = (Vector2) { INFINITY, INFINITY };
    *hi = (Vector2) { -INFINITY, -INFINITY };
    for (size_t i = 0; i < mesh->triangle_count; ++i)
        for (int j = 0; j < 3; ++j) {
            Vector3 v = mesh->vertices[mesh->triangles[i].vertices[j]];
            *lo = Vector2Min(*lo, (Vector2) { v.x, v.z });
            *hi = Vector2Max(*hi, (Vector2) { v.x, v.z });
        }
}

// Compares every cell size against the exhaustive answer at map east/Z point P.
static bool compare(Vector2 p)
{
    Vector2 position = { -p.x, p.y };
    const BoatMap *reference = &parity.maps[1];
    bool wet = false;
    for (int c = 0; c < CLEARANCES; ++c) {
        Vector2 expected_normal = { 7, 7 };
        bool expected = boat_map_water_contact_reference(
            reference, position, clearances[c], &expected_normal);
        wet = c == 0 ? expected : wet;
        for (int m = 0; m < CELL_SIZES; ++m) {
            Vector2 normal = { 7, 7 };
            BoatMapQueryCost cost = { 0 };
            bool inside = boat_map_water_contact_cost(
                &parity.maps[m], position, clearances[c], &normal, &cost);
            bool plain = boat_map_water(&parity.maps[m], position, clearances[c]);
            ++parity.queries;
            // The reference's answer does not depend on whether a normal is requested.
            if (inside != expected || plain != expected
                || memcmp(&normal, &expected_normal, sizeof(normal)) != 0) {
                if (parity.mismatches++ < 8)
                    fprintf(stderr,
                        "mismatch cell=%g p=%a,%a clearance=%g: %d/%d normal %a,%a vs %a,%a\n",
                        (double)cell_sizes[m], (double)p.x, (double)p.y, (double)clearances[c],
                        inside, expected, (double)normal.x, (double)normal.y,
                        (double)expected_normal.x, (double)expected_normal.y);
            }
            if (m == 1 && c == 2) {
                int k = !wet;
                if (parity.samples[k] == parity.capacity[k]) {
                    size_t capacity = parity.capacity[k] ? parity.capacity[k] * 2 : 1 << 16;
                    unsigned int *edges = realloc(parity.edges[k], capacity * sizeof(*edges));
                    if (!edges)
                        return false;
                    parity.edges[k] = edges;
                    unsigned int *triangles
                        = realloc(parity.triangles[k], capacity * sizeof(*triangles));
                    if (!triangles)
                        return false;
                    parity.triangles[k] = triangles;
                    parity.capacity[k] = capacity;
                }
                parity.edges[k][parity.samples[k]] = cost.edges;
                parity.triangles[k][parity.samples[k]++] = cost.triangles;
            }
        }
    }
    return true;
}

// P and its float neighbours either side of a seam on the given axis.
static bool compare_straddling(Vector2 p, bool vertical_seam)
{
    float *axis = vertical_seam ? &p.x : &p.y;
    float seam = *axis;
    const float values[] = { nextafterf(seam, -INFINITY), seam, nextafterf(seam, INFINITY) };
    for (int i = 0; i < 3; ++i) {
        *axis = values[i];
        if (!compare(p))
            return false;
    }
    return true;
}

static int compare_unsigned(const void *a, const void *b)
{
    unsigned int x = *(const unsigned int *)a, y = *(const unsigned int *)b;
    return x < y ? -1 : x > y;
}

static void report_cost(const char *name, unsigned int *values, size_t n)
{
    qsort(values, n, sizeof(*values), compare_unsigned);
    double sum = 0;
    for (size_t i = 0; i < n; ++i)
        sum += values[i];
    printf("%s mean=%.2f p95=%u max=%u", name, sum / (double)n, values[(n - 1) * 95 / 100],
        values[n - 1]);
}

static int queries(void)
{
    const BoatMapDescription *river = &waterfront_city_river_map;
    for (int m = 0; m < CELL_SIZES; ++m)
        CHECK(boat_map_init_cell(&parity.maps[m], river, cell_sizes[m]));
    const BoatMap *map = &parity.maps[1];
    Vector2 lo, hi;
    bounds(&lo, &hi);
    // 1. Dense lattice on 8 m multiples over the crop plus 40 m: every tested cell size's seams
    // lie on it, so many points sit exactly on cell boundaries.
    for (float z = floorf((lo.y - 40) / 8) * 8; z <= hi.y + 40; z += 8)
        for (float x = floorf((lo.x - 40) / 8) * 8; x <= hi.x + 40; x += 8)
            CHECK(compare((Vector2) { x, z }));
    size_t lattice = parity.queries;
    // 2. Along and across every shore edge (crop edges and bridge-leg holes included), just
    // inside and outside each tested clearance.
    const float offsets[] = { 0, .01f, -.01f, .49f, -.49f, .51f, -.51f, 2.99f, -2.99f, 3.01f,
        -3.01f, 11.99f, -11.99f, 12.01f, -12.01f, 30, -30 };
    const Mesh3D *mesh = river->mesh;
    for (size_t i = 0; i < map->shore_count; ++i) {
        Vector2 a = { mesh->vertices[map->shore[i].a].x, mesh->vertices[map->shore[i].a].z };
        Vector2 b = { mesh->vertices[map->shore[i].b].x, mesh->vertices[map->shore[i].b].z };
        Vector2 along = Vector2Subtract(b, a);
        Vector2 n = Vector2Normalize((Vector2) { -along.y, along.x });
        for (int t = 0; t <= 8; ++t)
            for (size_t o = 0; o < sizeof(offsets) / sizeof(*offsets); ++o)
                CHECK(compare(Vector2Add(
                    Vector2Add(a, Vector2Scale(along, t / 8.0f)), Vector2Scale(n, offsets[o]))));
    }
    size_t shore = parity.queries - lattice;
    // 3. Tile seams of both sizes, every 2 m and one float either side; then a fine patch
    // around each tile corner.
    for (size_t s = 0; s < sizeof(tile_sizes) / sizeof(*tile_sizes); ++s) {
        float size = tile_sizes[s];
        for (float seam = floorf(lo.x / size) * size; seam <= hi.x; seam += size)
            for (float z = lo.y - 40; z <= hi.y + 40; z += 2)
                CHECK(compare_straddling((Vector2) { seam, z }, true));
        for (float seam = floorf(lo.y / size) * size; seam <= hi.y; seam += size)
            for (float x = lo.x - 40; x <= hi.x + 40; x += 2)
                CHECK(compare_straddling((Vector2) { x, seam }, false));
        for (float cz = floorf(lo.y / size) * size; cz <= hi.y; cz += size)
            for (float cx = floorf(lo.x / size) * size; cx <= hi.x; cx += size)
                for (int dz = -4; dz <= 4; ++dz)
                    for (int dx = -4; dx <= 4; ++dx)
                        CHECK(compare((Vector2) { cx + dx * .25f, cz + dz * .25f }));
    }
    size_t seams = parity.queries - lattice - shore;
    // 4. Deterministic random points over the crop plus 100 m.
    uint32_t state = 0x2a2a2a2au;
    for (int i = 0; i < 50000; ++i) {
        float u[2];
        for (int k = 0; k < 2; ++k) {
            state = state * 1664525u + 1013904223u;
            u[k] = (float)(state >> 8) / (float)(1u << 24);
        }
        CHECK(compare((Vector2) {
            lo.x - 100 + (hi.x - lo.x + 200) * u[0], lo.y - 100 + (hi.y - lo.y + 200) * u[1] }));
    }
    // 5. Far away, including the exhaustive fallback beyond 1e6 m.
    const Vector2 far[]
        = { { 5000, 0 }, { -20000, 3000 }, { 0, -250000 }, { 2e6f, 2e6f }, { -FLT_MAX, 0 } };
    for (size_t i = 0; i < sizeof(far) / sizeof(*far); ++i)
        CHECK(compare(far[i]));
    printf("parity queries=%zu lattice=%zu shore=%zu seams=%zu mismatches=%zu\n", parity.queries,
        lattice, shore, seams, parity.mismatches);
    CHECK(parity.mismatches == 0);

    for (int k = 0; k < 2; ++k) {
        printf("index cell=%g %s candidates(normal, 3 m, %zu points): ", (double)BOAT_MAP_CELL_SIZE,
            k ? "land" : "water", parity.samples[k]);
        report_cost("edges", parity.edges[k], parity.samples[k]);
        fputs(" ", stdout);
        report_cost("triangles", parity.triangles[k], parity.samples[k]);
        printf(" exhaustive edges=%zu triangles=%zu\n", map->shore_count, map->water_count);
    }
    for (int m = 0; m < CELL_SIZES; ++m) {
        const BoatMapGrid *grid = &parity.maps[m].grid;
        size_t cells = (size_t)grid->columns * (size_t)grid->rows;
        printf("index cell=%g grid=%dx%d water_entries=%u shore_entries=%u bytes=%zu\n",
            (double)grid->cell_size, grid->columns, grid->rows, grid->water_start[cells],
            grid->shore_start[cells], boat_map_bytes(&parity.maps[m]));
    }
    for (int k = 0; k < 2; ++k) {
        free(parity.edges[k]);
        free(parity.triangles[k]);
    }
    for (int m = 0; m < CELL_SIZES; ++m)
        boat_map_destroy(&parity.maps[m]);
    return 0;
}

static bool listed(const unsigned int *list, unsigned int first, unsigned int count, unsigned int v)
{
    for (unsigned int i = 0; i < count; ++i)
        if (list[first + i] == v)
            return true;
    return false;
}

static int tiles(void)
{
    const BoatMapDescription *river = &waterfront_city_river_map;
    BoatMap map;
    CHECK(boat_map_init(&map, river));
    const Mesh3D *mesh = river->mesh;
    BoatMapTiles catalogue = { 0 };
    boat_map_tiles_destroy(&catalogue);
    BoatMap unready = { 0 };
    CHECK(!boat_map_tiles_init(&catalogue, &unready, 512) && !catalogue.tiles);
    CHECK(!boat_map_tiles_init(&catalogue, &map, 0) && !catalogue.tiles);
    const Mesh3D empty_mesh = { mesh->vertices, mesh->triangles, 0 };
    const Mesh3D land_mesh = { mesh->vertices, mesh->triangles + map.water_count, 1 };
    const BoatMapDescription empty = { &empty_mesh, 0 }, land_only = { &land_mesh, 1 },
                             missing = { NULL, 0 };
    CHECK(!boat_map_tiles_init_land(&catalogue, NULL, 512) && !catalogue.tiles);
    CHECK(!boat_map_tiles_init_land(&catalogue, &missing, 512) && !catalogue.tiles);
    CHECK(!boat_map_tiles_init_land(&catalogue, &empty, 512) && !catalogue.tiles);
    CHECK(!boat_map_tiles_init_land(&catalogue, &land_only, 512) && !catalogue.tiles);
    CHECK(!boat_map_tiles_init_land(&catalogue, river, -1) && !catalogue.tiles);
    for (size_t s = 0; s < sizeof(tile_sizes) / sizeof(*tile_sizes); ++s) {
        float size = tile_sizes[s];
        CHECK(boat_map_tiles_init(&catalogue, &map, size));
        CHECK(catalogue.map == &map && catalogue.tile_size == size);
        CHECK(fmodf(catalogue.origin.x, size) == 0 && fmodf(catalogue.origin.y, size) == 0);
        size_t n = (size_t)catalogue.columns * (size_t)catalogue.rows;
        // Every item is listed exactly where its geometry is within the margin of the tile.
        unsigned int *owners = calloc(mesh->triangle_count, sizeof(*owners));
        CHECK(owners);
        size_t empty = 0, land_max = 0, water_max = 0, shore_max = 0, land_tiles = 0;
        for (size_t t = 0; t < n; ++t) {
            const BoatMapTile *tile = &catalogue.tiles[t];
            CHECK(tile->max.x - tile->min.x == size && tile->max.y - tile->min.y == size);
            CHECK(boat_map_tiles_at(&catalogue, Vector2Scale(Vector2Add(tile->min, tile->max), .5f))
                == tile);
            empty += boat_map_tile_empty(tile);
            land_tiles += tile->land_count > 0;
            land_max = tile->land_count > land_max ? tile->land_count : land_max;
            water_max = tile->water_count > water_max ? tile->water_count : water_max;
            shore_max = tile->shore_count > shore_max ? tile->shore_count : shore_max;
            for (size_t i = 0; i < map.water_count; ++i) {
                Vector2 p[3];
                for (int j = 0; j < 3; ++j) {
                    Vector3 v = mesh->vertices[mesh->triangles[i].vertices[j]];
                    p[j] = (Vector2) { v.x, v.z };
                }
                bool near = boat_map_polygon_near_rect(p, 3, tile->min, tile->max, BOAT_MAP_MARGIN);
                CHECK(near
                    == listed(
                        catalogue.water, tile->water_first, tile->water_count, (unsigned int)i));
            }
            for (size_t i = 0; i < map.shore_count; ++i) {
                Vector3 a = mesh->vertices[map.shore[i].a], b = mesh->vertices[map.shore[i].b];
                Vector2 p[2] = { { a.x, a.z }, { b.x, b.z } };
                bool near = boat_map_polygon_near_rect(p, 2, tile->min, tile->max, BOAT_MAP_MARGIN);
                CHECK(near
                    == listed(
                        catalogue.shore, tile->shore_first, tile->shore_count, (unsigned int)i));
            }
            for (unsigned int k = 0; k < tile->land_count; ++k) {
                unsigned int index = catalogue.land[tile->land_first + k];
                CHECK(index >= map.water_count && index < mesh->triangle_count);
                CHECK(k == 0 || index > catalogue.land[tile->land_first + k - 1]);
                ++owners[index];
                const Triangle3D *triangle = &mesh->triangles[index];
                Vector2 centroid = { 0 };
                for (int j = 0; j < 3; ++j) {
                    Vector3 v = mesh->vertices[triangle->vertices[j]];
                    centroid = Vector2Add(centroid, (Vector2) { v.x / 3, v.z / 3 });
                    CHECK(v.x >= tile->land_min.x && v.y >= tile->land_min.y
                        && v.z >= tile->land_min.z && v.x <= tile->land_max.x
                        && v.y <= tile->land_max.y && v.z <= tile->land_max.z);
                }
                CHECK(boat_map_tiles_at(&catalogue, centroid) == tile);
            }
            CHECK(tile->land_count || tile->land_min.x > tile->land_max.x);
        }
        // Each land triangle (bridge included) belongs to exactly one tile.
        for (size_t i = map.water_count; i < mesh->triangle_count; ++i)
            CHECK(owners[i] == 1);
        free(owners);
        CHECK(boat_map_tiles_at(&catalogue, (Vector2) { catalogue.origin.x - 1, 0 }) == NULL);
        printf("tiles size=%g grid=%dx%d nonempty=%zu empty=%zu land_tiles=%zu "
               "max_land=%zu max_water=%zu max_shore=%zu entries water=%zu shore=%zu land=%zu "
               "bytes=%zu\n",
            (double)size, catalogue.columns, catalogue.rows, n - empty, empty, land_tiles, land_max,
            water_max, shore_max, catalogue.water_entries, catalogue.shore_entries,
            catalogue.land_entries, boat_map_tiles_bytes(&catalogue));
        // The renderers' land-only catalogue owns land exactly like the full one.
        BoatMapTiles land = { 0 };
        CHECK(boat_map_tiles_init_land(&land, river, size));
        CHECK(!land.map && land.description == river && land.water_count == map.water_count);
        CHECK(land.columns == catalogue.columns && land.rows == catalogue.rows);
        CHECK(!land.water_entries && !land.shore_entries);
        CHECK(land.land_entries == catalogue.land_entries
            && memcmp(land.land, catalogue.land, land.land_entries * sizeof(*land.land)) == 0);
        for (size_t t = 0; t < n; ++t) {
            const BoatMapTile *a = &land.tiles[t], *b = &catalogue.tiles[t];
            CHECK(a->land_first == b->land_first && a->land_count == b->land_count);
            CHECK(memcmp(&a->land_min, &b->land_min, sizeof(a->land_min)) == 0
                && memcmp(&a->land_max, &b->land_max, sizeof(a->land_max)) == 0);
        }
        boat_map_tiles_destroy(&land);
        boat_map_tiles_destroy(&catalogue);
        boat_map_tiles_destroy(&catalogue);
        CHECK(!catalogue.tiles && !catalogue.map);
    }
    boat_map_destroy(&map);
    return 0;
}

static int failures(void)
{
    BoatMap map;
    CHECK(!boat_map_init_cell(&map, &waterfront_city_river_map, 0) && !map.shore);
    CHECK(!boat_map_init_cell(&map, &waterfront_city_river_map, -1) && !map.grid.water_start);
    CHECK(!boat_map_init_cell(&map, &waterfront_city_river_map, NAN) && !map.grid.shore);
    CHECK(boat_location(BOAT_LOCATION_RIVER)->map == &waterfront_city_river_map);
    return 0;
}

int main(void)
{
    if (failures() || tiles() || queries())
        return 1;
    puts("boat_map_index=ok");
    return 0;
}
