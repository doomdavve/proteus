#include "waterfront_route.h"

#include "raymath.h"
#include "waterfront_upstream.h"

#include <math.h>
#include <string.h>

#define UPSTREAM_VESSELS (WATERFRONT_TRAFFIC_ALL - WATERFRONT_TRAFFIC_VESSELS)
#define MAX_PIECES (WATERFRONT_ROUTE_POINT_CAPACITY * 2)
#define LIFT_RAMP 40.0
#define LIFT_BEFORE 50.0
#define LIFT_AFTER 30.0

typedef struct {
    bool arc;
    Vector2 start, direction;
    Vector2 centre;
    float radius, angle, turn;
    float length;
} Piece;

typedef struct {
    Piece pieces[MAX_PIECES];
    int piece_count;
    float total;
    float speed;
    float stops[WATERFRONT_ROUTE_STOPS], legs[WATERFRONT_ROUTE_STOPS],
        dwell[WATERFRONT_ROUTE_STOPS];
    int stop_count;
} Route;

typedef struct {
    unsigned char shape;
    unsigned char recipe;
    bool bridge;
    float speed, accel, phase;
    double period;
    unsigned point_count, stop_count;
    WaterfrontRoutePoint points[WATERFRONT_ROUTE_POINT_CAPACITY];
    struct {
        unsigned char segment;
        float dwell;
    } stops[WATERFRONT_ROUTE_STOPS];
    float origin_x, origin_z, long_radius, short_radius;
    float start_distance, turn_radius;
} CompiledActor;

static const CompiledActor compiled[WATERFRONT_ROUTE_ACTORS] = {
    // Four river craft. The dock circuit's two lanes have 130 m straights and 14 m turns.
    { .shape = WATERFRONT_ROUTE_DOCK_CIRCUIT, .recipe = WATERFRONT_CRAFT_RUNABOUT, .speed = 2.2f,
        .phase = 70, .period = (260 + 28 * (double)PI) / 2.2, .start_distance = 70,
        .turn_radius = 14 },
    { .shape = WATERFRONT_ROUTE_ELLIPSE, .recipe = WATERFRONT_CRAFT_CRUISER, .period = 260,
        .phase = -PI / 2, .origin_x = 50, .origin_z = -290, .long_radius = 210,
        .short_radius = 28 },
    { .shape = WATERFRONT_ROUTE_ELLIPSE, .recipe = WATERFRONT_CRAFT_SAILBOAT, .period = 260,
        .phase = PI / 2, .origin_x = 50, .origin_z = -290, .long_radius = 210,
        .short_radius = 28 },
    { .shape = WATERFRONT_ROUTE_ELLIPSE, .recipe = WATERFRONT_CRAFT_FERRY, .period = 320,
        .phase = -PI / 2, .origin_x = 50, .origin_z = -290, .long_radius = 300,
        .short_radius = 65 },
    // Outer upstream service: two Älvsnabben loops half a timetable apart, one Frihamnen
    // ferry, and the schooner that schedules the Hisingsbron lift.
    { .shape = WATERFRONT_ROUTE_POINTS, .recipe = WATERFRONT_CRAFT_ALVSNABBEN, .speed = 6,
        .accel = 20, .phase = 0, .period = WATERFRONT_UPSTREAM_PERIOD, .point_count = 14,
        .stop_count = 3, .points
        = { { 2640, 800, 60 }, { 2786.5f, 842.3f, 40 }, { 2844.5f, 906.3f, 24 },
            { 2700, 1060, 70 }, { 2041.8f, 831.8f, 60 }, { 1898.8f, 766.8f, 60 },
            { 1600, 560, 150 }, { 1000, 485, 150 }, { 473, 418.2f, 60 }, { 357, 334.2f, 60 },
            { 275, 215, 60 }, { 340, 105, 70 }, { 900, 330, 150 }, { 2000, 640, 200 } },
        .stops = { { 1, -1 }, { 4, -1 }, { 8, -1 } } },
    { .shape = WATERFRONT_ROUTE_POINTS, .recipe = WATERFRONT_CRAFT_ALVSNABBEN, .speed = 6,
        .accel = 20, .phase = WATERFRONT_UPSTREAM_PERIOD / 2, .period = WATERFRONT_UPSTREAM_PERIOD,
        .point_count = 14, .stop_count = 3, .points
        = { { 2640, 800, 60 }, { 2786.5f, 842.3f, 40 }, { 2844.5f, 906.3f, 24 },
            { 2700, 1060, 70 }, { 2041.8f, 831.8f, 60 }, { 1898.8f, 766.8f, 60 },
            { 1600, 560, 150 }, { 1000, 485, 150 }, { 473, 418.2f, 60 }, { 357, 334.2f, 60 },
            { 275, 215, 60 }, { 340, 105, 70 }, { 900, 330, 150 }, { 2000, 640, 200 } },
        .stops = { { 1, -1 }, { 4, -1 }, { 8, -1 } } },
    { .shape = WATERFRONT_ROUTE_POINTS, .recipe = WATERFRONT_CRAFT_FRIHAMNEN, .speed = 4.5f,
        .accel = 18, .phase = 300, .period = WATERFRONT_UPSTREAM_PERIOD, .point_count = 9,
        .stop_count = 2, .points
        = { { 2786.5f, 842.3f, 40 }, { 2844.5f, 906.3f, 24 }, { 2690, 1200, 60 },
            { 2714, 1346, 40 }, { 2541.9f, 1420.6f, 35 }, { 2565, 1530, 35 }, { 2760, 1420, 60 },
            { 2700, 1100, 80 }, { 2675, 775, 40 } },
        .stops = { { 0, -1 }, { 3, -1 } } },
    { .shape = WATERFRONT_ROUTE_POINTS, .recipe = WATERFRONT_CRAFT_SCHOONER, .bridge = true,
        .speed = 0, .accel = 0, .phase = 0, .period = WATERFRONT_UPSTREAM_PERIOD,
        .point_count = 12, .points
        = { { 2590, 870, 60 }, { 2880, 1180, 200 }, { 3200, 1600, 200 },
            { 3513.1f, 1949.8f, 300 }, { 3700, 2080, 80 }, { 3990, 2185, 42 },
            { 3955, 2265, 42 }, { 3700, 2160, 80 }, { 3499.7f, 1969.6f, 300 },
            { 3160, 1640, 200 }, { 2840, 1240, 200 }, { 2510, 1010, 60 } } },
};

