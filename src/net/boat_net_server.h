#ifndef PROTEUS_BOAT_NET_SERVER_H
#define PROTEUS_BOAT_NET_SERVER_H

#include "core/boat_marks.h"
#include "core/boat_match.h"
#include "core/boat_race.h"
#include "net/boat_net_private.h"
#include "net/boat_net_transport.h"

#define BOAT_NET_INPUT_RING 256 // Commands buffered per slot, indexed by target tick.
#define BOAT_NET_REPEAT_TICKS 6 // Repeat a missing command this long (50 ms), then zero.
#define BOAT_NET_NEUTRAL_TICKS 30 // 250 ms without a command: ease controls to neutral.
#define BOAT_NET_MATCH_EVENTS 16 // Recent deathmatch or race events kept for the caller's log.
#define BOAT_NET_CATCH_UP_TICKS 8 // Ticks per service call at most; never a larger step.
#define BOAT_NET_MISBEHAVIOUR_LIMIT 16 // Malformed or forbidden messages before disconnecting.
// A definition revision with clients connected commits this many ticks after its announcement
// (2 s), rounded up to the next snapshot tick so the commit tick is a snapshot tick.
#define BOAT_NET_REVISION_LEAD_TICKS 240

// P5b: what the authority tells a room objective. Crews are fleet slots; the adapter numbers them
// from 1. Joins and leaves arrive between ticks, at the current tick; mark entries arrive after
// the tick that made them, before that tick's after_tick.
typedef enum {
    BOAT_NET_RULE_CREW_JOINED, // A peer was accepted into SLOT.
    BOAT_NET_RULE_CREW_LEFT, // SLOT's peer left, timed out or was dropped.
    BOAT_NET_RULE_MARK_REACHED, // SLOT's boat entered MARK (boat_marks_observe).
} BoatNetRuleKind;

typedef struct {
    uint8_t kind; // BoatNetRuleKind
    uint8_t slot;
    uint8_t mark; // Index into the room definition's marks; MARK_REACHED only.
} BoatNetRuleEvent;

// An optional rules adapter, the same pattern as BoatNetTransport. The server never calls Lua;
// the adapter (boat_server's room objective) may. Callbacks must not touch the server.
typedef struct {
    void *context;
    void (*event)(void *context, uint64_t tick, const BoatNetRuleEvent *event);
    void (*after_tick)(void *context, uint64_t tick); // After every tick and its events.
    // True when the objective's HUD or suspension changed since the last call; fills OUT (the
    // revision is the server's). Called once at init and after every after_tick.
    bool (*state)(void *context, BoatNetObjectiveState *out);
    // P5f: a definition revision committed after TICK; ROOM is the new running definition. Called
    // between ticks, before any event for the new definition's marks. Optional.
    void (*definition)(void *context, uint64_t tick, const BoatRoomDefinition *room);
    // L2, private sessions only: the committed FLEET after tick TICK and its events, read-only,
    // before after_tick. Optional.
    void (*committed)(void *context, uint64_t tick, const BoatFleet *fleet);
    // L2, private sessions only: the adapter's state for its client, written into OUT (at most
    // BOAT_NET_PRIVATE_STATE_BYTES); zero for none. Called at the end of every grant; the state is
    // sent to each ready peer (BOAT_NET_PRIVATE_STATE) when it differs from the last one sent.
    size_t (*private_state)(void *context, uint8_t *out);
    // L1, private sessions only: the storm the world's water carries from the next tick, after
    // committed. False leaves the water as it is. Optional.
    bool (*weather)(void *context, BoatStorm *storm);
    // L1, private sessions only: the client asked to recover SLOT at a frame boundary after TICK.
    // BOAT holds its current boat; true places the adapter's pose there in the same life. Optional;
    // without it a request is ignored.
    bool (*recover)(void *context, uint64_t tick, unsigned slot, Boat *boat);
    // L1b, private sessions only: the client asked to install definition REVISION with CHANGE (the
    // adapter's codes) at a frame boundary after TICK. RESULT holds SLOT's boat; true installs
    // it: with RESULT->moved the boat is placed there in the same life, with RESULT->rescene the
    // world's scene becomes RESULT->scene (an immutable resource, borrowed), and the water's storm
    // is read again (weather) before the next tick. False ignores the request. Optional.
    bool (*commit)(void *context, uint64_t tick, unsigned slot, uint32_t revision, uint8_t change,
        BoatNetCommitResult *result);
    // L1b, private sessions only: the adapter's inspection text, written into OUT (at most
    // CAPACITY bytes); the client asked for it. Returns its length. Optional.
    size_t (*inspect)(void *context, char *out, size_t capacity);
    // L4b, private sessions only: called by every grant at NOW, after its messages and before its
    // ticks (also when it grants none), so the adapter can collect an asynchronous evaluation;
    // the HUD (state) is read again afterwards. Optional.
    void (*service)(void *context, double now);
    // L4b, private sessions only: the outcome of the commit the adapter deferred
    // (BoatNetCommitResult.deferred), asked after every grant's service while one is open. RESULT
    // holds the deferred commit's slot's boat; true when the commit finished: applied (RESULT as
    // commit fills it) or failed or canceled (nothing set). False while it is still running.
    // Optional; without it a deferred commit is never completed.
    bool (*completion)(void *context, BoatNetCommitResult *result);
} BoatNetRules;

