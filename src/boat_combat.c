#include "boat_combat.h"
#include "boat_fleet.h"
#include "raymath.h"
#include "waterfront_harbor.h"
#include "waterfront_traffic.h"

#include <assert.h>
#include <math.h>
#include <string.h>

BoatCombatRules boat_combat_default_rules(void)
{
    return (BoatCombatRules) { 100, 8, 100, 100, 24, 2, 10, 30, 15, 240, 360, 960, 78, 180, 240 };
}

void boat_combat_bind(BoatFleet *f)
{
    f->world.combat = f->state.combat.enabled ? &f->state.combat : NULL;
    f->world.water.radial = f->state.combat.enabled ? f->state.combat.waves : NULL;
}

void boat_combat_enable(BoatFleet *f)
{
    f->state.combat
        = (BoatCombat) { .enabled = true, .firing = true, .rules = boat_combat_default_rules() };
    for (unsigned i = 0; i < BOAT_COMBAT_PLAYERS; ++i)
        boat_combat_spawn(f, i);
    boat_combat_bind(f);
}

void boat_combat_clear(BoatFleet *f)
{
    BoatCombat *c = &f->state.combat;
    for (unsigned i = 0; i < BOAT_COMBAT_BULLETS; ++i)
        c->bullets[i].active = false;
    for (unsigned i = 0; i < BOAT_COMBAT_GRENADES; ++i)
        c->grenades[i].active = false;
    for (unsigned i = 0; i < BOAT_COMBAT_WAVES; ++i)
        c->waves[i].active = false;
}

void boat_combat_spawn(BoatFleet *f, unsigned slot)
{
    BoatCombat *c = &f->state.combat;
    if (!c->enabled)
        return;
    uint32_t generation = c->players[slot].generation;
    c->players[slot] = (BoatCombatPlayer) { .health = c->rules.health,
        .life_start = f->state.tick,
        .generation = generation,
        .killer = UINT32_MAX };
}

void boat_combat_leave(BoatFleet *f, unsigned slot)
{
    BoatCombat *c = &f->state.combat;
    if (!c->enabled)
        return;
    ++c->players[slot].generation;
    for (unsigned i = 0; i < BOAT_COMBAT_BULLETS; ++i)
        if (c->bullets[i].owner == slot)
            c->bullets[i].active = false;
    for (unsigned i = 0; i < BOAT_COMBAT_GRENADES; ++i)
        if (c->grenades[i].owner == slot)
            c->grenades[i].active = false;
    for (unsigned i = 0; i < BOAT_COMBAT_WAVES; ++i)
        if (c->waves[i].owner == slot)
            c->waves[i].active = false;
}

static BoatCombatObject *available(BoatCombatObject *objects, unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        if (!objects[i].active)
            return &objects[i];
    return NULL;
}

