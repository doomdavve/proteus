#include "dance.h"
#include "choreography.h"

#include "raymath.h"

#include <math.h>

static float ease(float a, float b, float x)
{
    float t = Clamp((x - a) / (b - a), 0, 1);
    return t * t * (3 - 2 * t);
}

static Choreography choreography;

bool dance_load_choreography(const char *path, char *error, size_t error_size)
{
    if (path == NULL) {
        choreography = (Choreography) { 0 };
        return true;
    }
    return choreography_load(path, &choreography, error, error_size);
}

float dance_duration(void)
{
    return (choreography.count ? choreography.beats : 32) * 60.0f / 125.0f;
}

// Analytic two-bone IK. The pole sets the knee's bend plane, independent of the camera.
static Vector3 knee_position(Vector3 hip, Vector3 ankle)
{
    const float thigh = .52f, shin = .50f;
    Vector3 delta = Vector3Subtract(ankle, hip);
    float distance = Vector3Length(delta);
    Vector3 axis = Vector3Scale(delta, 1 / distance);
    Vector3 pole = { 0, 0, 1 };
    Vector3 bend = Vector3Normalize(
        Vector3Subtract(pole, Vector3Scale(axis, Vector3DotProduct(pole, axis))));
    float along = (thigh * thigh - shin * shin + distance * distance) / (2 * distance);
    float height = sqrtf(fmaxf(0, thigh * thigh - along * along));
    return Vector3Add(hip, Vector3Add(Vector3Scale(axis, along), Vector3Scale(bend, height)));
}

typedef struct {
    float beat;
    Vector3 pelvis;
    float lean, yaw, shoulder, sweep, reach;
} DanceKey;

// One eight-beat phrase. Positions are metres, rotations radians. Beats are zero-based:
// settle, prepare, cross, open, hit/hold, recover. The last key closes the phrase.
static const DanceKey PHRASE[] = {
    { 0, { .14f, .985f, 0 }, -.035f, .04f, 0, 0, 0 },
    { .65f, { .19f, .94f, 0 }, -.06f, -.08f, .06f, 0, 0 },
    { 1.35f, { .16f, 1.0f, 0 }, -.02f, .06f, 0, 0, 0 },
    { 2, { .20f, .96f, 0 }, -.04f, .18f, 0, 0, 0 },
    { 2.9f, { .23f, .94f, .04f }, -.05f, -.12f, 0, 1, 0 },
    { 3.75f, { .33f, .92f, .17f }, -.10f, -.35f, 0, 0, 1 },
    { 4, { .33f, .94f, .17f }, -.10f, -.35f, 0, 0, 1 },
    { 5.35f, { .33f, .94f, .17f }, -.10f, -.35f, 0, 0, 1 },
    { 5.85f, { .22f, .95f, .01f }, -.03f, 0, 0, 0, .35f },
    { 6.7f, { .14f, .96f, 0 }, -.04f, .08f, 0, 0, 0 },
    { 7.3f, { -.10f, .96f, 0 }, .02f, -.08f, 0, 0, 0 },
    { 8, { .14f, .985f, 0 }, -.035f, .04f, 0, 0, 0 },
};

static DanceKey phrase_at(float beat)
{
    beat = fmodf(beat + 8, 8);
    for (size_t i = 1; i < sizeof(PHRASE) / sizeof(PHRASE[0]); ++i) {
        if (beat > PHRASE[i].beat)
            continue;
        DanceKey a = PHRASE[i - 1], b = PHRASE[i];
        float t = ease(a.beat, b.beat, beat);
        return (DanceKey) {
            .pelvis = Vector3Lerp(a.pelvis, b.pelvis, t),
            .lean = Lerp(a.lean, b.lean, t),
            .yaw = Lerp(a.yaw, b.yaw, t),
            .shoulder = Lerp(a.shoulder, b.shoulder, t),
            .sweep = Lerp(a.sweep, b.sweep, t),
            .reach = Lerp(a.reach, b.reach, t),
        };
    }
    return PHRASE[0];
}

static Vector3 turn(Vector3 v, float yaw)
{
    return (Vector3) { v.x * cosf(yaw) + v.z * sinf(yaw), v.y, -v.x * sinf(yaw) + v.z * cosf(yaw) };
}

