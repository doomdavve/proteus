// Site fixture for the harbor-life Eriksberg marina. It mirrors the package layout in
// plugins/harbor-life/main.lua so the water/shore geometry can be checked without Lua, and uses
// the same registered hull and module recipes as staging.
#include "boat_location.h"
#include "boat_map.h"
#include "boat_scene_definition.h"
#include "boat_world.h"
#include "waterfront_harbor.h"

#include <math.h>
#include <stdio.h>

// Simulation metres: x = -east, z = north. Seven hulls lie in the finger slips of one long
// pontoon along the east bank; the passing craft's registered loop stays north of the pontoon.
static const struct {
    BoatSceneBerthRecipe recipe;
    const char *id;
    float x, z, heading;
} berths[] = {
    { BOAT_SCENE_BERTH_SAILBOAT_7M, "sailboat_aft", 13, 146, -3.14159265f / 2 },
    { BOAT_SCENE_BERTH_RUNABOUT, "runabout", 13, 151, -3.14159265f / 2 },
    { BOAT_SCENE_BERTH_SAILBOAT, "sailboat", 13, 156, -3.14159265f / 2 },
    { BOAT_SCENE_BERTH_CRUISER, "cruiser", 13, 161, -3.14159265f / 2 },
    { BOAT_SCENE_BERTH_SAILBOAT_7M, "sailboat_mid", 13, 166, -3.14159265f / 2 },
    { BOAT_SCENE_BERTH_SPEEDBOAT, "speedboat", 13, 171, -3.14159265f / 2 },
    { BOAT_SCENE_BERTH_SAILBOAT_7M, "sailboat_fore", 13, 176, -3.14159265f / 2 },
};
#define BERTH_COUNT (sizeof(berths) / sizeof(berths[0]))

static const struct {
    BoatSceneModuleKind kind;
    const char *id;
    float x, z, heading, anchor_x, anchor_z;
    const char *attaches;
} modules[] = {
    { BOAT_SCENE_MODULE_PONTOON, "pier", 20, 161.5f, 0, 0, 0, NULL },
    { BOAT_SCENE_MODULE_GANGWAY, "ramp", 0, 0, 0, 34.1f, 161.5f, "pier" },
    { BOAT_SCENE_MODULE_QUAY, "works", 38, 173, 0, 0, 0, NULL },
};
#define MODULE_COUNT (sizeof(modules) / sizeof(modules[0]))

static const float entrance_x = 4, entrance_z = 140, entrance_width = 8;
// A navigable boat pose facing east across the slips, used by --start and plugin anchors.
static const float bookmark_x = 2, bookmark_z = 161, bookmark_heading = 1.5707964f;

static int failures;

static void check(bool condition, const char *what)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s\n", what);
        ++failures;
    }
}

static bool water(const BoatMap *map, float x, float z, float clearance)
{
    return boat_map_water(map, (Vector2) { x, z }, clearance);
}

static void axes(float heading, Vector2 *length, Vector2 *beam)
{
    // Renderer heading 0 faces north (-z); forward is (sin, -cos).
    *length = (Vector2) { sinf(heading), -cosf(heading) };
    *beam = (Vector2) { cosf(heading), sinf(heading) };
}

static bool box_water(const BoatMap *map, float x, float z, float heading, float half_length,
    float half_beam, float clearance)
{
    Vector2 length, beam;
    axes(heading, &length, &beam);
    for (int end = -1; end <= 1; ++end)
        for (int side = -1; side <= 1; ++side) {
            Vector2 p = { x + length.x * end * half_length + beam.x * side * half_beam,
                z + length.y * end * half_length + beam.y * side * half_beam };
            if (!boat_map_water(map, p, clearance))
                return false;
        }
    return true;
}

static float extent(Vector2 axis, Vector2 length, Vector2 beam, float half_length, float half_beam)
{
    return half_length * fabsf(axis.x * length.x + axis.y * length.y)
        + half_beam * fabsf(axis.x * beam.x + axis.y * beam.y);
}

static bool overlap(float ax, float az, float ah, float ahl, float ahb, float bx, float bz,
    float bh, float bhl, float bhb, float margin)
{
    // Separating-axis test for two rotated rectangles in simulation X/Z.
    Vector2 al, ab, bl, bb;
    axes(ah, &al, &ab);
    axes(bh, &bl, &bb);
    const Vector2 axes[] = { al, ab, bl, bb };
    float dx = bx - ax, dz = bz - az;
    for (int i = 0; i < 4; ++i) {
        float separation = fabsf(dx * axes[i].x + dz * axes[i].y);
        float reach = extent(axes[i], al, ab, ahl, ahb) + extent(axes[i], bl, bb, bhl, bhb);
        if (separation > reach + margin)
            return false;
    }
    return true;
}

