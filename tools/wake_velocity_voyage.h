#ifndef WAKE_VELOCITY_VOYAGE_H
#define WAKE_VELOCITY_VOYAGE_H

// L1: voyages and tours on a private local session. WakeVoyageAuthority is the authority's rules
// adapter: it observes each committed tick read-only and steps the voyage, as boat_session_step
// does; reports the weather the world's water carries from the next tick; and places a recovery
// at a frame boundary. Its voyage state reaches the client as private state bytes
// (wake_voyage_view); the route is an immutable resource both sides borrow. No drawing, input or
// files.
//
// L1b: with a plugin revision, the adapter also owns that revision's objective transaction
// (wake_velocity_objective.h): the voyage's events queue after each committed tick and every
// 12-tick boundary runs the update, as wake_plugin_after_tick does on the direct path. Its HUD
// and suspension reach the client as ObjectiveState (rules.state); its generation and suspension
// text follow the voyage's bytes in the private state. Lua state never leaves the authority.
#include "boat_net_server.h"
#include "boat_session.h"
#include "wake_velocity_objective.h"

#define WAKE_VOYAGE_STATE_BYTES 40
// The objective's tail after the voyage's bytes: generation (4), suspension text length (1) and
// that many bytes of text.
#define WAKE_VOYAGE_OBJECTIVE_BYTES 5

typedef struct {
    unsigned slot; // The player's fleet slot.
    BoatVoyage voyage; // Borrows the session's route.
    BoatVoyageEvents events; // The latest committed tick's or recovery's.
    // L1b: the borrowed active revision whose objective runs here; NULL for none.
    const BoatPluginRevision *revision;
    WakeObjective objective;
    BoatNetObjectiveState reported; // The HUD last reported through rules.state.
    bool has_reported;
} WakeVoyageAuthority;

// The client's view of the authority's objective, from ObjectiveState and private state.
typedef struct {
    bool valid; // A HUD and objective tail have arrived.
    unsigned generation;
    bool suspended;
    char message[sizeof(((BoatPluginDiagnostic *)0)->message)]; // Why it was suspended.
    BoatNetObjectiveState hud;
} WakeObjectiveView;

// A new generation from SESSION's voyage (its route and start state). The objective's trace is
// kept; zero VOYAGE to clear it.
void wake_voyage_init(WakeVoyageAuthority *voyage, const BoatSession *session, unsigned slot);
// L1b: run REVISION's objective (a voyage revision with one, borrowed) as generation GENERATION,
// initialized at TICK. A revision without an objective (or NULL) runs none.
void wake_voyage_objective(WakeVoyageAuthority *voyage, const BoatPluginRevision *revision,
    unsigned generation, uint64_t tick);
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
// L1b: the objective tail of STATE into VIEW's generation and suspension text. False (VIEW
// unchanged) when STATE carries none or is malformed.
bool wake_voyage_objective_view(const uint8_t *state, size_t size, WakeObjectiveView *view);
// The client's weather reader for BoatLocalConfig.weather; CONTEXT is the borrowed route.
bool wake_voyage_weather(void *context, const uint8_t *state, size_t size, BoatStorm *storm);

#endif
