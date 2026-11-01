#include "boat_race.h"
#include "boat_map.h"
#include "raymath.h"

#include <math.h>

// Recovery candidates behind an anchor gate (ahead of it after a finish), in lateral order.
static const float RECOVERY_BACK[] = { 25, 45 };
static const float RECOVERY_SIDE[] = { 0, -18, 18, -36, 36, -54, 54 };
// Parking past the finish, facing back up-course: off the finish line's centre first.
static const float PARK_AHEAD[] = { 70, 100 };
static const float PARK_SIDE[] = { -40, 40, -80, 80, -120, 120, 0 };

static bool gate_valid(BoatRaceGate *gate)
{
    float length = Vector2Length(gate->direction);
    if (!isfinite(gate->centre.x) || !isfinite(gate->centre.y) || !isfinite(length)
        || fabsf(length - 1) > .01f || !(gate->half_width > 0))
        return false;
    gate->direction = Vector2Scale(gate->direction, 1 / length);
    return true;
}

bool boat_race_init(BoatRace *race, const BoatRaceRules *rules, const BoatRaceCourse *course)
{
    if (!rules || !course || !rules->countdown_ticks || !rules->cap_ticks
        || rules->skip_ticks > rules->recovery_ticks || rules->min_players < 1
        || rules->min_players > BOAT_FLEET_SLOTS || course->gate_count < 1
        || course->gate_count > BOAT_RACE_GATES || !isfinite(course->storm.centre.x)
        || !isfinite(course->storm.centre.y) || !isfinite(course->storm.velocity.x)
        || !isfinite(course->storm.velocity.y) || !isfinite(course->storm.radius)
        || course->storm.radius < 0 || !isfinite(course->storm.height) || course->storm.height < 0
        || !isfinite(course->storm.started)
        || (course->source.height > 0 && !boat_vessel_wake_valid(course->source)))
        return false;
    *race = (BoatRace) { .rules = *rules, .course = *course };
    BoatRaceCourse *c = &race->course;
    if (!gate_valid(&c->start))
        return false;
    for (unsigned i = 0; i < c->gate_count; ++i)
        if (!gate_valid(&c->gates[i]))
            return false;
    return true;
}

static void emit(BoatRaceOutput *out, BoatRaceEvent event)
{
    if (out && out->count < BOAT_RACE_MAX_EVENTS)
        out->events[out->count++] = event;
}

static void phase_event(BoatRace *r, uint64_t tick, BoatRaceOutput *out)
{
    emit(out,
        (BoatRaceEvent) {
            .tick = tick, .kind = BOAT_RACE_PHASE, .slot = BOAT_RACE_NOBODY, .value = r->phase });
}

bool boat_race_entrant(const BoatRace *r, unsigned slot)
{
    return slot < BOAT_FLEET_SLOTS && r->slots[slot].identity
        && r->entrants[slot].identity == r->slots[slot].identity;
}

static bool held_phase(const BoatRace *r)
{
    return r->phase == BOAT_RACE_GRID || r->phase == BOAT_RACE_COUNTDOWN;
}

bool boat_race_join(BoatRace *r, BoatFleet *fleet, unsigned slot)
{
    if (slot >= BOAT_FLEET_SLOTS || r->slots[slot].identity)
        return false;
    r->slots[slot] = (BoatRaceSlot) { .identity = ++r->joins };
    if (r->phase == BOAT_RACE_WAITING)
        boat_fleet_request_spawn(fleet, slot);
    return true;
}

void boat_race_leave(BoatRace *r, BoatFleet *fleet, unsigned slot)
{
    if (slot >= BOAT_FLEET_SLOTS)
        return;
    // A running entrant's row stays behind; observe reports it as DNF.
    r->slots[slot] = (BoatRaceSlot) { 0 };
    boat_fleet_leave(fleet, slot);
}

float boat_race_cross(const BoatRaceGate *gate, Vector2 a, Vector2 b)
{
    float da = Vector2DotProduct(Vector2Subtract(a, gate->centre), gate->direction);
    float db = Vector2DotProduct(Vector2Subtract(b, gate->centre), gate->direction);
    if (!(da < 0 && db >= 0))
        return -1; // Only a forward pass counts; reversing through a line never does.
    float fraction = da / (da - db);
    Vector2 at = Vector2Lerp(a, b, fraction);
    Vector2 side = { -gate->direction.y, gate->direction.x };
    return fabsf(Vector2DotProduct(Vector2Subtract(at, gate->centre), side)) <= gate->half_width
        ? fraction
        : -1;
}

