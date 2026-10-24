#include "boat_fleet.h"
#include "raymath.h"
#include "waterfront_city.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static_assert(sizeof(BoatFleetState) <= 8192, "a fleet checkpoint must stay within 8 KiB");
static_assert(sizeof(Boat) == 27 * sizeof(float), "boat_fleet_hash must list every Boat field");

// Authored basin spawn candidates in simulation X/Z, each facing the basin centre.
static const Vector2 SPAWNS[] = { { 0, 10 }, { -40, 30 }, { 40, -30 }, { 40, 30 }, { -40, -30 },
    { -80, 0 }, { 80, 0 }, { 0, -45 } };
enum { SPAWN_COUNT = sizeof(SPAWNS) / sizeof(*SPAWNS) };
// River-aligned local coordinates about the mapped start area: four lanes 60 m apart,
// with an alternate row 70 m across the river when a candidate is occupied or disturbed.
static const Vector2 RIVER_SPAWNS[SPAWN_COUNT] = { { -90, -35 }, { -90, 35 }, { -30, -35 },
    { -30, 35 }, { 30, -35 }, { 30, 35 }, { 90, -35 }, { 90, 35 } };
static const float SPAWN_CLEARANCE = 10; // Centre distance to every other hull.
static const float SPAWN_LOOKAHEAD = .5f; // Seconds of straight-line approach to rule out.
// Calm arcade-wake surface at the six supports, now and half a second ahead. Ambient waves are
// a uniform room setting, so only the wake contribution is checked.
static const float SPAWN_CALM_HEIGHT = .25f;
static const float SPAWN_CALM_GRADIENT = .2f;
static const float SPAWN_CALM_VERTICAL_SPEED = .5f;

bool boat_fleet_init(BoatFleet *fleet, const BoatWorldDefinition *definition, BoatWater water,
    unsigned life_ticks, float crest_height)
{
    if (!definition || water.player_wake_enabled)
        return false;
    if (definition->mapped)
        waterfront_city_select_river(definition->id == BOAT_LOCATION_RIVER);
    *fleet = (BoatFleet) { 0 };
    if (!boat_wake_objects_init(&fleet->state.wakes, life_ticks, crest_height))
        return false;
    water.objects = &fleet->state.wakes;
    boat_world_init(&fleet->world, definition, water);
    return true;
}

bool boat_fleet_request_spawn(BoatFleet *fleet, unsigned slot)
{
    if (slot >= BOAT_FLEET_SLOTS)
        return false;
    BoatFleetSlot *s = &fleet->state.slots[slot];
    if (!s->pending) {
        s->pending = true;
        s->requested = fleet->state.tick;
        s->request = fleet->state.next_request++;
    }
    return true;
}

void boat_fleet_place(BoatFleet *fleet, unsigned slot, Boat boat)
{
    assert(slot < BOAT_FLEET_SLOTS);
    BoatFleetSlot *s = &fleet->state.slots[slot];
    s->boat = boat;
    s->active = true;
    s->pending = false;
    ++s->life;
    // A new span without a seam across the teleport. The cursor stays, and the new span's own
    // sample window keeps births at least BOAT_OBJECT_EMIT_TICKS apart.
    BoatWakeObjectEmitter *emitter = &fleet->state.wakes.emitters[slot];
    *emitter = (BoatWakeObjectEmitter) { .next = emitter->next };
}

void boat_fleet_leave(BoatFleet *fleet, unsigned slot)
{
    assert(slot < BOAT_FLEET_SLOTS);
    fleet->state.slots[slot].active = false;
    fleet->state.slots[slot].pending = false;
}

BoatWorld boat_fleet_world(const BoatFleet *fleet)
{
    BoatWorld world = fleet->world;
    world.water.objects = &fleet->state.wakes;
    return world;
}

double boat_fleet_time(const BoatFleet *fleet) { return boat_tick_seconds(fleet->state.tick); }

void boat_fleet_save(const BoatFleet *fleet, BoatFleetState *checkpoint)
{
    *checkpoint = fleet->state;
}

void boat_fleet_restore(BoatFleet *fleet, const BoatFleetState *checkpoint)
{
    fleet->state = *checkpoint;
    fleet->world.water.objects = &fleet->state.wakes;
    fleet->stats = (BoatFleetStats) { 0 };
}

