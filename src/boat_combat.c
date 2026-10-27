#include "boat_combat.h"
#include "boat_fleet.h"
#include "boat_pose.h"
#include "raymath.h"
#include "waterfront_harbor.h"
#include "waterfront_traffic.h"

#include <assert.h>
#include <math.h>
#include <string.h>

BoatCombatRules boat_combat_default_rules(void)
{
    return (BoatCombatRules) { .health = 100,
        .damage = 0,
        .bullet_speed = 94,
        .bullet_range = 70,
        .grenade_distance = 7,
        .wave_height = 2,
        .wave_speed = 10,
        .wave_range = 30,
        .cannon_pressure = .1f,
        .pressure_recovery = .22f,
        .torpedo_damage = 40,
        .torpedo_speed = 32,
        .torpedo_range = 160,
        .torpedo_ticks = 180,
        .arming_ticks = 30,
        .torpedo_ammo = 3,
        .fire_ticks = 15,
        .heat_ticks = 480,
        .cool_ticks = 360,
        .grenade_ticks = 960,
        .splash_ticks = 36,
        .fuse_ticks = 120,
        .grace_ticks = 240 };
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
    for (unsigned i = 0; i < BOAT_COMBAT_TORPEDOES; ++i)
        c->torpedoes[i].active = false;
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
            c->torpedoes[i].active = false;
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
