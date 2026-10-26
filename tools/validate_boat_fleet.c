/* Multiplayer M2: the local four-boat authority. Links only proteus_boat_core. Checks that one
 * fleet boat reproduces the study's single-boat object-wake order byte for byte, that an
 * isolated pair reproduces boat_collide_pair, that checkpoints resume bit-identically, and the
 * contact solver, spawn and wake-pool contracts. Cross-compiler results are compared by
 * invariants and tolerances, never by bitwise identity across platforms. */
#include "boat_fleet.h"
#include "boat_fleet_bots.h"
#include "boat_test_world.h"
#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define YAW_INERTIA 2.2f // boat.c's normalized yaw inertia, for the energy check.

static const BoatWorldResources *basin(void) { return test_world(BOAT_LOCATION_BASIN); }

static bool fleet(BoatFleet *f, BoatWater water)
{
    return boat_fleet_init(f, basin(), water, BOAT_OBJECT_LIFE_TICKS, 1.8f);
}

static Boat boat_at(Vector2 position, float heading, Vector2 velocity)
{
    Boat boat;
    boat_reset(&boat);
    boat.position = position;
    boat.heading = heading;
    boat.velocity = velocity;
    return boat;
}

static BoatInput loop_input(const Boat *boat, unsigned tick)
{
    float throttle = tick < 1440 ? 1 : 0;
    float wheel = tick >= 420 && tick < 1440 ? 1 : 0;
    return (BoatInput) { Clamp((wheel - boat->wheel) / BOAT_STEP, -1, 1),
        Clamp((throttle - boat->throttle) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP), -1, 1), 0,
        0 };
}

static bool track(
    BoatWakeObjects *wakes, unsigned id, const Boat *boat, const BoatWorld *world, double time)
{
    BoatWakeStern s = boat_wake_stern(boat, world, time);
    return boat_wake_objects_track(
        wakes, id, (Vector2) { (float)s.x, (float)s.z }, (float)hypot(s.vx, s.vz), (float)s.wet);
}

// The reference single-world order shared by the study's object-wake mode and wake reviews.
typedef struct {
    BoatWorld world;
    BoatWakeObjects wakes;
    Boat boats[2];
    uint64_t tick;
} Reference;

static bool reference_init(Reference *r, BoatWater water, const Boat *boats, unsigned count)
{
    *r = (Reference) { 0 };
    if (!boat_wake_objects_init(&r->wakes, BOAT_OBJECT_LIFE_TICKS, 1.8f))
        return false;
    water.objects = &r->wakes;
    boat_world_init(&r->world, basin(), water);
    for (unsigned i = 0; i < count; ++i)
        r->boats[i] = boats[i];
    return true;
}

// One boat: advance, step, track. Two boats: both step, boat_collide_pair, then both track.
static bool reference_step(Reference *r, const BoatInput *inputs, unsigned count)
{
    boat_wake_objects_advance(&r->wakes);
    double now = boat_tick_seconds(++r->tick);
    for (unsigned i = 0; i < count; ++i)
        boat_step(&r->boats[i], &r->world, now, inputs[i]);
    if (count == 2)
        boat_collide_pair(&r->boats[0], 1, &r->boats[1], 1);
    bool ok = true;
    for (unsigned i = 0; i < count; ++i)
        ok &= track(&r->wakes, i, &r->boats[i], &r->world, now);
    return ok;
}

static bool same_wakes(const BoatWakeObjects *a, const BoatWakeObjects *b)
{
    BoatFleetState x = { .tick = a->tick, .wakes = *a }, y = { .tick = b->tick, .wakes = *b };
    return boat_fleet_hash(&x) == boat_fleet_hash(&y);
}

static int one_boat_equivalence(void)
{
    const BoatWater waters[]
        = { { 0 }, boat_world_test_water(basin()->definition, true, true, false) };
    for (unsigned w = 0; w < 2; ++w) {
        BoatFleet f;
        Reference r;
        Boat start;
        boat_reset_location(&start, basin()->definition);
        CHECK(fleet(&f, waters[w]));
        CHECK(reference_init(&r, waters[w], &start, 1));
        boat_fleet_place(&f, 0, start);
        unsigned airborne = 0;
        for (unsigned tick = 0; tick < 2400; ++tick) {
            BoatInput inputs[BOAT_FLEET_SLOTS] = { loop_input(&r.boats[0], tick) };
            CHECK(reference_step(&r, inputs, 1));
            CHECK(boat_fleet_step(&f, inputs));
            CHECK(f.stats.emission_failures == 0 && f.stats.pair_tests == 0);
            CHECK(memcmp(&f.state.slots[0].boat, &r.boats[0], sizeof(Boat)) == 0);
            CHECK(f.state.tick == r.tick && same_wakes(&f.state.wakes, &r.wakes));
            airborne += r.boats[0].motion_state == BOAT_AIRBORNE;
        }
        CHECK(r.wakes.emitted > 10);
        printf("one-boat equivalence (%s): %u ticks, emitted %llu, airborne ticks %u\n",
            w ? "waves" : "calm", 2400, (unsigned long long)r.wakes.emitted, airborne);
    }
    return 0;
}

