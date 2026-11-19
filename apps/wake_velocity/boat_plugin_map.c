#include "wake_velocity/boat_plugin_map.h"
#include "core/waterfront_outer.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAP_LIMIT 20000.0
#define STEP_MIN .25
#define STEP_MAX 8.0

static bool number(const char *text, double lo, double hi, float *out)
{
    char *end;
    errno = 0;
    double v = strtod(text, &end);
    if (errno || !text[0] || *end || !isfinite(v) || v < lo || v > hi)
        return false;
    *out = (float)v;
    return true;
}

static bool fail(char *message, unsigned size, const char *text)
{
    snprintf(message, size, "%s", text);
    return false;
}

bool boat_plugin_map_parse(
    int argc, char **argv, BoatPluginMapRequest *r, char *message, unsigned size)
{
    *r = (BoatPluginMapRequest) { 0 };
    if (argc >= 1 && !strcmp(argv[0], "probe")) {
        int n = argc - 1;
        if (n < 2 || n % 2 || n / 2 > BOAT_PLUGIN_MAP_PROBES)
            return fail(message, size, "probe takes 1..24 X Z pairs");
        for (int i = 0; i < n / 2; ++i)
            if (!number(argv[1 + 2 * i], -MAP_LIMIT, MAP_LIMIT, &r->probe[i][0])
                || !number(argv[2 + 2 * i], -MAP_LIMIT, MAP_LIMIT, &r->probe[i][1]))
                return fail(message, size, "probe coordinates must be finite and within 20000 m");
        r->probe_count = (unsigned)(n / 2);
        return true;
    }
    if (argc == 6 && !strcmp(argv[0], "box")) {
        r->grid = true;
        if (!number(argv[1], -MAP_LIMIT, MAP_LIMIT, &r->x0)
            || !number(argv[2], -MAP_LIMIT, MAP_LIMIT, &r->z0)
            || !number(argv[3], -MAP_LIMIT, MAP_LIMIT, &r->x1)
            || !number(argv[4], -MAP_LIMIT, MAP_LIMIT, &r->z1)
            || !number(argv[5], STEP_MIN, STEP_MAX, &r->step))
            return fail(message, size, "box needs finite X0 Z0 X1 Z1 and a step of 0.25..8 m");
        if (r->x0 > r->x1) {
            float t = r->x0;
            r->x0 = r->x1;
            r->x1 = t;
        }
        if (r->z0 > r->z1) {
            float t = r->z0;
            r->z0 = r->z1;
            r->z1 = t;
        }
        double cols = ceil(((double)r->x1 - r->x0) / r->step) + 1;
        double rows = ceil(((double)r->z1 - r->z0) / r->step) + 1;
        if (cols * rows > BOAT_PLUGIN_MAP_CELLS)
            return fail(
                message, size, "box exceeds 4096 cells; use a larger step or a smaller box");
        return true;
    }
    return fail(message, size, "usage: --map probe X Z [X Z ...] | --map box X0 Z0 X1 Z1 STEP");
}

// Oriented footprints of the draft, in simulation metres. Hulls and modules share the validator's
// frame: forward is (sin h, -cos h), beam is (cos h, sin h).
typedef struct {
    Vector2 centre, length, beam;
    float half_length, half_beam;
    char glyph;
    const char *id;
    const char *kind;
} Footprint;

static Footprint footprint(Vector2 centre, float heading, float half_length, float half_beam,
    char glyph, const char *id, const char *kind)
{
    return (Footprint) { centre, { sinf(heading), -cosf(heading) },
        { cosf(heading), sinf(heading) }, half_length, half_beam, glyph, id, kind };
}

static bool inside(const Footprint *f, Vector2 p, float margin)
{
    Vector2 d = { p.x - f->centre.x, p.y - f->centre.y };
    return fabsf(d.x * f->length.x + d.y * f->length.y) <= f->half_length + margin
        && fabsf(d.x * f->beam.x + d.y * f->beam.y) <= f->half_beam + margin;
}

#define FOOTPRINTS (BOAT_SCENE_BERTHS + BOAT_SCENE_MODULES + 1)

static unsigned footprints(const BoatSceneDefinition *scene, Footprint *out)
{
    unsigned n = 0;
    if (!scene)
        return 0;
    for (unsigned i = 0; scene->berths_present && i < scene->berth_count; ++i) {
        const BoatSceneBerth *b = &scene->berths[i];
        BoatModelSpec spec = boat_scene_berth_resolve(b);
        // Empty slots keep their largest-hull size, with a lowercase glyph.
        out[n++] = footprint((Vector2) { b->x, b->z }, b->heading, spec.length * .5f,
            spec.beam * .5f, b->empty ? 'b' : 'B', b->id, b->empty ? "berth_slot" : "berth");
    }
    for (unsigned i = 0; scene->modules_present && i < scene->module_count; ++i) {
        const BoatSceneModule *m = &scene->modules[i];
        if (m->kind == BOAT_SCENE_MODULE_GANGWAY) {
            const BoatSceneModule *t = boat_scene_find_module(scene, m->attaches);
            if (!t)
                continue;
            Vector2 landing = { t->x + t->width * .5f * cosf(t->heading),
                t->z + t->width * .5f * sinf(t->heading) };
            Vector2 d = { m->anchor_x - landing.x, m->anchor_z - landing.y };
            float span = hypotf(d.x, d.y);
            if (span < .01f)
                continue;
            Footprint f = { { (m->anchor_x + landing.x) * .5f, (m->anchor_z + landing.y) * .5f },
                { d.x / span, d.y / span }, { -d.y / span, d.x / span }, span * .5f, m->width * .5f,
                '=', m->id, "gangway" };
            out[n++] = f;
        } else
            out[n++] = footprint((Vector2) { m->x, m->z }, m->heading, m->length * .5f,
                m->width * .5f, m->kind == BOAT_SCENE_MODULE_QUAY ? 'Q' : '+', m->id,
                boat_scene_module_kind_name((BoatSceneModuleKind)m->kind));
    }
    return n;
}

