#include "boat_remote_view.h"

#include "raymath.h"

#include <math.h>

void boat_remote_history_reset(BoatRemoteHistory *h) { *h = (BoatRemoteHistory) { 0 }; }

static const BoatRemoteSnapshot *entry(const BoatRemoteHistory *h, unsigned age)
{
    // Age 0 is the newest.
    return &h->entries[(h->next + BOAT_REMOTE_HISTORY - 1 - age) % BOAT_REMOTE_HISTORY];
}

void boat_remote_history_push(
    BoatRemoteHistory *h, uint64_t tick, const BoatFleetSlot slots[BOAT_FLEET_SLOTS])
{
    if (h->count && tick <= entry(h, 0)->tick)
        return;
    BoatRemoteSnapshot *e = &h->entries[h->next];
    e->tick = tick;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        e->slots[i] = slots[i];
    h->next = (h->next + 1) % BOAT_REMOTE_HISTORY;
    if (h->count < BOAT_REMOTE_HISTORY)
        ++h->count;
}

Boat boat_remote_blend(const Boat *a, const Boat *b, float t)
{
    Boat boat = t < .5f ? *a : *b;
    boat.position = Vector2Lerp(a->position, b->position, t);
    boat.velocity = Vector2Lerp(a->velocity, b->velocity, t);
    boat.heading = a->heading + remainderf(b->heading - a->heading, 2 * PI) * t;
#define BLEND(field) boat.field = a->field + (b->field - a->field) * t
    BLEND(height);
    BLEND(vertical_velocity);
    BLEND(pitch);
    BLEND(roll);
    BLEND(yaw_velocity);
    BLEND(wheel);
    BLEND(throttle);
    BLEND(engine_response);
    BLEND(engine_angle);
    BLEND(planing);
    BLEND(wave_spray);
#undef BLEND
    return boat;
}

static Boat extrapolate(const Boat *b, float seconds)
{
    Boat boat = *b;
    boat.position = Vector2Add(b->position, Vector2Scale(b->velocity, seconds));
    boat.heading += b->yaw_velocity * seconds;
    boat.height += b->vertical_velocity * seconds;
    boat.pitch += b->pitch_velocity * seconds;
    boat.roll += b->roll_velocity * seconds;
    return boat;
}

BoatRemoteSample boat_remote_history_pose(
    const BoatRemoteHistory *h, unsigned slot, double tick, Boat *pose, uint32_t *life)
{
    if (!h->count || !entry(h, 0)->slots[slot].active)
        return BOAT_REMOTE_NONE;
    const BoatRemoteSnapshot *newest = entry(h, 0);
    if (tick >= (double)newest->tick) {
        double ahead = tick - (double)newest->tick;
        bool held = ahead > BOAT_REMOTE_EXTRAPOLATE_TICKS;
        ahead = fmin(ahead, BOAT_REMOTE_EXTRAPOLATE_TICKS);
        *pose = extrapolate(&newest->slots[slot].boat, (float)(ahead / BOAT_TICK_HZ));
        *life = newest->slots[slot].life;
        return held     ? BOAT_REMOTE_HELD
            : ahead > 0 ? BOAT_REMOTE_EXTRAPOLATED
                        : BOAT_REMOTE_INTERPOLATED;
    }
    for (unsigned age = 1; age < h->count; ++age) {
        const BoatRemoteSnapshot *a = entry(h, age), *b = entry(h, age - 1);
        if ((double)a->tick > tick)
            continue;
        const BoatFleetSlot *from = &a->slots[slot], *to = &b->slots[slot];
        if (!to->active)
            to = &newest->slots[slot]; // Left and came back: show the current life.
        *life = to->life;
        if (!from->active || from->life != to->life
            || Vector2Distance(from->boat.position, to->boat.position) > BOAT_REMOTE_TELEPORT) {
            *pose = to->boat;
        } else {
            float t = (float)((tick - (double)a->tick) / (double)(b->tick - a->tick));
            *pose = boat_remote_blend(&from->boat, &to->boat, t);
        }
        return BOAT_REMOTE_INTERPOLATED;
    }
    // Older than the history: hold the oldest pose of the newest life.
    for (unsigned age = h->count; age-- > 0;) {
        const BoatFleetSlot *s = &entry(h, age)->slots[slot];
        if (s->active && s->life == newest->slots[slot].life) {
            *pose = s->boat;
            *life = s->life;
            return BOAT_REMOTE_HELD;
        }
    }
    return BOAT_REMOTE_NONE;
}

// Closest approach of the two hulls within BOAT_REMOTE_CONTACT_SECONDS at current velocities.
static bool contact_soon(const Boat *own, const Boat *remote)
{
    Vector2 p = Vector2Subtract(remote->position, own->position);
    Vector2 v = Vector2Subtract(remote->velocity, own->velocity);
    float closing = -Vector2DotProduct(p, v), speed = Vector2LengthSqr(v);
    if (closing <= 0 || speed <= 0)
        return false;
    float t = fminf(closing / speed, BOAT_REMOTE_CONTACT_SECONDS);
    return Vector2Length(Vector2Add(p, Vector2Scale(v, t))) < BOAT_REMOTE_CONTACT_DISTANCE;
}

float boat_remote_proximity_update(BoatRemoteProximity *x, const Boat *own, const Boat *remote,
    uint32_t life, double now, float dt)
{
    if (x->life != life)
        *x = (BoatRemoteProximity) { .life = life, .far_since = -1 };
    float distance = Vector2Distance(own->position, remote->position);
    if (distance < BOAT_REMOTE_NEAR || contact_soon(own, remote)) {
        x->near = true;
        x->far_since = -1;
    } else if (x->near && distance > BOAT_REMOTE_FAR) {
        if (x->far_since < 0)
            x->far_since = now;
        if (now - x->far_since >= BOAT_REMOTE_FAR_SECONDS)
            x->near = false;
    } else {
        x->far_since = -1;
    }
    float step = dt / BOAT_REMOTE_BLEND_SECONDS;
    x->weight = x->near ? fminf(1, x->weight + step) : fmaxf(0, x->weight - step);
    return x->weight;
}
