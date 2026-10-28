#ifndef WAKE_VELOCITY_PLUGIN_H
#define WAKE_VELOCITY_PLUGIN_H
// Lua plugin P1: the solo session's opt-in voyage plugin (--plugin), its Reload/Undo keys, save
// polling and authoring panel. Definitions change only between ticks, through wake_plugin_frame.
#include "boat_plugin_host.h"
#include "boat_session.h"

typedef struct {
    const char *directory;
    BoatPluginHost host;
    BoatPluginFiles files; // The latest read; staging copies what it keeps.
    BoatPluginWatch watch;
    bool watching; // Save polling; the browser has no files to watch.
    char status[96], detail[192];
    bool alert;
} WakePlugin;

// The location DIRECTORY's manifest names, or NULL with the reason printed.
const BoatWorldDefinition *wake_plugin_location(const char *directory);
// Read DIRECTORY, check it against SESSION's location and begin its voyage instead of the
// compiled one. False with the diagnostic printed.
bool wake_plugin_open(WakePlugin *plugin, const char *directory, BoatSession *session, double now);
// At a tick boundary: stage the package on RELOAD or a settled save, hold the previous revision
// on UNDO (or discard a held one), and commit a held restart on APPLY. A rename applies at once.
// True when the caller must restart the session for a new exposure; SESSION's voyage route is
// already the new revision's.
bool wake_plugin_frame(
    WakePlugin *plugin, BoatSession *session, bool reload, bool undo, bool apply, double now);
// The authoring panel, in screen pixels, with its last line ending above BOTTOM.
void wake_plugin_draw(const WakePlugin *plugin, int x, int bottom);
#endif
