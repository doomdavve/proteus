#include "boat_minimap.h"
#include "boat_hud.h"
#include "boat_scenery.h"
#include "waterfront_harbor.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TRIM BOAT_MINIMAP_TRIM
#define SUBSAMPLES 4

// Paper-chart colours, after INT 1 / S-52 day conventions, muted slightly for the game.
static const Color paper = { 244, 241, 226, 255 }; // Deep water and label halos.
static const Color shallow = { 160, 202, 224, 255 }; // Within NEAR_SHORE metres of land.
static const Color offing = { 208, 228, 236, 255 }; // Within OFFING metres of land.
static const Color land = { 228, 206, 148, 255 };
static const Color built = { 200, 174, 122, 255 };
static const Color coast = { 36, 38, 40, 255 };
static const Color ink = { 30, 32, 36, 255 };
static const Color magenta = { 172, 34, 132, 255 };
static const Color pale_magenta = { 214, 140, 192, 255 };
#define PORT_RED { 200, 40, 40, 255 }
#define STARBOARD_GREEN { 32, 140, 72, 255 }
#define WHITE_MARK { 250, 250, 246, 255 }
#define BLACK_MARK { 36, 38, 40, 255 }
// Own ship: a high-visibility orange used by nothing else on the chart.
static const Color own_ship_color = { 255, 112, 0, 255 };
static const Color hull = { 150, 154, 160, 255 };
static const Color hatch = { 204, 198, 184, 255 }; // Outside every map triangle.
#define NEAR_SHORE 50.0f
#define OFFING 150.0f

// Largest inner sizes, in scene pixels; each layer keeps the map's aspect inside them.
static const int max_size[BOAT_MINIMAP_MODES][2] = { { 0, 0 }, { 90, 62 }, { 282, 120 } };
#define CORNER_MARGIN 4
#define CHART_TOP 22

// The crane origin's position (docs/adventure: origin_epsg4326) and the length of one minute
// there. The SWEREF 99 12 00 grid is within 0.1 degrees of true north here, so minutes are
// straight lines on the chart to well under a pixel.
#define ORIGIN_LATITUDE 57.6973921
#define ORIGIN_LONGITUDE 11.9089495
#define METRES_PER_LATITUDE_MINUTE 1856.2
#define METRES_PER_LONGITUDE_MINUTE 993.9
#define NAUTICAL_MILE 1852.0f

// Fixed waterfront features in the river's crane-origin map frame (east, north), shared by the
// river crop and the outer map; each chart label sits at the marker plus (label_dx, label_dy).
// Features outside a map's chart are left out.
typedef struct {
    const char *name;
    Vector2 map;
    int label_dx, label_dy;
} Landmark;

static const Landmark landmarks[] = {
    { "ERIKSBERG", { 0, 0 }, 0, -9 },
    { "NYA ALVSBORG", { -4177, -1314 }, 0, -9 },
};

// Älvsborgsbron's deck (the river map's authored bridge above 40 m) and its charted vertical
// clearance from OpenStreetMap (seamark:bridge:clearance_height).
static const Vector2 bridge_ends[2] = { { -614, -197 }, { -261, -1261 } };

// Channel marks as the outer dressing places them (OpenStreetMap seamarks, IALA A).
typedef struct {
    Vector2 map;
    Color color;
    bool lit;
} ChartMark;