// P5g: read-only authority observations, in simulation order. No files or Lua in net.
struct BoatNetServer;
typedef enum {
    BOAT_NET_RECORD_INPUT, // Immediately before fleet_step; INPUTS are the integrated controls.
    BOAT_NET_RECORD_RESULT, // Immediately after fleet_step, before native rules mutate it.
    BOAT_NET_RECORD_BEFORE_OBJECTIVE,
    BOAT_NET_RECORD_AFTER_OBJECTIVE,
    BOAT_NET_RECORD_BEFORE_DEFINITION,
    BOAT_NET_RECORD_AFTER_DEFINITION,
    BOAT_NET_RECORD_BOUNDARY, // Native rules / definition commit completed, or between ticks.
} BoatNetRecordPhase;
typedef struct {
    void *context;
    void (*observe)(void *context, const struct BoatNetServer *server, BoatNetRecordPhase phase,
        const BoatInput *inputs);
    void (*event)(void *context, uint64_t tick, const BoatNetRuleEvent *event);
} BoatNetRecord;

typedef struct {
    const char *token; // Room token every Hello must match exactly; "" for none.
    bool waves; // Room water: gentle ambient waves or calm.
    uint32_t epoch; // Non-zero session epoch.
    uint32_t first_sequence; // First outgoing sequence on every channel (wrap tests).
    unsigned max_players; // 1..BOAT_FLEET_SLOTS.
    // The room's package definition, borrowed for the server's lifetime. Its mode, rules, course
    // and scene replace the compiled defaults; its location must match RESOURCES.
    const BoatRoomDefinition *room;
    // The room's location, borrowed for the server's lifetime; must be ready.
    const BoatWorldResources *resources;
    BoatNetRules rules; // Zero for none.
    BoatNetRecord record; // Optional authority recorder; hosted rooms only.
    // AR6: a private session's world and spawn; its host advances it with boat_net_server_grant()
    // only. Disabled (zero) for every hosted room.
    BoatNetPrivateSession local;
} BoatNetServerConfig;

typedef enum {
    BOAT_NET_PEER_FREE,
    BOAT_NET_PEER_HANDSHAKE, // Connected; waiting for a Hello.
    BOAT_NET_PEER_BOOTSTRAP, // Accepted; the checkpoint is on its way, no boat yet.
    BOAT_NET_PEER_READY, // Installed the checkpoint; owns a boat that can spawn.
} BoatNetPeerPhase;

typedef struct {
    uint64_t tick;
    uint32_t life;
    BoatNetCommand command;
} BoatNetQueuedCommand;

