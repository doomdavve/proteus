#ifndef PROTEUS_BOAT_NET_PROTOCOL_H
#define PROTEUS_BOAT_NET_PROTOCOL_H

#include "boat_fleet.h"
#include "boat_match.h"
#include "boat_race.h"
#include "boat_room_definition.h"

#include <stddef.h>
#include <stdint.h>

// Multiplayer protocol v10 (dimensioned marina scenes). docs/MULTIPLAYER_PROTOCOL.md
// owns the byte tables.
// Every multi-byte field is big-endian; floats travel as IEEE-754 binary32 bit patterns. Encoders
// never copy structs, padding, bool or enum representations; decoders validate every field
// and never write the destination on failure.
#define BOAT_NET_MAGIC 0x50424e54u // "PBNT"
#define BOAT_NET_VERSION 10
#define BOAT_NET_HEADER_BYTES 32
#define BOAT_NET_MAX_MESSAGE 1100 // Whole message; the transport MTU allows for its headers.
#define BOAT_NET_MAX_PAYLOAD (BOAT_NET_MAX_MESSAGE - BOAT_NET_HEADER_BYTES)
#define BOAT_NET_PORT 24680
#define BOAT_NET_MAX_PEERS 8 // Four players plus four handshakes.
#define BOAT_NET_SNAPSHOT_TICKS 4 // 30 Hz snapshots.
#define BOAT_NET_BIRTH_TICKS 12 // 10 Hz recent-birth batches.
#define BOAT_NET_BIRTH_WINDOW_TICKS 180 // Each batch repeats 1.5 s of births.
#define BOAT_NET_MAX_BIRTHS 16
#define BOAT_NET_INPUT_TICKS 2 // 60 Hz command packets.
#define BOAT_NET_MAX_COMMANDS 12 // Recent ticks repeated in each command packet.
#define BOAT_NET_MAX_FUTURE_TICKS 24 // The authority's furthest accepted command.
#define BOAT_NET_MESSAGES_PER_SERVICE 128 // Messages handled per service call at most.
#define BOAT_NET_TIMEOUT 5.0 // Seconds without any message from the other end.
#define BOAT_NET_HANDSHAKE_TIMEOUT 10.0 // Seconds from connection to an installed world.
#define BOAT_NET_PAGE_BYTES 1024 // Checkpoint or definition bytes per bootstrap page.
#define BOAT_NET_MAX_CHECKPOINT 16384
#define BOAT_NET_MAX_DEFINITION BOAT_ROOM_BYTES_MAX
#define BOAT_NET_NAME_BYTES 16
#define BOAT_NET_TOKEN_BYTES 32
#define BOAT_NET_BUILD_BYTES 32
// Covers both compiled map crops and the basin; gameplay uses the selected map's shoreline.
#define BOAT_NET_WORLD_LIMIT 8192.0f

// Delivery classes. ENet maps each to one channel; the in-memory adapter models the same rules.
typedef enum {
    BOAT_NET_CONTROL, // Reliable, ordered: handshake, roster, spawn, resync, leave, match, race.
    BOAT_NET_STATE, // Unreliable, sequenced: commands up, snapshots down, ping/pong.
    BOAT_NET_BIRTHS, // Unreliable, sequenced: recent wake births.
    BOAT_NET_REGISTRY, // Unreliable, unsequenced: combat snapshot pages.
    BOAT_NET_BOOTSTRAP, // Reliable, ordered: world checkpoints.
    BOAT_NET_CHANNELS
} BoatNetChannel;

typedef enum {
    BOAT_NET_HELLO = 1,
    BOAT_NET_ACCEPT = 2,
    BOAT_NET_REJECT = 3,
    BOAT_NET_ROSTER = 4,
    BOAT_NET_WORLD_BEGIN = 5,
    BOAT_NET_WORLD_PAGE = 6,
    BOAT_NET_WORLD_END = 7,
    BOAT_NET_READY = 8,
    BOAT_NET_INPUT_BATCH = 9,
    BOAT_NET_SNAPSHOT = 10,
    BOAT_NET_WAKE_BIRTHS = 11,
    BOAT_NET_SPAWN_REQUEST = 12,
    BOAT_NET_PING = 13,
    BOAT_NET_PONG = 14,
    BOAT_NET_RESYNC_REQUEST = 15,
    BOAT_NET_DISCONNECT = 16,
    BOAT_NET_MATCH_STATE = 17,
    BOAT_NET_MATCH_EVENT = 18,
    BOAT_NET_RACE_STATE = 19,
    BOAT_NET_RACE_EVENT = 20,
    BOAT_NET_COMBAT_PAGE = 21, // Unsequenced bounded pages tied to snapshot tick.
    BOAT_NET_DEFINITION_BEGIN = 22, // S→C: the room definition, before the world checkpoint.
    BOAT_NET_DEFINITION_PAGE = 23, // S→C: one bounded piece of the definition bytes.
    BOAT_NET_DEFINITION_END = 24, // S→C: the definition is complete on this reliable channel.
    BOAT_NET_TYPE_LIMIT
} BoatNetType;

