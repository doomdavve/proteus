#include "boat_combat.h"
#include "boat_fleet.h"
#include "boat_pose.h"
#include "raymath.h"
#include "waterfront_harbor.h"
#include "waterfront_traffic.h"

#include <assert.h>
#include <math.h>
#include <string.h>

bool boat_combat_rules_equal(const BoatCombatRules *a, const BoatCombatRules *b)
{
    return a->health == b->health && a->damage == b->damage && a->bullet_speed == b->bullet_speed
        && a->bullet_range == b->bullet_range && a->grenade_distance == b->grenade_distance
        && a->wave_height == b->wave_height && a->wave_speed == b->wave_speed
        && a->wave_range == b->wave_range && a->cannon_pressure == b->cannon_pressure
        && a->pressure_recovery == b->pressure_recovery && a->torpedo_damage == b->torpedo_damage
        && a->torpedo_speed == b->torpedo_speed && a->torpedo_range == b->torpedo_range
        && a->torpedo_ticks == b->torpedo_ticks && a->arming_ticks == b->arming_ticks
        && a->torpedo_ammo == b->torpedo_ammo && a->fire_ticks == b->fire_ticks
        && a->heat_ticks == b->heat_ticks && a->cool_ticks == b->cool_ticks
        && a->grenade_ticks == b->grenade_ticks && a->splash_ticks == b->splash_ticks
        && a->fuse_ticks == b->fuse_ticks && a->grace_ticks == b->grace_ticks;
}

float boat_combat_cannon_rise(float age)
{
    return BOAT_CANNON_LIFT * age - .5f * BOAT_CANNON_GRAVITY * age * age;
}

Vector3 boat_combat_cannon_muzzle(const Boat *boat)
{
    return boat_pose_attachment(boat,
        (Vector3) {
            BOAT_CANNON_MUZZLE_SIDE, BOAT_CANNON_MUZZLE_HEIGHT, -BOAT_CANNON_MUZZLE_FORWARD });
}

void boat_combat_bind(BoatFleet *f)
{
    f->world.combat = f->state.combat.enabled ? &f->state.combat : NULL;
    f->world.water.radial = f->state.combat.enabled ? f->state.combat.waves : NULL;
}

void boat_combat_enable(BoatFleet *f, const BoatCombatRules *rules)
{
    assert(rules);
    f->state.combat = (BoatCombat) { .enabled = true, .firing = true, .rules = *rules };
    for (unsigned i = 0; i < BOAT_COMBAT_PLAYERS; ++i)
        boat_combat_spawn(f, i);
    boat_combat_bind(f);
}

// Inactive objects are canonical: either a recent hit echo that presentation still draws, or
// all zero. This keeps the checkpoint/hash independent of stale pool history and lets the
// compact wire format omit zero slots.
static void combat_canonicalize(BoatCombat *c, uint64_t tick)
{
    for (unsigned k = 0; k < BOAT_COMBAT_BULLETS + BOAT_COMBAT_GRENADES + BOAT_COMBAT_TORPEDOES;
        ++k) {
        BoatCombatObject *o = k < BOAT_COMBAT_BULLETS ? &c->bullets[k]
            : k < BOAT_COMBAT_BULLETS + BOAT_COMBAT_GRENADES
            ? &c->grenades[k - BOAT_COMBAT_BULLETS]
            : &c->torpedoes[k - BOAT_COMBAT_BULLETS - BOAT_COMBAT_GRENADES];
        if (!o->active && (!o->ended || tick - o->ended >= BOAT_COMBAT_ECHO_TICKS))
            *o = (BoatCombatObject) { 0 };
    }
    for (unsigned i = 0; i < BOAT_COMBAT_WAVES; ++i)
        if (!c->waves[i].active)
            c->waves[i] = (BoatRadialWave) { 0 };
}

void boat_combat_clear(BoatFleet *f)
{
    BoatCombat *c = &f->state.combat;
    for (unsigned i = 0; i < BOAT_COMBAT_BULLETS; ++i)
        c->bullets[i] = (BoatCombatObject) { 0 };
    for (unsigned i = 0; i < BOAT_COMBAT_GRENADES; ++i)
        c->grenades[i] = (BoatCombatObject) { 0 };
    for (unsigned i = 0; i < BOAT_COMBAT_TORPEDOES; ++i)
        c->torpedoes[i] = (BoatCombatObject) { 0 };
    for (unsigned i = 0; i < BOAT_COMBAT_WAVES; ++i)
        c->waves[i] = (BoatRadialWave) { 0 };
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
        .killer = UINT32_MAX,
        .ammo = c->rules.torpedo_ammo,
        .pressure_owner = UINT32_MAX };
}