static bool calm(const BoatWakeObjects *wakes, Vector2 point, uint64_t tick)
{
    BoatWakeObjectSample s = boat_wake_objects_query_at(wakes, point, tick);
    return fabsf(s.height) <= SPAWN_CALM_HEIGHT && Vector2Length(s.gradient) <= SPAWN_CALM_GRADIENT
        && fabsf(s.vertical_velocity) <= SPAWN_CALM_VERTICAL_SPEED;
}

// Minimum distance from TARGET to OTHER moving straight on for the lookahead.
static float approach(const Boat *other, Vector2 target)
{
    Vector2 offset = Vector2Subtract(other->position, target);
    float speed2 = Vector2LengthSqr(other->velocity);
    float t = speed2 > 0
        ? Clamp(-Vector2DotProduct(offset, other->velocity) / speed2, 0, SPAWN_LOOKAHEAD)
        : 0;
    return Vector2Length(Vector2Add(offset, Vector2Scale(other->velocity, t)));
}

static bool safe_spawn(const BoatFleet *fleet, unsigned slot, const Boat *candidate)
{
    const BoatFleetState *s = &fleet->state;
    for (unsigned other = 0; other < BOAT_FLEET_SLOTS; ++other)
        if (other != slot && s->slots[other].active
            && approach(&s->slots[other].boat, candidate->position) < SPAWN_CLEARANCE)
            return false;
    BoatWorld world = boat_fleet_world(fleet);
    if (!boat_hull_clear(candidate, &world, boat_fleet_time(fleet)))
        return false;
    Vector2 forward = { sinf(candidate->heading), -cosf(candidate->heading) };
    Vector2 right = { cosf(candidate->heading), sinf(candidate->heading) };
    uint64_t ahead = s->tick + (uint64_t)(SPAWN_LOOKAHEAD * BOAT_TICK_HZ);
    for (int end = -1; end <= 1; ++end)
        for (int side = -1; side <= 1; side += 2) {
            Vector2 point = Vector2Add(candidate->position,
                Vector2Add(Vector2Scale(forward, end * 1.4f), Vector2Scale(right, side * .65f)));
            if (!calm(&s->wakes, point, s->tick) || !calm(&s->wakes, point, ahead))
                return false;
        }
    return true;
}

// Due requests in queue order, each tested against the hulls already placed in this batch.
static void spawn_pending(BoatFleet *fleet)
{
    BoatFleetState *s = &fleet->state;
    bool tried[BOAT_FLEET_SLOTS] = { 0 };
    for (;;) {
        unsigned slot = BOAT_FLEET_SLOTS;
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
            const BoatFleetSlot *candidate = &s->slots[i];
            bool due = candidate->pending && !tried[i]
                && (s->tick - candidate->requested) % BOAT_FLEET_SPAWN_RETRY_TICKS == 0;
            if (due && (slot == BOAT_FLEET_SLOTS || candidate->request < s->slots[slot].request))
                slot = i;
        }
        if (slot == BOAT_FLEET_SLOTS)
            break;
        tried[slot] = true;
        bool placed = false;
        for (unsigned k = 0; k < SPAWN_COUNT && !placed; ++k) {
            Vector2 at = SPAWNS[(2 * slot + k) % SPAWN_COUNT];
            if (fleet->world.definition->mapped) {
                Vector2 local = RIVER_SPAWNS[(2 * slot + k) % SPAWN_COUNT];
                at = Vector2Add(fleet->world.definition->fleet_spawn,
                    (Vector2) { -.8f * local.x + .6f * local.y, .6f * local.x + .8f * local.y });
            }
            Boat boat;
            boat_reset(&boat);
            boat.position = at;
            Vector2 centre = fleet->world.definition->mapped ? fleet->world.definition->fleet_spawn
                                                             : (Vector2) { 0 };
            boat.heading = fleet->world.definition->mapped
                ? atan2f(centre.x - at.x, at.y - centre.y)
                : atan2f(-at.x, at.y);
            if (safe_spawn(fleet, slot, &boat)) {
                boat_fleet_place(fleet, slot, boat);
                placed = true;
            }
        }
        if (placed)
            ++fleet->stats.spawned;
        else
            ++fleet->stats.spawn_blocked; // Stays queued and retries at 10 Hz.
    }
}