typedef enum {
    BOAT_NET_OK,
    BOAT_NET_TRUNCATED, // Shorter than the header or its declared payload.
    BOAT_NET_BAD_MAGIC,
    BOAT_NET_BAD_VERSION, // The header is otherwise readable: answer a Hello with a Reject.
    BOAT_NET_BAD_HEADER, // Flags, reserved bytes or a payload beyond the limit.
    BOAT_NET_BAD_TYPE,
    BOAT_NET_BAD_LENGTH, // Payload size does not match the type and its counts.
    BOAT_NET_BAD_FIELD, // Invalid enum, count, index, string or reserved payload byte.
    BOAT_NET_NONFINITE,
    BOAT_NET_OUT_OF_RANGE, // Finite but outside the documented safety envelope.
} BoatNetError;

typedef enum {
    BOAT_NET_REJECT_VERSION = 1,
    BOAT_NET_REJECT_PHYSICS = 2,
    BOAT_NET_REJECT_CONTENT = 3,
    BOAT_NET_REJECT_TOKEN = 4,
    BOAT_NET_REJECT_FULL = 5,
    BOAT_NET_REJECT_MALFORMED = 6,
    BOAT_NET_REJECT_LIMIT
} BoatNetRejectReason;

typedef enum {
    BOAT_NET_LEAVE_QUIT = 1,
    BOAT_NET_LEAVE_TIMEOUT = 2,
    BOAT_NET_LEAVE_SHUTDOWN = 3,
    BOAT_NET_LEAVE_MISBEHAVING = 4,
    BOAT_NET_LEAVE_LIMIT
} BoatNetLeaveReason;

// What the authority integrated for a slot on its latest tick.
typedef enum {
    BOAT_NET_INPUT_NONE, // Inactive slot.
    BOAT_NET_INPUT_RECEIVED, // The command for exactly this tick.
    BOAT_NET_INPUT_REPEATED, // Missing; repeating the last received command (<= 6 ticks).
    BOAT_NET_INPUT_ZERO, // Missing for longer: zero rates (throttle holds).
    BOAT_NET_INPUT_NEUTRAL, // Inactive client or 250 ms without input: easing to neutral.
    BOAT_NET_INPUT_STATES
} BoatNetInputState;

#define BOAT_NET_COMMAND_GUN 2u
#define BOAT_NET_COMMAND_MASK 3u
#define BOAT_NET_COMMAND_INACTIVE 1u // Help, focus loss: the authority neutralizes controls.

typedef struct {
    uint8_t type, flags; // Flags are zero in v1.
    uint32_t epoch; // Room session epoch; zero in a Hello.
    uint32_t sequence; // Per sender and channel; wraps.
    uint64_t tick; // Authoritative tick the message refers to (see the protocol table).
    uint16_t payload; // Decoded payload length.
} BoatNetHeader;

typedef struct {
    int8_t steering, throttle; // Rates times 127: keyboard -1, 0 and 1 stay exact.
    uint8_t flags;
    uint16_t grenade;
    uint8_t torpedo;
} BoatNetCommand;

typedef struct {
    uint64_t tick; // Last tick whose command was received and used, zero for none.
    uint8_t state; // BoatNetInputState.
    BoatNetCommand used; // Rates integrated on the latest tick.
} BoatNetSlotInput;

typedef struct {
    uint32_t physics_revision;
    uint64_t content_digest;
    char name[BOAT_NET_NAME_BYTES + 1], token[BOAT_NET_TOKEN_BYTES + 1];
    char build[BOAT_NET_BUILD_BYTES + 1];
} BoatNetHello;

