#include "wake_velocity_voyage.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum { EXPOSED = 1, ARRIVED = 2, TOO_FAST = 4, OBSERVED = 8, WEATHER = 16 };

#ifdef __EMSCRIPTEN__
// The browser evaluates objectives only through its isolated worker (L4b): the game always gives
// the adapter one, and a migration (authoring) does not reach the browser's authority yet.
static bool wake_objective_migrate(WakeObjective *run, const BoatPluginRevision *candidate,
    unsigned active, uint64_t tick, BoatPluginObjective *migrated)
{
    (void)candidate, (void)active, (void)tick, (void)migrated;
    snprintf(run->objective_diagnostic.message, sizeof(run->objective_diagnostic.message),
        "the browser's local authority runs no migration");
    return false;
}
#endif

// INIT of REVISION's objective at TICK: on the isolated worker when the adapter has one (the
// result arrives in a later service), otherwise at once.
static void init_objective(
    WakeVoyageAuthority *v, const BoatPluginRevision *revision, uint64_t tick)
{
    if (v->worker) {
        wake_objective_begin(&v->objective, &v->job, v->worker, BOAT_PLUGIN_OBJECTIVE_INIT,
            revision, revision->number, v->objective.generation, tick, v->now);
        return;
    }
#ifdef __EMSCRIPTEN__
    v->objective.objective.quarantined = true;
    v->objective.objective_diagnostic = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_IO };
    snprintf(v->objective.objective_diagnostic.message,
        sizeof(v->objective.objective_diagnostic.message), "no isolated objective evaluator");
#else
    wake_objective_init(&v->objective, revision, tick);
#endif
}

// The update at boundary TICK, as init_objective runs INIT.
static void update_objective(WakeVoyageAuthority *v, uint64_t tick)
{
    if (v->worker) {
        wake_objective_begin(&v->objective, &v->job, v->worker, BOAT_PLUGIN_OBJECTIVE_UPDATE,
            v->revision, v->revision->number, v->objective.generation, tick, v->now);
        return;
    }
#ifndef __EMSCRIPTEN__
    wake_objective_update(&v->objective, v->revision, tick);
#endif
}

void wake_voyage_init(WakeVoyageAuthority *voyage, const BoatSession *session, unsigned slot)
{
    // The objective is large; clear it in place rather than through a temporary. Its trace is
    // inspection history and spans generations, as the direct path's does.
    static WakePluginTrace trace[WAKE_PLUGIN_TRACE];
    unsigned trace_count = voyage->objective.trace_count;
    memcpy(trace, voyage->objective.trace, sizeof(trace));
    wake_objective_cancel(&voyage->job, voyage->worker); // A new generation answers nothing old.
    const WakeObjectiveWorker *worker = voyage->worker;
    memset(voyage, 0, sizeof(*voyage));
    voyage->worker = worker;
    voyage->now = NAN; // The first service's; a job's deadline counts from it.
    memcpy(voyage->objective.trace, trace, sizeof(trace));
    voyage->objective.trace_count = trace_count;
    voyage->slot = slot;
    voyage->voyage = session->voyage;
}

void wake_voyage_revision(WakeVoyageAuthority *voyage, const BoatPluginRevision *revision,
    unsigned generation, uint64_t tick, WakeVoyageResolve resolve, void *resolve_context)
{
    voyage->revision = revision;
    voyage->resolve = resolve;
    voyage->resolve_context = resolve_context;
    voyage->has_reported = false;
    if (!revision)
        return;
    voyage->objective.generation = generation - 1; // Clearing advances it to GENERATION.
    if (wake_objective_clear(&voyage->objective, revision, tick))
        init_objective(voyage, revision, tick);
}

void wake_voyage_close(WakeVoyageAuthority *voyage)
{
    wake_objective_cancel(&voyage->job, voyage->worker);
}

// The voyage's events of TICK into the objective, and the update when TICK is a boundary.
static void observe(WakeVoyageAuthority *v, uint64_t tick)
{
    if (v->revision && v->objective.objective.initialized
        && wake_objective_queue(&v->objective, v->revision, tick, &v->events))
        update_objective(v, tick);
}