// Head-on and perpendicular fixtures: the solver's isolated-pair response is boat_collide_pair.
static int pair_equivalence(void)
{
    const Boat fixtures[][2] = {
        { boat_at((Vector2) { -9, 0 }, PI / 2, (Vector2) { 8, 0 }),
            boat_at((Vector2) { 9, 0 }, -PI / 2, (Vector2) { -8, 0 }) },
        { boat_at((Vector2) { -10, 1 }, PI / 2, (Vector2) { 10, 0 }),
            boat_at((Vector2) { 1, -9 }, PI, (Vector2) { 0, 6 }) },
    };
    for (unsigned k = 0; k < 2; ++k) {
        BoatFleet f;
        Reference r;
        CHECK(fleet(&f, (BoatWater) { 0 }));
        CHECK(reference_init(&r, (BoatWater) { 0 }, fixtures[k], 2));
        boat_fleet_place(&f, 0, fixtures[k][0]);
        boat_fleet_place(&f, 1, fixtures[k][1]);
        unsigned contacts = 0;
        for (unsigned tick = 0; tick < 720; ++tick) {
            BoatInput inputs[BOAT_FLEET_SLOTS]
                = { { 0, tick < 240 ? 1 : 0, 0, 0 }, { k ? .3f : 0, tick < 240 ? 1 : 0, 0, 0 } };
            CHECK(reference_step(&r, inputs, 2));
            CHECK(boat_fleet_step(&f, inputs));
            contacts += f.stats.contacts;
            CHECK(f.stats.residual == 0 && f.stats.passes <= 1);
            for (unsigned i = 0; i < 2; ++i)
                CHECK(memcmp(&f.state.slots[i].boat, &r.boats[i], sizeof(Boat)) == 0);
            CHECK(same_wakes(&f.state.wakes, &r.wakes));
        }
        CHECK(contacts > 0);
        printf("pair equivalence (%s): %u contact ticks match boat_collide_pair\n",
            k ? "side swipe" : "head-on", contacts);
    }
    return 0;
}

static float difference(const Boat *a, const Boat *b)
{
    float d = Vector2Distance(a->position, b->position);
    d = fmaxf(d, Vector2Distance(a->velocity, b->velocity));
    d = fmaxf(d, fabsf(remainderf(a->heading - b->heading, 2 * PI)));
    d = fmaxf(d, fabsf(a->yaw_velocity - b->yaw_velocity));
    return fmaxf(d, fabsf(a->height - b->height));
}

// The same encounter with roles and slots permuted stays within a small tolerance: canonical
// order is reproducible, and the response is symmetric up to float rounding.
static int permutations(void)
{
    const Boat cross[4] = {
        boat_at((Vector2) { -10, 0 }, PI / 2, (Vector2) { 6, 0 }),
        boat_at((Vector2) { 10, 0 }, -PI / 2, (Vector2) { -6, 0 }),
        boat_at((Vector2) { 0, -10 }, PI, (Vector2) { 0, 6 }),
        boat_at((Vector2) { 0, 10 }, 0, (Vector2) { 0, -6 }),
    };
    const unsigned orders[][4]
        = { { 0, 1, 2, 3 }, { 1, 0, 3, 2 }, { 2, 3, 0, 1 }, { 3, 2, 1, 0 }, { 2, 0, 3, 1 } };
    Boat results[5][4];
    unsigned contacts = 0, multi = 0;
    for (unsigned n = 2; n <= 4; n += 2) {
        for (unsigned o = 0; o < 5; ++o) {
            // Boat i takes the i-th slot of this order that exists for N boats.
            unsigned slot[4];
            for (unsigned i = 0, used = 0; i < 4 && used < n; ++i)
                if (orders[o][i] < n)
                    slot[used++] = orders[o][i];
            BoatFleet f;
            CHECK(fleet(&f, (BoatWater) { 0 }));
            for (unsigned i = 0; i < n; ++i)
                boat_fleet_place(&f, slot[i], cross[i]);
            for (unsigned tick = 0; tick < 240; ++tick) {
                BoatInput inputs[BOAT_FLEET_SLOTS] = { 0 };
                CHECK(boat_fleet_step(&f, inputs));
                contacts += f.stats.contacts;
                multi += f.stats.contacts > 1;
                CHECK(f.stats.passes <= BOAT_FLEET_SOLVER_PASSES);
            }
            for (unsigned i = 0; i < n; ++i)
                results[o][i] = f.state.slots[slot[i]].boat;
        }
        float worst = 0;
        for (unsigned o = 1; o < 5; ++o)
            for (unsigned i = 0; i < n; ++i)
                worst = fmaxf(worst, difference(&results[o][i], &results[0][i]));
        CHECK(worst < 1e-3f);
        printf("permutation/role swap, %u boats: worst difference %.3g after 240 ticks\n", n,
            (double)worst);
    }
    CHECK(contacts > 0 && multi > 0);
    return 0;
}

