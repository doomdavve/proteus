#ifndef PROTEUS_BOAT_PLUGIN_STAGE_H
#define PROTEUS_BOAT_PLUGIN_STAGE_H
// Disposable descriptor staging: native process or browser Web Worker. Private, versioned
// wire data carries no native pointers or struct layouts. Restricted Lua is not an OS sandbox.
#include "boat_plugin_host.h"

typedef struct BoatPluginStageJob BoatPluginStageJob;
// Native EXECUTABLE implements --plugin-stage-worker before opening window/audio. Input acquisition
// is the caller's responsibility. Start copies FILES and pins the host's commit epoch.
// Browser launch may bind location/map in an empty host and consume an already prepared
// matching receipt immediately; imported source is evaluated only in the Web Worker.
BoatPluginStageJob *boat_plugin_stage_start(
    const char *executable, const BoatPluginHost *host, const BoatPluginFiles *files, double now);
// Poll never evaluates Lua. 0 running, 1 staged, -1 rejected/deadline/stale/worker failure.
// Caller supplies finite monotonic NOW. Acceptance deadline is five seconds.
int boat_plugin_stage_poll(BoatPluginStageJob *job, BoatPluginHost *host, double now);
// Kill/reap or terminate the worker on cancel; release all handles. Safe after a completed poll.
void boat_plugin_stage_close(BoatPluginStageJob *job);
size_t boat_plugin_stage_bytes(const BoatPluginStageJob *job);
int boat_plugin_stage_worker(void);

// PC console queries: the same isolated worker evaluates SOURCE and renders EXPRESSION against
// the table it returns. No revision is created and no Lua runs on the game thread.
BoatPluginStageJob *boat_plugin_query_start(const char *executable, const char *manifest,
    size_t manifest_size, const char *source, size_t source_size, const char *expression,
    double now);
// 0 running, 1 answered, -1 rejected/deadline/worker failure. TEXT is NUL-terminated on 1.
int boat_plugin_query_poll(BoatPluginStageJob *job, char *text, size_t capacity,
    BoatPluginDiagnostic *diagnostic, double now);
int boat_plugin_query_worker(void);
#endif
