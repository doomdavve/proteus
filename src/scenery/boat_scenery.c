#include "scenery/boat_scenery.h"
#include "core/waterfront_city.h"
#include "core/waterfront_outer.h"
#include "scenery/boat_ambient.h"
#include "scenery/boat_water_atlas.h"
#include "scenery/building_windows.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// raylib 6.0 builds its DEFLATE decoder (external/sinfl.h) into rcore.c. DecompressData would
// allocate and wipe a 64 MB scratch buffer per call; the pack states its exact size instead.
extern int sinflate(void *out, int cap, const void *in, int size);

enum {
    PACK_MAGIC = 0x4E435350,
    PACK_VERSION_1 = 1,
    PACK_VERSION_3 = 3, // the record block stored raw
    PACK_VERSION = 4, // the record block deflated: u32 raw size, u32 crc32, one DEFLATE stream
    PACK_HEADER = 32
};
enum {
    BUILDING_MAGIC = 0x444C4250,
    BUILDING_HEAD = 20,
    BUILDING_SIZE = 56,
    RUN_SIZE = 12,
    FACADE_SIZE = 12,
    STYLE_SIZE = 36
};

typedef struct {
    const BoatMapDescription *map; // The compiled description it belongs to; NULL: free slot.
    BoatMapDescription description;
    Mesh3D mesh;
    void *payload; // Vertices, then triangles.
    size_t bytes;
    BoatSceneryBuilding *buildings; // Version 2 records; NULL for version 1.
    BoatSceneryRun *runs;
    BoatSceneryFacade *facades;
    BoatSceneryWindowStyle *styles;
    size_t building_count, run_count, facade_count, style_count;
    unsigned char *roles; // One per triangle when there are records.
} Scenery;

// Every location, plus the Eriksberg river crop (the river kit's layout source).
enum { SLOTS = BOAT_LOCATION_COUNT + 1 };
static Scenery loaded[SLOTS];

static Scenery *find(const BoatMapDescription *map)
{
    for (size_t i = 0; map && i < SLOTS; ++i)
        if (loaded[i].map == map || (loaded[i].map && &loaded[i].description == map))
            return &loaded[i];
    return NULL;
}

const BoatMapDescription *boat_scenery_of(const BoatMapDescription *map)
{
    Scenery *s = find(map);
    return s ? &s->description : NULL;
}

const BoatSceneryBuilding *boat_scenery_buildings(const BoatMapDescription *map, size_t *count)
{
    Scenery *s = find(map);
    if (count)
        *count = s ? s->building_count : 0;
    return s && s->building_count ? s->buildings : NULL;
}

const BoatSceneryRun *boat_scenery_runs(const BoatMapDescription *map, size_t *count)
{
    Scenery *s = find(map);
    if (count)
        *count = s ? s->run_count : 0;
    return s && s->run_count ? s->runs : NULL;
}

const BoatSceneryFacade *boat_scenery_facades(const BoatMapDescription *map, size_t *count)
{
    Scenery *s = find(map);
    if (count)
        *count = s ? s->facade_count : 0;
    return s && s->facade_count ? s->facades : NULL;
}

const BoatSceneryWindowStyle *boat_scenery_window_styles(
    const BoatMapDescription *map, size_t *count)
{
    Scenery *s = find(map);
    if (count)
        *count = s ? s->style_count : 0;
    return s && s->style_count ? s->styles : NULL;
}

const unsigned char *boat_scenery_roles(const BoatMapDescription *map)
{
    Scenery *s = find(map);
    return s ? s->roles : NULL;
}

const BoatMapDescription *boat_scenery_drawn(const BoatMapDescription *map)
{
    const BoatMapDescription *scenery = boat_scenery_of(map);
    if (scenery || !map || !map->mesh)
        return scenery;
    return boat_map_leading_water(map->mesh) < map->mesh->triangle_count ? map : NULL;
}

static uint32_t u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t u16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }

static float f32le(const unsigned char *p)
{
    uint32_t v = u32(p);
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static void scenery_free(Scenery *s)
{
    free(s->payload);
    free(s->buildings);
    free(s->runs);
    free(s->facades);
    free(s->styles);
    free(s->roles);
    *s = (Scenery) { 0 };
}

// A facade's wall pair must be what the generator reads: triangle WALL (a, c, b) standing on its
// base a-b with c above b, and the next triangle (a, d, c), both of role wall.
static bool wall_pair(const Scenery *s, uint32_t wall, float *length, float *height)
{
    const Triangle3D *t = &s->mesh.triangles[wall], *u = &s->mesh.triangles[wall + 1];
    Vector3 a = s->mesh.vertices[t->vertices[0]], c = s->mesh.vertices[t->vertices[1]];
    Vector3 b = s->mesh.vertices[t->vertices[2]];
    if (!boat_scenery_role_is_wall(s->roles[wall]) || s->roles[wall] != s->roles[wall + 1]
        || u->vertices[0] != t->vertices[0] || u->vertices[2] != t->vertices[1] || a.y != b.y
        || c.x != b.x || c.z != b.z || !(c.y > a.y))
        return false;
    *length = hypotf(b.x - a.x, b.z - a.z);
    *height = c.y - a.y;
    return isfinite(*length) && isfinite(*height) && *length > 0;
}

static uint32_t crc32_of(const unsigned char *p, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// Version 4 stores each record array byte-plane first (tools/map_pack.py shuffle_block), which
// deflates to half the size; restore the records in place. False when the head's counts do not
// fit SIZE.
static bool unshuffle_records(unsigned char *p, size_t size)
{
    if (size < BUILDING_HEAD || u32(p) != BUILDING_MAGIC)
        return false;
    uint64_t counts[3] = { u32(p + 4), u32(p + 8), u32(p + 12) };
    const size_t sizes[3] = { BUILDING_SIZE, RUN_SIZE, FACADE_SIZE };
    uint64_t total = BUILDING_HEAD + (uint64_t)u32(p + 16) * STYLE_SIZE;
    for (int i = 0; i < 3; ++i)
        total += counts[i] * sizes[i];
    if (total != size)
        return false;
    unsigned char *copy = malloc(size);
    if (!copy)
        return false;
    size_t at = BUILDING_HEAD;
    for (int i = 0; i < 3; ++i) {
        size_t n = (size_t)counts[i];
        for (size_t row = 0; row < n; ++row)
            for (size_t column = 0; column < sizes[i]; ++column)
                copy[at + row * sizes[i] + column] = p[at + column * n + row];
        at += n * sizes[i];
    }
    memcpy(p + BUILDING_HEAD, copy + BUILDING_HEAD, at - BUILDING_HEAD);
    free(copy);
    return true;
}

// Parse the version 3 record block (exactly SIZE bytes) into S, checking the contract of
// tools/map_pack.py: buildings sorted by first triangle after the leading water, disjoint, ids
// unique, details, runs and facades inside their building, runs ordered and disjoint, facades
// on wall pairs and in grids that fit their walls, finite bounds and styles.
static bool read_buildings(
    Scenery *s, const unsigned char *p, size_t size, uint32_t triangles, uint32_t water)
{
    if (size < BUILDING_HEAD || u32(p) != BUILDING_MAGIC)
        return false;
    uint64_t count = u32(p + 4), runs = u32(p + 8), facades = u32(p + 12), styles = u32(p + 16);
    if ((uint64_t)size
        != BUILDING_HEAD + count * BUILDING_SIZE + runs * RUN_SIZE + facades * FACADE_SIZE
            + styles * STYLE_SIZE)
        return false;
    s->buildings = calloc(count ? count : 1, sizeof(*s->buildings));
    s->runs = calloc(runs ? runs : 1, sizeof(*s->runs));
    s->facades = calloc(facades ? facades : 1, sizeof(*s->facades));
    s->styles = calloc(styles ? styles : 1, sizeof(*s->styles));
    s->roles = calloc(triangles ? triangles : 1, 1);
    if (!s->buildings || !s->runs || !s->facades || !s->styles || !s->roles)
        return false;
    const unsigned char *rp = p + BUILDING_HEAD + count * BUILDING_SIZE;
    for (uint64_t i = 0; i < runs; ++i, rp += RUN_SIZE) {
        uint32_t packed = u32(rp + 8);
        s->runs[i] = (BoatSceneryRun) { u32(rp), u32(rp + 4), (uint8_t)packed,
            (uint8_t)(packed >> 8), (uint16_t)(packed >> 16) };
    }
    const unsigned char *fp = rp;
    const unsigned char *sp = fp + facades * FACADE_SIZE;
    for (uint64_t i = 0; i < styles; ++i, sp += STYLE_SIZE) {
        float *v = &s->styles[i].first_bay;
        for (int k = 0; k < 9; ++k) {
            v[k] = f32le(sp + 4 * k);
            if (!isfinite(v[k]))
                return false;
        }
        const BoatSceneryWindowStyle *w = &s->styles[i];
        if (w->first_bay < 0 || w->first_floor < 0 || w->offset < 0 || !(w->bay_pitch > 0)
            || !(w->floor_pitch > 0) || !(w->ground_width > 0) || !(w->ground_height > 0)
            || !(w->upper_width > 0) || !(w->upper_height > 0))
            return false;
    }
    s->style_count = (size_t)styles;
    uint32_t previous_end = water;
    uint64_t next_run = 0, next_facade = 0;
    for (uint64_t i = 0; i < count; ++i) {
        const unsigned char *bp = p + BUILDING_HEAD + i * BUILDING_SIZE;
        BoatSceneryBuilding *b = &s->buildings[i];
        *b = (BoatSceneryBuilding) { u32(bp), u32(bp + 4), u32(bp + 8), u32(bp + 12), u32(bp + 16),
            u32(bp + 20), u32(bp + 24), u32(bp + 28),
            { f32le(bp + 32), f32le(bp + 36), f32le(bp + 40) },
            { f32le(bp + 44), f32le(bp + 48), f32le(bp + 52) } };
        if (b->first < previous_end || b->end <= b->first || b->end > triangles
            || b->detail_first < b->first || b->detail_first > b->end || b->run_first != next_run
            || b->run_count > runs - next_run || b->facade_first != next_facade
            || b->facade_count > facades - next_facade)
            return false;
        for (int k = 0; k < 3; ++k) {
            float lo = (&b->min.x)[k], hi = (&b->max.x)[k];
            if (!(isfinite(lo) && isfinite(hi) && lo <= hi))
                return false;
        }
        for (uint64_t j = i; j-- > 0;)
            if (s->buildings[j].id_hash == b->id_hash)
                return false;
        memset(s->roles + b->first, BOAT_SCENERY_ROLE_DETAIL, b->end - b->first);
        uint32_t cursor = b->first;
        for (uint32_t j = 0; j < b->run_count; ++j) {
            const BoatSceneryRun *r = &s->runs[b->run_first + j];
            if (r->first < cursor || r->end <= r->first || r->end > b->end || r->role == 0
                || r->role >= BOAT_SCENERY_ROLE_COUNT)
                return false;
            memset(s->roles + r->first, r->role, r->end - r->first);
            cursor = r->end;
        }
        int previous_index = -1;
        for (uint32_t j = 0; j < b->facade_count; ++j) {
            const unsigned char *q = fp + (b->facade_first + j) * FACADE_SIZE;
            BoatSceneryFacade *f = &s->facades[b->facade_first + j];
            *f = (BoatSceneryFacade) { b->id_hash, u32(q), u16(q + 4), u16(q + 6), u16(q + 8),
                q[10], q[11] };
            float length, height;
            if (q[11] >= BUILDING_PROFILE_COUNT || f->wall < b->first || f->wall + 2 > b->end
                || (int)f->index <= previous_index || f->bays == 0 || f->floors == 0
                || f->style >= styles || !wall_pair(s, f->wall, &length, &height))
                return false;
            previous_index = f->index;
            // Every window of the grid lies inside the wall.
            const BoatSceneryWindowStyle *w = &s->styles[f->style];
            float wide = fmaxf(w->ground_width, w->upper_width);
            float tall = f->floors == 1 ? w->ground_height : w->upper_height;
            if (w->first_bay + (float)(f->bays - 1) * w->bay_pitch + wide > length + .01f
                || w->first_floor + (float)(f->floors - 1) * w->floor_pitch + tall > height + .01f)
                return false;
        }
        next_run += b->run_count;
        next_facade += b->facade_count;
        previous_end = b->end;
    }
    s->building_count = (size_t)count;
    s->run_count = (size_t)runs;
    s->facade_count = (size_t)facades;
    return next_run == runs && next_facade == facades;
}

static bool same_vertex(Vector3 a, Vector3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// The pack's leading water must be the compiled map's, triangle for triangle.
static bool water_matches(const Mesh3D *mesh, size_t water, const Mesh3D *compiled)
{
    if (!compiled || compiled->triangle_count != water)
        return false;
    for (size_t i = 0; i < water; ++i) {
        const Triangle3D *a = &mesh->triangles[i], *b = &compiled->triangles[i];
        if (memcmp(&a->color, &b->color, sizeof(a->color)) != 0)
            return false;
        for (int j = 0; j < 3; ++j)
            if (!same_vertex(mesh->vertices[a->vertices[j]], compiled->vertices[b->vertices[j]]))
                return false;
    }
    return true;
}

bool boat_scenery_load_bytes(const BoatMapDescription *map, const void *bytes, size_t size)
{
    if (find(map))
        return true;
    const uint32_t one = 1;
    if (!map || !bytes || size < PACK_HEADER || *(const unsigned char *)&one != 1)
        return false; // The payload is read in place, so the host must be little-endian.
    Scenery *slot = NULL;
    for (size_t i = 0; !slot && i < SLOTS; ++i)
        slot = loaded[i].map ? NULL : &loaded[i];
    if (!slot)
        return false;
    const unsigned char *h = bytes;
    uint32_t vertices = u32(h + 8), triangles = u32(h + 12), water = u32(h + 16);
    uint32_t bridge = u32(h + 20), payload = u32(h + 24), packed = u32(h + 28);
    uint64_t expected
        = (uint64_t)vertices * sizeof(Vector3) + (uint64_t)triangles * sizeof(Triangle3D);
    uint32_t version = u32(h + 4);
    if (u32(h) != PACK_MAGIC
        || (version != PACK_VERSION_1 && version != PACK_VERSION_3 && version != PACK_VERSION)
        || payload != expected || payload > INT32_MAX || packed > size - PACK_HEADER
        || water > bridge || bridge > triangles || !water)
        return false;
    _Static_assert(sizeof(Vector3) == 12 && sizeof(Triangle3D) == 16, "Pack payload layout");
    unsigned char *out = malloc(payload ? payload : 1);
    if (!out)
        return false;
    // Version 1 ends with its stream; the others continue with exactly the record block (version 4:
    // deflated, behind its raw size).
    size_t tail = size - PACK_HEADER - packed;
    if ((version == PACK_VERSION_1 && tail) || (version == PACK_VERSION_3 && tail < BUILDING_HEAD)
        || (version == PACK_VERSION && tail < 8 + 2)
        || sinflate(out, (int)payload, h + PACK_HEADER, (int)packed) != (int)payload) {
        free(out);
        return false;
    }
    Mesh3D mesh = { (const Vector3 *)out,
        (const Triangle3D *)(out + (size_t)vertices * sizeof(Vector3)), triangles };
    bool valid = true;
    for (size_t i = 0; valid && i < triangles; ++i)
        for (int j = 0; j < 3; ++j)
            valid = valid && mesh.triangles[i].vertices[j] < vertices;
    if (!valid || boat_map_leading_water(&mesh) != water
        || !water_matches(&mesh, water, map->mesh)) {
        free(out);
        return false;
    }
    *slot = (Scenery) { .map = map, .mesh = mesh, .payload = out, .bytes = payload };
    if (version == PACK_VERSION_3
        && !read_buildings(slot, h + PACK_HEADER + packed, tail, triangles, water)) {
        scenery_free(slot);
        return false;
    }
    if (version == PACK_VERSION) {
        const unsigned char *block = h + PACK_HEADER + packed;
        uint32_t raw = u32(block);
        // The inflater does not bound corrupt input, so a truncated or damaged stream is refused
        // by its checksum first.
        bool intact = crc32_of(block + 8, tail - 8) == u32(block + 4);
        unsigned char *records
            = intact && raw >= BUILDING_HEAD && raw <= INT32_MAX ? malloc(raw) : NULL;
        bool read = records && sinflate(records, (int)raw, block + 8, (int)(tail - 8)) == (int)raw
            && unshuffle_records(records, raw)
            && read_buildings(slot, records, raw, triangles, water);
        free(records);
        if (!read) {
            scenery_free(slot);
            return false;
        }
    }
    slot->description = (BoatMapDescription) { &slot->mesh, bridge };
    return true;
}

bool boat_scenery_load(const BoatMapDescription *map, const char *path)
{
    if (find(map))
        return true;
    FILE *file = path ? fopen(path, "rb") : NULL;
    if (!file)
        return false;
    unsigned char *bytes = NULL;
    long size = -1;
    if (fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) > 0 && fseek(file, 0, SEEK_SET) == 0
        && (bytes = malloc((size_t)size)) && fread(bytes, 1, (size_t)size, file) != (size_t)size) {
        free(bytes);
        bytes = NULL;
    }
    fclose(file);
    bool ok = bytes && boat_scenery_load_bytes(map, bytes, (size_t)size);
    free(bytes);
    return ok;
}

bool boat_scenery_path(
    const BoatWorldDefinition *location, const char *assets, char *path, size_t capacity)
{
    if (!location || !location->mapped || !assets || !path)
        return false;
    int n = snprintf(path, capacity, "%sgeometry/%s/scenery.bin", assets, location->name);
    return n > 0 && (size_t)n < capacity;
}

bool boat_scenery_load_location(const BoatWorldDefinition *location, const char *assets)
{
    if (location && !location->mapped)
        return true;
    char path[4096];
    if (!boat_scenery_path(location, assets, path, sizeof(path))
        || !boat_scenery_load(location->map, path))
        return false;
    // A map with a kit ground lays the river kit out from the Eriksberg crop's pack.
    if (waterfront_kit_ground(location->map)) {
        int n = snprintf(path, sizeof(path), "%sgeometry/river/scenery.bin", assets);
        if (n <= 0 || (size_t)n >= sizeof(path)
            || !boat_scenery_load(&waterfront_city_river_map, path))
            return false;
    }
    // The water atlas and ambient occlusion are optional: without valid sidecars the shaders
    // keep their fallback and scenery draws unoccluded.
    boat_water_atlas_load_location(location, assets);
    boat_ambient_load_location(location, assets);
    return true;
}

bool boat_scenery_load_all(const char *assets)
{
    for (int i = 0; i < BOAT_LOCATION_COUNT; ++i)
        if (!boat_scenery_load_location(boat_location((BoatLocationId)i), assets))
            return false;
    return true;
}

size_t boat_scenery_bytes(const BoatMapDescription *map)
{
    Scenery *s = find(map);
    return s ? s->bytes : 0;
}

void boat_scenery_unload(void)
{
    boat_ambient_unload();
    boat_water_atlas_unload();
    for (size_t i = 0; i < SLOTS; ++i)
        scenery_free(&loaded[i]);
}