// Crossings at full speed, sampled at sub-tick phases and lateral offsets, never tunnel: the
// most a hull moves in a tick (0.3 m closing at 36 m/s) is far below its 1.5 m beam.
static int tunneling(void)
{
    unsigned encounters = 0;
    for (unsigned phase = 0; phase < 8; ++phase)
        for (int lateral = -12; lateral <= 12; lateral += 3) {
            float shift = .15f * (float)phase / 8, offset = (float)lateral / 10;
            const Boat pairs[][2] = {
                { boat_at((Vector2) { -12 - shift, offset }, PI / 2, (Vector2) { 18, 0 }),
                    boat_at((Vector2) { 12, 0 }, -PI / 2, (Vector2) { -18, 0 }) },
                { boat_at((Vector2) { -12 - shift, offset }, PI / 2, (Vector2) { 18, 0 }),
                    boat_at((Vector2) { 0, 12 }, 0, (Vector2) { 0, -18 }) },
            };
            for (unsigned k = 0; k < 2; ++k) {
                BoatFleet f;
                CHECK(fleet(&f, (BoatWater) { 0 }));
                boat_fleet_place(&f, 0, pairs[k][0]);
                boat_fleet_place(&f, 1, pairs[k][1]);
                unsigned contacts = 0;
                for (unsigned tick = 0; tick < 120; ++tick) {
                    BoatInput inputs[BOAT_FLEET_SLOTS] = { 0 };
                    CHECK(boat_fleet_step(&f, inputs));
                    contacts += f.stats.contacts;
                }
                CHECK(contacts > 0);
                ++encounters;
            }
        }
    printf("no tunneling: %u full-speed head-on and crossing encounters all made contact\n",
        encounters);
    return 0;
}

static unsigned random_state = 12345;

static float uniform(float low, float high)
{
    random_state = random_state * 1103515245u + 12345u;
    return low + (high - low) * (float)((random_state >> 8) & 0xffffff) / (float)0x1000000;
}

static double energy(const Boat *boats, const bool *active)
{
    double e = 0;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        if (active[i])
            e += .5 * Vector2LengthSqr(boats[i].velocity)
                + .5 * YAW_INERTIA * boats[i].yaw_velocity * boats[i].yaw_velocity;
    return e;
}

static float worst_depth(const Boat *boats, const bool *active)
{
    float depth = 0;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        for (unsigned j = i + 1; j < BOAT_FLEET_SLOTS; ++j) {
            BoatPairContact c;
            if (active[i] && active[j] && boat_pair_contact(&boats[i], &boats[j], &c))
                depth = fmaxf(depth, c.depth);
        }
    return depth;
}