int main(void)
{
    BoatWorldResources resources;
    if (!boat_world_resources_init(&resources, boat_location_find("outer"))) {
        fprintf(stderr, "FAIL cannot prepare the outer map\n");
        return 1;
    }
    const BoatMap *map = &resources.map;
    BoatModelSpec specs[BERTH_COUNT];
    for (unsigned i = 0; i < BERTH_COUNT; ++i) {
        specs[i] = boat_scene_berth_spec(berths[i].recipe);
        // Native contact pads 0.45 m for wave attitude and fenders, plus 0.9 m longitudinal.
        char what[96];
        snprintf(what, sizeof(what), "berth %s hull is open water", berths[i].id);
        check(box_water(map, berths[i].x, berths[i].z, berths[i].heading,
                  specs[i].length * .5f + .9f, specs[i].beam * .5f + .45f, .5f),
            what);
        snprintf(what, sizeof(what), "berth %s centre has 4 m clear water", berths[i].id);
        check(water(map, berths[i].x, berths[i].z, 4), what);
    }
    for (unsigned i = 0; i < BERTH_COUNT; ++i)
        for (unsigned j = i + 1; j < BERTH_COUNT; ++j) {
            char what[96];
            snprintf(
                what, sizeof(what), "hulls %s and %s are separate", berths[i].id, berths[j].id);
            check(!overlap(berths[i].x, berths[i].z, berths[i].heading, specs[i].length * .5f,
                      specs[i].beam * .5f, berths[j].x, berths[j].z, berths[j].heading,
                      specs[j].length * .5f, specs[j].beam * .5f, .45f),
                what);
        }
    for (unsigned i = 0; i < MODULE_COUNT; ++i) {
        char what[96];
        if (modules[i].kind == BOAT_SCENE_MODULE_GANGWAY) {
            // The landing is the pontoon's east-side attachment in simulation X/Z.
            const float landing_x = 20 + 1.175f, landing_z = 161.5f;
            float span = hypotf(modules[i].anchor_x - landing_x, modules[i].anchor_z - landing_z);
            snprintf(what, sizeof(what), "gangway %s shore anchor is land", modules[i].id);
            check(!water(map, modules[i].anchor_x, modules[i].anchor_z, 0), what);
            snprintf(what, sizeof(what), "gangway %s landing is water", modules[i].id);
            check(water(map, landing_x, landing_z, 0), what);
            snprintf(what, sizeof(what), "gangway %s span is 2..20 m", modules[i].id);
            check(span >= 2 && span <= 20, what);
            continue;
        }
        WaterfrontModuleSpec spec = waterfront_harbor_module_spec(modules[i].kind);
        snprintf(what, sizeof(what), "module %s is placed on its required surface", modules[i].id);
        if (modules[i].kind == BOAT_SCENE_MODULE_QUAY)
            check(!water(map, modules[i].x, modules[i].z, 0), what);
        else
            check(box_water(map, modules[i].x, modules[i].z, modules[i].heading,
                      spec.length * .5f + .9f, spec.beam * .5f + .45f, .5f),
                what);
        for (unsigned b = 0; b < BERTH_COUNT; ++b) {
            snprintf(what, sizeof(what), "module %s clears berth %s", modules[i].id, berths[b].id);
            check(!overlap(modules[i].x, modules[i].z, modules[i].heading, spec.length * .5f,
                      spec.beam * .5f, berths[b].x, berths[b].z, berths[b].heading,
                      specs[b].length * .5f, specs[b].beam * .5f, .3f),
                what);
        }
    }
    check(water(map, entrance_x, entrance_z, entrance_width * .5f),
        "the declared entrance has half its width clear");
    for (float z = 132; z <= 180; z += 2) {
        char what[96];
        snprintf(what, sizeof(what), "approach lane at z %.0f is open water", z);
        check(water(map, entrance_x, z, 3), what);
        for (unsigned i = 0; i < BERTH_COUNT; ++i) {
            // A 2.4 m beam hull passing at the lane centre must clear the moored hulls.
            snprintf(
                what, sizeof(what), "approach lane clears berth %s at z %.0f", berths[i].id, z);
            check(
                !overlap(entrance_x, z, 0, 2.4f, 1.2f, berths[i].x, berths[i].z, berths[i].heading,
                    specs[i].length * .5f + .9f, specs[i].beam * .5f + .45f, 0),
                what);
        }
    }
    check(water(map, bookmark_x, bookmark_z, 3), "the camera bookmark is open water");
    check(sinf(bookmark_heading) > .9f, "the camera bookmark faces east across the marina");
    for (unsigned i = 0; i < BERTH_COUNT; ++i) {
        char what[96];
        snprintf(what, sizeof(what), "the camera bookmark clears berth %s", berths[i].id);
        check(
            !overlap(bookmark_x, bookmark_z, bookmark_heading, 2.4f, 1.2f, berths[i].x, berths[i].z,
                berths[i].heading, specs[i].length * .5f + .9f, specs[i].beam * .5f + .45f, .5f),
            what);
    }
    boat_world_resources_destroy(&resources);
    printf("scene_site berths=%u modules=%u bookmark=%.0f,%.0f failures=%d\n",
        (unsigned)BERTH_COUNT, (unsigned)MODULE_COUNT, (double)bookmark_x, (double)bookmark_z,
        failures);
    return failures ? 1 : 0;
}