typedef enum {
    BOAT_NET_MODE_FREE,
    BOAT_NET_MODE_DEATHMATCH,
    BOAT_NET_MODE_RACE,
    BOAT_NET_MODE_LIMIT
} BoatNetRoomMode;

typedef struct {
    uint8_t slot;
    bool waves;
    uint8_t mode; // BoatNetRoomMode.
    uint8_t location; // BoatLocationId, selected by the authority.
    uint8_t snapshot_ticks, birth_ticks, input_ticks, max_commands, max_future;
    uint16_t tick_hz;
    uint32_t wake_life_ticks;
    float crest_height;
} BoatNetAccept;

typedef struct {
    uint8_t reason; // BoatNetRejectReason.
    uint16_t version;
    uint32_t physics_revision;
    uint64_t content_digest;
} BoatNetReject;

typedef enum {
    BOAT_NET_ROSTER_EMPTY,
    BOAT_NET_ROSTER_JOINING,
    BOAT_NET_ROSTER_PLAYING
} BoatNetRosterState;

typedef struct {
    struct {
        uint8_t state; // BoatNetRosterState.
        char name[BOAT_NET_NAME_BYTES + 1];
    } slots[BOAT_FLEET_SLOTS];
} BoatNetRoster;

typedef struct {
    uint32_t checkpoint, bytes;
    uint16_t pages;
    uint64_t digest; // FNV-1a 64 of the checkpoint bytes.
    uint64_t fleet_hash; // boat_fleet_hash() of the encoded state.
} BoatNetWorldBegin;

typedef struct {
    uint32_t definition, bytes;
    uint16_t pages;
    uint64_t digest; // FNV-1a 64 of the definition bytes; corruption detection only.
} BoatNetDefinitionBegin;

typedef struct {
    uint32_t checkpoint;
    uint16_t index, count, size;
    uint8_t data[BOAT_NET_PAGE_BYTES];
} BoatNetWorldPage;

typedef struct {
    uint32_t life; // The slot life these commands steer; others are ignored.
    uint8_t count; // Commands for ticks header.tick - count + 1 .. header.tick.
    uint64_t snapshot_tick; // Latest snapshot the client has applied.
    BoatNetCommand commands[BOAT_NET_MAX_COMMANDS];
} BoatNetInputBatch;

// Post-tick authoritative fleet state without wake records, which travel as births and
// checkpoints; wake_digest names the exact live record set at this tick.
typedef struct {
    uint32_t next_request;
    uint16_t live, peak_live;
    uint64_t emitted, expired, wake_digest;
    // GO tick of the race room's latest round, zero for none: the course source's clock and the
    // tick its grid holds release. Always zero outside race rooms.
    uint64_t race_go;
    BoatFleetSlot slots[BOAT_FLEET_SLOTS];
    BoatWakeObjectEmitter emitters[BOAT_FLEET_SLOTS];
    BoatNetSlotInput inputs[BOAT_FLEET_SLOTS];
} BoatNetSnapshot;

// Deathmatch scoreboard and round clock (docs/DEATHMATCH_PLAN.md); deathmatch rooms only.
typedef struct {
    uint8_t phase; // BoatMatchPhase.
    uint16_t respawn_ticks,
        skip_ticks; // Automatic respawn; a reset request skips the rest after skip_ticks.
    uint32_t round; // Rounds started.
    uint32_t round_ticks;
    uint64_t phase_end; // Tick the phase ends; zero in warmup.
    struct {
        bool present;
        BoatMatchRow row; // All zero for an absent slot.
    } slots[BOAT_FLEET_SLOTS];
} BoatNetMatchState;

// A capsize or round boundary; the header tick is the event's tick.
typedef struct {
    uint8_t kind; // BoatMatchKind.
    uint8_t victim, attacker; // BOAT_MATCH_NOBODY for none (rounds; attacker of a self-capsize).
    uint8_t cause; // BoatMatchCause.
    bool bailout;
    uint32_t victim_life, round;
    uint8_t reason; // BoatMatchDefeat.
} BoatNetMatchEvent;

typedef struct {
    bool present; // A player occupies the slot.
    bool entrant; // The row belongs to the latest round.
    bool current; // The slot's present player is that entrant.
    bool parked; // Finished and held past the finish.
    uint8_t status, place, gates; // BoatRaceStatus; place 0 unplaced; gates cleared.
    uint32_t recoveries;
    uint64_t finish; // Subticks after GO, once finished.
} BoatNetRaceRow;

