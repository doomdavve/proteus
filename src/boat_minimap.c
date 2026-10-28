#include "boat_minimap.h"
#include "boat_hud.h"
#include "waterfront_harbor.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TRIM 2
#define SUBSAMPLES 4

static const Color land = { 62, 72, 60, 255 };
static const Color water = { 40, 82, 108, 255 };
static const Color shore = { 141, 150, 122, 255 };
static const Color outer_trim = { 56, 74, 77, 255 };
static const Color inner_trim = { 8, 20, 25, 255 };
static const Color highlight = { 153, 166, 157, 255 };
static const Color halo = { 6, 14, 18, 255 };
static const Color ink = { 218, 223, 202, 255 };
static const Color muted = { 170, 182, 170, 255 };
static const Color shelter_color = { 118, 196, 178, 255 };
static const Color amber = { 233, 180, 104, 255 };
static const Color boat_color = { 255, 255, 255, 255 };

// Largest inner sizes, in scene pixels; each layer keeps the map's aspect inside them.
static const int max_size[BOAT_MINIMAP_MODES][2] = { { 0, 0 }, { 92, 64 }, { 284, 122 } };
#define CORNER_MARGIN 4
#define CHART_TOP 22

// Fixed waterfront landmarks in the river's crane-origin map frame (east, north), shared by the
// river crop and the outer map, and where each chart label sits relative to its marker.
typedef struct {
    const char *name;
    Vector2 map;
    int label_dx, label_dy;
} Landmark;

static const Landmark landmarks[] = {
    { "ERIKSBERG", { 0, 0 }, 0, -8 },
    { "ALVSBORGSBRON", { -454, -678 }, -8, 10 },
};

static Vector2 to_map(Vector2 position) { return (Vector2) { -position.x, position.y }; }

static Vector2 project_map(const BoatMinimapLayer *layer, Vector2 map)
{
    return (Vector2) { TRIM + (map.x - layer->north_west.x) * layer->scale,
        TRIM + (layer->north_west.y - map.y) * layer->scale };
}

static float cross(Vector2 o, Vector2 a, Vector2 b)
{
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// Mark the samples of a COLUMNS x ROWS grid whose centres lie in triangle A, B, C.
static void fill_triangle(
    unsigned char *mask, int columns, int rows, Vector2 a, Vector2 b, Vector2 c)
{
    float area = cross(a, b, c);
    if (area == 0)
        return;
    int x0 = (int)fmaxf(0, floorf(fminf(a.x, fminf(b.x, c.x))));
    int x1 = (int)fminf((float)columns - 1, ceilf(fmaxf(a.x, fmaxf(b.x, c.x))));
    int y0 = (int)fmaxf(0, floorf(fminf(a.y, fminf(b.y, c.y))));
    int y1 = (int)fminf((float)rows - 1, ceilf(fmaxf(a.y, fmaxf(b.y, c.y))));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            Vector2 p = { (float)x + .5f, (float)y + .5f };
            float u = cross(b, c, p) * area, v = cross(c, a, p) * area, w = cross(a, b, p) * area;
            if (u >= 0 && v >= 0 && w >= 0)
                mask[(size_t)y * columns + x] = 1;
        }
}

static Color mix(Color from, Color to, float t)
{
    return (Color) { (unsigned char)lroundf(from.r + (to.r - from.r) * t),
        (unsigned char)lroundf(from.g + (to.g - from.g) * t),
        (unsigned char)lroundf(from.b + (to.b - from.b) * t), 255 };
}

static void destroy_layer(BoatMinimapLayer *layer)
{
    UnloadImage(layer->base);
    UnloadImage(layer->frame);
    *layer = (BoatMinimapLayer) { 0 };
}