static WaterfrontRouteDefinition defaults;
static bool defaults_ready;
static Route built_routes[WATERFRONT_ROUTE_ACTORS];
static struct {
    float time;
    double period;
    unsigned actor;
} crossings[WATERFRONT_ROUTE_ACTORS * 2];
static int crossing_count;
static uint64_t cache_digest;
static bool cache_ready;
static BoatModel models[WATERFRONT_ROUTE_ACTORS];
static uint64_t model_digest;
static bool models_ready;
static BoatModel recipe_models[WATERFRONT_CRAFT_RECIPES];
static bool recipe_models_ready;

static const char *const shape_names[WATERFRONT_ROUTE_SHAPES]
    = { "dock_circuit", "ellipse", "points" };
static const char *const recipe_names[WATERFRONT_CRAFT_RECIPES] = { "runabout", "cruiser",
    "sailboat", "ferry", "alvsnabben", "frihamnen", "schooner", "workboat" };

const char *waterfront_route_shape_name(WaterfrontRouteShape shape)
{
    return shape < WATERFRONT_ROUTE_SHAPES ? shape_names[shape] : "unknown";
}

const char *waterfront_route_recipe_name(WaterfrontCraftRecipe recipe)
{
    return recipe < WATERFRONT_CRAFT_RECIPES ? recipe_names[recipe] : "unknown";
}

bool waterfront_route_recipe_from_name(const char *name, WaterfrontCraftRecipe *recipe)
{
    for (unsigned i = 0; i < WATERFRONT_CRAFT_RECIPES; ++i)
        if (!strcmp(name, recipe_names[i])) {
            *recipe = (WaterfrontCraftRecipe)i;
            return true;
        }
    return false;
}

static void copy_actor(WaterfrontRouteActor *out, const CompiledActor *in)
{
    *out = (WaterfrontRouteActor) { .shape = in->shape, .recipe = in->recipe, .retain = 0xff,
        .bridge = in->bridge, .speed = in->speed, .accel = in->accel, .phase = in->phase,
        .period = in->period, .point_count = in->point_count, .stop_count = in->stop_count,
        .origin_x = in->origin_x, .origin_z = in->origin_z, .long_radius = in->long_radius,
        .short_radius = in->short_radius, .start_distance = in->start_distance,
        .turn_radius = in->turn_radius };
    for (unsigned i = 0; i < in->point_count; ++i)
        out->points[i] = in->points[i];
    for (unsigned i = 0; i < in->stop_count; ++i)
        out->stops[i] = (WaterfrontRouteStop) {
            .segment = in->stops[i].segment, .dwell = in->stops[i].dwell };
}

const WaterfrontRouteDefinition *waterfront_route_defaults(void)
{
    if (!defaults_ready) {
        defaults.actor_count = WATERFRONT_ROUTE_ACTORS;
        static const char *const ids[WATERFRONT_ROUTE_ACTORS]
            = { "river_runabout", "river_cruiser", "river_sailboat", "river_ferry",
                "alvsnabben_a", "alvsnabben_b", "frihamnen", "schooner" };
        for (unsigned i = 0; i < WATERFRONT_ROUTE_ACTORS; ++i) {
            copy_actor(&defaults.actors[i], &compiled[i]);
            strcpy(defaults.actors[i].id, ids[i]);
        }
        defaults_ready = true;
    }
    return &defaults;
}

static double wrapped(double value, double period)
{
    value = fmod(value, period);
    return value < 0 ? value + period : value;
}

static bool finite_in(float value, float low, float high)
{
    return isfinite(value) && value >= low && value <= high;
}

static void fail(
    WaterfrontRouteDiagnostic *d, const char *field, const char *message)
{
    if (!d)
        return;
    size_t n = 0;
    for (; field[n] && n + 1 < sizeof(d->field); ++n)
        d->field[n] = field[n];
    d->field[n] = 0;
    n = 0;
    for (; message[n] && n + 1 < sizeof(d->message); ++n)
        d->message[n] = message[n];
    d->message[n] = 0;
}

// Build "PREFIX[INDEX]SUFFIX" without stdio, which core must not link.
static void indexed_path(
    char *out, size_t capacity, const char *prefix, unsigned index, const char *suffix)
{
    size_t n = 0;
    for (const char *p = prefix; *p && n + 1 < capacity; ++p)
        out[n++] = *p;
    if (n + 1 < capacity)
        out[n++] = '[';
    char digits[10];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + index % 10);
        index /= 10;
    } while (index && count < sizeof(digits));
    while (count && n + 1 < capacity)
        out[n++] = digits[--count];
    if (n + 1 < capacity)
        out[n++] = ']';
    for (const char *p = suffix; *p && n + 1 < capacity; ++p)
        out[n++] = *p;
    out[n] = 0;
}

static bool identifier(const char *text)
{
    if (!(*text >= 'a' && *text <= 'z'))
        return false;
    for (; *text; ++text)
        if (!((*text >= 'a' && *text <= 'z') || (*text >= '0' && *text <= '9') || *text == '_'))
            return false;
    return true;
}

