#include "boat_scenery.h"
#include "wake_velocity_scenery.h"
#include <emscripten/emscripten.h>

EM_JS(void, scenery_start, (const char *name, const char *path),
    { Module['boatScenery'].start(UTF8ToString(name), UTF8ToString(path)); })
EM_JS(int, scenery_state, (const char *name),
    { return Module['boatScenery'].state(UTF8ToString(name)); })
EM_JS(float, scenery_progress, (const char *name),
    { return Module['boatScenery'].progress(UTF8ToString(name)); })

bool wake_scenery_startup(const char *assets)
{
    (void)assets;
    return true;
}

bool wake_scenery_request(const BoatWorldDefinition *location, const char *assets)
{
    char path[4096];
    if (!boat_scenery_path(location, assets, path, sizeof(path)))
        return false;
    scenery_start(location->name, path);
    return true;
}

WakeSceneryStatus wake_scenery_poll(const BoatWorldDefinition *location, float *progress)
{
    int state = scenery_state(location->name);
    if (state == 1) {
        *progress = scenery_progress(location->name);
        return WAKE_SCENERY_PENDING;
    }
    return state == 2 ? WAKE_SCENERY_READY : WAKE_SCENERY_FAILED;
}
