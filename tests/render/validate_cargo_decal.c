#include "core/boat_pose.h"
#include "raylib.h"
#include "raymath.h"
#include "render/boat_cargo_view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "decal check %d: %s\n", __LINE__, #x);                                 \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static bool same_color3(Vector3 a, Vector3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

int main(void)
{
    BoatModel receiver, out;
    boat_model_begin(&receiver);
    // The bow extends beyond the raised loading deck, exactly as in the workboat view.
    boat_model_box(&receiver, (Vector3) { 0, .18f, 0 }, (Vector3) { 2.6f, .85f, 4.6f }, BLUE);
    boat_model_box(&receiver, (Vector3) { 0, .69f, 0 }, (Vector3) { 2.6f, .12f, 3.8f }, BROWN);
    for (int sample = 0; sample < 128; ++sample) {
        Boat b = { .height = .17f * sinf(sample * .17f),
            .pitch = .09f * cosf(sample * .19f),
            .roll = .12f * sinf(sample * .23f) };
        BoatModel posed;
        boat_model_begin(&posed);
        for (size_t t = 0; t < receiver.mesh.triangle_count; ++t) {
            Triangle3D triangle = receiver.triangles[t];
            boat_model_triangle(&posed,
                boat_pose_attachment(&b, receiver.vertices[triangle.vertices[0]]),
                boat_pose_attachment(&b, receiver.vertices[triangle.vertices[1]]),
                boat_pose_attachment(&b, receiver.vertices[triangle.vertices[2]]), triangle.color);
        }
        boat_model_begin(&out);
        Vector3 cargo = { .65f, 1.4f, -2.35f };
        CHECK(boat_cargo_decal(&out, &posed.mesh, cargo, NULL, 0, (Vector2) { 0 }, (Color) { 0 }));
        CHECK(out.mesh.triangle_count > 0 && !out.overflow);
        for (size_t i = 0; i < out.mesh.triangle_count * 3; ++i) {
            Vector3 p = out.vertices[i];
            CHECK(p.x >= cargo.x - .5001f && p.x <= cargo.x + .5001f);
            CHECK(p.z >= cargo.z - .5001f && p.z <= cargo.z + .5001f);
            p.y -= .012f;
            Vector3 local = Vector3Transform(Vector3Subtract(p, boat_pose_hull(&b).position),
                MatrixTranspose(boat_pose_rotation(&b)));
            CHECK(fabsf(local.y - .605f) < .0001f || fabsf(local.y - .75f) < .0001f);
            CHECK(fabsf(local.z) <= 2.3001f && fabsf(local.x) <= 1.3001f);
        }
    }
    // A nonplanar water cell must retain its actual two planes, not a new four-corner quad.
    boat_model_begin(&receiver);
    boat_model_triangle(
        &receiver, (Vector3) { -1, 0, -1 }, (Vector3) { -1, 0, 1 }, (Vector3) { 1, .3f, 1 }, BLUE);
    boat_model_triangle(
        &receiver, (Vector3) { -1, 0, -1 }, (Vector3) { 1, .3f, 1 }, (Vector3) { 1, 0, -1 }, BLUE);
    boat_model_begin(&out);
    CHECK(boat_cargo_decal(
        &out, &receiver.mesh, (Vector3) { 0, 2, 0 }, NULL, 0, (Vector2) { 0 }, (Color) { 0 }));
    CHECK(out.mesh.triangle_count > 0);
    for (size_t i = 0; i < out.mesh.triangle_count * 3; ++i) {
        Vector3 p = out.vertices[i];
        float height = .15f * (fminf(p.x, p.z) + 1);
        CHECK(fabsf(p.y - height - .012f) < .0001f);
    }
    boat_model_begin(&out);
    CHECK(boat_cargo_decal(
        &out, &receiver.mesh, (Vector3) { 5, 2, 5 }, NULL, 0, (Vector2) { 0 }, (Color) { 0 }));
    CHECK(!out.mesh.triangle_count);
    // Geometry above the projector's bottom is not a receiving surface.
    boat_model_begin(&out);
    CHECK(boat_cargo_decal(
        &out, &receiver.mesh, (Vector3) { 0, -2, 0 }, NULL, 0, (Vector2) { 0 }, (Color) { 0 }));
    CHECK(!out.mesh.triangle_count);
    // The work solids' ambient floor is bit-identical by day, when window_light is zero.
    WaterfrontLighting day = waterfront_lighting(WATERFRONT_DAYLIGHT_DAY);
    CHECK(day.window_light == 0);
    WaterfrontLighting day_work = boat_cargo_view_work_lighting(day);
    CHECK(day_work.cpu_ambient == day.cpu_ambient);
    CHECK(day_work.cpu_direct == day.cpu_direct);
    CHECK(day_work.direct_intensity == day.direct_intensity);
    CHECK(same_color3(day_work.cpu_tint, day.cpu_tint));
    CHECK(same_color3(day_work.sun_direction, day.sun_direction));
    CHECK(same_color3(day_work.direct, day.direct));

    // At night the floor follows occupied window light while every other field is preserved.
    WaterfrontLighting night;
    CHECK(waterfront_lighting_hour(0, &night));
    night.window_light = 1;
    WaterfrontLighting night_work = boat_cargo_view_work_lighting(night);
    CHECK(night_work.cpu_ambient == fmaxf(night.cpu_ambient, .8f));
    CHECK(night_work.cpu_ambient >= night.cpu_ambient);
    CHECK(night_work.cpu_direct == night.cpu_direct);
    CHECK(night_work.direct_intensity == night.direct_intensity);
    CHECK(same_color3(night_work.cpu_tint, night.cpu_tint));
    CHECK(same_color3(night_work.sun_direction, night.sun_direction));
    CHECK(same_color3(night_work.direct, night.direct));

    // The floor is continuous and nondecreasing across the window-light ramp.
    float previous = -1;
    for (int sample = 0; sample <= 20; ++sample) {
        WaterfrontLighting light = night;
        light.window_light = sample / 20.0f;
        WaterfrontLighting work = boat_cargo_view_work_lighting(light);
        CHECK(work.cpu_ambient == fmaxf(light.cpu_ambient, .8f * light.window_light));
        CHECK(work.cpu_ambient >= previous);
        CHECK(same_color3(work.cpu_tint, light.cpu_tint));
        previous = work.cpu_ambient;
    }
    // A frame already above the floor keeps its original ambient exactly.
    WaterfrontLighting bright = night;
    bright.cpu_ambient = .9f;
    bright.window_light = .5f;
    CHECK(boat_cargo_view_work_lighting(bright).cpu_ambient == .9f);

    puts("cargo decal: posed bow, clipped footprint, receiver planes and empty output passed");
    return 0;
}
