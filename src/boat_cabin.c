#include "boat_cabin.h"
#include "boat_flag.h"
#include "raymath.h"

// Historic cabin craft, retained in its original local hull and cabin coordinates.
static void player_box(Vector3 *vertices, Triangle3D *triangles, int *count, Vector3 position,
    Vector3 size, Color color)
{
    static const unsigned int faces[][3]
        = { { 0, 2, 1 }, { 0, 3, 2 }, { 4, 5, 6 }, { 4, 6, 7 }, { 0, 1, 5 }, { 0, 5, 4 },
              { 3, 7, 6 }, { 3, 6, 2 }, { 0, 4, 7 }, { 0, 7, 3 }, { 1, 2, 6 }, { 1, 6, 5 } };
    static const Vector3 corners[] = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
        { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } };
    // Each box owns 36 vertices; this keeps the little mesh builder straightforward.
    for (int face = 0; face < 12; ++face) {
        unsigned int base = (unsigned int)*count * 3;
        for (int v = 0; v < 3; ++v) {
            Vector3 corner = corners[faces[face][v]];
            vertices[base + v] = Vector3Add(position,
                (Vector3) {
                    corner.x * size.x * .5f, corner.y * size.y * .5f, corner.z * size.z * .5f });
        }
        triangles[(*count)++] = (Triangle3D) { { base, base + 1, base + 2 }, color };
    }
}

static const Mesh3D *old_hull(void)
{
    static Vector3 vertices[432];
    static Triangle3D triangles[144];
    static Mesh3D mesh = { vertices, triangles, 0 };
    if (mesh.triangle_count == 0) {
        const Vector2 outline[]
            = { { 0, -1.65f }, { -.65f, -.85f }, { -.75f, .9f }, { .75f, .9f }, { .65f, -.85f } };
        // Continue the hull up, across a broad cream lip, and down to the inset deck.
        const float heights[] = { .05f, .36f, .46f, .55f, .68f, .68f, .55f };
        const Color colors[]
            = { { 225, 83, 70, 255 }, { 255, 222, 143, 255 }, { 39, 104, 120, 255 },
                  { 39, 104, 120, 255 }, { 255, 237, 191, 255 }, { 191, 143, 85, 255 } };
        for (int ring = 0; ring < 7; ++ring)
            for (int v = 0; v < 5; ++v) {
                float taper = ring == 0 || ring >= 5 ? .82f : 1;
                vertices[ring * 5 + v]
                    = (Vector3) { outline[v].x * taper, heights[ring], outline[v].y * taper };
            }
        for (int ring = 0; ring < 6; ++ring)
            for (unsigned int v = 0; v < 5; ++v) {
                unsigned int a = ring * 5 + v, b = ring * 5 + (v + 1) % 5;
                triangles[mesh.triangle_count++] = (Triangle3D) { { a, b, b + 5 }, colors[ring] };
                triangles[mesh.triangle_count++]
                    = (Triangle3D) { { a, b + 5, a + 5 }, colors[ring] };
            }
        for (unsigned int v = 1; v < 4; ++v) {
            triangles[mesh.triangle_count++]
                = (Triangle3D) { { 30, 30 + v, 31 + v }, { 218, 163, 94, 255 } };
            triangles[mesh.triangle_count++] = (Triangle3D) { { 0, v + 1, v }, colors[0] };
        }
        int count = (int)mesh.triangle_count;
        // Stern ensign staff on the starboard quarter, clear of the steering outboard.
        int staff_start = count * 3;
        player_box(vertices, triangles, &count,
            (Vector3) { .69f / 1.4f, (BOAT_FLAG_STAFF_BASE_Y + BOAT_FLAG_STAFF_HEIGHT * .5f) / 1.4f,
                1.30f / 1.4f },
            (Vector3) { .032f / 1.4f, BOAT_FLAG_STAFF_HEIGHT / 1.4f, .032f / 1.4f },
            (Color) { 210, 172, 112, 255 });
        // Rake aft around the foot; match the cloth hoist's physical attachment line.
        for (int i = staff_start; i < count * 3; ++i)
            vertices[i].z += .2f * (vertices[i].y - BOAT_FLAG_STAFF_BASE_Y / 1.4f);
        // Raised side treads and a little foredeck hatch sit clear of the deck surface.
        for (int side = -1; side <= 1; side += 2)
            player_box(vertices, triangles, &count, (Vector3) { side * .49f, .565f, -.05f },
                (Vector3) { .12f, .03f, 1.1f }, (Color) { 247, 208, 137, 255 });
        player_box(vertices, triangles, &count, (Vector3) { 0, .57f, -.94f },
            (Vector3) { .48f, .04f, .42f }, (Color) { 39, 104, 120, 255 });
        player_box(vertices, triangles, &count, (Vector3) { 0, .6f, -.94f },
            (Vector3) { .36f, .02f, .3f }, (Color) { 255, 190, 57, 255 });
        mesh.triangle_count = (size_t)count;
    }
    return &mesh;
}

