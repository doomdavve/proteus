#include "boat_combat.h"
#include "boat_fleet.h"
#include "boat_match.h"
#include "boat_net_protocol.h"
#include "raymath.h"

// Validator assertions include setup/tick calls and must run in Release as well.
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static BoatWorldResources resources;
static BoatFleet f;
static BoatInput inputs[BOAT_FLEET_SLOTS];
static unsigned launches;

static void setup(void)
{
    assert(boat_fleet_init(&f, &resources, (BoatWater) { 0 }, BOAT_OBJECT_LIFE_TICKS, 1.8f));
    boat_combat_enable(&f);
    Boat b;
    boat_reset(&b);
    b.position = (Vector2) { 0, 0 };
    boat_fleet_place(&f, 0, b);
    memset(inputs, 0, sizeof(inputs));
    launches = 0;
}
static void advance(unsigned ticks)
{
    for (unsigned i = 0; i < ticks; ++i) {
        uint64_t next = f.state.combat.players[0].next_fire;
        assert(boat_fleet_step(&f, inputs));
        launches += next != f.state.combat.players[0].next_fire;
    }
}
static unsigned bullets(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < BOAT_COMBAT_BULLETS; ++i)
        n += f.state.combat.bullets[i].born != 0;
    return n;
}
static void gun(void)
{
    setup();
    inputs[0].gun = true;
    advance(239);
    assert(!bullets());
    advance(1);
    assert(bullets() == 1);
    advance(119);
    assert(launches == 8);
    advance(360);
    assert(launches == 32 && f.state.combat.players[0].overheated);
    advance(120);
    assert(launches == 32 && f.state.combat.players[0].heat == 1);
    inputs[0].gun = false;
    advance(253);
    assert(!f.state.combat.players[0].overheated);
    advance(107);
    assert(f.state.combat.players[0].heat < .0001f);
    setup();
    f.state.combat.rules.grace_ticks = 0;
    for (unsigned i = 0; i < BOAT_COMBAT_BULLETS; ++i)
        f.state.combat.bullets[i] = (BoatCombatObject) {
            .active = true, .born = 1, .height = 20, .direction = { 0, -1 }
        };
    f.state.tick = f.state.wakes.tick = 1;
    inputs[0].gun = true;
    advance(1);
    assert(f.state.combat.players[0].next_fire == 0);
    puts("combat: grace, 8 Hz, four-second overheat, release cooling and bullet capacity");
}
static void grenades(void)
{
    setup();
    f.state.combat.rules.grace_ticks = 0;
    inputs[0].grenade = 1;
    advance(1);
    assert(f.state.combat.grenades[0].active);
    uint64_t cooldown = f.state.combat.players[0].next_grenade;
    advance(119);
    assert(!f.state.combat.waves[0].active);
    advance(1);
    assert(f.state.combat.waves[0].active);
    assert(f.state.combat.waves[0].centre.y == 7);
    // Duplicate requests stay consumed after cooldown and prediction save/restore.
    BoatFleetState saved;
    boat_fleet_save(&f, &saved);
    advance(1000);
    uint64_t hash = boat_fleet_hash(&f.state);
    boat_fleet_restore(&f, &saved);
    advance(1000);
    assert(boat_fleet_hash(&f.state) == hash && f.state.combat.players[0].next_grenade == cooldown);
    inputs[0].grenade = 2;
    advance(1);
    assert(f.state.combat.players[0].next_grenade > cooldown);
    setup();
    f.state.combat.rules.grace_ticks = 0;
    for (unsigned i = 0; i < BOAT_COMBAT_WAVES; ++i)
        f.state.combat.waves[i]
            = (BoatRadialWave) { .active = true, .height = 2, .speed = 10, .range = 30 };
    inputs[0].grenade = 1;
    advance(1);
    assert(!f.state.combat.players[0].next_grenade);
    boat_combat_clear(&f);
    advance(1);
    assert(!f.state.combat.players[0].next_grenade);
    inputs[0].grenade = 2;
    advance(1);
    assert(f.state.combat.players[0].next_grenade);
    boat_fleet_leave(&f, 0);
    for (unsigned i = 0; i < BOAT_COMBAT_GRENADES; ++i)
        assert(!f.state.combat.grenades[i].active);
    puts(
        "combat: one numbered throw, fuse, pool reservations, replay, departure and round cleanup");
}
static void target(void)
{
    setup();
    f.state.tick = f.state.wakes.tick = 300;
    Boat b;
    boat_reset(&b);
    b.position = (Vector2) { 0, -10 };
    boat_fleet_place(&f, 1, b);
    f.state.combat.players[1].life_start = 0;
}
static void copy_boats(Boat before[4])
{
    for (unsigned i = 0; i < 4; ++i)
        before[i] = f.state.slots[i].boat;
}
static void shot(unsigned pool, unsigned owner, Vector2 start, Vector2 dir)
{
    f.state.combat.torpedoes[pool] = (BoatCombatObject) { .position = start,
        .direction = dir,
        .height = .2f,
        .born = f.state.tick - f.state.combat.rules.arming_ticks,
        .owner = owner,
        .life = f.state.slots[owner].life,
        .active = true };
}
static void hits(void)
{
    target();
    Boat before[4];
    copy_boats(before);
    shot(0, 0, (Vector2) { 0, -8 }, (Vector2) { 0, -1 });
    boat_combat_hits(&f, before);
    assert(f.state.combat.players[1].health == 60);
    assert(f.state.combat.players[0].damage[1] == 40);
    assert(!f.state.combat.torpedoes[0].active && f.state.combat.players[0].hit_serial == 1);
    // Damage feedback merges close hits and reports actual damage, including overkill.
    shot(0, 0, (Vector2) { 0, -8 }, (Vector2) { 0, -1 });
    boat_combat_hits(&f, before);
    assert(f.state.combat.players[1].health == 20 && f.state.combat.players[0].damage[1] == 80);
    shot(0, 0, (Vector2) { 0, -8 }, (Vector2) { 0, -1 });
    shot(1, 2, (Vector2) { 0, -8 }, (Vector2) { 0, -1 });
    boat_combat_hits(&f, before);
    assert(f.state.combat.players[1].health == 0 && f.state.combat.players[1].killer == 0);
    assert(f.state.combat.players[0].damage[1] == 100);
    BoatMatch m;
    BoatMatchRules r = boat_match_default_rules();
    BoatMatchOutput out;
    assert(boat_match_init(&m, &r));
    m.phase = BOAT_MATCH_PLAYING;
    m.round = 1;
    m.phase_end = 10000;
    boat_match_observe(&m, &f, &out);
    assert(m.rows[0].score == 1 && m.rows[1].capsized == 1 && out.count == 1
        && out.events[0].cause == BOAT_MATCH_TORPEDO);
    boat_match_observe(&m, &f, &out);
    assert(!out.count && m.rows[0].score == 1);
    f.state.tick += 360;
    boat_match_observe(&m, &f, &out);
    assert(out.spawn[1]);
    Boat b;
    boat_reset(&b);
    boat_fleet_place(&f, 1, b);
    assert(f.state.combat.players[1].health == 100 && f.state.combat.players[1].ammo == 3);
    // A target's replacement life resets its damage burst, even inside the merge interval.
    b.position = (Vector2) { 0, -10 };
    boat_fleet_place(&f, 1, b);
    f.state.combat.rules.grace_ticks = 0;
    copy_boats(before);
    shot(0, 0, (Vector2) { 0, -8 }, (Vector2) { 0, -1 });
    f.state.combat.torpedoes[0].born = f.state.tick;
    f.state.combat.rules.arming_ticks = 0;
    boat_combat_hits(&f, before);
    assert(f.state.combat.players[0].damage[1] == 40);
    assert(f.state.combat.players[0].damage_life[1] == f.state.slots[1].life);
    // Moving hull sweeps, birth fences, spawn grace and unarmed contact.
    target();
    copy_boats(before);
    before[1].position.x = -4;
    f.state.slots[1].boat.position.x = 4;
    shot(0, 0, (Vector2) { 0, -10 }, (Vector2) { 0, -1 });
    boat_combat_hits(&f, before);
    assert(f.state.combat.players[1].health == 60);
    target();
    copy_boats(before);
    shot(0, 0, (Vector2) { 0, -8 }, (Vector2) { 0, -1 });
    f.state.combat.players[1].life_start = 280;
    boat_combat_hits(&f, before);
    assert(f.state.combat.players[1].health == 100);
    f.state.combat.players[1].life_start = 0;
    f.state.combat.torpedoes[0].born = f.state.tick;
    boat_combat_hits(&f, before);
    assert(f.state.combat.players[1].health == 100 && !f.state.combat.torpedoes[0].active);
    // Torpedoes stop at basin walls even during their arming interval.
    target();
    copy_boats(before);
    shot(0, 0, (Vector2) { f.world.definition->half_extent.x - .01f, 0 }, (Vector2) { 1, 0 });
    boat_combat_hits(&f, before);
    assert(!f.state.combat.torpedoes[0].active);
    puts(
        "combat: armed torpedoes, swept hull/scenery, life fence, actual damage and single defeat");
}
static void cannon(void)
{
    target();
    Boat old[4];
    copy_boats(old);
    f.state.combat.bullets[0] = (BoatCombatObject) { .active = true,
        .position = { 0, -8 },
        .direction = { 0, -1 },
        .height = -.1f,
        .born = f.state.tick,
        .owner = 0 };
    boat_combat_hits(&f, old);
    assert(!f.state.combat.bullets[0].active && !f.state.combat.players[1].pressure);
    target();
    f.state.combat.rules.grace_ticks = 0;
    Boat before[4];
    copy_boats(before);
    for (unsigned n = 0; n < 40; ++n) {
        Boat *victim = &f.state.slots[1].boat;
        f.state.combat.bullets[0]
            = (BoatCombatObject) { .position = { victim->position.x - 1, victim->position.y },
                  .direction = { 1, 0 },
                  .height = victim->height + .65f,
                  .born = f.state.tick,
                  .owner = 0,
                  .active = true };
        boat_combat_hits(&f, before);
        assert(f.state.combat.players[1].health == 100);
        if (n == 0) {
            assert(victim->motion_state != BOAT_CAPSIZED);
            assert(f.state.combat.players[1].pressure > 0 && fabsf(victim->roll) > 0);
            assert(f.state.combat.players[0].damage[1] == 0);
        }
        if (victim->motion_state == BOAT_CAPSIZED)
            break;
        // Virtual cadence: recovery between packets, with the hull held in the stream.
        for (unsigned t = 0; t < 15; ++t) {
            ++f.state.tick;
            boat_combat_advance(&f, inputs);
        }
    }
    assert(f.state.slots[1].boat.motion_state == BOAT_CAPSIZED);
    BoatMatch m;
    BoatMatchRules r = boat_match_default_rules();
    r.grace_ticks = 0;
    BoatMatchOutput out;
    assert(boat_match_init(&m, &r));
    m.phase = BOAT_MATCH_PLAYING;
    m.phase_end = 10000;
    boat_match_observe(&m, &f, &out);
    assert(out.count == 1 && out.events[0].cause == BOAT_MATCH_CANNON && m.rows[0].score == 1);
    // Production ticks: both nearby and distant broadside targets capsize within one tank.
    for (unsigned distance = 10; distance <= 45; distance += 35) {
        target();
        f.state.combat.rules.grace_ticks = 0;
        f.state.slots[0].boat.heading = PI / 2;
        f.state.slots[1].boat.position = (Vector2) { (float)distance, 0 };
        inputs[0].gun = true;
        for (unsigned t = 0; t < 480 && f.state.slots[1].boat.motion_state != BOAT_CAPSIZED; ++t)
            advance(1);
        assert(f.state.slots[1].boat.motion_state == BOAT_CAPSIZED);
    }
    target();
    f.state.combat.players[1].pressure = .7f;
    advance(400);
    assert(f.state.combat.players[1].pressure == 0);
    puts("combat: progressive cannon heel, capsize credit, recovery, no hull damage");
}
static void cannon_arc(void)
{
    setup();
    f.state.combat.rules.grace_ticks = 0;
    inputs[0].gun = true;
    advance(1);
    inputs[0].gun = false;
    BoatCombatObject *b = &f.state.combat.bullets[0];
    float speed = f.state.combat.rules.bullet_speed;
    assert(fabsf(b->position.x + BOAT_CANNON_MUZZLE_SIDE) < 1e-6f);
    assert(fabsf(b->height - (BOAT_CANNON_MUZZLE_HEIGHT + boat_combat_cannon_rise(BOAT_STEP)))
        < 1e-5f);
    advance(21);
    float peak = b->height;
    assert(b->active && peak > 1.18f && peak < 1.23f);
    advance(15);
    assert(b->active && b->height < peak);
    assert(fabsf(b->height - (BOAT_CANNON_MUZZLE_HEIGHT + boat_combat_cannon_rise(37 * BOAT_STEP)))
        < 1e-5f);
    assert(fabsf(b->position.y - (-BOAT_CANNON_MUZZLE_FORWARD - speed * 37 * BOAT_STEP)) < 1e-4f);
    BoatFleetState saved;
    boat_fleet_save(&f, &saved);
    advance(60);
    uint64_t hash = boat_fleet_hash(&f.state);
    assert(!b->active && b->ended && b->height <= BOAT_CANNON_RADIUS + .02f);
    assert(b->position.y > -55 && b->position.y < -50); // Water contact before the range limit.
    boat_fleet_restore(&f, &saved);
    advance(60);
    assert(boat_fleet_hash(&f.state) == hash);
    // A tilted moving boat launches from the same full-pose attachment used by the housing.
    setup();
    f.state.combat.rules.grace_ticks = 0;
    Boat *boat = &f.state.slots[0].boat;
    boat->position = (Vector2) { 4, 5 };
    boat->heading = .7f;
    boat->pitch = .15f;
    boat->roll = -.2f;
    boat->height = .3f;
    Vector3 muzzle = boat_combat_cannon_muzzle(boat);
    boat_combat_advance(&f, (BoatInput[4]) { { .gun = true } });
    b = &f.state.combat.bullets[0];
    assert(fabsf(b->position.x + muzzle.x) < 1e-6f);
    assert(fabsf(b->position.y - muzzle.z) < 1e-6f && fabsf(b->height - muzzle.y) < 1e-6f);
    assert(fabsf(b->height - (boat->height + BOAT_CANNON_MUZZLE_HEIGHT)) > .01f);
    // A descending cube crosses the hull's top within one tick; an overhead cube misses.
    target();
    Boat before[4];
    copy_boats(before);
    f.state.combat.bullets[0] = (BoatCombatObject) { .active = true,
        .position = { 0, -10 },
        .direction = { 0, -1 },
        .height = 1.59f,
        .born = f.state.tick - 72,
        .owner = 0 };
    boat_combat_hits(&f, before);
    assert(f.state.combat.players[1].pressure > 0);
    target();
    copy_boats(before);
    f.state.combat.bullets[0] = (BoatCombatObject) { .active = true,
        .position = { 0, -10 },
        .direction = { 0, -1 },
        .height = 1.8f,
        .born = f.state.tick,
        .owner = 0 };
    boat_combat_hits(&f, before);
    assert(f.state.combat.bullets[0].active && !f.state.combat.players[1].pressure);
    puts("combat: cannon parabola, ascending/descending height sweeps, water landing and replay");
}

