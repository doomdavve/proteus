#ifndef PROTEUS_BOAT_WORK_LOCAL_H
#define PROTEUS_BOAT_WORK_LOCAL_H

// L3 (docs/ARCHITECTURE_AR1.md section 3): the working harbour as a private local session. The
// authority owns a BoatWorkSession; the client owns a mirror of what it presents (boat, crate,
// cranes, clock, wake trail, job HUD and notices), filled only from the authority's messages over
// the bounded memory transport. The client sends targets, not rates: each frame's actions, helm
// targets and crane axes, which the authority applies before the frame's first tick and turns
// into per-tick rates from its own boat with the expressions the game used. There is no client
// prediction: the authority sends each committed tick's motion, and the client presents tick N.
//
// Host management (the application's local calls between frames, outside any protocol): starting
// and restarting endpoints, the grant, and persistence capture, which reads and writes the
// authority's session through boat_work_local_authority as L2's course take does. Every change
// the host makes there (a reset, a resume, a fixture script) is followed by
// boat_work_local_restart, a new generation whose checkpoint brings the client up to date.
//
// No drawing, input or files. The job runs the package's Lua on the authority, so this library
// links the work session's plugin host; proteus_boat_net never links it.
#include "net/boat_net_transport.h"
#include "session/boat_work_session.h"

#define BOAT_WORK_LOCAL_CHUNK_TICKS 12
#define BOAT_WORK_LOCAL_MAX_GRANT 96
#define BOAT_WORK_LOCAL_TEXT 128 // Notice and waiting text, including the terminator.

// Actions, applied in this order before the frame's ticks (the game's key order).
enum {
    BOAT_WORK_RETURN_CRATE = 1u << 0, // The menu's RETURN CRATE.
    BOAT_WORK_HOOK = 1u << 1, // F
    BOAT_WORK_LASH = 1u << 2, // L
    BOAT_WORK_MOOR = 1u << 3, // G
    BOAT_WORK_VIEW = 1u << 4, // TAB
    BOAT_WORK_NEXT_TARGET = 1u << 5, // J
    BOAT_WORK_BRAKE = 1u << 6, // SPACE held
    BOAT_WORK_ACTIONS = (1u << 7) - 1,
};

// One frame's commands. ACTIVE is false while a menu, help or another window has the keys: then
// only RETURN_CRATE applies and the crane rests. AIM (1..3, zero for none) is the highest aim key
// pressed. A non-finite STEERING_TARGET steers by STEERING_RATE (keyboard). DEMO rests the helm.
typedef struct {
    unsigned actions;
    unsigned aim;
    bool active, demo;
    float slew, reach, hoist;
    float steering_target, steering_rate, thrust_target;
} BoatWorkFrame;

// Called after the client applied each tick's motion, with its mirror at that tick: presentation
// steps here.
typedef struct {
    void *context;
    void (*tick)(void *context, const BoatWorkSession *view);
} BoatWorkObserver;

typedef struct {
    uint64_t frames, chunks, ticks, restarts, motions, statuses;
} BoatWorkLocalStats;

typedef struct {
    BoatNetMemory *network;
    BoatNetTransport server, client;
    bool server_connected, client_connected;
    uint32_t generation;
    uint32_t sequence[2]; // Next sequence number per direction (to the client, to the server).
    bool running;
    // The authority: its session and the frame it is applying.
    BoatWorkSession authority;
    BoatWorkFrame frame;
    float slew, reach, hoist; // The frame's crane drive, fixed when the frame is applied.
    uint8_t status[BOAT_NET_MAX_PAYLOAD]; // The last status payload sent.
    size_t status_size;
    // The client: its mirror and the text the mirror's notice and waiting point to.
    BoatWorkSession view;
    char notice[BOAT_WORK_LOCAL_TEXT], waiting[BOAT_WORK_LOCAL_TEXT];
    uint64_t view_tick; // The newest motion applied.
    bool checkpointed; // The client has the generation's checkpoint (motion, trail and status).
    char failure[128];
    BoatWorkLocalStats stats;
} BoatWorkLocal;

// Create both endpoints with GENERATION (non-zero): the authority from a copy of INITIAL (a
// configured session at a frame boundary), the client from a fresh session of the same package,
// which the authority's checkpoint then brings to INITIAL's presented state. False (with failure
// text and everything released) when startup does not complete.
bool boat_work_local_start(
    BoatWorkLocal *local, const BoatWorkSession *initial, uint32_t generation, double now);
void boat_work_local_stop(BoatWorkLocal *local);
// A new generation from the authority's current session (after host management changed it).
bool boat_work_local_restart(BoatWorkLocal *local, double now);
// One host frame: FRAME's commands, then GRANT ticks (zero while paused, at most
// BOAT_WORK_LOCAL_MAX_GRANT) in chunks of at most BOAT_WORK_LOCAL_CHUNK_TICKS ending on multiples
// of it. OBSERVER (optional) sees each applied tick. False on an endpoint failure (D10).
bool boat_work_local_frame(BoatWorkLocal *local, double now, unsigned grant,
    const BoatWorkFrame *frame, const BoatWorkObserver *observer);
// The client's mirror after the latest frame.
const BoatWorkSession *boat_work_local_view(const BoatWorkLocal *local);
// Host management only (persistence capture and fixtures between frames): the authority's
// session. Follow any change with boat_work_local_restart.
BoatWorkSession *boat_work_local_authority(BoatWorkLocal *local);

// The authority's per-frame rules, shared with the validators' direct path: apply FRAME's
// actions to SESSION and return the frame's crane drive in SLEW, REACH and HOIST; then each
// tick's helm from the session's own boat.
void boat_work_apply_frame(
    BoatWorkSession *session, const BoatWorkFrame *frame, float *slew, float *reach, float *hoist);
BoatInput boat_work_helm(const BoatWorkSession *session, const BoatWorkFrame *frame);

// The codecs, for the validators. Each encodes into OUT (BOAT_NET_MAX_PAYLOAD bytes) and returns
// the size, or decodes, returning false (destination unchanged) for malformed bytes.
size_t boat_work_encode_frame(const BoatWorkFrame *frame, uint8_t *out);
bool boat_work_decode_frame(const uint8_t *data, size_t size, BoatWorkFrame *frame);
// The motion of SESSION's latest tick, with the trail segments it appended since PREVIOUS_NEXT
// (the trail's next index before the tick). A CHECKPOINT (a start, or a frame's actions without
// ticks) presents the current tick again and carries no segments.
size_t boat_work_encode_motion(
    const BoatWorkSession *session, unsigned previous_next, bool checkpoint, uint8_t *out);
bool boat_work_decode_motion(
    const uint8_t *data, size_t size, uint64_t tick, BoatWorkSession *view);
size_t boat_work_encode_status(const BoatWorkSession *session, uint8_t *out);
bool boat_work_decode_status(const uint8_t *data, size_t size, BoatWorkSession *view,
    char notice[BOAT_WORK_LOCAL_TEXT], char waiting[BOAT_WORK_LOCAL_TEXT]);
// Trail page FIRST.. (at most BOAT_WORK_TRAIL_PAGE segments) of SESSION's trail.
#define BOAT_WORK_TRAIL_PAGE 24
size_t boat_work_encode_trail(const BoatWorkSession *session, unsigned first, uint8_t *out);
bool boat_work_decode_trail(const uint8_t *data, size_t size, BoatWorkSession *view);

#endif
