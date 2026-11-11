#include "wake_velocity_voyage.h"

#include <string.h>

enum { EXPOSED = 1, ARRIVED = 2, TOO_FAST = 4, OBSERVED = 8, WEATHER = 16 };

void wake_voyage_init(WakeVoyageAuthority *voyage, const BoatSession *session, unsigned slot)
{
    *voyage = (WakeVoyageAuthority) { .slot = slot, .voyage = session->voyage };
}

// Run on the authority after each committed tick, as boat_session_step observes the voyage.
static void committed(void *context, uint64_t tick, const BoatFleet *fleet)
{
    WakeVoyageAuthority *v = context;
    const Boat *boat = &fleet->state.slots[v->slot].boat;
    boat_voyage_step_events(
        &v->voyage, boat->position, boat->velocity, boat_tick_seconds(tick), &v->events);
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
    return v->voyage.route ? wake_voyage_encode(&v->voyage, out) : 0;
}

BoatNetRules wake_voyage_rules(WakeVoyageAuthority *voyage)
{
    return (BoatNetRules) { .context = voyage,
        .committed = committed,
        .private_state = private_state,
        .weather = weather,
        .recover = recover };
}

bool wake_voyage_decode(const uint8_t *state, size_t size, BoatVoyage *voyage)
{
    const BoatVoyageRoute *route = voyage->route;
    if (!route || !state || size != WAKE_VOYAGE_STATE_BYTES || state[0] >= route->shelter_count
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
