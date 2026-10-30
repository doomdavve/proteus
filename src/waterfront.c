#include "waterfront.h"
#include "waterfront_corner.h"
#include "waterfront_upstream.h"

#include "raymath.h"

#include <math.h>

// The crane proxy is deliberately open. Beam dimensions are authored assumptions;
// overall placement and approximate height come from the retained OSM feature.
static Vector3 crane_point(float x, float y, float z)
{
    const float angle = 0.056f;
    return (Vector3) { x * cosf(angle) - z * sinf(angle), y, x * sinf(angle) + z * cosf(angle) };
}

static void beam(Renderer3D *r, Vector3 a, Vector3 b, float width, float depth)
{
    Vector3 direction = Vector3Normalize(Vector3Subtract(b, a));
    Vector3 reference = fabsf(direction.y) > 0.9f ? (Vector3) { 1, 0, 0 } : (Vector3) { 0, 1, 0 };
    Vector3 right
        = Vector3Scale(Vector3Normalize(Vector3CrossProduct(reference, direction)), width / 2);
    Vector3 up = Vector3Scale(Vector3Normalize(Vector3CrossProduct(direction, right)), depth / 2);
    Vector3 vertices[8];
    for (int i = 0; i < 8; ++i) {
        Vector3 center = i < 4 ? a : b;
        int corner = i % 4;
        vertices[i] = Vector3Add(center,
            Vector3Add(Vector3Scale(right, corner == 0 || corner == 3 ? -1 : 1),
                Vector3Scale(up, corner < 2 ? -1 : 1)));
    }
    const unsigned int indices[12][3] = {
        { 0, 2, 1 },
        { 0, 3, 2 },
        { 5, 7, 4 },
        { 5, 6, 7 },
        { 4, 3, 0 },
        { 4, 7, 3 },
        { 1, 6, 5 },
        { 1, 2, 6 },
        { 3, 6, 2 },
        { 3, 7, 6 },
        { 4, 1, 5 },
        { 4, 0, 1 },
    };
    Triangle3D triangles[12];
    for (int i = 0; i < 12; ++i)
        triangles[i] = (Triangle3D) { { indices[i][0], indices[i][1], indices[i][2] },
            { 107, 67, 62, 255 } };
    Mesh3D mesh = { vertices, triangles, 12 };
    renderer3d_draw_mesh(r, &mesh, (Transform3D) { .scale = { 1, 1, 1 } });
}

// Each foot stands on the kit's ground; the gantry keeps its authored height.
static void crane(Renderer3D *r, const WaterfrontScenery *scenery)
{
    for (int side = -1; side <= 1; side += 2) {
        float x = (float)side, sill = 1.5f;
        for (int face = -1; face <= 1; face += 2) {
            float z = (float)face;
            Vector3 foot = crane_point(x * 49, 0, z * 5);
            foot.y = waterfront_scenery_ground(scenery, foot.x, foot.z);
            sill += foot.y * .5f;
            beam(r, foot, crane_point(x * 45, 73, z * 3), 3, 3);
        }
        beam(r, crane_point(x * 49, sill, -5), crane_point(x * 49, sill, 5), 4, 4);
        beam(r, crane_point(x * 47, 35, -4), crane_point(x * 47, 35, 4), 2, 2);
    }
    beam(r, crane_point(-55, 76, 0), crane_point(55, 76, 0), 8, 6);
    beam(r, crane_point(-15, 81, 0), crane_point(3, 81, 0), 8, 4);
    beam(r, crane_point(5, 75, 0), crane_point(5, 48, 0), .4f, .4f);
    beam(r, crane_point(2, 48, 0), crane_point(8, 48, 0), 1.5f, 1.5f);
}

static void glow_quad(Renderer3D *r, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color)
{
    Vector3 vertices[] = { a, b, c, d };
    Triangle3D triangles[] = { { { 0, 1, 2 }, color }, { { 0, 2, 3 }, color } };
    Mesh3D mesh = { vertices, triangles, 2 };
    renderer3d_draw_emissive_mesh(r, &mesh, (Transform3D) { .scale = { 1, 1, 1 } });
}

static void light_box(Renderer3D *r, Vector3 position, Vector3 scale, Color color)
{
    const unsigned char texel = 0;
    Color palette[256] = { 0 };
    palette[240] = color;
    Material3D material = { &texel, palette, 1, 1, 1, true };
    renderer3d_draw_textured_box(
        r, (Transform3D) { .position = position, .scale = scale }, &material);
}

