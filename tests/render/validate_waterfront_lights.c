#include "waterfront_lights.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "Light check failed at line %d: %s\n", __LINE__, #condition);          \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static WaterfrontLightClusters clusters, other;
static float texels[WATERFRONT_LIGHT_TEXELS * 4];

static unsigned state = 12345;
static float next(void) // Deterministic 0..1.
{
    state = state * 1664525u + 1013904223u;
    return (state >> 8) / 16777216.0f;
}

static WaterfrontLight light(int id, float x, float z, float radius, float intensity)
{
    return (WaterfrontLight) { .id = id,
        .position = { x, 4, z },
        .direction = { 0, -1, 0 },
        .color = { 255, 200, 120, 255 },
        .radius = radius,
        .intensity = intensity,
        .inner_cone_cos = -1,
        .outer_cone_cos = -1 };
}

static WaterfrontLight crowd(int i)
{
    return light(i, (float)(i % 97) * 6, (float)(i / 97 * 6), 2, 1 + (float)(i % 7) * .1f);
}

static bool reaches(const WaterfrontLight *l, Vector3 p)
{
    float dx = l->position.x - p.x, dy = l->position.y - p.y, dz = l->position.z - p.z;
    return sqrtf(dx * dx + dy * dy + dz * dz) < l->radius;
}

// Every light reaching P, from its slot list, equals the brute-force set over all kept lights.
static bool slot_matches(const WaterfrontLightClusters *c, Vector3 p)
{
    int slot = waterfront_lights_slot(p);
    int from_slot = 0, everywhere = 0;
    for (int k = 0; k < c->slot_count[slot]; ++k)
        from_slot += reaches(&c->lights[c->indices[c->slot_offset[slot] + k]], p);
    for (int i = 0; i < c->light_count; ++i)
        everywhere += reaches(&c->lights[i], p);
    return from_slot == everywhere;
}