typedef struct {
    BoatNetPeerPhase phase;
    unsigned slot;
    char name[BOAT_NET_NAME_BYTES + 1];
    double connected_at, heard_at, last_resync;
    double input_tokens, spawn_tokens, refill_at; // Token buckets.
    uint32_t sequence[BOAT_NET_CHANNELS]; // Next outgoing sequence per channel.
    uint32_t input_sequence; // Newest command packet sequence received.
    bool input_seen;
    unsigned misbehaviour;
    uint32_t revision_ready; // The newest revision this peer answered REVISION_READY for.
    bool hello_held; // A Hello waits for the announced revision to commit (see HELLO below).
    BoatNetHello held;
} BoatNetServerPeer;

// Per-slot control record: the authority's only use of client messages.
typedef struct {
    BoatNetQueuedCommand ring[BOAT_NET_INPUT_RING];
    uint32_t life; // The life these records belong to.
    uint64_t fresh; // Tick of the last received command used, or of the life's start.
    BoatNetCommand last; // Last received command used.
} BoatNetSlotControl;

typedef struct {
    uint64_t ticks, snapshots, birth_batches, births_sent, bootstraps, bootstrap_bytes;
    uint64_t definition_bytes; // Encoded room definition bytes sent with each accepted peer.
    uint64_t accepted, rejected[BOAT_NET_REJECT_LIMIT], left, timeouts;
    // Commands stored; repeats of stored ones; packets whose newest command came too late.
    uint64_t commands, duplicate_commands, late_commands, future_commands, stale_life_commands;
    uint64_t stale_packets, rate_limited, connect_limited, malformed, forbidden, misbehaving;
    uint64_t input_states[BOAT_NET_INPUT_STATES]; // Ticks integrated with each input source.
    uint64_t objective_states; // ObjectiveState messages queued, broadcasts and joins alike.
    // Definition revisions (v14): candidates staged, announced, refused at staging, committed;
    // peers dropped for not being ready; Hellos held while an announcement was out.
    uint64_t revisions_staged, revisions_announced, revisions_refused, revisions_committed;
    uint64_t revision_leaves, revision_held_hellos, revision_bytes;
    uint32_t revision; // The running definition's revision (1 at start).
    uint64_t spawn_requests, resyncs, output_faults, emission_failures, births_truncated;
    uint64_t private_states; // Private state messages sent (L2).
    uint64_t recoveries, recoveries_ignored; // Private recover requests (L1).
    uint64_t commits, commits_ignored; // Private commit requests (L1b).
    uint64_t commits_deferred, commits_completed; // Deferred commits begun and finished (L4b).
    uint64_t inspections; // Private inspection texts sent (L1b).
    unsigned max_backlog, max_messages_per_service;
} BoatNetServerStats;

