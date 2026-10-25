#include "boat_skin.h"
#include "boat_flag.h"
#include "raymath.h"

#include <string.h>

static const char *names[] = { "sweden", "finland", "denmark", "norway" };
static const Color white_hull = { 239, 243, 245, 255 };
static const BoatSkinPalette palettes[] = {
    // Hull, deck, stripe/seat accent, upholstery. One shape and gameplay class for all four.
    { white_hull, { 0, 106, 167, 255 }, { 255, 205, 0, 255 }, { 26, 48, 65, 255 } },
    { white_hull, { 239, 243, 245, 255 }, { 0, 53, 128, 255 }, { 29, 48, 71, 255 } },
    { white_hull, { 198, 12, 48, 255 }, { 250, 246, 237, 255 }, { 63, 36, 43, 255 } },
    { white_hull, { 186, 12, 47, 255 }, { 0, 32, 91, 255 }, { 29, 40, 62, 255 } }
};
static const Color rubber = { 27, 39, 47, 255 };
static const Color glass = { 43, 85, 106, 255 };

unsigned int boat_skin_index(unsigned int skin) { return skin < BOAT_SKIN_COUNT ? skin : 0; }
const char *boat_skin_name(unsigned int skin) { return names[boat_skin_index(skin)]; }
BoatSkinPalette boat_skin_palette(unsigned int skin) { return palettes[boat_skin_index(skin)]; }

bool boat_skin_parse(const char *name, unsigned int *skin)
{
    if (!name || !skin)
        return false;
    for (unsigned int i = 0; i < BOAT_SKIN_COUNT; ++i)
        if (strcmp(name, names[i]) == 0) {
            *skin = i;
            return true;
        }
    return false;
}

static void triangle(BoatModel *m, Vector3 a, Vector3 b, Vector3 c, Color color)
{
    if (m->mesh.triangle_count >= BOAT_MODEL_MAX_TRIANGLES) {
        m->overflow = true;
        return;
    }
    unsigned int i = (unsigned int)m->mesh.triangle_count * 3;
    m->vertices[i] = a;
    m->vertices[i + 1] = b;
    m->vertices[i + 2] = c;
    m->triangles[m->mesh.triangle_count++] = (Triangle3D) { { i, i + 1, i + 2 }, color };
}

static void quad(BoatModel *m, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color)
{
    triangle(m, a, b, c, color);
    triangle(m, a, c, d, color);
}

// A thin solid pane avoids coincident front/back triangles in GPU depth/shadow passes.
static void pane(BoatModel *m, const Vector3 *front, int count)
{
    Vector3 normal = Vector3Normalize(Vector3CrossProduct(
        Vector3Subtract(front[1], front[0]), Vector3Subtract(front[2], front[0])));
    Vector3 back[4];
    for (int i = 0; i < count; ++i)
        back[i] = Vector3Subtract(front[i], Vector3Scale(normal, .018f));
    for (int i = 1; i + 1 < count; ++i) {
        triangle(m, front[0], front[i], front[i + 1], glass);
        triangle(m, back[0], back[i + 1], back[i], glass);
    }
    for (int i = 0; i < count; ++i) {
        int j = (i + 1) % count;
        quad(m, front[i], back[i], back[j], front[j], glass);
    }
}

static void hull(BoatModel *m, BoatSkinPalette p)
{
    // Five stations: a fine entry, flared shoulders, straight aft chines, compact transom.
    const float z[] = { -2.31f, -1.65f, -.65f, .55f, .90f, 1.26f };
    const float w[] = { .025f, .57f, .95f, 1.02f, .96f, .90f };
    const float y[] = { .65f, .79f, .78f, .72f, .72f, .72f };
    Vector3 ring[6][7];
    for (int i = 0; i < 6; ++i) {
        ring[i][0] = (Vector3) { -w[i], y[i], z[i] };
        ring[i][1] = (Vector3) { -w[i], y[i] - .055f, z[i] };
        ring[i][2] = (Vector3) { -w[i] * .82f, i == 0 ? .53f : .08f, z[i] };
        ring[i][3] = (Vector3) { 0, i == 0 ? .49f : -.24f, z[i] };
        ring[i][4] = (Vector3) { w[i] * .82f, i == 0 ? .53f : .08f, z[i] };
        ring[i][5] = (Vector3) { w[i], y[i] - .055f, z[i] };
        ring[i][6] = (Vector3) { w[i], y[i], z[i] };
    }
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 6; ++j)
            quad(m, ring[i][j + 1], ring[i + 1][j + 1], ring[i + 1][j], ring[i][j],
                j == 0 || j == 5 ? rubber : p.hull);
        Vector3 l0 = ring[i][0], r0 = ring[i][6];
        Vector3 l1 = ring[i + 1][0], r1 = ring[i + 1][6];
        if (i == 2 || i == 3) {
            // Leave a full cockpit well between the foredeck and aft deck.
            const float half = .72f;
            Vector3 inner_l0 = { -half, y[i], z[i] }, inner_r0 = { half, y[i], z[i] };
            Vector3 inner_l1 = { -half, y[i + 1], z[i + 1] };
            Vector3 inner_r1 = { half, y[i + 1], z[i + 1] };
            quad(m, l1, inner_l1, inner_l0, l0, p.deck);
            quad(m, inner_r1, r1, r0, inner_r0, p.deck);
        } else {
            // Broad foredeck and aft deck; the center accent narrows with the bow.
            Vector3 a = { -w[i] * .19f, y[i], z[i] }, b = { w[i] * .19f, y[i], z[i] };
            Vector3 c = { -w[i + 1] * .19f, y[i + 1], z[i + 1] };
            Vector3 d = { w[i + 1] * .19f, y[i + 1], z[i + 1] };
            quad(m, l1, c, a, l0, p.deck);
            quad(m, c, d, b, a, p.accent);
            quad(m, d, r1, r0, b, p.deck);
        }
    }
    for (int j = 1; j < 6; ++j) {
        triangle(m, ring[0][0], ring[0][j + 1], ring[0][j], p.hull);
        triangle(m, ring[5][0], ring[5][j], ring[5][j + 1], p.hull);
    }
    // Retain the accepted staff's position, height and aft rake exactly.
    float top = BOAT_FLAG_STAFF_BASE_Y + BOAT_FLAG_STAFF_HEIGHT;
    boat_model_beam(m, (Vector3) { .69f, BOAT_FLAG_STAFF_BASE_Y, 1.30f },
        (Vector3) { .69f, top, 1.30f + .2f * BOAT_FLAG_STAFF_HEIGHT }, .032f,
        (Color) { 210, 172, 112, 255 });
    boat_model_box(m, (Vector3) { .69f, .77f, 1.25f }, (Vector3) { .09f, .12f, .14f }, rubber);
}