void boat_combat_advance(BoatFleet *f, const BoatInput inputs[BOAT_COMBAT_PLAYERS])
{
    BoatCombat *c = &f->state.combat;
    if (!c->enabled)
        return;
    uint64_t tick = f->state.tick;
    const BoatCombatRules *r = &c->rules;
    for (unsigned i = 0; i < BOAT_COMBAT_WAVES; ++i)
        if (c->waves[i].active
            && (tick - c->waves[i].born) * BOAT_STEP * r->wave_speed >= r->wave_range)
            c->waves[i].active = false;
    // Reserve a ring for every accepted grenade. Four simultaneous throws always detonate.
    for (unsigned i = 0; i < BOAT_COMBAT_GRENADES; ++i) {
        BoatCombatObject *g = &c->grenades[i];
        if (!g->active || tick - g->born < r->fuse_ticks)
            continue;
        c->waves[i] = (BoatRadialWave) { .centre = g->position,
            .born = tick,
            .owner = g->owner,
            .generation = g->generation,
            .life = g->life,
            .height = r->wave_height,
            .speed = r->wave_speed,
            .range = r->wave_range,
            .active = true };
        g->active = false;
    }
    for (unsigned i = 0; i < BOAT_COMBAT_PLAYERS; ++i) {
        BoatCombatPlayer *p = &c->players[i];
        const BoatFleetSlot *s = &f->state.slots[i];
        bool ready = s->active && !s->pending && s->boat.motion_state != BOAT_CAPSIZED
            && p->health > 0 && c->firing && tick - p->life_start >= r->grace_ticks;
        bool gun = ready && inputs[i].gun && !p->overheated;
        if (gun) {
            p->heat = fminf(1, p->heat + 1.0f / (float)r->heat_ticks);
            if (p->heat >= .99999f) {
                p->heat = 1;
                p->overheated = true;
            }
        } else if (!inputs[i].gun || !ready) {
            p->heat = fmaxf(0, p->heat - 1.0f / (float)r->cool_ticks);
            if (p->heat < .3f)
                p->overheated = false;
        }
        Vector2 dir = { sinf(s->boat.heading), -cosf(s->boat.heading) };
        BoatCombatObject object
            = { .position = Vector2Add(s->boat.position, Vector2Scale(dir, 2.1f)),
                  .direction = dir,
                  .height = s->boat.height + .65f,
                  .born = tick,
                  .generation = p->generation,
                  .life = s->life,
                  .owner = i,
                  .active = true };
        if (gun && tick >= p->next_fire) {
            BoatCombatObject *b = available(c->bullets, BOAT_COMBAT_BULLETS);
            if (b) {
                *b = object;
                p->next_fire = tick + r->fire_ticks;
            }
        }
        // Consume even rejected serials; packet repetition must never defer a throw.
        uint16_t request = inputs[i].grenade;
        if (request && request != p->request) {
            p->request = request;
            if (!ready || tick < p->next_grenade)
                continue;
            for (unsigned k = 0; k < BOAT_COMBAT_GRENADES; ++k)
                if (!c->grenades[k].active && !c->waves[k].active) {
                    object.position
                        = Vector2Add(s->boat.position, Vector2Scale(dir, r->grenade_distance));
                    c->grenades[k] = object;
                    p->next_grenade = tick + r->grenade_ticks;
                    break;
                }
        }
    }
    boat_combat_bind(f);
}

// Slab sweep in the moving hull's relative frame. Slots break exact ties deterministically.
static float hull_hit(Vector2 a, Vector2 b, float height, const Boat *old, const Boat *now)
{
    if (height < fminf(old->height, now->height) - .5f
        || height > fmaxf(old->height, now->height) + 1.3f)
        return INFINITY;
    Vector2 right = { cosf(now->heading), sinf(now->heading) };
    Vector2 forward = { sinf(now->heading), -cosf(now->heading) };
    a = Vector2Subtract(a, old->position);
    b = Vector2Subtract(b, now->position);
    float start[] = { Vector2DotProduct(a, right), Vector2DotProduct(a, forward) };
    float end[] = { Vector2DotProduct(b, right), Vector2DotProduct(b, forward) };
    float low[] = { -.85f, -1.8f }, high[] = { .85f, 2.1f };
    float enter = 0, leave = 1;
    for (unsigned j = 0; j < 2; ++j) {
        float d = end[j] - start[j];
        if (fabsf(d) < 1e-8f) {
            if (start[j] < low[j] || start[j] > high[j])
                return INFINITY;
        } else {
            float x = (low[j] - start[j]) / d, y = (high[j] - start[j]) / d;
            enter = fmaxf(enter, fminf(x, y));
            leave = fminf(leave, fmaxf(x, y));
        }
    }
    return enter <= leave ? enter : INFINITY;
}

static bool blocked(const BoatFleet *f, Vector2 at, float height, double time)
{
    const BoatWorld *w = &f->world;
    if (boat_water_query_borrowed(&w->water, at, time).height >= height)
        return true;
    if (!w->definition->mapped)
        return fabsf(at.x) >= w->definition->half_extent.x
            || fabsf(at.y) >= w->definition->half_extent.y;
    Vector2 normal;
    WaterfrontTrafficContact contact;
    return !boat_map_water_contact(&w->resources->map, at, .05f, &normal)
        || waterfront_harbor_contact_height(at, .05f, height, height, w->water, time, &normal)
        || waterfront_traffic_contact_height(
            at, 0, .05f, .05f, .05f, height, height, w->water, time, &contact);
}

