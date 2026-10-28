#include "waterfront_upstream.h"

#include "raymath.h"

#include <math.h>

#define UPSTREAM_VESSELS (WATERFRONT_TRAFFIC_ALL - WATERFRONT_TRAFFIC_VESSELS)
#define MAX_POINTS 16
#define MAX_STOPS 3

// A loop corner: the route turns at (x, z) on a circular arc of RADIUS metres.
typedef struct {
    float x, z, radius;
} Waypoint;

typedef struct {
    const Waypoint *points;
    int count;
    int stops[MAX_STOPS], stop_count; // Line pieces (after corner i) whose middles are stops.
    float speed, accel; // Cruise speed (m/s) and the seconds taken to reach it.
    double phase; // Timetable seconds at time zero.
} RouteRecipe;

typedef struct {
    bool arc;
    Vector2 start, direction; // Line.
    Vector2 centre;
    float radius, angle, turn; // Arc: start angle and signed turn (positive anticlockwise).
    float length;
} Piece;

typedef struct {
    Piece pieces[MAX_POINTS * 2];
    int piece_count;
    float total, speed, dwell;
    float stops[MAX_STOPS], legs[MAX_STOPS];
} Route;

// The berths and lanes were fitted to the baked water (A3.5j): every hull stays at least 2 m
// inside it, and the schooner passes the lift span within 12 m of its middle. Stenpiren is
// a shared berth; these authored scenery routes do not avoid one another.
static const Waypoint alvsnabben[] = {
    { 2640, 800, 60 },
    { 2786.5f, 842.3f, 40 },
    { 2844.5f, 906.3f, 24 }, // Stenpiren
    { 2700, 1060, 70 },
    { 2041.8f, 831.8f, 60 },
    { 1898.8f, 766.8f, 60 }, // Lindholmspiren
    { 1600, 560, 150 },
    { 1000, 485, 150 },
    { 473, 418.2f, 60 },
    { 357, 334.2f, 60 }, // Eriksberg
    { 275, 215, 60 },
    { 340, 105, 70 },
    { 900, 330, 150 },
    { 2000, 640, 200 },
};
static const Waypoint frihamnen[] = {
    { 2786.5f, 842.3f, 40 },
    { 2844.5f, 906.3f, 24 }, // Stenpiren
    { 2690, 1200, 60 },
    { 2714, 1346, 40 },
    { 2541.9f, 1420.6f, 35 }, // Frihamnen
    { 2565, 1530, 35 },
    { 2760, 1420, 60 },
    { 2700, 1100, 80 },
    { 2675, 775, 40 },
};
static const Waypoint schooner[] = {
    { 2590, 870, 60 },
    { 2880, 1180, 200 },
    { 3200, 1600, 200 },
    { 3513.1f, 1949.8f, 300 },
    { 3700, 2080, 80 },
    { 3990, 2185, 42 },
    { 3955, 2265, 42 },
    { 3700, 2160, 80 },
    { 3499.7f, 1969.6f, 300 },
    { 3160, 1640, 200 },
    { 2840, 1240, 200 },
    { 2510, 1010, 60 },
};
#define POINTS(list) list, (int)(sizeof(list) / sizeof(list[0]))
static const RouteRecipe recipes[UPSTREAM_VESSELS] = {
    { POINTS(alvsnabben), { 1, 4, 8 }, 3, 6, 20, 0 },
    { POINTS(alvsnabben), { 1, 4, 8 }, 3, 6, 20, WATERFRONT_UPSTREAM_PERIOD / 2 },
    { POINTS(frihamnen), { 0, 3 }, 2, 4.5f, 18, 300 },
    { POINTS(schooner), { 0 }, 0, 0, 0, 0 },
};

static Route routes[UPSTREAM_VESSELS];
static float crossings[2]; // Timetable seconds at which the schooner crosses Hisingsbron.
static int crossing_count;
static BoatModel models[UPSTREAM_VESSELS], span;
static bool routes_ready, models_ready;

static double wrapped(double value, double period)
{
    value = fmod(value, period);
    return value < 0 ? value + period : value;
}

