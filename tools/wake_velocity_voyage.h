#ifndef WAKE_VELOCITY_VOYAGE_H
#define WAKE_VELOCITY_VOYAGE_H

// L1: voyages and tours on a private local session. WakeVoyageAuthority is the authority's rules
// adapter: it observes each committed tick read-only and steps the voyage, as boat_session_step
// does; reports the weather the world's water carries from the next tick; and places a recovery
// at a frame boundary. Its voyage state reaches the client as private state bytes
// (wake_voyage_view); the route is an immutable resource both sides borrow. No drawing, input or
// files.
#include "boat_net_server.h"
#include "boat_session.h"

#define WAKE_VOYAGE_STATE_BYTES 40

typedef struct {
    unsigned slot; // The player's fleet slot.
    BoatVoyage voyage; // Borrows the session's route.
    BoatVoyageEvents events; // The latest committed tick's or recovery's.
} WakeVoyageAuthority;

// A new generation from SESSION's voyage (its route and start state).
void wake_voyage_init(WakeVoyageAuthority *voyage, const BoatSession *session, unsigned slot);
// The rules adapter for the local authority: committed ticks, weather, recovery and private state.
BoatNetRules wake_voyage_rules(WakeVoyageAuthority *voyage);
// The voyage state codec.
size_t wake_voyage_encode(const BoatVoyage *voyage, uint8_t *out);
// Decode STATE into VOYAGE's progress, keeping its route. False (VOYAGE unchanged) for invalid
// bytes or a shelter outside the route.
bool wake_voyage_decode(const uint8_t *state, size_t size, BoatVoyage *voyage);
// The client's view: decode STATE into SESSION's voyage and the storm its water carries. False
// (SESSION unchanged) for invalid bytes.
bool wake_voyage_view(const uint8_t *state, size_t size, BoatSession *session);
// The client's weather reader for BoatLocalConfig.weather; CONTEXT is the borrowed route.
bool wake_voyage_weather(void *context, const uint8_t *state, size_t size, BoatStorm *storm);

#endif
