#ifndef WAKE_VELOCITY_PLUGIN_WEB_H
#define WAKE_VELOCITY_PLUGIN_WEB_H
// AR3 browser backend for the game's plugin glue: the page's authoring panel
// (Module.boatPluginOwner/Notice/Editing/Actions/ResetPending/MenuOpen), package export
// (Module.boatPluginExport) and the isolated objective evaluator (Module.boatPluginObjective,
// web/plugin_objective.js). Only the browser build compiles wake_velocity_plugin_web.c; the
// request/acceptance policy, deadlines and the T/T+1 gate stay in wake_velocity_plugin.c.
#include <stdbool.h>

// Publish the active revision's owner identity and status line.
void wake_web_plugin_owner(unsigned revision, unsigned low, unsigned high, const char *status);
// Whether the page's editor has focus.
bool wake_web_plugin_editing(void);
// Take the page's pending STAGE/UNDO/APPLY/CANCEL action flags (1 reload, 2 undo, 4 apply, 8
// cancel).
unsigned wake_web_plugin_actions(void);
// Start an evaluation of the encoded request; 0 when the evaluator is unavailable.
unsigned wake_web_objective_start(const unsigned char *bytes, unsigned size);
// 0 pending, 1 replied, negative failed.
int wake_web_objective_status(unsigned job);
// Copy the reply into BYTES (at most 49152 bytes); its size, or -1.
int wake_web_objective_read(unsigned job, unsigned char *bytes);
// Release one job; late replies are inert.
void wake_web_objective_close(unsigned job);
// Reset the evaluator at session end.
void wake_web_objective_shutdown(void);
// Publish the committed objective checkpoint for the page.
void wake_web_objective_snapshot(unsigned low, unsigned high, unsigned native_low,
    unsigned native_high, unsigned generation, bool initialized, bool quarantined, unsigned events,
    bool waiting, const unsigned char *bytes, unsigned size);
// Hand the manifest and source to the page's download; 0 when unavailable.
int wake_web_plugin_export(const unsigned char *manifest, unsigned manifest_size,
    const unsigned char *source, unsigned source_size);
// Tell the page whether a reset is pending and the menu is open.
void wake_web_plugin_controls(bool pending, bool menu_open);

#endif