void boat_combat_hits(BoatFleet *f, const Boat before[BOAT_COMBAT_PLAYERS])
{
    BoatCombat *c = &f->state.combat;
    if (!c->enabled)
        return;
    uint64_t tick = f->state.tick;
    double time = boat_fleet_time(f);
    // Pool order fixes simultaneous lethal-hit attribution; defeated targets stop taking damage.
    for (unsigned k = 0; k < BOAT_COMBAT_BULLETS; ++k) {
        BoatCombatObject *b = &c->bullets[k];
        if (!b->active)
            continue;
        if ((tick - b->born) * BOAT_STEP * c->rules.bullet_speed >= c->rules.bullet_range) {
            b->active = false;
            b->ended = tick;
            continue;
        }
        Vector2 end = Vector2Add(
            b->position, Vector2Scale(b->direction, c->rules.bullet_speed * BOAT_STEP));
        float first = INFINITY;
        unsigned victim = BOAT_COMBAT_PLAYERS;
        // Conservative 5 cm scenery sweep. At 100 m/s this is at most 17 probes/tick.
        unsigned steps = (unsigned)ceilf(c->rules.bullet_speed * BOAT_STEP / .05f);
        for (unsigned j = 0; j <= steps; ++j) {
            float t = (float)j / (float)steps;
            if (blocked(f, Vector2Lerp(b->position, end, t), b->height, time)) {
                first = t;
                break;
            }
        }
        for (unsigned i = 0; i < BOAT_COMBAT_PLAYERS; ++i) {
            const BoatFleetSlot *s = &f->state.slots[i];
            BoatCombatPlayer *p = &c->players[i];
            // Birth-time life fence: a projectile cannot hit a later replacement life.
            if (i == b->owner || !s->active || p->health <= 0 || b->born < p->life_start
                || tick - p->life_start < c->rules.grace_ticks)
                continue;
            float t = hull_hit(b->position, end, b->height, &before[i], &s->boat);
            if (t < first) {
                first = t;
                victim = i;
            }
        }
        if (first <= 1) {
            b->position = Vector2Lerp(b->position, end, first);
            b->active = false;
            b->ended = tick;
            if (victim < BOAT_COMBAT_PLAYERS) {
                BoatCombatPlayer *p = &c->players[victim];
                p->health = fmaxf(0, p->health - c->rules.damage);
                ++c->players[b->owner].hit_serial;
                c->players[b->owner].hit_tick = tick;
                if (!p->health) {
                    p->killer = b->owner;
                    p->defeated = tick;
                    f->state.slots[victim].boat.motion_state = BOAT_CAPSIZED;
                }
            }
        } else
            b->position = end;
    }
}

