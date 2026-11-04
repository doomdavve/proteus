/* Ambient occlusion bake (graphics G5c): writes or checks assets/geometry/<map>/ambient.bin from
 * the map's scenery pack alone (boat_ambient.h describes the format). Each sample point casts the
 * same fixed rays against the pack's other land, wall, building and bridge triangles, in a fixed
 * order with correctly rounded float arithmetic and no libm call but sqrtf, so the bytes are the
 * same on every platform; the `ambient_<map>` tests rebake and compare them with the committed
 * files.
 *
 *     ambient_bake --map river|outer --out FILE
 *     ambient_bake --map river|outer --check FILE
 *     ambient_bake --map river|outer --stats      # value histogram only
 */
#include "boat_ambient.h"
#include "boat_location.h"
#include "boat_map.h"
#include "boat_scenery.h"
#include "raylib.h"
#include "waterfront_corner.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Cosine-weighted directions in a corner's frame (x, y tangent; z along the normal): a golden-
// angle spiral over the unit disc lifted onto the hemisphere, written out so no libm call can
// differ.
static const float rays[BOAT_AMBIENT_RAYS][3]
    = { { 0.14433757f, 0.00000000f, 0.98952851f }, { -0.18434222f, 0.16887257f, 0.96824584f },
          { 0.02821653f, -0.32151282f, 0.94648472f }, { 0.23235143f, 0.30306129f, 0.92421138f },
          { -0.42639345f, -0.07542300f, 0.90138782f }, { 0.40391710f, -0.25693899f, 0.87797115f },
          { -0.13510236f, 0.50257406f, 0.85391256f }, { -0.25765486f, -0.49609875f, 0.82915620f },
          { 0.55900798f, 0.20414882f, 0.80363756f }, { -0.58155466f, 0.24005731f, 0.77728159f },
          { 0.28034777f, -0.59908691f, 0.75000000f }, { 0.20716987f, 0.66049020f, 0.72168784f },
          { -0.62441241f, -0.36185975f, 0.69221866f }, { 0.73250683f, -0.16103957f, 0.66143783f },
          { -0.44703752f, 0.63586486f, 0.62915287f }, { -0.10327602f, -0.79697390f, 0.59511904f },
          { 0.63401345f, 0.53434721f, 0.55901699f }, { -0.85318337f, 0.03528181f, 0.52041650f },
          { 0.62233177f, -0.61930324f, 0.47871355f }, { -0.04163641f, 0.90042568f, 0.43301270f },
          { -0.59215068f, -0.70959442f, 0.38188131f }, { 0.93803206f, 0.12621089f, 0.32274861f },
          { -0.79479267f, 0.55299604f, 0.25000000f }, { 0.21718307f, -0.96540053f, 0.14433757f } };

#define CELL 4.0f // Metres per side of the occluder grid's X/Z cells.
#define INSET .1f // A corner's sample point moves this far toward the centroid...
#define LIFT .02f // ...and this far off its face.

typedef struct {
    Vector3 a, e1, e2;
} Occluder;

typedef struct {
    float origin_x, origin_z;
    int columns, rows;
    unsigned *start; // columns * rows + 1 offsets into items.
    unsigned *items; // Occluder triangle indices, ascending within each cell.
} Grid;