// Passive solver fixtures: equal-mass momentum is conserved, kinetic energy never grows,
// overlap never grows, and boats packed against the wall stay inside it. Deep cases overlap at
// random; swept cases start clear and coast together until the tick of first touch, as in play.
static int solver_invariants(void)
{
    BoatWorld world;
    boat_world_init(&world, basin(), (BoatWater) { 0 });
    unsigned contacts[2] = { 0 }, residual[2] = { 0 }, holds = 0;
    float residual_depth[2] = { 0 };
    double worst_momentum = 0, worst_energy = 0;
    for (unsigned c = 0; c < 8000; ++c) {
        unsigned n = 2 + c % 3, swept = c >= 4000;
        bool wall = c % 4 == 3;
        Vector2 center = wall ? (Vector2) { BOAT_BASIN_HALF_X - 4, uniform(-60, 60) }
                              : (Vector2) { uniform(-60, 60), uniform(-40, 40) };
        Boat boats[BOAT_FLEET_SLOTS] = { 0 };
        bool active[BOAT_FLEET_SLOTS] = { 0 };
        for (unsigned i = 0; i < n; ++i) {
            for (unsigned attempt = 0; attempt < 64 && !active[i]; ++attempt) {
                float reach = swept ? 4.5f : 2.5f;
                Vector2 offset = { uniform(-reach, reach), uniform(-reach, reach) };
                Vector2 velocity = { uniform(-18, 18), uniform(-18, 18) };
                if (swept) // Mostly closing on the cluster centre.
                    velocity = Vector2Add(
                        Vector2Scale(offset, -uniform(0, 4)), Vector2Scale(velocity, .3f));
                boats[i] = boat_at(Vector2Add(center, offset), uniform(-PI, PI), velocity);
                boats[i].yaw_velocity = uniform(-2, 2);
                active[i] = boat_hull_clear(&boats[i], &world, 0);
                for (unsigned j = 0; swept && active[i] && j < i; ++j) {
                    BoatPairContact touch;
                    active[i] = !active[j] || !boat_pair_contact(&boats[i], &boats[j], &touch);
                }
                if (!swept)
                    break;
            }
        }
        // Coast tick by tick until the first touch, holding any hull the wall would stop.
        for (unsigned tick = 0; swept && tick < 240 && worst_depth(boats, active) == 0; ++tick)
            for (unsigned i = 0; i < n; ++i) {
                Boat moved = boats[i];
                moved.position
                    = Vector2Add(moved.position, Vector2Scale(moved.velocity, BOAT_STEP));
                moved.heading += moved.yaw_velocity * BOAT_STEP;
                if (active[i] && boat_hull_clear(&moved, &world, 0))
                    boats[i] = moved;
            }
        Vector2 momentum = { 0 };
        for (unsigned i = 0; i < n; ++i)
            if (active[i])
                momentum = Vector2Add(momentum, boats[i].velocity);
        double before = energy(boats, active);
        float depth = worst_depth(boats, active);
        BoatFleetStats stats = boat_fleet_solve(boats, active, &world, 0);
        CHECK(stats.passes <= BOAT_FLEET_SOLVER_PASSES);
        CHECK(stats.pair_tests <= BOAT_FLEET_PAIRS * (stats.passes + 1));
        Vector2 after = { 0 };
        for (unsigned i = 0; i < n; ++i)
            if (active[i]) {
                after = Vector2Add(after, boats[i].velocity);
                CHECK(boat_hull_clear(&boats[i], &world, 0));
                CHECK(isfinite(boats[i].position.x) && isfinite(boats[i].yaw_velocity));
            }
        worst_momentum = fmax(worst_momentum, Vector2Distance(momentum, after));
        worst_energy = fmax(worst_energy, energy(boats, active) - before);
        CHECK(Vector2Distance(momentum, after) < 1e-3f);
        CHECK(energy(boats, active) <= before * (1 + 1e-5) + 1e-6);
        CHECK(worst_depth(boats, active) <= depth + 1e-4f);
        CHECK(stats.contacts == 0 || stats.passes > 0);
        contacts[swept] += stats.contacts;
        residual[swept] += stats.residual > 0;
        residual_depth[swept] = fmaxf(residual_depth[swept], worst_depth(boats, active));
        holds += stats.shore_holds;
    }
    CHECK(contacts[0] > 1000 && contacts[1] > 1000 && holds > 0);
    CHECK(residual_depth[1] <= .05f); // The plan's residual penetration gate for live contacts.
    printf("solver invariants: momentum error %.2g, energy gain %.2g, %u shore holds\n",
        worst_momentum, worst_energy, holds);
    for (unsigned swept = 0; swept < 2; ++swept)
        printf("  %s: 4000 cases, %u contacts, %u with residual contact (deepest %.3f m)\n",
            swept ? "swept" : "deep random", contacts[swept], residual[swept],
            (double)residual_depth[swept]);
    return 0;
}

enum { RUN_TICKS = 6000, RESUME_TICKS = 600, MARKS = 6 };

