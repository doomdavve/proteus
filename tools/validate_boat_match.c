/* Capsize deathmatch rules (docs/DEATHMATCH_PLAN.md). Links only proteus_boat_core. Synthetic
 * cases drive boat_match_observe over a hand-set fleet (ticks, lives, contact impulses and
 * capsize latches) for the credit window, self-capsize, grace, respawn, bailout, leavers and
 * rounds; physical cases step a real fleet in which a planted wake record capsizes a boat. */
#include "boat_match.h"
#include "boat_test_world.h"

#include <stdio.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static BoatFleet fleet;
static BoatMatch match;
static BoatMatchOutput out;

static BoatMatchRules rules(void)
{
    BoatMatchRules r = boat_match_default_rules();
    r.round_ticks = 60 * BOAT_TICK_HZ;
    return r;
}

static Boat boat_at(float x)
{
    Boat boat;
    boat_reset(&boat);
    boat.position = (Vector2) { x, 0 };
    return boat;
}

// A fleet of COUNT placed boats at tick 1 and a match that has observed it once.
static int start(unsigned count, BoatMatchRules r)
{
    CHECK(boat_fleet_init(
        &fleet, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 }, BOAT_OBJECT_LIFE_TICKS, 1.8f));
    CHECK(boat_match_init(&match, &r));
    for (unsigned i = 0; i < count; ++i)
        boat_fleet_place(&fleet, i, boat_at(-30 + 20 * (float)i));
    fleet.state.tick = fleet.state.wakes.tick = 1;
    boat_match_observe(&match, &fleet, &out);
    return 0;
}

// Advance the hand-set fleet to TICK with no contacts and observe once.
static void at(uint64_t tick)
{
    fleet.state.tick = tick;
    fleet.stats = (BoatFleetStats) { 0 };
    boat_match_observe(&match, &fleet, &out);
}

static void hit(uint64_t tick, unsigned pair, float impulse)
{
    fleet.state.tick = tick;
    fleet.stats = (BoatFleetStats) { 0 };
    fleet.stats.pair_impulse[pair] = impulse;
    boat_match_observe(&match, &fleet, &out);
}

static void capsize(unsigned slot) { fleet.state.slots[slot].boat.motion_state = BOAT_CAPSIZED; }

// A fresh life for SLOT, as the fleet's spawn would place it.
static void respawn(unsigned slot) { boat_fleet_place(&fleet, slot, boat_at((float)slot * 20)); }

static const BoatMatchEvent *only_capsize(void)
{
    const BoatMatchEvent *found = NULL;
    for (unsigned i = 0; i < out.count; ++i)
        if (out.events[i].kind == BOAT_MATCH_CAPSIZE) {
            if (found)
                return NULL;
            found = &out.events[i];
        }
    return found;
}

static int warmup_and_rounds(void)
{
    BoatMatchRules r = rules();
    CHECK(start(1, r) == 0);
    CHECK(match.phase == BOAT_MATCH_WARMUP && out.count == 0 && match.round == 0);
    // A solo capsize in warmup is neither scored nor reported, but still respawns.
    capsize(0);
    at(100);
    CHECK(out.count == 0 && match.rows[0].score == 0 && match.slots[0].out);
    at(100 + r.respawn_ticks);
    CHECK(out.spawn[0]);
    respawn(0);
    // A second player starts the round and respawns everyone.
    boat_fleet_place(&fleet, 1, boat_at(20));
    at(1000);
    CHECK(match.phase == BOAT_MATCH_PLAYING && match.round == 1 && out.count == 1);
    CHECK(out.events[0].kind == BOAT_MATCH_ROUND_START && out.spawn[0] && out.spawn[1]);
    CHECK(match.phase_end == 1000 + r.round_ticks);
    match.rows[0].score = 3; // Rows at round end carry into the intermission scoreboard.
    match.rows[1].score = 3;
    CHECK(boat_match_rank(&match, 0) == 1 && boat_match_rank(&match, 1) == 1); // Shared lead.
    at(1000 + r.round_ticks - 1);
    CHECK(match.phase == BOAT_MATCH_PLAYING);
    at(1000 + r.round_ticks);
    CHECK(match.phase == BOAT_MATCH_INTERMISSION && out.count == 1);
    CHECK(out.events[0].kind == BOAT_MATCH_ROUND_END && match.rows[0].score == 3);
    // Capsizes during the intermission do not score.
    respawn(0);
    respawn(1);
    at(1000 + r.round_ticks + 1);
    capsize(1);
    at(1000 + r.round_ticks + 2);
    CHECK(only_capsize() == NULL && match.rows[1].score == 3);
    at(1000 + r.round_ticks + r.intermission_ticks);
    CHECK(match.phase == BOAT_MATCH_PLAYING && match.round == 2);
    CHECK(match.rows[0].score == 0 && match.rows[1].score == 0);
    // Below the minimum, a round is abandoned back to warmup.
    boat_fleet_leave(&fleet, 1);
    at(20000);
    CHECK(match.phase == BOAT_MATCH_WARMUP && out.count == 1);
    CHECK(out.events[0].kind == BOAT_MATCH_ROUND_END);
    return 0;
}

