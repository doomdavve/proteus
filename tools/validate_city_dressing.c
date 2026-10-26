#include "boat_test_maps.h"
#include "waterfront_city.h"
#include "waterfront_corner.h"
#include "waterfront_scene.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static const Mesh3D empty_mesh = { test_harbor_vertices, test_harbor_triangles, 0 };
static const BoatMapDescription empty = { &empty_mesh, 0 };

// Box geometry, colours, flags and a clearance grid: recorded from the process-global dressing
// at 2ea4755 (7944 boxes) before the scenery became caller-owned.
static uint64_t mix(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = p;
    for (size_t i = 0; i < n; ++i)
        h = (h ^ b[i]) * UINT64_C(1099511628211);
    return h;
}

static uint64_t box_digest(const WaterfrontScenery *s)
{
    uint64_t h = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < s->count; ++i) {
        h = mix(h, s->boxes[i].vertices, sizeof(s->boxes[i].vertices));
        h = mix(h, &s->boxes[i].color, sizeof(Color));
        unsigned char flags = (unsigned char)(s->boxes[i].emissive | s->boxes[i].waterline << 1);
        h = mix(h, &flags, 1);
    }
    return h;
}

static uint64_t clear_digest(const WaterfrontScenery *s)
{
    uint64_t h = UINT64_C(14695981039346656037);
    for (int z = -300; z <= 520; z += 7)
        for (int x = -400; x <= 480; x += 7) {
            unsigned char c = (unsigned char)waterfront_scenery_clear(
                s, (Vector3) { x + .3f, 3.6f, z + .7f }, .5f);
            h = mix(h, &c, 1);
        }
    return h;
}

// Wall-mounted fittings must stay within half a metre of an actual imported quay,
// including the submerged ladder ends. A relaxed global Y bound alone misses floating props.
static bool near_quay(Vector3 p)
{
    const Mesh3D *city = waterfront_city_river_map.mesh;
    for (size_t i = 0; i < city->triangle_count; ++i) {
        const Triangle3D *t = &city->triangles[i];
        Vector3 a = city->vertices[t->vertices[0]], c = city->vertices[t->vertices[1]];
        Vector3 b = city->vertices[t->vertices[2]];
        if (a.y != 0 || b.y != 0 || c.y != 3.5f)
            continue;
        float dx = b.x - a.x, dz = b.z - a.z;
        float length = dx * dx + dz * dz;
        if (length < .001f)
            continue;
        float u = fmaxf(0, fminf(1, ((p.x - a.x) * dx + (p.z - a.z) * dz) / length));
        if (hypotf(p.x - a.x - u * dx, p.z - a.z - u * dz) <= .5f)
            return true;
    }
    return false;
}

static int owners(const WaterfrontScenery *river)
{
    CHECK(box_digest(river) == UINT64_C(0xea3fae73bd61a0df) && river->count == 7944);
    CHECK(clear_digest(river) == UINT64_C(0x081bde4a44942f73));
    // A second river owner alive together: equal content, separate storage.
    WaterfrontScenery second = { 0 }, basin = { 0 }, other = { 0 };
    CHECK(waterfront_scenery_init(&second, &waterfront_city_river_map) && second.river);
    CHECK(second.boxes != river->boxes && second.count == river->count);
    CHECK(memcmp(second.boxes, river->boxes, river->count * sizeof(*river->boxes)) == 0);
    // The basin has no scenery and allocates nothing; nothing obstructs it.
    CHECK(waterfront_scenery_init(&basin, NULL) && basin.ready && !basin.river);
    CHECK(!basin.boxes && !basin.bounds && !basin.count);
    CHECK(waterfront_scenery_clear(&basin, waterfront_corner_position(-2.65f, .08f, -5), .1f));
    // Another map derives its quays and frontage but inherits no authored river kit.
    CHECK(waterfront_scenery_init(&other, &test_harbor_map) && other.ready && !other.river);
    CHECK(other.count > 0 && other.count < 400);
    int waterline = 0;
    for (size_t i = 0; i < other.count; ++i) {
        waterline += other.boxes[i].waterline;
        for (int v = 0; v < 8; ++v) {
            Vector3 p = other.boxes[i].vertices[v];
            CHECK(p.x > 95 && p.x < 145 && p.z > -2 && p.z < 32 && p.y > -.4f && p.y < 12);
        }
    }
    CHECK(waterline > 0);
    CHECK(waterfront_scenery_clear(&other, waterfront_corner_position(-2.65f, .08f, -5), .1f));
    // Failures leave the owner zeroed; destroy is idempotent.
    WaterfrontScenery failed = { .count = 9 };
    CHECK(!waterfront_scenery_init(&failed, &empty) && !failed.ready && !failed.boxes);
    CHECK(!failed.count && !waterfront_scenery_clear(&failed, (Vector3) { 0, 100, 0 }, 1));
    waterfront_scenery_destroy(&failed);
    // Destroy one river owner and rebind another between maps; the survivor is unchanged.
    uint64_t other_digest = box_digest(&other);
    size_t other_count = other.count;
    waterfront_scenery_destroy(&second);
    waterfront_scenery_destroy(&second);
    CHECK(!second.ready && !second.boxes);
    CHECK(box_digest(river) == UINT64_C(0xea3fae73bd61a0df));
    const WaterfrontCornerBox *kept = other.boxes;
    CHECK(waterfront_scenery_bind(&other, &test_harbor_map) && other.boxes == kept);
    CHECK(waterfront_scenery_bind(&other, &waterfront_city_river_map) && other.river);
    CHECK(box_digest(&other) == box_digest(river));
    CHECK(waterfront_scenery_bind(&other, &test_harbor_map) && !other.river);
    CHECK(box_digest(&other) == other_digest);
    CHECK(waterfront_scenery_bind(&other, NULL) && !other.count && !other.boxes);
    CHECK(box_digest(river) == UINT64_C(0xea3fae73bd61a0df));
    waterfront_scenery_destroy(&other);
    waterfront_scenery_destroy(&basin);
    printf("Scenery owners: two river owners, basin, %zu-box synthetic harbor (%d waterline), "
           "rebind and destroy ok\n",
        other_count, waterline);
    return 0;
}