// A checkpoint just before a step: the state after the bots chose this tick's inputs (and any
// reset requests), the bots' own state, and those inputs.
typedef struct {
    const char *name;
    bool taken;
    BoatFleetState state;
    BoatFleetBots bots;
    BoatInput inputs[BOAT_FLEET_SLOTS];
} Mark;

static Mark marks[MARKS]
    = { { .name = "mid-flight" }, { .name = "mid-contact" }, { .name = "half-emitted span" },
          { .name = "capsize" }, { .name = "pool wrap" }, { .name = "pending reset" } };
static uint64_t hashes[RUN_TICKS]; // Pre-step state at each tick.

static void take(Mark *mark, const BoatFleet *f, const BoatFleetBots *bots,
    const BoatInput inputs[BOAT_FLEET_SLOTS])
{
    if (mark->taken)
        return;
    mark->taken = true;
    boat_fleet_save(f, &mark->state);
    mark->bots = *bots;
    memcpy(mark->inputs, inputs, sizeof(mark->inputs));
}

// Four bots from tick zero. Slots 0 and 1 start 8 m apart closing head-on at 12 m/s, and slot 3
// starts upside down, so a contact, a capsize and a reset are certain.
static int fleet_start(BoatFleet *f, BoatFleetBots *bots)
{
    CHECK(fleet(f, (BoatWater) { 0 }));
    *bots = (BoatFleetBots) { 0 };
    for (unsigned slot = 0; slot < BOAT_FLEET_SLOTS; ++slot)
        CHECK(boat_fleet_request_spawn(f, slot));
    BoatInput none[BOAT_FLEET_SLOTS] = { 0 };
    CHECK(boat_fleet_step(f, none));
    CHECK(f->stats.spawned == BOAT_FLEET_SLOTS);
    f->state.slots[0].boat = boat_at((Vector2) { 0, 10 }, 0, (Vector2) { 0, -6 });
    f->state.slots[1].boat = boat_at((Vector2) { 0, 2 }, PI, (Vector2) { 0, 6 });
    f->state.slots[3].boat.roll = PI;
    return 0;
}

// Continue from pre-step tick T with INPUTS to END, checking every pre-step hash. Partway, a
// by-value clone continues on its own and must agree too.
static int resume(BoatFleet *f, BoatFleetBots *bots, BoatInput inputs[BOAT_FLEET_SLOTS], unsigned t,
    unsigned end, bool clone)
{
    CHECK(boat_fleet_hash(&f->state) == hashes[t]);
    for (;;) {
        CHECK(boat_fleet_step(f, inputs));
        if (++t >= end)
            return 0;
        boat_fleet_bots_drive(bots, f, inputs);
        CHECK(boat_fleet_hash(&f->state) == hashes[t]);
        if (clone) {
            BoatFleet copy = *f;
            BoatFleetBots copy_bots = *bots;
            BoatInput copy_inputs[BOAT_FLEET_SLOTS];
            memcpy(copy_inputs, inputs, sizeof(copy_inputs));
            CHECK(resume(&copy, &copy_bots, copy_inputs, t, end, false) == 0);
            clone = false;
        }
    }
}

static int checkpoints(void)
{
    BoatFleet f;
    BoatFleetBots bots;
    CHECK(fleet_start(&f, &bots) == 0);
    unsigned first = (unsigned)f.state.tick, wraps = 0;
    unsigned previous_next[BOAT_FLEET_SLOTS] = { 0 };
    for (unsigned tick = first; tick < RUN_TICKS; ++tick) {
        BoatInput inputs[BOAT_FLEET_SLOTS];
        boat_fleet_bots_drive(&bots, &f, inputs);
        hashes[tick] = boat_fleet_hash(&f.state);
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
            const BoatFleetSlot *s = &f.state.slots[i];
            const BoatWakeObjectEmitter *e = &f.state.wakes.emitters[i];
            if (s->active && s->boat.motion_state == BOAT_AIRBORNE)
                take(&marks[0], &f, &bots, inputs);
            if (e->initialized && f.state.tick - e->started == BOAT_OBJECT_EMIT_TICKS / 2)
                take(&marks[2], &f, &bots, inputs);
            if (s->active && s->boat.motion_state == BOAT_CAPSIZED)
                take(&marks[3], &f, &bots, inputs);
            if (previous_next[i] == BOAT_OBJECT_PER_BOAT - 1 && e->next == 0) {
                take(&marks[4], &f, &bots, inputs);
                ++wraps;
            }
            previous_next[i] = e->next;
            if (s->pending)
                take(&marks[5], &f, &bots, inputs);
        }
        if (f.stats.contacts)
            take(&marks[1], &f, &bots, inputs);
        CHECK(boat_fleet_step(&f, inputs));
        CHECK(f.stats.emission_failures == 0);
    }
    for (unsigned m = 0; m < MARKS; ++m) {
        if (!marks[m].taken) {
            fprintf(stderr, "checkpoint fixture never reached: %s\n", marks[m].name);
            return 1;
        }
        // Resume in a separately initialized fleet.
        BoatFleet resumed;
        CHECK(fleet(&resumed, (BoatWater) { 0 }));
        boat_fleet_restore(&resumed, &marks[m].state);
        unsigned start = (unsigned)resumed.state.tick;
        unsigned end = start + RESUME_TICKS < RUN_TICKS ? start + RESUME_TICKS : RUN_TICKS;
        CHECK(resume(&resumed, &marks[m].bots, marks[m].inputs, start, end, true) == 0);
        printf("checkpoint %-17s tick %5u resumes %u ticks bit-identically\n", marks[m].name, start,
            end - start);
    }
    CHECK(wraps > 0 && bots.resets > 0);
    printf("checkpoint size %zu bytes (budget 16384)\n", sizeof(BoatFleetState));
    return 0;
}

