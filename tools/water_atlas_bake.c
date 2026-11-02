/* Water atlas bake (graphics G4): writes or checks assets/geometry/<map>/water_atlas.bin from the
 * compiled water alone (boat_water_atlas.h describes the format). Everything is integer stepping
 * and correctly rounded float arithmetic in a fixed order, so the bytes are the same on every
 * platform; the `water_atlas` test rebakes and compares them with the committed files.
 *
 *     water_atlas_bake --map river|outer --out FILE
 *     water_atlas_bake --map river|outer --check FILE
 *     water_atlas_bake --map river|outer --preview PNG   # the four channels side by side
 */
#include "boat_location.h"
#include "boat_map.h"
#include "boat_water_atlas.h"
#include "raylib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHORE_RANGE 63.75f // Largest stored shore distance, metres.
#define DIRECTIONS 16

// Unit vectors at multiples of 22.5 degrees, written out so no libm call can differ.
static const float directions[DIRECTIONS][2] = { { 1, 0 }, { .92387953f, .38268343f },
    { .70710678f, .70710678f }, { .38268343f, .92387953f }, { 0, 1 }, { -.38268343f, .92387953f },
    { -.70710678f, .70710678f }, { -.92387953f, .38268343f }, { -1, 0 },
    { -.92387953f, -.38268343f }, { -.70710678f, -.70710678f }, { -.38268343f, -.92387953f },
    { 0, -1 }, { .38268343f, -.92387953f }, { .70710678f, -.70710678f },
    { .92387953f, -.38268343f } };
// Upwind of the shaders' cosmetic wind, which moves ripples along (-0.6, -0.8), and 22.5 degrees
// either side of it, so fetch varies smoothly instead of in streaks behind each corner.
static const float upwind[3][2]
    = { { .6f, .8f }, { .24818098f, .96871368f }, { .86047446f, .50949356f } };

typedef struct {
    int width, height;
    float origin_x, origin_z, spacing;
    unsigned char *water; // 1 where the texel centre is water.
} Raster;

static float segment_distance(float px, float pz, Vector3 a, Vector3 b)
{
    float ex = b.x - a.x, ez = b.z - a.z, wx = px - a.x, wz = pz - a.z;
    float length2 = ex * ex + ez * ez;
    float t = length2 > 0 ? (wx * ex + wz * ez) / length2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float dx = wx - t * ex, dz = wz - t * ez;
    return sqrtf(dx * dx + dz * dz);
}

// The nearest shore edge within SHORE_RANGE of renderer X/Z, from the collision grid.
static float shore_distance(const BoatMap *map, float x, float z)
{
    const BoatMapGrid *grid = &map->grid;
    const Mesh3D *mesh = map->description->mesh;
    int i0 = (int)floorf((x - SHORE_RANGE - grid->origin_x) / grid->cell_size);
    int i1 = (int)floorf((x + SHORE_RANGE - grid->origin_x) / grid->cell_size);
    int j0 = (int)floorf((z - SHORE_RANGE - grid->origin_z) / grid->cell_size);
    int j1 = (int)floorf((z + SHORE_RANGE - grid->origin_z) / grid->cell_size);
    float best = SHORE_RANGE;
    for (int j = j0 < 0 ? 0 : j0; j <= j1 && j < grid->rows; ++j)
        for (int i = i0 < 0 ? 0 : i0; i <= i1 && i < grid->columns; ++i) {
            int cell = j * grid->columns + i;
            for (unsigned k = grid->shore_start[cell]; k < grid->shore_start[cell + 1]; ++k) {
                BoatMapEdge edge = map->shore[grid->shore[k]];
                float d = segment_distance(x, z, mesh->vertices[edge.a], mesh->vertices[edge.b]);
                best = d < best ? d : best;
            }
        }
    return best;
}

static bool raster_water(const Raster *r, int i, int j)
{
    return i >= 0 && j >= 0 && i < r->width && j < r->height && r->water[j * r->width + i];
}

