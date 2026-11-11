#ifndef WAKE_VELOCITY_PLUGIN_H
#define WAKE_VELOCITY_PLUGIN_H
// Lua plugin P1: the solo session's opt-in voyage plugin (--plugin), its Reload/Undo keys, save
// polling and authoring panel. Definitions change only between ticks, through wake_plugin_frame.
#include "plugins/boat_plugin_catalog.h"
#include "plugins/boat_plugin_host.h"
#include "plugins/boat_plugin_stage.h"
#include "session/boat_session.h"
#include "wake_velocity/wake_velocity_objective.h"
#include <stdio.h>

// L1b: the game's local authority runs the objective and installs every commit. The
// game sets it (wake_plugin_attach) while a local session runs this plugin and fills VIEW and the
// inspection after each frame; NULL on the direct path (the review tools, and replay viewing).
// L4b: the browser's runs the objective on its isolated worker; the host never evaluates there.
typedef struct WakePluginAuthority {
    void *context;
    // After the host committed REVISION with CHANGE into SESSION (MOVED: the boat went to a tour's
    // start), or, for a migration, before it does: ask the authority to install it before the next
    // tick. False on an endpoint failure.
    bool (*commit)(void *context, BoatSession *session, const BoatPluginRevision *revision,
        BoatPluginChange change, bool moved);
    // Ask for a fresh inspection; it arrives within the next frame. False on an endpoint failure.
    bool (*inspect)(void *context);
    // L4b: the host discarded REVISION, whose migration is still in flight on the authority's
    // isolated worker: ask it to close that migration before the next tick. False on an endpoint
    // failure.
    bool (*cancel)(void *context, BoatSession *session, unsigned revision);
    WakeObjectiveView view; // The authority's objective after the latest frame.
    const char *inspection; // The newest inspection text (borrowed), its size and count.
    size_t inspection_size;
    uint64_t inspections;
    // A migration the authority is running: the held candidate's number and VIEW.commits when the
    // request was sent. Staging, Undo and Apply wait until its result arrives (L1-3).
    unsigned migrating, migrate_seen;
    bool failed; // A request could not be sent; the game stops the session.
    // L4b (browser): the inspection count when the checkpoint last asked for one (0: never).
    uint64_t checkpoint_asked;
    bool checkpoint_pending;
} WakePluginAuthority;

// The browser page's authoring actions (wake_web_plugin_actions), for WakePlugin.actions.
enum {
    WAKE_PLUGIN_ACTION_STAGE = 1,
    WAKE_PLUGIN_ACTION_UNDO = 2,
    WAKE_PLUGIN_ACTION_APPLY = 4,
    WAKE_PLUGIN_ACTION_CANCEL = 8, // Cancel staging, or a migration in flight.
};

typedef struct {
    const char *stage_executable; // Opt-in trusted native asynchronous reload; borrowed path.
    struct BoatPluginStageJob *query; // PC console query in the isolated worker.
    BoatPluginStageJob *staging; // Never copied into a preview checkpoint.
    const char *directory;
    bool authoring; // --plugin DIR: file watch, staging worker, bridge, hotkeys and panel.
    BoatPluginHost host;
    BoatPluginFiles files; // The latest read; staging copies what it keeps.
    BoatPluginWatch watch;
    bool watching; // Save polling; the browser has no files to watch.
    char status[96], detail[192];
    bool alert;
    const char *outcome; // Machine word for the latest status: "staging", "held", "applied", ...
    unsigned requests; // Staging requests begun, by saves, F5 or the tool bridge.
    WakeObjective run; // The objective transaction on the direct path.
    WakePluginAuthority *authority; // L1b: set while a local authority runs the objective.
    // L4b: the page's actions since the last frame (WAKE_PLUGIN_ACTION_*), which the game sets
    // before wake_plugin_frame and the frame consumes.
    unsigned actions;
} WakePlugin;

// Persistent native owner (all revision slots, file buffers, state/pending output, events,
// diagnostics and traces) plus one 2 MiB Lua heap. C stack and native world/view are separate.
_Static_assert(sizeof(WakePlugin) <= (1u << 20), "plugin native owner exceeds 1 MiB");

// The location DIRECTORY's manifest names, or NULL with the reason printed.
const BoatWorldDefinition *wake_plugin_location(const char *directory);
// Read DIRECTORY, check it against SESSION's location and begin its voyage instead of the
// compiled one. False with the diagnostic printed.
bool wake_plugin_open(WakePlugin *plugin, const char *directory, BoatSession *session, double now);
// PD1: open a build-time catalog entry as revision 1 without a Lua VM, file watch, staging
// worker or authoring panel. False with DIAGNOSTIC filled.
bool wake_plugin_open_catalog(
    WakePlugin *plugin, const BoatPluginCatalogEntry *entry, BoatSession *session);
