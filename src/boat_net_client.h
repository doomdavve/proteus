#ifndef PROTEUS_BOAT_NET_CLIENT_H
#define PROTEUS_BOAT_NET_CLIENT_H

#include "boat_net_transport.h"
#include "boat_prediction.h"
#include "boat_remote_view.h"

#define BOAT_NET_PENDING_BIRTHS 64 // Births held until their tick's snapshot or the baseline.
#define BOAT_NET_STALE 0.25 // Seconds without a snapshot before the view is marked stale.
#define BOAT_NET_RESYNC_AFTER 1.0 // Seconds of wake digest mismatch before a resync request.
#define BOAT_NET_HOLD_INCOMPLETE 0.1 // Seconds a prediction correction waits for missing births.
#define BOAT_NET_MATCH_FEED 6 // Recent deathmatch or race events kept for the feed.
#define BOAT_NET_EARLY_BOOTSTRAP (BOAT_NET_MAX_CHECKPOINT / BOAT_NET_PAGE_BYTES + 2)

typedef struct {
    const char *name, *token, *build;
    uint32_t first_sequence; // First outgoing sequence on every channel (wrap tests).
    bool predict; // Run a speculative fleet ahead of the mirror with the own commands.
    // BOAT_LOCATION_COUNT owners indexed by location ID, borrowed for the client's lifetime.
    // The room picks the location; an unready entry fails its bootstrap.
    const BoatWorldResources *resources;
} BoatNetClientConfig;

typedef enum {
    BOAT_NET_CLIENT_CONNECTING,
    BOAT_NET_CLIENT_HANDSHAKE, // Hello sent.
    BOAT_NET_CLIENT_BOOTSTRAP, // Accepted; assembling the world checkpoint.
    BOAT_NET_CLIENT_PLAYING, // Mirror installed; snapshots and births apply.
    BOAT_NET_CLIENT_REJECTED,
    BOAT_NET_CLIENT_CLOSED,
} BoatNetClientPhase;

typedef enum {
    BOAT_NET_RESET_NONE,
    BOAT_NET_RESET_SENT, // Requested; the authority has not yet queued it.
    BOAT_NET_RESET_WAITING, // Queued, waiting for a clear authored spawn.
} BoatNetResetState;

typedef struct {
    uint64_t snapshots, stale_snapshots, births_applied, births_duplicate, births_held;
    uint64_t births_dropped, bootstraps, bootstrap_failures, resyncs, decode_errors;
    uint64_t early_bootstrap; // Bootstrap messages held until Accept on the control channel.
    uint64_t stale_packets, input_packets, commands, skipped_commands, pings, pongs;
    uint64_t consistent, inconsistent; // Snapshots whose wake digest matched on arrival.
    uint64_t repaired; // Mismatches later cleared by births.
    double max_mismatch; // Longest repaired mismatch, seconds.
    uint64_t resets; // Own resets completed (a new life after a request).
    uint64_t
        resets_dropped; // Requests the authority never queued (rate limit), given up after 1 s.
    double max_reset; // Longest time from a reset request to its new life, seconds.
} BoatNetClientStats;

// A deathmatch event as received, with the roster names of that moment (leavers keep theirs).
typedef struct {
    BoatNetMatchEvent event;
    uint64_t tick;
    double at; // Local receipt time.
    char victim[BOAT_NET_NAME_BYTES + 1], attacker[BOAT_NET_NAME_BYTES + 1];
} BoatNetFeedEntry;

// A race event as received, with the slot's roster name of that moment.
typedef struct {
    BoatNetRaceEvent event;
    uint64_t tick;
    double at; // Local receipt time.
    char name[BOAT_NET_NAME_BYTES + 1];
} BoatNetRaceFeedEntry;

// Eight in-flight snapshot sets cover bounded page reordering. A completed correction
// supersedes older incomplete sets; a partial newer set cannot starve an older complete one.
#define BOAT_NET_COMBAT_SETS 8
typedef struct {
    uint64_t tick;
    BoatNetSnapshot snapshot;
    bool snapshot_received;
    unsigned pages;
    uint8_t bytes[BOAT_COMBAT_BYTES];
} BoatNetCombatSet;

