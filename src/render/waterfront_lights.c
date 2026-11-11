#include "render/waterfront_lights.h"

#include "raylib.h"
#include "raymath.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static float priority(const WaterfrontLight *light) { return light->intensity * light->radius; }

bool waterfront_lights_ranks_before(const WaterfrontLight *a, const WaterfrontLight *b)
{
    float pa = priority(a), pb = priority(b);
    if (pa != pb)
        return pa > pb;
    return a->id < b->id;
}

static int compare_rank(const void *a, const void *b)
{
    const WaterfrontLight *la = a, *lb = b;
    if (waterfront_lights_ranks_before(la, lb))
        return -1;
    return waterfront_lights_ranks_before(lb, la) ? 1 : 0;
}

void waterfront_lights_begin(WaterfrontLightClusters *clusters)
{
    clusters->light_count = 0;
    clusters->index_count = 0;
    clusters->stats = (WaterfrontLightStats) { 0 };
    clusters->built = false;
}

bool waterfront_lights_add(WaterfrontLightClusters *clusters, WaterfrontLight light)
{
    ++clusters->stats.submitted;
    if (!(light.radius > 0) || !(light.intensity > 0) || !isfinite(light.position.x)
        || !isfinite(light.position.y) || !isfinite(light.position.z)) {
        ++clusters->stats.dropped_lights;
        return false;
    }
    // A cell list never needs more than one period of the wrapped grid.
    light.radius = fminf(light.radius, WATERFRONT_LIGHT_CELL * (WATERFRONT_LIGHT_GRID - 1) / 2);
    if (clusters->light_count < WATERFRONT_LIGHTS_MAX) {
        clusters->lights[clusters->light_count++] = light;
        return true;
    }
    // Full: keep the top of the total rank order, whatever order lights arrive in.
    int lowest = 0;
    for (int i = 1; i < clusters->light_count; ++i)
        if (waterfront_lights_ranks_before(&clusters->lights[lowest], &clusters->lights[i]))
            lowest = i;
    ++clusters->stats.dropped_lights;
    if (!waterfront_lights_ranks_before(&light, &clusters->lights[lowest]))
        return false;
    clusters->lights[lowest] = light;
    return true;
}

static int wrap(int cell)
{
    int slot = cell % WATERFRONT_LIGHT_GRID;
    return slot < 0 ? slot + WATERFRONT_LIGHT_GRID : slot;
}

static int cell_of(float coordinate) { return (int)floorf(coordinate / WATERFRONT_LIGHT_CELL); }

int waterfront_lights_slot(Vector3 position)
{
    return wrap(cell_of(position.z)) * WATERFRONT_LIGHT_GRID + wrap(cell_of(position.x));
}

// Calls VISIT for every slot whose world cell the light's X/Z disc touches (closed bounds, so a
// point on a cell edge finds the light from either side).
static void visit_slots(const WaterfrontLight *light, void (*visit)(void *, int), void *context)
{
    Vector3 p = light->position;
    float r = light->radius;
    int x0 = cell_of(p.x - r), x1 = cell_of(p.x + r);
    int z0 = cell_of(p.z - r), z1 = cell_of(p.z + r);
    for (int z = z0; z <= z1; ++z) {
        float near_z = Clamp(p.z, z * WATERFRONT_LIGHT_CELL, (z + 1) * WATERFRONT_LIGHT_CELL);
        for (int x = x0; x <= x1; ++x) {
            float near_x = Clamp(p.x, x * WATERFRONT_LIGHT_CELL, (x + 1) * WATERFRONT_LIGHT_CELL);
            float dx = near_x - p.x, dz = near_z - p.z;
            if (dx * dx + dz * dz <= r * r)
                visit(context, wrap(z) * WATERFRONT_LIGHT_GRID + wrap(x));
        }
    }
}

typedef struct {
    WaterfrontLightClusters *clusters;
    int *wanted; // Assignments asked of each slot.
    int light;
    bool clipped;
} BuildPass;