// Race phase, clock and every slot's row (docs/RACE_PLAN.md); race rooms only.
typedef struct {
    uint8_t phase; // BoatRacePhase.
    uint8_t placed; // Places awarded this round.
    uint16_t recovery_ticks, skip_ticks, park_ticks;
    uint32_t round; // Rounds gridded.
    uint64_t phase_end; // Grid, countdown and results; zero otherwise.
    uint64_t go_tick; // Of the latest round; zero before the first.
    uint64_t deadline; // While racing; zero otherwise.
    BoatNetRaceRow slots[BOAT_FLEET_SLOTS];
} BoatNetRaceState;

// A race event; the header tick is the event's tick.
typedef struct {
    uint8_t kind; // BoatRaceKind.
    uint8_t slot; // BOAT_RACE_NOBODY for phase and abandon events.
    uint8_t value; // The phase, the gate or the place.
    uint32_t round;
    uint64_t time; // Finish subticks after GO; zero for other kinds.
} BoatNetRaceEvent;

typedef struct {
    uint8_t slot; // Pool slot; slot / BOAT_OBJECT_PER_BOAT is the emitter.
    BoatWakeObject record;
} BoatNetBirth;

typedef struct {
    uint8_t count;
    BoatNetBirth births[BOAT_NET_MAX_BIRTHS];
} BoatNetWakeBirths;

typedef struct {
    BoatNetHeader header;
    union {
        BoatNetHello hello;
        BoatNetAccept accept;
        BoatNetReject reject;
        BoatNetRoster roster;
        BoatNetWorldBegin world_begin;
        BoatNetDefinitionBegin definition_begin;
        BoatNetWorldPage world_page; // Also DEFINITION_PAGE: the same bounded page layout.
        uint32_t checkpoint; // WORLD_END, READY, DEFINITION_END.
        BoatNetInputBatch input;
        BoatNetSnapshot snapshot;
        BoatNetWakeBirths births;
        struct {
            uint32_t request, life; // Reset the slot while it is on LIFE (or not yet placed).
        } spawn;
        uint64_t clock; // PING sends the client clock in microseconds; PONG echoes it.
        struct {
            uint8_t reason; // 1: wake digest mismatch, 2: other.
            uint64_t snapshot_tick;
        } resync;
        uint8_t leave; // BoatNetLeaveReason.
        BoatNetMatchState match;
        BoatNetMatchEvent match_event;
        BoatNetRaceState race;
        BoatNetRaceEvent race_event;
    };
} BoatNetMessage;

// The wire match state of MATCH, and the event for EVENT.
void boat_net_match_state_of(const BoatMatch *match, BoatNetMatchState *state);
BoatNetMatchEvent boat_net_match_event_of(const BoatMatch *match, const BoatMatchEvent *event);
bool boat_net_match_state_equal(const BoatNetMatchState *a, const BoatNetMatchState *b);
// The wire race state of RACE, and the event for EVENT.
void boat_net_race_state_of(const BoatRace *race, BoatNetRaceState *state);
BoatNetRaceEvent boat_net_race_event_of(const BoatRace *race, const BoatRaceEvent *event);
bool boat_net_race_state_equal(const BoatNetRaceState *a, const BoatNetRaceState *b);
// The GO tick a snapshot or checkpoint carries for RACE: zero before its first round.
uint64_t boat_net_race_go(const BoatRace *race);
// Bind a race room's package course source to FLEET's water for GO (zero removes it), as the
// authority's race does: the client's mirror and prediction call this with each snapshot's race_go.
void boat_net_race_bind(BoatFleet *fleet, const BoatRaceCourse *course, uint64_t go);

// Encode MESSAGE (its header payload length is computed) into OUT. Returns the byte count, or
// zero for a message that does not satisfy the decoder's own validation.
size_t boat_net_encode(const BoatNetMessage *message, uint8_t out[BOAT_NET_MAX_MESSAGE]);
// Header only: enough to answer a Hello from another protocol version.
BoatNetError boat_net_decode_header(const uint8_t *data, size_t size, BoatNetHeader *header);
BoatNetError boat_net_decode(const uint8_t *data, size_t size, BoatNetMessage *message);
const char *boat_net_error_name(BoatNetError error);
const char *boat_net_reject_text(uint8_t reason);
// The channel a message type travels on.
BoatNetChannel boat_net_channel(uint8_t type);
// True when A follows B in modular u32 order (half-range comparison).
bool boat_net_sequence_newer(uint32_t a, uint32_t b);