typedef struct BoatNetServer {
    BoatNetTransport transport;
    BoatNetServerConfig config;
    char token[BOAT_NET_TOKEN_BYTES + 1];
    uint64_t content_digest;
    const BoatRoomDefinition *room; // CONFIG's borrowed definition.
    bool deathmatch, race_room; // Derived from ROOM's mode.
    uint8_t definition[BOAT_ROOM_BYTES_MAX]; // The encoded room definition.
    uint32_t definition_bytes;
    // P5f: the room a committed revision installed. CONFIG.room and ROOM point here from the
    // first commit on, so the fleet's scene pointer stays valid; before it they are borrowed.
    BoatRoomDefinition revised;
    // A candidate waiting for a boundary, and the announced one that commits at a fixed tick.
    struct {
        bool present;
        uint32_t revision, bytes;
        uint64_t commit_tick;
        uint8_t data[BOAT_ROOM_BYTES_MAX];
        BoatRoomDefinition room;
    } waiting, announced;
    BoatFleet fleet;
    BoatNetSlotInput inputs[BOAT_FLEET_SLOTS];
    BoatNetSlotControl control[BOAT_FLEET_SLOTS];
    int slot_peer[BOAT_FLEET_SLOTS]; // Peer owning each slot, or -1.
    BoatNetServerPeer peers[BOAT_NET_MAX_PEERS];
    bool started, overloaded;
    double origin, backlog_since;
    uint64_t origin_tick;
    uint64_t tick_limit; // Never tick past this (0: no limit), even when catching up.
    double connect_tokens, connect_refill_at; // Connection-rate bucket (see BOAT_NET_CONNECT_*).
    bool connect_started;
    double peer_timeout; // Seconds of silence before a peer times out (0: BOAT_NET_TIMEOUT).
    uint32_t next_checkpoint;
    BoatMatch match; // Deathmatch state; untouched in free play.
    BoatNetMatchState match_sent; // The scoreboard clients last received.
    BoatMatchEvent events[BOAT_NET_MATCH_EVENTS]; // Recent match events, a ring.
    uint64_t event_count; // Match events ever recorded; the newest is (count - 1) % ring size.
    BoatRace race; // Race state; untouched outside race rooms.
    BoatNetRaceState race_sent; // The race state clients last received.
    BoatRaceEvent race_events[BOAT_NET_MATCH_EVENTS]; // Recent race events, a ring.
    uint64_t race_event_count;
    BoatMarkTracker marks; // Which mark each slot is inside; used when ROOM has marks.
    BoatNetObjectiveState objective; // The HUD clients last received (revision 0: none yet).
    bool has_objective;
    int deferred_slot; // The slot of the commit the adapter deferred (L4b), or -1.
    uint8_t private_sent[BOAT_NET_PRIVATE_STATE_BYTES]; // The private state last sent (L2).
    size_t private_sent_size;
    BoatNetServerStats stats;
} BoatNetServer;

// A room at tick zero in CONFIG's location. False for an invalid configuration.
bool boat_net_server_init(
    BoatNetServer *server, BoatNetTransport transport, const BoatNetServerConfig *config);
// Handle waiting messages, then run the ticks due at monotonic time NOW (seconds; the first
// call starts the clock): at most BOAT_NET_CATCH_UP_TICKS, sending snapshots and births on
// their cadence, stopping at tick_limit when set. False once the room is overloaded (500 ms
// behind for a second).
bool boat_net_server_service(BoatNetServer *server, double now);
// AR6: a private session's service. Handle waiting messages (at most
// BOAT_NET_MESSAGES_PER_SERVICE, private input batches included), then run exactly TICKS ticks
// (the host's grant; zero while paused), refilling the input and spawn rate limits per tick, then
// send the rules adapter's private state when it changed. Before the ticks it services the rules
// adapter and collects a deferred commit's outcome (L4b). No
// peer timeout, catch-up or overload: NOW only stamps operational deadlines (resync rate limit).
// False for a hosted room.
bool boat_net_server_grant(BoatNetServer *server, double now, unsigned ticks);
// P5f: stage CANDIDATE (encoded bytes) as the room's next definition. It is validated as the
// room's own definition is (decode, structure, scene) and must keep the running mode and
// location; false with DIAGNOSTIC leaves the running revision and any waiting candidate alone. A
// valid candidate replaces the waiting one. It commits only at a boundary: race RESULTS or
// WAITING, deathmatch INTERMISSION or WARMUP, a free room with no player. With players connected
// it is announced to every one of them and commits BOAT_NET_REVISION_LEAD_TICKS later; a player
// not ready by then is dropped with BOAT_NET_LEAVE_REVISION. An empty free room commits at once.
// Revision numbers are assigned when a candidate is announced or committed.
bool boat_net_server_stage_revision(
    BoatNetServer *server, const uint8_t *bytes, size_t size, BoatRoomDiagnostic *diagnostic);
// True while a staged candidate has not committed (waiting for a boundary or announced).
bool boat_net_server_revision_pending(const BoatNetServer *server);
// Monotonic time of the next tick, for the caller's wait.
double boat_net_server_next_tick(const BoatNetServer *server);
unsigned boat_net_server_players(const BoatNetServer *server);
// Tell every peer the room is closing and disconnect it.
void boat_net_server_shutdown(BoatNetServer *server);

#endif