static const ChartMark chart_marks[] = {
    { { -4020, -1396 }, PORT_RED, true }, // Port lateral beacon.
    { { -3970, -1813 }, STARBOARD_GREEN, true }, // Starboard lateral beacon.
    { { -4673, -1963 }, STARBOARD_GREEN, true }, // Starboard lateral beacon.
    { { -3919, -3447 }, PORT_RED, true }, // Q R minor light, Långedrag.
    { { -4563, -1510 }, WHITE_MARK, true }, // No 30, VQ+LFl minor light.
    { { -1611, -1203 }, WHITE_MARK, true }, // Fl(2) minor light.
    { { -3708, -2773 }, BLACK_MARK, false }, // Polismästaren, isolated danger.
    { { -5267, -2935 }, WHITE_MARK, false }, // Skalskären, isolated danger.
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

static void destroy_layer(BoatMinimapLayer *layer)
{
    UnloadImage(layer->base);
    UnloadImage(layer->frame);
    *layer = (BoatMinimapLayer) { 0 };
}

// Two-pass chamfer distance, in samples, from every sample to the nearest one outside the mask.
static void shore_distance(const unsigned char *mask, float *distance, int columns, int rows)
{
    const float diagonal = 1.41421356f;
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < columns; ++x) {
            size_t i = (size_t)y * columns + x;
            float d = mask[i] ? FLT_MAX : 0;
            if (d > 0) {
                if (x > 0)
                    d = fminf(d, distance[i - 1] + 1);
                if (y > 0) {
                    d = fminf(d, distance[i - columns] + 1);
                    if (x > 0)
                        d = fminf(d, distance[i - columns - 1] + diagonal);
                    if (x < columns - 1)
                        d = fminf(d, distance[i - columns + 1] + diagonal);
                }
            }
            distance[i] = d;
        }
    for (int y = rows - 1; y >= 0; --y)
        for (int x = columns - 1; x >= 0; --x) {
            size_t i = (size_t)y * columns + x;
            float d = distance[i];
            if (x < columns - 1)
                d = fminf(d, distance[i + 1] + 1);
            if (y < rows - 1) {
                d = fminf(d, distance[i + columns] + 1);
                if (x < columns - 1)
                    d = fminf(d, distance[i + columns + 1] + diagonal);
                if (x > 0)
                    d = fminf(d, distance[i + columns - 1] + diagonal);
            }
            distance[i] = d;
        }
}