// L4b: the deferred migration to V->migrating ends with STATUS; for an applied one, MIGRATED
// becomes the objective. The server installs the outcome through completion.
static void settle(
    WakeVoyageAuthority *v, WakeCommitStatus status, const BoatPluginObjective *migrated)
{
    const BoatPluginRevision *next = v->migrating;
    v->migrating = NULL;
    v->commit = next->number;
    ++v->commits;
    v->commit_status = status;
    v->commit_message[0] = 0;
    v->completed = (BoatNetCommitResult) { 0 };
    v->has_completed = true;
    if (status == WAKE_COMMIT_FAILED)
        snprintf(v->commit_message, sizeof(v->commit_message), "%s",
            v->objective.objective_diagnostic.message);
    if (status != WAKE_COMMIT_APPLIED)
        return;
    wake_objective_adopt(&v->objective, next->number, migrated);
    v->voyage.route = &next->voyage.route;
    v->revision = next;
    v->completed.rescene = true;
    v->completed.scene = next->voyage.has_scene ? &next->voyage.scene : NULL;
}

// Run on the authority after each committed tick, as boat_session_step observes the voyage. The
// batch staged at the previous boundary commits first, as wake_plugin_before_tick does before
// that tick's step: nothing observes the objective between the step and this callback.
static void committed(void *context, uint64_t tick, const BoatFleet *fleet)
{
    WakeVoyageAuthority *v = context;
    if (v->job.job) {
        // The host granted a tick while a job was open (L4-1 forbids it): the batch for this tick
        // never arrived, so the objective cannot continue; a migration of the previous tick's
        // checkpoint fails.
        bool migrating = v->migrating != NULL;
        wake_objective_overrun(&v->objective, &v->job, v->worker);
        if (migrating)
            settle(v, WAKE_COMMIT_FAILED, NULL);
    }
    if (v->revision)
        wake_objective_before_tick(&v->objective);
    const Boat *boat = &fleet->state.slots[v->slot].boat;
    boat_voyage_step_events(
        &v->voyage, boat->position, boat->velocity, boat_tick_seconds(tick), &v->events);
    observe(v, tick);
}

// The objective's HUD and suspension, when they changed since the last report.
static bool state(void *context, BoatNetObjectiveState *out)
{
    WakeVoyageAuthority *v = context;
    if (!v->revision)
        return false;
    const BoatPluginObjective *o = &v->objective.objective;
    BoatNetObjectiveState next = { .suspended = o->quarantined };
    for (unsigned i = 0; i < o->hud_count && i < BOAT_NET_OBJECTIVE_ROWS; ++i) {
        snprintf(next.rows[i].id, sizeof(next.rows[i].id), "%s", o->hud[i].id);
        snprintf(next.rows[i].label, sizeof(next.rows[i].label), "%s", o->hud[i].label);
        next.rows[i].value = o->hud[i].value;
        next.row_count = (uint8_t)(i + 1);
    }
    if (v->has_reported && boat_net_objective_state_equal(&next, &v->reported))
        return false;
    v->reported = next;
    v->has_reported = true;
    *out = next;
    return true;
}

static bool weather(void *context, BoatStorm *storm)
{
    const WakeVoyageAuthority *v = context;
    if (!v->voyage.route)
        return false;
    *storm = boat_voyage_weather(&v->voyage);
    return true;
}

// boat_session_recover's voyage branch: the boat back at the latest shelter, the clock running.
static bool recover(void *context, uint64_t tick, unsigned slot, Boat *boat)
{
    WakeVoyageAuthority *v = context;
    if (slot != v->slot || !v->voyage.route)
        return false;
    boat_voyage_recover(&v->voyage, boat, boat_tick_seconds(tick));
    v->events = (BoatVoyageEvents) { .items = { { BOAT_VOYAGE_RECOVERED, v->voyage.shelter } },
        .count = 1 };
    observe(v, tick); // As the direct path's wake_plugin_after_tick after boat_session_recover.
    return true;
}

