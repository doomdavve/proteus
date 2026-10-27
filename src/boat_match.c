#include "boat_match.h"

#include <math.h>

BoatMatchRules boat_match_default_rules(void)
{
    return (BoatMatchRules) { .round_ticks = 300 * BOAT_TICK_HZ,
        .intermission_ticks = 10 * BOAT_TICK_HZ,
        .credit_ticks = 5 * BOAT_TICK_HZ,
        .grace_ticks = 2 * BOAT_TICK_HZ,
        .respawn_ticks = 3 * BOAT_TICK_HZ,
        .skip_ticks = 1 * BOAT_TICK_HZ,
        .contact_impulse = 1.5f,
        .wake_height = .35f,
        .min_players = 2 };
}

bool boat_match_init(BoatMatch *match, const BoatMatchRules *rules)
{
    if (!rules || !rules->round_ticks || !rules->credit_ticks || !(rules->contact_impulse > 0)
        || !(rules->wake_height > 0) || rules->min_players < 1
        || rules->min_players > BOAT_FLEET_SLOTS || rules->skip_ticks > rules->respawn_ticks)
        return false;
    *match = (BoatMatch) { .rules = *rules };
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        match->slots[i].touch.slot = BOAT_MATCH_NOBODY;
    return true;
}

static void emit(BoatMatchOutput *out, BoatMatchEvent event)
{
    if (out->count < BOAT_MATCH_MAX_EVENTS)
        out->events[out->count++] = event;
}

static bool in_grace(const BoatMatch *m, unsigned slot, uint64_t tick)
{
    return tick - m->slots[slot].life_start < m->rules.grace_ticks;
}

// A slot that may touch or be touched this tick: placed, past its grace and still upright.
static bool touchable(const BoatMatch *m, const BoatFleet *fleet, unsigned slot, uint64_t tick)
{
    return fleet->state.slots[slot].active && m->slots[slot].present && !m->slots[slot].out
        && !in_grace(m, slot, tick);
}

static void touch(BoatMatch *m, unsigned victim, unsigned by, uint8_t cause, uint64_t tick)
{
    m->slots[victim].touch = (BoatMatchTouch) { .tick = tick, .slot = (uint8_t)by, .cause = cause };
}

// The credited toucher of VICTIM at TICK, or BOAT_MATCH_NOBODY.
static unsigned credited(const BoatMatch *m, unsigned victim, uint64_t tick)
{
    const BoatMatchTouch *t = &m->slots[victim].touch;
    if (t->slot == BOAT_MATCH_NOBODY || !m->slots[t->slot].present
        || tick - t->tick > m->rules.credit_ticks)
        return BOAT_MATCH_NOBODY;
    return t->slot;
}

// Mark VICTIM out for this life; during a round, score it and report the event.
static void capsize(BoatMatch *m, unsigned victim, uint32_t life, uint64_t tick, bool bailout,
    uint8_t reason, BoatMatchOutput *out)
{
    BoatMatchSlot *s = &m->slots[victim];
    s->out = true;
    s->capsized_at = tick;
    if (m->phase != BOAT_MATCH_PLAYING)
        return;
    unsigned by = credited(m, victim, tick);
    ++m->rows[victim].capsized;
    if (by == BOAT_MATCH_NOBODY) {
        --m->rows[victim].score;
        ++m->rows[victim].self;
    } else {
        ++m->rows[by].score;
        ++m->rows[by].caused;
    }
    emit(out,
        (BoatMatchEvent) { .tick = tick,
            .victim_life = life,
            .kind = BOAT_MATCH_CAPSIZE,
            .victim = (uint8_t)victim,
            .attacker = (uint8_t)by,
            .cause = by == BOAT_MATCH_NOBODY ? BOAT_MATCH_CAUSE_NONE : s->touch.cause,
            .reason = reason,
            .bailout = bailout });
}