// Roofs mark built-up land: flat, non-green triangles at least 4 m above the lowest land in
// their pixel, covering a quarter of it or more. Water, terrain, trees and walls do not count.
static void built_up(const BoatMinimapLayer *layer, const Mesh3D *mesh, size_t water_count,
    int width, int height, unsigned char *roof, float *lowest)
{
    for (int i = 0; i < width * height; ++i)
        lowest[i] = FLT_MAX;
    Vector2 v[3];
    float y[3];
    for (size_t i = water_count; i < mesh->triangle_count; ++i) {
        for (int k = 0; k < 3; ++k) {
            Vector3 vertex = mesh->vertices[mesh->triangles[i].vertices[k]];
            v[k] = (Vector2) { (vertex.x - layer->north_west.x) * layer->scale,
                (layer->north_west.y - vertex.z) * layer->scale };
            y[k] = vertex.y;
        }
        float low = fminf(y[0], fminf(y[1], y[2]));
        int x0 = (int)fmaxf(0, floorf(fminf(v[0].x, fminf(v[1].x, v[2].x))));
        int x1 = (int)fminf((float)width - 1, floorf(fmaxf(v[0].x, fmaxf(v[1].x, v[2].x))));
        int y0 = (int)fmaxf(0, floorf(fminf(v[0].y, fminf(v[1].y, v[2].y))));
        int y1 = (int)fminf((float)height - 1, floorf(fmaxf(v[0].y, fmaxf(v[1].y, v[2].y))));
        for (int py = y0; py <= y1; ++py)
            for (int px = x0; px <= x1; ++px)
                lowest[py * width + px] = fminf(lowest[py * width + px], low);
    }
    int columns = width * SUBSAMPLES, rows = height * SUBSAMPLES;
    float subsample = layer->scale * SUBSAMPLES;
    for (size_t i = water_count; i < mesh->triangle_count; ++i) {
        Color color = mesh->triangles[i].color;
        if (color.g > color.r + 8 && color.g >= color.b)
            continue; // Trees and lawns.
        Vector3 p[3];
        for (int k = 0; k < 3; ++k)
            p[k] = mesh->vertices[mesh->triangles[i].vertices[k]];
        Vector3 e1 = { p[1].x - p[0].x, p[1].y - p[0].y, p[1].z - p[0].z };
        Vector3 e2 = { p[2].x - p[0].x, p[2].y - p[0].y, p[2].z - p[0].z };
        Vector3 n
            = { e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
        float length = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
        if (length == 0 || fabsf(n.y) < .9f * length)
            continue;
        float cx = ((p[0].x + p[1].x + p[2].x) / 3 - layer->north_west.x) * layer->scale;
        float cy = (layer->north_west.y - (p[0].z + p[1].z + p[2].z) / 3) * layer->scale;
        if (cx < 0 || cy < 0 || cx >= width || cy >= height)
            continue;
        if ((p[0].y + p[1].y + p[2].y) / 3 < lowest[(int)cy * width + (int)cx] + 4)
            continue;
        for (int k = 0; k < 3; ++k)
            v[k] = (Vector2) { (p[k].x - layer->north_west.x) * subsample,
                (layer->north_west.y - p[k].z) * subsample };
        fill_triangle(roof, columns, rows, v[0], v[1], v[2]);
    }
}

// The neat line: an outer and inner black line around a band of alternating black and white
// minutes, longitude along the top and bottom, latitude down the sides.
static void neat_line(BoatMinimapLayer *layer)
{
    Image *frame = &layer->frame;
    for (int x = 1; x < frame->width - 1; ++x) {
        double east = layer->north_west.x + (x - TRIM + .5) / layer->scale;
        bool odd = (long)floor(ORIGIN_LONGITUDE * 60 + east / METRES_PER_LONGITUDE_MINUTE) & 1;
        ImageDrawPixel(frame, x, 1, odd ? paper : ink);
        ImageDrawPixel(frame, x, frame->height - 2, odd ? paper : ink);
    }
    for (int y = 1; y < frame->height - 1; ++y) {
        double north = layer->north_west.y - (y - TRIM + .5) / layer->scale;
        bool odd = (long)floor(ORIGIN_LATITUDE * 60 + north / METRES_PER_LATITUDE_MINUTE) & 1;
        ImageDrawPixel(frame, 1, y, odd ? paper : ink);
        ImageDrawPixel(frame, frame->width - 2, y, odd ? paper : ink);
    }
}

// Land is buff, built-up land darker, and land touching water carries the black coastline.
// Water is white offshore and blue near land; the game has no soundings, so the tints stand
// for distance from shore, not depth.
static bool bake(BoatMinimapLayer *layer, const Mesh3D *mesh, size_t water_count, Rectangle extent,
    int max_width, int max_height)
{
    layer->scale = fminf(max_width / extent.width, max_height / extent.height);
    layer->north_west = (Vector2) { extent.x, extent.y + extent.height };
    int width = (int)ceilf(extent.width * layer->scale);
    int height = (int)ceilf(extent.height * layer->scale);
    int columns = width * SUBSAMPLES, rows = height * SUBSAMPLES;
    size_t samples = (size_t)columns * rows;
    unsigned char *mask = calloc(samples, 1), *roof = calloc(samples, 1);
    float *distance = malloc(samples * sizeof(float));
    float *lowest = malloc((size_t)width * height * sizeof(float));
    unsigned char *wet = malloc((size_t)width * height), *roofed = malloc((size_t)width * height);
    layer->base = GenImageColor(width, height, land);
    layer->frame = GenImageColor(width + TRIM * 2, height + TRIM * 2, ink);
    bool ok = mask && roof && distance && lowest && wet && roofed && layer->base.data
        && layer->frame.data;
    if (ok) {
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
        shore_distance(mask, distance, columns, rows);
        built_up(layer, mesh, water_count, width, height, roof, lowest);
        // Per pixel: water (WET 1), land (0) or uncharted (2, outside every map triangle), and
        // the roof fraction, smoothed over 3x3 pixels so built-up areas read as solid tints.
        Color *pixels = layer->base.data;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                int count = 0, roofs = 0;
                for (int sy = 0; sy < SUBSAMPLES; ++sy)
                    for (int sx = 0; sx < SUBSAMPLES; ++sx) {
                        size_t s = (size_t)(y * SUBSAMPLES + sy) * columns + x * SUBSAMPLES + sx;
                        count += mask[s];
                        roofs += roof[s];
                    }
                int i = y * width + x;
                wet[i] = count * 2 >= SUBSAMPLES * SUBSAMPLES ? 1 : lowest[i] == FLT_MAX ? 2 : 0;
                roofed[i] = (unsigned char)roofs;
            }
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                int i = y * width + x, roofs = 0, area = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (x + dx >= 0 && y + dy >= 0 && x + dx < width && y + dy < height) {
                            roofs += roofed[(y + dy) * width + x + dx];
                            area += SUBSAMPLES * SUBSAMPLES;
                        }
                size_t centre = (size_t)(y * SUBSAMPLES + SUBSAMPLES / 2) * columns + x * SUBSAMPLES
                    + SUBSAMPLES / 2;
                float metres = distance[centre] / subsample;
                if (wet[i] == 2)
                    pixels[i] = (x + y) % 4 ? paper : hatch; // Hatched: beyond the survey.
                else if (!wet[i])
                    pixels[i] = roofs * 5 >= area ? built : land;
                else
                    pixels[i] = metres < NEAR_SHORE ? shallow : metres < OFFING ? offing : paper;
            }
        // The coastline: land pixels beside water. Uncharted pixels carry none, so water cut by
        // the map's edge does not pretend to be a shore.
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                if (wet[y * width + x])
                    continue;
                bool shore = false;
                for (int d = 0; d < 4 && !shore; ++d) {
                    int nx = x + (d == 0) - (d == 1), ny = y + (d == 2) - (d == 3);
                    shore = nx >= 0 && ny >= 0 && nx < width && ny < height
                        && wet[ny * width + nx] == 1;
                }
                if (shore)
                    pixels[y * width + x] = coast;
            }
        neat_line(layer);
    }
    free(mask);
    free(roof);
    free(distance);
    free(lowest);
    free(wet);
    free(roofed);
    if (!ok)
        destroy_layer(layer);
    return ok;
}

