#include "boat_prediction.h"

#include "boat_net_server.h"
#include "raymath.h"

#include <math.h>

void boat_prediction_reset(BoatPrediction *p, const BoatFleet *authority,
    const BoatNetSlotInput inputs[BOAT_FLEET_SLOTS], unsigned slot)
{
    *p = (BoatPrediction) {
        .fleet = *authority, .authority_tick = authority->state.tick, .slot = slot
    };
    p->fleet.world.water.objects = &p->fleet.state.wakes;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        p->remote[i] = inputs[i].used;
}

void boat_prediction_record(BoatPrediction *p, uint64_t tick, uint32_t life, BoatNetCommand command)
{
    p->own[tick % BOAT_PREDICTION_HISTORY] = (BoatPredictionCommand) { tick, life, command };
}

// The authority's loss policy for the own slot, as far as the client can know it: the command
// sent for the tick, else the last one for up to BOAT_NET_REPEAT_TICKS, else zero rates.
static BoatInput own_input(const BoatPrediction *p, uint64_t tick)
{
    const BoatFleetSlot *slot = &p->fleet.state.slots[p->slot];
    for (uint64_t back = 0; back <= BOAT_NET_REPEAT_TICKS && back < tick; ++back) {
        const BoatPredictionCommand *c = &p->own[(tick - back) % BOAT_PREDICTION_HISTORY];
        if (c->tick != tick - back || c->life != slot->life)
            continue;
        BoatNetCommand command = c->command;
        if (command.flags & BOAT_NET_COMMAND_INACTIVE)
            command = boat_net_neutral_command(&slot->boat, command.flags);
        return boat_net_command_input(command);
    }
    return (BoatInput) { 0 };
}

static void step(BoatPrediction *p)
{
    if (p->authority_tick < p->release_tick && p->fleet.state.tick >= p->release_tick)
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            if (p->fleet.state.slots[i].held)
                boat_fleet_hold(&p->fleet, i, false);
    uint64_t tick = p->fleet.state.tick + 1;
    BoatInput inputs[BOAT_FLEET_SLOTS];
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        if (i == p->slot)
            inputs[i] = own_input(p, tick);
        else if (tick - p->authority_tick <= BOAT_PREDICTION_REMOTE_TICKS)
            inputs[i] = boat_net_command_input(p->remote[i]);
        else
            inputs[i] = (BoatInput) { 0 };
    }
    boat_fleet_step(&p->fleet, inputs);
}

// Reconcile by identity only: an authoritative birth replaces the speculative record that the
// replay published in the same pool slot at the same tick, never a nearby one.
static void apply_early(BoatPrediction *p, const BoatPredictionWakes *wakes)
{
    BoatWakeObjects *w = &p->fleet.state.wakes;
    for (unsigned i = 0; wakes && i < wakes->early_count; ++i) {
        const BoatNetBirth *b = &wakes->early[i];
        BoatWakeObject *o = &w->objects[b->slot];
        if (b->record.born == w->tick && o->amplitude && o->born == w->tick) {
            *o = b->record;
            ++p->stats.early_applied;
        }
    }
}

static unsigned run_to(BoatPrediction *p, uint64_t tick, const BoatPredictionWakes *wakes)
{
    unsigned steps = 0;
    while (p->fleet.state.tick < tick
        && p->fleet.state.tick < p->authority_tick + BOAT_PREDICTION_MAX_AHEAD) {
        step(p);
        apply_early(p, wakes);
        ++steps;
    }
    return steps;
}

unsigned boat_prediction_advance(BoatPrediction *p, uint64_t tick)
{
    unsigned steps = run_to(p, tick, NULL);
    p->stats.advanced_ticks += steps;
    if (p->fleet.state.tick < tick)
        p->stats.held_ticks += tick - p->fleet.state.tick;
    return steps;
}

static bool same_record(const BoatWakeObject *a, const BoatWakeObject *b)
{
    return a->position.x == b->position.x && a->position.y == b->position.y
        && a->direction.x == b->direction.x && a->direction.y == b->direction.y
        && a->length == b->length && a->amplitude == b->amplitude
        && a->inverse_speed == b->inverse_speed;
}

// Count how a correction changed the drawn and queried wake set at the speculative tick.
static void compare_wakes(BoatPrediction *p, const BoatWakeObjects *before, uint64_t tick)
{
    const BoatWakeObjects *after = &p->fleet.state.wakes;
    BoatPredictionStats *s = &p->stats;
    for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i) {
        const BoatWakeObject *a = &before->objects[i], *b = &after->objects[i];
        bool had = a->amplitude != 0, has = b->amplitude != 0;
        if (had && has && a->born == b->born) {
            if (!same_record(a, b)) {
                ++s->wakes_reshaped;
                s->visible_reshaped += tick - b->born >= BOAT_PREDICTION_VISIBLE_TICKS;
            }
            continue;
        }
        if (had) {
            ++s->wakes_vanished;
            s->visible_vanished += tick - a->born >= BOAT_PREDICTION_VISIBLE_TICKS;
        }
        if (has) {
            ++s->wakes_appeared;
            s->visible_appeared += tick - b->born >= BOAT_PREDICTION_VISIBLE_TICKS;
        }
    }
}