// Open water from texel (i, j) along DIRECTION, in metres up to LIMIT; leaving the raster
// counts as open.
static float fetch(const Raster *r, int i, int j, const float direction[2], float limit)
{
    float x = (i + .5f) * r->spacing, z = (j + .5f) * r->spacing;
    int steps = (int)ceilf(limit / r->spacing);
    for (int k = 1; k <= steps; ++k) {
        float px = x + direction[0] * (float)k * r->spacing;
        float pz = z + direction[1] * (float)k * r->spacing;
        int pi = (int)floorf(px / r->spacing), pj = (int)floorf(pz / r->spacing);
        if (pi < 0 || pj < 0 || pi >= r->width || pj >= r->height)
            return limit;
        if (!raster_water(r, pi, pj)) {
            float open = ((float)k - .5f) * r->spacing;
            return open < limit ? open : limit;
        }
    }
    return limit;
}

static unsigned char unit(float value)
{
    value = value < 0 ? 0 : value > 1 ? 1 : value;
    return (unsigned char)(value * 255 + .5f);
}

// Shore distance, depth, shelter and chop as grey panels left to right, north up.
static bool preview(const unsigned char *texels, int width, int height, const char *path)
{
    Image image = GenImageColor(4 * width, height, (Color) { 120, 60, 40, 255 });
    Color *pixels = image.data;
    for (int j = 0; j < height; ++j)
        for (int i = 0; i < width; ++i) {
            const unsigned char *t = texels + 4 * ((size_t)j * width + i);
            if (!t[0])
                continue;
            for (int c = 0; c < 4; ++c) {
                unsigned char v = t[c];
                pixels[(size_t)(height - 1 - j) * 4 * width + c * width + i]
                    = (Color) { v, v, v, 255 };
            }
        }
    bool ok = ExportImage(image, path);
    UnloadImage(image);
    return ok;
}

static const char *preview_path;

static unsigned char *bake(const BoatMapDescription *description, float spacing, int *size_out)
{
    BoatMap map;
    if (!boat_map_init(&map, description))
        return NULL;
    const Mesh3D *mesh = description->mesh;
    size_t water = boat_map_leading_water(mesh);
    float lo_x = INFINITY, lo_z = INFINITY, hi_x = -INFINITY, hi_z = -INFINITY;
    for (size_t t = 0; t < water; ++t)
        for (int k = 0; k < 3; ++k) {
            Vector3 v = mesh->vertices[mesh->triangles[t].vertices[k]];
            lo_x = fminf(lo_x, v.x);
            hi_x = fmaxf(hi_x, v.x);
            lo_z = fminf(lo_z, v.z);
            hi_z = fmaxf(hi_z, v.z);
        }
    Raster r = { .spacing = spacing };
    r.origin_x = floorf(lo_x / spacing) * spacing - spacing;
    r.origin_z = floorf(lo_z / spacing) * spacing - spacing;
    r.width = (int)ceilf((hi_x - r.origin_x) / spacing) + 1;
    r.height = (int)ceilf((hi_z - r.origin_z) / spacing) + 1;
    unsigned char *texels = NULL, *file = NULL;
    if (r.width > BOAT_WATER_ATLAS_MAX_SIZE || r.height > BOAT_WATER_ATLAS_MAX_SIZE)
        goto done;
    size_t count = (size_t)r.width * r.height;
    r.water = calloc(count, 1);
    texels = calloc(count, 4);
    if (!r.water || !texels)
        goto done;
    for (int j = 0; j < r.height; ++j)
        for (int i = 0; i < r.width; ++i) {
            float x = r.origin_x + (i + .5f) * spacing, z = r.origin_z + (j + .5f) * spacing;
            // Collision queries take simulation X, the renderer's negated.
            r.water[(size_t)j * r.width + i] = boat_map_water(&map, (Vector2) { -x, z }, 0);
        }
    size_t water_texels = 0;
    double shelter_sum = 0, chop_sum = 0;
    for (int j = 0; j < r.height; ++j)
        for (int i = 0; i < r.width; ++i) {
            if (!raster_water(&r, i, j))
                continue;
            float x = r.origin_x + (i + .5f) * spacing, z = r.origin_z + (j + .5f) * spacing;
            float shore = shore_distance(&map, x, z);
            float depth = .5f + .25f * shore;
            float open = 0;
            for (int d = 0; d < DIRECTIONS; ++d)
                open += fetch(&r, i, j, directions[d], BOAT_WATER_ATLAS_SHELTER_FETCH);
            float shelter = 1 - open / (DIRECTIONS * BOAT_WATER_ATLAS_SHELTER_FETCH);
            float chop = 0;
            for (int d = 0; d < 3; ++d)
                chop += fetch(&r, i, j, upwind[d], BOAT_WATER_ATLAS_CHOP_FETCH);
            chop /= 3 * BOAT_WATER_ATLAS_CHOP_FETCH;
            unsigned char *t = texels + 4 * ((size_t)j * r.width + i);
            int shore_units = (int)(shore * BOAT_WATER_ATLAS_SHORE_SCALE + .5f);
            int depth_units = (int)(depth * BOAT_WATER_ATLAS_DEPTH_SCALE + .5f);
            t[0] = (unsigned char)(shore_units < 1 ? 1 : shore_units > 255 ? 255 : shore_units);
            t[1] = (unsigned char)(depth_units > 255 ? 255 : depth_units);
            t[2] = unit(shelter);
            t[3] = unit(chop);
            ++water_texels;
            shelter_sum += shelter;
            chop_sum += chop;
        }
    if (preview_path && !preview(texels, r.width, r.height, preview_path))
        goto done;
    int compressed = 0;
    unsigned char *deflated = CompressData(texels, (int)(count * 4), &compressed);
    if (!deflated)
        goto done;
    file = malloc((size_t)BOAT_WATER_ATLAS_HEADER + (size_t)compressed);
    if (file) {
        boat_water_atlas_header(file, r.width, r.height, r.origin_x, r.origin_z, spacing,
            boat_water_atlas_key(description), (size_t)compressed);
        memcpy(file + BOAT_WATER_ATLAS_HEADER, deflated, (size_t)compressed);
        *size_out = BOAT_WATER_ATLAS_HEADER + compressed;
        printf("water_atlas size=%dx%d spacing=%g origin=%g,%g water_texels=%zu "
               "mean_shelter=%.3f mean_chop=%.3f bytes=%d\n",
            r.width, r.height, (double)spacing, (double)r.origin_x, (double)r.origin_z,
            water_texels, water_texels ? shelter_sum / water_texels : 0,
            water_texels ? chop_sum / water_texels : 0, *size_out);
    }
    MemFree(deflated);
done:
    free(r.water);
    free(texels);
    boat_map_destroy(&map);
    return file;
}