static void torpedoes(void)
{
    setup();
    inputs[0].torpedo = 1;
    advance(300);
    assert(f.state.combat.players[0].ammo == 3); // Grace-period request was consumed.
    inputs[0].torpedo = 2;
    f.state.combat.firing = false;
    advance(1);
    assert(f.state.combat.players[0].ammo == 3); // Intermission blocks launches.
    setup();
    f.state.combat.rules.grace_ticks = 0;
    for (unsigned k = 0; k < BOAT_COMBAT_TORPEDOES; ++k)
        f.state.combat.torpedoes[k] = (BoatCombatObject) {
            .active = true, .born = 1, .height = .2f, .direction = { 0, -1 }
        };
    f.state.tick = f.state.wakes.tick = 1;
    inputs[0].torpedo = 1;
    advance(1);
    assert(f.state.combat.players[0].ammo == 3 && !f.state.combat.players[0].next_torpedo);
    boat_combat_clear(&f);
    advance(1);
    assert(f.state.combat.players[0].ammo == 3); // Pool rejection remains consumed.
    setup();
    f.state.combat.rules.grace_ticks = 0;
    inputs[0].torpedo = 255;
    advance(1);
    assert(f.state.combat.players[0].ammo == 2 && f.state.combat.torpedoes[0].active);
    advance(200);
    assert(f.state.combat.players[0].ammo == 2); // A repeated request never relaunches.
    inputs[0].torpedo = 1;
    advance(1);
    assert(f.state.combat.players[0].ammo == 1);
    inputs[0].torpedo = 2;
    advance(1); // Rejected during reload, stays consumed.
    advance(200);
    assert(f.state.combat.players[0].ammo == 1);
    inputs[0].torpedo = 3;
    advance(1);
    assert(f.state.combat.players[0].ammo == 0);
    advance(200);
    inputs[0].torpedo = 4;
    advance(1);
    assert(f.state.combat.players[0].ammo == 0);
    boat_fleet_leave(&f, 0);
    for (unsigned i = 0; i < BOAT_COMBAT_TORPEDOES; ++i)
        assert(!f.state.combat.torpedoes[i].active);
    puts("combat: three torpedoes per life, serial wrap, cooldown, duplicate and departure");
}
static void attribution(void)
{
    target();
    f.state.combat.rules.grace_ticks = 0;
    BoatMatch m;
    BoatMatchRules r = boat_match_default_rules();
    r.grace_ticks = 0;
    BoatMatchOutput out;
    assert(boat_match_init(&m, &r));
    m.phase = BOAT_MATCH_PLAYING;
    m.round = 1;
    m.phase_end = 10000;
    boat_match_observe(&m, &f, &out);
    f.state.combat.waves[0] = (BoatRadialWave) { .active = true,
        .born = f.state.tick,
        .centre = f.state.slots[1].boat.position,
        .height = 2,
        .speed = 10,
        .range = 30,
        .owner = 0 };
    f.state.slots[1].boat.motion_state = BOAT_CAPSIZED;
    boat_match_observe(&m, &f, &out);
    assert(out.count == 1 && out.events[0].cause == BOAT_MATCH_GRENADE && m.rows[0].score == 1);
    puts("combat: grenade-wave attribution");
}