// Water coverage from SUBSAMPLES x SUBSAMPLES samples per pixel, blended from land to water,
// with a pale line on the land pixels that touch mostly-water ones.
static bool bake(BoatMinimapLayer *layer, const Mesh3D *mesh, size_t water_count, Rectangle extent,
    int max_width, int max_height)
{
    layer->scale = fminf(max_width / extent.width, max_height / extent.height);
    layer->north_west = (Vector2) { extent.x, extent.y + extent.height };
    int width = (int)ceilf(extent.width * layer->scale);
    int height = (int)ceilf(extent.height * layer->scale);
    int columns = width * SUBSAMPLES, rows = height * SUBSAMPLES;
    unsigned char *mask = calloc((size_t)columns * rows, 1);
    float *coverage = calloc((size_t)width * height, sizeof(float));
    if (!mask || !coverage) {
        free(mask);
        free(coverage);
        return false;
    }
    float subsample = layer->scale * SUBSAMPLES;
    for (size_t i = 0; i < water_count; ++i) {
        Vector2 v[3];
        for (int k = 0; k < 3; ++k) {
            Vector3 vertex = mesh->vertices[mesh->triangles[i].vertices[k]];
            v[k] = (Vector2) { (vertex.x - layer->north_west.x) * subsample,
                (layer->north_west.y - vertex.z) * subsample };
        }
        fill_triangle(mask, columns, rows, v[0], v[1], v[2]);
    }
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < columns; ++x)
            coverage[(y / SUBSAMPLES) * width + x / SUBSAMPLES] += mask[(size_t)y * columns + x];
    free(mask);
    layer->base = GenImageColor(width, height, land);
    layer->frame = GenImageColor(width + TRIM * 2, height + TRIM * 2, outer_trim);
    if (!layer->base.data || !layer->frame.data) {
        free(coverage);
        destroy_layer(layer);
        return false;
    }
    Color *pixels = layer->base.data;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            float t = coverage[y * width + x] / (SUBSAMPLES * SUBSAMPLES);
            bool coast = false;
            for (int d = 0; t < .5f && d < 4 && !coast; ++d) {
                int nx = x + (d == 0) - (d == 1), ny = y + (d == 2) - (d == 3);
                coast = nx >= 0 && ny >= 0 && nx < width && ny < height
                    && coverage[ny * width + nx] >= .5f * SUBSAMPLES * SUBSAMPLES;
            }
            pixels[y * width + x] = coast ? shore : mix(land, water, t);
        }
    free(coverage);
    ImageDrawRectangleLines(
        &layer->frame, (Rectangle) { 1, 1, width + 2.0f, height + 2.0f }, 1, inner_trim);
    ImageDrawLine(&layer->frame, 1, 0, width + TRIM * 2 - 2, 0, highlight);
    return true;
}

bool boat_minimap_init(BoatMinimap *minimap, const BoatWorldDefinition *location)
{
    *minimap = (BoatMinimap) { 0 };
    if (!location || !location->mapped || !location->map || !location->map->mesh)
        return false;
    const Mesh3D *mesh = location->map->mesh;
    size_t water_count = boat_map_leading_water(mesh);
    if (!water_count)
        return false;
    float west = FLT_MAX, east = -FLT_MAX, south = FLT_MAX, north = -FLT_MAX;
    for (size_t i = 0; i < mesh->triangle_count; ++i)
        for (int k = 0; k < 3; ++k) {
            Vector3 v = mesh->vertices[mesh->triangles[i].vertices[k]];
            west = fminf(west, v.x);
            east = fmaxf(east, v.x);
            south = fminf(south, v.z);
            north = fmaxf(north, v.z);
        }
    Rectangle extent = { west, south, east - west, north - south };
    for (int mode = BOAT_MINIMAP_CORNER; mode < BOAT_MINIMAP_MODES; ++mode)
        if (!bake(&minimap->layers[mode], mesh, water_count, extent, max_size[mode][0],
                max_size[mode][1])) {
            boat_minimap_destroy(minimap);
            return false;
        }
    minimap->location = location;
    return true;
}

void boat_minimap_destroy(BoatMinimap *minimap)
{
    for (int mode = BOAT_MINIMAP_CORNER; mode < BOAT_MINIMAP_MODES; ++mode)
        destroy_layer(&minimap->layers[mode]);
    *minimap = (BoatMinimap) { 0 };
}

bool boat_minimap_ready(const BoatMinimap *minimap) { return minimap->location != NULL; }

BoatMinimapMode boat_minimap_next(BoatMinimapMode mode)
{
    return (BoatMinimapMode)((mode + 1) % BOAT_MINIMAP_MODES);
}

static const BoatMinimapLayer *layer_for(const BoatMinimap *minimap, BoatMinimapMode mode)
{
    if (!boat_minimap_ready(minimap) || mode <= BOAT_MINIMAP_HIDDEN || mode >= BOAT_MINIMAP_MODES)
        return NULL;
    return &minimap->layers[mode];
}