int main(void)
{
    WaterfrontScenery river = { 0 };
    if (!waterfront_scenery_init(&river, &waterfront_city_river_map) || !river.river) {
        fprintf(stderr, "Cannot build the river scenery\n");
        return 1;
    }
    size_t count = river.count;
    const WaterfrontCornerBox *boxes = river.boxes;
    if (!boxes || count < 4000 || count > 16384) {
        fprintf(stderr, "Missing or unbounded city dressing: %zu boxes\n", count);
        return 1;
    }
    int waterline = 0;
    int west = 0, east = 0, north = 0, south = 0, emissive = 0;
    for (size_t i = 0; i < count; ++i) {
        Vector3 centre = { 0 };
        for (int v = 0; v < 8; ++v) {
            Vector3 p = boxes[i].vertices[v];
            if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z)
                || p.y < (boxes[i].waterline ? -.36f : 3.49f) || p.y > 12 || fabsf(p.x) > 500
                || p.z < -300 || p.z > 550) {
                fprintf(stderr, "Invalid city dressing vertex: box %zu corner %d\n", i, v);
                return 1;
            }
            if (boxes[i].waterline && p.y < 3.49f && !near_quay(p)) {
                fprintf(stderr, "Waterline fitting detached from quay: box %zu\n", i);
                return 1;
            }
            centre.x += p.x / 8;
            centre.z += p.z / 8;
        }
        waterline += boxes[i].waterline;
        west += centre.x < -100;
        east += centre.x > 100;
        north += centre.z > 250;
        south += centre.z < 0;
        emissive += boxes[i].emissive;
    }
    // Deliberately broad coverage thresholds: a detailed cafe alone cannot pass.
    if (west < 100 || east < 100 || north < 100 || south < 100 || emissive < 200) {
        fprintf(stderr, "Insufficient coverage W/E/N/S/emissive: %d/%d/%d/%d/%d\n", west, east,
            north, south, emissive);
        return 1;
    }
    if (waterline < 100) {
        fprintf(stderr, "Missing waterline fittings: %d boxes\n", waterline);
        return 1;
    }
    Vector3 inside = waterfront_corner_position(-2.65f, .08f, -5);
    if (waterfront_scenery_clear(&river, inside, .1f)
        || !waterfront_scenery_clear(&river, (Vector3) { 0, 100, 0 }, 2)) {
        fprintf(stderr, "City dressing clearance query failed\n");
        return 1;
    }
    if (!waterfront_scenery_bind(&river, &waterfront_city_river_map) || river.boxes != boxes
        || river.count != count) {
        fprintf(stderr, "City dressing changed after initialization\n");
        return 1;
    }
    if (owners(&river) != 0)
        return 1;
    printf("Waterline fittings: %d boxes\n", waterline);
    printf("Static city coverage W/E/N/S: %d/%d/%d/%d; %zu boxes, %d emissive\n", west, east, north,
        south, count, emissive);
    waterfront_scenery_destroy(&river);
    return 0;
}
