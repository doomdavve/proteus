#include "boat_cargo.h"
#include "raymath.h"

#include <math.h>

static const Vector3 crane_base = { -4, 5.5f, 3 };

void boat_cargo_reset(BoatCargoSandbox *s)
{
    *s = (BoatCargoSandbox) { .cargo = { .state = BOAT_CARGO_PAD, .position = { -5, 1.4f, 0 } },
        .crane = { .angle = -2.819842f, .reach = 3.162278f, .rope = 3.2f },
        .moored = true };
}

Vector3 boat_cargo_tip(const BoatCargoCrane *c)
{
    return Vector3Add(
        crane_base, (Vector3) { sinf(c->angle) * c->reach, 0, cosf(c->angle) * c->reach });
}

Vector3 boat_cargo_hook(const BoatCargoSandbox *s)
{
    if (s->cargo.state == BOAT_CARGO_HANGING)
        return Vector3Add(s->cargo.position, (Vector3) { 0, BOAT_CARGO_HALF, 0 });
    return Vector3Subtract(boat_cargo_tip(&s->crane), (Vector3) { 0, s->crane.rope, 0 });
}

void boat_cargo_motor(BoatCargoCrane *c, float slew, float reach, float hoist)
{
    c->angle = remainderf(c->angle + Clamp(slew, -1, 1) * .55f * BOAT_STEP, 2 * PI);
    c->reach = Clamp(c->reach + Clamp(reach, -1, 1) * 1.5f * BOAT_STEP, 2, 7);
    c->rope = Clamp(c->rope - Clamp(hoist, -1, 1) * 1.3f * BOAT_STEP, .7f, 5.2f);
}

bool boat_cargo_hook_toggle(BoatCargoSandbox *s)
{
    BoatCargo *c = &s->cargo;
    if (c->state == BOAT_CARGO_HANGING) {
        c->state = c->deck_contact ? BOAT_CARGO_LOOSE : BOAT_CARGO_FLOATING;
        c->settled = 0;
        return true;
    }
    if (!s->moored || c->state == BOAT_CARGO_SECURED)
        return false;
    Vector3 top = Vector3Add(c->position, (Vector3) { 0, BOAT_CARGO_HALF, 0 });
    float distance = Vector3Distance(top, boat_cargo_hook(s));
    if (distance > 1.25f)
        return false;
    // Preserve position/velocity; no teleport or initial constraint impulse.
    s->crane.rope = Clamp(Vector3Distance(boat_cargo_tip(&s->crane), top), .7f, 5.2f);
    c->state = BOAT_CARGO_HANGING;
    c->settled = 0;
    return true;
}

bool boat_cargo_lash_toggle(BoatCargoSandbox *s, const Boat *boat)
{
    BoatCargo *c = &s->cargo;
    if (Vector2Length(boat->velocity) > .5f || boat_pose_up(boat) < .9f)
        return false;
    if (c->state == BOAT_CARGO_SECURED) {
        c->velocity = boat_pose_motion(boat, c->deck_local).velocity;
        c->state = BOAT_CARGO_LOOSE;
        c->settled = 0;
        return true;
    }
    if (c->state != BOAT_CARGO_LOOSE || !c->deck_contact || c->settled < .5f)
        return false;
    c->state = BOAT_CARGO_SECURED;
    return true;
}

bool boat_cargo_moor_toggle(BoatCargoSandbox *s, const Boat *boat)
{
    if (s->moored) {
        if (s->cargo.state == BOAT_CARGO_HANGING || s->cargo.state == BOAT_CARGO_LOOSE)
            return false;
        s->moored = false;
        return true;
    }
    if (Vector2Length(boat->position) > .6f || Vector2Length(boat->velocity) > .5f
        || fabsf(remainderf(boat->heading, 2 * PI)) > .15f)
        return false;
    s->moored = true;
    return true;
}

static bool deck_inside(Vector3 p)
{
    return fabsf(p.x) <= BOAT_CARGO_DECK_X - BOAT_CARGO_HALF
        && fabsf(p.z) <= BOAT_CARGO_DECK_Z - BOAT_CARGO_HALF;
}