// The gate an entrant with GATES cleared measures from: the start, then each checkpoint.
static const BoatRaceGate *anchor(const BoatRace *r, unsigned gates)
{
    return gates ? &r->course.gates[gates - 1] : &r->course.start;
}

float boat_race_progress(const BoatRace *r, const BoatFleet *fleet, unsigned slot)
{
    const BoatRaceEntrant *e = &r->entrants[slot];
    if (slot >= BOAT_FLEET_SLOTS || e->gates >= r->course.gate_count)
        return 0;
    Vector2 from = anchor(r, e->gates)->centre, to = r->course.gates[e->gates].centre;
    Vector2 segment = Vector2Subtract(to, from);
    return Vector2DotProduct(Vector2Subtract(fleet->state.slots[slot].boat.position, from), segment)
        / Vector2Length(segment);
}

// Order of two running entrants: more gates, then further along the same segment.
static bool ahead(const BoatRace *r, const BoatFleet *fleet, unsigned a, unsigned b)
{
    const BoatRaceEntrant *ea = &r->entrants[a], *eb = &r->entrants[b];
    if (ea->gates != eb->gates)
        return ea->gates > eb->gates;
    return boat_race_progress(r, fleet, a) > boat_race_progress(r, fleet, b);
}

unsigned boat_race_position(const BoatRace *r, const BoatFleet *fleet, unsigned slot)
{
    if (slot >= BOAT_FLEET_SLOTS || !r->entrants[slot].identity)
        return 0;
    const BoatRaceEntrant *e = &r->entrants[slot];
    if (e->status == BOAT_RACE_FINISHED)
        return e->place;
    unsigned position = r->placed + 1;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        const BoatRaceEntrant *o = &r->entrants[i];
        if (i == slot || !o->identity || o->status == BOAT_RACE_FINISHED)
            continue;
        if (e->status == BOAT_RACE_DNF)
            position += o->status == BOAT_RACE_RUNNING || (o->status == BOAT_RACE_DNF && i < slot);
        else
            position += o->status == BOAT_RACE_RUNNING && ahead(r, fleet, i, slot);
    }
    return position;
}

static Boat facing(Vector2 position, Vector2 direction)
{
    Boat boat;
    boat_reset(&boat);
    boat.position = position;
    boat.heading = atan2f(direction.x, -direction.y);
    return boat;
}

// Enumerate the engine's bounded placement recipe for staging and live recovery alike.
bool boat_race_recovery_candidate(const BoatRaceGate *g, bool finished, unsigned index, Boat *boat)
{
    if (index >= 14)
        return false;
    const float *along = finished ? PARK_AHEAD : RECOVERY_BACK;
    const float *across = finished ? PARK_SIDE : RECOVERY_SIDE;
    float side_offset = across[index % 7];
    if (fabsf(side_offset) > g->half_width)
        return false;
    Vector2 side = { -g->direction.y, g->direction.x };
    Vector2 at = Vector2Add(g->centre,
        Vector2Add(Vector2Scale(g->direction, finished ? along[index / 7] : -along[index / 7]),
            Vector2Scale(side, side_offset)));
    *boat = facing(at, finished ? Vector2Negate(g->direction) : g->direction);
    return true;
}

