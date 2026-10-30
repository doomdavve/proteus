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

static bool player_clear_scene(const Boat *boat, const BoatSceneDefinition *scene)
{
    for (int end = -1; end <= 1; end += 2)
        for (int side = -1; side <= 1; side += 2) {
            Vector2 p = Vector2Add(boat->position,
                (Vector2) { sinf(boat->heading) * end * 1.6f + cosf(boat->heading) * side * .7f,
                    -cosf(boat->heading) * end * 1.6f + sinf(boat->heading) * side * .7f });
            if (waterfront_harbor_contact_height_scene(
                    scene, p, 0, -INFINITY, INFINITY, (BoatWater) { 0 }, 0, NULL))
                return false;
        }
    return true;
}

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
    // The docked ferry: one mesh under the model cap, its hull afloat on both river maps with
    // room for its fenders, a solid contact inside and open water just off its river side.
    const WaterfrontFerry *ferry = waterfront_harbor_ferry();
    const Mesh3D *ferry_mesh = waterfront_harbor_ferry_mesh();
    CHECK(valid_mesh(ferry_mesh) && ferry_mesh->triangle_count < BOAT_MODEL_MAX_TRIANGLES);
    Vector2 across = { cosf(ferry->heading), -sinf(ferry->heading) };
    Vector2 along = { sinf(ferry->heading), cosf(ferry->heading) };
    for (int u = -2; u <= 2; ++u)
        for (int v = -10; v <= 10; ++v) {
            float x = u * ferry->beam * .25f, z = v * ferry->length * .05f;
            Vector2 sim = { -(ferry->position.x + across.x * x + along.x * z),
                ferry->position.z + across.y * x + along.y * z };
            CHECK(test_river_water(sim, 1));
            CHECK(boat_map_water(&test_world(BOAT_LOCATION_OUTER)->map, sim, 1));
            CHECK(waterfront_harbor_contact(sim, 0, NULL));
            CHECK(!waterfront_harbor_contact_height(
                sim, 0, ferry->height + 1, INFINITY, (BoatWater) { 0 }, 0, NULL));
        }
    for (int v = -10; v <= 10; ++v) {
        float x = ferry->beam * .5f + 4, z = v * ferry->length * .05f;
        Vector2 sim = { -(ferry->position.x + across.x * x + along.x * z),
            ferry->position.z + across.y * x + along.y * z };
        CHECK(test_river_water(sim, 1));
        CHECK(!waterfront_harbor_contact(sim, 0, NULL));
    }
    CHECK(!waterfront_harbor_camera_clear(ferry->position, 0));
    CHECK(!waterfront_harbor_camera_clear(
        Vector3Add(ferry->position, (Vector3) { 0, ferry->height - 1, 0 }), 0));
    CHECK(waterfront_harbor_camera_clear(
        Vector3Add(ferry->position, (Vector3) { 0, ferry->height + 2, 0 }), 0));
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
        CHECK(waves.line_count == WATERFRONT_HARBOR_BOATS * 4 + 6 + WATERFRONT_FERRY_LINES * 2);
        CHECK(fabsf(waves.ferry.position.y) < .22f);
        for (int i = 0; i < WATERFRONT_FERRY_LINES; ++i) {
            // Ferry lines run from the quay-side deck edge down to bollards ashore.
            Vector3 deck = waterfront_harbor_attachment(
                waves.ferry, (Vector3) { -ferry->beam * .5f + 1, 11.2f, 0 });
            Vector3 top = waterfront_harbor_attachment(
                waves.lines[6 + WATERFRONT_HARBOR_BOATS * 4 + i * 2], (Vector3) { 0, 0, -.5f });
            CHECK(fabsf(top.y - deck.y) < .5f);
            Vector3 bollard = waterfront_harbor_attachment(
                waves.lines[7 + WATERFRONT_HARBOR_BOATS * 4 + i * 2], (Vector3) { 0, 0, .5f });
            CHECK(fabsf(bollard.y - 3.9f) < .01f);
            CHECK(!test_river_water((Vector2) { -bollard.x, bollard.z }, 0));
        }
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

    // L2.1: one caller-owned berth list drives poses, meshes, ropes, contacts and camera queries.
    // The compiled list remains the NULL-scene parity oracle.
    WaterfrontHarborFrame compiled_again;
    waterfront_harbor_evaluate_scene(NULL, (BoatWater) { 0 }, 0, NULL, &compiled_again);
    CHECK(!memcmp(&flat, &compiled_again, sizeof(flat)));
    BoatSceneDefinition zero = { .version = 1, .berths_present = true };
    WaterfrontHarborFrame empty;
    waterfront_harbor_evaluate_scene(&zero, (BoatWater) { 0 }, 0, NULL, &empty);
    CHECK(empty.berth_count == 0 && empty.line_count == 6 + WATERFRONT_FERRY_LINES * 2);

    BoatSceneDefinition two = { .version = 1, .berths_present = true, .berth_count = 2 };
    two.berths[0] = (BoatSceneBerth) { .id = "skiff",
        .recipe = BOAT_SCENE_BERTH_SKIFF,
        .mooring = BOAT_SCENE_MOORING_PONTOON,
        .x = 13,
        .z = 167,
        .heading = -PI / 2 };
    two.berths[1] = (BoatSceneBerth) { .id = "runabout",
        .recipe = BOAT_SCENE_BERTH_RUNABOUT,
        .mooring = BOAT_SCENE_MOORING_PONTOON,
        .x = 13,
        .z = 173,
        .heading = -PI / 2 };
    WaterfrontHarborFrame authored, independent;
    waterfront_harbor_evaluate_scene(&two, (BoatWater) { 0 }, 0, NULL, &authored);
    waterfront_harbor_evaluate_scene(&two, (BoatWater) { 0 }, 0, NULL, &independent);
    CHECK(!memcmp(&authored, &independent, sizeof(authored)));
    CHECK(authored.berth_count == 2 && authored.recipes[0] == BOAT_SCENE_BERTH_SKIFF
        && authored.recipes[1] == BOAT_SCENE_BERTH_RUNABOUT);
    CHECK(authored.line_count == 2 * 4 + 6 + WATERFRONT_FERRY_LINES * 2);
    WaterfrontBerth first = waterfront_harbor_berth_at(&two, 0);
    CHECK(!strcmp(first.id, "skiff") && first.position.x == -13 && first.position.z == 167);
    CHECK(waterfront_harbor_berth_count(&two) == 2);
    CHECK(waterfront_harbor_berth_count(NULL) == WATERFRONT_HARBOR_BOATS);
    CHECK(waterfront_harbor_recipe_mesh(BOAT_SCENE_BERTH_RECIPES) == NULL);
    CHECK(waterfront_harbor_contact_height_scene(
        &two, (Vector2) { 13, 167 }, .35f, -.4f, 2, (BoatWater) { 0 }, 0, NULL));
    CHECK(!waterfront_harbor_contact_height_scene(
        &two, (Vector2) { 13, 179 }, .35f, -.4f, 2, (BoatWater) { 0 }, 0, NULL));
    CHECK(!waterfront_harbor_camera_clear_scene(&two, (Vector3) { -13, .5f, 167 }, 0));
    CHECK(waterfront_harbor_camera_clear_scene(&two, (Vector3) { -13, .5f, 179 }, 0));

    // Reordering IDs changes no pose, rope, contact or camera result.
    BoatSceneDefinition reordered = two;
    strcpy(reordered.berths[0].id, "second");
    strcpy(reordered.berths[1].id, "first");
    WaterfrontHarborFrame same;
    waterfront_harbor_evaluate_scene(&reordered, (BoatWater) { 0 }, 0, NULL, &same);
    CHECK(!memcmp(&authored, &same, sizeof(authored)));

    // Moving or removing a berth moves/removes its hull, ropes, contact and camera exclusion;
    // the compiled hull at the old place is not drawn or contacted invisibly.
    BoatSceneDefinition moved = two;
    moved.berths[1].z = 179;
    WaterfrontHarborFrame after;
    waterfront_harbor_evaluate_scene(&moved, (BoatWater) { 0 }, 0, NULL, &after);
    CHECK(after.berth_count == 2 && after.boats[1].position.z == 179);
    CHECK(!waterfront_harbor_contact_height_scene(
        &moved, (Vector2) { 13, 173 }, .35f, -.4f, 2, (BoatWater) { 0 }, 0, NULL));
    CHECK(waterfront_harbor_contact_height_scene(
        &moved, (Vector2) { 13, 179 }, .35f, -.4f, 2, (BoatWater) { 0 }, 0, NULL));
    CHECK(waterfront_harbor_camera_clear_scene(&moved, (Vector3) { -13, .5f, 173 }, 0));
    CHECK(!waterfront_harbor_camera_clear_scene(&moved, (Vector3) { -13, .5f, 179 }, 0));
    BoatSceneDefinition removed = two;
    removed.berth_count = 1;
    WaterfrontHarborFrame one;
    waterfront_harbor_evaluate_scene(&removed, (BoatWater) { 0 }, 0, NULL, &one);
    CHECK(one.berth_count == 1 && one.line_count == 4 + 6 + WATERFRONT_FERRY_LINES * 2);
    CHECK(!waterfront_harbor_contact_height_scene(
        &removed, (Vector2) { 13, 173 }, .35f, -.4f, 2, (BoatWater) { 0 }, 0, NULL));
    CHECK(waterfront_harbor_camera_clear_scene(&removed, (Vector3) { -13, .5f, 173 }, 0));
    CHECK(waterfront_harbor_contact((Vector2) { 13, 179 }, .35f, NULL));
    CHECK(!waterfront_harbor_contact_height_scene(
        &two, (Vector2) { 13, 179 }, .35f, -.4f, 2, (BoatWater) { 0 }, 0, NULL));

    BoatSceneDefinition seven
        = { .version = 1, .berths_present = true, .berth_count = WATERFRONT_HARBOR_BOATS };
    for (int i = 0; i < WATERFRONT_HARBOR_BOATS; ++i) {
        const WaterfrontBerth *source = &berths[i];
        seven.berths[i] = (BoatSceneBerth) { .recipe = source->recipe,
            .mooring = source->mooring,
            .x = -source->position.x,
            .z = source->position.z,
            .heading = source->heading };
        memcpy(seven.berths[i].id, source->id, sizeof(seven.berths[i].id));
    }
    WaterfrontHarborFrame full;
    waterfront_harbor_evaluate_scene(&seven, (BoatWater) { 0 }, 0, NULL, &full);
    // Six authored pontoon-moored hulls plus the quay-moored workboat, which has no authored
    // quay attachment yet (L2.1 deliberately adds no new attachments).
    CHECK(full.berth_count == WATERFRONT_HARBOR_BOATS && full.line_count == 6 * 4 + 6 + 8);
    for (int i = 0; i < WATERFRONT_HARBOR_BOATS; ++i) {
        CHECK(close_vector(full.boats[i].position, flat.boats[i].position));
        CHECK(full.recipes[i] == berths[i].recipe);
    }

    // L2.3: the two-berth site with a small authored pontoon and gangway. The gangway's shore
    // end stays fixed while its pontoon landing bobs, and authored berth ropes follow that
    // pontoon's cleats instead of the compiled dock.
    BoatSceneDefinition marina = two;
    marina.modules_present = true;
    marina.module_count = 2;
    marina.modules[0] = (BoatSceneModule) {
        .id = "pier", .kind = BOAT_SCENE_MODULE_PONTOON_SMALL, .x = 20, .z = 170, .heading = 0
    };
    marina.modules[1] = (BoatSceneModule) { .id = "ramp",
        .kind = BOAT_SCENE_MODULE_GANGWAY,
        .x = 34.1f,
        .z = 170,
        .anchor_x = 34.1f,
        .anchor_z = 170,
        .attaches = "pier" };
    WaterfrontHarborFrame modular;
    waterfront_harbor_evaluate_scene(&marina, boat_water_gentle(), 0, NULL, &modular);
    CHECK(modular.module_count == 2 && modular.modules[0].kind == BOAT_SCENE_MODULE_PONTOON_SMALL
        && modular.modules[1].kind == BOAT_SCENE_MODULE_GANGWAY);
    CHECK(modular.line_count == 2 * 4 + 6 + WATERFRONT_FERRY_LINES * 2);
    Transform3D pier = modular.modules[0].pose;
    Vector3 anchor = waterfront_harbor_attachment(modular.lines[7], (Vector3) { 0, 0, .5f });
    Vector3 local = Vector3Transform(
        Vector3Subtract(anchor, pier.position), MatrixInvert(MatrixRotateXYZ(pier.rotation)));
    CHECK(fabsf(local.x - 1.05f) < .25f && fabsf(local.z) < 7);
    Vector3 shore = waterfront_harbor_attachment(modular.lines[1], (Vector3) { 0, 0, .5f });
    Vector3 landing = waterfront_harbor_attachment(modular.lines[2], (Vector3) { 0, 0, .5f });
    WaterfrontHarborFrame modular_later;
    waterfront_harbor_evaluate_scene(&marina, boat_water_gentle(), 7, NULL, &modular_later);
    Vector3 shore_later
        = waterfront_harbor_attachment(modular_later.lines[1], (Vector3) { 0, 0, .5f });
    Vector3 landing_later
        = waterfront_harbor_attachment(modular_later.lines[2], (Vector3) { 0, 0, .5f });
    CHECK(close_vector(shore, shore_later) && Vector3Distance(landing, landing_later) > .01f);
    Vector3 anchor_later
        = waterfront_harbor_attachment(modular_later.lines[7], (Vector3) { 0, 0, .5f });
    Transform3D pier_later = modular_later.modules[0].pose;
    Vector3 local_later = Vector3Transform(Vector3Subtract(anchor_later, pier_later.position),
        MatrixInvert(MatrixRotateXYZ(pier_later.rotation)));
    CHECK(fabsf(local_later.x - 1.05f) < .25f && fabsf(local_later.z) < 7);
    CHECK(waterfront_harbor_contact_height_scene(
        &marina, (Vector2) { 20, 170 }, .35f, -.4f, 2, (BoatWater) { 0 }, 0, NULL));
    CHECK(!waterfront_harbor_contact_height_scene(
        &marina, (Vector2) { 20, 182 }, .35f, -.4f, 2, (BoatWater) { 0 }, 0, NULL));
    CHECK(waterfront_harbor_contact((Vector2) { 20, 182 }, .35f, NULL));
    CHECK(!waterfront_harbor_camera_clear_scene(&marina, (Vector3) { -27.6f, 2, 170 }, 0));
    BoatSceneDefinition no_modules = marina;
    no_modules.module_count = 0;
    WaterfrontHarborFrame bare;
    waterfront_harbor_evaluate_scene(&no_modules, (BoatWater) { 0 }, 0, NULL, &bare);
    CHECK(bare.modules_authored && bare.module_count == 0);
    CHECK(!waterfront_harbor_contact_height_scene(
        &no_modules, (Vector2) { 20, 182 }, .35f, -.4f, 2, (BoatWater) { 0 }, 0, NULL));

    // L2.2 deterministic marina approach/exit: the authored lane is usable and both hulls stop
    // the same physics that reads BoatWorld.scene.
    {
        BoatSession boat;
        boat_session_init(&boat, test_world(BOAT_LOCATION_OUTER), (BoatWater) { 0 });
        boat.world.scene = &two;
        boat.boat.position = (Vector2) { 6, 150 };
        boat.boat.heading = PI; // Forward is +z (north) into the marina.
        boat.boat.velocity = (Vector2) { 0, 6 };
        for (int tick = 0; tick < 360; ++tick) {
            boat_session_step(&boat, (BoatInput) { .throttle_rate = .5f });
            CHECK(player_clear_scene(&boat.boat, &two));
        }
        CHECK(boat.boat.position.y > 160);
        // Leave the lane toward open water; no authored hull is contacted.
        boat.boat.heading = -PI / 2;
        boat.boat.velocity = (Vector2) { -5, 0 };
        for (int tick = 0; tick < 180; ++tick) {
            boat_session_step(&boat, (BoatInput) { 0 });
            CHECK(player_clear_scene(&boat.boat, &two));
        }
        boat_session_destroy(&boat);
    }
    for (int berth = 0; berth < 2; ++berth) {
        BoatSession boat;
        boat_session_init(&boat, test_world(BOAT_LOCATION_OUTER), (BoatWater) { 0 });
        boat.world.scene = &two;
        boat.boat.position = (Vector2) { 4, berth ? 173 : 167 };
        boat.boat.heading = PI / 2; // Forward is +x (east) into the moored hull.
        boat.boat.velocity = (Vector2) { 6, 0 };
        for (int tick = 0; tick < 240; ++tick)
            boat_session_step(&boat, (BoatInput) { .throttle_rate = .5f });
        CHECK(boat.boat.position.x < 9.6f); // Stopped before crossing the hull's centre.
        CHECK(isfinite(boat.boat.position.x));
        boat_session_destroy(&boat);
    }

    // Real simulation contacts: approach the workboat and pontoon, then back away.
    // The third run heads at the ferry's river side from 30 m out.
    Vector2 off = { -(ferry->position.x + across.x * 30), ferry->position.z + across.y * 30 };
    const Vector2 starts[] = { { -34, 35 }, { 0, 182 }, off };
    const float headings[] = { -PI / 2, PI / 2, atan2f(across.x, across.y) };
    for (int test = 0; test < 3; ++test) {
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
    printf(
        "boat_models=%d variants=%d preset_triangles=%zu ferry_triangles=%zu harbor_geometry=ok\n",
        BOAT_MODEL_KIND_COUNT, BOAT_MODEL_KIND_COUNT * 5, triangles, ferry_mesh->triangle_count);
    test_worlds_close();
    return 0;
}