BoatFleetStats boat_fleet_solve(Boat boats[BOAT_FLEET_SLOTS], const bool active[BOAT_FLEET_SLOTS],
    const BoatWorld *world, double time)
{
    BoatFleetStats stats = { 0 };
    float accumulated[BOAT_FLEET_PAIRS] = { 0 }, target[BOAT_FLEET_PAIRS] = { 0 };
    bool seen[BOAT_FLEET_PAIRS] = { 0 };
    for (unsigned pass = 0;; ++pass) {
        // Every pass reads one common candidate array; pairs in canonical slot order.
        BoatPairContact contact[BOAT_FLEET_PAIRS];
        bool touching[BOAT_FLEET_PAIRS] = { 0 };
        unsigned count[BOAT_FLEET_SLOTS] = { 0 }, touches = 0;
        for (unsigned i = 0, k = 0; i < BOAT_FLEET_SLOTS; ++i)
            for (unsigned j = i + 1; j < BOAT_FLEET_SLOTS; ++j, ++k) {
                if (!active[i] || !active[j])
                    continue;
                ++stats.pair_tests;
                if (boat_pair_contact(&boats[i], &boats[j], &contact[k])) {
                    touching[k] = true;
                    ++count[i];
                    ++count[j];
                    ++touches;
                }
            }
        if (pass == 0)
            stats.contacts = touches;
        if (touches == 0)
            break;
        if (pass == BOAT_FLEET_SOLVER_PASSES) {
            stats.residual = touches;
            break;
        }
        ++stats.passes;
        Boat next[BOAT_FLEET_SLOTS];
        memcpy(next, boats, sizeof(next));
        for (unsigned i = 0, k = 0; i < BOAT_FLEET_SLOTS; ++i)
            for (unsigned j = i + 1; j < BOAT_FLEET_SLOTS; ++j, ++k) {
                if (!touching[k])
                    continue;
                float speed = boat_pair_speed(&boats[i], &boats[j], contact[k]);
                // The first touch fixes the pair's bounce target; later passes only make up
                // the difference, so repeated passes never bounce twice.
                float change;
                if (!seen[k]) {
                    seen[k] = true;
                    target[k] = speed < 0 ? -BOAT_CONTACT_RESTITUTION * speed : 0;
                    change = speed < 0 ? -(1 + BOAT_CONTACT_RESTITUTION) * speed : 0;
                } else {
                    change = target[k] - speed;
                }
                // Jacobi relaxation: a hull in several contacts shares its correction.
                float share = 1.0f / (float)(count[i] > count[j] ? count[i] : count[j]);
                float impulse
                    = boat_pair_impulse(contact[k], BOAT_FLEET_MASS, BOAT_FLEET_MASS, change)
                    * share;
                float total = fmaxf(0, accumulated[k] + impulse);
                impulse = total - accumulated[k];
                accumulated[k] = total;
                boat_pair_apply(&next[i], BOAT_FLEET_MASS, &next[j], BOAT_FLEET_MASS, contact[k],
                    impulse, share);
            }
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
            if (count[i] && !boat_hull_clear(&next[i], world, time)) {
                next[i].position = boats[i].position; // Never separate through the shore.
                ++stats.shore_holds;
            }
        }
        memcpy(boats, next, sizeof(next));
    }
    memcpy(stats.pair_impulse, accumulated, sizeof(accumulated));
    return stats;
}

/* One tick n -> n+1:
 * 1. Tick-boundary operations: queued spawns in request order against all placed hulls.
 * 2. Advance the shared wake pool once to n+1; births through n stay, and the water every boat
 *    reads this tick is frozen (the fleet's world never takes the D3 trail).
 * 3. Integrate each active boat from S[n] with its input and the same frozen world; no boat
 *    reads another's partial state. The results form the candidate fleet.
 * 4. Simultaneous pair contacts on the candidates (boat_fleet_solve).
 * 5. Commit, sample every wet stern from the committed fleet and the pre-emission pool, then
 *    track each emitter once. Births at n+1 contribute nothing until n+2 (zero birth envelope).
 * Presentation, course progress and every file or device stay outside. */
