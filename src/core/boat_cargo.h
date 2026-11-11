#ifndef PROTEUS_BOAT_CARGO_H
#define PROTEUS_BOAT_CARGO_H

#include "core/boat_pose.h"
#include "core/boat_scene_definition.h"

// S0 fixture: one upright 0.8 m crate, one kinematic service crane and one deck.
// All positions are renderer coordinates. No cargo/cargo or reaction-force coupling.
// The berth, crane and pad offsets below are the layout shared by the registered work-site
// presets; a package's scene.work site places and turns it (S1a/S1b). Offsets are in the site
// frame: renderer axes turned by the site heading, with the berth at the origin. The deck
// belongs to the workboat.
#define BOAT_CARGO_HALF .4f
#define BOAT_CARGO_DECK_Y .75f
#define BOAT_CARGO_DECK_X 1.3f
#define BOAT_CARGO_DECK_Z 1.9f
#define BOAT_CARGO_BERTH_LEFT -.6f
#define BOAT_CARGO_BERTH_RIGHT 2.4f
#define BOAT_CARGO_BERTH_BACK 5.0f
#define BOAT_CARGO_BERTH_FRONT -3.5f
#define BOAT_CARGO_MOOR_SPEED 1.5f
#define BOAT_CARGO_MOOR_ANGLE .785398163f // 45 degrees.

// A registered workboat preset: the assisted berth's hull, thrust, deck rectangle and capture
// envelope. Reset picks one per sandbox; the macros above are the flat_deck values.
typedef struct {
    const char *name;
    BoatHull hull;
    float thrust;
    float deck_x, deck_y, deck_z;
    float berth_left, berth_right, berth_front, berth_back;
} BoatCargoWorkboat;

typedef enum {
    BOAT_CARGO_PAD,
    BOAT_CARGO_HANGING,
    BOAT_CARGO_LOOSE,
    BOAT_CARGO_SECURED,
    BOAT_CARGO_FLOATING
} BoatCargoState;

typedef enum { BOAT_CARGO_SUPPORT_PAD, BOAT_CARGO_SUPPORT_DECK } BoatCargoSupportKind;
typedef struct {
    BoatCargoSupportKind kind;
    unsigned char pad; // Site index for PAD.
    Vector3 deck_local; // Lashed position for DECK.
} BoatCargoSupport;

typedef struct {
    BoatCargoState state;
    Vector3 position, velocity, deck_local;
    Vector3 rotation;
    float settled; // Continuous seconds of low relative speed on the deck.
    unsigned char pad; // The crane whose pad supports a BOAT_CARGO_PAD load.
    bool deck_contact;
    // S1c: committed at a receiving pad; recovery and re-delivery keep it. Since S1d a load
    // commits once at each receiver (RECEIVERS has a bit per site), so a load set down at the
    // wrong one can still reach the right one.
    bool delivered;
    unsigned char receivers;
    BoatCargoSupport support; // Last stable support: a pad, or the lashed deck.
    unsigned load; // S1d: which load this is, from 1; a supply starts the next.
    bool wet; // Resting in water since its last support; a LOST event marks the first touch.
    float rest; // Continuous seconds at rest on a receiving pad, toward delivery.
} BoatCargo;

// One placed work site and its crane.
typedef struct {
    Vector3 origin; // Berth in renderer coordinates; Y remains zero.
    float heading; // Renderer radians, as Boat.heading at the berth; zero is the S0 frame.
    float angle, reach, rope; // The slew angle is in the site frame.
    unsigned char preset; // BoatSceneWorkPreset
    bool hidden; // Not built yet: no platform, berth, pad or contact until enabled.
} BoatCargoCrane;

// S1c: native cargo events, in the order they happened. SITE indexes the cranes: the site
// worked at for SECURED and LOST, the receiver for DELIVERED and the source pad for RESET.
// LOAD is the crate's load number when the event happened (S1d).
typedef enum {
    BOAT_CARGO_EVENT_SECURED,
    BOAT_CARGO_EVENT_DELIVERED,
    BOAT_CARGO_EVENT_LOST,
    BOAT_CARGO_EVENT_RESET
} BoatCargoEventKind;
typedef struct {
    BoatCargoEventKind kind;
    unsigned site;
    unsigned load;
} BoatCargoEvent;
#define BOAT_CARGO_EVENTS 8 // One tick and one action add at most two; drain every tick.