static bool build(Route *route, const WaterfrontRouteActor *actor,
    WaterfrontRouteDiagnostic *diagnostic)
{
    *route = (Route) { 0 };
    int n = (int)actor->point_count;
    Vector2 enter[WATERFRONT_ROUTE_POINT_CAPACITY], leave[WATERFRONT_ROUTE_POINT_CAPACITY];
    for (int i = 0; i < n; ++i) {
        const WaterfrontRoutePoint *a = &actor->points[(i + n - 1) % n],
                                   *p = &actor->points[i],
                                   *b = &actor->points[(i + 1) % n];
        Vector2 to_p = { p->x - a->x, p->z - a->z }, to_b = { b->x - p->x, b->z - p->z };
        float before = Vector2Length(to_p), after = Vector2Length(to_b);
        if (before < .01f || after < .01f) {
            fail(diagnostic, "route.points", "has a zero-length segment");
            return false;
        }
        Vector2 d0 = Vector2Scale(to_p, 1 / before), d1 = Vector2Scale(to_b, 1 / after);
        float turn = atan2f(d0.x * d1.y - d0.y * d1.x, Vector2DotProduct(d0, d1));
        if (fabsf(turn) > PI - .01f) {
            fail(diagnostic, "route.points", "reverses direction with no finite corner arc");
            return false;
        }
        float t = p->radius * tanf(fabsf(turn) * .5f);
        if (!isfinite(t) || t > before + .01f || t > after + .01f) {
            fail(diagnostic, "route.points", "corner radius is too large for its segments");
            return false;
        }
        enter[i] = (Vector2) { p->x - d0.x * t, p->z - d0.y * t };
        leave[i] = (Vector2) { p->x + d1.x * t, p->z + d1.y * t };
        float side = turn > 0 ? 1 : -1;
        Vector2 centre
            = { enter[i].x - d0.y * p->radius * side, enter[i].y + d0.x * p->radius * side };
        route->pieces[route->piece_count++].arc = true;
        route->pieces[route->piece_count - 1] = (Piece) { .arc = true, .centre = centre,
            .radius = p->radius,
            .angle = atan2f(enter[i].y - centre.y, enter[i].x - centre.x),
            .turn = turn, .length = p->radius * fabsf(turn) };
        route->pieces[route->piece_count++].arc = false; // The line after it, below.
    }
    float line_middle[WATERFRONT_ROUTE_POINT_CAPACITY], s = 0;
    for (int i = 0; i < n; ++i) {
        Vector2 a = leave[i], b = enter[(i + 1) % n];
        float length = Vector2Distance(a, b);
        if (length < .01f) {
            fail(diagnostic, "route.points", "has no straight piece between corners");
            return false;
        }
        route->pieces[2 * i + 1] = (Piece) { .start = a,
            .direction = Vector2Scale(Vector2Subtract(b, a), 1 / length), .length = length };
        s += route->pieces[2 * i].length;
        line_middle[i] = s + length * .5f;
        s += length;
    }
    route->total = s;
    route->stop_count = (int)actor->stop_count;
    if (!route->stop_count) {
        route->speed = actor->speed > 0 ? actor->speed : (float)(route->total / actor->period);
        if (actor->speed > 0 && fabs(route->total / actor->speed - actor->period) > .02) {
            fail(diagnostic, "route.period", "does not match the loop length and speed");
            return false;
        }
        if (route->speed <= 0) {
            fail(diagnostic, "route.speed", "must be positive");
            return false;
        }
        return true;
    }
    double moving = 0;
    for (int k = 0; k < route->stop_count; ++k) {
        unsigned segment = actor->stops[k].segment;
        if (segment >= actor->point_count) {
            fail(diagnostic, "route.stops", "names a segment outside the route");
            return false;
        }
        route->stops[k] = line_middle[segment];
    }
    for (int k = 0; k < route->stop_count; ++k) {
        float next = route->stops[(k + 1) % route->stop_count];
        route->legs[k] = (float)wrapped(next - route->stops[k], route->total);
        if (route->legs[k] <= 0)
            route->legs[k] = route->total;
        moving += route->legs[k] / actor->speed + actor->accel;
    }
    route->speed = actor->speed;
    if (route->speed <= 0) {
        fail(diagnostic, "route.speed", "must be positive when the route stops");
        return false;
    }
    bool automatic = actor->stops[0].dwell < 0;
    for (int k = 0; k < route->stop_count; ++k) {
        if ((actor->stops[k].dwell < 0) != automatic) {
            fail(diagnostic, "route.stops", "must all set dwell or all leave it automatic");
            return false;
        }
        route->dwell[k] = automatic ? (float)((actor->period - moving) / route->stop_count)
                                    : actor->stops[k].dwell;
        if (route->dwell[k] < 0 || !isfinite(route->dwell[k])) {
            fail(diagnostic, "route.period", "is too short for the route's travel time");
            return false;
        }
    }
    if (!automatic) {
        double total = moving;
        for (int k = 0; k < route->stop_count; ++k)
            total += route->dwell[k];
        if (fabs(total - actor->period) > .02) {
            fail(diagnostic, "route.period", "must equal travel time plus dwell times");
            return false;
        }
    }
    return true;
}

static const char *const preset_names[] = { "river_runabout", "river_cruiser",
    "river_sailboat", "river_ferry", "alvsnabben", "frihamnen", "schooner", "marina_pass" };

unsigned waterfront_route_preset_count(void)
{
    return (unsigned)(sizeof(preset_names) / sizeof(preset_names[0]));
}

const char *waterfront_route_preset_name(unsigned index)
{
    return index < waterfront_route_preset_count() ? preset_names[index] : NULL;
}