bool boat_minimap_init(BoatMinimap *minimap, const BoatWorldDefinition *location)
{
    *minimap = (BoatMinimap) { 0 };
    const BoatMapDescription *map
        = location && location->mapped ? boat_scenery_drawn(location->map) : NULL;
    if (!map || !map->mesh)
        return false;
    const Mesh3D *mesh = map->mesh;
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

static bool inside(const BoatMinimapLayer *layer, int x, int y)
{
    return x >= TRIM && y >= TRIM && x < layer->frame.width - TRIM
        && y < layer->frame.height - TRIM;
}

// Symbols never cover the neat line.
static void dot(BoatMinimapLayer *layer, int x, int y, Color color)
{
    if (inside(layer, x, y))
        ((Color *)layer->frame.data)[y * layer->frame.width + x] = color;
}

static void square(BoatMinimapLayer *layer, int x, int y, int radius, Color color)
{
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
            dot(layer, x + dx, y + dy, color);
}

static void line(BoatMinimapLayer *layer, Vector2 from, Vector2 to, Color color)
{
    int steps = (int)ceilf(fmaxf(fabsf(to.x - from.x), fabsf(to.y - from.y)));
    for (int i = 0; i <= steps; ++i) {
        float t = steps ? (float)i / steps : 0;
        dot(layer, (int)floorf(from.x + (to.x - from.x) * t),
            (int)floorf(from.y + (to.y - from.y) * t), color);
    }
}

// Five-by-five symbols, rows of five-bit masks with the highest bit leftmost.
static void symbol(BoatMinimapLayer *layer, const unsigned char rows[5], int x, int y, Color color)
{
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 5; ++col)
            if (rows[row] & (16 >> col))
                dot(layer, x - 2 + col, y - 2 + row, color);
}

// Chart lettering: dark or magenta capitals on a knocked-out paper halo, kept inside the frame.
static void text(BoatMinimapLayer *layer, const char *string, int centre, int top, Color color)
{
    int width = (int)strlen(string) * 4 - 1;
    int left = centre - width / 2;
    left = (int)fmaxf(TRIM + 1, fminf((float)left, (float)(layer->frame.width - TRIM - 1 - width)));
    top = (int)fmaxf(TRIM + 1, fminf((float)top, (float)(layer->frame.height - TRIM - 6)));
    for (int pass = 0; pass < 2; ++pass)
        for (const char *c = string; *c; ++c) {
            const char *rows = boat_hud_glyph(*c);
            int x = left + (int)(c - string) * 4;
            for (int row = 0; rows && row < 5; ++row)
                for (int col = 0; col < 3; ++col)
                    if ((rows[row] - '0') & (4 >> col)) {
                        if (pass)
                            dot(layer, x + col, top + row, color);
                        else
                            square(layer, x + col, top + row, 1, paper);
                    }
        }
}