// Interpolate angles instead of normalizing a straight blend near opposing directions.
// These authored directions never reach the antipodal case.
static Vector3 blend_direction(Vector3 a, Vector3 b, float t)
{
    a = Vector3Normalize(a);
    b = Vector3Normalize(b);
    float angle = acosf(Clamp(Vector3DotProduct(a, b), -1, 1));
    if (angle < .001f)
        return Vector3Normalize(Vector3Lerp(a, b, t));
    return Vector3Scale(
        Vector3Add(Vector3Scale(a, sinf((1 - t) * angle)), Vector3Scale(b, sinf(t * angle))),
        1 / sinf(angle));
}

static Vector3 arm_direction(Vector3 rest, Vector3 sweep, Vector3 reach, float s, float r)
{
    float total = s + r;
    if (total < .00001f)
        return Vector3Normalize(rest);
    return blend_direction(rest, blend_direction(sweep, reach, r / total), total);
}

static Vector3 crossing_foot(float phase, bool *planted)
{
    Vector3 home = { -.26f, .075f, 0 }, crossed = { .40f, .075f, .30f };
    float lift_off = 2, landing = 2.9f;
    Vector3 from = home, to = crossed;
    if (phase < lift_off) {
        *planted = true;
        return home;
    }
    if (phase >= landing && phase <= 5.85f) {
        *planted = true;
        return crossed;
    }
    if (phase > 5.85f) {
        lift_off = 5.85f;
        landing = 6.7f;
        from = crossed;
        to = home;
    }
    float t = Clamp((phase - lift_off) / (landing - lift_off), 0, 1);
    *planted = t <= 0 || t >= 1;
    Vector3 foot = Vector3Lerp(from, to, ease(0, 1, t));
    // Zero vertical velocity at takeoff/landing. The forward arc clears the support leg.
    float arc = sinf(PI * t);
    arc *= arc;
    foot.y += .17f * arc;
    foot.z += .12f * arc;
    return foot;
}

static void classic_evaluate(float time, DancePose *pose)
{
    *pose = (DancePose) { 0 };
    float local = fmodf(fmaxf(0, time), dance_duration());
    float beat = local * 125.0f / 60.0f;
    float phase = fmodf(beat, 8);
    // Keep expression constant during the hold; change intensity during the recovery.
    float energy = ease(14, 16, beat) * (1 - ease(30, 32, beat));
    DanceKey body = phrase_at(phase), arms = phrase_at(phase - .16f);
    Vector3 pelvis = body.pelvis;
    Vector3 chest = Vector3Add(pelvis, (Vector3) { body.lean, .39f, .025f });
    pose->beat = beat;
    pose->energy = energy;
    pose->phrase = "CLASSIC";
    pose->action = phase < 2 ? "SETTLE" : phase < 4 ? "CROSS" : phase < 5.35f ? "HOLD" : "RETURN";
    pose->head_yaw = body.yaw - .65f * ease(4.35f, 5.1f, phase) * (1 - ease(5.35f, 6.4f, phase));
    pose->joints[DANCE_PELVIS] = pelvis;
    pose->joints[DANCE_WAIST] = Vector3Lerp(pelvis, chest, .18f / .39f);
    pose->joints[DANCE_CHEST] = chest;
    pose->joints[DANCE_NECK] = Vector3Add(chest, (Vector3) { body.lean * .3f, .20f, 0 });
    pose->joints[DANCE_HEAD]
        = Vector3Add(pose->joints[DANCE_NECK], turn((Vector3) { 0, .145f, .035f }, pose->head_yaw));
    for (int side = 0; side < 2; ++side) {
        float sign = side == 0 ? -1 : 1;
        int hip = side == 0 ? DANCE_LEFT_HIP : DANCE_RIGHT_HIP;
        Vector3 ankle = { .26f, .075f, 0 };
        bool planted = true;
        if (side == 0)
            ankle = crossing_foot(phase, &planted);
        pose->contact[side] = planted ? DANCE_PLANTED : DANCE_AIRBORNE;
        pose->joints[hip] = Vector3Add(pelvis, (Vector3) { sign * .13f, 0, 0 });
        pose->joints[hip + 1] = knee_position(pose->joints[hip], ankle);
        pose->joints[hip + 2] = ankle;
        pose->joints[hip + 3] = Vector3Add(ankle, (Vector3) { 0, -.025f, .16f });

        int shoulder = side == 0 ? DANCE_LEFT_SHOULDER : DANCE_RIGHT_SHOULDER;
        pose->joints[shoulder] = Vector3Add(
            chest, turn((Vector3) { sign * .22f, .12f - sign * body.shoulder, 0 }, body.yaw));
        Vector3 upper = { sign * .20f, -1, .12f };
        Vector3 lower = { sign * .18f, -.65f, .65f };
        float reach = arms.reach * (.75f + .25f * energy);
        if (side == 0) {
            upper = arm_direction(upper, (Vector3) { .65f, -.35f, .8f },
                (Vector3) { -1, .35f, .15f }, arms.sweep, reach);
            lower = arm_direction(lower, (Vector3) { .8f, .5f, .4f }, (Vector3) { -1, .15f, .25f },
                arms.sweep, reach);
        } else {
            upper = blend_direction(upper, (Vector3) { .35f, -.8f, .2f }, reach);
            lower = blend_direction(lower, (Vector3) { -.35f, .75f, .5f }, reach);
        }
        upper = turn(Vector3Scale(Vector3Normalize(upper), .32f), body.yaw);
        lower = turn(Vector3Scale(Vector3Normalize(lower), .29f), body.yaw);
        pose->joints[shoulder + 1] = Vector3Add(pose->joints[shoulder], upper);
        pose->joints[shoulder + 2] = Vector3Add(pose->joints[shoulder + 1], lower);
    }
}