typedef struct {
    BoatCargo cargo;
    BoatCargoCrane cranes[BOAT_SCENE_WORK_SITES];
    unsigned crane_count;
    unsigned active; // The berth moored at, or being moored at; its crane holds the hook.
    unsigned source; // The service platform the load starts on and recovers to.
    const BoatCargoWorkboat *boat; // Hull, deck and berth of the assisted workboat.
    bool moored;
    bool mooring; // Requested assisted alignment; G cancels it before reaching the berth.
    BoatCargoEvent events[BOAT_CARGO_EVENTS];
    unsigned event_count;
} BoatCargoSandbox;

#define BOAT_CARGO_SNAPSHOT_LOADS 1
typedef struct {
    unsigned load, source;
    bool delivered;
    unsigned char receivers;
    BoatCargoSupport support; // Where it rests on resume: PAD or a lashed DECK only.
} BoatCargoLoadSnapshot;
typedef struct {
    unsigned crane_count, active;
    bool built[BOAT_SCENE_WORK_SITES];
    BoatCargoLoadSnapshot loads[BOAT_CARGO_SNAPSHOT_LOADS];
    unsigned load_count;
    bool moored; // Resume moored at ACTIVE's berth.
    Vector2 position; // Simulation metres, when not moored.
    float heading;
} BoatCargoSnapshot;
// Record the built sites, one stable load and the boat pose for resuming this session. A load
// that is hooked, loose on the deck or floating records where it last rested.
void boat_cargo_snapshot(const BoatCargoSandbox *sandbox, const Boat *boat, BoatCargoSnapshot *out);
// Rebuild the sandbox and boat from SNAPSHOT and the work sites it was taken with. Refuses,
// changing neither output, when the sites, load or pose cannot be restored.
bool boat_cargo_restore(BoatCargoSandbox *sandbox, unsigned workboat,
    const BoatSceneWorkSite *sites, unsigned count, const BoatCargoSnapshot *snapshot, Boat *boat);

typedef enum {
    BOAT_CARGO_MOOR_READY,
    BOAT_CARGO_MOOR_DISTANCE,
    BOAT_CARGO_MOOR_HEADING,
    BOAT_CARGO_MOOR_FAST,
    BOAT_CARGO_MOOR_UPRIGHT
} BoatCargoMoorReason;

typedef struct {
    Vector2 offset; // Site-frame X/Z relative to the marked berth; zero is the final pose.
    float distance, speed, heading_error;
    unsigned site; // The active berth when moored or mooring, otherwise the nearest one.
    BoatCargoMoorReason reason;
} BoatCargoMoorStatus;

typedef enum {
    BOAT_CARGO_SHADOW_WATER,
    BOAT_CARGO_SHADOW_PAD,
    BOAT_CARGO_SHADOW_DECK
} BoatCargoSurface;
typedef struct {
    Vector3 points[4]; // Receiver-clipped landing footprint, wound upward; count is 0 or 4.
    unsigned count;
    float clearance; // Crate bottom above the actual receiver, before the drawing offset.
} BoatCargoShadow;

// Reset keeps no outside state. The application separately resets its boat and clock.
void boat_cargo_reset(BoatCargoSandbox *sandbox);
void boat_cargo_reset_at(BoatCargoSandbox *sandbox, Vector2 mooring);
// Places every site and moors at the first visible service platform, its load on that pad.
// Hidden sites stay absent. False when COUNT is outside 1..BOAT_SCENE_WORK_SITES or no visible
// site is a service platform.
bool boat_cargo_reset_sites(
    BoatCargoSandbox *sandbox, const BoatSceneWorkSite *sites, unsigned count);
// As above with a BoatSceneWorkboat preset (out of range falls back to flat_deck).
bool boat_cargo_reset_sites_with(
    BoatCargoSandbox *sandbox, unsigned workboat, const BoatSceneWorkSite *sites, unsigned count);
// Registered workboat preset; out-of-range returns the flat_deck entry.
const BoatCargoWorkboat *boat_cargo_workboat(unsigned preset);
// Staging survey for one site: the platform, workboat approach and complete crane sweep clear
// the compiled water, SCENE's berths/modules (NULL: compiled) and every retained or authored
// travelling craft over its longest period. Conservative envelopes include the sampling gap.
// No depth, seabed support or human navigation approval is inferred.
bool boat_cargo_site_clear(const BoatMap *map, const BoatSceneDefinition *scene, BoatWater water,
    const BoatSceneWorkSite *site);