Rectangle boat_minimap_bounds(
    const BoatMinimap *minimap, BoatMinimapMode mode, int width, int height)
{
    const BoatMinimapLayer *layer = layer_for(minimap, mode);
    if (!layer)
        return (Rectangle) { 0 };
    float w = (float)layer->frame.width, h = (float)layer->frame.height;
    if (mode == BOAT_MINIMAP_CORNER)
        return (Rectangle) { width - w - CORNER_MARGIN, CORNER_MARGIN, w, h };
    (void)height;
    return (Rectangle) { floorf((width - w) / 2), CHART_TOP, w, h };
}

Vector2 boat_minimap_project(const BoatMinimap *minimap, BoatMinimapMode mode, Vector2 position)
{
    const BoatMinimapLayer *layer = layer_for(minimap, mode);
    return layer ? project_map(layer, to_map(position)) : (Vector2) { 0 };
}

// Markers never cover the trim.
static void dot(BoatMinimapLayer *layer, int x, int y, Color color)
{
    if (x >= TRIM && y >= TRIM && x < layer->frame.width - TRIM && y < layer->frame.height - TRIM)
        ((Color *)layer->frame.data)[y * layer->frame.width + x] = color;
}

static void square(BoatMinimapLayer *layer, int x, int y, int radius, Color color)
{
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
            dot(layer, x + dx, y + dy, color);
}

static void plus(BoatMinimapLayer *layer, int x, int y, Color color)
{
    square(layer, x, y, 1, halo);
    dot(layer, x, y, color);
    for (int d = -1; d <= 1; d += 2) {
        dot(layer, x + d, y, color);
        dot(layer, x, y + d, color);
    }
}

static void text(BoatMinimapLayer *layer, const char *string, int centre, int top, Color color)
{
    int width = (int)strlen(string) * 4 - 1;
    int left = centre - width / 2;
    left = (int)fmaxf(TRIM + 1, fminf((float)left, (float)(layer->frame.width - TRIM - 1 - width)));
    top = (int)fmaxf(TRIM + 1, fminf((float)top, (float)(layer->frame.height - TRIM - 6)));
    for (int pass = 0; pass < 2; ++pass)
        for (const char *c = string; *c; ++c) {
            const char *rows = boat_hud_glyph(*c);
            int x = left + (int)(c - string) * 4 + (pass ? 0 : 1);
            for (int row = 0; rows && row < 5; ++row)
                for (int col = 0; col < 3; ++col)
                    if ((rows[row] - '0') & (4 >> col))
                        dot(layer, x + col, top + row + (pass ? 0 : 1), pass ? color : halo);
        }
}

// Dashed outline of the exposed water.
static void dashed_circle(BoatMinimapLayer *layer, Vector2 centre, float radius, Color color)
{
    int steps = (int)fmaxf(16, ceilf(radius * 6.2831853f));
    for (int i = 0; i < steps; ++i) {
        if ((i / 2) % 2)
            continue;
        float angle = i * 6.2831853f / steps;
        dot(layer, (int)floorf(centre.x + cosf(angle) * radius),
            (int)floorf(centre.y + sinf(angle) * radius), color);
    }
}

// A pixel wedge pointing along the heading, outlined in the halo colour.
static void boat_marker(BoatMinimapLayer *layer, Vector2 centre, float heading, float size)
{
    // Simulation forward (sin, -cos) is map (-sin, -cos): east is -X, north is +Z; rows run south.
    Vector2 forward = { -sinf(heading), cosf(heading) };
    Vector2 side = { -forward.y, forward.x };
    Vector2 tip = { centre.x + forward.x * size, centre.y + forward.y * size };
    Vector2 back = { centre.x - forward.x * size * .6f, centre.y - forward.y * size * .6f };
    Vector2 left = { back.x + side.x * size * .7f, back.y + side.y * size * .7f };
    Vector2 right = { back.x - side.x * size * .7f, back.y - side.y * size * .7f };
    enum { SPAN = 9, SIDE = SPAN * 2 + 1 };
    unsigned char inside[SIDE * SIDE] = { 0 };
    int ox = (int)floorf(centre.x) - SPAN, oy = (int)floorf(centre.y) - SPAN;
    Vector2 local[3] = { { tip.x - ox, tip.y - oy }, { left.x - ox, left.y - oy },
        { right.x - ox, right.y - oy } };
    fill_triangle(inside, SIDE, SIDE, local[0], local[1], local[2]);
    inside[SPAN * SIDE + SPAN] = 1; // Never vanish, whatever the heading.
    for (int y = 0; y < SIDE; ++y)
        for (int x = 0; x < SIDE; ++x) {
            bool near = false;
            for (int dy = -1; dy <= 1 && !near; ++dy)
                for (int dx = -1; dx <= 1 && !near; ++dx) {
                    int nx = x + dx, ny = y + dy;
                    near = nx >= 0 && ny >= 0 && nx < SIDE && ny < SIDE && inside[ny * SIDE + nx];
                }
            if (near)
                dot(layer, ox + x, oy + y, inside[y * SIDE + x] ? boat_color : halo);
        }
}