void boat_combat_leave(BoatFleet *f, unsigned slot)
{
    BoatCombat *c = &f->state.combat;
    if (!c->enabled)
        return;
    ++c->players[slot].generation;
    for (unsigned i = 0; i < BOAT_COMBAT_TORPEDOES; ++i)
        if (c->torpedoes[i].owner == slot)
            c->torpedoes[i] = (BoatCombatObject) { 0 };
    for (unsigned i = 0; i < BOAT_COMBAT_BULLETS; ++i)
        if (c->bullets[i].owner == slot)
            c->bullets[i] = (BoatCombatObject) { 0 };
    for (unsigned i = 0; i < BOAT_COMBAT_GRENADES; ++i)
        if (c->grenades[i].owner == slot)
            c->grenades[i] = (BoatCombatObject) { 0 };
    for (unsigned i = 0; i < BOAT_COMBAT_WAVES; ++i)
        if (c->waves[i].owner == slot)
            c->waves[i] = (BoatRadialWave) { 0 };
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
    combat_canonicalize(c, tick);
    for (unsigned i = 0; i < BOAT_COMBAT_WAVES; ++i)
        if (c->waves[i].active
            && (tick - c->waves[i].born) * BOAT_STEP * r->wave_speed >= r->wave_range)
            c->waves[i] = (BoatRadialWave) { 0 };
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
        *g = (BoatCombatObject) { 0 };
    }
    for (unsigned i = 0; i < BOAT_COMBAT_PLAYERS; ++i) {
        BoatCombatPlayer *p = &c->players[i];
        const BoatFleetSlot *s = &f->state.slots[i];
        p->pressure = fmaxf(0, p->pressure - r->pressure_recovery * BOAT_STEP);
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
                Vector3 muzzle = boat_combat_cannon_muzzle(&s->boat);
                b->position = (Vector2) { -muzzle.x, muzzle.z };
                b->height = muzzle.y;
                p->next_fire = tick + r->fire_ticks;
            }
        }
        uint8_t torpedo = inputs[i].torpedo;
        if (torpedo && torpedo != p->torpedo_request) {
            p->torpedo_request = torpedo;
            if (ready && p->ammo && tick >= p->next_torpedo) {
                BoatCombatObject *t = available(c->torpedoes, BOAT_COMBAT_TORPEDOES);
                if (t) {
                    *t = object;
                    --p->ammo;
                    p->next_torpedo = tick + r->torpedo_ticks;
                }
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
                        = Vector2Subtract(s->boat.position, Vector2Scale(dir, r->grenade_distance));
                    object.direction = Vector2Negate(dir);
                    c->grenades[k] = object;
                    p->next_grenade = tick + r->grenade_ticks;
                    break;
                }
        }
    }
    boat_combat_bind(f);
}