static const Mesh3D *old_cabin(void)
{
    static Vector3 vertices[720];
    static Triangle3D triangles[240];
    static Mesh3D mesh = { vertices, triangles, 0 };
    if (mesh.triangle_count == 0) {
        int count = 0;
        Color cream = { 255, 237, 191, 255 }, glass = { 55, 166, 181, 255 };
        Color yellow = { 255, 190, 57, 255 }, coral = { 234, 91, 64, 255 };
        player_box(
            vertices, triangles, &count, (Vector3) { 0, 0, 0 }, (Vector3) { 1, 1, 1 }, cream);
        player_box(vertices, triangles, &count, (Vector3) { 0, .57f, 0 },
            (Vector3) { 1.2f, .18f, 1.22f }, yellow);
        for (int side = -1; side <= 1; side += 2) {
            // Split windscreens and broad side windows, inset in chunky cream frames.
            for (int pane = -1; pane <= 1; pane += 2)
                player_box(vertices, triangles, &count,
                    (Vector3) { pane * .22f, .14f, side * .505f }, (Vector3) { .34f, .48f, .025f },
                    glass);
            player_box(vertices, triangles, &count, (Vector3) { side * .505f, .16f, -.03f },
                (Vector3) { .025f, .44f, .72f }, glass);
            // A blocky life preserver below each side window.
            for (int bar = -1; bar <= 1; bar += 2) {
                player_box(vertices, triangles, &count,
                    (Vector3) { side * .54f, -.28f + bar * .13f, .08f },
                    (Vector3) { .08f, .08f, .34f }, coral);
                player_box(vertices, triangles, &count,
                    (Vector3) { side * .54f, -.28f, .08f + bar * .13f },
                    (Vector3) { .08f, .18f, .08f }, yellow);
            }
        }
        mesh.triangle_count = (size_t)count;
    }
    return &mesh;
}

static Mesh3D rounded_hull(const Mesh3D *source, Vector3 *vertices, Triangle3D *triangles)
{
    // Insert two shoulders between the bow tip and the original forequarters.
    static const int old_to_new[] = { 0, 2, 3, 4, 5 };
    for (int ring = 0; ring < 7; ++ring) {
        const Vector3 *v = source->vertices + ring * 5;
        Vector3 *out = vertices + ring * 7;
        out[0] = v[0];
        out[2] = v[1];
        out[3] = v[2];
        out[4] = v[3];
        out[5] = v[4];
        out[1] = Vector3Lerp(v[0], v[1], .32f);
        out[6] = Vector3Lerp(v[0], v[4], .32f);
        // Move the shoulders forward, making the bow fuller without lengthening it.
        out[1].z = out[6].z = v[0].z + (v[1].z - v[0].z) * .18f;
    }
    size_t out_count = 0;
    for (int ring = 0; ring < 6; ++ring) {
        for (int edge = 0; edge < 7; ++edge) {
            int original_edge = edge < 2 ? 0 : edge - 1;
            if (edge >= 5)
                original_edge = edge == 5 ? 4 : 4;
            Color color = source->triangles[ring * 10 + original_edge * 2].color;
            unsigned a = ring * 7 + edge, b = ring * 7 + (edge + 1) % 7;
            triangles[out_count++] = (Triangle3D) { { a, b, b + 7 }, color };
            triangles[out_count++] = (Triangle3D) { { a, b + 7, a + 7 }, color };
        }
    }
    size_t extra_count = 0;
    for (size_t i = 60; i < source->triangle_count; ++i) {
        Triangle3D t = source->triangles[i];
        for (int k = 0; k < 3; ++k) {
            unsigned v = t.vertices[k];
            if (v < 35)
                t.vertices[k] = (v / 5) * 7 + old_to_new[v % 5];
            else {
                vertices[49 + extra_count] = source->vertices[v];
                t.vertices[k] = 49 + extra_count++;
            }
        }
        triangles[out_count++] = t;
    }
    return (Mesh3D) { vertices, triangles, out_count };
}

