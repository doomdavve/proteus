#ifndef PROTEUS_BOAT_LOCAL_H
#define PROTEUS_BOAT_LOCAL_H

// AR6 (ledger L0): a private local session. One authority (BoatNetServer) and one predicting client
// (BoatNetClient) in this process and thread, over the bounded memory transport with no
// impairment. The host owns both endpoints and their schedule (docs/ARCHITECTURE_AR1.md
// sections 2, 3 and 5); neither endpoint reads the other's state, and a send only enqueues.
// No drawing, input, files or Lua: proteus_boat_net's boundary applies.
#include "boat_net_client.h"
#include "boat_net_server.h"

#define BOAT_LOCAL_CHUNK_TICKS 12 // Ticks per chunk at most; chunks end on multiples of 12.
#define BOAT_LOCAL_STARTUP_ROUNDS 8 // Service rounds a start may take before it fails.
// The largest grant: 0.1 s clamped frames at 8x (docs/ARCHITECTURE_AR1.md section 2).
#define BOAT_LOCAL_MAX_GRANT 96

typedef struct {
    // Location owners indexed by BoatLocationId (the client's table); RESOURCES[LOCATION] must be
    // ready. Borrowed for the session's lifetime.
    const BoatWorldResources *resources;
    unsigned location; // BoatLocationId
    const BoatSceneDefinition *scene; // Copied into the room definition; NULL keeps the defaults.
    BoatNetPrivateSession world; // Water, handling, wake pool and the spawned boat.
    // L2: the authority's rules adapter (application code, e.g. the time trial's); zero for none.
    // Its private state reaches the client after each chunk (boat_local_state).
    BoatNetRules rules;
} BoatLocalConfig;

// Called after each predicted tick of a chunk's intake: VIEW is the client's prediction at TICK
// (the boat the authority will commit) and WATER the wake pool that boat read during the tick,
// before the tick's emission, as boat_session_step's presentation reads it. Presentation steps
// here, never from the authority.
typedef struct {
    void *context;
    void (*tick)(void *context, const BoatFleet *view, unsigned slot, uint64_t tick,
        const BoatWakeObjects *water);
} BoatLocalObserver;

// A scripted command source (ledger L6): each tick's helm from PREDICTED, the boat the authority
// will step to produce TICK. Its rates are sent as they are, with no targets.
typedef struct {
    void *context;
    BoatInput (*command)(void *context, const Boat *predicted, uint64_t tick);
} BoatLocalScript;

typedef struct {
    uint64_t frames, chunks, ticks, restarts;
    unsigned max_chunks; // Most chunks in one frame.
    unsigned startup_rounds; // Service rounds the latest start took.
} BoatLocalStats;

typedef struct {
    BoatNetMemory *network;
    BoatNetServer server;
    BoatNetClient client;
    BoatRoomDefinition room;
    BoatLocalConfig config;
    uint32_t generation; // The session generation: the authority's epoch; restarts increment it.
    uint64_t tick; // Ticks granted so far: the authority's tick.
    bool running;
    BoatWakeObjects water; // The observer's pre-emission pool for the current tick.
    char failure[128]; // Why the latest start, frame or restart failed.
    BoatLocalStats stats;
} BoatLocal;

// Create both endpoints at tick zero with GENERATION (non-zero) and run the in-process handshake
// in at most BOAT_LOCAL_STARTUP_ROUNDS service rounds at NOW. False (with failure text and
// everything released) when the configuration is invalid or startup does not complete.
bool boat_local_start(
    BoatLocal *local, const BoatLocalConfig *config, uint32_t generation, double now);
// Release both endpoints and the transport. Safe on a stopped session.
void boat_local_stop(BoatLocal *local);
// A new session generation at tick zero from CONFIG: both endpoints are recreated, so nothing of
// the old generation (queued commands, acknowledgements) can reach the new one.
bool boat_local_restart(BoatLocal *local, const BoatLocalConfig *config, double now);
// The size of the chunk that starts after TICK with REMAINING granted ticks: up to the next
// multiple of BOAT_LOCAL_CHUNK_TICKS, at most REMAINING.
unsigned boat_local_chunk(uint64_t tick, unsigned remaining);
// Run one host frame's GRANT ticks (zero while paused, at most BOAT_LOCAL_MAX_GRANT) in chunks. Per
// chunk: the client commands each tick from INPUT and the frame's targets (solo's expressions) and
// OBSERVER sees each predicted tick; one batch is flushed; the authority handles its messages and
// runs the chunk; the client handles the results. False on an endpoint failure (D10), with failure
// text.
bool boat_local_frame(BoatLocal *local, double now, unsigned grant, BoatInput input,
    float thrust_target, float steering_target, const BoatLocalObserver *observer);
// boat_local_frame with each tick's command from SCRIPT instead of one frame's helm.
bool boat_local_frame_scripted(BoatLocal *local, double now, unsigned grant,
    const BoatLocalScript *script, const BoatLocalObserver *observer);
// The client's view: the predicted fleet, at the authority's tick after a frame.
const BoatFleet *boat_local_view(const BoatLocal *local);
// The player's slot in the view.
unsigned boat_local_slot(const BoatLocal *local);
// L2: the newest private state the client received (NULL before the first), with its size and
// the authority tick it describes.
const uint8_t *boat_local_state(const BoatLocal *local, size_t *size, uint64_t *tick);

#endif