static float swing_arc(float start, float end, float beat)
{
    float t = Clamp((beat - start) / (end - start), 0, 1);
    float arc = sinf(PI * t);
    return arc * arc;
}

// Zero acceleration at each end avoids a visible kick when a weight transfer starts/stops.
static float weight_ease(float start, float end, float beat)
{
    float t = Clamp((beat - start) / (end - start), 0, 1);
    return t * t * t * (t * (t * 6 - 15) + 10);
}

static float house_root_x(ChoreographyMove move, float p)
{
    if (move.flow || move.type == CHOREOGRAPHY_RECOVER)
        return Lerp(move.from.root_x, move.to.root_x, weight_ease(0, 2, p));
    float sign = move.lead == 0 ? -1 : 1;
    if (move.type == CHOREOGRAPHY_SHUFFLE)
        return -sign * .14f
            * (weight_ease(0, .4f, p) - 2 * weight_ease(.6f, 1.35f, p) + weight_ease(1.6f, 2, p));
    return sign
        * (-.14f * weight_ease(0, .4f, p) + .28f * weight_ease(.55f, 1.1f, p)
            - .28f * weight_ease(.95f, 1.65f, p) + .14f * weight_ease(1.6f, 2, p));
}

static int house_move_index(float *beat)
{
    *beat = fmodf(fmodf(*beat, choreography.beats) + choreography.beats, choreography.beats);
    for (int i = 0; i < choreography.count - 1; ++i) {
        if (*beat < choreography.moves[i].start + choreography.moves[i].duration)
            return i;
    }
    return choreography.count - 1;
}

// Sample the authored weight shift at an earlier beat, including across moves and the loop.
// This gives follow-through without integrating a spring or requiring seek history.
static float house_weight(float beat)
{
    int index = house_move_index(&beat);
    ChoreographyMove move = choreography.moves[index];
    return house_root_x(move, 2 * (beat - move.start) / move.duration);
}

static Vector3 body_vector(Vector3 v, float pitch, float yaw, float roll)
{
    Vector3 pitched
        = { v.x, v.y * cosf(pitch) - v.z * sinf(pitch), v.y * sinf(pitch) + v.z * cosf(pitch) };
    Vector3 rolled = { pitched.x * cosf(roll) - pitched.y * sinf(roll),
        pitched.x * sinf(roll) + pitched.y * cosf(roll), pitched.z };
    return turn(rolled, yaw);
}

static float gesture_phase(float p, float delay) { return Clamp((p - delay) / (2 - delay), 0, 1); }