static void scale_bar(BoatMinimapLayer *layer)
{
    float metres = 1000 * layer->scale > 80 ? 500 : 1000;
    int length = (int)lroundf(metres * layer->scale);
    int x = layer->frame.width - TRIM - 5 - length, y = layer->frame.height - TRIM - 5;
    for (int pass = 0; pass < 2; ++pass) {
        Color color = pass ? ink : halo;
        int o = pass ? 0 : 1;
        for (int i = 0; i <= length; ++i)
            dot(layer, x + i + o, y + o, color);
        for (int i = 1; i <= 2; ++i) {
            dot(layer, x + o, y - i + o, color);
            dot(layer, x + length + o, y - i + o, color);
        }
    }
    text(layer, metres < 1000 ? "500 M" : "1 KM", x + length / 2, y - 8, ink);
}

static void compass(BoatMinimapLayer *layer)
{
    int x = TRIM + 6, y = TRIM + 3;
    text(layer, "N", x, y + 4, ink);
    dot(layer, x, y + 1, ink);
    dot(layer, x - 1, y + 2, ink);
    dot(layer, x + 1, y + 2, ink);
}

const Image *boat_minimap_compose(BoatMinimap *minimap, BoatMinimapMode mode,
    const BoatVoyage *voyage, Vector2 position, float heading)
{
    if (!layer_for(minimap, mode))
        return NULL;
    BoatMinimapLayer *layer = &minimap->layers[mode];
    bool chart = mode == BOAT_MINIMAP_CHART;
    Color *frame = layer->frame.data;
    const Color *base = layer->base.data;
    for (int y = 0; y < layer->base.height; ++y)
        memcpy(&frame[(y + TRIM) * layer->frame.width + TRIM], &base[y * layer->base.width],
            sizeof(Color) * (size_t)layer->base.width);

    const BoatVoyageRoute *route = voyage ? voyage->route : NULL;
    if (route) {
        Vector2 centre = project_map(layer, to_map(route->exposure.centre));
        float radius = route->exposure.radius * route->exposed_fraction * layer->scale;
        dashed_circle(layer, centre, radius, amber);
        if (chart)
            text(layer, "EXPOSED", (int)centre.x, (int)(centre.y - radius) - 7, amber);
    }
    const WaterfrontFerry *ferry = waterfront_harbor_ferry();
    Landmark ferry_mark = { "FERRY", { ferry->position.x, ferry->position.z }, -14, -2 };
    for (size_t i = 0; i <= sizeof(landmarks) / sizeof(landmarks[0]); ++i) {
        const Landmark *mark
            = i < sizeof(landmarks) / sizeof(landmarks[0]) ? &landmarks[i] : &ferry_mark;
        Vector2 p = project_map(layer, mark->map);
        int x = (int)floorf(p.x), y = (int)floorf(p.y);
        square(layer, x, y, 1, halo);
        dot(layer, x, y, muted);
        if (chart)
            text(layer, mark->name, x + mark->label_dx, y + mark->label_dy, muted);
    }
    if (route) {
        unsigned destination = route->shelter_count - 1;
        for (unsigned i = 0; i < destination; ++i) {
            Vector2 p = project_map(layer, to_map(route->shelters[i].position));
            int x = (int)floorf(p.x), y = (int)floorf(p.y);
            if (i == voyage->shelter) {
                square(layer, x, y, 2, halo);
                square(layer, x, y, 1, shelter_color);
            } else
                plus(layer, x, y, shelter_color);
        }
        Vector2 p = project_map(layer, to_map(route->arrival_centre));
        int x = (int)floorf(p.x), y = (int)floorf(p.y);
        square(layer, x, y, 3, halo);
        square(layer, x, y, 2, amber);
        square(layer, x, y, 1, voyage->arrived ? amber : halo);
        dot(layer, x, y, amber);
        if (chart)
            text(layer, boat_voyage_destination(route), x, y + 5, amber);
    }
    if (chart) {
        scale_bar(layer);
        compass(layer);
    }
    boat_marker(layer, project_map(layer, to_map(position)), heading, chart ? 4.5f : 3.5f);
    return &layer->frame;
}
