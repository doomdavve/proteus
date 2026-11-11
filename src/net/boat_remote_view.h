#ifndef PROTEUS_BOAT_REMOTE_VIEW_H
#define PROTEUS_BOAT_REMOTE_VIEW_H

#include "boat_fleet.h"

// Multiplayer M4 remote view: other boats are drawn from authoritative snapshots about 100 ms
// behind the estimated server time, and blended toward their predicted collision proxy when
// they come close to the own boat. These poses are display only: they never collide, emit
// wakes or feed the prediction. No device, clock or transport access.
#define BOAT_REMOTE_HISTORY 16 // Snapshots kept (about 0.5 s at 30 Hz).
#define BOAT_REMOTE_DELAY_TICKS 12 // Interpolation delay behind the estimated server tick.
#define BOAT_REMOTE_EXTRAPOLATE_TICKS 12 // Past the newest snapshot, then hold.
#define BOAT_REMOTE_TELEPORT 10.0f // A larger jump between snapshots is not blended.
#define BOAT_REMOTE_NEAR 12.0f // Proximity mode within this separation...
#define BOAT_REMOTE_CONTACT_SECONDS 0.5f // ...or when closest approach is this soon
#define BOAT_REMOTE_CONTACT_DISTANCE 8.0f // and this close.
#define BOAT_REMOTE_FAR 16.0f // Leave proximity mode beyond this separation
#define BOAT_REMOTE_FAR_SECONDS 0.5 // held this long.
#define BOAT_REMOTE_BLEND_SECONDS 0.1f // Time to move fully into or out of proximity mode.

typedef struct {
    uint64_t tick;
    BoatFleetSlot slots[BOAT_FLEET_SLOTS];
} BoatRemoteSnapshot;

// Snapshots in increasing tick order, oldest overwritten first.
typedef struct {
    BoatRemoteSnapshot entries[BOAT_REMOTE_HISTORY];
    unsigned count, next;
} BoatRemoteHistory;

typedef enum {
    BOAT_REMOTE_NONE, // No snapshot holds this slot's boat.
    BOAT_REMOTE_INTERPOLATED,
    BOAT_REMOTE_EXTRAPOLATED, // Ahead of the newest snapshot, within the extrapolation limit.
    BOAT_REMOTE_HELD, // Beyond the history at either end: an unmoving pose.
} BoatRemoteSample;

typedef struct {
    float weight; // 0: interpolated pose, 1: predicted proxy.
    bool near;
    double far_since; // Negative while not counting down to leave.
    uint32_t life;
} BoatRemoteProximity;

void boat_remote_history_reset(BoatRemoteHistory *history);
// Append a snapshot; one not newer than the newest is ignored.
void boat_remote_history_push(
    BoatRemoteHistory *history, uint64_t tick, const BoatFleetSlot slots[BOAT_FLEET_SLOTS]);
// SLOT's pose at fractional authoritative TICK. Snapshots of different lives, or a jump over
// BOAT_REMOTE_TELEPORT, are not blended: the newer one is shown. LIFE receives the life shown.
BoatRemoteSample boat_remote_history_pose(
    const BoatRemoteHistory *history, unsigned slot, double tick, Boat *pose, uint32_t *life);
// Continuous pose values from A toward B by T; discrete state from the nearer of the two. Heading takes the short
// way round; pitch and roll stay continuous through flips.
Boat boat_remote_blend(const Boat *a, const Boat *b, float t);
// Update the proximity weight for a remote of life LIFE from the physical predicted poses of
// the own boat and that remote, and return it. A new life starts outside proximity mode.
float boat_remote_proximity_update(BoatRemoteProximity *proximity, const Boat *own,
    const Boat *remote, uint32_t life, double now, float dt);

#endif