static Vector3 sub(Vector3 a, Vector3 b) { return (Vector3) { a.x - b.x, a.y - b.y, a.z - b.z }; }
static Vector3 add(Vector3 a, Vector3 b) { return (Vector3) { a.x + b.x, a.y + b.y, a.z + b.z }; }
static Vector3 scale(Vector3 a, float s) { return (Vector3) { a.x * s, a.y * s, a.z * s }; }
static float dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vector3 cross(Vector3 a, Vector3 b)
{
    return (Vector3) { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
static Vector3 normalized(Vector3 a)
{
    float length = sqrtf(dot(a, a));
    return length > 0 ? scale(a, 1 / length) : a;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void cell_range(const Grid *g, float low, float high, bool z, int *first, int *last)
{
    float origin = z ? g->origin_z : g->origin_x;
    int count = z ? g->rows : g->columns;
    *first = clampi((int)floorf((low - origin) / CELL), 0, count - 1);
    *last = clampi((int)floorf((high - origin) / CELL), 0, count - 1);
}

// Two passes over the occluders, counting and then filling, keep each cell's list ascending.
static bool grid_init(Grid *g, const Mesh3D *mesh, size_t first)
{
    float lx = INFINITY, lz = INFINITY, hx = -INFINITY, hz = -INFINITY;
    for (size_t i = first; i < mesh->triangle_count; ++i)
        for (int j = 0; j < 3; ++j) {
            Vector3 v = mesh->vertices[mesh->triangles[i].vertices[j]];
            lx = fminf(lx, v.x), hx = fmaxf(hx, v.x), lz = fminf(lz, v.z), hz = fmaxf(hz, v.z);
        }
    *g = (Grid) { .origin_x = lx,
        .origin_z = lz,
        .columns = (int)((hx - lx) / CELL) + 1,
        .rows = (int)((hz - lz) / CELL) + 1 };
    size_t cells = (size_t)g->columns * (size_t)g->rows;
    g->start = calloc(cells + 1, sizeof(*g->start));
    if (!g->start)
        return false;
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = first; i < mesh->triangle_count; ++i) {
            const Triangle3D *t = &mesh->triangles[i];
            Vector3 a = mesh->vertices[t->vertices[0]], b = mesh->vertices[t->vertices[1]];
            Vector3 c = mesh->vertices[t->vertices[2]];
            int i0, i1, j0, j1;
            cell_range(
                g, fminf(a.x, fminf(b.x, c.x)), fmaxf(a.x, fmaxf(b.x, c.x)), false, &i0, &i1);
            cell_range(g, fminf(a.z, fminf(b.z, c.z)), fmaxf(a.z, fmaxf(b.z, c.z)), true, &j0, &j1);
            for (int j = j0; j <= j1; ++j)
                for (int k = i0; k <= i1; ++k) {
                    size_t cell = (size_t)j * (size_t)g->columns + (size_t)k;
                    if (pass == 0)
                        ++g->start[cell + 1];
                    else
                        g->items[g->start[cell]++] = (unsigned)i;
                }
        }
        if (pass == 0) {
            for (size_t cell = 0; cell < cells; ++cell)
                g->start[cell + 1] += g->start[cell];
            g->items = malloc((size_t)g->start[cells] * sizeof(*g->items) + 1);
            if (!g->items)
                return false;
        } else {
            // Filling advanced each start to the next cell's; shift them back.
            memmove(g->start + 1, g->start, cells * sizeof(*g->start));
            g->start[0] = 0;
        }
    }
    return true;
}

// Distance along unit DIRECTION from O to occluder T, or RANGE when there is no hit before it.
static float hit(const Occluder *t, Vector3 o, Vector3 direction, float range)
{
    Vector3 p = cross(direction, t->e2);
    float det = dot(t->e1, p);
    if (fabsf(det) < 1e-9f)
        return range;
    float inverse = 1 / det;
    Vector3 s = sub(o, t->a);
    float u = dot(s, p) * inverse;
    if (u < 0 || u > 1)
        return range;
    Vector3 q = cross(s, t->e1);
    float v = dot(direction, q) * inverse;
    if (v < 0 || u + v > 1)
        return range;
    float d = dot(t->e2, q) * inverse;
    return d > 0 && d < range ? d : range;
}

typedef struct {
    const BoatMapDescription *scenery;
    const Mesh3D *mesh;
    size_t water;
    Occluder *occluders; // Indexed by triangle; water entries unused.
    Grid grid;
    unsigned *stamp; // Per triangle: the last sample point that gathered it.
    unsigned *near; // The current sample point's candidates.
} Bake;

// The share of the hemisphere above point P of triangle I (unit normal N, centroid C) that
// nearby scenery closes off, 0..1. P moves INSET toward C and LIFT off the face first.
static float closed_at(Bake *b, size_t i, Vector3 n, Vector3 c, Vector3 p, unsigned *serial)
{
    const Mesh3D *mesh = b->mesh;
    Vector3 toward = sub(c, p);
    float length = sqrtf(dot(toward, toward));
    float inset = length > 2 * INSET ? INSET / length : .5f;
    Vector3 o = add(add(p, scale(toward, inset)), scale(n, LIFT));
    const float range = BOAT_AMBIENT_RANGE;
    // Candidates: occluders in the cells around O with a vertex in front of the face.
    int i0, i1, j0, j1, count = 0;
    cell_range(&b->grid, o.x - range, o.x + range, false, &i0, &i1);
    cell_range(&b->grid, o.z - range, o.z + range, true, &j0, &j1);
    unsigned stamp = ++*serial;
    for (int z = j0; z <= j1; ++z)
        for (int x = i0; x <= i1; ++x) {
            size_t cell = (size_t)z * (size_t)b->grid.columns + (size_t)x;
            for (unsigned k = b->grid.start[cell]; k < b->grid.start[cell + 1]; ++k) {
                unsigned other = b->grid.items[k];
                if (other == i || b->stamp[other] == stamp)
                    continue;
                b->stamp[other] = stamp;
                const Triangle3D *u = &mesh->triangles[other];
                bool front = false, low = true, high = true;
                for (int m = 0; m < 3; ++m) {
                    Vector3 q = sub(mesh->vertices[u->vertices[m]], o);
                    front = front || dot(q, n) > 0;
                    low = low && q.y < -range;
                    high = high && q.y > range;
                }
                if (front && !low && !high)
                    b->near[count++] = other;
            }
        }
    if (!count)
        return 0;
    Vector3 helper = fabsf(n.y) < .9f ? (Vector3) { 0, 1, 0 } : (Vector3) { 1, 0, 0 };
    Vector3 tx = normalized(cross(helper, n)), ty = cross(n, tx);
    float closed = 0;
    for (int r = 0; r < BOAT_AMBIENT_RAYS; ++r) {
        Vector3 direction
            = add(add(scale(tx, rays[r][0]), scale(ty, rays[r][1])), scale(n, rays[r][2]));
        float nearest = range;
        for (int k = 0; k < count; ++k)
            nearest = fminf(nearest, hit(&b->occluders[b->near[k]], o, direction, nearest));
        closed += 1 - nearest / range;
    }
    return closed / BOAT_AMBIENT_RAYS;
}

// Barycentric weights (this corner, the next, the one after) of the points whose mean a corner
// holds: the corner itself, two points along its edges and one inside its third of the triangle.
static const float region[][3]
    = { { 1, 0, 0 }, { 2 / 3.0f, 1 / 3.0f, 0 }, { 2 / 3.0f, 0, 1 / 3.0f }, { .5f, .25f, .25f } };

// Corner J of triangle I, 0..255. A facade corner is the corner point alone, since the GPU
// shades only its base metres; any other corner is the mean of its region, so that linear
// interpolation across a large triangle never spreads one creased corner over all of it.
static unsigned char corner(Bake *b, size_t i, int j, unsigned *serial)
{
    const Mesh3D *mesh = b->mesh;
    const Triangle3D *t = &mesh->triangles[i];
    Vector3 v[3] = { mesh->vertices[t->vertices[0]], mesh->vertices[t->vertices[1]],
        mesh->vertices[t->vertices[2]] };
    Vector3 n = normalized(cross(sub(v[1], v[0]), sub(v[2], v[0])));
    if (dot(n, n) == 0)
        return 0;
    Vector3 c = scale(add(add(v[0], v[1]), v[2]), 1 / 3.0f);
    bool facade = !boat_map_is_bridge_triangle(b->scenery, i)
        && waterfront_facade_triangle(v[0], v[1], v[2], t->color);
    int samples = facade ? 1 : (int)(sizeof(region) / sizeof(region[0]));
    float closed = 0;
    for (int s = 0; s < samples; ++s) {
        Vector3 p = add(add(scale(v[j], region[s][0]), scale(v[(j + 1) % 3], region[s][1])),
            scale(v[(j + 2) % 3], region[s][2]));
        closed += closed_at(b, i, n, c, p, serial);
    }
    int value = (int)(closed / (float)samples * 255 + .5f);
    return (unsigned char)(value > 255 ? 255 : value);
}

static unsigned char *bake(const BoatMapDescription *scenery, unsigned char **corners_out)
{
    const Mesh3D *mesh = scenery->mesh;
    Bake b = { .scenery = scenery, .mesh = mesh, .water = boat_map_leading_water(mesh) };
    unsigned char *corners = calloc(mesh->triangle_count * 3, 1);
    b.occluders = calloc(mesh->triangle_count, sizeof(*b.occluders));
    b.stamp = calloc(mesh->triangle_count, sizeof(*b.stamp));
    b.near = malloc(mesh->triangle_count * sizeof(*b.near));
    bool ok = corners && b.occluders && b.stamp && b.near && grid_init(&b.grid, mesh, b.water);
    for (size_t i = b.water; ok && i < mesh->triangle_count; ++i) {
        const Triangle3D *t = &mesh->triangles[i];
        Vector3 a = mesh->vertices[t->vertices[0]];
        b.occluders[i] = (Occluder) { a, sub(mesh->vertices[t->vertices[1]], a),
            sub(mesh->vertices[t->vertices[2]], a) };
    }
    unsigned serial = 0;
    for (size_t i = b.water; ok && i < mesh->triangle_count; ++i)
        for (int j = 0; j < 3; ++j)
            corners[i * 3 + (size_t)j] = corner(&b, i, j, &serial);
    free(b.occluders);
    free(b.stamp);
    free(b.near);
    free(b.grid.start);
    free(b.grid.items);
    if (!ok) {
        free(corners);
        return NULL;
    }
    *corners_out = corners;
    return corners;
}

int main(int argc, char **argv)
{
    const char *name = NULL, *out = NULL, *check = NULL;
    bool stats = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--stats") == 0)
            stats = true;
        else if (i + 1 < argc && strcmp(argv[i], "--map") == 0)
            name = argv[++i];
        else if (i + 1 < argc && strcmp(argv[i], "--out") == 0)
            out = argv[++i];
        else if (i + 1 < argc && strcmp(argv[i], "--check") == 0)
            check = argv[++i];
        else
            return 2;
    }
    const BoatWorldDefinition *location = NULL;
    if (name && strcmp(name, "river") == 0)
        location = boat_location(BOAT_LOCATION_RIVER);
    else if (name && strcmp(name, "outer") == 0)
        location = boat_location(BOAT_LOCATION_OUTER);
    if (!location || (out && check) || (!out && !check && !stats)) {
        fputs(
            "usage: ambient_bake --map river|outer --out FILE | --check FILE | --stats\n", stderr);
        return 2;
    }
    SetTraceLogLevel(LOG_WARNING);
    char path[4096];
    if (!boat_scenery_path(location, PROTEUS_SOURCE_DIR "/assets/", path, sizeof(path))
        || !boat_scenery_load(location->map, path)) {
        fprintf(stderr, "ambient_bake: cannot load %s\n", path);
        return 1;
    }
    const BoatMapDescription *scenery = boat_scenery_of(location->map);
    unsigned char *corners = NULL;
    if (!bake(scenery, &corners)) {
        fprintf(stderr, "ambient_bake: %s: bake failed\n", name);
        return 1;
    }
    size_t count = scenery->mesh->triangle_count * 3;
    if (stats) {
        size_t bins[8] = { 0 }, open = 0;
        for (size_t i = 0; i < count; ++i) {
            ++bins[corners[i] / 32];
            open += corners[i] == 0;
        }
        printf("ambient %s: %zu corners, %zu open;", name, count, open);
        for (int i = 0; i < 8; ++i)
            printf(" %d-%d:%zu", i * 32, i * 32 + 31, bins[i]);
        printf("\n");
    }
    int compressed = 0;
    unsigned char *deflated = CompressData(corners, (int)count, &compressed);
    size_t size = BOAT_AMBIENT_HEADER + (size_t)compressed;
    unsigned char *file = deflated ? malloc(size) : NULL;
    int result = !file;
    if (file) {
        boat_ambient_header(file, scenery->mesh->triangle_count, boat_ambient_key(scenery->mesh),
            (size_t)compressed);
        memcpy(file + BOAT_AMBIENT_HEADER, deflated, (size_t)compressed);
    }
    if (file && out) {
        FILE *f = fopen(out, "wb");
        result = !f || fwrite(file, 1, size, f) != size;
        if (f && fclose(f) != 0)
            result = 1;
    } else if (file && check) {
        FILE *f = fopen(check, "rb");
        unsigned char *expected = malloc(size + 1);
        size_t read = f && expected ? fread(expected, 1, size + 1, f) : 0;
        result = read != size || memcmp(expected, file, size) != 0;
        if (f)
            fclose(f);
        free(expected);
        if (result)
            fprintf(stderr, "ambient_bake: %s differs from a fresh %s bake\n", check, name);
        else
            printf("ambient=ok map=%s\n", name);
    }
    MemFree(deflated);
    free(file);
    free(corners);
    boat_scenery_unload();
    return result;
}