// Slab sweep in the moving hull's relative frame. Slots break exact ties deterministically.
static float hull_hit(Vector2 a, Vector2 b, float height, float end_height, float radius,
    const Boat *old, const Boat *now)
{
    Vector2 right = { cosf(now->heading), sinf(now->heading) };
    Vector2 forward = { sinf(now->heading), -cosf(now->heading) };
    a = Vector2Subtract(a, old->position);
    b = Vector2Subtract(b, now->position);
    float start[]
        = { Vector2DotProduct(a, right), Vector2DotProduct(a, forward), height - old->height };
    float end[]
        = { Vector2DotProduct(b, right), Vector2DotProduct(b, forward), end_height - now->height };
    float low[] = { -.85f - radius, -1.8f - radius, -.5f - radius };
    float high[] = { .85f + radius, 2.1f + radius, 1.3f + radius };
    float enter = 0, leave = 1;
    for (unsigned j = 0; j < 3; ++j) {
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
        || waterfront_harbor_contact_height_scene(
            w->scene, at, .05f, height, height, w->water, time, &normal)
        || waterfront_traffic_contact_height_scene(
            w->scene, at, 0, .05f, .05f, .05f, height, height, w->water, time, &contact);
}

void boat_combat_hits(BoatFleet *f, const Boat before[BOAT_COMBAT_PLAYERS])
{
    BoatCombat *c = &f->state.combat;
    if (!c->enabled)
        return;
    uint64_t tick = f->state.tick;
    double time = boat_fleet_time(f);
    // Pool order fixes simultaneous lethal-hit attribution; defeated targets stop taking damage.
    for (unsigned k = 0; k < BOAT_COMBAT_BULLETS + BOAT_COMBAT_TORPEDOES; ++k) {
        bool torpedo = k >= BOAT_COMBAT_BULLETS;
        BoatCombatObject *b = torpedo ? &c->torpedoes[k - BOAT_COMBAT_BULLETS] : &c->bullets[k];
        float speed = torpedo ? c->rules.torpedo_speed : c->rules.bullet_speed;
        float range = torpedo ? c->rules.torpedo_range : c->rules.bullet_range;
        if (!b->active)
            continue;
        if ((tick - b->born) * BOAT_STEP * speed >= range) {
            b->active = false;
            b->ended = tick;
            continue;
        }
        Vector2 end = Vector2Add(b->position, Vector2Scale(b->direction, speed * BOAT_STEP));
        float age = (float)(tick - b->born) * BOAT_STEP;
        float rise = boat_combat_cannon_rise(age);
        float end_height = torpedo
            ? boat_water_query_borrowed(&f->world.water, end, time).height + .2f
            : b->height + boat_combat_cannon_rise(age + BOAT_STEP) - rise;
        float first = INFINITY;
        unsigned victim = BOAT_COMBAT_PLAYERS;
        // Sample the curved path against scenery at intervals no longer than 5 cm.
        unsigned steps = (unsigned)ceilf(speed * BOAT_STEP / .05f);
        for (unsigned j = 0; j <= steps; ++j) {
            float t = (float)j / (float)steps;
            Vector2 at = Vector2Lerp(b->position, end, t);
            // Torpedoes follow the surface; scenery still stops them, including before arming.
            float height = torpedo
                ? boat_water_query_borrowed(&f->world.water, at, time).height + .2f
                : b->height + boat_combat_cannon_rise(age + t * BOAT_STEP) - rise;
            if (blocked(f, at, height - (torpedo ? 0 : BOAT_CANNON_RADIUS), time)) {
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
            if (s->pending || s->boat.motion_state == BOAT_CAPSIZED)
                continue;
            float t = hull_hit(b->position, end, b->height, end_height,
                torpedo ? 0 : BOAT_CANNON_RADIUS, &before[i], &s->boat);
            if (t < first) {
                first = t;
                victim = i;
            }
        }
        if (first <= 1) {
            b->position = Vector2Lerp(b->position, end, first);
            b->height = torpedo
                ? b->height + (end_height - b->height) * first
                : b->height + boat_combat_cannon_rise(age + first * BOAT_STEP) - rise;
            b->active = false;
            b->ended = tick;
            if (victim < BOAT_COMBAT_PLAYERS) {
                BoatCombatPlayer *p = &c->players[victim];
                BoatCombatPlayer *attacker = &c->players[b->owner];
                // Unarmed torpedoes break on contact without damaging the hull.
                if (torpedo && tick - b->born < c->rules.arming_ticks)
                    continue;
                ++attacker->hit_serial;
                attacker->hit_tick = tick;
                if (torpedo) {
                    float damage = fminf(p->health, c->rules.torpedo_damage);
                    p->health -= damage;
                    if (!attacker->damage_tick[victim] || tick - attacker->damage_tick[victim] > 90
                        || attacker->damage_life[victim] != f->state.slots[victim].life
                        || attacker->damage_generation[victim] != p->generation)
                        attacker->damage[victim] = 0;
                    attacker->damage[victim] += damage;
                    attacker->damage_tick[victim] = tick;
                    attacker->damage_life[victim] = f->state.slots[victim].life;
                    attacker->damage_generation[victim] = p->generation;
                    if (!p->health) {
                        p->killer = b->owner;
                        p->defeated = tick;
                        f->state.slots[victim].boat.motion_state = BOAT_CAPSIZED;
                    }
                } else {
                    Boat *boat = &f->state.slots[victim].boat;
                    Vector2 right = { cosf(boat->heading), sinf(boat->heading) };
                    float side = Vector2DotProduct(b->direction, right);
                    float load = c->rules.cannon_pressure * (.2f + .8f * fabsf(side));
                    p->pressure = fminf(1, p->pressure + load);
                    p->pressure_tick = tick;
                    p->pressure_owner = b->owner;
                    p->pressure_generation = attacker->generation;
                    boat->velocity = Vector2Add(boat->velocity, Vector2Scale(b->direction, .35f));
                    float sign = side >= 0 ? -1.0f : 1.0f;
                    boat->roll_velocity += sign * load * 3;
                    // A sustained stream overwhelms stability gradually, visibly heeling first.
                    if (fabsf(boat->roll) < p->pressure * 1.3f)
                        boat->roll = sign * p->pressure * 1.3f;
                    if (p->pressure >= 1) {
                        boat->roll = sign * 1.8f;
                        boat->motion_state = BOAT_CAPSIZED;
                    }
                }
            }
        } else {
            b->position = end;
            b->height = end_height;
        }
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
    real(c, &r->cannon_pressure);
    real(c, &r->pressure_recovery);
    real(c, &r->torpedo_damage);
    real(c, &r->torpedo_speed);
    real(c, &r->torpedo_range);
    word(c, &r->torpedo_ticks);
    word(c, &r->arming_ticks);
    word(c, &r->torpedo_ammo);
    if (s->enabled
        && (!(r->health > 0) || r->damage < 0 || r->bullet_speed <= 0 || r->bullet_speed > 200
            || r->bullet_range <= 0 || r->wave_range <= 0 || r->wave_speed <= 0 || !r->fire_ticks
            || !r->heat_ticks || !r->cool_ticks || !r->fuse_ticks || !r->splash_ticks
            || r->splash_ticks > r->fuse_ticks || r->grenade_distance <= 0
            || r->cannon_pressure <= 0 || r->cannon_pressure > 1 || r->pressure_recovery <= 0
            || r->torpedo_damage <= 0 || r->torpedo_speed <= 0 || r->torpedo_speed > 200
            || r->torpedo_range <= 0 || !r->torpedo_ticks || !r->arming_ticks || !r->torpedo_ammo
            || r->torpedo_ammo > 99))
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
        real(c, &p->pressure);
        wide(c, &p->next_torpedo);
        wide(c, &p->pressure_tick);
        word(c, &p->torpedo_request);
        word(c, &p->ammo);
        word(c, &p->pressure_owner);
        word(c, &p->pressure_generation);
        for (unsigned j = 0; j < BOAT_COMBAT_PLAYERS; ++j) {
            real(c, &p->damage[j]);
            wide(c, &p->damage_tick[j]);
            word(c, &p->damage_life[j]);
            word(c, &p->damage_generation[j]);
            if (p->damage[j] < 0 || p->damage[j] > r->health || p->damage_tick[j] > tick)
                c->valid = false;
        }
        if (p->pressure < 0 || p->pressure > 1 || p->pressure_tick > tick
            || p->ammo > r->torpedo_ammo || p->torpedo_request > UINT8_MAX
            || (p->pressure_owner >= BOAT_COMBAT_PLAYERS && p->pressure_owner != UINT32_MAX))
            c->valid = false;
        if (p->health < 0 || p->health > r->health || p->heat < 0 || p->heat > 1
            || p->request > UINT16_MAX || p->life_start > tick || p->hit_tick > tick
            || p->defeated > tick || (p->killer >= BOAT_COMBAT_PLAYERS && p->killer != UINT32_MAX))
            c->valid = false;
    }
    for (unsigned k = 0; k < BOAT_COMBAT_BULLETS + BOAT_COMBAT_GRENADES + BOAT_COMBAT_TORPEDOES;
        ++k) {
        BoatCombatObject *o = k < BOAT_COMBAT_BULLETS ? &s->bullets[k]
            : k < BOAT_COMBAT_BULLETS + BOAT_COMBAT_GRENADES
            ? &s->grenades[k - BOAT_COMBAT_BULLETS]
            : &s->torpedoes[k - BOAT_COMBAT_BULLETS - BOAT_COMBAT_GRENADES];
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

// Compact wire codec for the flagship deathmatch state. It deliberately omits the immutable
// rules block (the reliable checkpoint installed them), inactive objects and fields that do not
// affect the next tick's simulation. The canonical checkpoint codec above remains the hash and
// bootstrap representation.
typedef struct {
    uint8_t *out;
    size_t at, capacity;
    bool valid;
} CompactWriter;

typedef struct {
    const uint8_t *in;
    size_t at, size;
    bool valid;
} CompactReader;

static void cw8(CompactWriter *w, uint8_t value)
{
    if (!w->valid || w->at >= w->capacity) {
        w->valid = false;
        return;
    }
    w->out[w->at++] = value;
}

static void cw16(CompactWriter *w, uint16_t value)
{
    cw8(w, (uint8_t)(value >> 8));
    cw8(w, (uint8_t)value);
}

static void cw32(CompactWriter *w, uint32_t value)
{
    cw8(w, (uint8_t)(value >> 24));
    cw8(w, (uint8_t)(value >> 16));
    cw8(w, (uint8_t)(value >> 8));
    cw8(w, (uint8_t)value);
}

static void cwf(CompactWriter *w, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    cw32(w, bits);
}

static void cw_delta(CompactWriter *w, uint64_t value, uint64_t tick)
{
    int64_t delta = (int64_t)value - (int64_t)tick;
    if (delta < INT32_MIN || delta > INT32_MAX)
        w->valid = false;
    cw32(w, (uint32_t)(int32_t)delta);
}

static void cw_age(CompactWriter *w, uint64_t born, uint64_t tick)
{
    if (born > tick || tick - born > UINT16_MAX) {
        w->valid = false;
        return;
    }
    cw16(w, (uint16_t)(tick - born));
}

static uint8_t cr8(CompactReader *r)
{
    if (!r->valid || r->at >= r->size) {
        r->valid = false;
        return 0;
    }
    return r->in[r->at++];
}

static uint16_t cr16(CompactReader *r)
{
    uint16_t high = cr8(r), low = cr8(r);
    return (uint16_t)((high << 8) | low);
}

static uint32_t cr32(CompactReader *r)
{
    uint32_t a = cr8(r), b = cr8(r), c = cr8(r), d = cr8(r);
    return (a << 24) | (b << 16) | (c << 8) | d;
}

static float crf(CompactReader *r)
{
    uint32_t bits = cr32(r);
    float value;
    memcpy(&value, &bits, sizeof(value));
    if (!isfinite(value) || fabsf(value) > 1e6f)
        r->valid = false;
    return value;
}

static uint64_t cr_delta(CompactReader *r, uint64_t tick)
{
    int32_t delta = (int32_t)cr32(r);
    int64_t value = (int64_t)tick + delta;
    if (value < 0) {
        r->valid = false;
        return 0;
    }
    return (uint64_t)value;
}

static uint64_t cr_age(CompactReader *r, uint64_t tick)
{
    uint16_t age = cr16(r);
    return age == 0 ? tick : tick - age;
}

size_t boat_combat_compact_encode(
    const BoatCombat *combat, uint8_t *bytes, size_t capacity, uint64_t tick)
{
    CompactWriter w = { bytes, 0, capacity, true };
    cw8(&w, 1); // Format 1.
    cw8(&w, (uint8_t)((combat->enabled ? 1 : 0) | (combat->firing ? 2 : 0)));
    uint8_t object_bits[7] = { 0 };
    const unsigned object_count
        = BOAT_COMBAT_BULLETS + BOAT_COMBAT_GRENADES + BOAT_COMBAT_TORPEDOES;
    for (unsigned i = 0; i < object_count; ++i) {
        const BoatCombatObject *o = i < BOAT_COMBAT_BULLETS ? &combat->bullets[i]
            : i < BOAT_COMBAT_BULLETS + BOAT_COMBAT_GRENADES
            ? &combat->grenades[i - BOAT_COMBAT_BULLETS]
            : &combat->torpedoes[i - BOAT_COMBAT_BULLETS - BOAT_COMBAT_GRENADES];
        if (o->active || (o->ended && tick - o->ended < BOAT_COMBAT_ECHO_TICKS))
            object_bits[i / 8] |= (uint8_t)(1u << (i % 8));
    }
    for (unsigned i = 0; i < 7; ++i)
        cw8(&w, object_bits[i]);
    uint8_t wave_bits = 0;
    for (unsigned i = 0; i < BOAT_COMBAT_WAVES; ++i)
        if (combat->waves[i].active)
            wave_bits |= (uint8_t)(1u << i);
    cw8(&w, wave_bits);
    for (unsigned i = 0; i < BOAT_COMBAT_PLAYERS; ++i) {
        const BoatCombatPlayer *p = &combat->players[i];
        cwf(&w, p->health);
        cwf(&w, p->heat);
        cwf(&w, p->pressure);
        cw_delta(&w, p->life_start, tick);
        cw_delta(&w, p->next_fire, tick);
        cw_delta(&w, p->next_grenade, tick);
        cw_delta(&w, p->defeated, tick);
        cw_delta(&w, p->hit_tick, tick);
        cw_delta(&w, p->next_torpedo, tick);
        cw_delta(&w, p->pressure_tick, tick);
        cw32(&w, p->generation);
        cw32(&w, p->request);
        cw32(&w, p->hit_serial);
        cw32(&w, p->killer);
        cw32(&w, p->torpedo_request);
        cw32(&w, p->ammo);
        cw32(&w, p->pressure_owner);
        cw32(&w, p->pressure_generation);
        cw8(&w, p->overheated ? 1 : 0);
        for (unsigned j = 0; j < BOAT_COMBAT_PLAYERS; ++j) {
            cwf(&w, p->damage[j]);
            cw_delta(&w, p->damage_tick[j], tick);
            cw32(&w, p->damage_life[j]);
            cw32(&w, p->damage_generation[j]);
        }
    }
    for (unsigned i = 0; i < object_count; ++i) {
        const BoatCombatObject *o = i < BOAT_COMBAT_BULLETS ? &combat->bullets[i]
            : i < BOAT_COMBAT_BULLETS + BOAT_COMBAT_GRENADES
            ? &combat->grenades[i - BOAT_COMBAT_BULLETS]
            : &combat->torpedoes[i - BOAT_COMBAT_BULLETS - BOAT_COMBAT_GRENADES];
        if (!o->active && (!o->ended || tick - o->ended >= BOAT_COMBAT_ECHO_TICKS))
            continue;
        cwf(&w, o->position.x);
        cwf(&w, o->position.y);
        cwf(&w, o->direction.x);
        cwf(&w, o->direction.y);
        cwf(&w, o->height);
        cw_age(&w, o->born, tick);
        if (o->ended) {
            if (o->ended > tick || tick - o->ended >= UINT16_MAX)
                w.valid = false;
            else
                cw16(&w, (uint16_t)(tick - o->ended + 1)); // Zero means "not ended".
        } else
            cw16(&w, 0);
        cw32(&w, o->generation);
        cw32(&w, o->life);
        cw8(&w, (uint8_t)o->owner);
    }
    for (unsigned i = 0; i < BOAT_COMBAT_WAVES; ++i) {
        const BoatRadialWave *wave = &combat->waves[i];
        if (!wave->active)
            continue;
        cwf(&w, wave->centre.x);
        cwf(&w, wave->centre.y);
        cw_age(&w, wave->born, tick);
        cw32(&w, wave->generation);
        cw32(&w, wave->life);
        cw8(&w, (uint8_t)wave->owner);
        cwf(&w, wave->height);
        cwf(&w, wave->speed);
        cwf(&w, wave->range);
    }
    return w.valid ? w.at : 0;
}

static bool compact_player_valid(const BoatCombatPlayer *p, const BoatCombatRules *r, uint64_t tick)
{
    if (p->health < 0 || p->health > r->health || p->heat < 0 || p->heat > 1
        || p->request > UINT16_MAX || p->life_start > tick || p->hit_tick > tick
        || p->defeated > tick || (p->killer >= BOAT_COMBAT_PLAYERS && p->killer != UINT32_MAX)
        || p->pressure < 0 || p->pressure > 1 || p->pressure_tick > tick
        || p->ammo > r->torpedo_ammo || p->torpedo_request > UINT8_MAX
        || (p->pressure_owner >= BOAT_COMBAT_PLAYERS && p->pressure_owner != UINT32_MAX))
        return false;
    for (unsigned j = 0; j < BOAT_COMBAT_PLAYERS; ++j)
        if (p->damage[j] < 0 || p->damage[j] > r->health || p->damage_tick[j] > tick)
            return false;
    return true;
}

bool boat_combat_compact_decode(BoatCombat *combat, const uint8_t *bytes, size_t size,
    uint64_t tick, const BoatCombatRules *rules)
{
    if (!rules || !bytes || size == 0)
        return false;
    BoatCombatRules installed = *rules;
    CompactReader r = { bytes, 0, size, true };
    if (cr8(&r) != 1)
        return false;
    uint8_t flags = cr8(&r);
    if (flags & ~3u)
        return false;
    uint8_t object_bits[7];
    for (unsigned i = 0; i < 7; ++i)
        object_bits[i] = cr8(&r);
    uint8_t wave_bits = cr8(&r);
    BoatCombat decoded
        = { .enabled = (flags & 1) != 0, .firing = (flags & 2) != 0, .rules = installed };
    for (unsigned i = 0; i < BOAT_COMBAT_PLAYERS; ++i) {
        BoatCombatPlayer *p = &decoded.players[i];
        p->health = crf(&r);
        p->heat = crf(&r);
        p->pressure = crf(&r);
        p->life_start = cr_delta(&r, tick);
        p->next_fire = cr_delta(&r, tick);
        p->next_grenade = cr_delta(&r, tick);
        p->defeated = cr_delta(&r, tick);
        p->hit_tick = cr_delta(&r, tick);
        p->next_torpedo = cr_delta(&r, tick);
        p->pressure_tick = cr_delta(&r, tick);
        p->generation = cr32(&r);
        p->request = cr32(&r);
        p->hit_serial = cr32(&r);
        p->killer = cr32(&r);
        p->torpedo_request = cr32(&r);
        p->ammo = cr32(&r);
        p->pressure_owner = cr32(&r);
        p->pressure_generation = cr32(&r);
        uint8_t overheated = cr8(&r);
        if (overheated > 1)
            r.valid = false;
        p->overheated = overheated != 0;
        for (unsigned j = 0; j < BOAT_COMBAT_PLAYERS; ++j) {
            p->damage[j] = crf(&r);
            p->damage_tick[j] = cr_delta(&r, tick);
            p->damage_life[j] = cr32(&r);
            p->damage_generation[j] = cr32(&r);
        }
        if (r.valid && !compact_player_valid(p, &installed, tick))
            r.valid = false;
    }
    const unsigned object_count
        = BOAT_COMBAT_BULLETS + BOAT_COMBAT_GRENADES + BOAT_COMBAT_TORPEDOES;
    for (unsigned i = 0; i < object_count; ++i) {
        if (!(object_bits[i / 8] & (1u << (i % 8))))
            continue;
        BoatCombatObject *o = i < BOAT_COMBAT_BULLETS ? &decoded.bullets[i]
            : i < BOAT_COMBAT_BULLETS + BOAT_COMBAT_GRENADES
            ? &decoded.grenades[i - BOAT_COMBAT_BULLETS]
            : &decoded.torpedoes[i - BOAT_COMBAT_BULLETS - BOAT_COMBAT_GRENADES];
        o->position.x = crf(&r);
        o->position.y = crf(&r);
        o->direction.x = crf(&r);
        o->direction.y = crf(&r);
        o->height = crf(&r);
        o->born = cr_age(&r, tick);
        uint16_t ended_age = cr16(&r);
        o->ended = ended_age ? tick - (ended_age - 1) : 0;
        o->generation = cr32(&r);
        o->life = cr32(&r);
        o->owner = cr8(&r);
        o->active = o->ended == 0;
        if (r.valid
            && (o->owner >= BOAT_COMBAT_PLAYERS || o->born > tick || o->ended > tick
                || (o->active && fabsf(Vector2LengthSqr(o->direction) - 1) > .01f)
                || fabsf(o->position.x) > BOAT_COMBAT_WORLD_LIMIT
                || fabsf(o->position.y) > BOAT_COMBAT_WORLD_LIMIT || fabsf(o->height) > 1000))
            r.valid = false;
    }
    for (unsigned i = 0; i < BOAT_COMBAT_WAVES; ++i) {
        if (!(wave_bits & (1u << i)))
            continue;
        BoatRadialWave *wave = &decoded.waves[i];
        wave->centre.x = crf(&r);
        wave->centre.y = crf(&r);
        wave->born = cr_age(&r, tick);
        wave->generation = cr32(&r);
        wave->life = cr32(&r);
        wave->owner = cr8(&r);
        wave->height = crf(&r);
        wave->speed = crf(&r);
        wave->range = crf(&r);
        wave->active = true;
        if (r.valid
            && (wave->owner >= BOAT_COMBAT_PLAYERS || wave->born > tick
                || fabsf(wave->centre.x) > BOAT_COMBAT_WORLD_LIMIT
                || fabsf(wave->centre.y) > BOAT_COMBAT_WORLD_LIMIT || wave->height <= 0
                || wave->speed <= 0 || wave->range <= 0))
            r.valid = false;
    }
    if (!r.valid || r.at != size)
        return false;
    *combat = decoded;
    return true;
}
