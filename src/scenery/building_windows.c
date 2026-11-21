#include "scenery/building_windows.h"

#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// crc32 (zlib's, polynomial 0xEDB88320) of TEXT continuing the finished crc CRC, so that
// continue(crc32(id), "/3") equals crc32("<id>/3").
static uint32_t crc32_continue(uint32_t crc, const char *text)
{
    uint32_t c = ~crc;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        c ^= *p;
        for (int k = 0; k < 8; ++k)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

uint32_t building_window_facade_seed(uint32_t id_hash, unsigned facade)
{
    char suffix[16];
    snprintf(suffix, sizeof(suffix), "/%u", facade);
    return crc32_continue(id_hash, suffix);
}

static uint32_t fmix32(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

uint32_t building_window_seed(uint32_t facade_seed, unsigned bay, unsigned floor)
{
    return fmix32(facade_seed + bay * 0x9E3779B1u + floor * 0x85EBCA77u);
}

Color building_window_glass(uint32_t seed)
{
    return seed % 7 < 3 ? (Color) { 138, 185, 204, 255 }
        : seed % 7 == 3 ? (Color) { 181, 211, 221, 255 }
                        : (Color) { 66, 100, 120, 255 };
}

bool building_window_occupied(uint32_t seed)
{
    return ((seed ^ (seed >> 15)) * 2654435761u) >> 29 < 3;
}

typedef struct {
    uint16_t on_base, on_span, off_base, off_span; // Minutes: evening on and off windows.
    uint8_t evening, night, early; // Of 16 occupied windows: evening, all-night and early risers.
} Schedule;

static const Schedule SCHEDULES[BUILDING_PROFILE_COUNT] = {
    [BUILDING_PROFILE_RESIDENTIAL] = { 16 * 60, 240, 22 * 60, 180, 16, 3, 2 },
    [BUILDING_PROFILE_WORKPLACE] = { 7 * 60, 120, 18 * 60, 240, 16, 1, 0 },
    [BUILDING_PROFILE_INDUSTRIAL] = { 7 * 60, 120, 21 * 60, 120, 4, 1, 1 },
    [BUILDING_PROFILE_QUIET] = { 7 * 60, 120, 19 * 60, 120, 4, 0, 0 },
};

// An occupied window with schedule ROLE (0..15), and offsets A and B (0..255), at minute M.
static bool scheduled(const Schedule *s, unsigned role, unsigned a, unsigned b, unsigned m)
{
    unsigned on = s->on_base + s->on_span * a / 256, off = s->off_base + s->off_span * b / 256;
    if (role < s->night)
        return m >= on || m < 300;
    if (role < s->night + s->early && m >= 300 + 120 * a / 256 && m < 480)
        return true;
    if (role >= s->evening)
        return false;
    return (m >= on && m < off) || (off > 1440 && m < off - 1440);
}

bool building_window_lit(unsigned profile, uint32_t seed, int minute)
{
    if (!building_window_occupied(seed))
        return false;
    if (minute < 0)
        return true;
    uint32_t r = fmix32(seed + 0x7F4A7C15u);
    return scheduled(&SCHEDULES[profile < BUILDING_PROFILE_COUNT ? profile : 0], r & 15,
        (r >> 8) & 255, (r >> 16) & 255, (unsigned)minute % 1440);
}

float building_window_lit_share(unsigned profile, int minute)
{
    if (minute < 0)
        return 3.0f / 8;
    const Schedule *s = &SCHEDULES[profile < BUILDING_PROFILE_COUNT ? profile : 0];
    unsigned lit = 0;
    for (unsigned role = 0; role < 16; ++role)
        for (unsigned a = 8; a < 256; a += 16)
            for (unsigned b = 8; b < 256; b += 16)
                lit += scheduled(s, role, a, b, (unsigned)minute % 1440);
    return 3.0f / 8 * (float)lit / (16 * 16 * 16);
}

bool building_wall(const Mesh3D *mesh, const BoatSceneryFacade *facade, BuildingWall *wall)
{
    if (!mesh || !facade || (size_t)facade->wall + 2 > mesh->triangle_count)
        return false;
    const Triangle3D *t = &mesh->triangles[facade->wall], *u = &mesh->triangles[facade->wall + 1];
    Vector3 a = mesh->vertices[t->vertices[0]], c = mesh->vertices[t->vertices[1]];
    Vector3 b = mesh->vertices[t->vertices[2]];
    float length = hypotf(b.x - a.x, b.z - a.z);
    if (u->vertices[0] != t->vertices[0] || u->vertices[2] != t->vertices[1] || a.y != b.y
        || c.x != b.x || c.z != b.z || !(c.y > a.y) || !(length > 0))
        return false;
    Vector3 along = { (b.x - a.x) / length, 0, (b.z - a.z) / length };
    *wall = (BuildingWall) { .origin = a,
        .end = b,
        .along = along,
        .normal = { along.z, 0, -along.x },
        .length = length,
        .top = c.y };
    return true;
}

size_t building_window_count(const BoatSceneryFacade *facade)
{
    return (size_t)facade->bays * facade->floors;
}

static bool window_at(const BoatSceneryFacade *facade, const BoatSceneryWindowStyle *style,
    const BuildingWall *wall, uint32_t facade_seed, unsigned bay, unsigned floor,
    BuildingWindow *window)
{
    if (bay >= facade->bays || floor >= facade->floors)
        return false;
    float width = floor == 0 ? style->ground_width : style->upper_width;
    float height = floor == 0 ? style->ground_height : style->upper_height;
    float u = style->first_bay + (float)bay * style->bay_pitch;
    float y = wall->origin.y + style->first_floor + (float)floor * style->floor_pitch;
    Vector3 p = { wall->origin.x + wall->along.x * u + wall->normal.x * style->offset, y,
        wall->origin.z + wall->along.z * u + wall->normal.z * style->offset };
    Vector3 q = { p.x + wall->along.x * width, y, p.z + wall->along.z * width };
    uint32_t seed = building_window_seed(facade_seed, bay, floor);
    window->corners[0] = p;
    window->corners[1] = (Vector3) { p.x, y + height, p.z };
    window->corners[2] = (Vector3) { q.x, y + height, q.z };
    window->corners[3] = q;
    window->glass = building_window_glass(seed);
    window->occupied = building_window_occupied(seed);
    window->seed = seed;
    window->profile = facade->profile;
    window->bay = bay;
    window->floor = floor;
    return true;
}

bool building_window(const BoatSceneryFacade *facade, const BoatSceneryWindowStyle *style,
    const BuildingWall *wall, unsigned bay, unsigned floor, BuildingWindow *window)
{
    return window_at(facade, style, wall,
        building_window_facade_seed(facade->id_hash, facade->index), bay, floor, window);
}

size_t building_windows(const BoatSceneryFacade *facade, const BoatSceneryWindowStyle *style,
    const BuildingWall *wall, BuildingWindowFn fn, void *user)
{
    uint32_t facade_seed = building_window_facade_seed(facade->id_hash, facade->index);
    size_t count = 0;
    for (unsigned floor = 0; floor < facade->floors; ++floor)
        for (unsigned bay = 0; bay < facade->bays; ++bay) {
            BuildingWindow window;
            if (window_at(facade, style, wall, facade_seed, bay, floor, &window)) {
                if (fn)
                    fn(&window, user);
                ++count;
            }
        }
    return count;
}

// Shares of a window's width and height of the bands drawn on the glass, and their shading
// (kept in step with window_mask in assets/shaders/waterfront_city.fs).
#define BAND_TOP 0.14f
#define BAND_SIDE 0.08f
#define BAND_SILL 0.08f

static Color shade(Color glass, float factor, unsigned char lit)
{
    return (Color) { (unsigned char)(glass.r * factor), (unsigned char)(glass.g * factor),
        (unsigned char)(glass.b * factor), lit };
}

// Point (S, T) of the window in metres along and up from its bottom left corner, D out of the wall.
typedef struct {
    Vector3 origin, along, up, normal;
    float width, height;
} Frame;

static Frame frame_of(const BuildingWindow *window, const BuildingWall *wall)
{
    Frame f = { .origin = window->corners[0],
        .along = wall->along,
        .up = { 0, 1, 0 },
        .normal = wall->normal };
    f.width = Vector3Distance(window->corners[0], window->corners[3]);
    f.height = window->corners[1].y - window->corners[0].y;
    return f;
}

static Vector3 at(const Frame *f, float s, float t, float d)
{
    return (Vector3) { f->origin.x + f->along.x * s + f->normal.x * d, f->origin.y + t,
        f->origin.z + f->along.z * s + f->normal.z * d };
}

// A rectangle (S0..S1 along, T0..T1 up, at depth D) in the faces' order seen from outside.
static void flat(BuildingWindowFace *face, const Frame *f, float s0, float s1, float t0, float t1,
    float d, Color color)
{
    *face = (BuildingWindowFace) {
        { at(f, s0, t0, d), at(f, s0, t1, d), at(f, s1, t1, d), at(f, s1, t0, d) }, color
    };
}

// A face with the given quad whose visible side is toward NORMAL: the corner order is flipped
// when the quad winds the other way.
static void oriented(BuildingWindowFace *face, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3,
    Vector3 normal, Color color)
{
    Vector3 turn = Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(p2, p1));
    if (Vector3DotProduct(turn, normal) >= 0)
        *face = (BuildingWindowFace) { { p0, p1, p2, p3 }, color };
    else
        *face = (BuildingWindowFace) { { p0, p3, p2, p1 }, color };
}

size_t building_window_faces(const BuildingWindow *window, BuildingWindowFace faces[4])
{
    Vector3 along = Vector3Subtract(window->corners[3], window->corners[0]);
    float width = Vector3Length(along);
    float height = window->corners[1].y - window->corners[0].y;
    BuildingWall wall = { .along = Vector3Scale(along, 1 / width),
        .normal = { along.z / width, 0, -along.x / width } };
    Frame f = frame_of(window, &wall);
    Color glass = window->glass;
    unsigned char lit = window->occupied ? 255 : 0;
    float sill = height * BAND_SILL, top = height * (1 - BAND_TOP), side = width * BAND_SIDE;
    flat(&faces[0], &f, 0, width, 0, sill, 0,
        (Color) { (glass.r + 232) / 2, (glass.g + 228) / 2, (glass.b + 218) / 2, 0 });
    flat(&faces[1], &f, 0, width, top, height, 0, shade(glass, .4f, window->occupied ? 77 : 0));
    flat(&faces[2], &f, 0, side, sill, top, 0, shade(glass, .7f, window->occupied ? 153 : 0));
    flat(&faces[3], &f, side, width, sill, top, 0, (Color) { glass.r, glass.g, glass.b, lit });
    return 4;
}

size_t building_window_reveal(
    const BuildingWindow *window, const BuildingWall *wall, BuildingWindowFace faces[7])
{
    const float depth = 0.16f, rim = 0.07f, mullion = 0.04f;
    Frame f = frame_of(window, wall);
    const float w = f.width, h = f.height;
    const Vector3 up = { 0, 1, 0 };
    size_t n = 0;
    // Returns: lintel underside, sill top, jambs.
    oriented(&faces[n++], at(&f, 0, h, 0), at(&f, w, h, 0), at(&f, w, h, depth),
        at(&f, 0, h, depth), Vector3Negate(up), (Color) { 70, 68, 70, 0 });
    oriented(&faces[n++], at(&f, 0, 0, 0), at(&f, w, 0, 0), at(&f, w, 0, depth),
        at(&f, 0, 0, depth), up, (Color) { 226, 222, 212, 0 });
    oriented(&faces[n++], at(&f, 0, 0, 0), at(&f, 0, h, 0), at(&f, 0, h, depth),
        at(&f, 0, 0, depth), wall->along, (Color) { 130, 126, 122, 0 });
    oriented(&faces[n++], at(&f, w, 0, 0), at(&f, w, h, 0), at(&f, w, h, depth),
        at(&f, w, 0, depth), Vector3Negate(wall->along), (Color) { 130, 126, 122, 0 });
    // The rim: a frame on the outer plane over the lintel and under the sill (the jambs' edges
    // read from the returns), so the surround has a front face without 9 quads per window.
    const Color trim = { 232, 228, 218, 0 };
    flat(&faces[n++], &f, -rim, w + rim, h, h + rim, depth, trim);
    flat(&faces[n++], &f, -rim, w + rim, -rim, 0, depth, trim);
    if (window->floor == 0 && w >= 1.8f)
        flat(&faces[n++], &f, w / 2 - mullion, w / 2 + mullion, 0, h, depth * .5f,
            (Color) { 210, 206, 198, 0 });
    return n;
}
