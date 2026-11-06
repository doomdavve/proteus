#include "boat_cargo_view.h"
#include "boat_pose.h"
#include "raymath.h"

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
    puts("cargo decal: posed bow, clipped footprint, receiver planes and empty output passed");
    return 0;
}