static void presence(BoatMatch *m, const BoatFleet *fleet, uint64_t tick)
{
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        const BoatFleetSlot *f = &fleet->state.slots[i];
        BoatMatchSlot *s = &m->slots[i];
        bool present = f->active || f->pending;
        if (s->present && !present) {
            *s = (BoatMatchSlot) { .touch.slot = BOAT_MATCH_NOBODY };
            m->rows[i] = (BoatMatchRow) { 0 };
            for (unsigned j = 0; j < BOAT_FLEET_SLOTS; ++j)
                if (m->slots[j].touch.slot == i)
                    m->slots[j].touch.slot = BOAT_MATCH_NOBODY; // Credit dies with the player.
        } else if (!s->present && present) {
            *s = (BoatMatchSlot) { .touch.slot = BOAT_MATCH_NOBODY,
                .life = f->life,
                .life_start
                = fleet->state.combat.enabled ? fleet->state.combat.players[i].life_start : tick,
                .present = true };
            m->rows[i] = (BoatMatchRow) { 0 }; // Joiners start at zero.
        }
        if (f->active && f->life != s->life) {
            // A new life: grace, a clean touch record and nothing pending.
            *s = (BoatMatchSlot) { .touch.slot = BOAT_MATCH_NOBODY,
                .life = f->life,
                .life_start
                = fleet->state.combat.enabled ? fleet->state.combat.players[i].life_start : tick,
                .present = true,
                .airborne = f->boat.motion_state == BOAT_AIRBORNE };
        }
    }
}

static unsigned players(const BoatMatch *m)
{
    unsigned count = 0;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        count += m->slots[i].present;
    return count;
}

static void start_round(BoatMatch *m, uint64_t tick, BoatMatchOutput *out)
{
    m->phase = BOAT_MATCH_PLAYING;
    m->phase_end = tick + m->rules.round_ticks;
    ++m->round;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        m->rows[i] = (BoatMatchRow) { 0 };
        if (m->slots[i].present) {
            out->spawn[i] = true; // Everyone starts the round from a fresh spawn.
            m->slots[i].respawn_requested = true;
        }
    }
    emit(out,
        (BoatMatchEvent) { .tick = tick,
            .kind = BOAT_MATCH_ROUND_START,
            .victim = BOAT_MATCH_NOBODY,
            .attacker = BOAT_MATCH_NOBODY });
}

static void end_round(BoatMatch *m, uint64_t tick, BoatMatchOutput *out, bool intermission)
{
    m->phase = intermission ? BOAT_MATCH_INTERMISSION : BOAT_MATCH_WARMUP;
    m->phase_end = intermission ? tick + m->rules.intermission_ticks : 0;
    emit(out,
        (BoatMatchEvent) { .tick = tick,
            .kind = BOAT_MATCH_ROUND_END,
            .victim = BOAT_MATCH_NOBODY,
            .attacker = BOAT_MATCH_NOBODY });
}

static void rounds(BoatMatch *m, uint64_t tick, BoatMatchOutput *out)
{
    bool enough = players(m) >= m->rules.min_players;
    switch (m->phase) {
    case BOAT_MATCH_WARMUP:
        if (enough)
            start_round(m, tick, out);
        break;
    case BOAT_MATCH_PLAYING:
        if (!enough)
            end_round(m, tick, out, false); // Abandoned: back to free warmup play.
        else if (tick >= m->phase_end)
            end_round(m, tick, out, true);
        break;
    case BOAT_MATCH_INTERMISSION:
        if (tick < m->phase_end)
            break;
        if (enough) {
            start_round(m, tick, out);
        } else {
            m->phase = BOAT_MATCH_WARMUP;
            m->phase_end = 0;
        }
        break;
    }
}

static void touches(BoatMatch *m, const BoatFleet *fleet, uint64_t tick)
{
    const BoatWakeObjects *wakes = &fleet->state.wakes;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        const Boat *boat = &fleet->state.slots[i].boat;
        bool airborne = fleet->state.slots[i].active && boat->motion_state == BOAT_AIRBORNE;
        bool launched = airborne && !m->slots[i].airborne;
        m->slots[i].airborne = airborne;
        if (!touchable(m, fleet, i, tick))
            continue;
        // The strongest wake under the hull; a launch credits whichever emitter dominated.
        float strongest = 0, other = 0;
        unsigned owner = BOAT_MATCH_NOBODY, source = BOAT_MATCH_NOBODY;
        for (unsigned e = 0; e < BOAT_FLEET_SLOTS; ++e) {
            float h = fabsf(boat_wake_objects_query_emitter(wakes, e, boat->position).height);
            if (h > strongest) {
                strongest = h;
                owner = e;
            }
            if (e != i && m->slots[e].present && !in_grace(m, e, tick) && h > other) {
                other = h;
                source = e;
            }
        }
        if (source != BOAT_MATCH_NOBODY
            && (other >= m->rules.wake_height || (launched && owner == source)))
            touch(m, i, source, BOAT_MATCH_WAKE, tick);
        if (fleet->state.combat.enabled) {
            const BoatCombat *c = &fleet->state.combat;
            for (unsigned k = 0; k < BOAT_COMBAT_WAVES; ++k) {
                const BoatRadialWave *w = &c->waves[k];
                if (!w->active || w->owner == i || !m->slots[w->owner].present
                    || w->generation != c->players[w->owner].generation
                    || in_grace(m, w->owner, tick) || w->born < m->slots[i].life_start)
                    continue;
                float h
                    = boat_water_radial_query(*w, boat->position, boat_fleet_time(fleet)).height;
                if (h >= m->rules.wake_height && h > other) {
                    touch(m, i, w->owner, BOAT_MATCH_GRENADE, tick);
                    other = h;
                }
            }
        }
    }
    // Contacts after wakes: a hit on the same tick is the more direct cause.
    for (unsigned i = 0, k = 0; i < BOAT_FLEET_SLOTS; ++i)
        for (unsigned j = i + 1; j < BOAT_FLEET_SLOTS; ++j, ++k) {
            if (fleet->stats.pair_impulse[k] < m->rules.contact_impulse
                || !touchable(m, fleet, i, tick) || !touchable(m, fleet, j, tick))
                continue;
            touch(m, i, j, BOAT_MATCH_CONTACT, tick);
            touch(m, j, i, BOAT_MATCH_CONTACT, tick);
        }
}