// Live four-boat play with reset churn: bounded pool, no emission failures, shore containment,
// bounded solver work, safe spawns, lives advancing and wake histories kept through resets.
static int churn(void)
{
    BoatFleet f;
    BoatFleetBots bots;
    CHECK(fleet_start(&f, &bots) == 0);
    unsigned spawns = 0, contacts = 0, residual = 0, passes = 0, longest_wait = 0;
    unsigned peak_live = 0, flights = 0;
    uint32_t lives[BOAT_FLEET_SLOTS] = { 0 };
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        lives[i] = f.state.slots[i].life;
    for (unsigned tick = 0; tick < 12000; ++tick) {
        if (tick % 150 == 149)
            CHECK(boat_fleet_request_spawn(&f, (tick / 150) % BOAT_FLEET_SLOTS));
        BoatInput inputs[BOAT_FLEET_SLOTS];
        boat_fleet_bots_drive(&bots, &f, inputs);
        bool airborne[BOAT_FLEET_SLOTS];
        uint64_t requested[BOAT_FLEET_SLOTS];
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
            airborne[i] = f.state.slots[i].boat.motion_state == BOAT_AIRBORNE;
            requested[i] = f.state.slots[i].pending ? f.state.slots[i].requested : UINT64_MAX;
        }
        BoatWakeObject before[BOAT_OBJECT_CAPACITY];
        memcpy(before, f.state.wakes.objects, sizeof(before));
        CHECK(boat_fleet_step(&f, inputs));
        const BoatFleetStats *s = &f.stats;
        CHECK(s->emission_failures == 0);
        CHECK(s->passes <= BOAT_FLEET_SOLVER_PASSES);
        CHECK(s->pair_tests <= BOAT_FLEET_PAIRS * (s->passes + 1));
        CHECK(f.state.wakes.live <= BOAT_OBJECT_CAPACITY);
        contacts += s->contacts;
        residual += s->residual;
        passes += s->passes;
        peak_live = f.state.wakes.live > peak_live ? f.state.wakes.live : peak_live;
        BoatWorld world = boat_fleet_world(&f);
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
            const BoatFleetSlot *slot = &f.state.slots[i];
            CHECK(slot->active);
            CHECK(boat_hull_clear(&slot->boat, &world, boat_fleet_time(&f)));
            flights += !airborne[i] && slot->boat.motion_state == BOAT_AIRBORNE;
            if (slot->life == lives[i])
                continue;
            // A reset: the new hull is clear of everyone, and no live wake record vanished.
            CHECK(slot->life == lives[i] + 1);
            lives[i] = slot->life;
            ++spawns;
            unsigned wait = (unsigned)(f.state.tick - 1 - requested[i]);
            CHECK(wait % BOAT_FLEET_SPAWN_RETRY_TICKS == 0);
            longest_wait = wait > longest_wait ? wait : longest_wait;
            for (unsigned j = 0; j < BOAT_FLEET_SLOTS; ++j)
                CHECK(j == i
                    || Vector2Distance(slot->boat.position, f.state.slots[j].boat.position) > 9);
            for (unsigned k = 0; k < BOAT_OBJECT_CAPACITY; ++k)
                CHECK(before[k].amplitude == 0 || f.state.wakes.objects[k].amplitude != 0
                    || f.state.tick - before[k].born >= f.state.wakes.life_ticks);
        }
    }
    CHECK(spawns >= 70);
    CHECK(longest_wait <= 2 * BOAT_TICK_HZ);
    printf("four-boat churn: %u resets (longest wait %u ticks), %u contact pairs, %u solver "
           "passes, %u residual, peak live %u, %u flights, %u bot recoveries\n",
        spawns, longest_wait, contacts, passes, residual, peak_live, flights, bots.resets);
    return 0;
}