// Field codec shared by network checkpoints and the state digest. Decoder commits atomically.
typedef struct {
    uint8_t *write;
    const uint8_t *read;
    unsigned at;
    bool valid;
} CombatCodec;
static void word(CombatCodec *c, uint32_t *v)
{
    if (c->write)
        for (unsigned j = 0; j < 4; ++j)
            c->write[c->at++] = (uint8_t)(*v >> (24 - 8 * j));
    else {
        *v = 0;
        for (unsigned j = 0; j < 4; ++j)
            *v = (*v << 8) | c->read[c->at++];
    }
}
static void wide(CombatCodec *c, uint64_t *v)
{
    uint32_t hi = (uint32_t)(*v >> 32), lo = (uint32_t)*v;
    word(c, &hi);
    word(c, &lo);
    if (!c->write)
        *v = ((uint64_t)hi << 32) | lo;
}
static void real(CombatCodec *c, float *v)
{
    uint32_t bits;
    memcpy(&bits, v, 4);
    word(c, &bits);
    memcpy(v, &bits, 4);
    if (!isfinite(*v) || fabsf(*v) > 1e6f)
        c->valid = false;
}
static void flag(CombatCodec *c, bool *v)
{
    uint32_t value = *v;
    word(c, &value);
    if (value > 1)
        c->valid = false;
    *v = value != 0;
}
static void point(CombatCodec *c, Vector2 *v)
{
    real(c, &v->x);
    real(c, &v->y);
}
static void combat_codec(CombatCodec *c, BoatCombat *s, uint64_t tick)
{
    flag(c, &s->enabled);
    flag(c, &s->firing);
    BoatCombatRules *r = &s->rules;
    real(c, &r->health);
    real(c, &r->damage);
    real(c, &r->bullet_speed);
    real(c, &r->bullet_range);
    real(c, &r->grenade_distance);
    real(c, &r->wave_height);
    real(c, &r->wave_speed);
    real(c, &r->wave_range);
    word(c, &r->fire_ticks);
    word(c, &r->heat_ticks);
    word(c, &r->cool_ticks);
    word(c, &r->grenade_ticks);
    word(c, &r->splash_ticks);
    word(c, &r->fuse_ticks);
    word(c, &r->grace_ticks);
    if (s->enabled
        && (!(r->health > 0) || !(r->damage > 0) || r->bullet_speed <= 0 || r->bullet_speed > 200
            || r->bullet_range <= 0 || r->wave_range <= 0 || r->wave_speed <= 0 || !r->fire_ticks
            || !r->heat_ticks || !r->cool_ticks || !r->fuse_ticks || !r->splash_ticks
            || r->splash_ticks > r->fuse_ticks))
        c->valid = false;
    for (unsigned i = 0; i < BOAT_COMBAT_PLAYERS; ++i) {
        BoatCombatPlayer *p = &s->players[i];
        real(c, &p->health);
        real(c, &p->heat);
        wide(c, &p->life_start);
        wide(c, &p->next_fire);
        wide(c, &p->next_grenade);
        wide(c, &p->defeated);
        wide(c, &p->hit_tick);
        word(c, &p->generation);
        word(c, &p->request);
        word(c, &p->hit_serial);
        word(c, &p->killer);
        flag(c, &p->overheated);
        if (p->health < 0 || p->health > r->health || p->heat < 0 || p->heat > 1
            || p->request > UINT16_MAX || p->life_start > tick || p->hit_tick > tick
            || p->defeated > tick || (p->killer >= BOAT_COMBAT_PLAYERS && p->killer != UINT32_MAX))
            c->valid = false;
    }
    for (unsigned k = 0; k < BOAT_COMBAT_BULLETS + BOAT_COMBAT_GRENADES; ++k) {
        BoatCombatObject *o
            = k < BOAT_COMBAT_BULLETS ? &s->bullets[k] : &s->grenades[k - BOAT_COMBAT_BULLETS];
        point(c, &o->position);
        point(c, &o->direction);
        real(c, &o->height);
        wide(c, &o->born);
        wide(c, &o->ended);
        word(c, &o->generation);
        word(c, &o->life);
        word(c, &o->owner);
        flag(c, &o->active);
        if (o->owner >= BOAT_COMBAT_PLAYERS || o->born > tick || o->ended > tick
            || fabsf(o->position.x) > 8192 || fabsf(o->position.y) > 8192
            || fabsf(o->height) > 1000)
            c->valid = false;
        if (o->active && fabsf(Vector2LengthSqr(o->direction) - 1) > .01f)
            c->valid = false;
    }
    for (unsigned k = 0; k < BOAT_COMBAT_WAVES; ++k) {
        BoatRadialWave *w = &s->waves[k];
        point(c, &w->centre);
        wide(c, &w->born);
        word(c, &w->generation);
        word(c, &w->life);
        word(c, &w->owner);
        real(c, &w->height);
        real(c, &w->speed);
        real(c, &w->range);
        flag(c, &w->active);
        if (w->owner >= BOAT_COMBAT_PLAYERS || w->born > tick || fabsf(w->centre.x) > 8192
            || fabsf(w->centre.y) > 8192
            || (w->active && (w->height <= 0 || w->speed <= 0 || w->range <= 0)))
            c->valid = false;
    }
    assert(c->at == BOAT_COMBAT_BYTES);
}
void boat_combat_encode(const BoatCombat *combat, uint8_t bytes[BOAT_COMBAT_BYTES])
{
    BoatCombat copy = *combat;
    CombatCodec c = { .write = bytes, .valid = true };
    combat_codec(&c, &copy, UINT64_MAX);
}
bool boat_combat_decode(BoatCombat *combat, const uint8_t bytes[BOAT_COMBAT_BYTES], uint64_t tick)
{
    BoatCombat copy = { 0 };
    CombatCodec c = { .read = bytes, .valid = true };
    combat_codec(&c, &copy, tick);
    if (!c.valid)
        return false;
    *combat = copy;
    return true;
}
