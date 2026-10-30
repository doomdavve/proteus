#include "boat_flag.h"
#include "boat_pose.h"
#include "boat_skin.h"
#include "boat_view.h"
#include "raymath.h"

// Keep the checks in Release builds, where CI runs this validator.
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// Probe the flat rendered fabric, independently of its cell/triangle layout.
static Color flag_color_at(const Mesh3D *mesh, float x, float y)
{
    for (size_t i = 0; i < mesh->triangle_count; ++i) {
        const Triangle3D *t = &mesh->triangles[i];
        Vector3 a = mesh->vertices[t->vertices[0]], b = mesh->vertices[t->vertices[1]],
                c = mesh->vertices[t->vertices[2]];
        float area = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        float u = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / area;
        float v = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / area;
        if (u >= 0 && v >= 0 && u + v <= 1)
            return t->color;
    }
    assert(!"Missing fabric at flag probe");
    return (Color) { 0 };
}

static void flags(void)
{
    const float columns[] = { 0, .15625f, .3125f, .4375f, .55f, .6625f, .775f, .8875f, 1 };
    BoatFlagSurface surface;
    for (int y = 0; y < BOAT_FLAG_ROWS; ++y)
        for (int x = 0; x < BOAT_FLAG_COLS; ++x)
            surface.points[y * BOAT_FLAG_COLS + x] = (Vector3) { columns[x], y / 5.0f, 0 };
    BoatFlagSurface original = surface;
    BoatFlagMesh sweden_storage, other_storage;
    Mesh3D sweden = boat_flag_skin_mesh(&surface, BOAT_SKIN_SWEDEN, &sweden_storage);
    BoatFlagMesh retained = sweden_storage;
    const Color fields[] = { { 0, 106, 167, 255 }, { 250, 246, 237, 255 }, { 198, 12, 48, 255 },
        { 186, 12, 47, 255 }, { 242, 118, 28, 255 }, { 0, 106, 167, 255 } };
    const Color crosses[] = { { 255, 205, 0, 255 }, { 0, 53, 128, 255 }, { 250, 246, 237, 255 },
        { 0, 32, 91, 255 }, { 58, 64, 70, 255 }, { 255, 205, 0, 255 } };
    for (unsigned int skin = 0; skin < BOAT_SKIN_COUNT; ++skin) {
        Mesh3D mesh = boat_flag_skin_mesh(&surface, skin, &other_storage);
        assert(mesh.triangle_count == BOAT_FLAG_MESH_TRIANGLES);
        assert(memcmp(mesh.vertices, sweden.vertices, sizeof(other_storage.vertices)) == 0);
        for (size_t i = 0; i < mesh.triangle_count; ++i) {
            const Triangle3D *t = &mesh.triangles[i];
            for (int j = 0; j < 3; ++j)
                assert(t->vertices[j] < BOAT_FLAG_MESH_POINTS);
            assert(memcmp(t->vertices, sweden.triangles[i].vertices, sizeof(t->vertices)) == 0);
            assert(memcmp(&t->color, &mesh.triangles[(i / 4) * 4].color, sizeof(Color)) == 0);
        }
        Color field = flag_color_at(&mesh, .12f, .13f);
        Color vertical = flag_color_at(&mesh, .375f, .13f);
        Color horizontal = flag_color_at(&mesh, .12f, .51f);
        assert(memcmp(&field, &fields[skin], sizeof(Color)) == 0);
        assert(memcmp(&vertical, &crosses[skin], sizeof(Color)) == 0);
        assert(memcmp(&horizontal, &crosses[skin], sizeof(Color)) == 0);
        if (skin == BOAT_SKIN_NORWAY) {
            Color white = { 250, 246, 237, 255 };
            const Vector2 border[]
                = { { .33f, .13f }, { .42f, .13f }, { .12f, .42f }, { .12f, .58f } };
            for (size_t i = 0; i < sizeof(border) / sizeof(border[0]); ++i) {
                Color color = flag_color_at(&mesh, border[i].x, border[i].y);
                assert(memcmp(&color, &white, sizeof(Color)) == 0);
            }
        }
    }
    assert(memcmp(&surface, &original, sizeof(surface)) == 0);
    assert(memcmp(&sweden_storage, &retained, sizeof(retained)) == 0);
    boat_flag_skin_mesh(&surface, 999, &other_storage);
    assert(memcmp(&sweden_storage, &other_storage, sizeof(other_storage)) == 0);
}

