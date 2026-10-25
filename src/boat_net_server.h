#ifndef PROTEUS_BOAT_NET_SERVER_H
#define PROTEUS_BOAT_NET_SERVER_H

#include "boat_match.h"
#include "boat_net_transport.h"
#include "boat_race.h"

#define BOAT_NET_INPUT_RING 256 // Commands buffered per slot, indexed by target tick.
#define BOAT_NET_REPEAT_TICKS 6 // Repeat a missing command this long (50 ms), then zero.
#define BOAT_NET_NEUTRAL_TICKS 30 // 250 ms without a command: ease controls to neutral.
#define BOAT_NET_MATCH_EVENTS 16 // Recent deathmatch or race events kept for the caller's log.
#define BOAT_NET_CATCH_UP_TICKS 8 // Ticks per service call at most; never a larger step.
#define BOAT_NET_MISBEHAVIOUR_LIMIT 16 // Malformed or forbidden messages before disconnecting.

typedef struct {
    const char *token; // Room token every Hello must match exactly; "" for none.
    bool waves; // Room water: gentle ambient waves or calm.
    uint32_t epoch; // Non-zero session epoch.
    uint32_t first_sequence; // First outgoing sequence on every channel (wrap tests).
    unsigned max_players; // 1..BOAT_FLEET_SLOTS.
    bool deathmatch; // Capsize deathmatch rounds (docs/DEATHMATCH_PLAN.md) instead of free play.
    BoatMatchRules match; // Deathmatch rules; ignored in free play.
    // River sprint rounds (docs/RACE_PLAN.md) instead of free play; needs the river location.
    bool race;
    BoatRaceRules race_rules; // Ignored outside race rooms.
    // The room's location, borrowed for the server's lifetime; must be ready.
    const BoatWorldResources *resources;
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
    uint64_t accepted, rejected[BOAT_NET_REJECT_LIMIT], left, timeouts;
    // Commands stored; repeats of stored ones; packets whose newest command came too late.
    uint64_t commands, duplicate_commands, late_commands, future_commands, stale_life_commands;
    uint64_t stale_packets, rate_limited, malformed, forbidden, misbehaving;
    uint64_t input_states[BOAT_NET_INPUT_STATES]; // Ticks integrated with each input source.
    uint64_t spawn_requests, resyncs, output_faults, emission_failures, births_truncated;
    unsigned max_backlog, max_messages_per_service;
} BoatNetServerStats;

typedef struct {
    BoatNetTransport transport;
    BoatNetServerConfig config;
    char token[BOAT_NET_TOKEN_BYTES + 1];
    uint64_t content_digest;
    BoatFleet fleet;
    BoatNetSlotInput inputs[BOAT_FLEET_SLOTS];
    BoatNetSlotControl control[BOAT_FLEET_SLOTS];
    int slot_peer[BOAT_FLEET_SLOTS]; // Peer owning each slot, or -1.
    BoatNetServerPeer peers[BOAT_NET_MAX_PEERS];
    bool started, overloaded;
    double origin, backlog_since;
    uint64_t origin_tick;
    uint64_t tick_limit; // Never tick past this (0: no limit), even when catching up.
    uint32_t next_checkpoint;
    BoatMatch match; // Deathmatch state; untouched in free play.
    BoatNetMatchState match_sent; // The scoreboard clients last received.
    BoatMatchEvent events[BOAT_NET_MATCH_EVENTS]; // Recent match events, a ring.
    uint64_t event_count; // Match events ever recorded; the newest is (count - 1) % ring size.
    BoatRace race; // Race state; untouched outside race rooms.
    BoatNetRaceState race_sent; // The race state clients last received.
    BoatRaceEvent race_events[BOAT_NET_MATCH_EVENTS]; // Recent race events, a ring.
    uint64_t race_event_count;
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
// Monotonic time of the next tick, for the caller's wait.
double boat_net_server_next_tick(const BoatNetServer *server);
unsigned boat_net_server_players(const BoatNetServer *server);
// Tell every peer the room is closing and disconnect it.
void boat_net_server_shutdown(BoatNetServer *server);

#endif