static float gesture_envelope(float q) { return ease(.06f, .42f, q) * (1 - ease(.65f, .98f, q)); }

static Vector3 counter_direction(ChoreographyArms arms, int side, float beat, bool forearm)
{
    float s = side == 0 ? -1 : 1;
    float delayed = beat - arms.delay - (forearm ? .12f : 0);
    float drive = -s * (arms.lead == 0 ? 1 : -1) * arms.amount
        * (.75f * house_weight(delayed) / .24f + .2f * sinf(PI * delayed));
    return forearm ? (Vector3) { s * .18f, -.45f + .60f * drive, .75f + .18f * drive }
                   : (Vector3) { s * (.24f + .08f * fabsf(drive)), -1, .60f * drive };
}

static Vector3 house_arm_direction(ChoreographyArms arms, ChoreographyArms previous, int side,
    float beat, float elapsed, float arm_p, bool forearm)
{
    float s = side == 0 ? -1 : 1;
    Vector3 direction = blend_direction(counter_direction(previous, side, beat, forearm),
        counter_direction(arms, side, beat, forearm), ease(0, .3f, elapsed));
    float q = gesture_phase(arm_p, (arms.delay + (forearm ? .12f : 0)) * 2 / arms.span);
    float amount = arms.amount * gesture_envelope(q);
    bool leading = side == arms.lead;
    if (arms.type == CHOREOGRAPHY_SWEEP) {
        // Open one arm while the other folds toward the ribs. Hand arrives after elbow.
        Vector3 target = leading
            ? (forearm ? (Vector3) { s * 1.0f, .35f, .20f } : (Vector3) { s * .95f, .20f, .35f })
            : (forearm ? (Vector3) { -s * .12f, .35f, .90f } : (Vector3) { s * .40f, -.8f, .25f });
        direction = blend_direction(direction, target, amount);
    } else if (arms.type == CHOREOGRAPHY_PULL) {
        // Reach forward in preparation, then bend the elbow back and let the hand settle.
        float prepare = arms.amount * ease(.02f, .22f, q) * (1 - ease(.3f, .55f, q));
        Vector3 reach
            = forearm ? (Vector3) { s * .10f, .05f, 1 } : (Vector3) { s * .35f, -.45f, .8f };
        direction = blend_direction(direction, reach, prepare * (leading ? 1 : .35f));
        float pull = arms.amount * ease(.27f, .50f, q) * (1 - ease(.7f, .98f, q));
        Vector3 target
            = forearm ? (Vector3) { -s * .35f, .8f, .45f } : (Vector3) { s * .6f, -.75f, -.2f };
        direction = blend_direction(direction, target, pull * (leading ? 1 : .35f));
    }
    return direction;
}

