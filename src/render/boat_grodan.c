#include "boat_grodan.h"
#include "boat_flag.h"

// GH3 (docs/GOTHENBURG_HARBOUR_PLAN.md): the Grodan, a made-up stand-in for Gothenburg's open
// canal tour boats. 15 x 4.2 m and low, so it fits under the canal bridges; the dimensions are
// an estimate from the real boats' 73-92 seats, as no figures are published.
static const BoatModelSpec spec = { .length = 15,
    .beam = 4.2f,
    .draft = .6f,
    .freeboard = .8f,
    .bow_width = .45f,
    .stern_width = .92f,
    .fenders = true,
    .hull = { 58, 140, 72, 255 }, // Frog green, with a darker stripe and rear benches.
    .deck = { 173, 129, 80, 255 },
    .trim = { 30, 84, 52, 255 },
    .name = "GRODAN" };

static const Color seat = { 240, 196, 64, 255 }, frame = { 214, 220, 222, 255 };
static const Color glass = { 43, 85, 106, 255 }, console = { 58, 64, 70, 255 };
// Passengers' jackets and hats; the skipper wears the trim colour.
static const Color passengers[] = { { 196, 62, 52, 255 }, { 44, 92, 160, 255 },
    { 236, 232, 220, 255 }, { 40, 40, 46, 255 }, { 230, 160, 40, 255 }, { 120, 150, 90, 255 } };
static const Color skin_tone = { 224, 182, 150, 255 };

BoatModelSpec boat_grodan_spec(void) { return spec; }

static void cockpit(BoatModel *m)
{
    const float floor = spec.freeboard * .62f;
    // Benches across the open deck either side of a centre aisle, facing forward.
    for (int row = 0; row < 8; ++row) {
        float z = -4.2f + row * .95f;
        for (int side = -1; side <= 1; side += 2) {
            float x = side * 1.05f;
            boat_model_box(
                m, (Vector3) { x, floor + .24f, z }, (Vector3) { 1.5f, .1f, .42f }, seat);
            boat_model_box(
                m, (Vector3) { x, floor + .46f, z + .22f }, (Vector3) { 1.5f, .42f, .07f }, seat);
            // About half the seats are taken, the same ones every trip.
            for (int place = 0; place < 2; ++place) {
                if ((row * 5 + place * 3 + (side + 1)) % 4 == 1)
                    continue;
                float px = x + (place ? .38f : -.38f);
                Color jacket = passengers[(row * 2 + place + side + 6) % 6];
                boat_model_box(m, (Vector3) { px, floor + .58f, z + .02f },
                    (Vector3) { .44f, .5f, .3f }, jacket);
                boat_model_box(m, (Vector3) { px, floor + .95f, z }, (Vector3) { .22f, .24f, .22f },
                    skin_tone);
            }
        }
    }
    // A low raked windscreen across the bow and the guide's stand behind it.
    float front = -5.1f;
    boat_model_quad(m, (Vector3) { -1.7f, spec.freeboard, front },
        (Vector3) { 1.7f, spec.freeboard, front },
        (Vector3) { 1.55f, spec.freeboard + .45f, front + .3f },
        (Vector3) { -1.55f, spec.freeboard + .45f, front + .3f }, glass);
    boat_model_box(
        m, (Vector3) { 0, spec.freeboard + .02f, front }, (Vector3) { 3.5f, .05f, .06f }, frame);
    boat_model_box(m, (Vector3) { 0, floor + .55f, -4.75f }, (Vector3) { .5f, .9f, .4f }, console);
    // The helm aft, with its skipper.
    boat_model_box(m, (Vector3) { 0, floor + .45f, 4.5f }, (Vector3) { .9f, .9f, .5f }, console);
    boat_model_box(m, (Vector3) { 0, floor + .66f, 5.1f }, (Vector3) { .46f, .62f, .32f }, seat);
    boat_model_box(
        m, (Vector3) { 0, floor + 1.1f, 5.1f }, (Vector3) { .24f, .26f, .24f }, skin_tone);
    // A grab rail along each gunwale.
    for (int side = -1; side <= 1; side += 2)
        boat_model_box(m, (Vector3) { side * 1.95f, spec.freeboard + .28f, .2f },
            (Vector3) { .05f, .05f, 10.4f }, frame);
}

bool boat_grodan_generate(BoatModel *m, bool hull)
{
    if (hull) {
        if (!boat_model_generate(m, spec))
            return false;
        // The ensign staff at the stern, where boat_view moves the flag.
        Vector3 foot = boat_flag_attachment((Transform3D) { .scale = { 1, 1, 1 } }, 0);
        foot.z += BOAT_GRODAN_FLAG_AFT;
        boat_model_beam(m, (Vector3) { foot.x, spec.freeboard, foot.z }, foot, .05f, frame);
        boat_model_beam(m, foot,
            boat_flag_attachment((Transform3D) { .scale = { 1, 1, 1 } }, BOAT_FLAG_ROWS - 1), .05f,
            frame);
        return !m->overflow;
    }
    boat_model_begin(m);
    cockpit(m);
    return !m->overflow;
}
