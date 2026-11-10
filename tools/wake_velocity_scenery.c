#include "wake_velocity_scenery.h"
#include "boat_scenery.h"

bool wake_scenery_startup(const char *assets) { return boat_scenery_load_all(assets); }

bool wake_scenery_request(const BoatWorldDefinition *location, const char *assets)
{
    (void)location;
    (void)assets;
    return true;
}

WakeSceneryStatus wake_scenery_poll(const BoatWorldDefinition *location, float *progress)
{
    (void)location;
    *progress = 1;
    return WAKE_SCENERY_READY;
}