static void house_evaluate(float time, DancePose *pose)
{
    *pose = (DancePose) { 0 };
    float beat = fmodf(fmaxf(0, time), dance_duration()) * 125 / 60;
    int index = house_move_index(&beat);
    ChoreographyMove move = choreography.moves[index];
    ChoreographyMove previous
        = choreography.moves[(index + choreography.count - 1) % choreography.count];
    float elapsed = Clamp(beat - move.start, 0, move.duration);
    float p = 2 * elapsed / move.duration;
    float arm_p = 2 * fmodf(beat - move.arm_start, move.arms.span) / move.arms.span;
    float groove = Lerp(previous.groove, move.groove, ease(0, .25f, elapsed));
    int lead = move.lead, support = 1 - lead;
    float sign = lead == 0 ? -1 : 1;
    Vector3 feet[2] = { { -.22f, .075f, 0 }, { .22f, .075f, 0 } };
    float root_x = house_root_x(move, p);
    if (move.flow || move.type == CHOREOGRAPHY_RECOVER) {
        bool recover = move.type == CHOREOGRAPHY_RECOVER;
        for (int side = 0; side < 2; ++side) {
            float begin = recover && side == support ? 1.1f : .15f;
            float end = recover ? (side == lead ? 1.0f : 1.9f) : 1.7f;
            float blend = weight_ease(begin, end, p);
            feet[side].x = Lerp(move.from.x[side], move.to.x[side], blend);
            feet[side].z = Lerp(move.from.z[side], move.to.z[side], blend);
            if (side == lead)
                feet[side].y += (recover ? .06f : move.lift) * swing_arc(begin, end, p);
            bool moving = fabsf(move.to.x[side] - move.from.x[side]) > .00001f
                || fabsf(move.to.z[side] - move.from.z[side]) > .00001f;
            if (p > begin && p < end)
                pose->contact[side] = side == lead ? DANCE_AIRBORNE
                    : moving                       ? DANCE_SLIDING
                                                   : DANCE_PLANTED;
        }
        pose->action = recover                  ? "RECOVER"
            : move.type == CHOREOGRAPHY_SHUFFLE ? (lead == 0 ? "FLOW L" : "FLOW R")
                                                : (lead == 0 ? "SIDE FLOW L" : "SIDE FLOW R");
    } else if (move.type == CHOREOGRAPHY_SHUFFLE) {
        // A simplified running shuffle: lift/place the lead foot as the other slides back,
        // then change support and recover. Sliding is intentional contact, not foot drift.
        feet[lead].z = move.travel * (ease(.25f, .85f, p) - ease(1.2f, 1.85f, p));
        feet[lead].y += move.lift * swing_arc(.25f, .85f, p);
        feet[support].z = -.6f * move.travel * (ease(.45f, .85f, p) - ease(1.2f, 1.85f, p));
        feet[support].y += move.lift * swing_arc(1.2f, 1.85f, p);
        if (p > .25f && p < .85f)
            pose->contact[lead] = DANCE_AIRBORNE;
        else if (p > 1.2f && p < 1.85f)
            pose->contact[lead] = DANCE_SLIDING;
        if (p > .45f && p < .85f)
            pose->contact[support] = DANCE_SLIDING;
        else if (p > 1.2f && p < 1.85f)
            pose->contact[support] = DANCE_AIRBORNE;
        pose->action = lead == 0 ? "SHUFFLE L" : "SHUFFLE R";
    } else {
        feet[lead].x += sign * move.travel * (ease(.25f, .85f, p) - ease(1.20f, 1.85f, p));
        feet[lead].y += move.lift * (swing_arc(.25f, .85f, p) + swing_arc(1.20f, 1.85f, p));
        if ((p > .25f && p < .85f) || (p > 1.20f && p < 1.85f))
            pose->contact[lead] = DANCE_AIRBORNE;
        pose->action = lead == 0 ? "SIDE L" : "SIDE R";
    }
    // The jack continues through every move. Chest motion trails pelvis compression.
    float pulse = .5f - .5f * cosf(2 * PI * beat);
    Vector3 pelvis
        = { root_x, .98f - (.012f + .025f * groove) * pulse, .012f * groove * sinf(2 * PI * beat) };
    float body_gesture = move.arms.amount * gesture_envelope(gesture_phase(arm_p, 0));
    float arm_sign = move.arms.lead == 0 ? -1 : 1;
    float opening = move.arms.type == CHOREOGRAPHY_SWEEP ? body_gesture : 0;
    float pulling = move.arms.type == CHOREOGRAPHY_PULL ? body_gesture : 0;
    float pitch = .065f * groove * sinf(2 * PI * (beat - .08f)) - .04f * pulling;
    float roll = .09f * house_weight(beat - .08f) / .24f - arm_sign * .035f * opening;
    float yaw = -.35f * house_weight(beat - .12f) + .055f * groove * sinf(PI * (beat - .12f))
        + arm_sign * (.16f * opening - .06f * pulling);
    // A slow two-beat body roll passes through the waist into the ribcage. Counter-bending
    // keeps the head relatively settled instead of amplifying the pelvis bounce.
    float lower_pitch = pitch * .4f + .22f * groove * sinf(PI * (beat - .03f));
    float rib_pitch = pitch * .5f - .23f * groove * sinf(PI * (beat - .18f));
    float lower_roll = roll + .12f * groove * sinf(PI * (beat - .05f));
    float rib_roll = roll - .16f * groove * sinf(PI * (beat - .20f));
    float rib_yaw = yaw + .15f * groove * sinf(PI * (beat - .20f));
    Vector3 waist = Vector3Add(
        pelvis, body_vector((Vector3) { 0, .18f, 0 }, lower_pitch, yaw * .25f, lower_roll));
    Vector3 chest
        = Vector3Add(waist, body_vector((Vector3) { 0, .21f, 0 }, rib_pitch, rib_yaw, rib_roll));
    float shoulder_roll = rib_roll - .02f * groove * sinf(PI * (beat - .3f));
    pose->beat = beat;
    pose->energy = groove;
    pose->phrase = choreography.moves[index].phrase;
    pose->head_yaw = rib_yaw * .5f;
    pose->joints[DANCE_PELVIS] = pelvis;
    pose->joints[DANCE_WAIST] = waist;
    pose->joints[DANCE_CHEST] = chest;
    pose->joints[DANCE_NECK] = Vector3Add(
        chest, body_vector((Vector3) { 0, .20f, 0 }, rib_pitch, rib_yaw, shoulder_roll));
    pose->joints[DANCE_HEAD] = Vector3Add(pose->joints[DANCE_NECK],
        body_vector(
            (Vector3) { 0, .145f, .03f }, rib_pitch * .25f, rib_yaw * .5f, shoulder_roll * .25f));
    for (int side = 0; side < 2; ++side) {
        float s = side == 0 ? -1 : 1;
        int hip = side == 0 ? DANCE_LEFT_HIP : DANCE_RIGHT_HIP;
        pose->joints[hip] = Vector3Add(pelvis, (Vector3) { s * .13f, 0, 0 });
        pose->joints[hip + 1] = knee_position(pose->joints[hip], feet[side]);
        pose->joints[hip + 2] = feet[side];
        pose->joints[hip + 3] = Vector3Add(feet[side], (Vector3) { 0, -.025f, .16f });
        int shoulder = side == 0 ? DANCE_LEFT_SHOULDER : DANCE_RIGHT_SHOULDER;
        pose->joints[shoulder] = Vector3Add(
            chest, body_vector((Vector3) { s * .22f, .12f, 0 }, rib_pitch, rib_yaw, shoulder_roll));
        Vector3 upper
            = house_arm_direction(move.arms, previous.arms, side, beat, elapsed, arm_p, false);
        Vector3 lower
            = house_arm_direction(move.arms, previous.arms, side, beat, elapsed, arm_p, true);
        upper = body_vector(
            Vector3Scale(Vector3Normalize(upper), .32f), rib_pitch, rib_yaw, shoulder_roll);
        lower = body_vector(
            Vector3Scale(Vector3Normalize(lower), .29f), rib_pitch, rib_yaw, shoulder_roll);
        pose->joints[shoulder + 1] = Vector3Add(pose->joints[shoulder], upper);
        pose->joints[shoulder + 2] = Vector3Add(pose->joints[shoulder + 1], lower);
    }
}