// POOL (the authority's) lacks births its digest counts when their batch is still on the way.
// Try the speculative records of those pool slots, all emitters first, then every smaller
// subset of emitters; keep the first combination whose digest matches exactly. True when POOL
// ends complete.
static bool carry(
    BoatPrediction *p, BoatWakeObjects *pool, const BoatWakeObjects *speculative, uint64_t digest)
{
    if (boat_net_wake_digest(pool) == digest)
        return true;
    bool candidate[BOAT_OBJECT_CAPACITY] = { 0 };
    unsigned emitters = 0;
    for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i) {
        const BoatWakeObject *s = &speculative->objects[i], *a = &pool->objects[i];
        candidate[i] = s->amplitude && s->born <= pool->tick && s->born > a->born
            && pool->tick - s->born < pool->life_ticks;
        if (candidate[i])
            emitters |= 1u << (i / BOAT_OBJECT_PER_BOAT);
    }
    BoatWakeObjects trial;
    for (unsigned mask = emitters; mask; mask = (mask - 1) & emitters) {
        trial = *pool;
        unsigned carried = 0;
        for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i) {
            if (candidate[i] && mask & 1u << (i / BOAT_OBJECT_PER_BOAT)) {
                trial.objects[i] = speculative->objects[i];
                ++carried;
            }
        }
        if (boat_net_wake_digest(&trial) == digest) {
            *pool = trial;
            p->stats.carried += carried;
            return true;
        }
    }
    return false;
}

static float angle(float a, float b) { return remainderf(a - b, 2 * PI); }

bool boat_prediction_correct(BoatPrediction *p, const BoatFleet *authority,
    const BoatNetSlotInput inputs[BOAT_FLEET_SLOTS], const BoatPredictionWakes *wakes)
{
    uint64_t target = p->fleet.state.tick;
    BoatWakeObjects wakes_before = p->fleet.state.wakes, pool = authority->state.wakes;
    if (wakes && !carry(p, &pool, &wakes_before, wakes->digest)) {
        if (wakes->hold_incomplete) {
            ++p->stats.held_corrections;
            return false;
        }
        ++p->stats.incomplete_corrections;
    }
    BoatFleetSlot before[BOAT_FLEET_SLOTS];
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        before[i] = p->fleet.state.slots[i];
    boat_fleet_restore(&p->fleet, &authority->state);
    p->fleet.state.wakes = pool;
    p->authority_tick = authority->state.tick;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        p->remote[i] = inputs[i].used;
    unsigned replay = run_to(p, target, wakes);
    p->stats.replayed_ticks += replay;
    if (replay > p->stats.max_replay)
        p->stats.max_replay = replay;
    ++p->stats.corrections;
    ++p->correction;
    bool same_tick = p->fleet.state.tick == target;
    if (same_tick)
        compare_wakes(p, &wakes_before, target);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        const BoatFleetSlot *after = &p->fleet.state.slots[i];
        BoatPredictionError *e = &p->error[i];
        *e = (BoatPredictionError) { .snap = true };
        if (!same_tick || !before[i].active || !after->active || before[i].life != after->life)
            continue;
        const Boat *a = &before[i].boat, *b = &after->boat;
        *e = (BoatPredictionError) { Vector2Subtract(a->position, b->position),
            angle(a->heading, b->heading), a->height - b->height, angle(a->pitch, b->pitch),
            angle(a->roll, b->roll), false };
        float distance = Vector2Length(e->position);
        e->snap = distance > BOAT_PREDICTION_SNAP_DISTANCE
            || fabsf(e->heading) > BOAT_PREDICTION_SNAP_ANGLE
            || fabsf(e->pitch) > BOAT_PREDICTION_SNAP_ANGLE
            || fabsf(e->roll) > BOAT_PREDICTION_SNAP_ANGLE;
        p->stats.snaps += e->snap;
        if (i == p->slot) {
            p->stats.total_error += distance;
            p->stats.max_error = fmaxf(p->stats.max_error, distance);
        } else {
            p->stats.total_remote_error += distance;
            p->stats.max_remote_error = fmaxf(p->stats.max_remote_error, distance);
            ++p->stats.remote_errors;
        }
    }
    return true;
}