static void twin_outboards(void)
{
    Boat boat
        = { .position = { 17, -8 }, .height = .3f, .heading = .7f, .roll = .12f, .pitch = -.2f };
    for (int steering = -1; steering <= 1; ++steering) {
        boat.engine_angle = steering * .6f;
        BoatPoseParts pose = boat_pose_parts(&boat);
        for (int side = -1; side <= 1; side += 2) {
            Transform3D motor = boat_skin_outboard_pose(pose.hull, pose.engine, side);
            Vector3 expected
                = boat_pose_attachment(&boat, (Vector3) { side * .36f, .4f, BOAT_ENGINE_ARM });
            assert(Vector3Distance(motor.position, expected) < .00001f);
            assert(Vector3Distance(motor.rotation, pose.engine.rotation) < .00001f);
            assert(Vector3Distance(motor.scale, pose.engine.scale) < .00001f);
        }
    }
}

static void cabin_waterline(void)
{
    BoatViewState view = { 0 };
    view.boat.heading = .7f;
    view.boat.roll = .2f;
    view.boat.pitch = -.12f;
    BoatFlag flag = { 0 };
    boat_flag_reset(&flag, boat_pose_hull(&view.boat), (Vector3) { 0 }, 0);
    BoatFlagSurface original = flag.surface;
    WaterfrontPlayerState normal = { 0 }, cabin = { 0 };
    view.skin = BOAT_SKIN_SWEDEN;
    boat_player_pose(&view, &flag, &normal);
    view.skin = BOAT_SKIN_CABIN;
    boat_player_pose(&view, &flag, &cabin);
    Vector3 offset = Vector3Transform((Vector3) { 0, -.40f, 0 }, boat_pose_rotation(&view.boat));
    assert(
        Vector3Distance(cabin.hull.position, Vector3Add(normal.hull.position, offset)) < .00001f);
    assert(
        Vector3Distance(cabin.cabin.position, Vector3Add(normal.cabin.position, offset)) < .00001f);
    for (int i = 0; i < BOAT_FLAG_POINTS; ++i)
        assert(Vector3Distance(cabin.flag.points[i], Vector3Add(normal.flag.points[i], offset))
            < .00001f);
    assert(memcmp(&flag.surface, &original, sizeof(original)) == 0);
}