bool waterfront_route_preset(const char *id, WaterfrontRouteActor *actor)
{
    if (!id || !actor)
        return false;
    for (unsigned i = 0; i < waterfront_route_preset_count(); ++i)
        if (!strcmp(id, preset_names[i]))
            break;
    if (!strcmp(id, "marina_pass")) {
        *actor = (WaterfrontRouteActor) { .shape = WATERFRONT_ROUTE_POINTS,
            .recipe = WATERFRONT_CRAFT_RUNABOUT, .retain = 0xff, .speed = 0, .period = 1,
            .point_count = 4, .points = { { -20, 184, 5 }, { 20, 184, 5 }, { 20, 198, 5 },
                { -20, 198, 5 } } };
        strcpy(actor->id, "marina_pass");
        Route route;
        if (!build(&route, actor, NULL))
            return false;
        actor->speed = 4;
        actor->period = route.total / actor->speed;
        return true;
    }
    const WaterfrontRouteDefinition *def = waterfront_route_defaults();
    for (unsigned i = 0; i < WATERFRONT_ROUTE_ACTORS; ++i)
        if (!strcmp(id, def->actors[i].id)) {
            *actor = def->actors[i];
            return true;
        }
    // Friendly compiled aliases used by discovery and package examples.
    if (!strcmp(id, "alvsnabben")) {
        *actor = def->actors[4];
        strcpy(actor->id, "alvsnabben");
        return true;
    }
    return false;
}

static Vector2 sample(const Route *route, float s, Vector2 *tangent, float *curvature)
{
    s = (float)wrapped(s, route->total);
    for (int i = 0; i < route->piece_count; ++i) {
        const Piece *p = &route->pieces[i];
        if (s > p->length && i + 1 < route->piece_count) {
            s -= p->length;
            continue;
        }
        if (!p->arc) {
            *tangent = p->direction;
            *curvature = 0;
            return Vector2Add(p->start, Vector2Scale(p->direction, fminf(s, p->length)));
        }
        float side = p->turn > 0 ? 1 : -1, angle = p->angle + side * s / p->radius;
        *tangent = (Vector2) { -sinf(angle) * side, cosf(angle) * side };
        *curvature = side / p->radius;
        return (Vector2) { p->centre.x + p->radius * cosf(angle),
            p->centre.y + p->radius * sinf(angle) };
    }
    return (Vector2) { 0 };
}

static float travel(const Route *route, float accel, double t, float *speed)
{
    if (!route->stop_count) {
        *speed = route->speed;
        return (float)(t * route->speed);
    }
    for (int k = 0; k < route->stop_count; ++k) {
        if (t < route->dwell[k]) {
            *speed = 0;
            return route->stops[k];
        }
        t -= route->dwell[k];
        float v = route->speed, ta = accel, d = route->legs[k];
        double duration = d / v + ta;
        if (t < duration || k + 1 == route->stop_count) {
            t = fmin(t, duration);
            float x = (float)(t < ta ? t / ta : t > duration - ta ? (duration - t) / ta : 1);
            float ramp = v * ta * (x * x * x - x * x * x * x * .5f);
            *speed = v * x * x * (3 - 2 * x);
            if (t < ta)
                return route->stops[k] + ramp;
            if (t > duration - ta)
                return route->stops[k] + d - ramp;
            return route->stops[k] + v * ta * .5f + v * (float)(t - ta);
        }
        t -= duration;
    }
    return route->stops[0];
}