// Surveyed envelopes of two sites, which must not overlap.
bool boat_cargo_sites_apart(const BoatSceneWorkSite *a, const BoatSceneWorkSite *b);
// The simulation POINT lies within MARGIN metres of the site's surveyed envelope.
bool boat_cargo_site_near(const BoatSceneWorkSite *site, Vector2 point, float margin);
// Site frame <-> renderer coordinates; directions omit the origin.
Vector3 boat_cargo_site_direction(const BoatCargoCrane *crane, Vector3 local);
Vector3 boat_cargo_site_point(const BoatCargoCrane *crane, Vector3 local);
// The boat-local deck spot an aim assist (2 port, 3 starboard) lowers the crate onto.
Vector3 boat_cargo_deck_aim_point(int aim);
Vector3 boat_cargo_site_local(const BoatCargoCrane *crane, Vector3 point);
Vector2 boat_cargo_berth(const BoatCargoCrane *crane); // Simulation metres.
Vector3 boat_cargo_tip(const BoatCargoCrane *crane);
Vector3 boat_cargo_hook(const BoatCargoSandbox *sandbox); // The active crane's hook.
// Bounded motors at the same 120 Hz as the cargo/boat. Inputs are signed unit rates.
void boat_cargo_motor(BoatCargoCrane *crane, float slew, float reach, float hoist);
// Copy and clear the queued events; returns how many were copied (at most CAPACITY).
unsigned boat_cargo_take_events(
    BoatCargoSandbox *sandbox, BoatCargoEvent *events, unsigned capacity);
// Return a load that is neither hooked nor lashed to its source pad (the retry path) and queue
// RESET. A committed delivery stays committed.
bool boat_cargo_recover(BoatCargoSandbox *sandbox);
// Build hidden SITE. Refused, changing nothing, when the site is not hidden or the boat or the
// load is inside its surveyed envelope plus a hull margin; already visible sites are refused too.
bool boat_cargo_site_enable(BoatCargoSandbox *sandbox, unsigned site, const Boat *boat);
// Whether the boat or the load occupies SITE's envelope (the reason an enable is refused).
bool boat_cargo_site_occupied(const BoatCargoSandbox *sandbox, unsigned site, const Boat *boat);
// S1d: whether a delivered load, resting on a pad or floating (never hooked, aboard or lashed),
// can be used up for a supply.
bool boat_cargo_supply_ready(const BoatCargoSandbox *sandbox);
// S1d: use up the delivered load and place the next one, undelivered, on built service platform
// SITE's pad, which becomes its source. Refused, changing nothing, unless the load is ready and
// SITE is a visible service platform. Queues no event: the job asked for it.
bool boat_cargo_supply(BoatCargoSandbox *sandbox, unsigned site);
// Explicit generous attachment; release keeps the instantaneous load velocity.
bool boat_cargo_hook_toggle(BoatCargoSandbox *sandbox);
bool boat_cargo_lash_toggle(BoatCargoSandbox *sandbox, const Boat *boat);
// Reject unsafe departure. In a capture zone, make its site active and start bounded assisted
// alignment; G cancels.
bool boat_cargo_moor_toggle(BoatCargoSandbox *sandbox, const Boat *boat);
BoatCargoMoorStatus boat_cargo_moor_status(const BoatCargoSandbox *sandbox, const Boat *boat);
// The surveyed capture zone has clear water for this bounded, one-way assisted berth motion.
void boat_cargo_moor_step(BoatCargoSandbox *sandbox, Boat *boat);
// Native toy's hold-to-stop aid; leaves the ordinary game handling unchanged.
void boat_cargo_brake(Boat *boat);
BoatCargoShadow boat_cargo_shadow(const BoatCargoSandbox *sandbox, const Boat *boat,
    const BoatWorld *world, double now, BoatCargoSurface surface);
void boat_cargo_step(
    BoatCargoSandbox *sandbox, const Boat *boat, const BoatWorld *world, double now);
// Experimental bounded secured-load acceleration/braking and trim. Empty is an exact no-op.
// Apply once after boat_step_handling; no global BoatHandling/recording changes.
void boat_cargo_load_effect(const BoatCargoSandbox *sandbox, const Boat *before, Boat *boat);

#endif