static void count_slot(void *context, int slot)
{
    BuildPass *pass = context;
    ++pass->wanted[slot];
}

static void fill_slot(void *context, int slot)
{
    BuildPass *pass = context;
    WaterfrontLightClusters *c = pass->clusters;
    if (c->slot_count[slot] == WATERFRONT_LIGHT_CELL_LIGHTS) {
        pass->clipped = true;
        ++c->stats.dropped_assignments;
        return;
    }
    c->indices[c->slot_offset[slot] + c->slot_count[slot]++] = (unsigned short)pass->light;
}

void waterfront_lights_build(WaterfrontLightClusters *clusters)
{
    qsort(clusters->lights, (size_t)clusters->light_count, sizeof(*clusters->lights), compare_rank);
    static int wanted[WATERFRONT_LIGHT_SLOTS]; // Build is single-threaded, like drawing.
    memset(wanted, 0, sizeof(wanted));
    BuildPass pass = { .clusters = clusters, .wanted = wanted };
    for (int i = 0; i < clusters->light_count; ++i)
        visit_slots(&clusters->lights[i], count_slot, &pass);
    int offset = 0;
    clusters->stats.overflowed_slots = 0;
    clusters->stats.busiest_slot = 0;
    for (int slot = 0; slot < WATERFRONT_LIGHT_SLOTS; ++slot) {
        clusters->slot_offset[slot] = offset;
        clusters->slot_count[slot] = 0;
        offset += wanted[slot] < WATERFRONT_LIGHT_CELL_LIGHTS ? wanted[slot]
                                                              : WATERFRONT_LIGHT_CELL_LIGHTS;
        if (wanted[slot] > WATERFRONT_LIGHT_CELL_LIGHTS)
            ++clusters->stats.overflowed_slots;
        if (wanted[slot] > clusters->stats.busiest_slot)
            clusters->stats.busiest_slot = wanted[slot];
    }
    clusters->index_count = offset;
    // Lights are in rank order, so each slot keeps its highest-ranked lights.
    for (int i = 0; i < clusters->light_count; ++i) {
        pass.light = i;
        pass.clipped = false;
        visit_slots(&clusters->lights[i], fill_slot, &pass);
        if (pass.clipped)
            ++clusters->stats.clipped_lights;
    }
    clusters->built = true;
}

int waterfront_lights_pack(const WaterfrontLightClusters *clusters, float *texels)
{
    for (int i = 0; i < clusters->light_count; ++i) {
        const WaterfrontLight *l = &clusters->lights[i];
        float *t = &texels[i * 16];
        float record[16]
            = { l->position.x, l->position.y, l->position.z, l->radius, l->color.r / 255.0f,
                  l->color.g / 255.0f, l->color.b / 255.0f, l->intensity, l->direction.x,
                  l->direction.y, l->direction.z, 0, l->inner_cone_cos, l->outer_cone_cos, 0, 0 };
        memcpy(t, record, sizeof(record));
    }
    for (int slot = 0; slot < WATERFRONT_LIGHT_SLOTS; ++slot) {
        float *t = &texels[(WATERFRONT_LIGHT_SLOT_BASE + slot) * 4];
        t[0] = (float)clusters->slot_offset[slot];
        t[1] = (float)clusters->slot_count[slot];
        t[2] = t[3] = 0;
    }
    float *indices = &texels[WATERFRONT_LIGHT_INDEX_BASE * 4];
    for (int i = 0; i < clusters->index_count; ++i)
        indices[i] = (float)clusters->indices[i];
    int texels_used = WATERFRONT_LIGHT_INDEX_BASE + (clusters->index_count + 3) / 4;
    return (texels_used + WATERFRONT_LIGHT_TEXTURE_WIDTH - 1) / WATERFRONT_LIGHT_TEXTURE_WIDTH;
}