// Place an entrant at the first clear point behind its last cleared gate. Never beyond an
// uncleared gate; motion and crossing history start afresh. A finisher is parked instead: held
// past the finish, out of the corridor, facing back towards the line.
static bool recover(
    BoatRace *r, BoatFleet *fleet, unsigned slot, uint64_t tick, BoatRaceOutput *out)
{
    const BoatRaceEntrant *e = &r->entrants[slot];
    bool finished = e->status == BOAT_RACE_FINISHED;
    const BoatRaceGate *g = anchor(r, e->gates);
    for (unsigned index = 0; index < 14; ++index) {
        Boat boat;
        if (!boat_race_recovery_candidate(g, finished, index, &boat))
            continue;
        if (!boat_fleet_spawn_clear(fleet, slot, &boat))
            continue;
        boat_fleet_place(fleet, slot, boat);
        r->slots[slot].recovering = false;
        r->slots[slot].reset_wanted = false;
        if (finished) {
            boat_fleet_hold(fleet, slot, true);
            r->slots[slot].parked = true;
        } else {
            ++r->entrants[slot].recoveries;
        }
        emit(out,
            (BoatRaceEvent) { .tick = tick,
                .kind = finished ? BOAT_RACE_PARK : BOAT_RACE_RECOVER,
                .slot = (uint8_t)slot });
        return true;
    }
    return false; // Stays recovering and retries on the next observation.
}

// Recovery during a round for entrants; an ordinary fleet spawn otherwise.
static void request_recovery(
    BoatRace *r, BoatFleet *fleet, unsigned slot, uint64_t tick, BoatRaceOutput *out)
{
    if (r->phase == BOAT_RACE_RACING && boat_race_entrant(r, slot)) {
        r->slots[slot].recovering = true;
        recover(r, fleet, slot, tick, out);
    } else {
        boat_fleet_request_spawn(fleet, slot);
        r->slots[slot].reset_wanted = false;
    }
}

void boat_race_reset(BoatRace *r, BoatFleet *fleet, unsigned slot)
{
    if (slot >= BOAT_FLEET_SLOTS || !r->slots[slot].identity || held_phase(r)
        || !fleet->state.slots[slot].active || fleet->state.slots[slot].held)
        return; // Gridded and parked boats stay put; unplaced joiners wait for the grid.
    r->slots[slot].reset_wanted = true; // Observe applies it, after skip_ticks if capsized.
}

static unsigned present(const BoatRace *r)
{
    unsigned count = 0;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        count += r->slots[i].identity != 0;
    return count;
}

void boat_race_bind(const BoatRace *r, BoatFleet *fleet)
{
    // The ferry runs on the round's clock with its fixed pre-roll before GO.
    fleet->world.water.vessel = (BoatVesselWake) { 0 };
    fleet->world.water.storm = (BoatStorm) { 0 };
    if (r->round) {
        fleet->world.water.storm = r->course.storm;
        fleet->world.water.storm.started += boat_tick_seconds(r->go_tick);
    }
    if (r->round && r->course.source.height > 0) {
        fleet->world.water.vessel = r->course.source;
        fleet->world.water.vessel.started += boat_tick_seconds(r->go_tick);
    }
}

static void start_grid(BoatRace *r, BoatFleet *fleet, uint64_t tick, BoatRaceOutput *out)
{
    r->phase = BOAT_RACE_GRID;
    r->phase_end = tick + r->rules.grid_ticks;
    r->go_tick = r->phase_end + r->rules.countdown_ticks;
    r->deadline = 0;
    r->placed = 0;
    ++r->round;
    // The roster freezes now. Grid places rotate by one each round.
    unsigned order = 0;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        BoatRaceSlot *s = &r->slots[i];
        r->entrants[i] = (BoatRaceEntrant) { .identity = s->identity };
        if (!s->identity)
            continue;
        unsigned place = (order++ + r->round - 1) % BOAT_FLEET_SLOTS;
        boat_fleet_place(fleet, i, facing(r->course.grid[place], r->course.start.direction));
        boat_fleet_hold(fleet, i, true);
        s->recovering = s->reset_wanted = s->parked = false;
    }
    boat_race_bind(r, fleet);
    phase_event(r, tick, out);
}

static void start_results(BoatRace *r, BoatFleet *fleet, uint64_t tick, BoatRaceOutput *out)
{
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        BoatRaceEntrant *e = &r->entrants[i];
        if (e->identity && e->status == BOAT_RACE_RUNNING) {
            e->status = BOAT_RACE_DNF;
            emit(out, (BoatRaceEvent) { .tick = tick, .kind = BOAT_RACE_OUT, .slot = (uint8_t)i });
        }
        r->slots[i].recovering = false;
    }
    r->phase = BOAT_RACE_RESULTS;
    r->phase_end = tick + r->rules.results_ticks;
    phase_event(r, tick, out);
    (void)fleet;
}