int main(int argc, char **argv)
{
    const char *name = NULL, *out = NULL, *check = NULL;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--map") == 0)
            name = argv[i + 1];
        else if (strcmp(argv[i], "--out") == 0)
            out = argv[i + 1];
        else if (strcmp(argv[i], "--check") == 0)
            check = argv[i + 1];
        else if (strcmp(argv[i], "--preview") == 0)
            preview_path = argv[i + 1];
        else
            return 2;
    }
    // Atlas spacing per map: the river at 4 m, the much larger outer map at 8 m.
    const BoatWorldDefinition *location = NULL;
    float spacing = 0;
    if (name && strcmp(name, "river") == 0) {
        location = boat_location(BOAT_LOCATION_RIVER);
        spacing = 4;
    } else if (name && strcmp(name, "outer") == 0) {
        location = boat_location(BOAT_LOCATION_OUTER);
        spacing = 8;
    }
    if (!location || (out && check) || (!out && !check && !preview_path)) {
        fputs("usage: water_atlas_bake --map river|outer --out FILE | --check FILE"
              " [--preview PNG]\n",
            stderr);
        return 2;
    }
    SetTraceLogLevel(LOG_WARNING);
    int size = 0;
    unsigned char *file = bake(location->map, spacing, &size);
    if (!file) {
        fprintf(stderr, "water_atlas_bake: %s: bake failed\n", name);
        return 1;
    }
    int result = 0;
    if (out) {
        FILE *f = fopen(out, "wb");
        result = !f || fwrite(file, 1, (size_t)size, f) != (size_t)size;
        if (f && fclose(f) != 0)
            result = 1;
    } else if (check) {
        FILE *f = fopen(check, "rb");
        unsigned char *expected = malloc((size_t)size + 1);
        size_t read = f && expected ? fread(expected, 1, (size_t)size + 1, f) : 0;
        result = read != (size_t)size || memcmp(expected, file, (size_t)size) != 0;
        if (f)
            fclose(f);
        free(expected);
        if (result)
            fprintf(stderr, "water_atlas_bake: %s differs from a fresh %s bake\n", check, name);
        else
            printf("water_atlas=ok map=%s\n", name);
    }
    free(file);
    return result;
}