void boat_match_observe(BoatMatch *m, const BoatFleet *fleet, BoatMatchOutput *out)
{
    *out = (BoatMatchOutput) { 0 };
    uint64_t tick = fleet->state.tick;
    presence(m, fleet, tick);
    rounds(m, tick, out);
    touches(m, fleet, tick);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        const BoatFleetSlot *f = &fleet->state.slots[i];
        BoatMatchSlot *s = &m->slots[i];
        if (!s->present || !f->active)
            continue;
        const BoatCombat *c = &fleet->state.combat;
        bool destroyed = c->enabled && c->players[i].health <= 0;
        const BoatCombatPlayer *p = &c->players[i];
        if (c->enabled && !s->out && p->pressure_tick && p->pressure_tick <= tick
            && tick - p->pressure_tick <= m->rules.credit_ticks
            && p->pressure_owner < BOAT_FLEET_SLOTS
            && p->pressure_generation == c->players[p->pressure_owner].generation
            && m->slots[p->pressure_owner].present && !in_grace(m, i, tick)
            && p->pressure_tick >= s->touch.tick)
            touch(m, i, p->pressure_owner, BOAT_MATCH_CANNON, p->pressure_tick);
        if (!s->out && destroyed && c->players[i].killer < BOAT_FLEET_SLOTS)
            touch(m, i, c->players[i].killer, BOAT_MATCH_TORPEDO, tick);
        if (!s->out && (destroyed || f->boat.motion_state == BOAT_CAPSIZED))
            capsize(m, i, f->life, tick, false,
                destroyed ? BOAT_MATCH_DEFEAT_HULL : BOAT_MATCH_DEFEAT_CAPSIZE, out);
        uint64_t since = tick - s->capsized_at;
        if (s->out && !s->respawn_requested
            && (since >= m->rules.respawn_ticks
                || (s->reset_wanted && since >= m->rules.skip_ticks))) {
            out->spawn[i] = true;
            s->respawn_requested = true;
        }
    }
}

bool boat_match_reset(BoatMatch *m, const BoatFleet *fleet, unsigned slot, BoatMatchOutput *out)
{
    *out = (BoatMatchOutput) { 0 };
    if (slot >= BOAT_FLEET_SLOTS)
        return false;
    const BoatFleetSlot *f = &fleet->state.slots[slot];
    BoatMatchSlot *s = &m->slots[slot];
    uint64_t tick = fleet->state.tick;
    if (!f->active || !s->present)
        return true; // Not placed yet: the fleet's own spawn rules apply.
    if (s->out) {
        if (tick - s->capsized_at < m->rules.skip_ticks) {
            s->reset_wanted = true;
            return false;
        }
        s->respawn_requested = true;
        return true;
    }
    // Resetting an upright boat cannot deny a capsize to whoever touched it last.
    if (m->phase == BOAT_MATCH_PLAYING && credited(m, slot, tick) != BOAT_MATCH_NOBODY)
        capsize(m, slot, f->life, tick, true, BOAT_MATCH_DEFEAT_CAPSIZE, out);
    s->respawn_requested = s->out;
    return true;
}

unsigned boat_match_rank(const BoatMatch *m, unsigned slot)
{
    unsigned rank = 1;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        rank += m->slots[i].present && m->rows[i].score > m->rows[slot].score;
    return rank;
}
