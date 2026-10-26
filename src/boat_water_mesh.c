#include "boat_water_mesh.h"
#include "raymath.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef BoatWaterMeshSurface Surface;

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

static void surface_destroy(Surface *s)
{
    free(s->vertices);
    free(s->triangles);
    *s = (Surface) { 0 };
}

static void index_destroy(BoatWaterIndex *index);

void boat_water_mesh_cache_destroy(BoatWaterMeshCache *cache)
{
    surface_destroy(&cache->surface);
    surface_destroy(&cache->rest);
    free(cache->rest_first);
    index_destroy(&cache->index);
    surface_destroy(&cache->field);
    for (int i = 0; i < 2; ++i)
        surface_destroy(&cache->patches[i]);
    *cache = (BoatWaterMeshCache) { 0 };
}

// Map-dependent surfaces rebuild after a rebind; their storage is reused.
static void cache_bind(BoatWaterMeshCache *cache, const BoatMapDescription *map)
{
    if (cache->map == map)
        return;
    cache->map = map;
    cache->index.ready = false;
    cache->rest.count = 0;
    cache->surface.count = 0;
    cache->patches[0].count = cache->patches[1].count = 0;
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

// Vertical allowance for displaced water in a visibility box: ambient waves, storm and wakes.
#define BOAT_WATER_VISIBLE_PAD 8.0f

static int compare_ids(const void *a, const void *b)
{
    unsigned int x = *(const unsigned int *)a, y = *(const unsigned int *)b;
    return (x > y) - (x < y);
}

// Water triangles of MAP in mesh order: the leading y = 0 triangles, or the two basin ones.
static const Vector3 basin_water[2][3] = {
    { { -120, 0, -80 }, { -120, 0, 80 }, { 120, 0, 80 } },
    { { -120, 0, -80 }, { 120, 0, 80 }, { 120, 0, -80 } },
};

static void water_triangle(const BoatMapDescription *map, size_t i, Vector3 out[3])
{
    if (!map) {
        for (int j = 0; j < 3; ++j)
            out[j] = basin_water[i][j];
        return;
    }
    Triangle3D t = map->mesh->triangles[i];
    for (int j = 0; j < 3; ++j)
        out[j] = map->mesh->vertices[t.vertices[j]];
}

static size_t water_triangle_count(const BoatMapDescription *map)
{
    if (!map)
        return 2;
    const Mesh3D *city = map->mesh;
    size_t n = 0;
    while (n < city->triangle_count) {
        Triangle3D t = city->triangles[n];
        if (city->vertices[t.vertices[0]].y != 0 || city->vertices[t.vertices[1]].y != 0
            || city->vertices[t.vertices[2]].y != 0)
            break; // Imported water triangles precede land.
        ++n;
    }
    return n;
}

static void index_destroy(BoatWaterIndex *index)
{
    free(index->first);
    free(index->list);
    free(index->stamp);
    free(index->gathered);
    *index = (BoatWaterIndex) { 0 };
}

// Call VISIT(context, cell) for each grid cell whose square the triangle overlaps, row by row.
static void index_cover(
    const BoatWaterIndex *index, const Vector3 t[3], void (*visit)(void *, int), void *context)
{
    float min_z = fminf(t[0].z, fminf(t[1].z, t[2].z));
    float max_z = fmaxf(t[0].z, fmaxf(t[1].z, t[2].z));
    int row0 = (int)floorf((min_z - index->origin.y) / index->cell);
    int row1 = (int)floorf((max_z - index->origin.y) / index->cell);
    row0 = row0 < 0 ? 0 : row0;
    row1 = row1 >= index->rows ? index->rows - 1 : row1;
    for (int row = row0; row <= row1; ++row) {
        Vector3 band[12], clipped[12];
        float bottom = index->origin.y + row * index->cell;
        int count = clip_edge(t, 3, clipped, 1, bottom, true);
        count = clip_edge(clipped, count, band, 1, bottom + index->cell, false);
        if (!count)
            continue;
        float min_x = band[0].x, max_x = band[0].x;
        for (int j = 1; j < count; ++j) {
            min_x = fminf(min_x, band[j].x);
            max_x = fmaxf(max_x, band[j].x);
        }
        int col0 = (int)floorf((min_x - index->origin.x) / index->cell);
        int col1 = (int)floorf((max_x - index->origin.x) / index->cell);
        col0 = col0 < 0 ? 0 : col0;
        col1 = col1 >= index->columns ? index->columns - 1 : col1;
        for (int col = col0; col <= col1; ++col)
            visit(context, row * index->columns + col);
    }
}

typedef struct {
    BoatWaterIndex *index;
    unsigned int *cursor;
    unsigned int triangle;
} IndexFill;

static void index_count_cell(void *context, int cell)
{
    ++((IndexFill *)context)->index->first[cell + 1];
}

static void index_fill_cell(void *context, int cell)
{
    IndexFill *fill = context;
    fill->index->list[fill->cursor[cell]++] = fill->triangle;
}

static bool index_build(BoatWaterIndex *index, const BoatMapDescription *map)
{
    index_destroy(index);
    size_t count = water_triangle_count(map);
    float min_x = INFINITY, min_z = INFINITY, max_x = -INFINITY, max_z = -INFINITY;
    for (size_t i = 0; i < count; ++i) {
        Vector3 t[3];
        water_triangle(map, i, t);
        for (int j = 0; j < 3; ++j) {
            min_x = fminf(min_x, t[j].x);
            max_x = fmaxf(max_x, t[j].x);
            min_z = fminf(min_z, t[j].z);
            max_z = fmaxf(max_z, t[j].z);
        }
    }
    index->cell = BOAT_WATER_INDEX_CELL;
    index->count = count;
    if (!count) {
        index->ready = true;
        return true;
    }
    index->origin = (Vector2) { floorf(min_x / index->cell) * index->cell,
        floorf(min_z / index->cell) * index->cell };
    index->columns = (int)floorf((max_x - index->origin.x) / index->cell) + 1;
    index->rows = (int)floorf((max_z - index->origin.y) / index->cell) + 1;
    size_t cells = (size_t)index->columns * (size_t)index->rows;
    unsigned int *cursor = NULL;
    index->first = calloc(cells + 1, sizeof(*index->first));
    index->stamp = calloc(count, sizeof(*index->stamp));
    if (!index->first || !index->stamp)
        goto fail;
    IndexFill fill = { .index = index };
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < count; ++i) {
            Vector3 t[3];
            water_triangle(map, i, t);
            fill.triangle = (unsigned int)i;
            index_cover(index, t, pass ? index_fill_cell : index_count_cell, &fill);
        }
        if (pass)
            break;
        for (size_t c = 0; c < cells; ++c)
            index->first[c + 1] += index->first[c];
        index->entries = index->first[cells];
        index->list = malloc((index->entries ? index->entries : 1) * sizeof(*index->list));
        cursor = malloc(cells * sizeof(*cursor));
        if (!index->list || !cursor)
            goto fail;
        memcpy(cursor, index->first, cells * sizeof(*cursor));
        fill.cursor = cursor;
    }
    free(cursor);
    index->ready = true;
    return true;