void dance_evaluate(float time, DancePose *pose)
{
    if (choreography.count)
        house_evaluate(time, pose);
    else
        classic_evaluate(time, pose);
}

// A tiny octagonal stick in world space, shared by every bone. No skinned mesh is needed yet.
static void stick(Renderer3D *renderer, Vector3 a, Vector3 b, float radius, Color color)
{
    Vector3 axis = Vector3Normalize(Vector3Subtract(b, a));
    Vector3 reference = fabsf(axis.y) > .9f ? (Vector3) { 1, 0, 0 } : (Vector3) { 0, 1, 0 };
    Vector3 u = Vector3Normalize(Vector3CrossProduct(axis, reference));
    Vector3 v = Vector3CrossProduct(axis, u);
    Vector3 vertices[16];
    Triangle3D triangles[28];
    int count = 0;
    for (int i = 0; i < 8; ++i) {
        float angle = 2 * PI * i / 8;
        Vector3 offset = Vector3Add(
            Vector3Scale(u, radius * cosf(angle)), Vector3Scale(v, radius * sinf(angle)));
        vertices[i] = Vector3Add(a, offset);
        vertices[i + 8] = Vector3Add(b, offset);
        unsigned int n = (unsigned int)(i + 1) % 8, k = (unsigned int)i;
        triangles[count++] = (Triangle3D) { { k, n, k + 8 }, color };
        triangles[count++] = (Triangle3D) { { n, n + 8, k + 8 }, color };
    }
    for (unsigned int i = 1; i < 7; ++i) {
        triangles[count++] = (Triangle3D) { { 0, i + 1, i }, color };
        triangles[count++] = (Triangle3D) { { 8, 8 + i, 9 + i }, color };
    }
    Mesh3D mesh = { vertices, triangles, (size_t)count };
    renderer3d_draw_mesh(renderer, &mesh, (Transform3D) { .scale = { 1, 1, 1 } });
}