int main(void)
{
    // Slots: floor per 8 m cell, wrapped on both signs.
    CHECK(waterfront_lights_slot((Vector3) { 0, 0, 0 }) == 0);
    CHECK(waterfront_lights_slot((Vector3) { 7.99f, 0, 0 }) == 0);
    CHECK(waterfront_lights_slot((Vector3) { 8, 0, 0 }) == 1);
    CHECK(waterfront_lights_slot((Vector3) { -.01f, 0, 0 }) == WATERFRONT_LIGHT_GRID - 1);
    CHECK(waterfront_lights_slot((Vector3) { 0, 0, 8 }) == WATERFRONT_LIGHT_GRID);
    float period = WATERFRONT_LIGHT_CELL * WATERFRONT_LIGHT_GRID;
    CHECK(waterfront_lights_slot((Vector3) { 3 + period, 0, -5 - 2 * period })
        == waterfront_lights_slot((Vector3) { 3, 0, -5 }));

    // Rank: intensity * radius, then the lower ID.
    WaterfrontLight a = light(7, 0, 0, 10, 1), b = light(3, 0, 0, 5, 2), c = light(9, 0, 0, 20, 1);
    CHECK(waterfront_lights_ranks_before(&b, &a) && !waterfront_lights_ranks_before(&a, &b));
    CHECK(waterfront_lights_ranks_before(&c, &a));

    // Invalid lights are dropped and counted.
    waterfront_lights_begin(&clusters);
    CHECK(!waterfront_lights_add(&clusters, light(1, 0, 0, 0, 1)));
    CHECK(!waterfront_lights_add(&clusters, light(2, 0, 0, 5, 0)));
    CHECK(!waterfront_lights_add(&clusters, light(3, NAN, 0, 5, 1)));
    waterfront_lights_build(&clusters);
    CHECK(clusters.light_count == 0 && clusters.stats.dropped_lights == 3);
    CHECK(clusters.stats.submitted == 3 && clusters.index_count == 0);

    // A light on a cell corner reaches the four cells around it, and points on every side of
    // the edges see it.
    waterfront_lights_begin(&clusters);
    waterfront_lights_add(&clusters, light(5, 16, 16, 3, 1));
    waterfront_lights_build(&clusters);
    CHECK(clusters.index_count == 4);
    for (float dx = -2; dx <= 2; dx += .25f)
        for (float dz = -2; dz <= 2; dz += .25f)
            CHECK(slot_matches(&clusters, (Vector3) { 16 + dx, 4, 16 + dz }));

    // A realistic scene: no overflow, and every point matches brute force, including the far
    // side of the wrap and cell edges.
    waterfront_lights_begin(&clusters);
    for (int i = 0; i < 160; ++i)
        CHECK(waterfront_lights_add(&clusters,
            light(
                100 + i, -300 + 600 * next(), -300 + 600 * next(), 6 + 30 * next(), .5f + next())));
    waterfront_lights_build(&clusters);
    CHECK(clusters.light_count == 160 && clusters.stats.dropped_lights == 0);
    CHECK(clusters.stats.overflowed_slots == 0 && clusters.stats.clipped_lights == 0);
    CHECK(clusters.stats.busiest_slot <= WATERFRONT_LIGHT_CELL_LIGHTS);
    for (int i = 0; i < 20000; ++i) {
        float x = -340 + 680 * next(), z = -340 + 680 * next();
        if (i % 4 == 0)
            x = WATERFRONT_LIGHT_CELL * roundf(x / WATERFRONT_LIGHT_CELL);
        CHECK(slot_matches(&clusters, (Vector3) { x, 4 * next(), z }));
    }
    // Lists are in rank order, and lights are sorted.
    for (int i = 1; i < clusters.light_count; ++i)
        CHECK(waterfront_lights_ranks_before(&clusters.lights[i - 1], &clusters.lights[i]));
    for (int slot = 0; slot < WATERFRONT_LIGHT_SLOTS; ++slot)
        for (int k = 1; k < clusters.slot_count[slot]; ++k)
            CHECK(clusters.indices[clusters.slot_offset[slot] + k - 1]
                < clusters.indices[clusters.slot_offset[slot] + k]);

    // Submission order does not matter: the same set reversed builds the same lists.
    other = clusters;
    WaterfrontLight kept[160];
    memcpy(kept, clusters.lights, sizeof(kept));
    waterfront_lights_begin(&other);
    for (int i = 159; i >= 0; --i)
        waterfront_lights_add(&other, kept[i]);
    waterfront_lights_build(&other);
    CHECK(other.index_count == clusters.index_count);
    CHECK(memcmp(other.lights, clusters.lights, sizeof(kept)) == 0);
    CHECK(memcmp(other.indices, clusters.indices, sizeof(*other.indices) * other.index_count) == 0);

    // Overflow: 40 lights over one spot keep the 16 strongest there and count the rest.
    waterfront_lights_begin(&clusters);
    for (int i = 0; i < 40; ++i)
        waterfront_lights_add(&clusters, light(500 + i, 4, 4, 3, 1 + i * .01f));
    waterfront_lights_build(&clusters);
    CHECK(clusters.light_count == 40 && clusters.stats.overflowed_slots == 1);
    CHECK(clusters.stats.clipped_lights == 24 && clusters.stats.dropped_assignments == 24);
    CHECK(clusters.stats.busiest_slot == 40);
    int slot = waterfront_lights_slot((Vector3) { 4, 0, 4 });
    CHECK(clusters.slot_count[slot] == WATERFRONT_LIGHT_CELL_LIGHTS);
    for (int k = 0; k < WATERFRONT_LIGHT_CELL_LIGHTS; ++k)
        CHECK(clusters.lights[clusters.indices[clusters.slot_offset[slot] + k]].id == 539 - k);

    // Global capacity keeps the top of the rank order whatever the order of arrival.
    for (int pass = 0; pass < 2; ++pass) {
        WaterfrontLightClusters *c = pass ? &other : &clusters;
        waterfront_lights_begin(c);
        for (int n = 0; n < WATERFRONT_LIGHTS_MAX + 200; ++n) {
            int i = pass ? WATERFRONT_LIGHTS_MAX + 199 - n : n;
            waterfront_lights_add(c, crowd(i));
        }
        waterfront_lights_build(c);
        CHECK(c->light_count == WATERFRONT_LIGHTS_MAX && c->stats.dropped_lights == 200);
        CHECK(c->stats.submitted == WATERFRONT_LIGHTS_MAX + 200);
    }
    CHECK(memcmp(other.lights, clusters.lights, sizeof(clusters.lights)) == 0);
    // No dropped light outranks the lowest kept one.
    const WaterfrontLight *lowest = &clusters.lights[clusters.light_count - 1];
    int outranking = 0;
    for (int i = 0; i < WATERFRONT_LIGHTS_MAX + 200; ++i) {
        WaterfrontLight l = crowd(i);
        outranking += waterfront_lights_ranks_before(&l, lowest) || l.id == lowest->id;
    }
    CHECK(outranking == WATERFRONT_LIGHTS_MAX);

    // Packing: records, headers and indices at their documented texels.
    waterfront_lights_begin(&clusters);
    waterfront_lights_add(&clusters, light(42, 12, 20, 6, 1.5f));
    waterfront_lights_build(&clusters);
    int rows = waterfront_lights_pack(&clusters, texels);
    CHECK(rows
        == (WATERFRONT_LIGHT_INDEX_BASE + 1 + WATERFRONT_LIGHT_TEXTURE_WIDTH - 1)
            / WATERFRONT_LIGHT_TEXTURE_WIDTH);
    CHECK(rows <= WATERFRONT_LIGHT_TEXTURE_HEIGHT);
    CHECK(texels[0] == 12 && texels[2] == 20 && texels[3] == 6 && texels[7] == 1.5f);
    slot = waterfront_lights_slot((Vector3) { 12, 0, 20 });
    float *header = &texels[(WATERFRONT_LIGHT_SLOT_BASE + slot) * 4];
    CHECK(header[1] == clusters.slot_count[slot] && header[1] >= 1);
    CHECK(texels[(WATERFRONT_LIGHT_INDEX_BASE * 4) + (int)header[0]] == 0);
    CHECK(WATERFRONT_LIGHT_TEXELS % WATERFRONT_LIGHT_TEXTURE_WIDTH == 0);

    printf("Light clusters: %d slots of %g m, %d lights per slot, %d lights\n",
        WATERFRONT_LIGHT_SLOTS, WATERFRONT_LIGHT_CELL, WATERFRONT_LIGHT_CELL_LIGHTS,
        WATERFRONT_LIGHTS_MAX);
    return 0;
}