static int credit(void)
{
    BoatMatchRules r = rules();
    CHECK(start(2, r) == 0);
    CHECK(match.phase == BOAT_MATCH_PLAYING);
    respawn(0);
    respawn(1);
    at(2);
    // A hit inside the grace records nothing: the capsize is a self-capsize.
    hit(2 + r.grace_ticks - 1, 0, 5);
    CHECK(match.slots[1].touch.slot == BOAT_MATCH_NOBODY);
    capsize(1);
    at(400);
    const BoatMatchEvent *e = only_capsize();
    CHECK(e && e->victim == 1 && e->attacker == BOAT_MATCH_NOBODY && !e->bailout);
    CHECK(match.rows[1].score == -1 && match.rows[1].self == 1 && match.rows[1].capsized == 1);
    // Latched once per life.
    at(401);
    CHECK(only_capsize() == NULL && match.rows[1].score == -1);
    // A weak contact is not a touch; a firm one is, both ways, and credits at the window edge.
    respawn(1);
    uint64_t t = 1000;
    at(t);
    hit(t + r.grace_ticks, 0, r.contact_impulse * .5f);
    CHECK(match.slots[1].touch.slot == BOAT_MATCH_NOBODY);
    t += r.grace_ticks + 1;
    hit(t, 0, r.contact_impulse);
    CHECK(match.slots[1].touch.slot == 0 && match.slots[0].touch.slot == 1);
    CHECK(match.slots[1].touch.cause == BOAT_MATCH_CONTACT);
    capsize(1);
    at(t + r.credit_ticks);
    e = only_capsize();
    CHECK(e && e->attacker == 0 && e->cause == BOAT_MATCH_CONTACT && e->victim_life == 3);
    CHECK(match.rows[0].score == 1 && match.rows[0].caused == 1 && match.rows[1].score == -1);
    CHECK(match.rows[1].capsized == 2 && match.rows[1].self == 1);
    // One tick past the window is a self-capsize.
    respawn(1);
    t += r.credit_ticks + 1;
    at(t);
    hit(t + r.grace_ticks, 0, r.contact_impulse);
    capsize(1);
    at(t + r.grace_ticks + r.credit_ticks + 1);
    e = only_capsize();
    CHECK(e && e->attacker == BOAT_MATCH_NOBODY && match.rows[1].score == -2);
    return 0;
}

static int respawns(void)
{
    BoatMatchRules r = rules();
    CHECK(start(2, r) == 0);
    respawn(0);
    respawn(1);
    capsize(1);
    at(500);
    CHECK(only_capsize() && !out.spawn[1]);
    at(500 + r.respawn_ticks - 1);
    CHECK(!out.spawn[1]);
    at(500 + r.respawn_ticks);
    CHECK(out.spawn[1]);
    at(500 + r.respawn_ticks + 1);
    CHECK(!out.spawn[1]); // Requested once.
    // R right after a capsize waits for skip_ticks, then respawns early.
    respawn(1);
    at(2000);
    capsize(1);
    at(3000);
    CHECK(!boat_match_reset(&match, &fleet, 1, &out) && out.count == 0);
    at(3000 + r.skip_ticks - 1);
    CHECK(!out.spawn[1]);
    at(3000 + r.skip_ticks);
    CHECK(out.spawn[1]);
    CHECK(boat_match_reset(&match, &fleet, 1, &out)); // Later repeats pass straight through.
    // R on an untouched upright boat is a free reset.
    respawn(1);
    at(5000);
    CHECK(boat_match_reset(&match, &fleet, 1, &out) && out.count == 0);
    CHECK(match.rows[1].capsized == 2);
    return 0;
}

