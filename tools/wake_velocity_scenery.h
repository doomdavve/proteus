// AR3 scenery acquisition: make a mapped location's scenery pack available to
// boat_scenery_load_location. CMake selects one implementation: wake_velocity_scenery.c (native
// builds read every pack from disk at startup) or wake_velocity_scenery_web.c (the browser fetches
// a pack when a session first needs it, through the page's Module.boatScenery). No call waits:
// the caller draws its progress and yields a frame between polls. A fetched pack stays with the
// page; the loaded scenery lives until boat_scenery_unload.
#ifndef WAKE_VELOCITY_SCENERY_H
#define WAKE_VELOCITY_SCENERY_H

#include "boat_location.h"
#include <stdbool.h>

typedef enum { WAKE_SCENERY_PENDING, WAKE_SCENERY_READY, WAKE_SCENERY_FAILED } WakeSceneryStatus;

// Once at startup, before any session: native loads every mapped pack (false when one fails);
// the browser loads nothing and returns true.
bool wake_scenery_startup(const char *assets);
// Whether acquisition waits for a window: the browser fetches after a session's window opens, so
// the session's first CPU draw (and its capture) waits for the pack. Native: false.
bool wake_scenery_deferred(void);
// Begin acquiring LOCATION's pack (mapped, not yet loaded). False when it cannot be requested.
bool wake_scenery_request(const BoatWorldDefinition *location, const char *assets);
// The request's status; PROGRESS (0 to 1) is written while it is pending.
WakeSceneryStatus wake_scenery_poll(const BoatWorldDefinition *location, float *progress);

#endif
