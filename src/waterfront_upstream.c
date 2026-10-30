#include "waterfront_upstream.h"

#include "raymath.h"
#include "waterfront_route.h"

#include <math.h>

#define UPSTREAM_VESSELS (WATERFRONT_TRAFFIC_ALL - WATERFRONT_TRAFFIC_VESSELS)

static BoatModel span;
static bool span_ready;

BoatModelSpec waterfront_upstream_spec(int index)
{
    if (index < 0 || index >= UPSTREAM_VESSELS)
        return (BoatModelSpec) { 0 };
    return waterfront_route_spec(NULL, WATERFRONT_TRAFFIC_VESSELS + index);
}

const Mesh3D *waterfront_upstream_mesh(int index)
{
    if (index < 0 || index >= UPSTREAM_VESSELS)
        return NULL;
    return waterfront_route_mesh(NULL, WATERFRONT_TRAFFIC_VESSELS + index);
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

const Mesh3D *waterfront_upstream_span_mesh(void)
{
    if (!span_ready) {
        build_span(&span);
        span_ready = true;
    }
    return &span.mesh;
}

WaterfrontVesselMotion waterfront_upstream_motion(int index, double time)
{
    if (index < 0 || index >= UPSTREAM_VESSELS)
        return (WaterfrontVesselMotion) { 0 };
    return waterfront_route_motion(NULL, WATERFRONT_TRAFFIC_VESSELS + index, time);
}

int waterfront_upstream_stops(int index, Vector2 *stops, int capacity)
{
    if (index < 0 || index >= UPSTREAM_VESSELS)
        return 0;
    return waterfront_route_stops(NULL, WATERFRONT_TRAFFIC_VESSELS + index, stops, capacity);
}

float waterfront_upstream_lift(double time)
{
    return waterfront_route_lift(NULL, time);
}

Transform3D waterfront_upstream_span_pose(double time)
{
    Vector2 u = Vector2Normalize(
        Vector2Subtract(WATERFRONT_HISINGSBRON_SOUTH, WATERFRONT_HISINGSBRON_NORTH));
    float middle = (WATERFRONT_LIFT_SPAN_START + WATERFRONT_LIFT_SPAN_END) * .5f;
    Vector2 c = Vector2Add(WATERFRONT_HISINGSBRON_NORTH, Vector2Scale(u, middle));
    return (Transform3D) { .position = { c.x, waterfront_upstream_lift(time), c.y },
        .rotation = boat_model_rotation(MatrixRotateY(atan2f(u.x, u.y))),
        .scale = { 1, 1, 1 } };
}
