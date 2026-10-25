#ifndef WAKE_VELOCITY_STATE_H
#define WAKE_VELOCITY_STATE_H
#include "wake_velocity_drive.h"
/* FNV-1a over named %a numerical values, excluding pointers and cosmetic state.
 * Fold once per nominal frame; same-build evidence, not a replay/network format. */
#define WAKE_VELOCITY_HASH_SEED UINT64_C(14695981039346656037)
// TIME is the session's world time, hashed in the boat's former cached-time position.
uint64_t wake_velocity_state_hash(
    uint64_t hash, const Boat *boat, double time, const BoatWakeObjects *wakes);
#endif
