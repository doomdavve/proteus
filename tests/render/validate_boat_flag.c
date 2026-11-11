#include "boat_flag.h"
#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "Flag check line %d: %s\n", __LINE__, #c);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int trial(Vector3 velocity, bool turning)
{
    BoatFlag flag = { 0 }, repeat = { 0 };
    Transform3D hull = { .position = { 0, 0, 10 }, .scale = { 1, 1, 1 } };
    Vector3 average = { 0 };
    float motion = 0;
    for (int tick = 0; tick < 1440; ++tick) {
        float t = tick * BOAT_FLAG_STEP;
        hull.position = Vector3Add(hull.position, Vector3Scale(velocity, BOAT_FLAG_STEP));
        if (turning)
            hull.rotation = (Vector3) { .1f * sinf(t), .9f * t, .23f * sinf(t * 1.3f) };
        Vector3 before = flag.surface.points[BOAT_FLAG_COLS - 1];
        boat_flag_step(&flag, hull, velocity, t);
        boat_flag_step(&repeat, hull, velocity, t);
        CHECK(memcmp(&flag, &repeat, sizeof(flag)) == 0);
        for (int y = 0; y < BOAT_FLAG_ROWS; ++y) {
            int root = y * BOAT_FLAG_COLS;
            CHECK(Vector3Distance(flag.surface.points[root], boat_flag_attachment(hull, y))
                < .00001f);
            for (int x = 0; x < BOAT_FLAG_COLS; ++x) {
                Vector3 p = flag.surface.points[root + x];
                CHECK(isfinite(p.x) && isfinite(p.y) && isfinite(p.z));
                CHECK(Vector3Distance(p, flag.surface.points[root]) < BOAT_FLAG_WIDTH + .001f);
            }
        }
        if (tick > 1200) {
            Vector3 tip = flag.surface.points[BOAT_FLAG_COLS - 1];
            average = Vector3Add(average, Vector3Subtract(tip, flag.surface.points[0]));
            motion += Vector3Length(Vector3Subtract(
                Vector3Subtract(tip, before), Vector3Scale(velocity, BOAT_FLAG_STEP)));
        }
    }
    Vector3 air = Vector3Subtract(boat_flag_wind(hull.position, 12), velocity);
    float alignment = Vector3DotProduct(Vector3Normalize((Vector3) { average.x, 0, average.z }),
        Vector3Normalize((Vector3) { air.x, 0, air.z }));
    printf("Flag velocity=(%.0f,%.0f,%.0f) turn=%d alignment=%.3f tip motion=%.3fm mean "
           "tip=(%.3f,%.3f,%.3f)\n",
        velocity.x, velocity.y, velocity.z, turning, alignment, motion, average.x / 239,
        average.y / 239, average.z / 239);
    if (Vector3Length(velocity) > .1f)
        CHECK(alignment > .8f);
    CHECK(motion > .001f); // Gentle movement at rest, sustained flutter under way.
    if (Vector3Length(velocity) < .1f) {
        Vector3 tip = Vector3Scale(average, 1.0f / 239);
        CHECK(tip.y < -.85f * BOAT_FLAG_WIDTH);
        CHECK(hypotf(tip.x, tip.z) < .35f * BOAT_FLAG_WIDTH);
    }
    return 0;
}

static int stopping(void)
{
    BoatFlag flag = { 0 };
    Transform3D hull = { .scale = { 1, 1, 1 } };
    for (int tick = 0; tick < 1200; ++tick) {
        Vector3 velocity = tick < 240 ? (Vector3) { 0, 0, -18 } : (Vector3) { 0 };
        hull.position = Vector3Add(hull.position, Vector3Scale(velocity, BOAT_FLAG_STEP));
        boat_flag_step(&flag, hull, velocity, tick * BOAT_FLAG_STEP);
    }
    Vector3 tip = Vector3Subtract(flag.surface.points[BOAT_FLAG_COLS - 1], flag.surface.points[0]);
    printf("Stopped flag: fly drop=%.3fm, horizontal reach=%.3fm\n", -tip.y, hypotf(tip.x, tip.z));
    CHECK(tip.y < -.85f * BOAT_FLAG_WIDTH);
    CHECK(hypotf(tip.x, tip.z) < .35f * BOAT_FLAG_WIDTH);
    return 0;
}

int main(void)
{
    CHECK(stopping() == 0);
    CHECK(trial((Vector3) { 0 }, false) == 0);
    CHECK(trial((Vector3) { 0, 0, -18 }, false) == 0);
    CHECK(trial((Vector3) { 0, 0, 8 }, false) == 0);
    CHECK(trial((Vector3) { 12, 0, 0 }, true) == 0);
    BoatFlag flag = { 0 };
    Transform3D hull = { .scale = { 1, 1, 1 } };
    boat_flag_step(&flag, hull, (Vector3) { 0 }, 0);
    hull.position = (Vector3) { 1000, 0, -300 };
    boat_flag_step(&flag, hull, (Vector3) { 0 }, 1);
    CHECK(Vector3Distance(flag.surface.points[8], hull.position) < 4); // Reset after teleport.
    Mesh3D mesh = boat_flag_mesh(&flag.surface);
    CHECK(mesh.triangle_count == 160);
    int yellow = 0;
    for (size_t i = 0; i < mesh.triangle_count; i += 2) {
        const Triangle3D *a = &mesh.triangles[i], *b = &mesh.triangles[i + 1];
        for (int j = 0; j < 3; ++j)
            CHECK(a->vertices[j] < BOAT_FLAG_POINTS && b->vertices[j] < BOAT_FLAG_POINTS);
        if (a->color.r == 255)
            yellow += 2;
    }
    CHECK(yellow == 48); // 8 cross cells + 4 additional hoist-band cells, both faces.
    puts("Swedish flag: deterministic cloth, attachments, wind/reverse/turn response and topology "
         "pass.");
    return 0;
}