static void wave(void)
{
    BoatRadialWave w = { .active = true, .height = 2, .speed = 10, .range = 30 };
    Vector2 p = { 8.5f, 0 };
    double t = .8, dt = 1e-4;
    BoatStormSample a = boat_water_radial_query(w, p, t);
    float dx = (boat_water_radial_query(w, (Vector2) { p.x + .001f, 0 }, t).height
                   - boat_water_radial_query(w, (Vector2) { p.x - .001f, 0 }, t).height)
        / .002f;
    float dy = (boat_water_radial_query(w, p, t + dt).height
                   - boat_water_radial_query(w, p, t - dt).height)
        / (float)(2 * dt);
    assert(fabsf(a.gradient.x - dx) < .001f && fabsf(a.vertical_velocity - dy) < .002f);
    assert(!boat_water_radial_query(w, (Vector2) { 30, 0 }, 3).height);
    setup();
    f.state.combat.rules.grace_ticks = 0;
    f.state.combat.waves[0] = (BoatRadialWave) {
        .active = true, .centre = { 0, 5 }, .height = 2, .speed = 10, .range = 30
    };
    advance(120);
    assert(f.state.slots[0].boat.height > .3f || f.state.slots[0].boat.landings > 0);
    puts("combat: analytic radial derivatives, finite support and physical self-wave");
}
static void codec(void)
{
    setup();
    f.state.combat.rules.grace_ticks = 0;
    inputs[0] = (BoatInput) { .gun = true, .grenade = 1, .torpedo = 1 };
    advance(200);
    uint8_t data[BOAT_NET_MAX_CHECKPOINT];
    BoatNetSlotInput records[4] = { 0 }, decoded[4];
    BoatFleetState state;
    size_t size = boat_net_checkpoint_encode(&f.state, records, 0, data, sizeof(data));
    assert(size);
    assert(boat_net_checkpoint_decode(data, size, &state, decoded, NULL) == BOAT_NET_OK);
    assert(boat_fleet_hash(&state) == boat_fleet_hash(&f.state));
    uint8_t combat[BOAT_COMBAT_BYTES];
    boat_combat_encode(&f.state.combat, combat);
    for (unsigned i = 0; i < 4; ++i) {
        BoatNetMessage m = { .header = { .type = BOAT_NET_COMBAT_PAGE, .tick = f.state.tick },
            .world_page = { .checkpoint = (uint32_t)f.state.tick,
                .index = (uint16_t)i,
                .count = 4,
                .size = (uint16_t)(i == 3 ? BOAT_COMBAT_BYTES - 3072 : 1024) } };
        memcpy(m.world_page.data, combat + 1024 * i, m.world_page.size);
        uint8_t wire[BOAT_NET_MAX_MESSAGE];
        size_t bytes = boat_net_encode(&m, wire);
        assert(bytes && bytes <= 1100);
        BoatNetMessage back;
        assert(boat_net_decode(wire, bytes, &back) == BOAT_NET_OK);
        wire[38] = 0;
        wire[39] = 5;
        assert(boat_net_decode(wire, bytes, &back) != BOAT_NET_OK);
    }
    combat[0] = 255;
    assert(!boat_combat_decode(&state.combat, combat, f.state.tick));
    puts("combat: full checkpoint, hash, bounded pages and malformed-state rejection");
}
static int compare_cost(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static void benchmark(void)
{
    for (unsigned mode = 0; mode < 2; ++mode) {
        setup();
        if (!mode)
            f.state.combat.enabled = false;
        for (unsigned i = 0; i < 4; ++i) {
            Boat b;
            boat_reset(&b);
            b.position = (Vector2) { (float)i * 15 - 20, 20 };
            boat_fleet_place(&f, i, b);
            boat_fleet_hold(&f, i, true);
        }
        double cost[600], sum = 0;
        for (unsigned t = 0; t < 600; ++t) {
            if (mode) {
                for (unsigned k = 0; k < BOAT_COMBAT_BULLETS; ++k)
                    f.state.combat.bullets[k] = (BoatCombatObject) { .active = true,
                        .born = f.state.tick,
                        .height = 8,
                        .direction = { 0, -1 },
                        .position = { (float)(k % 8) * 4 - 16, 0 },
                        .owner = k % 4 };
                for (unsigned k = 0; k < 4; ++k)
                    f.state.combat.waves[k] = (BoatRadialWave) { .active = true,
                        .born = f.state.tick > 60 ? f.state.tick - 60 : 0,
                        .height = 2,
                        .speed = 10,
                        .range = 30,
                        .centre = { (float)k * 15 - 20, 15 },
                        .owner = k };
            }
            clock_t start = clock();
            assert(boat_fleet_step(&f, inputs));
            cost[t] = (double)(clock() - start) * 1000 / CLOCKS_PER_SEC;
            sum += cost[t];
        }
        qsort(cost, 600, sizeof(*cost), compare_cost);
        printf("combat cost (%s, four held boats): mean %.3f ms p95 %.3f ms p99 %.3f ms/tick\n",
            mode ? "40 packets + four waves" : "disabled", sum / 600, cost[569], cost[593]);
    }
}

int main(void)
{
    assert(boat_world_resources_init(&resources, boat_location(BOAT_LOCATION_BASIN)));
    gun();
    grenades();
    hits();
    cannon();
    cannon_arc();
    torpedoes();
    attribution();
    wave();
    codec();
    benchmark();
    boat_world_resources_destroy(&resources);
    puts("boat_combat=ok");
    return 0;
}