static Color cabin_color(Color c)
{
    static const Color before[] = { { 225, 83, 70, 255 }, { 255, 222, 143, 255 },
        { 39, 104, 120, 255 }, { 255, 237, 191, 255 }, { 191, 143, 85, 255 }, { 218, 163, 94, 255 },
        { 247, 208, 137, 255 }, { 255, 190, 57, 255 }, { 234, 91, 64, 255 },
        { 55, 166, 181, 255 } };
    static const Color after[] = { { 235, 238, 235, 255 }, { 48, 58, 59, 255 },
        { 230, 234, 231, 255 }, { 244, 245, 239, 255 }, { 173, 129, 80, 255 },
        { 183, 136, 82, 255 }, { 224, 227, 222, 255 }, { 243, 245, 240, 255 },
        { 191, 112, 73, 255 }, { 71, 111, 127, 255 } };
    for (size_t i = 0; i < sizeof(before) / sizeof(before[0]); ++i)
        if (c.r == before[i].r && c.g == before[i].g && c.b == before[i].b)
            return after[i];
    return c;
}

void boat_cabin_generate(BoatModel *m, bool hull)
{
    Vector3 rounded_vertices[700], cabin_vertices[720];
    Triangle3D rounded_triangles[180];
    const Mesh3D *source = hull ? old_hull() : old_cabin();
    Mesh3D adjusted = *source;
    if (hull) {
        adjusted = rounded_hull(source, rounded_vertices, rounded_triangles);
    } else {
        for (size_t i = 0; i < source->triangle_count * 3; ++i)
            cabin_vertices[i] = source->vertices[i];
        for (int i = 36; i < 72; ++i) {
            float factor = cabin_vertices[i].y > .57f ? .90f : 1.10f;
            cabin_vertices[i].x *= factor;
            cabin_vertices[i].z *= factor;
        }
        adjusted.vertices = cabin_vertices;
    }
    boat_model_begin(m);
    for (size_t i = 0; i < adjusted.triangle_count; ++i) {
        Triangle3D t = adjusted.triangles[i];
        unsigned int base = (unsigned int)m->mesh.triangle_count * 3;
        for (int j = 0; j < 3; ++j)
            m->vertices[base + j] = adjusted.vertices[t.vertices[j]];
        m->triangles[m->mesh.triangle_count++]
            = (Triangle3D) { { base, base + 1, base + 2 }, cabin_color(t.color) };
    }
    if (hull) {
        boat_model_box(m, (Vector3) { 0, .40f, 1.015f }, (Vector3) { 1.10f, .08f, .38f },
            (Color) { 221, 221, 204, 255 });
        boat_model_box(m, (Vector3) { 0, .45f, 1.015f }, (Vector3) { .98f, .025f, .31f },
            (Color) { 170, 121, 73, 255 });
        boat_model_box(m, (Vector3) { 0, -.10f, .76f }, (Vector3) { .18f, .30f, .18f },
            (Color) { 36, 46, 48, 255 });
    }
}