// Windows of the land triangles of one tile, in index order.
static void windows(
    Renderer3D *r, const BoatMapDescription *map, const unsigned int *triangles, size_t count)
{
    // Each imported wall starts with a triangle a,c,b (two base vertices).
    // Derive inset windows from that edge, preserving arbitrary facade orientation.
    // Rows are measured from the wall's base, which is 3.5 m in the river.
    const Mesh3D *city = map->mesh;
    for (size_t k = 0; k < count; ++k) {
        size_t i = triangles[k];
        if (boat_map_is_bridge_triangle(map, i))
            continue;
        const Triangle3D *t = &city->triangles[i];
        Vector3 a = city->vertices[t->vertices[0]];
        Vector3 c = city->vertices[t->vertices[1]];
        Vector3 b = city->vertices[t->vertices[2]];
        if (!waterfront_window_wall(t->color, a, c, b))
            continue;
        Vector3 edge = Vector3Subtract(b, a);
        float length = Vector3Length(edge);
        Vector3 along = Vector3Scale(edge, 1 / length);
        Vector3 normal = { along.z, 0, -along.x };
        if (Vector3DotProduct(normal, Vector3Subtract(r->camera.position, a)) <= 0
            || Vector3Distance(r->camera.position, a) > 430)
            continue;
        for (int floor = 0; a.y + 2.5f + floor * 3.2f < c.y - 1; ++floor) {
            for (int column = 0; 2.5f + column * 4 < length - 1.5f; ++column) {
                unsigned int seed = (unsigned int)i * 1664525u + (unsigned int)column * 1013904223u
                    + (unsigned int)floor * 374761393u;
                Color color = seed % 7 < 3 ? (Color) { 138, 185, 204, 255 }
                    : seed % 7 == 3        ? (Color) { 181, 211, 221, 255 }
                                           : (Color) { 66, 100, 120, 255 };
                Vector3 p = Vector3Add(Vector3Add(a, Vector3Scale(along, 2.5f + column * 4)),
                    Vector3Scale(normal, .035f));
                p.y = a.y + 1.5f + floor * 3.2f;
                Vector3 q = Vector3Add(p, Vector3Scale(along, floor == 0 ? 2.2f : 1.5f));
                Vector3 up = { 0, floor == 0 ? 1.9f : 1.5f, 0 };
                glow_quad(r, p, Vector3Add(p, up), Vector3Add(q, up), q, color);
            }
        }
    }
}

static void waterline_details(Renderer3D *r, const WaterfrontScenery *scenery)
{
    size_t count = scenery->count;
    const WaterfrontCornerBox *boxes = scenery->boxes;
    const unsigned int faces[][3]
        = { { 0, 2, 1 }, { 0, 3, 2 }, { 5, 7, 4 }, { 5, 6, 7 }, { 4, 3, 0 }, { 4, 7, 3 },
              { 1, 6, 5 }, { 1, 2, 6 }, { 3, 6, 2 }, { 3, 7, 6 }, { 4, 1, 5 }, { 4, 0, 1 } };
    for (size_t i = 0; i < count; ++i) {
        if (!boxes[i].waterline)
            continue;
        Triangle3D triangles[12];
        for (int f = 0; f < 12; ++f)
            triangles[f]
                = (Triangle3D) { { faces[f][0], faces[f][1], faces[f][2] }, boxes[i].color };
        Mesh3D mesh = { boxes[i].vertices, triangles, 12 };
        renderer3d_draw_mesh(r, &mesh, (Transform3D) { .scale = { 1, 1, 1 } });
    }
}