// Rates to the wire and back. Non-finite rates encode as zero.
BoatNetCommand boat_net_command(BoatInput input, uint8_t flags);
BoatInput boat_net_command_input(BoatNetCommand command);
// The command an inactive or silent slot integrates: controls easing toward neutral.
BoatNetCommand boat_net_neutral_command(const Boat *boat, uint8_t flags);

// Compatibility digest of all compiled locations/shore geometry, the cannon constants, the
// fleet/wake contract and the weather recipes. The room supplies the course, rules and contacts.
uint64_t boat_net_content_digest(void);
// Canonical digest of the live record set (slot index and every field of live records).
uint64_t boat_net_wake_digest(const BoatWakeObjects *wakes);
uint64_t boat_net_bytes_digest(const void *data, size_t size);

// Snapshot of STATE after its latest tick, with each slot's input record.
void boat_net_snapshot_of(const BoatFleetState *state,
    const BoatNetSlotInput inputs[BOAT_FLEET_SLOTS], BoatNetSnapshot *snapshot);
// Complete checkpoint bytes: everything in a snapshot (RACE_GO included) plus the wake pool's
// records and settings. Returns the byte count (well below BOAT_NET_MAX_CHECKPOINT).
size_t boat_net_checkpoint_encode(const BoatFleetState *state,
    const BoatNetSlotInput inputs[BOAT_FLEET_SLOTS], uint64_t race_go, uint8_t *out,
    size_t capacity);
BoatNetError boat_net_checkpoint_decode(const uint8_t *data, size_t size, BoatFleetState *state,
    BoatNetSlotInput inputs[BOAT_FLEET_SLOTS], uint64_t *race_go);

// LAN discovery (docs/MULTIPLAYER_PROTOCOL.md#lan-discovery): plain UDP datagrams outside ENet
// on their own port. A client broadcasts a query; every room that hears it answers the sender
// directly. Query and answer have the same size, so an answer never amplifies its query.
#define BOAT_NET_DISCOVERY_PORT 24681
#define BOAT_NET_DISCOVERY_BYTES 52
#define BOAT_NET_DISCOVERY_FORMAT 1
#define BOAT_NET_LAN_ROOMS 8 // Rooms a search list keeps.

typedef struct {
    uint16_t version; // The room's protocol version, which may differ from this build's.
    uint32_t physics_revision;
    uint64_t content_digest;
    uint32_t epoch; // Tells one room's answers (over loopback and the LAN) from another's.
    uint32_t address; // IPv4 a.b.c.d as a << 24 | ...; zero means the answer's sender.
    uint16_t port; // The room's game port.
    uint8_t mode; // BoatNetRoomMode for this version; not validated across versions.
    uint8_t players, max_players;
    bool token; // Joining needs a room token.
    char name[BOAT_NET_NAME_BYTES + 1]; // The host machine's name; may be empty.
} BoatNetLanRoom;

typedef struct {
    unsigned count;
    BoatNetLanRoom rooms[BOAT_NET_LAN_ROOMS]; // In the order first heard.
    double seen[BOAT_NET_LAN_ROOMS];
} BoatNetLanList;

size_t boat_net_lan_query_encode(uint8_t out[BOAT_NET_DISCOVERY_BYTES]);
bool boat_net_lan_query_decode(const uint8_t *data, size_t size);
// Zero for a room the decoder would refuse.
size_t boat_net_lan_room_encode(const BoatNetLanRoom *room, uint8_t out[BOAT_NET_DISCOVERY_BYTES]);
bool boat_net_lan_room_decode(const uint8_t *data, size_t size, BoatNetLanRoom *room);
// Whether this build's handshake would pass the room's version, handling and content checks.
bool boat_net_lan_room_compatible(const BoatNetLanRoom *room);
// "a.b.c.d:port" of a room whose address is known.
void boat_net_lan_endpoint(const BoatNetLanRoom *room, char *out, size_t size);
// Add or refresh ROOM, heard at NOW. A room already listed (same epoch, port and name) keeps its
// position and first address. A full list ignores new rooms.
void boat_net_lan_list_note(BoatNetLanList *list, const BoatNetLanRoom *room, double now);
// Forget rooms not heard from for more than AGE seconds; the rest keep their order.
void boat_net_lan_list_expire(BoatNetLanList *list, double now, double age);

#endif
