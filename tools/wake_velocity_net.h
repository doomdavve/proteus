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

// wake_velocity --connect (multiplayer M4 in progress): the session's boat, clock and water show
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
} NetStudy;

// Connect, handshake and install the world (blocking, at most ten seconds). On failure print
// the reason and return false.
// WORLDS holds BOAT_LOCATION_COUNT owners indexed by location; the room picks one, which
// SESSION and the client borrow until net_study_close().
bool net_study_connect(NetStudy *net, const char *endpoint, const char *name, const char *token,
    const BoatWorldResources *worlds, BoatSession *session);
// One displayed frame: service the network with this frame's controls and update the view.
// False when the connection ended (the reason was printed).
bool net_study_frame(NetStudy *net, BoatSession *session, BoatInput input, float thrust_target,
    float steering_target, bool active, bool reset);
// The other boats as rival views at TIME, each with its slot's skin.
void net_study_rivals(const NetStudy *net, double time, BoatRivals *rivals);
// A slot's skin: the same on every client.
unsigned net_study_skin(unsigned slot);
// The own boat's reset, spawn or (in a deathmatch) capsize notice (NULL when there is none), and
// the opacity of the short fade from dark that hides the camera cut to a new life.
const char *net_study_notice(const NetStudy *net);
float net_study_fade(const NetStudy *net);
// One HUD line: slot, players, round trip and staleness.
const char *net_study_status(const NetStudy *net);
// The room's players, one right-aligned line each; returns the y below the last line.
int net_study_draw_roster(const NetStudy *net, int right, int top);
// A deathmatch room (docs/DEATHMATCH_PLAN.md) has sent its scoreboard.
bool net_study_deathmatch(const NetStudy *net);
// A race room (docs/RACE_PLAN.md) has sent its state.
bool net_study_race(const NetStudy *net);
// Fill VIEW's race markers: the course lines and the own next gate.
void net_study_race_view(const NetStudy *net, BoatViewState *view);
// Deathmatch HUD at window resolution: round clock and own score at the top centre, a kill feed
// below FEED_TOP at the right, and the scoreboard while SCOREBOARD is held or between rounds.
void net_study_draw_match(
    const NetStudy *net, int width, int height, int feed_top, bool scoreboard);
// Race HUD at window resolution: phase, clock, live position and next gate at the top centre,
// a large countdown, finishes and retirements below FEED_TOP at the right, and the standings
// while BOARD is held or during results.
void net_study_draw_race(const NetStudy *net, int width, int height, int feed_top, bool board);
void net_study_close(NetStudy *net);
#endif