bool boat_fleet_step(BoatFleet *fleet, const BoatInput inputs[BOAT_FLEET_SLOTS])
{
    BoatFleetState *s = &fleet->state;
    if (s->tick == UINT64_MAX)
        return false;
    fleet->world.water.objects = &s->wakes;
    fleet->stats = (BoatFleetStats) { 0 };
    spawn_pending(fleet);
    BoatFleetStats spawns = fleet->stats;
    ++s->tick;
    boat_wake_objects_advance(&s->wakes);
    assert(s->wakes.tick == s->tick);
    double now = boat_tick_seconds(s->tick);
    Boat boats[BOAT_FLEET_SLOTS];
    bool active[BOAT_FLEET_SLOTS];
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        active[i] = s->slots[i].active;
        boats[i] = s->slots[i].boat;
        if (!active[i])
            continue;
        BoatInput input = inputs[i];
        // The authority never integrates a non-finite command.
        if (!isfinite(input.steering_rate))
            input.steering_rate = 0;
        if (!isfinite(input.throttle_rate))
            input.throttle_rate = 0;
        boat_step(&boats[i], &fleet->world, now, input);
    }
    fleet->stats = boat_fleet_solve(boats, active, &fleet->world, now);
    fleet->stats.spawned = spawns.spawned;
    fleet->stats.spawn_blocked = spawns.spawn_blocked;
    BoatWakeStern sterns[BOAT_FLEET_SLOTS];
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        if (active[i]) {
            s->slots[i].boat = boats[i];
            sterns[i] = boat_wake_stern(&boats[i], &fleet->world, now);
        }
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        if (active[i]
            && !boat_wake_objects_track(&s->wakes, i,
                (Vector2) { (float)sterns[i].x, (float)sterns[i].z },
                (float)hypot(sterns[i].vx, sterns[i].vz), (float)sterns[i].wet))
            ++fleet->stats.emission_failures;
    return true;
}

static void hash_bytes(uint64_t *hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        *hash ^= bytes[i];
        *hash *= UINT64_C(1099511628211);
    }
}

static void hash_u64(uint64_t *hash, uint64_t value)
{
    unsigned char bytes[8];
    for (int i = 0; i < 8; ++i)
        bytes[i] = (unsigned char)(value >> (8 * i));
    hash_bytes(hash, bytes, sizeof(bytes));
}

static void hash_float(uint64_t *hash, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    hash_u64(hash, bits);
}

static void hash_vector(uint64_t *hash, Vector2 value)
{
    hash_float(hash, value.x);
    hash_float(hash, value.y);
}

static void hash_boat(uint64_t *hash, const Boat *b)
{
    hash_vector(hash, b->position);
    hash_vector(hash, b->velocity);
    const float values[] = { b->heading, b->throttle, b->engine_response, b->planing, b->height,
        b->vertical_velocity, b->pitch, b->pitch_velocity, b->roll, b->roll_velocity, b->dry_time,
        b->landing_time, b->inverted_contact_time, b->turn_roll_load, b->wave_spray, b->wheel,
        b->engine_angle, b->yaw_velocity, b->physical_wake_time };
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); ++i)
        hash_float(hash, values[i]);
    hash_u64(hash, (uint64_t)b->motion_state);
    hash_u64(hash, b->landings);
    hash_vector(hash, b->physical_wake_start);
}

uint64_t boat_fleet_hash(const BoatFleetState *state)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    hash_u64(&hash, state->tick);
    hash_u64(&hash, state->next_request);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        const BoatFleetSlot *slot = &state->slots[i];
        hash_u64(&hash, slot->requested);
        hash_u64(&hash, slot->life);
        hash_u64(&hash, slot->request);
        hash_u64(&hash, slot->active);
        hash_u64(&hash, slot->pending);
        if (slot->active)
            hash_boat(&hash, &slot->boat);
    }
    const BoatWakeObjects *w = &state->wakes;
    hash_u64(&hash, w->tick);
    hash_u64(&hash, w->emitted);
    hash_u64(&hash, w->expired);
    hash_u64(&hash, w->life_ticks);
    hash_u64(&hash, w->live);
    hash_u64(&hash, w->peak_live);
    hash_float(&hash, w->crest_height);
    for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i) {
        const BoatWakeObject *o = &w->objects[i];
        hash_vector(&hash, o->position);
        hash_vector(&hash, o->direction);
        hash_float(&hash, o->length);
        hash_float(&hash, o->amplitude);
        hash_float(&hash, o->inverse_speed);
        hash_u64(&hash, o->born);
    }
    for (unsigned i = 0; i < BOAT_OBJECT_BOATS; ++i) {
        const BoatWakeObjectEmitter *e = &w->emitters[i];
        hash_vector(&hash, e->start);
        hash_u64(&hash, e->started);
        hash_u64(&hash, e->tracked);
        hash_u64(&hash, e->next);
        hash_float(&hash, e->strength);
        hash_float(&hash, e->strength_sum);
        hash_u64(&hash, e->initialized);
    }
    return hash;
}