int main(void)
{
    cabin_waterline();
    twin_outboards();
    flags();
    // The countries share the white hull; the explorer's differs. Accent stripes retain a
    // distinct livery color.
    const Color white_hull = { 239, 243, 245, 255 };
    for (unsigned int skin = 0; skin < BOAT_SKIN_CABIN; ++skin) {
        BoatSkinPalette palette = boat_skin_palette(skin);
        assert((memcmp(&palette.hull, &white_hull, sizeof(Color)) == 0)
            == (skin < BOAT_SKIN_COUNTRIES));
        for (unsigned int other = 0; other < skin; ++other) {
            BoatSkinPalette previous = boat_skin_palette(other);
            assert(memcmp(&palette.accent, &previous.accent, sizeof(Color)) != 0);
        }
    }
    // Every livery has identical vertices/topology. The broad color panels must differ.
    for (int part = BOAT_SKIN_HULL; part <= BOAT_SKIN_ENGINE; ++part) {
        const Mesh3D *base = boat_skin_mesh(0, part);
        assert(base && base->triangle_count > 0);
        for (unsigned int skin = 0; skin < BOAT_SKIN_CABIN; ++skin) {
            unsigned int parsed = 99;
            assert(boat_skin_parse(boat_skin_name(skin), &parsed) && parsed == skin);
            const Mesh3D *mesh = boat_skin_mesh(skin, part);
            assert(mesh->triangle_count == base->triangle_count);
            bool different = false;
            for (size_t i = 0; i < mesh->triangle_count; ++i) {
                const Triangle3D *t = &mesh->triangles[i];
                assert(memcmp(t->vertices, base->triangles[i].vertices, sizeof(t->vertices)) == 0);
                different |= memcmp(&t->color, &base->triangles[i].color, sizeof(Color)) != 0;
                Vector3 v[3];
                for (int j = 0; j < 3; ++j) {
                    assert(t->vertices[j] < mesh->triangle_count * 3);
                    v[j] = mesh->vertices[t->vertices[j]];
                    assert(isfinite(v[j].x) && isfinite(v[j].y) && isfinite(v[j].z));
                    assert(memcmp(&v[j], &base->vertices[t->vertices[j]], sizeof(Vector3)) == 0);
                }
                assert(Vector3Length(Vector3CrossProduct(
                           Vector3Subtract(v[1], v[0]), Vector3Subtract(v[2], v[0])))
                    > .000001f);
            }
            assert(skin == 0 || different);
        }
    }
    unsigned int cabin = 99;
    assert(boat_skin_parse("cabin", &cabin) && cabin == BOAT_SKIN_CABIN);
    assert(boat_skin_mesh(cabin, BOAT_SKIN_HULL)->triangle_count > 0);
    assert(boat_skin_mesh(cabin, BOAT_SKIN_COCKPIT)->triangle_count > 0);
    for (int part = BOAT_SKIN_HULL; part <= BOAT_SKIN_COCKPIT; ++part) {
        const Mesh3D *mesh = boat_skin_mesh(cabin, part);
        for (size_t i = 0; i < mesh->triangle_count; ++i) {
            const Triangle3D *t = &mesh->triangles[i];
            Vector3 v[3];
            for (int j = 0; j < 3; ++j) {
                assert(t->vertices[j] < mesh->triangle_count * 3);
                v[j] = mesh->vertices[t->vertices[j]];
                assert(isfinite(v[j].x) && isfinite(v[j].y) && isfinite(v[j].z));
            }
            assert(Vector3Length(Vector3CrossProduct(
                       Vector3Subtract(v[1], v[0]), Vector3Subtract(v[2], v[0])))
                > .000001f);
        }
    }
    assert(boat_skin_mesh(999, BOAT_SKIN_HULL) == boat_skin_mesh(0, BOAT_SKIN_HULL));
    unsigned int unchanged = 2;
    assert(!boat_skin_parse("unknown", &unchanged) && unchanged == 2);
    assert(!boat_skin_parse(NULL, &unchanged));
    assert(!boat_skin_mesh(0, (BoatSkinPart)99));
    BoatModel custom;
    BoatSkinPalette palette = boat_skin_palette(0);
    palette.hull = (Color) { 3, 7, 11, 255 };
    bool generated = boat_skin_generate(&custom, palette, BOAT_SKIN_HULL);
    assert(generated);
    bool found = false;
    for (size_t i = 0; i < custom.mesh.triangle_count; ++i)
        found |= memcmp(&custom.triangles[i].color, &palette.hull, sizeof(Color)) == 0;
    assert(found);
    // World hull silhouette remains within the existing player footprint.
    Boat boat = { 0 };
    BoatPoseParts pose = boat_pose_parts(&boat);
    for (size_t i = 0; i < custom.mesh.triangle_count * 3; ++i) {
        Vector3 v = boat_model_point(pose.hull, custom.vertices[i]);
        assert(fabsf(v.x) <= 1.051f && v.z >= -2.311f && v.z <= 1.48f);
    }
    puts("Nordic skins and flags: independent colors, bordered cross, shared geometry, custom "
         "palette, finite nondegenerate meshes and bounds "
         "pass.");
    return 0;
}
