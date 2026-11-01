// M1: exercise the shipped marina and rejection boundaries through native staging.
#include "boat_plugin_host.h"
#include "boat_plugin_io.h"
#include "boat_world.h"
#include "raymath.h"
#include "waterfront_harbor.h"
#include "waterfront_outer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static bool accepted(const BoatPluginVoyage *v, const BoatWorldResources *r)
{
    BoatPluginDiagnostic d;
    BoatSceneDiagnostic sd;
    return boat_scene_validate(&v->scene, &sd)
        && boat_plugin_check_location(v, r->definition, &r->map, &d) == BOAT_PLUGIN_OK;
}

static bool same_vector(Vector3 a, Vector3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

static bool same_pose(Transform3D a, Transform3D b)
{
    return same_vector(a.position, b.position) && same_vector(a.rotation, b.rotation)
        && same_vector(a.scale, b.scale);
}

int main(void)
{
    BoatWorldResources resources;
    CHECK(boat_world_resources_init(&resources, boat_location_find("outer")));
    BoatPluginFiles files;
    BoatPluginDiagnostic d;
    CHECK(boat_plugin_read("plugins/harbor-life", NULL, &files, &d) == BOAT_PLUGIN_OK);
    BoatPluginBundle bundle = boat_plugin_bundle(&files);
    BoatPluginVoyage original;
    CHECK(boat_plugin_load_voyage(&bundle, NULL, &original, &d) == BOAT_PLUGIN_OK);
    CHECK(accepted(&original, &resources));
    CHECK(original.scene.berth_count == 12 && original.scene.module_count == 13);
    BoatPluginVoyage v = original;
    v.scene.module_count = BOAT_SCENE_MODULES + 1;
    CHECK(!accepted(&v, &resources));
    v = original;
    v.scene.berth_count = BOAT_SCENE_BERTHS + 1;
    CHECK(!accepted(&v, &resources));
    v = original;
    strcpy(v.scene.modules[3].id, "pier");
    CHECK(!accepted(&v, &resources));
    v = original;
    strcpy(v.scene.modules[5].attaches, "missing");
    CHECK(!accepted(&v, &resources));
    v = original;
    v.scene.modules[5].x += .5f; // unsafe penetration at a declared join
    CHECK(!accepted(&v, &resources));
    v = original;
    v.scene.modules[5].x -= .5f; // detached join
    CHECK(!accepted(&v, &resources));
    v = original;
    v.scene.modules[5].deck_height += .1f;
    CHECK(!accepted(&v, &resources));
    v = original;
    v.scene.modules[0].x = 35; // shore
    CHECK(!accepted(&v, &resources));
    v = original;
    v.scene.modules[2].deck_height = 3.8f; // buried by outer terrain
    CHECK(!accepted(&v, &resources));
    v = original;
    v.scene.modules[2].x = 20; // unsupported fixed deck
    CHECK(!accepted(&v, &resources));
    v = original;
    v.scene.modules[3].x = v.scene.entrance_x;
    v.scene.modules[3].z = v.scene.entrance_z;
    CHECK(!accepted(&v, &resources));
    v = original;
    v.scene.modules[3].x = v.scene.modules[0].x;
    v.scene.modules[3].z = v.scene.modules[0].z; // undeclared overlap
    CHECK(!accepted(&v, &resources));
    for (unsigned i = 0; i < 3; ++i) {
        v = original;
        float *dimension = i == 0 ? &v.scene.modules[3].length
            : i == 1              ? &v.scene.modules[3].width
                                  : &v.scene.modules[3].deck_height;
        *dimension = NAN;
        CHECK(!accepted(&v, &resources));
        *dimension = 0;
        CHECK(!accepted(&v, &resources));
        *dimension = 100;
        CHECK(!accepted(&v, &resources));
    }
    BoatSceneModuleKind kind;
    CHECK(!boat_scene_module_kind_from_name("pontoon", &kind));
    CHECK(!boat_scene_module_kind_from_name("pontoon_small", &kind));
    CHECK(!boat_scene_module_kind_from_name("quay", &kind));
    // Bounds are inclusive; placement is a separate check.
    BoatSceneDefinition bounds = { .version = BOAT_SCENE_VERSION,
        .modules_present = true,
        .module_count = 1,
        .modules = { { .id = "bound",
            .kind = BOAT_SCENE_MODULE_PLATFORM,
            .length = .5f,
            .width = .5f,
            .deck_height = .25f } } };
    CHECK(boat_scene_validate(&bounds, NULL));
    bounds.modules[0].length = 60;
    bounds.modules[0].width = 20;
    bounds.modules[0].deck_height = 2;
    CHECK(boat_scene_validate(&bounds, NULL));
    // Direct/reverse seeks: floating joins remain exact, fixed quay and shore end stay fixed.
    WaterfrontHarborFrame first, later, repeat;
    memset(&first, 0xa5, sizeof(first));
    memset(&repeat, 0x5a, sizeof(repeat));
    waterfront_harbor_evaluate_scene(&original.scene, boat_water_gentle(), 0, NULL, &first);
    waterfront_harbor_evaluate_scene(&original.scene, boat_water_gentle(), 7, NULL, &later);
    waterfront_harbor_evaluate_scene(&original.scene, boat_water_gentle(), 0, NULL, &repeat);
    // Aggregate padding has no defined value after C struct assignments. Compare every
    // meaningful field exactly, including live array entries, instead of object bytes.
    CHECK(first.berth_count == repeat.berth_count);
    CHECK(first.modules_authored == repeat.modules_authored);
    CHECK(first.module_count == repeat.module_count && first.line_count == repeat.line_count);
    CHECK(same_pose(first.ferry, repeat.ferry));
    CHECK(same_pose(first.pontoon, repeat.pontoon));
    CHECK(same_pose(first.gangway, repeat.gangway));
    CHECK(same_pose(first.quay, repeat.quay));
    for (int i = 0; i < first.berth_count; ++i) {
        CHECK(first.recipes[i] == repeat.recipes[i]);
        CHECK(same_pose(first.boats[i], repeat.boats[i]));
    }
    for (int i = 0; i < first.module_count; ++i) {
        CHECK(first.modules[i].kind == repeat.modules[i].kind);
        CHECK(same_pose(first.modules[i].pose, repeat.modules[i].pose));
    }
    for (int i = 0; i < first.line_count; ++i)
        CHECK(same_pose(first.lines[i], repeat.lines[i]));
    CHECK(
        Vector3Distance(first.modules[0].pose.position, later.modules[0].pose.position) > .00001f);
    CHECK(Vector3Distance(first.modules[2].pose.position, later.modules[2].pose.position) == 0);
    for (unsigned i = 0; i < original.scene.module_count; ++i) {
        const BoatSceneModule *m = &original.scene.modules[i];
        if (!m->attaches[0] || m->kind == BOAT_SCENE_MODULE_GANGWAY)
            continue;
        const BoatSceneModule *parent = boat_scene_find_module(&original.scene, m->attaches);
        unsigned j = (unsigned)(parent - original.scene.modules);
        CHECK(fabsf(later.modules[i].pose.position.y - later.modules[j].pose.position.y) < .00001f);
    }
    Vector3 shore0 = waterfront_harbor_attachment(first.modules[1].pose, (Vector3) { 0, 0, -.5f });
    Vector3 shore7 = waterfront_harbor_attachment(later.modules[1].pose, (Vector3) { 0, 0, -.5f });
    CHECK(Vector3Distance(shore0, shore7) < .00001f);
    for (unsigned i = 0; i < original.scene.module_count; ++i) {
        if (original.scene.modules[i].kind == BOAT_SCENE_MODULE_GANGWAY)
            continue;
        Vector3 p = later.modules[i].pose.position;
        CHECK(waterfront_harbor_contact_height_scene(&original.scene, (Vector2) { -p.x, p.z }, .1f,
            p.y - .2f, p.y + .2f, boat_water_gentle(), 7, NULL));
        CHECK(!waterfront_harbor_camera_clear_scene(&original.scene, p, .1f));
    }
    // A rejected geometric edit must preserve the installed definition and Undo history.
    static BoatPluginHost host;
    CHECK(boat_plugin_host_start(&host, resources.definition, &resources.map, &bundle));
    uint64_t digest = boat_plugin_host_active(&host)->voyage.scene_digest;
    static char source[BOAT_PLUGIN_SOURCE_MAX];
    int bytes = snprintf(source, sizeof(source),
        "local def = (function()\n%.*s\nend)()\ndef.scene.marina.modules[3].x = 20\nreturn def\n",
        (int)bundle.source_size, bundle.source);
    CHECK(bytes > 0 && (size_t)bytes < sizeof(source));
    BoatPluginBundle edited = bundle;
    edited.source = source;
    edited.source_size = (size_t)bytes;
    CHECK(!boat_plugin_host_stage(&host, &edited));
    CHECK(host.diagnostic.status == BOAT_PLUGIN_SCHEMA && host.depth == 1 && !host.held);
    CHECK(boat_plugin_host_active(&host)->number == 1
        && boat_plugin_host_active(&host)->voyage.scene_digest == digest);
    // A fully placed maximum-budget marina in the open fjord, with distinct IDs.
    v = original;
    // Keep the shipped ferry/bridge/visiting-craft timetable in the capacity placement proof.
    v.scene.gulls_present = false;
    memset(&v.scene.gulls, 0, sizeof(v.scene.gulls));
    Vector2 positions[BOAT_SCENE_MODULES + BOAT_SCENE_BERTHS];
    unsigned found = 0;
    for (float z = -3000; z <= -2200 && found < BOAT_SCENE_MODULES + BOAT_SCENE_BERTHS; z += 16)
        for (float x = 3800; x <= 4500 && found < BOAT_SCENE_MODULES + BOAT_SCENE_BERTHS; x += 16)
            if (boat_map_water(&resources.map, (Vector2) { x, z }, 8))
                positions[found++] = (Vector2) { x, z };
    CHECK(found == BOAT_SCENE_MODULES + BOAT_SCENE_BERTHS);
    v.scene.module_count = BOAT_SCENE_MODULES;
    for (unsigned i = 0; i < BOAT_SCENE_MODULES; ++i) {
        v.scene.modules[i] = (BoatSceneModule) { .kind = BOAT_SCENE_MODULE_PLATFORM,
            .x = positions[i].x,
            .z = positions[i].y,
            .length = 8,
            .width = 2,
            .deck_height = .74f };
        snprintf(v.scene.modules[i].id, BOAT_SCENE_TEXT, "module_%u", i);
    }
    v.scene.berth_count = BOAT_SCENE_BERTHS;
    for (unsigned i = 0; i < BOAT_SCENE_BERTHS; ++i) {
        v.scene.berths[i] = (BoatSceneBerth) { .recipe = BOAT_SCENE_BERTH_RUNABOUT,
            .x = positions[BOAT_SCENE_MODULES + i].x,
            .z = positions[BOAT_SCENE_MODULES + i].y };
        snprintf(v.scene.berths[i].id, BOAT_SCENE_TEXT, "berth_%u", i);
    }
    if (!accepted(&v, &resources)) {
        boat_plugin_check_location(&v, resources.definition, &resources.map, &d);
        fprintf(stderr, "%s: %s\n", d.field, d.message);
        return 1;
    }
    waterfront_harbor_evaluate_scene(&v.scene, boat_water_gentle(), 7, NULL, &later);
    CHECK(later.berth_count == BOAT_SCENE_BERTHS && later.module_count == BOAT_SCENE_MODULES);
    printf("scene_site=ok berths=%u modules=%u scene_bytes=%zu frame_bytes=%zu lines_capacity=%u\n",
        v.scene.berth_count, v.scene.module_count, sizeof(v.scene), sizeof(later),
        WATERFRONT_HARBOR_LINES);
    for (unsigned i = 0; i < v.scene.berth_count; ++i)
        v.scene.berths[i].mooring = BOAT_SCENE_MOORING_PONTOON;
    for (unsigned i = 1; i < v.scene.module_count; ++i) {
        v.scene.modules[i].kind = BOAT_SCENE_MODULE_GANGWAY;
        strcpy(v.scene.modules[i].attaches, v.scene.modules[0].id);
        v.scene.modules[i].anchor_x = v.scene.modules[0].x + 4;
        v.scene.modules[i].anchor_z = v.scene.modules[0].z;
    }
    waterfront_harbor_evaluate_scene(&v.scene, boat_water_gentle(), 7, NULL, &later);
    CHECK(later.line_count == BOAT_SCENE_BERTHS * 4 + (BOAT_SCENE_MODULES - 1) * 6 + 8);
    CHECK(later.line_count <= WATERFRONT_HARBOR_LINES);
    boat_world_resources_destroy(&resources);
    return 0;
}