// The host's commit of a held revision, as the direct path's commit applies it to the session and
// objective (tools/wake_velocity_plugin.c). A failed migration keeps everything and reports why.
static bool commit(void *context, uint64_t tick, unsigned slot, uint32_t number, uint8_t change,
    BoatNetCommitResult *result)
{
    WakeVoyageAuthority *v = context;
    if (change == WAKE_VOYAGE_CANCEL) {
        // L4b: the host discarded the candidate whose migration is in flight.
        if (slot != v->slot || !v->migrating || v->migrating->number != number)
            return false;
        wake_objective_cancel(&v->job, v->worker);
        settle(v, WAKE_COMMIT_CANCELED, NULL);
        v->has_completed = false; // Taking this request settles the deferred one.
        return true;
    }
    const BoatPluginRevision *previous = v->revision;
    const BoatPluginRevision *next
        = v->resolve && previous ? v->resolve(v->resolve_context, number) : NULL;
    // The host holds its controls while a job is open (L1-3 and L4b), so a request then is ignored.
    if (slot != v->slot || !next || strcmp(next->manifest.kind, "voyage") || !v->voyage.route
        || change < WAKE_VOYAGE_REBIND || change > WAKE_VOYAGE_RESTART || v->job.job)
        return false;
    const BoatVoyageRoute *route = &next->voyage.route;
    // The world borrows the active revision's scene, as the direct path's bind_scene does.
    v->migrating = NULL;
    if (change == WAKE_VOYAGE_MIGRATE && v->worker) {
        // L4b: the migration runs on the isolated worker. The commit completes when its result
        // arrives (service), and the host grants nothing meanwhile (the job is pending).
        v->migrating = next;
        if (!wake_objective_begin(&v->objective, &v->job, v->worker, BOAT_PLUGIN_OBJECTIVE_MIGRATE,
                next, previous->number, v->objective.generation, tick, v->now)) {
            settle(v, WAKE_COMMIT_FAILED, NULL);
            v->has_completed = false; // Settled at once: nothing was deferred.
            return true;
        }
        result->deferred = true;
        return true;
    }
    result->rescene = true;
    result->scene = next->voyage.has_scene ? &next->voyage.scene : NULL;
    v->commit = number;
    ++v->commits;
    v->commit_status = WAKE_COMMIT_APPLIED;
    v->commit_message[0] = 0;
    switch ((WakeVoyageCommit)change) {
    case WAKE_VOYAGE_CANCEL: // Handled above.
    case WAKE_VOYAGE_REBIND:
        v->voyage.route = route;
        break;
    case WAKE_VOYAGE_LIVE: {
        const char *old = v->voyage.shelter < previous->voyage.route.shelter_count
            ? previous->voyage.shelter_ids[v->voyage.shelter]
            : NULL;
        const char *ids[BOAT_VOYAGE_SHELTERS] = { 0 };
        unsigned count = route->shelter_count < BOAT_VOYAGE_SHELTERS ? route->shelter_count
                                                                     : BOAT_VOYAGE_SHELTERS;
        for (unsigned i = 0; i < count; ++i)
            ids[i] = next->voyage.shelter_ids[i];
        boat_voyage_rebind(&v->voyage, route, old, ids, count);
        break;
    }
    case WAKE_VOYAGE_MIGRATE: {
        static BoatPluginObjective migrated;
        if (!wake_objective_migrate(&v->objective, next, previous->number, tick, &migrated)) {
            v->commit_status = WAKE_COMMIT_FAILED;
            snprintf(v->commit_message, sizeof(v->commit_message), "%s",
                v->objective.objective_diagnostic.message);
            result->rescene = false;
            return true; // Handled: the revision stays the previous one.
        }
        wake_objective_adopt(&v->objective, number, &migrated);
        v->voyage.route = route;
        break;
    }
    case WAKE_VOYAGE_RESTART:
        boat_voyage_restart(&v->voyage, route, boat_tick_seconds(tick));
        if (route->ordered) {
            // boat_session_restart_voyage's tour start.
            boat_reset(&result->boat);
            result->boat.position = route->shelters[0].position;
            result->boat.heading = route->shelters[0].heading;
            result->moved = true;
        }
        v->revision = next;
        wake_objective_cancel(&v->job, v->worker);
        if (wake_objective_clear(&v->objective, next, tick))
            init_objective(v, next, tick);
        return true;
    }
    v->revision = next;
    return true;
}