static void build(Route *route, const RouteRecipe *recipe)
{
    *route = (Route) { 0 };
    int n = recipe->count;
    Vector2 enter[MAX_POINTS], leave[MAX_POINTS];
    for (int i = 0; i < n; ++i) {
        const Waypoint *a = &recipe->points[(i + n - 1) % n], *p = &recipe->points[i],
                       *b = &recipe->points[(i + 1) % n];
        Vector2 d0 = Vector2Normalize((Vector2) { p->x - a->x, p->z - a->z });
        Vector2 d1 = Vector2Normalize((Vector2) { b->x - p->x, b->z - p->z });
        float turn = atan2f(d0.x * d1.y - d0.y * d1.x, Vector2DotProduct(d0, d1));
        float t = p->radius * tanf(fabsf(turn) * .5f);
        enter[i] = (Vector2) { p->x - d0.x * t, p->z - d0.y * t };
        leave[i] = (Vector2) { p->x + d1.x * t, p->z + d1.y * t };
        float side = turn > 0 ? 1 : -1;
        Vector2 centre
            = { enter[i].x - d0.y * p->radius * side, enter[i].y + d0.x * p->radius * side };
        route->pieces[route->piece_count++] = (Piece) { .arc = true,
            .centre = centre,
            .radius = p->radius,
            .angle = atan2f(enter[i].y - centre.y, enter[i].x - centre.x),
            .turn = turn,
            .length = p->radius * fabsf(turn) };
        route->pieces[route->piece_count++] = (Piece) { 0 }; // The line after it, below.
    }
    float line_middle[MAX_POINTS], s = 0;
    for (int i = 0; i < n; ++i) {
        Vector2 a = leave[i], b = enter[(i + 1) % n];
        float length = Vector2Distance(a, b);
        route->pieces[2 * i + 1] = (Piece) { .start = a,
            .direction = Vector2Scale(Vector2Subtract(b, a), 1 / length),
            .length = length };
        s += route->pieces[2 * i].length;
        line_middle[i] = s + length * .5f;
        s += length;
    }
    route->total = s;
    if (!recipe->stop_count) {
        route->speed = (float)(route->total / WATERFRONT_UPSTREAM_PERIOD);
        return;
    }
    double moving = 0;
    for (int k = 0; k < recipe->stop_count; ++k)
        route->stops[k] = line_middle[recipe->stops[k]];
    for (int k = 0; k < recipe->stop_count; ++k) {
        float next = route->stops[(k + 1) % recipe->stop_count];
        route->legs[k] = (float)wrapped(next - route->stops[k], route->total);
        if (route->legs[k] <= 0)
            route->legs[k] = route->total;
        moving += route->legs[k] / recipe->speed + recipe->accel;
    }
    route->speed = recipe->speed;
    route->dwell = (float)((WATERFRONT_UPSTREAM_PERIOD - moving) / recipe->stop_count);
}

// Position, unit tangent and curvature (anticlockwise positive) at arc length S.
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