// The caution-area limit: a dashed magenta circle.
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

static void ring(BoatMinimapLayer *layer, int x, int y, int radius, Color color)
{
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
            float d = sqrtf((float)(dx * dx + dy * dy));
            if (fabsf(d - radius) < .5f)
                dot(layer, x + dx, y + dy, color);
        }
}

// ECDIS own ship: two concentric rings with a heading line from them, distinct from the single
// ring of a conspicuous landmark, in orange to stand out from the chart. No halo: the chart
// shows through between the strokes.
static void own_ship(BoatMinimapLayer *layer, Vector2 centre, float heading, float length)
{
    // Simulation forward (sin, -cos) is map (-sin, -cos): east is -X, north is +Z; rows run south.
    Vector2 forward = { -sinf(heading), cosf(heading) };
    int x = (int)floorf(centre.x), y = (int)floorf(centre.y);
    Vector2 from = { (float)x + .5f, (float)y + .5f };
    Vector2 to = { from.x + forward.x * length, from.y + forward.y * length };
    ring(layer, x, y, 3, own_ship_color);
    ring(layer, x, y, 1, own_ship_color);
    line(layer, from, to, own_ship_color);
    dot(layer, x, y, own_ship_color);
}

static void bridge(BoatMinimapLayer *layer, bool chart)
{
    Vector2 a = project_map(layer, bridge_ends[0]), b = project_map(layer, bridge_ends[1]);
    float length = hypotf(b.x - a.x, b.y - a.y);
    if (length < 1 || !inside(layer, (int)a.x, (int)a.y) || !inside(layer, (int)b.x, (int)b.y))
        return;
    Vector2 side = { -(b.y - a.y) / length, (b.x - a.x) / length };
    for (int s = -1; s <= 1; s += 2)
        line(layer, (Vector2) { a.x + side.x * s, a.y + side.y * s },
            (Vector2) { b.x + side.x * s, b.y + side.y * s }, ink);
    if (chart) {
        // On the south-east bank, clear of the river's symbols and the scale bar.
        int x = (int)((a.x + b.x) / 2) + 35, y = (int)((a.y + b.y) / 2) + 12;
        text(layer, "ALVSBORGSBRON", x, y, ink);
        text(layer, "VCL 45", x, y + 6, ink);
    }
}