static int bailout_and_leavers(void)
{
    BoatMatchRules r = rules();
    CHECK(start(3, r) == 0);
    for (unsigned i = 0; i < 3; ++i)
        respawn(i);
    uint64_t t = 1000;
    at(t);
    t += r.grace_ticks;
    hit(t, 1, r.contact_impulse); // Pair 1 is slots 0 and 2.
    CHECK(match.slots[2].touch.slot == 0);
    CHECK(boat_match_reset(&match, &fleet, 2, &out));
    const BoatMatchEvent *e = only_capsize();
    CHECK(e && e->bailout && e->attacker == 0 && e->victim == 2);
    CHECK(match.rows[0].score == 1 && match.rows[2].score == 0 && match.rows[2].capsized == 1);
    // While the bailout spawn is pending, a capsize in the same life is not scored again.
    capsize(2);
    at(t + 1);
    CHECK(only_capsize() == NULL && match.rows[0].score == 1);
    // A leaver's pending credit is dropped, and their row with them.
    respawn(2);
    t += 2000;
    at(t);
    t += r.grace_ticks;
    hit(t, 1, r.contact_impulse);
    boat_fleet_leave(&fleet, 0);
    capsize(2);
    at(t + 10);
    e = only_capsize();
    CHECK(e && e->attacker == BOAT_MATCH_NOBODY && match.rows[2].score == -1);
    CHECK(match.rows[0].score == 0 && !match.slots[0].present);
    // A rejoining player starts at zero.
    boat_fleet_request_spawn(&fleet, 0);
    at(t + 11);
    CHECK(match.slots[0].present && match.rows[0].score == 0 && match.phase == BOAT_MATCH_PLAYING);
    return 0;
}

// Plant one record of EMITTER whose crest stands under POSITION one second after its birth.
static void plant(unsigned emitter, Vector2 position)
{
    BoatWakeObjects *w = &fleet.state.wakes;
    w->objects[emitter * BOAT_OBJECT_PER_BOAT] = (BoatWakeObject) {
        .position = { position.x, position.y - 3.3f },
        .direction = { 1, 0 },
        .length = 4,
        .amplitude = 1.8f,
        .inverse_speed = .01f,
        .born = w->tick - BOAT_TICK_HZ,
    };
    ++w->live;
}

// Step a real fleet with an upside-down boat in slot 1 over a planted wake of EMITTER.
static int physical(unsigned emitter, const BoatMatchEvent **event)
{
    BoatMatchRules r = rules();
    r.grace_ticks = 0; // The inverted boat latches within 0.6 s of its placement.
    CHECK(start(2, r) == 0);
    BoatInput none[BOAT_FLEET_SLOTS] = { 0 };
    for (unsigned tick = 0; tick < 2 * BOAT_TICK_HZ; ++tick) // Room for a planted birth tick.
        CHECK(boat_fleet_step(&fleet, none));
    Boat victim = boat_at(10);
    victim.roll = PI;
    boat_fleet_place(&fleet, 1, victim);
    plant(emitter, victim.position);
    float height
        = boat_wake_objects_query_emitter(&fleet.state.wakes, emitter, victim.position).height;
    CHECK(height > r.wake_height);
    CHECK(boat_wake_objects_query_emitter(&fleet.state.wakes, emitter ^ 1, victim.position).height
        == 0);
    for (unsigned tick = 0; tick < 2 * BOAT_TICK_HZ; ++tick) {
        CHECK(boat_fleet_step(&fleet, none));
        boat_match_observe(&match, &fleet, &out);
        if ((*event = only_capsize()))
            return 0;
    }
    CHECK(!"no capsize");
    return 1;
}

static int wakes(void)
{
    const BoatMatchEvent *e;
    CHECK(physical(0, &e) == 0);
    CHECK(e->victim == 1 && e->attacker == 0 && e->cause == BOAT_MATCH_WAKE);
    CHECK(match.rows[0].score == 1);
    CHECK(physical(1, &e) == 0); // The victim's own wake never counts as a touch.
    CHECK(e->victim == 1 && e->attacker == BOAT_MATCH_NOBODY && match.rows[1].score == -1);
    return 0;
}

static int rejects(void)
{
    BoatMatchRules r = rules();
    r.round_ticks = 0;
    CHECK(!boat_match_init(&match, &r));
    r = rules();
    r.contact_impulse = 0;
    CHECK(!boat_match_init(&match, &r));
    r = rules();
    r.skip_ticks = r.respawn_ticks + 1;
    CHECK(!boat_match_init(&match, &r));
    r = rules();
    r.min_players = BOAT_FLEET_SLOTS + 1;
    CHECK(!boat_match_init(&match, &r));
    return 0;
}

int main(void)
{
    if (warmup_and_rounds() || credit() || respawns() || bailout_and_leavers() || wakes()
        || rejects())
        return 1;
    printf("boat_match=ok\n");
    test_worlds_close();
    return 0;
}