fail:
    free(cursor);
    index_destroy(index);
    return false;
}

// Triangles listed by any cell of the inclusive range that VISIBLE (when given) accepts, once each,
// ascending, in the scratch array (valid until the next gather). False on allocation failure.
static bool index_gather(BoatWaterIndex *index, int col0, int row0, int col1, int row1,
    BoatWaterMeshVisible visible, void *context, const unsigned int **sources, size_t *count_out)
{
    *sources = index->gathered;
    *count_out = 0;
    if (!index->count)
        return true;
    col0 = col0 < 0 ? 0 : col0;
    row0 = row0 < 0 ? 0 : row0;
    col1 = col1 >= index->columns ? index->columns - 1 : col1;
    row1 = row1 >= index->rows ? index->rows - 1 : row1;
    if (!++index->epoch) {
        memset(index->stamp, 0, index->count * sizeof(*index->stamp));
        index->epoch = 1;
    }
    size_t n = 0;
    for (int row = row0; row <= row1; ++row)
        for (int col = col0; col <= col1; ++col) {
            int cell = row * index->columns + col;
            if (visible && index->first[cell] != index->first[cell + 1]) {
                Vector3 min = { index->origin.x + col * index->cell, -BOAT_WATER_VISIBLE_PAD,
                    index->origin.y + row * index->cell };
                Vector3 max = { min.x + index->cell, BOAT_WATER_VISIBLE_PAD, min.z + index->cell };
                if (!visible(context, min, max))
                    continue;
            }
            for (unsigned int k = index->first[cell]; k < index->first[cell + 1]; ++k) {
                unsigned int id = index->list[k];
                if (index->stamp[id] == index->epoch)
                    continue;
                index->stamp[id] = index->epoch;
                if (n == index->gathered_capacity) {
                    size_t capacity = n ? n * 2 : 256;
                    unsigned int *g = realloc(index->gathered, capacity * sizeof(*g));
                    if (!g)
                        return false;
                    index->gathered = g;
                    index->gathered_capacity = capacity;
                }
                index->gathered[n++] = id;
            }
        }
    qsort(index->gathered, n, sizeof(*index->gathered), compare_ids);
    *sources = index->gathered;
    *count_out = n;
    return true;
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

static const Mesh3D *water_patch(
    BoatWaterMeshCache *cache, const BoatMapDescription *map, Vector2 world_center, bool fine)
{
    cache_bind(cache, map);
    Surface *s = &cache->patches[fine];
    Vector2 center = { roundf(world_center.x / 12) * 12, roundf(world_center.y / 12) * 12 };
    if (s->count && Vector2Equals(center, cache->patch_centers[fine]))
        return &s->mesh;
    s->count = 0;
    Vector2 origin = { center.x - 96, center.y - 96 };
    if (!cache->index.ready && !index_build(&cache->index, map))
        return NULL;
    BoatWaterIndex *index = &cache->index;
    // Only triangles listed by a cell the patch square touches can contribute to it.
    size_t count;
    const unsigned int *sources;
    bool ok = true;
    if (cache->full_scan) {
        sources = NULL;
        count = index->count;
    } else {
        ok = index_gather(index, (int)floorf((origin.x - index->origin.x) / index->cell),
            (int)floorf((origin.y - index->origin.y) / index->cell),
            (int)floorf((origin.x + 192 - index->origin.x) / index->cell),
            (int)floorf((origin.y + 192 - index->origin.y) / index->cell), NULL, NULL, &sources,
            &count);
    }
    for (size_t i = 0; ok && i < count; ++i) {
        Vector3 t[3];
        water_triangle(map, sources ? sources[i] : i, t);
        ok = patch_triangle(s, t[0], t[1], t[2], origin, fine);
    }
    cache->patch_sources[fine] = count;
    if (!ok) {
        s->count = 0;
        return NULL;
    }
    cache->patch_centers[fine] = center;
    s->mesh = (Mesh3D) { s->vertices, s->triangles, s->count };
    return &s->mesh;
}

const Mesh3D *boat_water_patch(
    BoatWaterMeshCache *cache, const BoatMapDescription *map, Vector2 center)
{
    return water_patch(cache, map, center, false);
}

const Mesh3D *boat_water_patch_arcade(
    BoatWaterMeshCache *cache, const BoatMapDescription *map, Vector2 center)
{
    return water_patch(cache, map, center, true);
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

const Mesh3D *boat_water_surface(
    BoatWaterMeshCache *cache, const BoatMapDescription *map, BoatWater water, double time)
{
    return boat_water_surface_visible(cache, map, water, time, NULL, NULL);
}

// Tessellate every water triangle once, remembering which rest triangles each produced.
static bool rest_build(BoatWaterMeshCache *cache, const BoatMapDescription *map)
{
    size_t count = cache->index.count;
    unsigned int *first = realloc(cache->rest_first, (count + 1) * sizeof(*first));
    if (!first)
        return false;
    cache->rest_first = first;
    Surface *rest = &cache->rest;
    rest->count = 0;
    for (size_t i = 0; i < count; ++i) {
        Vector3 t[3];
        water_triangle(map, i, t);
        first[i] = (unsigned int)rest->count;
        if (!subdivide(rest, t[0], t[1], t[2], 0)) {
            rest->count = 0;
            return false;
        }
    }
    first[count] = (unsigned int)rest->count;
    return true;
}

const Mesh3D *boat_water_surface_visible(BoatWaterMeshCache *cache, const BoatMapDescription *map,
    BoatWater water, double time, BoatWaterMeshVisible visible, void *context)
{
    cache_bind(cache, map);
    Surface *s = &cache->surface;
    if (!cache->index.ready && !index_build(&cache->index, map))
        return NULL;
    if (!cache->rest.count && !rest_build(cache, map))
        return NULL;
    BoatWaterIndex *index = &cache->index;
    const unsigned int *sources = NULL;
    size_t source_count = index->count;
    if (visible && !cache->full_scan && index->count
        && !index_gather(index, 0, 0, index->columns - 1, index->rows - 1, visible, context,
            &sources, &source_count))
        return NULL;
    s->count = 0;
    for (size_t k = 0; k < source_count; ++k) {
        size_t i = sources ? sources[k] : k; // No visibility test: every triangle, in order.
        for (unsigned int r = cache->rest_first[i]; r < cache->rest_first[i + 1]; ++r) {
            Triangle3D t = cache->rest.triangles[r];
            if (!surface_triangle(s, cache->rest.vertices[t.vertices[0]],
                    cache->rest.vertices[t.vertices[1]], cache->rest.vertices[t.vertices[2]])) {
                s->count = 0;
                return NULL;
            }
        }
    }
    cache->surface_sources = source_count;
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
        Color base = map ? (Color) { 45, 79, 104, 255 } : (Color) { 36, 107, 125, 255 };
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

const Mesh3D *boat_water_surface_field(
    BoatWaterMeshCache *cache, BoatWater water, const BoatWake *field, double time)
{
    Surface *s = &cache->field;
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
