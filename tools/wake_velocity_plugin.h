#ifndef WAKE_VELOCITY_PLUGIN_H
#define WAKE_VELOCITY_PLUGIN_H
// Lua plugin P1: the solo session's opt-in voyage plugin (--plugin), its Reload/Undo keys, save
// polling and authoring panel. Definitions change only between ticks, through wake_plugin_frame.
#include "boat_plugin_host.h"
#include "boat_plugin_stage.h"
#include "boat_session.h"
#include <stdio.h>

#define WAKE_PLUGIN_TRACE 16

typedef struct {
    int64_t tick;
    unsigned revision, events;
    BoatPluginTransition transition;
    BoatPluginStatus status;
    uint64_t before, after;
    BoatPluginCommand commands[BOAT_PLUGIN_COMMANDS];
    unsigned command_count;
} WakePluginTrace;

typedef struct {
    const char *stage_executable; // Opt-in trusted native asynchronous reload; borrowed path.
    BoatPluginStageJob *staging; // Never copied into a preview checkpoint.
    const char *directory;
    BoatPluginHost host;
    BoatPluginFiles files; // The latest read; staging copies what it keeps.
    BoatPluginWatch watch;
    bool watching; // Save polling; the browser has no files to watch.
    char status[96], detail[192];
    bool alert;
    const char *outcome; // Machine word for the latest status: "staging", "held", "applied", ...
    unsigned requests; // Staging requests begun, by saves, F5 or the tool bridge.
    BoatPluginObjective objective, staged_objective;
    BoatPluginEvent events[BOAT_PLUGIN_EVENTS];
    unsigned event_count, generation;
    int64_t sequence;
    bool objective_pending;
    BoatPluginDiagnostic objective_diagnostic;
    WakePluginTrace trace[WAKE_PLUGIN_TRACE]; // Oldest first; bounded inspection history.
    unsigned trace_count;
} WakePlugin;

// Persistent native owner (all revision slots, file buffers, state/pending output, events,
// diagnostics and traces) plus one 2 MiB Lua heap. C stack and native world/view are separate.
_Static_assert(sizeof(WakePlugin) <= (1u << 20), "plugin native owner exceeds 1 MiB");

// The location DIRECTORY's manifest names, or NULL with the reason printed.
const BoatWorldDefinition *wake_plugin_location(const char *directory);
// Read DIRECTORY, check it against SESSION's location and begin its voyage instead of the
// compiled one. False with the diagnostic printed.
bool wake_plugin_open(WakePlugin *plugin, const char *directory, BoatSession *session, double now);
// Cancel/reap any pending staging job at session shutdown.
void wake_plugin_close(WakePlugin *plugin);
// Browser authoring controls pause solo input while the opt-in panel is open.
bool wake_plugin_editing(void);
// At a tick boundary: stage the package on RELOAD or a settled save, hold the previous revision
// on UNDO (or discard a held one), and commit a held migration/restart on APPLY. A rename applies
// at once. True when the caller must restart the session for a new exposure; SESSION's voyage route
// is already the new revision's.
bool wake_plugin_frame(
    WakePlugin *plugin, BoatSession *session, bool reload, bool undo, bool apply, double now);
// At a tick boundary: stage FILES (copied) like F5 would stage the package. The tool bridge's
// byte-pinned request; same return as wake_plugin_frame.
bool wake_plugin_stage_files(
    WakePlugin *plugin, BoatSession *session, const BoatPluginFiles *files, double now);
// Tick adapter: publish T's output before T+1; capture all output immediately after a step
// or recovery. Reset only alongside a new native voyage/session, never beside a moving world.
void wake_plugin_before_tick(WakePlugin *plugin);
void wake_plugin_after_tick(WakePlugin *plugin, const BoatSession *session);
void wake_plugin_reset_objective(WakePlugin *plugin, const BoatSession *session);
// Isolated native solo review only. No recordings, borrowed combat/field/radial sources,
// filesystem polling or live application effects. Resources/directory/handling remain borrowed
// immutable inputs; source revisions, native progress, water history, wake pools, presentation,
// queue, pending output, diagnostics and trace are owned by this complete checkpoint.
typedef struct {
    WakePlugin plugin;
    BoatSession session;
    BoatWakeObjects objects, traffic;
    bool has_objects, has_traffic, valid;
} WakePluginPreview;

typedef struct {
    BoatInput input;
    bool recover; // Recovery at the pre-step boundary, before pending publication.
} WakePluginReviewTick;

bool wake_plugin_preview_save(
    WakePluginPreview *checkpoint, const WakePlugin *plugin, const BoatSession *session);
bool wake_plugin_preview_restore(const WakePluginPreview *checkpoint, WakePlugin *plugin,
    BoatSession *session, BoatWakeObjects *objects, BoatWakeObjects *traffic);
// Restore and deterministically replay COUNT inputs after the checkpoint. Never polls files.
bool wake_plugin_preview_seek(const WakePluginPreview *checkpoint, WakePlugin *plugin,
    BoatSession *session, BoatWakeObjects *objects, BoatWakeObjects *traffic,
    const WakePluginReviewTick *ticks, unsigned count);
// Developer inspection: canonical state bytes (hex), queued events, accepted commands and
// transition hashes. No persistent UI in ordinary play.
void wake_plugin_inspect(const WakePlugin *plugin, FILE *output);
// The authoring panel, in screen pixels, with its last line ending above BOTTOM.
void wake_plugin_draw(const WakePlugin *plugin, int x, int bottom);
#endif