static void contact(BoatCargo *c, const Boat *boat)
{
    Matrix inverse = MatrixTranspose(boat_pose_rotation(boat));
    Vector3 centre = boat_pose_attachment(boat, (Vector3) { 0 });
    Vector3 local = Vector3Transform(Vector3Subtract(c->position, centre), inverse);
    float level = BOAT_CARGO_DECK_Y + BOAT_CARGO_HALF;
    c->deck_contact = false;
    if (boat_pose_up(boat) < .75f || !deck_inside(local) || local.y > level + .03f
        || local.y < level - .35f)
        return;
    Vector3 point_velocity = boat_pose_motion(boat, local).velocity;
    Vector3 relative = Vector3Transform(Vector3Subtract(c->velocity, point_velocity), inverse);
    if (relative.y > .6f)
        return;
    local.y = level;
    c->position = boat_pose_attachment(boat, local);
    // Bounded sliding friction in the support frame, retaining deck-point velocity.
    relative.x *= .93f;
    relative.z *= .93f;
    relative.y = fmaxf(0, -relative.y * .08f);
    c->velocity = Vector3Add(point_velocity, Vector3Transform(relative, boat_pose_rotation(boat)));
    c->deck_local = local;
    c->deck_contact = true;
    c->rotation = boat_pose_hull(boat).rotation;
    c->settled = Vector3Length(relative) < .18f ? c->settled + BOAT_STEP : 0;
}

void boat_cargo_step(BoatCargoSandbox *s, const Boat *boat, const BoatWorld *world, double now)
{
    BoatCargo *c = &s->cargo;
    if (c->state == BOAT_CARGO_SECURED) {
        c->position = boat_pose_attachment(boat, c->deck_local);
        c->velocity = boat_pose_motion(boat, c->deck_local).velocity;
        c->rotation = boat_pose_hull(boat).rotation;
        // An inverted boat cannot keep its cargo; recovery always remains available.
        if (boat_pose_up(boat) < .5f) {
            c->state = BOAT_CARGO_FLOATING;
            c->settled = 0;
            c->deck_contact = false;
        }
        return;
    }
    Vector3 old = c->position;
    c->velocity.y -= 9.81f * BOAT_STEP;
    c->velocity = Vector3Scale(c->velocity, .999f);
    c->position = Vector3Add(old, Vector3Scale(c->velocity, BOAT_STEP));
    if (c->state == BOAT_CARGO_HANGING) {
        Vector3 anchor
            = Vector3Subtract(boat_cargo_tip(&s->crane), (Vector3) { 0, BOAT_CARGO_HALF, 0 });
        // Unilateral cable: a supported load may slacken the rope. Four fixed projections.
        for (int iteration = 0; iteration < 4; ++iteration) {
            Vector3 delta = Vector3Subtract(c->position, anchor);
            float length = Vector3Length(delta);
            if (length > s->crane.rope)
                c->position = Vector3Add(anchor, Vector3Scale(delta, s->crane.rope / length));
        }
        c->velocity = Vector3Scale(Vector3Subtract(c->position, old), 1 / BOAT_STEP);
        // Damping keeps sway readable without an endless precision wait.
        c->velocity.x *= .997f;
        c->velocity.z *= .997f;
    }
    contact(c, boat);
    if (!c->deck_contact) {
        c->settled = 0;
        c->rotation = (Vector3) { 0 };
    }
    bool pad = fabsf(c->position.x + 5) < 1.1f && fabsf(c->position.z) < 1.5f;
    float floor = pad
        ? 1.4f
        : boat_water_query_borrowed(&world->water, (Vector2) { -c->position.x, c->position.z }, now)
                .height
            + .12f;
    if (!c->deck_contact && c->position.y < floor) {
        c->position.y = floor;
        c->velocity.y = fmaxf(0, -c->velocity.y * .12f);
        c->velocity.x *= .96f;
        c->velocity.z *= .96f;
    }
    if (c->state != BOAT_CARGO_HANGING) {
        c->state = c->deck_contact ? BOAT_CARGO_LOOSE : pad ? BOAT_CARGO_PAD : BOAT_CARGO_FLOATING;
    }
    // A single bounded body; falling/suspended simulation never depends on the camera.
    c->velocity = Vector3ClampValue(c->velocity, 0, 12);
}

void boat_cargo_load_effect(const BoatCargoSandbox *s, const Boat *before, Boat *boat)
{
    if (s->cargo.state != BOAT_CARGO_SECURED)
        return;
    // Added mass reduces changes of speed (including braking), preserving empty physics.
    boat->velocity = Vector2Lerp(before->velocity, boat->velocity, .68f);
    float trim = Clamp(s->cargo.deck_local.z * .035f, -.055f, .055f);
    float heel = Clamp(-s->cargo.deck_local.x * .09f, -.075f, .075f);
    // Approximate the existing six supports' angular stiffness, giving a readable 3–4 degree
    // equilibrium at the deck edges. This is authored balance, not a wider buoyancy model.
    boat->pitch_velocity += (trim * 45 - boat->pitch_velocity * .6f) * BOAT_STEP;
    boat->roll_velocity += (heel * 60 - boat->roll_velocity * .6f) * BOAT_STEP;
}
