#include "wake_velocity_voyage.h"

#include <stdio.h>
#include <string.h>

enum { EXPOSED = 1, ARRIVED = 2, TOO_FAST = 4, OBSERVED = 8, WEATHER = 16 };

void wake_voyage_init(WakeVoyageAuthority *voyage, const BoatSession *session, unsigned slot)
{
    // The objective is large; clear it in place rather than through a temporary. Its trace is
    // inspection history and spans generations, as the direct path's does.
    static WakePluginTrace trace[WAKE_PLUGIN_TRACE];
    unsigned trace_count = voyage->objective.trace_count;
    memcpy(trace, voyage->objective.trace, sizeof(trace));
    memset(voyage, 0, sizeof(*voyage));
    memcpy(voyage->objective.trace, trace, sizeof(trace));
    voyage->objective.trace_count = trace_count;
    voyage->slot = slot;
    voyage->voyage = session->voyage;
}

void wake_voyage_objective(WakeVoyageAuthority *voyage, const BoatPluginRevision *revision,
    unsigned generation, uint64_t tick)
{
    voyage->revision = revision && revision->voyage.manifest.objective ? revision : NULL;
    voyage->has_reported = false;
    if (!voyage->revision)
        return;
    voyage->objective.generation = generation - 1; // Clearing advances it to GENERATION.
    if (wake_objective_clear(&voyage->objective, voyage->revision, tick))
        wake_objective_init(&voyage->objective, voyage->revision, tick);
}

// The voyage's events of TICK into the objective, and the update when TICK is a boundary.
static void observe(WakeVoyageAuthority *v, uint64_t tick)
{
    if (v->revision && v->objective.objective.initialized
        && wake_objective_queue(&v->objective, v->revision, tick, &v->events))
        wake_objective_update(&v->objective, v->revision, tick);
}

// Run on the authority after each committed tick, as boat_session_step observes the voyage. The
// batch staged at the previous boundary commits first, as wake_plugin_before_tick does before
// that tick's step: nothing observes the objective between the step and this callback.
static void committed(void *context, uint64_t tick, const BoatFleet *fleet)
{
    WakeVoyageAuthority *v = context;
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
    const char *message
        = v->objective.objective.quarantined ? v->objective.objective_diagnostic.message : "";
    size_t length = strnlen(message, sizeof(v->objective.objective_diagnostic.message) - 1);
    *p++ = (uint8_t)length;
    memcpy(p, message, length);
    return size + WAKE_VOYAGE_OBJECTIVE_BYTES + length;
}

BoatNetRules wake_voyage_rules(WakeVoyageAuthority *voyage)
{
    return (BoatNetRules) { .context = voyage,
        .committed = committed,
        .private_state = private_state,
        .state = state,
        .weather = weather,
        .recover = recover };
}

// The voyage's bytes alone, or followed by a well-formed objective tail.
static bool voyage_size(const uint8_t *state, size_t size)
{
    if (size == WAKE_VOYAGE_STATE_BYTES)
        return true;
    size_t tail = WAKE_VOYAGE_STATE_BYTES + WAKE_VOYAGE_OBJECTIVE_BYTES;
    return size >= tail && size == tail + state[tail - 1];
}

bool wake_voyage_objective_view(const uint8_t *state, size_t size, WakeObjectiveView *view)
{
    size_t tail = WAKE_VOYAGE_STATE_BYTES + WAKE_VOYAGE_OBJECTIVE_BYTES;
    if (!state || size < tail || !voyage_size(state, size)
        || state[tail - 1] >= sizeof(view->message))
        return false;
    const uint8_t *p = state + WAKE_VOYAGE_STATE_BYTES;
    view->generation = get32(&p);
    size_t length = *p++;
    memcpy(view->message, p, length);
    view->message[length] = 0;
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
