#include "boat_skin.h"
#include "boat_flag.h"
#include "raymath.h"

#include <string.h>

static const char *names[] = { "sweden", "finland", "denmark", "norway" };
static const Color white_hull = { 239, 243, 245, 255 };
static const BoatSkinPalette palettes[]
    = { // Hull, deck, stripe/seat accent, upholstery. One shape and gameplay class for all four.
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
    // A fine entry opens gradually into the cockpit shoulders and tapered transom.
    const float z[] = { -2.31f, -1.92f, -1.35f, -.65f, .55f, .90f, 1.26f };
    const float w[] = { .025f, .26f, .57f, .86f, .96f, .90f, .82f };
    const float y[] = { .65f, .72f, .77f, .78f, .72f, .70f, .68f };
    const float chine[] = { .53f, .30f, .13f, .08f, .08f, .10f, .12f };
    const float keel[] = { .49f, .22f, -.12f, -.24f, -.24f, -.22f, -.18f };
    enum { STATIONS = sizeof(z) / sizeof(z[0]) };
    Vector3 ring[STATIONS][7];
    for (int i = 0; i < STATIONS; ++i) {
        ring[i][0] = (Vector3) { -w[i], y[i], z[i] };
        ring[i][1] = (Vector3) { -w[i], y[i] - .035f, z[i] };
        ring[i][2] = (Vector3) { -w[i] * .82f, chine[i], z[i] };
        ring[i][3] = (Vector3) { 0, keel[i], z[i] };
        ring[i][4] = (Vector3) { w[i] * .82f, chine[i], z[i] };
        ring[i][5] = (Vector3) { w[i], y[i] - .035f, z[i] };
        ring[i][6] = (Vector3) { w[i], y[i], z[i] };
    }
    for (int i = 0; i < STATIONS - 1; ++i) {
        for (int j = 0; j < 6; ++j)
            quad(m, ring[i][j + 1], ring[i + 1][j + 1], ring[i + 1][j], ring[i][j],
                j == 0 || j == 5 ? rubber : p.hull);
        Vector3 l0 = ring[i][0], r0 = ring[i][6];
        Vector3 l1 = ring[i + 1][0], r1 = ring[i + 1][6];
        if (i == 3 || i == 4) {
            // Leave a full cockpit well between the foredeck and aft deck.
            const float half = .72f;
            Vector3 inner_l0 = { -half, y[i], z[i] }, inner_r0 = { half, y[i], z[i] };
            Vector3 inner_l1 = { -half, y[i + 1], z[i + 1] };
            Vector3 inner_r1 = { half, y[i + 1], z[i + 1] };
            quad(m, l1, inner_l1, inner_l0, l0, p.deck);
            quad(m, inner_r1, r1, r0, inner_r0, p.deck);
        } else {
            // Broad foredeck and aft deck; the center accent narrows with the bow.
            float crown0 = i < 3 ? .035f * w[i] : 0;
            float crown1 = i < 3 ? .035f * w[i + 1] : 0;
            Vector3 a = { -w[i] * .19f, y[i] + crown0, z[i] };
            Vector3 b = { w[i] * .19f, y[i] + crown0, z[i] };
            Vector3 c = { -w[i + 1] * .19f, y[i + 1] + crown1, z[i + 1] };
            Vector3 d = { w[i + 1] * .19f, y[i + 1] + crown1, z[i + 1] };
            quad(m, l1, c, a, l0, p.deck);
            quad(m, c, d, b, a, p.accent);
            quad(m, d, r1, r0, b, p.deck);
        }
    }
    for (int j = 1; j < 6; ++j) {
        triangle(m, ring[0][0], ring[0][j + 1], ring[0][j], p.hull);
        triangle(
            m, ring[STATIONS - 1][0], ring[STATIONS - 1][j], ring[STATIONS - 1][j + 1], p.hull);
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
    Vector3 c = { .58f, 1.07f, -.10f }, d = { -.58f, 1.07f, -.10f };
    Vector3 l = { -.72f, .75f, .40f }, r = { .72f, .75f, .40f };
    pane(m, (Vector3[]) { a, d, c, b }, 4);
    pane(m, (Vector3[]) { a, l, d }, 3);
    pane(m, (Vector3[]) { b, c, r }, 3);
    boat_model_beam(m, d, c, .026f, p.deck);
    boat_model_beam(m, d, l, .026f, p.deck);
    boat_model_beam(m, c, r, .026f, p.deck);
    // The open cockpit pan sits well below the foredeck; the seats follow its floor.
    boat_model_box(m, (Vector3) { 0, .44f, .12f }, (Vector3) { 1.34f, .04f, 1.50f }, rubber);
    for (int side = -1; side <= 1; side += 2) {
        boat_model_box(
            m, (Vector3) { side * .34f, .51f, .51f }, (Vector3) { .43f, .12f, .45f }, p.upholstery);
        boat_model_box(
            m, (Vector3) { side * .34f, .68f, .75f }, (Vector3) { .43f, .37f, .13f }, p.upholstery);
        boat_model_box(
            m, (Vector3) { side * .34f, .87f, .75f }, (Vector3) { .38f, .025f, .14f }, p.accent);
    }
    // Low aft fairing follows the transom taper instead of ending in a tall block.
    Vector3 aft[] = { { -.69f, .73f, .96f }, { .69f, .73f, .96f }, { .60f, .68f, 1.25f },
        { -.60f, .68f, 1.25f }, { -.69f, .84f, .96f }, { .69f, .84f, .96f }, { .60f, .75f, 1.25f },
        { -.60f, .75f, 1.25f } };
    quad(m, aft[7], aft[6], aft[5], aft[4], p.deck);
    quad(m, aft[4], aft[5], aft[1], aft[0], p.deck);
    quad(m, aft[5], aft[6], aft[2], aft[1], p.deck);
    quad(m, aft[6], aft[7], aft[3], aft[2], p.deck);
    quad(m, aft[7], aft[4], aft[0], aft[3], p.deck);
    quad(m, aft[0], aft[1], aft[2], aft[3], p.deck);
}

static void engine(BoatModel *m, BoatSkinPalette p)
{
    boat_model_box(m, (Vector3) { 0, -.16f, 0 }, (Vector3) { .86f, .68f, .88f }, rubber);
    // A bevelled white cowl keeps the outboard compact in silhouette.
    const Vector2 outline[] = { { -.32f, -.5f }, { .32f, -.5f }, { .5f, -.32f }, { .5f, .32f },
        { .32f, .5f }, { -.32f, .5f }, { -.5f, .32f }, { -.5f, -.32f } };
    Vector3 lower[8], upper[8];
    for (int i = 0; i < 8; ++i) {
        lower[i] = (Vector3) { outline[i].x, .18f, outline[i].y };
        upper[i] = (Vector3) { outline[i].x * .85f, .42f, outline[i].y * .85f };
    }
    for (int i = 0; i < 8; ++i) {
        int j = (i + 1) % 8;
        quad(m, lower[i], upper[i], upper[j], lower[j], p.hull);
        triangle(m, (Vector3) { 0, .42f, 0 }, upper[j], upper[i], p.hull);
        triangle(m, (Vector3) { 0, .18f, 0 }, lower[i], lower[j], p.hull);
    }
    quad(m, (Vector3) { -.275f, .21f, .4926f }, (Vector3) { .275f, .21f, .4926f },
        (Vector3) { .275f, .27f, .4739f }, (Vector3) { -.275f, .27f, .4739f }, p.accent);
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
        engine(m, p);
        break;
    default:
        return false;
    }
    return !m->overflow;
}

Transform3D boat_skin_outboard_pose(Transform3D hull, Transform3D engine_center, int side)
{
    Vector3 offset
        = Vector3Transform((Vector3) { side * .36f, 0, 0 }, MatrixRotateXYZ(hull.rotation));
    engine_center.position = Vector3Add(engine_center.position, offset);
    return engine_center;
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