static void cockpit(BoatModel *m, BoatSkinPalette p)
{
    // Low wraparound windscreen, swept aft. Open cockpit and two bucket-seat silhouettes.
    Vector3 a = { -.70f, .80f, -.66f }, b = { .70f, .80f, -.66f };
    Vector3 c = { .60f, 1.18f, -.24f }, d = { -.60f, 1.18f, -.24f };
    Vector3 l = { -.73f, .77f, .32f }, r = { .73f, .77f, .32f };
    pane(m, (Vector3[]) { a, d, c, b }, 4);
    pane(m, (Vector3[]) { a, l, d }, 3);
    pane(m, (Vector3[]) { b, c, r }, 3);
    boat_model_beam(m, d, c, .035f, p.deck);
    boat_model_beam(m, d, l, .035f, p.deck);
    boat_model_beam(m, c, r, .035f, p.deck);
    // The open cockpit pan sits well below the foredeck; the seats follow its floor.
    boat_model_box(m, (Vector3) { 0, .44f, .12f }, (Vector3) { 1.34f, .04f, 1.50f }, rubber);
    for (int side = -1; side <= 1; side += 2) {
        boat_model_box(
            m, (Vector3) { side * .34f, .51f, .51f }, (Vector3) { .43f, .12f, .45f }, p.upholstery);
        boat_model_box(m, (Vector3) { side * .34f, .68f, .75f }, (Vector3) { .43f, .37f, .13f },
            p.upholstery);
        boat_model_box(
            m, (Vector3) { side * .34f, .87f, .75f }, (Vector3) { .38f, .025f, .14f }, p.accent);
    }
    boat_model_box(m, (Vector3) { 0, .85f, 1.10f }, (Vector3) { 1.35f, .15f, .29f }, p.deck);
}

bool boat_skin_generate(BoatModel *m, BoatSkinPalette p, BoatSkinPart part)
{
    if (!m)
        return false;
    boat_model_begin(m);
    switch (part) {
    case BOAT_SKIN_HULL:
        hull(m, p);
        for (size_t i = 0; i < m->mesh.triangle_count * 3; ++i) {
            m->vertices[i].x /= 1.4f;
            m->vertices[i].y /= 1.4f;
            m->vertices[i].z /= 1.4f;
        }
        break;
    case BOAT_SKIN_COCKPIT:
        cockpit(m, p);
        for (size_t i = 0; i < m->mesh.triangle_count * 3; ++i)
            m->vertices[i].y = (m->vertices[i].y - 1.1f) / .7f;
        break;
    case BOAT_SKIN_ENGINE:
        boat_model_box(m, (Vector3) { 0, -.16f, 0 }, (Vector3) { .86f, .68f, .88f }, rubber);
        boat_model_box(m, (Vector3) { 0, .30f, 0 }, (Vector3) { 1, .24f, 1 }, p.hull);
        boat_model_box(m, (Vector3) { 0, .30f, .505f }, (Vector3) { .72f, .08f, .02f }, p.accent);
        break;
    default:
        return false;
    }
    return !m->overflow;
}

const Mesh3D *boat_skin_mesh(unsigned int skin, BoatSkinPart part)
{
    static BoatModel models[BOAT_SKIN_COUNT][3];
    skin = boat_skin_index(skin);
    if (part < BOAT_SKIN_HULL || part > BOAT_SKIN_ENGINE)
        return NULL;
    BoatModel *m = &models[skin][part];
    if (!m->mesh.triangle_count)
        boat_skin_generate(m, boat_skin_palette(skin), part);
    return &m->mesh;
}