bool dance_draw(
    Renderer3D *renderer, Color *pixels, int width, int height, float time, bool side_view)
{
    for (int i = 0; i < width * height; ++i)
        pixels[i] = (Color) { 17, 23, 34, 255 };
    View3D camera = {
        .position = side_view ? (Vector3) { 4.0f, 1.9f, .15f } : (Vector3) { 2.6f, 2.0f, 4.7f },
        .target = { 0, .95f, 0 },
        .up = { 0, 1, 0 },
        .vertical_fov = .57f,
        .near_plane = .1f,
        .far_plane = 20,
    };
    if (!renderer3d_begin(renderer, pixels, width, height, camera, (Vector3) { -1, 2, 3 }))
        return false;
    renderer3d_draw_box(renderer,
        (Transform3D) { .position = { 0, -.07f, 0 }, .scale = { 3.5f, .1f, 2.6f } },
        (Color) { 50, 63, 76, 255 });
    for (int i = -3; i <= 3; ++i) {
        float x = i * .5f;
        stick(renderer, (Vector3) { x, -.015f, -1.25f }, (Vector3) { x, -.015f, 1.25f }, .004f,
            (Color) { 83, 98, 111, 255 });
    }
    DancePose pose;
    dance_evaluate(time, &pose);
    Color body = { 234, 226, 206, 255 };
    for (int side = 0; side < 2; ++side) {
        Color limb = side == 0 ? (Color) { 100, 205, 215, 255 } : (Color) { 243, 171, 109, 255 };
        int hip = side == 0 ? DANCE_LEFT_HIP : DANCE_RIGHT_HIP;
        int shoulder = side == 0 ? DANCE_LEFT_SHOULDER : DANCE_RIGHT_SHOULDER;
        Vector3 foot = pose.joints[hip + 2];
        renderer3d_draw_box(renderer,
            (Transform3D) {
                .position = { foot.x, -.008f, foot.z + .06f }, .scale = { .18f, .016f, .30f } },
            pose.contact[side] == DANCE_PLANTED       ? (Color) { 98, 124, 131, 255 }
                : pose.contact[side] == DANCE_SLIDING ? (Color) { 220, 141, 66, 255 }
                                                      : (Color) { 38, 47, 60, 255 });
        stick(renderer, pose.joints[DANCE_PELVIS], pose.joints[hip], .035f, body);
        for (int j = 0; j < 3; ++j)
            stick(renderer, pose.joints[hip + j], pose.joints[hip + j + 1], j == 2 ? .047f : .035f,
                limb);
        stick(renderer, pose.joints[DANCE_CHEST], pose.joints[shoulder], .035f, body);
        for (int j = 0; j < 2; ++j)
            stick(renderer, pose.joints[shoulder + j], pose.joints[shoulder + j + 1], .028f, limb);
    }
    stick(renderer, pose.joints[DANCE_PELVIS], pose.joints[DANCE_WAIST], .045f, body);
    stick(renderer, pose.joints[DANCE_WAIST], pose.joints[DANCE_CHEST], .042f, body);
    stick(renderer, pose.joints[DANCE_CHEST], pose.joints[DANCE_NECK], .035f, body);
    Vector3 head = pose.joints[DANCE_HEAD];
    stick(renderer, pose.joints[DANCE_NECK], head, .035f, body);
    stick(renderer, Vector3Add(head, (Vector3) { 0, -.06f, 0 }),
        Vector3Add(head, (Vector3) { 0, .10f, 0 }), .095f, body);
    // A small nose makes the facing direction readable in the side view.
    stick(renderer, head, Vector3Add(head, turn((Vector3) { 0, 0, .13f }, pose.head_yaw)), .028f,
        body);
    return true;
}