// The mirror fleet holds the newest authoritative snapshot, rebuilt from a complete checkpoint
// plus births. With prediction, every snapshot or applied birth batch corrects the speculative
// fleet, which each command advances to its target tick; the caller draws that instead.
typedef struct {
    BoatNetTransport transport;
    char name[BOAT_NET_NAME_BYTES + 1], token[BOAT_NET_TOKEN_BYTES + 1];
    char build[BOAT_NET_BUILD_BYTES + 1];
    uint32_t first_sequence;
    const BoatWorldResources *resources; // Borrowed per-location owners (see the config).
    bool predict, predict_dirty;
    BoatNetClientPhase phase;
    double started_at;
    double serviced_at, pong_after; // Exclude RTT samples spanning a local loading stall.
    BoatNetReject reject; // Valid when REJECTED.
    uint8_t leave; // Server's leave reason when CLOSED, zero for a transport close.
    uint8_t timed_out; // 1: no handshake/world within 10 s, 2: no snapshot for 5 s.
    BoatNetAccept accept;
    uint32_t epoch;
    unsigned slot;
    BoatNetRoster roster;
    // Deathmatch rooms: the authority's scoreboard and events, never predicted.
    bool match_valid;
    BoatNetMatchState match;
    BoatNetMatchState result; // The latest finished round's scoreboard, as first announced.
    BoatNetFeedEntry feed[BOAT_NET_MATCH_FEED];
    uint64_t feed_count; // Events ever received; the newest is (count - 1) % ring size.
    // Race rooms: the authority's phase, clock and rows, and its events, never predicted.
    bool race_valid;
    BoatNetRaceState race;
    BoatNetRaceState race_result; // The latest round's rows as its results phase began.
    BoatNetRaceFeedEntry race_feed[BOAT_NET_MATCH_FEED];
    uint64_t race_feed_count;
    uint64_t race_go; // Of the newest snapshot: binds the course source and the hold release.
    // Authoritative mirror: valid once PLAYING.
    BoatFleet fleet;
    BoatNetSlotInput inputs[BOAT_FLEET_SLOTS];
    BoatFleetSlot previous[BOAT_FLEET_SLOTS]; // Slots of the snapshot before the newest.
    uint64_t previous_tick;
    uint64_t wake_digest; // Of the newest snapshot.
    bool wake_consistent;
    double snapshot_at, mismatch_since, resync_at;
    BoatNetCombatSet combat_sets[BOAT_NET_COMBAT_SETS];
    // Bootstrap assembly.
    BoatNetWorldBegin begin;
    bool assembling;
    unsigned pages_received;
    bool page_received[BOAT_NET_MAX_CHECKPOINT / BOAT_NET_PAGE_BYTES];
    uint8_t assembly[BOAT_NET_MAX_CHECKPOINT];
    // ENet orders each reliable channel independently. One complete checkpoint can arrive
    // before Accept; keep its wire messages bounded until the session epoch is known.
    BoatNetEvent early_bootstrap[BOAT_NET_EARLY_BOOTSTRAP];
    unsigned early_bootstrap_count;
    BoatNetBirth pending[BOAT_NET_PENDING_BIRTHS];
    unsigned pending_count;
    // Sequencing.
    uint32_t sequence[BOAT_NET_CHANNELS];
    uint32_t received[BOAT_NET_CHANNELS];
    bool received_any[BOAT_NET_CHANNELS];
    // Clock estimate and commands.
    double offset; // Estimated server tick minus local seconds times the tick rate.
    bool offset_valid;
    double rtt, ping_at, input_clock;
    unsigned lead; // Ticks ahead of the estimated server tick.
    uint64_t command_tick; // Target tick of the newest command.
    uint32_t command_life;
    unsigned command_count, commands_since_send;
    BoatNetCommand history[BOAT_NET_MAX_COMMANDS]; // Newest last.
    uint32_t reset_request, requests; // Pending reset request ID, and IDs issued.
    bool resetting; // A sent reset request awaits its new life.
    uint32_t reset_life; // The life that request replaces.
    double reset_at; // When it was sent.
    BoatNetClientStats stats;
    BoatPrediction prediction; // Valid once PLAYING when predicting.
    BoatRemoteHistory remotes; // Recent authoritative boat states, for drawing other boats.
} BoatNetClient;

void boat_net_client_init(BoatNetClient *client, BoatNetTransport transport,
    const BoatNetClientConfig *config, double now);
// Handle waiting messages and, while playing, generate one command per local 120 Hz tick from
// INPUT (INACTIVE asks the authority to neutralize the controls). False once rejected or closed.
bool boat_net_client_service(BoatNetClient *client, double now, BoatInput input, bool inactive);
// Optional local target controller: converts thrust to ordinary wire rates per predicted tick.
// Requires prediction; INPUT steering and INACTIVE keep their ordinary meanings.
bool boat_net_client_service_thrust(
    BoatNetClient *client, double now, BoatInput input, bool inactive, float thrust_target);
// As above, with optional absolute wheel target; NAN keeps INPUT's steering rate.
bool boat_net_client_service_targets(BoatNetClient *client, double now, BoatInput input,
    bool inactive, float thrust_target, float steering_target);
// Ask the authority for a new life at a safe spawn (menu action or held Backspace).
void boat_net_client_request_reset(BoatNetClient *client);
// Progress of the latest reset request; SECONDS (may be NULL) receives its age.
BoatNetResetState boat_net_client_reset_state(
    const BoatNetClient *client, double now, double *seconds);
// Graceful leave.
void boat_net_client_leave(BoatNetClient *client);
bool boat_net_client_stale(const BoatNetClient *client, double now);
// The client's own slot, or NULL before the baseline or while it has no boat.
const BoatFleetSlot *boat_net_client_own(const BoatNetClient *client);
// Estimated current authoritative tick (fractional), or the mirror's before a clock estimate.
double boat_net_client_server_tick(const BoatNetClient *client, double now);
// The fleet to draw: the speculative one when predicting, else the mirror.
const BoatFleet *boat_net_client_view(const BoatNetClient *client);
// Text for a rejection or close, for the console.
const char *boat_net_client_failure(const BoatNetClient *client);

#endif