// A pool filled to all 128 records at the exact emission cadence, each emitter's cursor on its
// oldest record: four bots keep driving through resets, a leave and a rejoin at the wrap without
// a single capacity failure.
static int full_pool(void)
{
    BoatFleet f;
    BoatFleetBots bots;
    CHECK(fleet_start(&f, &bots) == 0);
    BoatInput inputs[BOAT_FLEET_SLOTS];
    for (unsigned tick = 0; tick < 2000; ++tick) {
        boat_fleet_bots_drive(&bots, &f, inputs);
        CHECK(boat_fleet_step(&f, inputs));
    }
    BoatWakeObjects *w = &f.state.wakes;
    for (unsigned e = 0; e < BOAT_OBJECT_BOATS; ++e)
        for (unsigned k = 0; k < BOAT_OBJECT_PER_BOAT; ++k) {
            unsigned index = e * BOAT_OBJECT_PER_BOAT + (w->emitters[e].next + k) % 32;
            w->objects[index] = (BoatWakeObject) {
                .position = { -100 + 50 * (float)e, -60 + 3.5f * (float)k },
                .direction = { 1, 0 },
                .length = 4,
                .amplitude = 1.2f,
                .inverse_speed = .2f,
                .born = f.state.tick - w->life_ticks + 1 + BOAT_OBJECT_EMIT_TICKS * k,
            };
        }
    w->live = w->peak_live = BOAT_OBJECT_CAPACITY;
    unsigned crowded = 0, births = (unsigned)w->emitted;
    for (unsigned tick = 0; tick < 4000; ++tick) {
        if (tick == 5)
            CHECK(boat_fleet_request_spawn(&f, 1));
        if (tick == 30)
            boat_fleet_leave(&f, 2);
        if (tick == 90)
            CHECK(boat_fleet_request_spawn(&f, 2));
        boat_fleet_bots_drive(&bots, &f, inputs);
        CHECK(boat_fleet_step(&f, inputs));
        CHECK(f.stats.emission_failures == 0 && w->live <= BOAT_OBJECT_CAPACITY);
        crowded += w->live >= BOAT_OBJECT_CAPACITY - 8;
    }
    CHECK(f.state.slots[1].life >= 2 && f.state.slots[2].life >= 2 && f.state.slots[2].active);
    CHECK(crowded > 0);
    printf("full pool: from 128 live records, %u ticks at 120 or more, %u births through "
           "resets, a leave and a rejoin at the wrap\n",
        crowded, (unsigned)w->emitted - births);
    return 0;
}

