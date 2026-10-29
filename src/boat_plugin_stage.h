#ifndef PROTEUS_BOAT_PLUGIN_STAGE_H
#define PROTEUS_BOAT_PLUGIN_STAGE_H
// Native disposable descriptor staging. Private, versioned wire data; no native pointers or
// struct layouts cross a process boundary. Restricted Lua is not an OS security sandbox.
#include "boat_plugin_host.h"

typedef struct BoatPluginStageJob BoatPluginStageJob;
// EXECUTABLE implements --plugin-stage-worker before opening a window/audio. Input acquisition
// is the caller's responsibility. Start copies FILES and pins the host's commit epoch.
BoatPluginStageJob *boat_plugin_stage_start(
    const char *executable, const BoatPluginHost *host, const BoatPluginFiles *files, double now);
// Poll never evaluates Lua. 0 running, 1 staged, -1 rejected/deadline/stale/worker failure.
// Caller supplies finite monotonic NOW. Acceptance deadline is five seconds.
int boat_plugin_stage_poll(BoatPluginStageJob *job, BoatPluginHost *host, double now);
// Kill/reap on cancel; release all handles. Safe after a completed poll.
void boat_plugin_stage_close(BoatPluginStageJob *job);
size_t boat_plugin_stage_bytes(const BoatPluginStageJob *job);
int boat_plugin_stage_worker(void);
#endif
