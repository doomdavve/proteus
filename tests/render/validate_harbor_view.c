#include "render/waterfront_harbor_view.h"

#include "raylib.h"
#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void)
{
    static BoatSceneDefinition scene = { .version = BOAT_SCENE_VERSION,
        .berths_present = true,
        .modules_present = true,
        .entrance_present = true,
        .entrance_width = 8,
        .entrance_z = -30,
        .berth_count = 2,
        .berths
        = { { .id = "small",
                .recipe = BOAT_SCENE_BERTH_RUNABOUT,
                .appearance
                = { .length = 4.5f, .beam = 1.7f, .hull = { 15, 40, 120, 255 }, .name = "SMALL" } },
            { .id = "large",
                .recipe = BOAT_SCENE_BERTH_RUNABOUT,
                .x = 20,
                .appearance = { .length = 7.0f,
                    .beam = 2.6f,
                    .hull = { 240, 150, 40, 255 },
                    .name = "LARGE" } } } };
    WaterfrontHarborFrame frame;
    WaterfrontHarborModels a = { 0 }, b = { 0 }, candidate;
    CHECK(boat_scene_validate(&scene, NULL));
    waterfront_harbor_evaluate_scene(&scene, boat_water_gentle(), 7, NULL, &frame);
    CHECK(frame.specs[0].length == 4.5f && !strcmp(frame.specs[1].name, "LARGE"));
    CHECK(waterfront_harbor_models_prepare(&candidate, &a, &frame));
    waterfront_harbor_models_commit(&a, &candidate);
    CHECK(waterfront_harbor_models_prepare(&candidate, &b, &frame));
    waterfront_harbor_models_commit(&b, &candidate);
    CHECK(a.boats[0] && a.boats[1] && a.boats[0] != a.boats[1] && a.boats[0] != b.boats[0]);
    const Mesh3D *mesh = waterfront_harbor_models_mesh(&b, 0);
    CHECK(mesh->vertices == b.boats[0]->vertices);
    Vector3 vertex = mesh->vertices[0];
    BoatModel *old = a.boats[0];
    unsigned builds = a.builds;
    CHECK(waterfront_harbor_models_prepare(&candidate, &a, &frame));
    waterfront_harbor_models_commit(&a, &candidate);
    CHECK(a.boats[0] == old && a.builds == builds); // No rebuilding unchanged frames.
    frame.specs[0].hull.r++;
    frame.specs[1].length = NAN; // Fail after allocating the first candidate.
    CHECK(!waterfront_harbor_models_prepare(&candidate, &a, &frame));
    CHECK(a.boats[0] == old && a.builds == builds && candidate.count == 0);
    for (unsigned cycle = 0; cycle < 96; ++cycle) {
        waterfront_harbor_evaluate_scene(
            &scene, boat_water_gentle(), cycle & 1 ? 7 : 0, NULL, &frame);
        if (cycle & 1)
            frame.specs[0].hull.r++;
        CHECK(waterfront_harbor_models_prepare(&candidate, &a, &frame));
        waterfront_harbor_models_commit(&a, &candidate);
        CHECK(waterfront_harbor_models_mesh(&a, 0)->vertices == a.boats[0]->vertices);
        CHECK(Vector3Equals(vertex, waterfront_harbor_models_mesh(&b, 0)->vertices[0]));
        frame.berth_count = cycle % 3 == 0 ? 0 : 1;
        CHECK(waterfront_harbor_models_prepare(&candidate, &a, &frame));
        waterfront_harbor_models_commit(&a, &candidate);
        CHECK(!a.boats[1] && (frame.berth_count != 0 || !a.boats[0]));
    }
    waterfront_harbor_models_destroy(&a);
    waterfront_harbor_models_destroy(&a);
    CHECK(b.boats[0] && Vector3Equals(vertex, waterfront_harbor_models_mesh(&b, 0)->vertices[0]));
    // Same resolved dimensions feed contact/camera queries; no recipe-sized invisible hull.
    CHECK(!waterfront_harbor_contact_height_scene(
        &scene, (Vector2) { 1.5f, 0 }, 0, -INFINITY, INFINITY, (BoatWater) { 0 }, 0, NULL));
    CHECK(waterfront_harbor_contact_height_scene(
        &scene, (Vector2) { 21.5f, 0 }, 0, -INFINITY, INFINITY, (BoatWater) { 0 }, 0, NULL));
    CHECK(waterfront_harbor_camera_clear_scene(&scene, (Vector3) { -1.6f, 0, 0 }, 0));
    CHECK(!waterfront_harbor_camera_clear_scene(&scene, (Vector3) { -21.6f, 0, 0 }, 0));
    printf("harbor_view=ok independent=2 cycles=96 retained_bytes=%zu\n",
        waterfront_harbor_models_bytes(&b));
    waterfront_harbor_models_destroy(&b);
    return 0;
}