// Arc length and speed along ROUTE at timetable time T: a dwell at each stop, then a smooth
// start (smoothstep speed), a cruise and a mirrored stop.
static float travel(const Route *route, const RouteRecipe *recipe, double t, float *speed)
{
    if (!recipe->stop_count) {
        *speed = route->speed;
        return (float)(t * route->speed);
    }
    for (int k = 0; k < recipe->stop_count; ++k) {
        if (t < route->dwell) {
            *speed = 0;
            return route->stops[k];
        }
        t -= route->dwell;
        float v = recipe->speed, ta = recipe->accel, d = route->legs[k];
        double duration = d / v + ta;
        if (t < duration || k + 1 == recipe->stop_count) {
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

static Vector2 axis(void)
{
    return Vector2Normalize(
        Vector2Subtract(WATERFRONT_HISINGSBRON_SOUTH, WATERFRONT_HISINGSBRON_NORTH));
}

static float across(Vector2 p)
{
    Vector2 u = axis(), d = Vector2Subtract(p, WATERFRONT_HISINGSBRON_NORTH);
    return u.x * d.y - u.y * d.x;
}

static void prepare(void)
{
    if (routes_ready)
        return;
    for (int i = 0; i < UPSTREAM_VESSELS; ++i)
        build(&routes[i], &recipes[i]);
    // Find where the schooner's loop crosses the bridge axis: a sign change of the signed
    // distance, refined by bisection.
    const Route *r = &routes[WATERFRONT_UPSTREAM_SCHOONER];
    Vector2 tangent;
    float curvature, step = 2;
    for (float s = 0; s < r->total && crossing_count < 2; s += step) {
        float a = across(sample(r, s, &tangent, &curvature));
        float b = across(sample(r, s + step, &tangent, &curvature));
        if ((a < 0) == (b < 0))
            continue;
        float low = s, high = s + step;
        for (int i = 0; i < 24; ++i) {
            float mid = (low + high) * .5f;
            if ((across(sample(r, mid, &tangent, &curvature)) < 0) == (a < 0))
                low = mid;
            else
                high = mid;
        }
        crossings[crossing_count++] = (float)wrapped(
            (low + high) * .5 / r->speed - recipes[WATERFRONT_UPSTREAM_SCHOONER].phase,
            WATERFRONT_UPSTREAM_PERIOD);
    }
    routes_ready = true;
}

BoatModelSpec waterfront_upstream_spec(int index)
{
    if (index < 0 || index >= UPSTREAM_VESSELS)
        return (BoatModelSpec) { 0 };
    BoatModelSpec spec = boat_model_preset(BOAT_MODEL_FERRY);
    spec.hull = (Color) { 236, 234, 226, 255 };
    spec.trim = (Color) { 32, 72, 128, 255 };
    if (index < 2) { // Älvsnabben: a 30 m river ferry with a long saloon.
        spec.length = 30;
        spec.beam = 8;
        spec.draft = 1.5f;
        spec.freeboard = 2;
        spec.cabin_length = 16;
        spec.cabin_offset = -1;
    } else if (index == 2) {
        spec.length = 26;
        spec.beam = 7.2f;
        spec.draft = 1.4f;
        spec.freeboard = 1.8f;
        spec.cabin_length = 13;
        spec.trim = (Color) { 24, 110, 120, 255 };
    } else { // A two-masted schooner; the masts are built by hand (see build_schooner).
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
    }
    return spec;
}

static void build_schooner(BoatModel *m)
{
    BoatModelSpec spec = waterfront_upstream_spec(WATERFRONT_UPSTREAM_SCHOONER);
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
        // Boom and gaff with the sail furled along the boom.
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

static void build_span(BoatModel *m)
{
    const Color top = { 96, 100, 108, 255 }, side = { 206, 210, 214, 255 },
                under = { 120, 124, 130, 255 };
    float x = WATERFRONT_LIFT_SPAN_WIDTH * .5f;
    float z = (WATERFRONT_LIFT_SPAN_END - WATERFRONT_LIFT_SPAN_START) * .5f;
    float y0 = WATERFRONT_LIFT_UNDERSIDE, y1 = y0 + WATERFRONT_LIFT_DEPTH;
    boat_model_begin(m);
    boat_model_quad(m, (Vector3) { -x, y1, -z }, (Vector3) { -x, y1, z }, (Vector3) { x, y1, z },
        (Vector3) { x, y1, -z }, top);
    boat_model_quad(m, (Vector3) { -x, y0, -z }, (Vector3) { x, y0, -z }, (Vector3) { x, y0, z },
        (Vector3) { -x, y0, z }, under);
    for (int s = -1; s <= 1; s += 2) {
        float sx = s * x;
        boat_model_quad(m, (Vector3) { sx, y0, z * s }, (Vector3) { sx, y0, -z * s },
            (Vector3) { sx, y1, -z * s }, (Vector3) { sx, y1, z * s }, side);
        float sz = s * z;
        boat_model_quad(m, (Vector3) { -x * s, y0, sz }, (Vector3) { x * s, y0, sz },
            (Vector3) { x * s, y1, sz }, (Vector3) { -x * s, y1, sz }, side);
        // Railings along both edges, and the trusses' lower chords beneath the deck.
        boat_model_beam(m, (Vector3) { sx * .985f, y1 + 1.1f, -z },
            (Vector3) { sx * .985f, y1 + 1.1f, z }, .18f, side);
        for (float k = -z; k <= z + .01f; k += z / 3)
            boat_model_beam(m, (Vector3) { sx * .985f, y1, k },
                (Vector3) { sx * .985f, y1 + 1.1f, k }, .14f, side);
        boat_model_beam(m, (Vector3) { sx * .6f, y0 - .9f, -z + 2 },
            (Vector3) { sx * .6f, y0 - .9f, z - 2 }, .8f, under);
    }
}

const Mesh3D *waterfront_upstream_mesh(int index)
{
    if (index < 0 || index >= UPSTREAM_VESSELS)
        return NULL;
    if (!models_ready) {
        for (int i = 0; i < UPSTREAM_VESSELS; ++i)
            if (i == WATERFRONT_UPSTREAM_SCHOONER)
                build_schooner(&models[i]);
            else
                boat_model_generate(&models[i], waterfront_upstream_spec(i));
        build_span(&span);
        models_ready = true;
    }
    return &models[index].mesh;
}

const Mesh3D *waterfront_upstream_span_mesh(void)
{
    waterfront_upstream_mesh(0);
    return &span.mesh;
}

WaterfrontVesselMotion waterfront_upstream_motion(int index, double time)
{
    WaterfrontVesselMotion out = { 0 };
    if (index < 0 || index >= UPSTREAM_VESSELS || !isfinite(time))
        return out;
    prepare();
    const Route *route = &routes[index];
    const RouteRecipe *recipe = &recipes[index];
    float speed, curvature;
    Vector2 tangent;
    float s
        = travel(route, recipe, wrapped(time + recipe->phase, WATERFRONT_UPSTREAM_PERIOD), &speed);
    Vector2 p = sample(route, s, &tangent, &curvature);
    out.position = (Vector3) { p.x, 0, p.y };
    out.velocity = (Vector3) { tangent.x * speed, 0, tangent.y * speed };
    out.heading = atan2f(-tangent.x, -tangent.y);
    out.yaw_rate = -speed * curvature;
    return out;
}

int waterfront_upstream_stops(int index, Vector2 *stops, int capacity)
{
    if (index < 0 || index >= UPSTREAM_VESSELS)
        return 0;
    prepare();
    Vector2 tangent;
    float curvature;
    for (int k = 0; k < recipes[index].stop_count && k < capacity; ++k)
        stops[k] = sample(&routes[index], routes[index].stops[k], &tangent, &curvature);
    return recipes[index].stop_count;
}

// The span starts rising RAMP + BEFORE seconds ahead of each crossing, is fully up from
// BEFORE seconds ahead until AFTER seconds after it, and is down again RAMP seconds later.
#define LIFT_RAMP 40.0
#define LIFT_BEFORE 50.0
#define LIFT_AFTER 30.0

float waterfront_upstream_lift(double time)
{
    if (!isfinite(time))
        return 0;
    prepare();
    float raised = 0;
    for (int i = 0; i < crossing_count; ++i) {
        double d = wrapped(time - crossings[i] + WATERFRONT_UPSTREAM_PERIOD * .5,
                       WATERFRONT_UPSTREAM_PERIOD)
            - WATERFRONT_UPSTREAM_PERIOD * .5;
        double x = d < -LIFT_BEFORE ? (d + LIFT_BEFORE + LIFT_RAMP) / LIFT_RAMP
            : d > LIFT_AFTER        ? (LIFT_AFTER + LIFT_RAMP - d) / LIFT_RAMP
                                    : 1;
        float f = (float)fmin(fmax(x, 0), 1);
        raised = fmaxf(raised, f * f * (3 - 2 * f));
    }
    return raised * WATERFRONT_LIFT_HEIGHT;
}

Transform3D waterfront_upstream_span_pose(double time)
{
    Vector2 u = axis();
    float middle = (WATERFRONT_LIFT_SPAN_START + WATERFRONT_LIFT_SPAN_END) * .5f;
    Vector2 c = Vector2Add(WATERFRONT_HISINGSBRON_NORTH, Vector2Scale(u, middle));
    return (Transform3D) { .position = { c.x, waterfront_upstream_lift(time), c.y },
        .rotation = boat_model_rotation(MatrixRotateY(atan2f(u.x, u.y))),
        .scale = { 1, 1, 1 } };
}
