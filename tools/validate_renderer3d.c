#include "renderer3d.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "Renderer check failed at line %d: %s\n", __LINE__, #condition);       \
            renderer3d_destroy(&renderer);                                                         \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void)
{
    Renderer3D renderer = { 0 };
    Color pixels[80 * 60] = { 0 }, reference[80 * 60];
    View3D camera = { .position = { 0, 0, 0 },
        .target = { 0, 0, 1 },
        .up = { 0, 1, 0 },
        .vertical_fov = 1.2f,
        .near_plane = 0.1f,
        .far_plane = 20 };
    Vector3 light = { 0, 0, -1 };
    Transform3D boxes[] = {
        { .position = { -0.3f, 0, 3 }, .rotation = { 0.2f, 0.6f, 0 }, .scale = { 2, 1, 2 } },
        { .position = { 0.3f, 0, 3 }, .rotation = { -0.3f, -0.5f, 0 }, .scale = { 1, 2, 2 } },
    };
    Color colors[] = { RED, BLUE };
    // Intersecting objects must produce the same image in either submission order.
    for (int pass = 0; pass < 2; ++pass) {
        memset(pixels, 0, sizeof(pixels));
        CHECK(renderer3d_begin(&renderer, pixels, 80, 60, camera, light));
        for (int j = 0; j < 2; ++j) {
            int i = pass == 0 ? j : 1 - j;
            renderer3d_draw_box(&renderer, boxes[i], colors[i]);
        }
        if (pass == 0)
            memcpy(reference, pixels, sizeof(pixels));
        else
            CHECK(memcmp(reference, pixels, sizeof(pixels)) == 0);
    }
    int red = 0, blue = 0;
    for (int i = 0; i < 80 * 60; ++i) {
        red += pixels[i].r > pixels[i].b;
        blue += pixels[i].b > pixels[i].r;
    }
    CHECK(red > 0 && blue > 0);
    // Two closed boxes: 24 submitted triangles, of which the camera sees at most three faces
    // each, and nothing in front of the camera is clipped away.
    CHECK(renderer.stats.meshes == 2 && renderer.stats.triangles == 24);
    CHECK(renderer.stats.front_triangles > 0 && renderer.stats.front_triangles <= 12);
    CHECK(renderer.stats.rasterized == renderer.stats.front_triangles);

    // The apex lies behind the near plane; clipping must retain the visible portion.
    Vector3 vertices[] = { { -1, -1, 2 }, { 0, 1, -0.5f }, { 1, -1, 2 } };
    Triangle3D triangle = { { 0, 1, 2 }, WHITE };
    Mesh3D mesh = { vertices, &triangle, 1 };
    Transform3D identity = { .scale = { 1, 1, 1 } };
    memset(pixels, 0, sizeof(pixels));
    CHECK(renderer3d_begin(&renderer, pixels, 80, 60, camera, light));
    renderer3d_draw_mesh(&renderer, &mesh, identity);
    int covered = 0;
    for (int i = 0; i < 80 * 60; ++i) {
        CHECK(isfinite(renderer.depth[i]));
        CHECK(renderer.depth[i] >= 0 && renderer.depth[i] <= 1.0f / camera.near_plane + 0.001f);
        covered += renderer.depth[i] > 0;
    }
    CHECK(covered > 0 && covered < 80 * 60);
    // Clipping turns the one triangle into a polygon of several rasterized triangles.
    CHECK(renderer.stats.meshes == 1 && renderer.stats.triangles == 1);
    CHECK(renderer.stats.front_triangles == 1 && renderer.stats.rasterized >= 2);

    // This triangle lies on z = y + 3. At pixel (40,30), perspective gives
    // inverse_z = (1 + 0.5 / focal_length) / 3, independently of barycentric code.
    vertices[1] = (Vector3) { 0, 1, 4 };
    CHECK(renderer3d_begin(&renderer, pixels, 80, 60, camera, light));
    renderer3d_draw_mesh(&renderer, &mesh, identity);
    float expected_depth = (1.0f + 0.5f / renderer.focal_length) / 3.0f;
    CHECK(fabsf(renderer.depth[30 * 80 + 40] - expected_depth) < 0.00001f);

    // Emissive windows retain their color away from the sun, but are still occluded.
    CHECK(renderer3d_begin(&renderer, pixels, 80, 60, camera, (Vector3) { 0, 0, 1 }));
    triangle.color = (Color) { 230, 169, 88, 255 };
    renderer3d_draw_emissive_mesh(&renderer, &mesh, identity);
    CHECK(memcmp(&pixels[30 * 80 + 40], &triangle.color, sizeof(Color)) == 0);
    CHECK(fabsf(renderer.depth[30 * 80 + 40] - expected_depth) < 0.00001f);
    renderer3d_draw_box(
        &renderer, (Transform3D) { .position = { 0, 0, 1 }, .scale = { 1, 1, .2f } }, BLUE);
    Color occluder = pixels[30 * 80 + 40];
    renderer3d_draw_emissive_mesh(&renderer, &mesh, identity);
    CHECK(memcmp(&pixels[30 * 80 + 40], &occluder, sizeof(Color)) == 0);
    triangle.color = WHITE;

    // Fully outside each of the six frustum planes, including behind the camera.
    const Vector3 outside[] = {
        { 0, 0, -10 },
        { 0, 0, 30 },
        { -100, 0, 0 },
        { 100, 0, 0 },
        { 0, -100, 0 },
        { 0, 100, 0 },
    };
    for (size_t i = 0; i < sizeof(outside) / sizeof(outside[0]); ++i) {
        CHECK(renderer3d_begin(&renderer, pixels, 80, 60, camera, light));
        Transform3D transform = identity;
        transform.position = outside[i];
        renderer3d_draw_mesh(&renderer, &mesh, transform);
        for (int j = 0; j < 80 * 60; ++j)
            CHECK(renderer.depth[j] == 0);
    }

    // An emissive indexed face has analytically known world-space texel coordinates.
    // Include negative coordinates and frustum clipping; depth reconstructs the same mapping.
    unsigned char texels[4] = { 0, 1, 2, 3 };
    Color palette[256];
    for (int level = 0; level < 16; ++level)
        for (int texel = 0; texel < 16; ++texel)
            palette[level * 16 + texel]
                = (Color) { (unsigned char)(texel + 1), (unsigned char)level, 0, 255 };
    Material3D material = { texels, palette, 2, 2, 2, true };
    Transform3D wall = { .position = { 0, 0, 3 }, .scale = { 8, 8, 2 } };
    for (int pass = 0; pass < 2; ++pass) {
        float angle = pass == 0 ? 0 : 0.35f;
        wall.rotation.y = angle;
        CHECK(renderer3d_begin(&renderer, pixels, 80, 60, camera, light));
        renderer3d_draw_textured_box(&renderer, wall, &material);
        for (int y = 0; y < 60; ++y) {
            for (int x = 0; x < 80; ++x) {
                float dx = ((float)x + 0.5f - 40) / renderer.focal_length;
                float dy = (30 - (float)y - 0.5f) / renderer.focal_length;
                // Ray/plane intersection with the rotated front face, independent of
                // the renderer's triangle interpolation. This catches affine mapping.
                float z = (3 * cosf(angle) - 1) / (sinf(angle) * dx + cosf(angle));
                int u = ((int)floorf(dx * z * 2) % 2 + 2) % 2;
                int v = ((int)floorf(-dy * z * 2) % 2 + 2) % 2;
                CHECK(pixels[y * 80 + x].r == texels[v * 2 + u] + 1);
                CHECK(pixels[y * 80 + x].g == 15);
            }
        }
    }
    wall.rotation.y = 0;
    material.emissive = false;
    CHECK(renderer3d_begin(&renderer, pixels, 80, 60, camera, light));
    renderer3d_draw_textured_box(&renderer, wall, &material);
    memcpy(reference, pixels, sizeof(pixels));
    renderer.point_light = (PointLight3D) { .position = { 0, 0, 0 }, .radius = 6, .intensity = 2 };
    // Begin must clear point-light state, then the caller sets this frame's light.
    CHECK(renderer3d_begin(&renderer, pixels, 80, 60, camera, light));
    CHECK(renderer.point_light.radius == 0);
    renderer.point_light = (PointLight3D) { .position = { 0, 0, 0 }, .radius = 6, .intensity = 2 };
    renderer3d_draw_textured_box(&renderer, wall, &material);
    CHECK(pixels[30 * 80 + 40].g > reference[30 * 80 + 40].g);
    CHECK(pixels[30 * 80 + 40].g > pixels[0].g);
    for (int i = 0; i < 80 * 60; ++i) {
        CHECK(pixels[i].r == reference[i].r);
        CHECK(pixels[i].g < 16);
    }
    // A light behind the surface contributes nothing (normal-facing test).
    CHECK(renderer3d_begin(&renderer, pixels, 80, 60, camera, light));
    renderer.point_light = (PointLight3D) { .position = { 0, 0, 6 }, .radius = 10, .intensity = 2 };
    renderer3d_draw_textured_box(&renderer, wall, &material);
    CHECK(memcmp(reference, pixels, sizeof(pixels)) == 0);

    // Moving camera and object together preserves projection and world-space lighting.
    CHECK(renderer3d_begin(&renderer, pixels, 80, 60, camera, light));
    memset(pixels, 0, sizeof(pixels));
    renderer3d_draw_box(&renderer, boxes[0], WHITE);
    memcpy(reference, pixels, sizeof(pixels));
    camera.position.x += 4;
    camera.target.x += 4;
    boxes[0].position.x += 4;
    CHECK(renderer3d_begin(&renderer, pixels, 80, 60, camera, light));
    memset(pixels, 0, sizeof(pixels));
    renderer3d_draw_box(&renderer, boxes[0], WHITE);
    CHECK(memcmp(reference, pixels, sizeof(pixels)) == 0);

    // Resizing reuses capacity but resets the entire active depth buffer.
    CHECK(renderer3d_begin(&renderer, pixels, 40, 30, camera, light));
    CHECK(renderer3d_begin(&renderer, pixels, 80, 60, camera, light));
    for (int i = 0; i < 80 * 60; ++i)
        CHECK(renderer.depth[i] == 0);
    camera.near_plane = 0;
    CHECK(!renderer3d_begin(&renderer, pixels, 80, 60, camera, light));
    renderer3d_destroy(&renderer);
    CHECK(renderer.depth == NULL && renderer.capacity == 0);
    puts("3D renderer validation passed");
    return 0;
}