// Requests spawn in queue order, blocked candidates wait at 10 Hz, and a reset keeps the pool.
static int spawn_rules(void)
{
    BoatFleet f;
    CHECK(fleet(&f, (BoatWater) { 0 }));
    BoatInput none[BOAT_FLEET_SLOTS] = { 0 };
    CHECK(boat_fleet_step(&f, none));
    // Tall young crests over slot 1's first two candidates, (40, -30) and (40, 30). Slot 2's
    // first choice, (-40, -30), is slot 1's third.
    const Vector2 blocked[] = { { 40, -30 }, { 40, 30 } };
    for (unsigned k = 0; k < 2; ++k) {
        BoatWakeObject *o = &f.state.wakes.objects[3 * BOAT_OBJECT_PER_BOAT + k];
        *o = (BoatWakeObject) { .position = blocked[k],
            .direction = { 1, 0 },
            .length = 6,
            .amplitude = 1.8f,
            .inverse_speed = .2f,
            .born = f.state.tick - 30 };
        ++f.state.wakes.live;
    }
    CHECK(boat_fleet_request_spawn(&f, 2));
    CHECK(boat_fleet_request_spawn(&f, 1));
    CHECK(boat_fleet_request_spawn(&f, 1)); // Idempotent: keeps its place.
    CHECK(boat_fleet_step(&f, none));
    CHECK(f.stats.spawned == 2);
    const Vector2 two = f.state.slots[2].boat.position, one = f.state.slots[1].boat.position;
    CHECK(two.x == -40 && two.y == -30); // First request, first choice.
    CHECK(one.x == -80 && one.y == 0); // Its first choices are rough or now taken.
    // Block every candidate with crests: a request waits, retrying every 12 ticks.
    unsigned live = f.state.wakes.live;
    for (unsigned k = 0; k < 8; ++k) {
        static const Vector2 all[] = { { 0, 10 }, { -40, 30 }, { 40, -30 }, { 40, 30 },
            { -40, -30 }, { -80, 0 }, { 80, 0 }, { 0, -45 } };
        BoatWakeObject *o = &f.state.wakes.objects[3 * BOAT_OBJECT_PER_BOAT + 2 + k];
        *o = (BoatWakeObject) { .position = all[k],
            .direction = { 0, 1 },
            .length = 6,
            .amplitude = 1.8f,
            .inverse_speed = .2f,
            .born = f.state.tick - 30 };
        ++f.state.wakes.live;
    }
    CHECK(boat_fleet_request_spawn(&f, 0));
    uint64_t requested = f.state.slots[0].requested;
    unsigned blocked_ticks = 0;
    while (!f.state.slots[0].active) {
        CHECK(boat_fleet_step(&f, none));
        CHECK(f.stats.spawned == 0
            || (f.state.tick - 1 - requested) % BOAT_FLEET_SPAWN_RETRY_TICKS == 0);
        blocked_ticks += f.stats.spawn_blocked;
        CHECK(f.state.tick < requested + 8 * BOAT_TICK_HZ);
    }
    CHECK(blocked_ticks > 0 && f.state.slots[0].life == 1);
    // An active boat's reset keeps every live record and only restarts its emitter's span.
    BoatWakeObjects wakes = f.state.wakes;
    Boat moved = f.state.slots[1].boat;
    moved.position = (Vector2) { 20, 20 };
    boat_fleet_place(&f, 1, moved);
    CHECK(memcmp(wakes.objects, f.state.wakes.objects, sizeof(wakes.objects)) == 0);
    CHECK(f.state.wakes.emitters[1].next == wakes.emitters[1].next);
    CHECK(!f.state.wakes.emitters[1].initialized && f.state.slots[1].life == 2);
    CHECK(f.state.wakes.live >= live);
    printf("spawn rules: queue order, idempotent requests, %u blocked retries over %llu ticks\n",
        blocked_ticks, (unsigned long long)(f.state.tick - requested));
    return 0;
}

static int rejects(void)
{
    BoatFleet f;
    BoatWater trail = { .player_wake_enabled = true };
    CHECK(!boat_fleet_init(&f, basin(), trail, BOAT_OBJECT_LIFE_TICKS, 1.8f));

    CHECK(!boat_fleet_init(&f, basin(), (BoatWater) { 0 }, 4000, 1.8f));
    CHECK(fleet(&f, (BoatWater) { 0 }));
    CHECK(!boat_fleet_request_spawn(&f, BOAT_FLEET_SLOTS));
    f.state.tick = f.state.wakes.tick = UINT64_MAX;
    BoatInput none[BOAT_FLEET_SLOTS] = { 0 };
    CHECK(!boat_fleet_step(&f, none));
    // Non-finite commands are integrated as zero.
    CHECK(fleet(&f, (BoatWater) { 0 }));
    BoatFleet g = f;
    boat_fleet_place(&f, 0, boat_at((Vector2) { 0, 0 }, 0, (Vector2) { 0, -3 }));
    boat_fleet_place(&g, 0, boat_at((Vector2) { 0, 0 }, 0, (Vector2) { 0, -3 }));
    BoatInput bad[BOAT_FLEET_SLOTS] = { { NAN, INFINITY, 0, 0 } };
    CHECK(boat_fleet_step(&f, bad) && boat_fleet_step(&g, none));
    CHECK(boat_fleet_hash(&f.state) == boat_fleet_hash(&g.state));
    return 0;
}

int main(void)
{
    if (one_boat_equivalence() || pair_equivalence() || permutations() || tunneling()
        || solver_invariants() || checkpoints() || churn() || full_pool() || spawn_rules()
        || rejects())
        return 1;
    printf("boat_fleet=ok\n");
    test_worlds_close();
    return 0;
}