void waterfront_draw_traffic(Renderer3D *r, const WaterfrontTrafficFrame *frame)
{
    static const Vector3 vertices[]
        = { { -.5f, 0, -.5f }, { -.5f, 0, .5f }, { .5f, 0, .5f }, { .5f, 0, -.5f } };
    for (int i = 0; i < frame->wake_count; ++i) {
        const WaterfrontTrafficWake *wake = &frame->wakes[i];
        Triangle3D triangles[] = { { { 0, 1, 2 }, wake->color }, { { 0, 2, 3 }, wake->color } };
        Mesh3D mesh = { vertices, triangles, 2 };
        renderer3d_draw_emissive_mesh(r, &mesh, wake->pose);
    }
    const Color colors[] = { { 222, 70, 52, 255 }, { 72, 191, 139, 255 }, { 235, 223, 188, 255 } };
    for (int i = 0; i < frame->vessel_count; ++i) {
        const WaterfrontVesselState *vessel = &frame->vessels[i];
        renderer3d_draw_mesh(r, waterfront_traffic_mesh_slot(frame->mesh[i]), vessel->pose);
        float size = i >= 3 ? .32f : .18f;
        for (int light = 0; light < WATERFRONT_TRAFFIC_LIGHTS; ++light)
            light_box(r, vessel->lights[light], (Vector3) { size, size, size }, colors[light]);
    }
    if (frame->lift)
        renderer3d_draw_mesh(r, waterfront_upstream_span_mesh(), frame->lift_pose);
}

void waterfront_draw_harbor(Renderer3D *r, const WaterfrontHarborFrame *frame)
{
    if (frame->modules_authored) {
        for (int i = 0; i < frame->module_count; ++i) {
            const WaterfrontModuleFrame *m = &frame->modules[i];
            if (m->kind == BOAT_SCENE_MODULE_GANGWAY)
                renderer3d_draw_box(r, m->pose, (Color) { 154, 123, 84, 255 });
            else
                renderer3d_draw_mesh(r, waterfront_harbor_module_mesh(m->kind), m->pose);
        }
    } else {
        renderer3d_draw_mesh(r, waterfront_harbor_quay_mesh(), frame->quay);
        renderer3d_draw_mesh(r, waterfront_harbor_pontoon_mesh(), frame->pontoon);
        renderer3d_draw_box(r, frame->gangway, (Color) { 154, 123, 84, 255 });
    }
    renderer3d_draw_mesh(r, waterfront_harbor_ferry_mesh(), frame->ferry);
    for (int i = 0; i < frame->berth_count; ++i)
        renderer3d_draw_mesh(r, waterfront_harbor_recipe_mesh(frame->recipes[i]), frame->boats[i]);
    for (int i = 0; i < frame->line_count; ++i)
        renderer3d_draw_box(r, frame->lines[i], (Color) { 169, 153, 113, 255 });
}

bool waterfront_draw_boating(Renderer3D *r, const WaterfrontScenery *scenery, Color *pixels,
    int width, int height, View3D camera, bool omit_water, WaterfrontSquall squall)
{
    if (!scenery->ready || !scenery->map)
        return false;
    waterfront_sky_draw_squall(pixels, width, height, camera, squall);
    if (!renderer3d_begin(r, pixels, width, height, camera, waterfront_sun_direction()))
        return false;
    // Flat water first, then the land of each tile in view; land triangles keep their index
    // order within a tile. Windows below follow the same tiles. Window quads stand 3.5 cm
    // off their wall, so the test grows each tile's box by more than that.
    const BoatMapTiles *tiles = &scenery->tiles;
    const Mesh3D *city = scenery->full->mesh;
    if (!omit_water) {
        Mesh3D water = *city;
        water.triangle_count = tiles->water_count;
        renderer3d_draw_mesh(r, &water, (Transform3D) { .scale = { 1, 1, 1 } });
    }
    const Vector3 grow = { .1f, .1f, .1f };
    size_t count = boat_map_tiles_count(tiles);
    for (size_t t = 0; t < count; ++t) {
        const BoatMapTile *tile = &tiles->tiles[t];
        if (tile->land_count
            && renderer3d_box_visible(
                r, Vector3Subtract(tile->land_min, grow), Vector3Add(tile->land_max, grow)))
            renderer3d_draw_mesh_triangles(
                r, city, &tiles->land[tile->land_first], tile->land_count);
    }
    if (scenery->river) {
        WaterfrontSceneFrame frame;
        waterfront_scene_evaluate(0, (BoatWater) { 0 }, &frame);
        renderer3d_draw_box(r, frame.bridge, frame.bridge_color);
    }
    waterline_details(r, scenery);
    for (size_t t = 0; t < count; ++t) {
        const BoatMapTile *tile = &tiles->tiles[t];
        if (tile->land_count
            && renderer3d_box_visible(
                r, Vector3Subtract(tile->land_min, grow), Vector3Add(tile->land_max, grow)))
            windows(r, scenery->full, &tiles->land[tile->land_first], tile->land_count);
    }
    if (scenery->river)
        crane(r, scenery);
    return true;
}
