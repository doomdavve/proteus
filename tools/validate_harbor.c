#include "boat_map.h"
#include "boat_session.h"
#include "boat_test_world.h"
#include "waterfront_harbor.h"
#include "waterfront_scene.h"

#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "Harbor check failed at %d: %s\n", __LINE__, #test);                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static bool valid_mesh(const Mesh3D *mesh)
{
    if (!mesh || !mesh->triangle_count || mesh->triangle_count > BOAT_MODEL_MAX_TRIANGLES)
        return false;
    for (size_t i = 0; i < mesh->triangle_count; ++i) {
        const Triangle3D *t = &mesh->triangles[i];
        Vector3 v[3];
        for (int j = 0; j < 3; ++j) {
            if (t->vertices[j] >= mesh->triangle_count * 3)
                return false;
            v[j] = mesh->vertices[t->vertices[j]];
            if (!isfinite(v[j].x) || !isfinite(v[j].y) || !isfinite(v[j].z))
                return false;
        }
        if (Vector3LengthSqr(
                Vector3CrossProduct(Vector3Subtract(v[1], v[0]), Vector3Subtract(v[2], v[0])))
            < 1e-10f)
            return false;
    }
    return true;
}

static bool close_vector(Vector3 a, Vector3 b) { return Vector3Distance(a, b) < .0001f; }

static bool player_clear(const Boat *boat)
{
    // Independent conservative bow/stern/beam rectangle; the actual collision solver
    // also checks a margin at these stations rather than testing only the centre.
    for (int end = -1; end <= 1; end += 2)
        for (int side = -1; side <= 1; side += 2) {
            Vector2 p = Vector2Add(boat->position,
                (Vector2) { sinf(boat->heading) * end * 1.6f + cosf(boat->heading) * side * .7f,
                    -cosf(boat->heading) * end * 1.6f + sinf(boat->heading) * side * .7f });
            if (waterfront_harbor_contact(p, 0, NULL))
                return false;
        }
    return true;
}