// Back to free boating: everyone present and unplaced takes an ordinary spawn.
static void start_waiting(BoatRace *r, BoatFleet *fleet, uint64_t tick, BoatRaceOutput *out)
{
    r->phase = BOAT_RACE_WAITING;
    r->phase_end = r->deadline = 0;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        if (fleet->state.slots[i].held)
            boat_fleet_hold(fleet, i, false);
        r->slots[i].recovering = r->slots[i].parked = false;
        if (r->slots[i].identity && !fleet->state.slots[i].active)
            boat_fleet_request_spawn(fleet, i);
    }
    phase_event(r, tick, out);
}

static unsigned running(const BoatRace *r)
{
    unsigned count = 0;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        count += r->entrants[i].identity && r->entrants[i].status == BOAT_RACE_RUNNING;
    return count;
}

static bool any_entrant_present(const BoatRace *r)
{
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        if (boat_race_entrant(r, i))
            return true;
    return false;
}

// Forward crossings of the next gates over one observed segment, in order. Finishers of this
// observation are returned with their interpolated times for place resolution.
static void crossings(BoatRace *r, const BoatFleet *fleet, uint64_t tick, BoatRaceOutput *out,
    bool finishers[BOAT_FLEET_SLOTS])
{
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        BoatRaceEntrant *e = &r->entrants[i];
        const BoatRaceSlot *s = &r->slots[i];
        const BoatFleetSlot *f = &fleet->state.slots[i];
        if (!boat_race_entrant(r, i) || e->status != BOAT_RACE_RUNNING || !f->active || !s->tracked
            || s->life != f->life)
            continue;
        Vector2 a = s->previous, b = f->boat.position;
        float from = 0;
        while (e->gates < r->course.gate_count) {
            const BoatRaceGate *gate = &r->course.gates[e->gates];
            float fraction = boat_race_cross(gate, a, b);
            if (fraction < from)
                break;
            from = fraction;
            if (++e->gates == r->course.gate_count) {
                // The segment covers tick-1 -> tick.
                double ticks = (double)(tick - 1 - r->go_tick) + fraction;
                e->finish = (uint64_t)llround(ticks * BOAT_RACE_SUBTICKS);
                e->status = BOAT_RACE_FINISHED;
                finishers[i] = true;
            } else {
                emit(out,
                    (BoatRaceEvent) { .tick = tick,
                        .kind = BOAT_RACE_GATE,
                        .slot = (uint8_t)i,
                        .value = (uint8_t)(e->gates - 1) });
            }
        }
    }
}

// Places for this observation's finishers, by interpolated time; equal times share a place.
static void award(BoatRace *r, uint64_t tick, BoatRaceOutput *out, const bool finishers[])
{
    unsigned base = r->placed;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        if (!finishers[i])
            continue;
        unsigned place = base + 1;
        for (unsigned j = 0; j < BOAT_FLEET_SLOTS; ++j)
            place += finishers[j] && r->entrants[j].finish < r->entrants[i].finish;
        r->entrants[i].place = (uint8_t)place;
        ++r->placed;
    }
    // Events in place order, then slot order.
    for (unsigned place = base + 1; place <= r->placed; ++place)
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            if (finishers[i] && r->entrants[i].place == place)
                emit(out,
                    (BoatRaceEvent) { .tick = tick,
                        .time = r->entrants[i].finish,
                        .kind = BOAT_RACE_FINISH,
                        .slot = (uint8_t)i,
                        .value = (uint8_t)place });
    if (!base && r->placed) {
        uint64_t extra = tick + r->rules.after_winner_ticks;
        if (extra < r->deadline)
            r->deadline = extra;
    }
}