static uint64_t fold(uint64_t hash, const void *bytes, size_t size)
{
    const unsigned char *p = bytes;
    for (size_t i = 0; i < size; ++i) {
        hash ^= p[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t fold_float(uint64_t hash, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    unsigned char bytes[4] = { (unsigned char)bits, (unsigned char)(bits >> 8),
        (unsigned char)(bits >> 16), (unsigned char)(bits >> 24) };
    return fold(hash, bytes, sizeof(bytes));
}

uint64_t waterfront_route_digest(const WaterfrontRouteDefinition *routes)
{
    uint64_t hash = 14695981039346656037ull;
    if (!routes)
        return hash;
    hash = fold(hash, &routes->actor_count, sizeof(routes->actor_count));
    for (unsigned i = 0; i < routes->actor_count && i < WATERFRONT_ROUTE_ACTORS; ++i) {
        const WaterfrontRouteActor *a = &routes->actors[i];
        hash = fold(hash, a->id, strlen(a->id) + 1);
        hash = fold(hash, &a->shape, 1);
        hash = fold(hash, &a->recipe, 1);
        hash = fold(hash, &a->retain, 1);
        hash = fold(hash, &a->bridge, 1);
        hash = fold_float(hash, a->speed);
        hash = fold_float(hash, a->accel);
        hash = fold_float(hash, a->phase);
        hash = fold_float(hash, (float)a->period);
        hash = fold(hash, &a->point_count, sizeof(a->point_count));
        hash = fold(hash, &a->stop_count, sizeof(a->stop_count));
        for (unsigned p = 0; p < a->point_count; ++p) {
            hash = fold_float(hash, a->points[p].x);
            hash = fold_float(hash, a->points[p].z);
            hash = fold_float(hash, a->points[p].radius);
        }
        for (unsigned s = 0; s < a->stop_count; ++s) {
            hash = fold(hash, &a->stops[s].segment, 1);
            hash = fold_float(hash, a->stops[s].dwell);
        }
        hash = fold_float(hash, a->origin_x);
        hash = fold_float(hash, a->origin_z);
        hash = fold_float(hash, a->long_radius);
        hash = fold_float(hash, a->short_radius);
        hash = fold_float(hash, a->start_distance);
        hash = fold_float(hash, a->turn_radius);
    }
    return hash;
}

bool waterfront_route_has_content(const WaterfrontRouteDefinition *routes)
{
    return routes && routes->actor_count > 0;
}

bool waterfront_route_has_bridge(const WaterfrontRouteDefinition *routes)
{
    if (!routes)
        return true;
    for (unsigned i = 0; i < routes->actor_count; ++i)
        if (routes->actors[i].bridge)
            return true;
    return false;
}

// Return the compiled actor when ROUTES is NULL, or ROUTES' actor INDEX.
static const WaterfrontRouteActor *actor_at(
    const WaterfrontRouteDefinition *routes, int index, bool *compiled_actor)
{
    if (index < 0)
        return NULL;
    if (!routes) {
        if (index >= WATERFRONT_ROUTE_ACTORS)
            return NULL;
        *compiled_actor = true;
        return &waterfront_route_defaults()->actors[index];
    }
    if ((unsigned)index >= routes->actor_count)
        return NULL;
    *compiled_actor = false;
    return &routes->actors[index];
}

static const WaterfrontRouteDefinition *definition(
    const WaterfrontRouteDefinition *routes)
{
    return routes ? routes : waterfront_route_defaults();
}

static void prepare(const WaterfrontRouteDefinition *routes)
{
    const WaterfrontRouteDefinition *def = definition(routes);
    uint64_t digest = waterfront_route_digest(def);
    if (cache_ready && digest == cache_digest)
        return;
    cache_digest = digest;
    cache_ready = true;
    crossing_count = 0;
    unsigned count = def->actor_count;
    if (count > WATERFRONT_ROUTE_ACTORS)
        count = WATERFRONT_ROUTE_ACTORS;
    for (unsigned i = 0; i < count; ++i)
        if (def->actors[i].shape == WATERFRONT_ROUTE_POINTS)
            build(&built_routes[i], &def->actors[i], NULL);
    // Find where each bridge actor's loop crosses the bridge axis: a sign change of the
    // signed distance, refined by bisection.
    Vector2 axis = Vector2Normalize(
        Vector2Subtract(WATERFRONT_HISINGSBRON_SOUTH, WATERFRONT_HISINGSBRON_NORTH));
    for (unsigned i = 0; i < count; ++i) {
        const WaterfrontRouteActor *actor = &def->actors[i];
        if (!actor->bridge || actor->shape != WATERFRONT_ROUTE_POINTS)
            continue;
        const Route *r = &built_routes[i];
        Vector2 tangent;
        float curvature, step = 2;
        int found = 0;
        for (float s = 0; s < r->total && found < 2; s += step) {
            Vector2 a = sample(r, s, &tangent, &curvature);
            Vector2 b = sample(r, s + step, &tangent, &curvature);
            Vector2 da = { a.x - WATERFRONT_HISINGSBRON_NORTH.x,
                a.y - WATERFRONT_HISINGSBRON_NORTH.y };
            Vector2 db = { b.x - WATERFRONT_HISINGSBRON_NORTH.x,
                b.y - WATERFRONT_HISINGSBRON_NORTH.y };
            float across_a = axis.x * da.y - axis.y * da.x;
            float across_b = axis.x * db.y - axis.y * db.x;
            if ((across_a < 0) == (across_b < 0))
                continue;
            float low = s, high = s + step;
            for (int k = 0; k < 24; ++k) {
                float mid = (low + high) * .5f;
                Vector2 p = sample(r, mid, &tangent, &curvature);
                Vector2 d = { p.x - WATERFRONT_HISINGSBRON_NORTH.x,
                    p.y - WATERFRONT_HISINGSBRON_NORTH.y };
                float across = axis.x * d.y - axis.y * d.x;
                if ((across < 0) == (across_a < 0))
                    low = mid;
                else
                    high = mid;
            }
            crossings[crossing_count].time = (float)wrapped(
                (low + high) * .5 / r->speed - actor->phase, actor->period);
            crossings[crossing_count].period = actor->period;
            crossings[crossing_count].actor = i;
            ++crossing_count;
            ++found;
        }
    }
}

static BoatModelSpec recipe_spec(unsigned recipe, bool orange_runabout)
{
    BoatModelSpec spec;
    switch (recipe) {
    case WATERFRONT_CRAFT_RUNABOUT:
        spec = boat_model_preset(BOAT_MODEL_RUNABOUT);
        if (orange_runabout) {
            spec.hull = (Color) { 227, 154, 60, 255 };
            spec.trim = (Color) { 242, 227, 196, 255 };
        }
        return spec;
    case WATERFRONT_CRAFT_CRUISER: return boat_model_preset(BOAT_MODEL_CRUISER);
    case WATERFRONT_CRAFT_SAILBOAT: return boat_model_preset(BOAT_MODEL_SAILBOAT);
    case WATERFRONT_CRAFT_FERRY: return boat_model_preset(BOAT_MODEL_FERRY);
    case WATERFRONT_CRAFT_WORKBOAT: return boat_model_preset(BOAT_MODEL_WORKBOAT);
    case WATERFRONT_CRAFT_ALVSNABBEN:
        spec = boat_model_preset(BOAT_MODEL_FERRY);
        spec.hull = (Color) { 236, 234, 226, 255 };
        spec.trim = (Color) { 32, 72, 128, 255 };
        spec.length = 30;
        spec.beam = 8;
        spec.draft = 1.5f;
        spec.freeboard = 2;
        spec.cabin_length = 16;
        spec.cabin_offset = -1;
        return spec;
    case WATERFRONT_CRAFT_FRIHAMNEN:
        spec = boat_model_preset(BOAT_MODEL_FERRY);
        spec.hull = (Color) { 236, 234, 226, 255 };
        spec.trim = (Color) { 32, 72, 128, 255 };
        spec.length = 26;
        spec.beam = 7.2f;
        spec.draft = 1.4f;
        spec.freeboard = 1.8f;
        spec.cabin_length = 13;
        spec.trim = (Color) { 24, 110, 120, 255 };
        return spec;
    case WATERFRONT_CRAFT_SCHOONER:
        spec = boat_model_preset(BOAT_MODEL_SAILBOAT);
        spec.length = 32;
        spec.beam = 7;
        spec.draft = 1.5f;
        spec.freeboard = 1.8f;
        spec.bow_width = .08f;
        spec.stern_width = .6f;
        spec.cabin_length = 7;
        spec.cabin_height = 1.1f;
        spec.cabin_offset = 2;
        spec.mast_height = 24;
        spec.hull = (Color) { 34, 58, 52, 255 };
        spec.deck = (Color) { 184, 147, 98, 255 };
        spec.trim = (Color) { 232, 224, 204, 255 };
        return spec;
    default: return (BoatModelSpec) { 0 };
    }
}

BoatModelSpec waterfront_route_spec(const WaterfrontRouteDefinition *routes, int index)
{
    bool compiled_actor = false;
    const WaterfrontRouteActor *actor = actor_at(routes, index, &compiled_actor);
    if (!actor)
        return (BoatModelSpec) { 0 };
    return recipe_spec(
        actor->recipe, compiled_actor && actor->recipe == WATERFRONT_CRAFT_RUNABOUT && index == 0);
}

static void build_schooner(BoatModel *m)
{
    BoatModelSpec spec = recipe_spec(WATERFRONT_CRAFT_SCHOONER, false);
    spec.mast_height = 0;
    if (!boat_model_generate(m, spec))
        return;
    const Color wood = { 120, 88, 58, 255 }, sail = { 236, 230, 214, 255 },
                rope = { 60, 56, 50, 255 };
    float l = spec.length, deck = spec.freeboard;
    const float mast_z[2] = { -l * .2f, l * .08f }, mast_top[2] = { 22, 24 };
    const float boom_end[2] = { l * .02f, l * .44f };
    for (int i = 0; i < 2; ++i) {
        Vector3 foot = { 0, deck - .2f, mast_z[i] }, head = { 0, mast_top[i], mast_z[i] };
        boat_model_beam(m, foot, head, .42f, wood);
        Vector3 boom = { 0, deck + 1.5f, mast_z[i] }, end = { 0, deck + 1.9f, boom_end[i] };
        boat_model_beam(m, boom, end, .26f, wood);
        boat_model_beam(m, (Vector3) { 0, deck + 1.95f, (mast_z[i] + boom_end[i]) * .5f },
            (Vector3) { 0, deck + 1.95f, boom_end[i] - .6f }, .7f, sail);
        boat_model_beam(m, (Vector3) { 0, mast_top[i] - 7, mast_z[i] },
            (Vector3) { 0, mast_top[i] - 4.5f, mast_z[i] + l * .2f }, .2f, wood);
        for (int side = -1; side <= 1; side += 2)
            boat_model_beam(m, (Vector3) { 0, mast_top[i] - 1.5f, mast_z[i] },
                (Vector3) { side * spec.beam * .46f, deck, mast_z[i] + 1.5f }, .07f, rope);
    }
    Vector3 bowsprit = { 0, deck + .9f, -l * .64f };
    boat_model_beam(m, (Vector3) { 0, deck + .3f, -l * .44f }, bowsprit, .3f, wood);
    boat_model_beam(m, bowsprit, (Vector3) { 0, mast_top[0] - 1, mast_z[0] }, .07f, rope);
    boat_model_beam(m, (Vector3) { 0, mast_top[0] - 1, mast_z[0] },
        (Vector3) { 0, mast_top[1] - 1, mast_z[1] }, .07f, rope);
    boat_model_beam(m, (Vector3) { 0, mast_top[1] - 1, mast_z[1] },
        (Vector3) { 0, deck + .4f, l * .49f }, .07f, rope);
    if (m->overflow)
        m->mesh.triangle_count = 0;
}

const Mesh3D *waterfront_route_mesh(const WaterfrontRouteDefinition *routes, int index)
{
    bool compiled_actor = false;
    const WaterfrontRouteActor *actor = actor_at(routes, index, &compiled_actor);
    if (!actor)
        return NULL;
    uint64_t digest = waterfront_route_digest(routes);
    if (!models_ready || model_digest != digest) {
        unsigned count = routes ? routes->actor_count : WATERFRONT_ROUTE_ACTORS;
        if (count > WATERFRONT_ROUTE_ACTORS)
            count = WATERFRONT_ROUTE_ACTORS;
        for (unsigned i = 0; i < count; ++i) {
            WaterfrontCraftRecipe recipe
                = routes ? (WaterfrontCraftRecipe)routes->actors[i].recipe
                         : (WaterfrontCraftRecipe)compiled[i].recipe;
            if (recipe == WATERFRONT_CRAFT_SCHOONER)
                build_schooner(&models[i]);
            else
                boat_model_generate(&models[i], waterfront_route_spec(routes, (int)i));
        }
        model_digest = digest;
        models_ready = true;
    }
    return &models[index].mesh;
}

const Mesh3D *waterfront_route_recipe_mesh(WaterfrontCraftRecipe recipe)
{
    if (recipe >= WATERFRONT_CRAFT_RECIPES)
        return NULL;
    if (!recipe_models_ready) {
        for (unsigned i = 0; i < WATERFRONT_CRAFT_RECIPES; ++i)
            if (i == WATERFRONT_CRAFT_SCHOONER)
                build_schooner(&recipe_models[i]);
            else
                boat_model_generate(&recipe_models[i], recipe_spec(i, false));
        recipe_models_ready = true;
    }
    return &recipe_models[recipe].mesh;
}

double waterfront_route_period(const WaterfrontRouteDefinition *routes, int index)
{
    bool compiled_actor = false;
    const WaterfrontRouteActor *actor = actor_at(routes, index, &compiled_actor);
    return actor ? actor->period : 0;
}

WaterfrontVesselMotion waterfront_route_motion(
    const WaterfrontRouteDefinition *routes, int index, double time)
{
    WaterfrontVesselMotion out = { 0 };
    bool compiled_actor = false;
    const WaterfrontRouteActor *actor = actor_at(routes, index, &compiled_actor);
    if (!actor || !isfinite(time))
        return out;
    Vector2 p, v, a = { 0 };
    if (actor->shape == WATERFRONT_ROUTE_DOCK_CIRCUIT) {
        double distance
            = wrapped(time * actor->speed + actor->phase, actor->period * actor->speed);
        float arc = actor->turn_radius * PI;
        if (distance < 130) {
            p = (Vector2) { 2, -45 + (float)distance };
            v = (Vector2) { 0, actor->speed };
        } else if (distance < 130 + arc) {
            float u = ((float)distance - 130) / actor->turn_radius;
            p = (Vector2) { 16 - actor->turn_radius * cosf(u),
                85 + actor->turn_radius * sinf(u) };
            v = (Vector2) { actor->speed * sinf(u), actor->speed * cosf(u) };
            a = (Vector2) { actor->speed * actor->speed / actor->turn_radius * cosf(u),
                -actor->speed * actor->speed / actor->turn_radius * sinf(u) };
        } else if (distance < 260 + arc) {
            p = (Vector2) { 30, 85 - ((float)distance - 130 - arc) };
            v = (Vector2) { 0, -actor->speed };
        } else {
            float u = ((float)distance - 260 - arc) / actor->turn_radius;
            p = (Vector2) { 16 + actor->turn_radius * cosf(u),
                -45 - actor->turn_radius * sinf(u) };
            v = (Vector2) { -actor->speed * sinf(u), -actor->speed * cosf(u) };
            a = (Vector2) { -actor->speed * actor->speed / actor->turn_radius * cosf(u),
                actor->speed * actor->speed / actor->turn_radius * sinf(u) };
        }
    } else if (actor->shape == WATERFRONT_ROUTE_ELLIPSE) {
        static const Vector2 along = { .8f, .6f }, across = { -.6f, .8f };
        double rate = 2 * (double)PI / actor->period;
        float u = (float)wrapped(time * rate + actor->phase, 2 * (double)PI);
        Vector2 local = { actor->long_radius * cosf(u), actor->short_radius * sinf(u) };
        Vector2 first = { -actor->long_radius * sinf(u) * (float)rate,
            actor->short_radius * cosf(u) * (float)rate };
        Vector2 second
            = { -local.x * (float)(rate * rate), -local.y * (float)(rate * rate) };
        p = Vector2Add((Vector2) { actor->origin_x, actor->origin_z },
            Vector2Add(Vector2Scale(along, local.x), Vector2Scale(across, local.y)));
        v = Vector2Add(Vector2Scale(along, first.x), Vector2Scale(across, first.y));
        a = Vector2Add(Vector2Scale(along, second.x), Vector2Scale(across, second.y));
    } else {
        prepare(routes);
        const Route *route = &built_routes[index];
        float speed, curvature;
        Vector2 tangent;
        float s = travel(
            route, actor->accel, wrapped(time + actor->phase, actor->period), &speed);
        p = sample(route, s, &tangent, &curvature);
        out.position = (Vector3) { p.x, 0, p.y };
        out.velocity = (Vector3) { tangent.x * speed, 0, tangent.y * speed };
        out.heading = atan2f(-tangent.x, -tangent.y);
        out.yaw_rate = -speed * curvature;
        return out;
    }
    out.position = (Vector3) { p.x, 0, p.y };
    out.velocity = (Vector3) { v.x, 0, v.y };
    out.heading = atan2f(-v.x, -v.y);
    out.yaw_rate = (v.y * a.x - v.x * a.y) / Vector2LengthSqr(v);
    return out;
}

int waterfront_route_stops(const WaterfrontRouteDefinition *routes, int index, Vector2 *stops,
    int capacity)
{
    bool compiled_actor = false;
    const WaterfrontRouteActor *actor = actor_at(routes, index, &compiled_actor);
    if (!actor || actor->shape != WATERFRONT_ROUTE_POINTS)
        return 0;
    prepare(routes);
    Vector2 tangent;
    float curvature;
    for (unsigned k = 0; k < actor->stop_count && (int)k < capacity; ++k)
        stops[k] = sample(&built_routes[index], built_routes[index].stops[k], &tangent, &curvature);
    return (int)actor->stop_count;
}

float waterfront_route_lift(const WaterfrontRouteDefinition *routes, double time)
{
    if (!isfinite(time))
        return 0;
    prepare(routes);
    float raised = 0;
    for (int i = 0; i < crossing_count; ++i) {
        double period = crossings[i].period;
        double d = wrapped(time - crossings[i].time + period * .5, period) - period * .5;
        double x = d < -LIFT_BEFORE ? (d + LIFT_BEFORE + LIFT_RAMP) / LIFT_RAMP
            : d > LIFT_AFTER        ? (LIFT_AFTER + LIFT_RAMP - d) / LIFT_RAMP
                                    : 1;
        float f = (float)fmin(fmax(x, 0), 1);
        raised = fmaxf(raised, f * f * (3 - 2 * f));
    }
    return raised * WATERFRONT_LIFT_HEIGHT;
}

bool waterfront_route_bridge_window(const WaterfrontRouteDefinition *routes, int index,
    double *entry, double *leave, WaterfrontRouteDiagnostic *diagnostic)
{
    bool compiled_actor = false;
    const WaterfrontRouteActor *actor = actor_at(routes, index, &compiled_actor);
    if (!actor || !actor->bridge) {
        fail(diagnostic, "route.bridge", "must name a tallest bridge vessel");
        return false;
    }
    prepare(routes);
    BoatModelSpec spec = waterfront_route_spec(routes, index);
    float cruise = actor->speed > 0 ? actor->speed : built_routes[index].speed;
    double half = spec.length * .5 / fmax(cruise, .1);
    for (int i = 0; i < crossing_count; ++i)
        if (crossings[i].actor == (unsigned)index) {
            *entry = crossings[i].time - half;
            *leave = crossings[i].time + half;
            return true;
        }
    fail(diagnostic, "route.points", "does not cross the Hisingsbron lift span");
    return false;
}

bool waterfront_route_validate(
    const WaterfrontRouteDefinition *routes, WaterfrontRouteDiagnostic *diagnostic)
{
    if (diagnostic)
        diagnostic->field[0] = diagnostic->message[0] = 0;
    if (!routes)
        return true;
    if (routes->actor_count > WATERFRONT_ROUTE_ACTORS) {
        fail(diagnostic, "scene.traffic.vessels", "exceeds the eight-craft capacity");
        return false;
    }
    for (unsigned i = 0; i < routes->actor_count; ++i) {
        char path[64];
        const WaterfrontRouteActor *a = &routes->actors[i];
        indexed_path(path, sizeof(path), "scene.traffic.vessels", i + 1, ".id");
        if (!a->id[0] || !identifier(a->id)) {
            fail(diagnostic, path, "must be a-z, 0-9 or '_', starting with a letter");
            return false;
        }
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(a->id, routes->actors[j].id)) {
                fail(diagnostic, path, "repeats an existing vessel ID");
                return false;
            }
        indexed_path(path, sizeof(path), "scene.traffic.vessels", i + 1, ".recipe");
        if (a->recipe >= WATERFRONT_CRAFT_RECIPES) {
            fail(diagnostic, path, "is not a registered craft recipe");
            return false;
        }
        indexed_path(path, sizeof(path), "scene.traffic.vessels", i + 1, ".period");
        if (!isfinite((float)a->period) || a->period < WATERFRONT_ROUTE_MIN_PERIOD
            || a->period > WATERFRONT_ROUTE_MAX_PERIOD) {
            fail(diagnostic, path, "must be 5..86400 seconds");
            return false;
        }
        if (!finite_in(a->speed, 0, 40) || !finite_in(a->accel, 0, 120)
            || !finite_in(a->phase, -WATERFRONT_ROUTE_MAX_PERIOD, WATERFRONT_ROUTE_MAX_PERIOD)) {
            fail(diagnostic, "scene.traffic.vessels", "has an invalid speed, acceleration or phase");
            return false;
        }
        if (a->stop_count > WATERFRONT_ROUTE_STOPS) {
            fail(diagnostic, "scene.traffic.vessels", "has more than eight stops");
            return false;
        }
        if (a->shape >= WATERFRONT_ROUTE_SHAPES) {
            fail(diagnostic, "scene.traffic.vessels", "has an unknown route shape");
            return false;
        }
        if (a->shape == WATERFRONT_ROUTE_POINTS) {
            if (a->point_count < 3 || a->point_count > WATERFRONT_ROUTE_POINT_CAPACITY) {
                fail(diagnostic, "scene.traffic.vessels", "needs 3..32 route points");
                return false;
            }
            for (unsigned p = 0; p < a->point_count; ++p) {
                if (!finite_in(a->points[p].x, -WATERFRONT_ROUTE_HALF_EXTENT,
                        WATERFRONT_ROUTE_HALF_EXTENT)
                    || !finite_in(a->points[p].z, -WATERFRONT_ROUTE_HALF_EXTENT,
                        WATERFRONT_ROUTE_HALF_EXTENT)
                    || !finite_in(a->points[p].radius, 0, 5000)) {
                    fail(diagnostic, "scene.traffic.vessels", "has an invalid route point");
                    return false;
                }
            }
            for (unsigned s = 0; s < a->stop_count; ++s) {
                if (a->stops[s].segment >= a->point_count
                    || (s && a->stops[s].segment <= a->stops[s - 1].segment)
                    || (a->stops[s].dwell < -1
                        || (a->stops[s].dwell >= 0 && !isfinite(a->stops[s].dwell)))) {
                    fail(diagnostic, "scene.traffic.vessels", "has invalid or unordered stops");
                    return false;
                }
            }
            if (a->bridge && a->stop_count) {
                fail(diagnostic, "scene.traffic.vessels", "bridge vessels cannot stop");
                return false;
            }
            Route route;
            if (!build(&route, a, diagnostic))
                return false;
        } else if (a->shape == WATERFRONT_ROUTE_ELLIPSE) {
            if (!finite_in(a->origin_x, -WATERFRONT_ROUTE_HALF_EXTENT,
                    WATERFRONT_ROUTE_HALF_EXTENT)
                || !finite_in(a->origin_z, -WATERFRONT_ROUTE_HALF_EXTENT,
                    WATERFRONT_ROUTE_HALF_EXTENT)
                || !finite_in(a->long_radius, 0, 5000) || !finite_in(a->short_radius, 0, 5000)
                || a->stop_count) {
                fail(diagnostic, "scene.traffic.vessels", "has an invalid ellipse route");
                return false;
            }
        } else if (a->shape == WATERFRONT_ROUTE_DOCK_CIRCUIT) {
            float total = 260 + 2 * PI * a->turn_radius;
            if (!finite_in(a->turn_radius, .1f, 5000) || !finite_in(a->start_distance, -1e6f, 1e6f)
                || a->stop_count || a->speed <= 0
                || fabs(total / a->speed - a->period) > .02) {
                fail(diagnostic, "scene.traffic.vessels", "has an invalid dock circuit");
                return false;
            }
        }
    }
    // Bridge dependencies are one group: a tall actor must cross the span, leave the lift
    // enough time to complete every cycle, and not share an occupancy window with another.
    for (unsigned i = 0; i < routes->actor_count; ++i) {
        const WaterfrontRouteActor *a = &routes->actors[i];
        if (!a->bridge)
            continue;
        double entry, leave;
        if (!waterfront_route_bridge_window(routes, (int)i, &entry, &leave, diagnostic))
            return false;
        double minimum = 2 * (LIFT_BEFORE + LIFT_RAMP + LIFT_AFTER + LIFT_RAMP);
        if (a->period < minimum) {
            fail(diagnostic, "scene.traffic.vessels.period",
                "is too short for the bridge lift cycle");
            return false;
        }
        for (unsigned j = i + 1; j < routes->actor_count; ++j)
            if (routes->actors[j].bridge) {
                double other_entry, other_leave;
                if (!waterfront_route_bridge_window(
                        routes, (int)j, &other_entry, &other_leave, diagnostic))
                    return false;
                if (fabs(other_entry - entry) < LIFT_RAMP + LIFT_BEFORE) {
                    fail(diagnostic, "scene.traffic.vessels",
                        "has overlapping bridge passage windows");
                    return false;
                }
            }
    }
    return true;
}