int main(void)
{
    static BoatModel a, b;
    size_t triangles = 0;
    for (int kind = 0; kind < BOAT_MODEL_KIND_COUNT; ++kind) {
        BoatModelSpec spec = boat_model_preset((BoatModelKind)kind);
        CHECK(boat_model_generate(&a, spec) && boat_model_generate(&b, spec));
        CHECK(valid_mesh(&a.mesh) && !a.overflow);
        CHECK(a.mesh.triangle_count == b.mesh.triangle_count);
        CHECK(!memcmp(a.vertices, b.vertices, a.mesh.triangle_count * 3 * sizeof(Vector3)));
        CHECK(!memcmp(a.triangles, b.triangles, a.mesh.triangle_count * sizeof(Triangle3D)));
        triangles += a.mesh.triangle_count;
        for (int variant = 0; variant < 5; ++variant) {
            BoatModelSpec varied = spec;
            varied.length *= .85f + variant * .08f;
            varied.cabin_length *= .85f + variant * .08f;
            varied.beam *= .9f + variant * .04f;
            varied.freeboard *= .85f + variant * .07f;
            varied.bow_width = .025f + variant * .09f;
            varied.windshield_rake = variant * .15f;
            varied.fenders = variant % 2 != 0;
            CHECK(boat_model_generate(&a, varied));
            CHECK(valid_mesh(&a.mesh) && !a.overflow);
        }
    }
    BoatModelSpec invalid = boat_model_preset(BOAT_MODEL_SKIFF);
    invalid.length = NAN;
    CHECK(!boat_model_generate(&a, invalid) && a.mesh.triangle_count == 0);
    invalid = boat_model_preset(BOAT_MODEL_RUNABOUT);
    invalid.engines = 3;
    CHECK(!boat_model_generate(&a, invalid));
    invalid.engines = 1;
    invalid.cabin_length = .1f;
    CHECK(!boat_model_generate(&a, invalid));
    CHECK(!boat_model_generate(NULL, invalid));
    invalid = boat_model_preset(BOAT_MODEL_CRUISER);
    invalid.cabin_length = .6f;
    invalid.windshield_rake = .6f;
    CHECK(!boat_model_generate(&a, invalid));
    CHECK(waterfront_harbor_boat_mesh(-1) == NULL);
    CHECK(waterfront_harbor_boat_mesh(WATERFRONT_HARBOR_BOATS) == NULL);
    CHECK(valid_mesh(waterfront_harbor_pontoon_mesh()));
    CHECK(valid_mesh(waterfront_harbor_quay_mesh()));
    const WaterfrontBerth *berths = waterfront_harbor_berths();
    for (int i = 0; i < WATERFRONT_HARBOR_BOATS; ++i) {
        const Mesh3D *mesh = waterfront_harbor_boat_mesh(i);
        CHECK(valid_mesh(mesh));
        Transform3D pose = { .position = berths[i].position,
            .rotation = { 0, berths[i].heading, 0 },
            .scale = { 1, 1, 1 } };
        for (size_t t = 0; t < mesh->triangle_count; ++t)
            for (int j = 0; j < 3; ++j) {
                Vector3 p = waterfront_harbor_attachment(
                    pose, mesh->vertices[mesh->triangles[t].vertices[j]]);
                CHECK(test_river_water((Vector2) { -p.x, p.z }, .3f));
            }
        CHECK(waterfront_harbor_contact((Vector2) { -pose.position.x, pose.position.z }, 0, NULL));
    }
    for (int z = -100; z <= 275; z += 5) {
        CHECK(test_river_water((Vector2) { -12, z }, 3));
        CHECK(!waterfront_harbor_contact((Vector2) { -12, z }, 3, NULL));
    }
    WaterfrontHarborFrame flat, waves, again;
    waterfront_harbor_evaluate((BoatWater) { 0 }, 0, &flat);
    waterfront_harbor_evaluate((BoatWater) { 0 }, 87, &again);
    CHECK(!memcmp(&flat, &again, sizeof(flat)));
    for (int tick = 0; tick < 120 * 20; tick += 17) {
        float time = tick / 120.0f;
        waterfront_harbor_evaluate(boat_water_gentle(), time, &waves);
        waterfront_harbor_evaluate(boat_water_gentle(), time, &again);
        CHECK(!memcmp(&waves, &again, sizeof(waves)));
        CHECK(waves.line_count == WATERFRONT_HARBOR_LINES);
        for (int i = 0; i < WATERFRONT_HARBOR_BOATS; ++i) {
            CHECK(fabsf(waves.boats[i].position.y) < .22f);
            CHECK(isfinite(waves.boats[i].rotation.x) && isfinite(waves.boats[i].rotation.z));
            CHECK(waves.boats[i].position.x == flat.boats[i].position.x);
            CHECK(waves.boats[i].position.z == flat.boats[i].position.z);
            BoatModelSpec s = waterfront_harbor_spec(i);
            Vector3 cleat = waterfront_harbor_attachment(
                waves.boats[i], (Vector3) { s.beam * .39f, s.freeboard + .10f, -s.length * .24f });
            Transform3D line = waves.lines[6 + i * 4];
            CHECK(
                close_vector(cleat, waterfront_harbor_attachment(line, (Vector3) { 0, 0, -.5f })));
            CHECK(close_vector(waterfront_harbor_attachment(line, (Vector3) { 0, 0, .5f }),
                waterfront_harbor_attachment(waves.lines[7 + i * 4], (Vector3) { 0, 0, -.5f })));
            if (i < 6) {
                const Mesh3D *dock = waterfront_harbor_pontoon_mesh();
                for (int end = 0; end < 2; ++end) {
                    Vector3 anchor = waterfront_harbor_attachment(
                        waves.lines[7 + i * 4 + end * 2], (Vector3) { 0, 0, .5f });
                    Vector3 local
                        = Vector3Transform(Vector3Subtract(anchor, waves.pontoon.position),
                            MatrixInvert(MatrixRotateXYZ(waves.pontoon.rotation)));
                    float nearest = INFINITY;
                    for (size_t t = 0; t < dock->triangle_count; ++t)
                        for (int v = 0; v < 3; ++v)
                            nearest = fminf(nearest,
                                Vector3Distance(
                                    local, dock->vertices[dock->triangles[t].vertices[v]]));
                    CHECK(nearest < .25f); // Rope ends must meet modeled pontoon fittings.
                }
            }
        }
    }
    // Real simulation contacts: approach the workboat and pontoon, then back away.
    const Vector2 starts[] = { { -34, 35 }, { 0, 182 } };
    const float headings[] = { -PI / 2, PI / 2 };
    for (int test = 0; test < 2; ++test) {
        BoatSession boat;
        boat_session_init(&boat, test_world(BOAT_LOCATION_RIVER), (BoatWater) { 0 });
        boat.boat.position = starts[test];
        boat.boat.heading = boat.presentation.camera_heading = headings[test];
        boat.boat.velocity
            = (Vector2) { sinf(boat.boat.heading) * 8, -cosf(boat.boat.heading) * 8 };
        for (int tick = 0; tick < 600; ++tick) {
            boat_session_step(&boat, (BoatInput) { 0 });
            CHECK(player_clear(&boat.boat));
            CHECK(isfinite(boat.boat.yaw_velocity));
        }
        CHECK(Vector2Distance(boat.boat.position, starts[test]) > 1);
        Vector2 contact = boat.boat.position;
        boat.boat.throttle = -.5f;
        for (int tick = 0; tick < 600; ++tick) {
            boat_session_step(&boat, (BoatInput) { 0 });
            CHECK(player_clear(&boat.boat));
        }
        CHECK(Vector2Distance(contact, boat.boat.position) > 3);
    }
    printf("boat_models=%d variants=%d preset_triangles=%zu harbor_geometry=ok\n",
        BOAT_MODEL_KIND_COUNT, BOAT_MODEL_KIND_COUNT * 5, triangles);
    test_worlds_close();
    return 0;
}
