// L1.1 planned marina site fixture. It uses only compiled water and native hull recipes,
// so it can check the selected coordinates before any scene grant exists.
#include "boat_location.h"
#include "boat_map.h"
#include "boat_model.h"
#include "boat_world.h"

#include <math.h>
#include <stdio.h>

// Simulation metres: x = -east, z = north. The two berths reuse the first two compiled
// recipes and the compiled marina's positions, so their water clearance has a long history.
static const struct {
    BoatModelKind kind;
    const char *id;
    float x, z, heading;
} berths[] = {
    { BOAT_MODEL_SKIFF, "skiff", 13, 167, -3.14159265f / 2 },
    { BOAT_MODEL_RUNABOUT, "runabout", 13, 173, -3.14159265f / 2 },
};
#define BERTH_COUNT (sizeof(berths) / sizeof(berths[0]))

static const float pontoon_x = 20, pontoon_z = 170, pontoon_length = 14, pontoon_beam = 2.35f;
static const float gangway_anchor_x = 34.1f, gangway_anchor_z = 170;
// The pontoon's west-side landing in renderer coordinates is (-21, 170) at z = 170.
static const float gangway_landing_x = 21, gangway_landing_z = 170;
static const float entrance_x = 6, entrance_z = 168;
// A navigable boat pose facing north into the marina, used by --start and plugin anchors.
static const float bookmark_x = 6, bookmark_z = 158;

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
        specs[i] = boat_model_preset(berths[i].kind);
        // Native contact pads 0.45 m for wave attitude and fenders, plus 0.9 m longitudinal.
        char what[96];
        snprintf(what, sizeof(what), "berth %s hull is open water", berths[i].id);
        check(box_water(map, berths[i].x, berths[i].z, berths[i].heading,
                  specs[i].length * .5f + .9f, specs[i].beam * .5f + .45f, .5f),
            what);
        snprintf(what, sizeof(what), "berth %s centre has 4 m clear water", berths[i].id);
        check(water(map, berths[i].x, berths[i].z, 4), what);
    }
    check(!overlap(berths[0].x, berths[0].z, berths[0].heading, specs[0].length * .5f,
              specs[0].beam * .5f, berths[1].x, berths[1].z, berths[1].heading,
              specs[1].length * .5f, specs[1].beam * .5f, .45f),
        "the two hull envelopes are separate");
    check(box_water(map, pontoon_x, pontoon_z, 0, pontoon_length * .5f + .9f,
              pontoon_beam * .5f + .45f, .5f),
        "the small pontoon is open water");
    check(!water(map, gangway_anchor_x, gangway_anchor_z, 0),
        "the gangway shore anchor is land");
    check(water(map, gangway_landing_x, gangway_landing_z, 0),
        "the gangway pontoon landing is water");
    check(hypotf(gangway_anchor_x - gangway_landing_x, gangway_anchor_z - gangway_landing_z)
            >= 2 && hypotf(gangway_anchor_x - gangway_landing_x,
                            gangway_anchor_z - gangway_landing_z)
            <= 20,
        "the gangway span is 2..20 m");
    for (float z = 150; z <= 176; z += 2) {
        char what[96];
        snprintf(what, sizeof(what), "approach lane at z %.0f is open water", z);
        check(water(map, entrance_x, z, 3), what);
        for (unsigned i = 0; i < BERTH_COUNT; ++i) {
            // A 2.4 m beam hull passing at the lane centre must clear the moored hulls.
            snprintf(what, sizeof(what), "approach lane clears berth %s at z %.0f", berths[i].id, z);
            check(!overlap(entrance_x, z, 0, 2.4f, 1.2f, berths[i].x, berths[i].z,
                      berths[i].heading, specs[i].length * .5f + .9f,
                      specs[i].beam * .5f + .45f, 0),
                what);
        }
    }
    check(water(map, bookmark_x, bookmark_z, 3), "the camera bookmark is open water");
    check(water(map, entrance_x, entrance_z, 4), "the declared entrance has 4 m clear water");
    boat_world_resources_destroy(&resources);
    printf("scene_site berths=%u pontoon=%.1fx%.1f gangway=%.1fm bookmark=%.0f,%.0f "
           "failures=%d\n",
        (unsigned)BERTH_COUNT, pontoon_length, pontoon_beam,
        (double)hypotf(gangway_anchor_x - gangway_landing_x,
            gangway_anchor_z - gangway_landing_z),
        bookmark_x, bookmark_z, failures);
    return failures ? 1 : 0;
}
