/* Lua plugins P0 (docs/PLUGIN_PLAN.md): the reference package decodes to exactly the compiled
 * fixture voyage; bad manifests, syntax, runtime errors, forbidden facilities, invalid fields,
 * memory and instruction exhaustion and unsafe files are rejected with a structured diagnostic;
 * a failure never changes the caller's last good definition and never leaks the Lua state. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L // mkdir and symlink for the file cases under strict C17.
#endif
#include "boat_fixtures.h"
#include "boat_location.h"
#include "boat_map.h"
#include "boat_plugin.h"
#include "boat_plugin_io.h"
#include "boat_test_cases.h"
#include "boat_world.h"
#include "raymath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define REFERENCE "plugins/langedrag"

static const char MANIFEST[] = "{\"id\": \"test.voyage\", \"name\": \"Test\", \"api\": 1, "
                               "\"kind\": \"voyage\", \"entry\": \"main.lua\", \"location\": "
                               "\"outer\", \"capabilities\": [\"voyage.define\"], "
                               "\"state_version\": 1}";

static BoatPluginFiles reference_files;
static bool reference_read;

static const BoatPluginFiles *reference(void)
{
    if (!reference_read) {
        BoatPluginDiagnostic d;
        if (boat_plugin_read(REFERENCE, NULL, &reference_files, &d) != BOAT_PLUGIN_OK) {
            fprintf(stderr, "cannot read " REFERENCE ": %s: %s\n", d.file, d.message);
            exit(1);
        }
        reference_read = true;
    }
    return &reference_files;
}

static void show(const char *label, const BoatPluginDiagnostic *d)
{
    printf("%s: %s %s:%d [%s] %s\n", label, boat_plugin_status_name(d->status), d->file, d->line,
        d->field, d->message);
}

static BoatPluginStatus load(const char *manifest, const char *source, size_t size,
    const BoatPluginLimits *limits, BoatPluginVoyage *out, BoatPluginDiagnostic *d)
{
    BoatPluginBundle bundle = { manifest, strlen(manifest), source, size };
    return boat_plugin_load_voyage(&bundle, limits, out, d);
}

static BoatPluginStatus load_source(
    const char *source, BoatPluginVoyage *out, BoatPluginDiagnostic *d)
{
    return load(MANIFEST, source, strlen(source), NULL, out, d);
}

// The reference source with one fragment replaced, in a static buffer.
static const char *variant(const char *from, const char *to)
{
    static char text[BOAT_PLUGIN_SOURCE_MAX];
    const BoatPluginFiles *files = reference();
    char source[BOAT_PLUGIN_SOURCE_MAX + 1];
    memcpy(source, files->source, files->source_size);
    source[files->source_size] = 0;
    const char *at = strstr(source, from);
    if (!at) {
        fprintf(stderr, "fragment not in the reference: %s\n", from);
        exit(1);
    }
    snprintf(text, sizeof(text), "%.*s%s%s", (int)(at - source), source, to, at + strlen(from));
    return text;
}

// Expect STATUS, and FIELD and LINE when given (NULL / -1 skip them); never a leak.
static int expect(const char *label, const char *manifest, const char *source,
    BoatPluginStatus status, const char *field, int line)
{
    BoatPluginVoyage out;
    memset(&out, 0x5a, sizeof(out));
    BoatPluginVoyage before = out;
    BoatPluginDiagnostic d;
    BoatPluginStatus got
        = load(manifest ? manifest : MANIFEST, source, strlen(source), NULL, &out, &d);
    show(label, &d);
    CHECK(got == status && d.status == status);
    CHECK(!field || !strcmp(d.field, field));
    CHECK(line < 0 || d.line == line);
    CHECK(d.message[0]);
    CHECK(d.cost.leaked_bytes == 0);
    CHECK(!memcmp(&out, &before, sizeof(out)));
    return 0;
}

static int same_route(const BoatVoyageRoute *a, const BoatVoyageRoute *b)
{
    CHECK(a->shelter_count == b->shelter_count);
    for (unsigned i = 0; i < a->shelter_count; ++i) {
        const BoatVoyageShelter *s = &a->shelters[i], *t = &b->shelters[i];
        CHECK(!strcmp(s->name, t->name));
        CHECK(!memcmp(&s->position, &t->position, sizeof(s->position)));
        CHECK(!memcmp(&s->heading, &t->heading, sizeof(s->heading)));
        CHECK(!memcmp(&s->radius, &t->radius, sizeof(s->radius)));
    }
    CHECK(!memcmp(&a->arrival_centre, &b->arrival_centre, sizeof(a->arrival_centre)));
    CHECK(!memcmp(&a->arrival_radius, &b->arrival_radius, sizeof(float)));
    CHECK(!memcmp(&a->arrival_speed, &b->arrival_speed, sizeof(float)));
    // BoatStorm has trailing padding on MSVC. Compare every authored field's bits,
    // never indeterminate struct padding against a static compiled definition.
    CHECK(!memcmp(&a->exposure.centre.x, &b->exposure.centre.x, sizeof(float)));
    CHECK(!memcmp(&a->exposure.centre.y, &b->exposure.centre.y, sizeof(float)));
    CHECK(!memcmp(&a->exposure.velocity.x, &b->exposure.velocity.x, sizeof(float)));
    CHECK(!memcmp(&a->exposure.velocity.y, &b->exposure.velocity.y, sizeof(float)));
    CHECK(!memcmp(&a->exposure.radius, &b->exposure.radius, sizeof(float)));
    CHECK(!memcmp(&a->exposure.height, &b->exposure.height, sizeof(float)));
    CHECK(!memcmp(&a->exposure.started, &b->exposure.started, sizeof(double)));
    CHECK(!memcmp(&a->exposure.build, &b->exposure.build, sizeof(float)));
    CHECK(!memcmp(&a->exposed_fraction, &b->exposed_fraction, sizeof(float)));
    CHECK(!memcmp(&a->weather_range, &b->weather_range, sizeof(float)));
    return 0;
}

// L1.2: the optional scene envelope decodes and validates without installing runtime content.
// The typed definition also exercises the planned berth/module limits before L2 exposes them.
static int scene_contract(void)
{
    static const char scene_manifest[]
        = "{\"id\": \"test.voyage\", \"name\": \"Test\", "
          "\"api\": 1, \"kind\": \"voyage\", \"entry\": \"main.lua\", \"location\": "
          "\"outer\", \"capabilities\": [\"voyage.define\", \"scene.define\"], "
          "\"state_version\": 1}";
    const char *with_scene = variant("return {", "return { scene = { version = 2 },");
    BoatPluginVoyage voyage;
    BoatPluginDiagnostic d;
    CHECK(
        load(scene_manifest, with_scene, strlen(with_scene), NULL, &voyage, &d) == BOAT_PLUGIN_OK);
    CHECK(voyage.has_scene && voyage.scene.version == BOAT_SCENE_VERSION);
    CHECK(voyage.scene_digest == boat_scene_digest(&voyage.scene));
    CHECK(!boat_scene_has_content(&voyage.scene));
    CHECK(voyage.digest == boat_plugin_route_digest(&boat_fixture_voyage_outer));

    // Omitted and granted-but-absent envelopes keep the compiled scene.
    CHECK(load_source(variant("return {", "return { scene = { version = 2 },"), &voyage, &d)
        == BOAT_PLUGIN_SCHEMA);
    CHECK(!strcmp(d.field, "scene"));
    CHECK(load_source(variant("return {", "return {"), &voyage, &d) == BOAT_PLUGIN_OK);
    CHECK(!voyage.has_scene && voyage.scene_digest == 0);
    CHECK(load(scene_manifest, variant("return {", "return {"),
              strlen(variant("return {", "return {")), NULL, &voyage, &d)
        == BOAT_PLUGIN_OK);
    CHECK(!voyage.has_scene);

    // Reject every unsupported or malformed envelope, leaving OUT untouched.
    CHECK(load(scene_manifest, variant("return {", "return { scene = {},"),
              strlen(variant("return {", "return { scene = {},")), NULL, &voyage, &d)
        == BOAT_PLUGIN_SCHEMA);
    CHECK(!strcmp(d.field, "scene.version"));
    const char *bad_version = variant("return {", "return { scene = { version = 1 },");
    CHECK(load(scene_manifest, bad_version, strlen(bad_version), NULL, &voyage, &d)
        == BOAT_PLUGIN_SCHEMA);
    CHECK(!strcmp(d.field, "scene.version"));
    const char *unknown = variant("return {", "return { scene = { version = 2, galley = {} },");
    CHECK(load(scene_manifest, unknown, strlen(unknown), NULL, &voyage, &d) == BOAT_PLUGIN_SCHEMA);
    CHECK(!strcmp(d.field, "scene.galley"));
    const char *not_table = variant("return {", "return { scene = 1,");
    CHECK(load(scene_manifest, not_table, strlen(not_table), NULL, &voyage, &d)
        == BOAT_PLUGIN_SCHEMA);
    CHECK(!strcmp(d.field, "scene"));

    // Manifest grants are a canonical prefix. These are parse-only checks.
    BoatPluginManifest manifest;
    static const struct {
        const char *capabilities, *field;
    } grants[] = {
        { "[\"voyage.define\", \"objective.hud\", \"scene.define\"]", "capabilities[3]" },
        { "[\"voyage.define\", \"scene.define\", \"objective.timers\"]", "capabilities[3]" },
        { "[\"voyage.define\", \"scene.define\", \"scene.define\"]", "capabilities[3]" },
        { "[\"voyage.define\", \"scene.define\", \"objective.hud\", \"objective.timers\", "
          "\"objective.timers\"]",
            "capabilities" },
        { "[\"race.define\", \"scene.define\"]", "capabilities[1]" },
    };
    for (size_t i = 0; i < sizeof(grants) / sizeof(grants[0]); ++i) {
        char json[512];
        snprintf(json, sizeof(json),
            "{\"id\":\"t\",\"name\":\"T\",\"api\":1,\"kind\":\"voyage\","
            "\"entry\":\"main.lua\",\"location\":\"outer\",\"capabilities\":%s,"
            "\"state_version\":1}",
            grants[i].capabilities);
        BoatPluginStatus status
            = boat_plugin_parse_manifest(json, strlen(json), NULL, &manifest, &d);
        CHECK(status == BOAT_PLUGIN_MANIFEST);
        CHECK(!strcmp(d.field, grants[i].field));
    }
    static const char canonical[]
        = "{\"id\":\"t\",\"name\":\"T\",\"api\":1,"
          "\"kind\":\"voyage\",\"entry\":\"main.lua\",\"location\":\"outer\","
          "\"capabilities\":[\"voyage.define\",\"scene.define\",\"objective.hud\","
          "\"objective.timers\"],\"state_version\":9}";
    CHECK(boat_plugin_parse_manifest(canonical, strlen(canonical), NULL, &manifest, &d)
        == BOAT_PLUGIN_OK);
    CHECK(manifest.scene && manifest.objective && manifest.timers && manifest.state_version == 9);

    // Typed definition validation: planned fields are strict before L2 exposes them in Lua.
    BoatSceneDefinition scene = { .version = BOAT_SCENE_VERSION };
    BoatSceneDiagnostic sd;
    CHECK(boat_scene_validate(&scene, &sd));
    CHECK(!boat_scene_has_content(&scene));
    uint64_t empty = boat_scene_digest(&scene);
    scene.berths_present = true;
    scene.berth_count = 1;
    scene.entrance_present = true;
    scene.entrance_x = 6;
    scene.entrance_z = 168;
    scene.entrance_width = 8;
    strcpy(scene.berths[0].id, "a");
    scene.berths[0].recipe = BOAT_SCENE_BERTH_SKIFF;
    scene.berths[0].mooring = BOAT_SCENE_MOORING_PONTOON;
    scene.berths[0].x = 13;
    scene.berths[0].z = 167;
    scene.berths[0].heading = -1.5707964f;
    CHECK(boat_scene_validate(&scene, &sd));
    CHECK(boat_scene_has_content(&scene));
    CHECK(boat_scene_digest(&scene) != empty);
    CHECK(!strcmp(boat_scene_berth_recipe_name(BOAT_SCENE_BERTH_SAILBOAT_7M), "sailboat_7m"));
    BoatSceneBerthRecipe recipe;
    CHECK(boat_scene_berth_recipe_from_name("workboat", &recipe)
        && recipe == BOAT_SCENE_BERTH_WORKBOAT);
    CHECK(!boat_scene_berth_recipe_from_name("yacht", &recipe));
    CHECK(boat_scene_berth_spec(BOAT_SCENE_BERTH_SAILBOAT_7M).length == 7);

    scene.berth_count = 2;
    scene.berths[1] = scene.berths[0];
    CHECK(!boat_scene_validate(&scene, &sd) && !strcmp(sd.field, "scene.marina.berths[2].id"));
    strcpy(scene.berths[1].id, "b");
    scene.berths[1].x = 30000;
    CHECK(!boat_scene_validate(&scene, &sd) && !strcmp(sd.field, "scene.marina.berths[2].x"));
    scene.berths[1].x = 13;
    scene.berths[1].recipe = BOAT_SCENE_BERTH_RECIPES;
    CHECK(!boat_scene_validate(&scene, &sd) && !strcmp(sd.field, "scene.marina.berths[2].recipe"));
    scene.berths[1].recipe = BOAT_SCENE_BERTH_RUNABOUT;
    scene.berth_count = BOAT_SCENE_BERTHS + 1;
    CHECK(!boat_scene_validate(&scene, &sd) && !strcmp(sd.field, "scene.marina.berths"));
    scene.berth_count = 1;

    scene.modules_present = true;
    scene.module_count = 1;
    strcpy(scene.modules[0].id, "ramp");
    scene.modules[0].kind = BOAT_SCENE_MODULE_GANGWAY;
    scene.modules[0].length = 12.925f;
    scene.modules[0].width = 1.5f;
    scene.modules[0].deck_height = .74f;
    scene.modules[0].x = scene.modules[0].anchor_x = 34.1f;
    scene.modules[0].z = scene.modules[0].anchor_z = 170;
    strcpy(scene.modules[0].attaches, "missing");
    CHECK(
        !boat_scene_validate(&scene, &sd) && !strcmp(sd.field, "scene.marina.modules[1].attaches"));
    CHECK(boat_scene_find_berth(&scene, "a") == &scene.berths[0]);
    strcpy(scene.modules[0].id, "a"); // IDs are unique across berths and modules.
    CHECK(!boat_scene_validate(&scene, &sd) && !strcmp(sd.field, "scene.marina.modules[1].id"));
    strcpy(scene.modules[0].id, "ramp");
    scene.module_count = BOAT_SCENE_MODULES + 1;
    CHECK(!boat_scene_validate(&scene, &sd) && !strcmp(sd.field, "scene.marina.modules"));
    return 0;
}

// L2.2: the seven-berth Eriksberg package decodes, validates and carries stable IDs. Invalid
// geometry is rejected with a field path; an invalid candidate never writes the caller's
// definition.
// L3.2: the harbor-life package's authored passing craft and retained default, with semantic
// failures for route selection, craft recipe, speed/period, capacity and conflicts.
static const char *harbor_variant(const char *from, const char *to)
{
    static char text[BOAT_PLUGIN_SOURCE_MAX + 1];
    static BoatPluginFiles files;
    BoatPluginDiagnostic d;
    if (boat_plugin_read("plugins/harbor-life", NULL, &files, &d) != BOAT_PLUGIN_OK)
        return NULL;
    memcpy(text, files.source, files.source_size);
    text[files.source_size] = 0;
    char *at = strstr(text, from);
    if (!at)
        return NULL;
    size_t old = strlen(from), replacement = strlen(to);
    if (strlen(text) - old + replacement >= sizeof(text))
        return NULL;
    memmove(at + replacement, at + old, strlen(at + old) + 1);
    memcpy(at, to, replacement);
    return text;
}

static int scene_traffic(void)
{
    static BoatPluginFiles files;
    BoatPluginDiagnostic d;
    CHECK(boat_plugin_read("plugins/harbor-life", NULL, &files, &d) == BOAT_PLUGIN_OK);
    BoatPluginBundle bundle = boat_plugin_bundle(&files);
    BoatPluginVoyage voyage;
    CHECK(boat_plugin_load_voyage(&bundle, NULL, &voyage, &d) == BOAT_PLUGIN_OK);
    CHECK(voyage.scene.traffic_present && voyage.scene.traffic.actor_count == 4);
    const WaterfrontRouteActor *passing = &voyage.scene.traffic.actors[0];
    CHECK(!strcmp(passing->id, "passing") && passing->recipe == WATERFRONT_CRAFT_RUNABOUT);
    CHECK(passing->shape == WATERFRONT_ROUTE_POINTS && passing->point_count == 4);
    CHECK(passing->speed == 4 && passing->period > 24 && passing->period < 26);
    CHECK(passing->retain == 0xff);
    const WaterfrontRouteActor *ferry = &voyage.scene.traffic.actors[1];
    CHECK(!strcmp(ferry->id, "ferry") && ferry->recipe == WATERFRONT_CRAFT_ALVSNABBEN);
    CHECK(ferry->stop_count == 3 && ferry->stops[0].dwell == 35);
    CHECK(!strcmp(ferry->stops[0].id, "stenpiren") && !strcmp(ferry->stops[1].id, "lindholmspiren")
        && !strcmp(ferry->stops[2].id, "eriksberg"));
    CHECK(ferry->stops[1].dwell < 0 && ferry->stops[2].dwell < 0);
    CHECK(!strcmp(voyage.scene.traffic.actors[2].id, "river_runabout")
        && voyage.scene.traffic.actors[2].retain == 0);
    CHECK(!strcmp(voyage.scene.traffic.actors[3].id, "schooner")
        && voyage.scene.traffic.actors[3].retain == WATERFRONT_TRAFFIC_VESSELS + 3);
    CHECK(waterfront_route_has_bridge(&voyage.scene.traffic));
    CHECK(voyage.scene.gulls_present && voyage.scene.gulls.flock_count == 1);
    const WaterfrontFlock *flock = &voyage.scene.gulls.flocks[0];
    CHECK(!strcmp(flock->id, "harbor") && flock->recipe == WATERFRONT_FLOCK_GULL);
    CHECK(flock->count == 8 && flock->seed == 3 && flock->phase == 0);
    CHECK(flock->cue_count == 2 && flock->cues[0].start_tick == 240
        && flock->cues[0].kind == WATERFRONT_FLOCK_CUE_FEEDING
        && flock->cues[1].kind == WATERFRONT_FLOCK_CUE_ALARM);
    CHECK(flock->cues[1].end_tick == 1200 && flock->cues[1].amplitude == 1);
    CHECK(voyage.scene_digest == boat_scene_digest(&voyage.scene));
    WaterfrontVesselMotion start
        = waterfront_traffic_motion_routes(&voyage.scene.traffic, 0, passing->phase);
    WaterfrontVesselMotion lap = waterfront_traffic_motion_routes(
        &voyage.scene.traffic, 0, passing->phase + passing->period);
    CHECK(Vector3Distance(start.position, lap.position) < .01f);
    BoatWorldResources resources;
    CHECK(boat_world_resources_init(&resources, boat_location_find("outer")));
    CHECK(boat_plugin_check_location(&voyage, resources.definition, &resources.map, &d)
        == BOAT_PLUGIN_OK);

    struct {
        const char *from, *to, *field;
    } cases[] = {
        { "id = \"passing\", recipe = \"runabout\"", "id = \"passing\", recipe = \"yacht\"",
            "scene.traffic.vessels[1].recipe" },
        { "route = \"marina_pass\"", "route = \"missing_route\"",
            "scene.traffic.vessels[1].route" },
        { "speed = 4, phase = 0", "speed = 0, phase = 0", "scene.traffic.vessels[1].speed" },
        { "period = 24.8540", "period = 1", "scene.traffic.vessels[1].period" },
        { "{ id = \"stenpiren\", dwell = 35 }", "{ id = \"wrong_pier\", dwell = 35 }",
            "scene.traffic.vessels[2].stops[1]" },
        { "dwell = 35", "dwell = -1", "scene.traffic.vessels[2].stops[1].dwell" },
        { "recipe = \"gull\"", "recipe = \"seal\"", "scene.gulls.flocks[1].recipe" },
        { "count = 8", "count = 9", "scene.gulls.flocks[1].count" },
        { "kind = \"alarm\"", "kind = \"storm\"", "scene.gulls.flocks[1].cues[2].kind" },
        { "start = 900", "start = 1200", "scene.gulls.flocks[1].cues" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const char *source = harbor_variant(cases[i].from, cases[i].to);
        CHECK(source);
        BoatPluginStatus status = load(files.manifest, source, strlen(source), NULL, &voyage, &d);
        CHECK(status == BOAT_PLUGIN_SCHEMA);
        CHECK(!strcmp(d.field, cases[i].field));
    }
    // A non-retained authored copy of the compiled river actor is a real schedule conflict.
    const char *conflict = harbor_variant(
        "route = \"marina_pass\",\n          speed = 4, phase = 0, period = 24.8540",
        "route = \"river_runabout\",\n          speed = 2.2, phase = 70, period = 158.165");
    CHECK(conflict);
    CHECK(load(files.manifest, conflict, strlen(conflict), NULL, &voyage, &d) == BOAT_PLUGIN_OK);
    CHECK(boat_plugin_check_location(&voyage, resources.definition, &resources.map, &d)
        == BOAT_PLUGIN_SCHEMA);
    CHECK(!strcmp(d.field, "scene.traffic.vessels[1]"));
    boat_world_resources_destroy(&resources);

    // Native definition rejection: capacity, zero speed for an authored craft, bad geometry,
    // an uncrossable bridge actor and a discontinuous loop.
    BoatSceneDefinition scene = { .version = BOAT_SCENE_VERSION, .traffic_present = true };
    WaterfrontRouteDiagnostic route;
    CHECK(waterfront_route_preset("marina_pass", &scene.traffic.actors[0]));
    scene.traffic.actor_count = 1;
    BoatSceneDiagnostic sd;
    CHECK(boat_scene_validate(&scene, &sd));
    scene.traffic.actors[0].speed = 0;
    CHECK(!boat_scene_validate(&scene, &sd));
    scene.traffic.actors[0].speed = 4;
    scene.traffic.actors[0].points[0].radius = -1;
    CHECK(!waterfront_route_validate(&scene.traffic, &route));
    scene.traffic.actors[0].points[0].radius = 6;
    scene.traffic.actors[0].period = 1;
    CHECK(!waterfront_route_validate(&scene.traffic, &route));
    scene.traffic.actors[0].period = 24.8540;
    scene.traffic.actor_count = WATERFRONT_ROUTE_ACTORS + 1;
    CHECK(!waterfront_route_validate(&scene.traffic, &route));
    return 0;
}

static int scene_berths(void)
{
    static BoatPluginFiles files;
    BoatPluginDiagnostic d;
    CHECK(boat_plugin_read("plugins/harbor-life", NULL, &files, &d) == BOAT_PLUGIN_OK);
    BoatPluginBundle bundle = boat_plugin_bundle(&files);
    BoatPluginVoyage voyage;
    CHECK(boat_plugin_load_voyage(&bundle, NULL, &voyage, &d) == BOAT_PLUGIN_OK);
    CHECK(voyage.has_scene && voyage.scene.version == BOAT_SCENE_VERSION);
    CHECK(voyage.scene.berths_present && voyage.scene.berth_count == 12);
    CHECK(!strcmp(voyage.scene.berths[0].id, "sailboat_aft")
        && voyage.scene.berths[0].recipe == BOAT_SCENE_BERTH_SAILBOAT_7M
        && voyage.scene.berths[0].mooring == BOAT_SCENE_MOORING_PONTOON);
    CHECK(!strcmp(voyage.scene.berths[1].id, "runabout")
        && voyage.scene.berths[1].recipe == BOAT_SCENE_BERTH_RUNABOUT);
    CHECK(!strcmp(voyage.scene.berths[6].id, "sailboat_fore")
        && voyage.scene.berths[6].recipe == BOAT_SCENE_BERTH_SAILBOAT_7M);
    CHECK(voyage.scene.berths[0].x == 13 && voyage.scene.berths[0].z == 146
        && voyage.scene.berths[6].z == 176);
    CHECK(voyage.scene.entrance_present && voyage.scene.entrance_x == 4
        && voyage.scene.entrance_z == 140 && voyage.scene.entrance_width == 8);
    CHECK(voyage.scene.modules_present && voyage.scene.module_count == 13);
    CHECK(!strcmp(voyage.scene.modules[0].id, "pier")
        && voyage.scene.modules[0].kind == BOAT_SCENE_MODULE_WALKWAY);
    CHECK(!strcmp(voyage.scene.modules[1].id, "ramp")
        && voyage.scene.modules[1].kind == BOAT_SCENE_MODULE_GANGWAY
        && !strcmp(voyage.scene.modules[1].attaches, "pier"));
    CHECK(!strcmp(voyage.scene.modules[2].id, "works")
        && voyage.scene.modules[2].kind == BOAT_SCENE_MODULE_QUAY);
    CHECK(voyage.scene_digest != 0 && voyage.scene_digest == boat_scene_digest(&voyage.scene));
    BoatWorldResources resources;
    CHECK(boat_world_resources_init(&resources, boat_location_find("outer")));
    CHECK(boat_plugin_check_location(&voyage, resources.definition, &resources.map, &d)
        == BOAT_PLUGIN_OK);

    static char source[BOAT_PLUGIN_SOURCE_MAX + 1];
    // A single fragment replacement in the harbor-life source.
    struct {
        const char *from, *to, *field;
        bool location;
    } cases[] = {
        { "marina = {", "marina = { pontoons = {},", "scene.marina.pontoons", false },
        { "      entrance = { x = 4, z = 140, width = 8 },\n", "", "scene.marina.entrance", false },
        { "id = \"runabout\"", "id = \"sailboat_aft\"", "scene.marina.berths[2].id", false },
        { "recipe = \"runabout\"", "recipe = \"yacht\"", "scene.marina.berths[2].recipe", false },
        { "mooring = \"pontoon\"", "mooring = \"quay\"", "scene.marina.berths[1].mooring", false },
        { "x = 13, z = 176", "x = 32, z = 176", "scene.marina.berths[7]", true },
        { "x = 13, z = 176", "x = 13, z = 171", "scene.marina.berths[7]", true },
        { "entrance = { x = 4, z = 140, width = 8 }", "entrance = { x = 13, z = 146, width = 8 }",
            "scene.marina.berths[1]", true },
        { "x = 13, z = 146", "x = -12, z = 60", "scene.marina.berths[1]", true },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        memcpy(source, files.source, files.source_size);
        source[files.source_size] = 0;
        char *at = strstr(source, cases[i].from);
        CHECK(at);
        size_t from_size = strlen(cases[i].from), to_size = strlen(cases[i].to);
        CHECK(files.source_size - from_size + to_size <= BOAT_PLUGIN_SOURCE_MAX);
        memmove(
            at + to_size, at + from_size, files.source_size - (size_t)(at - source) - from_size);
        memcpy(at, cases[i].to, to_size);
        size_t size = files.source_size - from_size + to_size;
        source[size] = 0;
        BoatPluginVoyage candidate;
        memset(&candidate, 0x5a, sizeof(candidate));
        BoatPluginVoyage before = candidate;
        BoatPluginStatus status = load(files.manifest, source, size, NULL, &candidate, &d);
        show(cases[i].field, &d);
        if (!cases[i].location) {
            CHECK(status == BOAT_PLUGIN_SCHEMA && !strcmp(d.field, cases[i].field));
            CHECK(!memcmp(&candidate, &before, sizeof(candidate)));
        } else {
            CHECK(status == BOAT_PLUGIN_OK);
            CHECK(boat_plugin_check_location(&candidate, resources.definition, &resources.map, &d)
                == BOAT_PLUGIN_SCHEMA);
            show(cases[i].field, &d);
            CHECK(!strcmp(d.field, cases[i].field));
        }
    }
    // Capacity + 1 is rejected by the Lua list decoder before native validation.
    BoatPluginVoyage too_many;
    const char *capacity = variant("return {",
        "return { scene = { version = 2, marina = { berths = (function() local t={} for i=1,33 do "
        "t[i]={} end return t end)() } },");
    CHECK(load(files.manifest, capacity, strlen(capacity), NULL, &too_many, &d)
        == BOAT_PLUGIN_SCHEMA);
    CHECK(!strcmp(d.field, "scene.marina.berths"));
    capacity = variant("return {",
        "return { scene = { version = 2, marina = { modules = (function() local t={} for i=1,65 do "
        "t[i]={} end return t end)() } },");
    CHECK(load(files.manifest, capacity, strlen(capacity), NULL, &too_many, &d)
        == BOAT_PLUGIN_SCHEMA);
    CHECK(!strcmp(d.field, "scene.marina.modules"));

    // L2.3 modules: the small pontoon and gangway decode; malformed, detached, overlapping and
    // over-budget modules reject with their field paths.
    struct {
        const char *from, *to, *field;
    } module_cases[] = {
        { "kind = \"walkway\"", "kind = \"barge\"", "scene.marina.modules[1].kind" },
        { "attaches = \"pier\"", "attaches = \"missing\"", "scene.marina.modules[2].attaches" },
        { "anchor = { x = 34.1, z = 161.5 }", "anchor = { x = 20, z = 161.5 }",
            "scene.marina.modules[2]" },
        { "anchor = { x = 34.1, z = 161.5 }", "anchor = { x = 45, z = 161.5 }",
            "scene.marina.modules[2]" },
        { "x = 20, z = 161.5", "x = 34, z = 161.5", "scene.marina.modules[6].attaches" },
        { "id = \"works\"", "id = \"pier\"", "scene.marina.modules[3].id" },

    };
    for (size_t i = 0; i < sizeof(module_cases) / sizeof(module_cases[0]); ++i) {
        memcpy(source, files.source, files.source_size);
        source[files.source_size] = 0;
        char *at = strstr(source, module_cases[i].from);
        CHECK(at);
        size_t from_size = strlen(module_cases[i].from), to_size = strlen(module_cases[i].to);
        CHECK(files.source_size - from_size + to_size <= BOAT_PLUGIN_SOURCE_MAX);
        memmove(
            at + to_size, at + from_size, files.source_size - (size_t)(at - source) - from_size);
        memcpy(at, module_cases[i].to, to_size);
        size_t size = files.source_size - from_size + to_size;
        source[size] = 0;
        BoatPluginVoyage candidate;
        BoatPluginStatus status = load(files.manifest, source, size, NULL, &candidate, &d);
        show(module_cases[i].field, &d);
        if (!strcmp(module_cases[i].field, "scene.marina.modules[1].kind")
            || !strcmp(module_cases[i].field, "scene.marina.modules[2].attaches")
            || !strcmp(module_cases[i].field, "scene.marina.modules[6].attaches")
            || !strcmp(module_cases[i].field, "scene.marina.modules[3].id")
            || !strcmp(module_cases[i].field, "scene.marina.modules"))
            CHECK(status == BOAT_PLUGIN_SCHEMA && !strcmp(d.field, module_cases[i].field));
        else {
            CHECK(status == BOAT_PLUGIN_OK);
            CHECK(boat_plugin_check_location(&candidate, resources.definition, &resources.map, &d)
                == BOAT_PLUGIN_SCHEMA);
            CHECK(!strcmp(d.field, module_cases[i].field));
        }
    }
    boat_world_resources_destroy(&resources);
    return 0;
}

// The package on disk decodes field for field, bit for bit, to the compiled route, and its names
// outlive the Lua state and the source bytes.
static int reference_matches_compiled(void)
{
    BoatPluginFiles *files = malloc(sizeof(*files));
    CHECK(files);
    BoatPluginDiagnostic d;
    CHECK(boat_plugin_read(REFERENCE, NULL, files, &d) == BOAT_PLUGIN_OK);
    BoatPluginBundle bundle = boat_plugin_bundle(files);
    BoatPluginVoyage voyage;
    BoatPluginStatus status = boat_plugin_load_voyage(&bundle, NULL, &voyage, &d);
    show("reference", &d);
    CHECK(status == BOAT_PLUGIN_OK);
    memset(files, 0, sizeof(*files));
    free(files);

    CHECK(!strcmp(voyage.manifest.id, "proteus.langedrag"));
    CHECK(!strcmp(voyage.manifest.location, "outer") && voyage.manifest.api == 1);
    static const char *const ids[] = { "dock", "bridge", "east_shore", "langedrag" };
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(!strcmp(voyage.shelter_ids[i], ids[i]));
        CHECK(voyage.route.shelters[i].name == voyage.shelter_names[i]);
    }
    if (same_route(&voyage.route, &boat_fixture_voyage_outer))
        return 1;
    CHECK(!strcmp(boat_voyage_destination(&voyage.route), "LANGEDRAG"));
    uint64_t compiled = boat_plugin_route_digest(&boat_fixture_voyage_outer);
    CHECK(voyage.digest == compiled);

    // A copy owns its names: scribbling over the original leaves it intact.
    BoatPluginVoyage copy;
    boat_plugin_voyage_copy(&copy, &voyage);
    memset(&voyage, 0xa5, sizeof(voyage));
    CHECK(copy.route.shelters[2].name == copy.shelter_names[2]);
    if (same_route(&copy.route, &boat_fixture_voyage_outer))
        return 1;
    CHECK(boat_plugin_route_digest(&copy.route) == compiled);
    printf("boat_plugin digest=%016llx compiled=%016llx instructions<%llu peak_bytes=%zu "
           "fields=%u\n",
        (unsigned long long)copy.digest, (unsigned long long)compiled,
        (unsigned long long)(d.cost.instructions + BOAT_PLUGIN_HOOK_STEP), d.cost.peak_bytes,
        d.cost.fields);
    return 0;
}

// The allowed libraries can compute the same definition.
static int computed_definition(void)
{
    static const char computed[]
        = "local names = { 'DOCK', 'BRIDGE', 'EAST SHORE', 'LANGEDRAG' }\n"
          "local ids = { 'dock', 'bridge', 'east_shore', 'langedrag' }\n"
          "local places = { { -12, 60, 0, 60 }, { 650, -850, 1.33, 150 },\n"
          "  { 3250, -2000, .86, 200 }, { 3600, -3370, 1.57, 0 } }\n"
          "local shelters = {}\n"
          "for i, p in ipairs(places) do\n"
          "  assert(string.upper(ids[i]):sub(1, 4) == names[i]:sub(1, 4))\n"
          "  table.insert(shelters, { id = ids[i], name = names[i], x = p[1], z = p[2],\n"
          "    heading = p[3], radius = p[4] })\n"
          "end\n"
          "local centre = { x = math.floor(3595.5), z = -3370 }\n"
          "return { voyage = { shelters = shelters,\n"
          "  arrival = { x = centre.x, z = centre.z, radius = 75, max_speed = 6 },\n"
          "  exposure = { x = 4300, z = -2650, radius = 700, height = 1.6, build = 60 },\n"
          "  exposed_fraction = 0.8, weather_range = 2500 } }\n";
    BoatPluginVoyage voyage;
    BoatPluginDiagnostic d;
    BoatPluginStatus status = load_source(computed, &voyage, &d);
    show("computed", &d);
    CHECK(status == BOAT_PLUGIN_OK);
    CHECK(voyage.digest == boat_plugin_route_digest(&boat_fixture_voyage_outer));
    return 0;
}

static int syntax_errors(void)
{
    if (expect("unclosed", NULL, "return {", BOAT_PLUGIN_SYNTAX, "", 1)
        || expect("line three", NULL, "return {\n  voyage = {\n    shelters = = 1\n",
            BOAT_PLUGIN_SYNTAX, "", 3)
        || expect("bytecode", NULL, "\x1bLuaT\x00\x19\x93\r\n\x1a\n", BOAT_PLUGIN_SYNTAX, "", -1))
        return 1;
    // Nesting deeper than the compiler's C-level limit fails cleanly.
    static char deep[1300];
    strcpy(deep, "return ");
    for (int i = 0; i < 600; ++i)
        strcat(deep, "{");
    for (int i = 0; i < 600; ++i)
        strcat(deep, "}");
    return expect("deep", NULL, deep, BOAT_PLUGIN_SYNTAX, "", -1);
}

static int runtime_errors(void)
{
    return expect("index nil", NULL, "local t = nil\nreturn t.voyage", BOAT_PLUGIN_RUNTIME, "", 2)
        || expect("error", NULL, "\n\nerror('authored failure')", BOAT_PLUGIN_RUNTIME, "", 3)
        || expect("error table", NULL, "error({})", BOAT_PLUGIN_RUNTIME, "", 0);
}

// Every facility outside the allowlist is absent: calling it is a runtime error on its line.
static int forbidden_facilities(void)
{
    static const char *const calls[] = { "io.write('x')", "os.exit(1)", "require('os')",
        "load('return 1')()", "loadfile('main.lua')", "dofile('main.lua')", "print('x')",
        "string.dump(type)", "debug.traceback()", "package.loadlib('x', 'y')",
        "coroutine.create(type)", "utf8.char(65)", "pcall(error)", "xpcall(error, type)",
        "setmetatable({}, {})", "getmetatable('x').__index.rep('x', 1)", "collectgarbage()",
        "rawset({}, 1, 1)", "math.random()", "math.randomseed(1)", "string.find('a', 'a')",
        "('a'):gsub('a', 'b')", "string.gmatch('a', 'a')", "string.match('a', 'a')",
        "table.move({}, 1, 1, 1)", "warn('x')" };
    for (size_t i = 0; i < sizeof(calls) / sizeof(calls[0]); ++i) {
        char source[128];
        snprintf(source, sizeof(source), "local _ = 1\n%s\nreturn {}", calls[i]);
        if (expect(calls[i], NULL, source, BOAT_PLUGIN_RUNTIME, "", 2))
            return 1;
    }
    return 0;
}

static int invalid_fields(void)
{
    static const struct {
        const char *from, *to, *field;
    } cases[] = {
        { "heading = 1.33", "heading = 0/0", "voyage.shelters[2].heading" },
        { "heading = 1.33", "heading = 1/0", "voyage.shelters[2].heading" },
        { "heading = 1.33", "heading = 7", "voyage.shelters[2].heading" },
        { "x = 3250", "x = 30000", "voyage.shelters[3].x" },
        { "radius = 150", "radius = '150'", "voyage.shelters[2].radius" },
        { "radius = 150", "radius = 0", "voyage.shelters[2].radius" },
        { "id = \"bridge\"", "id = \"dock\"", "voyage.shelters[2].id" },
        { "id = \"bridge\"", "id = \"Bridge\"", "voyage.shelters[2].id" },
        { "id = \"bridge\"", "id = 7", "voyage.shelters[2].id" },
        { "name = \"BRIDGE\"", "name = \"Bridge\"", "voyage.shelters[2].name" },
        { "name = \"BRIDGE\"", "name = \"BRIDGE\\0\"", "voyage.shelters[2].name" },
        { "name = \"BRIDGE\"", "name = \"\"", "voyage.shelters[2].name" },
        { "name = \"BRIDGE\"", "name = \"ALVSBORGSBRON AND BEYOND\"", "voyage.shelters[2].name" },
        { "name = \"BRIDGE\"", "name = \"BRIDGE\", colour = 1", "voyage.shelters[2].colour" },
        { "heading = 1.57, radius = 0", "heading = 1.57, radius = 5", "voyage.shelters[4].radius" },
        { "arrival = {", "arrivals = {", "voyage.arrivals" },
        { "arrival = { x = 3595, z = -3370, radius = 75, max_speed = 6 },", "", "voyage.arrival" },
        { "max_speed = 6", "max_speed = 0", "voyage.arrival.max_speed" },
        { "max_speed = 6", "", "voyage.arrival.max_speed" },
        { "x = 3595", "x = 3000", "voyage.shelters[4]" },
        { "radius = 700", "radius = 3000", "voyage.shelters[3]" },
        { "height = 1.6", "height = -1", "voyage.exposure.height" },
        { "exposed_fraction = 0.8", "exposed_fraction = 2", "voyage.exposed_fraction" },
        { "weather_range = 2500", "weather_range = true", "voyage.weather_range" },
        { "shelters = {", "shelters = { [7] = 1,", "voyage.shelters" },
        { "return {", "return { extra = 1,", "extra" },
        { "return {", "return 5 or {", "" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        if (expect(cases[i].to, NULL, variant(cases[i].from, cases[i].to), BOAT_PLUGIN_SCHEMA,
                cases[i].field, 0))
            return 1;
    // Shelter counts outside 2..4 and lists with gaps.
    static const char *const lists[] = {
        "{ { id = 'a', name = 'A', x = 3600, z = -3370, heading = 0, radius = 0 } }",
        ("{ { id = 'a', name = 'A', x = 0, z = 0, heading = 0, radius = 9 },"
         "  { id = 'b', name = 'B', x = 0, z = 0, heading = 0, radius = 9 },"
         "  { id = 'c', name = 'C', x = 0, z = 0, heading = 0, radius = 9 },"
         "  { id = 'd', name = 'D', x = 0, z = 0, heading = 0, radius = 9 },"
         "  { id = 'e', name = 'E', x = 3600, z = -3370, heading = 0, radius = 0 } }"),
        ("{ [1] = { id = 'a', name = 'A', x = 0, z = 0, heading = 0, radius = 9 },"
         "  [3] = { id = 'c', name = 'C', x = 3600, z = -3370, heading = 0, radius = 0 } }"),
    };
    for (size_t i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i) {
        char source[1024];
        snprintf(source, sizeof(source),
            "return { voyage = { shelters = %s,\n"
            "  arrival = { x = 3595, z = -3370, radius = 75, max_speed = 6 },\n"
            "  exposure = { x = 4300, z = -2650, radius = 700, height = 1.6, build = 60 },\n"
            "  exposed_fraction = 0.8, weather_range = 2500 } }",
            lists[i]);
        if (expect("shelter list", NULL, source, BOAT_PLUGIN_SCHEMA, "voyage.shelters", 0))
            return 1;
    }
    // Arbitrarily deep data is never walked: only the schema's own levels are read.
    return expect("deep data", NULL,
        "local t = {}\nfor i = 1, 1000 do t = { t } end\nreturn { voyage = t }", BOAT_PLUGIN_SCHEMA,
        "voyage", 0);
}

static int manifests(void)
{
    static const struct {
        const char *manifest, *field;
    } cases[] = {
        { "{\"id\": \"x\"", "" },
        { "[1, 2]", "" },
        { "{\"id\": \"a\", \"id\": \"b\", \"name\": \"N\", \"api\": 1, \"kind\": \"voyage\", "
          "\"entry\": \"main.lua\", \"location\": \"outer\", \"capabilities\": "
          "[\"voyage.define\"], \"state_version\": 1}",
            "id" },
        { "{\"id\": \"a\", \"name\": \"N\", \"api\": 1, \"kind\": \"voyage\", \"entry\": "
          "\"main.lua\", \"location\": \"outer\", \"capabilities\": [\"voyage.define\"], "
          "\"state_version\": 1, \"limits\": {\"memory\": 99999999}}",
            "limits" },
        { "{\"name\": \"N\", \"api\": 1, \"kind\": \"voyage\", \"entry\": \"main.lua\", "
          "\"location\": \"outer\", \"capabilities\": [\"voyage.define\"], \"state_version\": 1}",
            "id" },
        { "{\"id\": \"A B\", \"name\": \"N\", \"api\": 1, \"kind\": \"voyage\", \"entry\": "
          "\"main.lua\", \"location\": \"outer\", \"capabilities\": [\"voyage.define\"], "
          "\"state_version\": 1}",
            "id" },
        { "{\"id\": \"a\", \"name\": \"N\", \"api\": 2, \"kind\": \"voyage\", \"entry\": "
          "\"main.lua\", \"location\": \"outer\", \"capabilities\": [\"voyage.define\"], "
          "\"state_version\": 1}",
            "api" },
        { "{\"id\": \"a\", \"name\": \"N\", \"api\": 1, \"kind\": \"unsupported\", \"entry\": "
          "\"main.lua\", \"location\": \"outer\", \"capabilities\": [\"voyage.define\"], "
          "\"state_version\": 1}",
            "kind" },
        { "{\"id\": \"a\", \"name\": \"N\", \"api\": 1, \"kind\": \"voyage\", \"entry\": "
          "\"../main.lua\", \"location\": \"outer\", \"capabilities\": [\"voyage.define\"], "
          "\"state_version\": 1}",
            "entry" },
        { "{\"id\": \"a\", \"name\": \"N\", \"api\": 1, \"kind\": \"voyage\", \"entry\": "
          "\"main.lua\", \"location\": \"river\", \"capabilities\": [\"voyage.define\"], "
          "\"state_version\": 1}",
            "location" },
        { "{\"id\": \"a\", \"name\": \"N\", \"api\": 1, \"kind\": \"voyage\", \"entry\": "
          "\"main.lua\", \"location\": \"outer\", \"capabilities\": [\"net.socket\"], "
          "\"state_version\": 1}",
            "capabilities[1]" },
        { "{\"id\": \"a\", \"name\": \"N\", \"api\": 1, \"kind\": \"voyage\", \"entry\": "
          "\"main.lua\", \"location\": \"outer\", \"capabilities\": [\"voyage.define\", "
          "\"voyage.define\"], \"state_version\": 1}",
            "capabilities[2]" },
        { "{\"id\": \"a\", \"name\": \"N\", \"api\": 1, \"kind\": \"voyage\", \"entry\": "
          "\"main.lua\", \"location\": \"outer\", \"capabilities\": [\"voyage.define\"], "
          "\"state_version\": 0}",
            "state_version" },
    };
    const BoatPluginFiles *files = reference();
    char source[BOAT_PLUGIN_SOURCE_MAX + 1];
    memcpy(source, files->source, files->source_size);
    source[files->source_size] = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        if (expect(
                cases[i].field, cases[i].manifest, source, BOAT_PLUGIN_MANIFEST, cases[i].field, 0))
            return 1;
    // Sizes beyond the host's limits, which a package cannot raise.
    static char big[BOAT_PLUGIN_MANIFEST_MAX + 64];
    size_t prefix = strlen(MANIFEST);
    memcpy(big, MANIFEST, prefix);
    memset(big + prefix, ' ', sizeof(big) - 1 - prefix);
    if (expect("large manifest", big, source, BOAT_PLUGIN_LIMIT, "", 0))
        return 1;
    BoatPluginLimits small = boat_plugin_default_limits;
    small.source_bytes = 100;
    BoatPluginVoyage out;
    BoatPluginDiagnostic d;
    CHECK(load(MANIFEST, source, strlen(source), &small, &out, &d) == BOAT_PLUGIN_LIMIT);
    show("large source", &d);
    CHECK(!strcmp(d.file, "main.lua"));
    return 0;
}

static int memory_exhaustion(void)
{
    BoatPluginLimits limits = boat_plugin_default_limits;
    limits.instructions = UINT64_MAX; // Isolate the memory limit.
    static const char *const sources[] = { "local s = string.rep('x', 1 << 24)\nreturn {}",
        "local t = {}\nfor i = 1, 1e8 do t[i] = i end\nreturn t",
        "local s = 'x'\nwhile true do s = s .. s end" };
    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
        BoatPluginVoyage out;
        BoatPluginDiagnostic d;
        BoatPluginStatus status = load(MANIFEST, sources[i], strlen(sources[i]), &limits, &out, &d);
        show("memory", &d);
        CHECK(status == BOAT_PLUGIN_MEMORY);
        CHECK(d.cost.peak_bytes <= limits.memory_bytes && d.cost.leaked_bytes == 0);
    }
    // Too little memory even to compile.
    limits.memory_bytes = 4096;
    BoatPluginVoyage out;
    BoatPluginDiagnostic d;
    const BoatPluginFiles *files = reference();
    BoatPluginStatus status = load(MANIFEST, files->source, files->source_size, &limits, &out, &d);
    show("tiny heap", &d);
    CHECK(status == BOAT_PLUGIN_MEMORY && d.cost.leaked_bytes == 0);
    return 0;
}

// Nonterminating scripts stop on the instruction budget, deterministically and without a clock.
static int instruction_budget(void)
{
    static const char *const sources[]
        = { "\nwhile true do end", "\nlocal function f() return f() end return f()",
              "local n = 0\nrepeat n = n + 1 until n < 0" };
    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
        BoatPluginVoyage out;
        BoatPluginDiagnostic d;
        CHECK(load_source(sources[i], &out, &d) == BOAT_PLUGIN_BUDGET);
        show("budget", &d);
        CHECK(d.line == 2 && d.cost.leaked_bytes == 0);
        CHECK(d.cost.instructions > boat_plugin_default_limits.instructions
            && d.cost.instructions
                <= boat_plugin_default_limits.instructions + BOAT_PLUGIN_HOOK_STEP);
    }
    // Deep recursion exhausts the stack or the heap, never the process.
    BoatPluginVoyage out;
    BoatPluginDiagnostic d;
    BoatPluginStatus status
        = load_source("local function f() return 1 + f() end\nreturn f()", &out, &d);
    show("recursion", &d);
    CHECK(status == BOAT_PLUGIN_RUNTIME || status == BOAT_PLUGIN_MEMORY);
    CHECK(d.cost.leaked_bytes == 0);
    return 0;
}

// Failures leave a valid definition untouched, repeated loads leak nothing, and diagnostics are
// identical from load to load.
static int last_good_survives(void)
{
    const BoatPluginFiles *files = reference();
    BoatPluginBundle bundle = boat_plugin_bundle(files);
    BoatPluginVoyage good;
    BoatPluginDiagnostic d;
    CHECK(boat_plugin_load_voyage(&bundle, NULL, &good, &d) == BOAT_PLUGIN_OK);
    BoatPluginVoyage before;
    memcpy(&before, &good, sizeof(good));
    static const char *const failures[] = { "return {", "while true do end", "error('x')",
        "return { voyage = 1 }", "local s = string.rep('x', 1 << 24)",
        "return { voyage = {}, b = 1, c = 2, d = 3, e = 4 }" };
    BoatPluginDiagnostic first[sizeof(failures) / sizeof(failures[0])];
    for (int round = 0; round < 8; ++round)
        for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
            BoatPluginDiagnostic f;
            CHECK(load_source(failures[i], &good, &f) != BOAT_PLUGIN_OK);
            CHECK(f.cost.leaked_bytes == 0);
            if (round == 0)
                first[i] = f;
            CHECK(f.status == first[i].status && f.line == first[i].line
                && !strcmp(f.field, first[i].field) && !strcmp(f.message, first[i].message));
            CHECK(!memcmp(&good, &before, sizeof(good)));
            BoatPluginVoyage again;
            CHECK(boat_plugin_load_voyage(&bundle, NULL, &again, &d) == BOAT_PLUGIN_OK);
            CHECK(again.digest == good.digest && d.cost.leaked_bytes == 0);
        }
    return 0;
}

#ifndef _WIN32
static int write_file(const char *path, const char *text, size_t size)
{
    FILE *file = fopen(path, "wb");
    CHECK(file);
    CHECK(fwrite(text, 1, size, file) == size);
    CHECK(fclose(file) == 0);
    return 0;
}

// A fresh package directory under the build tree's scratch directory.
static const char *package(const char *name, const char *manifest, const char *source)
{
    static char directory[512];
    char path[600];
    snprintf(directory, sizeof(directory), "%s/%s", BOAT_PLUGIN_TEST_DIR, name);
    mkdir(directory, 0755);
    snprintf(path, sizeof(path), "%s/plugin.json", directory);
    unlink(path);
    if (manifest && write_file(path, manifest, strlen(manifest)))
        exit(1);
    snprintf(path, sizeof(path), "%s/main.lua", directory);
    unlink(path);
    rmdir(path);
    if (source && write_file(path, source, strlen(source)))
        exit(1);
    return directory;
}

static int read_status(const char *directory, BoatPluginStatus status, const char *file)
{
    BoatPluginFiles *files = malloc(sizeof(*files));
    CHECK(files);
    memset(files, 0x5a, sizeof(*files));
    BoatPluginDiagnostic d;
    BoatPluginStatus got = boat_plugin_read(directory, NULL, files, &d);
    show(directory, &d);
    bool untouched = got != BOAT_PLUGIN_OK && files->manifest_size == (size_t)0x5a5a5a5a5a5a5a5aull;
    free(files);
    CHECK(got == status);
    CHECK(got == BOAT_PLUGIN_OK || (untouched && !strcmp(d.file, file)));
    return 0;
}

static int package_files(void)
{
    char source[BOAT_PLUGIN_SOURCE_MAX + 1];
    const BoatPluginFiles *files = reference();
    memcpy(source, files->source, files->source_size);
    source[files->source_size] = 0;
    if (read_status(package("good", MANIFEST, source), BOAT_PLUGIN_OK, "")
        || read_status(package("missing_entry", MANIFEST, NULL), BOAT_PLUGIN_IO, "main.lua")
        || read_status(package("missing_manifest", NULL, source), BOAT_PLUGIN_IO, "plugin.json"))
        return 1;
    // An entry that escapes the directory is refused by the manifest before any read.
    char escape[sizeof(MANIFEST) + 16];
    snprintf(escape, sizeof(escape), "%s", MANIFEST);
    char *entry = strstr(escape, "\"main.lua\"");
    memmove(entry + 4, entry + 1, strlen(entry + 1) + 1);
    memcpy(entry, "\"../", 4); // "../main.lua"
    if (read_status(package("escape", escape, NULL), BOAT_PLUGIN_MANIFEST, "plugin.json"))
        return 1;
    // A symbolic link, even to a valid entry, and a directory are not regular files.
    char path[600], target[600];
    const char *linked = package("symlink", MANIFEST, NULL);
    snprintf(path, sizeof(path), "%s/main.lua", linked);
    snprintf(target, sizeof(target), "%s/good/main.lua", BOAT_PLUGIN_TEST_DIR);
    CHECK(symlink(target, path) == 0);
    if (read_status(linked, BOAT_PLUGIN_IO, "main.lua"))
        return 1;
    unlink(path);
    const char *folder = package("directory", MANIFEST, NULL);
    snprintf(path, sizeof(path), "%s/main.lua", folder);
    CHECK(mkdir(path, 0755) == 0);
    if (read_status(folder, BOAT_PLUGIN_IO, "main.lua"))
        return 1;
    rmdir(path);
    // One byte over the limit; exactly the limit is read.
    static char large[BOAT_PLUGIN_SOURCE_MAX + 2];
    memset(large, '-', BOAT_PLUGIN_SOURCE_MAX + 1);
    large[BOAT_PLUGIN_SOURCE_MAX + 1] = 0;
    if (read_status(package("oversized", MANIFEST, large), BOAT_PLUGIN_LIMIT, "main.lua"))
        return 1;
    large[BOAT_PLUGIN_SOURCE_MAX] = 0;
    return read_status(package("exact", MANIFEST, large), BOAT_PLUGIN_OK, "");
}
#endif

// The decoded shelters and arrival are open water of the outer map with room for the hull, and
// the start shelter is the location's spawn (as validate_boat_voyage requires of the route).
static int navigable_water(void)
{
    BoatPluginBundle bundle = boat_plugin_bundle(reference());
    BoatPluginVoyage voyage;
    BoatPluginDiagnostic d;
    CHECK(boat_plugin_load_voyage(&bundle, NULL, &voyage, &d) == BOAT_PLUGIN_OK);
    const BoatWorldDefinition *outer = boat_location(BOAT_LOCATION_OUTER);
    CHECK(!strcmp(voyage.manifest.location, "outer"));
    BoatWorldResources world;
    CHECK(boat_world_resources_init(&world, outer));
    const BoatVoyageRoute *route = &voyage.route;
    int failed = 0;
    if (route->shelters[0].position.x != outer->spawn.x
        || route->shelters[0].position.y != outer->spawn.y)
        failed = 1;
    for (unsigned i = 0; i < route->shelter_count; ++i)
        failed |= !boat_map_water(&world.map, route->shelters[i].position, 8);
    failed |= !boat_map_water(&world.map, route->arrival_centre, 20);
    boat_world_resources_destroy(&world);
    CHECK(!failed);
    return 0;
}

static const TestCase cases[] = {
    TEST_CASE(reference_matches_compiled),
    TEST_CASE(computed_definition),
    TEST_CASE(syntax_errors),
    TEST_CASE(runtime_errors),
    TEST_CASE(forbidden_facilities),
    TEST_CASE(invalid_fields),
    TEST_CASE(manifests),
    TEST_CASE(scene_contract),
    TEST_CASE(scene_berths),
    TEST_CASE(scene_traffic),
    TEST_CASE(memory_exhaustion),
    TEST_CASE(instruction_budget),
    TEST_CASE(last_good_survives),
#ifndef _WIN32
    TEST_CASE(package_files),
#endif
    TEST_CASE(navigable_water),
};

int main(int argc, char **argv)
{
    return test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
}