static void phases(BoatRace *r, BoatFleet *fleet, uint64_t tick, BoatRaceOutput *out)
{
    switch (r->phase) {
    case BOAT_RACE_WAITING:
        if (present(r) >= r->rules.min_players)
            start_grid(r, fleet, tick, out);
        break;
    case BOAT_RACE_GRID:
    case BOAT_RACE_COUNTDOWN:
    case BOAT_RACE_RACING:
        if (!any_entrant_present(r)) {
            emit(out,
                (BoatRaceEvent) {
                    .tick = tick, .kind = BOAT_RACE_ABANDON, .slot = BOAT_RACE_NOBODY });
            start_waiting(r, fleet, tick, out);
        } else if (r->phase == BOAT_RACE_GRID && tick >= r->phase_end) {
            r->phase = BOAT_RACE_COUNTDOWN;
            r->phase_end = r->go_tick;
            phase_event(r, tick, out);
        } else if (r->phase == BOAT_RACE_COUNTDOWN && tick >= r->go_tick) {
            // Everyone is released on the same tick boundary.
            for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
                if (fleet->state.slots[i].held)
                    boat_fleet_hold(fleet, i, false);
            r->phase = BOAT_RACE_RACING;
            r->phase_end = 0;
            r->deadline = r->go_tick + r->rules.cap_ticks;
            phase_event(r, tick, out);
        } else if (r->phase == BOAT_RACE_RACING && (!running(r) || tick >= r->deadline)) {
            start_results(r, fleet, tick, out);
        }
        break;
    case BOAT_RACE_RESULTS:
        if (tick < r->phase_end)
            break;
        if (present(r) >= r->rules.min_players)
            start_grid(r, fleet, tick, out);
        else
            start_waiting(r, fleet, tick, out);
        break;
    }
}

void boat_race_observe(BoatRace *r, BoatFleet *fleet, BoatRaceOutput *out)
{
    *out = (BoatRaceOutput) { 0 };
    uint64_t tick = fleet->state.tick;
    // Entrants who left are out; their rows and any finish stay.
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        BoatRaceEntrant *e = &r->entrants[i];
        if (e->identity && e->status == BOAT_RACE_RUNNING && !boat_race_entrant(r, i)
            && r->phase != BOAT_RACE_WAITING && r->phase != BOAT_RACE_RESULTS) {
            e->status = BOAT_RACE_DNF;
            emit(out, (BoatRaceEvent) { .tick = tick, .kind = BOAT_RACE_OUT, .slot = (uint8_t)i });
        }
    }
    bool finishers[BOAT_FLEET_SLOTS] = { 0 };
    if (r->phase == BOAT_RACE_RACING) {
        crossings(r, fleet, tick, out, finishers);
        award(r, tick, out, finishers);
    }
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        BoatRaceSlot *s = &r->slots[i];
        const BoatFleetSlot *f = &fleet->state.slots[i];
        if (!s->identity || !f->active)
            continue;
        if (s->life != f->life) {
            s->capsized = false;
            s->reset_wanted = false;
        }
        if (!s->capsized && f->boat.motion_state == BOAT_CAPSIZED) {
            s->capsized = true;
            s->capsized_at = tick;
        }
        const BoatRaceEntrant *e = &r->entrants[i];
        bool finisher = boat_race_entrant(r, i) && e->status == BOAT_RACE_FINISHED
            && (r->phase == BOAT_RACE_RACING || r->phase == BOAT_RACE_RESULTS);
        if (s->parked) {
            continue;
        } else if (s->recovering) {
            recover(r, fleet, i, tick, out);
        } else if (finisher) {
            // The finish observation's tick, from the stored time after GO.
            uint64_t finished = r->go_tick + 1 + e->finish / BOAT_RACE_SUBTICKS;
            if (tick >= finished + r->rules.park_ticks || s->capsized || s->reset_wanted) {
                s->recovering = true;
                recover(r, fleet, i, tick, out);
            }
        } else if (!f->pending && !held_phase(r)) {
            uint64_t since = tick - s->capsized_at;
            if ((s->capsized && since >= r->rules.recovery_ticks)
                || (s->reset_wanted && (!s->capsized || since >= r->rules.skip_ticks)))
                request_recovery(r, fleet, i, tick, out);
        }
    }
    phases(r, fleet, tick, out);
    // Crossing history for the next observation; a new life starts without a segment.
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        BoatRaceSlot *s = &r->slots[i];
        const BoatFleetSlot *f = &fleet->state.slots[i];
        s->tracked = s->identity && f->active;
        if (s->life != f->life) // Placed by the race during this observation.
            s->capsized = false;
        s->life = f->life;
        s->previous = f->boat.position;
    }
}