// Cancel/reap pending jobs, restore the session's compiled scene and retire the browser
// evaluator at session shutdown.
void wake_plugin_close(WakePlugin *plugin, BoatSession *session);
// Browser authoring controls pause solo input while the opt-in panel is open.
bool wake_plugin_editing(void);
// At a tick boundary: stage the package on RELOAD or a settled save, hold the previous revision
// on UNDO (or discard a held one), and commit a held migration/restart on APPLY. The page's
// ACTIONS join them; Undo or the page's cancel during a migration still in flight on the
// authority cancels it. With an authority whose evaluation is pending, every control answers
// busy. A rename applies
// at once. True when the caller must restart the session for a new exposure; SESSION's voyage route
// is already the new revision's.
bool wake_plugin_frame(
    WakePlugin *plugin, BoatSession *session, bool reload, bool undo, bool apply, double now);
// At a tick boundary: stage FILES (copied) like F5 would stage the package. The tool bridge's
// byte-pinned request; same return as wake_plugin_frame.
bool wake_plugin_stage_files(
    WakePlugin *plugin, BoatSession *session, const BoatPluginFiles *files, double now);
// L1b: hand the objective and commits to AUTHORITY (NULL returns them to the direct path, whose
// objective then starts empty at the current generation). The host's own checkpoint is cleared:
// no Lua state stays here while an authority runs it.
void wake_plugin_attach(WakePlugin *plugin, WakePluginAuthority *authority);
// L1b: the host's revision NUMBER (active, in the Undo history or held), or NULL. The authority
// resolves commits through it; a revision never moves while it is borrowed.
const BoatPluginRevision *wake_plugin_revision(const WakePlugin *plugin, unsigned number);
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
// L4b: the objective checkpoint the browser's authoring checks read (Module.boatPluginCheckpoint).
typedef struct {
    int64_t tick; // The objective's checkpoint tick.
    uint64_t native_tick; // The session's.
    unsigned generation, events; // Events: the queue's length.
    bool initialized, quarantined, waiting;
    unsigned state_size;
    unsigned char state[BOAT_PLUGIN_STATE_BYTES];
} WakePluginCheckpoint;

// The checkpoint: the direct path's objective, or with an authority its latest inspection (tick,
// generation, suspension, queue and state, which can lag the view by a frame) and its view
// (initialized, waiting). False before an authority's first inspection, and while that
// inspection is of an older generation than the view.
bool wake_plugin_checkpoint(
    const WakePlugin *plugin, const BoatSession *session, WakePluginCheckpoint *out);
// L4b: in the browser while authoring, publish the checkpoint and, with an authority, ask for the
// next inspection (one in flight). Natively nothing.
void wake_plugin_publish(WakePlugin *plugin, const BoatSession *session);
// The authoring panel, in screen pixels, with its last line ending above BOTTOM.
void wake_plugin_draw(const WakePlugin *plugin, int x, int bottom);

// PC live console -----------------------------------------------------------------------------
// Move a running session from runtime to authoring. The catalog revision's embedded source and
// manifest become the console's base; no save polling starts. False without an active revision.
bool wake_plugin_promote(WakePlugin *plugin);
// Stage SOURCE (copied) as the console's candidate revision. True when a worker started.
bool wake_plugin_stage_source(
    WakePlugin *plugin, BoatSession *session, const char *source, size_t size, double now);
// The session's manifest text and the active revision's source, borrowed.
size_t wake_plugin_manifest(const WakePlugin *plugin, const char **text);
size_t wake_plugin_source(const WakePlugin *plugin, const char **text);
// Add a canonical optional voyage grant to this session's manifest copy. False with MESSAGE.
bool wake_plugin_grant(WakePlugin *plugin, const char *capability, char *message, size_t capacity);
// Start a read-only expression query in the isolated worker. One at a time.
bool wake_plugin_query_begin(
    WakePlugin *plugin, const char *source, size_t size, const char *expression, double now);
// 0 pending, 1 answered (TEXT), -1 failed (MESSAGE when supplied).
int wake_plugin_query_result(WakePlugin *plugin, char *text, size_t capacity, double now);
bool wake_plugin_query_pending(const WakePlugin *plugin);
// Write the active package as plugin.json and main.lua. Never writes into a directory that holds
// another package. False with MESSAGE.
bool wake_plugin_save(
    const WakePlugin *plugin, const char *directory, char *message, size_t capacity);
// A held LIVE candidate may be installed when the hull clears its candidate envelopes.
bool wake_plugin_live_ready(const WakePlugin *plugin, const BoatSession *session);
#endif
