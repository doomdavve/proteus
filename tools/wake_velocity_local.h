#ifndef WAKE_VELOCITY_LOCAL_H
#define WAKE_VELOCITY_LOCAL_H

// AR6 (ledger L0): the game's glue for private free boating as a local authority and client
// (src/boat_local.h). The game keeps its BoatSession for views, audio and HUD; this glue fills
// its boat, clock, presentation and arcade wake pool from the client's prediction, never from
// the authority. Native builds only.
#include "boat_local.h"
#include "boat_profile.h"
#include "wake_velocity_drive.h"

typedef struct {
    BoatLocal local;
    bool active;
} WakeLocal;

// Start a session from SESSION's location, water, scene, handling and boat and OBJECTS' pool
// recipe. WORLDS is the game's location table. False with LOCAL's failure text.
bool wake_local_open(WakeLocal *local, const BoatSession *session, const ObjectStudy *objects,
    const BoatWorldResources *worlds, double now);
// After the game restarted SESSION and OBJECTS: a new generation from their reset state.
bool wake_local_restart(
    WakeLocal *local, const BoatSession *session, const ObjectStudy *objects, double now);
// Grant the ticks ACCUMULATOR holds (the solo tick loop's arithmetic) and run them; then copy the
// predicted pool into OBJECTS. Presentation steps per predicted tick. False on an endpoint
// failure (D10).
bool wake_local_frame(WakeLocal *local, BoatSession *session, ObjectStudy *objects,
    double *accumulator, BoatInput input, float thrust_target, float steering_target, double now,
    BoatFrameMetrics *metrics);
void wake_local_close(WakeLocal *local);

#endif
