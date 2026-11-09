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

typedef struct {
    // Location owners indexed by BoatLocationId (the client's table); RESOURCES[LOCATION] must be
    // ready. Borrowed for the session's lifetime.
    const BoatWorldResources *resources;
    unsigned location; // BoatLocationId
    const BoatSceneDefinition *scene; // Copied into the room definition; NULL keeps the defaults.
    BoatNetPrivateSession world; // Water, handling, wake pool and the spawned boat.
} BoatLocalConfig;

// Called after each predicted tick of a chunk's intake: VIEW is the client's prediction at TICK
// (the boat the authority will commit). Presentation steps here, never the authority.
typedef struct {
    void *context;
    void (*tick)(void *context, const BoatFleet *view, unsigned slot, uint64_t tick);
} BoatLocalObserver;

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
// Run one host frame's GRANT ticks (zero while paused) in chunks. Per chunk: the client commands
// each tick from INPUT and the frame's targets (solo's expressions) and OBSERVER sees each
// predicted tick; one batch is flushed; the authority handles its messages and runs the chunk;
// the client handles the results. False on an endpoint failure (D10), with failure text.
bool boat_local_frame(BoatLocal *local, double now, unsigned grant, BoatInput input,
    float thrust_target, float steering_target, const BoatLocalObserver *observer);
// The client's view: the predicted fleet, at the authority's tick after a frame.
const BoatFleet *boat_local_view(const BoatLocal *local);
// The player's slot in the view.
unsigned boat_local_slot(const BoatLocal *local);

#endif