static void put32(uint8_t **p, uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        *(*p)++ = (uint8_t)(value >> shift);
}

static uint32_t get32(const uint8_t **p)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value = value << 8 | *(*p)++;
    return value;
}

static void putd(uint8_t **p, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put32(p, (uint32_t)(bits >> 32));
    put32(p, (uint32_t)bits);
}

static double getd(const uint8_t **p)
{
    uint64_t bits = (uint64_t)get32(p) << 32;
    bits |= get32(p);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

size_t wake_voyage_encode(const BoatVoyage *voyage, uint8_t *out)
{
    uint8_t *p = out;
    *p++ = (uint8_t)voyage->shelter;
    *p++ = (uint8_t)((voyage->exposed ? EXPOSED : 0) | (voyage->arrived ? ARRIVED : 0)
        | (voyage->too_fast ? TOO_FAST : 0) | (voyage->observed ? OBSERVED : 0)
        | (voyage->weather ? WEATHER : 0));
    *p++ = (uint8_t)voyage->event;
    *p++ = (uint8_t)voyage->event_shelter;
    put32(&p, voyage->recoveries);
    putd(&p, voyage->weather_started);
    putd(&p, voyage->started);
    putd(&p, voyage->arrival);
    putd(&p, voyage->event_time);
    return (size_t)(p - out);
}

static size_t private_state(void *context, uint8_t *out)
{
    const WakeVoyageAuthority *v = context;
    if (!v->voyage.route)
        return 0;
    size_t size = wake_voyage_encode(&v->voyage, out);
    if (!v->revision)
        return size;
    uint8_t *p = out + size;
    put32(&p, v->objective.generation);
    put32(&p, v->revision->number);
    put32(&p, v->commit);
    put32(&p, v->commits);
    *p++ = (uint8_t)v->commit_status;
    *p++ = (uint8_t)((v->job.job ? WAKE_VOYAGE_PENDING : 0)
        | (v->objective.objective.initialized ? WAKE_VOYAGE_INITIALIZED : 0));
    const char *message = v->objective.objective.quarantined
        ? v->objective.objective_diagnostic.message
        : v->commit_status == WAKE_COMMIT_FAILED ? v->commit_message
                                                 : "";
    const char *end = memchr(message, 0, sizeof(v->commit_message) - 1);
    size_t length = end ? (size_t)(end - message) : sizeof(v->commit_message) - 1;
    *p++ = (uint8_t)length;
    memcpy(p, message, length);
    return size + WAKE_VOYAGE_OBJECTIVE_BYTES + length;
}

// The objective's inspection lines (wake_plugin_inspect's, without the host's).
static size_t inspect(void *context, char *out, size_t capacity)
{
    const WakeVoyageAuthority *v = context;
    if (!v->revision)
        return 0;
    size_t first;
    return wake_objective_inspect(&v->objective, v->revision->number, out, capacity, &first);
}

// L4b: collect the isolated evaluation's reply, if it arrived; the server reads the HUD (and a
// migration's completion) next.
static void service(void *context, double now)
{
    WakeVoyageAuthority *v = context;
    v->now = now;
    if (!v->job.job)
        return;
    if (!v->migrating) {
        wake_objective_poll(&v->objective, &v->job, v->worker, now);
        return;
    }
    static BoatPluginObjective migrated;
    int outcome = wake_objective_poll_migration(&v->objective, &v->job, v->worker, now, &migrated);
    if (outcome)
        settle(v, outcome > 0 ? WAKE_COMMIT_APPLIED : WAKE_COMMIT_FAILED, &migrated);
}

// L4b: a settled migration's outcome for the server, once.
static bool completion(void *context, BoatNetCommitResult *result)
{
    WakeVoyageAuthority *v = context;
    if (!v->has_completed)
        return false;
    v->has_completed = false;
    if (v->completed.rescene) {
        result->rescene = true;
        result->scene = v->completed.scene;
    }
    return true;
}

BoatNetRules wake_voyage_rules(WakeVoyageAuthority *voyage)
{
    return (BoatNetRules) { .context = voyage,
        .committed = committed,
        .private_state = private_state,
        .state = state,
        .commit = commit,
        .inspect = inspect,
        .weather = weather,
        .recover = recover,
        .service = service,
        .completion = completion };
}

// The voyage's bytes alone, or followed by a well-formed objective tail.
static bool voyage_size(const uint8_t *state, size_t size)
{
    if (size == WAKE_VOYAGE_STATE_BYTES)
        return true;
    size_t tail = WAKE_VOYAGE_STATE_BYTES + WAKE_VOYAGE_OBJECTIVE_BYTES;
    return size >= tail && size == tail + state[tail - 1];
}

bool wake_voyage_objective_view(
    const uint8_t *state, size_t size, const BoatNetObjectiveState *hud, WakeObjectiveView *view)
{
    size_t tail = WAKE_VOYAGE_STATE_BYTES + WAKE_VOYAGE_OBJECTIVE_BYTES;
    if (!state || size < tail || !voyage_size(state, size)
        || state[tail - 1] >= sizeof(view->message)
        || (state[tail - 2] & ~(WAKE_VOYAGE_PENDING | WAKE_VOYAGE_INITIALIZED))
        || state[tail - 3] > WAKE_COMMIT_CANCELED)
        return false;
    const uint8_t *p = state + WAKE_VOYAGE_STATE_BYTES;
    WakeObjectiveView next = { .valid = true };
    next.generation = get32(&p);
    next.revision = get32(&p);
    next.commit = get32(&p);
    next.commits = get32(&p);
    next.commit_status = (WakeCommitStatus)*p++;
    next.pending = (*p & WAKE_VOYAGE_PENDING) != 0;
    next.initialized = (*p++ & WAKE_VOYAGE_INITIALIZED) != 0;
    size_t length = *p++;
    memcpy(next.message, p, length);
    next.message[length] = 0;
    if (hud) {
        next.suspended = hud->suspended;
        for (unsigned i = 0; i < hud->row_count && i < BOAT_PLUGIN_HUD; ++i) {
            BoatPluginHud *row = &next.hud[next.hud_count++];
            snprintf(row->id, sizeof(row->id), "%s", hud->rows[i].id);
            snprintf(row->label, sizeof(row->label), "%s", hud->rows[i].label);
            row->value = hud->rows[i].value;
        }
    }
    *view = next;
    return true;
}

bool wake_voyage_decode(const uint8_t *state, size_t size, BoatVoyage *voyage)
{
    const BoatVoyageRoute *route = voyage->route;
    if (!route || !state || !voyage_size(state, size) || state[0] >= route->shelter_count
        || (state[1] & ~(EXPOSED | ARRIVED | TOO_FAST | OBSERVED | WEATHER))
        || state[2] > BOAT_VOYAGE_WEATHER || state[3] >= route->shelter_count)
        return false;
    const uint8_t *p = state + 4;
    BoatVoyage v = { .route = route,
        .shelter = state[0],
        .exposed = (state[1] & EXPOSED) != 0,
        .arrived = (state[1] & ARRIVED) != 0,
        .too_fast = (state[1] & TOO_FAST) != 0,
        .observed = (state[1] & OBSERVED) != 0,
        .weather = (state[1] & WEATHER) != 0,
        .event = (BoatVoyageEvent)state[2],
        .event_shelter = state[3] };
    v.recoveries = get32(&p);
    v.weather_started = getd(&p);
    v.started = getd(&p);
    v.arrival = getd(&p);
    v.event_time = getd(&p);
    *voyage = v;
    return true;
}

bool wake_voyage_view(const uint8_t *state, size_t size, BoatSession *session)
{
    if (!wake_voyage_decode(state, size, &session->voyage))
        return false;
    session->world.water.storm = boat_voyage_weather(&session->voyage);
    return true;
}

bool wake_voyage_weather(void *context, const uint8_t *state, size_t size, BoatStorm *storm)
{
    BoatVoyage voyage = { .route = context };
    if (!wake_voyage_decode(state, size, &voyage))
        return false;
    *storm = boat_voyage_weather(&voyage);
    return true;
}