// Charted marks: a pile in the mark's colour outlined in black, with a magenta flare if lit.
static void marks(BoatMinimapLayer *layer)
{
    for (size_t i = 0; i < sizeof(chart_marks) / sizeof(chart_marks[0]); ++i) {
        Vector2 p = project_map(layer, chart_marks[i].map);
        int x = (int)floorf(p.x), y = (int)floorf(p.y);
        if (!inside(layer, x, y))
            continue;
        for (int dy = -2; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                dot(layer, x + dx, y + dy, ink);
        dot(layer, x, y - 1, chart_marks[i].color);
        dot(layer, x, y, chart_marks[i].color);
        if (chart_marks[i].lit) {
            dot(layer, x + 2, y - 2, magenta);
            dot(layer, x + 2, y - 3, magenta);
        }
    }
}

static void scale_bar(BoatMinimapLayer *layer)
{
    bool half = NAUTICAL_MILE * layer->scale > 80;
    int length = (int)lroundf(NAUTICAL_MILE * (half ? .5f : 1) * layer->scale);
    int x = layer->frame.width - TRIM - 5 - length, y = layer->frame.height - TRIM - 4;
    for (int i = -1; i <= length + 1; ++i)
        for (int dy = -1; dy <= 2; ++dy)
            dot(layer, x + i, y + dy, ink);
    // Alternating cables (tenths of a mile).
    for (int i = 0; i < length; ++i)
        if ((int)floorf(i * (half ? 5.0f : 10.0f) / length) % 2 == 0)
            dot(layer, x + i, y, paper), dot(layer, x + i, y + 1, paper);
    text(layer, half ? "0.5 NM" : "1 NM", x + length / 2, y - 8, ink);
}

// A small magenta compass rose with its north arrow.
static void compass(BoatMinimapLayer *layer)
{
    int x = TRIM + 8, y = TRIM + 13;
    ring(layer, x, y, 5, magenta);
    for (int i = 1; i <= 7; ++i)
        dot(layer, x, y - i + 1, magenta);
    dot(layer, x - 1, y - 4, magenta);
    dot(layer, x + 1, y - 4, magenta);
    for (int i = -3; i <= 3; ++i)
        dot(layer, x + i, y, magenta);
    dot(layer, x, y + 3, magenta);
    text(layer, "N", x, y - 12, magenta);
}

static void ferry(BoatMinimapLayer *layer, bool chart)
{
    const WaterfrontFerry *f = waterfront_harbor_ferry();
    Vector2 centre = project_map(layer, (Vector2) { f->position.x, f->position.z });
    if (!inside(layer, (int)centre.x, (int)centre.y))
        return;
    // The hull's long axis, from the renderer heading (bow along renderer (sin, cos)).
    Vector2 along = { sinf(f->heading), -cosf(f->heading) };
    float half = fmaxf(1, f->length * .5f * layer->scale);
    float beam = fmaxf(1, f->beam * layer->scale);
    Vector2 side = { -along.y, along.x };
    // The hull at its charted length and beam, light grey inside an ink outline.
    for (int pass = 0; pass < 2; ++pass)
        for (float w = -beam * .5f - (pass ? 0 : 1); w <= beam * .5f + (pass ? 0 : 1); w += .5f)
            line(layer,
                (Vector2) { centre.x - along.x * (half + !pass) + side.x * w,
                    centre.y - along.y * (half + !pass) + side.y * w },
                (Vector2) { centre.x + along.x * (half + !pass) + side.x * w,
                    centre.y + along.y * (half + !pass) + side.y * w },
                pass ? hull : ink);
    if (chart)
        text(layer, "FERRY", (int)centre.x - 14, (int)centre.y + 3, ink);
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

    bridge(layer, chart);
    ferry(layer, chart);
    marks(layer);
    for (size_t i = 0; i < sizeof(landmarks) / sizeof(landmarks[0]); ++i) {
        Vector2 p = project_map(layer, landmarks[i].map);
        int x = (int)floorf(p.x), y = (int)floorf(p.y);
        if (!inside(layer, x, y))
            continue;
        // Conspicuous object: a ring with its position dot.
        ring(layer, x, y, 2, ink);
        dot(layer, x, y, ink);
        if (chart)
            text(layer, landmarks[i].name, x + landmarks[i].label_dx, y + landmarks[i].label_dy,
                ink);
    }
    const BoatVoyageRoute *route = voyage ? voyage->route : NULL;
    if (route) {
        Vector2 centre = project_map(layer, to_map(route->exposure.centre));
        float radius = route->exposure.radius * route->exposed_fraction * layer->scale;
        dashed_circle(layer, centre, radius, magenta);
        if (chart)
            text(layer, "EXPOSED", (int)centre.x, (int)(centre.y - radius) - 7, magenta);
        // Anchorages: the recovery shelter in full magenta, the others pale.
        static const unsigned char anchor[5] = { 0x04, 0x0e, 0x04, 0x15, 0x0e };
        unsigned destination = route->shelter_count - 1;
        for (unsigned i = 0; i < destination; ++i) {
            Vector2 p = project_map(layer, to_map(route->shelters[i].position));
            int x = (int)floorf(p.x), y = (int)floorf(p.y);
            symbol(layer, anchor, x, y, i == voyage->shelter ? magenta : pale_magenta);
        }
        // The destination as a route waypoint: a magenta ring, filled once arrived.
        Vector2 p = project_map(layer, to_map(route->arrival_centre));
        int x = (int)floorf(p.x), y = (int)floorf(p.y);
        ring(layer, x, y, 3, magenta);
        square(layer, x, y, voyage->arrived ? 2 : 0, magenta);
        if (chart)
            text(layer, boat_voyage_destination(route), x, y + 6, magenta);
    }
    if (chart) {
        scale_bar(layer);
        compass(layer);
    }
    own_ship(layer, project_map(layer, to_map(position)), heading, chart ? 9 : 7);
    return &layer->frame;
}