static bool in_entrance(const BoatSceneDefinition *scene, Vector2 p)
{
    return scene && scene->entrance_present
        && hypotf(p.x - scene->entrance_x, p.y - scene->entrance_z) <= scene->entrance_width * .5f;
}

// Highest-priority overlay glyph at P, or 0.
static char overlay(const BoatSceneDefinition *scene, const Footprint *f, unsigned n, Vector2 p)
{
    char best = 0;
    static const char order[] = "=+QBbE"; // earlier wins
    for (unsigned i = 0; i < n; ++i)
        if (inside(&f[i], p, 0) && (!best || strchr(order, f[i].glyph) < strchr(order, best)))
            best = f[i].glyph;
    if (!best && in_entrance(scene, p))
        best = 'E';
    return best;
}

static void json_text(const char *text)
{
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < 32 || *p >= 127)
            printf("\\u%04x", *p);
        else {
            if (*p == '"' || *p == '\\')
                putchar('\\');
            putchar(*p);
        }
    }
    putchar('"');
}

static void print_probe(const BoatMap *map, const BoatSceneDefinition *scene, const Footprint *f,
    unsigned n, WaterfrontGround ground, WaterfrontOverhead over, Vector2 p)
{
    bool water = boat_map_water(map, p, 0);
    printf("{\"x\":%.9g,\"z\":%.9g,\"surface\":\"%s\"", (double)p.x, (double)p.y,
        water ? "water" : "land");
    static const float radii[] = { .5f, 1, 2, 4, 8 };
    float clear = 0;
    for (unsigned i = 0; water && i < sizeof(radii) / sizeof(*radii); ++i)
        if (boat_map_water(map, p, radii[i]))
            clear = radii[i];
        else
            break;
    printf(",\"clear_water_m\":%.9g", (double)clear);
    if (ground && !water)
        printf(",\"ground_m\":%.3f", (double)ground(-p.x, p.y));
    float top = over ? over(-p.x, p.y) : INFINITY;
    if (isfinite(top))
        printf(",\"overhead_m\":%.3f", (double)top);
    printf(",\"occupied_by\":[");
    bool first = true;
    for (unsigned i = 0; i < n; ++i)
        if (inside(&f[i], p, 0)) {
            printf("%s{\"kind\":\"%s\",\"id\":", first ? "" : ",", f[i].kind);
            json_text(f[i].id);
            putchar('}');
            first = false;
        }
    printf("],\"in_entrance\":%s}", in_entrance(scene, p) ? "true" : "false");
}

void boat_plugin_map_print(
    const BoatPluginMapRequest *r, const BoatMap *map, const BoatSceneDefinition *scene)
{
    Footprint f[FOOTPRINTS];
    unsigned n = footprints(scene, f);
    WaterfrontGround ground = waterfront_kit_ground(map->description);
    WaterfrontOverhead over = waterfront_overhead(map->description);
    printf("\"map\":{\"overlay\":%s,", scene ? "true" : "false");
    if (!r->grid) {
        printf("\"probes\":[");
        for (unsigned i = 0; i < r->probe_count; ++i) {
            if (i)
                putchar(',');
            print_probe(
                map, scene, f, n, ground, over, (Vector2) { r->probe[i][0], r->probe[i][1] });
        }
        printf("]}");
        return;
    }
    unsigned cols = (unsigned)ceil(((double)r->x1 - r->x0) / r->step) + 1;
    unsigned rows = (unsigned)ceil(((double)r->z1 - r->z0) / r->step) + 1;
    // North is up and east is right, as on a map: simulation x is minus east, so column 0 is the
    // largest x (west) and row 0 the largest z (north). Cells are sampled at their centres.
    printf("\"grid\":{\"step\":%.9g,\"cols\":%u,\"rows\":%u,\"x_west\":%.9g,\"z_north\":%.9g,"
           "\"axes\":\"row 0 is north (max z); column 0 is west (max x); x = x_west - col*step, "
           "z = z_north - row*step\","
           "\"legend\":{\"~\":\"water\",\"#\":\"land\",\"+\":\"floating module\","
           "\"=\":\"gangway\",\"Q\":\"quay deck\",\"B\":\"moored hull\","
           "\"b\":\"empty berth slot\",\"E\":\"marina entrance\"},\"rows_text\":[",
        (double)r->step, cols, rows, (double)r->x1, (double)r->z1);
    for (unsigned row = 0; row < rows; ++row) {
        if (row)
            putchar(',');
        putchar('"');
        for (unsigned col = 0; col < cols; ++col) {
            Vector2 p = { r->x1 - (float)col * r->step, r->z1 - (float)row * r->step };
            char c = overlay(scene, f, n, p);
            putchar(c ? c : boat_map_water(map, p, 0) ? '~' : '#');
        }
        putchar('"');
    }
    printf("]}}");
}
