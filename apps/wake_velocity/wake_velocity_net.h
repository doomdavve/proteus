#ifndef WAKE_VELOCITY_NET_H
#define WAKE_VELOCITY_NET_H
#include "boat_net_client.h"
#if defined(__EMSCRIPTEN__)
#include "boat_net_web.h"
#else
#include "boat_net_enet.h"
#endif
#include "boat_session.h"
#include "boat_view.h"

// The room client of wake_velocity --connect and --host: a BoatNetClient of a hosted authority.
// Private sessions use the same client library through wake_velocity_local.c, which presents only
// the own boat from prediction; a room also presents remote boats and the room's HUDs, so the two
// glues stay separate by design (architecture plan L5-1). The session's boat, clock and water show
// the client's predicted fleet, so the own boat answers the keys at once. Corrections move the
// physical state immediately; the drawn poses blend small ones away over about 120 ms. Every
// other boat is drawn from authoritative snapshots about 100 ms behind the estimated server
// time and, close to the own boat, blended toward its predicted collision proxy. Each slot
// keeps its own presentation (spray, cloth) and its skin: slots 0-3 are Sweden, Finland,
// Denmark and Norway on every client.
typedef struct {
#if !defined(__EMSCRIPTEN__)
    BoatNetEnet *enet;
#endif
    bool transport_open;
    bool weapons_blocked;
    uint16_t grenade_request;
    uint8_t torpedo_request;
    BoatNetClient client;
    BoatPresentation presentations[BOAT_FLEET_SLOTS]; // Other slots; the own one is the session's.
    Boat shown[BOAT_FLEET_SLOTS]; // Drawn poses of the other boats.
    bool own_visible, visible[BOAT_FLEET_SLOTS];
    uint32_t own_life, lives[BOAT_FLEET_SLOTS];
    double respawned_at; // Clock time of the own boat's latest new life.
    uint32_t capsized_life; // Own life last seen capsized, and the mirror tick it was first seen.
    uint64_t capsized_tick;
    BoatPredictionError offset[BOAT_FLEET_SLOTS]; // Visual correction still being blended.
    uint32_t correction; // Prediction correction serial already folded into the offsets.
    uint64_t presented_tick; // Last tick the presentations were stepped for.
    BoatRemoteProximity proximity[BOAT_FLEET_SLOTS];
    uint64_t samples[BOAT_REMOTE_HELD + 1], near_frames, frames; // Other-boat diagnostics.
    char failure[200]; // Why connecting failed or the connection ended (also printed).
} WakeRoom;

// Connect, handshake and install the world (blocking, at most ten seconds). On failure print
// the reason and return false.
// WORLDS holds BOAT_LOCATION_COUNT owners indexed by location; the room picks one, which
// SESSION and the client borrow until wake_room_close().
bool wake_room_connect(WakeRoom *net, const char *endpoint, const char *name, const char *token,
    const BoatWorldResources *worlds, BoatSession *session);
// Finish joining once the window, renderer and audio are ready. A windowed client holds its
// READY until here so the authority does not queue reliable spawn traffic during startup.
bool wake_room_ready(WakeRoom *net);
// One displayed frame: service the network with this frame's controls and update the view.
// False when the connection ended (the reason was printed).
bool wake_room_frame(WakeRoom *net, BoatSession *session, BoatInput input, float thrust_target,
    float steering_target, bool active, bool reset);
// The other boats as rival views at TIME, each with its slot's skin.
void wake_room_rivals(const WakeRoom *net, double time, BoatRivals *rivals);
// A slot's skin: the same on every client.
unsigned wake_room_skin(unsigned slot);
// The own boat's reset, spawn or (in a deathmatch) capsize notice (NULL when there is none), and
// the opacity of the short fade from dark that hides the camera cut to a new life.
const char *wake_room_notice(const WakeRoom *net);
float wake_room_fade(const WakeRoom *net);
// One HUD line: slot, players, round trip and staleness.
const char *wake_room_status(const WakeRoom *net);
// The room's players, one right-aligned line each; returns the y below the last line.
int wake_room_draw_roster(const WakeRoom *net, int right, int top);
// A deathmatch room (docs/DEATHMATCH_PLAN.md) has sent its scoreboard.
bool wake_room_deathmatch(const WakeRoom *net);
// A race room (docs/RACE_PLAN.md) has sent its state.
bool wake_room_race(const WakeRoom *net);
// Fill VIEW's race markers: the course lines and the own next gate.
void wake_room_race_view(const WakeRoom *net, BoatViewState *view);
// Point VIEW at the room's objective marks (none when the room has none).
void wake_room_marks_view(const WakeRoom *net, BoatViewState *view);
// P5d: the room objective's rows as "LABEL value" lines in a panel whose right edge is RIGHT and
// whose top is TOP, at window resolution. Draws nothing without an ObjectiveState; a suspended
// objective shows OBJECTIVE SUSPENDED. Returns the y below the panel (TOP when nothing is drawn).
int wake_room_draw_objective(const WakeRoom *net, int right, int top);
void wake_room_combat_hud(const WakeRoom *net, int width, int height, const BoatViewState *view,
    const BoatPresentation *presentation, const BoatWorld *world, BoatCamera camera);
// Deathmatch HUD at window resolution: round clock and own score at HEADER_TOP, a kill feed
// below FEED_TOP at the right, and the scoreboard while SCOREBOARD is held or between rounds.
void wake_room_draw_match(
    const WakeRoom *net, int width, int height, int header_top, int feed_top, bool scoreboard);
// Race HUD at window resolution: phase, clock, live position and next gate at the top centre,
// a large countdown, finishes and retirements below FEED_TOP at the right, and the standings
// while BOARD is held or during results.
void wake_room_draw_race(const WakeRoom *net, int width, int height, int feed_top, bool board);
void wake_room_close(WakeRoom *net);
#endif
