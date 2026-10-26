#include "boat_combat.h"
#include "boat_fleet.h"
#include "boat_match.h"
#include "boat_net_protocol.h"
#include "raymath.h"

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
    advance(120);
    assert(launches == 16 && f.state.combat.players[0].overheated);
    advance(120);
    assert(launches == 16 && f.state.combat.players[0].heat == 1);
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
    puts("combat: grace, 8 Hz, two-second overheat, release cooling and bullet capacity");
}
static void grenades(void)
{
    setup();
    f.state.combat.rules.grace_ticks = 0;
    inputs[0].grenade = 1;
    advance(1);
    assert(f.state.combat.grenades[0].active);
    uint64_t cooldown = f.state.combat.players[0].next_grenade;
    advance(179);
    assert(!f.state.combat.waves[0].active);
    advance(1);
    assert(f.state.combat.waves[0].active);
    assert(f.state.combat.waves[0].centre.y == -24);
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
static void shot(unsigned pool, unsigned owner, Vector2 start, Vector2 dir, float height)
{
    f.state.combat.bullets[pool] = (BoatCombatObject) { .position = start,
        .direction = dir,
        .height = height,
        .born = f.state.tick,
        .owner = owner,
        .life = f.state.slots[owner].life,
        .active = true };
}
static void hits(void)
{
    target();
    Boat before[4];
    for (unsigned i = 0; i < 4; ++i)
        before[i] = f.state.slots[i].boat;
    shot(0, 0, (Vector2) { 0, -7.7f }, (Vector2) { 0, -1 }, .65f);
    boat_combat_hits(&f, before);
    assert(f.state.combat.players[1].health == 92);
    assert(!f.state.combat.bullets[0].active && f.state.combat.players[0].hit_serial == 1);
    // A target crossing a stationary projectile's path is swept in relative motion.
    target();
    before[1] = f.state.slots[1].boat;
    before[1].position.x = -4;
    f.state.slots[1].boat.position.x = 4;
    shot(0, 0, (Vector2) { 0, -10 }, (Vector2) { 0, -1 }, .65f);
    boat_combat_hits(&f, before);
    assert(f.state.combat.players[1].health == 92);
    target();
    for (unsigned i = 0; i < 4; ++i)
        before[i] = f.state.slots[i].boat;
    // Water blocks a low shot before the hull; grazing scenery never hits through it.
    shot(0, 0, (Vector2) { 0, -7.7f }, (Vector2) { 0, -1 }, -.1f);
    boat_combat_hits(&f, before);
    assert(f.state.combat.players[1].health == 100);
    shot(0, 0, (Vector2) { 0, -7.7f }, (Vector2) { 0, -1 }, .65f);
    f.state.combat.players[1].life_start = 301;
    boat_combat_hits(&f, before);
    assert(f.state.combat.players[1].health == 100);
    f.state.combat.players[1].life_start = 300;
    f.state.tick = 301;
    boat_combat_hits(&f, before);
    assert(f.state.combat.players[1].health == 100);
    // Two bullets on the same tick score one defeat, lethal shooter wins.
    target();
    for (unsigned i = 0; i < 4; ++i)
        before[i] = f.state.slots[i].boat;
    f.state.combat.players[1].health = 8;
    shot(0, 0, (Vector2) { 0, -7.7f }, (Vector2) { 0, -1 }, .65f);
    shot(1, 2, (Vector2) { 0, -7.7f }, (Vector2) { 0, -1 }, .65f);
    boat_combat_hits(&f, before);
    assert(f.state.combat.players[1].health == 0 && f.state.combat.players[1].killer == 0);
    assert(f.state.slots[1].boat.motion_state == BOAT_CAPSIZED);
    BoatMatch m;
    BoatMatchRules r = boat_match_default_rules();
    BoatMatchOutput out;
    assert(boat_match_init(&m, &r));
    m.phase = BOAT_MATCH_PLAYING;
    m.round = 1;
    m.phase_end = 10000;
    boat_match_observe(&m, &f, &out);
    assert(m.rows[0].score == 1 && m.rows[1].capsized == 1 && out.count == 1
        && out.events[0].cause == BOAT_MATCH_GUN);
    boat_match_observe(&m, &f, &out);
    assert(!out.count && m.rows[0].score == 1);
    f.state.tick += 360;
    boat_match_observe(&m, &f, &out);
    assert(out.spawn[1]);
    Boat b;
    boat_reset(&b);
    boat_fleet_place(&f, 1, b);
    assert(f.state.combat.players[1].health == 100 && !f.state.combat.players[1].heat);
    puts("combat: moving sweeps, water occlusion, life fences, lethal tie, one defeat and respawn");
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
    // Simultaneous reciprocal lethal hits each receive one authority-owned kill.
    target();
    f.state.combat.players[0].health = f.state.combat.players[1].health = 8;
    f.state.combat.rules.grace_ticks = 0;
    Boat before[4];
    for (unsigned i = 0; i < 4; ++i)
        before[i] = f.state.slots[i].boat;
    shot(0, 0, (Vector2) { 0, -7.7f }, (Vector2) { 0, -1 }, .65f);
    shot(1, 1, (Vector2) { 0, -2.8f }, (Vector2) { 0, 1 }, .65f);
    boat_combat_hits(&f, before);
    assert(boat_match_init(&m, &r));
    m.phase = BOAT_MATCH_PLAYING;
    m.round = 1;
    m.phase_end = 10000;
    boat_match_observe(&m, &f, &out);
    assert(out.count == 2 && m.rows[0].caused == 1 && m.rows[1].caused == 1);
    assert(out.events[0].reason == BOAT_MATCH_DEFEAT_HULL);
    puts("combat: grenade attribution and simultaneous reciprocal hull defeats");
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
    inputs[0] = (BoatInput) { .gun = true, .grenade = 1 };
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
                for (unsigned k = 0; k < 64; ++k)
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
            mode ? "64 bullets + four waves" : "disabled", sum / 600, cost[569], cost[593]);
    }
}

int main(void)
{
    assert(boat_world_resources_init(&resources, boat_location(BOAT_LOCATION_BASIN)));
    gun();
    grenades();
    hits();
    attribution();
    wave();
    codec();
    benchmark();
    boat_world_resources_destroy(&resources);
    puts("boat_combat=ok");
    return 0;
}
